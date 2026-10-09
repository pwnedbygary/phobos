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
//threads/threads/change: started again, a thread is back at its first priority). Its exit status reads NOT_DORMANT
//until it ends, as a PSP's does (threads/threads/threadend: DORMANT before it first starts, NOT_DORMANT after), and
//its run figures count from now (chosen: no recording shows a restarted thread's).
auto Kernel::startThread(Thread& thread, u32 argumentLength, u32 argumentPointer, u32 returnAddress) -> void {
  thread.priority = thread.initialPriority;
  thread.exitStatus = s32(ErrorNotDormant);
  thread.runCycles = 0;
  thread.interruptPreempts = thread.threadPreempts = thread.releases = 0;
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
//receive tells how many bytes it moved, however its wait ends (messages.cpp); sceKernelGetTlsAddr, a block's
//address, or NULL for any error the call under it returned (tls.cpp).
auto Kernel::ready(Thread& thread, u32 returnValue) -> void {
  if(thread.wait == Wait::Tlspl && thread.waitMode == TlsByLibrary && s32(returnValue) < 0) returnValue = 0;
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
//mayWait() first, before they change anything: a call into the program can't wait, nor a thread with dispatching
//held off. The same checks here are a last guard.)
auto Kernel::block(Wait wait, u32 id, u64 wakeAt, u32 timeoutPointer, bool callbacks) -> void {
  if(!current) return;
  if(interrupting) return result(ErrorIllegalContext);
  if(dispatchSuspended || !interruptsEnabled) return result(ErrorCanNotWait);
  current->status = Status::Waiting;
  current->wait = wait;
  current->waitID = id;
  current->wakeAt = wakeAt;
  current->timeoutPointer = timeoutPointer;
  current->callbacks = callbacks;
  if(callbacks) wakeForCallbacks(*current);
  reschedule();
}

//The calling thread waits as block() has it, with a timeout (microseconds, a word at pointer; none without one) as
//long as a PSP's lasts, which pspautotests' threads/scheduling/waittimeouts recorded for every kind of wait alike:
//max(timeout, 205) + about 35 microseconds from the call, what's left of it written back as the wait ends (0 if it
//ran out). A very short one times out at once, its time not written back: 1 did, 100 lasted its 240, and the edge
//between, which the test says moves with the call and from run to run, is taken as 30 here. At once is still a
//moment's wait (TimeoutLate's), in which other threads run: the PSP's scheduling logs have a thread polling with
//timeouts of 1 leave room between its polls for a worse one to print a line and make a call.
auto Kernel::blockTimed(Wait wait, u32 id, u32 pointer, bool callbacks) -> void {
  if(!pointer) return block(wait, id, 0, 0, callbacks);
  u64 asked = memory.read(4, pointer);
  bool atOnce = asked <= TimeoutAtOnce;
  u64 length = atOnce ? TimeoutLate : std::max(asked, TimeoutLeast) + TimeoutLate;
  block(wait, id, cycles + length * (CPUFrequency / 1'000'000), atOnce ? 0 : pointer, callbacks);
}

//Picks the thread to run: the ready one with the highest priority (the lowest number), the one ready the longest
//among equals. The running thread keeps the CPU unless one with a strictly higher priority is ready, and keeps it
//whatever is ready while it holds interrupts or dispatching off, even when it has just put itself back in line
//(rotating its own priority's line, or changing its own priority, which make it ready): it stays running, and gives
//way only once switching is allowed again, to a thread better than it then. During a call into the program the
//choice waits until it's over.
auto Kernel::reschedule() -> void {
  if(interrupting) {
    rescheduleAfter = true;
    return;
  }
  if((dispatchSuspended || !interruptsEnabled) && current
  && (current->status == Status::Running || current->status == Status::Ready)) {
    current->status = Status::Running;
    return;
  }
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
    current->threadPreempts++;
  }
  switchTo(best);
}

//Puts the running thread's registers aside and loads next's (none: the CPU idles until a thread is ready). A thread
//still in its wait was made ready only to run its callbacks (wakeForCallbacks()): they start now. Another thread
//taking the CPU, or none, has interrupts on: only the thread that held them off loses them (it can't lose the CPU
//meanwhile unless it ends). The one leaving adds the time it had the CPU to its run figures.
auto Kernel::switchTo(Thread* next) -> void {
  if(current && current == next) {  //it's already in the CPU
    next->status = Status::Running;
    cpu.scc.halted = 0;
  } else {
    if(current) {
      save(current->context);
      current->runCycles += cycles - ranSince;
    }
    current = next;
    ranSince = cycles;
    interruptsEnabled = true;
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
      if(thread->status != Status::Waiting || thread->wait != Wait::Vblank) continue;
      if(s32(vblanks - thread->waitCount) >= 0) ready(*thread, 0), woke = true;  //its blank came
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
  timerEvents();  //(an alarm's or a virtual timer's handler joins the calls into the program)
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || !thread->wakeAt || cycles < thread->wakeAt) continue;
    leaveWait(*thread, timeUp(*thread));
    woke = true;
  }
  if(woke) reschedule();
}

//A thread stops waiting without having been given what it waited for, told value (its time ran out, or another
//thread let it go: sceKernelReleaseWaitThread): a lightweight mutex counts one waiter fewer, a thread that waited on
//an event flag is told its bits, and those in line behind it are served as they would have been had it never come
//(waiterLeft()). (The caller reschedules.)
auto Kernel::leaveWait(Thread& thread, u32 value) -> void {
  if(thread.wait == Wait::LwMutex) memory.write(4, thread.waitID + 12, memory.read(4, thread.waitID + 12) - 1);
  if(thread.wait == Wait::EventFlag) eventFlagTimedOut(thread);
  Wait wait = thread.wait;
  ready(thread, value);
  waiterLeft(wait, thread.waitID);
}

//What a wait whose time is up returns: a delay, 0; a synchronous read or write, a decode, or the movie player's
//work, its result; any other, a timeout.
auto Kernel::timeUp(const Thread& thread) const -> u32 {
  if(thread.wait == Wait::Delay) return 0;
  if(thread.wait == Wait::File || thread.wait == Wait::Codec || thread.wait == Wait::Psmf) return thread.waitCount;
  return ErrorWaitTimeout;
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
  u64 next = std::min({nextVblank, nextAudioEvent(), nextAsyncEvent(), nextTimerEvent()});
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
  //(a file's asynchronous request being done may wake a thread: one waiting for it, or through its callback; and so
  //may an alarm's or a virtual timer's handler)
  bool timed = geBusy || vblankHandlers() || nextAsyncEvent() != ~0ull || nextTimerEvent() != ~0ull;
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
//leaves that wait unserved (waiterLeft()). The kernel mutexes it held are free, each going to its next waiter.
auto Kernel::endThread(Thread& thread, s32 status) -> void {
  Wait wait = thread.wait;
  WaitState before = thread.waitBeforeCallback;
  thread.status = Status::Dormant;
  thread.wait = Wait::None;
  thread.callbacks = thread.inCallback = false;
  thread.waitBeforeCallback = {};
  thread.exitStatus = status;
  extensionsEnded(thread);
  for(auto& [uid, other] : threads) {
    if(other->status == Status::Waiting && other->wait == Wait::ThreadEnd && other->waitID == thread.uid) {
      ready(*other, u32(status));
    }
  }
  waiterLeft(wait, thread.waitID);
  waiterLeft(before.wait, before.id);
  mutexesFreed(thread.uid);
  tlsThreadEnded(thread.uid);
  moduleThreadEnded(thread, status);
  fontAbandoned(thread.uid);
  mpegAbandoned(thread.uid);
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
  fontAbandoned(uid);
  mpegAbandoned(uid);
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

//The calling thread's ID. An interrupt handler is no thread: pspautotests' threads/alarm recorded an alarm's handler
//finding itself neither the thread it interrupted nor the other one (what it got wasn't printed: ILLEGAL_CONTEXT
//here, as other calls a handler can't make are told).
auto Kernel::sceKernelGetThreadId() -> void {
  if(interrupting) return result(ErrorIllegalContext);
  result(current ? current->uid : 0);
}

//A status structure for the program (the SceKernel...Info structures, pspthreadman.h): its first word is its size,
//and its first bytes are copied as far as the size the program put in that word says, no further than the whole
//(0: nothing). pspautotests recorded every status function so (threads/mutex, threads/events/refer,
//threads/semaphores/refer, threads/lwmutex/refer, threads/threads/refer): whatever size is asked, 1 or 0xffffffff,
//the size reads back as the structure's (a size of 1 copies its first byte, the rest of the word being 0 already),
//and a size short of a field leaves it as it was, 82 copying the half of the word at 80 below 82.
auto Kernel::report(u32 address, const Report& structure) -> void {
  u32 size = memory.read(4, address);
  memory.copyIn(address, structure.bytes.data(), std::min<u64>(size, structure.bytes.size()));
}

//What a thread's status says it is (PspThreadStatus): running 1, ready 2, waiting 4, dormant 16; suspended adds 8,
//its waiting (4) kept but not its readiness (2).
auto Kernel::threadStatus(const Thread& thread) const -> u32 {
  u32 status = u32(thread.status);
  if(thread.suspended) status = (thread.status == Status::Ready ? 0 : status) | 8;
  return status;
}

//What a waiting thread waits for, in the PSP's numbers: 1 sleep, 2 delay, 3 semaphore, 4 event flag, 5 mailbox,
//6 VPL, 7 FPL, 8 message pipe, 9 thread end; any other wait, or none, 0.
auto Kernel::threadWaitType(const Thread& thread) const -> u32 {
  if(thread.status != Status::Waiting) return 0;
  switch(thread.wait) {
  case Wait::Sleep: return 1;
  case Wait::Delay: return 2;
  case Wait::Semaphore: return 3;
  case Wait::EventFlag: return 4;
  case Wait::Mailbox: return 5;
  case Wait::Vpl: return 6;
  case Wait::Fpl: return 7;
  case Wait::PipeSend: case Wait::PipeReceive: return 8;
  case Wait::ThreadEnd: return 9;
  default: return 0;
  }
}

//The time a thread has had the CPU since it was started, in microseconds (a SceKernelSysClock's unit): what it had
//as it last left the CPU, and the running thread's time since it got it (a call into the program on top of it
//counts as the call's, not the thread's).
auto Kernel::threadRunTime(const Thread& thread) const -> u64 {
  u64 ran = thread.runCycles;
  if(&thread == current && !interrupting) ran += cycles - ranSince;
  return ran / (CPUFrequency / 1'000'000);
}

//(thread, info): a SceKernelThreadInfo (pspthreadman.h): name, attributes, status, entry, stack, gp, priorities,
//what it waits for, wakeup count, exit status, and the run figures (as sceKernelReferThreadRunStatus gives them). Its
//size goes by the SDK the program was built with (sceKernelSetCompiledSdkVersion), as pspautotests'
//threads/threads/refer recorded: up to 2.60, 104 bytes, and any size asked is taken (report()); after it, 108 (a last
//word, 0), and a larger size than that refused (ILLEGAL_SIZE), nothing written, 0xffffffff among them.
auto Kernel::sceKernelReferThreadStatus() -> void {
  auto thread = findThread(arg(0));
  if(!thread) return result(ErrorUnknownThread);
  u32 length = sdkVersion > 0x0206'0010 ? 108 : 104;
  if(length == 108 && memory.read(4, arg(1)) > length) return result(ErrorIllegalSize);
  Report info(length);
  info.name(4, thread->name);
  info.word(36, thread->attributes);
  info.word(40, threadStatus(*thread));
  info.word(44, thread->entry);
  info.word(48, thread->stackBlock);
  info.word(52, thread->stackSize);
  info.word(56, thread->gp);
  info.word(60, thread->initialPriority);
  info.word(64, thread->priority);
  info.word(68, threadWaitType(*thread));
  info.word(72, thread->status == Status::Waiting ? thread->waitID : 0);
  info.word(76, thread->wakeupCount);
  info.word(80, u32(thread->exitStatus));
  u64 ran = threadRunTime(*thread);
  info.word(84, u32(ran));
  info.word(88, u32(ran >> 32));
  info.word(92, thread->interruptPreempts);
  info.word(96, thread->threadPreempts);
  info.word(100, thread->releases);
  report(arg(1), info);
  result(0);
}

//(thread, status; 0 for the caller): a SceKernelThreadRunStatus (pspthreadman.h, 44 bytes): the thread's status,
//current priority, wait type and what it waits for, and wakeup count, as its full status gives them; then its run
//figures since it was last started: the time it has had the CPU (a SceKernelSysClock, 64 bits of microseconds), how
//many times a call into the program interrupted it (each interrupt handler, alarm, virtual timer or GE callback that
//ran on top of it), how many times a better thread took the CPU from it while it could still run, and how many times
//sceKernelReleaseWaitThread let it go of a wait. Copied as far as its size word says (report()). Ace Combat X's
//vertical blank handler asks after its main thread every other blank, waking it unless it's running; the size word
//it passes is whatever its stack held (0 here: nothing written, and it wakes the thread each time, as before).
//No pspautotests program calls it; chosen: the figures' meanings (pspsdk names them only), counted from a start, and
//the size rule of every other status function (threads/threads/refer's refusal of large sizes after 2.60 isn't
//taken on: that structure grew a word with it).
auto Kernel::sceKernelReferThreadRunStatus() -> void {
  auto thread = findThread(arg(0));
  if(!thread) return result(ErrorUnknownThread);
  Report status(44);
  status.word(4, threadStatus(*thread));
  status.word(8, thread->priority);
  status.word(12, threadWaitType(*thread));
  status.word(16, thread->status == Status::Waiting ? thread->waitID : 0);
  status.word(20, thread->wakeupCount);
  u64 ran = threadRunTime(*thread);
  status.word(24, u32(ran));
  status.word(28, u32(ran >> 32));
  status.word(32, thread->interruptPreempts);
  status.word(36, thread->threadPreempts);
  status.word(40, thread->releases);
  report(arg(1), status);
  result(0);
}

//(where to put it): the system's status (pspthreadman.h's SceKernelSystemStatus, 28 bytes: its size, a status, the
//idle thread's clocks (64 bits), how often the CPU came out of idling, and the thread and VFPU switches): the status
//0, and the counts, which aren't kept, 0; as much of it written as its size asks for, as with the threads' status.
//Dante's Inferno asks as it starts.
auto Kernel::sceKernelReferSystemStatus() -> void {
  report(arg(0), Report(28));
  result(0);
}

//The IDs of the thread manager's objects of a kind, in the order they were made (pspsdk's SceKernelIdListType): 1
//threads, 2 semaphores, 3 event flags, 4 mailboxes, 5 VPLs, 6 FPLs, 7 message pipes, 8 callbacks, 9 thread event
//handlers (the kernel has none), 10 alarms, 11 virtual timers, 12 mutexes, 13 lightweight mutexes, 14 thread-local
//storage pools; and threads by state, 64 sleeping, 65 delaying, 66 suspended (whatever else they're doing), 67 dormant.
//False for any other kind. pspautotests' threads/threads/threadmanidlist recorded 1-14 and 64-67 taken and all else
//refused, 14 listing such a pool once made; pspsdk names 1-11 and 64-67. Chosen: 12 and 13 as the two kinds of mutex,
//which came with the same firmware as those pools, in that order.
auto Kernel::threadmanIDs(u32 type, std::vector<u32>& ids) -> bool {
  auto all = [&](auto& objects) { for(auto& entry : objects) ids.push_back(entry.first); };
  auto threadsWhere = [&](auto&& matches) {
    for(auto& [uid, thread] : threads) if(matches(*thread)) ids.push_back(uid);
  };
  auto poolsOf = [&](bool variable) {
    for(auto& [uid, pool] : pools) if(pool.variable == variable) ids.push_back(uid);
  };
  switch(type) {
  case 1: all(threads); return true;
  case 2: all(semaphores); return true;
  case 3: all(eventFlags); return true;
  case 4: all(mailboxes); return true;
  case 5: poolsOf(true); return true;
  case 6: poolsOf(false); return true;
  case 7: all(pipes); return true;
  case 8: all(callbacks); return true;
  case 9: return true;
  case 10: all(alarms); return true;
  case 11: all(vtimers); return true;
  case 12: all(mutexes); return true;
  case 13: all(lwMutexes); return true;
  case 14: all(tlsPools); return true;
  case 64: threadsWhere([&](const Thread& t) { return t.status == Status::Waiting && t.wait == Wait::Sleep; });
    return true;
  case 65: threadsWhere([&](const Thread& t) { return t.status == Status::Waiting && t.wait == Wait::Delay; });
    return true;
  case 66: threadsWhere([&](const Thread& t) { return t.suspended; }); return true;
  case 67: threadsWhere([&](const Thread& t) { return t.status == Status::Dormant; }); return true;
  }
  return false;
}

//(kind, buffer, its size in IDs, where to put how many there are): the IDs of the objects of a kind
//(threadmanIDs()), as many as the buffer takes, and how many there are in all; it returns how many it wrote. As
//threads/threads/threadmanidlist recorded: the kind is checked first (ILLEGAL_TYPE), then the size (a negative one
//ILLEGAL_ADDR), neither writing the count; a size of 0, with or without a buffer, gives the count alone, and the
//count's pointer may be 0. Its threads made dormant, sleeping, delaying and suspended were listed as such. How many
//it returns isn't recorded but for a buffer larger than the list, where it's the count: pspsdk says "either 0 or the
//same as idcount", which is what a buffer of none and a large one give if it's how many were written (chosen), and
//its own thread utilities ask for the count with a buffer of none, so the count is all there are. A buffer the IDs
//can't go in is refused (ILLEGAL_ADDR; the test's comment says a PSP crashes on a null one).
auto Kernel::sceKernelGetThreadmanIdList() -> void {
  u32 buffer = arg(1), count = arg(3);
  s32 size = s32(arg(2));
  std::vector<u32> ids;
  if(!threadmanIDs(arg(0), ids)) return result(ErrorIllegalType);
  if(size < 0) return result(ErrorIllegalAddress);
  u32 written = std::min<u64>(ids.size(), u32(size));
  if(written && !memory.reaches(buffer, written * 4)) return result(ErrorIllegalAddress);
  for(u32 n = 0; n < written; n++) memory.write(4, buffer + n * 4, ids[n]);
  if(count && memory.reaches(count, 4)) memory.write(4, count, ids.size());
  result(written);
}

//(ID): the kind of thread manager object an ID is (threadmanIDs()'s numbers, 1 to 14; 14, a thread-local storage pool,
//chosen: threadmanidtype makes none), or ILLEGAL_ARGUMENT for one that isn't any, as threads/threads/threadmanidtype
//recorded: a thread 1, whatever it's doing; a deleted one, -1, 0, 1, a memory block and a module ILLEGAL_ARGUMENT.
auto Kernel::sceKernelGetThreadmanIdType() -> void {
  u32 uid = arg(0);
  if(threads.count(uid)) return result(1);
  if(semaphores.count(uid)) return result(2);
  if(eventFlags.count(uid)) return result(3);
  if(mailboxes.count(uid)) return result(4);
  if(auto pool = pools.find(uid); pool != pools.end()) return result(pool->second.variable ? 5 : 6);
  if(pipes.count(uid)) return result(7);
  if(callbacks.count(uid)) return result(8);
  if(alarms.count(uid)) return result(10);
  if(vtimers.count(uid)) return result(11);
  if(mutexes.count(uid)) return result(12);
  if(lwMutexes.count(uid)) return result(13);
  if(tlsPools.count(uid)) return result(14);
  result(ErrorIllegalArgument);
}

//Waits for a number of microseconds (and, with callbacks, runs the thread's callbacks as they're notified), as long
//as a PSP takes: its thread manager wakes a thread no sooner than about 205 microseconds on, and waking it takes
//about 25 more. pspautotests' threads/scheduling/delaylen recorded every delay from 1 to 209 microseconds taking
//about 230, and longer ones about 25 more than asked (220 about 250, 300 about 330, 1000 about 1030), CB or not. A
//delay of 0 gives the CPU up for a moment, no more (delayzero: it returns at once, or lets a worse thread in).
auto Kernel::delay(u32 microseconds, bool callbacks) -> void {
  if(!mayWait()) return;
  result(0);
  u64 length = microseconds ? std::max<u64>(microseconds, 205) + 25 : 0;
  block(Wait::Delay, 0, cycles + std::max<u64>(1, length * (CPUFrequency / 1'000'000)), 0, callbacks);
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

//(thread, 0 for the caller): the wakeups that came for it while it was awake are forgotten; returns how many there
//were (pspsdk's pspthreadman.h: "Cancel a thread that was to be woken with sceKernelWakeupThread").
auto Kernel::sceKernelCancelWakeupThread() -> void {
  auto thread = findThread(arg(0));
  if(!thread) return result(ErrorUnknownThread);
  result(thread->wakeupCount);
  thread->wakeupCount = 0;
}

//(thread): a thread in a wait is let go of it, whatever it waits for, its wait ending as a timeout's does (those in
//line behind it served) but told RELEASE_WAIT; one made ready to run its callbacks counts as waiting still, one
//running them doesn't. As pspautotests' threads/threads/release recorded: a delay, a sleep and a semaphore's wait
//let go, the released thread running at once if it's better than the caller; a thread not waiting (ended, or never
//started) NOT_WAIT; thread 0 or the caller itself ILLEGAL_THID; one there isn't UNKNOWN_THID.
auto Kernel::sceKernelReleaseWaitThread() -> void {
  if(!arg(0) || (current && arg(0) == current->uid)) return result(ErrorIllegalThread);
  auto found = threads.find(arg(0));
  if(found == threads.end()) return result(ErrorUnknownThread);
  auto& thread = *found->second;
  bool waiting = thread.status == Status::Waiting || thread.status == Status::Ready;
  if(!waiting || thread.wait == Wait::None) return result(ErrorNotWait);
  leaveWait(thread, ErrorReleaseWait);
  thread.releases++;
  result(0);
  reschedule();
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
  blockTimed(Wait::ThreadEnd, thread->uid, arg(1), callbacks);
}

auto Kernel::sceKernelWaitThreadEnd() -> void {
  waitThreadEnd(false);
}

auto Kernel::sceKernelWaitThreadEndCB() -> void {
  waitThreadEnd(true);
}

//(name, attributes, initial count, largest count, options). As pspautotests' threads/semaphores/create recorded: a
//NULL name is ERROR, attributes past 0x1ff ILLEGAL_ATTR (0x100, waiters served by priority, is taken), and any counts
//at all are taken: a negative first count, one above the largest, a negative largest.
auto Kernel::sceKernelCreateSema() -> void {
  s32 initial = s32(arg(2)), maximum = s32(arg(3));
  if(!arg(0)) return result(ErrorError);
  if(arg(1) & ~0x1ffu) return result(ErrorIllegalAttribute);
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
  //(in 64 bits: a semaphore may hold any count, so the sum can pass what 32 hold)
  if(count <= 0 || s64(semaphore.count) + count > semaphore.maximum) return result(ErrorSemaphoreOverflow);
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
  //With callbacks notified, no timeout and no other thread in line, a CB wait whose count is there waits all the
  //same: its callbacks run first, at once and in its wait, and it takes the count once they're done if it's still
  //there, first in line whatever queued meanwhile (resumeWait(); waitMode marks it). Patapon 2's memory stick
  //callback, notified as it's registered, takes and gives back the semaphore the game's next CB wait would take at
  //once, and waited on it for good when run after. Otherwise (chosen: nothing recorded shows which) the count is
  //taken first and the callbacks run after, as every CB wait that ends at once does (callbacksOnReturn()): with
  //another thread in line, served in order, one ahead wanting more would hold this wait up.
  bool callbacksFirst = semaphore.count >= count && callbacks && !arg(2) && callbacksDue();
  auto inLine = [&](Wait wait, u32 id) { return wait == Wait::Semaphore && id == semaphore.uid; };
  for(auto& [uid, thread] : threads) {
    if(!callbacksFirst) break;
    callbacksFirst = !inLine(thread->wait, thread->waitID)
                  && !(thread->inCallback && inLine(thread->waitBeforeCallback.wait, thread->waitBeforeCallback.id));
  }
  if(semaphore.count >= count && !callbacksFirst) {
    semaphore.count -= count;
    return callbacksOnReturn(callbacks);
  }
  current->waitCount = count;
  current->waitMode = callbacksFirst;
  current->readySince = ++readySequence;  //its place in the queue
  if(callbacksFirst) {
    current->wait = Wait::Semaphore;
    current->waitID = semaphore.uid;
    current->wakeAt = 0;
    current->timeoutPointer = 0;
    current->callbacks = true;
    return runCallbacks(*current);
  }
  blockTimed(Wait::Semaphore, semaphore.uid, arg(2), callbacks);
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

//(semaphore, new count, where to put how many threads waited): every thread waiting on it is told the wait was
//cancelled (WAIT_CANCEL), and its count becomes the new one, which can't be past its maximum (ILLEGAL_COUNT, nothing
//written), as pspautotests' threads/semaphores/cancel recorded. A negative count is taken as the count it was made
//with. What the recordings show of one: -1 and -3 accepted on a semaphore made with 0, which reads 0 afterwards;
//that fits 0 as well as "as it was made". The PSP takes a negative count where it refuses one past the maximum, so
//it isn't read as a count, but as something else to set; going back to the count it was made with is the one such
//meaning (and the mutex's cancel takes it the same way).
auto Kernel::sceKernelCancelSema() -> void {
  auto found = semaphores.find(arg(0));
  if(found == semaphores.end()) return result(ErrorUnknownSemaphore);
  auto& semaphore = found->second;
  s32 count = s32(arg(1));
  if(count > semaphore.maximum) return result(ErrorIllegalCount);
  u32 waiting = 0;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != Wait::Semaphore || thread->waitID != arg(0)) continue;
    ready(*thread, ErrorWaitCancelled);
    waiting++;
  }
  if(arg(2)) memory.write(4, arg(2), waiting);
  semaphore.count = count < 0 ? semaphore.initial : count;
  result(0);
  reschedule();
}

//Lightweight mutexes keep their state in the program's own memory, in a 32-byte work area (SceLwMutexWorkarea):
//the lock count, the locking thread (0 for none), the attributes (0x100: waiters served by priority; 0x200: the same
//thread may lock it again), how many threads wait for it, its UID, and three words the PSP zeroes. The kernel keeps
//its name, attributes and the count it was made with besides, for its status. Making one held, locking, trying and
//unlocking it are a thread's alone (fromThread()), as for the kernel's mutexes. As pspautotests' threads/lwmutex
//recorded (create, unlock): a NULL name is ERROR, attributes past 0x3ff ILLEGAL_ATTR, then the count is checked; the
//work area says thread 0 while it's free, made so or unlocked.
auto Kernel::sceKernelCreateLwMutex() -> void {
  u32 workArea = arg(0), attributes = arg(2);
  s32 count = s32(arg(3));
  if(!arg(1)) return result(ErrorError);
  if(attributes & ~0x3ffu) return result(ErrorIllegalAttribute);
  if(count < 0 || (count > 1 && !(attributes & 0x200))) return result(ErrorIllegalCount);
  if(count && !fromThread()) return;
  u32 uid = newUID();
  if(!uid) return result(ErrorNoMemory);
  lwMutexes[uid] = {workArea, memory.readString(arg(1), 31), attributes, count};
  memory.write(4, workArea + 0, u32(count));
  memory.write(4, workArea + 4, count ? current->uid : 0);
  memory.write(4, workArea + 8, attributes);
  memory.write(4, workArea + 12, 0);
  memory.write(4, workArea + 16, uid);
  memory.fill(workArea + 20, 0, 12);
  result(0);
}

//A lightweight mutex's SceKernelLwMutexInfo (64 bytes: name, attributes, UID, work area, the count it was made with
//and its count now, the locking thread or -1, how many threads wait), copied as far as its size word says
//(report()), as pspautotests' threads/lwmutex/refer recorded for both functions; one there isn't is
//LWMUTEX_NOTFOUND.
auto Kernel::lwMutexStatus(u32 uid, u32 address) -> void {
  auto found = lwMutexes.find(uid);
  if(found == lwMutexes.end()) return result(ErrorLwMutexNotFound);
  auto& mutex = found->second;
  u32 count = memory.read(4, mutex.workArea), waiting = 0;
  for(auto& [id, thread] : threads) {
    waiting += thread->status == Status::Waiting && thread->wait == Wait::LwMutex && thread->waitID == mutex.workArea;
  }
  Report info(64);
  info.name(4, mutex.name);
  info.word(36, mutex.attributes);
  info.word(40, uid);
  info.word(44, mutex.workArea);
  info.word(48, mutex.initial);
  info.word(52, count);
  info.word(56, count ? memory.read(4, mutex.workArea + 4) : 0xffff'ffff);
  info.word(60, waiting);
  report(address, info);
  result(0);
}

//(work area, info): the status of the mutex the work area holds the UID of.
auto Kernel::sceKernelReferLwMutexStatus() -> void {
  lwMutexStatus(memory.read(4, arg(0) + 16), arg(1));
}

//(UID, info)
auto Kernel::sceKernelReferLwMutexStatusByID() -> void {
  lwMutexStatus(arg(0), arg(1));
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
  memory.write(4, workArea + 16, 0xffff'ffff);  //(what sceKernelTryLockLwMutex_600 knows a deleted one by)
  result(0);
  reschedule();
}

//(work area, count, timeout); with callbacks, the thread's callbacks run while it waits. Only while it waits: the
//lightweight mutexes are Kernel_Library's, a user-mode library, whose lock takes a free (or its own) mutex without
//entering the kernel, so no callback can run there. Peace Walker counts on that: it locks one while holding a lock
//its power callback takes, and running the callback (notified as it was registered) there deadlocked it.
auto Kernel::lockLwMutex(bool callbacks) -> void {
  if(!mayWait() || !fromThread()) return;
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
  blockTimed(Wait::LwMutex, workArea, timeout, callbacks);
}

auto Kernel::sceKernelLockLwMutex() -> void {
  lockLwMutex(false);
}

auto Kernel::sceKernelLockLwMutexCB() -> void {
  lockLwMutex(true);
}

auto Kernel::sceKernelTryLockLwMutex() -> void {
  if(!fromThread()) return;
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

//(work area, count): sceKernelTryLockLwMutex_600, the newer firmware's try, as Kernel_Library has it in user mode: the
//lock taken in the work area alone, without asking the kernel, so a work area made by hand locks as well as one the
//kernel made. As pspautotests' threads/lwmutex/try600 recorded for both: a deleted one is LWMUTEX_NOTFOUND (its ID
//-1: sceKernelDeleteLwMutex), a count under 1 ILLEGAL_COUNT, and so is a count but 1 for a free mutex that isn't
//recursive; its holder trying again is LWMUTEX_RECURSIVE for one that isn't, LWMUTEX_LOCK_OVERFLOW past 2^31 - 1
//for one that is; held by another, LWMUTEX_LOCKED (where a PSP's older try answers MUTEX_LOCKED to everything,
//threads/lwmutex/try). God Eater 2 tries its locks so.
auto Kernel::sceKernelTryLockLwMutex_600() -> void {
  if(!fromThread()) return;
  u32 workArea = arg(0);
  s32 count = arg(1);
  if(memory.read(4, workArea + 16) == 0xffff'ffff) return result(ErrorLwMutexNotFound);
  if(count <= 0) return result(ErrorIllegalCount);
  s32 level = memory.read(4, workArea);
  u32 owner = memory.read(4, workArea + 4), attributes = memory.read(4, workArea + 8);
  bool recursive = attributes & 0x200;
  if(!level) {
    if(!recursive && count != 1) return result(ErrorIllegalCount);
    memory.write(4, workArea, count);
    memory.write(4, workArea + 4, current->uid);
    return result(0);
  }
  if(owner != current->uid) return result(ErrorLwMutexLocked);
  if(!recursive) return result(ErrorLwMutexRecursion);
  if(level > 0x7fff'ffff - count) return result(ErrorLwMutexOverflow);
  memory.write(4, workArea, level + count);
  result(0);
}

//Gives an unlocked mutex to the thread that has waited for it longest, or with attribute 0x100 to the best of them
//(the longest waiting among equals), as pspautotests' threads/scheduling/mutexhandoff recorded for both kinds of
//mutex.
auto Kernel::unlockLwMutex(u32 workArea) -> void {
  bool byPriority = memory.read(4, workArea + 8) & 0x100;
  auto before = [&](const Thread& a, const Thread& b) {
    if(byPriority && a.priority != b.priority) return a.priority < b.priority;
    return a.readySince < b.readySince;
  };
  Thread* next = nullptr;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != Wait::LwMutex || thread->waitID != workArea) continue;
    if(!next || before(*thread, *next)) next = thread.get();
  }
  if(!next) return;
  memory.write(4, workArea, next->waitCount);
  memory.write(4, workArea + 4, next->uid);
  memory.write(4, workArea + 12, memory.read(4, workArea + 12) - 1);
  ready(*next, 0);
}

auto Kernel::sceKernelUnlockLwMutex() -> void {
  if(!fromThread()) return;
  u32 workArea = arg(0), count = arg(1);
  if(!lwMutexes.count(memory.read(4, workArea + 16))) return result(ErrorLwMutexNotFound);
  if(s32(count) <= 0) return result(ErrorIllegalCount);
  u32 level = memory.read(4, workArea), owner = memory.read(4, workArea + 4);
  if(!level || owner != current->uid) return result(ErrorLwMutexUnlocked);
  if(count > level) return result(ErrorLwMutexUnderflow);
  result(0);
  memory.write(4, workArea, level - count);
  if(level - count) return;
  memory.write(4, workArea + 4, 0);
  unlockLwMutex(workArea);
  reschedule();
}

//The low 32 bits of the time since power on, in microseconds.
auto Kernel::sceKernelGetSystemTimeLow() -> void {
  result(u32(cycles / (CPUFrequency / 1'000'000)));
}

//(semaphore, info): a SceKernelSemaInfo (pspthreadman.h, 56 bytes): name, attributes, initial, current and largest
//count, how many threads wait; copied as far as its size word says, the size reading back as 56 (report():
//pspautotests' threads/semaphores/refer).
auto Kernel::sceKernelReferSemaStatus() -> void {
  auto found = semaphores.find(arg(0));
  if(found == semaphores.end()) return result(ErrorUnknownSemaphore);
  auto& semaphore = found->second;
  u32 waiting = 0;
  for(auto& [uid, thread] : threads) {
    waiting += thread->status == Status::Waiting && thread->wait == Wait::Semaphore && thread->waitID == arg(0);
  }
  Report info(56);
  info.name(4, semaphore.name);
  info.word(36, semaphore.attributes);
  info.word(40, semaphore.initial);
  info.word(44, semaphore.count);
  info.word(48, semaphore.maximum);
  info.word(52, waiting);
  report(arg(1), info);
  result(0);
}

//(thread, priority): a new priority for a thread, as pspautotests' threads/threads/change found it on a PSP. A user
//thread's priorities run from 0x08 to 0x77 (those above and below are the kernel's: ILLEGAL_PRIORITY), and 0 stands
//for the caller's own; thread 0 is the caller. A thread not started yet, or ended, can't be changed (DORMANT); one
//ready, waiting or suspended can. The thread goes to the back of its new priority's line: one that's ready goes in
//behind those ready already, and so does the caller, which so gives way to any other thread of its priority (even
//when its priority doesn't change). A thread that ends up above the caller's takes over at once. With interrupts or
//dispatching held off the caller keeps the CPU all the same (reschedule()), its new priority counting once they're
//back. The test doesn't show which comes first, a bad priority or a bad thread: the priority is checked first here.
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

//(thread): what a dormant thread ended with (DORMANT, one never started); one still going has none yet. Thread 0 is
//no thread here, the caller's own ID asked for (pspautotests' threads/threads/exitstatus: UNKNOWN_THID for 0,
//NOT_DORMANT for the caller).
auto Kernel::sceKernelGetThreadExitStatus() -> void {
  auto thread = arg(0) ? findThread(arg(0)) : nullptr;
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

//How much room is left on the calling thread's stack: from its stack pointer down to the stack's bottom. On a PSP
//(threads/threads/stackfree), a thread of a 4 KiB stack checking from a function of its own with no room of its own
//has 0xeb0 left, one holding 1 KiB there 0xab0: the 0x140 a new thread starts below its top (startThread()), the
//function's 0x10, and 0x400. A stack pointer outside the stack (chosen: an overrun stack, or a call from an
//interrupt handler, which has no thread's) is 0.
auto Kernel::sceKernelCheckThreadStack() -> void {
  if(!current || interrupting) return result(0);
  u32 sp = cpu.ipu.r[29], bottom = current->stackBlock;
  result(sp >= bottom && sp - bottom <= current->stackSize ? sp - bottom : 0);
}

//(size, function, argument): the function called on the calling thread with a stack lent for the call, its stack
//pointer at the lent stack's top, and its result returned when it returns, the thread's own stack back then. As
//threads/threads/extend recorded: the thread's ID the same, its status giving the lent stack while the function runs
//(the size rounded up to 0x100: 640 and 768 bytes both 0x300) and its own again after, calls within calls, the
//function's result; under 512 bytes refused (ILLEGAL_STACK_SIZE), -1 bytes more than there is (NO_MEMORY); the stack
//taken from the top of the user partition, filled with 0xff bytes and the thread's ID at its bottom, as a new
//thread's (createThread()), k0 still pointing into the thread's own. Dragon Ball Z: Tenkaichi Tag Team calls into its
//game through it at boot. From an interrupt handler, refused (chosen).
auto Kernel::sceKernelExtendThreadStack() -> void {
  if(!current || interrupting) return result(ErrorIllegalContext);
  if(arg(0) < 0x200) return result(ErrorIllegalStackSize);
  auto block = allocate(arg(0), 1, 0, "stack: " + current->name);
  if(!block) return result(ErrorNoMemory);
  memory.fill(block->address, 0xff, block->size);
  memory.write(4, block->address, current->uid);
  u32 function = arg(1), argument = arg(2);
  auto& lent = current->extensions.emplace_back();
  save(lent.caller);
  lent.stack = current->stackBlock, lent.size = current->stackSize;
  current->stackBlock = block->address, current->stackSize = block->size;
  cpu.ipu.r[4] = argument;
  cpu.ipu.r[29] = block->address + block->size;
  cpu.ipu.r[31] = Trampoline + 48;
  cpu.ipu.pc = function;
  cpu.ipu.pd = function + 4;
}

//The trampoline's syscall after a function on a lent stack returns (its result in v0): the thread carries on where
//it called, its own stack back, the lent one freed.
auto Kernel::extendReturned() -> void {
  if(!current || current->extensions.empty()) return;
  u32 value = cpu.ipu.r[2];
  auto lent = current->extensions.back();
  current->extensions.pop_back();
  for(auto& block : blocks) if(block.address == current->stackBlock) { release(block.uid); break; }
  current->stackBlock = lent.stack, current->stackSize = lent.size;
  restore(lent.caller);
  cpu.ipu.r[2] = value;
}

//A thread that ends with stacks lent to it gives them back, innermost first: its stack is its own again.
auto Kernel::extensionsEnded(Thread& thread) -> void {
  while(!thread.extensions.empty()) {
    for(auto& block : blocks) if(block.address == thread.stackBlock) { release(block.uid); break; }
    thread.stackBlock = thread.extensions.back().stack, thread.stackSize = thread.extensions.back().size;
    thread.extensions.pop_back();
  }
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
//caller's own, the caller does, giving way to its equals, unless interrupts or dispatching are held off: then it
//keeps the CPU (reschedule()). A user thread's priorities (0x08-0x77) and 0 are taken, anything else is
//ILLEGAL_PRIORITY, as pspautotests' threads/threads/rotate recorded.
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
//switching was allowed, 0 if it was held off already. From an interrupt handler, ILLEGAL_CONTEXT. A function that
//waits is refused meanwhile, before it does anything (CAN_NOT_WAIT, as intr/waits recorded: mayWait()), and
//rotating the caller's line leaves it the CPU. pspsdk's pspthreadman.h names the functions; what they return is
//chosen (the pair works whichever way the state is read).
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
