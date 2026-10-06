//Saving and loading the CPU's state, for save states: every register a program can see (the integer unit's, the
//FPU's, the VFPU's with its prefixes and its random number generator, coprocessor 0's) and whether it's halted.
//Compiled code isn't saved: whoever loads a state starts the recompiler afresh (Recompiler::reset()), since memory
//changed under the code compiled from it.
auto Allegrex::serialize(serializer& s) -> void {
  s(ipu.r);
  s(ipu.lo);
  s(ipu.hi);
  s(ipu.pc);
  s(ipu.pd);
  s(fpu.r);
  s(fpu.csr);
  s(scc.r);
  s(scc.interrupts);
  s(scc.halted);
  s(vfpu.r);
  s(vfpu.pfxs);
  s(vfpu.pfxt);
  s(vfpu.pfxd);
  s(vfpu.cc);
  s(vfpu.rcx);
}
