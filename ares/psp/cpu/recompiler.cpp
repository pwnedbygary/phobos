//The recompiler's dispatcher, its cache of compiled blocks, and the block compiler.
//
//How a block runs: the compiled code is a function that takes the CPU (in sljit's saved register S0) and works on
//the same registers the interpreter does (ipu.r[], hi, lo), so either can carry on where the other stopped. When
//a block leaves, ipu.pc and ipu.pd say where the CPU goes next, exactly as if the interpreter had run those
//instructions, and `executed` says how many ran. (Within a block they're stored only where something reads them:
//where it leaves, and before a call into the interpreter.) A block that ends as blocks usually do goes on to the next
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
  exits.clear(), into.clear(), waiting.clear(), outOf.clear();
  sections.clear();
  writePages.clear();
  sectionTable.clear();
  compiled.clear();
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
  compiled.assign(SectionCount / 64, 0);
  table = sectionTable.data();
  for(u32 index = 0; index < SectionCount; index++) writable(index);
  //(the direct region: the largest block of steady pages one after another in the host's memory as in the PSP's, its
  //size a power of two and its first page a multiple of it, from each run of such pages)
  direct = {};
  if(!self.pages || !self.steady) return;
  for(u32 index = 0; index < SectionCount;) {
    u32 end = index + 1;
    if(self.steady[index] && self.pages[index]) {
      while(end < SectionCount && self.steady[end] && self.pages[end] == self.pages[index] + (end - index) * SectionSize) {
        end++;
      }
      for(u32 size = SectionCount; size * SectionSize > direct.size; size >>= 1) {
        u32 first = (index + size - 1) / size * size;
        if(first + size <= end) {
          direct = {first * SectionSize, size * SectionSize};
          break;
        }
      }
    }
    index = end;
  }
}

//What the S1 register holds as blocks run (emit()): the direct region's memory less its first address, where there is
//one, else the page table.
auto Allegrex::Recompiler::directBase() const -> sljit_sw {
  if(!direct.size) return (sljit_sw)self.pages;
  return sljit_sw((uintptr_t)self.pages[direct.first / SectionSize] - direct.first);
}

//The page table in r: S1 itself where there's no direct region, else loaded.
auto Allegrex::Recompiler::pageTable(reg r) -> sljit_s32 {
  if(!direct.size) return SLJIT_S1;
  mov64(r, field(&self.pages));
  return r.fst;
}

//Memory changed at address, so code compiled from there may be stale: its whole section goes, and is compiled
//again when it next runs. Whoever writes memory that can hold code calls this: the PSP's memory map for every
//write, the CPU's and also the ones it doesn't make (DMA, or the HLE kernel loading a module). (A block still
//running from there runs on to its end, as compiled; no block goes on to one from there after that.)
auto Allegrex::Recompiler::invalidate(u32 address) -> void {
  if(sections.empty()) return;
  drop((address & 0x1fff'ffff) / SectionSize);
}

//invalidate() for every section size bytes from address reach. Only those there are need anything (compiled: memory
//reports every change to VRAM for all four of its copies, three of which the table never lists, and a hardware
//renderer's every primitive changes VRAM's pages); a page without one has its store table entry as writable() makes
//it already, having been told of each change to what that looks at (the page table's entry: vramGuard; its watch:
//protect(), unwatched()). Without a page table, every one goes.
auto Allegrex::Recompiler::invalidateRange(u32 address, u32 size) -> void {
  if(sections.empty() || !size) return;
  u32 first = (address & 0x1fff'ffff) / SectionSize;
  u32 last = ((address + size - 1) & 0x1fff'ffff) / SectionSize;
  if(!self.pages) {
    for(u32 index = first;; index = (index + 1) % SectionCount) {
      drop(index);
      if(index == last) break;
    }
    return;
  }
  //(the range, round the end of memory's 512 MiB to its start if it goes past: one or two runs of sections)
  auto run = [&](u32 from, u32 to) {
    for(u32 word = from / 64; word <= to / 64; word++) {
      u64 bits = compiled[word];
      if(word == from / 64) bits &= ~0ull << from % 64;
      if(word == to / 64 && to % 64 != 63) bits &= (2ull << to % 64) - 1;
      while(bits) {
        u32 index = word * 64 + std::countr_zero(bits);
        bits &= bits - 1;
        drop(index);
      }
    }
  };
  if(first <= last) return run(first, last);
  run(first, SectionCount - 1), run(0, last);
}

//The section's code goes: no block starts there now, nor goes on to one, and stores may go straight to it again. Its
//own blocks' exits are dead code from now on; every exit linked to its blocks goes back to its lookup, even one in
//dead code (a block still running from a section that went runs on to its end), and the live ones wait for those
//blocks to be compiled again.
auto Allegrex::Recompiler::drop(u32 index) -> void {
  if(auto own = outOf.find(index); own != outOf.end()) {
    for(u32 exit : own->second) exits[exit].live = false;
    outOf.erase(own);
  }
  if(auto linked = into.find(index); linked != into.end()) {
    for(u32 exit : linked->second) {
      patch(exits[exit].jump, exits[exit].lookup);
      if(exits[exit].live) waiting[exits[exit].target].push_back(exit);
    }
    into.erase(linked);
  }
  sections[index].reset();
  sectionTable[index] = nullptr;
  compiled[index / 64] &= ~(1ull << index % 64);
  writable(index);
}

//The owner watches page now (Allegrex::watched): compiled stores to it must go through write() from here on.
auto Allegrex::Recompiler::protect(u32 page) -> void {
  if(page < writePages.size()) writePages[page] = nullptr;
}

//The owner watches page no more: compiled stores may go straight to it again (writable()).
auto Allegrex::Recompiler::unwatched(u32 page) -> void {
  writable(page);
}

//Compiled stores may go straight to page index again, unless it holds compiled code (block() takes it out as it
//compiles there) or the owner watches it.
auto Allegrex::Recompiler::writable(u32 index) -> void {
  if(!self.pages || index >= writePages.size() || index >= sections.size()) return;
  bool kept = (self.watched && self.watched[index]) || sections[index];
  writePages[index] = kept ? nullptr : self.pages[index];
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
  if(section && section->mirror != address >> 29) drop(index);
  if(!section) {
    section = std::make_unique<Section>();
    section->mirror = address >> 29;
    sectionTable[index] = section.get();
    compiled[index / 64] |= 1ull << index % 64;
  }
  u32 word = address % SectionSize / 4;
  auto& code = section->blocks[word];
  if(!code) {
    code = emit(address, section->bodies[word]);
    writePages[index] = nullptr;  //this page holds compiled code now, so stores to it must go through write()
    //(the exits waiting for this block go straight to it now)
    if(auto waiters = waiting.find(address); waiters != waiting.end()) {
      auto list = std::move(waiters->second);
      waiting.erase(waiters);
      for(u32 exit : list) if(exits[exit].live) link(exit, section->bodies[word]);
    }
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
  //What loads and stores reach memory through (recompiler-memory.cpp), in saved registers S1 and S2: set here, before
  //the body, and kept by the blocks that go on to each other, which all set them alike.
  mov64(sreg(1), imm(directBase()));
  mov64(sreg(2), imm((sljit_sw)writePages.data()));
  auto bodyLabel = sljit_emit_label(compiler);
  u32 index = (address & 0x1fff'ffff) / SectionSize;  //(a block never leaves its section)
  calls = false;
  cold.clear();
  linking.clear();
  destination = {};
  prefixes = {};  //(not known as a block starts: recompiler-vfpu.cpp)
  forgetAll();  //(nor is anything held: a block before it may have left anything in those registers)
  u32 count = 0;       //instructions in the block so far
  bool ended = false;  //it ends at an instruction after which run() and the HLE kernel take over (endsBlock())
  //Where it goes next: a place known as it's compiled (next), or where pc and pd say; or the block has ended itself.
  enum class Next : u32 { Unknown, Known, Done } going = Next::Unknown;
  u32 next = 0;

  while(true) {
    u32 instruction = self.read(Word, address);
    count++;
    bool lastInSection = address % SectionSize == SectionSize - 4;

    if(isBranch(instruction)) {
      //A native branch decides where it goes, then the block includes its delay slot, compiled along each way it can
      //go (emitDelaySlot()). A branch in the last word of a section would need its delay slot from the next one, so
      //the interpreter runs it instead; if it's taken, pd won't be pc + 4 afterwards, and run() has the interpreter
      //take the delay slot as well. The coprocessors' branches end the block at the branch, as the interpreter's did
      //(taken, the block may go on through the delay slot to the target by itself: emitCoprocessorBranch()).
      Branch branch;
      if(instruction >> 26 == 0x11 || instruction >> 26 == 0x12) {
        emitCoprocessorBranch(address, instruction, count);
        going = Next::Done;
      } else if(!lastInSection && (branch = emitBranch(address, instruction)).kind != Branch::None) {
        wrote();
        emitDelaySlot(address, branch, count);
        count++;
        going = Next::Done;
      } else {
        emitInterpreter(address, instruction, count, false);
      }
      break;
    }

    if(!emitInstruction(address, instruction, count, false)) {
      emitInterpreter(address, instruction, count, false);
      if(endsBlock(instruction)) {
        ended = true;
        break;
      }
    }
    prefixesAfter(instruction);

    if(lastInSection) {
      going = Next::Known, next = address + 4;
      break;
    }
    address += 4;
  }

  if(ended) {
    mov32(field(&executed), imm(count));
    jumpEpilog();
  } else if(going == Next::Known) {
    emitEnd(count, true, next);
  } else if(going == Next::Unknown) {
    emitEnd(count, false, 0);
  }
  for(u32 n = 0; n < cold.size(); n++) cold[n]();  //(cold code may add cold code)
  cold.clear();

  //(endFunction() as nall has it, but for the body's address, which is only known in between)
  memory::jitprotect(false);
  auto code = (u8*)sljit_generate_code(compiler, 0, &allocator);
  body = (u8*)sljit_get_label_addr(bodyLabel);
  allocator.reserve(sljit_get_generated_code_size(compiler));
  addExits(index);
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
//A syscall in a delay slot doesn't end its block (endsBlock() is for the others), so the HLE kernel has run when
//this comes after it, as it does after nearly every system call: each import stub is a jr ra with its syscall in
//the delay slot. That's exact because the kernel stops a run only by halting the CPU or bringing runLimit forward,
//both checked here; anything else Allegrex::run()'s loop came to do between blocks would need checking here too.
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
    mov64(reg(2), mem(SLJIT_MEM2(pageTable(reg(2)), SLJIT_R1), 3));
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

//The end of a block of count instructions that goes on to target, pc and pd not stored for it yet (known), or to
//wherever pc and pd say. With chains off (the tests' comparison) it leaves for run() instead.
auto Allegrex::Recompiler::emitEnd(u32 count, bool known, u32 target) -> void {
  if(!chains) {
    if(known) {
      mov32(field(&self.ipu.pc), imm(target));
      mov32(field(&self.ipu.pd), imm(target + 4));
    }
    mov32(field(&executed), imm(count));
    return jumpEpilog();
  }
  if(known) return emitChainTo(count, target);
  emitChain(count);
}

//emitChain() for a block that goes on to target, which the compiled code knows already: its section and word are worked
//out here, and of run()'s checks only the page's is made as it runs, the CPU's owner being free to change the table
//(but for a page it says never changes, listed as it is now: Allegrex::steady).
//pc and pd, target and the word after it, are stored only where it leaves for run(). Whether the CPU halted is only
//looked at if the block called the interpreter, the only way it can halt (an exception leaves before this; a syscall,
//halt, break or eret ends its block for run(), but for a syscall in a delay slot, which is such a call).
auto Allegrex::Recompiler::emitChainTo(u32 count, u32 target) -> void {
  std::vector<sljit_jump*> leave;
  auto unless = [&](sljit_jump* jump) { leave.push_back(jump); };
  emitCounted(count, leave);
  u32 index = (target & 0x1fff'ffff) / SectionSize;
  if(self.pages && !(self.steady && self.steady[index] && self.pages[index])) {
    unless(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_MEM1(pageTable(reg(1))), index * sizeof(u8*), SLJIT_IMM, 0));
  }
  emitLink(target);  //(straight to the target's body once it's compiled; until then, the lookup)
  mov64(reg(1), field(&table));
  mov64(reg(1), mem(reg(1), index * sizeof(Section*)));
  unless(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0));
  unless(cmp32_jump(mem(reg(1), offsetof(Section, mirror)), imm(target >> 29), flag_ne));
  mov64(reg(1), mem(reg(1), offsetof(Section, bodies) + target % SectionSize / 4 * sizeof(u8*)));
  unless(sljit_emit_cmp(compiler, SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0));
  sljit_emit_ijump(compiler, SLJIT_JUMP, SLJIT_R1, 0);

  auto here = sljit_emit_label(compiler);
  for(auto jump : leave) sljit_set_label(jump, here);
  mov32(field(&self.ipu.pc), imm(target));
  mov32(field(&self.ipu.pd), imm(target + 4));
  mov32(field(&executed), imm(0));
  jumpEpilog();
}

//An exit's jump (Exit): a jump of the host's own (ARM64's B, x86-64's JMP rel32) to the code right after it, which
//link() and drop() rewrite. On other hosts there's none, and the lookup always runs.
#if defined(ARCHITECTURE_ARM64)
static constexpr u32 LinkSize = 4;
#elif defined(ARCHITECTURE_AMD64)
static constexpr u32 LinkSize = 5;
#else
static constexpr u32 LinkSize = 0;
#endif
auto Allegrex::Recompiler::emitLink(u32 target) -> void {
  if(!LinkSize) return;
  linking.push_back({target, sljit_emit_label(compiler)});
  #if defined(ARCHITECTURE_ARM64)
  u32 b = 0x1400'0001;  //B .+4
  sljit_emit_op_custom(compiler, &b, sizeof(b));
  #elif defined(ARCHITECTURE_AMD64)
  u8 jmp[5] = {0xe9, 0, 0, 0, 0};  //JMP .+5
  sljit_emit_op_custom(compiler, jmp, sizeof(jmp));
  #endif
}

//The exits of the block just compiled (in section index), once its code is where it runs: each linked if its target
//is compiled already (a block going on to itself is: its body is known by now), else waiting for it (block() links it
//then).
auto Allegrex::Recompiler::addExits(u32 index) -> void {
  for(auto& [target, label] : linking) {
    u8* jump = (u8*)sljit_get_label_addr(label);
    u32 exit = exits.size();
    exits.push_back({jump, jump + LinkSize, target, true});
    outOf[index].push_back(exit);
    auto& section = sections[(target & 0x1fff'ffff) / SectionSize];
    u8* body = section && section->mirror == target >> 29 ? section->bodies[target % SectionSize / 4] : nullptr;
    if(body) link(exit, body);
    else waiting[target].push_back(exit);
  }
  linking.clear();
}

auto Allegrex::Recompiler::link(u32 exit, u8* body) -> void {
  if(patch(exits[exit].jump, body)) into[(exits[exit].target & 0x1fff'ffff) / SectionSize].push_back(exit);
}

//Rewrites the jump at jump to go to to; false if that's too far for it (it's left as it was then).
auto Allegrex::Recompiler::patch(u8* jump, u8* to) -> bool {
  #if defined(ARCHITECTURE_ARM64)
  s64 distance = (to - jump) / 4;
  if(distance < -(1 << 25) || distance >= 1 << 25) return false;
  u32 b = 0x1400'0000 | (u32(distance) & 0x3ff'ffff);
  memory::jitprotect(false);
  std::memcpy(jump, &b, sizeof(b));
  memory::jitprotect(true);
  SLJIT_CACHE_FLUSH(jump, jump + sizeof(b));
  return true;
  #elif defined(ARCHITECTURE_AMD64)
  s64 distance = to - (jump + 5);
  if(distance != s32(distance)) return false;
  s32 rel = s32(distance);
  memory::jitprotect(false);
  std::memcpy(jump + 1, &rel, sizeof(rel));
  memory::jitprotect(true);
  return true;
  #else
  return false;
  #endif
}

//Allegrex::run()'s round after count instructions, for emitChainTo() and a coprocessor branch taken
//(emitCoprocessorBranch()): they're counted in instructionsRun, and the run stops (a jump added to leave) once that
//reaches its limit or the CPU has halted, which is only looked at if the block called the interpreter.
auto Allegrex::Recompiler::emitCounted(u32 count, std::vector<sljit_jump*>& leave) -> void {
  mov64(reg(0), field(&self.instructionsRun));
  add64(reg(0), reg(0), imm(count));
  mov64(field(&self.instructionsRun), reg(0));
  auto limit = field(&self.runLimit);
  leave.push_back(sljit_emit_cmp(compiler, SLJIT_GREATER_EQUAL, SLJIT_R0, 0, limit.fst, limit.snd));
  if(calls) {
    mov32_u8(reg(0), field(&self.scc.halted));
    leave.push_back(cmp32_jump(reg(0), imm(0), flag_ne));
  }
}

//pc and pd as the interpreter has them while the instruction at address runs, for a call into it: past it, or in a
//delay slot, where the branch before it goes, stored here when the block knows that (destination: emitDelaySlot())
//and by the branch itself otherwise.
auto Allegrex::Recompiler::emitPC(u32 address, bool delaySlot) -> void {
  if(delaySlot && !destination.known) return;
  u32 pc = delaySlot ? destination.target : address + 4;
  mov32(field(&self.ipu.pc), imm(pc));
  mov32(field(&self.ipu.pd), imm(pc + 4));
}

//Compiles one instruction as a call to execute(), pc and pd first set as the interpreter has them while that
//instruction runs (emitPC()). If the instruction raises an exception, the block leaves right after it.
auto Allegrex::Recompiler::emitInterpreter(u32 address, u32 instruction, u32 count, bool delaySlot) -> void {
  calls = true;
  emitPC(address, delaySlot);
  mov32(field(&executed), imm(count));
  bool syscall = instruction >> 26 == 0x00 && (instruction & 0x3f) == 0x0c;
  callf(syscall ? &Allegrex::executeSyscall : &Allegrex::execute, imm(address), imm(instruction));
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

//Cold code: the jumps given go to code, out of the block's straight line, which then comes back here, to the end of
//the instruction's fast path. It's emitted after the block's end (emit()), holding what the block holds here and
//knowing where a delay slot goes (destination), as if it were here; so the registers the instruction reads must be
//read before the first of the jumps.
auto Allegrex::Recompiler::emitCold(const std::vector<sljit_jump*>& jumps, const std::function<void ()>& code) -> void {
  auto back = sljit_emit_label(compiler);
  std::vector<sljit_jump*> from;
  for(auto jump : jumps) if(jump) from.push_back(jump);
  cold.push_back([this, back, from, code, kept = holding, where = destination] {
    auto here = sljit_emit_label(compiler);
    for(auto jump : from) sljit_set_label(jump, here);
    holding = kept, destination = where;
    code();
    sljit_set_label(sljit_emit_jump(compiler, SLJIT_JUMP), back);
  });
}

//emitFallback() as cold code. The interpreter runs a native instruction there, which halts the CPU only by an
//exception, after which the block leaves: so it doesn't count among the block's calls (calls).
auto Allegrex::Recompiler::emitColdFallback(const std::vector<sljit_jump*>& jumps, u32 address, u32 instruction,
                                            u32 count, bool delaySlot) -> void {
  emitCold(jumps, [=, this] { emitFallback(address, instruction, count, delaySlot); });
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

//Register index's value, to be read: the host register holding it, loaded there first if none does yet (r0 is the
//constant 0: nothing compiled writes it, and the interpreter sets it to 0 after every instruction). Its loading comes
//before whatever the instruction does with it.
auto Allegrex::Recompiler::use(u32 index) -> op_base {
  if(index == 0) return imm(0);
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

//The registers an instruction reads (rs, and rt where it's a source), where host registers hold them, are kept for it
//(pinned) before anything is compiled: def() then takes another host register for a result into one of them, so
//use() still finds the value the instruction reads, whichever of the two it asks for first.
auto Allegrex::Recompiler::pinSources(u32 instruction) -> void {
  u32 op = instruction >> 26;
  bool rt = op == 0x00 || (op >= 0x04 && op <= 0x07) || (op >= 0x14 && op <= 0x17) || op == 0x1f || op == 0x22
         || op == 0x26 || (op >= 0x28 && op <= 0x2e);
  for(u32 index : {instruction >> 21 & 31, rt ? instruction >> 16 & 31 : 0}) {
    if(index && holding.slot[index] >= 0) holding.pinned |= 1 << holding.slot[index];
  }
}

//Where register index's new value goes (never r0's: an instruction writing only r0 compiles to nothing), stored by
//wrote(). Until then the old value is the register's, in ipu.r[] and for use() (which loads it afresh if its host
//register is now this one: pinSources() keeps that from happening to a value the instruction reads).
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

//Instructions after which compiled code mustn't go on by itself, though they aren't branches. (One in a delay slot
//is compiled with its branch, which goes on as emitChain() says.)
auto Allegrex::Recompiler::endsBlock(u32 instruction) const -> bool {
  if(instruction >> 26 == 0x00) {
    u32 funct = instruction & 0x3f;
    return funct == 0x0c || funct == 0x0d;  //syscall (the HLE kernel may switch to another thread), break
  }
  if(instruction == 0x7000'0000) return true;  //halt: the CPU stops
  if(instruction >> 21 == 0x210 && (instruction & 0x3f) == 0x18) return true;  //eret: goes on at EPC
  return false;
}
