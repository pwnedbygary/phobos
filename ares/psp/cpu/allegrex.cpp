#include "allegrex.hpp"

namespace ares::PlayStationPortable {

#include "interpreter.cpp"
#include "interpreter-ipu.cpp"
#include "interpreter-scc.cpp"
#include "interpreter-fpu.cpp"
#include "interpreter-vfpu.cpp"
#include "exceptions.cpp"
#include "recompiler.cpp"
#include "recompiler-ipu.cpp"
#include "recompiler-memory.cpp"
#include "serialization.cpp"

//Clears every register and starts the program at entry (the game's entry point, as the loader finds it).
auto Allegrex::power(u32 entry) -> void {
  ipu = {};
  fpu = {};
  vfpu = {};
  vfpu.pfxs = vfpu.pfxt = PrefixIdentity;  //the prefixes start out doing nothing
  for(u32 n : range(8)) vfpu.rcx[n] = 0x3f80'0000 | (n < 4 ? 1 << n : 0);  //the random number generator's start
  scc = {};
  scc.interrupts = 1;  //on, as a program finds them (pspautotests' intr/mfic read 1 first)
  pipeline = {};
  ipu.pc = entry;
  ipu.pd = entry + 4;
  recompiler.reset();
}

//Runs one instruction: fetch it, move the program counter along, then do what it says. The program counter moves
//before the instruction runs, so a branch only has to point pd somewhere else (see IPU in allegrex.hpp).
auto Allegrex::instruction() -> void {
  u32 address = ipu.pc;
  if(address & 3) {
    pipeline.address = address;
    return addressError(Exception::AddressLoad, address);
  }
  u32 word = read(Word, address);
  ipu.pc = ipu.pd;
  ipu.pd += 4;
  execute(address, word);
}

//Does what one instruction word says, once pc and pd have moved past it. The interpreter comes here for every
//instruction, and compiled code for each instruction it has no native version of, so both run the same code.
//Returns whether the instruction raised an exception.
auto Allegrex::execute(u32 address, u32 instruction) -> u32 {
  pipeline.address = address;
  pipeline.instruction = instruction;
  pipeline.exception = 0;
  decoderEXECUTE();
  ipu.r[0] = 0;  //instructions may write r0, but it always reads as zero
  return pipeline.exception;
}

//Runs at least the given number of instructions, stopping early if the CPU halts, or once it reaches a runLimit a
//syscall brought forward; returns how many ran. With the recompiler, whole blocks run at a time, so it may run a few
//more than asked.
auto Allegrex::run(u64 instructions) -> u64 {
  instructionsRun = 0;
  runLimit = instructions;
  while(instructionsRun < runLimit && !scc.halted) {
    if(recompiler.enabled) {
      instructionsRun += recompiler.run();
    } else {
      instruction();
      instructionsRun++;
    }
  }
  return instructionsRun;
}

}
