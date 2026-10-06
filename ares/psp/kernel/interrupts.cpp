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
