//The kernel tests' machine: the system with the HLE kernel, collecting what the program writes and what the kernel
//notes, plus a small assembler for test programs that call system functions.
#pragma once

#include "elf.hpp"
#include "../../ares/psp/kernel/kernel.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

namespace allegrex_test::psp {

using ares::PlayStationPortable::Kernel;

//A machine with the kernel: what the program writes and what the kernel notes are collected. Test programs call
//system functions through stubs (jr ra; syscall code) at Stubs, one per function, as the loader would make them.
struct KernelMachine {
  static constexpr u32 Stubs = 0x0890'0000, Strings = 0x0891'0000, Results = 0x0892'0000;
  System system;
  Kernel kernel{system, system.memory, system.ge};
  std::string output;
  std::vector<std::string> notes;
  std::vector<std::string> stubbed;
  u32 nextString = Strings;

  KernelMachine(u32 ramSize = 32_MiB) : system(ramSize) {
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

//The machine's state as the system saves it (memory, the CPU, the GE and the kernel); and loaded into a machine,
//false if it's refused.
inline auto saveState(KernelMachine& m) -> std::vector<u8> {
  serializer s;
  m.system.memory.serialize(s);
  m.system.serialize(s);
  m.system.ge.serialize(s);
  m.kernel.serialize(s);
  return {s.data(), s.data() + s.size()};
}

inline auto loadState(KernelMachine& m, const std::vector<u8>& state) -> bool {
  serializer s{state.data(), u32(state.size())};
  m.system.memory.serialize(s);
  m.system.serialize(s);
  bool valid = m.system.ge.serialize(s);
  return m.kernel.serialize(s) && valid;
}

//A save, a load and a save: the machine's state loads into a fresh machine with as much memory (prepare() gives it
//what the system would, the same devices and disc), which makes the very same state. False if it doesn't.
inline auto roundTrip(KernelMachine& m, const std::function<void(KernelMachine&)>& prepare = {}) -> bool {
  auto state = saveState(m);
  KernelMachine fresh(u32(m.system.memory.ram.size()));
  if(prepare) prepare(fresh);
  return loadState(fresh, state) && saveState(fresh) == state;
}

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

//A fresh host folder for a test, removed when the test ends.
struct HostFolder {
  std::filesystem::path path;
  HostFolder() {
    std::random_device random;
    path = std::filesystem::temp_directory_path() / ("phobos-psp-" + std::to_string(random()));
    std::filesystem::create_directories(path);
  }
  ~HostFolder() { std::error_code error; std::filesystem::remove_all(path, error); }
  auto put(const std::string& name, const std::string& text) -> void {
    std::filesystem::create_directories((path / name).parent_path());
    std::ofstream(path / name, std::ios::binary) << text;
  }
  auto get(const std::string& name) -> std::string {
    std::ifstream file(path / name, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  }
};

//A disc image in the drive, read from bytes in memory.
inline auto discFrom(const std::vector<u8>& bytes) -> std::shared_ptr<ares::PlayStationPortable::Disc> {
  auto disc = std::make_shared<ares::PlayStationPortable::Disc>();
  auto data = std::make_shared<std::vector<u8>>(bytes);
  std::string error;
  disc->open([data](u64 offset, void* out, u64 size) -> u64 {
    size = offset < data->size() ? std::min<u64>(size, data->size() - offset) : 0;
    memcpy(out, data->data() + offset, size);
    return size;
  }, bytes.size(), error);
  return disc;
}

//The folder PSP_TEST_PROGRAMS names (see tools/psp-test-programs/build.sh), or null when it isn't set (or is empty).
inline auto testPrograms() -> const char* {
  const char* folder = std::getenv("PSP_TEST_PROGRAMS");
  return folder && *folder ? folder : nullptr;
}

//A program from PSP_TEST_PROGRAMS; empty when it isn't set.
inline auto testProgram(const char* name) -> std::vector<u8> {
  const char* programs = testPrograms();
  if(!programs) return {};
  std::ifstream stream(std::string(programs) + "/" + name, std::ios::binary);
  return std::vector<u8>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

}
