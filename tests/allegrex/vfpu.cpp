//Host tests for the VFPU (ares/psp/cpu/interpreter-vfpu.cpp). Encodings and expected results follow pspdev's VFPU
//documentation. Like the CPU tests, they run on the interpreter and again on the recompiler.

#include "harness.hpp"

//Inside the harness's namespace, so register names like s1 win over ares's integer types of the same name.
namespace allegrex_test {
namespace {

//Register numbers. A single is row * 32 + matrix * 4 + column; a vector's bit 5 picks a row over a column and its
//low two bits which one; a matrix's bit 5 transposes it.
constexpr auto S(uint32_t matrix, uint32_t column, uint32_t row) { return row << 5 | matrix << 2 | column; }
constexpr auto C(uint32_t matrix, uint32_t column) { return matrix << 2 | column; }           //quad column
constexpr auto Rw(uint32_t matrix, uint32_t row) { return 1u << 5 | matrix << 2 | row; }      //quad row
constexpr auto M(uint32_t matrix) { return matrix << 2; }
constexpr auto E(uint32_t matrix) { return 1u << 5 | matrix << 2; }

//Encoders.
constexpr auto alu(uint32_t opcode, uint32_t size, uint32_t rd, uint32_t rs, uint32_t rt) -> uint32_t {
  return opcode << 23 | rt << 16 | (size >= 3 ? 1u : 0u) << 15 | rs << 8 | (size == 2 || size == 4 ? 1u : 0u) << 7 | rd;
}
constexpr auto memory(uint32_t opcode, uint32_t reg, int32_t offset, uint32_t base, uint32_t low) -> uint32_t {
  return opcode << 26 | base << 21 | (reg & 31) << 16 | ((uint32_t)offset & 0xfffc) | low;
}
constexpr auto lvs(uint32_t reg, int32_t offset, uint32_t base) { return memory(0x32, reg, offset, base, reg >> 5 & 3); }
constexpr auto svs(uint32_t reg, int32_t offset, uint32_t base) { return memory(0x3a, reg, offset, base, reg >> 5 & 3); }
constexpr auto lvq(uint32_t reg, int32_t offset, uint32_t base) { return memory(0x36, reg, offset, base, reg >> 5 & 1); }
constexpr auto svq(uint32_t reg, int32_t offset, uint32_t base) { return memory(0x3e, reg, offset, base, reg >> 5 & 1); }
constexpr auto lvlq(uint32_t reg, int32_t offset, uint32_t base) { return memory(0x35, reg, offset, base, reg >> 5 & 1); }
constexpr auto lvrq(uint32_t reg, int32_t offset, uint32_t base) { return memory(0x35, reg, offset, base, 2 | (reg >> 5 & 1)); }
constexpr auto mtv(uint32_t gpr, uint32_t reg) { return 0x48e00000u | gpr << 16 | reg; }
constexpr auto mfv(uint32_t gpr, uint32_t reg) { return 0x48600000u | gpr << 16 | reg; }
constexpr auto vpfxs(uint32_t prefix) { return 0xdc000000u | prefix; }
constexpr auto vpfxt(uint32_t prefix) { return 0xdd000000u | prefix; }
constexpr auto vpfxd(uint32_t prefix) { return 0xde000000u | prefix; }
constexpr auto bvt(uint32_t bit, int32_t offset) { return 0x49010000u | bit << 18 | ((uint32_t)offset & 0xffff); }
constexpr auto bvf(uint32_t bit, int32_t offset) { return 0x49000000u | bit << 18 | ((uint32_t)offset & 0xffff); }
constexpr auto vcmp(uint32_t condition, uint32_t size, uint32_t rs, uint32_t rt) { return alu(0b011011000, size, condition, rs, rt); }
constexpr auto unary(uint32_t code, uint32_t size, uint32_t rd, uint32_t rs) { return alu(0b110100000, size, rd, rs, code); }
constexpr auto viim(uint32_t rd, uint32_t value) { return 0xdf000000u | rd << 16 | (value & 0xffff); }
constexpr auto vfim(uint32_t rd, uint32_t value) { return 0xdf800000u | rd << 16 | (value & 0xffff); }
constexpr uint32_t vnop = 0xffff0000;
enum : uint32_t {
  Vadd = 0b011000000, Vsub = 0b011000001, Vdiv = 0b011000111, Vmul = 0b011001000, Vdot = 0b011001001,
  Vscl = 0b011001010, Vhdp = 0b011001100, Vcrs = 0b011001101, Vdet = 0b011001110, Vmin = 0b011011010,
  Vmax = 0b011011011, Vslt = 0b011011111, Vcrsp = 0b111100101, Vmmul = 0b111100000, Vtfm4 = 0b111100011,
  Vmscl = 0b111100100, Vmatrix = 0b111100111,
};
enum : uint32_t {  //unary codes, in the rt field
  Vmov = 0x00, Vabs = 0x01, Vneg = 0x02, Vidt = 0x03, Vsat0 = 0x04, Vzero = 0x06, Vone = 0x07, Vrcp = 0x10,
  Vrsq = 0x11, Vsin = 0x12, Vcos = 0x13, Vexp2 = 0x14, Vlog2 = 0x15, Vsqrt = 0x16, Vasin = 0x17, Vf2h = 0x32,
  Vh2f = 0x33, Vi2uc = 0x3c, Vsrt1 = 0x40, Vbfy1 = 0x42, Vocp = 0x44, Vfad = 0x46, Vavg = 0x47, Vsgn = 0x4a,
  Vt5650 = 0x5b,
};

auto f(uint32_t bits) -> float { return std::bit_cast<float>(bits); }

//Close enough for the transcendental functions, which the hardware approximates too.
auto near(uint32_t actual, float expected) -> bool { return std::fabs(f(actual) - expected) < 1e-5f; }

auto setQuad(Allegrex& s, uint32_t reg, std::initializer_list<float> values) -> void {
  //quad columns and rows, through the addressing under test
  uint32_t base = (reg >> 2 & 7) * 4, index = reg & 3, k = 0;
  bool row = reg >> 5 & 1;
  for(float value : values) {
    s.vfpu.r[row ? base + k + 32 * index : base + index + 32 * k] = bits(value);
    k++;
  }
}

auto addressing() -> void {
  //lv.q into a column fills one column of a matrix; into a row, one row. A single's number is its own index.
  Machine m;
  for(uint32_t i = 0; i < 8; i++) m.ram.write32(Data + 4 * i, bits((float)(i + 1)));
  m.run({lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff), lvq(C(1, 2), 0, s0), lvq(Rw(3, 1), 16, s0),
         lvs(S(5, 3, 2), 4, s0), svs(S(5, 3, 2), 64, s0)});
  CHECK(m.ram.read32(Data + 64), bits(2.0f));
  for(uint32_t k = 0; k < 4; k++) {
    CHECK(m.cpu.vfpu.r[1 * 4 + 2 + 32 * k], bits((float)(k + 1)));      //C120: matrix 1, column 2, rows 0-3
    CHECK(m.cpu.vfpu.r[3 * 4 + k + 32 * 1], bits((float)(k + 5)));      //R301: matrix 3, row 1, columns 0-3
  }
  CHECK(m.cpu.vfpu.r[2 * 32 + 5 * 4 + 3], bits(2.0f));

  //sv.q writes a row out in order, and pairs and triples start where bit 6 says.
  Machine store;
  store.run({lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff), svq(Rw(2, 3), 0, s0)},
            [](Allegrex& s) { setQuad(s, Rw(2, 3), {9.0f, 8.0f, 7.0f, 6.0f}); });
  for(uint32_t k = 0; k < 4; k++) CHECK(store.ram.read32(Data + 4 * k), bits(9.0f - (float)k));

  //vmov.p C002 (bit 6: rows 2 and 3 of column 0) to R020 (row 0... columns 2 and 3).
  Machine pair;
  pair.run({unary(Vmov, 2, 1u << 6 | 1u << 5 | C(1, 0), 1u << 6 | C(0, 0))},
           [](Allegrex& s) { s.vfpu.r[S(0, 0, 2)] = bits(5.0f); s.vfpu.r[S(0, 0, 3)] = bits(6.0f); });
  CHECK(pair.cpu.vfpu.r[S(1, 2, 0)], bits(5.0f));
  CHECK(pair.cpu.vfpu.r[S(1, 3, 0)], bits(6.0f));

  //A triple with bit 6 starts one row down.
  Machine triple;
  triple.run({unary(Vmov, 3, C(2, 1), 1u << 6 | C(0, 1))},
             [](Allegrex& s) { for(uint32_t r = 0; r < 4; r++) s.vfpu.r[S(0, 1, r)] = bits((float)r); });
  CHECK(triple.cpu.vfpu.r[S(2, 1, 0)], bits(1.0f));
  CHECK(triple.cpu.vfpu.r[S(2, 1, 2)], bits(3.0f));
}

auto arithmetic() -> void {
  Machine m;
  m.run({alu(Vadd, 4, Rw(0, 0), Rw(1, 0), Rw(2, 0)), alu(Vsub, 4, Rw(0, 1), Rw(1, 0), Rw(2, 0)),
         alu(Vmul, 4, Rw(0, 2), Rw(1, 0), Rw(2, 0)), alu(Vdiv, 4, Rw(0, 3), Rw(1, 0), Rw(2, 0)),
         alu(Vmin, 2, Rw(3, 0), Rw(1, 0), Rw(2, 0)), alu(Vmax, 2, Rw(3, 1), Rw(1, 0), Rw(2, 0))},
        [](Allegrex& s) {
          setQuad(s, Rw(1, 0), {1.0f, -2.0f, 6.0f, 8.0f});
          setQuad(s, Rw(2, 0), {4.0f, 2.0f, 3.0f, -0.5f});
        });
  const float sums[] = {5, 0, 9, 7.5f}, differences[] = {-3, -4, 3, 8.5f}, products[] = {4, -4, 18, -4},
              quotients[] = {0.25f, -1, 2, -16};
  for(uint32_t k = 0; k < 4; k++) {
    CHECK(m.cpu.vfpu.r[k], bits(sums[k]));
    CHECK(m.cpu.vfpu.r[k + 32], bits(differences[k]));
    CHECK(m.cpu.vfpu.r[k + 64], bits(products[k]));
    CHECK(m.cpu.vfpu.r[k + 96], bits(quotients[k]));
  }
  CHECK(m.cpu.vfpu.r[12], bits(1.0f));
  CHECK(m.cpu.vfpu.r[13], bits(-2.0f));
  CHECK(m.cpu.vfpu.r[12 + 32], bits(4.0f));
  CHECK(m.cpu.vfpu.r[13 + 32], bits(2.0f));

  //Denormals count as zero, in and out.
  Machine flush;
  flush.run({alu(Vadd, 1, S(0, 0, 0), S(1, 0, 0), S(2, 0, 0))},
            [](Allegrex& s) { s.vfpu.r[S(1, 0, 0)] = 0x00000001; s.vfpu.r[S(2, 0, 0)] = 0x80000000; });
  CHECK(flush.cpu.vfpu.r[S(0, 0, 0)] & 0x7fffffff, 0);
}

auto prefixes() -> void {
  //rs: swizzle w, z, y, x with lane 0 negated; rt: lane 1 the constant 3, lane 2 |z|; rd: lane 0 saturated to
  //0..1, lane 3 masked.
  Machine m;
  uint32_t swizzleReverse = 3 | 2 << 2 | 1 << 4 | 0 << 6;
  uint32_t constantThree = (0xe4 & ~(3u << 2)) | 1 << (8 + 1) | 1 << (12 + 1);  //lane 1: swizzle 0, absolute bit
  m.run({vpfxs(swizzleReverse | 1 << 16), vpfxt(constantThree | 1 << (8 + 2)),
         vpfxd(1 << 0 | 1 << (8 + 3)), alu(Vadd, 4, Rw(0, 0), Rw(1, 0), Rw(2, 0)),
         alu(Vadd, 4, Rw(0, 1), Rw(1, 0), Rw(2, 0))},
        [](Allegrex& s) {
          setQuad(s, Rw(1, 0), {1.0f, 2.0f, 3.0f, 4.0f});
          setQuad(s, Rw(2, 0), {10.0f, 20.0f, -30.0f, 40.0f});
          s.vfpu.r[3] = bits(99.0f);
        });
  CHECK(m.cpu.vfpu.r[0], bits(1.0f));        //-4 + 10 = 6, saturated to 1
  CHECK(m.cpu.vfpu.r[1], bits(6.0f));        //3 + the constant 3
  CHECK(m.cpu.vfpu.r[2], bits(32.0f));       //2 + |-30|
  CHECK(m.cpu.vfpu.r[3], bits(99.0f));       //masked
  //The prefixes apply once: the next vadd is plain.
  CHECK(m.cpu.vfpu.r[32], bits(11.0f));
  CHECK(m.cpu.vfpu.r[35], bits(44.0f));
  CHECK(m.cpu.vfpu.pfxs, 0xe4);
  CHECK(m.cpu.vfpu.pfxd, 0);

  //Saturation to -1..1 turns -0 into 0 only for 0..1; constants can be negated.
  Machine negative;
  negative.run({vpfxs(0xe4 | 1 << 12 | 1 << 16), vpfxd(3), alu(Vmul, 1, S(0, 0, 0), S(1, 0, 0), S(2, 0, 0))},
               [](Allegrex& s) { s.vfpu.r[S(2, 0, 0)] = bits(5.0f); });
  CHECK(negative.cpu.vfpu.r[S(0, 0, 0)], bits(-0.0f));  //-(constant 0) * 5, within -1..1
}

auto products() -> void {
  Machine m;
  m.run({alu(Vdot, 4, S(3, 0, 0), Rw(1, 0), Rw(2, 0)), alu(Vhdp, 4, S(3, 1, 0), Rw(1, 0), Rw(2, 0)),
         alu(Vscl, 4, Rw(4, 0), Rw(1, 0), S(2, 0, 0)), alu(Vcrsp, 3, Rw(5, 0), Rw(1, 0), Rw(2, 0)),
         alu(Vcrs, 3, Rw(5, 1), Rw(1, 0), Rw(2, 0)), alu(Vdet, 2, S(3, 2, 0), Rw(1, 0), Rw(2, 0))},
        [](Allegrex& s) {
          setQuad(s, Rw(1, 0), {1.0f, 2.0f, 3.0f, 4.0f});
          setQuad(s, Rw(2, 0), {5.0f, 6.0f, 7.0f, 8.0f});
        });
  CHECK(m.cpu.vfpu.r[S(3, 0, 0)], bits(70.0f));        //5 + 12 + 21 + 32
  CHECK(m.cpu.vfpu.r[S(3, 1, 0)], bits(46.0f));        //5 + 12 + 21 + 8
  CHECK(m.cpu.vfpu.r[S(4, 0, 0)], bits(5.0f));         //scaled by rt's first lane
  CHECK(m.cpu.vfpu.r[S(4, 3, 0)], bits(20.0f));
  CHECK(m.cpu.vfpu.r[S(5, 0, 0)], bits(-4.0f));        //(1,2,3) x (5,6,7) = (-4, 8, -4)
  CHECK(m.cpu.vfpu.r[S(5, 1, 0)], bits(8.0f));
  CHECK(m.cpu.vfpu.r[S(5, 2, 0)], bits(-4.0f));
  CHECK(m.cpu.vfpu.r[S(5, 0, 1)], bits(14.0f));        //vcrs: 2 * 7
  CHECK(m.cpu.vfpu.r[S(5, 1, 1)], bits(15.0f));        //3 * 5
  CHECK(m.cpu.vfpu.r[S(5, 2, 1)], bits(6.0f));         //1 * 6
  CHECK(m.cpu.vfpu.r[S(3, 2, 0)], bits(-4.0f));        //1 * 6 - 2 * 5
}

auto comparisons() -> void {
  //vcmp.q LT sets a bit per lane, bit 4 for any and bit 5 for all; bvt branches on one of them; vcmovt moves the
  //lanes whose own bit is set (selector 6).
  Machine m;
  m.run({vcmp(2, 4, Rw(1, 0), Rw(2, 0)), vnop, bvt(4, 2), nop, addiu(s0, zero, 1), bvf(5, 2), nop,
         addiu(s1, zero, 1), alu(0b110100101, 4, Rw(3, 0), Rw(1, 0), 0x20 | 6)},
        [](Allegrex& s) {
          setQuad(s, Rw(1, 0), {1.0f, 5.0f, 2.0f, 9.0f});
          setQuad(s, Rw(2, 0), {2.0f, 4.0f, 3.0f, 9.0f});
          setQuad(s, Rw(3, 0), {-1.0f, -1.0f, -1.0f, -1.0f});
        });
  CHECK(m.cpu.vfpu.cc, 0b010101);
  CHECK(m.gpr(s0), 0);  //bvt 4 taken
  CHECK(m.gpr(s1), 0);  //bvf 5 taken (not every lane)
  CHECK(m.cpu.vfpu.r[S(3, 0, 0)], bits(1.0f));
  CHECK(m.cpu.vfpu.r[S(3, 1, 0)], bits(-1.0f));
  CHECK(m.cpu.vfpu.r[S(3, 2, 0)], bits(2.0f));
  CHECK(m.cpu.vfpu.r[S(3, 3, 0)], bits(-1.0f));

  //A pair keeps the bits of lanes 2 and 3.
  Machine pair;
  pair.run({vcmp(4, 2, Rw(1, 0), Rw(2, 0))}, [](Allegrex& s) { s.vfpu.cc = 0b001100; });
  CHECK(pair.cpu.vfpu.cc, 0b111111);

  Machine slt;
  slt.run({alu(Vslt, 2, Rw(3, 0), Rw(1, 0), Rw(2, 0))},
          [](Allegrex& s) { setQuad(s, Rw(1, 0), {1, 3, 0, 0}); setQuad(s, Rw(2, 0), {2, 2, 0, 0}); });
  CHECK(slt.cpu.vfpu.r[S(3, 0, 0)], bits(1.0f));
  CHECK(slt.cpu.vfpu.r[S(3, 1, 0)], bits(0.0f));
}

auto conversions() -> void {
  Machine m;
  m.run({alu(0b110100101, 2, Rw(3, 0), Rw(1, 0), 4),        //vi2f.p by 2^-4
         alu(0b110100100, 4, Rw(4, 0), Rw(2, 0), 0x20 | 1), //vf2iz.q by 2^1
         alu(0b110100100, 4, Rw(5, 0), Rw(2, 0), 0x00 | 0), //vf2in.q
         alu(0b110100100, 4, Rw(6, 0), Rw(2, 0), 0x40 | 0), //vf2iu.q
         alu(0b110100100, 4, Rw(7, 0), Rw(2, 0), 0x60 | 0), //vf2id.q
         viim(S(3, 0, 1), (uint32_t)-5), vfim(S(3, 1, 1), 0x3c00), alu(0b110100000, 4, Rw(3, 2), 0, 0x60 | 8)},
        [](Allegrex& s) {
          s.vfpu.r[S(1, 0, 0)] = 48;
          s.vfpu.r[S(1, 1, 0)] = (uint32_t)-16;
          setQuad(s, Rw(2, 0), {2.5f, -2.5f, 1e10f, -3.7f});
        });
  CHECK(m.cpu.vfpu.r[S(3, 0, 0)], bits(3.0f));
  CHECK(m.cpu.vfpu.r[S(3, 1, 0)], bits(-1.0f));
  CHECK(m.cpu.vfpu.r[S(4, 0, 0)], 5);                     //2.5 * 2, truncated
  CHECK(m.cpu.vfpu.r[S(4, 1, 0)], (uint32_t)-5);
  CHECK(m.cpu.vfpu.r[S(4, 2, 0)], 0x7fffffff);            //saturated
  CHECK(m.cpu.vfpu.r[S(4, 3, 0)], (uint32_t)-7);
  CHECK(m.cpu.vfpu.r[S(5, 0, 0)], 2);                     //to nearest even
  CHECK(m.cpu.vfpu.r[S(5, 1, 0)], (uint32_t)-2);
  CHECK(m.cpu.vfpu.r[S(6, 0, 0)], 3);                     //up
  CHECK(m.cpu.vfpu.r[S(6, 3, 0)], (uint32_t)-3);
  CHECK(m.cpu.vfpu.r[S(7, 0, 0)], 2);                     //down
  CHECK(m.cpu.vfpu.r[S(7, 3, 0)], (uint32_t)-4);
  CHECK(m.cpu.vfpu.r[S(3, 0, 1)], bits(-5.0f));           //viim
  CHECK(m.cpu.vfpu.r[S(3, 1, 1)], bits(1.0f));            //vfim 0x3c00 = 1.0 in half precision
  for(uint32_t k = 0; k < 4; k++) CHECK(m.cpu.vfpu.r[S(3, k, 2)], 0x3fc90fdb);  //vcst 8: pi/2 (numbered from 1)

  //Half floats there and back.
  Machine half;
  half.run({unary(Vf2h, 4, Rw(3, 0), Rw(1, 0)), unary(Vh2f, 2, Rw(4, 0), Rw(3, 0))},
           [](Allegrex& s) { setQuad(s, Rw(1, 0), {1.5f, -2.0f, 65504.0f, 1e-10f}); });
  CHECK(half.cpu.vfpu.r[S(3, 0, 0)], 0xc0003e00u);
  CHECK(half.cpu.vfpu.r[S(4, 0, 0)], bits(1.5f));
  CHECK(half.cpu.vfpu.r[S(4, 1, 0)], bits(-2.0f));
  CHECK(half.cpu.vfpu.r[S(4, 2, 0)], bits(65504.0f));
  CHECK(half.cpu.vfpu.r[S(4, 3, 0)], 0);                  //too small: zero

  //vi2uc.q packs the top bits of four positive integers, negative ones as 0; vt5650.q packs colors.
  Machine pack;
  pack.run({unary(Vi2uc, 4, S(3, 0, 0), Rw(1, 0)), unary(Vt5650, 4, Rw(4, 0), Rw(2, 0))},
           [](Allegrex& s) {
             s.vfpu.r[S(1, 0, 0)] = 0x7f800000; s.vfpu.r[S(1, 1, 0)] = 0x80000000;
             s.vfpu.r[S(1, 2, 0)] = 0x01000000; s.vfpu.r[S(1, 3, 0)] = 0x3fffffff;
             s.vfpu.r[S(2, 0, 0)] = 0xffffffff; s.vfpu.r[S(2, 1, 0)] = 0x000000f8;
             s.vfpu.r[S(2, 2, 0)] = 0x0000fc00; s.vfpu.r[S(2, 3, 0)] = 0x00f80000;
           });
  CHECK(pack.cpu.vfpu.r[S(3, 0, 0)], 0x7f0200ffu);
  CHECK(pack.cpu.vfpu.r[S(4, 0, 0)], 0x001fffffu);
  CHECK(pack.cpu.vfpu.r[S(4, 1, 0)], 0xf80007e0u);
}

auto functions() -> void {
  Machine m;
  m.run({unary(Vsin, 1, S(3, 0, 0), S(1, 0, 0)), unary(Vcos, 1, S(3, 1, 0), S(1, 1, 0)),
         unary(Vcos, 1, S(3, 2, 0), S(1, 0, 0)), unary(Vrcp, 1, S(3, 3, 0), S(1, 2, 0)),
         unary(Vrsq, 1, S(3, 0, 1), S(1, 2, 0)), unary(Vsqrt, 1, S(3, 1, 1), S(1, 3, 0)),
         unary(Vexp2, 1, S(3, 2, 1), S(1, 0, 1)), unary(Vlog2, 1, S(3, 3, 1), S(1, 1, 1)),
         unary(Vasin, 1, S(3, 0, 2), S(1, 0, 0)), unary(Vsgn, 4, Rw(4, 0), Rw(2, 0)),
         unary(Vocp, 1, S(3, 1, 2), S(1, 2, 0)), unary(Vabs, 1, S(3, 2, 2), S(2, 1, 0)),
         unary(Vneg, 1, S(3, 3, 2), S(1, 0, 0)), unary(Vsat0, 4, Rw(5, 0), Rw(2, 0))},
        [](Allegrex& s) {
          s.vfpu.r[S(1, 0, 0)] = bits(1.0f); s.vfpu.r[S(1, 1, 0)] = bits(0.0f); s.vfpu.r[S(1, 2, 0)] = bits(4.0f);
          s.vfpu.r[S(1, 3, 0)] = bits(9.0f); s.vfpu.r[S(1, 0, 1)] = bits(3.0f); s.vfpu.r[S(1, 1, 1)] = bits(8.0f);
          setQuad(s, Rw(2, 0), {-3.0f, -0.25f, 0.0f, 7.0f});
        });
  CHECK(near(m.cpu.vfpu.r[S(3, 0, 0)], 1.0f), 1);   //sin of one quarter turn
  CHECK(near(m.cpu.vfpu.r[S(3, 1, 0)], 1.0f), 1);   //cos 0
  CHECK(near(m.cpu.vfpu.r[S(3, 2, 0)], 0.0f), 1);   //cos of a quarter turn
  CHECK(m.cpu.vfpu.r[S(3, 3, 0)], bits(0.25f));
  CHECK(near(m.cpu.vfpu.r[S(3, 0, 1)], 0.5f), 1);
  CHECK(m.cpu.vfpu.r[S(3, 1, 1)], bits(3.0f));
  CHECK(m.cpu.vfpu.r[S(3, 2, 1)], bits(8.0f));
  CHECK(m.cpu.vfpu.r[S(3, 3, 1)], bits(3.0f));
  CHECK(near(m.cpu.vfpu.r[S(3, 0, 2)], 1.0f), 1);   //asin(1) is one quarter turn
  CHECK(m.cpu.vfpu.r[S(4, 0, 0)], bits(-1.0f));
  CHECK(m.cpu.vfpu.r[S(4, 2, 0)], bits(0.0f));
  CHECK(m.cpu.vfpu.r[S(4, 3, 0)], bits(1.0f));
  CHECK(m.cpu.vfpu.r[S(3, 1, 2)], bits(-3.0f));     //1 - 4
  CHECK(m.cpu.vfpu.r[S(3, 2, 2)], bits(0.25f));
  CHECK(m.cpu.vfpu.r[S(3, 3, 2)], bits(-1.0f));
  CHECK(m.cpu.vfpu.r[S(5, 0, 0)], bits(0.0f));
  CHECK(m.cpu.vfpu.r[S(5, 3, 0)], bits(1.0f));

  //Reductions and the sorting and butterfly passes.
  Machine reduce;
  reduce.run({unary(Vfad, 4, S(3, 0, 0), Rw(1, 0)), unary(Vavg, 4, S(3, 1, 0), Rw(1, 0)),
              unary(Vsrt1, 4, Rw(4, 0), Rw(1, 0)), unary(Vbfy1, 4, Rw(5, 0), Rw(1, 0))},
             [](Allegrex& s) { setQuad(s, Rw(1, 0), {4.0f, 1.0f, 2.0f, 9.0f}); });
  CHECK(reduce.cpu.vfpu.r[S(3, 0, 0)], bits(16.0f));
  CHECK(reduce.cpu.vfpu.r[S(3, 1, 0)], bits(4.0f));
  const float sorted[] = {1, 4, 2, 9}, butterfly[] = {5, 3, 11, -7};
  for(uint32_t k = 0; k < 4; k++) {
    CHECK(reduce.cpu.vfpu.r[S(4, k, 0)], bits(sorted[k]));
    CHECK(reduce.cpu.vfpu.r[S(5, k, 0)], bits(butterfly[k]));
  }
}

auto matrices() -> void {
  //vmidt.q gives the identity; vmmul.q follows the documented order, rd[r][c] = sum of rs[c][k] * rt[r][k];
  //vtfm4 and vhtfm4 transform by a matrix's rows.
  Machine m;
  m.run({alu(Vmatrix, 4, M(0), 0, Vidt), alu(Vmmul, 4, M(3), M(1), M(2)), alu(Vtfm4, 4, Rw(4, 0), M(1), Rw(5, 0)),
         alu(Vtfm4, 3, Rw(4, 1), M(1), Rw(5, 0)), alu(Vmscl, 2, M(6), M(1), S(5, 0, 0)),
         alu(Vmatrix, 3, M(7), 0, Vone), alu(Vmatrix, 2, M(7), 0, Vzero)},
        [](Allegrex& s) {
          for(uint32_t r = 0; r < 4; r++) {
            for(uint32_t c = 0; c < 4; c++) {
              s.vfpu.r[4 + c + 32 * r] = bits((float)(r * 4 + c + 1));      //matrix 1: 1..16 by rows
              s.vfpu.r[8 + c + 32 * r] = bits(r == c ? 2.0f : 0.0f);        //matrix 2: twice the identity
            }
          }
          setQuad(s, Rw(5, 0), {1.0f, 0.0f, 0.0f, 1.0f});
        });
  for(uint32_t r = 0; r < 4; r++) {
    for(uint32_t c = 0; c < 4; c++) {
      CHECK(m.cpu.vfpu.r[c + 32 * r], bits(r == c ? 1.0f : 0.0f));
      //rd[r][c] = sum over k of m1[c][k] * 2 * id[r][k] = 2 * m1[c][r]
      CHECK(m.cpu.vfpu.r[12 + c + 32 * r], bits(2.0f * (float)(c * 4 + r + 1)));
    }
  }
  const float transformed[] = {1 + 4, 5 + 8, 9 + 12, 13 + 16};
  for(uint32_t k = 0; k < 4; k++) CHECK(m.cpu.vfpu.r[16 + k], bits(transformed[k]));
  //vhtfm4 (size field t): rows dotted with (1, 0, 0) plus their last element, the same here
  for(uint32_t k = 0; k < 4; k++) CHECK(m.cpu.vfpu.r[16 + k + 32], bits(transformed[k]));
  CHECK(m.cpu.vfpu.r[24 + 0], bits(1.0f));   //vmscl.p M600 = M100 * 1
  CHECK(m.cpu.vfpu.r[24 + 1 + 32], bits(6.0f));
  CHECK(m.cpu.vfpu.r[28 + 0], bits(0.0f));   //vmone.t then vmzero.p
  CHECK(m.cpu.vfpu.r[28 + 2 + 64], bits(1.0f));

  //A transposed operand reads columns where the plain one reads rows.
  Machine transposed;
  transposed.run({alu(Vmatrix, 4, M(3), E(1), Vmov)},
                 [](Allegrex& s) { for(uint32_t i = 0; i < 4; i++) s.vfpu.r[4 + i] = bits((float)(i + 1)); });
  for(uint32_t r = 0; r < 4; r++) CHECK(transposed.cpu.vfpu.r[12 + 32 * r], bits((float)(r + 1)));

  //vrot.q: cosine in lane 0 and sine in lane 1 of half a quarter turn.
  Machine rotation;
  rotation.run({alu(0b111100111, 4, Rw(3, 0), S(1, 0, 0), 0x20 | 0 | 1 << 2)},
               [](Allegrex& s) { s.vfpu.r[S(1, 0, 0)] = bits(0.5f); });
  CHECK(near(rotation.cpu.vfpu.r[S(3, 0, 0)], 0.70710678f), 1);
  CHECK(near(rotation.cpu.vfpu.r[S(3, 1, 0)], 0.70710678f), 1);
  CHECK(rotation.cpu.vfpu.r[S(3, 2, 0)], 0);
}

auto moves() -> void {
  //mtv and mfv move a register to and from a GPR; mtvc and mfvc reach the control registers (128 + n).
  Machine m;
  m.run({mtv(t0, S(2, 1, 3)), mfv(t1, S(2, 1, 3)), mtv(t2, 128 + 0), mfv(t3, 128 + 0), mfv(t4, 128 + 3),
         mtv(t5, 128 + 2), mfv(t6, 128 + 2)},
        [](Allegrex& s) {
          s.ipu.r[t0] = 0x12345678; s.ipu.r[t2] = 0x1b; s.ipu.r[t5] = 0xffffffff; s.vfpu.cc = 0x2a;
        });
  CHECK(m.cpu.vfpu.r[S(2, 1, 3)], 0x12345678);
  CHECK(m.gpr(t1), 0x12345678);
  CHECK(m.gpr(t3), 0x1b);
  CHECK(m.gpr(t4), 0x2a);
  CHECK(m.gpr(t6), 0xfff);   //vpfxd keeps 12 bits

  //lvr.q and lvl.q together load an unaligned quad.
  Machine unaligned;
  for(uint32_t i = 0; i < 8; i++) unaligned.ram.write32(Data + 4 * i, 100 + i);
  unaligned.run({lui(s0, Data >> 16), ori(s0, s0, (Data & 0xffff) + 4), lvrq(Rw(1, 0), 0, s0), lvlq(Rw(1, 0), 12, s0)});
  for(uint32_t k = 0; k < 4; k++) CHECK(unaligned.cpu.vfpu.r[4 + k], 101 + k);

  //lv.q needs 16-byte alignment.
  Machine misaligned;
  misaligned.run({lui(s0, Data >> 16), ori(s0, s0, (Data & 0xffff) + 4), lvq(Rw(1, 0), 0, s0)});
  CHECK(misaligned.exceptions.empty() ? 0 : (uint32_t)misaligned.exceptions[0].first, (uint32_t)Exception::AddressLoad);
}

}

//Instructions whose results follow directly from pspdev's descriptions, worked out by hand.
auto more() -> void {
  //vsbn.s puts rt's integer as rs's exponent (1.5 scaled to 2^3); vsbz.s takes the exponent away again; vlgb.s
  //reads it.
  Machine exponents;
  exponents.run({alu(0b011000010, 1, S(3, 0, 0), S(1, 0, 0), S(1, 1, 0)), unary(0x36, 1, S(3, 1, 0), S(1, 2, 0)),
                 unary(0x37, 1, S(3, 2, 0), S(1, 2, 0)), unary(0x37, 1, S(3, 3, 0), S(1, 3, 0))},
                [](Allegrex& s) {
                  s.vfpu.r[S(1, 0, 0)] = bits(1.5f); s.vfpu.r[S(1, 1, 0)] = 3;
                  s.vfpu.r[S(1, 2, 0)] = bits(12.0f); s.vfpu.r[S(1, 3, 0)] = bits(0.5f);
                });
  CHECK(exponents.cpu.vfpu.r[S(3, 0, 0)], bits(12.0f));
  CHECK(exponents.cpu.vfpu.r[S(3, 1, 0)], bits(1.5f));
  CHECK(exponents.cpu.vfpu.r[S(3, 2, 0)], bits(3.0f));
  CHECK(exponents.cpu.vfpu.r[S(3, 3, 0)], bits(-1.0f));

  //Bytes and halfwords spread into lanes, and packed back.
  Machine spread;
  spread.run({unary(0x39, 1, Rw(3, 0), S(1, 0, 0)),   //vc2i.s
              unary(0x38, 1, Rw(4, 0), S(1, 1, 0)),   //vuc2ifs.s
              unary(0x3b, 2, Rw(5, 0), Rw(2, 0)),     //vs2i.p
              unary(0x3a, 1, Rw(6, 0), S(1, 2, 0)),   //vus2i.s
              unary(0x3f, 4, Rw(7, 0), Rw(5, 0)),     //vi2s.q, of what vs2i.p made
              unary(0x3e, 2, S(1, 3, 3), Rw(2, 2))},  //vi2us.p
             [](Allegrex& s) {
               s.vfpu.r[S(1, 0, 0)] = 0x80402010; s.vfpu.r[S(1, 1, 0)] = 0x000000ff; s.vfpu.r[S(1, 2, 0)] = 0xffff8000;
               s.vfpu.r[S(2, 0, 0)] = 0x22221111; s.vfpu.r[S(2, 1, 0)] = 0x44443333;
               s.vfpu.r[S(2, 0, 2)] = 0x40000000; s.vfpu.r[S(2, 1, 2)] = 0x80000000;
             });
  const uint32_t topBytes[] = {0x10000000, 0x20000000, 0x40000000, 0x80000000};
  const uint32_t halves[] = {0x11110000, 0x22220000, 0x33330000, 0x44440000};
  for(uint32_t k = 0; k < 4; k++) {
    CHECK(spread.cpu.vfpu.r[S(3, k, 0)], topBytes[k]);
    CHECK(spread.cpu.vfpu.r[S(5, k, 0)], halves[k]);
  }
  CHECK(spread.cpu.vfpu.r[S(4, 0, 0)], 0x7fffffff);  //0xff repeated down the lane
  CHECK(spread.cpu.vfpu.r[S(4, 1, 0)], 0);
  CHECK(spread.cpu.vfpu.r[S(6, 0, 0)], 0x40000000);  //0x8000 scaled up by 2^15
  CHECK(spread.cpu.vfpu.r[S(6, 1, 0)], 0x7fff8000);
  CHECK(spread.cpu.vfpu.r[S(7, 0, 0)], 0x22221111);
  CHECK(spread.cpu.vfpu.r[S(7, 1, 0)], 0x44443333);
  CHECK(spread.cpu.vfpu.r[S(1, 3, 3)], 0x00008000);  //the negative lane gives 0

  //Colors: 8 bits a channel (red lowest) into 4444 and 5551.
  Machine colors;
  colors.run({unary(0x59, 4, Rw(3, 0), Rw(1, 0)), unary(0x5a, 4, Rw(4, 0), Rw(1, 0))},
             [](Allegrex& s) { s.vfpu.r[S(1, 0, 0)] = 0x80402010; s.vfpu.r[S(1, 1, 0)] = 0xffffffff; });
  CHECK(colors.cpu.vfpu.r[S(3, 0, 0)], 0xffff8421u);
  CHECK(colors.cpu.vfpu.r[S(3, 1, 0)], 0);
  CHECK(colors.cpu.vfpu.r[S(4, 0, 0)], 0xffffa082u);

  //vqmul.q: i times j is k, and (0, 0, 0, 1) is the identity.
  Machine quaternions;
  quaternions.run({alu(0b111100101, 4, Rw(3, 0), Rw(1, 0), Rw(2, 0)), alu(0b111100101, 4, Rw(3, 1), Rw(1, 1), Rw(2, 1))},
                  [](Allegrex& s) {
                    setQuad(s, Rw(1, 0), {1, 0, 0, 0}); setQuad(s, Rw(2, 0), {0, 1, 0, 0});
                    setQuad(s, Rw(1, 1), {0, 0, 0, 1}); setQuad(s, Rw(2, 1), {1, 2, 3, 4});
                  });
  const float k[] = {0, 0, 1, 0};
  for(uint32_t lane = 0; lane < 4; lane++) {
    CHECK(quaternions.cpu.vfpu.r[S(3, lane, 0)], bits(k[lane]));
    CHECK(quaternions.cpu.vfpu.r[S(3, lane, 1)], bits((float)(lane + 1)));
  }

  //vsocp.s, vbfy2.q, the other sorting passes, vscmp and vsge.
  Machine lanes;
  lanes.run({unary(0x45, 1, Rw(3, 0), S(1, 0, 0)), unary(0x43, 4, Rw(4, 0), Rw(2, 0)),
             unary(0x41, 4, Rw(5, 0), Rw(2, 1)), unary(0x48, 4, Rw(6, 0), Rw(2, 2)), unary(0x49, 4, Rw(7, 0), Rw(2, 3)),
             alu(0b011011101, 3, Rw(3, 1), Rw(1, 1), Rw(1, 2)), alu(0b011011110, 3, Rw(3, 2), Rw(1, 1), Rw(1, 2))},
            [](Allegrex& s) {
              s.vfpu.r[S(1, 0, 0)] = bits(0.25f);
              setQuad(s, Rw(2, 0), {1, 2, 3, 4}); setQuad(s, Rw(2, 1), {9, 2, 1, 4});
              setQuad(s, Rw(2, 2), {1, 4, 2, 9}); setQuad(s, Rw(2, 3), {1, 2, 4, 9});
              setQuad(s, Rw(1, 1), {1, 2, 3, 0}); setQuad(s, Rw(1, 2), {2, 2, 2, 0});
            });
  CHECK(lanes.cpu.vfpu.r[S(3, 0, 0)], bits(0.75f));
  CHECK(lanes.cpu.vfpu.r[S(3, 1, 0)], bits(0.25f));
  const float butterfly[] = {4, 6, -2, -2}, second[] = {4, 1, 2, 9}, third[] = {4, 1, 9, 2}, fourth[] = {9, 4, 2, 1};
  const float signs[] = {-1, 0, 1}, atLeast[] = {0, 1, 1};
  for(uint32_t lane = 0; lane < 4; lane++) {
    CHECK(lanes.cpu.vfpu.r[S(4, lane, 0)], bits(butterfly[lane]));
    CHECK(lanes.cpu.vfpu.r[S(5, lane, 0)], bits(second[lane]));
    CHECK(lanes.cpu.vfpu.r[S(6, lane, 0)], bits(third[lane]));
    CHECK(lanes.cpu.vfpu.r[S(7, lane, 0)], bits(fourth[lane]));
  }
  for(uint32_t lane = 0; lane < 3; lane++) {
    CHECK(lanes.cpu.vfpu.r[S(3, lane, 1)], bits(signs[lane]));
    CHECK(lanes.cpu.vfpu.r[S(3, lane, 2)], bits(atLeast[lane]));
  }

  //svr.q and svl.q together store a quad at an address aligned to 4 but not 16; vmtvc and vmfvc reach the
  //control registers (3 is the condition codes).
  Machine stores;
  stores.run({lui(s0, Data >> 16), ori(s0, s0, (Data & 0xffff) + 4),
              memory(0x3d, Rw(1, 0), 0, s0, 2 | 1), memory(0x3d, Rw(1, 0), 12, s0, 1),
              alu(0b110100000, 1, 3, S(2, 0, 0), 0x51), alu(0b110100000, 1, S(2, 1, 0), 3, 0x50)},
             [](Allegrex& s) { setQuad(s, Rw(1, 0), {5, 6, 7, 8}); s.vfpu.r[S(2, 0, 0)] = 0x2a; });
  for(uint32_t lane = 0; lane < 4; lane++) CHECK(stores.ram.read32(Data + 4 + 4 * lane), bits((float)(lane + 5)));
  CHECK(stores.cpu.vfpu.cc, 0x2a);
  CHECK(stores.cpu.vfpu.r[S(2, 1, 0)], 0x2a);

  //Which instructions use up the prefixes. vpfxs here negates rs's lane 0, so a vadd.s of 1 and 2 that still sees
  //it gives 1, and one after the prefixes were used up gives 3.
  auto oneAndTwo = [](Allegrex& s) { s.vfpu.r[S(1, 0, 0)] = bits(1.0f); s.vfpu.r[S(2, 0, 0)] = bits(2.0f); };
  uint32_t negateX = 0xe4 | 1 << 16;
  //vnop uses them up, as pspdev's documentation says.
  Machine nop;
  nop.run({vpfxs(negateX), vnop, alu(Vadd, 1, S(0, 0, 0), S(1, 0, 0), S(2, 0, 0))}, oneAndTwo);
  CHECK(nop.cpu.vfpu.r[S(0, 0, 0)], bits(3.0f));
  //vmfvc doesn't, and vmtvc can set one (control register 128 is rs's prefix).
  Machine control;
  control.run({vpfxs(negateX), alu(0b110100000, 1, S(3, 0, 0), 3, 0x50), alu(Vadd, 1, S(0, 0, 0), S(1, 0, 0), S(2, 0, 0)),
               alu(0b110100000, 1, 0, S(3, 1, 0), 0x51), alu(Vadd, 1, S(0, 1, 0), S(1, 0, 0), S(2, 0, 0))},
              [&](Allegrex& s) { oneAndTwo(s); s.vfpu.r[S(3, 1, 0)] = negateX; });
  CHECK(control.cpu.vfpu.r[S(0, 0, 0)], bits(1.0f));  //still negated after vmfvc
  CHECK(control.cpu.vfpu.r[S(0, 1, 0)], bits(1.0f));  //negated by the prefix vmtvc set
  //An instruction that raises an exception instead of running leaves them: here a reserved encoding of the vadd
  //group, and vcrs, which only exists for triples, used on a quad.
  for(uint32_t reserved : {0x61800000u, alu(Vcrs, 4, Rw(0, 0), Rw(1, 0), Rw(2, 0))}) {
    Machine fault;
    fault.run({vpfxs(negateX), reserved}, oneAndTwo);
    CHECK(fault.exceptions.size(), 1);
    CHECK(fault.cpu.vfpu.pfxs, negateX);
  }
}

//The random number generator. From power on, its state after each draw must be the one fp64 recovered from a
//PSP's output (PPSSPP issue 16946), unpacked here from the eight RCX registers into its five parts.
auto random() -> void {
  struct State { uint32_t a, b, c, d, e; };
  auto unpack = [](const Allegrex& cpu) -> State {
    auto& r = cpu.vfpu.rcx;
    State s{(r[0] & 0xffff) | r[4] << 16, (r[1] & 0xffff) | r[5] << 16, (r[2] & 0xffff) | r[6] << 16,
            (r[3] & 0xffff) | r[7] << 16, 0};
    for(uint32_t n = 0; n < 8; n++) s.e |= (r[n] >> 16 & 15) << (4 * n);
    return s;
  };
  auto vrndi = [](uint32_t size, uint32_t rd) { return unary(0x21, size, rd, 0); };

  const struct { uint32_t draws; State state; } recovered[] = {
    { 3, {0xc35937cc, 0x2998d882, 0x00000030, 0x00000074, 0}},
    { 4, {0x2e130a5d, 0x6398b906, 0x00000074, 0x00000118, 0}},
    {14, {0x22dc176b, 0xbc304c24, 0x000be744, 0x001cbcc0, 0}},
    {19, {0x9e3c9a7c, 0x1a7acf35, 0x03d038f0, 0x0934cf34, 0}},
  };
  for(auto& [draws, state] : recovered) {
    Machine m;
    std::vector<uint32_t> program(draws, vrndi(1, S(3, 0, 0)));
    m.run(program);
    State s = unpack(m.cpu);
    CHECK(s.a, state.a);
    CHECK(s.b, state.b);
    CHECK(s.c, state.c);
    CHECK(s.d, state.d);
    CHECK(s.e, state.e);
    CHECK(m.cpu.vfpu.r[S(3, 0, 0)], state.a + state.b + state.d);  //each draw gives a + b + d
  }

  //The registers start as 1, 2, 4 and 8 for c and d's low halves (and a's and b's), and always read 0x3f8 on top.
  Machine fresh;
  fresh.run({});
  CHECK(fresh.cpu.vfpu.rcx[0], 0x3f800001);
  CHECK(fresh.cpu.vfpu.rcx[3], 0x3f800008);
  CHECK(fresh.cpu.vfpu.rcx[7], 0x3f800000);

  //vrnds.s spreads the seed over the registers: a half each (low for 0-3, high for 4-7) and one nibble each.
  Machine seed;
  seed.run({alu(0b110100000, 1, 0, S(1, 0, 0), 0x20), mtv(t0, 128 + 8 + 2), mfv(t1, 128 + 8 + 2)},
           [](Allegrex& s) { s.vfpu.r[S(1, 0, 0)] = 0x12345678; s.ipu.r[t0] = 0xffffffff; });
  const uint32_t seeded[8] = {0x3f885678, 0x3f875678, 0x3f865678, 0x3f855678,
                              0x3f841234, 0x3f831234, 0x3f821234, 0x3f811234};
  for(uint32_t n = 0; n < 8; n++) if(n != 2) CHECK(seed.cpu.vfpu.rcx[n], seeded[n]);
  CHECK(seed.gpr(t1), 0x3f8fffff);  //mtvc keeps 20 bits of an RCX register

  //vrndi.q fills its lanes from the last back, and its destination prefix (here masking lane 0) applies to the
  //last lane only.
  Machine quad, singles, masked;
  quad.run({vrndi(4, Rw(3, 0))});
  singles.run({vrndi(1, S(2, 0, 0)), vrndi(1, S(2, 1, 0)), vrndi(1, S(2, 2, 0)), vrndi(1, S(2, 3, 0))});
  for(uint32_t lane = 0; lane < 4; lane++) CHECK(quad.cpu.vfpu.r[S(3, lane, 0)], singles.cpu.vfpu.r[S(2, 3 - lane, 0)]);
  masked.run({vpfxd(1 << 8), vrndi(2, Rw(3, 0))}, [](Allegrex& s) { setQuad(s, Rw(3, 0), {7, 7, 7, 7}); });
  CHECK(masked.cpu.vfpu.r[S(3, 0, 0)], singles.cpu.vfpu.r[S(2, 1, 0)]);  //the second number drawn
  CHECK(masked.cpu.vfpu.r[S(3, 1, 0)], bits(7.0f));                      //masked
}

auto vfpuTests() -> Tests {
  return {
    {"vfpu addressing", addressing}, {"vfpu arithmetic", arithmetic}, {"vfpu prefixes", prefixes},
    {"vfpu products", products}, {"vfpu comparisons", comparisons}, {"vfpu conversions", conversions},
    {"vfpu functions", functions}, {"vfpu matrices", matrices}, {"vfpu moves", moves}, {"vfpu more", more},
    {"vfpu random", random},
  };
}

}
