//Threads, and what they wait on: delays, sleep, other threads ending, semaphores and lightweight mutexes.

auto Kernel::findThread(u32 uid) -> Thread* {
  if(uid == 0) return current;  //0 means the calling thread
  auto found = threads.find(uid);
  return found == threads.end() ? nullptr : found->second.get();
}

//A new thread, dormant until started: its stack comes from the top of the user partition, as the PSP takes it.
auto Kernel::createThread(const std::string& name, u32 entry, u32 priority, u32 stackSize, u32 attributes, u32 gp) -> s32 {
  if(priority < 0x01 || priority > 0x7f) return ErrorIllegalPriority;
  if(stackSize < 0x200) return ErrorIllegalStackSize;
  auto block = allocate(stackSize, 1, 0, "stack: " + name);
  if(!block) return ErrorNoMemory;
  auto thread = std::make_unique<Thread>();
  thread->uid = nextUID++;
  thread->name = name;
  thread->entry = entry;
  thread->priority = thread->initialPriority = priority;
  thread->stackBlock = block->address;
  thread->stackSize = block->size;
  thread->attributes = attributes;
  thread->gp = gp;
  u32 uid = thread->uid;
  threads[uid] = std::move(thread);
  return uid;
}

//Sets a dormant thread going from its entry point: fresh registers, the stack pointer at the top of its stack, the
//argument (argumentLength bytes at argumentPointer) copied just below it, with a0 its length and a1 where it is. ra
//points at the trampoline, so returning from the entry function ends the thread. The caller has checked that the
//argument is readable and fits (argumentFits()).
auto Kernel::startThread(Thread& thread, u32 argumentLength, u32 argumentPointer) -> void {
  auto& c = thread.context;
  c = {};
  c.pc = thread.entry;
  c.pd = thread.entry + 4;
  c.pfxs = c.pfxt = 0xe4;  //the VFPU's prefixes doing nothing
  c.fcsr = 0x0000'0e00;    //FCSR as a PSP program finds it (measured): rounding to the nearest, traps for overflow,
                           //dividing by zero and invalid operations enabled (see interpreter-fpu.cpp)
  u32 sp = thread.stackBlock + thread.stackSize - 0x100;  //the top 256 bytes are the kernel's, as on the PSP
  if(argumentPointer && argumentLength) {
    sp = (sp - argumentLength) & ~15u;
    std::vector<u8> argument(argumentLength);
    memory.copyOut(argument.data(), argumentPointer, argumentLength);
    memory.copyIn(sp, argument.data(), argumentLength);
    c.gpr[4] = argumentLength;
    c.gpr[5] = sp;
  }
  c.gpr[29] = (sp - 0x40) & ~15u;
  c.gpr[28] = thread.gp;
  c.gpr[31] = Trampoline;
  c.gpr[26] = thread.stackBlock + thread.stackSize - 0x100;  //k0: the thread's kernel area
  thread.wakeupCount = 0;
  ready(thread, 0);
  reschedule();
}

auto Kernel::save(Context& c) -> void {
  std::copy(cpu.ipu.r, cpu.ipu.r + 32, c.gpr);
  c.lo = cpu.ipu.lo; c.hi = cpu.ipu.hi; c.pc = cpu.ipu.pc; c.pd = cpu.ipu.pd;
  std::copy(cpu.fpu.r, cpu.fpu.r + 32, c.fpr);
  c.fcsr = cpu.fpu.csr;
  std::copy(cpu.vfpu.r, cpu.vfpu.r + 128, c.vpr);
  c.pfxs = cpu.vfpu.pfxs; c.pfxt = cpu.vfpu.pfxt; c.pfxd = cpu.vfpu.pfxd; c.cc = cpu.vfpu.cc;
}

auto Kernel::restore(const Context& c) -> void {
  std::copy(c.gpr, c.gpr + 32, cpu.ipu.r);
  cpu.ipu.lo = c.lo; cpu.ipu.hi = c.hi; cpu.ipu.pc = c.pc; cpu.ipu.pd = c.pd;
  std::copy(c.fpr, c.fpr + 32, cpu.fpu.r);
  cpu.fpu.csr = c.fcsr;
  std::copy(c.vpr, c.vpr + 128, cpu.vfpu.r);
  cpu.vfpu.pfxs = c.pfxs; cpu.vfpu.pfxt = c.pfxt; cpu.vfpu.pfxd = c.pfxd; cpu.vfpu.cc = c.cc;
}

//A thread may run again; returnValue is what the function it waited in returns (its v0).
auto Kernel::ready(Thread& thread, u32 returnValue) -> void {
  if(thread.timeoutPointer) {  //what's left of its timeout, in microseconds (none, if it ran out)
    u64 left = thread.wakeAt > cycles ? (thread.wakeAt - cycles) / (CPUFrequency / 1'000'000) : 0;
    memory.write(4, thread.timeoutPointer, returnValue == ErrorWaitTimeout ? 0 : u32(left));
    thread.timeoutPointer = 0;
  }
  thread.status = Status::Ready;
  thread.wait = Wait::None;
  thread.wakeAt = 0;
  thread.readySince = ++readySequence;
  thread.context.gpr[2] = returnValue;
}

//The calling thread waits for something: for wakeAt (a cycle, 0 for no time limit) at the latest. Another thread
//runs meanwhile. (Functions that wait check mayWait() first: a call into the program can't wait.)
auto Kernel::block(Wait wait, u32 id, u64 wakeAt, u32 timeoutPointer) -> void {
  if(!current) return;
  if(interrupting) return result(ErrorIllegalContext);
  current->status = Status::Waiting;
  current->wait = wait;
  current->waitID = id;
  current->wakeAt = wakeAt;
  current->timeoutPointer = timeoutPointer;
  reschedule();
}

//Picks the thread to run: the ready one with the highest priority (the lowest number), the one ready the longest
//among equals. The running thread keeps the CPU unless one with a strictly higher priority is ready. During a call
//into the program the choice waits until it's over.
auto Kernel::reschedule() -> void {
  if(interrupting) {
    rescheduleAfter = true;
    return;
  }
  Thread* best = nullptr;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Ready) continue;
    if(!best || thread->priority < best->priority
    || (thread->priority == best->priority && thread->readySince < best->readySince)) best = thread.get();
  }
  if(current && current->status == Status::Running) {
    if(!best || best->priority >= current->priority) return;
    current->status = Status::Ready;
    current->readySince = ++readySequence;
  }
  switchTo(best);
}

//Puts the running thread's registers aside and loads next's (none: the CPU idles until a thread is ready).
auto Kernel::switchTo(Thread* next) -> void {
  if(current && current == next) {  //it's already in the CPU
    next->status = Status::Running;
    cpu.scc.halted = 0;
    return;
  }
  if(current) save(current->context);
  current = next;
  if(!next) {
    cpu.scc.halted = 1;
    return;
  }
  restore(next->context);
  next->status = Status::Running;
  cpu.scc.halted = 0;
}

//What's due by now: vertical blanks, delays ending, timeouts running out. A thread woken with a higher priority than
//the running one takes over.
auto Kernel::events() -> void {
  bool woke = false;
  while(cycles >= nextVblank) {
    nextVblank += VblankCycles;
    vblanks++;
    for(auto& [uid, thread] : threads) {
      if(thread->status == Status::Waiting && thread->wait == Wait::Vblank) ready(*thread, 0), woke = true;
    }
    if(!controller.cycle && sampleController()) woke = true;
  }
  while(controller.cycle && cycles >= controller.nextSample) {  //a sampling cycle's timer
    controller.nextSample += u64(controller.cycle) * (CPUFrequency / 1'000'000);
    if(sampleController()) woke = true;
  }
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || !thread->wakeAt || cycles < thread->wakeAt) continue;
    if(thread->wait == Wait::LwMutex) {  //it stops waiting: the mutex has one waiter fewer
      memory.write(4, thread->waitID + 12, memory.read(4, thread->waitID + 12) - 1);
    }
    if(thread->wait == Wait::EventFlag) eventFlagTimedOut(*thread);
    ready(*thread, thread->wait == Wait::Delay ? 0 : ErrorWaitTimeout);
    woke = true;
  }
  if(woke) reschedule();
}

//How many cycles until the next thing that's due (at most until the next vertical blank).
auto Kernel::untilNextEvent() const -> u64 {
  u64 next = nextVblank;
  if(controller.cycle) next = std::min(next, controller.nextSample);
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wakeAt) next = std::min(next, thread->wakeAt);
  }
  return next > cycles ? next - cycles : 1;
}

//No thread can run: time jumps to the next thing due (or to end, if that comes first). False if nothing ever will be:
//no thread waits for a time or a frame, so they all wait on each other (or there are none left). The GE still
//running, or a call into the program waiting its turn, will come first.
auto Kernel::idle(u64 end) -> bool {
  if(geBusy || interrupting || (!calls.empty() && interruptsEnabled)) return true;
  bool timed = false;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting) continue;
    if(thread->wakeAt || thread->wait == Wait::Vblank || thread->wait == Wait::Controller) timed = true;
  }
  if(!timed) {
    note(threads.empty() ? "no threads left to run" : "every thread is waiting for another: none will run again");
    return false;
  }
  cycles = std::min(end, cycles + untilNextEvent());
  events();
  return true;
}

//A thread's run is over (status: what it returned, or passed to the exit function): it's dormant again, and the
//threads waiting for its end are told how it ended.
auto Kernel::endThread(Thread& thread, s32 status) -> void {
  thread.status = Status::Dormant;
  thread.wait = Wait::None;
  thread.exitStatus = status;
  for(auto& [uid, other] : threads) {
    if(other->status == Status::Waiting && other->wait == Wait::ThreadEnd && other->waitID == thread.uid) {
      ready(*other, u32(status));
    }
  }
}

//The trampoline's syscall: the running thread's entry function returned (with its result in v0).
auto Kernel::threadReturned() -> void {
  if(!current) return;
  endThread(*current, s32(cpu.ipu.r[2]));
  reschedule();
}

auto Kernel::sceKernelCreateThread() -> void {
  result(createThread(memory.readString(arg(0), 31), arg(1), arg(2), arg(3), arg(4), cpu.ipu.r[28]));
}

//Whether a start argument of length bytes fits on the thread's stack, below the kernel's 256 bytes, with room left
//for the thread's first stack frame.
auto Kernel::argumentFits(const Thread& thread, u32 length) const -> bool {
  return length <= thread.stackSize - 0x100 - 0x40 - 15;
}

//(thread, argument length, argument). An argument too long for the thread's stack, or not readable, is refused
//before anything starts. (The PSP's own code for the first isn't known: a generic one stands in.)
auto Kernel::sceKernelStartThread() -> void {
  auto thread = findThread(arg(0));
  if(!thread || arg(0) == 0) return result(ErrorUnknownThread);
  if(thread->status != Status::Dormant) return result(ErrorNotDormant);
  u32 length = arg(1), pointer = arg(2);
  if(pointer && length) {
    if(!argumentFits(*thread, length)) return result(ErrorIllegalArgument);
    if(!memory.reaches(pointer, length)) return result(ErrorIllegalAddress);
  }
  result(0);
  startThread(*thread, length, pointer);
}

auto Kernel::sceKernelExitThread() -> void {
  if(!current) return;
  endThread(*current, s32(arg(0)));
  reschedule();
}

//The calling thread ends and is deleted at once: its stack goes back to the user partition.
auto Kernel::sceKernelExitDeleteThread() -> void {
  if(!current) return;
  Thread* thread = current;
  endThread(*thread, s32(arg(0)));
  current = nullptr;  //nothing to save: the thread is gone
  for(auto& block : blocks) {
    if(block.address == thread->stackBlock) { release(block.uid); break; }
  }
  threads.erase(thread->uid);
  reschedule();
}

auto Kernel::sceKernelDeleteThread() -> void {
  auto thread = findThread(arg(0));
  if(!thread || arg(0) == 0) return result(ErrorUnknownThread);
  if(thread == current) return result(ErrorIllegalThread);
  if(thread->status != Status::Dormant) return result(ErrorNotDormant);
  for(auto& block : blocks) {
    if(block.address == thread->stackBlock) { release(block.uid); break; }
  }
  threads.erase(thread->uid);
  result(0);
}

auto Kernel::sceKernelGetThreadId() -> void {
  result(current ? current->uid : 0);
}

//Fills a SceKernelThreadInfo (pspthreadman.h) as far as the size its first word gives: name, attributes, status,
//entry, stack, gp, priorities, what it waits for, wakeup count, exit status.
auto Kernel::sceKernelReferThreadStatus() -> void {
  auto thread = findThread(arg(0));
  if(!thread) return result(ErrorUnknownThread);
  u32 info = arg(1);
  u32 size = memory.read(4, info);
  auto put = [&](u32 offset, u32 value) { if(offset + 4 <= size) memory.write(4, info + offset, value); };
  for(u32 offset = 4; offset < 36 && offset < size; offset++) {
    memory.write(1, info + offset, offset - 4 < thread->name.size() ? u8(thread->name[offset - 4]) : 0);
  }
  u32 waitType = 0;  //the PSP's numbers: 1 sleep, 2 delay, 3 semaphore, 9 thread end
  if(thread->status == Status::Waiting) {
    switch(thread->wait) {
    case Wait::Sleep: waitType = 1; break;
    case Wait::Delay: waitType = 2; break;
    case Wait::Semaphore: waitType = 3; break;
    case Wait::ThreadEnd: waitType = 9; break;
    default: break;
    }
  }
  put(36, thread->attributes);
  put(40, u32(thread->status));
  put(44, thread->entry);
  put(48, thread->stackBlock);
  put(52, thread->stackSize);
  put(56, thread->gp);
  put(60, thread->initialPriority);
  put(64, thread->priority);
  put(68, waitType);
  put(72, thread->status == Status::Waiting ? thread->waitID : 0);
  put(76, thread->wakeupCount);
  put(80, u32(thread->exitStatus));
  for(u32 offset = 84; offset < 104; offset += 4) put(offset, 0);  //run clocks, preemption and release counts
  result(0);
}

auto Kernel::sceKernelDelayThread() -> void {
  if(!mayWait()) return;
  result(0);
  block(Wait::Delay, 0, cycles + std::max<u64>(1, u64(arg(0)) * (CPUFrequency / 1'000'000)));
}

//Sleeps until another thread wakes it, unless a wakeup already came while it was awake.
auto Kernel::sceKernelSleepThread() -> void {
  if(!mayWait()) return;
  result(0);
  if(current && current->wakeupCount) {
    current->wakeupCount--;
    return;
  }
  block(Wait::Sleep, 0, 0);
}

auto Kernel::sceKernelWakeupThread() -> void {
  auto thread = findThread(arg(0));
  if(!thread || arg(0) == 0) return result(ErrorUnknownThread);
  result(0);
  if(thread->status == Status::Waiting && thread->wait == Wait::Sleep) {
    ready(*thread, 0);
    reschedule();
  } else {
    thread->wakeupCount++;
  }
}

auto Kernel::sceKernelWaitThreadEnd() -> void {
  if(!mayWait()) return;
  auto thread = findThread(arg(0));
  if(!thread || arg(0) == 0) return result(ErrorUnknownThread);
  if(thread->status == Status::Dormant) return result(u32(thread->exitStatus));
  u32 timeout = arg(1);
  result(0);
  block(Wait::ThreadEnd, thread->uid, timeout ? cycles + u64(memory.read(4, timeout)) * (CPUFrequency / 1'000'000) : 0,
        timeout);
}

auto Kernel::sceKernelCreateSema() -> void {
  s32 initial = s32(arg(2)), maximum = s32(arg(3));
  if(initial < 0 || maximum <= 0 || initial > maximum) return result(ErrorIllegalCount);
  u32 uid = nextUID++;
  semaphores[uid] = {uid, memory.readString(arg(0), 31), arg(1), initial, maximum};
  result(uid);
}

auto Kernel::sceKernelDeleteSema() -> void {
  auto found = semaphores.find(arg(0));
  if(found == semaphores.end()) return result(ErrorUnknownSemaphore);
  result(0);
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::Semaphore && thread->waitID == arg(0)) {
      ready(*thread, ErrorWaitDeleted);
    }
  }
  semaphores.erase(found);
  reschedule();
}

//Hands the semaphore's count to the threads waiting on it, the longest-waiting first, while there's enough for the
//next one.
auto Kernel::signalSemaphores(Semaphore& semaphore) -> void {
  while(true) {
    Thread* next = nullptr;
    for(auto& [uid, thread] : threads) {
      if(thread->status != Status::Waiting || thread->wait != Wait::Semaphore || thread->waitID != semaphore.uid) continue;
      if(!next || thread->readySince < next->readySince) next = thread.get();
    }
    if(!next || s32(next->waitCount) > semaphore.count) return;
    semaphore.count -= next->waitCount;
    ready(*next, 0);
  }
}

auto Kernel::sceKernelSignalSema() -> void {
  auto found = semaphores.find(arg(0));
  if(found == semaphores.end()) return result(ErrorUnknownSemaphore);
  auto& semaphore = found->second;
  s32 count = s32(arg(1));
  if(count <= 0 || semaphore.count + count > semaphore.maximum) return result(ErrorSemaphoreOverflow);
  semaphore.count += count;
  result(0);
  signalSemaphores(semaphore);
  reschedule();
}

auto Kernel::sceKernelWaitSema() -> void {
  if(!mayWait()) return;
  auto found = semaphores.find(arg(0));
  if(found == semaphores.end()) return result(ErrorUnknownSemaphore);
  auto& semaphore = found->second;
  s32 count = s32(arg(1));
  if(count <= 0 || count > semaphore.maximum) return result(ErrorIllegalCount);
  result(0);
  if(semaphore.count >= count) {
    semaphore.count -= count;
    return;
  }
  u32 timeout = arg(2);
  current->waitCount = count;
  current->readySince = ++readySequence;  //its place in the queue
  block(Wait::Semaphore, semaphore.uid,
        timeout ? cycles + u64(memory.read(4, timeout)) * (CPUFrequency / 1'000'000) : 0, timeout);
}

auto Kernel::sceKernelPollSema() -> void {
  auto found = semaphores.find(arg(0));
  if(found == semaphores.end()) return result(ErrorUnknownSemaphore);
  auto& semaphore = found->second;
  s32 count = s32(arg(1));
  if(count <= 0) return result(ErrorIllegalCount);
  if(semaphore.count < count) return result(ErrorSemaphoreZero);
  semaphore.count -= count;
  result(0);
}

//Lightweight mutexes keep their state in the program's own memory, in a 32-byte work area (SceLwMutexWorkarea):
//the lock count, the locking thread (-1 for none), the attributes (0x200: the same thread may lock it again), how
//many threads wait for it, and its UID.
auto Kernel::sceKernelCreateLwMutex() -> void {
  u32 workArea = arg(0), attributes = arg(2);
  s32 count = s32(arg(3));
  if(count < 0 || (count > 1 && !(attributes & 0x200))) return result(ErrorIllegalCount);
  u32 uid = nextUID++;
  lwMutexes[uid] = workArea;
  memory.write(4, workArea + 0, u32(count));
  memory.write(4, workArea + 4, count && current ? current->uid : 0xffff'ffff);
  memory.write(4, workArea + 8, attributes);
  memory.write(4, workArea + 12, 0);
  memory.write(4, workArea + 16, uid);
  result(0);
}

auto Kernel::sceKernelDeleteLwMutex() -> void {
  u32 workArea = arg(0), uid = memory.read(4, workArea + 16);
  if(!lwMutexes.count(uid)) return result(ErrorLwMutexNotFound);
  lwMutexes.erase(uid);
  for(auto& [thread_uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::LwMutex && thread->waitID == workArea) {
      ready(*thread, ErrorWaitDeleted);
    }
  }
  memory.write(4, workArea + 16, 0);
  result(0);
  reschedule();
}

auto Kernel::sceKernelLockLwMutex() -> void {
  if(!mayWait()) return;
  u32 workArea = arg(0), count = arg(1), timeout = arg(2);
  if(!lwMutexes.count(memory.read(4, workArea + 16))) return result(ErrorLwMutexNotFound);
  if(s32(count) <= 0) return result(ErrorIllegalCount);
  u32 level = memory.read(4, workArea), owner = memory.read(4, workArea + 4), attributes = memory.read(4, workArea + 8);
  result(0);
  if(!level) {
    memory.write(4, workArea, count);
    memory.write(4, workArea + 4, current->uid);
    return;
  }
  if(owner == current->uid) {
    if(!(attributes & 0x200)) return result(ErrorLwMutexRecursion);
    memory.write(4, workArea, level + count);
    return;
  }
  memory.write(4, workArea + 12, memory.read(4, workArea + 12) + 1);
  current->waitCount = count;
  current->readySince = ++readySequence;
  block(Wait::LwMutex, workArea,
        timeout ? cycles + u64(memory.read(4, timeout)) * (CPUFrequency / 1'000'000) : 0, timeout);
}

auto Kernel::sceKernelTryLockLwMutex() -> void {
  u32 workArea = arg(0), count = arg(1);
  if(!lwMutexes.count(memory.read(4, workArea + 16))) return result(ErrorLwMutexNotFound);
  if(s32(count) <= 0) return result(ErrorIllegalCount);
  u32 level = memory.read(4, workArea), owner = memory.read(4, workArea + 4), attributes = memory.read(4, workArea + 8);
  if(!level) {
    memory.write(4, workArea, count);
    memory.write(4, workArea + 4, current->uid);
    return result(0);
  }
  if(owner == current->uid && (attributes & 0x200)) {
    memory.write(4, workArea, level + count);
    return result(0);
  }
  result(ErrorLwMutexLocked);
}

//Gives an unlocked mutex to the thread that has waited for it longest.
auto Kernel::unlockLwMutex(u32 workArea) -> void {
  Thread* next = nullptr;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != Wait::LwMutex || thread->waitID != workArea) continue;
    if(!next || thread->readySince < next->readySince) next = thread.get();
  }
  if(!next) return;
  memory.write(4, workArea, next->waitCount);
  memory.write(4, workArea + 4, next->uid);
  memory.write(4, workArea + 12, memory.read(4, workArea + 12) - 1);
  ready(*next, 0);
}

auto Kernel::sceKernelUnlockLwMutex() -> void {
  u32 workArea = arg(0), count = arg(1);
  if(!lwMutexes.count(memory.read(4, workArea + 16))) return result(ErrorLwMutexNotFound);
  if(s32(count) <= 0) return result(ErrorIllegalCount);
  u32 level = memory.read(4, workArea), owner = memory.read(4, workArea + 4);
  if(!level || owner != current->uid) return result(ErrorLwMutexUnlocked);
  if(count > level) return result(ErrorLwMutexUnderflow);
  result(0);
  memory.write(4, workArea, level - count);
  if(level - count) return;
  memory.write(4, workArea + 4, 0xffff'ffff);
  unlockLwMutex(workArea);
  reschedule();
}

//The low 32 bits of the time since power on, in microseconds.
auto Kernel::sceKernelGetSystemTimeLow() -> void {
  result(u32(cycles / (CPUFrequency / 1'000'000)));
}
