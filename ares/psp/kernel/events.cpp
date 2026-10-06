//Event flags: 32 bits a program's threads share to say what has happened. Setting bits wakes the threads waiting for
//them. A thread waits for all of its bits (mode AND, 0) or any of them (OR, 1), and may clear, as it stops waiting,
//the bits it asked for (CLEAR, 0x20) or every bit (CLEARALL, 0x10); it's told what the bits were before that (also
//when it gives up: a poll that fails, a wait that times out, the flag deleted). Unless a flag was made for several
//(attribute 0x200), only one thread may wait on it at a time. Waiters are woken first come first served, or by
//priority if the flag says so (attribute 0x100). The order the errors are checked in, and when the bits are told,
//are as pspautotests' tests/threads/events found them on a PSP.
//
//Callbacks: functions a thread registers for the system to call, here only so a program can set up its exit callback
//(which runs when the player quits from the HOME menu; there's no HOME menu yet, so it never does).

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
  u32 uid = nextUID++;
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

//(flag, bits): sets them, and wakes each waiter they satisfy, in turn (one that clears bits may leave the next
//unsatisfied).
auto Kernel::sceKernelSetEventFlag() -> void {
  auto found = eventFlags.find(arg(0));
  if(found == eventFlags.end()) return result(ErrorUnknownEventFlag);
  auto& flag = found->second;
  flag.pattern |= arg(1);
  for(auto thread : eventFlagWaiters(flag)) {
    if(!flagMatches(flag.pattern, thread->waitCount, thread->waitMode)) continue;
    if(thread->waitPointer) memory.write(4, thread->waitPointer, flag.pattern);
    flag.pattern = flagCleared(flag.pattern, thread->waitCount, thread->waitMode);
    ready(*thread, 0);
  }
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
auto Kernel::sceKernelWaitEventFlag() -> void {
  if(!mayWait()) return;
  u32 bits = arg(1), mode = arg(2), seen = arg(3), timeout = arg(4);
  auto flag = eventFlagFor(arg(0), bits, mode);
  if(!flag) return;
  if(flagMatches(flag->pattern, bits, mode)) {
    if(seen) memory.write(4, seen, flag->pattern);
    flag->pattern = flagCleared(flag->pattern, bits, mode);
    return result(0);
  }
  if(timeout && !memory.read(4, timeout)) return result(ErrorWaitTimeout);
  result(0);
  current->waitCount = bits;
  current->waitMode = mode;
  current->waitPointer = seen;
  current->readySince = ++readySequence;  //its place in the queue
  block(Wait::EventFlag, flag->uid,
        timeout ? cycles + u64(memory.read(4, timeout)) * (CPUFrequency / 1'000'000) : 0, timeout);
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

//(name, function, argument)
auto Kernel::sceKernelCreateCallback() -> void {
  u32 uid = nextUID++;
  callbacks[uid] = {uid, memory.readString(arg(0), 31), arg(1), arg(2), current ? current->uid : 0};
  result(uid);
}

auto Kernel::sceKernelDeleteCallback() -> void {
  if(!callbacks.erase(arg(0))) return result(ErrorUnknownCallback);
  if(exitCallback == arg(0)) exitCallback = 0;
  result(0);
}

auto Kernel::sceKernelRegisterExitCallback() -> void {
  if(!callbacks.count(arg(0))) return result(ErrorUnknownCallback);
  exitCallback = arg(0);
  result(0);
}

//Sleeping where callbacks may run: as sleeping, while nothing notifies them.
auto Kernel::sceKernelSleepThreadCB() -> void {
  sceKernelSleepThread();
}

//Whether any callbacks ran: none do yet.
auto Kernel::sceKernelCheckCallback() -> void {
  result(0);
}
