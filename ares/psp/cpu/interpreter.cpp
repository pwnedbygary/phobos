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

//The VFPU's fields: three 7-bit register numbers, and the operand size (1 to 4 lanes) from bits 7 and 15. Its
//loads and stores keep the register number's top bits in the low bits of the offset, which is a multiple of four.
#define VD   u8(OPCODE & 0x7f)
#define VS   u8(OPCODE >>  8 & 0x7f)
#define VT   u8(OPCODE >> 16 & 0x7f)
#define VN   u32(1 + (OPCODE >> 7 & 1) + 2 * (OPCODE >> 15 & 1))
#define VTS  u8((OPCODE >> 16 & 31) | (OPCODE & 3) << 5)
#define VTQ  u8((OPCODE >> 16 & 31) | (OPCODE & 1) << 5)
#define IMMv s16(OPCODE & 0xfffc)

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
  jp(0x12, COP2);
  op(0x14, BEQL, RS, RT, IMMi16);
  op(0x15, BNEL, RS, RT, IMMi16);
  op(0x16, BLEZL, RS, IMMi16);
  op(0x17, BGTZL, RS, IMMi16);
  //VFPU instructions use up the prefixes once they've run (see VFPU in allegrex.hpp)
  case 0x18: decoderVFPU0(); return vfpuPrefixesUsed();
  case 0x19: decoderVFPU1(); return vfpuPrefixesUsed();
  case 0x1b: decoderVFPU3(); return vfpuPrefixesUsed();
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
  op(0x32, LVS, VTS, RS, IMMv);
  case 0x34:
    //vmfvc and vmtvc move control registers, the prefixes among them, and leave the prefixes alone (as PPSSPP
    //has them; pspdev's documentation doesn't say)
    if(OPCODE >> 16 == 0xd050) return VMFVC(VD, u8(OPCODE >> 8 & 0x7f));
    if(OPCODE >> 16 == 0xd051) return VMTVC(u8(OPCODE & 0x7f), VS);
    decoderVFPU4();
    return vfpuPrefixesUsed();
  case 0x35:  //lvl.q, or lvr.q when bit 1 is set
    if(OPCODE & 2) return LVRQ(VTQ, RS, IMMv);
    return LVLQ(VTQ, RS, IMMv);
  op(0x36, LVQ, VTQ, RS, IMMv);
  jp(0x37, VFPU5);  //the prefix instructions, which set the prefixes rather than use them up
  op(0x38, SC, RT, RS, IMMi16);
  op(0x39, SWC1, FT, RS, IMMi16);
  op(0x3a, SVS, VTS, RS, IMMv);
  case 0x3c: decoderVFPU6(); return vfpuPrefixesUsed();
  case 0x3d:  //svl.q, or svr.q when bit 1 is set
    if(OPCODE & 2) return SVRQ(VTQ, RS, IMMv);
    return SVLQ(VTQ, RS, IMMv);
  op(0x3e, SVQ, VTQ, RS, IMMv);
  jp(0x3f, VFPU7);  //vnop, vsync and vflush, which see to the prefixes themselves
  }
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

//Coprocessor 2 transfers: moves between the integer and VFPU registers, and the VFPU's branches.
auto Allegrex::decoderCOP2() -> void {
  switch(RSn) {
  case 0x03:  //mfv, or mfvc for the control registers (numbers from 128)
    if(OPCODE & 0x80) return MFVC(RT, OPCODE & 0x7f);
    return MFV(RT, OPCODE & 0x7f);
  case 0x07:  //mtv, or mtvc
    if(OPCODE & 0x80) return MTVC(RT, OPCODE & 0x7f);
    return MTV(RT, OPCODE & 0x7f);
  op(0x08, BV, OPCODE.bit(16), OPCODE.bit(17), u8(OPCODE >> 18 & 7), IMMi16);  //bit 16: on true; bit 17: likely
  }
  INVALID();
}

//The VFPU's groups pick their instruction from bits 23-25, and within some groups from other fields.
auto Allegrex::decoderVFPU0() -> void {
  switch(OPCODE >> 23 & 7) {
  op(0, VADD, VD, VS, VT, VN);
  op(1, VSUB, VD, VS, VT, VN);
  op(2, VSBN, VD, VS, VT, VN);
  op(7, VDIV, VD, VS, VT, VN);
  }
  INVALID();
}

auto Allegrex::decoderVFPU1() -> void {
  switch(OPCODE >> 23 & 7) {
  op(0, VMUL, VD, VS, VT, VN);
  op(1, VDOT, VD, VS, VT, VN);
  op(2, VSCL, VD, VS, VT, VN);
  op(4, VHDP, VD, VS, VT, VN);
  op(5, VCRS, VD, VS, VT, VN);
  op(6, VDET, VD, VS, VT, VN);
  }
  INVALID();
}

auto Allegrex::decoderVFPU3() -> void {
  switch(OPCODE >> 23 & 7) {
  op(0, VCMP, u8(OPCODE & 15), VS, VT, VN);  //the condition sits where rd would
  op(2, VMIN, VD, VS, VT, VN);
  op(3, VMAX, VD, VS, VT, VN);
  op(5, VSCMP, VD, VS, VT, VN);
  op(6, VSGE, VD, VS, VT, VN);
  op(7, VSLT, VD, VS, VT, VN);
  }
  INVALID();
}

//Mostly instructions with one operand, which the rt field picks; also the conversions to and from integers.
auto Allegrex::decoderVFPU4() -> void {
  if(OPCODE >> 24 == 0xd3) return VWBN(VD, VS, VN, u8(OPCODE >> 16));
  switch(OPCODE >> 23 & 7) {
  case 0: break;
  case 4: return VF2I(VD, VS, VN, u8(OPCODE >> 16 & 31), OPCODE >> 21 & 3);  //vf2in, vf2iz, vf2iu, vf2id
  case 5:
    if((OPCODE >> 21 & 3) == 0) return VI2F(VD, VS, VN, u8(OPCODE >> 16 & 31));
    if((OPCODE >> 20 & 7) == 2) return VCMOV(VD, VS, VN, OPCODE.bit(19), u8(OPCODE >> 16 & 7));  //vcmovt, vcmovf
    return INVALID();
  default: return INVALID();
  }
  if(VT >= 0x60) return VCST(VD, VN, VT & 31);
  switch(VT) {
  op(0x00, VMOV, VD, VS, VN);
  op(0x01, VABS, VD, VS, VN);
  op(0x02, VNEG, VD, VS, VN);
  op(0x03, VIDT, VD, VN);
  op(0x04, VSAT0, VD, VS, VN);
  op(0x05, VSAT1, VD, VS, VN);
  op(0x06, VZERO, VD, VN);
  op(0x07, VONE, VD, VN);
  op(0x10, VRCP, VD, VS, VN);
  op(0x11, VRSQ, VD, VS, VN);
  op(0x12, VSIN, VD, VS, VN);
  op(0x13, VCOS, VD, VS, VN);
  op(0x14, VEXP2, VD, VS, VN);
  op(0x15, VLOG2, VD, VS, VN);
  op(0x16, VSQRT, VD, VS, VN);
  op(0x17, VASIN, VD, VS, VN);
  op(0x18, VNRCP, VD, VS, VN);
  op(0x1a, VNSIN, VD, VS, VN);
  op(0x1c, VREXP2, VD, VS, VN);
  op(0x20, VRNDS, VS, VN);
  op(0x21, VRNDI, VD, VN);
  op(0x22, VRNDF, VD, VN, 0x3f80'0000u);  //vrndf1: from 1 up to 2
  op(0x23, VRNDF, VD, VN, 0x4000'0000u);  //vrndf2: from 2 up to 4
  op(0x32, VF2H, VD, VS, VN);
  op(0x33, VH2F, VD, VS, VN);
  op(0x36, VSBZ, VD, VS, VN);
  op(0x37, VLGB, VD, VS, VN);
  op(0x38, VUC2I, VD, VS, VN);
  op(0x39, VC2I, VD, VS, VN);
  op(0x3a, VUS2I, VD, VS, VN);
  op(0x3b, VS2I, VD, VS, VN);
  op(0x3c, VI2UC, VD, VS, VN);
  op(0x3d, VI2C, VD, VS, VN);
  op(0x3e, VI2US, VD, VS, VN);
  op(0x3f, VI2S, VD, VS, VN);
  op(0x40, VSRT, VD, VS, VN, 1);
  op(0x41, VSRT, VD, VS, VN, 2);
  op(0x42, VBFY1, VD, VS, VN);
  op(0x43, VBFY2, VD, VS, VN);
  op(0x44, VOCP, VD, VS, VN);
  op(0x45, VSOCP, VD, VS, VN);
  op(0x46, VFAD, VD, VS, VN);
  op(0x47, VAVG, VD, VS, VN);
  op(0x48, VSRT, VD, VS, VN, 3);
  op(0x49, VSRT, VD, VS, VN, 4);
  op(0x4a, VSGN, VD, VS, VN);
  op(0x59, VT4444, VD, VS, VN);
  op(0x5a, VT5551, VD, VS, VN);
  op(0x5b, VT5650, VD, VS, VN);
  }
  INVALID();
}

//vpfxs, vpfxt, vpfxd, and viim and vfim, which keep their destination in the rt field.
auto Allegrex::decoderVFPU5() -> void {
  switch(OPCODE >> 24 & 3) {
  op(0, VPFXS, OPCODE & 0xf'ffff);
  op(1, VPFXT, OPCODE & 0xf'ffff);
  op(2, VPFXD, OPCODE & 0xfff);
  case 3:
    if(OPCODE.bit(23)) return VFIM(VT, IMMu16);
    return VIIM(VT, IMMi16);
  }
}

//The matrix instructions, and a few others with two operands.
auto Allegrex::decoderVFPU6() -> void {
  switch(OPCODE >> 23 & 7) {
  op(0, VMMUL, VD, VS, VT, VN);
  case 1: case 2: case 3: {
    //vtfm2-4 when the size field matches the matrix's size (2 to 4), vhtfm2-4 when it's one less
    u32 matrix = (OPCODE >> 23 & 7) + 1;
    if(VN == matrix) return VTFM(VD, VS, VT, matrix);
    if(VN == matrix - 1) return VHTFM(VD, VS, VT, matrix);
    return INVALID();
  }
  op(4, VMSCL, VD, VS, VT, VN);
  case 5:
    if(VN == 3) return VCRSP(VD, VS, VT, VN);
    if(VN == 4) return VQMUL(VD, VS, VT, VN);
    return INVALID();
  case 7:
    if((OPCODE >> 21 & 3) == 1) return VROT(VD, VS, VN, u8(OPCODE >> 16 & 31));
    switch(VT) {
    op(0x00, VMMOV, VD, VS, VN);
    op(0x03, VMIDT, VD, VN);
    op(0x06, VMZERO, VD, VN);
    op(0x07, VMONE, VD, VN);
    }
    return INVALID();
  }
  INVALID();
}

auto Allegrex::decoderVFPU7() -> void {
  switch(OPCODE) {
  case 0xffff'0000: return VNOP();
  case 0xffff'0320: case 0xffff'040d: return VSYNC();  //vsync, vflush
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
#undef VD
#undef VS
#undef VT
#undef VN
#undef VTS
#undef VTQ
#undef IMMv
