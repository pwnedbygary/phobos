//What happens when the CPU can't go on with an instruction.
//
//On a real PSP an exception jumps into the kernel's handler. Under HLE there's no kernel code to jump to, and a game
//running correctly shouldn't raise any (syscalls go to syscallHook first), so an exception means a bug or something
//not implemented yet: it goes to exceptionHook with the address of the instruction that raised it, or, without a
//hook, the CPU halts there.
auto Allegrex::exception(Exception code) -> void {
  if(exceptionHook) return exceptionHook(code, pipeline.address);
  scc.halted = 1;
}

//An address that isn't aligned to its access size; BadVAddr (coprocessor 0 register 8) remembers which.
auto Allegrex::addressError(Exception code, u32 address) -> void {
  scc.r[8] = address;
  exception(code);
}
