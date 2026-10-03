//Host tests for psp/cpu/allegrex: each runs a short program from RAM and checks the registers and memory.
//The encodings come from the MIPS32 manual; the Allegrex-only ones are the assembled examples in binutils'
//Allegrex test (gas/testsuite/gas/mips/allegrex.d).
//usage: tests/allegrex/run-tests.sh

#include "../../psp/cpu/allegrex.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <utility>
#include <vector>

using phobos::psp::Allegrex;
using phobos::psp::Bus;
using Exception = Allegrex::Exception;

namespace {

constexpr uint32_t Base = 0x08800000;
constexpr uint32_t Data = Base + 0x8000;

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
constexpr auto bgezl(uint32_t s, int32_t o) { return I(0x01, 0x03, s, o); }
constexpr auto bltzal(uint32_t s, int32_t o) { return I(0x01, 0x10, s, o); }
constexpr auto bgezal(uint32_t s, int32_t o) { return I(0x01, 0x11, s, o); }
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

struct Ram : Bus {
  std::vector<uint8_t> bytes = std::vector<uint8_t>(0x10000);
  auto at(uint32_t address) -> uint8_t& {
    uint32_t offset = address - Base;
    if(offset >= bytes.size()) {
      std::fprintf(stderr, "access outside the test's RAM: %08x\n", address);
      std::abort();
    }
    return bytes[offset];
  }
  auto read8(uint32_t a) -> uint8_t override { return at(a); }
  auto read16(uint32_t a) -> uint16_t override { return (uint16_t)(at(a) | at(a + 1) << 8); }
  auto read32(uint32_t a) -> uint32_t override { return read16(a) | (uint32_t)read16(a + 2) << 16; }
  auto write8(uint32_t a, uint8_t v) -> void override { at(a) = v; }
  auto write16(uint32_t a, uint16_t v) -> void override { at(a) = (uint8_t)v; at(a + 1) = (uint8_t)(v >> 8); }
  auto write32(uint32_t a, uint32_t v) -> void override { write16(a, (uint16_t)v); write16(a + 2, (uint16_t)(v >> 16)); }
};

struct Machine {
  Ram ram;
  Allegrex cpu{ram};
  std::vector<std::pair<Exception, uint32_t>> exceptions;

  Machine() {
    cpu.exceptionHook = [this](Exception exception, uint32_t address) {
      exceptions.push_back({exception, address});
      cpu.state.halted = true;
    };
  }

  //Places the program at Base with a halt after it, sets up the registers, and runs it until it halts.
  auto run(std::initializer_list<uint32_t> code, const std::function<void(Allegrex::State&)>& setup = {}) -> void {
    uint32_t address = Base;
    for(uint32_t word : code) { ram.write32(address, word); address += 4; }
    ram.write32(address, halt);
    cpu.reset(Base);
    if(setup) setup(cpu.state);
    cpu.run(10000);
    if(!cpu.state.halted) std::fprintf(stderr, "program didn't halt\n");
  }

  auto gpr(uint32_t index) const -> uint32_t { return cpu.state.gpr[index]; }
  auto fpr(uint32_t index) const -> uint32_t { return cpu.state.fpr[index]; }
};

int failures = 0;
const char* currentTest = "";

auto check(int line, const char* what, uint64_t actual, uint64_t expected) -> void {
  if(actual == expected) return;
  failures++;
  std::printf("FAIL %s (line %d): %s = 0x%llx, expected 0x%llx\n", currentTest, line, what,
              (unsigned long long)actual, (unsigned long long)expected);
}
#define CHECK(actual, expected) check(__LINE__, #actual, (uint64_t)(actual), (uint64_t)(expected))

auto bits(float value) -> uint32_t { return std::bit_cast<uint32_t>(value); }

auto alu() -> void {
  Machine m;
  m.run({
    addiu(t0, zero, -5), lui(t1, 0x1234), ori(t1, t1, 0x5678), addu(t2, t0, t1), subu(t3, t1, t0),
    and_(t4, t1, t0), or_(t5, t1, t0), xor_(t6, t1, t0), nor(t7, t1, zero),
    slt(s0, t0, t1), sltu(s1, t0, t1), slti(s2, t0, -4), sltiu(s3, t1, -1), andi(s4, t0, 0xffff),
    xori(s5, t1, 0xffff), addiu(zero, zero, 5),
  });
  CHECK(m.gpr(t0), 0xfffffffb);
  CHECK(m.gpr(t1), 0x12345678);
  CHECK(m.gpr(t2), 0x12345673);
  CHECK(m.gpr(t3), 0x1234567d);
  CHECK(m.gpr(t4), 0x12345678);
  CHECK(m.gpr(t5), 0xfffffffb);
  CHECK(m.gpr(t6), 0xedcba983);
  CHECK(m.gpr(t7), 0xedcba987);
  CHECK(m.gpr(s0), 1);   // -5 < 0x12345678 signed
  CHECK(m.gpr(s1), 0);   // but not unsigned
  CHECK(m.gpr(s2), 1);   // -5 < -4
  CHECK(m.gpr(s3), 1);   // sltiu compares with the sign-extended 0xffffffff
  CHECK(m.gpr(s4), 0xfffb);  // andi zero-extends
  CHECK(m.gpr(s5), 0x1234a987);
  CHECK(m.gpr(zero), 0);
}

auto shifts() -> void {
  Machine m;
  m.run({sll(t1, t0, 4), srl(t2, t0, 4), sra(t3, t0, 4), sllv(t4, t0, a0), srlv(t5, t0, a0), srav(t6, t0, a0)},
        [](Allegrex::State& s) { s.gpr[t0] = 0x80000ff1; s.gpr[a0] = 36; });
  CHECK(m.gpr(t1), 0x0000ff10);
  CHECK(m.gpr(t2), 0x080000ff);
  CHECK(m.gpr(t3), 0xf80000ff);
  CHECK(m.gpr(t4), 0x0000ff10);  // only the shift amount's low 5 bits count
  CHECK(m.gpr(t5), 0x080000ff);
  CHECK(m.gpr(t6), 0xf80000ff);

  const std::pair<uint32_t, uint32_t> rotations[] = {
    {0x002ac902, 0x81234567},  // rotr $25,$10,4
    {0x002acf02, 0x23456781},  // rotr $25,$10,28
    {0x008ac846, 0x81234567},  // rotrv $25,$10,$4, with $4 = 36: by 4
  };
  for(auto [instruction, expected] : rotations) {
    Machine rotate;
    rotate.run({instruction}, [](Allegrex::State& s) { s.gpr[10] = 0x12345678; s.gpr[4] = 36; });
    CHECK(rotate.gpr(25), expected);
  }
}

auto allegrexBits() -> void {
  Machine m;
  m.run({
    0x7ca43980,  // ext $4,$5,6,8
    0x7c0a4420,  // seb $8,$10
    0x7c0a4620,  // seh $8,$10, over seb's result
  }, [](Allegrex::State& s) { s.gpr[5] = 0x12345678; s.gpr[10] = 0x000080f0; });
  CHECK(m.gpr(4), (0x12345678u >> 6) & 0xff);
  CHECK(m.gpr(8), 0xffff80f0);  // seh ran last

  Machine insert;
  insert.run({0x7ca46984}, [](Allegrex::State& s) { s.gpr[4] = 0xffffffff; s.gpr[5] = 0x12345600; });  // ins $4,$5,6,8
  CHECK(insert.gpr(4), 0xffffc03f);  // bits 6-13 from $5's low byte (0x00)

  Machine bytes;
  bytes.run({0x7c0a40a0}, [](Allegrex::State& s) { s.gpr[10] = 0x11223344; });  // wsbh $8,$10
  CHECK(bytes.gpr(8), 0x22114433);
  Machine word;
  word.run({0x7c0a40e0}, [](Allegrex::State& s) { s.gpr[10] = 0x11223344; });  // wsbw $8,$10
  CHECK(word.gpr(8), 0x44332211);
  Machine reverse;
  reverse.run({0x7c0a4520}, [](Allegrex::State& s) { s.gpr[10] = 0x00000001; });  // bitrev $8,$10
  CHECK(reverse.gpr(8), 0x80000000);
  Machine reverse2;
  reverse2.run({0x7c0a4520}, [](Allegrex::State& s) { s.gpr[10] = 0x12345678; });
  CHECK(reverse2.gpr(8), 0x1e6a2c48);
  Machine byteSign;
  byteSign.run({0x7c0a4420}, [](Allegrex::State& s) { s.gpr[10] = 0x000000f0; });  // seb $8,$10
  CHECK(byteSign.gpr(8), 0xfffffff0);

  Machine count;
  count.run({0x00402817, 0x00801816}, [](Allegrex::State& s) {  // clo $5,$2; clz $3,$4
    s.gpr[2] = 0xfff00000; s.gpr[4] = 0x0000ffff;
  });
  CHECK(count.gpr(5), 12);
  CHECK(count.gpr(3), 16);
  Machine countZero;
  countZero.run({0x00801816}, [](Allegrex::State& s) { s.gpr[4] = 0; });
  CHECK(countZero.gpr(3), 32);

  Machine minmax;
  minmax.run({0x0109382d, 0x0109502c}, [](Allegrex::State& s) {  // min $7,$8,$9; max $10,$8,$9
    s.gpr[8] = 0xfffffffe; s.gpr[9] = 3;
  });
  CHECK(minmax.gpr(7), 0xfffffffe);  // signed: -2 < 3
  CHECK(minmax.gpr(10), 3);

  Machine moves;
  moves.run({0x0064100a, 0x0064280b}, [](Allegrex::State& s) {  // movz $2,$3,$4; movn $5,$3,$4
    s.gpr[2] = 1; s.gpr[3] = 7; s.gpr[4] = 0; s.gpr[5] = 9;
  });
  CHECK(moves.gpr(2), 7);  // $4 is zero, so movz moves
  CHECK(moves.gpr(5), 9);  // and movn doesn't
}

auto multiplyDivide() -> void {
  Machine m;
  m.run({mult(t0, t1), mfhi(s0), mflo(s1), multu(t0, t1), mfhi(s2), mflo(s3)},
        [](Allegrex::State& s) { s.gpr[t0] = 0xfffffffe; s.gpr[t1] = 3; });
  CHECK(m.gpr(s0), 0xffffffff);  // -2 * 3 = -6
  CHECK(m.gpr(s1), 0xfffffffa);
  CHECK(m.gpr(s2), 2);           // 0xfffffffe * 3
  CHECK(m.gpr(s3), 0xfffffffa);

  Machine d;
  d.run({div_(t0, t1), mflo(s0), mfhi(s1), divu(t0, t1), mflo(s2), mfhi(s3)},
        [](Allegrex::State& s) { s.gpr[t0] = (uint32_t)-7; s.gpr[t1] = 2; });
  CHECK(d.gpr(s0), (uint32_t)-3);  // truncates toward zero
  CHECK(d.gpr(s1), (uint32_t)-1);
  CHECK(d.gpr(s2), 0x7ffffffc);
  CHECK(d.gpr(s3), 1);

  Machine zeroDivisor;
  zeroDivisor.run({div_(t0, zero), mflo(s0), mfhi(s1), div_(t1, zero), mflo(s2), divu(t0, zero), mflo(s3), mfhi(s4)},
                  [](Allegrex::State& s) { s.gpr[t0] = 5; s.gpr[t1] = (uint32_t)-5; });
  CHECK(zeroDivisor.gpr(s0), 0xffffffff);
  CHECK(zeroDivisor.gpr(s1), 5);
  CHECK(zeroDivisor.gpr(s2), 1);
  CHECK(zeroDivisor.gpr(s3), 0xffffffff);
  CHECK(zeroDivisor.gpr(s4), 5);

  Machine overflowing;
  overflowing.run({div_(t0, t1), mflo(s0), mfhi(s1)},
                  [](Allegrex::State& s) { s.gpr[t0] = 0x80000000; s.gpr[t1] = (uint32_t)-1; });
  CHECK(overflowing.gpr(s0), 0x80000000);
  CHECK(overflowing.gpr(s1), 0);
  CHECK(overflowing.exceptions.size(), 0);

  Machine accumulate;
  accumulate.run({mthi(t2), mtlo(t3), 0x0109001c, mfhi(s0), mflo(s1),  // madd $8,$9
                  0x0109002e, mfhi(s2), mflo(s3),                       // msub $8,$9
                  0x0109001d, mfhi(s4), mflo(s5),                       // maddu $8,$9
                  0x0109002f, mfhi(s6), mflo(s7)},                      // msubu $8,$9
                 [](Allegrex::State& s) {
                   s.gpr[8] = 0xfffffffe; s.gpr[9] = 0x10; s.gpr[t2] = 0; s.gpr[t3] = 0x10;
                 });
  CHECK(accumulate.gpr(s0), 0xffffffff);  // 0x10 + (-2 * 16) = -16
  CHECK(accumulate.gpr(s1), 0xfffffff0);
  CHECK(accumulate.gpr(s2), 0);           // and back
  CHECK(accumulate.gpr(s3), 0x10);
  CHECK(accumulate.gpr(s4), 0xf);         // 0x10 + 0xfffffffe * 16 = 0xf_ffffffe0 + 0x10
  CHECK(accumulate.gpr(s5), 0xfffffff0);
  CHECK(accumulate.gpr(s6), 0);
  CHECK(accumulate.gpr(s7), 0x10);
}

auto loadsStores() -> void {
  Machine m;
  m.run({
    lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff),
    sw(t0, 0, s0), sh(t1, 4, s0), sb(t1, 6, s0),
    lb(s1, 3, s0), lbu(s2, 3, s0), lh(s3, 2, s0), lhu(s4, 2, s0), lw(s5, 0, s0), lhu(s6, 4, s0), lbu(s7, 6, s0),
  }, [](Allegrex::State& s) { s.gpr[t0] = 0x8899aabb; s.gpr[t1] = 0x1234; });
  CHECK(m.gpr(s1), 0xffffff88);
  CHECK(m.gpr(s2), 0x88);
  CHECK(m.gpr(s3), 0xffff8899);
  CHECK(m.gpr(s4), 0x8899);
  CHECK(m.gpr(s5), 0x8899aabb);
  CHECK(m.gpr(s6), 0x1234);
  CHECK(m.gpr(s7), 0x34);
}

auto unaligned() -> void {
  for(uint32_t offset = 0; offset < 4; offset++) {
    Machine m;
    for(uint32_t i = 0; i < 8; i++) m.ram.write8(Data + i, (uint8_t)(0x10 + i));
    // Little-endian unaligned load: lwr at the address, lwl at the address + 3.
    m.run({lui(s0, Data >> 16), ori(s0, s0, (Data & 0xffff) + offset), lwr(t0, 0, s0), lwl(t0, 3, s0)},
          [](Allegrex::State& s) { s.gpr[t0] = 0xdeadbeef; });
    uint32_t expected = 0;
    for(uint32_t i = 0; i < 4; i++) expected |= (uint32_t)(0x10 + offset + i) << (i * 8);
    CHECK(m.gpr(t0), expected);

    Machine store;
    store.run({lui(s0, Data >> 16), ori(s0, s0, (Data & 0xffff) + offset), swr(t0, 0, s0), swl(t0, 3, s0)},
              [](Allegrex::State& s) { s.gpr[t0] = 0xa1b2c3d4; });
    CHECK(store.ram.read8(Data + offset), 0xd4);
    CHECK(store.ram.read8(Data + offset + 1), 0xc3);
    CHECK(store.ram.read8(Data + offset + 2), 0xb2);
    CHECK(store.ram.read8(Data + offset + 3), 0xa1);
    if(offset > 0) CHECK(store.ram.read8(Data + offset - 1), 0);  // neighbours untouched
    CHECK(store.ram.read8(Data + offset + 4), 0);
  }

  // A lone lwl keeps the register's low bytes; a lone lwr its high bytes.
  Machine part;
  part.ram.write32(Data, 0x44332211);
  part.run({lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff), lwl(t0, 1, s0), lwr(t1, 1, s0)},
           [](Allegrex::State& s) { s.gpr[t0] = 0xaabbccdd; s.gpr[t1] = 0xaabbccdd; });
  CHECK(part.gpr(t0), 0x2211ccdd);
  CHECK(part.gpr(t1), 0xaa443322);
}

auto branches() -> void {
  // beq taken: the delay slot runs, the instruction after it doesn't.
  Machine taken;
  taken.run({beq(t0, t0, 2), addiu(s0, zero, 1), addiu(s1, zero, 1), addiu(s2, zero, 1)});
  CHECK(taken.gpr(s0), 1);
  CHECK(taken.gpr(s1), 0);
  CHECK(taken.gpr(s2), 1);

  Machine notTaken;
  notTaken.run({bne(t0, t0, 2), addiu(s0, zero, 1), addiu(s1, zero, 1)});
  CHECK(notTaken.gpr(s0), 1);
  CHECK(notTaken.gpr(s1), 1);

  // A likely branch that isn't taken skips its delay slot.
  Machine likely;
  likely.run({beql(t0, t1, 2), addiu(s0, zero, 1), addiu(s1, zero, 1), bnel(t0, t1, 2), addiu(s2, zero, 1),
              addiu(s3, zero, 1), addiu(s4, zero, 1)},
             [](Allegrex::State& s) { s.gpr[t0] = 1; s.gpr[t1] = 2; });
  CHECK(likely.gpr(s0), 0);
  CHECK(likely.gpr(s1), 1);
  CHECK(likely.gpr(s2), 1);  // bnel taken: its delay slot runs
  CHECK(likely.gpr(s3), 0);
  CHECK(likely.gpr(s4), 1);

  Machine compares;
  compares.run({blez(t0, 2), nop, addiu(s0, zero, 1), bgtz(t1, 2), nop, addiu(s1, zero, 1),
                bltz(t0, 2), nop, addiu(s2, zero, 1), bgez(zero, 2), nop, addiu(s3, zero, 1),
                bgezl(t0, 2), addiu(s4, zero, 1), addiu(s5, zero, 1)},
               [](Allegrex::State& s) { s.gpr[t0] = (uint32_t)-1; s.gpr[t1] = 1; });
  CHECK(compares.gpr(s0), 0);
  CHECK(compares.gpr(s1), 0);
  CHECK(compares.gpr(s2), 0);
  CHECK(compares.gpr(s3), 0);
  CHECK(compares.gpr(s4), 0);  // bgezl not taken: delay slot skipped
  CHECK(compares.gpr(s5), 1);

  // The link versions write ra even when they don't branch.
  Machine link;
  link.run({bltzal(t0, 2), nop, addiu(s0, zero, 1)}, [](Allegrex::State& s) { s.gpr[t0] = 1; });
  CHECK(link.gpr(ra), Base + 8);
  CHECK(link.gpr(s0), 1);

  Machine linkTaken;
  linkTaken.run({bgezal(zero, 2), nop, addiu(s0, zero, 1)});
  CHECK(linkTaken.gpr(ra), Base + 8);
  CHECK(linkTaken.gpr(s0), 0);

  Machine jump;
  jump.run({j(Base + 12), addiu(s0, zero, 1), addiu(s1, zero, 1)});
  CHECK(jump.gpr(s0), 1);  // the delay slot
  CHECK(jump.gpr(s1), 0);

  // jal links past its delay slot; jr returns; jalr links into rd.
  Machine calls;
  calls.run({jal(Base + 0x20), addiu(s0, zero, 1), addiu(s1, s1, 1), jalr(t9, t0), nop, addiu(s2, s2, 1), halt, halt,
             addiu(s3, zero, 1), jr(ra), addiu(s4, zero, 1), halt, halt, halt, halt, halt,
             addiu(s5, zero, 1), jr(t9), nop},
            [](Allegrex::State& s) { s.gpr[t0] = Base + 0x40; });
  CHECK(calls.gpr(s0), 1);  // jal's delay slot
  CHECK(calls.gpr(s3), 1);  // the call
  CHECK(calls.gpr(s4), 1);  // jr's delay slot
  CHECK(calls.gpr(s1), 1);  // returned
  CHECK(calls.gpr(s5), 1);  // jalr's target
  CHECK(calls.gpr(t9), Base + 0x14);
  CHECK(calls.gpr(s2), 1);
}

auto syscalls() -> void {
  // An import stub as the loader writes it: `jr ra` with the syscall in its delay slot.
  Machine m;
  std::vector<uint32_t> codes;
  uint32_t resumeAt = 0;
  m.cpu.syscallHook = [&](uint32_t code) {
    codes.push_back(code);
    resumeAt = m.cpu.state.pc;
    m.cpu.state.gpr[v0] = 42;
    return true;
  };
  m.run({jal(Base + 0x10), nop, addiu(s0, v0, 0), halt, jr(ra), syscall(0x1234)});
  CHECK(codes.size(), 1);
  CHECK(codes.empty() ? 0 : codes[0], 0x1234);
  CHECK(resumeAt, Base + 8);
  CHECK(m.gpr(s0), 42);
  CHECK(m.exceptions.size(), 0);

  Machine unhandled;
  unhandled.run({syscall(7)});
  CHECK(unhandled.exceptions.size(), 1);
  CHECK(unhandled.exceptions.empty() ? 0 : (uint32_t)unhandled.exceptions[0].first, (uint32_t)Exception::Syscall);
}

auto exceptions() -> void {
  Machine overflow;
  overflow.run({add(t2, t0, t1)}, [](Allegrex::State& s) { s.gpr[t0] = 0x7fffffff; s.gpr[t1] = 1; s.gpr[t2] = 5; });
  CHECK(overflow.exceptions.size(), 1);
  CHECK(overflow.exceptions.empty() ? 0 : (uint32_t)overflow.exceptions[0].first, (uint32_t)Exception::Overflow);
  CHECK(overflow.exceptions.empty() ? 0 : overflow.exceptions[0].second, Base);
  CHECK(overflow.gpr(t2), 5);  // not written

  Machine wraps;
  wraps.run({addu(t2, t0, t1), addi(t3, t0, -1), sub(t4, t1, t0)},
            [](Allegrex::State& s) { s.gpr[t0] = 0x7fffffff; s.gpr[t1] = 1; });
  CHECK(wraps.gpr(t2), 0x80000000);
  CHECK(wraps.gpr(t3), 0x7ffffffe);
  CHECK(wraps.gpr(t4), 0x80000002);
  CHECK(wraps.exceptions.size(), 0);

  Machine misaligned;
  misaligned.run({lw(t1, 2, t0)}, [](Allegrex::State& s) { s.gpr[t0] = Data; s.gpr[t1] = 9; });
  CHECK(misaligned.exceptions.empty() ? 0 : (uint32_t)misaligned.exceptions[0].first, (uint32_t)Exception::AddressLoad);
  CHECK(misaligned.cpu.state.cop0[8], Data + 2);
  CHECK(misaligned.gpr(t1), 9);

  Machine breakpoint;
  breakpoint.run({break_});
  CHECK(breakpoint.exceptions.empty() ? 0 : (uint32_t)breakpoint.exceptions[0].first, (uint32_t)Exception::Breakpoint);

  Machine vfpu;
  vfpu.run({0xd8000000});  // lv.q: the VFPU isn't there yet
  CHECK(vfpu.exceptions.empty() ? 0 : (uint32_t)vfpu.exceptions[0].first, (uint32_t)Exception::ReservedInstruction);
}

auto fpuArithmetic() -> void {
  Machine m;
  m.run({
    mtc1(t0, 0), mtc1(t1, 1),
    fop(0x00, 2, 0, 1), fop(0x01, 3, 0, 1), fop(0x02, 4, 0, 1), fop(0x03, 5, 0, 1), fop(0x04, 6, 1),
    fop(0x05, 7, 8), fop(0x07, 9, 0), fop(0x06, 10, 1), mfc1(s0, 2),
  }, [](Allegrex::State& s) {
    s.gpr[t0] = bits(6.0f); s.gpr[t1] = bits(4.0f); s.fpr[8] = bits(-1.5f);
  });
  CHECK(m.fpr(2), bits(10.0f));
  CHECK(m.fpr(3), bits(2.0f));
  CHECK(m.fpr(4), bits(24.0f));
  CHECK(m.fpr(5), bits(1.5f));
  CHECK(m.fpr(6), bits(2.0f));
  CHECK(m.fpr(7), bits(1.5f));
  CHECK(m.fpr(9), bits(-6.0f));
  CHECK(m.fpr(10), bits(4.0f));
  CHECK(m.gpr(s0), bits(10.0f));

  // Moves keep the exact bits, NaN payloads included.
  Machine nan;
  nan.run({mtc1(t0, 3), fop(0x06, 4, 3), mfc1(s0, 4)}, [](Allegrex::State& s) { s.gpr[t0] = 0x7fa00001; });
  CHECK(nan.gpr(s0), 0x7fa00001);
}

auto fpuConversions() -> void {
  struct Case { float value; uint32_t round, trunc, ceil, floor; };
  const Case cases[] = {
    {2.5f, 2, 2, 3, 2}, {3.5f, 4, 3, 4, 3}, {-2.5f, (uint32_t)-2, (uint32_t)-2, (uint32_t)-2, (uint32_t)-3},
    {1.4999f, 1, 1, 2, 1}, {-2.7f, (uint32_t)-3, (uint32_t)-2, (uint32_t)-2, (uint32_t)-3},
    {3e9f, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff}, {-3e9f, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff},
  };
  for(const Case& c : cases) {
    Machine m;
    m.run({fop(0x0c, 1, 0), fop(0x0d, 2, 0), fop(0x0e, 3, 0), fop(0x0f, 4, 0)},
          [&](Allegrex::State& s) { s.fpr[0] = bits(c.value); });
    CHECK(m.fpr(1), c.round);
    CHECK(m.fpr(2), c.trunc);
    CHECK(m.fpr(3), c.ceil);
    CHECK(m.fpr(4), c.floor);
  }

  Machine nan;
  nan.run({fop(0x0d, 1, 0)}, [](Allegrex::State& s) { s.fpr[0] = 0x7fc00000; });
  CHECK(nan.fpr(1), 0x7fffffff);

  // cvt.w.s follows the rounding mode in fcr31's low bits.
  const uint32_t byMode[] = {2, 2, 3, 2};
  for(uint32_t mode = 0; mode < 4; mode++) {
    Machine m;
    m.run({addiu(t0, zero, (int32_t)mode), ctc1(t0, 31), fop(0x24, 1, 0), cfc1(s0, 31)},
          [](Allegrex::State& s) { s.fpr[0] = bits(2.5f); });
    CHECK(m.fpr(1), byMode[mode]);
    CHECK(m.gpr(s0), mode);
  }

  Machine toFloat;
  toFloat.run({cvtsw(1, 0)}, [](Allegrex::State& s) { s.fpr[0] = (uint32_t)-7; });
  CHECK(toFloat.fpr(1), bits(-7.0f));
}

auto fpuCompare() -> void {
  // c.olt true, then bc1t taken; c.eq false, then bc1f taken; bc1tl not taken skips its delay slot.
  Machine m;
  m.run({ccond(CondOlt, 0, 1), bc1(Bc1t, 2), nop, addiu(s0, zero, 1),
         ccond(CondEq, 0, 1), bc1(Bc1f, 2), nop, addiu(s1, zero, 1),
         bc1(Bc1tl, 2), addiu(s2, zero, 1), addiu(s3, zero, 1)},
        [](Allegrex::State& s) { s.fpr[0] = bits(1.0f); s.fpr[1] = bits(2.0f); });
  CHECK(m.gpr(s0), 0);
  CHECK(m.gpr(s1), 0);
  CHECK(m.gpr(s2), 0);
  CHECK(m.gpr(s3), 1);
  CHECK(m.cpu.state.fcr31 >> 23 & 1, 0);

  // With a NaN: unordered conditions are true, ordered ones false.
  Machine nan;
  nan.run({ccond(CondUn, 0, 1), cfc1(s0, 31), ccond(CondOlt, 0, 1), cfc1(s1, 31), ccond(CondUle, 0, 1), cfc1(s2, 31)},
          [](Allegrex::State& s) { s.fpr[0] = 0x7fc00000; s.fpr[1] = bits(2.0f); });
  CHECK(nan.gpr(s0) >> 23 & 1, 1);
  CHECK(nan.gpr(s1) >> 23 & 1, 0);
  CHECK(nan.gpr(s2) >> 23 & 1, 1);
}

auto fpuMemory() -> void {
  Machine m;
  m.run({lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff), swc1(5, 0, s0), lwc1(6, 0, s0), lw(s1, 0, s0)},
        [](Allegrex::State& s) { s.fpr[5] = bits(-0.75f); });
  CHECK(m.fpr(6), bits(-0.75f));
  CHECK(m.gpr(s1), bits(-0.75f));

  // With flush to zero (fcr31 bit 24) a subnormal result becomes zero.
  Machine flush;
  flush.run({lui(t0, 0x0100), ctc1(t0, 31), fop(0x02, 2, 0, 1)},
            [](Allegrex::State& s) { s.fpr[0] = bits(1e-30f); s.fpr[1] = bits(1e-10f); });
  CHECK(flush.fpr(2), 0);
  Machine keep;
  keep.run({fop(0x02, 2, 0, 1)}, [](Allegrex::State& s) { s.fpr[0] = bits(1e-30f); s.fpr[1] = bits(1e-10f); });
  CHECK(std::fpclassify(std::bit_cast<float>(keep.fpr(2))) == FP_SUBNORMAL, 1);
}

auto system() -> void {
  // mtic sets the interrupt state and mfic reads it back; halt stops the CPU where it is.
  Machine m;
  m.run({0x70020026, 0x70080024}, [](Allegrex::State& s) { s.gpr[2] = 1; });  // mtic $2,$0; mfic $8,$0
  CHECK(m.cpu.state.interrupts, 1);
  CHECK(m.gpr(8), 1);
  CHECK(m.cpu.state.pc, Base + 12);  // past the halt

  Machine atomic;
  atomic.run({lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff), ll(t0, 0, s0), addiu(t0, t0, 1), sc(t0, 0, s0),
              lw(t1, 0, s0)},
             [](Allegrex::State&) {});
  CHECK(atomic.gpr(t0), 1);  // sc succeeded
  CHECK(atomic.gpr(t1), 1);

  Machine cache;
  cache.run({0xbc980004}, [](Allegrex::State& s) { s.gpr[4] = Data; });  // cache 0x18,4($4)
  CHECK(cache.exceptions.size(), 0);
}

}

int main() {
  const std::pair<const char*, void (*)()> tests[] = {
    {"alu", alu}, {"shifts", shifts}, {"allegrex bits", allegrexBits}, {"multiply/divide", multiplyDivide},
    {"loads/stores", loadsStores}, {"unaligned", unaligned}, {"branches", branches}, {"syscalls", syscalls},
    {"exceptions", exceptions}, {"fpu arithmetic", fpuArithmetic}, {"fpu conversions", fpuConversions},
    {"fpu compare", fpuCompare}, {"fpu memory", fpuMemory}, {"system", system},
  };
  for(auto& [name, run] : tests) {
    currentTest = name;
    int before = failures;
    run();
    std::printf("%s %s\n", failures == before ? "pass" : "FAIL", name);
  }
  std::printf("%d failure%s\n", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
