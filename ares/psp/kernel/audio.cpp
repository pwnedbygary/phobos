//Sound output: the audio driver (sceAudio) as a program sees it. What each call accepts, refuses and returns, and how
//long it takes, follow pspautotests' audio and intr tests (programs whose results were recorded on a PSP, firmware
//6.xx) and pspsdk's pspaudio.h, by way of a behavior specification written from them for this code.
//
//The mixer channels: eight, numbered 0-7. A program reserves one with a sample count (a multiple of 64, from 64 to
//65472) and a format (pairs of 16-bit samples, left and right, or single 16-bit samples, mono). A channel holds one
//buffer at a time, in its "slot": where the buffer is, and how many of its samples haven't been taken yet. Nothing
//is copied as a buffer is handed over: the mixer's DMA reads the program's memory as it plays, taking a block of 64
//samples from every channel with a buffer at each block boundary, every 64/44100 s (about 1451 microseconds), mono
//as fast as stereo. The first buffer handed over while the DMA is idle starts it, and its first block is taken there
//and then (the mixer outranks every thread); a buffer handed over while it runs waits for the next boundary. Once a
//boundary has taken the last samples there were, the DMA runs on for one more block, while that block is heard, and
//then stops: a buffer handed over before then is taken at the next boundary, one after it starts the DMA again.
//
//A blocking output into a busy slot waits until the slot's buffer has been taken, then hands its own over and
//returns. One thread may wait so on a channel; another is told BUSY at once. So in a steady stream one buffer plays
//while the next is held by its waiting thread, and each output returns one buffer's time after the one before:
//that's what paces games' sound threads. 64-sample buffers are paced the same way, a block each: of a run of them
//handed over one after another from idle, the first is taken as it's handed over, the second goes into the free slot,
//and each from the third on waits for a boundary, so K of them take K - 2 blocks' time.
//
//The SRC channel: one more output beside the mixer, reached by two families of calls (sceAudioOutput2*, and
//sceAudioSRC* with a rate of its own), with two slots ("armed" buffers, played one after the other) and a completion
//flag. An output arms its buffer and then waits for a completion: one pending lets it return at once (starting from
//idle makes one), else it returns when the buffer playing has finished. A buffer plays for its samples' time at the
//channel's rate; the next one armed carries straight on. Its slot frees as its transfer ends, about 100
//microseconds before its last samples are heard (pspautotests' audio/output2/rest: a 64-sample buffer, 1451
//microseconds long, reads as gone after "13XX"): so the first buffer after an idle stretch retires 100 microseconds
//short of its length, and each one chained after it a whole buffer later, which keeps a stream's pace.
//
//The samples aren't mixed yet: the system's sound stream plays silence (System::run()). What a mixer needs is here,
//at the moments the PSP's takes it: mixerBlock() is where each channel's block of 64 samples is taken (at buffer, so
//many samples in, in its format, at its volumes), and srcRetire() is where an SRC buffer has been played. Mixing them
//into a queue for the system's stream to play from comes next.
//
//Not here: sceAudioOneshotOutput, input and routing, sceAudioSetFrequency; and the time the calls themselves take on
//a PSP (an output starting the DMA spends 100 microseconds to a millisecond, an SRC output over 100): system
//functions take no time here. The PSP's driver leaves a channel marked as waited on for good when a wait for it is
//refused (an output into a busy channel from an interrupt handler); that isn't copied: the output is refused, and
//the channel left as it was.

static constexpr u32 AudioStereo = 0x00, AudioMono = 0x10;

//A mixer channel's sample count: a multiple of 64, from 64 to 65472.
static auto mixerSamplesValid(u32 samples) -> bool {
  return samples >= 64 && samples <= 65472 && samples % 64 == 0;
}

//The SRC channel's: 17 to 4111.
static auto srcSamplesValid(u32 samples) -> bool {
  return samples >= 17 && samples <= 4111;
}

//The SRC channel's rates; 0 means the output's own, 44.1 kHz.
static auto srcRateValid(u32 rate) -> bool {
  for(u32 valid : {0u, 8'000u, 11'025u, 12'000u, 16'000u, 22'050u, 24'000u, 32'000u, 44'100u, 48'000u}) {
    if(rate == valid) return true;
  }
  return false;
}

//The thread waiting on a channel (0-7), or on the SRC channel (Audio::WaitSrc, Audio::WaitSrcDrain), if one is. Its
//output's arguments wait with it: the buffer to hand over and its volumes (see Thread's waitPointer, waitCount and
//waitMode).
auto Kernel::audioWaiter(u32 waitID) -> Thread* {
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::Audio && thread->waitID == waitID) {
      return thread.get();
    }
  }
  return nullptr;
}

//Whether the calling thread may wait for a channel: 0 if it may, else the error its output returns instead. From an
//interrupt handler, ILLEGAL_CONTEXT; with interrupts held off (sceKernelCpuSuspendIntr), or dispatching
//(sceKernelSuspendDispatchThread), CAN_NOT_WAIT, as pspautotests' intr/waits found. Only a call that has to wait is
//refused so: one that can finish at once does so whatever the context. (With no thread running at all, which only a
//test calling directly can arrange, there's no one to wait either.)
auto Kernel::audioWaitRefused() const -> u32 {
  if(interrupting) return ErrorIllegalContext;
  if(!interruptsEnabled || dispatchSuspended || !current) return ErrorCanNotWait;
  return 0;
}

//A buffer goes into the free slot of channel number, with the volumes it came with (a negative one leaves that side
//as it was): they're the channel's from this buffer on. Its samples are all still to take. A null buffer puts nothing
//in the slot, so nothing plays and the slot stays free, but its count is set all the same (sceAudioGetChannelRestLen
//tells it). A real buffer starts the DMA if it's idle, and its first block is taken now, as block 0 of the run; while
//the DMA runs, the buffer waits for the next boundary like any other.
auto Kernel::handOver(u32 number, u32 buffer, s32 left, s32 right) -> void {
  auto& channel = audio.channels[number];
  if(left >= 0) channel.leftVolume = left;
  if(right >= 0) channel.rightVolume = right;
  channel.buffer = buffer;
  channel.length = channel.remaining = channel.sampleCount;
  if(!buffer || audio.dma.running) return;
  audio.dma.running = true;
  audio.dma.nextBlock = cycles;
  audio.dma.fraction = 0;
  mixerBlock();
}

//A block boundary, now: the DMA takes the next 64 samples from every channel with a buffer. That's where they'd be
//mixed for the speakers: 64 samples from buffer + (length - remaining) samples in (4 bytes each in stereo, 2 in
//mono), scaled by the channel's volumes (not yet: see the top). A buffer whose last samples go leaves its slot free,
//and a thread waiting on the channel hands its own buffer over then (to be taken from the next boundary on) and
//returns with the channel's sample count. A boundary with nothing to take stops the DMA: the block the one before
//took has been heard. Returns whether a thread woke.
auto Kernel::mixerBlock() -> bool {
  bool took = false, woke = false;
  for(u32 number = 0; number < 8; number++) {
    auto& channel = audio.channels[number];
    if(!channel.buffer) continue;
    took = true;
    channel.remaining = channel.remaining > 64 ? channel.remaining - 64 : 0;
    if(channel.remaining) continue;
    channel.buffer = 0;
    if(auto thread = audioWaiter(number)) {
      handOver(number, thread->waitPointer, s32(thread->waitCount), s32(thread->waitMode));
      ready(*thread, channel.sampleCount);
      woke = true;
    }
  }
  if(!took) {
    audio.dma.running = false;
    return woke;
  }
  auto& dma = audio.dma;
  dma.fraction += Audio::BlockFraction;
  dma.nextBlock += Audio::BlockCycles + dma.fraction / 49;
  dma.fraction %= 49;
  return woke;
}

//The four mixer outputs, once each call's own volume rule has passed (below): (channel, buffer, left and right
//volumes, negative for the channel's own). Then, in this order: the channel's number (INVALID_CHANNEL), its
//reservation (NOT_INIT, from any context), the slot. A free slot takes the buffer, and the call returns the
//channel's sample count at once, without waiting for it to play. A busy slot turns a non-blocking output away (BUSY).
//A blocking one waits until the slot's buffer has been taken, then hands its own over and returns; unless another
//thread waits on the channel already (BUSY at once: one waiter per channel), or this one can't wait
//(audioWaitRefused()). A null buffer goes the same way: on a busy channel, a blocking one is how a program waits for
//it to drain.
auto Kernel::mixerOutput(u32 number, u32 buffer, s32 left, s32 right, bool blocking) -> void {
  if(number >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[number];
  if(!channel.reserved) return result(ErrorAudioChannelNotInitialized);
  if(!channel.buffer) {  //(a thread only waits while the slot is busy)
    handOver(number, buffer, left, right);
    return result(channel.sampleCount);
  }
  if(!blocking || audioWaiter(number)) return result(ErrorAudioChannelBusy);
  if(auto refused = audioWaitRefused()) return result(refused);
  current->waitPointer = buffer;
  current->waitCount = u32(left);
  current->waitMode = u32(right);
  block(Wait::Audio, number, 0);
}

//How long the SRC channel takes to play `samples` at its rate, in cycles: rounded up, so never faster than a PSP.
auto Kernel::srcDuration(u32 samples) const -> u64 {
  return (u64(samples) * CPUFrequency + audio.src.rate - 1) / audio.src.rate;
}

//The SRC channel's first armed buffer has been transferred: its slot is free, and the second, if there is one, plays
//on from here, at once, retiring a whole buffer later (both 100 microseconds ahead of what's heard: srcOutput()).
//That's a completion: it wakes the thread waiting for one, or stays pending for the next output to take. Once
//nothing is armed, the threads waiting for the channel to drain return as well. Returns whether a thread woke.
auto Kernel::srcRetire() -> bool {
  auto& src = audio.src;
  src.buffers[0] = src.buffers[1];
  src.buffers[1] = {};
  src.armed--;
  if(src.armed) src.retireAt += srcDuration(src.buffers[0].sampleCount);
  bool woke = false;
  if(auto thread = audioWaiter(Audio::WaitSrc)) {
    ready(*thread, thread->waitCount);
    woke = true;
  } else {
    src.completion = true;
  }
  if(src.armed) return woke;
  while(auto thread = audioWaiter(Audio::WaitSrcDrain)) {
    ready(*thread, 0);
    woke = true;
  }
  return woke;
}

//What has come due by now, in the order it came: the mixer's block boundaries and the SRC channel's buffers being
//retired. Returns whether a thread woke.
auto Kernel::audioEvents() -> bool {
  bool woke = false;
  while(true) {
    u64 block = audio.dma.running ? audio.dma.nextBlock : ~0ull;
    u64 retire = audio.src.armed ? audio.src.retireAt : ~0ull;
    if(std::min(block, retire) > cycles) return woke;
    if(block <= retire ? mixerBlock() : srcRetire()) woke = true;
  }
}

//When the next of them is due (never: ~0).
auto Kernel::nextAudioEvent() const -> u64 {
  u64 next = audio.dma.running ? audio.dma.nextBlock : ~0ull;
  if(audio.src.armed) next = std::min(next, audio.src.retireAt);
  return next;
}

//(channel, sample count, format): reserves a mixer channel and returns its number. A negative channel asks for the
//highest free one, free meaning neither reserved nor still playing a buffer it was released with. Checked in this
//order: the search (NO_CHANNELS), the number (8 and up: INVALID_CHANNEL), a reservation already (INVALID_CHANNEL
//too), the sample count (INVALID_SIZE), the format (INVALID_FORMAT). The volumes start at 0. A channel asked for by
//number may still be playing out a buffer: only its reservation counts, and an output to it is BUSY till that's done.
auto Kernel::sceAudioChReserve() -> void {
  s32 number = s32(arg(0));
  u32 samples = arg(1), format = arg(2);
  if(number < 0) {
    for(number = 7; number >= 0; number--) {
      if(!audio.channels[number].reserved && !audio.channels[number].buffer) break;
    }
    if(number < 0) return result(ErrorAudioNoChannels);
  }
  if(number >= 8 || audio.channels[number].reserved) return result(ErrorAudioInvalidChannel);
  if(!mixerSamplesValid(samples)) return result(ErrorAudioSampleCount);
  if(format != AudioStereo && format != AudioMono) return result(ErrorAudioInvalidFormat);
  auto& channel = audio.channels[number];
  channel.reserved = true;
  channel.sampleCount = samples;
  channel.format = format;
  channel.leftVolume = channel.rightVolume = 0;
  result(number);
}

//(channel): the reservation ends; a buffer in the slot plays out. Not while a thread waits on the channel (BUSY).
auto Kernel::sceAudioChRelease() -> void {
  u32 number = arg(0);
  if(number >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[number];
  if(!channel.reserved) return result(ErrorAudioChannelNotReserved);
  if(audioWaiter(number)) return result(ErrorAudioChannelBusy);
  channel.reserved = false;
  result(0);
}

//(channel, volume, buffer): one volume for both sides, 0 to 0xFFFF. Above that is refused (INVALID_VOLUME) before
//anything else is looked at; a negative one (compared signed: 0x80000000 is negative) keeps the channel's volumes.
auto Kernel::sceAudioOutput() -> void {
  s32 volume = s32(arg(1));
  if(volume > 0xffff) return result(ErrorAudioInvalidVolume);
  mixerOutput(arg(0), arg(2), volume, volume, false);
}

auto Kernel::sceAudioOutputBlocking() -> void {
  s32 volume = s32(arg(1));
  if(volume > 0xffff) return result(ErrorAudioInvalidVolume);
  mixerOutput(arg(0), arg(2), volume, volume, true);
}

//(channel, left volume, right volume, buffer): each side as sceAudioOutput's volume.
auto Kernel::sceAudioOutputPanned() -> void {
  s32 left = s32(arg(1)), right = s32(arg(2));
  if(left > 0xffff || right > 0xffff) return result(ErrorAudioInvalidVolume);
  mixerOutput(arg(0), arg(3), left, right, false);
}

//(channel, left volume, right volume, buffer): stricter than the others: the two volumes ORed together must be
//0xFFFF at most, so a negative one is refused too.
auto Kernel::sceAudioOutputPannedBlocking() -> void {
  if((arg(1) | arg(2)) > 0xffff) return result(ErrorAudioInvalidVolume);
  mixerOutput(arg(0), arg(3), s32(arg(1)), s32(arg(2)), true);
}

//(channel): the samples still to play: what the slot has left (or the count a null buffer set), plus a whole buffer
//if a thread waits to hand one over. The reservation isn't looked at: an idle channel has 0.
auto Kernel::sceAudioGetChannelRestLen() -> void {
  u32 number = arg(0);
  if(number >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[number];
  result(channel.remaining + (audioWaiter(number) ? channel.sampleCount : 0));
}

//(channel): the same, except that what the slot has left counts only for a real buffer: after a null one, 0.
auto Kernel::sceAudioGetChannelRestLength() -> void {
  u32 number = arg(0);
  if(number >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[number];
  result((channel.buffer ? channel.remaining : 0) + (audioWaiter(number) ? channel.sampleCount : 0));
}

//(channel, sample count): the count of the buffers handed over from now on. Checked in this order: the channel's
//number, the count (INVALID_SIZE, even for a channel not reserved), a thread waiting (BUSY), the reservation
//(NOT_INIT, not NOT_RESERVED).
auto Kernel::sceAudioSetChannelDataLen() -> void {
  u32 number = arg(0), samples = arg(1);
  if(number >= 8) return result(ErrorAudioInvalidChannel);
  if(!mixerSamplesValid(samples)) return result(ErrorAudioSampleCount);
  if(audioWaiter(number)) return result(ErrorAudioChannelBusy);
  auto& channel = audio.channels[number];
  if(!channel.reserved) return result(ErrorAudioChannelNotInitialized);
  channel.sampleCount = samples;
  result(0);
}

//(channel, format): stereo or mono from the next buffer on. Not while a buffer is in the slot or a thread waits
//(BUSY, checked before the reservation, NOT_RESERVED); then the format (INVALID_FORMAT).
auto Kernel::sceAudioChangeChannelConfig() -> void {
  u32 number = arg(0), format = arg(1);
  if(number >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[number];
  if(channel.buffer || audioWaiter(number)) return result(ErrorAudioChannelBusy);
  if(!channel.reserved) return result(ErrorAudioChannelNotReserved);
  if(format != AudioStereo && format != AudioMono) return result(ErrorAudioInvalidFormat);
  channel.format = format;
  result(0);
}

//(channel, left volume, right volume): a side above 0xFFFF is refused (INVALID_VOLUME), before the channel's number
//is looked at; a negative side is left as it is. Neither the reservation nor a thread waiting matters.
auto Kernel::sceAudioChangeChannelVolume() -> void {
  s32 left = s32(arg(1)), right = s32(arg(2));
  if(left > 0xffff || right > 0xffff) return result(ErrorAudioInvalidVolume);
  u32 number = arg(0);
  if(number >= 8) return result(ErrorAudioInvalidChannel);
  auto& channel = audio.channels[number];
  if(left >= 0) channel.leftVolume = left;
  if(right >= 0) channel.rightVolume = right;
  result(0);
}

//Reserving the SRC channel (sample count, rate, channels), for either family: sceAudioOutput2Reserve(n) is
//sceAudioSRCChReserve(n, 44100, 2). Checked in this order: 4 channels (a "not supported" code of its own), any other
//number but 2 (INVALID_SIZE), the sample count with its top bit ignored (17-4111, else INVALID_SIZE), the rate
//(INVALID_FREQUENCY; no bit is ignored there), and the channel reserved already, by either family
//(ALREADY_RESERVED). Reserving it doesn't take a mixer channel, nor does it need one.
auto Kernel::srcReserve(u32 samples, u32 rate, u32 channels) -> void {
  auto& src = audio.src;
  if(channels == 4) return result(ErrorNotImplemented);
  if(channels != 2) return result(ErrorInvalidSize);
  samples &= 0x7fff'ffff;
  if(!srcSamplesValid(samples)) return result(ErrorInvalidSize);
  if(!srcRateValid(rate)) return result(ErrorAudioInvalidFrequency);
  if(src.reserved) return result(ErrorAudioChannelAlreadyReserved);
  src.reserved = true;
  src.sampleCount = samples;
  src.rate = rate ? rate : 44'100;
  src.completion = false;
  result(0);
}

//Releasing it, by either family's call, whichever reserved it, from any thread: refused while a buffer is armed
//(ALREADY_RESERVED), not put off until it has played. A program drains the channel first, with a null output.
auto Kernel::srcRelease() -> void {
  auto& src = audio.src;
  if(!src.reserved) return result(ErrorAudioChannelNotReserved);
  if(src.armed) return result(ErrorAudioChannelAlreadyReserved);
  src.reserved = false;
  result(0);
}

//(volume, buffer), for either family. The volume first: 0 to 0xFFFFF, a negative one refused (INVALID_VOLUME); then
//the reservation (NOT_RESERVED); then, with both slots armed, BUSY at once, whoever else waits. Otherwise a real
//buffer is armed in the free slot, with the channel's sample count as it is now (sceAudioOutput2ChangeLength's later
//changes are for later buffers), playing at once if nothing was, its transfer ending (it retires) 100 microseconds
//before it's been heard; and the call waits for a completion. One pending is taken and the call returns at once
//(starting from idle makes one, so the first output after a pause doesn't wait); else it returns as the buffer
//playing retires, one buffer's time in a steady stream. It returns the sample count its buffer was armed with.
//
//A null buffer arms nothing and returns 0: at once if nothing is armed, else once everything armed has played.
//
//A call that would wait from an interrupt handler or with interrupts held off returns the wait's error instead
//(pspautotests' intr/waits), even with a completion pending, which stays pending; but its buffer stays armed, and
//plays.
auto Kernel::srcOutput() -> void {
  auto& src = audio.src;
  s32 volume = s32(arg(0));
  u32 buffer = arg(1);
  if(volume < 0 || volume > 0xf'ffff) return result(ErrorAudioInvalidVolume);
  if(!src.reserved) return result(ErrorAudioChannelNotReserved);
  if(src.armed == 2) return result(ErrorAudioChannelBusy);
  if(!buffer) {
    if(!src.armed) return result(0);
    if(auto refused = audioWaitRefused()) return result(refused);
    return block(Wait::Audio, Audio::WaitSrcDrain, 0);
  }
  if(!src.armed) {
    src.retireAt = cycles + srcDuration(src.sampleCount) - Audio::SrcLead;
    src.completion = true;
  }
  src.buffers[src.armed++] = {buffer, src.sampleCount, u32(volume)};
  if(auto refused = audioWaitRefused()) return result(refused);
  if(src.completion) {
    src.completion = false;
    return result(src.sampleCount);
  }
  current->waitCount = src.sampleCount;
  block(Wait::Audio, Audio::WaitSrc, 0);
}

auto Kernel::sceAudioOutput2Reserve() -> void { srcReserve(arg(0), 44'100, 2); }
auto Kernel::sceAudioSRCChReserve() -> void { srcReserve(arg(0), arg(1), arg(2)); }
auto Kernel::sceAudioOutput2Release() -> void { srcRelease(); }
auto Kernel::sceAudioSRCChRelease() -> void { srcRelease(); }
auto Kernel::sceAudioOutput2OutputBlocking() -> void { srcOutput(); }
auto Kernel::sceAudioSRCOutputBlocking() -> void { srcOutput(); }

//The samples still to play: each armed buffer counts as the channel's sample count as it is now, however far it has
//played (so after sceAudioOutput2ChangeLength(64), a 4096-sample buffer still playing counts 64).
auto Kernel::sceAudioOutput2GetRestSample() -> void {
  if(!audio.src.reserved) return result(ErrorAudioChannelNotReserved);
  result(audio.src.armed * audio.src.sampleCount);
}

//(sample count): the count of the buffers handed over from now on, 17 to 4111 (else INVALID_SIZE, the mixer's code,
//checked before the reservation); the buffers armed already play out at their own.
auto Kernel::sceAudioOutput2ChangeLength() -> void {
  u32 samples = arg(0);
  if(!srcSamplesValid(samples)) return result(ErrorAudioSampleCount);
  if(!audio.src.reserved) return result(ErrorAudioChannelNotReserved);
  audio.src.sampleCount = samples;
  result(0);
}
