//The PSP system tests' machine: the Allegrex with the real memory map (ares/psp/memory) behind it, as the PSP
//system has it, and the GE, plus the Allegrex tests' instruction encoders and checks.
#pragma once

#include "../allegrex/harness.hpp"
#include "../../ares/psp/memory/memory.hpp"
#include "../../ares/psp/ge/ge.hpp"

//Inside the Allegrex tests' namespace, so its register names (s0, s1...) hide ares's integer types of the same
//names.
namespace allegrex_test::psp {

using ares::PlayStationPortable::Memory;
using ares::PlayStationPortable::GE;

struct System : Allegrex {
  Memory memory;
  GE ge{memory};
  std::vector<u8*> table;
  std::vector<std::pair<u32, bool>> unmapped;  //accesses with nothing behind them: the address, and whether a store
  std::vector<std::pair<Exception, u32>> exceptions;

  System(u32 ramSize = 32_MiB) {
    memory.power(ramSize);
    memory.buildPages(table);
    pages = table.data();
    memory.written = [this](u32 address, u32 size) { recompiler.invalidateRange(address, size); };
    memory.unmapped = [this](u32 address, bool store) { unmapped.push_back({address, store}); };
    exceptionHook = [this](Exception exception, u32 address) {
      exceptions.push_back({exception, address});
      scc.halted = 1;
    };
  }

  auto read(u32 size, u32 address) -> u32 override { return memory.read(size, address); }
  auto write(u32 size, u32 address, u32 data) -> void override { memory.write(size, address, data); }

  //Places the code at address with a halt after it, and runs it on the engine picked until it halts.
  auto runProgram(u32 address, const std::vector<u32>& code, bool recompile) -> void {
    for(u32 i = 0; i < code.size(); i++) memory.write(Word, address + i * 4, code[i]);
    memory.write(Word, address + code.size() * 4, halt);
    recompiler.enabled = recompile;
    power(address);
    run(100000);
    if(!scc.halted) std::fprintf(stderr, "program didn't halt\n");
  }
};

auto memoryTests() -> Tests;
auto loaderTests() -> Tests;
auto kernelTests() -> Tests;
auto callbackTests() -> Tests;
auto powerTests() -> Tests;
auto audioTests() -> Tests;
auto fileTests() -> Tests;
auto geTests() -> Tests;
auto drawTests() -> Tests;
auto draw3dTests() -> Tests;
auto measureTests() -> Tests;
auto discTests() -> Tests;
auto discFormatTests() -> Tests;
auto stateTests() -> Tests;

}
