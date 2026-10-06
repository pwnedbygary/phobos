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
//short of its length, and each one chained after it a whole buffer later, which keeps a stream's pace. A buffer
//armed after the slots have both freed, but while the last one's final 100 microseconds are still being heard,
//carries straight on after them too, and retires 100 microseconds before its own end: a whole buffer after the last
//one's end. So a program that drains the channel (a null output) before each buffer keeps that pace as well, never
//getting ahead of what's heard.
//
//What the speakers hear. Think of the PSP's sound as a long strip of sound frames, 44,100 a second, each a left and
//a right sample, numbered from power on (sampleFrame() says which frame is heard at a moment). Every channel adds
//what it plays to the frames where it's heard, and the system takes the finished frames, those heard by now, a frame
//of the PSP's at a time (audioOutput(), from System::run()), and hands them to the front end's speakers. The frames
//not taken yet wait in a ring (Audio::Output).
//- A mixer channel: each block of 64 samples is heard from the boundary that takes it, for 64 frames. There
//  (mixerBlock()) the DMA reads the block from the program's memory, multiplies each sample by its side's volume
//  (pspsdk's PSP_AUDIO_VOLUME_MAX, 0x8000, plays a sample as it is; 0x4000 at half its size; up to 0xFFFF, nearly
//  twice) and adds it to those frames. A mono sample is heard on both sides, each at its own volume.
//- The SRC channel plays its buffers at its own rate, 8 to 48 kHz: from the moment the first is armed after an idle
//  stretch, one after another. Its samples are converted to 44.1 kHz as they're heard (srcRender()): each output
//  frame falls somewhere between two of the channel's samples, and takes the straight line between them (linear
//  interpolation). At 22050 Hz every other frame is one of its samples as it is, and the frames between are halfway
//  between their neighbors, so a buffer fills twice as many frames as it has samples. A buffer is read while it
//  plays, never after its slot has been handed back (srcRetire() finishes it first).
//- The frames add up, and the sum is clamped to what a 16-bit sample holds, -32768 to 32767, as the system takes
//  each one: two loud channels together saturate rather than wrap round into noise.
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

//The output frame heard at a moment: a cycle, and how far past it in 49ths of one (as the mixer's DMA counts its
//blocks; the same 49 that FrameRate is). Frame n starts at n * 370000 / 49 cycles, so this is the last frame to
//start by then, worked out in two parts so that no product overflows, whatever the clock.
auto Kernel::sampleFrame(u64 cycle, u32 fraction) const -> u64 {
  return cycle / Audio::FrameCycles * Audio::FrameRate
       + (cycle % Audio::FrameCycles * Audio::FrameRate + fraction) / Audio::FrameCycles;
}

//The first cycle at which a frame is heard (sampleFrame() turned round): frame * 370000 / 49, rounded up, worked out
//in two parts too.
auto Kernel::frameCycle(u64 frame) const -> u64 {
  return frame / Audio::FrameRate * Audio::FrameCycles
       + (frame % Audio::FrameRate * Audio::FrameCycles + Audio::FrameRate - 1) / Audio::FrameRate;
}

//Room in the output for frames up to last (not included), which are about to be added to: returns where the room
//ends, last or short of it. The ring holds OutputFrames frames from the first the system hasn't taken. The channels
//add to frames a block ahead of the clock at most (the mixer's 64 from a boundary; the SRC channel's SrcAhead), and
//room for those is made by dropping the oldest frames, which happens only with no system taking them (the kernel's
//own tests): the system takes them every frame of the PSP's. Frames further ahead, which only a channel gone wrong
//would add, take no room from those not taken: what doesn't fit is left out at the far end. So the first frame not
//taken never passes the clock, and the system gets every frame as the clock reaches it. (A frame the system has
//taken already can't be added to: callers pass over frames before output.start.)
auto Kernel::outputRoom(u64 last) -> u64 {
  auto& output = audio.output;
  u64 near = std::min(last, sampleFrame(cycles) + 64);
  if(near > output.start + Audio::OutputFrames) {
    u64 start = near - Audio::OutputFrames;
    if(start - output.start >= Audio::OutputFrames) {
      std::fill(output.samples.begin(), output.samples.end(), 0);
    } else {
      for(u64 frame = output.start; frame < start; frame++) {
        u32 slot = frame % Audio::OutputFrames * 2;
        output.samples[slot] = output.samples[slot + 1] = 0;
      }
    }
    output.start = start;
  }
  last = std::min(last, output.start + Audio::OutputFrames);
  output.end = std::max({output.end, output.start, last});
  return last;
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

//A channel's next 64 samples, as the DMA takes them: from buffer + (length - remaining) samples in (4 bytes a
//sample in stereo, its left then its right; 2 in mono), each side multiplied by its volume over 0x8000 (shifted
//down, so a fraction rounds down, towards minus infinity) and added into block, 64 pairs of left and right. A
//buffer with no memory behind it plays silence.
auto Kernel::mixBlock(const Audio::Channel& channel, s32* block) -> void {
  bool mono = channel.format == AudioMono;
  u32 bytesPerSample = mono ? 2 : 4, size = 64 * bytesPerSample;
  u32 address = channel.buffer + (channel.length - channel.remaining) * bytesPerSample;
  u8 copy[64 * 4];
  const u8* bytes = memory.pointer(address, size);
  if(!bytes) {  //(through VRAM's rearranged copies, a piece at a time; or not there at all)
    if(!memory.copyOut(copy, address, size)) return;
    bytes = copy;
  }
  s32 leftVolume = channel.leftVolume, rightVolume = channel.rightVolume;
  for(u32 n = 0; n < 64; n++) {
    const u8* sample = bytes + n * bytesPerSample;
    s32 left = s16(sample[0] | sample[1] << 8), right = mono ? left : s16(sample[2] | sample[3] << 8);
    block[n * 2 + 0] += left * leftVolume >> 15;
    block[n * 2 + 1] += right * rightVolume >> 15;
  }
}

//A block boundary, now: the DMA takes the next 64 samples from every channel with a buffer, and they're added to
//the 64 output frames heard from now (mixBlock()). A buffer whose last samples go leaves its slot free, and a thread
//waiting on the channel hands its own buffer over then (to be taken from the next boundary on) and returns with the
//channel's sample count. A boundary with nothing to take stops the DMA: the block the one before took has been
//heard. Returns whether a thread woke.
auto Kernel::mixerBlock() -> bool {
  bool took = false, woke = false;
  s32 block[64 * 2] = {};
  for(u32 number = 0; number < 8; number++) {
    auto& channel = audio.channels[number];
    if(!channel.buffer) continue;
    took = true;
    mixBlock(channel, block);
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
  auto& output = audio.output;
  u64 first = sampleFrame(dma.nextBlock, dma.fraction), last = outputRoom(first + 64);
  for(u64 frame = std::max(first, output.start); frame < last; frame++) {
    u32 slot = frame % Audio::OutputFrames * 2, n = u32(frame - first) * 2;
    output.samples[slot + 0] += block[n + 0];
    output.samples[slot + 1] += block[n + 1];
  }
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

//The index-th sample of the SRC channel's armed buffers, counting from the first buffer's start on into the second:
//its left and right (stereo, 16 bits each), and its buffer's volume. Memory that isn't there is silence. False past
//the samples armed.
auto Kernel::srcSample(u64 index, s32& left, s32& right, u32& volume) -> bool {
  auto& src = audio.src;
  for(u32 n = 0; n < src.armed; n++) {
    auto& buffer = src.buffers[n];
    if(index >= buffer.sampleCount) {
      index -= buffer.sampleCount;
      continue;
    }
    left = right = 0;
    volume = buffer.volume;
    if(auto bytes = memory.pointer(buffer.address + u32(index) * 4, 4)) {
      left = s16(bytes[0] | bytes[1] << 8);
      right = s16(bytes[2] | bytes[3] << 8);
    }
    return true;
  }
  return false;
}

//The SRC channel's samples, converted to the output's 44.1 kHz and added to it, from the frame it got to up to
//frame until (not included); or with wholeBuffer, until the first armed buffer has been heard to its end, wherever
//that falls (its slot is about to be handed back: srcRetire()). Where it is in its samples counts in 44100ths of
//one; each output frame moves it on by the channel's rate (22050 at 22050 Hz: half a sample). A frame between two
//samples is the straight line between them, weighed by how near it is to each, and scaled by its first sample's
//buffer's volume (0x8000 plays it as it is, as for the mixer). It stops where the samples armed run out, to go on
//from there as more come; should the system have taken the frames meanwhile, it goes on from the first it hasn't.
//(A frame past the output's room, which only a channel far ahead of the clock would reach, is lost: outputRoom().)
auto Kernel::srcRender(u64 until, bool wholeBuffer) -> void {
  auto& src = audio.src;
  auto& output = audio.output;
  if(!src.armed) return;
  src.renderedTo = std::max(src.renderedTo, output.start);
  u64 firstEnd = u64(src.buffers[0].sampleCount) * 44'100;
  while(wholeBuffer ? src.position < firstEnd : src.renderedTo < until) {
    u64 index = src.position / 44'100;
    s64 weight = src.position % 44'100;
    s32 left, right, nextLeft = 0, nextRight = 0;
    u32 volume, nextVolume;
    if(!srcSample(index, left, right, volume)) break;
    srcSample(index + 1, nextLeft, nextRight, nextVolume);  //(past the samples armed: towards silence)
    s64 mixedLeft = left + (nextLeft - left) * weight / 44'100;
    s64 mixedRight = right + (nextRight - right) * weight / 44'100;
    if(outputRoom(src.renderedTo + 1) > src.renderedTo) {
      u32 slot = src.renderedTo % Audio::OutputFrames * 2;
      output.samples[slot + 0] += s32(mixedLeft * volume >> 15);
      output.samples[slot + 1] += s32(mixedRight * volume >> 15);
    }
    src.renderedTo++;
    src.position += src.rate;
  }
}

//The SRC channel's first armed buffer has been transferred: its slot is free, and the second, if there is one, plays
//on from here, at once, retiring a whole buffer later (both 100 microseconds ahead of what's heard: srcOutput()).
//That's a completion: it wakes the thread waiting for one, or stays pending for the next output to take. Once
//nothing is armed, the threads waiting for the channel to drain return as well. Returns whether a thread woke.
//The buffer's last samples are added to the output first, ahead of the clock by those 100 microseconds, as the
//program may fill it again from now on.
auto Kernel::srcRetire() -> bool {
  auto& src = audio.src;
  srcRender(0, true);
  src.position -= std::min(src.position, u64(src.buffers[0].sampleCount) * 44'100);
  src.buffers[0] = src.buffers[1];
  src.buffers[1] = {};
  src.armed--;
  if(src.armed) src.retireAt += srcDuration(src.buffers[0].sampleCount);
  else src.position = 0;
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

//What the speakers get: every output frame heard by now (the kernel's clock), the SRC channel's samples brought up
//to now first, each frame's sum clamped to 16 bits and appended to frames as a left and a right sample. They're the
//system's from here, and leave the ring. (Frames a whole ring older than now, which only a machine that hasn't been
//asked for a long while has, are dropped: there's no room for them.)
auto Kernel::audioOutput(std::vector<s16>& frames) -> void {
  auto& output = audio.output;
  u64 now = sampleFrame(cycles);
  srcRender(now);
  outputRoom(now);
  for(; output.start < now; output.start++) {
    u32 slot = output.start % Audio::OutputFrames * 2;
    frames.push_back(s16(std::clamp<s32>(output.samples[slot + 0], -32768, 32767)));
    frames.push_back(s16(std::clamp<s32>(output.samples[slot + 1], -32768, 32767)));
    output.samples[slot + 0] = output.samples[slot + 1] = 0;
  }
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
//changes are for later buffers), playing at once if nothing was (or, if the last buffer's final samples are still to
//be heard, straight after them), its transfer ending (it retires) 100 microseconds before it's been heard; and the
//call waits for a completion. One pending is taken and the call returns at once (starting from idle makes one, so
//the first output after a pause doesn't wait); else it returns as the buffer playing retires, one buffer's time in a
//steady stream. It returns the sample count its buffer was armed with.
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
  if(!src.armed) {  //heard from now, or from the last buffer's end if that's still to be heard
    u64 now = sampleFrame(cycles), heardFrom = cycles;
    if(src.renderedTo > now) heardFrom = frameCycle(src.renderedTo);
    else src.renderedTo = now;
    src.retireAt = heardFrom + srcDuration(src.sampleCount) - Audio::SrcLead;
    src.completion = true;
    src.position = 0;
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
