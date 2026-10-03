//The decoder: turns an instruction word into the operation it names.
//
//An instruction's top six bits are its opcode. Most opcodes are a single instruction, but a few hold a whole group:
//SPECIAL (0x00) picks its instruction from the low six bits, REGIMM (0x01) from the rt field, the coprocessors
//(0x10, 0x11) from the rs field, and so on. The other fields name registers and constants:
//
//  31    26 25  21 20  16 15  11 10   6 5    0
//  [opcode] [ rs ] [ rt ] [ rd ] [ sa ] [funct]   register form: rd = rs (op) rt, sa is a shift amount
//  [opcode] [ rs ] [ rt ] [  16-bit immediate ]   immediate form: rt = rs (op) imm, or a branch offset
//  [opcode] [       26-bit jump target        ]   jump form
//
//The macros keep each table to one line per instruction, the way ares's other MIPS cores (ps1, n64) do it.

#define OPCODE pipeline.instruction
#define RD ipu.r[RDn]
#define RT ipu.r[RTn]
#define RS ipu.r[RSn]

#define jp(id, name, ...) case id: return decoder##name(__VA_ARGS__)
#define op(id, name, ...) case id: return name(__VA_ARGS__)

#define SA     u8(OPCODE >>  6 & 31)
#define RDn    u8(OPCODE >> 11 & 31)
#define RTn    u8(OPCODE >> 16 & 31)
#define RSn    u8(OPCODE >> 21 & 31)
#define FD     u8(OPCODE >>  6 & 31)
#define FS     u8(OPCODE >> 11 & 31)
#define FT     u8(OPCODE >> 16 & 31)
#define IMMi16 s16(OPCODE)
#define IMMu16 u16(OPCODE)
#define IMMu26 u32(OPCODE & 0x03ff'ffff)

auto Allegrex::decoderEXECUTE() -> void {
  switch(OPCODE >> 26) {
  jp(0x00, SPECIAL);
  jp(0x01, REGIMM);
  op(0x02, J, IMMu26);
  op(0x03, JAL, IMMu26);
  op(0x04, BEQ, RS, RT, IMMi16);
  op(0x05, BNE, RS, RT, IMMi16);
  op(0x06, BLEZ, RS, IMMi16);
  op(0x07, BGTZ, RS, IMMi16);
  op(0x08, ADDI, RT, RS, IMMi16);
  op(0x09, ADDIU, RT, RS, IMMi16);
  op(0x0a, SLTI, RT, RS, IMMi16);
  op(0x0b, SLTIU, RT, RS, IMMi16);
  op(0x0c, ANDI, RT, RS, IMMu16);
  op(0x0d, ORI, RT, RS, IMMu16);
  op(0x0e, XORI, RT, RS, IMMu16);
  op(0x0f, LUI, RT, IMMu16);
  jp(0x10, SCC);
  jp(0x11, FPU);
  op(0x14, BEQL, RS, RT, IMMi16);
  op(0x15, BNEL, RS, RT, IMMi16);
  op(0x16, BLEZL, RS, IMMi16);
  op(0x17, BGTZL, RS, IMMi16);
  jp(0x1c, SPECIAL2);
  jp(0x1f, SPECIAL3);
  op(0x20, LB, RT, RS, IMMi16);
  op(0x21, LH, RT, RS, IMMi16);
  op(0x22, LWL, RT, RS, IMMi16);
  op(0x23, LW, RT, RS, IMMi16);
  op(0x24, LBU, RT, RS, IMMi16);
  op(0x25, LHU, RT, RS, IMMi16);
  op(0x26, LWR, RT, RS, IMMi16);
  op(0x28, SB, RT, RS, IMMi16);
  op(0x29, SH, RT, RS, IMMi16);
  op(0x2a, SWL, RT, RS, IMMi16);
  op(0x2b, SW, RT, RS, IMMi16);
  op(0x2e, SWR, RT, RS, IMMi16);
  op(0x2f, CACHE);
  op(0x30, LL, RT, RS, IMMi16);
  op(0x31, LWC1, FT, RS, IMMi16);
  op(0x38, SC, RT, RS, IMMi16);
  op(0x39, SWC1, FT, RS, IMMi16);
  }
  //Everything else isn't an instruction yet: the VFPU's opcodes (0x12, 0x18-0x1b, 0x32-0x37, 0x3a-0x3f) come with
  //the VFPU.
  INVALID();
}

auto Allegrex::decoderSPECIAL() -> void {
  switch(OPCODE & 0x3f) {
  op(0x00, SLL, RD, RT, SA);
  case 0x02:  //srl, or rotr when the rs field is 1
    if(RSn == 1) return ROTR(RD, RT, SA);
    return SRL(RD, RT, SA);
  op(0x03, SRA, RD, RT, SA);
  op(0x04, SLLV, RD, RT, RS);
  case 0x06:  //srlv, or rotrv when the sa field is 1
    if(SA == 1) return ROTRV(RD, RT, RS);
    return SRLV(RD, RT, RS);
  op(0x07, SRAV, RD, RT, RS);
  op(0x08, JR, RS);
  op(0x09, JALR, RD, RS);
  op(0x0a, MOVZ, RD, RS, RT);
  op(0x0b, MOVN, RD, RS, RT);
  op(0x0c, SYSCALL);
  op(0x0d, BREAK);
  op(0x0f, SYNC);
  op(0x10, MFHI, RD);
  op(0x11, MTHI, RS);
  op(0x12, MFLO, RD);
  op(0x13, MTLO, RS);
  op(0x16, CLZ, RD, RS);  //standard MIPS puts clz and clo in SPECIAL2; the Allegrex has them here
  op(0x17, CLO, RD, RS);
  op(0x18, MULT, RS, RT);
  op(0x19, MULTU, RS, RT);
  op(0x1a, DIV, RS, RT);
  op(0x1b, DIVU, RS, RT);
  op(0x1c, MADD, RS, RT);  //likewise madd, maddu, msub and msubu
  op(0x1d, MADDU, RS, RT);
  op(0x20, ADD, RD, RS, RT);
  op(0x21, ADDU, RD, RS, RT);
  op(0x22, SUB, RD, RS, RT);
  op(0x23, SUBU, RD, RS, RT);
  op(0x24, AND, RD, RS, RT);
  op(0x25, OR, RD, RS, RT);
  op(0x26, XOR, RD, RS, RT);
  op(0x27, NOR, RD, RS, RT);
  op(0x2a, SLT, RD, RS, RT);
  op(0x2b, SLTU, RD, RS, RT);
  op(0x2c, MAX, RD, RS, RT);
  op(0x2d, MIN, RD, RS, RT);
  op(0x2e, MSUB, RS, RT);
  op(0x2f, MSUBU, RS, RT);
  }
  INVALID();
}

auto Allegrex::decoderREGIMM() -> void {
  switch(RTn) {
  op(0x00, BLTZ, RS, IMMi16);
  op(0x01, BGEZ, RS, IMMi16);
  op(0x02, BLTZL, RS, IMMi16);
  op(0x03, BGEZL, RS, IMMi16);
  op(0x10, BLTZAL, RS, IMMi16);
  op(0x11, BGEZAL, RS, IMMi16);
  op(0x12, BLTZALL, RS, IMMi16);
  op(0x13, BGEZALL, RS, IMMi16);
  }
  INVALID();
}

//The Allegrex's only SPECIAL2 instructions are its own: halt, mfic and mtic.
auto Allegrex::decoderSPECIAL2() -> void {
  if(OPCODE == 0x7000'0000) return HALT();
  switch(OPCODE & 0xffe0'07ff) {
  case 0x7000'0024: return MFIC(RT);
  case 0x7000'0026: return MTIC(RT);
  }
  INVALID();
}

//SPECIAL3 holds MIPS32r2's bit-field instructions, and bshfl (0x20), whose sa field picks a byte or bit shuffle.
auto Allegrex::decoderSPECIAL3() -> void {
  switch(OPCODE & 0x3f) {
  op(0x00, EXT, RT, RS, SA, RDn + 1u);  //rd holds the field's size minus one
  op(0x04, INS, RT, RS, SA, RDn);       //rd holds the field's top bit
  case 0x20:
    if(RSn != 0) break;
    switch(SA) {
    op(0x02, WSBH, RD, RT);
    op(0x03, WSBW, RD, RT);
    op(0x10, SEB, RD, RT);
    op(0x14, BITREV, RD, RT);
    op(0x18, SEH, RD, RT);
    }
    break;
  }
  INVALID();
}

auto Allegrex::decoderSCC() -> void {
  switch(RSn) {
  op(0x00, MFC0, RT, RDn);
  op(0x04, MTC0, RT, RDn);
  case 0x10:
    if((OPCODE & 0x3f) == 0x18) return ERET();
    break;
  }
  INVALID();
}

auto Allegrex::decoderFPU() -> void {
  switch(RSn) {
  op(0x00, MFC1, RT, FS);
  op(0x02, CFC1, RT, RDn);
  op(0x04, MTC1, RT, FS);
  op(0x06, CTC1, RT, RDn);
  op(0x08, BC1, OPCODE.bit(16), OPCODE.bit(17), IMMi16);  //bit 16: branch when true; bit 17: likely
  case 0x10:  //single-precision operations, picked by the low six bits
    if((OPCODE & 0x3f) >= 0x30) return FCOMPARE(FS, FT, OPCODE & 15);
    switch(OPCODE & 0x3f) {
    op(0x00, FADD, FD, FS, FT);
    op(0x01, FSUB, FD, FS, FT);
    op(0x02, FMUL, FD, FS, FT);
    op(0x03, FDIV, FD, FS, FT);
    op(0x04, FSQRT, FD, FS);
    op(0x05, FABS, FD, FS);
    op(0x06, FMOV, FD, FS);
    op(0x07, FNEG, FD, FS);
    op(0x0c, FROUND, FD, FS);
    op(0x0d, FTRUNC, FD, FS);
    op(0x0e, FCEIL, FD, FS);
    op(0x0f, FFLOOR, FD, FS);
    op(0x24, FCVTWS, FD, FS);
    }
    break;
  case 0x14:  //word (integer) format: only the conversion to single precision
    if((OPCODE & 0x3f) == 0x20) return FCVTSW(FD, FS);
    break;
  }
  INVALID();
}

auto Allegrex::INVALID() -> void {
  exception(Exception::ReservedInstruction);
}

#undef OPCODE
#undef RD
#undef RT
#undef RS
#undef jp
#undef op
#undef SA
#undef RDn
#undef RTn
#undef RSn
#undef FD
#undef FS
#undef FT
#undef IMMi16
#undef IMMu16
#undef IMMu26
