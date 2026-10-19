//Spins: a thread polling system functions in a loop that changes nothing until something comes from outside it (a
//vertical blank, a timer, another thread). Both Need for Speeds' idle thread puts itself back in its line, asking its
//ID and its own status (for its priority) first, 175,000 times a frame (half a million calls) till a count another
//thread or a handler keeps moves on; the Twisted Metal, Disaster Report 3 and Armored Core intro movies ask the
//movie's ring how full it is (and signal a semaphore already at its most) in 90,000 to 370,000 calls a frame till the
//reader thread feeds it; Resistance asks the GE whether its list is done (sceGeListSync(id, 1)), with the PSP's GE
//timing some 285,000 times a frame, till it is. (Need for Speed: Shift asks sceGeDrawSync(1) some 45,000 times a
//frame, but each turn also takes a semaphore that's free, a wait that succeeds at once and so isn't calm: its loop is
//never one that can be skipped.) A system call costs the clock one cycle, so a turn of such a loop is a few dozen
//cycles of the PSP's time, and nothing else happens till the CPU's go ends at the next thing due (Kernel::run(): only
//system functions bring that end sooner, and these, quiet or calm, don't).
//
//So once a loop has come round the same way a few times, the kernel proves that its next turn leaves everything a
//later turn sees as it was, and jumps ahead by whole turns to just before the go's end, the last turns running as
//they would: registers, memory, the clock and every later result come out as running every turn makes them. The
//proof is one turn run on a CPU of its own (SpinShadow), through the interpreter, from the real registers:
//  - its loads and stores reach main RAM alone, the stores kept aside (the turn's own loads see them) and only to
//    pages whose stores do nothing more (spinStorable(): no compiled code there, none the GE watches);
//  - its system calls are worked out by the kernel without being made (Function::spin): what each returns, what it
//    writes, what it changes (rotating the caller's own line, alone in it: the ready order's count), and which of the
//    bytes it writes the time decides (a thread's run time), which the turn must never read, nor write over another
//    way;
//  - it must come back to the call it started after (the anchor: the same syscall, going on to the same place), every
//    register as it was, and every byte it stored holding what memory holds (but those the time decides).
//Then every turn does what this one did, at its own time: the CPU is deterministic (the interpreter and compiled code
//alike: tests/allegrex holds them to it), every value it reads is the same, and so is what each function returns. The
//go ends at the first block end where the run's count reaches its limit, and a turn's blocks end at the same counts
//whether or not the turns before it ran (the anchor's syscall always ends its block: endsBlock(), or the branch it's
//the delay slot of); so skipping turns whose blocks all end before the limit (and before a list's FINISH falls due,
//which a system call would take) leaves where the go ends, and so when everything after happens, as it was. Nothing
//of this is kept in a state: a spin found again after a state loads is skipped the same way, and one not found runs
//every turn, to the same end.

//The shadow: a CPU of its own running one turn of a spin through the interpreter (see above). A load or store of the
//turn's own outside main RAM, a store that would do more than store, a byte the time decides read or written over, an
//exception, a halt or a function the kernel can't work out without making it ends the proof (failed); a function's
//own stores and checks of an address are answered as Memory answers them (reaches()).
struct SpinShadow : Allegrex {
  Kernel& kernel;
  Memory& memory;
  bool failed = false;
  bool closed = false;    //the turn came back to the anchor
  u64 start = 0;          //cycles, as the turn's first instruction runs
  u32 executed = 0;       //the turn's instructions before the one running
  u32 period = 0;         //the turn's instructions, the anchor's syscall last
  u32 rotations = 0;      //the caller's own line rotated, alone in it: each one the ready order's count goes up by
  u32 timedWrites = 0;    //function writes with bytes the time decides
  //The turn's stores so far, by physical address: what each byte holds now, and whether the time decided it
  struct Byte { u8 value; bool timed; };
  std::unordered_map<u32, Byte> stored;
  struct Range { u32 address, size; };  //(physical)
  std::vector<Range> reads;
  //The bytes the time decides that the turn's calls wrote, written again for real at the last turn's time
  //(Kernel::spinSkip()): a thread's run time in its status at address, the status's bytes first to end (not that
  //one), and where in the turn the call was made
  struct Timed {
    const Kernel::Thread* thread;
    u32 address, first, end;
    u32 executed;
  };
  std::vector<Timed> timed;

  SpinShadow(Kernel& kernel, Memory& memory) : kernel(kernel), memory(memory) {
    syscallHook = [this](u32 code) { return this->kernel.spinCall(*this, code); };
    exceptionHook = [this](Exception, u32) { failed = true; };
  }

  auto time() const -> u64 { return start + executed; }
  auto arg(u32 n) const -> u32 { return n < 4 ? ipu.r[4 + n] : ipu.r[8 + n - 4]; }

  //Whether size bytes at address are all there, in main RAM. Where Memory finds them not all there, they aren't (a
  //function checking an address it was given finds that out the same way); where it finds them elsewhere, the proof
  //ends: VRAM, which the GE's workers may draw into as the CPU runs, and the scratchpad, which polling loops don't
  //touch.
  auto reaches(u32 address, u32 size) -> bool {
    u32 offset = (address & 0x1fff'ffff) - Memory::RAMBase;
    if(offset < memory.ram.size()) return size <= memory.ram.size() - offset;
    if(memory.reaches(address, size)) failed = true;
    return false;
  }

  //A byte as the turn has it (physical, in main RAM): its own store's, or memory's.
  auto byte(u32 physical) -> u8 {
    if(auto found = stored.find(physical); found != stored.end()) {
      if(found->second.timed) failed = true;
      return found->second.value;
    }
    return memory.ram[physical - Memory::RAMBase];
  }

  auto put(u32 physical, u8 value, bool timed) -> void {
    auto [held, added] = stored.try_emplace(physical, Byte{value, timed});
    if(added) return;
    if(held->second.timed && !timed) failed = true;
    held->second = {value, timed};
  }

  auto read(u32 size, u32 address) -> u32 override {
    if(failed || !reaches(address, size)) return failed = true, 0;
    u32 physical = address & 0x1fff'ffff, value = 0;
    reads.push_back({physical, size});
    for(u32 n = 0; n < size; n++) value |= u32(byte(physical + n)) << 8 * n;
    return value;
  }

  auto write(u32 size, u32 address, u32 data) -> void override {
    if(failed || !reaches(address, size) || !kernel.spinStorable(address, size)) return void(failed = true);
    u32 physical = address & 0x1fff'ffff;
    for(u32 n = 0; n < size; n++) put(physical + n, data >> 8 * n, false);
  }

  auto marked(u32 library) -> bool { return kernel.mpegMarkedIn(*this, library); }

  //A function's write: size bytes at address, as Memory::copyIn() makes it (nothing, if they aren't all there), the
  //bytes from timedFrom to timedTo of it decided by the time. Whether it wrote them.
  auto store(u32 address, const u8* bytes, u32 size, u32 timedFrom = 0, u32 timedTo = 0) -> bool {
    timedTo = std::min(timedTo, size);
    timedFrom = std::min(timedFrom, timedTo);
    if(failed || !size || !reaches(address, size)) return false;
    if(!kernel.spinStorable(address, size)) return failed = true, false;
    u32 physical = address & 0x1fff'ffff;
    for(u32 n = 0; n < size; n++) put(physical + n, bytes[n], n >= timedFrom && n < timedTo);
    if(timedFrom < timedTo) timedWrites++;
    return true;
  }
};

//After a quiet or calm call (syscall()): the spin it may be part of. A turn is from one call at a place (the anchor:
//the syscall's address and where the program goes on from it) to the next; once SpinTurns of them in a row have taken
//the same time, spinSkip(). A call elsewhere within SpinLongest cycles is another of the turn's; past that, it may
//anchor a new spin.
auto Kernel::spinTurn() -> void {
  u32 site = cpu.pipeline.address, back = cpu.ipu.pc;
  if(site != spin.site || back != spin.back) {
    if(spin.site && cycles - spin.last <= SpinLongest) return;
    spin = {site, back, cycles};
    return;
  }
  u64 period = cycles - spin.last;
  spin.last = cycles;
  if(period != spin.period || !period) {  //(none: called from outside the CPU's go, as tests call functions)
    spin.period = period;
    spin.turns = 0;
    return;
  }
  if(++spin.turns < SpinTurns + spin.wait) return;
  spin.turns = 0;
  spinSkip();
}

//The spin's next turns skipped, if its next turn is proved to change nothing (spinProve()): every whole turn that fits
//before the go's limit but the last SpinMargin, each counted in the clock and in what it changes in the kernel, the
//last skipped one's bytes the time decides written as its calls would have written them. A proof that fails waits
//longer each time before the next.
auto Kernel::spinSkip() -> void {
  //(in a go of the CPU's, whose limit and count these are; nothing else may come in meanwhile: no call into the
  //program waiting or running, no vertical blank's handlers)
  if(!going || interrupting || !current || current->status != Status::Running || !calls.empty() || vblankPending) return;
  //(with the CPU's work beside the GE counted, the go's limit exactly, as the synchronous GE's go has it, where the GE
  //thread's bound set it short: as many turns skipped, GE::Cost::Contended, below)
  if constexpr(GE::Cost::Contended) if(ge.thread && !geLimits.empty()) {
    for(auto& limit : geLimits) goExact = std::min(goExact, geExactLimit(limit));
    geLimits.clear(), geFolded.clear();
    cpu.runLimit = goExact;
  }
  //the skipped turns' blocks all end before the go's limit: the last at the count the anchor's syscall ends on
  u64 room = cpu.runLimit > counted + 1 ? cpu.runLimit - counted - 1 : 0;
  if(room / spin.period <= SpinMargin) return;
  u64 turns = room / spin.period - SpinMargin, skipped = turns * spin.period;
  //a list's FINISH (or, with the PSP's GE timing, its SIGNAL) taken at the first system call once it's due: none comes
  //by within the turns skipped
  //(with the CPU's work beside the GE counted, the time exactly: skipped turns end one stretch for all of them, run
  //ones one each, GE::Cost::Contended, so a skip must be the synchronous GE's; the GE thread waited for where the
  //bound doesn't decide)
  auto due = [&](u64 bound, const u64& at) {
    if(bound > cycles + skipped) return false;
    if constexpr(GE::Cost::Contended) geFold(true), bound = at;
    return bound <= cycles + skipped;
  };
  if((geFinishDue && due(geBound(), geDoneAt)) || (geSignalDue && due(geReachBound(), geReachAt))) return;
  SpinShadow shadow{*this, memory};
  if(!spinProve(shadow) || shadow.period != spin.period) {
    spin.wait = std::min(spin.wait * 2 + SpinTurns, SpinWaitMost);
    return;
  }
  spin.wait = 0;
  if(shadow.rotations) {
    readySequence += turns * shadow.rotations;
    current->readySince = readySequence;
  }
  for(auto& write : shadow.timed) {
    auto info = threadInfo(*write.thread, cycles + 1 + (turns - 1) * spin.period + write.executed);
    memory.copyIn(write.address + write.first, info.bytes + write.first, write.end - write.first);
  }
  cycles += skipped;
  counted += skipped;
  cpu.instructionsRun += skipped;
  spun += turns;
  if(gePspTiming) geContend(cycles - skipped, cycles, skipped);  //(the turns' own accesses go uncounted)
}

//One turn on the shadow from the CPU as it is, after the anchor's call: true if it comes back to the anchor with every
//register as it was, its stores and calls having changed nothing but bytes the time decides, which nothing in it reads
//or writes another way.
auto Kernel::spinProve(SpinShadow& shadow) -> bool {
  shadow.ipu = cpu.ipu;
  shadow.fpu = cpu.fpu;
  shadow.vfpu = cpu.vfpu;
  shadow.scc = cpu.scc;
  shadow.start = cycles + 1;  //(the anchor's syscall is an instruction too)
  for(u32 n = 0; n < SpinLongest && !shadow.closed; n++) {
    shadow.executed = n;
    shadow.instruction();
    if(shadow.failed || shadow.pipeline.exception || shadow.scc.halted) return false;
    shadow.period = n + 1;
  }
  if(!shadow.closed) return false;
  auto same = [](const auto& a, const auto& b) { return !std::memcmp(&a, &b, sizeof(a)); };
  if(!same(shadow.ipu, cpu.ipu) || !same(shadow.fpu.r, cpu.fpu.r) || shadow.fpu.csr != cpu.fpu.csr) return false;
  if(!same(shadow.vfpu, cpu.vfpu) || !same(shadow.scc.r, cpu.scc.r)) return false;
  if(shadow.scc.interrupts != cpu.scc.interrupts || shadow.scc.halted != cpu.scc.halted) return false;
  for(auto& [physical, held] : shadow.stored) {
    if(!held.timed && held.value != memory.ram[physical - Memory::RAMBase]) return false;
  }
  //(every write with bytes the time decides is one the skip writes again)
  if(shadow.timed.size() != shadow.timedWrites) return false;
  //(a load the turn made before a byte the time decides was written: what the last turn's call wrote there)
  if(shadow.timedWrites) {
    for(auto& read : shadow.reads) {
      for(u32 n = 0; n < read.size; n++) {
        if(auto found = shadow.stored.find(read.address + n); found != shadow.stored.end() && found->second.timed) {
          return false;
        }
      }
    }
  }
  return true;
}

//A system call on the shadow: the function worked out (Function::spin), its result in the shadow's registers. The
//anchor's call closes the turn.
auto Kernel::spinCall(SpinShadow& shadow, u32 code) -> bool {
  if(shadow.failed || code < FirstImportCode || code - FirstImportCode >= imports.size()) return false;
  auto function = imports[code - FirstImportCode].function;
  if(!function || !function->spin) return false;
  if(!(this->*function->spin)(shadow, shadow.time()) || shadow.failed) return false;
  if(shadow.pipeline.address == spin.site && shadow.ipu.pc == spin.back) shadow.closed = true;
  return true;
}

//Whether the size bytes at address (in main RAM) are on pages whose stores do no more than store: none holds compiled
//code (a store there drops it), and the GE watches none (a store there forgets textures decoded from it).
auto Kernel::spinStorable(u32 address, u32 size) -> bool {
  u32 physical = address & 0x1fff'ffff;
  for(u32 page = physical / Memory::PageSize; page <= (physical + size - 1) / Memory::PageSize; page++) {
    if(page < memory.watched.size() && memory.watched[page]) return false;
    if(cpu.recompiler.holds(page)) return false;
  }
  return true;
}

//The functions a spin's turn may call, worked out without being made, each as the function itself does it in the case
//it takes: its result in the shadow's v0, its writes through store(), what it changes counted in the shadow; false for
//a call that would do something else (the turn isn't skipped).

auto Kernel::spinGetThreadId(SpinShadow& shadow, u64) -> bool {
  shadow.ipu.r[2] = interrupting ? ErrorIllegalContext : current ? current->uid : 0;
  return true;
}

//(the running thread's run time is the bytes the time decides)
auto Kernel::spinReferThreadStatus(SpinShadow& shadow, u64 time) -> bool {
  auto thread = findThread(shadow.arg(0));
  if(!thread) return shadow.ipu.r[2] = ErrorUnknownThread, true;
  u32 address = shadow.arg(1), length = threadInfoLength(), size = shadow.read(4, address);
  if(length == 108 && size > length) return shadow.ipu.r[2] = ErrorIllegalSize, true;
  auto info = threadInfo(*thread, time);
  u32 copied = std::min(size, length), timed = thread == current && !interrupting ? ThreadInfoRunTime : 0;
  bool wrote = shadow.store(address, info.bytes, copied, timed, timed ? timed + 8 : 0);
  if(wrote && timed && copied > timed) {
    shadow.timed.push_back({thread, address, timed, std::min(copied, timed + 8), shadow.executed});
  }
  shadow.ipu.r[2] = 0;
  return true;
}

//The caller's own line, which it goes to the end of and then has the CPU again, no other thread being ready at its
//priority or above (reschedule()); and an illegal priority, refused. Any other rotation may hand the CPU over, or
//reorders other threads.
auto Kernel::spinRotateThreadReadyQueue(SpinShadow& shadow, u64) -> bool {
  u32 priority = shadow.arg(0);
  if(priority == 0 && current) priority = current->priority;
  if(priority < 0x08 || priority > 0x77) return shadow.ipu.r[2] = ErrorIllegalPriority, true;
  if(!current || current->status != Status::Running || current->priority != priority || current->suspended) return false;
  if(interrupting || dispatchSuspended || !shadow.scc.interrupts || current->wait != Wait::None) return false;
  for(auto& [uid, thread] : threads) {
    if(thread.get() == current || thread->status != Status::Ready || thread->suspended) continue;
    if(thread->priority <= priority) return false;
  }
  shadow.ipu.r[2] = 0;
  shadow.rotations++;
  return true;
}

//Refused (no such semaphore, a count past its most): a signal that counts changes the semaphore.
auto Kernel::spinSignalSema(SpinShadow& shadow, u64) -> bool {
  u32 refused = signalRefused(shadow.arg(0), s32(shadow.arg(1)));
  if(!refused) return false;
  shadow.ipu.r[2] = refused;
  return true;
}

auto Kernel::spinMpegRingbufferAvailableSize(SpinShadow& shadow, u64) -> bool {
  shadow.ipu.r[2] = ringAvailable(shadow, shadow.arg(0));
  return !shadow.failed;
}

auto Kernel::spinGetVcount(SpinShadow& shadow, u64) -> bool {
  shadow.ipu.r[2] = vblanks;
  return true;
}

//The GE's looks (a mode but 0, which waits): what the GE and its lists are doing changes only in a system call or
//between the CPU's goes (a list's FINISH is taken at a call once it's due, as is its SIGNAL with the PSP's GE timing
//once the GE's front end reaches it, and the go ends by then: spinSkip()), so every turn of a go finds what this one
//finds.
auto Kernel::spinGeListSync(SpinShadow& shadow, u64) -> bool {
  s32 index = geIndex(shadow.arg(0));
  if(index < 0) return shadow.ipu.r[2] = ErrorInvalidID, true;
  if(shadow.arg(1) == 0) return false;
  shadow.ipu.r[2] = geListLook(index, shadow.arg(1));
  return true;
}

auto Kernel::spinGeDrawSync(SpinShadow& shadow, u64) -> bool {
  if(shadow.arg(0) == 0) return false;
  shadow.ipu.r[2] = geDrawLook(shadow.arg(0));
  return true;
}
