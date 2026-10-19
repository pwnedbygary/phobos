//Loads and stores, compiled to reach the host's memory directly through the page table (Allegrex::pages) instead
//of calling the interpreter, which would call read() or write().
//
//The fast path, in steps:
//  1. the address: rs plus the instruction's offset;
//  2. its alignment: a halfword's address must be even, a word's a multiple of four, a quad's (lv.q, sv.q) of 16;
//  3. its page: the table's entry for address bits 12-28, the physical page (the top three bits only pick a mirror);
//  4. the access itself, at that page's host memory plus the address's low 12 bits.
//Everything off the fast path runs the instruction through the interpreter instead, exactly as if it had never
//been compiled: a misaligned address (the interpreter raises the exception), a page without an entry (hardware
//registers, which read() and write() handle), and for stores, a page holding compiled code (writePages leaves
//those out, so the store goes through write(), which drops that code).
//
//The tables stay in two of sljit's saved registers, S1 for loads (the CPU's) and S2 for stores (writePages), which
//every block's prologue sets (emit()) and blocks going on to each other keep. The address is rs's host register
//itself, or the 32-bit sum in reg(0): only its bits 0-28 are used, by 64-bit operations that mask the rest away, so
//whatever sljit leaves in a 32-bit result's upper half never matters. Everything off the fast path is cold code, out
//of the block's straight line (emitCold()).

auto Allegrex::Recompiler::emitMemory(u32 address, u32 instruction, u32 count, bool delaySlot, bool store,
                                       u32 alignment, s16 offset, const std::function<void ()>& access) -> void {
  u32 rs = instruction >> 21 & 31;
  op_base base = reg(0);
  if(rs == 0) mov32(reg(0), imm(offset));
  else if(offset == 0) base = use(rs);
  else add32(reg(0), use(rs), imm(offset));
  sljit_jump* misaligned = nullptr;
  if(alignment > 1) {
    test32(base, imm(alignment - 1), set_z);
    misaligned = jump(flag_nz);
  }
  if(self.dataCache.enabled) emitDataCache(base, store);  //(with the PSP's GE timing alone)
  //the physical page, address bits 12-28, into reg(1)
  #if defined(ARCHITECTURE_ARM64)
  u32 ubfx = 0xd340'0000 | 12 << 16 | 28 << 10 | sljit_get_register_index(SLJIT_GP_REGISTER, base.fst) << 5
           | sljit_get_register_index(SLJIT_GP_REGISTER, SLJIT_R1);  //UBFX X1, base, #12, #17
  sljit_emit_op_custom(compiler, &ubfx, sizeof(ubfx));
  #else
  lshr64(reg(1), base, imm(12));
  and64(reg(1), reg(1), imm(0x1'ffff));
  #endif
  mov64(reg(1), mem(SLJIT_MEM2(store ? SLJIT_S2 : SLJIT_S1, SLJIT_R1), 3));  //its entry: table + page * 8
  auto missing = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
  and64(reg(0), base, imm(0xfff));
  if(alignment == 16) add64(reg(1), reg(1), reg(0));  //(a quad's four words: reg(1) the host address)
  access();
  emitColdFallback({misaligned, missing}, address, instruction, count, delaySlot);  //(a load's result read back)
}

//With the PSP's GE timing, the access's line in its set's most recently used way of the data cache's tags
//(Allegrex::Cache): there (dirty already, for a store), nothing more; else, cold, dataCacheMiss() does the rest and
//returns the address, back where base had it (reg(0), or a saved register the call keeps). Before the page is looked
//up, so reg(1) to reg(3) are free; a misaligned access has gone to the interpreter already.
auto Allegrex::Recompiler::emitDataCache(op_base base, bool store) -> void {
  lshr32(reg(1), base, imm(6));                           //the line's key
  and32(reg(2), reg(1), imm(Cache::Sets - 1));            //its set
  add64(reg(3), sreg(0), imm((const u8*)self.dataCache.recent - (const u8*)&self));
  mov32(reg(2), mem(SLJIT_MEM2(SLJIT_R3, SLJIT_R2), 2));  //the set's most recently used way
  if(store) or32(reg(1), reg(1), imm(Cache::Dirty));
  else and32(reg(2), reg(2), imm(~Cache::Dirty));
  auto missed = sljit_emit_cmp(compiler, SLJIT_NOT_EQUAL, SLJIT_R1, 0, SLJIT_R2, 0);
  emitCold({missed}, [=, this] {
    mov32(reg(1), base);
    callf(&Allegrex::dataCacheMiss, reg(1), imm(store ? 1 : 0));
  });
}

//With the PSP's GE timing, the line of the instruction at address (as a block starts, or its code reaches a line's
//first word) in its set's most recently used way of the instruction cache's tags (Allegrex::Cache): there, nothing
//more; else, cold, instructionCacheMiss() does the rest. Between instructions, where no scratch register holds
//anything; code an uncached mirror runs holds no line.
auto Allegrex::Recompiler::emitInstructionCache(u32 address) -> void {
  if(u32 top = address >> 29; top != 0 && top != 4) return;
  mov32(reg(0), field(&self.instructionCache.recent[(address >> 6) & (Cache::Sets - 1)]));
  auto missed = cmp32_jump(reg(0), imm(Cache::codeKey(address)), flag_ne);
  emitCold({missed}, [=, this] { callf(&Allegrex::instructionCacheMiss, imm(address)); });
}

#define RTn (instruction >> 16 & 31)
#define Rt  use(RTn)
#define Dt  def(RTn)
#define FT  (instruction >> 16 & 31)
#define VTS ((instruction >> 16 & 31) | (instruction & 3) << 5)
#define VTQ ((instruction >> 16 & 31) | (instruction & 1) << 5)

//Each access below reads or writes the host's memory at Host, the page emitMemory() found plus the address's low 12
//bits, or a quad's at mem(reg(1), byte offset). A load into r0 does nothing once it's past the checks (which can
//still raise an exception). sb, sh and sw store rt as use() gives it (its host register, read before the fast path
//splits off; r0 as the constant 0); the coprocessors' stores and lwl, lwr, swl and swr work in reg(2) and reg(3) (and a
//quad's in reg(0)), as reg(0) and reg(1) hold the address. A load's result goes into the host register that holds it
//from then on (recompiler.cpp's use() and def()).
#define Host mem(SLJIT_MEM2(SLJIT_R1, SLJIT_R0), 0)
auto Allegrex::Recompiler::emitLoadStore(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool {
  if(!self.pages) return false;
  //The VFPU's loads and stores keep two bits of their register number in the offset's low bits.
  s16 offset = s16(instruction);
  s16 vfpuOffset = s16(instruction & 0xfffc);
  auto memory = [&](bool store, u32 alignment, s16 displacement, const std::function<void ()>& access) {
    u32 op = instruction >> 26;
    //(a store's value, and the bytes lwl and lwr keep, are read before the fast path splits off)
    if(op == 0x28 || op == 0x29 || op == 0x2b || op == 0x2a || op == 0x2e || ((op == 0x22 || op == 0x26) && RTn)) {
      (void)Rt;
    }
    emitMemory(address, instruction, count, delaySlot, store, alignment, displacement, access);
    return true;
  };

  switch(instruction >> 26) {

  //LB Rt,Rs,i16
  case 0x20: return memory(false, 1, offset, [&] {
    if(!RTn) return;
    mov32_s8(Dt, Host);
  });

  //LH Rt,Rs,i16
  case 0x21: return memory(false, 2, offset, [&] {
    if(!RTn) return;
    mov32_s16(Dt, Host);
  });

  //LW Rt,Rs,i16
  case 0x23: return memory(false, 4, offset, [&] {
    if(!RTn) return;
    mov32(Dt, Host);
  });

  //LBU Rt,Rs,i16
  case 0x24: return memory(false, 1, offset, [&] {
    if(!RTn) return;
    mov32_u8(Dt, Host);
  });

  //LHU Rt,Rs,i16
  case 0x25: return memory(false, 2, offset, [&] {
    if(!RTn) return;
    mov32_u16(Dt, Host);
  });

  //SB Rt,Rs,i16
  case 0x28: return memory(true, 1, offset, [&] {
    mov32_u8(Host, Rt);
  });

  //SH Rt,Rs,i16
  case 0x29: return memory(true, 2, offset, [&] {
    mov32_u16(Host, Rt);
  });

  //SW Rt,Rs,i16
  case 0x2b: return memory(true, 4, offset, [&] {
    mov32(Host, Rt);
  });

  //LWL and LWR Rt,Rs,i16, SWL and SWR: an unaligned word's halves, as the interpreter has them (interpreter-ipu.cpp),
  //from the aligned word round the address, whose byte within it (reg(2)) gives the shift: for lwl and swl, 8 for
  //each byte below the word's last; for lwr and swr, 8 for each above its first. lwl keeps rt's bytes below the
  //shift (a mask of shift ones), lwr those it shifts in no bytes over (the top shift bits); swl and swr keep the
  //memory word's likewise. A shift is 24 at most, so none of these shifts by 32 or more. A store's read and write
  //go through the store table, as sw's, onto memory that's plain host memory either way.
  case 0x22: case 0x26: return memory(false, 1, offset, [&] {
    if(!RTn) return;
    bool left = instruction >> 26 == 0x22;
    and32(reg(2), reg(0), imm(3));
    and64(reg(0), reg(0), imm(0xffc));
    mov32(reg(3), Host);
    if(left) xor32(reg(2), reg(2), imm(3));
    shl32(reg(2), reg(2), imm(3));
    if(left) {
      shl32(reg(3), reg(3), reg(2));
      mov32(reg(1), imm(1));
      shl32(reg(1), reg(1), reg(2));
      sub32(reg(1), reg(1), imm(1));
    } else {
      lshr32(reg(3), reg(3), reg(2));
      mov32(reg(1), imm(-1));
      lshr32(reg(1), reg(1), reg(2));
      xor32(reg(1), reg(1), imm(-1));
    }
    and32(reg(1), reg(1), Rt);
    or32(Dt, reg(1), reg(3));
  });
  case 0x2a: case 0x2e: return memory(true, 1, offset, [&] {
    bool left = instruction >> 26 == 0x2a;
    and32(reg(2), reg(0), imm(3));
    and64(reg(0), reg(0), imm(0xffc));
    add64(reg(1), reg(1), reg(0));  //(the word's host address, freeing reg(0))
    mov32(reg(3), mem(reg(1), 0));
    if(left) xor32(reg(2), reg(2), imm(3));
    shl32(reg(2), reg(2), imm(3));
    if(left) {
      mov32(reg(0), imm(-1));
      lshr32(reg(0), reg(0), reg(2));
      xor32(reg(0), reg(0), imm(-1));
      and32(reg(3), reg(3), reg(0));
      lshr32(reg(0), Rt, reg(2));
    } else {
      mov32(reg(0), imm(1));
      shl32(reg(0), reg(0), reg(2));
      sub32(reg(0), reg(0), imm(1));
      and32(reg(3), reg(3), reg(0));
      shl32(reg(0), Rt, reg(2));
    }
    or32(reg(3), reg(3), reg(0));
    mov32(mem(reg(1), 0), reg(3));
  });

  //LWC1 Ft,Rs,i16
  case 0x31: return memory(false, 4, offset, [&] {
    mov32(reg(0), Host);
    mov32(field(&self.fpu.r[FT]), reg(0));
  });

  //LV.S Vt,Rs,i16
  case 0x32: return memory(false, 4, vfpuOffset, [&] {
    mov32(reg(0), Host);
    mov32(field(&self.vfpu.r[VTS]), reg(0));
  });

  //LV.Q Vt,Rs,i16: the four lanes of a column or row, which the register number fixes when the block is compiled
  case 0x36: return memory(false, 16, vfpuOffset, [&] {
    auto lanes = self.vfpuLine(VTQ, 4);
    for(u32 k : range(4)) {
      mov32(reg(0), mem(reg(1), 4 * k));
      mov32(field(&self.vfpu.r[lanes[k]]), reg(0));
    }
  });

  //SWC1 Ft,Rs,i16
  case 0x39: return memory(true, 4, offset, [&] {
    mov32(reg(2), field(&self.fpu.r[FT]));
    mov32(Host, reg(2));
  });

  //SV.S Vt,Rs,i16
  case 0x3a: return memory(true, 4, vfpuOffset, [&] {
    mov32(reg(2), field(&self.vfpu.r[VTS]));
    mov32(Host, reg(2));
  });

  //SV.Q Vt,Rs,i16
  case 0x3e: return memory(true, 16, vfpuOffset, [&] {
    auto lanes = self.vfpuLine(VTQ, 4);
    for(u32 k : range(4)) {
      mov32(reg(0), field(&self.vfpu.r[lanes[k]]));
      mov32(mem(reg(1), 4 * k), reg(0));
    }
  });

  }
  return false;
}

#undef RTn
#undef Rt
#undef Dt
#undef FT
#undef VTS
#undef VTQ
#undef Host
