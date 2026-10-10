//The buttons and the analog stick. The system sets what the player holds (controller.buttons and the stick) as it
//runs; the PSP samples them once a frame, at the vertical blank, or every so many microseconds if a program sets a
//sampling cycle, and keeps the last 64 samples. "Peek" functions give the latest samples at once; "read" functions
//give the samples that came since the last read, waiting for one if none has. The latch gathers presses and releases
//between reads, so a program polling now and then still sees a button tapped in between.
//
//How each call behaves follows uOFW's reading of the PSP's own controller driver (ctrl.c).

//The buttons a game sees: the pad, the four face buttons, the shoulders, start and select, the hold switch, and
//0x10000, which tells it the system has taken the controls (for its HOME menu). The rest (volume, the screen and
//music buttons, the disc and memory stick switches) only the system sees.
static constexpr u32 GameButtons = 0x3'f3f9;

//A sample (events() takes them: at each vertical blank, or on the cycle's timer), handed to a thread waiting to read
//one. Returns whether one woke.
auto Kernel::sampleController() -> bool {
  auto& c = controller;
  u32 buttons = c.buttons & GameButtons;
  bool analog = c.mode == 1;
  c.samples[c.next] = {u32(cycles / (CPUFrequency / 1'000'000)), buttons, analog ? c.analogX : u8(128),
                       analog ? c.analogY : u8(128)};
  c.next = (c.next + 1) % 64;
  c.unread = std::min(c.unread + 1, 63u);  //the 64th would be the one being overwritten

  u32 changed = buttons ^ c.sampled;
  c.latch.made |= buttons & changed;
  c.latch.broken |= ~buttons & changed;
  c.latch.held |= buttons;
  c.latch.released |= ~buttons;
  c.latch.samples++;
  c.sampled = buttons;

  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != Wait::Controller) continue;
    u32 count = thread->waitCount & 0x7fff'ffff;
    ready(*thread, readSamples(thread->waitID, count, thread->waitCount >> 31));
    return true;  //only one thread can wait here (readController())
  }
  return false;
}

//count SceCtrlData (16 bytes each) from the samples kept, starting with samples[first]: the time in microseconds,
//the buttons held (or, negative, those not held), and the stick's two axes.
auto Kernel::writeSamples(u32 address, u32 first, u32 count, bool negative) -> void {
  for(u32 n = 0; n < count; n++) {
    auto& sample = controller.samples[(first + n) % 64];
    u32 at = address + n * 16;
    memory.write(4, at, sample.time);
    memory.write(4, at + 4, negative ? ~sample.buttons : sample.buttons);
    memory.write(1, at + 8, sample.x);
    memory.write(1, at + 9, sample.y);
    memory.fill(at + 10, 0, 6);
  }
}

//A read, once there's a sample it hasn't had: the new samples, oldest first (the newest count of them, if there are
//more). It still fills all count places, as the PSP does: past the newest sample come the oldest kept. Returns how
//many were new.
auto Kernel::readSamples(u32 address, u32 count, bool negative) -> u32 {
  u32 fresh = std::min(controller.unread, count);
  controller.unread = 0;
  writeSamples(address, (controller.next + 64 - fresh) % 64, count, negative);
  return fresh;
}

//A SceCtrlLatch: what the latch has gathered. Returns how many samples that covers.
auto Kernel::writeLatch(u32 address) -> u32 {
  auto& latch = controller.latch;
  memory.write(4, address + 0, latch.made);
  memory.write(4, address + 4, latch.broken);
  memory.write(4, address + 8, latch.held);
  memory.write(4, address + 12, latch.released);
  return latch.samples;
}

//(cycle): 0 samples at each vertical blank; 5555 to 20000 samples every that many microseconds instead (about 180 to
//50 times a second); anything else is refused. Returns the previous cycle.
auto Kernel::sceCtrlSetSamplingCycle() -> void {
  u32 cycle = arg(0);
  if(cycle && (cycle < 5555 || cycle > 20000)) return result(ErrorInvalidValue);
  u32 previous = controller.cycle;
  controller.cycle = cycle;
  if(cycle) controller.nextSample = cycles + u64(cycle) * (CPUFrequency / 1'000'000);
  result(previous);
}

auto Kernel::sceCtrlGetSamplingCycle() -> void {
  if(arg(0)) memory.write(4, arg(0), controller.cycle);
  result(0);
}

//(mode: 0 digital, 1 analog): the previous mode. Samples taken from then on have the stick, or not.
auto Kernel::sceCtrlSetSamplingMode() -> void {
  u32 previous = controller.mode;
  controller.mode = arg(0) ? 1 : 0;
  result(previous);
}

auto Kernel::sceCtrlGetSamplingMode() -> void {
  if(arg(0)) memory.write(4, arg(0), controller.mode);
  result(0);
}

//How many samples a program asked for: the PSP takes the count as a byte, and keeps 64 samples, so 64 or more is too
//many.
static auto sampleCount(u32 argument) -> u32 { return argument & 0xff; }

//(data, count): the last count samples, oldest first; returns count.
auto Kernel::peekController(bool negative) -> void {
  u32 address = arg(0), count = sampleCount(arg(1));
  if(count >= 64) return result(ErrorInvalidSize);
  if(count && !memory.reaches(address, count * 16)) return result(ErrorIllegalAddress);
  writeSamples(address, (controller.next + 64 - count) % 64, count, negative);
  result(count);
}

//(data, count): the samples since the last read, at once if there are any (a program that waits for the vertical
//blank and then reads finds that frame's sample there), else after waiting for the next. Only one thread may wait:
//the PSP waits on an event flag made for a single waiter, so a second is refused. A bad count is refused ahead of
//whether the thread may wait (intr/waits recorded it so in an interrupt handler and with dispatching held off).
auto Kernel::readController(bool negative) -> void {
  u32 address = arg(0), count = sampleCount(arg(1));
  if(count >= 64) return result(ErrorInvalidSize);
  if(!mayWait()) return;
  if(count && !memory.reaches(address, count * 16)) return result(ErrorIllegalAddress);
  if(controller.unread) return result(readSamples(address, count, negative));
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::Controller) return result(ErrorEventFlagMulti);
  }
  result(0);
  current->waitCount = count | (negative ? 0x8000'0000 : 0);
  block(Wait::Controller, address, 0);
}

auto Kernel::sceCtrlPeekBufferPositive() -> void { peekController(false); }
auto Kernel::sceCtrlPeekBufferNegative() -> void { peekController(true); }
auto Kernel::sceCtrlReadBufferPositive() -> void { readController(false); }
auto Kernel::sceCtrlReadBufferNegative() -> void { readController(true); }

auto Kernel::sceCtrlPeekLatch() -> void {
  if(!memory.reaches(arg(0), 16)) return result(ErrorIllegalAddress);
  result(writeLatch(arg(0)));
}

//As peek, and starts the latch afresh. It doesn't wait: read again before the next sample and the latch is empty,
//with a count of 0. (pspsdk's notes say a second read waits for the next sample; the PSP itself could settle it.)
auto Kernel::sceCtrlReadLatch() -> void {
  if(!memory.reaches(arg(0), 16)) return result(ErrorIllegalAddress);
  result(writeLatch(arg(0)));
  controller.latch = {};
}

//(how far the stick moves to put off the idle timer, how far to bring the PSP back from idle): pspsdk's pspctrl.h
//has -1 for never, 0 for always (the stick unmoved), and 1 to 128 for that much movement on either axis; anything
//else, in either, is refused (INVALID_VALUE) with neither kept, as pspautotests' ctrl/idle recorded. Kept to be
//read back: the PSP emulated never dims or sleeps, so nothing waits on them.
auto Kernel::sceCtrlSetIdleCancelThreshold() -> void {
  s32 reset = s32(arg(0)), back = s32(arg(1));
  if(reset < -1 || reset > 128 || back < -1 || back > 128) return result(ErrorInvalidValue);
  controller.idleReset = reset;
  controller.idleBack = back;
  result(0);
}

//(where to put each; null for either not wanted): -1 each until they're set. An address of the kernel's (its top
//bit set) is refused (PRIVILEGE_REQUIRED, ctrl/idle's 0xDEADBEEF), before either is written.
auto Kernel::sceCtrlGetIdleCancelThreshold() -> void {
  u32 reset = arg(0), back = arg(1);
  if((reset | back) & 0x8000'0000) return result(ErrorPrivilegeRequired);
  if(reset) memory.write(4, reset, controller.idleReset);
  if(back) memory.write(4, back, controller.idleBack);
  result(0);
}

//sceHprm, the remote on the headphone socket: nothing is plugged in (pspsdk's psphprm.h: the three checks give 1 for
//plugged in, else 0), so no key is held and its latch is empty. A buffer the answer can't go in is ILLEGAL_ADDR
//(chosen, as the controller's latch).
auto Kernel::sceHprmIsHeadphoneExist() -> void { result(0); }
auto Kernel::sceHprmIsRemoteExist() -> void { result(0); }
auto Kernel::sceHprmIsMicrophoneExist() -> void { result(0); }

//(slot, or -1 for the first free one; callback): a callback to be told when headphones or the remote come or go,
//or a key on the remote changes; with nothing ever plugged in, it never is. pspsdk gives only the pair's NIDs, so
//they're taken to be as scePower's (pspsdk's psppower.h: a slot 0-15 or -1, a callback sceKernelCreateCallback
//made; the slot taken given back for -1, else 0), which is how Soulcalibur: Broken Destiny calls it at boot (-1, and
//the callback it has just made; it keeps the callback, not the result). Sony spelt the second's name "Unregitser":
//its NID, 0x444ed0b7, is that name's hash. Refusals as scePower's (power.cpp): a slot past the 16 or a used one,
//no callback INVALID_ID (chosen, as no recording shows them).
auto Kernel::sceHprmRegisterCallback() -> void {
  s32 slot = s32(arg(0));
  u32 callback = arg(1);
  if(slot < -1 || slot >= 16) return result(ErrorInvalidIndex);
  if(!callback) return result(ErrorInvalidID);
  if(slot == -1) {
    slot = 0;
    while(slot < 16 && hprmCallbacks[slot]) slot++;
    if(slot == 16) return result(ErrorOutOfMemory);
    result(slot);
  } else {
    if(hprmCallbacks[slot]) return result(ErrorAlready);
    result(0);
  }
  hprmCallbacks[slot] = callback;
}

//(slot)
auto Kernel::sceHprmUnregitserCallback() -> void {
  s32 slot = s32(arg(0));
  if(slot < 0 || slot >= 16) return result(ErrorInvalidIndex);
  if(!hprmCallbacks[slot]) return result(ErrorNotFound);
  hprmCallbacks[slot] = 0;
  result(0);
}

//(where the keys held go)
auto Kernel::sceHprmPeekCurrentKey() -> void {
  if(!memory.reaches(arg(0), 4)) return result(ErrorIllegalAddress);
  memory.write(4, arg(0), 0);
  result(0);
}

//(where the latch's four words go): pressed, released, held and not held since the last read, none of each.
auto Kernel::sceHprmPeekLatch() -> void {
  if(!memory.reaches(arg(0), 16)) return result(ErrorIllegalAddress);
  memory.fill(arg(0), 0, 16);
  result(0);
}

auto Kernel::sceHprmReadLatch() -> void {
  sceHprmPeekLatch();
}
