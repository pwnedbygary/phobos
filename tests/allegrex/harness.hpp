//The shared harness for the Allegrex tests: a RAM the test CPU reads and writes, a machine that runs a program to
//its halt, the checks, and instruction encoders written from the MIPS32 manual.
#pragma once

#include "prelude.hpp"
#include "../../ares/psp/cpu/allegrex.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

namespace allegrex_test {

using ares::PlayStationPortable::Allegrex;
using Exception = Allegrex::Exception;
using Tests = std::vector<std::pair<const char*, void (*)()>>;

constexpr uint32_t Base = 0x08800000;
constexpr uint32_t Data = Base + 0x8000;
constexpr uint32_t Unmapped = Data + 0x1000;  //RAM left out of the page table, as hardware registers would be

enum : uint32_t { zero, at, v0, v1, a0, a1, a2, a3, t0, t1, t2, t3, t4, t5, t6, t7,
                  s0, s1, s2, s3, s4, s5, s6, s7, t8, t9, k0, k1, gp, sp, fp, ra };

//Encoders, by format.
constexpr auto R(uint32_t funct, uint32_t rd, uint32_t rs, uint32_t rt, uint32_t sa = 0) -> uint32_t {
  return rs << 21 | rt << 16 | rd << 11 | sa << 6 | funct;
}
constexpr auto I(uint32_t opcode, uint32_t rt, uint32_t rs, int32_t immediate) -> uint32_t {
  return opcode << 26 | rs << 21 | rt << 16 | ((uint32_t)immediate & 0xffff);
}
constexpr auto addiu(uint32_t t, uint32_t s, int32_t i) { return I(0x09, t, s, i); }
constexpr auto addi(uint32_t t, uint32_t s, int32_t i) { return I(0x08, t, s, i); }
constexpr auto lui(uint32_t t, int32_t i) { return I(0x0f, t, 0, i); }
constexpr auto ori(uint32_t t, uint32_t s, int32_t i) { return I(0x0d, t, s, i); }
constexpr auto andi(uint32_t t, uint32_t s, int32_t i) { return I(0x0c, t, s, i); }
constexpr auto xori(uint32_t t, uint32_t s, int32_t i) { return I(0x0e, t, s, i); }
constexpr auto slti(uint32_t t, uint32_t s, int32_t i) { return I(0x0a, t, s, i); }
constexpr auto sltiu(uint32_t t, uint32_t s, int32_t i) { return I(0x0b, t, s, i); }
constexpr auto addu(uint32_t d, uint32_t s, uint32_t t) { return R(0x21, d, s, t); }
constexpr auto add(uint32_t d, uint32_t s, uint32_t t) { return R(0x20, d, s, t); }
constexpr auto subu(uint32_t d, uint32_t s, uint32_t t) { return R(0x23, d, s, t); }
constexpr auto sub(uint32_t d, uint32_t s, uint32_t t) { return R(0x22, d, s, t); }
constexpr auto and_(uint32_t d, uint32_t s, uint32_t t) { return R(0x24, d, s, t); }
constexpr auto or_(uint32_t d, uint32_t s, uint32_t t) { return R(0x25, d, s, t); }
constexpr auto xor_(uint32_t d, uint32_t s, uint32_t t) { return R(0x26, d, s, t); }
constexpr auto nor(uint32_t d, uint32_t s, uint32_t t) { return R(0x27, d, s, t); }
constexpr auto slt(uint32_t d, uint32_t s, uint32_t t) { return R(0x2a, d, s, t); }
constexpr auto sltu(uint32_t d, uint32_t s, uint32_t t) { return R(0x2b, d, s, t); }
constexpr auto sll(uint32_t d, uint32_t t, uint32_t a) { return R(0x00, d, 0, t, a); }
constexpr auto srl(uint32_t d, uint32_t t, uint32_t a) { return R(0x02, d, 0, t, a); }
constexpr auto sra(uint32_t d, uint32_t t, uint32_t a) { return R(0x03, d, 0, t, a); }
constexpr auto sllv(uint32_t d, uint32_t t, uint32_t s) { return R(0x04, d, s, t); }
constexpr auto srlv(uint32_t d, uint32_t t, uint32_t s) { return R(0x06, d, s, t); }
constexpr auto srav(uint32_t d, uint32_t t, uint32_t s) { return R(0x07, d, s, t); }
constexpr auto mult(uint32_t s, uint32_t t) { return R(0x18, 0, s, t); }
constexpr auto multu(uint32_t s, uint32_t t) { return R(0x19, 0, s, t); }
constexpr auto div_(uint32_t s, uint32_t t) { return R(0x1a, 0, s, t); }
constexpr auto divu(uint32_t s, uint32_t t) { return R(0x1b, 0, s, t); }
constexpr auto mfhi(uint32_t d) { return R(0x10, d, 0, 0); }
constexpr auto mflo(uint32_t d) { return R(0x12, d, 0, 0); }
constexpr auto mthi(uint32_t s) { return R(0x11, 0, s, 0); }
constexpr auto mtlo(uint32_t s) { return R(0x13, 0, s, 0); }
constexpr auto movz(uint32_t d, uint32_t s, uint32_t t) { return R(0x0a, d, s, t); }
constexpr auto movn(uint32_t d, uint32_t s, uint32_t t) { return R(0x0b, d, s, t); }
constexpr auto rotr(uint32_t d, uint32_t t, uint32_t a) { return R(0x02, d, 1, t, a); }
constexpr auto rotrv(uint32_t d, uint32_t t, uint32_t s) { return R(0x06, d, s, t, 1); }
constexpr auto clz(uint32_t d, uint32_t s) { return R(0x16, d, s, 0); }
constexpr auto clo(uint32_t d, uint32_t s) { return R(0x17, d, s, 0); }
constexpr auto madd(uint32_t s, uint32_t t) { return R(0x1c, 0, s, t); }
constexpr auto maddu(uint32_t s, uint32_t t) { return R(0x1d, 0, s, t); }
constexpr auto msub(uint32_t s, uint32_t t) { return R(0x2e, 0, s, t); }
constexpr auto msubu(uint32_t s, uint32_t t) { return R(0x2f, 0, s, t); }
constexpr auto max(uint32_t d, uint32_t s, uint32_t t) { return R(0x2c, d, s, t); }
constexpr auto min(uint32_t d, uint32_t s, uint32_t t) { return R(0x2d, d, s, t); }
//SPECIAL3: ext and ins (rd holds the field's size minus one, or its top bit), and bshfl's shuffles
constexpr auto ext(uint32_t t, uint32_t s, uint32_t lsb, uint32_t size) {
  return 0x1fu << 26 | s << 21 | t << 16 | (size - 1) << 11 | lsb << 6 | 0x00;
}
constexpr auto ins(uint32_t t, uint32_t s, uint32_t lsb, uint32_t size) {
  return 0x1fu << 26 | s << 21 | t << 16 | (lsb + size - 1) << 11 | lsb << 6 | 0x04;
}
constexpr auto bshfl(uint32_t shuffle, uint32_t d, uint32_t t) { return 0x1fu << 26 | t << 16 | d << 11 | shuffle << 6 | 0x20; }
constexpr auto wsbh(uint32_t d, uint32_t t) { return bshfl(0x02, d, t); }
constexpr auto wsbw(uint32_t d, uint32_t t) { return bshfl(0x03, d, t); }
constexpr auto seb(uint32_t d, uint32_t t) { return bshfl(0x10, d, t); }
constexpr auto bitrev(uint32_t d, uint32_t t) { return bshfl(0x14, d, t); }
constexpr auto seh(uint32_t d, uint32_t t) { return bshfl(0x18, d, t); }
constexpr auto jr(uint32_t s) { return R(0x08, 0, s, 0); }
constexpr auto jalr(uint32_t d, uint32_t s) { return R(0x09, d, s, 0); }
constexpr auto syscall(uint32_t code) { return code << 6 | 0x0c; }
constexpr uint32_t break_ = 0x0000000d;
constexpr uint32_t nop = 0;
constexpr uint32_t halt = 0x70000000;
constexpr auto beq(uint32_t s, uint32_t t, int32_t o) { return I(0x04, t, s, o); }
constexpr auto bne(uint32_t s, uint32_t t, int32_t o) { return I(0x05, t, s, o); }
constexpr auto blez(uint32_t s, int32_t o) { return I(0x06, 0, s, o); }
constexpr auto bgtz(uint32_t s, int32_t o) { return I(0x07, 0, s, o); }
constexpr auto beql(uint32_t s, uint32_t t, int32_t o) { return I(0x14, t, s, o); }
constexpr auto bnel(uint32_t s, uint32_t t, int32_t o) { return I(0x15, t, s, o); }
constexpr auto bltz(uint32_t s, int32_t o) { return I(0x01, 0x00, s, o); }
constexpr auto bgez(uint32_t s, int32_t o) { return I(0x01, 0x01, s, o); }
constexpr auto bltzl(uint32_t s, int32_t o) { return I(0x01, 0x02, s, o); }
constexpr auto bgezl(uint32_t s, int32_t o) { return I(0x01, 0x03, s, o); }
constexpr auto blezl(uint32_t s, int32_t o) { return I(0x16, 0, s, o); }
constexpr auto bgtzl(uint32_t s, int32_t o) { return I(0x17, 0, s, o); }
constexpr auto bltzal(uint32_t s, int32_t o) { return I(0x01, 0x10, s, o); }
constexpr auto bgezal(uint32_t s, int32_t o) { return I(0x01, 0x11, s, o); }
constexpr auto bltzall(uint32_t s, int32_t o) { return I(0x01, 0x12, s, o); }
constexpr auto bgezall(uint32_t s, int32_t o) { return I(0x01, 0x13, s, o); }
constexpr auto j(uint32_t target) { return 0x02u << 26 | (target >> 2 & 0x03ffffff); }
constexpr auto jal(uint32_t target) { return 0x03u << 26 | (target >> 2 & 0x03ffffff); }
constexpr auto lb(uint32_t t, int32_t o, uint32_t b) { return I(0x20, t, b, o); }
constexpr auto lh(uint32_t t, int32_t o, uint32_t b) { return I(0x21, t, b, o); }
constexpr auto lwl(uint32_t t, int32_t o, uint32_t b) { return I(0x22, t, b, o); }
constexpr auto lw(uint32_t t, int32_t o, uint32_t b) { return I(0x23, t, b, o); }
constexpr auto lbu(uint32_t t, int32_t o, uint32_t b) { return I(0x24, t, b, o); }
constexpr auto lhu(uint32_t t, int32_t o, uint32_t b) { return I(0x25, t, b, o); }
constexpr auto lwr(uint32_t t, int32_t o, uint32_t b) { return I(0x26, t, b, o); }
constexpr auto sb(uint32_t t, int32_t o, uint32_t b) { return I(0x28, t, b, o); }
constexpr auto sh(uint32_t t, int32_t o, uint32_t b) { return I(0x29, t, b, o); }
constexpr auto swl(uint32_t t, int32_t o, uint32_t b) { return I(0x2a, t, b, o); }
constexpr auto sw(uint32_t t, int32_t o, uint32_t b) { return I(0x2b, t, b, o); }
constexpr auto swr(uint32_t t, int32_t o, uint32_t b) { return I(0x2e, t, b, o); }
constexpr auto ll(uint32_t t, int32_t o, uint32_t b) { return I(0x30, t, b, o); }
constexpr auto sc(uint32_t t, int32_t o, uint32_t b) { return I(0x38, t, b, o); }
constexpr auto lwc1(uint32_t ft, int32_t o, uint32_t b) { return I(0x31, ft, b, o); }
constexpr auto swc1(uint32_t ft, int32_t o, uint32_t b) { return I(0x39, ft, b, o); }
constexpr auto mtc1(uint32_t t, uint32_t fs) { return 0x44800000u | t << 16 | fs << 11; }
constexpr auto mfc1(uint32_t t, uint32_t fs) { return 0x44000000u | t << 16 | fs << 11; }
constexpr auto ctc1(uint32_t t, uint32_t fs) { return 0x44c00000u | t << 16 | fs << 11; }
constexpr auto cfc1(uint32_t t, uint32_t fs) { return 0x44400000u | t << 16 | fs << 11; }
constexpr auto fop(uint32_t funct, uint32_t fd, uint32_t fs, uint32_t ft = 0) {
  return 0x46000000u | ft << 16 | fs << 11 | fd << 6 | funct;
}
constexpr auto ccond(uint32_t condition, uint32_t fs, uint32_t ft) { return fop(0x30 | condition, 0, fs, ft); }
constexpr auto cvtsw(uint32_t fd, uint32_t fs) { return 0x46800020u | fs << 11 | fd << 6; }
constexpr auto bc1(uint32_t kind, int32_t o) { return 0x45000000u | kind << 16 | ((uint32_t)o & 0xffff); }
enum : uint32_t { Bc1f, Bc1t, Bc1fl, Bc1tl };
enum : uint32_t { CondF, CondUn, CondEq, CondUeq, CondOlt, CondUlt, CondOle, CondUle };

//Which engine Machine runs programs on, unless a test picks one: main() runs every test on each.
inline bool useRecompiler = false;

//64 KiB of RAM at Base, also reachable through the Allegrex's mirrors (the same address with other top three bits,
//such as 0x48800000 for 0x08800000); anything else is a test bug, so it stops the run.
struct Ram {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x10000);
  auto at(uint32_t address) -> uint8_t& {
    uint32_t offset = (address & 0x1fffffff) - (Base & 0x1fffffff);
    if(offset >= bytes.size()) {
      std::fprintf(stderr, "access outside the test's RAM: %08x\n", address);
      std::abort();
    }
    return bytes[offset];
  }
  auto read8(uint32_t a) -> uint8_t { return at(a); }
  auto read16(uint32_t a) -> uint16_t { return (uint16_t)(at(a) | at(a + 1) << 8); }
  auto read32(uint32_t a) -> uint32_t { return read16(a) | (uint32_t)read16(a + 2) << 16; }
  auto write8(uint32_t a, uint8_t v) -> void { at(a) = v; }
  auto write16(uint32_t a, uint16_t v) -> void { at(a) = (uint8_t)v; at(a + 1) = (uint8_t)(v >> 8); }
  auto write32(uint32_t a, uint32_t v) -> void { write16(a, (uint16_t)v); write16(a + 2, (uint16_t)(v >> 16)); }
};

//The Allegrex leaves memory to whoever owns it; here that's the test's RAM. Like the PSP's memory map, it tells the
//recompiler about every write, so code compiled from there is compiled again. Its page table lists the RAM's
//pages, except the one at Unmapped, which compiled code then reaches through read() and write().
struct TestCPU : Allegrex {
  Ram& ram;
  std::vector<u8*> table = std::vector<u8*>(Recompiler::SectionCount);
  uint32_t writes = 0;  //how many writes came through write()

  TestCPU(Ram& ram) : ram(ram) {
    for(uint32_t offset = 0; offset < ram.bytes.size(); offset += 4096) {
      if(Base + offset == Unmapped) continue;
      table[((Base & 0x1fffffff) + offset) >> 12] = ram.bytes.data() + offset;
    }
    pages = table.data();
  }

  auto read(u32 size, u32 address) -> u32 override {
    if(size == Byte) return ram.read8(address);
    if(size == Half) return ram.read16(address);
    return ram.read32(address);
  }

  auto write(u32 size, u32 address, u32 data) -> void override {
    if(size == Byte) ram.write8(address, data);
    else if(size == Half) ram.write16(address, data);
    else ram.write32(address, data);
    recompiler.invalidate(address);
    writes++;
  }
};

struct Machine {
  Ram ram;
  TestCPU cpu{ram};
  bool recompile = useRecompiler;
  std::vector<std::pair<Exception, uint32_t>> exceptions;

  Machine() {
    cpu.exceptionHook = [this](Exception exception, u32 address) {
      exceptions.push_back({exception, address});
      cpu.scc.halted = 1;
    };
  }

  //Places the program at Base with a halt after it, sets up the registers, and runs it until it halts.
  auto run(std::initializer_list<uint32_t> code, const std::function<void(Allegrex&)>& setup = {}) -> void {
    run(std::vector<uint32_t>(code), setup);
  }

  auto run(const std::vector<uint32_t>& code, const std::function<void(Allegrex&)>& setup = {}) -> void {
    uint32_t address = Base;
    for(uint32_t word : code) { ram.write32(address, word); address += 4; }
    ram.write32(address, halt);
    cpu.recompiler.enabled = recompile;
    cpu.power(Base);
    if(setup) setup(cpu);
    cpu.run(10000);
    if(!cpu.scc.halted) std::fprintf(stderr, "program didn't halt\n");
  }

  auto gpr(uint32_t index) const -> uint32_t { return cpu.ipu.r[index]; }
  auto fpr(uint32_t index) const -> uint32_t { return cpu.fpu.r[index]; }
};

//vfpu.cpp
auto vfpuTests() -> Tests;

//measured.cpp
auto measured() -> void;

inline int failures = 0;
inline const char* currentTest = "";

inline auto check(int line, const char* what, uint64_t actual, uint64_t expected) -> void {
  if(actual == expected) return;
  failures++;
  std::printf("FAIL %s (line %d): %s = 0x%llx, expected 0x%llx\n", currentTest, line, what,
              (unsigned long long)actual, (unsigned long long)expected);
}
#define CHECK(actual, expected) check(__LINE__, #actual, (uint64_t)(actual), (uint64_t)(expected))

inline auto bits(float value) -> uint32_t { return std::bit_cast<uint32_t>(value); }

}
