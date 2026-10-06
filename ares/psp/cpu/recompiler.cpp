//The recompiler's dispatcher, its cache of compiled blocks, and the block compiler.
//
//How a block runs: the compiled code is a function that takes the CPU (in sljit's saved register S0) and works on
//the same registers the interpreter does (ipu.r[], hi, lo), so either can carry on where the other stopped. When
//a block leaves, ipu.pc and ipu.pd say where the CPU goes next, exactly as if the interpreter had run those
//instructions, and `executed` says how many ran.
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
  for(u32 index = 0; index < SectionCount; index++) writable(index);
}

//Memory changed at address, so code compiled from there may be stale: its whole section goes, and is compiled
//again when it next runs. Whoever writes memory that can hold code calls this: the PSP's memory map for every
//write, the CPU's and also the ones it doesn't make (DMA, or the HLE kernel loading a module).
auto Allegrex::Recompiler::invalidate(u32 address) -> void {
  if(sections.empty()) return;
  u32 index = (address & 0x1fff'ffff) / SectionSize;
  sections[index].reset();
  writable(index);  //no code there now: stores may go straight to it again
}

auto Allegrex::Recompiler::invalidateRange(u32 address, u32 size) -> void {
  if(sections.empty() || !size) return;
  u32 first = (address & 0x1fff'ffff) / SectionSize;
  u32 last = ((address + size - 1) & 0x1fff'ffff) / SectionSize;
  for(u32 index = first;; index = (index + 1) % SectionCount) {
    sections[index].reset();
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
  if(!self.pages) return;
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
  ((void (*)(Allegrex*))code)(&self);
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
  }
  auto& code = section->blocks[address % SectionSize / 4];
  if(!code) {
    code = emit(address);
    writePages[index] = nullptr;  //this page holds compiled code now, so stores to it must go through write()
  }
  return code;
}

//Compiles the block starting at address: instructions up to a branch and its delay slot, or up to the end of the
//section, or up to one after which compiled code mustn't go on by itself (endsBlock()).
auto Allegrex::Recompiler::emit(u32 address) -> u8* {
  beginFunction(1);
  u32 count = 0;          //instructions in the block so far
  bool pcStored = false;  //whether ipu.pc and ipu.pd already say where to go after the last instruction

  while(true) {
    u32 instruction = self.read(Word, address);
    count++;
    bool lastInSection = address % SectionSize == SectionSize - 4;

    if(isBranch(instruction)) {
      //A native branch sets pc and pd for its delay slot, which the block then includes. A branch in the last word
      //of a section would need its delay slot from the next one, so the interpreter runs it instead; if it's
      //taken, pd won't be pc + 4 afterwards, and run() has the interpreter take the delay slot as well.
      if(!lastInSection && emitBranch(address, instruction, count)) {
        address += 4;
        count++;
        u32 delaySlot = self.read(Word, address);
        //(A branch in a delay slot has no defined meaning on MIPS; the interpreter's is used.)
        if(isBranch(delaySlot) || !emitInstruction(address, delaySlot, count, true)) {
          emitInterpreter(address, delaySlot, count, true);
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
      if(endsBlock(instruction)) break;
    }

    if(lastInSection) {
      if(!pcStored) {
        mov32(field(&self.ipu.pc), imm(address + 4));
        mov32(field(&self.ipu.pd), imm(address + 8));
      }
      break;
    }
    address += 4;
  }

  mov32(field(&executed), imm(count));
  jumpEpilog();

  memory::jitprotect(false);
  auto code = endFunction();
  memory::jitprotect(true);
  return code;
}

//Compiles one instruction as a call to execute(). Outside a delay slot, pc and pd are set first, as the interpreter
//has them while that instruction runs; in a delay slot, the branch before it has already set them. If the
//instruction raises an exception, the block leaves right after it.
auto Allegrex::Recompiler::emitInterpreter(u32 address, u32 instruction, u32 count, bool delaySlot) -> void {
  if(!delaySlot) {
    mov32(field(&self.ipu.pc), imm(address + 4));
    mov32(field(&self.ipu.pd), imm(address + 8));
  }
  mov32(field(&executed), imm(count));
  callf(&Allegrex::execute, imm(address), imm(instruction));
  testJumpEpilog();
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
