//Host tests for the Allegrex's interpreter (ares/psp/cpu): each runs a short program from RAM and checks the registers
//and memory. The encodings come from the MIPS32 manual; the Allegrex-only ones are the assembled examples in
//binutils' Allegrex test (gas/testsuite/gas/mips/allegrex.d).
//usage: tests/allegrex/run-tests.sh

#include "harness.hpp"

#include <chrono>
#include <string>

//Inside the harness's namespace, so register names like s1 win over ares's integer types of the same name.
namespace allegrex_test {
namespace {

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
        [](Allegrex& s) { s.ipu.r[t0] = 0x80000ff1; s.ipu.r[a0] = 36; });
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
    rotate.run({instruction}, [](Allegrex& s) { s.ipu.r[10] = 0x12345678; s.ipu.r[4] = 36; });
    CHECK(rotate.gpr(25), expected);
  }
}

auto allegrexBits() -> void {
  Machine m;
  m.run({
    0x7ca43980,  // ext $4,$5,6,8
    0x7c0a4420,  // seb $8,$10
    0x7c0a4620,  // seh $8,$10, over seb's result
  }, [](Allegrex& s) { s.ipu.r[5] = 0x12345678; s.ipu.r[10] = 0x000080f0; });
  CHECK(m.gpr(4), (0x12345678u >> 6) & 0xff);
  CHECK(m.gpr(8), 0xffff80f0);  // seh ran last

  Machine insert;
  insert.run({0x7ca46984}, [](Allegrex& s) { s.ipu.r[4] = 0xffffffff; s.ipu.r[5] = 0x12345600; });  // ins $4,$5,6,8
  CHECK(insert.gpr(4), 0xffffc03f);  // bits 6-13 from $5's low byte (0x00)

  Machine bytes;
  bytes.run({0x7c0a40a0}, [](Allegrex& s) { s.ipu.r[10] = 0x11223344; });  // wsbh $8,$10
  CHECK(bytes.gpr(8), 0x22114433);
  Machine word;
  word.run({0x7c0a40e0}, [](Allegrex& s) { s.ipu.r[10] = 0x11223344; });  // wsbw $8,$10
  CHECK(word.gpr(8), 0x44332211);
  Machine reverse;
  reverse.run({0x7c0a4520}, [](Allegrex& s) { s.ipu.r[10] = 0x00000001; });  // bitrev $8,$10
  CHECK(reverse.gpr(8), 0x80000000);
  Machine reverse2;
  reverse2.run({0x7c0a4520}, [](Allegrex& s) { s.ipu.r[10] = 0x12345678; });
  CHECK(reverse2.gpr(8), 0x1e6a2c48);
  Machine byteSign;
  byteSign.run({0x7c0a4420}, [](Allegrex& s) { s.ipu.r[10] = 0x000000f0; });  // seb $8,$10
  CHECK(byteSign.gpr(8), 0xfffffff0);

  Machine count;
  count.run({0x00402817, 0x00801816}, [](Allegrex& s) {  // clo $5,$2; clz $3,$4
    s.ipu.r[2] = 0xfff00000; s.ipu.r[4] = 0x0000ffff;
  });
  CHECK(count.gpr(5), 12);
  CHECK(count.gpr(3), 16);
  Machine countZero;
  countZero.run({0x00801816}, [](Allegrex& s) { s.ipu.r[4] = 0; });
  CHECK(countZero.gpr(3), 32);

  Machine minmax;
  minmax.run({0x0109382d, 0x0109502c}, [](Allegrex& s) {  // min $7,$8,$9; max $10,$8,$9
    s.ipu.r[8] = 0xfffffffe; s.ipu.r[9] = 3;
  });
  CHECK(minmax.gpr(7), 0xfffffffe);  // signed: -2 < 3
  CHECK(minmax.gpr(10), 3);

  Machine moves;
  moves.run({0x0064100a, 0x0064280b}, [](Allegrex& s) {  // movz $2,$3,$4; movn $5,$3,$4
    s.ipu.r[2] = 1; s.ipu.r[3] = 7; s.ipu.r[4] = 0; s.ipu.r[5] = 9;
  });
  CHECK(moves.gpr(2), 7);  // $4 is zero, so movz moves
  CHECK(moves.gpr(5), 9);  // and movn doesn't
}

auto multiplyDivide() -> void {
  Machine m;
  m.run({mult(t0, t1), mfhi(s0), mflo(s1), multu(t0, t1), mfhi(s2), mflo(s3)},
        [](Allegrex& s) { s.ipu.r[t0] = 0xfffffffe; s.ipu.r[t1] = 3; });
  CHECK(m.gpr(s0), 0xffffffff);  // -2 * 3 = -6
  CHECK(m.gpr(s1), 0xfffffffa);
  CHECK(m.gpr(s2), 2);           // 0xfffffffe * 3
  CHECK(m.gpr(s3), 0xfffffffa);

  Machine d;
  d.run({div_(t0, t1), mflo(s0), mfhi(s1), divu(t0, t1), mflo(s2), mfhi(s3)},
        [](Allegrex& s) { s.ipu.r[t0] = (uint32_t)-7; s.ipu.r[t1] = 2; });
  CHECK(d.gpr(s0), (uint32_t)-3);  // truncates toward zero
  CHECK(d.gpr(s1), (uint32_t)-1);
  CHECK(d.gpr(s2), 0x7ffffffc);
  CHECK(d.gpr(s3), 1);

  Machine zeroDivisor;
  zeroDivisor.run({div_(t0, zero), mflo(s0), mfhi(s1), div_(t1, zero), mflo(s2), divu(t0, zero), mflo(s3), mfhi(s4),
                   divu(t2, zero), mflo(s5)},
                  [](Allegrex& s) { s.ipu.r[t0] = 5; s.ipu.r[t1] = (uint32_t)-5; s.ipu.r[t2] = 0x10000; });
  CHECK(zeroDivisor.gpr(s0), 0xffffffff);
  CHECK(zeroDivisor.gpr(s1), 5);
  CHECK(zeroDivisor.gpr(s2), 1);
  CHECK(zeroDivisor.gpr(s3), 0x0000ffff);  //divu: 0xffff for a dividend below 0x10000, as a PSP gives
  CHECK(zeroDivisor.gpr(s4), 5);
  CHECK(zeroDivisor.gpr(s5), 0xffffffff);  //and all ones from 0x10000 up

  Machine overflowing;
  overflowing.run({div_(t0, t1), mflo(s0), mfhi(s1)},
                  [](Allegrex& s) { s.ipu.r[t0] = 0x80000000; s.ipu.r[t1] = (uint32_t)-1; });
  CHECK(overflowing.gpr(s0), 0x80000000);
  CHECK(overflowing.gpr(s1), 0);
  CHECK(overflowing.exceptions.size(), 0);

  Machine accumulate;
  accumulate.run({mthi(t2), mtlo(t3), 0x0109001c, mfhi(s0), mflo(s1),  // madd $8,$9
                  0x0109002e, mfhi(s2), mflo(s3),                       // msub $8,$9
                  0x0109001d, mfhi(s4), mflo(s5),                       // maddu $8,$9
                  0x0109002f, mfhi(s6), mflo(s7)},                      // msubu $8,$9
                 [](Allegrex& s) {
                   s.ipu.r[8] = 0xfffffffe; s.ipu.r[9] = 0x10; s.ipu.r[t2] = 0; s.ipu.r[t3] = 0x10;
                 });
  CHECK(accumulate.gpr(s0), 0xffffffff);  // 0x10 + (-2 * 16) = -16
  CHECK(accumulate.gpr(s1), 0xfffffff0);
  CHECK(accumulate.gpr(s2), 0);           // and back
  CHECK(accumulate.gpr(s3), 0x10);
  CHECK(accumulate.gpr(s4), 0xf);         // 0x10 + 0xfffffffe * 16 = 0xf_ffffffe0 + 0x10
  CHECK(accumulate.gpr(s5), 0xfffffff0);
  CHECK(accumulate.gpr(s6), 0);
  CHECK(accumulate.gpr(s7), 0x10);

  Machine wrapping;  // past the largest signed 64-bit total and back: the hardware wraps around
  wrapping.run({mthi(t2), mtlo(t3), 0x0109001c, mfhi(s0), mflo(s1),  // madd $8,$9
                0x0109002e, mfhi(s2), mflo(s3)},                     // msub $8,$9
               [](Allegrex& s) {
                 s.ipu.r[8] = 1; s.ipu.r[9] = 1; s.ipu.r[t2] = 0x7fffffff; s.ipu.r[t3] = 0xffffffff;
               });
  CHECK(wrapping.gpr(s0), 0x80000000);
  CHECK(wrapping.gpr(s1), 0);
  CHECK(wrapping.gpr(s2), 0x7fffffff);
  CHECK(wrapping.gpr(s3), 0xffffffff);
}

auto loadsStores() -> void {
  Machine m;
  m.run({
    lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff),
    sw(t0, 0, s0), sh(t1, 4, s0), sb(t1, 6, s0),
    lb(s1, 3, s0), lbu(s2, 3, s0), lh(s3, 2, s0), lhu(s4, 2, s0), lw(s5, 0, s0), lhu(s6, 4, s0), lbu(s7, 6, s0),
  }, [](Allegrex& s) { s.ipu.r[t0] = 0x8899aabb; s.ipu.r[t1] = 0x1234; });
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
          [](Allegrex& s) { s.ipu.r[t0] = 0xdeadbeef; });
    uint32_t expected = 0;
    for(uint32_t i = 0; i < 4; i++) expected |= (uint32_t)(0x10 + offset + i) << (i * 8);
    CHECK(m.gpr(t0), expected);

    Machine store;
    store.run({lui(s0, Data >> 16), ori(s0, s0, (Data & 0xffff) + offset), swr(t0, 0, s0), swl(t0, 3, s0)},
              [](Allegrex& s) { s.ipu.r[t0] = 0xa1b2c3d4; });
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
           [](Allegrex& s) { s.ipu.r[t0] = 0xaabbccdd; s.ipu.r[t1] = 0xaabbccdd; });
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
             [](Allegrex& s) { s.ipu.r[t0] = 1; s.ipu.r[t1] = 2; });
  CHECK(likely.gpr(s0), 0);
  CHECK(likely.gpr(s1), 1);
  CHECK(likely.gpr(s2), 1);  // bnel taken: its delay slot runs
  CHECK(likely.gpr(s3), 0);
  CHECK(likely.gpr(s4), 1);

  Machine compares;
  compares.run({blez(t0, 2), nop, addiu(s0, zero, 1), bgtz(t1, 2), nop, addiu(s1, zero, 1),
                bltz(t0, 2), nop, addiu(s2, zero, 1), bgez(zero, 2), nop, addiu(s3, zero, 1),
                bgezl(t0, 2), addiu(s4, zero, 1), addiu(s5, zero, 1)},
               [](Allegrex& s) { s.ipu.r[t0] = (uint32_t)-1; s.ipu.r[t1] = 1; });
  CHECK(compares.gpr(s0), 0);
  CHECK(compares.gpr(s1), 0);
  CHECK(compares.gpr(s2), 0);
  CHECK(compares.gpr(s3), 0);
  CHECK(compares.gpr(s4), 0);  // bgezl not taken: delay slot skipped
  CHECK(compares.gpr(s5), 1);

  // The link versions write ra even when they don't branch.
  Machine link;
  link.run({bltzal(t0, 2), nop, addiu(s0, zero, 1)}, [](Allegrex& s) { s.ipu.r[t0] = 1; });
  CHECK(link.gpr(ra), Base + 8);
  CHECK(link.gpr(s0), 1);

  Machine linkTaken;
  linkTaken.run({bgezal(zero, 2), nop, addiu(s0, zero, 1)});
  CHECK(linkTaken.gpr(ra), Base + 8);
  CHECK(linkTaken.gpr(s0), 0);

  // With ra as the source, the test reads ra from before the link: negative here, though the return address that
  // replaces it is positive.
  auto negativeRa = [](Allegrex& s) { s.ipu.r[ra] = 0x80000000; };
  Machine bltzalRa;
  bltzalRa.run({bltzal(ra, 2), nop, addiu(s0, zero, 1)}, negativeRa);
  CHECK(bltzalRa.gpr(ra), Base + 8);
  CHECK(bltzalRa.gpr(s0), 0);  // taken
  Machine bgezalRa;
  bgezalRa.run({bgezal(ra, 2), nop, addiu(s0, zero, 1)}, negativeRa);
  CHECK(bgezalRa.gpr(ra), Base + 8);
  CHECK(bgezalRa.gpr(s0), 1);  // not taken
  Machine bltzallRa;
  bltzallRa.run({bltzall(ra, 2), addiu(s1, zero, 1), addiu(s0, zero, 1)}, negativeRa);
  CHECK(bltzallRa.gpr(s1), 1);  // taken: the delay slot runs
  CHECK(bltzallRa.gpr(s0), 0);
  Machine bgezallRa;
  bgezallRa.run({bgezall(ra, 2), addiu(s1, zero, 1), addiu(s0, zero, 1)}, negativeRa);
  CHECK(bgezallRa.gpr(s1), 0);  // not taken: the delay slot is skipped
  CHECK(bgezallRa.gpr(s0), 1);

  Machine jump;
  jump.run({j(Base + 12), addiu(s0, zero, 1), addiu(s1, zero, 1)});
  CHECK(jump.gpr(s0), 1);  // the delay slot
  CHECK(jump.gpr(s1), 0);

  // jal links past its delay slot; jr returns; jalr links into rd.
  Machine calls;
  calls.run({jal(Base + 0x20), addiu(s0, zero, 1), addiu(s1, s1, 1), jalr(t9, t0), nop, addiu(s2, s2, 1), halt, halt,
             addiu(s3, zero, 1), jr(ra), addiu(s4, zero, 1), halt, halt, halt, halt, halt,
             addiu(s5, zero, 1), jr(t9), nop},
            [](Allegrex& s) { s.ipu.r[t0] = Base + 0x40; });
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
  m.cpu.syscallHook = [&](u32 code) -> bool {
    codes.push_back(code);
    resumeAt = m.cpu.ipu.pc;
    m.cpu.ipu.r[v0] = 42;
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
  overflow.run({add(t2, t0, t1)}, [](Allegrex& s) { s.ipu.r[t0] = 0x7fffffff; s.ipu.r[t1] = 1; s.ipu.r[t2] = 5; });
  CHECK(overflow.exceptions.size(), 1);
  CHECK(overflow.exceptions.empty() ? 0 : (uint32_t)overflow.exceptions[0].first, (uint32_t)Exception::Overflow);
  CHECK(overflow.exceptions.empty() ? 0 : overflow.exceptions[0].second, Base);
  CHECK(overflow.gpr(t2), 5);  // not written

  Machine wraps;
  wraps.run({addu(t2, t0, t1), addi(t3, t0, -1), sub(t4, t1, t0)},
            [](Allegrex& s) { s.ipu.r[t0] = 0x7fffffff; s.ipu.r[t1] = 1; });
  CHECK(wraps.gpr(t2), 0x80000000);
  CHECK(wraps.gpr(t3), 0x7ffffffe);
  CHECK(wraps.gpr(t4), 0x80000002);
  CHECK(wraps.exceptions.size(), 0);

  Machine misaligned;
  misaligned.run({lw(t1, 2, t0)}, [](Allegrex& s) { s.ipu.r[t0] = Data; s.ipu.r[t1] = 9; });
  CHECK(misaligned.exceptions.empty() ? 0 : (uint32_t)misaligned.exceptions[0].first, (uint32_t)Exception::AddressLoad);
  CHECK(misaligned.cpu.scc.r[8], Data + 2);
  CHECK(misaligned.gpr(t1), 9);

  Machine breakpoint;
  breakpoint.run({break_});
  CHECK(breakpoint.exceptions.empty() ? 0 : (uint32_t)breakpoint.exceptions[0].first, (uint32_t)Exception::Breakpoint);

  Machine cop3;
  cop3.run({0x4c000000});  // coprocessor 3, which the Allegrex doesn't have
  CHECK(cop3.exceptions.empty() ? 0 : (uint32_t)cop3.exceptions[0].first, (uint32_t)Exception::ReservedInstruction);
}

auto fpuArithmetic() -> void {
  Machine m;
  m.run({
    mtc1(t0, 0), mtc1(t1, 1),
    fop(0x00, 2, 0, 1), fop(0x01, 3, 0, 1), fop(0x02, 4, 0, 1), fop(0x03, 5, 0, 1), fop(0x04, 6, 1),
    fop(0x05, 7, 8), fop(0x07, 9, 0), fop(0x06, 10, 1), mfc1(s0, 2),
  }, [](Allegrex& s) {
    s.ipu.r[t0] = bits(6.0f); s.ipu.r[t1] = bits(4.0f); s.fpu.r[8] = bits(-1.5f);
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

  // The arithmetic follows the rounding mode in fcr31's low bits, as on a PSP (measured: round 3). 1 + 2^-30 is just
  // past 1: up gives the next float, the others 1. 1 - 1 is -0 rounding down. 1 / 3 lies between two floats: nearest
  // and up give the upper one, toward zero and down the lower. And 2^24 + 1, an integer a float can't hold, goes to
  // 2^24 + 2 rounding up and 2^24 otherwise (nearest goes to the even neighbour).
  struct Rounded { uint32_t sum, difference, third, integer; };
  const Rounded byMode[] = {
    {0x3f800000, 0x00000000, 0x3eaaaaab, 0x4b800000},  // nearest
    {0x3f800000, 0x00000000, 0x3eaaaaaa, 0x4b800000},  // toward zero
    {0x3f800001, 0x00000000, 0x3eaaaaab, 0x4b800001},  // up
    {0x3f800000, 0x80000000, 0x3eaaaaaa, 0x4b800000},  // down
  };
  for(uint32_t mode = 0; mode < 4; mode++) {
    Machine r;
    r.run({addiu(t0, zero, (int32_t)mode), ctc1(t0, 31), fop(0x00, 2, 0, 1), fop(0x01, 3, 0, 0), fop(0x03, 4, 0, 5),
           cvtsw(6, 7)},
          [](Allegrex& s) {
            s.fpu.r[0] = bits(1.0f); s.fpu.r[1] = 0x30800000;  // 2^-30
            s.fpu.r[5] = bits(3.0f); s.fpu.r[7] = (1 << 24) + 1;
          });
    CHECK(r.fpr(2), byMode[mode].sum);
    CHECK(r.fpr(3), byMode[mode].difference);
    CHECK(r.fpr(4), byMode[mode].third);
    CHECK(r.fpr(6), byMode[mode].integer);
  }

  // Moves keep the exact bits, NaN payloads included.
  Machine nan;
  nan.run({mtc1(t0, 3), fop(0x06, 4, 3), mfc1(s0, 4)}, [](Allegrex& s) { s.ipu.r[t0] = 0x7fa00001; });
  CHECK(nan.gpr(s0), 0x7fa00001);
}

auto fpuConversions() -> void {
  struct Case { float value; uint32_t round, trunc, ceil, floor; };
  const Case cases[] = {
    {2.5f, 2, 2, 3, 2}, {3.5f, 4, 3, 4, 3}, {-2.5f, (uint32_t)-2, (uint32_t)-2, (uint32_t)-2, (uint32_t)-3},
    {1.4999f, 1, 1, 2, 1}, {-2.7f, (uint32_t)-3, (uint32_t)-2, (uint32_t)-2, (uint32_t)-3},
    {3e9f, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff}, {-3e9f, 0x80000000, 0x80000000, 0x80000000, 0x80000000},
  };
  for(const Case& c : cases) {
    Machine m;
    m.run({fop(0x0c, 1, 0), fop(0x0d, 2, 0), fop(0x0e, 3, 0), fop(0x0f, 4, 0)},
          [&](Allegrex& s) { s.fpu.r[0] = bits(c.value); });
    CHECK(m.fpr(1), c.round);
    CHECK(m.fpr(2), c.trunc);
    CHECK(m.fpr(3), c.ceil);
    CHECK(m.fpr(4), c.floor);
  }

  Machine nan;
  nan.run({fop(0x0d, 1, 0)}, [](Allegrex& s) { s.fpu.r[0] = 0x7fc00000; });
  CHECK(nan.fpr(1), 0x7fffffff);

  // cvt.w.s follows the rounding mode in fcr31's low bits.
  const uint32_t byMode[] = {2, 2, 3, 2};
  for(uint32_t mode = 0; mode < 4; mode++) {
    Machine m;
    m.run({addiu(t0, zero, (int32_t)mode), ctc1(t0, 31), fop(0x24, 1, 0), cfc1(s0, 31)},
          [](Allegrex& s) { s.fpu.r[0] = bits(2.5f); });
    CHECK(m.fpr(1), byMode[mode]);
    CHECK(m.gpr(s0), mode);
  }

  Machine toFloat;
  toFloat.run({cvtsw(1, 0)}, [](Allegrex& s) { s.fpu.r[0] = (uint32_t)-7; });
  CHECK(toFloat.fpr(1), bits(-7.0f));
}

auto fpuCompare() -> void {
  // c.olt true, then bc1t taken; c.eq false, then bc1f taken; bc1tl not taken skips its delay slot.
  Machine m;
  m.run({ccond(CondOlt, 0, 1), bc1(Bc1t, 2), nop, addiu(s0, zero, 1),
         ccond(CondEq, 0, 1), bc1(Bc1f, 2), nop, addiu(s1, zero, 1),
         bc1(Bc1tl, 2), addiu(s2, zero, 1), addiu(s3, zero, 1)},
        [](Allegrex& s) { s.fpu.r[0] = bits(1.0f); s.fpu.r[1] = bits(2.0f); });
  CHECK(m.gpr(s0), 0);
  CHECK(m.gpr(s1), 0);
  CHECK(m.gpr(s2), 0);
  CHECK(m.gpr(s3), 1);
  CHECK(m.cpu.fpu.csr >> 23 & 1, 0);

  // With a NaN: unordered conditions are true, ordered ones false.
  Machine nan;
  nan.run({ccond(CondUn, 0, 1), cfc1(s0, 31), ccond(CondOlt, 0, 1), cfc1(s1, 31), ccond(CondUle, 0, 1), cfc1(s2, 31)},
          [](Allegrex& s) { s.fpu.r[0] = 0x7fc00000; s.fpu.r[1] = bits(2.0f); });
  CHECK(nan.gpr(s0) >> 23 & 1, 1);
  CHECK(nan.gpr(s1) >> 23 & 1, 0);
  CHECK(nan.gpr(s2) >> 23 & 1, 1);
}

auto fpuMemory() -> void {
  Machine m;
  m.run({lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff), swc1(5, 0, s0), lwc1(6, 0, s0), lw(s1, 0, s0)},
        [](Allegrex& s) { s.fpu.r[5] = bits(-0.75f); });
  CHECK(m.fpr(6), bits(-0.75f));
  CHECK(m.gpr(s1), bits(-0.75f));

  // With flush to zero (fcr31 bit 24) a subnormal result becomes zero.
  Machine flush;
  flush.run({lui(t0, 0x0100), ctc1(t0, 31), fop(0x02, 2, 0, 1)},
            [](Allegrex& s) { s.fpu.r[0] = bits(1e-30f); s.fpu.r[1] = bits(1e-10f); });
  CHECK(flush.fpr(2), 0);
  Machine keep;
  keep.run({fop(0x02, 2, 0, 1)}, [](Allegrex& s) { s.fpu.r[0] = bits(1e-30f); s.fpu.r[1] = bits(1e-10f); });
  CHECK(std::fpclassify(std::bit_cast<float>(keep.fpr(2))) == FP_SUBNORMAL, 1);
}

// A program run on a fresh interpreter and on a fresh recompiler (prepare: their RAM and page tables first, as alike):
// whether every register, the FPU's and VFPU's, the exceptions and all of RAM come out the same (each difference
// reported).
auto sameOnBoth(const char* name, const std::vector<uint32_t>& code, const std::function<void(Allegrex&)>& setup,
                const std::function<void(Machine&)>& prepare = {}) -> bool {
  Machine a, b;
  a.recompile = false, b.recompile = true;
  for(auto* m : {&a, &b}) {
    for(uint32_t n = 0; n < m->ram.bytes.size(); n++) m->ram.bytes[n] = uint8_t(n * 13 + n / 253);
    if(prepare) prepare(*m);
  }
  a.run(code, setup), b.run(code, setup);
  bool same = (uint32_t)a.cpu.fpu.csr == (uint32_t)b.cpu.fpu.csr && a.cpu.ipu.hi == b.cpu.ipu.hi
           && a.cpu.ipu.lo == b.cpu.ipu.lo && a.exceptions == b.exceptions && a.cpu.ipu.pc == b.cpu.ipu.pc
           && a.cpu.scc.r[8] == b.cpu.scc.r[8] && a.cpu.vfpu.cc == b.cpu.vfpu.cc;
  for(uint32_t n = 0; n < 32; n++) same &= a.gpr(n) == b.gpr(n) && a.fpr(n) == b.fpr(n);
  for(uint32_t n = 0; n < 128; n++) same &= a.cpu.vfpu.r[n] == b.cpu.vfpu.r[n];
  same &= a.ram.bytes == b.ram.bytes;
  if(!same) {
    failures++;
    std::printf("FAIL %s: %s differs\n", currentTest, name);
    for(uint32_t n = 0; n < 32; n++) {
      if(a.gpr(n) != b.gpr(n)) std::printf("    r%u %08x interpreted, %08x compiled\n", n, a.gpr(n), b.gpr(n));
      if(a.fpr(n) != b.fpr(n)) std::printf("    f%u %08x interpreted, %08x compiled\n", n, a.fpr(n), b.fpr(n));
    }
    std::printf("    fcr31 %08x, %08x; exceptions %zu, %zu; pc %08x, %08x\n", (uint32_t)a.cpu.fpu.csr,
                (uint32_t)b.cpu.fpu.csr, a.exceptions.size(), b.exceptions.size(), a.cpu.ipu.pc, b.cpu.ipu.pc);
    for(uint32_t word : code) std::printf("    %08x\n", word);
  }
  return same;
}

// Loads reach main memory's direct region (the largest aligned block of steady pages one after another in the host's
// memory) without the page table, and every other load through it, as cold code. Two layouts of the test's RAM: the
// region in its middle (pages 4 to 7, with pages 1 and 9 listed but not steady and page 8 the one the table leaves
// out), and at its very end (pages 12 to 15, the pages before it not steady). Each kind of load, from rs and with
// negative offsets, at the region's first and last bytes, just outside it on either side, misaligned, through other
// mirrors, on a steady page apart from it, on listed pages that aren't steady, and on the page the table leaves out,
// run once and again compiled with the listed pages that aren't steady then taken out of the table: the
// interpreter's registers and exceptions, and read() reached (on the recompiler) only for pages the table leaves out.
auto directLoads() -> void {
  auto lvs = [](uint32_t vt, uint32_t b) { return 0x32u << 26 | b << 21 | (vt & 31) << 16 | (vt >> 5 & 3); };
  auto lvq = [](uint32_t vt, uint32_t b) { return 0x36u << 26 | b << 21 | (vt & 31) << 16 | (vt >> 5 & 1); };
  struct Load { uint32_t word, size; int32_t offset; };
  const Load loads[] = {
    {lb(t0, 0, s0), 1, 0}, {lbu(t0, 0, s0), 1, 0}, {lh(t0, 0, s0), 2, 0}, {lhu(t0, 0, s0), 2, 0}, {lw(t0, 0, s0), 4, 0},
    {lwl(t0, 0, s0), 0, 0}, {lwr(t0, 0, s0), 0, 0}, {lwc1(3, 0, s0), 4, 0}, {lvs(0x25, s0), 4, 0},
    {lvq(0x21, s0), 16, 0}, {lw(t0, -4, s0), 4, -4}, {lh(t0, -2, s0), 2, -2}, {lb(t0, -1, s0), 1, -1},
  };
  auto page = [](uint32_t address) { return (address & 0x1fffffff) >> 12; };
  struct Layout { uint32_t first; std::vector<uint32_t> unsteady; std::vector<uint32_t> addresses; };
  constexpr uint32_t Middle = Base + 0x4000, Taken = Base + 0x9000, Apart = Base + 0x1000;
  constexpr uint32_t End = Base + 0x10000, Last = End - 0x4000;
  const Layout layouts[] = {
    {Middle, {Apart, Taken}, {Middle, Middle + 1, Middle + 2, Middle + 0x3ff0, Middle + 0x3ffc, Middle + 0x3fff,
      Middle - 16, Middle - 1, Unmapped, Unmapped + 3, Unmapped + 0x40, Taken, Taken + 2, Base + 0xa100, Apart,
      Apart + 2, 0x48804010, 0x88804020, 0xa8807ff0}},
    {Last, {Base, Base + 0x1000, Base + 0x2000, Base + 0x3000, Base + 0x4000, Base + 0x5000, Base + 0x6000,
      Base + 0x7000, Base + 0x9000, Base + 0xa000, Base + 0xb000}, {End - 1, End - 2, End - 3, End - 4, End - 8,
      End - 16, End - 15, Last, Last - 4, Last - 16, Last - 1, Last + 0x2000, (End - 16) | 0x40000000,
      (End - 4) | 0xa0000000}},
  };
  uint32_t mismatches = 0;
  for(auto& layout : layouts) {
    for(auto& load : loads) {
      for(uint32_t address : layout.addresses) {
        uint32_t effective = address + load.offset;
        bool aligned = !load.size || effective % load.size == 0;
        uint32_t from = load.size ? effective : effective & ~3u, bytes = load.size ? load.size : 4;
        //(an access the interpreter makes would stay inside the test's RAM, or it stops the run)
        if(aligned && ((from & 0x1fffffff) < (Base & 0x1fffffff) || (from & 0x1fffffff) + bytes > (End & 0x1fffffff))) {
          continue;
        }
        std::vector<uint32_t> code = {lui(s0, address >> 16), ori(s0, s0, address & 0xffff), load.word};
        auto setup = [](Allegrex& s) { s.ipu.r[t0] = 0x12345678; };
        Machine reference, m;
        reference.recompile = false;
        for(auto* machine : {&reference, &m}) {
          for(uint32_t n = 0; n < machine->ram.bytes.size(); n++) machine->ram.bytes[n] = uint8_t(n * 7 + n / 251);
        }
        for(uint32_t unsteady : layout.unsteady) m.cpu.steadyPages[page(unsteady)] = 0;
        reference.run(code, setup);
        for(uint32_t pass = 0; pass < 2; pass++) {
          uint32_t reads = 0;
          if(pass == 0) {
            m.run(code, setup);
            if(m.recompile) {
              CHECK(m.cpu.recompiler.direct.first, layout.first & 0x1fffffff);
              CHECK(m.cpu.recompiler.direct.size, 0x4000);
            }
          } else {
            for(uint32_t unsteady : layout.unsteady) if(page(unsteady) != page(Base)) m.cpu.table[page(unsteady)] = nullptr;
            m.cpu.ipu.pc = Base, m.cpu.ipu.pd = Base + 4, m.cpu.scc.halted = 0;
            m.exceptions.clear();
            setup(m.cpu);
            uint32_t before = m.cpu.reads;
            m.cpu.run(10000);
            reads = m.cpu.reads - before;
          }
          bool same = m.gpr(t0) == reference.gpr(t0) && m.fpr(3) == reference.fpr(3)
                   && m.exceptions == reference.exceptions && m.cpu.scc.r[8] == reference.cpu.scc.r[8];
          for(uint32_t n = 0; n < 128; n++) same &= m.cpu.vfpu.r[n] == reference.cpu.vfpu.r[n];
          bool unlisted = page(effective) == page(Unmapped);
          for(uint32_t unsteady : layout.unsteady) {
            unlisted |= pass == 1 && page(unsteady) != page(Base) && page(effective) == page(unsteady);
          }
          if(pass == 1 && m.recompile) same &= reads == (aligned && unlisted ? load.size == 16 ? 4u : 1u : 0u);
          if(!same && ++mismatches <= 5) {
            std::printf("FAIL %s: %08x at %08x, pass %u: t0 %08x vs %08x, %zu exceptions vs %zu, %u reads\n",
                        currentTest, load.word, address, pass, m.gpr(t0), reference.gpr(t0), m.exceptions.size(),
                        reference.exceptions.size(), reads);
          }
        }
      }
    }
  }
  CHECK(mismatches, 0);

  //Loads chasing pointers through the region: a result into its own base, with an offset and without, in a delay
  //slot, through the uncached mirror, and a load off the fast path (the page the table leaves out) between.
  if(!useRecompiler) return;
  constexpr uint32_t Nodes = Base + 0x3000;
  sameOnBoth("loads chasing pointers", {
    lui(t0, Nodes >> 16), ori(t0, t0, Nodes & 0xffff), addiu(s0, zero, 100), addiu(s1, zero, 0),
    lui(s2, Unmapped >> 16), ori(s2, s2, Unmapped & 0xffff),
    lw(t0, 0, t0), lw(t1, 4, t0), lw(t3, 0x20, s2), addu(s1, s1, t1), lw(t4, 8, t0), addu(s3, t4, t3),
    addiu(s0, s0, -1), bne(s0, zero, -8), lw(t0, 8, t0),
    lw(t0, 0, t0), lh(t5, 6, t0), lbu(t6, 4, t0), lb(t7, 7, t0),
  }, {}, [](Machine& m) {
    for(uint32_t i = 0; i < 8; i++) {
      uint32_t node = Nodes + 16 * i;
      m.ram.write32(node, (Nodes + 16 * ((i + 1) % 8)) | (i == 5 ? 0x4000'0000u : 0));
      m.ram.write32(node + 4, i * 3 + 1);
      m.ram.write32(node + 8, Nodes + 16 * ((i + 3) % 8));
    }
    m.ram.write32(Unmapped + 0x20, 0xabcd'ef01);
  });
}

// r0 as each operand of every native instruction, which compiled code reads as the constant 0 (and as a destination,
// which compiles to nothing); and read again after the interpreter ran something between: both engines alike.
auto zeroRegister() -> void {
  if(!useRecompiler) return;  //(it runs both engines itself)
  const std::vector<std::vector<uint32_t>> programs = {
    {addiu(t0, zero, -5)}, {slti(t0, zero, -1)}, {slti(t0, zero, 1)}, {sltiu(t0, zero, 1)}, {sltiu(t0, zero, -1)},
    {andi(t0, zero, 0xffff)}, {ori(t0, zero, 0x1234)}, {xori(t0, zero, 0x8000)},
    {addu(t0, zero, a0)}, {addu(t0, a0, zero)}, {subu(t0, zero, a0)}, {subu(t0, a0, zero)}, {addu(t0, zero, zero)},
    {and_(t0, zero, a0)}, {or_(t0, a0, zero)}, {xor_(t0, zero, a0)}, {nor(t0, zero, zero)}, {nor(t0, a0, zero)},
    {slt(t0, zero, a0)}, {slt(t0, a0, zero)}, {sltu(t0, zero, a0)}, {sltu(t0, a0, zero)}, {slt(t0, zero, zero)},
    {sll(t0, zero, 3)}, {srl(t0, zero, 31)}, {sra(t0, zero, 7)}, {rotr(t0, zero, 9)},
    {sllv(t0, zero, a1)}, {sllv(t0, a1, zero)}, {srlv(t0, zero, a1)}, {srlv(t0, a1, zero)}, {srav(t0, a0, zero)},
    {srav(t0, zero, a1)}, {rotrv(t0, a1, zero)}, {rotrv(t0, zero, a1)},
    {movz(t0, a0, zero)}, {movn(t0, a0, zero)}, {movz(t0, zero, a1)}, {movn(t0, zero, a1)}, {movz(t0, zero, zero)},
    {mthi(zero), mtlo(zero)}, {mult(zero, a0), mfhi(t0), mflo(t1)}, {multu(a0, zero), mfhi(t0), mflo(t1)},
    {madd(zero, a0), mfhi(t0), mflo(t1)}, {msubu(a0, zero), mfhi(t0), mflo(t1)}, {maddu(zero, zero), mflo(t1)},
    {div_(a0, zero), mfhi(t0), mflo(t1)}, {divu(zero, a0), mfhi(t0), mflo(t1)}, {div_(zero, a2), mflo(t1)},
    {clz(t0, zero)}, {clo(t0, zero)}, {max(t0, zero, a0)}, {min(t0, a0, zero)}, {max(t0, zero, zero)},
    {ext(t0, zero, 3, 5)}, {ins(t0, zero, 3, 5)}, {ins(a1, zero, 0, 32)}, {wsbh(t0, zero)}, {wsbw(t0, zero)},
    {seb(t0, zero)}, {seh(t0, zero)}, {add(t0, zero, a0)}, {add(t0, a0, zero)}, {sub(t0, zero, a2)},
    {sub(t0, zero, a0)}, {addi(t0, zero, -1)}, {addi(t0, zero, 0x7fff)},
    {sw(zero, 0, s7)}, {sh(zero, 2, s7)}, {sb(zero, 1, s7)}, {swl(zero, 1, s7)}, {swr(zero, 2, s7)},
    {lw(zero, 0, s7)}, {lb(zero, 3, s7)}, {lwl(zero, 1, s7)}, {lw(zero, 1, s7)}, {mtc1(zero, 3)},
    {mtc1(a0, 0), mfc1(zero, 0)}, {0x48e0'0005u /*mtv zero to VFPU register 5*/}, {0x4860'0005u /*mfv to zero*/},
    {beq(zero, zero, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {bne(zero, zero, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {beql(zero, a0, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {bnel(zero, zero, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {blez(zero, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {bgtz(zero, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {bltz(zero, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {bgez(zero, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {bgezal(zero, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {bltzall(zero, 2), addiu(t0, t0, 1), addiu(t1, t1, 1), addiu(t2, t2, 1)},
    {jalr(zero, s6), nop},
    {addiu(t0, zero, 1), bitrev(t1, a0), addu(t2, zero, t1), or_(t3, t1, zero)},
    {ctc1(zero, 31), cfc1(t0, 31), addu(t1, zero, t0)},
  };
  for(auto& code : programs) {
    sameOnBoth("r0 as an operand", code, [](Allegrex& s) {
      for(uint32_t n = 1; n < 32; n++) s.ipu.r[n] = 0x9e37'79b9u * n;
      s.ipu.r[a0] = 0x8000'0005, s.ipu.r[a1] = 0x1234'567f, s.ipu.r[a2] = 0x8000'0000;
      s.ipu.r[s6] = Base + 0x400, s.ipu.r[s7] = Data;
      s.ipu.hi = 0x1111'1111, s.ipu.lo = 0x2222'2222;
      s.fpu.csr = 0x0100'0003;
    }, [](Machine& m) { m.ram.write32(Base + 0x400, halt); });
  }
}

// With flush to zero (fcr31 bit 24), a subnormal input or result counts as the zero of its sign. The recompiler's
// native arithmetic and compares are compiled for the bit as it is when their block is compiled: they must give the
// interpreter's bits with it set, and a block compiled with it one way must still do so run with it the other way, or
// with another rounding mode.
auto fpuFlushing() -> void {
  static constexpr uint32_t Flush = 0x01000000;
  struct Case { uint32_t funct, s, t, flushed, kept; };
  const Case cases[] = {
    {0x00, 0x00000001, 0x00000000, 0x00000000, 0x00000001},  // a subnormal and +0
    {0x00, 0x80000001, 0x00000000, 0x00000000, 0x80000001},  // -0 + +0 is +0
    {0x00, 0x80000001, 0x80000000, 0x80000000, 0x80000001},  // -0 + -0 is -0
    {0x00, 0x00800000, 0x80000001, 0x00800000, 0x007fffff},  // the smallest normal less nothing, or less 2^-149
    {0x01, 0x00800001, 0x00800000, 0x00000000, 0x00000001},  // a difference of 2^-149
    {0x02, 0x80800000, 0x3f000000, 0x80000000, 0x80400000},  // -2^-127
    {0x02, 0x3f7fffff, 0x00800000, 0x00800000, 0x00800000},  // halfway below 2^-126, rounded up to it: normal, kept
    {0x03, 0x00000001, 0x3f800000, 0x00000000, 0x00000001},  // 2^-149 / 1
    {0x03, 0x00800000, 0x40000000, 0x00000000, 0x00400000},  // 2^-126 / 2
    {0x03, 0x00000001, 0x00000000, 0x7fc00000, 0x7f800000},  // flushed, 0 / 0: the NaN made from numbers
    {0x02, 0x80000001, 0x7f800000, 0x7fc00000, 0xff800000},  // flushed, -0 * infinity
  };
  for(const Case& c : cases) {
    for(uint32_t csr : {Flush, 0u}) {
      Machine m;
      m.run({fop(c.funct, 2, 0, 1)}, [&](Allegrex& s) { s.fpu.r[0] = c.s, s.fpu.r[1] = c.t, s.fpu.csr = csr; });
      CHECK(m.fpr(2), csr ? c.flushed : c.kept);
    }
  }
  // compares: +0 and a subnormal are equal flushed, -2^-149 below +2^-149 only kept
  Machine compares;
  compares.run({ccond(CondEq, 0, 1), cfc1(s0, 31), ccond(CondOlt, 2, 0), cfc1(s1, 31), ccond(CondUle, 0, 3), cfc1(s2, 31)},
               [](Allegrex& s) {
                 s.fpu.r[0] = 0x00000001, s.fpu.r[1] = 0x00000000, s.fpu.r[2] = 0x80000001, s.fpu.r[3] = 0x80000000;
                 s.fpu.csr = Flush;
               });
  CHECK(compares.gpr(s0), Flush | 1 << 23);
  CHECK(compares.gpr(s1), Flush);
  CHECK(compares.gpr(s2), Flush | 1 << 23);

  // Random arithmetic and compares on numbers round 2^-126, both engines bit for bit, mostly flushing.
  uint32_t seed = 0x6d2b79f5;
  auto random = [&](uint32_t range) -> uint32_t {
    seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
    return seed % range;
  };
  auto tiny = [&]() -> uint32_t {
    uint32_t sign = random(2) << 31;
    switch(random(9)) {
    case 0: return sign;
    case 1: return sign | (1 + random(0x7fffff));                    // subnormal
    case 2: return sign | (0x00800000 + random(0x800));               // just past the smallest normal
    case 3: return sign | (0x007ff000 + random(0x1000));              // just below it
    case 4: return sign | (1 + random(40)) << 23 | random(0x800000);  // small normal numbers
    case 5: return sign | (0x3e800000 + random(0x1000000));           // 1/4 to 1
    case 6: return sign | (0x3f800000 + random(0x1000000));           // 1 to 4
    case 7: return sign | 0x7f800000 | (random(3) ? 0 : 1 + random(0x7fffff));  // infinities, NaNs
    default: return sign | (100 + random(100)) << 23 | random(0x800000);
    }
  };
  if(!useRecompiler) return;
  uint32_t mismatches = 0;
  for(uint32_t program = 0; program < 1500; program++) {
    std::vector<uint32_t> code;
    for(uint32_t n = 0; n < 12; n++) {
      if(random(3)) code.push_back(fop(random(4), 16 + n, random(16), random(16)));
      else code.push_back(ccond(random(16), random(16), random(16))), code.push_back(cfc1(t0 + n % 8, 31));
    }
    uint32_t floats[16];
    for(auto& f : floats) f = tiny();
    uint32_t csr = random(8) ? Flush : random(2) ? 0 : Flush | (1 + random(3));
    auto setup = [&](Allegrex& s) {
      for(uint32_t n = 0; n < 16; n++) s.fpu.r[n] = floats[n];
      s.fpu.csr = csr;
    };
    Machine interpreter, recompiler;
    interpreter.recompile = false;
    recompiler.recompile = true;
    interpreter.run(code, setup);
    recompiler.run(code, setup);
    bool same = interpreter.cpu.fpu.csr == recompiler.cpu.fpu.csr;
    for(uint32_t n = 0; n < 32; n++) same &= interpreter.fpr(n) == recompiler.fpr(n) && interpreter.gpr(n) == recompiler.gpr(n);
    if(!same && ++mismatches <= 3) {
      std::printf("FAIL %s: program %u differs (fcr31 %08x)\n", currentTest, program, csr);
      for(uint32_t word : code) std::printf("  %08x\n", word);
    }
  }
  CHECK(mismatches, 0);

  // A block compiled with flushing one way, then run again (not compiled again) with it the other way, or with another
  // rounding mode: the interpreter's answers.
  const std::vector<uint32_t> code = {fop(0x00, 2, 0, 1), fop(0x02, 3, 0, 1), fop(0x03, 4, 1, 0),
                                      ccond(CondEq, 0, 5), cfc1(s0, 31), fop(0x01, 6, 1, 1)};
  auto inputs = [](Allegrex& s, uint32_t csr) {
    s.fpu.r[0] = 0x00400000, s.fpu.r[1] = 0x3f7fffff, s.fpu.r[5] = 0x80000000, s.fpu.csr = csr;
  };
  for(uint32_t compiled : {0u, Flush}) {
    for(uint32_t later : {0u, Flush, 1u, Flush | 3u}) {
      Machine reference;
      reference.recompile = false;
      reference.run(code, [&](Allegrex& s) { inputs(s, later); });
      Machine m;
      m.recompile = true;
      m.run(code, [&](Allegrex& s) { inputs(s, compiled); });
      m.cpu.ipu.pc = Base, m.cpu.ipu.pd = Base + 4, m.cpu.scc.halted = 0;
      for(uint32_t n = 0; n < 32; n++) m.cpu.fpu.r[n] = 0;
      inputs(m.cpu, later);
      m.cpu.run(10000);
      for(uint32_t n = 2; n <= 6; n++) CHECK(m.fpr(n), reference.fpr(n));
      CHECK(m.gpr(s0), reference.gpr(s0));
      CHECK(m.cpu.fpu.csr, reference.cpu.fpu.csr);
    }
  }

  // The bit switched by ctc1 in the middle of a block, either way, with another rounding mode too, and from r0: each
  // instruction after it checks the mode it was compiled for.
  for(uint32_t start : {0u, Flush, Flush | 1u}) {
    for(uint32_t other : {0u, Flush, Flush | 2u, 3u}) {
      std::vector<uint32_t> switching = {
        fop(0x00, 2, 0, 1), ctc1(t0, 31), fop(0x00, 3, 0, 1), fop(0x01, 4, 1, 0), fop(0x02, 5, 0, 6),
        fop(0x03, 7, 0, 6), ccond(CondEq, 0, 8), cfc1(s0, 31), ccond(CondOlt, 1, 8), cfc1(s3, 31),
        ctc1(t1, 31), fop(0x00, 9, 0, 1), ccond(CondEq, 0, 8), cfc1(s1, 31), fop(0x02, 10, 11, 12),
        ctc1(zero, 31), fop(0x02, 13, 11, 12), fop(0x00, 14, 0, 8), cfc1(s2, 31),
      };
      sameOnBoth("the flush bit switched in a block", switching, [&](Allegrex& s) {
        s.fpu.r[0] = 0x00400000, s.fpu.r[1] = 0x80000001, s.fpu.r[6] = 0x3f000000, s.fpu.r[8] = 0;
        s.fpu.r[11] = 0x3f7fffff, s.fpu.r[12] = 0x00800000;
        s.fpu.csr = start, s.ipu.r[t0] = other, s.ipu.r[t1] = start;
      });
    }
  }

  // The bit switched in a jump's delay slot, the arithmetic in the target's block, compiled before in the other mode.
  for(uint32_t start : {0u, Flush}) {
    constexpr uint32_t Target = Base + 0x2000;
    Machine pair[2];
    for(uint32_t engine : {0u, 1u}) {
      auto& m = pair[engine];
      m.recompile = engine == 1;
      m.ram.write32(Target, fop(0x00, 2, 0, 1));
      m.ram.write32(Target + 4, fop(0x02, 3, 0, 6));
      m.ram.write32(Target + 8, ccond(CondEq, 0, 8));
      m.ram.write32(Target + 12, cfc1(s0, 31));
      m.ram.write32(Target + 16, halt);
      auto setup = [&](Allegrex& s) {
        s.fpu.r[0] = 0x00400000, s.fpu.r[1] = 0x80000001, s.fpu.r[6] = 0x3f000000, s.fpu.r[8] = 0;
        s.fpu.csr = start, s.ipu.r[t0] = start ^ Flush;
      };
      m.run({j(Target), nop}, setup);
      m.cpu.write(Allegrex::Word, Base + 4, ctc1(t0, 31));
      m.cpu.ipu.pc = Base, m.cpu.ipu.pd = Base + 4, m.cpu.scc.halted = 0;
      setup(m.cpu);
      m.cpu.run(10000);
    }
    for(uint32_t n : {2u, 3u}) CHECK(pair[1].fpr(n), pair[0].fpr(n));
    CHECK(pair[1].gpr(s0), pair[0].gpr(s0));
    CHECK((uint32_t)pair[1].cpu.fpu.csr, (uint32_t)pair[0].cpu.fpu.csr);
  }
}

auto system() -> void {
  // mtic sets the interrupt flag and mfic reads it back; halt stops the CPU where it is.
  Machine m;
  m.run({0x70020026, 0x70080024}, [](Allegrex& s) { s.ipu.r[2] = 0; });  // mtic $2,$0; mfic $8,$0
  CHECK(m.cpu.scc.interrupts, 0);
  CHECK(m.gpr(8), 0);
  CHECK(m.cpu.ipu.pc, Base + 12);  // past the halt

  // The flag is on as a program starts, and mtic keeps the lowest bit of what it's given alone, as pspautotests'
  // intr/mfic recorded on a PSP (mtic of 2 and of 0x80000000 read back 0).
  Machine flag;
  flag.run({mfic(t0), mtic(t1), mfic(t2), mtic(t3), mfic(t4), mtic(t5), mfic(t6)}, [](Allegrex& s) {
    s.ipu.r[t1] = 2, s.ipu.r[t3] = 3, s.ipu.r[t5] = 0x80000000;
  });
  CHECK(flag.gpr(t0), 1);
  CHECK(flag.gpr(t2), 0);
  CHECK(flag.gpr(t4), 1);
  CHECK(flag.gpr(t6), 0);
  CHECK(flag.cpu.scc.interrupts, 0);

  Machine atomic;
  atomic.run({lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff), ll(t0, 0, s0), addiu(t0, t0, 1), sc(t0, 0, s0),
              lw(t1, 0, s0)},
             [](Allegrex&) {});
  CHECK(atomic.gpr(t0), 1);  // sc succeeded
  CHECK(atomic.gpr(t1), 1);

  Machine cache;
  cache.run({0xbc980004}, [](Allegrex& s) { s.ipu.r[4] = Data; });  // cache 0x18,4($4)
  CHECK(cache.exceptions.size(), 0);
}

// Cases the recompiler handles in ways of its own. They run on both engines, which must give the same answers.
auto recompilerCases() -> void {
  // A call and a return: on the recompiler, both blocks were compiled and kept.
  Machine cached;
  cached.run({addiu(s0, zero, 1), jal(Base + 0x10), nop, halt, addiu(s0, s0, 1), jr(ra), nop});
  CHECK(cached.gpr(s0), 2);
  if(cached.recompile) {
    auto& section = cached.cpu.recompiler.sections[(Base & 0x1fffffff) / 4096];
    CHECK(section && section->blocks[0], 1);  // the block at Base
    CHECK(section && section->blocks[4], 1);  // the function at Base + 0x10
  }

  // Code that rewrites a function it has already run: the second call runs the new instruction.
  std::vector<uint32_t> rewriting = {
    jal(Base + 0x40), nop,  // s0 = 1
    lui(t0, (Base + 0x40) >> 16), ori(t0, t0, (Base + 0x40) & 0xffff),
    lui(t1, addiu(s0, s0, 10) >> 16), ori(t1, t1, addiu(s0, s0, 10) & 0xffff),
    sw(t1, 0, t0),          // the function's first instruction becomes addiu s0,s0,10
    jal(Base + 0x40), nop,  // s0 = 11
    halt,
  };
  rewriting.resize(16, halt);
  for(uint32_t word : {addiu(s0, s0, 1), jr(ra), nop}) rewriting.push_back(word);  // at Base + 0x40
  Machine rewrite;
  rewrite.run(rewriting);
  CHECK(rewrite.gpr(s0), 11);

  // The same for a function whose page the owner takes out of the page table after it was compiled (as the PSP's
  // memory does with VRAM a renderer holds), rewrites, and puts back: the code compiled there is forgotten all the
  // same.
  constexpr uint32_t Function = Base + 0x2000;
  Machine unlisted;
  unlisted.cpu.steadyPages[(Function & 0x1fffffff) >> 12] = 0;
  unlisted.cpu.syscallHook = [&](u32) -> bool {
    uint32_t page = (Function & 0x1fffffff) >> 12;
    auto listed = unlisted.cpu.table[page];
    unlisted.cpu.table[page] = nullptr;
    unlisted.ram.write32(Function, addiu(s0, s0, 10));
    unlisted.cpu.recompiler.invalidateRange(Function, 4);
    unlisted.cpu.table[page] = listed;
    return true;
  };
  unlisted.run({jal(Function), nop, syscall(1), jal(Function), nop},
               [](Allegrex& s) {
                 auto& ram = static_cast<TestCPU&>(s).ram;
                 ram.write32(Function, addiu(s0, s0, 1));
                 ram.write32(Function + 4, jr(ra));
                 ram.write32(Function + 8, nop);
               });
  CHECK(unlisted.gpr(s0), 11);

  // A block that goes on into a page the owner may take out of the table (not steady), compiled with the page listed:
  // once it's out, and its bytes changed where the recompiler didn't hear of it (as the GE's workers draw into VRAM's
  // busy pages), the block leaves for run(), which interprets what's there now.
  {
    constexpr uint32_t Edge = Base + 0x1ffc, Out = Base + 0x2000;
    Machine m;
    m.cpu.steadyPages[(Out & 0x1fffffff) >> 12] = 0;
    m.ram.write32(Edge, addiu(s0, s0, 1));
    m.ram.write32(Out, addiu(s1, s1, 1));
    m.ram.write32(Out + 4, halt);
    m.run({j(Edge), nop});
    m.cpu.table[(Out & 0x1fffffff) >> 12] = nullptr;
    m.ram.write32(Out, addiu(s1, s1, 100));
    m.cpu.ipu.pc = Base, m.cpu.ipu.pd = Base + 4, m.cpu.scc.halted = 0;
    m.cpu.run(10000);
    CHECK(m.gpr(s0), 2);
    CHECK(m.gpr(s1), 101);
  }

  // A syscall in a delay slot that halts the CPU (as the kernel does when every thread waits on something): nothing
  // runs after it, though the jump's target is a block compiled and linked already (by a first run, not halting).
  {
    constexpr uint32_t Target = Base + 0x100;
    Machine m;
    bool halting = false;
    m.cpu.syscallHook = [&](u32) -> bool {
      if(halting) m.cpu.scc.halted = 1;
      return true;
    };
    m.ram.write32(Target, addiu(s1, s1, 1));
    m.ram.write32(Target + 4, halt);
    m.run({addiu(s0, s0, 1), j(Target), syscall(1)});
    halting = true;
    m.cpu.ipu.pc = Base, m.cpu.ipu.pd = Base + 4, m.cpu.scc.halted = 0;
    m.cpu.run(10000);
    CHECK(m.gpr(s0), 2);
    CHECK(m.gpr(s1), 1);
    CHECK(m.cpu.ipu.pc, Target);
  }

  // A branch in the last word of a 4 KiB section, so its delay slot is in the next one: taken, not taken, and a
  // likely branch not taken, which skips the delay slot.
  for(auto [branch, delaySlotRuns, nextRuns] : {std::tuple{beq(zero, zero, 2), 1u, 0u},
                                               std::tuple{bne(zero, zero, 2), 1u, 1u},
                                               std::tuple{bnel(zero, zero, 2), 0u, 1u}}) {
    Machine edge;
    edge.ram.write32(Base + 0xff8, addiu(s0, zero, 1));
    edge.ram.write32(Base + 0xffc, branch);
    edge.ram.write32(Base + 0x1000, addiu(s1, zero, 1));  // the delay slot
    edge.ram.write32(Base + 0x1004, addiu(s2, zero, 1));
    edge.ram.write32(Base + 0x1008, halt);
    edge.run({j(Base + 0xff8), nop});
    CHECK(edge.gpr(s0), 1);
    CHECK(edge.gpr(s1), delaySlotRuns);
    CHECK(edge.gpr(s2), nextRuns);
  }

  // The HLE kernel switching threads at a syscall in a delay slot, to a thread that stopped between a branch and
  // its delay slot: that delay slot runs, then the branch's target.
  Machine threads;
  threads.cpu.syscallHook = [&](u32) -> bool {
    threads.cpu.ipu.pc = Base + 0x100;
    threads.cpu.ipu.pd = Base + 0x200;
    return true;
  };
  threads.ram.write32(Base + 0x100, addiu(s1, zero, 1));
  threads.ram.write32(Base + 0x104, addiu(s2, zero, 1));
  threads.ram.write32(Base + 0x200, addiu(s3, zero, 1));
  threads.ram.write32(Base + 0x204, halt);
  threads.run({jal(Base + 0x10), nop, addiu(s0, zero, 1), halt, jr(ra), syscall(1)});
  CHECK(threads.gpr(s0), 0);  // the old thread never went on
  CHECK(threads.gpr(s1), 1);
  CHECK(threads.gpr(s2), 0);
  CHECK(threads.gpr(s3), 1);
  // The same from a delay slot of an FPU branch taken (one the recompiler would otherwise go on to the target of).
  Machine coprocessor;
  coprocessor.cpu.syscallHook = [&](u32) -> bool {
    coprocessor.cpu.ipu.pc = Base + 0x100;
    coprocessor.cpu.ipu.pd = Base + 0x200;
    return true;
  };
  coprocessor.ram.write32(Base + 0x100, addiu(s1, zero, 1));
  coprocessor.ram.write32(Base + 0x104, addiu(s2, zero, 1));
  coprocessor.ram.write32(Base + 0x200, addiu(s3, zero, 1));
  coprocessor.ram.write32(Base + 0x204, halt);
  coprocessor.run({mtc1(zero, 0), ccond(CondEq, 0, 0), bc1(Bc1t, 2), syscall(1), halt, addiu(s0, zero, 1), halt});
  CHECK(coprocessor.gpr(s0), 0);
  CHECK(coprocessor.gpr(s1), 1);
  CHECK(coprocessor.gpr(s2), 0);
  CHECK(coprocessor.gpr(s3), 1);

  // A store that rewrites the delay slot of an FPU branch taken later in its block: the new instruction runs.
  Machine slot;
  slot.run({lui(t0, Base >> 16), lui(t1, addiu(s0, s0, 2) >> 16), ori(t1, t1, addiu(s0, s0, 2) & 0xffff),
            mtc1(zero, 0), ccond(CondEq, 0, 0), sw(t1, 0x1c, t0), bc1(Bc1t, 2), addiu(s0, s0, 1), halt, halt});
  CHECK(slot.gpr(s0), 2);

  // The same function called through two mirrors of the memory: the return address it makes follows the mirror.
  std::vector<uint32_t> calls = {
    jal(Base + 0x40), nop, or_(s1, s0, zero),                 // through 0x08800000
    lui(t0, 0x4880), ori(t0, t0, 0x0040), jalr(ra, t0), nop,  // through 0x48800000
    halt,
  };
  calls.resize(16, halt);
  for(uint32_t word : {or_(t9, ra, zero), jal(Base + 0x80), nop, jr(t9), nop}) calls.push_back(word);  // Base + 0x40
  calls.resize(32, halt);
  for(uint32_t word : {or_(s0, ra, zero), jr(ra), nop}) calls.push_back(word);  // Base + 0x80
  Machine mirrors;
  mirrors.run(calls);
  CHECK(mirrors.gpr(s1), Base + 0x4c);
  CHECK(mirrors.gpr(s0), 0x40000000 | (Base + 0x4c));

  // Loads and stores: compiled ones reach RAM through the page table without write(), and the page left out of it
  // through write(); both give the same results as the interpreter.
  Machine memory;
  memory.run({lui(s0, Data >> 16), ori(s0, s0, Data & 0xffff), lui(s1, Unmapped >> 16), ori(s1, s1, Unmapped & 0xffff),
              sw(t0, 0, s0), sh(t0, 6, s0), sb(t0, 9, s0), lw(t1, 0, s0), lhu(t2, 6, s0), lb(t3, 9, s0),
              sw(t0, 0, s1), lw(t4, 0, s1), lh(t5, 2, s1)},
             [](Allegrex& s) { s.ipu.r[t0] = 0x89abcdef; });
  CHECK(memory.gpr(t1), 0x89abcdef);
  CHECK(memory.gpr(t2), 0xcdef);
  CHECK(memory.gpr(t3), 0xffffffef);
  CHECK(memory.gpr(t4), 0x89abcdef);
  CHECK(memory.gpr(t5), 0xffff89ab);
  CHECK(memory.ram.read32(Data + 4) >> 16, 0xcdef);
  CHECK(memory.cpu.writes, memory.recompile ? 1 : 4);  // on the recompiler, only the store to the unmapped page

  // An exception in the middle of a block: what ran before it stays done, nothing after it runs, pc is past it.
  Machine fault;
  fault.run({addiu(s0, zero, 1), lw(t0, 1, zero), addiu(s1, zero, 1)});
  CHECK(fault.gpr(s0), 1);
  CHECK(fault.gpr(s1), 0);
  CHECK(fault.exceptions.size(), 1);
  CHECK(fault.cpu.ipu.pc, Base + 8);
  CHECK(fault.cpu.scc.r[8], 1);
}

// Blocks that go on to the next by themselves (Recompiler::emitChain()) must stop a run after exactly the
// instructions it stopped after when each block left for run(), as where a run stops decides when interrupts and
// events land. A loop of blocks (an interpreted instruction, a syscall, a fall into the next section, a call, a
// likely branch both ways, a store that drops its own section's code, a jump through a register, an FPU branch
// taken; the page after the syscall's taken out of the page table now
// and then) runs in short runs of 1 to 37 instructions on two recompilers,
// one chaining and one not: each run must end at the same count, the same pc and pd and the same registers, and
// each syscall see the same count of instructions before it.
auto chainedRuns() -> void {
  if(!useRecompiler) return;
  constexpr uint32_t Loop = Base + 0xff0, Next = Base + 0x2040, Function = Base + 0x1100;
  struct Run {
    Machine m;
    std::vector<uint64_t> befores;
  } runs[2];
  for(uint32_t n = 0; n < 2; n++) {
    auto& m = runs[n].m;
    auto& befores = runs[n].befores;
    auto put = [&](uint32_t address, std::initializer_list<uint32_t> words) {
      for(uint32_t word : words) m.ram.write32(address, word), address += 4;
    };
    put(Base, {addiu(s0, zero, 0), lui(t2, Function >> 16), ori(t2, t2, Function & 0xffff), lw(t1, 0, t2),
               j(Loop), nop});
    put(Loop, {addiu(s0, s0, 1), bitrev(t3, s0), syscall(1), addiu(s4, s4, 3)});  // the last word of a section
    put(Base + 0x1000, {jal(Function), nop, beql(s1, zero, 2), addiu(s2, s2, 1), sw(t1, 0, t2),
                        lui(t0, Next >> 16), ori(t0, t0, Next & 0xffff), jr(t0), nop});
    //(the FPU branch is taken every other time round, so its delay slot is also where a block starts)
    put(Next, {mtc1(s1, 0), ccond(CondEq, 0, 1), bc1(Bc1f, 2), addiu(s5, s5, 1), addiu(s6, s6, 1),
               slti(t4, s0, 50), bne(t4, zero, -(int32_t)((Next + 28 - Loop) / 4)), nop, halt});
    put(Function, {addiu(s1, s1, 1), andi(s1, s1, 1), jr(ra), nop});
    m.cpu.syscallHook = [&m, &befores](u32) -> bool {
      befores.push_back(m.cpu.instructionsBefore());
      //Every third syscall takes the page the loop falls into out of the page table until the next one, as the
      //CPU's owner does with VRAM's pages while the GE's workers draw there: no chain may go on into it then.
      constexpr u32 Page = ((Base & 0x1fffffff) + 0x1000) >> 12;
      m.cpu.table[Page] = befores.size() % 3 == 1 ? nullptr : m.ram.bytes.data() + 0x1000;
      m.cpu.recompiler.writable(Page);
      return true;
    };
    m.cpu.steadyPages[((Base & 0x1fffffff) + 0x1000) >> 12] = 0;
    m.cpu.recompiler.enabled = true;
    m.cpu.recompiler.chains = n == 0;
    m.cpu.power(Base);
  }
  auto& chained = runs[0].m.cpu;
  auto& unchained = runs[1].m.cpu;
  uint32_t mismatches = 0;
  for(uint32_t round = 0; round < 2000 && !chained.scc.halted; round++) {
    uint32_t limit = 1 + round % 37;
    uint64_t a = chained.run(limit), b = unchained.run(limit);
    bool same = a == b && chained.ipu.pc == unchained.ipu.pc && chained.ipu.pd == unchained.ipu.pd;
    for(uint32_t r = 0; r < 32; r++) same = same && chained.ipu.r[r] == unchained.ipu.r[r];
    if(!same && ++mismatches <= 3) {
      std::printf("FAIL %s: run %u of %u: %llu vs %llu instructions, pc %08x vs %08x\n", currentTest, round, limit,
                  (unsigned long long)a, (unsigned long long)b, chained.ipu.pc, unchained.ipu.pc);
    }
  }
  CHECK(mismatches, 0);
  CHECK(chained.scc.halted, 1);
  CHECK(unchained.scc.halted, 1);
  CHECK(chained.ipu.r[s0], 50);
  CHECK(chained.ipu.r[s5], 50);
  CHECK(chained.ipu.r[s6], 25);
  CHECK(runs[0].befores == runs[1].befores, 1);
  CHECK(runs[0].befores.size(), 50);
}

// Runs asked for more instructions than a signed 64-bit count holds (2^63 and more), or nearly: a loop runs to its
// halt, every instruction counted (the count left is in a register as compiled code runs, on ARM64).
auto hugeRuns() -> void {
  for(uint64_t limit : {~0ull, 1ull << 63, (1ull << 63) - 1, 1ull << 62}) {
    Machine m;
    uint32_t address = Base;
    for(uint32_t word : {addiu(s0, zero, 0), addiu(s0, s0, 1), slti(t0, s0, 1000), bne(t0, zero, -3), nop, halt}) {
      m.ram.write32(address, word), address += 4;
    }
    m.cpu.recompiler.enabled = m.recompile;
    m.cpu.power(Base);
    uint64_t ran = m.cpu.run(limit);
    CHECK(m.gpr(s0), 1000);
    CHECK(m.cpu.scc.halted, 1);
    CHECK(ran, 1 + 1000 * 4 + 1);
  }
}

// Syscalls that move a run on: one bringing its limit forward (as the kernel does when something it set off comes due
// sooner), one counting instructions it skipped (as the kernel's spins do), every third call. A loop calling an import
// stub (a jr ra with its syscall in the delay slot) runs in short runs on two recompilers, one chaining (counting the
// run in a register where the host has one) and one not: each run must end at the same count, pc and registers.
auto movedRuns() -> void {
  if(!useRecompiler) return;  //(it runs both engines itself)
  constexpr uint32_t Stub = Base + 0x200;
  struct Run { Machine m; uint32_t calls = 0; } runs[2];
  for(uint32_t n = 0; n < 2; n++) {
    auto& run = runs[n];
    run.m.cpu.syscallHook = [&run](u32) -> bool {
      auto& cpu = run.m.cpu;
      if(++run.calls % 3 == 1) cpu.runLimit = std::min<u64>(cpu.runLimit, cpu.instructionsBefore() + 2);
      if(run.calls % 3 == 2) cpu.instructionsRun += 5;
      return true;
    };
    for(uint32_t address = Base; uint32_t word : {addiu(s0, s0, 1), jal(Stub), nop, slti(t0, s0, 200),
                                                  bne(t0, zero, -5), nop, halt}) {
      run.m.ram.write32(address, word), address += 4;
    }
    run.m.ram.write32(Stub, jr(ra));
    run.m.ram.write32(Stub + 4, syscall(1));
    run.m.cpu.recompiler.enabled = run.m.recompile = true;
    run.m.cpu.recompiler.chains = n == 1;
    run.m.cpu.power(Base);
  }
  auto& a = runs[0].m.cpu;
  auto& b = runs[1].m.cpu;
  uint32_t mismatches = 0;
  for(uint32_t round = 0; round < 3000 && !a.scc.halted; round++) {
    uint32_t limit = 1 + round % 23;
    uint64_t x = a.run(limit), y = b.run(limit);
    bool same = x == y && a.ipu.pc == b.ipu.pc && a.ipu.pd == b.ipu.pd && a.ipu.r[s0] == b.ipu.r[s0];
    if(!same && ++mismatches <= 3) {
      std::printf("FAIL %s: run %u of %u: %llu vs %llu instructions, pc %08x vs %08x\n", currentTest, round, limit,
                  (unsigned long long)x, (unsigned long long)y, a.ipu.pc, b.ipu.pc);
    }
  }
  CHECK(mismatches, 0);
  CHECK(a.scc.halted, 1);
  CHECK(b.scc.halted, 1);
  CHECK(b.ipu.r[s0], 200);

  //The same with two threads the hook switches between every other call (pc, pd and every register, as the kernel
  //switches them), the limit brought forward every fifth call and skipped instructions counted every third.
  constexpr uint32_t A = Base, B = Base + 0x1000;
  struct Thread { Machine m; uint32_t calls = 0; decltype(Allegrex::ipu) other{}; } threads[2];
  for(uint32_t n = 0; n < 2; n++) {
    auto& thread = threads[n];
    thread.m.cpu.syscallHook = [&thread](u32) -> bool {
      auto& cpu = thread.m.cpu;
      thread.calls++;
      if(thread.calls % 2 == 0) std::swap(cpu.ipu, thread.other);
      if(thread.calls % 5 == 1) cpu.runLimit = std::min<u64>(cpu.runLimit, cpu.instructionsBefore() + 3);
      if(thread.calls % 3 == 2) cpu.instructionsRun += 7;
      return true;
    };
    for(uint32_t address = A; uint32_t word : {addiu(s0, s0, 1), jal(Stub), nop, slti(t0, s0, 300), bne(t0, zero, -5),
                                               nop, halt}) {
      thread.m.ram.write32(address, word), address += 4;
    }
    for(uint32_t address = B; uint32_t word : {addiu(s1, s1, 2), lw(t1, 0, s2), addu(s3, s3, t1), jal(Stub), nop,
                                               j(B), nop}) {
      thread.m.ram.write32(address, word), address += 4;
    }
    thread.m.ram.write32(Stub, jr(ra));
    thread.m.ram.write32(Stub + 4, syscall(1));
    thread.m.ram.write32(Base + 0x3000, 5);
    thread.m.cpu.recompiler.enabled = thread.m.recompile = true;
    thread.m.cpu.recompiler.chains = n == 1;
    thread.m.cpu.power(A);
    thread.other = thread.m.cpu.ipu;
    thread.other.pc = B, thread.other.pd = B + 4, thread.other.r[s2] = Base + 0x3000;
  }
  auto& c = threads[0].m.cpu;
  auto& d = threads[1].m.cpu;
  mismatches = 0;
  for(uint32_t round = 0; round < 5000 && !(c.scc.halted && d.scc.halted); round++) {
    uint32_t limit = 1 + round % 29;
    uint64_t x = c.run(limit), y = d.run(limit);
    bool same = x == y && c.ipu.pc == d.ipu.pc && c.ipu.pd == d.ipu.pd && c.ipu.r[s0] == d.ipu.r[s0]
             && c.ipu.r[s1] == d.ipu.r[s1] && c.ipu.r[s3] == d.ipu.r[s3] && threads[0].calls == threads[1].calls;
    if(!same && ++mismatches <= 3) {
      std::printf("FAIL %s: switching threads, run %u of %u: %llu vs %llu instructions, pc %08x vs %08x\n",
                  currentTest, round, limit, (unsigned long long)x, (unsigned long long)y, c.ipu.pc, d.ipu.pc);
    }
  }
  CHECK(mismatches, 0);
  CHECK(threads[1].calls > 100, true);
}

// The coprocessors' branches taken: a chaining recompiler runs the delay slot and goes on to the target itself
// (emitCoprocessorBranch()), where one that doesn't chain leaves for run(), which has the interpreter take the delay
// slot. Every run must still stop after the same instructions, in the same state: FPU and VFPU branches, likely or
// not, taken every other time round, with delay slots native, interpreted, storing, loading from a page the page
// table leaves out, a syscall (which leaves for run() still), a target just past the delay slot (where run() starts
// a block at the delay slot instead), a branch in a section's last word whose delay slot, in the next section, the
// loop rewrites, and at the end a delay slot whose load raises an exception, taken.
auto coprocessorBranchesTaken() -> void {
  if(!useRecompiler) return;
  constexpr uint32_t Loop = Base + 0x100, Edge = Base + 0xff8;
  auto mtvc = [](uint32_t t, uint32_t index) { return 0x48e00000u | t << 16 | 0x80 | index; };
  auto mtv = [](uint32_t t, uint32_t vd) { return 0x48e00000u | t << 16 | vd; };
  auto vaddS = [](uint32_t vd, uint32_t vs, uint32_t vt) { return 0x60000000u | vt << 16 | vs << 8 | vd; };
  auto bv = [](bool onTrue, bool likely, uint32_t bit, int32_t o) {
    return 0x49000000u | likely << 17 | onTrue << 16 | bit << 18 | ((uint32_t)o & 0xffff);
  };
  struct Run {
    Machine m;
    std::vector<uint64_t> befores;
  } runs[2];
  for(uint32_t n = 0; n < 2; n++) {
    auto& m = runs[n].m;
    auto& befores = runs[n].befores;
    auto put = [&](uint32_t address, std::initializer_list<uint32_t> words) {
      for(uint32_t word : words) m.ram.write32(address, word), address += 4;
    };
    put(Base, {addiu(s0, zero, 0), lui(t2, Data >> 16), ori(t2, t2, Data & 0xffff), lui(t3, Unmapped >> 16),
               ori(t3, t3, Unmapped & 0xffff), lui(t1, addiu(t6, t6, 1) >> 16), ori(t1, t1, addiu(t6, t6, 1) & 0xffff),
               lui(a1, (Edge + 8) >> 16), ori(a1, a1, (Edge + 8) & 0xffff), lui(a3, 0x3f80), mtv(a3, 2), j(Loop), nop});
    put(Loop, {
      addiu(s0, s0, 1), andi(t0, s0, 1), mtc1(t0, 0), mtc1(zero, 1), ccond(CondEq, 0, 1),  //true when s0 is even
      addu(a2, t1, t0), sw(a2, 0, a1),                             //the edge's delay slot, next section's, rewritten
      bc1(Bc1t, 2), addiu(s1, s1, 1), addiu(s2, s2, 1),           //native delay slot
      mtvc(t0, 3),                                                 //cc bit 0: s0 odd
      bv(true, false, 0, 2), bitrev(s3, s0), addiu(s4, s4, 1),    //interpreted delay slot
      bc1(Bc1fl, 2), sw(s0, 0, t2), addiu(s5, s5, 1),              //likely, a store in its delay slot
      bv(false, true, 0, 2), lw(s6, 0, t3), addiu(s7, s7, 1),      //likely, a load through read()
      bc1(Bc1f, 1), addiu(t4, t4, 1),                              //to just past its delay slot
      bv(true, false, 0, 2), syscall(1), addiu(t5, t5, 1),         //a syscall in its delay slot
      bv(true, false, 0, 2), vaddS(1, 1, 2), addiu(v0, v0, 1),     //a VFPU delay slot, the prefixes at rest
      0xdc0100e4, bv(false, false, 0, 2), vaddS(3, 3, 2), addiu(v1, v1, 1),  //vpfxs (lane 0 negated) before it
      j(Edge), nop,
    });
    put(Edge, {ccond(CondEq, 0, 1), bc1(Bc1t, 2), addiu(t6, t6, 1), addiu(t7, t7, 1),  //the last word, and across
               slti(t9, s0, 60), bne(t9, zero, -(int32_t)((Edge + 24 - Loop) / 4)), nop,
               bc1(Bc1t, 2), lw(t8, 2, t2), nop, halt});               //taken (60 is even), its load misaligned
    m.ram.write32(Unmapped, 0x1234'5678);
    m.cpu.syscallHook = [&m, &befores](u32) -> bool {
      befores.push_back(m.cpu.instructionsBefore());
      return true;
    };
    m.cpu.recompiler.enabled = true;
    m.cpu.recompiler.chains = n == 0;
    m.cpu.power(Base);
  }
  auto& chained = runs[0].m.cpu;
  auto& unchained = runs[1].m.cpu;
  uint32_t mismatches = 0;
  for(uint32_t round = 0; round < 4000 && !(chained.scc.halted && unchained.scc.halted); round++) {
    uint32_t limit = 1 + round % 23;
    uint64_t a = chained.run(limit), b = unchained.run(limit);
    bool same = a == b && chained.ipu.pc == unchained.ipu.pc && chained.ipu.pd == unchained.ipu.pd &&
                chained.scc.halted == unchained.scc.halted;
    for(uint32_t r = 0; r < 32; r++) same = same && chained.ipu.r[r] == unchained.ipu.r[r];
    for(uint32_t r = 0; r < 32; r++) same = same && chained.fpu.r[r] == unchained.fpu.r[r];
    for(uint32_t r = 0; r < 128; r++) same = same && chained.vfpu.r[r] == unchained.vfpu.r[r];
    same = same && chained.fpu.csr == unchained.fpu.csr && chained.vfpu.cc == unchained.vfpu.cc &&
           chained.vfpu.pfxs == unchained.vfpu.pfxs && chained.vfpu.pfxt == unchained.vfpu.pfxt &&
           chained.vfpu.pfxd == unchained.vfpu.pfxd && runs[0].m.ram.bytes == runs[1].m.ram.bytes;
    if(!same && ++mismatches <= 3) {
      std::printf("FAIL %s: run %u of %u: %llu vs %llu instructions, pc %08x vs %08x\n", currentTest, round, limit,
                  (unsigned long long)a, (unsigned long long)b, chained.ipu.pc, unchained.ipu.pc);
    }
  }
  CHECK(mismatches, 0);
  CHECK(chained.scc.halted, 1);
  CHECK(chained.ipu.r[s0], 60);
  CHECK(chained.ipu.r[s1], 60);  //every delay slot ran, taken or not, but the likely ones' not taken
  CHECK(chained.ipu.r[s2], 30);
  CHECK(chained.ipu.r[s4], 30);
  CHECK(chained.ipu.r[s5], 30);
  CHECK(chained.ipu.r[s7], 30);
  CHECK(chained.ipu.r[t4], 60);
  CHECK(chained.ipu.r[t5], 30);
  CHECK(chained.ipu.r[t6], 90);  //1 when s0 is even, 2 when odd
  CHECK(chained.ipu.r[t7], 30);
  CHECK(chained.ipu.r[s6], 0x1234'5678);
  CHECK(chained.ipu.r[v0], 30);  //(each VFPU branch not taken when s0 is odd, the other when it's even)
  CHECK(chained.ipu.r[v1], 30);
  CHECK(chained.vfpu.r[1], 0x4270'0000u);  //60.0: every VFPU delay slot ran
  CHECK(runs[0].befores == runs[1].befores, 1);
  CHECK(runs[0].befores.size(), 60);
  CHECK(runs[0].m.exceptions.size(), 1);
  CHECK(runs[0].m.exceptions == runs[1].m.exceptions, 1);
  CHECK(runs[0].m.ram.read32(Data), 59);
}

// A native branch's delay slot is compiled along each way the branch goes, pc and pd stored only where the block
// leaves or calls the interpreter (emitDelaySlot()). Every branch, taken and not (a likely one skipping its delay
// slot then), and every jump, with a delay slot of each kind: native, interpreted, a syscall and a load raising an
// exception (whose hooks record pc and pd as they are then, and the instructions run before), a syscall that moves pc
// and pd (as a thread switch does: to the word after the delay slot, whichever way the branch went), a load from a
// page the page table leaves out, a division by zero and an FPU division giving NaN (cold code that comes back), a
// VFPU instruction after mtvc set a prefix the block can't know (the interpreter's way, cold) and a reserved VFPU
// encoding (which raises). The interpreter and the recompiler must record the same and end the same, and runs of 1 to
// 37 instructions at a time, chained and not, must stop in the same places.
auto delaySlotsEachWay() -> void {
  if(!useRecompiler) return;
  enum : uint32_t { Taken = 48, Case = 64 };  //(each case: 16 words, the taken way's from word 12)
  struct Form { uint32_t word; bool likely; int32_t a, b; };  //the branch at word 5, rs = t0 = a, rt = t1 = b
  constexpr int32_t offset = (Taken - 24) / 4;
  const Form forms[] = {
    {beq(t0, t1, offset), 0, 3, 3}, {beq(t0, t1, offset), 0, 3, 4}, {bne(t0, t1, offset), 0, 3, 4},
    {bne(t0, t1, offset), 0, 3, 3}, {blez(t0, offset), 0, -1, 0}, {blez(t0, offset), 0, 1, 0},
    {bgtz(t0, offset), 0, 1, 0}, {bgtz(t0, offset), 0, 0, 0}, {bltz(t0, offset), 0, -1, 0},
    {bltz(t0, offset), 0, 0, 0}, {bgez(t0, offset), 0, 0, 0}, {bgez(t0, offset), 0, -1, 0},
    {bltzal(t0, offset), 0, -1, 0}, {bgezal(t0, offset), 0, -1, 0}, {beql(t0, t1, offset), 1, 3, 3},
    {beql(t0, t1, offset), 1, 3, 4}, {bnel(t0, t1, offset), 1, 3, 4}, {bnel(t0, t1, offset), 1, 3, 3},
    {blezl(t0, offset), 1, 0, 0}, {blezl(t0, offset), 1, 2, 0}, {bgtzl(t0, offset), 1, 2, 0},
    {bgtzl(t0, offset), 1, -2, 0}, {bltzl(t0, offset), 1, -2, 0}, {bltzl(t0, offset), 1, 2, 0},
    {bgezl(t0, offset), 1, 2, 0}, {bgezl(t0, offset), 1, -2, 0}, {bltzall(t0, offset), 1, -5, 0},
    {bgezall(t0, offset), 1, -5, 0}, {j(0), 0, 0, 0}, {jal(0), 0, 0, 0}, {jr(t2), 0, 0, 0}, {jalr(t3, t2), 0, 0, 0},
  };
  //each delay slot, and the word before the branch (mtvc t5 into pfxs for the VFPU instruction after it)
  const uint32_t mtvc = 0x48e0'0080u | t5 << 16, vaddS = 0x6000'0000u | 1 << 16 | 2 << 8 | 3;
  const std::pair<uint32_t, uint32_t> slots[] = {{nop, addiu(s2, s2, 3)}, {nop, bitrev(s3, t0)}, {nop, syscall(5)},
    {nop, lw(t9, 1, s7)}, {nop, lw(t8, 0, s6)}, {nop, syscall(6)}, {nop, div_(t0, zero)}, {nop, fop(0x03, 2, 0, 1)},
    {mtvc, vaddS}, {nop, 0xd019'0000u}};
  struct Event {
    uint32_t kind, what, pc, pd;
    uint64_t before;
    auto operator==(const Event&) const -> bool = default;
  };
  struct Run {
    Machine m;
    std::vector<Event> events;
  } runs[3];  //the interpreter; the recompiler; the recompiler without chains
  for(uint32_t n : range(3)) {
    auto& m = runs[n].m;
    auto& events = runs[n].events;
    uint32_t at = Base, cases = 0;
    for(auto& form : forms) {
      for(auto [before, slot] : slots) {
        uint32_t next = at + Case;
        uint32_t word = form.word;
        if(word >> 26 == 0x02 || word >> 26 == 0x03) word |= (at + Taken) >> 2 & 0x03ff'ffff;
        uint32_t words[16] = {addiu(t0, zero, form.a), addiu(t1, zero, form.b), lui(t2, (at + Taken) >> 16),
                              ori(t2, t2, (at + Taken) & 0xffff), before, word, slot, addiu(s0, s0, 1), j(next), nop,
                              nop, nop, addiu(s1, s1, 1), j(next), nop, nop};
        for(uint32_t k : range(16)) m.ram.write32(at + 4 * k, words[k]);
        at = next, cases++;
      }
    }
    m.ram.write32(at, halt);
    m.cpu.syscallHook = [&m, &events](u32 code) -> bool {
      events.push_back({1, code, m.cpu.ipu.pc, m.cpu.ipu.pd, m.cpu.instructionsBefore()});
      if(code == 6) m.cpu.ipu.pc = m.cpu.pipeline.address + 4, m.cpu.ipu.pd = m.cpu.ipu.pc + 4;
      return true;
    };
    m.cpu.exceptionHook = [&m, &events](Exception, u32 address) {  //(not halting: the program goes on)
      events.push_back({2, address, m.cpu.ipu.pc, m.cpu.ipu.pd, m.cpu.instructionsBefore()});
    };
    m.cpu.recompiler.enabled = n != 0;
    m.cpu.recompiler.chains = n != 2;
    m.cpu.power(Base);
    m.cpu.ipu.r[s7] = Data, m.cpu.ipu.r[s6] = Unmapped, m.cpu.ipu.r[t5] = 0x55;
    m.ram.write32(Unmapped, 0x1234'5678);
  }
  auto same = [&](uint32_t x, uint32_t y) {
    auto& a = runs[x].m.cpu;
    auto& b = runs[y].m.cpu;
    bool same = a.ipu.pc == b.ipu.pc && a.ipu.pd == b.ipu.pd && a.scc.halted == b.scc.halted;
    same = same && a.ipu.hi == b.ipu.hi && a.ipu.lo == b.ipu.lo && a.fpu.csr == b.fpu.csr;
    same = same && a.vfpu.pfxs == b.vfpu.pfxs && a.vfpu.pfxt == b.vfpu.pfxt && a.vfpu.pfxd == b.vfpu.pfxd;
    for(uint32_t r : range(32)) same = same && a.ipu.r[r] == b.ipu.r[r] && a.fpu.r[r] == b.fpu.r[r];
    for(uint32_t r : range(128)) same = same && a.vfpu.r[r] == b.vfpu.r[r];
    return same;
  };
  runs[0].m.cpu.run(100'000);
  runs[1].m.cpu.run(100'000);
  CHECK(same(0, 1), 1);
  CHECK(runs[0].events == runs[1].events, 1);
  CHECK(runs[0].m.cpu.scc.halted, 1);
  CHECK(runs[0].events.size(), 4 * std::size(forms) - 4 * 7);  //(the 7 likely branches not taken skip theirs)
  //chained against unchained, a few instructions at a time (runs[1] again, from the start)
  runs[1].events.clear();
  runs[1].m.cpu.ipu = runs[2].m.cpu.ipu, runs[1].m.cpu.fpu = runs[2].m.cpu.fpu, runs[1].m.cpu.vfpu = runs[2].m.cpu.vfpu;
  runs[1].m.cpu.scc.halted = 0;
  uint32_t mismatches = 0;
  for(uint32_t round = 0; round < 20000 && !runs[1].m.cpu.scc.halted; round++) {
    uint32_t limit = 1 + round % 37;
    uint64_t a = runs[1].m.cpu.run(limit), b = runs[2].m.cpu.run(limit);
    if((a != b || !same(1, 2)) && ++mismatches <= 3) {
      std::printf("FAIL %s: run %u of %u: %llu vs %llu instructions, pc %08x vs %08x\n", currentTest, round, limit,
                  (unsigned long long)a, (unsigned long long)b, runs[1].m.cpu.ipu.pc, runs[2].m.cpu.ipu.pc);
    }
  }
  CHECK(mismatches, 0);
  CHECK(runs[1].m.cpu.scc.halted, 1);
  CHECK(runs[1].events == runs[2].events, 1);  //(the instructions before each counted from its run's start)
  CHECK(runs[1].events.size(), runs[0].events.size());
}

// Blocks linked to each other (recompiler.cpp's exits: a jump straight to the target's body) unlinked when the
// target's code goes: a function a call site has linked to, rewritten by the program itself halfway; a loop that
// rewrites its own first instruction on every round, with a larger constant each time (its block, running on from a
// section that went, mustn't go on to its old self); and a function called through two mirrors in turn (its section
// compiled afresh for each), which keeps the address bgezal links (as it runs through either mirror), called also from
// a site first compiled right after a call through the other mirror (whose exit mustn't take the code compiled for
// that mirror). The recompiler must end as the interpreter does, links made and waiting, also with code
// memory small enough to start afresh every few blocks; and runs of a few instructions at a time, chained and not, must
// stop in the same places.
auto linkedBlocks() -> void {
  if(!useRecompiler) return;
  constexpr uint32_t Function = Base + 0x1000, Loop = Base + 0x2000, Mirrored = Function | 0x4000'0000;
  auto program = [&](Machine& m) {
    auto put = [&](uint32_t address, std::initializer_list<uint32_t> words) {
      for(uint32_t word : words) m.ram.write32(address, word), address += 4;
    };
    //main: 40 rounds of calling the function, rewriting its first word at round 20, every third call through the
    //uncached mirror, and at round 30, right after a call through the mirror, a call from a site compiled only then;
    //then the self-rewriting loop
    put(Base, {addiu(s0, zero, 0), lui(t7, Function >> 16), ori(t7, t7, Function & 0xffff), lui(t3, Mirrored >> 16),
               ori(t3, t3, Mirrored & 0xffff), lui(t6, addiu(s1, s1, 100) >> 16),
               ori(t6, t6, addiu(s1, s1, 100) & 0xffff), addiu(t5, zero, 20),
               /*32, a round:*/ addiu(s0, s0, 1), bne(s0, t5, 2), nop, sw(t6, 0, t7),
               /*48:*/ addiu(t4, zero, 3), div_(s0, t4), mfhi(t4), beq(t4, zero, 5), nop,
               /*68:*/ jal(Function), nop, j(Base + 92), nop,
               /*84:*/ jalr(ra, t3), nop,
               /*92:*/ addiu(t9, zero, 30), bne(s0, t9, 3), nop, jal(Function), nop,
               /*112:*/ slti(t0, s0, 40), bne(t0, zero, -22), nop, j(Loop), nop});
    //the function: what bgezal links, its own address through the mirror it runs from, goes into s4
    put(Function, {addiu(s1, s1, 1), addu(t2, ra, zero), bgezal(zero, 1), nop, xor_(s4, s4, ra), jr(t2), nop});
    //the loop: its first word becomes addiu s2,s2,50 by the store in it, then 51, 52 and so on, a round each
    put(Loop, {lui(t8, addiu(s2, s2, 50) >> 16), ori(t8, t8, addiu(s2, s2, 50) & 0xffff), lui(t9, (Loop + 16) >> 16),
               ori(t9, t9, (Loop + 16) & 0xffff), /*16:*/ addiu(s2, s2, 1), sw(t8, 0, t9), addiu(t8, t8, 1),
               addiu(s3, s3, 1), slti(t0, s3, 30), bne(t0, zero, -6), nop, halt});
  };
  Machine runs[3];  //the interpreter, the recompiler, the recompiler without chains
  for(uint32_t n : range(3)) {
    program(runs[n]);
    runs[n].cpu.recompiler.enabled = n != 0;
    runs[n].cpu.recompiler.chains = n != 2;
    runs[n].cpu.power(Base);
  }
  runs[0].cpu.run(100'000);
  runs[1].cpu.run(100'000);
  bool same = true;
  for(uint32_t r : range(32)) same = same && runs[0].cpu.ipu.r[r] == runs[1].cpu.ipu.r[r];
  CHECK(same, 1);
  CHECK(runs[0].cpu.scc.halted, 1);
  CHECK(runs[1].cpu.scc.halted, 1);
  CHECK(runs[0].gpr(s1), 19 + 22 * 100);  //(round 20's call already took the rewritten word; round 30 called twice)
  CHECK(runs[1].gpr(s1), 19 + 22 * 100);
  CHECK(runs[0].gpr(s2), 1 + (50 + 78) * 29 / 2);  //(round r took the word round r - 1 stored: 50 + r - 2)
  CHECK(runs[1].gpr(s2), 1 + (50 + 78) * 29 / 2);
  //(links were made: the call site's to the function, and j Loop's waits since the loop's first block went)
  CHECK(runs[1].cpu.recompiler.into.empty(), 0);
  CHECK(runs[1].cpu.recompiler.waiting.empty(), 0);
  //with code memory so small it starts afresh every few blocks, links and all
  Machine tight;
  program(tight);
  tight.cpu.recompiler.enabled = true;
  tight.cpu.recompiler.codeMemory = 1_MiB + 8_KiB;  //(it starts afresh with under 1 MiB left)
  tight.cpu.power(Base);
  tight.cpu.run(100'000);
  same = tight.cpu.scc.halted;
  for(uint32_t r : range(32)) same = same && runs[0].cpu.ipu.r[r] == tight.cpu.ipu.r[r];
  CHECK(same, 1);
  //chained against unchained, a few instructions at a time, from the start again
  for(uint32_t n : {1u, 2u}) {
    program(runs[n]);
    runs[n].cpu.power(Base);
  }
  uint32_t mismatches = 0;
  for(uint32_t round = 0; round < 20000 && !runs[1].cpu.scc.halted; round++) {
    uint32_t limit = 1 + round % 37;
    uint64_t a = runs[1].cpu.run(limit), b = runs[2].cpu.run(limit);
    bool alike = a == b && runs[1].cpu.ipu.pc == runs[2].cpu.ipu.pc && runs[1].cpu.ipu.pd == runs[2].cpu.ipu.pd;
    for(uint32_t r : range(32)) alike = alike && runs[1].cpu.ipu.r[r] == runs[2].cpu.ipu.r[r];
    if(!alike && ++mismatches <= 3) {
      std::printf("FAIL %s: run %u of %u: %llu vs %llu instructions, pc %08x vs %08x\n", currentTest, round, limit,
                  (unsigned long long)a, (unsigned long long)b, runs[1].cpu.ipu.pc, runs[2].cpu.ipu.pc);
    }
  }
  CHECK(mismatches, 0);
  CHECK(runs[1].gpr(s1), 19 + 22 * 100);
  CHECK(runs[1].gpr(s2), 1 + (50 + 78) * 29 / 2);

  //A block in main RAM (steady: no page check going on to it) rewritten after a block was linked to it: by a store in
  //the linking block itself, through each mirror, into the linking block's own page or another; and by a write the
  //owner reports (as an HLE copy or DMA does). Each time the new code runs.
  for(uint32_t far : {0u, 1u}) {
    for(uint32_t mirror : {0u, 0x4000'0000u, 0x8000'0000u, 0xa000'0000u}) {
      const uint32_t target = far ? Base + 0x2000 : Base + 0x100, through = (target & 0x1fff'ffff) | mirror;
      Machine m;
      m.ram.write32(target, addiu(s1, s1, 1));
      m.ram.write32(target + 4, halt);
      m.ram.write32(Data, addiu(s1, s1, 1));
      m.run({lui(t0, through >> 16), ori(t0, t0, through & 0xffff), lui(t2, Data >> 16), ori(t2, t2, Data & 0xffff),
             lw(t1, 0, t2), beq(s2, zero, 2), nop, sw(t1, 0, t0), addiu(s0, s0, 1), j(target), nop});
      CHECK(m.gpr(s1), 1);
      m.ram.write32(Data, addiu(s1, s1, 100));
      m.cpu.ipu.r[s2] = 1;
      for(uint32_t round = 0; round < 2; round++) {
        m.cpu.ipu.pc = Base, m.cpu.ipu.pd = Base + 4, m.cpu.scc.halted = 0;
        m.cpu.run(10000);
      }
      CHECK(m.gpr(s1), 201);
      CHECK(m.gpr(s0), 3);
    }
  }
  {
    constexpr uint32_t Target = Base + 0x2000;
    Machine m;
    m.ram.write32(Target, addiu(s1, s1, 1));
    m.ram.write32(Target + 4, halt);
    uint32_t calls = 0;
    m.cpu.syscallHook = [&](u32) -> bool {
      if(++calls == 2) {
        m.ram.write32(Target, addiu(s1, s1, 50));
        m.cpu.recompiler.invalidateRange(Target, 4);
      }
      return true;
    };
    m.run({syscall(0), addiu(s0, s0, 1), j(Target), nop});
    for(uint32_t round = 0; round < 2; round++) {
      m.cpu.ipu.pc = Base, m.cpu.ipu.pd = Base + 4, m.cpu.scc.halted = 0;
      m.cpu.run(10000);
    }
    CHECK(m.gpr(s1), 101);
  }
}

// Not a test: with ALLEGREX_BENCHMARK set, times a loop of loads, stores and arithmetic on the interpreter, on the
// recompiler without a page table (its loads and stores call the interpreter), and on the recompiler. For numbers
// that mean something, build without the sanitizers: SANITIZE= ALLEGREX_BENCHMARK=1 tests/allegrex/run-tests.sh
auto benchmark() -> void {
  constexpr uint64_t Instructions = 50'000'000;
  const char* names[] = {"interpreter", "recompiler without a page table", "recompiler"};
  for(uint32_t engine : {0, 1, 2}) {
    bool recompile = engine > 0;
    Machine m;
    if(engine == 1) m.cpu.pages = nullptr;
    uint32_t address = Base;
    for(uint32_t word : {lw(t0, 0, s0), addiu(t0, t0, 1), sw(t0, 0, s0), addu(t1, t1, t0), sll(t2, t1, 3),
                         xor_(t3, t2, t1), andi(t4, t3, 0xff), lbu(t5, 5, s0), sltu(t6, t4, t5),
                         beq(zero, zero, -10), addiu(t7, t7, 1)}) {
      m.ram.write32(address, word);
      address += 4;
    }
    m.cpu.recompiler.enabled = recompile;
    m.cpu.power(Base);
    m.cpu.ipu.r[s0] = Data;
    auto start = std::chrono::steady_clock::now();
    uint64_t executed = m.cpu.run(Instructions);
    std::chrono::duration<double> seconds = std::chrono::steady_clock::now() - start;
    std::printf("%s: %.0f million instructions a second\n", names[engine], executed / seconds.count() / 1e6);
  }
}

// Random programs, each run by the interpreter and by the recompiler, which must end in exactly the same state.
// Control only ever moves forward, so every program reaches its halt.
auto matchesInterpreter() -> void {
  uint32_t seed = 0x2545f491;
  auto next = [&]() -> uint32_t {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  auto random = [&](uint32_t range) -> uint32_t { return next() % range; };
  // any register but s6 and s7, which hold the address of the halt after the program and of the data
  auto anyRegister = [&]() -> uint32_t { uint32_t r = random(30); return r >= s6 ? r + 2 : r; };
  // values that make compares and branches go both ways
  auto value = [&]() -> uint32_t {
    switch(random(6)) {
    case 0: return 0;
    case 1: return random(16);
    case 2: return -random(16);
    case 3: return 0x80000000u >> random(32);
    default: return next();
    }
  };
  // floats at the edges the FPU's native code steps around: zeros, infinities, NaNs (quiet and signaling), the
  // subnormals and the smallest normal numbers, the edge of what converts to a word, and ordinary numbers
  auto floatValue = [&]() -> uint32_t {
    uint32_t sign = random(2) << 31;
    switch(random(10)) {
    case 0: return sign;
    case 1: return sign | 0x7f800000;
    case 2: return sign | 0x7fc00000 | random(0x400000);
    case 3: return sign | 0x7f800000 | (1 + random(0x3fffff));
    case 4: return sign | (random(2) ? 1 + random(0x7fffff) : 0x00800000 + random(0x100));
    case 5: return sign | (0x4effff00 + random(0x200));
    case 6: return sign | (0x3f800000 + random(0x800000));
    case 7: return value();
    default: return sign | (100 + random(56)) << 23 | random(0x800000);
    }
  };

  // VFPU encodings: an operation with three 7-bit registers and a size of 1 to 4 lanes, and a VFPU branch
  auto vfpuOp = [](uint32_t opcode, uint32_t size, uint32_t rd, uint32_t rs, uint32_t rt) -> uint32_t {
    return opcode << 23 | rt << 16 | (size >= 3) << 15 | rs << 8 | (size == 2 || size == 4) << 7 | rd;
  };
  auto bv = [](uint32_t kind, uint32_t bit, int32_t offset) -> uint32_t {
    return 0x49000000u | bit << 18 | kind << 16 | ((uint32_t)offset & 0xffff);
  };

  constexpr uint32_t Length = 40, Programs = 2000;
  uint32_t mismatches = 0;
  for(uint32_t program = 0; program < Programs; program++) {
    std::vector<uint32_t> code;
    bool afterBranch = false;
    while(code.size() < Length) {
      uint32_t index = code.size();
      uint32_t d = anyRegister(), s = anyRegister(), t = random(4) ? anyRegister() : zero;
      int32_t immediate = (int32_t)random(0x10000) - 0x8000;

      // A branch goes forward, at most as far as the halt after the program; it never sits in a delay slot, or
      // last (where a likely branch not taken would skip the halt).
      bool branch = !afterBranch && index + 1 < Length && random(5) == 0;
      afterBranch = branch;
      if(branch) {
        int32_t offset = 1 + random(std::min<uint32_t>(8, Length - index - 1));
        uint32_t target = Base + (index + 1 + offset) * 4;
        switch(random(21)) {
        case 20: code.push_back(bv(random(4), random(6), offset)); break;  // bvf, bvt, bvfl, bvtl
        case  0: code.push_back(beq(s, t, offset)); break;
        case  1: code.push_back(bne(s, t, offset)); break;
        case  2: code.push_back(blez(s, offset)); break;
        case  3: code.push_back(bgtz(s, offset)); break;
        case  4: code.push_back(beql(s, t, offset)); break;
        case  5: code.push_back(bnel(s, t, offset)); break;
        case  6: code.push_back(blezl(s, offset)); break;
        case  7: code.push_back(bgtzl(s, offset)); break;
        case  8: code.push_back(bltz(s, offset)); break;
        case  9: code.push_back(bgez(s, offset)); break;
        case 10: code.push_back(bltzl(s, offset)); break;
        case 11: code.push_back(bgezl(s, offset)); break;
        case 12: code.push_back(bltzal(s, offset)); break;
        case 13: code.push_back(bgezal(s, offset)); break;
        case 14: code.push_back(bltzall(s, offset)); break;
        case 15: code.push_back(bgezall(s, offset)); break;
        case 16: code.push_back(j(target)); break;
        case 17: code.push_back(jal(target)); break;
        case 18: code.push_back(bc1(random(4), offset)); break;
        case 19: code.push_back(random(2) ? jr(s6) : jalr(random(2) ? ra : d, s6)); break;  // to the halt
        }
        continue;
      }

      // memory: 256 bytes of data at s7 (in the recompiler's direct region), or at s7 + 0x1000, the page left out of the
      // page table (so compiled code reaches it through read() and write()); aligned, and now and then not
      uint32_t region = random(4) ? 0 : 0x1000;
      uint32_t wordOffset = region + random(64) * 4 + (random(30) ? 0 : 1 + random(3));
      uint32_t halfOffset = region + random(128) * 2 + (random(30) ? 0 : 1);
      uint32_t byteOffset = region + random(256);
      uint32_t word = 0;
      uint32_t vd = random(128), vs = random(128), vt = random(128), size = 1 + random(4);
      switch(random(62)) {
      case  0: word = addiu(d, s, immediate); break;
      case  1: word = slti(d, s, immediate); break;
      case  2: word = sltiu(d, s, immediate); break;
      case  3: word = andi(d, s, immediate); break;
      case  4: word = ori(d, s, immediate); break;
      case  5: word = xori(d, s, immediate); break;
      case  6: word = lui(d, immediate); break;
      case  7: word = addu(d, s, t); break;
      case  8: word = subu(d, s, t); break;
      case  9: word = and_(d, s, t); break;
      case 10: word = or_(d, s, t); break;
      case 11: word = xor_(d, s, t); break;
      case 12: word = nor(d, s, t); break;
      case 13: word = slt(d, s, t); break;
      case 14: word = sltu(d, s, t); break;
      case 15: word = sll(d, t, random(32)); break;
      case 16: word = srl(d, t, random(32)); break;
      case 17: word = sra(d, t, random(32)); break;
      case 18: word = sllv(d, t, s); break;
      case 19: word = srlv(d, t, s); break;
      case 20: word = srav(d, t, s); break;
      case 21: word = rotr(d, t, random(32)); break;
      case 22: word = rotrv(d, t, s); break;
      case 23: word = movz(d, s, t); break;
      case 24: word = movn(d, s, t); break;
      case 25: word = random(2) ? mfhi(d) : mflo(d); break;
      case 26: word = random(2) ? mthi(s) : mtlo(s); break;
      case 27: word = random(2) ? seb(d, t) : seh(d, t); break;
      case 28: word = random(2) ? max(d, s, t) : min(d, s, t); break;
      // ones that leave some inputs to the interpreter (add, addi and sub may overflow, which ends the program;
      // division by zero and by -1) or the whole instruction (bitrev)
      case 29: word = random(2) ? addi(d, s, immediate) : add(d, s, t); break;
      case 30: word = sub(d, s, t); break;
      case 31: word = random(2) ? mult(s, t) : multu(s, t); break;
      case 32: word = random(2) ? div_(s, t) : divu(s, t); break;
      case 33: word = random(2) ? (random(2) ? madd(s, t) : maddu(s, t)) : (random(2) ? msub(s, t) : msubu(s, t)); break;
      case 34: word = random(2) ? clz(d, s) : clo(d, s); break;
      case 35: word = random(3) == 0 ? wsbh(d, t) : random(2) ? wsbw(d, t) : bitrev(d, t); break;
      case 36: {
        uint32_t lsb = random(32), size = 1 + random(32 - lsb);
        word = random(2) ? ext(d, s, lsb, size) : ins(d, s, lsb, size);
        // now and then fields that run past bit 31, or an ins whose top is below its bottom
        if(!random(4)) word = (word & ~0xffc0u) | random(32) << 11 | random(32) << 6;
        break;
      }
      case 37: word = lw(d, wordOffset, s7); break;
      case 38: word = sw(t, wordOffset, s7); break;
      case 39: word = random(2) ? lh(d, halfOffset, s7) : lhu(d, halfOffset, s7); break;
      case 40: word = sh(t, halfOffset, s7); break;
      case 41: word = random(2) ? lb(d, byteOffset, s7) : lbu(d, byteOffset, s7); break;
      case 42: word = sb(t, byteOffset, s7); break;
      case 43: word = random(2) ? lwl(d, byteOffset, s7) : lwr(d, byteOffset, s7); break;
      case 44: word = random(2) ? swl(t, byteOffset, s7) : swr(t, byteOffset, s7); break;
      case 45: word = random(8) ? mtc1(t, random(32)) : ctc1(t, 31); break;  // ctc1: another rounding, or flushing
      case 46: word = mfc1(d, random(32)); break;
      case 47: word = fop(random(8), random(32), random(32), random(32)); break;  // add.s ... neg.s
      case 48: {  // cvt.s.w; round, trunc, ceil and floor to words; cvt.w.s
        uint32_t conversion = random(3);
        word = conversion == 0 ? cvtsw(random(32), random(32))
             : conversion == 1 ? fop(0x0c + random(4), random(32), random(32)) : fop(0x24, random(32), random(32));
        break;
      }
      case 49: word = ccond(random(16), random(32), random(32)); break;
      // the VFPU, which the recompiler compiles in part (the rest, and any instruction with a prefix set, it leaves to
      // the interpreter)
      case 50: {
        const uint32_t operations[] = {0xc0, 0xc1, 0xc7, 0xc8, 0xc9, 0xca, 0xda, 0xdb, 0xde, 0xdf};  // vadd ... vslt
        word = vfpuOp(operations[random(10)], size, vd, vs, vt);
        break;
      }
      case 51: word = vfpuOp(0xd8, size, random(16), vs, vt); break;  // vcmp
      case 52: {
        const uint32_t codes[] = {0x00, 0x01, 0x02, 0x04, 0x05, 0x06, 0x07, 0x10, 0x16, 0x44, 0x4a};  // vmov ... vsgn
        word = vfpuOp(0x1a0, size, vd, vs, codes[random(11)]);
        break;
      }
      case 53: word = random(3) == 2 ? 0xde000000u | random(0x1000) : (0xdc000000u | random(2) << 24 | random(0x100000)); break;
      case 54: {  // mtv, mfv, and the control registers (128 and up) with mtvc and mfvc
        uint32_t reg = random(4) ? vd : 128 + random(16);  // (8-15: the random number generator's state)
        word = random(2) ? 0x48e00000u | t << 16 | reg : 0x48600000u | d << 16 | reg;
        break;
      }
      case 55: {  // lv.s, sv.s
        uint32_t offset = wordOffset;
        word = (random(2) ? 0x32u : 0x3au) << 26 | s7 << 21 | (vd & 31) << 16 | (offset & 0xfffc) | (vd >> 5 & 3);
        break;
      }
      case 56: {  // vmmul, vtfm2-4 and vhtfm2-4 (as the size matches or not), vi2f
        const uint32_t operations[] = {0x1e0, 0x1e1, 0x1e2, 0x1e3, 0x1a5};
        uint32_t operation = operations[random(5)];
        word = vfpuOp(operation, size, vd, vs, operation == 0x1a5 ? random(32) : vt);
        break;
      }
      case 57: {  // vnop, vsync, vflush, cache
        const uint32_t waits[] = {0xffff0000u, 0xffff0320u, 0xffff040du};
        word = random(4) ? waits[random(3)] : 0xbc000000u | random(0x400000);
        break;
      }
      case 58: word = 0xdf000000u | random(2) << 23 | vt << 16 | random(0x10000); break;  // viim, vfim
      case 59: {  // vsbn, vhdp, vcrs, vdet, vscmp
        const uint32_t operations[] = {0xc2, 0xcc, 0xcd, 0xce, 0xdd};
        word = vfpuOp(operations[random(5)], size, vd, vs, vt);
        break;
      }
      case 60: {  // vmscl, vcrsp and vqmul; vmmov, vmidt, vmzero, vmone and vrot (and group 7's reserved codes)
        const uint32_t codes[] = {0x00, 0x03, 0x06, 0x07, 0x01, 0x20};
        uint32_t operation = random(3) ? 0x1e7 : 0x1e4 + random(2);
        uint32_t code = codes[random(6)];
        word = vfpuOp(operation, size, vd, vs, operation == 0x1e7 ? code | (code == 0x20 ? random(32) : 0) : vt);
        break;
      }
      case 61: {  // vf2i, vcmov, and more of the unary group: vidt, vrsq, vsin, vcos, vf2h, vh2f, vs2i, vi2s, vcst
        const uint32_t codes[] = {0x03, 0x11, 0x12, 0x13, 0x32, 0x33, 0x3b, 0x3f, 0x60};
        uint32_t kind = random(4);
        if(kind == 0) word = vfpuOp(0x1a4, size, vd, vs, random(128));                         // vf2in ... vf2id
        else if(kind == 1) word = vfpuOp(0x1a5, size, vd, vs, 0x20 | random(2) << 3 | random(8));  // vcmovt, vcmovf
        else word = vfpuOp(0x1a0, size, vd, vs, codes[random(9)] | (random(9) == 8 ? random(32) : 0));
        break;
      }
      }
      code.push_back(word);
    }

    uint32_t registers[32], floats[32], vectors[128], data[128];
    for(auto& r : registers) r = random(4) ? value() : floatValue();
    for(auto& f : floats) f = floatValue();
    for(auto& v : vectors) v = value();
    uint32_t cc = random(64);
    for(auto& d : data) d = next();
    uint32_t hi = value(), lo = value(), csr = random(4) | random(2) << 23 | random(2) << 24;
    auto setup = [&](Allegrex& c) {
      for(uint32_t n = 1; n < 32; n++) c.ipu.r[n] = registers[n];
      c.ipu.r[s6] = Base + Length * 4;
      c.ipu.r[s7] = Data;
      c.ipu.hi = hi;
      c.ipu.lo = lo;
      for(uint32_t n = 0; n < 32; n++) c.fpu.r[n] = floats[n];
      c.fpu.csr = csr;
      for(uint32_t n = 0; n < 128; n++) c.vfpu.r[n] = vectors[n];
      c.vfpu.cc = cc;
    };
    Machine interpreter, recompiler;
    interpreter.recompile = false;
    recompiler.recompile = true;
    for(uint32_t n = 0; n < 128; n++) {
      uint32_t address = n < 64 ? Data + n * 4 : Unmapped + (n - 64) * 4;
      interpreter.ram.write32(address, data[n]);
      recompiler.ram.write32(address, data[n]);
    }
    interpreter.run(code, setup);
    recompiler.run(code, setup);
    if(program == 0) {  //(the data in the recompiler's direct region, so its loads take the fast path there)
      auto& direct = recompiler.cpu.recompiler.direct;
      CHECK((Data & 0x1fffffff) - direct.first < direct.size, true);
    }

    auto& a = interpreter.cpu;
    auto& b = recompiler.cpu;
    char difference[96] = {};
    auto differs = [&](const char* what, uint32_t index, uint32_t x, uint32_t y) {
      if(x != y && !difference[0]) std::snprintf(difference, sizeof(difference), "%s %u: %08x vs %08x", what, index, x, y);
    };
    for(uint32_t n = 0; n < 32; n++) differs("r", n, a.ipu.r[n], b.ipu.r[n]);
    differs("hi", 0, a.ipu.hi, b.ipu.hi);
    differs("lo", 0, a.ipu.lo, b.ipu.lo);
    differs("pc", 0, a.ipu.pc, b.ipu.pc);
    differs("pd", 0, a.ipu.pd, b.ipu.pd);
    for(uint32_t n = 0; n < 32; n++) differs("f", n, a.fpu.r[n], b.fpu.r[n]);
    differs("fcr31", 0, a.fpu.csr, b.fpu.csr);
    for(uint32_t n = 0; n < 128; n++) differs("v", n, a.vfpu.r[n], b.vfpu.r[n]);
    differs("pfxs", 0, a.vfpu.pfxs, b.vfpu.pfxs);
    differs("pfxt", 0, a.vfpu.pfxt, b.vfpu.pfxt);
    differs("pfxd", 0, a.vfpu.pfxd, b.vfpu.pfxd);
    differs("vfpu cc", 0, a.vfpu.cc, b.vfpu.cc);
    differs("badvaddr", 0, a.scc.r[8], b.scc.r[8]);
    differs("halted", 0, a.scc.halted, b.scc.halted);
    differs("exceptions", 0, interpreter.exceptions.size(), recompiler.exceptions.size());
    if(interpreter.exceptions.size() == recompiler.exceptions.size()) {
      for(uint32_t n = 0; n < interpreter.exceptions.size(); n++) {
        differs("exception at", n, interpreter.exceptions[n].second, recompiler.exceptions[n].second);
      }
    }
    for(uint32_t n = 0; n < interpreter.ram.bytes.size(); n++) differs("byte", n, interpreter.ram.bytes[n], recompiler.ram.bytes[n]);
    if(difference[0]) {
      failures++;
      if(++mismatches <= 3) {
        std::printf("FAIL %s: program %u, %s\n", currentTest, program, difference);
        for(uint32_t n = 0; n < code.size(); n++) std::printf("  %08x: %08x\n", Base + n * 4, code[n]);
      }
    }
  }
  CHECK(mismatches, 0);
}

}

}

int main() {
  using namespace allegrex_test;
  if(std::getenv("ALLEGREX_BENCHMARK")) {
    benchmark();
    return 0;
  }
  Tests tests = {
    {"alu", alu}, {"shifts", shifts}, {"allegrex bits", allegrexBits}, {"multiply/divide", multiplyDivide},
    {"loads/stores", loadsStores}, {"unaligned", unaligned}, {"direct loads", directLoads}, {"branches", branches},
    {"syscalls", syscalls},
    {"exceptions", exceptions}, {"fpu arithmetic", fpuArithmetic}, {"fpu conversions", fpuConversions},
    {"fpu compare", fpuCompare}, {"fpu memory", fpuMemory}, {"fpu flushing", fpuFlushing}, {"system", system},
    {"recompiler", recompilerCases},
    {"chained runs", chainedRuns}, {"runs moved by syscalls", movedRuns}, {"runs of 2^62 to 2^64", hugeRuns},
    {"r0 as an operand", zeroRegister},
    {"coprocessor branches taken", coprocessorBranchesTaken},
    {"delay slots each way", delaySlotsEachWay}, {"linked blocks", linkedBlocks},
  };
  for(auto& test : vfpuTests()) tests.push_back(test);
  checkRequired();
  int groups = 0;
  for(bool recompiler : {false, true}) {
    useRecompiler = recompiler;
    for(auto& [name, run] : tests) {
      std::string label = std::string(name) + (recompiler ? " (recompiler)" : " (interpreter)");
      currentTest = label.c_str();
      int before = failures;
      run();
      report(before);
      groups++;
    }
  }
  for(auto& [name, run] : Tests{{"recompiler matches interpreter", matchesInterpreter}, {"measured on a PSP", measured}}) {
    currentTest = name;
    int before = failures;
    run();
    report(before);
    groups++;
  }
  summary(groups);
  return failures ? 1 : 0;
}
