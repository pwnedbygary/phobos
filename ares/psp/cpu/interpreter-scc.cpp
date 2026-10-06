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

//Reads the interrupt enable state; mfic and mtic are how the kernel turns interrupts off around code that mustn't
//be interrupted, then back on as they were.
auto Allegrex::MFIC(u32& rt) -> void {
  rt = scc.interrupts;
}

auto Allegrex::MTC0(cu32& rt, u8 rd) -> void {
  scc.r[rd] = rt;
}

auto Allegrex::MTIC(cu32& rt) -> void {
  scc.interrupts = rt;
}
