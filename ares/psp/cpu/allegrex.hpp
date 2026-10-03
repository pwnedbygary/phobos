#pragma once
#include <cstdint>
#include <functional>

namespace ares::PlayStationPortable {

// What the CPU reads and writes through. Addresses are the CPU's own; the memory map decides what they reach.
struct Bus {
  virtual ~Bus() = default;
  virtual auto read8(uint32_t address) -> uint8_t = 0;
  virtual auto read16(uint32_t address) -> uint16_t = 0;
  virtual auto read32(uint32_t address) -> uint32_t = 0;
  virtual auto write8(uint32_t address, uint8_t value) -> void = 0;
  virtual auto write16(uint32_t address, uint16_t value) -> void = 0;
  virtual auto write32(uint32_t address, uint32_t value) -> void = 0;
};

// The PSP's main CPU: a MIPS II core with a single-precision FPU, some MIPS32r2 instructions (ext, ins, seb,
// seh, wsbh, rotr, movz, movn) and its own (min, max, bitrev, wsbw, clz/clo and madd/msub at other encodings,
// halt, mfic, mtic). There is no TLB. VFPU instructions raise ReservedInstruction until the VFPU is written.
class Allegrex {
public:
  enum class Exception : uint8_t {
    AddressLoad = 4,
    AddressStore = 5,
    Syscall = 8,
    Breakpoint = 9,
    ReservedInstruction = 10,
    Overflow = 12,
  };

  struct State {
    uint32_t gpr[32];
    uint32_t hi, lo;
    uint32_t pc;    // the instruction that runs next
    uint32_t npc;   // and the one after it: a branch sets this, so its delay slot still runs first
    uint32_t fpr[32];  // bit patterns, so moves keep every bit (NaN payloads included)
    uint32_t fcr31;    // rounding mode in bits 0-1, compare condition in bit 23, flush to zero in bit 24
    uint32_t cop0[32];
    uint32_t interrupts;  // what mfic reads and mtic sets
    bool halted;          // by halt, until the HLE kernel has something to run
  };

  explicit Allegrex(Bus& bus) : bus(bus) {}

  auto reset(uint32_t entry) -> void;
  auto step() -> void;
  // Runs up to `count` instructions, stopping early when the CPU halts; returns how many ran.
  auto run(uint64_t count) -> uint64_t;

  State state{};

  // A syscall's 20-bit code, for the HLE kernel, which returns true when it handled it. The CPU has already moved
  // on, so pc is where execution resumes (the return address, for an import stub's `jr ra; syscall n`).
  std::function<bool(uint32_t code)> syscallHook;
  // An exception nobody handles in HLE, with the address of the instruction that raised it. Without a hook the
  // CPU halts.
  std::function<void(Exception, uint32_t address)> exceptionHook;

private:
  Bus& bus;
  uint32_t current = 0;  // address of the instruction being executed

  auto execute(uint32_t op) -> void;
  auto special(uint32_t op) -> void;
  auto regimm(uint32_t op) -> void;
  auto special2(uint32_t op) -> void;
  auto special3(uint32_t op) -> void;
  auto cop0(uint32_t op) -> void;
  auto cop1(uint32_t op) -> void;
  auto fpuSingle(uint32_t op) -> void;

  auto raise(Exception exception) -> void;
  auto branch(bool taken, uint32_t op) -> void;
  auto branchLikely(bool taken, uint32_t op) -> void;
  auto set(uint32_t index, uint32_t value) -> void { state.gpr[index] = value; }
  auto loadAddress(uint32_t op) const -> uint32_t;
  auto fpuCondition(bool value) -> void;
  auto toWord(float value, uint32_t mode) const -> uint32_t;
  auto flush(float value) const -> float;
};

}
