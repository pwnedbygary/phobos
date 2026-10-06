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
  zeroDivisor.run({div_(t0, zero), mflo(s0), mfhi(s1), div_(t1, zero), mflo(s2), divu(t0, zero), mflo(s3), mfhi(s4)},
                  [](Allegrex& s) { s.ipu.r[t0] = 5; s.ipu.r[t1] = (uint32_t)-5; });
  CHECK(zeroDivisor.gpr(s0), 0xffffffff);
  CHECK(zeroDivisor.gpr(s1), 5);
  CHECK(zeroDivisor.gpr(s2), 1);
  CHECK(zeroDivisor.gpr(s3), 0xffffffff);
  CHECK(zeroDivisor.gpr(s4), 5);

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
    {3e9f, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff}, {-3e9f, 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff},
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

auto system() -> void {
  // mtic sets the interrupt state and mfic reads it back; halt stops the CPU where it is.
  Machine m;
  m.run({0x70020026, 0x70080024}, [](Allegrex& s) { s.ipu.r[2] = 1; });  // mtic $2,$0; mfic $8,$0
  CHECK(m.cpu.scc.interrupts, 1);
  CHECK(m.gpr(8), 1);
  CHECK(m.cpu.ipu.pc, Base + 12);  // past the halt

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

  // VFPU encodings: an operation with three 7-bit registers and a size of 1 to 4 lanes, and a VFPU branch
  auto vfpuOp = [](uint32_t opcode, uint32_t size, uint32_t rd, uint32_t rs, uint32_t rt) -> uint32_t {
    return opcode << 23 | rt << 16 | (size >= 3) << 15 | rs << 8 | (size == 2 || size == 4) << 7 | rd;
  };
  auto bv = [](uint32_t kind, uint32_t bit, int32_t offset) -> uint32_t {
    return 0x49000000u | bit << 18 | kind << 16 | ((uint32_t)offset & 0xffff);
  };

  constexpr uint32_t Length = 40, Programs = 500;
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

      // memory: 256 bytes of data at s7, or at s7 + 0x1000, the page left out of the page table (so compiled code
      // reaches it through read() and write()); aligned, and now and then not
      uint32_t region = random(4) ? 0 : 0x1000;
      uint32_t wordOffset = region + random(64) * 4 + (random(30) ? 0 : 1 + random(3));
      uint32_t halfOffset = region + random(128) * 2 + (random(30) ? 0 : 1);
      uint32_t byteOffset = region + random(256);
      uint32_t word = 0;
      uint32_t vd = random(128), vs = random(128), vt = random(128), size = 1 + random(4);
      switch(random(56)) {
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
      // ones the recompiler leaves to the interpreter (add, addi and sub may overflow, which ends the program)
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
      case 45: word = mtc1(t, random(32)); break;
      case 46: word = mfc1(d, random(32)); break;
      case 47: word = fop(random(3), random(32), random(32), random(32)); break;  // add.s, sub.s, mul.s
      case 48: word = cvtsw(random(32), random(32)); break;
      case 49: word = ccond(random(16), random(32), random(32)); break;
      // the VFPU, which the recompiler also leaves to the interpreter
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
        uint32_t reg = random(4) ? vd : 128 + random(4);
        word = random(2) ? 0x48e00000u | t << 16 | reg : 0x48600000u | d << 16 | reg;
        break;
      }
      case 55: {  // lv.s, sv.s
        uint32_t offset = wordOffset;
        word = (random(2) ? 0x32u : 0x3au) << 26 | s7 << 21 | (vd & 31) << 16 | (offset & 0xfffc) | (vd >> 5 & 3);
        break;
      }
      }
      code.push_back(word);
    }

    uint32_t registers[32], floats[32], vectors[128], data[128];
    for(auto& r : registers) r = value();
    for(auto& f : floats) f = value();
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
    {"loads/stores", loadsStores}, {"unaligned", unaligned}, {"branches", branches}, {"syscalls", syscalls},
    {"exceptions", exceptions}, {"fpu arithmetic", fpuArithmetic}, {"fpu conversions", fpuConversions},
    {"fpu compare", fpuCompare}, {"fpu memory", fpuMemory}, {"system", system}, {"recompiler", recompilerCases},
  };
  for(auto& test : vfpuTests()) tests.push_back(test);
  int groups = 0;
  for(bool recompiler : {false, true}) {
    useRecompiler = recompiler;
    for(auto& [name, run] : tests) {
      std::string label = std::string(name) + (recompiler ? " (recompiler)" : " (interpreter)");
      currentTest = label.c_str();
      int before = failures;
      run();
      std::printf("%s %s\n", failures == before ? "pass" : "FAIL", currentTest);
      groups++;
    }
  }
  currentTest = "recompiler matches interpreter";
  int before = failures;
  matchesInterpreter();
  std::printf("%s %s\n", failures == before ? "pass" : "FAIL", currentTest);
  groups++;
  std::printf("%d groups, %d failure%s\n", groups, failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
