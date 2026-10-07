//Native code for the integer instructions and the branches. Whatever returns false here is compiled as a call into
//the interpreter instead (recompiler.cpp); so are, as they run, the few cases a native instruction leaves to it
//(an overflow that raises an exception, a division by zero), from the start, before anything is written.
//
//No MIPS register stays in a host register from one instruction to the next: each instruction reads its inputs
//from ipu.r[] and writes its result straight back. That keeps every instruction independent, and the state always
//in memory, where the interpreter and the HLE kernel look for it.
//
//r0 always reads as zero, so an instruction whose only effect would be writing r0 compiles to nothing.
//
//A few instructions read only a register's low byte or halfword from memory: the lowest-addressed one, on every
//host this runs on (ARM64 and x86-64 are little-endian).

#define RDn (instruction >> 11 & 31)
#define RTn (instruction >> 16 & 31)
#define RSn (instruction >> 21 & 31)
#define SA  (instruction >>  6 & 31)
#define Rd  gpr(RDn)
#define Rt  gpr(RTn)
#define Rs  gpr(RSn)
#define Hi  field(&self.ipu.hi)
#define Lo  field(&self.ipu.lo)
#define PC  field(&self.ipu.pc)
#define PD  field(&self.ipu.pd)
#define i16 s16(instruction)
#define n16 u16(instruction)

auto Allegrex::Recompiler::emitInstruction(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool {
  switch(instruction >> 26) {

  case 0x00: return emitSPECIAL(address, instruction, count, delaySlot);

  //ADDI Rt,Rs,i16: ADDIU's sum, which may overflow (emitTrapping())
  case 0x08: {
    emitTrapping(address, instruction, count, delaySlot, RTn, false);
    return true;
  }

  //the FPU, and coprocessor 2's moves (recompiler-fpu.cpp)
  case 0x11: return emitFPU(address, instruction, count, delaySlot);
  case 0x12: return emitCOP2(instruction);

  //the VFPU (recompiler-vfpu.cpp)
  case 0x18: case 0x19: case 0x1b: case 0x34: case 0x3c: case 0x3f:
    return emitVFPU(address, instruction, count, delaySlot);

  //CACHE: nothing to do, as in the interpreter
  case 0x2f: return true;

  //loads and stores (recompiler-memory.cpp)
  case 0x20: case 0x21: case 0x23: case 0x24: case 0x25: case 0x28: case 0x29: case 0x2b:
  case 0x31: case 0x32: case 0x36: case 0x39: case 0x3a: case 0x3e:
    return emitLoadStore(address, instruction, count, delaySlot);

  //ADDIU Rt,Rs,i16
  case 0x09: {
    if(RTn) add32(Rt, Rs, imm(i16));
    return true;
  }

  //SLTI Rt,Rs,i16
  case 0x0a: {
    if(!RTn) return true;
    cmp32(Rs, imm(i16), set_slt);
    mov32_f(Rt, flag_slt);
    return true;
  }

  //SLTIU Rt,Rs,i16 (the constant is sign-extended, then compared unsigned)
  case 0x0b: {
    if(!RTn) return true;
    cmp32(Rs, imm(i16), set_ult);
    mov32_f(Rt, flag_ult);
    return true;
  }

  //ANDI Rt,Rs,n16
  case 0x0c: {
    if(RTn) and32(Rt, Rs, imm(n16));
    return true;
  }

  //ORI Rt,Rs,n16
  case 0x0d: {
    if(RTn) or32(Rt, Rs, imm(n16));
    return true;
  }

  //XORI Rt,Rs,n16
  case 0x0e: {
    if(RTn) xor32(Rt, Rs, imm(n16));
    return true;
  }

  //LUI Rt,n16
  case 0x0f: {
    if(RTn) mov32(Rt, imm(s32(u32(n16) << 16)));
    return true;
  }

  case 0x1f: return emitSPECIAL3(instruction);

  }
  return false;
}

//SPECIAL3: the bit fields, and bshfl's byte shuffles (bitrev goes to the interpreter).
auto Allegrex::Recompiler::emitSPECIAL3(u32 instruction) -> bool {
  switch(instruction & 0x3f) {

  //EXT Rt,Rs,lsb,size: size bits of rs from bit lsb (the sa field) down to bit 0; rd holds the size less one
  case 0x00: {
    if(!RTn) return true;
    lshr32(reg(0), Rs, imm(SA));
    and32(Rt, reg(0), imm(s32(u32((1ull << (RDn + 1)) - 1))));
    return true;
  }

  //INS Rt,Rs,lsb,msb: rs's low bits into bits lsb (sa) to msb (rd) of rt, the rest of rt kept; a field whose top
  //is below its bottom leaves rt as it was, as the interpreter's does
  case 0x04: {
    u32 lsb = SA, msb = RDn;
    if(!RTn || msb < lsb) return true;
    u32 mask = u32(((1ull << (msb - lsb + 1)) - 1) << lsb);
    shl32(reg(0), Rs, imm(lsb));
    and32(reg(0), reg(0), imm(s32(mask)));
    and32(reg(1), Rt, imm(s32(~mask)));
    or32(Rt, reg(0), reg(1));
    return true;
  }

  //bshfl, with rs = 0 (which the interpreter requires too)
  case 0x20: {
    if(RSn != 0) return false;
    switch(SA) {

    //WSBH Rd,Rt: the bytes of each halfword swapped
    case 0x02: {
      if(!RDn) return true;
      mov32(reg(0), Rt);
      and32(reg(1), reg(0), imm(0x00ff'00ff));
      shl32(reg(1), reg(1), imm(8));
      lshr32(reg(0), reg(0), imm(8));
      and32(reg(0), reg(0), imm(0x00ff'00ff));
      or32(Rd, reg(0), reg(1));
      return true;
    }

    //WSBW Rd,Rt: all four bytes reversed
    case 0x03: {
      if(!RDn) return true;
      mov32(reg(0), Rt);
      sljit_emit_op1(compiler, SLJIT_REV32, SLJIT_R0, 0, SLJIT_R0, 0);
      mov32(Rd, reg(0));
      return true;
    }

    //SEB Rd,Rt
    case 0x10: {
      if(!RDn) return true;
      mov32_s8(reg(0), Rt);
      mov32(Rd, reg(0));
      return true;
    }

    //SEH Rd,Rt
    case 0x18: {
      if(!RDn) return true;
      mov32_s16(reg(0), Rt);
      mov32(Rd, reg(0));
      return true;
    }

    }
    return false;
  }

  }
  return false;
}

//ADD Rd,Rs,Rt, ADDI Rt,Rs,i16 and SUB Rd,Rs,Rt: ADDU's, ADDIU's and SUBU's results, but a result that overflows a
//signed 32-bit number raises an exception and leaves the destination as it was. The result goes into a host
//register first, and on an overflow the instruction runs through the interpreter from the start (nothing written
//yet), which raises the exception exactly as it always has. rd is the destination's number (rt's, for ADDI).
auto Allegrex::Recompiler::emitTrapping(u32 address, u32 instruction, u32 count, bool delaySlot, u32 rd,
                                        bool subtract) -> void {
  mov32(reg(0), Rs);
  if(instruction >> 26 == 0x08) add32(reg(0), reg(0), imm(i16), set_o);
  else if(subtract) sub32(reg(0), reg(0), Rt, set_o);
  else add32(reg(0), reg(0), Rt, set_o);
  auto overflow = jump(flag_o);
  if(rd) mov32(gpr(rd), reg(0));
  auto done = jump();
  setLabel(overflow);
  emitInterpreter(address, instruction, count, delaySlot);
  setLabel(done);
}

//MULT, MULTU, MADD, MADDU, MSUB and MSUBU Rs,Rt: the 64-bit product of rs and rt (each widened by its sign, or
//not) into hi and lo, or added to (accumulate 1) or taken from (-1) the 64-bit number they hold together, wrapping
//round as the interpreter's unsigned sum does.
auto Allegrex::Recompiler::emitMultiply(u32 instruction, bool isSigned, s32 accumulate) -> void {
  if(isSigned) {
    mov64_s32(reg(0), Rs);
    mov64_s32(reg(1), Rt);
  } else {
    mov64_u32(reg(0), Rs);
    mov64_u32(reg(1), Rt);
  }
  mul64(reg(0), reg(0), reg(1));
  if(accumulate) {
    mov64_u32(reg(1), Lo);
    mov64_u32(reg(2), Hi);
    shl64(reg(2), reg(2), imm(32));
    or64(reg(1), reg(1), reg(2));
    if(accumulate > 0) add64(reg(0), reg(1), reg(0));
    else sub64(reg(0), reg(1), reg(0));
  }
  mov32(Lo, reg(0));
  lshr64(reg(0), reg(0), imm(32));
  mov32(Hi, reg(0));
}

auto Allegrex::Recompiler::emitSPECIAL(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool {
  switch(instruction & 0x3f) {

  //SLL Rd,Rt,Sa (sll r0,r0,0 is nop)
  case 0x00: {
    if(RDn) shl32(Rd, Rt, imm(SA));
    return true;
  }

  //SRL Rd,Rt,Sa, or ROTR Rd,Rt,Sa when rs is 1
  case 0x02: {
    if(!RDn) return true;
    if(RSn == 1) rotr32(Rd, Rt, imm(SA));
    else lshr32(Rd, Rt, imm(SA));
    return true;
  }

  //SRA Rd,Rt,Sa
  case 0x03: {
    if(RDn) ashr32(Rd, Rt, imm(SA));
    return true;
  }

  //SLLV Rd,Rt,Rs (the "m" shifts use only the amount's low five bits, as MIPS does)
  case 0x04: {
    if(RDn) mshl32(Rd, Rt, Rs);
    return true;
  }

  //SRLV Rd,Rt,Rs, or ROTRV Rd,Rt,Rs when sa is 1 (rotations always use the low five bits)
  case 0x06: {
    if(!RDn) return true;
    if(SA == 1) rotr32(Rd, Rt, Rs);
    else mlshr32(Rd, Rt, Rs);
    return true;
  }

  //SRAV Rd,Rt,Rs
  case 0x07: {
    if(RDn) mashr32(Rd, Rt, Rs);
    return true;
  }

  //MOVZ Rd,Rs,Rt
  case 0x0a: {
    if(!RDn) return true;
    auto skip = cmp32_jump(Rt, imm(0), flag_ne);
    mov32(Rd, Rs);
    setLabel(skip);
    return true;
  }

  //MOVN Rd,Rs,Rt
  case 0x0b: {
    if(!RDn) return true;
    auto skip = cmp32_jump(Rt, imm(0), flag_eq);
    mov32(Rd, Rs);
    setLabel(skip);
    return true;
  }

  //MFHI Rd
  case 0x10: {
    if(RDn) mov32(Rd, Hi);
    return true;
  }

  //MTHI Rs
  case 0x11: {
    mov32(Hi, Rs);
    return true;
  }

  //MFLO Rd
  case 0x12: {
    if(RDn) mov32(Rd, Lo);
    return true;
  }

  //MTLO Rs
  case 0x13: {
    mov32(Lo, Rs);
    return true;
  }

  //CLZ Rd,Rs and CLO Rd,Rs: leading zeros, or leading ones (the zeros of rs inverted); 32 for none
  case 0x16: case 0x17: {
    if(!RDn) return true;
    if(instruction & 1) xor32(reg(0), Rs, imm(-1));
    else mov32(reg(0), Rs);
    sljit_emit_op1(compiler, SLJIT_CLZ32, SLJIT_R0, 0, SLJIT_R0, 0);
    mov32(Rd, reg(0));
    return true;
  }

  //MULT, MULTU Rs,Rt
  case 0x18: case 0x19: {
    emitMultiply(instruction, !(instruction & 1), 0);
    return true;
  }

  //DIV and DIVU Rs,Rt: the quotient into lo and the remainder into hi, rounded toward zero, by the host's own
  //division. Dividing by zero, and dividing by -1 (where the most negative number's quotient doesn't fit), get the
  //PSP's answers from the interpreter: the host leaves those undefined (x86 traps on them).
  case 0x1a: case 0x1b: {
    bool isSigned = !(instruction & 1);
    mov32(reg(1), Rt);
    auto byZero = cmp32_jump(reg(1), imm(0), flag_eq);
    sljit_jump* byMinusOne = isSigned ? cmp32_jump(reg(1), imm(-1), flag_eq) : nullptr;
    mov32(reg(0), Rs);
    sljit_emit_op0(compiler, isSigned ? SLJIT_DIVMOD_S32 : SLJIT_DIVMOD_U32);  //r0: the quotient; r1: the remainder
    mov32(Lo, reg(0));
    mov32(Hi, reg(1));
    auto done = jump();
    setLabel(byZero);
    if(byMinusOne) setLabel(byMinusOne);
    emitInterpreter(address, instruction, count, delaySlot);
    setLabel(done);
    return true;
  }

  //MADD, MADDU Rs,Rt
  case 0x1c: case 0x1d: {
    emitMultiply(instruction, !(instruction & 1), +1);
    return true;
  }

  //ADD Rd,Rs,Rt (emitTrapping())
  case 0x20: {
    emitTrapping(address, instruction, count, delaySlot, RDn, false);
    return true;
  }

  //SUB Rd,Rs,Rt
  case 0x22: {
    emitTrapping(address, instruction, count, delaySlot, RDn, true);
    return true;
  }

  //ADDU Rd,Rs,Rt
  case 0x21: {
    if(RDn) add32(Rd, Rs, Rt);
    return true;
  }

  //SUBU Rd,Rs,Rt
  case 0x23: {
    if(RDn) sub32(Rd, Rs, Rt);
    return true;
  }

  //AND Rd,Rs,Rt
  case 0x24: {
    if(RDn) and32(Rd, Rs, Rt);
    return true;
  }

  //OR Rd,Rs,Rt
  case 0x25: {
    if(RDn) or32(Rd, Rs, Rt);
    return true;
  }

  //XOR Rd,Rs,Rt
  case 0x26: {
    if(RDn) xor32(Rd, Rs, Rt);
    return true;
  }

  //NOR Rd,Rs,Rt
  case 0x27: {
    if(!RDn) return true;
    or32(reg(0), Rs, Rt);
    xor32(Rd, reg(0), imm(-1));
    return true;
  }

  //SLT Rd,Rs,Rt
  case 0x2a: {
    if(!RDn) return true;
    cmp32(Rs, Rt, set_slt);
    mov32_f(Rd, flag_slt);
    return true;
  }

  //SLTU Rd,Rs,Rt
  case 0x2b: {
    if(!RDn) return true;
    cmp32(Rs, Rt, set_ult);
    mov32_f(Rd, flag_ult);
    return true;
  }

  //MAX Rd,Rs,Rt: reg(1) starts as rt and becomes rs if rs is greater
  case 0x2c: {
    if(!RDn) return true;
    mov32(reg(0), Rs);
    mov32(reg(1), Rt);
    cmp32(reg(0), reg(1), set_sgt);
    cmov32(reg(1), reg(0), reg(1), flag_sgt);
    mov32(Rd, reg(1));
    return true;
  }

  //MIN Rd,Rs,Rt: likewise, if rs is less
  case 0x2d: {
    if(!RDn) return true;
    mov32(reg(0), Rs);
    mov32(reg(1), Rt);
    cmp32(reg(0), reg(1), set_slt);
    cmov32(reg(1), reg(0), reg(1), flag_slt);
    mov32(Rd, reg(1));
    return true;
  }

  //MSUB, MSUBU Rs,Rt
  case 0x2e: case 0x2f: {
    emitMultiply(instruction, !(instruction & 1), -1);
    return true;
  }

  }
  return false;
}

//Branches and jumps. Each one decides where the CPU goes after its delay slot and stores that in pc (and pd as the
//word after it), which is how the interpreter has them while the delay slot runs; recompiler.cpp then compiles the
//delay slot and ends the block. The decision comes first, before the link or the delay slot can change the
//registers it depends on. The coprocessors' branches end their block by themselves (emitCoprocessorBranch()).
auto Allegrex::Recompiler::emitBranch(u32 address, u32 instruction, u32 count) -> bool {
  //a branch's offset counts instructions from its delay slot
  u32 target = address + 4 + i16 * 4;

  switch(instruction >> 26) {

  case 0x00: {
    //JR Rs
    if((instruction & 0x3f) == 0x08) {
      mov32(reg(0), Rs);
      mov32(PC, reg(0));
      add32(PD, reg(0), imm(4));
      return true;
    }
    //JALR Rd,Rs (the target is read before rd is written, in case they're the same register)
    if((instruction & 0x3f) == 0x09) {
      mov32(reg(0), Rs);
      if(RDn) mov32(Rd, imm(address + 8));
      mov32(PC, reg(0));
      add32(PD, reg(0), imm(4));
      return true;
    }
    return false;
  }

  //REGIMM: BLTZ, BGEZ, BLTZL, BGEZL, BLTZAL, BGEZAL, BLTZALL, BGEZALL Rs,i16
  //The rt field picks the form: bit 0 makes it "greater or equal" instead of "less than" zero, bit 1 makes it
  //likely, bit 4 makes it link. Linking stores the return address in ra, taken or not, after rs is read.
  case 0x01: {
    bool greaterEqual = RTn & 1;
    bool likely = RTn & 2;
    bool link = RTn & 0x10;
    mov32(reg(0), Rs);
    if(link) mov32(gpr(31), imm(address + 8));
    auto taken = cmp32_jump(reg(0), imm(0), greaterEqual ? flag_sge : flag_slt);
    emitBranchOutcome(taken, address, target, likely, count);
    return true;
  }

  //J target: within the 256 MiB region the delay slot is in
  case 0x02: {
    emitJump(((address + 4) & 0xf000'0000) | (instruction & 0x03ff'ffff) << 2);
    return true;
  }

  //JAL target
  case 0x03: {
    mov32(gpr(31), imm(address + 8));
    emitJump(((address + 4) & 0xf000'0000) | (instruction & 0x03ff'ffff) << 2);
    return true;
  }

  //BEQ Rs,Rt,i16 and BEQL
  case 0x04: case 0x14: {
    auto taken = cmp32_jump(Rs, Rt, flag_eq);
    emitBranchOutcome(taken, address, target, instruction >> 26 == 0x14, count);
    return true;
  }

  //BNE Rs,Rt,i16 and BNEL
  case 0x05: case 0x15: {
    auto taken = cmp32_jump(Rs, Rt, flag_ne);
    emitBranchOutcome(taken, address, target, instruction >> 26 == 0x15, count);
    return true;
  }

  //BLEZ Rs,i16 and BLEZL
  case 0x06: case 0x16: {
    auto taken = cmp32_jump(Rs, imm(0), flag_sle);
    emitBranchOutcome(taken, address, target, instruction >> 26 == 0x16, count);
    return true;
  }

  //BGTZ Rs,i16 and BGTZL
  case 0x07: case 0x17: {
    auto taken = cmp32_jump(Rs, imm(0), flag_sgt);
    emitBranchOutcome(taken, address, target, instruction >> 26 == 0x17, count);
    return true;
  }

  }
  return false;
}

//The coprocessors' branches, BC1F, BC1T, BC1FL and BC1TL on the FPU's condition (FCSR bit 23), and BVF, BVT, BVFL
//and BVTL on one of the VFPU's condition codes (bits 18-20 pick which); bit 16 branches when it's true, bit 17
//makes the branch likely. The block ends right after one, as it did when the interpreter ran them: where blocks
//end is where a run of the CPU may stop (Allegrex::run() checks its limit between blocks), so it decides when
//interrupts and the HLE kernel's events land, and has to stay as it was for every run to come out the same. So the
//branch leaves pc and pd as the interpreter's would: pc at the delay slot and pd at the target or past the delay
//slot, or both past the delay slot for a likely branch not taken; run() then has the interpreter take a delay slot
//before a taken branch's target, as before. Not taken, the block goes on to the next by itself (emitChainTo()).
auto Allegrex::Recompiler::emitCoprocessorBranch(u32 address, u32 instruction, u32 count) -> void {
  u32 target = address + 4 + s16(instruction) * 4;
  if(instruction >> 26 == 0x11) test32(field(&self.fpu.csr), imm(1 << 23), set_z);
  else test32(field(&self.vfpu.cc), imm(1 << (instruction >> 18 & 7)), set_z);
  auto taken = jump(instruction >> 16 & 1 ? flag_nz : flag_z);
  bool likely = instruction >> 17 & 1;
  mov32(PC, imm(address + (likely ? 8 : 4)));
  mov32(PD, imm(address + (likely ? 12 : 8)));
  if(chains) {
    emitChainTo(count, address + (likely ? 8 : 4));
  } else {
    mov32(field(&executed), imm(count));
    jumpEpilog();
  }
  setLabel(taken);
  mov32(PC, imm(address + 4));
  mov32(PD, imm(target));
  mov32(field(&executed), imm(count));
  jumpEpilog();
}

//Finishes a conditional branch, given the jump its "taken" case makes. Either way pc and pd are set for the delay
//slot: to the target if taken, to the instruction after the delay slot if not. A likely branch that isn't taken
//skips its delay slot instead, so the block ends right there, going on after the delay slot (by itself, as a block
//ending anywhere else does: emitChain()).
auto Allegrex::Recompiler::emitBranchOutcome(sljit_jump* taken, u32 address, u32 target, bool likely, u32 count) -> void {
  mov32(PC, imm(address + 8));
  mov32(PD, imm(address + 12));
  sljit_jump* done = nullptr;
  if(likely) {
    if(chains) {
      emitChainTo(count, address + 8);
    } else {
      mov32(field(&executed), imm(count));
      jumpEpilog();
    }
  } else {
    done = jump();
  }
  setLabel(taken);
  emitJump(target);
  if(done) setLabel(done);
}

auto Allegrex::Recompiler::emitJump(u32 target) -> void {
  mov32(PC, imm(target));
  mov32(PD, imm(target + 4));
}

#undef RDn
#undef RTn
#undef RSn
#undef SA
#undef Rd
#undef Rt
#undef Rs
#undef Hi
#undef Lo
#undef PC
#undef PD
#undef i16
#undef n16
