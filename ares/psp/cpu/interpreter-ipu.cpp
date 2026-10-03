//The integer instructions, one function per instruction, named after its mnemonic.
//
//How to read them: rs and rt are the input registers, and rd (or rt, for instructions that carry a constant) is the
//output. imm is the 16-bit constant stored inside the instruction itself. "Signed" means a value's top bit is its
//sign (0x8000'0000 is the most negative number); "unsigned" means all 32 bits are magnitude.

//Branches and jumps don't move pc: the delay slot (the next instruction) is already there and always runs first.
//They move pd, the address the CPU continues at after the delay slot. See IPU in allegrex.hpp.
auto Allegrex::branch(u32 target) -> void {
  ipu.pd = target;
}

//The "likely" branches (beql, bnel, ...) run their delay slot only when they're taken. When they aren't, the slot
//is skipped, as if it weren't there.
auto Allegrex::skipDelaySlot() -> void {
  ipu.pc = ipu.pd;
  ipu.pd += 4;
}

//add and addi raise an exception when the result doesn't fit in a signed 32-bit number (rd is left unchanged), so
//that's checked first: the sum overflowed when both inputs have the same sign and the sum's sign differs.
auto Allegrex::ADD(u32& rd, cu32& rs, cu32& rt) -> void {
  u32 sum = rs + rt;
  if(~(rs ^ rt) & (rs ^ sum) & 1 << 31) return exception(Exception::Overflow);
  rd = sum;
}

auto Allegrex::ADDI(u32& rt, cu32& rs, s16 imm) -> void {
  u32 sum = rs + imm;
  if(~(rs ^ u32(imm)) & (rs ^ sum) & 1 << 31) return exception(Exception::Overflow);
  rt = sum;
}

//The "u" in addiu, addu and subu means the result simply wraps around on overflow, no exception. (The constant in
//addiu is still sign-extended: addiu with 0xffff subtracts one.)
auto Allegrex::ADDIU(u32& rt, cu32& rs, s16 imm) -> void {
  rt = rs + imm;
}

auto Allegrex::ADDU(u32& rd, cu32& rs, cu32& rt) -> void {
  rd = rs + rt;
}

auto Allegrex::AND(u32& rd, cu32& rs, cu32& rt) -> void {
  rd = rs & rt;
}

//The logic instructions zero-extend their constant: andi with 0xffff keeps the low 16 bits.
auto Allegrex::ANDI(u32& rt, cu32& rs, u16 imm) -> void {
  rt = rs & imm;
}

//A branch's offset counts instructions (four bytes each) from its delay slot, which is where pc already points.
auto Allegrex::BEQ(cu32& rs, cu32& rt, s16 imm) -> void {
  if(rs == rt) branch(ipu.pc + imm * 4);
}

auto Allegrex::BEQL(cu32& rs, cu32& rt, s16 imm) -> void {
  if(rs == rt) return branch(ipu.pc + imm * 4);
  skipDelaySlot();
}

auto Allegrex::BGEZ(cs32& rs, s16 imm) -> void {
  if(rs >= 0) branch(ipu.pc + imm * 4);
}

//The "and link" branches store a return address in ra (r31), taken or not: the instruction after the delay slot,
//which is pd. Whether to branch is decided first, because rs may be ra itself and must be tested as it was.
auto Allegrex::BGEZAL(cs32& rs, s16 imm) -> void {
  bool taken = rs >= 0;
  ipu.r[31] = ipu.pd;
  if(taken) branch(ipu.pc + imm * 4);
}

auto Allegrex::BGEZALL(cs32& rs, s16 imm) -> void {
  bool taken = rs >= 0;
  ipu.r[31] = ipu.pd;
  if(taken) return branch(ipu.pc + imm * 4);
  skipDelaySlot();
}

auto Allegrex::BGEZL(cs32& rs, s16 imm) -> void {
  if(rs >= 0) return branch(ipu.pc + imm * 4);
  skipDelaySlot();
}

auto Allegrex::BGTZ(cs32& rs, s16 imm) -> void {
  if(rs > 0) branch(ipu.pc + imm * 4);
}

auto Allegrex::BGTZL(cs32& rs, s16 imm) -> void {
  if(rs > 0) return branch(ipu.pc + imm * 4);
  skipDelaySlot();
}

//Reverses the order of all 32 bits: bit 0 becomes bit 31. It swaps neighbouring bits, then pairs, then nibbles,
//then the four bytes.
auto Allegrex::BITREV(u32& rd, cu32& rt) -> void {
  u32 value = rt;
  value = (value >> 1 & 0x5555'5555) | (value & 0x5555'5555) << 1;
  value = (value >> 2 & 0x3333'3333) | (value & 0x3333'3333) << 2;
  value = (value >> 4 & 0x0f0f'0f0f) | (value & 0x0f0f'0f0f) << 4;
  rd = value >> 24 | (value >> 8 & 0xff00) | (value << 8 & 0xff'0000) | value << 24;
}

auto Allegrex::BLEZ(cs32& rs, s16 imm) -> void {
  if(rs <= 0) branch(ipu.pc + imm * 4);
}

auto Allegrex::BLEZL(cs32& rs, s16 imm) -> void {
  if(rs <= 0) return branch(ipu.pc + imm * 4);
  skipDelaySlot();
}

auto Allegrex::BLTZ(cs32& rs, s16 imm) -> void {
  if(rs < 0) branch(ipu.pc + imm * 4);
}

auto Allegrex::BLTZAL(cs32& rs, s16 imm) -> void {
  bool taken = rs < 0;
  ipu.r[31] = ipu.pd;
  if(taken) branch(ipu.pc + imm * 4);
}

auto Allegrex::BLTZALL(cs32& rs, s16 imm) -> void {
  bool taken = rs < 0;
  ipu.r[31] = ipu.pd;
  if(taken) return branch(ipu.pc + imm * 4);
  skipDelaySlot();
}

auto Allegrex::BLTZL(cs32& rs, s16 imm) -> void {
  if(rs < 0) return branch(ipu.pc + imm * 4);
  skipDelaySlot();
}

auto Allegrex::BNE(cu32& rs, cu32& rt, s16 imm) -> void {
  if(rs != rt) branch(ipu.pc + imm * 4);
}

auto Allegrex::BNEL(cu32& rs, cu32& rt, s16 imm) -> void {
  if(rs != rt) return branch(ipu.pc + imm * 4);
  skipDelaySlot();
}

auto Allegrex::BREAK() -> void {
  exception(Exception::Breakpoint);
}

//The CPU has caches (small fast copies of memory), and games use cache to keep them in step with memory. The
//interpreter reads memory directly, so there's nothing to do. (The recompiler will care: a game that writes new
//code must not keep running the old compiled copy.)
auto Allegrex::CACHE() -> void {
}

//Count leading ones/zeros: how many bits in a row, from the top, are 1 (or 0). A zero register has 32 leading zeros.
auto Allegrex::CLO(u32& rd, cu32& rs) -> void {
  rd = std::countl_one(rs);
}

auto Allegrex::CLZ(u32& rd, cu32& rs) -> void {
  rd = std::countl_zero(rs);
}

//Division puts the quotient in lo and the remainder in hi, rounding toward zero (-7 / 2 is -3, remainder -1).
//Dividing by zero doesn't fault on MIPS; this gives what MIPS chips commonly do (not yet checked on a PSP): lo
//becomes -1 for a positive dividend and 1 for a negative one, hi the dividend. The most negative number divided by
//-1 doesn't fit either, and gives itself, remainder 0.
auto Allegrex::DIV(cs32& rs, cs32& rt) -> void {
  if(rt == 0) {
    ipu.lo = rs < 0 ? 1 : -1;
    ipu.hi = rs;
  } else if(rs == s32(0x8000'0000) && rt == -1) {
    ipu.lo = rs;
    ipu.hi = 0;
  } else {
    ipu.lo = rs / rt;
    ipu.hi = rs % rt;
  }
}

auto Allegrex::DIVU(cu32& rs, cu32& rt) -> void {
  if(rt == 0) {
    ipu.lo = -1;
    ipu.hi = rs;
  } else {
    ipu.lo = rs / rt;
    ipu.hi = rs % rt;
  }
}

//Extract a bit field: rt gets size bits of rs, starting at bit lsb, moved down to bit 0.
auto Allegrex::EXT(u32& rt, cu32& rs, u32 lsb, u32 size) -> void {
  u64 mask = (1ull << size) - 1;
  rt = rs >> lsb & mask;
}

//Insert a bit field: the low bits of rs replace bits lsb to msb of rt; the rest of rt stays. (msb below lsb isn't a
//valid field; rt is left alone.)
auto Allegrex::INS(u32& rt, cu32& rs, u32 lsb, u32 msb) -> void {
  if(msb < lsb) return;
  u32 mask = ((1ull << (msb - lsb + 1)) - 1) << lsb;
  rt = (rt & ~mask) | (rs << lsb & mask);
}

//A jump's 26-bit target is a word address within the 256 MiB region its delay slot is in: the top four bits come
//from the delay slot's address.
auto Allegrex::J(u32 imm) -> void {
  branch((ipu.pc & 0xf000'0000) | imm << 2);
}

//jal is how functions get called: jump, and leave the return address (after the delay slot) in ra.
auto Allegrex::JAL(u32 imm) -> void {
  ipu.r[31] = ipu.pd;
  branch((ipu.pc & 0xf000'0000) | imm << 2);
}

//The target is read before rd is written, in case they're the same register.
auto Allegrex::JALR(u32& rd, cu32& rs) -> void {
  u32 target = rs;
  rd = ipu.pd;
  branch(target);
}

auto Allegrex::JR(cu32& rs) -> void {
  branch(rs);
}

//Loads: a byte, halfword (16 bits) or word (32 bits) from memory at rs + imm. A halfword or word address that
//isn't a multiple of its size is an error on MIPS (lwl and lwr below are how unaligned words are read).
auto Allegrex::LB(u32& rt, cu32& rs, s16 imm) -> void {
  rt = s8(read(Byte, rs + imm));
}

auto Allegrex::LBU(u32& rt, cu32& rs, s16 imm) -> void {
  rt = u8(read(Byte, rs + imm));
}

auto Allegrex::LH(u32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 1) return addressError(Exception::AddressLoad, address);
  rt = s16(read(Half, address));
}

auto Allegrex::LHU(u32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 1) return addressError(Exception::AddressLoad, address);
  rt = u16(read(Half, address));
}

//Load linked and store conditional build atomic operations: sc only stores if nothing touched the word since ll.
//With one core and no interrupts between them in HLE, nothing can, so ll is a plain load and sc always succeeds.
auto Allegrex::LL(u32& rt, cu32& rs, s16 imm) -> void {
  LW(rt, rs, imm);
}

//Load upper immediate: the constant goes in the top 16 bits (with ori, two instructions make any 32-bit value).
auto Allegrex::LUI(u32& rt, u16 imm) -> void {
  rt = imm << 16;
}

auto Allegrex::LW(u32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressLoad, address);
  rt = read(Word, address);
}

//lwl and lwr read a word that isn't aligned to four bytes, in two halves: lwr at the word's first byte, lwl at its
//last. Each reads the aligned word that contains its address and replaces only some of rt's bytes.
//
//The PSP is little-endian (a word's lowest byte comes first in memory). lwl ("left") fills rt's high bytes with the
//bytes at and below its address; lwr ("right") fills rt's low bytes with the bytes at and above it. For an
//unaligned word at A, "lwr rt, 0(A); lwl rt, 3(A)" ends with all four of its bytes in rt.
auto Allegrex::LWL(u32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  u32 shift = (3 - (address & 3)) * 8;
  u32 word = read(Word, address & ~3);
  u32 keep = shift ? ~0u >> (32 - shift) : 0;
  rt = (rt & keep) | word << shift;
}

auto Allegrex::LWR(u32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  u32 shift = (address & 3) * 8;
  u32 word = read(Word, address & ~3);
  u32 keep = shift ? ~(~0u >> shift) : 0;
  rt = (rt & keep) | word >> shift;
}

//Multiply-accumulate: hi and lo together hold one 64-bit number, and these add (madd) or subtract (msub) the 64-bit
//product of rs and rt to it. Handy for long sums of products, like a dot product. The sum wraps around like the
//hardware's, which is why it's kept unsigned even for the signed forms (a signed overflow is undefined in C++).
auto Allegrex::MADD(cs32& rs, cs32& rt) -> void {
  u64 accumulator = (u64)ipu.hi << 32 | ipu.lo;
  accumulator += (u64)((s64)rs * rt);
  ipu.lo = accumulator;
  ipu.hi = accumulator >> 32;
}

auto Allegrex::MADDU(cu32& rs, cu32& rt) -> void {
  u64 accumulator = (u64)ipu.hi << 32 | ipu.lo;
  accumulator += (u64)rs * rt;
  ipu.lo = accumulator;
  ipu.hi = accumulator >> 32;
}

auto Allegrex::MAX(u32& rd, cs32& rs, cs32& rt) -> void {
  rd = rs > rt ? rs : rt;
}

auto Allegrex::MFHI(u32& rd) -> void {
  rd = ipu.hi;
}

auto Allegrex::MFLO(u32& rd) -> void {
  rd = ipu.lo;
}

auto Allegrex::MIN(u32& rd, cs32& rs, cs32& rt) -> void {
  rd = rs < rt ? rs : rt;
}

//Conditional moves: copy rs to rd only if rt is non-zero (movn) or zero (movz).
auto Allegrex::MOVN(u32& rd, cu32& rs, cu32& rt) -> void {
  if(rt != 0) rd = rs;
}

auto Allegrex::MOVZ(u32& rd, cu32& rs, cu32& rt) -> void {
  if(rt == 0) rd = rs;
}

auto Allegrex::MSUB(cs32& rs, cs32& rt) -> void {
  u64 accumulator = (u64)ipu.hi << 32 | ipu.lo;
  accumulator -= (u64)((s64)rs * rt);
  ipu.lo = accumulator;
  ipu.hi = accumulator >> 32;
}

auto Allegrex::MSUBU(cu32& rs, cu32& rt) -> void {
  u64 accumulator = (u64)ipu.hi << 32 | ipu.lo;
  accumulator -= (u64)rs * rt;
  ipu.lo = accumulator;
  ipu.hi = accumulator >> 32;
}

auto Allegrex::MTHI(cu32& rs) -> void {
  ipu.hi = rs;
}

auto Allegrex::MTLO(cu32& rs) -> void {
  ipu.lo = rs;
}

//A 32 x 32-bit multiply makes a 64-bit product: the low half goes in lo, the high half in hi.
auto Allegrex::MULT(cs32& rs, cs32& rt) -> void {
  s64 product = (s64)rs * rt;
  ipu.lo = product;
  ipu.hi = product >> 32;
}

auto Allegrex::MULTU(cu32& rs, cu32& rt) -> void {
  u64 product = (u64)rs * rt;
  ipu.lo = product;
  ipu.hi = product >> 32;
}

auto Allegrex::NOR(u32& rd, cu32& rs, cu32& rt) -> void {
  rd = ~(rs | rt);
}

auto Allegrex::OR(u32& rd, cu32& rs, cu32& rt) -> void {
  rd = rs | rt;
}

auto Allegrex::ORI(u32& rt, cu32& rs, u16 imm) -> void {
  rt = rs | imm;
}

//Rotate right: bits shifted out of the bottom come back in at the top.
auto Allegrex::ROTR(u32& rd, cu32& rt, u8 sa) -> void {
  rd = std::rotr(rt, sa);
}

auto Allegrex::ROTRV(u32& rd, cu32& rt, cu32& rs) -> void {
  rd = std::rotr(rt, int(rs & 31));
}

auto Allegrex::SB(cu32& rt, cu32& rs, s16 imm) -> void {
  write(Byte, rs + imm, u8(rt));
}

auto Allegrex::SC(u32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressStore, address);
  write(Word, address, rt);
  rt = 1;
}

//Sign-extend a byte or halfword: copy its top bit into all the bits above it, so -1 as a byte (0xff) stays -1.
auto Allegrex::SEB(u32& rd, cu32& rt) -> void {
  rd = s8(rt);
}

auto Allegrex::SEH(u32& rd, cu32& rt) -> void {
  rd = s16(rt);
}

auto Allegrex::SH(cu32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 1) return addressError(Exception::AddressStore, address);
  write(Half, address, u16(rt));
}

//Shifts by the amount in the instruction (sll, srl, sra) or in a register (only its low five bits count: shifting
//by 36 shifts by 4). srl fills the top with zeros; sra copies the sign bit down, so negative numbers stay negative.
auto Allegrex::SLL(u32& rd, cu32& rt, u8 sa) -> void {
  rd = rt << sa;
}

auto Allegrex::SLLV(u32& rd, cu32& rt, cu32& rs) -> void {
  rd = rt << (rs & 31);
}

//Set on less than: rd becomes 1 if rs < rt, else 0. slt compares signed, sltu unsigned.
auto Allegrex::SLT(u32& rd, cs32& rs, cs32& rt) -> void {
  rd = rs < rt;
}

auto Allegrex::SLTI(u32& rt, cs32& rs, s16 imm) -> void {
  rt = rs < imm;
}

//sltiu sign-extends its constant and then compares unsigned: sltiu with 0xffff compares against 0xffff'ffff.
auto Allegrex::SLTIU(u32& rt, cu32& rs, s16 imm) -> void {
  rt = rs < u32(s32(imm));
}

auto Allegrex::SLTU(u32& rd, cu32& rs, cu32& rt) -> void {
  rd = rs < rt;
}

auto Allegrex::SRA(u32& rd, cs32& rt, u8 sa) -> void {
  rd = rt >> sa;
}

auto Allegrex::SRAV(u32& rd, cs32& rt, cu32& rs) -> void {
  rd = rt >> (rs & 31);
}

auto Allegrex::SRL(u32& rd, cu32& rt, u8 sa) -> void {
  rd = rt >> sa;
}

auto Allegrex::SRLV(u32& rd, cu32& rt, cu32& rs) -> void {
  rd = rt >> (rs & 31);
}

auto Allegrex::SUB(u32& rd, cu32& rs, cu32& rt) -> void {
  u32 difference = rs - rt;
  if((rs ^ rt) & (rs ^ difference) & 1 << 31) return exception(Exception::Overflow);
  rd = difference;
}

auto Allegrex::SUBU(u32& rd, cu32& rs, cu32& rt) -> void {
  rd = rs - rt;
}

auto Allegrex::SW(cu32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressStore, address);
  write(Word, address, rt);
}

//swl and swr write an unaligned word in two halves, mirroring lwl and lwr: swl stores rt's high bytes at and below
//its address, swr its low bytes at and above it.
auto Allegrex::SWL(cu32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  u32 shift = (3 - (address & 3)) * 8;
  u32 word = read(Word, address & ~3);
  u32 keep = shift ? ~(~0u >> shift) : 0;
  write(Word, address & ~3, (word & keep) | rt >> shift);
}

auto Allegrex::SWR(cu32& rt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  u32 shift = (address & 3) * 8;
  u32 word = read(Word, address & ~3);
  u32 keep = shift ? ~0u >> (32 - shift) : 0;
  write(Word, address & ~3, (word & keep) | rt << shift);
}

//Orders memory accesses on multi-core or cached systems; nothing to wait for here.
auto Allegrex::SYNC() -> void {
}

//A call into the operating system. The 20-bit code in the instruction says which function; the HLE kernel answers
//it through syscallHook. Games reach system functions through stubs ("jr ra" with the syscall in its delay slot),
//so pc already holds the return address when the hook runs.
auto Allegrex::SYSCALL() -> void {
  u32 code = pipeline.instruction >> 6 & 0xfffff;
  if(syscallHook && syscallHook(code)) return;
  exception(Exception::Syscall);
}

//Swap the bytes within each halfword (wsbh), or all four bytes of the word (wsbw): handy for reading big-endian
//data on this little-endian CPU.
auto Allegrex::WSBH(u32& rd, cu32& rt) -> void {
  rd = (rt & 0x00ff'00ff) << 8 | (rt >> 8 & 0x00ff'00ff);
}

auto Allegrex::WSBW(u32& rd, cu32& rt) -> void {
  rd = rt >> 24 | (rt >> 8 & 0xff00) | (rt << 8 & 0xff'0000) | rt << 24;
}

auto Allegrex::XOR(u32& rd, cu32& rs, cu32& rt) -> void {
  rd = rs ^ rt;
}

auto Allegrex::XORI(u32& rt, cu32& rs, u16 imm) -> void {
  rt = rs ^ imm;
}
