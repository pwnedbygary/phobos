//The kernel tests' machine: the system with the HLE kernel, collecting what the program writes and what the kernel
//notes, plus a small assembler for test programs that call system functions.
#pragma once

#include "elf.hpp"
#include "../../ares/psp/kernel/kernel.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::Kernel;

//A machine with the kernel: what the program writes and what the kernel notes are collected. Test programs call
//system functions through stubs (jr ra; syscall code) at Stubs, one per function, as the loader would make them.
struct KernelMachine {
  static constexpr u32 Stubs = 0x0890'0000, Strings = 0x0891'0000, Results = 0x0892'0000;
  System system;
  Kernel kernel{system, system.memory};
  std::string output;
  std::vector<std::string> notes;
  std::vector<std::string> stubbed;
  u32 nextString = Strings;

  KernelMachine() {
    kernel.output = [this](const std::string& text) { output += text; };
    kernel.log = [this](const std::string& text) { notes.push_back(text); };
    kernel.power();
  }

  auto stub(const char* name) -> u32 {
    for(u32 n = 0; n < stubbed.size(); n++) if(stubbed[n] == name) return Stubs + n * 8;
    u32 address = Stubs + stubbed.size() * 8;
    stubbed.push_back(name);
    system.memory.write(4, address, jr(ra));
    system.memory.write(4, address + 4, syscall(kernel.importCode("test", Kernel::nid(name))));
    return address;
  }

  auto string(const std::string& text) -> u32 {
    u32 address = nextString;
    system.memory.copyIn(address, text.c_str(), text.size() + 1);
    nextString += (text.size() + 4) & ~3u;
    return address;
  }

  //Calls a function directly, as if from a program: arguments in a0-a3 and t0-t3; returns v0.
  auto call(const char* name, std::initializer_list<u32> arguments) -> u32 {
    u32 n = 0;
    for(u32 value : arguments) system.ipu.r[n < 4 ? 4 + n : 8 + n - 4] = value, n++;
    kernel.syscall(kernel.importCode("test", Kernel::nid(name)));
    return system.ipu.r[2];
  }

  //Starts a program at entry as the first thread (priority 0x20), and runs it (for up to a third of a second of the
  //PSP's time, in cycles).
  auto runProgram(u32 entry, bool recompile, u64 budget = Kernel::CPUFrequency / 3) -> void {
    system.recompiler.enabled = recompile;
    system.power(entry);
    s32 uid = kernel.createThread("main", entry, 0x20, 0x4000, 0, 0);
    kernel.startThread(*kernel.threads[uid], 0, 0);
    kernel.run(budget);
  }
};

//Writes a program a word at a time; calls go through the machine's stubs.
struct Assembler {
  KernelMachine& m;
  u32 address;
  auto here() const -> u32 { return address; }
  auto put(u32 word) -> void { m.system.memory.write(4, address, word); address += 4; }
  auto li(u32 r, u32 value) -> void { put(lui(r, value >> 16)); put(ori(r, r, value & 0xffff)); }
  auto call(const char* name) -> void { put(jal(m.stub(name))); put(nop); }
  auto print(const std::string& text) -> void {
    li(a0, 1); li(a1, m.string(text)); li(a2, text.size()); call("sceIoWrite");
  }
};

}
