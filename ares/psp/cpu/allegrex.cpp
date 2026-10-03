#include "allegrex.hpp"

namespace ares::PlayStationPortable {

#include "interpreter.cpp"
#include "interpreter-ipu.cpp"
#include "interpreter-scc.cpp"
#include "interpreter-fpu.cpp"
#include "exceptions.cpp"

//Clears every register and starts the program at entry (the game's entry point, as the loader finds it).
auto Allegrex::power(u32 entry) -> void {
  ipu = {};
  fpu = {};
  scc = {};
  pipeline = {};
  ipu.pc = entry;
  ipu.pd = entry + 4;
}

//Runs one instruction: fetch it, move the program counter along, then do what it says. The program counter moves
//before the instruction runs, so a branch only has to point pd somewhere else (see IPU in allegrex.hpp).
auto Allegrex::instruction() -> void {
  pipeline.address = ipu.pc;
  if(pipeline.address & 3) return addressError(Exception::AddressLoad, pipeline.address);
  pipeline.instruction = read(Word, pipeline.address);
  ipu.pc = ipu.pd;
  ipu.pd += 4;
  decoderEXECUTE();
  ipu.r[0] = 0;  //instructions may write r0, but it always reads as zero
}

//Runs up to the given number of instructions, stopping early if the CPU halts; returns how many ran.
auto Allegrex::run(u64 instructions) -> u64 {
  u64 executed = 0;
  while(executed < instructions && !scc.halted) {
    instruction();
    executed++;
  }
  return executed;
}

}
