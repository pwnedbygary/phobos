//Sound output (sceAudio): eight channels a game reserves and hands buffers of 16-bit samples to, which the PSP's
//audio driver mixes and plays at 44.1 kHz, and one more channel, the SRC channel (sceAudioOutput2 and sceAudioSRC),
//that plays at a rate of its own.
//
//Only the timing is here so far: the samples aren't played, but each buffer takes as long as it would to play, and
//a game waiting to hand over the next one waits that long, which is how most games pace their sound (and some their
//whole game). The driver's behavior, as PPSSPP's notes describe it from tests on the PSP:
//  - A channel holds one buffer at a time, with nothing queued behind it. The driver reads it straight from the
//    game's memory as it plays, 64 samples at a time for every channel at once (a block, 64/44100 of a second),
//    while any channel plays. The first block is taken as the first buffer arrives, before the call that handed it
//    over returns.
//  - Handing over a buffer while the channel's last one still plays fails (BUSY) with sceAudioOutput; with
//    sceAudioOutputBlocking the thread waits for it to finish, then hands it over. One thread may wait per channel; a
//    second is told BUSY. Either returns the channel's sample count once the buffer is taken.
//  - The SRC channel holds two buffers, played one after the other; handing over a third fails. Its blocking output
//    returns once a buffer has finished playing since the last one did (the first after an idle spell at once).
//What isn't played yet: the samples themselves (the speakers get silence), volumes, mono and stereo alike.

namespace {
  constexpr u32 AudioSampleRate = 44'100;
  constexpr u32 AudioBlock = 64;                 //samples a channel gives the mixer at a time
  constexpr u32 AudioStereo = 0x00, AudioMono = 0x10;  //pspaudio.h's PSP_AUDIO_FORMAT_*
  constexpr u32 AudioSource = 8;                 //the SRC channel's number among the waits
  //A block lasts 64 / 44100 s, 483265.3 of the CPU's cycles; 49 blocks are a whole number of them (23680000).
  constexpr u32 AudioBlocksWhole = 49;
}

//When block number `block` (counting from the mixer's start) is mixed.
auto Kernel::audioBlockAt(u64 block) const -> u64 {
  return audio.mixStart + block * AudioBlock * CPUFrequency / AudioSampleRate;
}

//Hands a channel a buffer (address, volumes; a negative volume leaves the channel's as it was): its sample count, or
//why not. With nothing playing, the mixer starts and takes the buffer's first block at once. A buffer at address 0 is
//taken but never plays (it counts as a buffer's worth of samples left, as on the PSP).
auto Kernel::audioOutput(u32 number, u32 address, s32 left, s32 right) -> u32 {
  auto& channel = audio.channels[number];
  if(!channel.reserved) return ErrorAudioChannelNotInitialized;
  if(channel.address) return ErrorAudioChannelBusy;
  channel.remaining = channel.sampleCount;
  if(left >= 0) channel.leftVolume = left;
  if(right >= 0) channel.rightVolume = right;
  channel.address = address;
  if(address && !audio.mixing) {
    audio.mixing = true;
    audio.mixStart = cycles;
    audio.blocks = 0;
    mixAudio();
  }
  return channel.sampleCount;
}

//A block: 64 samples from every channel playing. A channel whose buffer runs out is free again: a thread waiting to
//hand it the next one does now, and stops waiting. When none plays any more, the mixer stops.
auto Kernel::mixAudio() -> void {
  audio.blocks++;
  bool playing = false;
  for(u32 number = 0; number < 8; number++) {
    auto& channel = audio.channels[number];
    if(!channel.address) continue;
    u32 count = std::min(channel.remaining, AudioBlock);
    channel.address += count * (channel.format == AudioMono ? 2 : 4);
    channel.remaining -= count;
    if(channel.remaining) { playing = true; continue; }
    channel.address = 0;
    auto waiter = threads.find(channel.waiting);
    channel.waiting = 0;
    if(waiter == threads.end() || waiter->second->status != Status::Waiting) continue;
    auto& thread = *waiter->second;
    if(thread.wait != Wait::Audio || thread.waitID != number) continue;
    ready(thread, audioOutput(number, channel.waitingAddress, channel.waitingLeft, channel.waitingRight));
    playing |= channel.address != 0;
  }
  if(!playing) audio.mixing = false;
  if(audio.blocks == AudioBlocksWhole) {  //start counting again from a whole number of cycles, as they'd overflow
    audio.mixStart = audioBlockAt(AudioBlocksWhole);
    audio.blocks = 0;
  }
}

//The SRC channel's first buffer is done: the next plays on, and the oldest thread waiting is told, or else the next
//to output finds it done already.
auto Kernel::sourceFinished() -> void {
  auto& source = audio.source;
  source.queued--;
  source.lengths[0] = source.lengths[1];
  if(source.queued) source.finishAt += u64(source.lengths[0]) * CPUFrequency / source.frequency;
  Thread* oldest = nullptr;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != Wait::Audio || thread->waitID != AudioSource) continue;
    if(!oldest || thread->readySince < oldest->readySince) oldest = thread.get();
  }
  if(oldest) ready(*oldest, oldest->waitCount);
  else source.completion = true;
}

//What's due: the mixer's blocks, the SRC channel's buffers ending. True if a thread woke.
auto Kernel::audioEvents() -> bool {
  bool woke = false;
  while(audio.mixing && cycles >= audioBlockAt(audio.blocks)) {
    auto before = readySequence;
    mixAudio();
    woke |= readySequence != before;
  }
  while(audio.source.queued && cycles >= audio.source.finishAt) {
    auto before = readySequence;
    sourceFinished();
    woke |= readySequence != before;
  }
  return woke;
}

//The next audio event's cycle, or none (~0).
auto Kernel::nextAudioEvent() const -> u64 {
  u64 next = ~0ull;
  if(audio.mixing) next = audioBlockAt(audio.blocks);
  if(audio.source.queued) next = std::min(next, audio.source.finishAt);
  return next;
}

//(channel 0-7, or -1 for the highest free one; samples per buffer, 64 to 65472 in steps of 64; format: 0x00 stereo,
//0x10 mono): the channel reserved.
auto Kernel::sceAudioChReserve() -> void {
  s32 number = s32(arg(0));
  u32 samples = arg(1), format = arg(2);
  if(number < 0) {
    number = 7;
    while(number >= 0 && (audio.channels[number].sampleCount || audio.channels[number].address)) number--;
    if(number < 0) return result(ErrorAudioNoChannels);
  }
  if(number >= 8 || audio.channels[number].reserved) return result(ErrorAudioInvalidChannel);
  if(samples & 63 || !samples || samples > 65536 - 64) return result(ErrorAudioSampleCount);
  if(format != AudioStereo && format != AudioMono) return result(ErrorAudioInvalidFormat);
  auto& channel = audio.channels[number];
  channel.reserved = true;
  channel.sampleCount = samples;
  channel.format = format;
  channel.leftVolume = channel.rightVolume = 0;
  result(number);
}

//(channel): free again (a buffer still playing plays on). Not while a thread waits on it.
auto Kernel::sceAudioChRelease() -> void {
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[arg(0)];
  if(!channel.reserved) return result(ErrorAudioChannelNotReserved);
  if(channel.waiting) return result(ErrorAudioChannelBusy);
  channel.reserved = false;
  channel.sampleCount = 0;
  result(0);
}

//Hands over a buffer, waiting for the channel's last one to finish first.
auto Kernel::audioOutputBlocking(u32 number, u32 address, s32 left, s32 right) -> void {
  u32 taken = audioOutput(number, address, left, right);
  if(taken != ErrorAudioChannelBusy) return result(taken);
  auto& channel = audio.channels[number];
  if(channel.waiting || !current) return result(ErrorAudioChannelBusy);
  if(!mayWait()) return;
  channel.waiting = current->uid;
  channel.waitingAddress = address;
  channel.waitingLeft = left;
  channel.waitingRight = right;
  result(channel.sampleCount);
  block(Wait::Audio, number, 0);
}

//(channel, volume 0-0xffff for both sides, buffer)
auto Kernel::sceAudioOutputBlocking() -> void {
  if(s32(arg(1)) > 0xffff) return result(ErrorAudioInvalidVolume);
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  audioOutputBlocking(arg(0), arg(2), s32(arg(1)), s32(arg(1)));
}

//(channel, left volume, right volume, buffer): here the volumes are checked as unsigned (PPSSPP's notes).
auto Kernel::sceAudioOutputPannedBlocking() -> void {
  if((arg(1) | arg(2)) > 0xffff) return result(ErrorAudioInvalidVolume);
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  audioOutputBlocking(arg(0), arg(3), s32(arg(1)), s32(arg(2)));
}

//(channel, volume, buffer): without waiting.
auto Kernel::sceAudioOutput() -> void {
  if(s32(arg(1)) > 0xffff) return result(ErrorAudioInvalidVolume);
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  result(audioOutput(arg(0), arg(2), s32(arg(1)), s32(arg(1))));
}

auto Kernel::sceAudioOutputPanned() -> void {
  if(s32(arg(1)) > 0xffff || s32(arg(2)) > 0xffff) return result(ErrorAudioInvalidVolume);
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  result(audioOutput(arg(0), arg(3), s32(arg(1)), s32(arg(2))));
}

//(channel): the samples still to play: the buffer's left, and a waiting thread's whole buffer. The two functions
//differ only for a buffer at address 0, which the second doesn't count.
auto Kernel::sceAudioGetChannelRestLen() -> void {
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[arg(0)];
  result(channel.remaining + (channel.waiting ? channel.sampleCount : 0));
}

auto Kernel::sceAudioGetChannelRestLength() -> void {
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[arg(0)];
  result((channel.address ? channel.remaining : 0) + (channel.waiting ? channel.sampleCount : 0));
}

//(channel, samples per buffer from now on)
auto Kernel::sceAudioSetChannelDataLen() -> void {
  u32 samples = arg(1);
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  if(samples & 63 || !samples || samples > 65536 - 64) return result(ErrorAudioSampleCount);
  auto& channel = audio.channels[arg(0)];
  if(channel.waiting) return result(ErrorAudioChannelBusy);
  if(!channel.reserved) return result(ErrorAudioChannelNotInitialized);
  channel.sampleCount = samples;
  result(0);
}

//(channel, format): not while a buffer plays or a thread waits.
auto Kernel::sceAudioChangeChannelConfig() -> void {
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[arg(0)];
  if(channel.waiting || channel.address) return result(ErrorAudioChannelBusy);
  if(!channel.reserved) return result(ErrorAudioChannelNotReserved);
  if(arg(1) != AudioStereo && arg(1) != AudioMono) return result(ErrorAudioInvalidFormat);
  channel.format = arg(1);
  result(0);
}

//(channel, left volume, right volume): a negative one is left as it was.
auto Kernel::sceAudioChangeChannelVolume() -> void {
  s32 left = s32(arg(1)), right = s32(arg(2));
  if(left > 0xffff || right > 0xffff) return result(ErrorAudioInvalidVolume);
  if(arg(0) >= 8) return result(ErrorAudioInvalidChannel);
  if(left >= 0) audio.channels[arg(0)].leftVolume = left;
  if(right >= 0) audio.channels[arg(0)].rightVolume = right;
  result(0);
}

//The SRC channel. Reserving it: (samples per buffer, 17 to 4111; the top bit ignored), at 44.1 kHz in stereo; or
//(samples, rate: 0 for 44.1 kHz or one of the nine it takes, format: 2, stereo, alone).
auto Kernel::sourceReserve(u32 samples, u32 frequency) -> void {
  auto& source = audio.source;
  samples &= 0x7fff'ffff;
  if(samples < 17 || samples > 4111) return result(ErrorInvalidSize);
  if(source.reserved) return result(ErrorAudioChannelAlreadyReserved);
  source = {};
  source.reserved = true;
  source.sampleCount = samples;
  source.frequency = frequency ? frequency : AudioSampleRate;
  result(0);
}

auto Kernel::sceAudioOutput2Reserve() -> void {
  sourceReserve(arg(0), 0);
}

auto Kernel::sceAudioSRCChReserve() -> void {
  u32 frequency = arg(1), format = arg(2);
  static constexpr u32 Rates[] = {44'100, 22'050, 11'025, 48'000, 32'000, 24'000, 16'000, 12'000, 8'000};
  if(format == 4) return result(ErrorNotImplemented);
  if(format != 2) return result(ErrorInvalidSize);
  u32 samples = arg(0) & 0x7fff'ffff;
  if(samples < 17 || samples > 4111) return result(ErrorInvalidSize);
  if(frequency && std::find(std::begin(Rates), std::end(Rates), frequency) == std::end(Rates)) {
    return result(ErrorAudioInvalidFrequency);
  }
  sourceReserve(samples, frequency);
}

//Releasing it: not while it plays. It leaves a buffer's end behind it, which the next output after reserving it again
//finds (PPSSPP's notes).
auto Kernel::sourceRelease() -> void {
  auto& source = audio.source;
  if(!source.reserved) return result(ErrorAudioChannelNotReserved);
  if(source.queued) return result(ErrorAudioChannelAlreadyReserved);
  source = {};
  source.completion = true;
  result(0);
}

auto Kernel::sceAudioOutput2Release() -> void { sourceRelease(); }
auto Kernel::sceAudioSRCChRelease() -> void { sourceRelease(); }

//(volume up to 0xfffff, buffer): queues the buffer if there's room (two at most), then waits for a buffer to finish
//(at once if one has since the last wait, or if this one started the channel). A buffer at address 0 only waits.
auto Kernel::sourceOutput(u32 volume, u32 address) -> void {
  auto& source = audio.source;
  if(volume > 0xf'ffff) return result(ErrorAudioInvalidVolume);
  if(!source.reserved) return result(ErrorAudioChannelNotReserved);
  if(source.queued == 2) return result(ErrorAudioChannelBusy);
  u32 taken = 0;
  if(address) {
    source.volume = volume;
    if(!source.queued) {
      source.completion = true;  //starting to play counts as a buffer done, so the first output doesn't wait
      source.finishAt = cycles + u64(source.sampleCount) * CPUFrequency / source.frequency;
    }
    source.lengths[source.queued++] = source.sampleCount;
    taken = source.sampleCount;
  } else if(!source.queued) {
    return result(0);
  }
  if(interrupting) return result(ErrorIllegalContext);
  if(source.completion || !current) {
    source.completion = false;
    return result(taken);
  }
  result(taken);
  current->waitCount = taken;  //what it returns once woken
  current->readySince = ++readySequence;
  block(Wait::Audio, AudioSource, 0);
}

auto Kernel::sceAudioOutput2OutputBlocking() -> void { sourceOutput(arg(0), arg(1)); }
auto Kernel::sceAudioSRCOutputBlocking() -> void { sourceOutput(arg(0), arg(1)); }

//(samples per buffer from now on): the buffers queued keep their lengths.
auto Kernel::sceAudioOutput2ChangeLength() -> void {
  if(arg(0) - 17 >= 0xfff) return result(ErrorAudioSampleCount);
  if(!audio.source.reserved) return result(ErrorAudioChannelNotReserved);
  audio.source.sampleCount = arg(0);
  result(0);
}

//The samples queued, counted in buffers of the present length.
auto Kernel::sceAudioOutput2GetRestSample() -> void {
  if(!audio.source.reserved) return result(ErrorAudioChannelNotReserved);
  result(audio.source.queued * audio.source.sampleCount);
}
