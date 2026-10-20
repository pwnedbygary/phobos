//The GE driver (sceGe_user): what the PSP's operating system offers programs for the graphics chip (ge/ge.hpp). A
//program hands it display lists, and the driver queues them, gives each to the GE in turn, and deals with whatever
//makes one stop:
//  - the stall address: the program hasn't written further yet. When it moves the address on
//    (sceGeListUpdateStallAddr), the GE carries on.
//  - FINISH then END: the list is done. The program's finish callback runs, and the next list starts.
//  - SIGNAL then END: the driver does what the signal's kind (its bits 16-23) asks: call the program's signal
//    callback, jump, call or return elsewhere in memory, pause... and the GE goes on.
//Callbacks come in pairs, signal and finish, each with an argument, registered with sceGeSetCallback (16 at most).
//They're called as interrupt handlers (interrupts.cpp) with the FINISH's or SIGNAL's low 16 bits, the argument, and
//where the list had got to.
//
//A program sees how its lists are doing with sceGeListSync and sceGeDrawSync, and can wait there for them to finish.
//The GE runs a list as far as it can as soon as it's queued or its stall address moves, every pixel drawn there and
//then; but the program hears that a list is done only once a PSP's GE would have drawn it (geTime()), as the GE's
//interrupt for its FINISH is taken (geInterrupt()). With the PSP's timing a SIGNAL's interrupt waits too, the GE
//stopped at it till a PSP's GE's front end would have got there: long before the drawing ahead of it is done (round
//6's time-signals), but no further ahead than the primitives it may hand on (GE::Cost::Queue), so near a long
//list's end: Army of Two's flip, its list's last SIGNAL, came 0.27 ms before the list's FINISH, 30 ms after it was
//queued (docs/psp-core.md, part 106).
//
//What each call does follows uOFW's reading of the PSP's own driver (ge.c, stall.S); sceGeBreak, what callbacks see,
//lists queued twice and the older SDKs' ways (geOldSdk()) follow what pspautotests' gpu/ge and gpu/signals recorded on
//a PSP. Not yet: the debugger's breakpoints, SIGNALs that patch texture or CLUT addresses, and a SIGNAL call's stack
//in use by another list refused.

auto Kernel::geIndex(u32 id) const -> s32 {
  u32 index = id - GeListIDs;
  return index < 64 ? s32(index) : -1;
}

//The GE takes up a list where it left off (or at its start).
auto Kernel::geLoad(GeList& list) -> void {
  ge.list = list.registers;
  geRestoreBase(list.base);
}

//BASE's word as the driver saved it, put back the way the PSP's driver does it: by having the GE run the word as a
//command (uOFW's _sceGeSetInternalReg). A word of 0, a list that never ran, is a NOP: BASE stays as the last list
//left it.
auto Kernel::geRestoreBase(u32 word) -> void {
  if(word >> 24 != GE::Base) return;
  ge.commands[GE::Base] = word;
  if(ge.thread) ge.thread->word(word, ~0u);  //(the render GE's too, from no list: ge/thread.hpp)
}

//Whether the GE waits at a list's stall address: the list's own, not the GE's. They differ for a paused list the GE
//still has (on its way to a PAUSE's FINISH, or an older SDK's in its suspending signal's callback): a stall address
//moved then stays with the list, which reads as drawing, the GE waiting where it was to stop (gpu/ge/queue2,
//gpu/signals/handlercalls). Nor at a FINISH whose interrupt is still to come, though the GE has stopped just past it,
//where a list GU ends has its stall address: a PSP's GE would be drawing the list still, at least till its time
//(geInterrupt(); unmeasured).
auto Kernel::geAtStall(u32 index) const -> bool {
  u32 stall = geLists[index].registers.stall;
  return geRunning == s32(index) && stall && ge.list.address == stall && !geFinishDue && !geSignalDue;
}

//The GE runs its list as far as it goes: to the stall address, or through FINISHes and SIGNALs (dealt with as the
//driver does) to the end of the queue. A million commands a frame, however many goes it has and however often they
//stop it: past that, the list is left to go on in the next frame (each vertical blank gives it another million), so
//no list (one that jumps back to itself, or signals over and over) holds the frame up for long. (The budget counts
//commands, not what they draw: one that draws big primitives over and over can still make the frame slow to end.)
//One the GE gave up on (a bare END, a fault, a SIGNAL the driver can't do) stays in the queue as running, and whoever
//waits for it waits on: the PSP's driver never hears the end of such a list either.
auto Kernel::geRun() -> void {
  geBusy = false;
  if(geRunning >= 0 && !geSuspended) {  //(an older SDK's list, paused for its suspending signal's callback: geOldSdk())
    auto& running = geLists[geRunning];
    if(running.state == GeList::State::Paused && !running.pausing) running.state = GeList::State::Running;
  }
  while(geRunning >= 0 && !geSuspended && !geFinishDue && !geSignalDue) {
    if(!geLeft) {
      geBusy = true;
      return;
    }
    u64 ran = 0;
    ge.owedPixels = ge.owedPrimitives = 0;
    ge.pspTiming = gePspTiming;
    if(gePspTiming) geResume();
    auto stop = ge.run(geLeft, ran);
    ge.wordsRun = nullptr;
    geLeft -= ran;
    geCommands += ran;
    geTime(ran);
    switch(stop) {
    case GE::Stop::Stalled:  return;
    case GE::Stop::Busy:     geBusy = true; return;
    case GE::Stop::Finished: geFinishDue = true; return;
    case GE::Stop::Signaled:
      if(gePspTiming && !geReachReached()) {  //(waiting there for its time, as at a FINISH: geInterrupt())
        geSignalDue = true;
        return;
      }
      geSignaled();
      break;
    case GE::Stop::Ended:
      note("a display list ENDed without a FINISH or SIGNAL before it: the GE stopped there");
      geRunning = -1;
      return;
    case GE::Stop::Faulted:  //the GE said why
      geRunning = -1;
      return;
    }
  }
}

//What the GE has just been given (commands words of the list, and what its primitives counted: GE::owePrimitive())
//takes a PSP's GE at least this long, in clocks of the bus, which is the GE's (half the PLL's: 95, 111, 133 or 166
//MHz, power.cpp). The GE takes it up once it's done with what came before, or now. Two ways, by the option "GE
//Timing" (gePspTiming):
//- "Fast", the opt-in: Sony's figures for it (its Hot Chips 2004 paper), at 166 MHz 664 million pixels a second
//  (four a clock) and 35 million polygons a second through its geometry and its setup. Those are its peaks: a clock
//  for every four pixels, or 166/35 clocks for every primitive, whichever is longer (the two may go on at once), and
//  nothing for what no figure gives (the commands, primitives dropped before they're set up, block transfers). A
//  PSP's GE takes 4 to 12 times this over games' lists (docs/psp-core.md, part 103).
//- "PSP" (the default): as the owner's PSP took it (GE::Cost: rounds 6, 8, 9 and 10's timed lists, docs/psp-core.md
//  part 106), its two ends going on at once as far as the queue between them lets them (GE::Pipe), from one run of a
//  list to the next (geResume()); a list's own time as it starts and the driver's CPU time besides (geOwe(),
//  geDriven()).
//  Its front end has got through it (geReachAt, a SIGNAL's time) with its own work done: no further ahead of the
//  drawing than the queue holds.
auto Kernel::geTime(u64 commands) -> void {
  u64 pixels = ge.owedPixels, primitives = ge.owedPrimitives;
  ge.owedPixels = ge.owedPrimitives = 0;
  if(ge.thread) {  //(counted there: geFold())
    geRuns.push_back({ge.thread->runsHanded(), cycles, std::clamp<u32>(powerState.bus, 1, 166), 0, 0, gePipeAt,
                      commands});
    if(gePspTiming) geCacheCounts(geRuns.back().caches);  //(for the fold's geSeeCaches(), as the run ends)
    if(geProfile) [[unlikely]] geProfiledRun();
    else if(geRuns.size() >= GeThread::RunsKept) geFoldKept();
    return;
  }
  if(gePspTiming) {
    ge.pipeStop(commands);
    u64 bus = std::clamp<u32>(powerState.bus, 1, 166);
    u64 before = geDoneAt;
    geDoneAt = gePipeAt + geCycles(ge.pipeTime(), bus);
    geReachAt = gePipeAt + geCycles(ge.pipe.front, bus);
    if(before <= cycles && geDoneAt > cycles) geSeeCaches();  //(a stalled list going on)
    u64 from = std::min(geDoneAt, std::max(before, cycles));
    if(geProfile) [[unlikely]] geProfileWork(pixels, primitives, geDoneAt - from);
    return;
  }
  if(!pixels && !primitives) return;
  u64 bus = std::clamp<u32>(powerState.bus, 1, 166);
  u64 owed = std::max(pixels * 35, primitives * 664) * (CPUFrequency / 1'000'000) / (140 * bus);
  geDoneAt = std::max(geDoneAt, cycles) + owed;
  if(geProfile) [[unlikely]] geProfileWork(pixels, primitives, owed);
}

//With the PSP's timing, a run of the list the GE has takes the GE up as its last run left it: the queue between its
//two ends, the back end's drawing going on (at a stall address, or past a SIGNAL), the front end's lead; its times
//moved on to now, what was done by then gone (GE::pipeShift()).
auto Kernel::geResume() -> void {
  if(ge.thread && !geStretches.empty()) geFold(true);  //(gePipeAt as the stretches waiting push it: geContend())
  if(cycles <= gePipeAt) return;
  u64 bus = std::clamp<u32>(powerState.bus, 1, 166);
  f64 clocks = f64(cycles - gePipeAt) * bus / (CPUFrequency / 1'000'000);
  if(ge.thread) ge.thread->resume(clocks);  //(the GE thread's pipeline, moved on as the run's record starts)
  else ge.pipeShift(clocks);
  gePipeAt = cycles;
}

//With the PSP's timing, the CPU beside the GE (GE::Cost::Contention): cycles from to to passed, the CPU running
//`instructions` of its own in them (none waiting), its caches moving the lines they counted since last seen. A stretch
//begun while the GE is busy (geDoneAt ahead) adds to its busy window the cycles that passed while it was, and its
//instructions and lines (with the GE done part way, their share that fell before, in whole counts); the window owes the
//GE the push geContendDue() works out from its sums, and what it hasn't pushed of that pushes the GE's pipeline back
//now (gePipeAt, which its times count from, so the list's next run keeps it; geDoneAt with it, and a due SIGNAL's
//geReachAt). So a burst of lines costs the GE its share once, not again in every cycle of the window after it. The
//window starts afresh when the GE is idle: the CPU's work before a list costs it nothing (the counts seen again as the
//GE gets busy: geSeeCaches()).
auto Kernel::geContend(u64 from, u64 to, u64 instructions) -> void {
  if(ge.thread) [[unlikely]] return geContendThreaded(from, to, instructions);
  if(geDoneAt <= from) {
    geWindow = {};
    return;
  }
  u64 counts[4];
  geCacheCounts(counts);
  geContendNow(from, to, instructions, counts, geSignalDue, powerState.cpu);
}

//With the GE on its thread, a stretch ended while runs handed over aren't folded yet: kept with the caches' counts as
//it ended, whether a SIGNAL was due then and the CPU's clock then (GeStretch), and taken as geFold() folds the last run
//handed over before it, from the times the fold leaves and the list's clocks by kind the GE thread gives with each run,
//as the synchronous GE takes it after that run's geTime(): all it takes is its own or the fold's, so it comes out the
//same whenever the fold comes, the clock set since or not. Else taken now, with the clocks the last run folded left.
//No more than GeStretchesKept wait: past that, the oldest are folded with the run they wait on.
NALL_NOINLINE auto Kernel::geContendThreaded(u64 from, u64 to, u64 instructions) -> void {
  GeStretch stretch{from, to, instructions};
  geCacheCounts(stretch.caches);
  stretch.signal = geSignalDue;
  stretch.cpu = powerState.cpu;
  stretch.number = geStretchesMade++;
  if(geRuns.empty()) return geContendNow(from, to, instructions, stretch.caches, stretch.signal, stretch.cpu);
  stretch.run = geRuns.back().run, geStretches.push_back(stretch);
  if(geStretches.size() >= GeStretchesKept) geFold(true, geStretches.front().run);
}

//(geContend()'s, with the caches' counts as the stretch ended, whether a SIGNAL was due then and the CPU's clock then:
//with the GE on its thread, the list's clocks by kind those the last run folded left, geFold())
auto Kernel::geContendNow(u64 from, u64 to, u64 instructions, const u64 (&counts)[4], bool signal, u32 cpu) const
  -> void {
  if(geDoneAt <= from) {
    geWindow = {};
    return;
  }
  using Cost = GE::Cost;
  u64 fills = counts[0] - geCacheSeen[0] + counts[3] - geCacheSeen[3];
  u64 writebacks = counts[1] - geCacheSeen[1], uncached = counts[2] - geCacheSeen[2];
  for(u32 n : range(4)) geCacheSeen[n] = counts[n];
  u64 overlap = std::min(geDoneAt, to) - from;
  if(overlap < to - from) {  //(the GE done part way: the share of the stretch's work that fell before, spread evenly)
    f64 share = f64(overlap) / f64(to - from);
    auto part = [&](u64 count) { return u64(f64(count) * share); };
    instructions = part(instructions), fills = part(fills), writebacks = part(writebacks), uncached = part(uncached);
  }
  auto& w = geWindow;
  w.passed += overlap, w.instructions += instructions;
  w.fills += fills, w.writebacks += writebacks, w.uncached += uncached;
  geBusyCycles += f64(overlap);
  geBusyLines += f64(fills) + Cost::WritebackLine * f64(writebacks) + Cost::UncachedLine * f64(uncached);
  u64 due = geContendDue(w, cpu);
  if(due <= w.pushed) return;
  u64 pushed = due - w.pushed;
  w.pushed = due;
  gePipeAt += pushed, geDoneAt += pushed;
  if(signal && geReachAt > from) geReachAt += pushed;  //(else geTime() works it out from gePipeAt again)
}

//The cycles a busy window pushes the GE back by, all told: its lines (each read in one, each written back
//WritebackLine of one, each uncached access UncachedLine) over its time as a PSP's CPU at cpu MHz would take it (its
//instructions' and stalls' cycles, or the cycles passed if longer: geContendBound()), as a share u of a thread
//streaming RAM flat out's, make the list's work take 1 + sum(Contention[kind] x its share of the list's clocks) x u as
//long: passed x slow / (1 + slow), in whole cycles. (cpu: the clock as the stretch adding to the window ended.)
auto Kernel::geContendDue(const GeWindow& w, u32 cpu) const -> u64 {
  using Cost = GE::Cost;
  f64 lines = f64(w.fills) + Cost::WritebackLine * f64(w.writebacks) + Cost::UncachedLine * f64(w.uncached);
  if(!(lines > 0) || !(ge.allClocks > 0)) return 0;
  f64 counted = f64(CPUFrequency) / (std::clamp<u32>(cpu, 1, 333) * 1e6);  //(cycles here to a CPU cycle)
  f64 stalls = Cost::FillCycles * f64(w.fills) + Cost::WritebackCycles * f64(w.writebacks) +
               Cost::UncachedCycles * f64(w.uncached);
  f64 time = std::max(f64(w.passed), (Cost::InstructionCycles * f64(w.instructions) + stalls) * counted);
  f64 u = lines * counted / (time * Cost::StreamLines), slow = 0;
  for(u32 kind : range(4)) slow += Cost::Contention[kind] * ge.ramClocks[kind] / ge.allClocks * u;
  return u64(f64(w.passed) * slow / (1 + slow));
}

//Whether a busy window's time is the PSP's CPU's for its instructions and stalls (longer than the cycles passed) rather
//than the cycles passed: the push geContendDue() works out has a kink where one gives way to the other. (At the CPU's
//clock now: spinSkip()'s, with no stretch waiting.)
auto Kernel::geContendBound(const GeWindow& w) const -> bool {
  using Cost = GE::Cost;
  f64 counted = f64(CPUFrequency) / (std::clamp<u32>(powerState.cpu, 1, 333) * 1e6);
  f64 stalls = Cost::FillCycles * f64(w.fills) + Cost::WritebackCycles * f64(w.writebacks) +
               Cost::UncachedCycles * f64(w.uncached);
  return (Cost::InstructionCycles * f64(w.instructions) + stalls) * counted > f64(w.passed);
}

//The CPU's caches' counts as they are now, seen by the GE's busy window (geContend()): as the GE gets busy (geOwe(),
//geTime()), with no instruction run since the last stretch was counted, so its window counts the lines from there.
auto Kernel::geSeeCaches() -> void {
  geCacheCounts(geCacheSeen);
}

//The CPU's caches' counts now, in geCacheSeen's order: the data cache's lines read in, written back and uncached
//accesses, the instruction cache's lines read in.
auto Kernel::geCacheCounts(u64 (&counts)[4]) const -> void {
  auto& data = cpu.dataCache;
  counts[0] = data.fills, counts[1] = data.writebacks, counts[2] = data.uncached;
  counts[3] = cpu.instructionCache.fills;
}

//The driver's own work in a call, the CPU's (GE::Cost::EnqueueCycles, StallCycles), which the call takes before it
//returns, the GE drawing meanwhile, so a list done by then has its finish callback before the program goes on.
//(docs/psp-core.md, part 76; the driver's other calls take nothing, unmeasured.)
auto Kernel::geDriven(u64 cpuCycles) -> void {
  if(!gePspTiming) return;
  cycles += cpuCycles * CPUFrequency / (u64(std::clamp<u32>(powerState.cpu, 1, 333)) * 1'000'000);
}

//A list starting, with the PSP's timing: the GE's own time (clocks: GE::Cost::List) before its front end reads it, the
//list before done with (its FINISH waited for all of its drawing).
auto Kernel::geOwe(f64 clocks) -> void {
  if(!gePspTiming || !(clocks > 0)) return;
  if(ge.thread) geFold(true);  //(geDoneAt as the runs handed over left it)
  u64 bus = std::clamp<u32>(powerState.bus, 1, 166);
  u64 owed = geCycles(clocks, bus);
  if(geDoneAt <= cycles) geSeeCaches();
  gePipeAt = std::max(geDoneAt, cycles);
  if(ge.thread) {  //(the GE thread's pipeline begun afresh, the list's clocks by kind with it: geFold())
    ge.thread->owe(clocks);
    for(auto& kind : ge.ramClocks) kind = 0;
    ge.allClocks = 0;
  } else {
    ge.pipeStart(), ge.pipe.front = clocks;
  }
  geDoneAt = gePipeAt + owed;
  if(geProfile) [[unlikely]] geProfileWork(0, 0, owed);
}

//(the GE on its thread) The oldest runs folded while the thread keeps no more runs' counts (GeThread::RunsKept) than
//are waiting to be folded. Out of geTime(), which every run takes.
NALL_NOINLINE auto Kernel::geFoldKept() const -> void {
  while(geRuns.size() >= GeThread::RunsKept) geFold(true, geRuns.front().run);
}

//(the GE on its thread, a profile recording) The run just handed over counted at once, its charge recorded where the
//synchronous GE records it (geProfileWork()), as it is and in the same place among the profile's records; the runs
//handed over before the profile started folded first, unrecorded, as then.
NALL_NOINLINE auto Kernel::geProfiledRun() -> void {
  GeRun run = geRuns.back();
  if(gePspTiming) {  //(its charge as geTime()'s: from when the GE takes it up to when it's done)
    geFold(true, run.run - 1);
    u64 before = geDoneAt;
    geFold(true);
    GeThread::Owed owed;
    ge.thread->owedOf(run.run, false, owed);
    geProfileWork(owed.pixels, owed.primitives, geDoneAt - std::min(geDoneAt, std::max(before, run.cycles)));
    return;
  }
  geFold(true);
  GeThread::Owed owed;
  ge.thread->owedOf(run.run, false, owed);
  if(owed.pixels || owed.primitives) {
    geProfileWork(owed.pixels, owed.primitives, geOwed(owed.pixels, owed.primitives, run.bus));
  }
}

auto Kernel::geOwed(u64 pixels, u64 primitives, u32 bus) -> u64 {
  return std::max(pixels * 35, primitives * 664) * (CPUFrequency / 1'000'000) / (140 * u64(bus));
}

auto Kernel::geCycles(f64 clocks, u64 bus) -> u64 {
  return u64(clocks * (CPUFrequency / 1'000'000) / bus);
}

//The runs the GE thread has ended (with wait, every one through the run numbered through, waiting for it) folded into
//geDoneAt in turn, as geTime() folds each synchronously: with the PSP's timing, its times from its pipeline's clocks.
auto Kernel::geFold(bool wait, u64 through) const -> void {
  while(!geRuns.empty() && geRuns.front().run <= through) {
    auto& run = geRuns.front();
    GeThread::Owed owed;
    if(!ge.thread || !ge.thread->owedOf(run.run, wait, owed)) return;
    if(gePspTiming) {
      u64 before = geDoneAt;
      geDoneAt = run.pipeAt + geCycles(owed.time, run.bus);
      geReachAt = run.pipeAt + geCycles(owed.front, run.bus);
      if(before <= run.cycles && geDoneAt > run.cycles) {  //(a stalled list going on: geTime()'s, as the run ended)
        for(u32 n : range(4)) geCacheSeen[n] = run.caches[n];
      }
    } else if(owed.pixels || owed.primitives) {
      geDoneAt = std::max(geDoneAt, run.cycles) + geOwed(owed.pixels, owed.primitives, run.bus);
    }
    geFoldedRun = run.run;
    GeRun folded = run;
    folded.pixels = owed.pixels, folded.primitives = owed.primitives, folded.doneAt = geDoneAt, folded.reachAt = geReachAt;
    if(!geLimits.empty()) geFolded.push_back(folded);
    if constexpr(GE::Cost::Contended) if(gePspTiming) {  //(the stretches after it, from these times, each kept too)
      for(u32 kind : range(4)) ge.ramClocks[kind] = owed.ramClocks[kind];  //(the kernel's GE draws nothing meanwhile)
      ge.allClocks = owed.allClocks;
      while(!geStretches.empty() && geStretches.front().run == run.run) {
        auto stretch = geStretches.front();
        geStretches.pop_front();
        geContendNow(stretch.from, stretch.to, stretch.instructions, stretch.caches, stretch.signal, stretch.cpu);
        folded.doneAt = geDoneAt, folded.reachAt = geReachAt, folded.stretch = stretch.number;
        if(!geLimits.empty()) geFolded.push_back(folded);
      }
    }
    geRuns.pop_front();
  }
}

//At most what geDoneAt will be once every run handed over is folded: the run under way's counts so far added as it
//would add its own (they only grow, and so does what they owe), the runs after it left out (they only add more). With
//the PSP's timing, the last run's: from its pipeline's clocks so far where the GE thread has got to it, and at least
//its words' work (half of it: past any rounding of the clocks summed), which no run's time comes in under.
auto Kernel::geBound() const -> u64 {
  geFold(false);
  if(geRuns.empty()) return geDoneAt;
  if(gePspTiming) {
    auto& run = geRuns.back();
    GeThread::Owed owed;
    f64 clocks = f64(run.words) * GE::Cost::Command * 0.5;
    if(ge.thread->owedSoFar(run.run, owed)) clocks = std::max(clocks, owed.time);
    return run.pipeAt + geCycles(clocks, run.bus);
  }
  auto& run = geRuns.front();
  GeThread::Owed owed;
  if(!ge.thread->owedSoFar(run.run, owed) || (!owed.pixels && !owed.primitives)) return geDoneAt;
  return std::max(geDoneAt, run.cycles) + geOwed(owed.pixels, owed.primitives, run.bus);
}

//At most what geReachAt will be once every run handed over is folded, as geBound() (with the PSP's timing alone).
auto Kernel::geReachBound() const -> u64 {
  geFold(false);
  if(geRuns.empty()) return geReachAt;
  auto& run = geRuns.back();
  GeThread::Owed owed;
  f64 clocks = f64(run.words) * GE::Cost::Command * 0.5;
  if(ge.thread->owedSoFar(run.run, owed)) clocks = std::max(clocks, owed.front);
  return run.pipeAt + geCycles(clocks, run.bus);
}

//Whether the GE's time has come (cycles at or past geDoneAt): at once where the bound is past cycles, else once the GE
//thread has counted every run handed over (a wait for it: the GE thread is then what the emulation thread waits on).
auto Kernel::geReached() -> bool {
  if(geRuns.empty()) return cycles >= geDoneAt;  //(none handed over: always so with the thread off)
  if(geBound() > cycles) return false;
  geFold(true);
  return cycles >= geDoneAt;
}

//Whether the GE's front end has got through what it was given (cycles at or past geReachAt), as geReached().
auto Kernel::geReachReached() -> bool {
  if(geRuns.empty()) return cycles >= geReachAt;
  if(geReachBound() > cycles) return false;
  geFold(true);
  return cycles >= geReachAt;
}

//geDoneAt about to be set afresh: the runs before don't count, and the go's limits worked out from the bound are
//worked out exactly first, as they stood.
auto Kernel::geClearRuns() -> void {
  for(auto& limit : geLimits) goExact = std::min(goExact, geExactLimit(limit));
  if(!geStretches.empty()) geFold(true);  //(what the stretches waiting leave of the busy window, as the synchronous GE)
  geLimits.clear(), geFolded.clear(), geRuns.clear(), geStretches.clear();
  geFoldedRun = ge.thread ? ge.thread->runsHanded() : 0;
}

//A limit untilNextEvent() worked out from the bound (GeLimit), worked out exactly: geDoneAt as it was then, with the
//runs handed over by then folded on it (their counts waited for), in place of the bound.
auto Kernel::geExactLimit(const GeLimit& limit) -> u64 {
  geFold(true, limit.run);
  u64 at = limit.foldedAt;
  for(auto& run : geFolded) {
    if(run.run <= limit.foldedRun) continue;
    if(gePspTiming) {  //(the times the runs and stretches before it left, in turn: none after it)
      if(run.stretch == ~0ull ? run.run > limit.run : run.stretch >= limit.stretches) break;
      at = limit.signal ? run.reachAt : run.doneAt;
      continue;
    }
    if(run.run > limit.run || run.stretch != ~0ull || (!run.pixels && !run.primitives)) continue;
    at = std::max(at, run.cycles) + geOwed(run.pixels, run.primitives, run.bus);
  }
  u64 next = limit.others;
  if(limit.due && at > limit.cycles) next = std::min(next, at);
  u64 until = next > limit.cycles ? next - limit.cycles : 1;
  return limit.before + std::min(limit.cap, std::max<u64>(1, until));
}

//The FINISH of the list the GE runs, met (geFinishDue): the GE has stopped at its END and raised its interrupt, which
//the driver deals with (geFinished()) once the GE's time has come (geDoneAt) and an interrupt may be taken: with
//interrupts on, no call into the program running, and none of the GE's waiting its turn (the PSP takes the GE's next
//interrupt only once the last one's handler has returned). With the PSP's timing a SIGNAL met waits the same way
//(geSignalDue) for the GE's front end to have got there (geReachAt: long before the drawing ahead of it is done but
//in a long list), then the driver does what it says (geSignaled()). Till then the list reads as drawing, as
//pspautotests recorded: a SIGNAL's callback that lets the GE go on finds its list drawing still, its FINISH run already
//(gpu/signals/continue, simple), and with interrupts held off a list stays drawing however long the CPU spins
//(gpu/ge/intrsuspend). It's taken at the end of a system function, or as the kernel's loop goes round; then the
//next list runs. Returns whether one was taken.
auto Kernel::geInterrupt() -> bool {
  bool taken = false;
  while((geFinishDue && !interrupting && interruptsEnabled && geReached()) ||
        (geSignalDue && !interrupting && interruptsEnabled && geReachReached())) {
    if(std::any_of(calls.begin(), calls.end(), [](const Call& call) { return call.kind == Call::Ge; })) break;
    if(geSignalDue) geSignalDue = false, geSignaled();
    else geFinishDue = false, geFinished();
    geRun();
    taken = true;
  }
  return taken;
}

//A list's callback: its finish or signal function, with id, its argument and where the list had got to (0 for an
//older SDK's: geOldSdk()). With suspends, the GE waits until it returns. Returns whether there was one to call.
auto Kernel::geCall(GeList& list, bool finish, u32 id, bool suspends) -> bool {
  if(list.callback < 0 || list.callback >= 16 || !geCallbacks[list.callback].used) return false;
  auto& callback = geCallbacks[list.callback];
  u32 function = finish ? callback.finishFunction : callback.signalFunction;
  if(!function) return false;
  if(suspends) geSuspended = true;
  queueCall(function, callback.gp, id, finish ? callback.finishArgument : callback.signalArgument,
            geOldSdk() ? 0 : ge.list.address, suspends);
  if(geProfile) [[unlikely]] calls.back().profile = geProfileCallback(finish, list, id);
  return true;
}

//Programs built with SDKs up to 2.00.10, or saying none, meet the driver's older ways, as pspautotests' gpu/signals
//recorded. A list reads as paused while its suspending SIGNAL's callback runs, the GE still on it, so the callback
//can't break it off (BUSY), and its stall address moved there is the list's alone: the GE goes on to where it was to
//stop, the list reading as drawing there (handlercalls with 0x01000010, suspend saying none; handlercalls puts the cut
//above 0x02000010). Callbacks aren't told where the list had got to (saying none; pause2 tells it at 0x02080000), and
//sceGeContinue on a list drawing says -1 (saying none; ALREADY at 0x06060010): taken to change at the same cut.
auto Kernel::geOldSdk() const -> bool {
  return sdkVersion <= 0x0200'0010;
}

//Out of the queue and free for the next list; whoever waits for it to finish is told it has. A list that isn't in
//the queue is left as it is.
auto Kernel::geRemove(u32 index) -> void {
  auto at = std::find(geQueue.begin(), geQueue.end(), index);
  if(at == geQueue.end()) return;
  geQueue.erase(at);
  geFree.push_back(index);
  geWake(Wait::GeList, index);
}

auto Kernel::geWake(Wait wait, u32 index) -> void {
  bool woke = false;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != wait || thread->waitID != index) continue;
    ready(*thread, 0);
    woke = true;
  }
  if(wait == Wait::GeDraw && woke) geDrawSynced();
  if(woke) reschedule();
}

//sceGeDrawSync's return: the lists that finished are forgotten.
auto Kernel::geDrawSynced() -> void {
  for(auto& list : geLists) {
    if(list.state != GeList::State::Completed) continue;
    list.state = GeList::State::None;
    list.started = false;
  }
}

//An END after a FINISH (uOFW's _sceGeFinishInterrupt). After a SYNC signal it only lets the GE go on; after a PAUSE
//signal the list stops here, paused until sceGeContinue, and its signal callback is told. Otherwise the list is done:
//its finish callback runs while the GE waits (so it can change what the next list will meet); then geEnded().
auto Kernel::geFinished() -> void {
  u32 index = geRunning;
  auto& list = geLists[index];
  if(list.syncing) {
    list.syncing = false;
    return;
  }
  if(list.pausing) {
    list.pausing = false;
    u32 stall = list.registers.stall;  //(the list's own, moved while it was pausing: geAtStall())
    list.registers = ge.list;
    list.registers.stall = stall;
    list.base = ge.commands[GE::Base];
    geRunning = -1;
    geCall(list, false, list.signalID);
    return;
  }
  list.state = GeList::State::Completed;
  if(!list.stack.empty()) note("a display list finished inside a SIGNAL call");
  geFinishing = index;
  if(!geCall(list, true, ge.finishWord & 0xffff, true)) geEnded();
}

//The rest of a list's ending, once its finish callback has returned: the GE's state is put back if it was saved,
//whoever waits for the list is told, and the next list starts.
auto Kernel::geEnded() -> void {
  u32 index = geFinishing;
  geFinishing = -1;
  if(geLists[index].context) geRestoreContext(geLists[index].context);
  geRemove(index);
  geStartNext();
}

//The GE takes the next list in the queue, unless that one's paused; with none left, whoever waits for all drawing to
//finish is told it has.
auto Kernel::geStartNext() -> void {
  geRunning = -1;
  if(geQueue.empty()) return geWake(Wait::GeDraw, 0);
  u32 index = geQueue.front();
  auto& next = geLists[index];
  if(next.state == GeList::State::Paused) return;
  next.state = GeList::State::Running;
  if(next.context && !next.started) geSaveContext(next.context);
  if(!next.started) geOwe(GE::Cost::List);  //(a list's own time as it starts: GE::Cost)
  next.started = true;
  geLoad(next);
  geRunning = index;
}

//An END after a SIGNAL (uOFW's _sceGeListInterrupt). The SIGNAL's bits 16-23 are its kind. Jumps and calls take
//their address from the SIGNAL's low 16 bits (the top half) and the END's (the bottom half): as it is, from the
//SIGNAL's own address, or from the offset (ORIGIN, OFFSET_ADDR). Calls keep where the list was on its own stack, as
//deep as it allows, and the list continues there at a return.
auto Kernel::geSignaled() -> void {
  auto& list = geLists[geRunning];
  u32 signal = ge.signalWord, end = ge.endWord, kind = signal >> 16 & 0xff;
  u32 data = (signal & 0xffff) << 16 | (end & 0xffff);
  u32 at = (ge.list.address - 8) & 0x0fff'ffff;  //the SIGNAL's own address
  auto target = [&](u32 relativeKind, u32 originKind) {
    u32 address = kind == relativeKind ? at + data : kind == originKind ? ge.list.offset + data : data;
    if(address & 3) note("a SIGNAL jumped or called to an address not on a word");
    return address & 0x0fff'fffc;
  };
  switch(kind) {
  case 0x01:  //the callback runs, then the GE goes on
    if(geCall(list, false, signal & 0xffff, true) && geOldSdk()) list.state = GeList::State::Paused;
    return;
  case 0x02:  //the GE goes on, and the callback runs; but should another callback wait its turn or run still, the GE
              //waits for this one too: the PSP takes the GE's next interrupt only once the last one's handler has
              //returned, so the callbacks can't pile up
    geCall(list, false, signal & 0xffff, interrupting || !calls.empty());
    return;
  case 0x03:  //paused at the next FINISH
    list.state = GeList::State::Paused;
    list.pausing = true;
    list.signalID = signal & 0xffff;  //the PSP's driver keeps 16 bits of it
    return;
  case 0x08:  //the next FINISH doesn't end the list
    list.syncing = true;
    return;
  case 0x10: case 0x13: case 0x15:  //jump
    ge.list.address = target(0x13, 0x15);
    return;
  case 0x11: case 0x14: case 0x16:  //call
    if(list.stack.size() >= list.stackLimit) {
      note("a SIGNAL called deeper than its list allows: the GE stopped there");
      geRunning = -1;
      return;
    }
    list.stack.push_back({ge.list, ge.commands[GE::Base]});
    ge.list.address = target(0x14, 0x16);
    ge.list.depth = 0;  //the called list starts with the GE's own CALLs unused
    return;
  case 0x12:  //return
    if(list.stack.empty()) {
      note("a SIGNAL returned without a SIGNAL call to return from: the GE stopped there");
      geRunning = -1;
      return;
    }
    {
      u32 stall = ge.list.stall;
      ge.list = list.stack.back().registers;
      ge.list.stall = stall;
      geRestoreBase(list.stack.back().base);
      list.stack.pop_back();
    }
    return;
  case 0xf0: case 0xff:  //breakpoints, for the PSP's debugger
    return;
  }
  if(kind >= 0x20 && kind <= 0x38) return note("SIGNALs that patch texture or CLUT addresses aren't emulated yet");
  note("a SIGNAL of a kind the driver doesn't know: the GE stopped there");
  geRunning = -1;
}

//sceGeSaveContext's buffer (2 KiB of the program's): Phobos's own layout, which programs don't look inside. Each
//command's last word, the matrices and where their next element goes, the list registers, and the vertex and index
//addresses.
auto Kernel::geSaveContext(u32 address) -> void {
  u32 at = address;
  auto put = [&](u32 value) { memory.write(4, at, value); at += 4; };
  for(u32 word : ge.commands) put(word);
  for(u32 element : ge.bones) put(element);
  for(u32 element : ge.world) put(element);
  for(u32 element : ge.view) put(element);
  for(u32 element : ge.projection) put(element);
  for(u32 element : ge.textureMatrix) put(element);
  for(u32 index : {ge.boneIndex, ge.worldIndex, ge.viewIndex, ge.projectionIndex, ge.textureIndex}) put(index);
  put(ge.list.address); put(ge.list.stall); put(ge.list.offset); put(ge.list.depth);
  for(u32 n : {0, 1}) put(ge.list.returnAddress[n]), put(ge.list.returnOffset[n]);
  put(ge.vertexAddress); put(ge.indexAddress);
}

auto Kernel::geRestoreContext(u32 address) -> void {
  u32 at = address;
  auto get = [&]() { u32 value = memory.read(4, at); at += 4; return value; };
  for(u32& word : ge.commands) word = get();
  for(u32& element : ge.bones) element = get();
  for(u32& element : ge.world) element = get();
  for(u32& element : ge.view) element = get();
  for(u32& element : ge.projection) element = get();
  for(u32& element : ge.textureMatrix) element = get();
  for(u32* index : {&ge.boneIndex, &ge.worldIndex, &ge.viewIndex, &ge.projectionIndex, &ge.textureIndex}) *index = get();
  //the GE has room for two CALLs: a buffer the program has written over can't make it more
  ge.list.address = get(); ge.list.stall = get(); ge.list.offset = get(); ge.list.depth = std::min(get(), 2u);
  for(u32 n : {0, 1}) ge.list.returnAddress[n] = get(), ge.list.returnOffset[n] = get();
  ge.vertexAddress = get(); ge.indexAddress = get();
  if(ge.thread) ge.handState();  //(the render GE's too: ge/thread.hpp)
}

//The GE's memory: VRAM, 2 MiB at 0x04000000.
auto Kernel::sceGeEdramGetAddr() -> void { result(Memory::VRAMBase); }
auto Kernel::sceGeEdramGetSize() -> void { result(Memory::VRAMSize); }

//(width): how VRAM's swizzled copies lay its bytes out (pspsdk's pspge.h: 512, 1024, 2048 or 4096). As
//pspautotests' gpu/ge/edram recorded, 0, 0x200, 0x400, 0x800 and 0x1000 are taken, and the width there was is
//returned (0x400 the first time: what the PSP starts with; 0 after a 0); anything else is refused (INVALID_VALUE).
//Only the number is kept: the swizzled copies keep the layout they have at the starting width (memory.cpp's
//vramOffset()), whatever is set; gpu/ge/edramswizzle's patterns for the other widths would show what they become.
auto Kernel::sceGeEdramSetAddrTranslation() -> void {
  u32 width = arg(0);
  if(width && width != 0x200 && width != 0x400 && width != 0x800 && width != 0x1000) {
    return result(ErrorInvalidValue);
  }
  result(geTranslation);
  geTranslation = width;
}

//(list, stall address (0: none), callbacks (sceGeSetCallback's number; negative for none), options): the list goes at
//the queue's end, and the GE starts on it at once if it's free. Returns the list's ID. The options (PspGeListArgs, if
//given): their size, where to save the GE's state, and how deep SIGNAL calls may go (32 if not said).
auto Kernel::sceGeListEnQueue() -> void {
  ProfiledCall profile{*this, GeProfile::EnQueue};
  geEnqueue(false);
}

//As above, but at the queue's front, paused: it runs when the program calls sceGeContinue. Only while the front list
//is paused itself, and not still the GE's, or there's none, so as not to cut in on a list the GE has.
auto Kernel::sceGeListEnQueueHead() -> void {
  ProfiledCall profile{*this, GeProfile::EnQueueHead};
  geEnqueue(true);
}

auto Kernel::geEnqueue(bool head) -> void {
  u32 address = arg(0), stall = arg(1), options = arg(3), context = 0, stackLimit = 32;
  if(options) {
    if(options & 3) return result(ErrorInvalidPointer);
    context = memory.read(4, options + 4);
    if(memory.read(4, options) >= 16) {
      stackLimit = memory.read(4, options + 8);
      if(stackLimit >= 256) return result(ErrorInvalidSize);
    }
  }
  if((address | stall | context) & 3) return result(ErrorInvalidPointer);
  //Built with SDK 2.00 or later, a program can't queue a list at an address the queue holds already: where a list
  //starts, or where it was broken off or paused (where it would go on), its finished list still queued while its
  //finish callback runs; not where one is now (pspautotests' gpu/ge/queue: 0x01000010 queues it, 0x02000000 doesn't).
  if(sdkVersion >= 0x0200'0000) {
    for(u32 index : geQueue) {
      if(geLists[index].registers.address == (address & 0x0fff'ffff)) return result(ErrorBusy);
    }
  }
  if(geFree.empty()) return result(ErrorOutOfMemory);
  //At the head, not ahead of a paused list the GE still has (on its way to a PAUSE's FINISH, or an older SDK's in its
  //suspending signal's callback): put back in the queue under the GE, that one would start over (unmeasured).
  if(head && !geQueue.empty() && (geLists[geQueue.front()].state != GeList::State::Paused
                                  || geRunning == s32(geQueue.front()))) {
    return result(ErrorInvalidValue);
  }
  u32 index = geFree.front();
  geFree.pop_front();
  auto& list = geLists[index];
  list = {};
  list.registers.address = address & 0x0fff'ffff;
  list.registers.stall = stall & 0x0fff'ffff;
  list.callback = std::max(s32(arg(2)), -1);
  list.context = context;
  list.stackLimit = stackLimit;
  result(GeListIDs + index);
  if(head) {  //the paused list goes back to queued: it runs once the new one is done, without a sceGeContinue
    if(!geQueue.empty()) geLists[geQueue.front()].state = GeList::State::Queued;
    list.state = GeList::State::Paused;
    geQueue.push_front(index);
    return geDriven(GE::Cost::EnqueueCycles);
  }
  geQueue.push_back(index);
  if(geQueue.size() > 1) {
    list.state = GeList::State::Queued;
    return geDriven(GE::Cost::EnqueueCycles);
  }
  list.state = GeList::State::Running;
  list.started = true;
  if(context) geSaveContext(context);
  ge.list = list.registers;  //BASE stays as it was
  geRunning = index;
  geOwe(GE::Cost::List);  //(a list's own time as it starts: GE::Cost)
  geRun();
  geDriven(GE::Cost::EnqueueCycles);
}

//Takes a list out of the queue, unless the GE has started it.
auto Kernel::sceGeListDeQueue() -> void {
  ProfiledCall profile{*this, GeProfile::DeQueue};
  s32 index = geIndex(arg(0));
  if(index < 0 || geLists[index].state == GeList::State::None) return result(ErrorInvalidID);
  auto& list = geLists[index];
  if(list.started) return result(ErrorBusy);
  list.state = GeList::State::None;
  geRemove(index);
  result(0);
}

//(list, stall address): the GE may go as far as the new address. A list that has finished can't be moved on.
auto Kernel::sceGeListUpdateStallAddr() -> void {
  ProfiledCall profile{*this, GeProfile::UpdateStallAddr};
  s32 index = geIndex(arg(0));
  if(index < 0) return result(ErrorInvalidID);
  auto& list = geLists[index];
  u32 stall = arg(1) & 0x0fff'ffff;
  switch(list.state) {
  case GeList::State::Running:
    list.registers.stall = stall;
    if(geRunning == index) ge.list.stall = stall;
    result(0);
    geRun();
    return geDriven(GE::Cost::StallCycles);
  case GeList::State::Queued:
  case GeList::State::Paused:
    list.registers.stall = stall;
    result(0);
    return geDriven(GE::Cost::StallCycles);
  case GeList::State::Completed:
    return result(ErrorAlready);
  default:
    return result(ErrorInvalidID);
  }
}

//(list, mode): 0 waits for the list to finish; 1 says how it's doing: 0 finished, 1 queued, 2 drawing, 3 stopped at
//its stall address, 4 paused (or queued again after running). A look, or a call refused, changes nothing (calm):
//games poll the look in a loop while the GE draws (spins.cpp).
auto Kernel::sceGeListSync() -> void {
  ProfiledCall profile{*this, GeProfile::ListSync};
  s32 index = geIndex(arg(0));
  if(index < 0) return calm = true, result(ErrorInvalidID);
  auto& list = geLists[index];
  using State = GeList::State;
  if(arg(1) == 0) {
    if(!mayWait()) return;
    result(0);
    if(list.state == State::Queued || list.state == State::Running || list.state == State::Paused) {
      block(Wait::GeList, index, 0);
    }
    return;
  }
  calm = true;
  result(geListLook(index, arg(1)));
}

//What sceGeListSync says of list index in a mode other than 0 (which waits): its state, or the mode refused. It must
//never read the clock: a spin's skipped turns all get the answer it gives as the skip is made (spins.cpp).
auto Kernel::geListLook(u32 index, u32 mode) const -> u32 {
  if(mode != 1) return ErrorInvalidMode;
  auto& list = geLists[index];
  using State = GeList::State;
  switch(list.state) {
  case State::Queued:    return list.started ? 4 : 1;
  case State::Running:   return geAtStall(index) ? 3 : 2;
  case State::Completed: return 0;
  case State::Paused:    return 4;
  default:               return ErrorInvalidID;
  }
}

//(mode): 0 waits for every list to finish, then forgets the finished ones; 1 says how the GE is doing: 0 nothing to
//do, 2 drawing, 3 stopped at a stall address. A list whose finish callback runs is still queued but done: in it, 0
//if it was the last, 2 with another behind it (pspautotests' gpu/signals and gpu/ge/queue2 recorded both). A look,
//or a mode refused, changes nothing (calm), as with sceGeListSync.
auto Kernel::sceGeDrawSync() -> void {
  ProfiledCall profile{*this, GeProfile::DrawSync};
  if(arg(0) == 0) {
    if(!mayWait()) return;
    result(0);
    if(geQueue.empty()) return geDrawSynced();
    return block(Wait::GeDraw, 0, 0);
  }
  calm = true;
  result(geDrawLook(arg(0)));
}

//What sceGeDrawSync says in a mode other than 0 (which waits): how the GE is doing, or the mode refused. Never reading
//the clock, as geListLook().
auto Kernel::geDrawLook(u32 mode) const -> u32 {
  if(mode != 1) return ErrorInvalidMode;
  for(u32 index : geQueue) {
    if(geLists[index].state != GeList::State::Completed) return geAtStall(index) ? 3 : 2;
  }
  return 0;
}

//(PspGeCallbackData: signal function and argument, finish function and argument): the callbacks' number, for
//enqueueing lists with. They run with the global pointer the program had here.
auto Kernel::sceGeSetCallback() -> void {
  ProfiledCall profile{*this, GeProfile::SetCallback};
  u32 data = arg(0);
  for(u32 id = 0; id < 16; id++) {
    if(geCallbacks[id].used) continue;
    geCallbacks[id] = {true, memory.read(4, data), memory.read(4, data + 4), memory.read(4, data + 8),
                       memory.read(4, data + 12), cpu.ipu.r[28]};
    return result(id);
  }
  result(ErrorOutOfMemory);
}

auto Kernel::sceGeUnsetCallback() -> void {
  ProfiledCall profile{*this, GeProfile::UnsetCallback};
  if(arg(0) >= 16) return result(ErrorInvalidID);
  geCallbacks[arg(0)] = {};
  result(0);
}

//Lets the front list go on if it's paused, unless the GE still has it: its PAUSE signal still on its way to its
//FINISH (gpu/ge/queue2), or an older SDK's list paused for its suspending signal's callback (geOldSdk(); unmeasured:
//gpu/signals/suspend leaves it out, as it crashed the PSP's older driver).
auto Kernel::sceGeContinue() -> void {
  ProfiledCall profile{*this, GeProfile::Continue};
  if(geQueue.empty()) return result(0);
  u32 index = geQueue.front();
  auto& list = geLists[index];
  if(list.state == GeList::State::Running) return result(geOldSdk() ? 0xffff'ffff : ErrorAlready);
  if(list.state != GeList::State::Paused) return result(ErrorNotSupported);
  if(list.pausing || geRunning == s32(index)) return result(ErrorBusy);
  list.state = GeList::State::Running;
  if(list.context && !list.started) geSaveContext(list.context);
  if(!list.started) geOwe(GE::Cost::List);
  list.started = true;
  geLoad(list);
  geRunning = index;
  result(0);
  geRun();
}

//(mode, parameters): stops the GE (pspge.h: "interrupt drawing queue"). As pspautotests' gpu/ge/break and breakwait
//recorded on a PSP:
//  - a mode but 0 or 1 is refused (INVALID_MODE), then parameters the program couldn't pass (an address whose 16
//    bytes reach the kernel's half: PRIV_REQUIRED; none at all is fine; the PSP doesn't look inside them), then an
//    empty queue (ALREADY);
//  - mode 0 breaks off the list the GE has, as it is, and returns its ID: it's left paused at the queue's front, and
//    sceGeContinue takes it up where it was. A list paused already (by a PAUSE signal) is BUSY; so the GE runs no
//    list then, and sceGeSaveContext works again. (pspge.h has the two modes' results the other way round.)
//  - mode 1 throws every list away, queued, running or done, and returns 0: the next list enqueued takes the first
//    ID again (breakwait: "id reused"). Threads waiting for a list or for all drawing aren't woken: they wait on
//    until a list that ends wakes them (that list's ID, or the queue empty).
//What the lists thrown away had saved of the GE's state (sceGeListEnQueue's options) isn't put back. Their finish and
//signal callbacks still waiting their turn are dropped, and one running returns to no GE to go on: else they'd be
//for a list that's gone, or whose ID went to a new list, and one returning would end the new list ahead of its own
//finish callback. Unmeasured: pspautotests doesn't record whether a PSP drops a pending callback at a break. A FINISH
//the GE has run whose interrupt is still to come goes with its list in mode 1 (gpu/ge/intrsuspend: no callback once
//interrupts are back on), and with it what the GE had left to draw. Mode 0 takes it first, as though the interrupt
//came now, held off or not, and so the FINISH each list queued after it meets as it starts, those lists run then:
//their pixels are drawn, and nothing of them is left for sceGeContinue to take up. With a finish callback, though,
//the lists after it start only once that has returned: the break finds the list done (ALREADY) and breaks none of
//them off. (Unmeasured: break's "Completed list" is done before its break, as its list draws nothing.)
auto Kernel::sceGeBreak() -> void {
  ProfiledCall profile{*this, GeProfile::Break};
  u32 mode = arg(0), parameters = arg(1);
  if(mode > 1) return result(ErrorInvalidMode);
  if((parameters | (parameters + 16)) & 0x8000'0000) return result(ErrorPrivilegeRequired);
  if(geQueue.empty()) return result(ErrorAlready);
  if(mode == 1) {
    for(auto& list : geLists) list = {};
    geQueue.clear();
    geFree.clear();
    for(u32 index = 0; index < 64; index++) geFree.push_back(index);
    geRunning = -1;
    geBusy = false;
    geSuspended = false;
    geFinishing = -1;
    geFinishDue = false;
    geSignalDue = false;
    geClearRuns();
    geDoneAt = geReachAt = 0;
    std::erase_if(calls, [](const Call& call) { return call.kind == Call::Ge; });
    callResumesGe = false;
    return result(0);
  }
  while(geFinishDue || geSignalDue) {  //(the next list may meet its own FINISH as it starts)
    if(geSignalDue) geSignalDue = false, geSignaled();
    else geFinishDue = false, geFinished();
    geRun();
  }
  if(geQueue.empty()) return result(ErrorAlready);
  u32 index = geQueue.front();
  auto& list = geLists[index];
  if(list.state == GeList::State::Paused) return result(ErrorBusy);
  if(list.state != GeList::State::Running || geRunning != s32(index)) return result(ErrorAlready);
  list.registers = ge.list;
  list.base = ge.commands[GE::Base];
  list.state = GeList::State::Paused;
  geRunning = -1;
  geBusy = false;
  result(GeListIDs + index);
}

//(command): its last word. 0xff is refused, as the PSP's driver does.
auto Kernel::sceGeGetCmd() -> void {
  if(arg(0) >= 0xff) return result(ErrorInvalidIndex);
  result(ge.commands[arg(0)]);
}

//(which: 0-7 the bones, 8 world, 9 view, 10 projection, 11 texture; where): its elements as the GE holds them, each a
//float's top 24 bits.
auto Kernel::sceGeGetMtx() -> void {
  u32 which = arg(0), to = arg(1);
  if(which >= 12) return result(ErrorInvalidIndex);
  const u32* matrix = which < 8 ? ge.bones + which * 12 : which == 8 ? ge.world : which == 9 ? ge.view
                    : which == 10 ? ge.projection : ge.textureMatrix;
  u32 size = which == 10 ? 16 : 12;
  for(u32 n = 0; n < size; n++) memory.write(4, to + n * 4, matrix[n]);
  result(0);
}

//Not while the GE runs a list, stalled or not: -1 then, as the PSP's driver says. Waiting for a callback to return
//(a SIGNAL that suspends, a list's FINISH) it has stopped, and saves: pspautotests' gpu/ge/callbackstate. So it has
//at a FINISH whose interrupt is still to come, once the GE's time for it has come (geInterrupt()).
auto Kernel::sceGeSaveContext() -> void {
  if(arg(0) & 3) return result(ErrorInvalidPointer);
  bool stopped = geSuspended || (geFinishDue && geReached()) || (geSignalDue && geReachReached());
  if(geRunning >= 0 && !stopped) return result(0xffff'ffff);
  geSaveContext(arg(0));
  result(0);
}

auto Kernel::sceGeRestoreContext() -> void {
  if(arg(0) & 3) return result(ErrorInvalidPointer);
  if(geRunning >= 0) return result(ErrorBusy);
  geRestoreContext(arg(0));
  result(0);
}
