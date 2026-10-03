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
//sljit's 32-bit operations leave a register's upper half undefined, so a 32-bit value is widened (mov64_u32)
//before it becomes part of a 64-bit host address.

auto Allegrex::Recompiler::emitMemory(u32 address, u32 instruction, u32 count, bool delaySlot, bool store,
                                       u32 alignment, s16 offset, const std::function<void ()>& access) -> void {
  u8* const* table = store ? writePages.data() : self.pages;

  mov32(reg(0), gpr(instruction >> 21 & 31));
  add32(reg(0), reg(0), imm(offset));
  sljit_jump* misaligned = nullptr;
  if(alignment > 1) {
    test32(reg(0), imm(alignment - 1), set_z);
    misaligned = jump(flag_nz);
  }
  and32(reg(1), reg(0), imm(0x1fff'ffff));
  lshr32(reg(1), reg(1), imm(12));
  mov64_u32(reg(1), reg(1));
  mov64(reg(2), imm((sljit_sw)table));
  mov64(reg(1), mem(SLJIT_MEM2(SLJIT_R2, SLJIT_R1), 3));  //the page's entry: table + page * 8
  auto missing = sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
  and32(reg(0), reg(0), imm(0xfff));
  mov64_u32(reg(0), reg(0));
  add64(reg(1), reg(1), reg(0));  //reg(1): the host address
  access();
  auto done = jump();

  if(misaligned) setLabel(misaligned);
  setLabel(missing);
  emitInterpreter(address, instruction, count, delaySlot);
  setLabel(done);
}

#define RTn (instruction >> 16 & 31)
#define Rt  gpr(RTn)
#define FT  (instruction >> 16 & 31)
#define VTS ((instruction >> 16 & 31) | (instruction & 3) << 5)
#define VTQ ((instruction >> 16 & 31) | (instruction & 1) << 5)

//Each access below reads or writes at mem(reg(1), byte offset), where emitMemory() left the host address. A load
//into r0 does nothing once it's past the checks (which can still raise an exception).
auto Allegrex::Recompiler::emitLoadStore(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool {
  if(!self.pages) return false;
  //The VFPU's loads and stores keep two bits of their register number in the offset's low bits.
  s16 offset = s16(instruction);
  s16 vfpuOffset = s16(instruction & 0xfffc);
  auto memory = [&](bool store, u32 alignment, s16 displacement, const std::function<void ()>& access) {
    emitMemory(address, instruction, count, delaySlot, store, alignment, displacement, access);
    return true;
  };

  switch(instruction >> 26) {

  //LB Rt,Rs,i16
  case 0x20: return memory(false, 1, offset, [&] {
    if(!RTn) return;
    mov32_s8(reg(0), mem(reg(1), 0));
    mov32(Rt, reg(0));
  });

  //LH Rt,Rs,i16
  case 0x21: return memory(false, 2, offset, [&] {
    if(!RTn) return;
    mov32_s16(reg(0), mem(reg(1), 0));
    mov32(Rt, reg(0));
  });

  //LW Rt,Rs,i16
  case 0x23: return memory(false, 4, offset, [&] {
    if(!RTn) return;
    mov32(reg(0), mem(reg(1), 0));
    mov32(Rt, reg(0));
  });

  //LBU Rt,Rs,i16
  case 0x24: return memory(false, 1, offset, [&] {
    if(!RTn) return;
    mov32_u8(reg(0), mem(reg(1), 0));
    mov32(Rt, reg(0));
  });

  //LHU Rt,Rs,i16
  case 0x25: return memory(false, 2, offset, [&] {
    if(!RTn) return;
    mov32_u16(reg(0), mem(reg(1), 0));
    mov32(Rt, reg(0));
  });

  //SB Rt,Rs,i16
  case 0x28: return memory(true, 1, offset, [&] {
    mov32(reg(0), Rt);
    mov32_u8(mem(reg(1), 0), reg(0));
  });

  //SH Rt,Rs,i16
  case 0x29: return memory(true, 2, offset, [&] {
    mov32(reg(0), Rt);
    mov32_u16(mem(reg(1), 0), reg(0));
  });

  //SW Rt,Rs,i16
  case 0x2b: return memory(true, 4, offset, [&] {
    mov32(reg(0), Rt);
    mov32(mem(reg(1), 0), reg(0));
  });

  //LWC1 Ft,Rs,i16
  case 0x31: return memory(false, 4, offset, [&] {
    mov32(reg(0), mem(reg(1), 0));
    mov32(field(&self.fpu.r[FT]), reg(0));
  });

  //LV.S Vt,Rs,i16
  case 0x32: return memory(false, 4, vfpuOffset, [&] {
    mov32(reg(0), mem(reg(1), 0));
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
    mov32(reg(0), field(&self.fpu.r[FT]));
    mov32(mem(reg(1), 0), reg(0));
  });

  //SV.S Vt,Rs,i16
  case 0x3a: return memory(true, 4, vfpuOffset, [&] {
    mov32(reg(0), field(&self.vfpu.r[VTS]));
    mov32(mem(reg(1), 0), reg(0));
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
#undef FT
#undef VTS
#undef VTQ
