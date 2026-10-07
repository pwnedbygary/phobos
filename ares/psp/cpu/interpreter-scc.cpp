//Coprocessor 0 (system control) and the Allegrex's own system instructions.
//
//On a real PSP these belong to the kernel: exception handling, interrupts, power saving. Under HLE the game runs in
//user mode and the kernel is Phobos's own code, so most of this only needs to remember what was written.

//Return from exception: continue at the address in EPC (register 14), where the exception happened.
auto Allegrex::ERET() -> void {
  ipu.pc = scc.r[14];
  ipu.pd = ipu.pc + 4;
}

//Waits for an interrupt; the kernel's idle thread runs it to save power. The CPU stops here (run() returns) until
//the HLE kernel clears halted because something is ready to run.
auto Allegrex::HALT() -> void {
  scc.halted = 1;
}

auto Allegrex::MFC0(u32& rt, u8 rd) -> void {
  rt = scc.r[rd];
}

//Reads the interrupt flag (1 on, 0 held off); mfic and mtic are how the kernel turns interrupts off around code
//that mustn't be interrupted, then back on as they were (a PSP's sceKernelCpuSuspendIntr is mfic then mtic of 0, and
//sceKernelCpuResumeIntr an mtic, as pspautotests' intr/mfic notes).
auto Allegrex::MFIC(u32& rt) -> void {
  rt = scc.interrupts;
}

auto Allegrex::MTC0(cu32& rt, u8 rd) -> void {
  scc.r[rd] = rt;
}

//Sets the interrupt flag from a register's lowest bit alone: intr/mfic read 0 back after mtic of 2 and of 0x80000000.
auto Allegrex::MTIC(cu32& rt) -> void {
  scc.interrupts = rt & 1;
}
