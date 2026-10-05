//Event flags: 32 bits a program's threads share to say what has happened. Setting bits wakes the threads waiting for
//them. A thread waits for all of its bits (mode AND, 0) or any of them (OR, 1), and may clear, as it stops waiting,
//the bits it asked for (CLEAR, 0x20) or every bit (CLEARALL, 0x10); it's told what the bits were before that (also
//when it gives up: a poll that fails, a wait that times out, the flag deleted). Unless a flag was made for several
//(attribute 0x200), only one thread may wait on it at a time. Waiters are woken first come first served, or by
//priority if the flag says so (attribute 0x100). The order the errors are checked in, and when the bits are told,
//are as pspautotests' tests/threads/events found them on a PSP.
//
//Callbacks: functions a thread registers for the system (or another thread) to call back when something happens: the
//player quitting from the HOME menu (the exit callback), the power switch, the disc drive. Being told is called being
//notified; a callback notified since it last ran counts how many times, and keeps the last notification's word. It
//runs on the thread that made it, and only when that thread lets it: by waiting in a function whose name ends in CB
//(sceKernelSleepThreadCB, sceKernelDelayThreadCB, sceDisplayWaitVblankStartCB...), or by calling
//sceKernelCheckCallback. Then, by the thread's priority, as it would run: its registers are put aside (as it is, in
//its wait), the callback runs on its stack as fn(count, word, its own argument), and once it returns, the next one
//notified runs, then the thread goes back into its wait, which may have ended meanwhile (its time ran out: the clock
//doesn't stop for callbacks; what it waited for came, or was deleted). A callback that returns anything but 0 is
//deleted. (As PPSSPP's reading of the PSP has it, from pspautotests' threads/callbacks.) A CB function that needn't
//wait at all still runs the callbacks notified by then before it returns (callbacksOnReturn()).

static auto flagMatches(u32 pattern, u32 bits, u32 mode) -> bool {
  return mode & 1 ? (pattern & bits) != 0 : (pattern & bits) == bits;
}

static auto flagCleared(u32 pattern, u32 bits, u32 mode) -> u32 {
  if(mode & 0x10) return 0;
  if(mode & 0x20) return pattern & ~bits;
  return pattern;
}

//The threads waiting on the flag, in the order they're served.
auto Kernel::eventFlagWaiters(const EventFlag& flag) -> std::vector<Thread*> {
  std::vector<Thread*> waiters;
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::EventFlag && thread->waitID == flag.uid) {
      waiters.push_back(thread.get());
    }
  }
  bool byPriority = flag.attributes & 0x100;
  std::sort(waiters.begin(), waiters.end(), [&](Thread* a, Thread* b) {
    if(byPriority && a->priority != b->priority) return a->priority < b->priority;
    return a->readySince < b->readySince;
  });
  return waiters;
}

//(name, attributes, initial bits, options)
auto Kernel::sceKernelCreateEventFlag() -> void {
  if(arg(1) & ~0x3ffu) return result(ErrorIllegalAttribute);
  u32 uid = newUID();
  if(!uid) return result(ErrorNoMemory);
  eventFlags[uid] = {uid, memory.readString(arg(0), 31), arg(1), arg(2), arg(2)};
  result(uid);
}

//Threads still waiting are told the flag is gone, and its bits.
auto Kernel::sceKernelDeleteEventFlag() -> void {
  auto found = eventFlags.find(arg(0));
  if(found == eventFlags.end()) return result(ErrorUnknownEventFlag);
  for(auto thread : eventFlagWaiters(found->second)) {
    if(thread->waitPointer) memory.write(4, thread->waitPointer, found->second.pattern);
    ready(*thread, ErrorWaitDeleted);
  }
  eventFlags.erase(found);
  result(0);
  reschedule();
}

//Wakes each waiter the flag's bits satisfy, in turn (one that clears bits may leave the next unsatisfied).
auto Kernel::wakeEventFlagWaiters(EventFlag& flag) -> void {
  for(auto thread : eventFlagWaiters(flag)) {
    if(!flagMatches(flag.pattern, thread->waitCount, thread->waitMode)) continue;
    if(thread->waitPointer) memory.write(4, thread->waitPointer, flag.pattern);
    flag.pattern = flagCleared(flag.pattern, thread->waitCount, thread->waitMode);
    ready(*thread, 0);
  }
}

//(flag, bits): sets them, and wakes the waiters they satisfy.
auto Kernel::sceKernelSetEventFlag() -> void {
  auto found = eventFlags.find(arg(0));
  if(found == eventFlags.end()) return result(ErrorUnknownEventFlag);
  found->second.pattern |= arg(1);
  wakeEventFlagWaiters(found->second);
  result(0);
  reschedule();
}

//(flag, bits): keeps only those bits.
auto Kernel::sceKernelClearEventFlag() -> void {
  auto found = eventFlags.find(arg(0));
  if(found == eventFlags.end()) return result(ErrorUnknownEventFlag);
  found->second.pattern &= arg(1);
  result(0);
}

//What waiting and polling check before anything else, in order: the mode, that some bits are asked for, the flag,
//and, on a flag for one, that no other thread is waiting. Null, with the error for the result, if one fails.
auto Kernel::eventFlagFor(u32 uid, u32 bits, u32 mode) -> EventFlag* {
  if((mode & ~0x31u) || (mode & 0x30) == 0x30) return result(ErrorIllegalMode), nullptr;
  if(!bits) return result(ErrorEventFlagPattern), nullptr;
  auto found = eventFlags.find(uid);
  if(found == eventFlags.end()) return result(ErrorUnknownEventFlag), nullptr;
  auto& flag = found->second;
  if(!(flag.attributes & 0x200) && !eventFlagWaiters(flag).empty()) return result(ErrorEventFlagMulti), nullptr;
  return &flag;
}

//(flag, bits, mode, where to put the bits seen, timeout): at once if the bits are there, else waits for them. A
//timeout of 0 gives up at once, without telling the bits.
auto Kernel::waitEventFlag(bool callbacks) -> void {
  if(!mayWait()) return;
  u32 bits = arg(1), mode = arg(2), seen = arg(3), timeoutPointer = arg(4);
  auto flag = eventFlagFor(arg(0), bits, mode);
  if(!flag) return;
  if(flagMatches(flag->pattern, bits, mode)) {
    if(seen) memory.write(4, seen, flag->pattern);
    flag->pattern = flagCleared(flag->pattern, bits, mode);
    result(0);
    return callbacksOnReturn(callbacks);
  }
  if(timeoutPointer && !memory.read(4, timeoutPointer)) return result(ErrorWaitTimeout);
  result(0);
  current->waitCount = bits;
  current->waitMode = mode;
  current->waitPointer = seen;
  current->readySince = ++readySequence;  //its place in the queue
  block(Wait::EventFlag, flag->uid, timeout(timeoutPointer), timeoutPointer, callbacks);
}

auto Kernel::sceKernelWaitEventFlag() -> void {
  waitEventFlag(false);
}

auto Kernel::sceKernelWaitEventFlagCB() -> void {
  waitEventFlag(true);
}

//A wait on a flag ran out of time: the thread is told the bits as they are.
auto Kernel::eventFlagTimedOut(Thread& thread) -> void {
  auto found = eventFlags.find(thread.waitID);
  if(found != eventFlags.end() && thread.waitPointer) memory.write(4, thread.waitPointer, found->second.pattern);
}

//(flag, bits, mode, where to put the bits seen): as waiting, but never waits; told the bits either way.
auto Kernel::sceKernelPollEventFlag() -> void {
  u32 bits = arg(1), mode = arg(2), seen = arg(3);
  auto flag = eventFlagFor(arg(0), bits, mode);
  if(!flag) return;
  if(seen) memory.write(4, seen, flag->pattern);
  if(!flagMatches(flag->pattern, bits, mode)) return result(ErrorEventFlagCondition);
  flag->pattern = flagCleared(flag->pattern, bits, mode);
  result(0);
}

//(flag, info): a SceKernelEventFlagInfo (pspthreadman.h): size, name, attributes, initial and current bits, how many
//threads wait.
auto Kernel::sceKernelReferEventFlagStatus() -> void {
  auto found = eventFlags.find(arg(0));
  if(found == eventFlags.end()) return result(ErrorUnknownEventFlag);
  auto& flag = found->second;
  u32 info = arg(1);
  for(u32 n = 0; n < 32; n++) memory.write(1, info + 4 + n, n < flag.name.size() ? u8(flag.name[n]) : 0);
  memory.write(4, info + 36, flag.attributes);
  memory.write(4, info + 40, flag.initial);
  memory.write(4, info + 44, flag.pattern);
  memory.write(4, info + 48, eventFlagWaiters(flag).size());
  result(0);
}

//A callback is told something happened (argument: the word it's told): it counts, and if its thread waits where it
//may run, the thread is made ready to run it. False if there's no such callback. (The caller reschedules.)
auto Kernel::notifyCallback(u32 uid, u32 argument) -> bool {
  auto found = callbacks.find(uid);
  if(found == callbacks.end()) return false;
  found->second.notifyCount++;
  found->second.notifyArg = argument;
  if(auto owner = threads.find(found->second.thread); owner != threads.end()) wakeForCallbacks(*owner->second);
  return true;
}

//The thread's first callback notified and not run yet (the oldest first), or none.
auto Kernel::pendingCallback(const Thread& thread) -> Callback* {
  for(auto& [uid, callback] : callbacks) {
    if(callback.thread == thread.uid && callback.notifyCount) return &callback;
  }
  return nullptr;
}

//A thread waiting where its callbacks may run, with one notified, is made ready with its wait kept: when its turn
//comes (switchTo()), it runs them, then goes back to its wait.
auto Kernel::wakeForCallbacks(Thread& thread) -> void {
  if(thread.status != Status::Waiting || !thread.callbacks || thread.inCallback || !pendingCallback(thread)) return;
  thread.status = Status::Ready;
  thread.readySince = ++readySequence;
}

//A wait where callbacks may run that ends at once, what it waits for being there already (a semaphore's count, a
//wakeup that came first...), still runs the callbacks notified by then, as waiting would have. They run now, as
//sceKernelCheckCallback's do, and the function returns what it got (its result already in v0) once they're done:
//what it took is kept, and no timeout can run out meanwhile, as it could if the thread waited after all. Not inside
//a callback (the next runs when it returns), nor in an interrupt handler.
auto Kernel::callbacksOnReturn(bool callbacks) -> void {
  if(!callbacks || interrupting || !current || current->inCallback || !pendingCallback(*current)) return;
  runCallbacks(*current);
}

//The thread (its registers in the CPU) runs its notified callbacks: its registers and its wait are put aside as they
//are, and the first starts. If none is notified any more (it was cancelled after its thread was woken for it), the
//thread goes straight back.
auto Kernel::runCallbacks(Thread& thread) -> void {
  save(thread.beforeCallback);
  thread.waitBeforeCallback = {thread.wait, thread.waitID, thread.waitCount, thread.waitMode, thread.waitPointer,
                               thread.timeoutPointer, thread.wakeAt, thread.callbacks};
  thread.wait = Wait::None;  //while its callbacks run, it isn't waiting: they may wait themselves
  thread.wakeAt = 0;
  thread.timeoutPointer = 0;
  thread.callbacks = false;
  if(callNextCallback(thread)) return;
  backFromCallbacks(thread);
}

//Starts the thread's next notified callback, on its stack below where it was: fn(how many times it was notified, the
//last notification's word, its own argument), returning to the trampoline, and so to callbackReturned(). Its count
//starts again from 0. False if none is notified.
auto Kernel::callNextCallback(Thread& thread) -> bool {
  auto callback = pendingCallback(thread);
  if(!callback) return false;
  thread.inCallback = true;
  thread.callbackID = callback->uid;
  cpu.ipu.r[4] = callback->notifyCount;
  cpu.ipu.r[5] = callback->notifyArg;
  cpu.ipu.r[6] = callback->argument;
  callback->notifyCount = callback->notifyArg = 0;
  cpu.ipu.r[29] = (thread.beforeCallback.gpr[29] - 0x40) & ~15u;
  cpu.ipu.r[31] = Trampoline + 16;
  cpu.ipu.pc = callback->function;
  cpu.ipu.pd = callback->function + 4;
  return true;
}

//The trampoline's syscall: the running thread's callback returned (its result in v0). Anything but 0 deletes it. The
//next one notified runs; when none is left, the thread is put back as its callbacks found it.
auto Kernel::callbackReturned() -> void {
  if(!current || !current->inCallback) return;
  auto& thread = *current;
  if(cpu.ipu.r[2]) deleteCallback(thread.callbackID);
  thread.inCallback = false;
  if(callNextCallback(thread)) return;
  backFromCallbacks(thread);
}

//The thread's callbacks are done: from sceKernelCheckCallback it carries on there; from a wait, it goes back into it,
//which ends at once if what ended it came meanwhile (resumeWait()).
auto Kernel::backFromCallbacks(Thread& thread) -> void {
  auto& before = thread.waitBeforeCallback;
  thread.wait = before.wait;
  thread.waitID = before.id;
  thread.waitCount = before.count;
  thread.waitMode = before.mode;
  thread.waitPointer = before.pointer;
  thread.timeoutPointer = before.timeoutPointer;
  thread.wakeAt = before.wakeAt;
  thread.callbacks = before.callbacks;
  before = {};
  if(thread.wait == Wait::None) {
    restore(thread.beforeCallback);
    return reschedule();
  }
  thread.context = thread.beforeCallback;
  thread.status = Status::Waiting;
  if(current == &thread) current = nullptr;  //its registers are in its context, the CPU's are the callback's
  resumeWait(thread);
  reschedule();
}

//A thread back in its wait after its callbacks: what would have ended the wait meanwhile ends it now. Its time ran
//out (the clock doesn't stop for callbacks); what it waited for came; or what it waited on was deleted (the wait ends
//as deleted, as PPSSPP has it).
auto Kernel::resumeWait(Thread& thread) -> void {
  if(thread.wakeAt && cycles >= thread.wakeAt) {
    if(thread.wait == Wait::EventFlag) eventFlagTimedOut(thread);
    return ready(thread, thread.wait == Wait::Delay ? 0 : ErrorWaitTimeout);
  }
  switch(thread.wait) {
  case Wait::Sleep:
    if(thread.wakeupCount) thread.wakeupCount--, ready(thread, 0);
    break;
  case Wait::Semaphore:
    if(auto found = semaphores.find(thread.waitID); found != semaphores.end()) signalSemaphores(found->second);
    else ready(thread, ErrorWaitDeleted);
    break;
  case Wait::EventFlag:
    if(auto found = eventFlags.find(thread.waitID); found != eventFlags.end()) wakeEventFlagWaiters(found->second);
    else ready(thread, ErrorWaitDeleted);
    break;
  case Wait::ThreadEnd:
    if(auto other = threads.find(thread.waitID); other == threads.end()) ready(thread, ErrorWaitDeleted);
    else if(other->second->status == Status::Dormant) ready(thread, u32(other->second->exitStatus));
    break;
  case Wait::Vblank:  //it waited for the next vertical blank after its count
    if(vblanks != thread.waitCount) ready(thread, 0);
    break;
  case Wait::Umd:     //for any of these bits of the drive's state
    if(thread.waitCount & umdState()) ready(thread, 0);
    break;
  case Wait::Fpl: case Wait::Vpl:
    if(auto found = pools.find(thread.waitID); found != pools.end()) poolWake(found->second);
    else ready(thread, ErrorWaitDeleted);
    break;
  default:            //a delay whose time isn't up
    break;
  }
}

//(name, function, argument): a callback of the calling thread's. A function with any of its top four address bits set
//is refused (PPSSPP's notes).
auto Kernel::sceKernelCreateCallback() -> void {
  if(arg(1) & 0xf000'0000) return result(ErrorIllegalAddress);
  u32 uid = newUID();
  if(!uid) return result(ErrorNoMemory);
  callbacks[uid] = {uid, memory.readString(arg(0), 31), arg(1), arg(2), current ? current->uid : 0};
  result(uid);
}

auto Kernel::deleteCallback(u32 uid) -> bool {
  if(!callbacks.erase(uid)) return false;
  if(exitCallback == uid) exitCallback = 0;
  return true;
}

auto Kernel::sceKernelDeleteCallback() -> void {
  result(deleteCallback(arg(0)) ? 0 : ErrorUnknownCallback);
}

//(callback, word): notifies it, as the system would; its thread, waiting where it may run, runs it when its priority
//says (at once if that's above the caller's).
auto Kernel::sceKernelNotifyCallback() -> void {
  if(!notifyCallback(arg(0), arg(1))) return result(ErrorUnknownCallback);
  result(0);
  reschedule();
}

//(callback): forgets its notifications.
auto Kernel::sceKernelCancelCallback() -> void {
  auto found = callbacks.find(arg(0));
  if(found == callbacks.end()) return result(ErrorUnknownCallback);
  found->second.notifyCount = found->second.notifyArg = 0;
  result(0);
}

//(callback): how many times it's been notified since it last ran.
auto Kernel::sceKernelGetCallbackCount() -> void {
  auto found = callbacks.find(arg(0));
  result(found == callbacks.end() ? ErrorUnknownCallback : found->second.notifyCount);
}

//(callback, info): a SceKernelCallbackInfo (pspthreadman.h) as far as the size in its first word: size, name, thread,
//function, argument, notification count and word.
auto Kernel::sceKernelReferCallbackStatus() -> void {
  auto found = callbacks.find(arg(0));
  if(found == callbacks.end()) return result(ErrorUnknownCallback);
  auto& callback = found->second;
  u32 info = arg(1), size = memory.read(4, info);
  for(u32 offset = 4; offset < 36 && offset < size; offset++) {
    memory.write(1, info + offset, offset - 4 < callback.name.size() ? u8(callback.name[offset - 4]) : 0);
  }
  u32 words[] = {callback.thread, callback.function, callback.argument, callback.notifyCount, callback.notifyArg};
  for(u32 n = 0; n < 5; n++) if(36 + n * 4 + 4 <= size) memory.write(4, info + 36 + n * 4, words[n]);
  result(0);
}

auto Kernel::sceKernelRegisterExitCallback() -> void {
  if(!callbacks.count(arg(0))) return result(ErrorUnknownCallback);
  exitCallback = arg(0);
  result(0);
}

//Sleeping where the thread's callbacks may run.
auto Kernel::sceKernelSleepThreadCB() -> void {
  sleep(true);
}

//Runs the calling thread's notified callbacks now: 1 if any ran (it returns once they have), else 0. Not from a
//callback, nor from an interrupt handler (ILLEGAL_CONTEXT, as PPSSPP's reading of the PSP has it).
auto Kernel::sceKernelCheckCallback() -> void {
  if(interrupting || (current && current->inCallback)) return result(ErrorIllegalContext);
  if(!current || !pendingCallback(*current)) return result(0);
  result(1);
  runCallbacks(*current);
}
