//Threads, and what they wait on: delays, sleep, other threads ending, semaphores and lightweight mutexes.

auto Kernel::findThread(u32 uid) -> Thread* {
  if(uid == 0) return current;  //0 means the calling thread
  auto found = threads.find(uid);
  return found == threads.end() ? nullptr : found->second.get();
}

//A new object's ID. They count up from 0x100 and are never handed out twice; being positive 32-bit numbers, they run
//out after 2^31 less 256 of them, and there's none (0) after that: the object can't be made.
auto Kernel::newUID() -> u32 {
  return nextUID <= LastUID ? nextUID++ : 0;
}

//A new thread, dormant until started: its stack comes from the top of the user partition, as the PSP takes it. The
//PSP fills a new stack with 0xff bytes (what sceKernelGetThreadStackFreeSize counts) and writes the thread's ID at
//its bottom, unless the thread's attributes say not to (PSP_THREAD_ATTR_NO_FILLSTACK, 0x100000; PPSSPP's notes).
auto Kernel::createThread(const std::string& name, u32 entry, u32 priority, u32 stackSize, u32 attributes, u32 gp) -> s32 {
  if(priority < 0x01 || priority > 0x7f) return ErrorIllegalPriority;
  if(stackSize < 0x200) return ErrorIllegalStackSize;
  auto block = allocate(stackSize, 1, 0, "stack: " + name);
  if(!block) return ErrorNoMemory;
  u32 uid = newUID();
  if(!uid) return release(block->uid), ErrorNoMemory;
  if(!(attributes & 0x0010'0000)) {
    memory.fill(block->address, 0xff, block->size);
    memory.write(4, block->address, uid);
  }
  auto thread = std::make_unique<Thread>();
  thread->uid = uid;
  thread->name = name;
  thread->entry = entry;
  thread->priority = thread->initialPriority = priority;
  thread->stackBlock = block->address;
  thread->stackSize = block->size;
  thread->attributes = attributes;
  thread->gp = gp;
  threads[uid] = std::move(thread);
  return uid;
}

//Sets a dormant thread going from its entry point: fresh registers, the stack pointer at the top of its stack, the
//argument (argumentLength bytes at argumentPointer) copied just below it, with a0 its length and a1 where it is. ra
//points at the trampoline (returnAddress: its first syscall, or the third for a module's module_start), so
//returning from the entry function ends the thread. The caller has checked that the argument is readable and fits
//(argumentFits()). It starts at the priority it was made with, whatever its last run changed it to (pspautotests'
//threads/threads/change: started again, a thread is back at its first priority).
auto Kernel::startThread(Thread& thread, u32 argumentLength, u32 argumentPointer, u32 returnAddress) -> void {
  thread.priority = thread.initialPriority;
  auto& c = thread.context;
  c = {};
  c.pc = thread.entry;
  c.pd = thread.entry + 4;
  c.pfxs = c.pfxt = 0xe4;  //the VFPU's prefixes doing nothing
  c.fcsr = 0x0000'0e00;    //FCSR as a PSP program finds it (measured): rounding to the nearest, traps for overflow,
                           //dividing by zero and invalid operations enabled (see interpreter-fpu.cpp)
  //The top 256 bytes are the kernel's, as on the PSP (k0 points at them), and start zeroed: Peace Walker's C library
  //reads a pointer of its own for the thread at k0 + 4, falling back on a global one when it's 0, and the 0xff bytes
  //a new stack is filled with crashed it.
  u32 sp = thread.stackBlock + thread.stackSize - 0x100;
  memory.fill(sp, 0, 0x100);
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
  c.gpr[31] = returnAddress;
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

//A thread may run again; returnValue is what the function it waited in returns (its v0). A message pipe's send or
//receive tells how many bytes it moved, however its wait ends (messages.cpp).
auto Kernel::ready(Thread& thread, u32 returnValue) -> void {
  if(thread.timeoutPointer) {  //what's left of its timeout, in microseconds (none, if it ran out)
    u64 left = thread.wakeAt > cycles ? (thread.wakeAt - cycles) / (CPUFrequency / 1'000'000) : 0;
    memory.write(4, thread.timeoutPointer, returnValue == ErrorWaitTimeout ? 0 : u32(left));
    thread.timeoutPointer = 0;
  }
  if((thread.wait == Wait::PipeSend || thread.wait == Wait::PipeReceive) && thread.waitResult) {
    memory.write(4, thread.waitResult, thread.waitDone);
    thread.waitResult = 0;
  }
  thread.status = Status::Ready;
  thread.wait = Wait::None;
  thread.wakeAt = 0;
  thread.readySince = ++readySequence;
  thread.context.gpr[2] = returnValue;
}

//The calling thread waits for something: for wakeAt (a cycle, 0 for no time limit) at the latest. Another thread
//runs meanwhile. With callbacks (the functions whose names end in CB), the thread's callbacks run when they're
//notified, the wait going on after them (events.cpp); one notified already runs at once. (Functions that wait check
//mayWait() first: a call into the program can't wait.)
auto Kernel::block(Wait wait, u32 id, u64 wakeAt, u32 timeoutPointer, bool callbacks) -> void {
  if(!current) return;
  if(interrupting) return result(ErrorIllegalContext);
  if(dispatchSuspended) return result(ErrorCanNotWait);  //no other thread could run meanwhile
  current->status = Status::Waiting;
  current->wait = wait;
  current->waitID = id;
  current->wakeAt = wakeAt;
  current->timeoutPointer = timeoutPointer;
  current->callbacks = callbacks;
  if(callbacks) wakeForCallbacks(*current);
  reschedule();
}

//When a wait with a timeout gives up: the timeout's microseconds (a word at pointer) from now, as a cycle; 0, no
//time limit, if there's no pointer.
auto Kernel::timeout(u32 pointer) const -> u64 {
  return pointer ? cycles + u64(memory.read(4, pointer)) * (CPUFrequency / 1'000'000) : 0;
}

//Picks the thread to run: the ready one with the highest priority (the lowest number), the one ready the longest
//among equals. The running thread keeps the CPU unless one with a strictly higher priority is ready. During a call
//into the program the choice waits until it's over.
auto Kernel::reschedule() -> void {
  if(interrupting) {
    rescheduleAfter = true;
    return;
  }
  if(dispatchSuspended && current && current->status == Status::Running) return;  //it keeps the CPU
  Thread* best = nullptr;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Ready || thread->suspended) continue;
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

//Puts the running thread's registers aside and loads next's (none: the CPU idles until a thread is ready). A thread
//still in its wait was made ready only to run its callbacks (wakeForCallbacks()): they start now.
auto Kernel::switchTo(Thread* next) -> void {
  if(current && current == next) {  //it's already in the CPU
    next->status = Status::Running;
    cpu.scc.halted = 0;
  } else {
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
  if(next->wait != Wait::None) runCallbacks(*next);
}

//What's due by now: vertical blanks, delays ending, timeouts running out. A thread woken with a higher priority than
//the running one takes over.
auto Kernel::events() -> void {
  bool woke = false;
  while(cycles >= nextVblank) {
    nextVblank += VblankCycles;
    vblanks++;
    geLeft = GeBudget;  //the GE's commands for the next frame
    for(auto& [uid, thread] : threads) {
      if(thread->status == Status::Waiting && thread->wait == Wait::Vblank) ready(*thread, 0), woke = true;
    }
    vblankInterrupt();
    if(!controller.cycle && sampleController()) woke = true;
  }
  while(controller.cycle && cycles >= controller.nextSample) {  //a sampling cycle's timer
    controller.nextSample += u64(controller.cycle) * (CPUFrequency / 1'000'000);
    if(sampleController()) woke = true;
  }
  if(audioEvents()) woke = true;
  if(asyncEvents()) woke = true;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || !thread->wakeAt || cycles < thread->wakeAt) continue;
    if(thread->wait == Wait::LwMutex) {  //it stops waiting: the mutex has one waiter fewer
      memory.write(4, thread->waitID + 12, memory.read(4, thread->waitID + 12) - 1);
    }
    if(thread->wait == Wait::EventFlag) eventFlagTimedOut(*thread);
    Wait wait = thread->wait;
    ready(*thread, wait == Wait::Delay ? 0 : ErrorWaitTimeout);
    waiterLeft(wait, thread->waitID);
    woke = true;
  }
  if(woke) reschedule();
}

//A thread waiting for a semaphore's count or a memory pool's room stopped waiting without being served (its time
//ran out, or it ended): both serve their waiters in order, one that doesn't fit holding up those behind it, so with
//it gone, those behind it that fit are served now, as they would have been had it never come.
auto Kernel::waiterLeft(Wait wait, u32 id) -> void {
  if(wait == Wait::Semaphore) {
    if(auto found = semaphores.find(id); found != semaphores.end()) signalSemaphores(found->second);
  }
  if(wait == Wait::Fpl || wait == Wait::Vpl) {
    if(auto found = pools.find(id); found != pools.end()) poolWake(found->second);
  }
  //a message pipe's waiters are in line too (messages.cpp)
  if(wait == Wait::PipeSend || wait == Wait::PipeReceive) {
    if(auto found = pipes.find(id); found != pipes.end()) pipeServe(found->second);
  }
}

//How many cycles until the next thing that's due (at most until the next vertical blank).
auto Kernel::untilNextEvent() const -> u64 {
  u64 next = std::min({nextVblank, nextAudioEvent(), nextAsyncEvent()});
  if(controller.cycle) next = std::min(next, controller.nextSample);
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wakeAt) next = std::min(next, thread->wakeAt);
  }
  return next > cycles ? next - cycles : 1;
}

//No thread can run: time jumps to the next thing due (or to end, if that comes first). False if nothing ever will be:
//no thread waits for a time or a frame, so they all wait on each other (or there are none left). A call into the
//program waiting its turn comes first. The GE still running goes on as time passes: Kernel::run() gives it another
//go each time round, so even a display list that never ends lets the frame end.
auto Kernel::idle(u64 end) -> bool {
  if(interrupting || (!calls.empty() && interruptsEnabled)) return true;
  //(a file's asynchronous request being done may wake a thread: one waiting for it, or through its callback)
  bool timed = geBusy || vblankHandlers() || nextAsyncEvent() != ~0ull;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting) continue;
    if(thread->wakeAt || thread->wait == Wait::Vblank || thread->wait == Wait::Controller) timed = true;
    if(thread->wait == Wait::Audio) timed = true;  //a buffer playing ends it
  }
  if(!timed) {
    if(!stuck) {
      note(threads.empty() ? "no threads left to run" : "every thread is waiting for another: none will run again");
    }
    stuck = true;
    return false;
  }
  cycles = std::min(end, cycles + untilNextEvent());
  events();
  return true;
}

//A thread's run is over (status: what it returned, or passed to the exit function): it's dormant again, and the
//threads waiting for its end are told how it ended, as are those waiting for a module whose module_start or
//module_stop it ran (modules.cpp). A thread ended in a wait (terminated), or in a callback that put its wait aside,
//leaves that wait unserved (waiterLeft()).
auto Kernel::endThread(Thread& thread, s32 status) -> void {
  Wait wait = thread.wait;
  WaitState before = thread.waitBeforeCallback;
  thread.status = Status::Dormant;
  thread.wait = Wait::None;
  thread.callbacks = thread.inCallback = false;
  thread.waitBeforeCallback = {};
  thread.exitStatus = status;
  for(auto& [uid, other] : threads) {
    if(other->status == Status::Waiting && other->wait == Wait::ThreadEnd && other->waitID == thread.uid) {
      ready(*other, u32(status));
    }
  }
  waiterLeft(wait, thread.waitID);
  waiterLeft(before.wait, before.id);
  moduleThreadEnded(thread, status);
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

//The calling thread ends. One made to run a module's module_start or module_stop is deleted too, as
//sceKernelExitDeleteThread does: the PSP's module manager deletes the thread it made, however that ends.
auto Kernel::sceKernelExitThread() -> void {
  if(!current) return;
  Thread* thread = current;
  bool made = madeForModule(thread->uid);
  endThread(*thread, s32(arg(0)));
  if(made) {
    current = nullptr;  //nothing to save: the thread is gone
    deleteThread(*thread);
  }
  reschedule();
}

//A thread is gone: its stack goes back to the user partition, and its callbacks go with it (they could only ever run
//on it).
auto Kernel::deleteThread(Thread& thread) -> void {
  u32 uid = thread.uid;
  for(auto& block : blocks) {
    if(block.address == thread.stackBlock) { release(block.uid); break; }
  }
  std::erase_if(callbacks, [&](auto& item) { return item.second.thread == uid; });
  if(current == &thread) current = nullptr;  //nothing to save
  threads.erase(uid);
}

//The calling thread ends and is deleted at once.
auto Kernel::sceKernelExitDeleteThread() -> void {
  if(!current) return;
  endThread(*current, s32(arg(0)));
  deleteThread(*current);
  reschedule();
}

auto Kernel::sceKernelDeleteThread() -> void {
  auto thread = findThread(arg(0));
  if(!thread || arg(0) == 0) return result(ErrorUnknownThread);
  if(thread == current) return result(ErrorIllegalThread);
  if(thread->status != Status::Dormant) return result(ErrorNotDormant);
  deleteThread(*thread);
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
  //the PSP's numbers: 1 sleep, 2 delay, 3 semaphore, 4 event flag, 5 mailbox, 6 VPL, 7 FPL, 8 message pipe, 9 thread
  //end
  u32 waitType = 0;
  if(thread->status == Status::Waiting) {
    switch(thread->wait) {
    case Wait::Sleep: waitType = 1; break;
    case Wait::Delay: waitType = 2; break;
    case Wait::Semaphore: waitType = 3; break;
    case Wait::EventFlag: waitType = 4; break;
    case Wait::Mailbox: waitType = 5; break;
    case Wait::PipeSend: case Wait::PipeReceive: waitType = 8; break;
    case Wait::Vpl: waitType = 6; break;
    case Wait::Fpl: waitType = 7; break;
    case Wait::ThreadEnd: waitType = 9; break;
    default: break;
    }
  }
  u32 status = u32(thread->status);  //suspended: 8, with its waiting (4) kept, but not its readiness (2)
  if(thread->suspended) status = (thread->status == Status::Ready ? 0 : status) | 8;
  put(36, thread->attributes);
  put(40, status);
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

//Waits for a number of microseconds (and, with callbacks, runs the thread's callbacks as they're notified).
auto Kernel::delay(u32 microseconds, bool callbacks) -> void {
  if(!mayWait()) return;
  result(0);
  block(Wait::Delay, 0, cycles + std::max<u64>(1, u64(microseconds) * (CPUFrequency / 1'000'000)), 0, callbacks);
}

auto Kernel::sceKernelDelayThread() -> void {
  delay(arg(0), false);
}

auto Kernel::sceKernelDelayThreadCB() -> void {
  delay(arg(0), true);
}

//Sleeps until another thread wakes it, unless a wakeup already came while it was awake.
auto Kernel::sleep(bool callbacks) -> void {
  if(!mayWait()) return;
  result(0);
  if(current && current->wakeupCount) {
    current->wakeupCount--;
    return callbacksOnReturn(callbacks);
  }
  block(Wait::Sleep, 0, 0, 0, callbacks);
}

auto Kernel::sceKernelSleepThread() -> void {
  sleep(false);
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

//(thread, timeout): waits for the thread to end, and returns what it ended with.
auto Kernel::waitThreadEnd(bool callbacks) -> void {
  if(!mayWait()) return;
  auto thread = findThread(arg(0));
  if(!thread || arg(0) == 0) return result(ErrorUnknownThread);
  if(thread->status == Status::Dormant) {
    result(u32(thread->exitStatus));
    return callbacksOnReturn(callbacks);
  }
  result(0);
  block(Wait::ThreadEnd, thread->uid, timeout(arg(1)), arg(1), callbacks);
}

auto Kernel::sceKernelWaitThreadEnd() -> void {
  waitThreadEnd(false);
}

auto Kernel::sceKernelWaitThreadEndCB() -> void {
  waitThreadEnd(true);
}

auto Kernel::sceKernelCreateSema() -> void {
  s32 initial = s32(arg(2)), maximum = s32(arg(3));
  if(initial < 0 || maximum <= 0 || initial > maximum) return result(ErrorIllegalCount);
  u32 uid = newUID();
  if(!uid) return result(ErrorNoMemory);
  semaphores[uid] = {uid, memory.readString(arg(0), 31), arg(1), initial, maximum, initial};
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
//next one: as it's signalled, and as a waiter leaves without its count (waiterLeft()).
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

//(semaphore, count, timeout)
auto Kernel::waitSemaphore(bool callbacks) -> void {
  if(!mayWait()) return;
  auto found = semaphores.find(arg(0));
  if(found == semaphores.end()) return result(ErrorUnknownSemaphore);
  auto& semaphore = found->second;
  s32 count = s32(arg(1));
  if(count <= 0 || count > semaphore.maximum) return result(ErrorIllegalCount);
  result(0);
  if(semaphore.count >= count) {
    semaphore.count -= count;
    return callbacksOnReturn(callbacks);
  }
  current->waitCount = count;
  current->readySince = ++readySequence;  //its place in the queue
  block(Wait::Semaphore, semaphore.uid, timeout(arg(2)), arg(2), callbacks);
}

auto Kernel::sceKernelWaitSema() -> void {
  waitSemaphore(false);
}

auto Kernel::sceKernelWaitSemaCB() -> void {
  waitSemaphore(true);
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
  u32 uid = newUID();
  if(!uid) return result(ErrorNoMemory);
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

//(work area, count, timeout); with callbacks, the thread's callbacks run while it waits. Only while it waits: the
//lightweight mutexes are Kernel_Library's, a user-mode library, whose lock takes a free (or its own) mutex without
//entering the kernel, so no callback can run there. Peace Walker counts on that: it locks one while holding a lock
//its power callback takes, and running the callback (notified as it was registered) there deadlocked it.
auto Kernel::lockLwMutex(bool callbacks) -> void {
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
        timeout ? cycles + u64(memory.read(4, timeout)) * (CPUFrequency / 1'000'000) : 0, timeout, callbacks);
}

auto Kernel::sceKernelLockLwMutex() -> void {
  lockLwMutex(false);
}

auto Kernel::sceKernelLockLwMutexCB() -> void {
  lockLwMutex(true);
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

//(semaphore, info): a SceKernelSemaInfo (pspthreadman.h) as far as the size in its first word: name, attributes,
//initial, current and largest count, how many threads wait.
auto Kernel::sceKernelReferSemaStatus() -> void {
  auto found = semaphores.find(arg(0));
  if(found == semaphores.end()) return result(ErrorUnknownSemaphore);
  auto& semaphore = found->second;
  u32 info = arg(1), size = memory.read(4, info), waiting = 0;
  for(auto& [uid, thread] : threads) {
    waiting += thread->status == Status::Waiting && thread->wait == Wait::Semaphore && thread->waitID == arg(0);
  }
  for(u32 offset = 4; offset < 36 && offset < size; offset++) {
    memory.write(1, info + offset, offset - 4 < semaphore.name.size() ? u8(semaphore.name[offset - 4]) : 0);
  }
  u32 words[] = {semaphore.attributes, u32(semaphore.initial), u32(semaphore.count), u32(semaphore.maximum), waiting};
  for(u32 n = 0; n < 5; n++) if(36 + n * 4 + 4 <= size) memory.write(4, info + 36 + n * 4, words[n]);
  result(0);
}

//(thread, priority): a new priority for a thread, as pspautotests' threads/threads/change found it on a PSP. A user
//thread's priorities run from 0x08 to 0x77 (those above and below are the kernel's: ILLEGAL_PRIORITY), and 0 stands
//for the caller's own; thread 0 is the caller. A thread not started yet, or ended, can't be changed (DORMANT); one
//ready, waiting or suspended can. The thread goes to the back of its new priority's line: one that's ready goes in
//behind those ready already, and so does the caller, which so gives way to any other thread of its priority (even
//when its priority doesn't change). A thread that ends up above the caller's takes over at once. The test doesn't
//show which comes first, a bad priority or a bad thread: the priority is checked first here.
auto Kernel::sceKernelChangeThreadPriority() -> void {
  u32 priority = arg(1);
  if(priority == 0 && current) priority = current->priority;
  if(priority < 0x08 || priority > 0x77) return result(ErrorIllegalPriority);
  auto thread = findThread(arg(0));
  if(!thread) return result(ErrorUnknownThread);
  if(thread->status == Status::Dormant) return result(ErrorDormant);
  thread->priority = priority;
  result(0);
  //a thread waiting keeps its place: for a semaphore, readySince is its place in that queue
  if(thread->status == Status::Ready || thread->status == Status::Running) {
    thread->status = Status::Ready;  //the caller included: reschedule() picks between it and the rest afresh
    thread->readySince = ++readySequence;
  }
  reschedule();
}

//(thread): what a dormant thread ended with; one still going has none yet.
auto Kernel::sceKernelGetThreadExitStatus() -> void {
  auto thread = findThread(arg(0));
  if(!thread) return result(ErrorUnknownThread);
  if(thread->status != Status::Dormant) return result(ErrorNotDormant);
  result(u32(thread->exitStatus));
}

//(thread): ends another thread, wherever it is, as if it had exited (those waiting for its end are told it was
//terminated); not the caller. One made to run a module's module_start or module_stop is deleted too, as
//sceKernelExitThread deletes it: the PSP's module manager deletes the thread it made, however that ends.
auto Kernel::sceKernelTerminateThread() -> void {
  auto thread = findThread(arg(0));
  if(arg(0) == 0 || thread == current) return result(ErrorIllegalThread);
  if(!thread) return result(ErrorUnknownThread);
  if(thread->status == Status::Dormant) return result(ErrorDormant);
  bool made = madeForModule(thread->uid);
  endThread(*thread, s32(ErrorThreadTerminated));
  thread->suspended = false;
  if(made) deleteThread(*thread);
  result(0);
  reschedule();
}

//(thread): ends another thread and deletes it (a module's module_start or module_stop thread too, its module told
//as when it exits).
auto Kernel::sceKernelTerminateDeleteThread() -> void {
  auto thread = findThread(arg(0));
  if(arg(0) == 0 || thread == current) return result(ErrorIllegalThread);
  if(!thread) return result(ErrorUnknownThread);
  if(thread->status != Status::Dormant) endThread(*thread, s32(ErrorThreadTerminated));
  deleteThread(*thread);
  result(0);
  reschedule();
}

//(thread): it stops being scheduled until resumed, whatever its state (a wait goes on meanwhile); not the caller.
auto Kernel::sceKernelSuspendThread() -> void {
  auto thread = findThread(arg(0));
  if(arg(0) == 0 || thread == current) return result(ErrorIllegalThread);
  if(!thread) return result(ErrorUnknownThread);
  if(thread->status == Status::Dormant) return result(ErrorDormant);
  if(thread->suspended) return result(ErrorSuspended);
  thread->suspended = true;
  result(0);
}

auto Kernel::sceKernelResumeThread() -> void {
  auto thread = findThread(arg(0));
  if(arg(0) == 0 || thread == current) return result(ErrorIllegalThread);
  if(!thread) return result(ErrorUnknownThread);
  if(!thread->suspended) return result(ErrorNotSuspended);
  thread->suspended = false;
  result(0);
  reschedule();
}

//(attributes to clear, attributes to set) on the calling thread: only the VFPU's (0x4000) may change (PPSSPP's
//notes).
auto Kernel::sceKernelChangeCurrentThreadAttr() -> void {
  if((arg(0) | arg(1)) & ~0x4000u) return result(ErrorIllegalAttribute);
  if(current) current->attributes = (current->attributes & ~arg(0)) | arg(1);
  result(0);
}

//(thread): how much of a thread's stack it has never used; thread 0 is the caller. A new stack is filled with 0xff
//bytes (createThread()), so counting up from its bottom, the bytes still 0xff were never written, and the first one
//that isn't ends the count. The bottom 16 bytes, where the thread's ID is, aren't counted: on a PSP (pspautotests'
//threads/threads/stackfree), a thread whose 4 KiB stack had gone 0x150 bytes deep has 0xea0 free, one that had gone
//0x550 deep 0xaa0. A stack that wasn't filled (PSP_THREAD_ATTR_NO_FILLSTACK) is counted the same way, through
//whatever was in that memory before: there, nothing. The stack is read where it is, in RAM (a block of the user
//partition), not copied out first.
auto Kernel::sceKernelGetThreadStackFreeSize() -> void {
  auto thread = findThread(arg(0));
  if(!thread) return result(ErrorUnknownThread);
  u32 unused = 0;
  if(const u8* stack = memory.pointer(thread->stackBlock, thread->stackSize)) {
    while(16 + unused < thread->stackSize && stack[16 + unused] == 0xff) unused++;
  }
  result(unused);
}

//The profiler's figures for a thread, or for all (sceKernelReferThreadProfiler, sceKernelReferGlobalProfiler): only
//development PSPs keep them; a retail one has none to give.
auto Kernel::sceKernelReferThreadProfiler() -> void {
  result(0);
}

//The calling thread's priority now.
auto Kernel::sceKernelGetThreadCurrentPriority() -> void {
  result(current ? current->priority : ErrorIllegalThread);
}

//(priority, 0 for the caller's): the first thread ready at that priority goes to the back of its line; at the
//caller's own, the caller does, giving way to its equals. A user thread's priorities (0x08-0x77) and 0 are taken,
//anything else is ILLEGAL_PRIORITY, as pspautotests' threads/threads/rotate recorded.
auto Kernel::sceKernelRotateThreadReadyQueue() -> void {
  u32 priority = arg(0);
  if(priority == 0 && current) priority = current->priority;
  if(priority < 0x08 || priority > 0x77) return result(ErrorIllegalPriority);
  result(0);
  if(current && current->status == Status::Running && current->priority == priority) {
    current->status = Status::Ready;  //reschedule() picks between it and its equals afresh
    current->readySince = ++readySequence;
    return reschedule();
  }
  Thread* first = nullptr;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Ready || thread->priority != priority) continue;
    if(!first || thread->readySince < first->readySince) first = thread.get();
  }
  if(first) first->readySince = ++readySequence;
}

//Holds off switching threads: the running thread keeps the CPU, whatever becomes ready, until it resumes dispatching
//(a short critical section: SOCOM Fireteam Bravo's sound code takes one). Returns the state to resume with: 1 if
//switching was allowed, 0 if it was held off already. From an interrupt handler, ILLEGAL_CONTEXT. A thread that
//would wait meanwhile is refused (CAN_NOT_WAIT, as with interrupts held off: block()). pspsdk's pspthreadman.h
//names the functions; what they return is chosen (the pair works whichever way the state is read).
auto Kernel::sceKernelSuspendDispatchThread() -> void {
  if(interrupting) return result(ErrorIllegalContext);
  result(dispatchSuspended ? 0 : 1);
  dispatchSuspended = true;
}

//(state): switching allowed again if the state says it was (1), and whichever thread should run now does.
auto Kernel::sceKernelResumeDispatchThread() -> void {
  if(interrupting) return result(ErrorIllegalContext);
  result(0);
  if(arg(0)) {
    dispatchSuspended = false;
    reschedule();
  }
}
