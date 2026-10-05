//Calls into the program. Now and then the kernel has to run one of the program's own functions: the GE's callbacks,
//when a display list finishes or signals (later, interrupt handlers the program registers). The PSP runs them as
//interrupt handlers, and so does this: the thread running is set aside exactly as it was (or the idle CPU, if none
//is), the function runs on the kernel's interrupt stack with its arguments in a0-a2, the global pointer it was
//registered with, and ra pointing at a trampoline; when it returns there, the CPU is put back and the thread carries
//on, none the wiser. Calls that come meanwhile wait their turn.
//
//A call starts when it can: at the end of the system function that caused it (the result already in v0), or as the
//kernel's loop goes round. Not while one is running, and not while the program holds interrupts off
//(sceKernelCpuSuspendIntr). A handler may call system functions but not wait in one (the PSP says
//ILLEGAL_CONTEXT); a thread it wakes runs once it has returned. (The PSP passes handlers (id, argument, list) this way:
//uOFW's sceKernelCallSubIntrHandler.)

auto Kernel::queueCall(u32 function, u32 gp, u32 a0, u32 a1, u32 a2, bool resumesGe) -> void {
  calls.push_back({function, gp, {a0, a1, a2}, resumesGe});
}

auto Kernel::startCall() -> void {
  if(interrupting || !interruptsEnabled || calls.empty()) return;
  auto call = calls.front();
  calls.pop_front();
  save(interrupted);
  interruptedHalted = cpu.scc.halted;
  interrupting = true;
  callResumesGe = call.resumesGe;
  for(u32 n = 0; n < 3; n++) cpu.ipu.r[4 + n] = call.arguments[n];
  cpu.ipu.r[28] = call.gp;
  cpu.ipu.r[29] = InterruptStack - 16;
  cpu.ipu.r[31] = Trampoline + 8;  //its syscall comes back here, to callReturned()
  cpu.ipu.pc = call.function;
  cpu.ipu.pd = call.function + 4;
  cpu.scc.halted = 0;
}

//The trampoline's syscall: the function returned. The CPU goes back as it was; the GE goes on if it was waiting for
//this; the next call waiting starts, or, if a thread woke meanwhile, the scheduler picks who runs.
auto Kernel::callReturned() -> void {
  if(!interrupting) return;
  restore(interrupted);
  cpu.scc.halted = interruptedHalted;
  interrupting = false;
  if(callResumesGe) {
    callResumesGe = false;
    geSuspended = false;
    if(geFinishing >= 0) geEnded();
    geRun();
  }
  startCall();
  if(!interrupting && rescheduleAfter) {
    rescheduleAfter = false;
    reschedule();
  }
}

//For a function that may wait: false, with the error for the result, during a call into the program.
auto Kernel::mayWait() -> bool {
  if(!interrupting) return true;
  result(ErrorIllegalContext);
  return false;
}

//Holds calls into the program back, returning whether they were let through before (what ResumeIntr takes back).
auto Kernel::sceKernelCpuSuspendIntr() -> void {
  result(interruptsEnabled);
  interruptsEnabled = false;
}

auto Kernel::sceKernelCpuResumeIntr() -> void {
  interruptsEnabled = arg(0) != 0;
  result(0);
}

//Handlers a program registers for an interrupt's sub-interrupts (InterruptManager): of those a program may use, the
//vertical blank's (interrupt 30; its sub-interrupts 0-15, the rest the kernel's) and the GE's (25). Each enabled one
//is called at its interrupt as fn(sub-interrupt, argument), with the global pointer it was registered with, the way
//calls into the program go (above). The vertical blank's run at each blank, in their numbers' order; nothing here
//raises the GE's yet (its driver calls the program's GE callbacks itself). The rules, and the errors in their order,
//are PPSSPP's reading of interruptman.prx (from tests on a PSP).

//Which of the two a number is (0 the GE's, 1 the vertical blank's), or -1: the PSP has handlers for some other
//interrupts, but none a program may add to (for those it says ILLEGAL_INTRCODE), and none for the rest (NOTFOUND).
static auto subInterruptSet(u32 interrupt, u32& error) -> s32 {
  if(interrupt >= 67) return error = Kernel::ErrorIllegalInterruptCode, -1;
  if(interrupt == 25) return 0;
  if(interrupt == 30) return 1;
  static constexpr u8 KernelOnly[] = {4, 6, 21, 7, 10, 12, 15, 16, 17, 18, 19, 20, 22, 23, 24, 26, 31, 36, 50, 56,
                                      57, 58, 59, 60, 61, 65};
  bool kernels = std::find(std::begin(KernelOnly), std::end(KernelOnly), interrupt) != std::end(KernelOnly);
  error = kernels ? Kernel::ErrorIllegalInterruptCode : Kernel::ErrorHandlerNotFound;
  return -1;
}

//(interrupt, sub-interrupt, handler, argument)
auto Kernel::sceKernelRegisterSubIntrHandler() -> void {
  u32 interrupt = arg(0), sub = arg(1), error = 0;
  s32 set = subInterruptSet(interrupt, error);
  if(set < 0) return result(error);
  if(sub >= 32) return result(ErrorIllegalInterruptCode);
  if(interrupt == 30 && ((sub >= 18 && sub <= 20) || (sub >= 24 && sub <= 26))) return result(ErrorHandlerFound);
  if(interrupt == 30 && sub >= 16) return result(ErrorIllegalInterruptCode);
  auto& handler = subInterrupts[set][sub];
  if(handler.function) return result(ErrorHandlerFound);
  handler.function = arg(2);
  handler.argument = arg(3);
  handler.gp = cpu.ipu.r[28];
  result(0);
}

//(interrupt, sub-interrupt): the handler goes, and its sub-interrupt with it.
auto Kernel::sceKernelReleaseSubIntrHandler() -> void {
  u32 interrupt = arg(0), sub = arg(1), error = 0;
  s32 set = subInterruptSet(interrupt, error);
  if(set < 0) return result(error);
  if(sub >= 32) return result(ErrorIllegalInterruptCode);
  if(interrupt == 30 && sub >= 16) return result(ErrorHandlerNotFound);
  auto& handler = subInterrupts[set][sub];
  if(!handler.function) return result(ErrorHandlerNotFound);
  handler = {};
  result(0);
}

//(interrupt, sub-interrupt): lets its handler be called (even before one's registered: it's called once there is).
auto Kernel::sceKernelEnableSubIntr() -> void {
  u32 interrupt = arg(0), sub = arg(1), error = 0;
  s32 set = subInterruptSet(interrupt, error);
  if(set < 0 || sub >= 32) return result(ErrorIllegalInterruptCode);
  subInterrupts[set][sub].enabled = true;
  result(0);
}

auto Kernel::sceKernelDisableSubIntr() -> void {
  u32 interrupt = arg(0), sub = arg(1), error = 0;
  s32 set = subInterruptSet(interrupt, error);
  if(set < 0 || sub >= 32) return result(ErrorIllegalInterruptCode);
  subInterrupts[set][sub].enabled = false;
  result(0);
}

//The vertical blank's handlers, called as it starts.
auto Kernel::vblankInterrupt() -> void {
  for(u32 sub = 0; sub < 32; sub++) {
    auto& handler = subInterrupts[1][sub];
    if(handler.enabled && handler.function) queueCall(handler.function, handler.gp, sub, handler.argument, 0);
  }
}

//Whether any vertical blank handler will be called: time goes on for it while every thread waits.
auto Kernel::vblankHandlers() const -> bool {
  for(auto& handler : subInterrupts[1]) if(handler.enabled && handler.function) return true;
  return false;
}
