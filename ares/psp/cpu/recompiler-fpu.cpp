//Native code for the FPU (coprocessor 1), and for the moves between the integer registers and either coprocessor.
//Whatever returns false here is compiled as a call into the interpreter instead (recompiler.cpp).
//
//The FPU's arithmetic is the host's own: one IEEE single-precision operation, rounded to the nearest, is the same
//operation on every host, so it gives the same bits however it's reached. The interpreter (interpreter-fpu.cpp)
//does a little more around it, and the native code does exactly that much or hands the instruction over:
//  - FCSR's rounding mode (bits 0-1) other than to the nearest, and its flush-to-zero bit (24): the interpreter
//    switches the host's rounding or flushes subnormals then. Native arithmetic runs only while FCSR has none of
//    those three bits set, which it checks each time (a game may set them, with ctc1, at any moment); otherwise the
//    instruction goes to the interpreter. With them clear, f() and setF() leave values as they are.
//  - A result that isn't a number: which NaN the PSP gives depends on the inputs' bits (pickNaN()), so the native
//    code, having computed into a host register without writing fd, hands the instruction to the interpreter,
//    which computes it again from the same, untouched, inputs.
//The arithmetic runs on the emulation thread, in the host's floating-point mode, exactly as the interpreter's does
//(the host's own defaults: to the nearest, subnormals kept, no traps), so even subnormal inputs and results come out
//alike. Every native operation here is a single one (no product fused into a sum, which would round once instead of
//twice).

#define RTn (instruction >> 16 & 31)
#define Rt  gpr(RTn)
#define FD  (instruction >>  6 & 31)
#define FS  (instruction >> 11 & 31)
#define FT  (instruction >> 16 & 31)
#define Fd  field(&self.fpu.r[FD])
#define Fs  field(&self.fpu.r[FS])
#define Ft  field(&self.fpu.r[FT])
#define Csr field(&self.fpu.csr)

auto Allegrex::Recompiler::emitFPU(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool {
  switch(instruction >> 21 & 31) {

  //MFC1 Rt,Fs: the bits as they are
  case 0x00: {
    if(!RTn) return true;
    mov32(reg(0), Fs);
    mov32(Rt, reg(0));
    return true;
  }

  //MTC1 Rt,Fs
  case 0x04: {
    mov32(reg(0), Rt);
    mov32(Fs, reg(0));
    return true;
  }

  //single precision
  case 0x10: {
    switch(instruction & 0x3f) {
    case 0x00: emitFPUArithmetic(address, instruction, count, delaySlot, SLJIT_ADD_F32); return true;  //ADD.S
    case 0x01: emitFPUArithmetic(address, instruction, count, delaySlot, SLJIT_SUB_F32); return true;  //SUB.S
    case 0x02: emitFPUArithmetic(address, instruction, count, delaySlot, SLJIT_MUL_F32); return true;  //MUL.S
    case 0x03: emitFPUArithmetic(address, instruction, count, delaySlot, SLJIT_DIV_F32); return true;  //DIV.S

    //ABS.S, MOV.S and NEG.S only touch bits (the sign, or nothing), as the interpreter's do
    case 0x05: and32(Fd, Fs, imm(0x7fff'ffff)); return true;
    case 0x06: mov32(reg(0), Fs); mov32(Fd, reg(0)); return true;
    case 0x07: xor32(Fd, Fs, imm(s32(0x8000'0000))); return true;

    //TRUNC.W.S: below 2^31 in size (the exponent's and the significand's bits under 0x4f000000), the host's
    //conversion toward zero gives exactly what toWord() does (a subnormal, flushed or not, truncates to 0). The
    //rest (too big either way, -2^31 itself among them, infinities and NaNs, which toWord() clamps its own way)
    //goes to the interpreter.
    case 0x0d: {
      mov32(reg(0), Fs);
      and32(reg(1), reg(0), imm(0x7fff'ffff));
      cmp32(reg(1), imm(0x4f00'0000), set_uge);
      auto outside = jump(flag_uge);
      sljit_emit_fop1(compiler, SLJIT_CONV_S32_FROM_F32, SLJIT_R1, 0, Fs.fst, Fs.snd);
      mov32(Fd, reg(1));
      auto done = jump();
      setLabel(outside);
      emitInterpreter(address, instruction, count, delaySlot);
      setLabel(done);
      return true;
    }
    }
    if((instruction & 0x3f) >= 0x30) {
      emitFPUCompare(address, instruction, count, delaySlot);
      return true;
    }
    return false;
  }

  //CVT.S.W Fd,Fs: a whole number to a float, rounded to the nearest by the host as by the interpreter (an integer
  //is never subnormal, so the flush-to-zero bit changes nothing); the other rounding modes go to the interpreter
  case 0x14: {
    if((instruction & 0x3f) != 0x20) return false;
    mov32(reg(0), Csr);
    test32(reg(0), imm(3), set_z);
    auto other = jump(flag_nz);
    sljit_emit_fop1(compiler, SLJIT_CONV_F32_FROM_S32, SLJIT_FR0, 0, Fs.fst, Fs.snd);
    sljit_emit_fop1(compiler, SLJIT_MOV_F32, Fd.fst, Fd.snd, SLJIT_FR0, 0);
    auto done = jump();
    setLabel(other);
    emitInterpreter(address, instruction, count, delaySlot);
    setLabel(done);
    return true;
  }

  }
  return false;
}

//ADD.S, SUB.S, MUL.S, DIV.S Fd,Fs,Ft: see the top of this file.
auto Allegrex::Recompiler::emitFPUArithmetic(u32 address, u32 instruction, u32 count, bool delaySlot, s32 op)
  -> void {
  mov32(reg(0), Csr);
  test32(reg(0), imm(0x0100'0003), set_z);
  auto other = jump(flag_nz);
  sljit_emit_fop2(compiler, op, SLJIT_FR0, 0, Fs.fst, Fs.snd, Ft.fst, Ft.snd);
  auto notNumber = sljit_emit_fcmp(compiler, SLJIT_UNORDERED | SLJIT_32, SLJIT_FR0, 0, SLJIT_FR0, 0);
  sljit_emit_fop1(compiler, SLJIT_MOV_F32, Fd.fst, Fd.snd, SLJIT_FR0, 0);
  auto done = jump();
  setLabel(other);
  setLabel(notNumber);
  emitInterpreter(address, instruction, count, delaySlot);
  setLabel(done);
}

//C.cond.S Fs,Ft: FCSR bit 23 becomes the comparison's answer. The condition's low three bits are the outcomes that
//count (bit 0 unordered, bit 1 equal, bit 2 less), so each condition is one of the host's ordered or unordered
//comparisons; bit 3 (signaling) changes no answer. Only the flush-to-zero bit matters here (f() flushes subnormal
//inputs when it's set): with it, the interpreter compares.
auto Allegrex::Recompiler::emitFPUCompare(u32 address, u32 instruction, u32 count, bool delaySlot) -> void {
  static constexpr s32 holds[8] = {
    0, SLJIT_UNORDERED, SLJIT_ORDERED_EQUAL, SLJIT_UNORDERED_OR_EQUAL,
    SLJIT_ORDERED_LESS, SLJIT_UNORDERED_OR_LESS, SLJIT_ORDERED_LESS_EQUAL, SLJIT_UNORDERED_OR_LESS_EQUAL,
  };
  mov32(reg(0), Csr);
  test32(reg(0), imm(0x0100'0000), set_z);
  auto flushing = jump(flag_nz);
  and32(reg(0), reg(0), imm(~(1 << 23)));
  if(u32 condition = instruction & 7) {
    //(each sljit comparison and its opposite differ in the lowest bit)
    auto fails = sljit_emit_fcmp(compiler, (holds[condition] ^ 1) | SLJIT_32, Fs.fst, Fs.snd, Ft.fst, Ft.snd);
    or32(reg(0), reg(0), imm(1 << 23));
    setLabel(fails);
  }
  mov32(Csr, reg(0));
  auto done = jump();
  setLabel(flushing);
  emitInterpreter(address, instruction, count, delaySlot);
  setLabel(done);
}

//Coprocessor 2's moves: MFV and MTV copy a VFPU register's bits to or from an integer register, as the interpreter
//does, and leave the prefixes alone; so does MFVC (bit 7: a control register: recompiler-vfpu.cpp). MTVC goes to the
//interpreter.
auto Allegrex::Recompiler::emitCOP2(u32 instruction) -> bool {
  u32 vd = instruction & 0x7f;
  switch(instruction >> 21 & 31) {

  //MFV Rt,Vd, or MFVC Rt,index
  case 0x03: {
    if(instruction & 0x80) {
      emitMFVC(instruction);
      return true;
    }
    if(!RTn) return true;
    mov32(reg(0), field(&self.vfpu.r[vd]));
    mov32(Rt, reg(0));
    return true;
  }

  //MTV Rt,Vd
  case 0x07: {
    if(instruction & 0x80) return false;
    mov32(reg(0), Rt);
    mov32(field(&self.vfpu.r[vd]), reg(0));
    return true;
  }

  }
  return false;
}

#undef RTn
#undef Rt
#undef FD
#undef FS
#undef FT
#undef Fd
#undef Fs
#undef Ft
#undef Csr
