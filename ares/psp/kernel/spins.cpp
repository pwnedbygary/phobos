//Spins: a thread polling system functions in a loop that changes nothing until something comes from outside it (a
//vertical blank, a timer, another thread). Both Need for Speeds' idle thread puts itself back in its line, asking its
//ID and its own status (for its priority) first, 175,000 times a frame (half a million calls) till a count another
//thread or a handler keeps moves on; the Twisted Metal, Disaster Report 3 and Armored Core intro movies ask the
//movie's ring how full it is (and signal a semaphore already at its most) in 90,000 to 370,000 calls a frame till the
//reader thread feeds it; Resistance asks the GE whether its list is done (sceGeListSync(id, 1)), with the PSP's GE
//timing some 285,000 times a frame, till it is; Need for Speed: Shift asks sceGeDrawSync(1) some 45,000 times a frame,
//each turn also signalling a semaphore and taking it back, twice, with waits that succeed at once (calm: they wake no
//thread and hand the CPU to none), as Burnout Dominator's and Legends' turns do; and Midnight Club: L.A. Remix checks
//its callbacks (sceKernelCheckCallback) some 8,000 times a frame with none due (calm). A system call costs the clock
//one cycle, so a turn of such a loop is a few dozen cycles of the PSP's time, and nothing else happens till the CPU's
//go ends at the next thing due (Kernel::run(): only system functions bring that end sooner, and these, quiet or calm,
//don't).
//
//So once a loop has come round the same way a few times, the kernel proves that its next turn leaves everything a
//later turn sees as it was, and jumps ahead by whole turns to just before the go's end, the last turns running as
//they would: registers, memory, the clock and every later result come out as running every turn makes them. The
//proof is one turn run on a CPU of its own (SpinShadow), through the interpreter, from the real registers:
//  - its loads and stores reach main RAM alone, the stores kept aside (the turn's own loads see them) and only to
//    pages whose stores do nothing more (spinStorable(): no compiled code there, none the GE watches);
//  - its system calls are worked out by the kernel without being made (Function::spin): what each returns, what it
//    writes, what it changes (rotating the caller's own line, alone in it: the ready order's count; a semaphore's
//    count, as the turn's own signals and waits so far have it), and which of the bytes it writes the time decides (a
//    thread's run time), which the turn must never read, nor write over another way;
//  - it must come back to the call it started after (the anchor: the same syscall, going on to the same place), every
//    register as it was, every byte it stored holding what memory holds (but those the time decides), and every
//    semaphore's count where it was.
//Then every turn does what this one did, at its own time: the CPU is deterministic (the interpreter and compiled code
//alike: tests/allegrex holds them to it), every value it reads is the same, and so is what each function returns. The
//go ends at the first block end where the run's count reaches its limit, and a turn's blocks end at the same counts
//whether or not the turns before it ran (the anchor's syscall always ends its block: endsBlock(), or the branch it's
//the delay slot of); so skipping turns whose blocks all end before the limit (and before a list's FINISH falls due,
//which a system call would take) leaves where the go ends, and so when everything after happens, as it was. Nothing
//of this is kept in a state: a spin found again after a state loads is skipped the same way, and one not found runs
//every turn, to the same end.
//
//One loop whose turns change something is skipped too: Dante's Inferno's frame pacing (ULUS10469), which polls the
//real-time clock (sceRtcGetCurrentTick) and adds the microseconds since the tick it kept to a float of seconds till
//that reaches a threshold. Its code is matched word for word (spinTicking()), its turns worked out from the clock's
//ticks with the CPU's own float arithmetic (spinTickTurns()), one of them run on the shadow against that
//(spinTickProve()), and what the last turn skipped leaves written in their place (spinTickLast()): below.

//The shadow: a CPU of its own running one turn of a spin through the interpreter (see above). A load or store of the
//turn's own outside main RAM, a store that would do more than store, a byte the time decides read or written over, an
//exception, a halt or a function the kernel can't work out without making it ends the proof (failed); a function's
//own stores and checks of an address are answered as Memory answers them (reaches()).
struct SpinShadow : Allegrex {
  Kernel& kernel;
  Memory& memory;
  bool failed = false;
  bool closed = false;    //the turn came back to the anchor
  bool ticking = false;   //the clock loop's proof (spinTickProve()): the clock answered (spinGetCurrentTick())
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
  //What the turn's calls added to each semaphore's count so far, less what they took, by its ID (spinSignalSema(),
  //spinWaitSemaphore()): each back at 0 by the turn's end, or the turn changes it
  std::map<u32, s64> semaphores;
  auto held(u32 uid) const -> s64 {
    auto found = semaphores.find(uid);
    return found == semaphores.end() ? 0 : found->second;
  }

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

//The clock loop's float arithmetic as its turn does it, through the CPU's own functions (cpu/interpreter-fpu.cpp: the
//rounding, flushing and NaNs as FCSR has them, the CPU's given), on a shadow of its own.
struct Kernel::TickMath {
  SpinShadow shadow;
  TickMath(Kernel& kernel, Memory& memory, u32 csr) : shadow(kernel, memory) { shadow.fpu.csr = csr; }
  auto seconds(u32 difference) -> u32 {  //cvt.s.w f1, f0; f0 = 1,000,000.0; div.s f1, f1, f0
    shadow.fpu.r[0] = difference;
    shadow.FCVTSW(1, 0);
    shadow.fpu.r[0] = 0x4974'2400;
    shadow.FDIV(1, 1, 0);
    return shadow.fpu.r[1];
  }
  auto add(u32 acc, u32 seconds) -> u32 {  //add.s f0, f0, f1
    shadow.fpu.r[0] = acc, shadow.fpu.r[1] = seconds;
    shadow.FADD(0, 0, 1);
    return shadow.fpu.r[0];
  }
  auto below(u32 acc, u32 threshold) -> bool {  //c.lt.s f0, f1
    shadow.fpu.r[0] = acc, shadow.fpu.r[1] = threshold;
    shadow.FCOMPARE(0, 1, 0xc);
    return shadow.fpu.csr.bit(23);
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
    spinRefusalsEnd();
    spin = {site, back, cycles};
    return;
  }
  u64 period = cycles - spin.last;
  spin.last = cycles;
  if(period != spin.period || !period) {  //(none: called from outside the CPU's go, as tests call functions)
    spinRefusalsEnd();
    spin.period = period;
    spin.turns = 0;
    return;
  }
  u32 due = SpinTurns + spin.wait;  //(the turn whose end tries a skip)
  if(++spin.turns + 2 >= due && gePspTiming) {  //(what it and the turn before it move in the CPU's caches)
    u64 counts[4];
    spinCounts(counts);
    u32 calls = u32(std::min<u64>(syscalls - spin.called, ~0u));
    if(spin.turns == due) {
      spin.steady = calls == spin.calls;
      for(u32 n : range(4)) spin.steady &= counts[n] - spin.counts[n] == spin.moved[n];
    }
    for(u32 n : range(4)) spin.moved[n] = counts[n] - spin.counts[n], spin.counts[n] = counts[n];
    spin.calls = calls, spin.called = syscalls;
  }
  if(spin.turns < due) return;
  spin.turns = 0;
  spinSkip();
}

//The spin's next turns skipped, if its next turn is proved to change nothing (spinProve()): every whole turn that fits
//before the go's limit but the last SpinMargin, each counted in the clock and in what it changes in the kernel, the
//last skipped one's bytes the time decides written as its calls would have written them. A proof that fails waits
//longer each time before the next. The clock loop's turns are worked out instead, what the last leaves written.
auto Kernel::spinSkip() -> void {
  spinRefusalsEnd();  //(a try, whatever comes of it, ends the turns run in place of a skip refused before)
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
  //With the PSP's GE timing, the GE busy for all of the turns or idle for all of them, not done part way: their one
  //stretch then adds to its busy window what theirs would (its sums whole counts). It pushes the GE back as far as
  //theirs would have by the last turn's end if the push the window owes (geContendDue()) is at its most there, or no
  //more than before the first turn: so it is where that push falls and then rises at most along the turns, which it
  //does with each turn one stretch (one system call), or with the turns moving no lines (then any number: a spin's
  //turn never waits, so its stretches are all instructions), the window's time the same kind throughout
  //(geContendBound()): along the turns the cycles passed, the lines and the CPU's time with its stalls grow evenly and
  //the list's shares stay, so what the window owes is monotone along them, and each of its parts (geContendOwing():
  //the front end's a / (1 + a), the back end's b) times the cycles passed is convex, or concave and rising. The one
  //mix that could peak, the front's concave and the back's convex as what the window owes falls, doesn't: the ratio of
  //their curvatures rises along the turns, so the push's slope turns only from falling to rising, and the sum has no
  //maximum within them. The cap can make one, the push following it up and then falling with the sum: a cap binding
  //at either end refuses the skip, counted (spinRefused()). Only one binding at the start and not at the end can make
  //a skip inexact, so refusing at either end refuses more than it must, never less. With no lines in the window nor
  //the turns' the window owes nothing. And the last two turns run moved the same lines with as many system calls
  //(spinTurn()): each turn skipped moves what they did (below).
  if(gePspTiming && !spin.steady) return void(spinChecks.unsteady++);
  if(gePspTiming && ge.thread) geFold(true);  //(the window and the GE's time as the synchronous GE has them)
  auto longer = [&] { spin.wait = std::min(spin.wait * 2 + SpinTurns, SpinWaitMost); };
  //The clock loop (below), where its code is the template's: the turns that come back to the anchor, as many as the
  //room has at most, and with the PSP's GE timing and the GE busy no more than end before it's done (the turn that
  //takes it past that runs, and the next try comes after it); one turn on the shadow as they have it, or the next try
  //waits longer, as after a proof that failed. Not the template: on as any spin, whose proof fails at the clock's call.
  bool ticking = false;
  SpinTick tick;
  TickLast last{};
  if(spinClockAnchor() && spinTicking(tick)) {
    u64 most = turns;
    if(gePspTiming && geDoneAt > cycles) most = std::min(most, (geDoneAt - 1 - cycles) / spin.period);
    if(!most) return;  //(the GE done within the next turn)
    if(u32(tick.tick - tick.kept) >> 31) return longer();  //(turn 1's difference would take its other way)
    TickMath math{*this, memory, u32(cpu.fpu.csr)};
    u32 acc = 0;
    if(!(turns = spinTickTurns(tick, math, most, acc))) return;  //(the loop leaves at the next turn)
    SpinShadow shadow{*this, memory};
    if(!spinTickProve(tick, shadow, math)) return longer();
    skipped = turns * spin.period, last = spinTickLast(tick, turns, acc), ticking = true;
  }
  //(a try at the clock loop refused below waits longer too: each refused try has worked its turns out)
  auto refused = [&](u64& check) { check++; if(ticking) longer(); };
  if(gePspTiming && geDoneAt > cycles) {
    if(geDoneAt <= cycles + skipped) return refused(spinChecks.partway);
    bool moving = spin.moved[0] || spin.moved[1] || spin.moved[2] || spin.moved[3];
    GeWindow end = geWindow;
    end.passed += skipped, end.instructions += skipped;
    end.fills += turns * (spin.moved[0] + spin.moved[3]), end.writebacks += turns * spin.moved[1];
    end.uncached += turns * spin.moved[2];
    bool lines = moving || geWindow.fills || geWindow.writebacks || geWindow.uncached;
    if(lines && ((moving && spin.calls > 1) || geContendBound(geWindow) != geContendBound(end))) {
      return refused(moving && spin.calls > 1 ? spinChecks.calls : spinChecks.kind);
    }
    if(lines && (geContendCapped(geWindow) || geContendCapped(end))) {
      if(ticking) longer();
      return spinRefused(skipped);
    }
  }
  SpinShadow shadow{*this, memory};
  if(!ticking && (!spinProve(shadow) || shadow.period != spin.period)) return longer();
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
  if(ticking) spinTickSkip(tick, last);
  if(gePspTiming) {
    //Each turn skipped moves the lines in the CPU's caches the last one run did: the turns are alike, the same
    //accesses in the same order, and after one such turn the caches hold what any later one leaves them holding, each
    //set its turn's last two lines (or, touched by one line or none, as it was), so every turn from there counts the
    //same and changes nothing. (The last two turns run moving the same lines, the one before the last was such a
    //turn, and the last counted what each turn from there does.)
    auto& data = cpu.dataCache;
    data.fills += turns * spin.moved[0], data.writebacks += turns * spin.moved[1];
    data.uncached += turns * spin.moved[2], cpu.instructionCache.fills += turns * spin.moved[3];
    geContend(cycles - skipped, cycles, skipped);
  }
}

//A skip refused because the cap binds at either end of its turns (spinSkip()), for the runner's report: counted, and
//the turns that run in its place counted from here, by geInstructions' count, till the next try or the spin's end
//(spinRefusalsEnd()), each instruction once, and no further than the turns it would have skipped (skipped, their
//instructions: a turn's cycles are its instructions). A try with less room than the turns to the next try has no next
//try in its go: what the go's end brings (an interrupt's handler, a thread woken) runs before the spin goes on, and
//isn't the spin's.
auto Kernel::spinRefused(u64 skipped) -> void {
  geSkipsRefused++;
  if(!geRefusing) geRefusing = true, geRefusedFrom = geInstructions, geRefusedUntil = geInstructions + skipped;
}

//The instructions run in place of a refused skip, from its refusal till now or its own turns' end, into the report's
//count.
auto Kernel::spinRefusalsEnd() -> void {
  if(geRefusing) geRefusedInstructions += std::min(geInstructions, geRefusedUntil) - geRefusedFrom, geRefusing = false;
}

//The CPU's caches' counts as they are (data lines read in, written back, uncached accesses, instruction lines read in).
auto Kernel::spinCounts(u64 (&counts)[4]) const -> void {
  auto& data = cpu.dataCache;
  counts[0] = data.fills, counts[1] = data.writebacks, counts[2] = data.uncached;
  counts[3] = cpu.instructionCache.fills;
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
  //(every semaphore's count where it was: what the turn's waits took, its signals gave back)
  for(auto& [uid, net] : shadow.semaphores) if(net) return false;
  return true;
}

//A system call on the shadow: the function worked out (Function::spin), its result in the shadow's registers. The
//anchor's call closes the turn.
auto Kernel::spinCall(SpinShadow& shadow, u32 code) -> bool {
  if(shadow.failed || code < FirstImportCode || code - FirstImportCode >= imports.size()) return false;
  auto function = imports[code - FirstImportCode].function;
  //(the clock has no Function::spin: it's answered on the clock loop's shadow alone, so no other proof passes it)
  auto worked = !function ? nullptr
              : shadow.ticking && function->handler == &Kernel::sceRtcGetCurrentTick ? &Kernel::spinGetCurrentTick
              : function->spin;
  if(!worked || !(this->*worked)(shadow, shadow.time()) || shadow.failed) return false;
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

//Refused (no such semaphore, a count past its most in the turn's view, its own signals and waits so far counted): its
//result alone. One that counts, with no thread waiting on that semaphore and no better thread to take the CPU
//(reschedule(), with the interrupts as the turn has them at the call: mtic may have changed them since the anchor):
//the count up, in the turn's view. Any other may wake a thread or hand the CPU over.
auto Kernel::spinSignalSema(SpinShadow& shadow, u64) -> bool {
  u32 uid = shadow.arg(0);
  s32 count = s32(shadow.arg(1));
  if(u32 refused = signalRefused(uid, count, shadow.held(uid))) return shadow.ipu.r[2] = refused, true;
  for(auto& [id, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::Semaphore && thread->waitID == uid) return false;
  }
  if(wouldPreempt(shadow.scc.interrupts)) return false;
  shadow.semaphores[uid] += count;
  shadow.ipu.r[2] = 0;
  return true;
}

//A wait refused, the refusal decided with the interrupts as the turn has them at the call: its result alone (but
//ILLEGAL_CONTEXT's, which comes only while interrupting, where no spin is skipped). One whose count is there in the
//turn's view, a CB wait's callbacks not due (nothing makes one due within a go: a callback is notified between goes,
//or by a call that is neither quiet nor calm): the count taken, in the turn's view. One that would wait, or run its
//callbacks, isn't worked out.
auto Kernel::spinWaitSemaphore(SpinShadow& shadow, bool callbacks) -> bool {
  u32 uid = shadow.arg(0);
  s32 count = s32(shadow.arg(1));
  Semaphore* semaphore = nullptr;
  if(u32 refused = semaphoreWaitRefused(uid, count, shadow.scc.interrupts, semaphore)) {
    if(refused == ErrorIllegalContext) return false;
    return shadow.ipu.r[2] = refused, true;
  }
  if(callbacks && callbacksDue()) return false;
  if(s64(semaphore->count) + shadow.held(uid) < count) return false;
  shadow.semaphores[uid] -= count;
  shadow.ipu.r[2] = 0;
  return true;
}

auto Kernel::spinWaitSema(SpinShadow& shadow, u64) -> bool {
  return spinWaitSemaphore(shadow, false);
}

auto Kernel::spinWaitSemaCB(SpinShadow& shadow, u64) -> bool {
  return spinWaitSemaphore(shadow, true);
}

//Nothing due to run: 0. Not its ILLEGAL_CONTEXT path (from a handler or a callback: the kernel's call there isn't calm,
//so no turn making it is skipped), nor a check with one due, which runs it.
auto Kernel::spinCheckCallback(SpinShadow& shadow, u64) -> bool {
  if(interrupting || (current && current->inCallback)) return false;
  if(current && pendingCallback(*current)) return false;
  shadow.ipu.r[2] = 0;
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

//The clock loop: Dante's Inferno's frame pacing (ULUS10469). A timer's update() calls sceRtcGetCurrentTick, keeps the
//new tick, and adds the microseconds since the tick it kept, over 1,000,000, to a float of seconds; its elapsed()
//returns the float; the wait, in their caller, goes round while that's below a threshold. A turn is 55 instructions
//from one call to the clock to the next (the anchor, in the import's stub). Between the tick's changes (one a
//microsecond: every 6 turns or so) a turn changes nothing; at each it adds 1e-6 as single precision has it (the
//CPU's rounding). The rest it reads never moves: its code, its timer's vtable, the threshold, what it stores itself.
//So the turns that come back, and what the last of them leaves, follow from the ticks at its calls (rtcTick()) and
//the float's additions at their changes, made with the CPU's own functions in the turns' order. Its words (the
//template) are matched at every try wherever they are, through the pointers the turn holds: update()'s (the anchor's
//return less 0x18; word 4 the jal to the stub; words 22-27, the way a difference with its top bit set takes, never
//run in a skip, left out), elapsed()'s (the timer's vtable's slot 4, update() its slot 3) and the wait's (elapsed()'s
//return less 0xc; its last word the likely branch's delay slot, run only as the branch is taken).
namespace {
  constexpr u32 TickPeriod = 55;
  constexpr u64 TickMicrosecond = Kernel::CPUFrequency / 1'000'000;  //(cycles: rtcTick()'s)
  //update()'s words 0-21, then 28-38
  constexpr u32 TickUpdate[] = {
    0x27bd'ffe0,  //addiu sp, sp, -32
    0xafbf'000c,  //sw    ra, 12(sp)
    0xafb0'0008,  //sw    s0, 8(sp)
    0x0080'8021,  //addu  s0, a0, zero     the timer
    0x0000'0000,  //jal   the stub         (worked out from the anchor)
    0x27a4'0018,  //addiu a0, sp, 24       where the tick goes
    0x8fa6'0018,  //lw    a2, 24(sp)       the tick, low (the anchor's call returns here)
    0x8e04'0008,  //lw    a0, 8(s0)        the tick kept, low
    0x8fa5'001c,  //lw    a1, 28(sp)       the tick, high
    0x8e02'000c,  //lw    v0, 12(s0)       the tick kept, high
    0x00c4'182b,  //sltu  v1, a2, a0       the borrow
    0x00c4'2023,  //subu  a0, a2, a0       the difference, low
    0x00a2'1023,  //subu  v0, a1, v0
    0xae06'0008,  //sw    a2, 8(s0)        the tick kept
    0x0043'1023,  //subu  v0, v0, v1       the difference, high
    0xafa2'0014,  //sw    v0, 20(sp)
    0xae05'000c,  //sw    a1, 12(s0)
    0x0480'0004,  //bltz  a0, word 22      (its top bit set)
    0xafa4'0010,  //sw    a0, 16(sp)
    0x4484'0000,  //mtc1  a0, f0
    0x1000'0007,  //b     word 28
    0x4680'0060,  //cvt.s.w f1, f0         the microseconds
    0x3c02'4974,  //lui   v0, 0x4974
    0x3442'2400,  //ori   v0, v0, 0x2400   1,000,000.0
    0x4482'0000,  //mtc1  v0, f0
    0x4600'0843,  //div.s f1, f1, f0       the seconds
    0xc600'0004,  //lwc1  f0, 4(s0)        the float
    0x4601'0000,  //add.s f0, f0, f1
    0xe600'0004,  //swc1  f0, 4(s0)
    0x8fbf'000c,  //lw    ra, 12(sp)
    0x8fb0'0008,  //lw    s0, 8(sp)
    0x03e0'0008,  //jr    ra
    0x27bd'0020,  //addiu sp, sp, 32
  };
  constexpr u32 TickElapsed[] = {
    0x27bd'fff0,  //addiu sp, sp, -16
    0xafbf'000c,  //sw    ra, 12(sp)
    0xafb0'0008,  //sw    s0, 8(sp)
    0x8c99'0000,  //lw    t9, 0(a0)        the vtable
    0x8f39'000c,  //lw    t9, 12(t9)       update()
    0x0320'f809,  //jalr  ra, t9
    0x0080'8021,  //addu  s0, a0, zero
    0xc600'0004,  //lwc1  f0, 4(s0)        the float, returned
    0x8fbf'000c,  //lw    ra, 12(sp)
    0x8fb0'0008,  //lw    s0, 8(sp)
    0x03e0'0008,  //jr    ra
    0x27bd'0010,  //addiu sp, sp, 16
  };
  constexpr u32 TickWait[] = {
    0x8f39'0010,  //lw    t9, 16(t9)       elapsed()
    0x0320'f809,  //jalr  ra, t9
    0x2624'0018,  //addiu a0, s1, 24       the timer, in its owner
    0xc621'0064,  //lwc1  f1, 100(s1)      the threshold
    0x4601'003c,  //c.lt.s f0, f1
    0x0000'0000,  //nop
    0x4503'fff9,  //bc1tl word 0           round again while below
    0x8e39'0018,  //lw    t9, 24(s1)       the timer's vtable
  };

  //A word of main RAM, where the address is aligned and its four bytes there.
  auto tickWord(Memory& memory, u32 address, u32& value) -> bool {
    u32 offset = (address & 0x1fff'ffff) - Memory::RAMBase;
    if(address & 3 || offset >= memory.ram.size() || memory.ram.size() - offset < 4) return false;
    value = memory.ram[offset] | memory.ram[offset + 1] << 8 | memory.ram[offset + 2] << 16;
    value |= u32(memory.ram[offset + 3]) << 24;
    return true;
  }
}

//Whether the spin's anchor is sceRtcGetCurrentTick's syscall.
auto Kernel::spinClockAnchor() -> bool {
  u32 word = 0;
  if(!tickWord(memory, spin.site, word) || (word & 0xfc00'003f) != 0x0000'000c) return false;
  u32 code = word >> 6 & 0xf'ffff;
  if(code < FirstImportCode || code - FirstImportCode >= imports.size()) return false;
  auto function = imports[code - FirstImportCode].function;
  return function && function->handler == &Kernel::sceRtcGetCurrentTick;
}

//sceRtcGetCurrentTick on the clock loop's shadow (spinCall()): the tick at the call's time, at a0.
auto Kernel::spinGetCurrentTick(SpinShadow& shadow, u64 time) -> bool {
  u64 tick = rtcTick(time);
  u8 bytes[8];
  for(u32 n : range(8)) bytes[n] = u8(tick >> 8 * n);
  if(!shadow.store(shadow.arg(0), bytes, 8)) return false;
  shadow.ipu.r[2] = 0;
  return true;
}

//The template matched against the CPU as the anchor's call left it, and what the turns start from (SpinTick): the
//period; the stub; the registers the turn holds there (a0 where the tick went, update()'s frame + 24; s1 the timer's
//owner, the timer less 24; t9 update(); ra the anchor's return; v0 the call's 0); the words; the frames' saved
//timer and returns; every address the turn loads from or stores to aligned and in main RAM, its stores to pages where
//they do no more than store (spinStorable()), none over another or over what the turn reads but its own stores (its
//code, the vtable's pointer and slots, the threshold); and the call's tick where the call put it. False: not this
//loop.
auto Kernel::spinTicking(SpinTick& t) -> bool {
  auto& r = cpu.ipu.r;
  u32 w = 0, vtable = 0, slot = 0, saved = 0;
  auto word = [&](u32 address, u32& value) { return tickWord(memory, address, value); };
  auto is = [&](u32 address, u32 value) { return word(address, w) && w == value; };
  auto ticks = [&](u32 address, u64& value) {
    u32 high = 0;
    if(!word(address, w) || !word(address + 4, high)) return false;
    return value = w | u64(high) << 32, true;
  };
  t.stack = r[29], t.timer = r[16], t.update = spin.back - 0x18;
  if(spin.period != TickPeriod || !is(spin.site - 4, 0x03e0'0008)) return false;  //(the stub's jr ra)
  if(r[4] != t.stack + 24 || r[17] != t.timer - 24 || r[25] != t.update || r[31] != spin.back || r[2]) return false;
  for(u32 n : range(std::size(TickUpdate))) {
    u32 want = n == 4 ? 0x0c00'0000 | ((spin.site - 4) >> 2 & 0x03ff'ffff) : TickUpdate[n];
    if(!is(t.update + 4 * (n < 22 ? n : n + 6), want)) return false;
  }
  if(!word(t.timer, vtable) || !word(vtable + 12, slot) || slot != t.update || !word(vtable + 16, t.elapsed)) {
    return false;
  }
  for(u32 n : range(std::size(TickElapsed))) if(!is(t.elapsed + 4 * n, TickElapsed[n])) return false;
  if(!is(t.stack + 8, t.timer) || !is(t.stack + 12, t.elapsed + 0x1c) || !word(t.stack + 44, saved)) return false;
  t.wait = saved - 0xc;
  for(u32 n : range(std::size(TickWait))) if(!is(t.wait + 4 * n, TickWait[n])) return false;
  //(main RAM is one run of memory, so the words read here and above take in every byte the turn stores: the timer's
  //+ 4 to + 15, and the frames' + 8 to + 47)
  if(!word(t.timer + 4, t.acc) || !word(t.timer + 76, t.threshold) || !ticks(t.timer + 8, t.kept)) return false;
  if(!spinStorable(t.timer + 4, 12) || !spinStorable(t.stack + 8, 24) || !spinStorable(t.stack + 40, 8)) return false;
  struct Range { u32 from, to; };
  auto at = [](u32 address, u32 size) { return Range{address & 0x1fff'ffff, (address & 0x1fff'ffff) + size}; };
  Range stores[] = {at(t.timer + 4, 12), at(t.stack + 8, 24), at(t.stack + 40, 8)};
  Range reads[] = {at(t.update, 0x9c), at(t.elapsed, 0x30), at(t.wait, 0x20), at(spin.site - 4, 8), at(t.timer, 4),
                   at(t.timer + 76, 4), at(vtable + 12, 8)};
  auto over = [](Range a, Range b) { return a.from < b.to && b.from < a.to; };
  for(auto& store : stores) {
    for(auto& read : reads) if(over(store, read)) return false;
    for(auto& other : stores) if(&other != &store && over(store, other)) return false;
  }
  t.cycles = cycles;
  return ticks(t.stack + 24, t.tick) && t.tick == rtcTick(cycles);
}

//The turns from the anchor that come back to it, no more than most, and the float as the last of them leaves it, acc.
//Turn i takes the tick its call before wrote, tick(i - 1), against the one kept, and keeps it; call j comes j turns
//of 55 cycles after the anchor's. So turn 1 adds the anchor's tick's difference from the timer's, whatever it is;
//after it, the turn after a call whose tick is new adds the seconds of 1 microsecond (the step), and every other turn
//+0, which leaves the float as it is: +0 changes only -0 (but rounding down), a NaN that signals and, with FCSR's
//flush bit, a subnormal, and the CPU's addition of something positive or +0 leaves none of those (docs/psp-core.md,
//part 118). The first turn whose float isn't below the threshold leaves the loop.
auto Kernel::spinTickTurns(const SpinTick& t, TickMath& math, u64 most, u32& acc) -> u64 {
  acc = math.add(t.acc, math.seconds(u32(t.tick - t.kept)));
  if(!math.below(acc, t.threshold)) return 0;
  u32 step = math.seconds(1);
  u64 c = t.cycles, q = c / TickMicrosecond;
  for(u64 r = 1;; r++) {
    u64 call = (TickMicrosecond * (q + r) - c + TickPeriod - 1) / TickPeriod;  //(the r-th call whose tick is new)
    if(call >= most) return most;
    u32 next = math.add(acc, step);
    spinTickAdditions++;
    if(!math.below(next, t.threshold)) return call;
    acc = next;
  }
}

//What the last of turns turns leaves, acc its float (spinTickTurns()): the one place the turns' state is worked out,
//for the skip, the shadow's turn and the tests. Turn n keeps tick(n - 1), its difference from the one kept before it
//(the timer's for turn 1), and calls for tick(n); a1 and a2 are tick(n - 1)'s words, v1 the borrow of the difference's
//low word, f0 the float.
auto Kernel::spinTickLast(const SpinTick& t, u64 turns, u32 acc) const -> TickLast {
  u64 kept = rtcTick(t.cycles + (turns - 1) * TickPeriod);
  u64 before = turns > 1 ? rtcTick(t.cycles + (turns - 2) * TickPeriod) : t.kept;
  u64 difference = kept - before, tick = rtcTick(t.cycles + turns * TickPeriod);
  TickLast last{};
  last.v1 = u32(kept) < u32(before), last.a1 = u32(kept >> 32), last.a2 = u32(kept), last.f0 = acc;
  last.timer[0] = acc, last.timer[1] = u32(kept), last.timer[2] = u32(kept >> 32);
  last.stack[0] = u32(difference), last.stack[1] = u32(difference >> 32);
  last.stack[2] = u32(tick), last.stack[3] = u32(tick >> 32);
  return last;
}

//One turn on the shadow from the CPU's registers (the clock answered: spinGetCurrentTick()), against the first turn's
//state as spinTickLast() has it: back at the anchor in 55 instructions, every register the CPU's but v1, a1, a2 and
//f0, every byte it stored memory's but the timer's float and tick and the frame's difference and tick, those the
//model's; nothing else changed.
auto Kernel::spinTickProve(const SpinTick& t, SpinShadow& shadow, TickMath& math) -> bool {
  auto last = spinTickLast(t, 1, math.add(t.acc, math.seconds(u32(t.tick - t.kept))));
  shadow.ticking = true;
  shadow.ipu = cpu.ipu;
  shadow.fpu = cpu.fpu;
  shadow.vfpu = cpu.vfpu;
  shadow.scc = cpu.scc;
  shadow.start = cycles + 1;  //(the anchor's syscall is an instruction too)
  for(u32 n = 0; n < TickPeriod && !shadow.closed; n++) {
    shadow.executed = n;
    shadow.instruction();
    if(shadow.failed || shadow.pipeline.exception || shadow.scc.halted) return false;
    shadow.period = n + 1;
  }
  if(!shadow.closed || shadow.period != TickPeriod) return false;
  auto ipu = cpu.ipu;
  auto fpu = cpu.fpu;
  ipu.r[3] = last.v1, ipu.r[5] = last.a1, ipu.r[6] = last.a2, fpu.r[0] = last.f0;
  auto same = [](const auto& a, const auto& b) { return !std::memcmp(&a, &b, sizeof(a)); };
  if(!same(shadow.ipu, ipu) || !same(shadow.fpu.r, fpu.r) || shadow.fpu.csr != cpu.fpu.csr) return false;
  if(!same(shadow.vfpu, cpu.vfpu) || !same(shadow.scc.r, cpu.scc.r)) return false;
  if(shadow.scc.interrupts != cpu.scc.interrupts || shadow.scc.halted != cpu.scc.halted) return false;
  if(shadow.timedWrites || shadow.rotations) return false;
  for(auto& [uid, net] : shadow.semaphores) if(net) return false;
  u8 model[28];
  for(u32 n : range(28)) model[n] = (n < 12 ? last.timer[n / 4] : last.stack[(n - 12) / 4]) >> 8 * (n % 4);
  u32 timer = (t.timer & 0x1fff'ffff) + 4, stack = (t.stack & 0x1fff'ffff) + 16, modelled = 0;
  for(auto& [physical, held] : shadow.stored) {
    u32 at = physical - timer < 12 ? physical - timer : physical - stack < 16 ? 12 + physical - stack : ~0u;
    if(held.timed || held.value != (at < 28 ? model[at] : memory.ram[physical - Memory::RAMBase])) return false;
    modelled += at < 28;
  }
  return modelled == 28;
}

//The skip's own writes: what the last turn skipped leaves (spinTickLast()), in the CPU's registers and through memory
//(on pages spinTicking() found its stores do no more than store, as the turns' own stores would).
auto Kernel::spinTickSkip(const SpinTick& t, const TickLast& last) -> void {
  cpu.ipu.r[3] = last.v1, cpu.ipu.r[5] = last.a1, cpu.ipu.r[6] = last.a2;
  cpu.fpu.r[0] = last.f0;
  for(u32 n : range(3)) memory.write(4, t.timer + 4 + 4 * n, last.timer[n]);
  for(u32 n : range(4)) memory.write(4, t.stack + 16 + 4 * n, last.stack[n]);
}
