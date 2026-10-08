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
//every block's prologue sets (emit()) and blocks going on to each other keep. The address is worked out in 64 bits
//from the base register widened without its sign: only its bits 0-28 are used from there on, and those are the
//32-bit sum's, so no 32-bit result (whose upper half sljit leaves undefined) has to be widened again.

auto Allegrex::Recompiler::emitMemory(u32 address, u32 instruction, u32 count, bool delaySlot, bool store,
                                       u32 alignment, s16 offset, const std::function<void ()>& access) -> void {
  mov64_u32(reg(0), use(instruction >> 21 & 31));
  add64(reg(0), reg(0), imm(offset));
  sljit_jump* misaligned = nullptr;
  if(alignment > 1) {
    test32(reg(0), imm(alignment - 1), set_z);
    misaligned = jump(flag_nz);
  }
  lshr64(reg(1), reg(0), imm(12));
  and64(reg(1), reg(1), imm(0x1'ffff));  //the physical page: address bits 12-28
  mov64(reg(1), mem(SLJIT_MEM2(store ? SLJIT_S2 : SLJIT_S1, SLJIT_R1), 3));  //its entry: table + page * 8
  auto missing = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
  and64(reg(0), reg(0), imm(0xfff));
  if(alignment == 16) add64(reg(1), reg(1), reg(0));  //(a quad's four words: reg(1) the host address)
  access();
  auto done = jump();

  if(misaligned) setLabel(misaligned);
  setLabel(missing);
  emitFallback(address, instruction, count, delaySlot);  //(a load's result read back from what it wrote)
  setLabel(done);
}

#define RTn (instruction >> 16 & 31)
#define Rt  use(RTn)
#define Dt  def(RTn)
#define FT  (instruction >> 16 & 31)
#define VTS ((instruction >> 16 & 31) | (instruction & 3) << 5)
#define VTQ ((instruction >> 16 & 31) | (instruction & 1) << 5)

//Each access below reads or writes the host's memory at Host, the page emitMemory() found plus the address's low 12
//bits, or a quad's at mem(reg(1), byte offset). A load into r0 does nothing once it's past the checks (which can
//still raise an exception). Stores take their value in reg(2), as reg(0) and reg(1) hold the address. A load's
//result goes into the host register that holds it from then on (recompiler.cpp's use() and def()).
#define Host mem(SLJIT_MEM2(SLJIT_R1, SLJIT_R0), 0)
auto Allegrex::Recompiler::emitLoadStore(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool {
  if(!self.pages) return false;
  //The VFPU's loads and stores keep two bits of their register number in the offset's low bits.
  s16 offset = s16(instruction);
  s16 vfpuOffset = s16(instruction & 0xfffc);
  auto memory = [&](bool store, u32 alignment, s16 displacement, const std::function<void ()>& access) {
    u32 op = instruction >> 26;
    if(op == 0x28 || op == 0x29 || op == 0x2b) (void)Rt;  //(a store's value is read before the fast path splits)
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
    mov32(reg(2), Rt);
    mov32_u8(Host, reg(2));
  });

  //SH Rt,Rs,i16
  case 0x29: return memory(true, 2, offset, [&] {
    mov32(reg(2), Rt);
    mov32_u16(Host, reg(2));
  });

  //SW Rt,Rs,i16
  case 0x2b: return memory(true, 4, offset, [&] {
    mov32(reg(2), Rt);
    mov32(Host, reg(2));
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
