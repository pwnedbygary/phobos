//The disc drive (sceUmdUser): whether a disc is in it, and its state, which games check and wait for before reading.
//
//The drive's state is a set of bits: no disc, a disc, a disc changed, not ready, ready, readable. A real drive spins
//up when a game activates it; games expect a disc to be ready early on, activated or not, and here it is readable
//at once, as PPSSPP's notes on the hardware advise, until the game deactivates the drive (ready, no longer
//readable), and again once it activates it. Each of those tells the callback the game registered for the drive, as
//pspautotests' umd/callbacks recorded, and wakes the threads waiting for the state it brings (umd/wait). No disc is
//ever swapped, so a thread waiting for a state the drive won't come to waits until its timeout, if it gave one, or
//for good. (MotorStorm: Arctic Edge activates the drive and waits, a vertical blank at a time, for its callback to
//say the disc is ready and readable.)

//The drive's state: a disc is in it, ready, and readable unless the game deactivated the drive (an image, or a folder
//standing for one); or there's none.
auto Kernel::umdState() const -> u32 {
  if(!disc && !devices.count("disc0")) return UmdNotPresent;
  return UmdPresent | UmdReady | (umdDeactivated ? 0 : UmdReadable);
}

//The drive's state changed: the callback registered for it is told (notified), to run when its thread next may, and
//the threads waiting for any bit of the state the drive is now in wake.
auto Kernel::umdChanged(u32 notified) -> void {
  bool woke = umdCallback && notifyCallback(umdCallback, notified);
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != Wait::Umd || !(thread->waitCount & umdState())) continue;
    ready(*thread, 0);
    woke = true;
  }
  if(woke) reschedule();
}

//Waits for any of stat's bits in the drive's state; timeout in microseconds, 0 for none. The PSP takes a timeout of
//1 microsecond as 25, and any other of at most 209 as 240; the wait that runs callbacks has no 25 (PPSSPP's
//measurements). Bits it can't wait for are refused ahead of whether the thread may wait, as intr/waits recorded.
auto Kernel::umdWait(u32 stat, u32 timeout, bool callbacks) -> void {
  constexpr u32 Waitable = UmdNotPresent | UmdPresent | UmdNotReady | UmdReady | UmdReadable;  //not "changed"
  if(!(stat & Waitable)) return result(ErrorInvalidArgument);
  if(!mayWait()) return;
  if(stat & umdState()) {
    result(0);
    return callbacksOnReturn(callbacks);
  }
  if(timeout == 1 && !callbacks) timeout = 25;
  else if(timeout && timeout <= 209) timeout = 240;
  result(0);
  if(current) current->waitCount = stat;  //what it waits for, should its callbacks run first (resumeWait())
  block(Wait::Umd, 0, timeout ? cycles + u64(timeout) * (CPUFrequency / 1'000'000) : 0, 0, callbacks);
}

//(): 1 if a disc is in the drive.
auto Kernel::sceUmdCheckMedium() -> void {
  result(umdState() & UmdPresent ? 1 : 0);
}

//(mode 1 or 2, the drive's name, which must be "disc0:" and in the program's memory): readies the drive (it's ready
//already), readable again if it was deactivated. The callback is told the state, without its "ready" for a program
//that never said which SDK it was built with (0x22 rather than 0x32: older SDKs' programs see it so).
auto Kernel::sceUmdActivate() -> void {
  if(arg(0) < 1 || arg(0) > 2) return result(ErrorInvalidArgument);
  if(!arg(1) || arg(1) & 0x8000'0000 || memory.readString(arg(1), 8) != "disc0:") return result(ErrorInvalidArgument);
  result(0);
  umdDeactivated = false;
  u32 state = umdState();
  umdChanged(sdkVersion || !(state & UmdPresent) ? state : state & ~UmdReady);
}

//(mode up to 18, the drive's name: needed for mode 2 only, and not compared), as PPSSPP's notes describe.
//The drive stays ready, no longer readable, and the callback is told.
auto Kernel::sceUmdDeactivate() -> void {
  if(arg(0) > 18) return result(ErrorInvalidArgument);
  if((arg(0) == 2 && !arg(1)) || arg(1) & 0x8000'0000) return result(ErrorInvalidArgument);
  result(0);
  umdDeactivated = true;
  umdChanged(umdState());
}

auto Kernel::sceUmdGetDriveStat() -> void {
  result(umdState());
}

//(stat)
auto Kernel::sceUmdWaitDriveStat() -> void {
  umdWait(arg(0), 0, false);
}

//(stat, timeout in microseconds)
auto Kernel::sceUmdWaitDriveStatWithTimer() -> void {
  umdWait(arg(0), arg(1), false);
}

//(stat, timeout in microseconds): as sceUmdWaitDriveStatWithTimer but without the 25-microsecond step, the thread's
//callbacks running meanwhile.
auto Kernel::sceUmdWaitDriveStatCB() -> void {
  umdWait(arg(0), arg(1), true);
}

//Wakes the threads waiting on the drive, telling them the wait was cancelled.
auto Kernel::sceUmdCancelWaitDriveStat() -> void {
  result(0);
  bool woke = false;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != Wait::Umd) continue;
    ready(*thread, ErrorWaitCancelled);
    woke = true;
  }
  if(woke) reschedule();
}

auto Kernel::sceUmdGetErrorStat() -> void {
  result(0);
}

//(where to put a pspUmdInfo: its size, which must be 8, then the disc's kind): a game disc.
auto Kernel::sceUmdGetDiscInfo() -> void {
  u32 info = arg(0);
  if(!memory.reaches(info, 8) || info & 0x8000'0000 || memory.read(4, info) != 8) return result(ErrorInvalidArgument);
  memory.write(4, info + 4, 0x10);
  result(0);
}

//(callback): to be told when the drive's state changes (as the game activates or deactivates it). Registering
//another replaces it.
auto Kernel::sceUmdRegisterUMDCallBack() -> void {
  if(!callbacks.count(arg(0))) return result(ErrorInvalidArgument);
  umdCallback = arg(0);
  result(0);
}

//(callback): no longer told. Refused for any but the one registered, 0 included while one is (and 0 is taken
//while none is); the result is the callback's ID, or 0 for programs of SDKs after 3.00 (umd/register recorded).
auto Kernel::sceUmdUnRegisterUMDCallBack() -> void {
  if(arg(0) != umdCallback) return result(ErrorInvalidArgument);
  umdCallback = 0;
  result(sdkVersion > 0x0300'0000 ? 0 : arg(0));
}

//sceUmdReplacePermit and sceUmdReplaceProhibit: whether the player may swap the disc (there's no other)
auto Kernel::sceUmdReplacePermit() -> void {
  result(0);
}
