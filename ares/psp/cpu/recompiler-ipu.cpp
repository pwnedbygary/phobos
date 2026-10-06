//Native code for the integer instructions: the common ones that can't raise an exception, and the branches.
//Whatever returns false here is compiled as a call into the interpreter instead (recompiler.cpp).
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

auto Allegrex::Recompiler::emitInstruction(u32 instruction) -> bool {
  switch(instruction >> 26) {

  case 0x00: return emitSPECIAL(instruction);

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

  //SPECIAL3: here, only seb and seh (bshfl with rs = 0, which the interpreter requires too)
  case 0x1f: {
    if((instruction & 0x3f) != 0x20 || RSn != 0) return false;
    if(SA == 0x10) {  //SEB Rd,Rt
      if(!RDn) return true;
      mov32_s8(reg(0), Rt);
      mov32(Rd, reg(0));
      return true;
    }
    if(SA == 0x18) {  //SEH Rd,Rt
      if(!RDn) return true;
      mov32_s16(reg(0), Rt);
      mov32(Rd, reg(0));
      return true;
    }
    return false;
  }

  }
  return false;
}

auto Allegrex::Recompiler::emitSPECIAL(u32 instruction) -> bool {
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

  }
  return false;
}

//Branches and jumps. Each one decides where the CPU goes after its delay slot and stores that in pc (and pd as the
//word after it), which is how the interpreter has them while the delay slot runs; recompiler.cpp then compiles the
//delay slot and ends the block. The decision comes first, before the link or the delay slot can change the
//registers it depends on. Branches not handled here (the FPU's and the VFPU's) go to the interpreter.
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

//Finishes a conditional branch, given the jump its "taken" case makes. Either way pc and pd are set for the delay
//slot: to the target if taken, to the instruction after the delay slot if not. A likely branch that isn't taken
//skips its delay slot instead, so the block leaves right there, going on after the delay slot.
auto Allegrex::Recompiler::emitBranchOutcome(sljit_jump* taken, u32 address, u32 target, bool likely, u32 count) -> void {
  mov32(PC, imm(address + 8));
  mov32(PD, imm(address + 12));
  sljit_jump* done = nullptr;
  if(likely) {
    mov32(field(&executed), imm(count));
    jumpEpilog();
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
