//The recompiler's dispatcher, its cache of compiled blocks, and the block compiler.
//
//How a block runs: the compiled code is a function that takes the CPU (in sljit's saved register S0) and works on
//the same registers the interpreter does (ipu.r[], hi, lo), so either can carry on where the other stopped. When
//a block leaves, ipu.pc and ipu.pd say where the CPU goes next, exactly as if the interpreter had run those
//instructions, and `executed` says how many ran. A block that ends as blocks usually do goes on to the next
//block by itself, doing what Allegrex::run() and run() would have done in between, so that every block starts and
//ends where it always did (emitChain()).
//
//An instruction with a native version (recompiler-ipu.cpp) is translated directly. Every other one is compiled as
//a call to execute(), the interpreter's own path; that includes every instruction that can raise an exception, so
//exceptions happen exactly as they do in the interpreter, and the block leaves right after one.
//
//Most of this is plain MIPS, and would serve any MIPS CPU without a TLB (the PS1's R3000A, for one): the cache by
//physical address, the delay slots, the calls into the interpreter. Only the native instructions are Allegrex's.

//Throws away every compiled block and starts the code memory afresh.
auto Allegrex::Recompiler::reset() -> void {
  sections.clear();
  writePages.clear();
  sectionTable.clear();
  table = nullptr;
  if(!enabled) return;
  if(!allocator) allocator.resize(codeMemory, bump_allocator::executable);
  if(!allocator) {
    //No memory that code may run from (a system that forbids it): the interpreter runs everything instead.
    enabled = false;
    return;
  }
  allocator.release();
  sections.resize(SectionCount);
  writePages.resize(SectionCount);
  sectionTable.assign(SectionCount, nullptr);
  table = sectionTable.data();
  for(u32 index = 0; index < SectionCount; index++) writable(index);
}

//Memory changed at address, so code compiled from there may be stale: its whole section goes, and is compiled
//again when it next runs. Whoever writes memory that can hold code calls this: the PSP's memory map for every
//write, the CPU's and also the ones it doesn't make (DMA, or the HLE kernel loading a module). (A block still
//running from there runs on to its end, as compiled; no block goes on to one from there after that.)
auto Allegrex::Recompiler::invalidate(u32 address) -> void {
  if(sections.empty()) return;
  u32 index = (address & 0x1fff'ffff) / SectionSize;
  sections[index].reset();
  sectionTable[index] = nullptr;
  writable(index);  //no code there now: stores may go straight to it again
}

auto Allegrex::Recompiler::invalidateRange(u32 address, u32 size) -> void {
  if(sections.empty() || !size) return;
  u32 first = (address & 0x1fff'ffff) / SectionSize;
  u32 last = ((address + size - 1) & 0x1fff'ffff) / SectionSize;
  for(u32 index = first;; index = (index + 1) % SectionCount) {
    sections[index].reset();
    sectionTable[index] = nullptr;
    writable(index);
    if(index == last) break;
  }
}

//The owner watches page now (Allegrex::watched): compiled stores to it must go through write() from here on.
auto Allegrex::Recompiler::protect(u32 page) -> void {
  if(page < writePages.size()) writePages[page] = nullptr;
}

//Compiled stores may go straight to page index again, unless it holds compiled code (block() takes it out again as
//it compiles there) or the owner watches it.
auto Allegrex::Recompiler::writable(u32 index) -> void {
  if(!self.pages || index >= writePages.size()) return;
  writePages[index] = self.watched && self.watched[index] ? nullptr : self.pages[index];
}

//Runs one block, or one instruction in the interpreter where no block can start; returns how many instructions
//ran.
auto Allegrex::Recompiler::run() -> u32 {
  //Blocks start with nothing pending: not between a branch and its delay slot (there, pd isn't pc + 4), and not at
  //a misaligned address (whose fetch raises an exception). The interpreter takes those steps.
  auto& ipu = self.ipu;
  if(ipu.pd != ipu.pc + 4 || ipu.pc & 3) {
    self.instruction();
    return 1;
  }
  //Memory the page table leaves out is the owner's to handle (hardware registers, or another address for memory
  //listed once elsewhere, as VRAM's copies are), so code there is interpreted: nothing compiled from it can go
  //stale through a store the recompiler doesn't see.
  if(self.pages && !self.pages[(ipu.pc & 0x1fff'ffff) / SectionSize]) {
    self.instruction();
    return 1;
  }
  if(sections.empty()) reset();
  if(!enabled) {
    self.instruction();
    return 1;
  }
  auto code = block(ipu.pc);
  inBlock = true;
  ((void (*)(Allegrex*))code)(&self);
  inBlock = false;
  return executed;
}

//Finds the compiled block starting at address, compiling it first if there isn't one yet.
auto Allegrex::Recompiler::block(u32 address) -> u8* {
  //Out of room for new code: start over. (Done before anything below holds on to a section.)
  if(allocator.available() < 1_MiB) reset();

  u32 index = (address & 0x1fff'ffff) / SectionSize;
  auto& section = sections[index];
  //Code compiled through another mirror of this memory knows the wrong addresses (the return addresses it stores,
  //where its jumps go), so that section starts afresh. Games run their code through one mirror, so this doesn't
  //happen back and forth.
  if(!section || section->mirror != address >> 29) {
    section = std::make_unique<Section>();
    section->mirror = address >> 29;
    sectionTable[index] = section.get();
  }
  u32 word = address % SectionSize / 4;
  auto& code = section->blocks[word];
  if(!code) {
    code = emit(address, section->bodies[word]);
    writePages[index] = nullptr;  //this page holds compiled code now, so stores to it must go through write()
  }
  return code;
}

//Compiles the block starting at address: instructions up to a branch and its delay slot, or up to the end of the
//section, or up to one after which compiled code mustn't go on by itself (endsBlock()). Returns the function run()
//calls, and in body where its own instructions begin, past the prologue, for other blocks to go on to.
auto Allegrex::Recompiler::emit(u32 address, u8*& body) -> u8* {
  //Every block has the same prologue (two float registers, for the FPU's native arithmetic: recompiler-fpu.cpp),
  //so the stack frame one block's prologue made serves any other's body, and its epilogue.
  beginFunction(1, 3 + Held, 4, 2);
  //The page tables loads and stores look pages up in (recompiler-memory.cpp), in saved registers S1 and S2: set
  //here, before the body, and kept by the blocks that go on to each other, which all set them alike.
  mov64(sreg(1), imm((sljit_sw)self.pages));
  mov64(sreg(2), imm((sljit_sw)writePages.data()));
  auto bodyLabel = sljit_emit_label(compiler);
  calls = false;
  prefixesAtRest = false;  //(not known as a block starts: recompiler-vfpu.cpp)
  forgetAll();  //(nor is anything held: a block before it may have left anything in those registers)
  u32 count = 0;          //instructions in the block so far
  bool pcStored = false;  //whether ipu.pc and ipu.pd already say where to go after the last instruction
  bool ended = false;     //it ends at an instruction after which run() and the HLE kernel take over (endsBlock())
  //Where it goes next, when that's known as it's compiled: one place (next), or either of two (next or other) as a
  //branch decides; or the block has ended itself (done).
  enum class Next : u32 { Unknown, Known, Either, Done } going = Next::Unknown;
  u32 next = 0, other = 0;

  while(true) {
    u32 instruction = self.read(Word, address);
    count++;
    bool lastInSection = address % SectionSize == SectionSize - 4;

    if(isBranch(instruction)) {
      //A native branch sets pc and pd for its delay slot, which the block then includes. A branch in the last word
      //of a section would need its delay slot from the next one, so the interpreter runs it instead; if it's
      //taken, pd won't be pc + 4 afterwards, and run() has the interpreter take the delay slot as well. The
      //coprocessors' branches end the block where the interpreter's did, before their delay slots, likewise.
      if(instruction >> 26 == 0x11 || instruction >> 26 == 0x12) {
        emitCoprocessorBranch(address, instruction, count);
        going = Next::Done;
      } else if(!lastInSection && emitBranch(address, instruction, count)) {
        wrote();
        u32 branch = address;
        address += 4;
        count++;
        u32 delaySlot = self.read(Word, address);
        //(A branch in a delay slot has no defined meaning on MIPS; the interpreter's is used.)
        if(isBranch(delaySlot) || !emitInstruction(address, delaySlot, count, true)) {
          emitInterpreter(address, delaySlot, count, true);
        } else if(instruction >> 26 == 0x02 || instruction >> 26 == 0x03) {
          //(a native delay slot leaves pc and pd as the branch set them: an interpreted one may not, a syscall
          //switching threads say)
          going = Next::Known, next = (address & 0xf000'0000) | (instruction & 0x03ff'ffff) << 2;  //j, jal
        } else if(instruction >> 26 != 0x00) {  //(jr and jalr go where a register says)
          //a likely branch not taken has left already (emitBranchOutcome()): one past its delay slot is taken
          u32 op = instruction >> 26;
          bool likely = (op >= 0x14 && op <= 0x17) || (op == 0x01 && instruction >> 17 & 1);
          going = likely ? Next::Known : Next::Either;
          next = branch + 4 + s16(instruction) * 4, other = branch + 8;
        }
      } else {
        emitInterpreter(address, instruction, count, false);
      }
      break;
    }

    if(emitInstruction(address, instruction, count, false)) {
      pcStored = false;
    } else {
      emitInterpreter(address, instruction, count, false);
      pcStored = true;
      if(endsBlock(instruction)) {
        ended = true;
        break;
      }
    }
    prefixesAfter(instruction);

    if(lastInSection) {
      if(!pcStored) {
        mov32(field(&self.ipu.pc), imm(address + 4));
        mov32(field(&self.ipu.pd), imm(address + 8));
      }
      going = Next::Known, next = address + 4;
      break;
    }
    address += 4;
  }

  if(ended || (!chains && going != Next::Done)) {
    mov32(field(&executed), imm(count));
    jumpEpilog();
  } else if(going == Next::Known) {
    emitChainTo(count, next);
  } else if(going == Next::Either) {
    auto taken = cmp32_jump(field(&self.ipu.pc), imm(next), flag_eq);
    emitChainTo(count, other);
    setLabel(taken);
    emitChainTo(count, next);
  } else if(going == Next::Unknown) {
    emitChain(count);
  }

  //(endFunction() as nall has it, but for the body's address, which is only known in between)
  memory::jitprotect(false);
  auto code = (u8*)sljit_generate_code(compiler, 0, &allocator);
  body = (u8*)sljit_get_label_addr(bodyLabel);
  allocator.reserve(sljit_get_generated_code_size(compiler));
  resetCompiler();
  memory::jitprotect(true);
  return code;
}

//The end of a block of count instructions, which goes on to the next block itself instead of leaving for run():
//it does what Allegrex::run(), run() and block() would do next, the same checks in the same state, and leaves for
//them wherever they would do anything but run the next block that's compiled already. So:
//  - Allegrex::run()'s round: the block's instructions are counted in instructionsRun, and the run stops when that
//    reaches its limit (which a syscall may have brought forward) or the CPU has halted;
//  - run()'s checks: no block starts between a branch and its delay slot (pd isn't pc + 4), at a misaligned pc, or
//    on a page the page table leaves out (which the CPU's owner may change as the CPU runs: VRAM's pages, while
//    the GE's workers draw there), so those leave for the interpreter's step, as before;
//  - block()'s lookup, by the section's table entry (none once its code is dropped: invalidate()), the mirror the
//    section was compiled for, and the block at pc's word; a block not compiled yet leaves to be compiled.
//Every block thus starts, and ends, where it always did, and the run stops after exactly the same instructions:
//where a run stops decides when interrupts and the kernel's events land. What's left undone is only what doesn't
//change what the program does: run() going round its loop, and block() starting the code memory afresh once it's
//nearly full (which waits for the next time it compiles). Leaving after having counted the block, it says it ran
//none more (executed 0).
auto Allegrex::Recompiler::emitChain(u32 count) -> void {
  std::vector<sljit_jump*> leave;
  auto unless = [&](sljit_jump* jump) { leave.push_back(jump); };
  mov64(reg(0), field(&self.instructionsRun));
  add64(reg(0), reg(0), imm(count));
  mov64(field(&self.instructionsRun), reg(0));
  auto limit = field(&self.runLimit);
  unless(sljit_emit_cmp(compiler, SLJIT_GREATER_EQUAL, SLJIT_R0, 0, limit.fst, limit.snd));
  mov32_u8(reg(0), field(&self.scc.halted));
  unless(cmp32_jump(reg(0), imm(0), flag_ne));

  mov32(reg(0), field(&self.ipu.pc));
  mov32(reg(1), field(&self.ipu.pd));
  add32(reg(2), reg(0), imm(4));
  unless(cmp32_jump(reg(1), reg(2), flag_ne));
  test32(reg(0), imm(3), set_z);
  unless(jump(flag_nz));
  and32(reg(1), reg(0), imm(0x1fff'ffff));
  lshr32(reg(1), reg(1), imm(12));
  mov64_u32(reg(1), reg(1));  //reg(1): the page, and the section
  if(self.pages) {
    mov64(reg(2), mem(SLJIT_MEM2(SLJIT_S1, SLJIT_R1), 3));  //(S1: the page table, emit())
    unless(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0));
  }

  mov64(reg(2), field(&table));
  mov64(reg(2), mem(SLJIT_MEM2(SLJIT_R2, SLJIT_R1), 3));
  unless(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0));
  mov32(reg(3), mem(reg(2), offsetof(Section, mirror)));
  lshr32(reg(1), reg(0), imm(29));
  unless(cmp32_jump(reg(3), reg(1), flag_ne));
  lshr32(reg(0), reg(0), imm(2));
  and32(reg(0), reg(0), imm(SectionWords - 1));
  mov64_u32(reg(0), reg(0));
  add64(reg(2), reg(2), imm(offsetof(Section, bodies)));
  mov64(reg(2), mem(SLJIT_MEM2(SLJIT_R2, SLJIT_R0), 3));
  unless(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R2, 0, SLJIT_IMM, 0));
  sljit_emit_ijump(compiler, SLJIT_JUMP, SLJIT_R2, 0);

  auto here = sljit_emit_label(compiler);
  for(auto jump : leave) sljit_set_label(jump, here);
  mov32(field(&executed), imm(0));
  jumpEpilog();
}

//emitChain() for a block that goes on to target, which the compiled code knows already (pc is target and pd the
//word after it, as the branch or the end of the section set them): its section and word are worked out here, and
//of run()'s checks only the page's is made as it runs, the CPU's owner being free to change the table. Whether the
//CPU halted is only looked at if the block called the interpreter, the only way it can halt (an exception leaves
//before this; a syscall, halt, break or eret ends its block for run()).
auto Allegrex::Recompiler::emitChainTo(u32 count, u32 target) -> void {
  std::vector<sljit_jump*> leave;
  auto unless = [&](sljit_jump* jump) { leave.push_back(jump); };
  mov64(reg(0), field(&self.instructionsRun));
  add64(reg(0), reg(0), imm(count));
  mov64(field(&self.instructionsRun), reg(0));
  auto limit = field(&self.runLimit);
  unless(sljit_emit_cmp(compiler, SLJIT_GREATER_EQUAL, SLJIT_R0, 0, limit.fst, limit.snd));
  if(calls) {
    mov32_u8(reg(0), field(&self.scc.halted));
    unless(cmp32_jump(reg(0), imm(0), flag_ne));
  }
  u32 index = (target & 0x1fff'ffff) / SectionSize;
  if(self.pages) {
    unless(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_MEM1(SLJIT_S1), index * sizeof(u8*), SLJIT_IMM, 0));
  }
  mov64(reg(1), field(&table));
  mov64(reg(1), mem(reg(1), index * sizeof(Section*)));
  unless(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0));
  unless(cmp32_jump(mem(reg(1), offsetof(Section, mirror)), imm(target >> 29), flag_ne));
  mov64(reg(1), mem(reg(1), offsetof(Section, bodies) + target % SectionSize / 4 * sizeof(u8*)));
  unless(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0));
  sljit_emit_ijump(compiler, SLJIT_JUMP, SLJIT_R1, 0);

  auto here = sljit_emit_label(compiler);
  for(auto jump : leave) sljit_set_label(jump, here);
  mov32(field(&executed), imm(0));
  jumpEpilog();
}

//Compiles one instruction as a call to execute(). Outside a delay slot, pc and pd are set first, as the interpreter
//has them while that instruction runs; in a delay slot, the branch before it has already set them. If the
//instruction raises an exception, the block leaves right after it.
auto Allegrex::Recompiler::emitInterpreter(u32 address, u32 instruction, u32 count, bool delaySlot) -> void {
  calls = true;
  if(!delaySlot) {
    mov32(field(&self.ipu.pc), imm(address + 4));
    mov32(field(&self.ipu.pd), imm(address + 8));
  }
  mov32(field(&executed), imm(count));
  callf(&Allegrex::execute, imm(address), imm(instruction));
  testJumpEpilog();
  forgetAll();  //(it may have written any register)
}

//emitInterpreter() for a native instruction's side path (a load's page without an entry, an overflow), where the
//interpreter runs the same instruction: it writes only that instruction's results, so what the host registers
//hold stays true but for those, which are read back from what it wrote (they're stored again as the native
//instruction's would be: the same value).
auto Allegrex::Recompiler::emitFallback(u32 address, u32 instruction, u32 count, bool delaySlot) -> void {
  auto kept = holding;
  emitInterpreter(address, instruction, count, delaySlot);
  holding = kept;
  for(u32 k = 0; k < Held; k++) {
    if(holding.pending[k] >= 0) mov32(sreg(3 + k), gpr(holding.pending[k]));
  }
}

//The game's registers in host registers: within a block, a register's value once read stays in a host register,
//so the instructions after read it from there instead of from ipu.r[] again; and a result is worked out in one,
//to be read from there too. They're only copies: every result is still stored to ipu.r[] as its instruction ends
//(wrote()), so ipu.r[] is always as the interpreter would have it, wherever the block leaves, calls the interpreter
//or goes on to another block, and nothing has to be put back. What the copies can miss is a register written some
//other way, so those are forgotten: by the interpreter (emitInterpreter()), by a coprocessor's move into one
//(emitInstruction()), by a branch's link (emitBranch()); and a block starts holding none (emit()).
//
//A host register for a new value: one holding nothing, else the least recently used, but none the instruction being
//compiled uses already (an instruction uses three at most, and there are at least three).
auto Allegrex::Recompiler::take() -> u32 {
  s32 best = -1;
  for(u32 k = 0; k < Held; k++) {
    if(holding.pinned >> k & 1) continue;
    if(holding.held[k] < 0) { best = k; break; }
    if(best < 0 || holding.used[k] < holding.used[best]) best = k;
  }
  assert(best >= 0);
  if(holding.held[best] >= 0) holding.slot[holding.held[best]] = -1, holding.held[best] = -1;
  holding.pinned |= 1 << best;
  holding.used[best] = ++holding.clock;
  return best;
}

//Register index's value, to be read: the host register holding it, loaded there first if none does yet (r0, always
//zero, is read from ipu.r[] as before). Its loading comes before whatever the instruction does with it.
auto Allegrex::Recompiler::use(u32 index) -> op_base {
  if(index == 0) return gpr(0);
  s32 k = holding.slot[index];
  if(k < 0) {
    k = take();
    mov32(sreg(3 + k), gpr(index));
    holding.slot[index] = k, holding.held[k] = index;
  }
  holding.pinned |= 1 << k;
  holding.used[k] = ++holding.clock;
  return sreg(3 + k);
}

//Where register index's new value goes (never r0's: an instruction writing only r0 compiles to nothing), stored by
//wrote(). Until then the old value is the register's, in ipu.r[] and for use() (which loads it afresh if its host
//register is now this one).
auto Allegrex::Recompiler::def(u32 index) -> op_base {
  s32 k = holding.slot[index];
  if(k >= 0 && !(holding.pinned >> k & 1)) {
    holding.slot[index] = -1, holding.held[k] = -1;
    holding.pinned |= 1 << k;
    holding.used[k] = ++holding.clock;
  } else {
    k = take();
  }
  holding.pending[k] = index;
  return sreg(3 + k);
}

//The instruction is done: its results are stored to ipu.r[], and their host registers hold them from now on.
auto Allegrex::Recompiler::wrote() -> void {
  for(u32 k = 0; k < Held; k++) {
    s32 index = holding.pending[k];
    if(index < 0) continue;
    mov32(gpr(index), sreg(3 + k));
    if(holding.slot[index] >= 0) holding.held[holding.slot[index]] = -1;
    holding.slot[index] = k, holding.held[k] = index;
    holding.pending[k] = -1;
  }
  holding.pinned = 0;
}

auto Allegrex::Recompiler::forget(u32 index) -> void {
  s32 k = holding.slot[index];
  if(k >= 0) holding.slot[index] = -1, holding.held[k] = -1;
}

auto Allegrex::Recompiler::forgetAll() -> void {
  for(auto& k : holding.slot) k = -1;
  for(u32 k = 0; k < Held; k++) holding.held[k] = -1, holding.pending[k] = -1, holding.used[k] = 0;
  holding.pinned = 0;
}

//Whether an instruction is a branch or jump: it has a delay slot, and changes where the CPU goes after that.
auto Allegrex::Recompiler::isBranch(u32 instruction) const -> bool {
  switch(instruction >> 26) {
  case 0x00: return (instruction & 0x3e) == 0x08;          //jr, jalr
  case 0x01: return (instruction >> 16 & 0x0c) == 0;       //bltz, bgez, bltzal, bgezal and their likely forms
  case 0x02: case 0x03: return true;                       //j, jal
  case 0x04: case 0x05: case 0x06: case 0x07: return true;  //beq, bne, blez, bgtz
  case 0x11: case 0x12: return (instruction >> 21 & 31) == 0x08;  //bc1f, bc1t; the VFPU's bvf, bvt (and likely)
  case 0x14: case 0x15: case 0x16: case 0x17: return true;  //beql, bnel, blezl, bgtzl
  }
  return false;
}

//Instructions after which compiled code mustn't go on by itself, though they aren't branches.
auto Allegrex::Recompiler::endsBlock(u32 instruction) const -> bool {
  if(instruction >> 26 == 0x00) {
    u32 funct = instruction & 0x3f;
    return funct == 0x0c || funct == 0x0d;  //syscall (the HLE kernel may switch to another thread), break
  }
  if(instruction == 0x7000'0000) return true;  //halt: the CPU stops
  if(instruction >> 21 == 0x210 && (instruction & 0x3f) == 0x18) return true;  //eret: goes on at EPC
  return false;
}
