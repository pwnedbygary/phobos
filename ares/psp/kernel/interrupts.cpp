//Calls into the program. Now and then the kernel has to run one of the program's own functions: the GE's callbacks,
//when a display list finishes or signals, the sub-interrupt handlers the program registers (below), and alarms' and
//virtual timers' handlers (timers.cpp). The PSP runs them as interrupt handlers, and so does this: the thread running
//is set aside exactly as it was (or the idle CPU, if none is), the function runs on the kernel's interrupt stack
//with its arguments in a0-a3, the global pointer it was registered with, and ra pointing at a trampoline; when it
//returns there, the CPU is put back and the thread carries on, none the wiser. Calls that come meanwhile wait their
//turn.
//
//A call starts when it can: at the end of the system function that caused it (the result already in v0), or as the
//kernel's loop goes round. Not while one is running, and not while the program holds interrupts off
//(sceKernelCpuSuspendIntr). A handler may call system functions but not wait in one (the PSP says
//ILLEGAL_CONTEXT); a thread it wakes runs once it has returned. (The PSP passes handlers (id, argument, list) this
//way: uOFW's sceKernelCallSubIntrHandler.)

auto Kernel::queueCall(u32 function, u32 gp, u32 a0, u32 a1, u32 a2, bool resumesGe) -> void {
  calls.push_back({function, gp, {a0, a1, a2, 0}, resumesGe, false, Call::Ge});
}

//The next call starts, if one may: none is running and interrupts aren't held off. A vertical blank held off till
//now comes first: its handlers join the queue, once however many blanks went by (vblankInterrupt()); unless the
//last blank's still wait their turn there, when it stays pending. A thread it interrupts counts it in its run
//figures, its time stopping while the call runs (threads.cpp).
auto Kernel::startCall() -> void {
  if(interrupting || !interruptsEnabled) return;
  if(vblankPending && !vblankQueued()) {
    vblankPending = false;
    queueVblankHandlers();
  }
  if(calls.empty()) return;
  auto call = calls.front();
  calls.pop_front();
  if(current) {
    current->runCycles += cycles - ranSince;
    current->interruptPreempts++;
    ranSince = cycles;  //(a handler that ends the game switches away from it, which mustn't count this again)
  }
  save(interrupted);
  interruptedHalted = cpu.scc.halted;
  interrupting = true;
  callResumesGe = call.resumesGe;
  callKind = call.kind;
  callID = call.id;
  if(call.kind == Call::VTimer) vtimerClocks(call.id);
  for(u32 n = 0; n < 4; n++) cpu.ipu.r[4 + n] = call.arguments[n];
  cpu.ipu.r[28] = call.gp;
  cpu.ipu.r[29] = InterruptStack - 16;
  cpu.ipu.r[31] = Trampoline + 8;  //its syscall comes back here, to callReturned()
  cpu.ipu.pc = call.function;
  cpu.ipu.pd = call.function + 4;
  cpu.scc.halted = 0;
}

//The trampoline's syscall: the function returned. The CPU goes back as it was, interrupts on as the call found them
//(calls start only with interrupts on: a handler that held them off and returned doesn't pass that on to the thread
//it interrupted, as on a PSP, where the interrupted context's state comes back with it); a timer's handler has what
//it returned say when it's called again (timers.cpp); the GE goes on if it was waiting for this; the next call
//waiting starts, or, if a thread woke meanwhile, the scheduler picks who runs.
auto Kernel::callReturned() -> void {
  if(!interrupting) return;
  u32 returned = cpu.ipu.r[2];
  restore(interrupted);
  cpu.scc.halted = interruptedHalted;
  interrupting = false;
  interruptsEnabled = true;
  ranSince = cycles;
  if(callKind == Call::Alarm || callKind == Call::VTimer) timerReturned(callKind, callID, returned);
  callKind = Call::Plain;
  callID = 0;
  if(callResumesGe) {
    callResumesGe = false;
    geSuspended = false;
    if(geFinishing >= 0) geEnded();
    geRun();
  }
  geInterrupt();
  startCall();
  if(!interrupting && rescheduleAfter) {
    rescheduleAfter = false;
    reschedule();
  }
}

//For a function that may wait, before anything else it does: false, with the error for the result, during a call
//into the program (ILLEGAL_CONTEXT), or with interrupts or dispatching held off (CAN_NOT_WAIT: no other thread could
//run meanwhile). pspautotests' intr/waits found a PSP refusing so, every function that waits, whether the call would
//have had to wait or not (a free lightweight mutex, an event flag's bits set already, a thread that has ended), and
//before looking at what it waits on (an ID that isn't one); the few arguments it checks ahead of that, its callers
//check first.
auto Kernel::mayWait() -> bool {
  if(interrupting) return result(ErrorIllegalContext), false;
  if(dispatchSuspended || !interruptsEnabled) return result(ErrorCanNotWait), false;
  return true;
}

//For a function that makes the calling thread a mutex's holder, or takes its locks off (trying a lock, unlocking,
//cancelling to a count, making one held), before anything else it does: false, with ILLEGAL_CONTEXT for the result,
//in a call into the program, or with no thread running (a test calling directly). There's no thread to hold it: an
//interrupt handler's lock would leave the mutex held by nobody, which no thread could unlock, or by whatever thread
//it interrupted, which never took it. pspautotests record only the waiting locks in a handler (intr/waits:
//ILLEGAL_CONTEXT, before the mutex is looked at); these are refused alike, as the PSP's other thread functions are
//in a handler (vtimers/interrupt's create and delete).
auto Kernel::fromThread() -> bool {
  if(interrupting || !current) return result(ErrorIllegalContext), false;
  return true;
}

//Interrupts held off (sceKernelCpuSuspendIntr): the CPU's own interrupt flag, which user code reads and sets with
//mfic and mtic, as pspautotests' intr/mfic recorded (1 as a program starts; only its lowest bit counts: resuming
//with 2 leaves them off). interruptsEnabled is that very flag (cpu.scc.interrupts), so the program's own mfic and
//mtic and these functions see and set the same one. With no interrupt, nothing can take the CPU from the running
//thread: the vertical blank's handlers and the GE's callbacks wait, and so do threads whose waits end meanwhile
//(reschedule()), as the timer's and the sound DMA's interrupts are what would have woken them. Nor may the thread
//wait itself (mayWait()). So the flag goes with the thread that cleared it: a thread taking the CPU runs with its
//own, which is on (switchTo()), and a handler's comes back on as it returns (callReturned()). Brave Story holds
//interrupts off around its own lock; its sound thread, taking the CPU as a buffer ended in there, found them off,
//every blocking output refused, and spun for good at the top priority. Turned back on by the program's own mtic
//rather than by sceKernelCpuResumeIntr, what waited comes at the kernel's next look, not at once: the calls held
//back as its next system call ends or the next thing comes due, a better thread made ready meanwhile at the
//scheduler's next pick (the next thread woken, or the holder's next wait).

//Holds interrupts off, returning whether they were on before (what ResumeIntr takes back).
auto Kernel::sceKernelCpuSuspendIntr() -> void {
  result(interruptsEnabled);
  interruptsEnabled = false;
}

//Turns interrupts back on, or keeps them off, as the flag says. Back on, what waited for them comes now: the calls
//into the program held back, then the thread the scheduler picks. (Kernel_Library's sceKernelCpuResumeIntrWithSync
//is listed as this.)
auto Kernel::sceKernelCpuResumeIntr() -> void {
  bool was = interruptsEnabled;
  interruptsEnabled = arg(0) & 1;
  result(0);
  if(interruptsEnabled && !was) {
    startCall();
    reschedule();
  }
}

//Sub-interrupt handlers. A PSP interrupt's own handler may share the interrupt out among sub-interrupts, numbered
//from 0, each with a handler and an argument of its own. A program may hang its handlers on two interrupts alone: the
//GE's (25) and the vertical blank's (30). The rest, below 67 (the PSP's interrupt numbers stop there), are of three
//kinds, which decide the errors registering and releasing get there. pspautotests' intr/registersub and
//intr/releasesub recorded them on a PSP (firmware 6.61, under PSPLink, whose own drivers may have a hand in which
//interrupts have handlers), and the table restates them by number; the names are pspsdk's (pspintrman.h).
enum class SubInterrupts : u8 {
  None,     //no handler at all: nothing to register on or release (NOTFOUND_HANDLER)
  Single,   //a handler without sub-interrupts: every sub number is out of range (ILLEGAL_INTRCODE)
  Kernel,   //sub-interrupts of the kernel's: registering is refused (ILLEGAL_INTRCODE), releasing finds none of the
            //program's (NOTFOUND_HANDLER)
  Program,  //the GE's and the vertical blank's
};

//Interrupts 0-66 by number, ten to a line: n none, s single, k the kernel's, p the program's.
static constexpr char subInterruptTable[] =
  "nnnnknksnn"  // 0- 9: 4 GPIO, 5 ATA, 6 UMD, 7 memory stick, 8 WLAN
  "snsnnsssss"  //10-19: 10 audio, 12 I2C, 14 SIRCS, 15-18 the system timers, 19 a thread interrupt
  "skssspsnnn"  //20-29: 20 NAND, 21 DMACplus, 22-23 DMA, 24 MEMLMD, 25 the GE
  "psnnnnsnnn"  //30-39: 30 the vertical blank, 31 MECODEC, 36 the headphone remote
  "nnnnnnnnnn"  //40-49
  "snnnnnssss"  //50-59
  "ssnnnsn";    //60-66: 60-61 memory stick, 65 a thread interrupt, 66 the interrupt manager's
static_assert(sizeof(subInterruptTable) == 67 + 1);

static auto subInterruptKind(u32 interrupt) -> SubInterrupts {
  switch(subInterruptTable[interrupt]) {
  case 's': return SubInterrupts::Single;
  case 'k': return SubInterrupts::Kernel;
  case 'p': return SubInterrupts::Program;
  default:  return SubInterrupts::None;
  }
}

//The program's 32 sub-interrupts on an interrupt it may use (the vertical blank's or the GE's); null for any other.
auto Kernel::subHandlers(u32 interrupt) -> SubHandler* {
  if(interrupt == 30) return vblankSubs;
  if(interrupt == 25) return geSubs;
  return nullptr;
}

//On the vertical blank's interrupt, the program's sub-interrupts are 0-15. Of the rest, the display driver holds
//18-20 and 24-26 (registering there finds a handler already: FOUND_HANDLER), and 16, 17, 21-23 and 27-31 can't be
//registered at all (ILLEGAL_INTRCODE). The GE's were only tried at 0 (accepted): all 32 are the program's here.
static auto vblankSubHeld(u32 sub) -> bool {
  return (sub >= 18 && sub <= 20) || (sub >= 24 && sub <= 26);
}

//(interrupt, sub-interrupt, handler, argument): the handler is called (sub-interrupt, argument) when the interrupt
//comes, with the caller's global pointer, once it's enabled (sceKernelEnableSubIntr, before or after). Numbers are
//unsigned (-1 is huge). Checked in this order: the interrupt (67 and up: ILLEGAL_INTRCODE; then its kind), the
//sub-interrupt (32 and up: ILLEGAL_INTRCODE; then the vertical blank's own rules), a handler there already
//(FOUND_HANDLER). A null handler takes no place: registering one where there's none returns 0 and leaves it empty,
//and a real one may be registered after it; over a real one, it finds that one (FOUND_HANDLER), like any other.
auto Kernel::sceKernelRegisterSubIntrHandler() -> void {
  u32 interrupt = arg(0), sub = arg(1), function = arg(2), argument = arg(3);
  if(interrupt >= 67) return result(ErrorIllegalInterruptCode);
  switch(subInterruptKind(interrupt)) {
  case SubInterrupts::None: return result(ErrorHandlerNotFound);
  case SubInterrupts::Single: case SubInterrupts::Kernel: return result(ErrorIllegalInterruptCode);
  case SubInterrupts::Program: break;
  }
  if(sub >= 32) return result(ErrorIllegalInterruptCode);
  if(interrupt == 30 && sub >= 16) return result(vblankSubHeld(sub) ? ErrorHandlerFound : ErrorIllegalInterruptCode);
  auto& handler = subHandlers(interrupt)[sub];
  if(handler.function) return result(ErrorHandlerFound);
  if(function) {
    handler.function = function;
    handler.argument = argument;
    handler.gp = cpu.ipu.r[28];
  }
  result(0);
}

//(interrupt, sub-interrupt): the program's handler there goes, and the sub-interrupt is disabled with it: registered
//again, it doesn't run until it's enabled again. The interrupt is checked as for registering, except that the
//kernel's sub-interrupts (4, 6, 21) have none of the program's to release (NOTFOUND_HANDLER); then the sub-interrupt
//(32 and up: ILLEGAL_INTRCODE), and whether the program has a handler there (NOTFOUND_HANDLER, as on the vertical
//blank's 16-31, which can't have one).
auto Kernel::sceKernelReleaseSubIntrHandler() -> void {
  u32 interrupt = arg(0), sub = arg(1);
  if(interrupt >= 67) return result(ErrorIllegalInterruptCode);
  switch(subInterruptKind(interrupt)) {
  case SubInterrupts::None: case SubInterrupts::Kernel: return result(ErrorHandlerNotFound);
  case SubInterrupts::Single: return result(ErrorIllegalInterruptCode);
  case SubInterrupts::Program: break;
  }
  if(sub >= 32) return result(ErrorIllegalInterruptCode);
  auto& handler = subHandlers(interrupt)[sub];
  if(!handler.function) return result(ErrorHandlerNotFound);
  handler = {};
  result(0);
}

//(interrupt, sub-interrupt), enabled or disabled: its handler runs when the interrupt comes only while it's enabled.
//Registered or not makes no difference: enabling first and registering after works (pspautotests' intr/enablesub),
//and either call on a sub-interrupt with no handler returns 0. An interrupt of 67 and up, or a sub-interrupt of 32
//and up, is refused (ILLEGAL_INTRCODE). Interrupts other than the vertical blank's weren't tried on a PSP: here an
//interrupt with no handler gets NOTFOUND_HANDLER, one without sub-interrupts ILLEGAL_INTRCODE, and the kernel's
//sub-interrupts 0, leaving them as they are.
static auto subInterruptRefused(u32 interrupt, u32 sub) -> u32 {
  if(interrupt >= 67) return Kernel::ErrorIllegalInterruptCode;
  switch(subInterruptKind(interrupt)) {
  case SubInterrupts::None: return Kernel::ErrorHandlerNotFound;
  case SubInterrupts::Single: return Kernel::ErrorIllegalInterruptCode;
  case SubInterrupts::Kernel: case SubInterrupts::Program: break;
  }
  return sub >= 32 ? Kernel::ErrorIllegalInterruptCode : 0;
}

auto Kernel::sceKernelEnableSubIntr() -> void {
  if(auto refused = subInterruptRefused(arg(0), arg(1))) return result(refused);
  if(auto handlers = subHandlers(arg(0))) handlers[arg(1)].enabled = true;
  result(0);
}

auto Kernel::sceKernelDisableSubIntr() -> void {
  if(auto refused = subInterruptRefused(arg(0), arg(1))) return result(refused);
  if(auto handlers = subHandlers(arg(0))) handlers[arg(1)].enabled = false;
  result(0);
}

//A vertical blank's interrupt (events() at each blank). Its handlers run as calls into the program, now; but while
//they can't (interrupts held off, a call running, or the last blank's handlers still waiting their turn), the
//interrupt waits, as the PSP's interrupt controller keeps an interrupt pending: once. More blanks meanwhile add
//nothing to it, and when it's let through, each handler then registered and enabled runs once: a program that held
//interrupts off for 600 blanks gets one call of each, not 600, and handlers that take longer than a frame run back
//to back, the queue never holding more than one blank's.
auto Kernel::vblankInterrupt() -> void {
  if(interrupting || !interruptsEnabled || vblankQueued()) {
    vblankPending = true;
    return;
  }
  queueVblankHandlers();
}

//The vertical blank's handlers join the calls into the program: each registered and enabled one, by its number,
//called (its number, its argument) with the global pointer it was registered with.
auto Kernel::queueVblankHandlers() -> void {
  for(u32 sub = 0; sub < 32; sub++) {
    auto& handler = vblankSubs[sub];
    if(!handler.function || !handler.enabled) continue;
    calls.push_back({handler.function, handler.gp, {sub, handler.argument, 0, 0}, false, true});
  }
}

//Whether a vertical blank's handlers wait their turn among the calls into the program.
auto Kernel::vblankQueued() const -> bool {
  return std::any_of(calls.begin(), calls.end(), [](const Call& call) { return call.vblank; });
}

//Whether a handler runs at the vertical blanks to come: then the threads waiting on what it does aren't stuck (time
//goes on for it while every thread waits).
auto Kernel::vblankHandlers() const -> bool {
  for(auto& handler : vblankSubs) if(handler.function && handler.enabled) return true;
  return false;
}
