//Spins (ares/psp/kernel/spins.cpp): a thread polling quiet or calm system functions in a loop that comes back to
//where it was, skipped by whole turns to just before the CPU's go ends. Programs run in steps of an eighth of a frame
//on both engines with the skip on and off, and after every step the machine's whole state (memory, the CPU, the GE,
//the kernel) is the same either way: the Need for Speeds' idle loop (also with its ticker at its own priority, and
//with a status of 88 bytes), the intro movies' wait (also through the ring's library, one naming no ring, and with
//interrupts held off) and the GE asked after as it draws (also with its list stalled), skipped; loops that aren't one
//turn over and over (a count stored, a register counting, an FPU
//register adding up, the thread's run time read, the clock polled, a semaphore that counts, another priority's line
//rotated, a store into its own compiled code), not skipped. And skips made directly, against the same turns run: every
//register and byte, the clock, the ready order's count and the run time the last turn wrote (to the cycle); the turns'
//boundaries, none outside a go or with a thread of the caller's priority ready, and the pages a turn may store to.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {

constexpr u32 R = KernelMachine::Results;
constexpr u32 Count = R, Seen = R + 0x40, Ring = R + 0x100, Spare = R + 0x200, Library = R + 0x1000;
constexpr u32 Rounds = 3;
constexpr u64 Step = Kernel::VblankCycles / 8;

auto branchTo(Assembler& a, u32 target) -> s32 { return s32(target - a.here() - 4) / 4; }

//Starts thread name at entry, at priority (0x10: better than main's 0x20), from main.
auto startThread(Assembler& main, KernelMachine& m, const char* name, u32 entry, u32 priority = 0x10) -> void {
  main.li(a0, m.string(name)); main.li(a1, entry); main.li(a2, priority); main.li(a3, 0x1000);
  main.li(t0, 0); main.li(t1, 0);
  main.call("sceKernelCreateThread");
  main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
  main.call("sceKernelStartThread");
}

//What a loop does besides polling, to make it one that can't be skipped (Plain: none); or, Equal, its ticker at
//main's own priority, ready at main's next rotation once its delay ends (as the go ends) and taking the CPU there; or,
//Size88, the status's size word set to 88 before each call, which then copies the run time's first word alone (and
//writes the size word back as the whole's).
enum class Twist : u32 { Plain, Stored, Counting, RunTime, Clock, Counted, OwnCode, Fpu, OtherLine, Equal, Size88 };

//Where the idle loop's anchor is, for the direct skip: sceKernelReferThreadStatus's syscall, and where main goes on.
struct Idle { u32 refer = 0, back = 0; };

//The Need for Speeds' idle loop: main (priority 0x20, alone at it) asks its own ID, its status (into its stack frame)
//and rotates its own line with the priority the status gave, till a count a ticker thread moves on every tick
//microseconds reaches a target; three targets of five counts, the status's run time as each came noted.
auto idleLoop(KernelMachine& m, Twist twist = Twist::Plain, u32 tick = 200) -> Idle {
  Idle idle;
  Assembler ticker{m, 0x0880'3000};
  u32 again = ticker.here();
  ticker.li(a0, tick); ticker.call("sceKernelDelayThread");
  ticker.li(t0, Count); ticker.put(lw(t1, 0, t0)); ticker.put(addiu(t1, t1, 1)); ticker.put(sw(t1, 0, t0));
  ticker.put(beq(zero, zero, branchTo(ticker, again))); ticker.put(nop);
  Assembler main{m, 0x0880'1000};
  if(twist == Twist::Counted) {
    main.li(a0, m.string("counted")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 0x7fff'ffff); main.li(t0, 0);
    main.call("sceKernelCreateSema");
    main.put(addu(s7, v0, zero));
  }
  startThread(main, m, "ticker", 0x0880'3000, twist == Twist::Equal ? 0x20 : 0x10);
  if(twist == Twist::Size88) main.li(t5, 0x5a5a'5a5a), main.put(sw(t5, -0x80 + 0x10 + 88, sp));  //(under bytes 88-91)
  if(twist == Twist::Fpu) main.li(t0, 0x3f80'0000), main.put(mtc1(t0, 2));  //(f2: 1.0)
  main.li(s3, Count); main.li(s4, 0);
  u32 round = main.here();
  main.put(addiu(s4, s4, 1));
  main.put(sll(s5, s4, 2)); main.put(addu(s5, s5, s4));  //five counts a round
  u32 loop = main.here();
  main.put(addiu(sp, sp, -0x80));
  main.put(sw(s0, 0x78, sp)); main.put(sw(ra, 0x7c, sp));
  main.call("sceKernelGetThreadId");
  if(twist == Twist::Size88) main.li(t5, 88), main.put(sw(t5, 0x10, sp));
  main.put(addu(a0, v0, zero)); main.put(addiu(a1, sp, 0x10));
  main.call("sceKernelReferThreadStatus");
  idle.refer = m.stub("sceKernelReferThreadStatus") + 4, idle.back = main.here();
  if(twist == Twist::OtherLine) main.li(a0, 0x30);  //(no thread there: it changes nothing, but isn't worked out)
  else main.put(lw(a0, 0x50, sp));
  main.call("sceKernelRotateThreadReadyQueue");
  main.put(lw(s0, 0x78, sp)); main.put(lw(ra, 0x7c, sp));
  switch(twist) {
  case Twist::Stored:  //(the count in memory alone: no register keeps it)
    main.li(t5, Spare), main.put(lw(t6, 0, t5)), main.put(addiu(t6, t6, 1)), main.put(sw(t6, 0, t5));
    main.put(addu(t6, zero, zero));
    break;
  case Twist::Counting: main.put(addiu(s6, s6, 1)); break;
  case Twist::RunTime: main.put(lw(t6, 0x10 + 84, sp)); break;
  case Twist::Clock: main.call("sceKernelGetSystemTimeLow"); break;
  case Twist::Counted: main.put(addu(a0, s7, zero)), main.li(a1, 1), main.call("sceKernelSignalSema"); break;
  case Twist::OwnCode: main.li(t5, 0x0880'1ff0), main.put(lw(t6, 0, t5)), main.put(sw(t6, 0, t5)); break;
  case Twist::Fpu: main.put(fop(0x00, 1, 1, 2)); break;  //add.s f1, f1, f2
  case Twist::Plain: case Twist::OtherLine: case Twist::Equal: case Twist::Size88: break;
  }
  main.put(addiu(sp, sp, 0x80));
  main.put(lw(t0, 0, s3)); main.put(sltu(t1, t0, s5));
  main.put(bne(t1, zero, branchTo(main, loop))); main.put(nop);
  main.put(lw(t0, -0x80 + 0x10 + 84, sp));  //(the run time the last turn's status gave)
  main.put(sll(t1, s4, 2)); main.li(t2, Seen); main.put(addu(t2, t2, t1)); main.put(sw(t0, 0, t2));
  main.put(sltiu(t3, s4, Rounds));
  main.put(bne(t3, zero, branchTo(main, round))); main.put(nop);
  main.call("sceKernelExitGame");
  m.system.memory.write(4, 0x0880'1ff0, 0x1234'5678);  //(a word in the loop's own page: Twist::OwnCode)
  return idle;
}

//The intro movies' wait: main asks a movie ring how many packets are free and the vertical blank count, and signals a
//semaphore already at its most (refused), till a feeder thread (every 500 microseconds) puts 5 packets in, or the next
//vertical blank comes; four times, the ring emptied after each. held: interrupts held off round the wait, which keeps
//the feeder out (no thread switches then), so each ends at the blank. library: the ring's library's memory marked
//as sceMpegCreate marks it, naming the ring (1: the count read through it) or none (2: an address with nothing behind
//it, so the ring counts its own); 0, no library.
auto movieWait(KernelMachine& m, bool held = false, u32 library = 0) -> void {
  m.system.memory.write(4, Ring + 0, 64);  //packets
  m.system.memory.write(4, Ring + 12, 0);  //holding data
  m.system.memory.write(4, Ring + 40, library ? Library : 0);
  if(library) m.system.memory.copyIn(Library, "LIBMPEG", 8);
  if(library) m.system.memory.write(4, Library + 0x10, library == 1 ? Ring : 0);
  Assembler feeder{m, 0x0880'3000};
  u32 again = feeder.here();
  feeder.li(a0, 500); feeder.call("sceKernelDelayThread");
  feeder.li(t0, Ring); feeder.li(t1, 5); feeder.put(sw(t1, 12, t0));
  feeder.put(beq(zero, zero, branchTo(feeder, again))); feeder.put(nop);
  Assembler main{m, 0x0880'1000};
  main.li(a0, m.string("full")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 1); main.li(t0, 0);
  main.call("sceKernelCreateSema");
  main.put(addu(s0, v0, zero));
  startThread(main, m, "feeder", 0x0880'3000);
  main.li(s3, Ring); main.li(s4, 0);
  u32 round = main.here();
  main.put(addiu(s4, s4, 1));
  if(held) main.call("sceKernelCpuSuspendIntr"), main.put(addu(s2, v0, zero));
  main.call("sceDisplayGetVcount");
  main.put(addu(s5, v0, zero));
  u32 loop = main.here();
  main.put(addu(a0, s3, zero)); main.call("sceMpegRingbufferAvailableSize");
  main.put(addu(s1, v0, zero));
  main.call("sceDisplayGetVcount");
  main.put(addu(s6, v0, zero));
  main.put(addu(a0, s0, zero)); main.li(a1, 1); main.call("sceKernelSignalSema");
  main.put(lw(t0, 0, s3));
  u32 data = main.here();
  main.put(bne(s1, t0, 0)); main.put(nop);  //(to past the loop: patched below)
  main.put(beq(s5, s6, branchTo(main, loop))); main.put(nop);
  m.system.memory.write(4, data, bne(s1, t0, s32(main.here() - data - 4) / 4));
  if(held) main.put(addu(a0, s2, zero)), main.call("sceKernelCpuResumeIntr");
  main.put(sw(zero, 12, s3));
  main.put(sltiu(t3, s4, 4));
  main.put(bne(t3, zero, branchTo(main, round))); main.put(nop);
  main.call("sceKernelExitGame");
}

//How a program asks after the GE as it draws (geWait()): sceGeDrawSync(1); sceGeListSync(its list, 1); the first
//with its list held at the list's start (its stall address) till another thread moves that to its end; or the first
//with a register counting the turns, which makes it a loop that can't be skipped.
enum class Look : u32 { Draw, List, Stalled, Counting };

constexpr u32 GeList = 0x0894'0000, GeVertices = 0x0896'0000, GeCallbacks = R + 0x300, Finished = R + 0x310;
constexpr u32 Pending = R + 0x320;

//Need for Speed: Shift's and Resistance's wait for the GE (part 76): main queues a list of 16 sprites covering the
//screen, each a fill the GE takes its time over, a finish callback counting the lists done, and asks how the GE is
//doing till it says done, the clock noted after each; three lists. With the list stalled, a mover thread (every 2000
//microseconds) sees it pending and moves its stall address to its end, after which the GE draws it.
auto geWait(KernelMachine& m, Look look) -> void {
  auto& memory = m.system.memory;
  u32 at = GeList, vertices = GeVertices;
  auto put = [&](u32 command, u32 argument = 0) {
    memory.write(4, at, command << 24 | (argument & 0xff'ffff));
    at += 4;
  };
  put(GE::FrameBufferPointer, 0); put(GE::FrameBufferWidth, 512); put(GE::FrameBufferPixelFormat, 3);
  put(GE::Scissor1, 0); put(GE::Scissor2, 271 << 10 | 479); put(GE::Region2, 1023 << 10 | 1023);
  put(GE::VertexType, 0x80'011c);  //(through: a color and a 16-bit x, y and z)
  put(GE::Base, vertices >> 8 & 0xf'0000); put(GE::VertexAddress, vertices);
  for(u32 sprite = 0; sprite < 16; sprite++) {
    for(auto [x, y] : {std::pair<u32, u32>{0, 0}, {480, 272}}) {
      memory.write(4, vertices, 0xff00'00ff + sprite);
      memory.write(2, vertices + 4, x), memory.write(2, vertices + 6, y), memory.write(2, vertices + 8, 0);
      vertices += 12;
    }
    put(GE::Primitive, 6 << 16 | 2);  //(sprites)
  }
  put(GE::Finish); put(GE::End);
  u32 end = at;
  Assembler callback{m, 0x0880'4000};  //(the finish callback: one more list done)
  callback.li(t0, Finished); callback.put(lw(t1, 0, t0)); callback.put(addiu(t1, t1, 1)); callback.put(sw(t1, 0, t0));
  callback.put(jr(ra)); callback.put(nop);
  memory.write(4, GeCallbacks + 8, 0x0880'4000);
  Assembler mover{m, 0x0880'3000};
  u32 again = mover.here();
  mover.li(a0, 2000); mover.call("sceKernelDelayThread");
  mover.li(t0, Pending); mover.put(lw(a0, 0, t0)); mover.put(lw(t1, 4, t0));
  mover.put(beq(t1, zero, branchTo(mover, again))); mover.put(nop);
  mover.put(sw(zero, 4, t0));
  mover.li(a1, end); mover.call("sceGeListUpdateStallAddr");
  mover.put(beq(zero, zero, branchTo(mover, again))); mover.put(nop);
  Assembler main{m, 0x0880'1000};
  main.li(a0, GeCallbacks); main.call("sceGeSetCallback");
  main.put(addu(s1, v0, zero));
  if(look == Look::Stalled) startThread(main, m, "mover", 0x0880'3000);
  main.li(s4, 0);
  u32 round = main.here();
  main.put(addiu(s4, s4, 1));
  main.li(a0, GeList); main.li(a1, look == Look::Stalled ? GeList : 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  main.put(addu(s0, v0, zero));
  if(look == Look::Stalled) {
    main.li(t0, Pending); main.put(sw(s0, 0, t0)); main.li(t1, 1); main.put(sw(t1, 4, t0));
  }
  u32 poll = main.here();
  if(look == Look::List) main.put(addu(a0, s0, zero)), main.li(a1, 1), main.call("sceGeListSync");
  else main.li(a0, 1), main.call("sceGeDrawSync");
  if(look == Look::Counting) main.put(addiu(s6, s6, 1));
  main.put(bne(v0, zero, branchTo(main, poll))); main.put(nop);
  main.call("sceKernelGetSystemTimeLow");
  main.put(sll(t1, s4, 2)); main.li(t2, Seen); main.put(addu(t2, t2, t1)); main.put(sw(v0, 0, t2));
  main.put(sltiu(t3, s4, Rounds));
  main.put(bne(t3, zero, branchTo(main, round))); main.put(nop);
  main.call("sceKernelExitGame");
}

//A machine with a program, its main thread started at 0x0880'1000 (priority 0x20, a 16 KiB stack). Its date starts
//at a fixed time, not the host's (whole seconds, which two machines made a moment apart may not share).
auto machine(const std::function<void(KernelMachine&)>& program, bool recompile, bool spins)
-> std::unique_ptr<KernelMachine> {
  auto m = std::make_unique<KernelMachine>();
  m->kernel.startTime = 1'700'000'000ull * 1'000'000;
  program(*m);
  m->kernel.spins = spins;
  m->system.recompiler.enabled = recompile;
  m->system.power(0x0880'1000);
  s32 uid = m->kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
  m->kernel.startThread(*m->kernel.threads[uid], 0, 0);
  return m;
}

//The program run in steps (up to steps of them, or its end) with the skip on and off, on one engine: true if after
//every step the two machines' states are the same. skipped: the turns skipped with it on.
auto bothWays(const std::function<void(KernelMachine&)>& program, bool recompile, u32 steps, u64& skipped) -> bool {
  auto on = machine(program, recompile, true), off = machine(program, recompile, false);
  bool same = true;
  for(u32 step = 0; step < steps && !(on->kernel.exited && off->kernel.exited); step++) {
    on->kernel.run(Step), off->kernel.run(Step);
    if(saveState(*on) != saveState(*off)) {
      if(same) std::printf("  states differ after step %u (%s)\n", step + 1, recompile ? "recompiler" : "interpreter");
      same = false;
    }
  }
  CHECK(on->kernel.exited && off->kernel.exited, true);
  CHECK(off->kernel.spun, 0);
  for(auto& note : on->notes) std::printf("  note: %s\n", note.c_str());
  skipped = on->kernel.spun;
  return same;
}

}

//The idle loop and the movie wait, skipped (thousands of turns), every step's state the same as with every turn run;
//the run times the idle loop noted moving on as its targets came. With the ticker at main's priority, ready as a go
//begins, the turn whose rotation hands it the CPU isn't skipped (a proof there finds it ready), the rest are. With
//interrupts held off the wait is skipped as well (no thread switch, nor a call into the program, could come in it
//either way), the same; and with the ring's count read through its library. The status set to 88 bytes each turn
//(written back as 104 by the call) is skipped too, its second word of run time left as it was.
static auto spinsSkipped() -> void {
  for(bool recompile : {false, true}) {
    u64 skipped = 0;
    CHECK(bothWays([](KernelMachine& m) { idleLoop(m); }, recompile, 16, skipped), true);
    CHECK(skipped > 1000, true);
    CHECK(bothWays([](KernelMachine& m) { idleLoop(m, Twist::Equal); }, recompile, 16, skipped), true);
    CHECK(skipped > 1000, true);
    CHECK(bothWays([](KernelMachine& m) { movieWait(m); }, recompile, 40, skipped), true);
    CHECK(skipped > 1000, true);
    CHECK(bothWays([](KernelMachine& m) { movieWait(m, true); }, recompile, 40, skipped), true);
    CHECK(skipped > 1000, true);
    CHECK(bothWays([](KernelMachine& m) { movieWait(m, false, 1); }, recompile, 40, skipped), true);
    CHECK(skipped > 1000, true);
    CHECK(bothWays([](KernelMachine& m) { movieWait(m, false, 2); }, recompile, 40, skipped), true);
    CHECK(skipped > 1000, true);
    //the status of 88 bytes: its run time's first word the only bytes the time decides, its second never written
    CHECK(bothWays([](KernelMachine& m) { idleLoop(m, Twist::Size88); }, recompile, 16, skipped), true);
    CHECK(skipped > 1000, true);
  }
  auto m = machine([](KernelMachine& m) { idleLoop(m); }, true, true);
  m->kernel.run(Step * 16);
  CHECK(m->kernel.exited, true);
  for(u32 round = 2; round <= Rounds; round++) {
    CHECK(m->system.memory.read(4, Seen + round * 4) > m->system.memory.read(4, Seen + round * 4 - 4), true);
  }
}

//Loops that aren't the same turn over and over, never skipped, the same either way: a count stored in memory each
//turn, a register counting turns, an FPU register adding up, the thread's run time read from its status, the clock
//polled (whose answer moves on), a semaphore signalled that counts. One rotating another priority's line (none ready
//there) changes nothing, but isn't a case the kernel works out, so isn't skipped either. A store into the loop's own
//page (the same word again) drops its compiled code, so with the recompiler that one isn't skipped; the interpreter
//compiles nothing, and skips it.
static auto spinsRefused() -> void {
  for(bool recompile : {false, true}) {
    for(auto twist : {Twist::Stored, Twist::Counting, Twist::Fpu, Twist::RunTime, Twist::Clock, Twist::Counted,
                      Twist::OtherLine, Twist::OwnCode}) {
      u64 skipped = 0;
      CHECK(bothWays([twist](KernelMachine& m) { idleLoop(m, twist); }, recompile, 16, skipped), true);
      if(twist == Twist::OwnCode && !recompile) {
        CHECK(skipped > 1000, true);
      } else {
        if(skipped) std::printf("  twist %u skipped %llu turns\n", u32(twist), (unsigned long long)skipped);
        CHECK(skipped, 0);
      }
    }
  }
}

//One skip made directly, as a syscall in a go would make it (the anchor, sceKernelReferThreadStatus, its last turns
//proved), against the same turns run: the state is the same at once, its clock, the caller's place in its line and
//the ready order's count, and the run time the last turn's status wrote (no later turn has rewritten it yet). And the
//pages a turn may store to: not one the GE watches (a store there forgets textures decoded from it), nor one holding
//compiled code (a store there drops it).
static auto spinsDirect() -> void {
  Idle idle;
  //main inside its loop (its ticker asleep for a while), about to make the anchor's syscall
  auto positioned = [&] {
    auto m = machine([&](KernelMachine& m) { idle = idleLoop(m, Twist::Plain, 100'000); }, false, false);
    m->kernel.run(Kernel::VblankCycles);
    while(m->system.ipu.pc != idle.refer || m->system.ipu.pd != idle.back) m->kernel.run(1);
    return m;
  };
  auto a = positioned(), b = positioned(), c = positioned();
  u64 period = 0;  //(stepped round once on the third)
  do c->kernel.run(1), period++; while(c->system.ipu.pc != idle.refer || c->system.ipu.pd != idle.back);
  CHECK(period > 20 && period < 60, true);
  CHECK(a->kernel.current && a->kernel.current->name == "main", true);
  //the anchor's syscall in a go of its own (with room for turns turns and the margin, or not in a go), the detector a
  //turn short of a proof: the turns skipped
  auto anchor = [&](KernelMachine& m, u64 turns, bool going = true) {
    auto& k = m.kernel;
    k.spins = true;
    k.spin = {idle.refer, idle.back, k.cycles - period, period, Kernel::SpinTurns - 1, 0};
    k.counted = 0;
    k.going = going;
    m.system.instructionsRun = 0;
    m.system.runLimit = (turns + Kernel::SpinMargin) * period + 1;
    m.system.instruction();
    k.going = false;
    u64 ran = m.system.instructionsRun + 1;  //(and the syscall)
    k.cycles += ran - k.counted, k.counted = ran;
    return k.spun;
  };
  constexpr u64 Turns = 500;
  //the run time the last skipped turn's status gives (the anchor's call ending it, at the anchor's time plus the turns)
  //on a microsecond's last cycle, then on its first, main's run figures moved back alike on both machines: a skip
  //writing it a cycle late, or early, writes another microsecond
  for(u64 phase : {332, 0}) {
    auto x = positioned(), y = positioned();
    for(auto* m : {x.get(), y.get()}) {
      auto& thread = *m->kernel.current;
      u64 ran = thread.runCycles + m->kernel.cycles + Turns * period - m->kernel.ranSince;
      thread.runCycles -= (ran % 333 + 333 - phase) % 333;
    }
    CHECK(anchor(*x, Turns), Turns);
    y->kernel.run(Turns * period + 1);
    u32 at = x->system.ipu.r[29] + 0x10 + 84;
    CHECK(x->system.memory.read(4, at), y->system.memory.read(4, at));
    CHECK(saveState(*x) == saveState(*y), true);
  }
  CHECK(anchor(*a, Turns), Turns);
  b->kernel.run(Turns * period + 1);
  CHECK(a->kernel.cycles, b->kernel.cycles);
  CHECK(a->kernel.readySequence, b->kernel.readySequence);
  CHECK(a->kernel.current->readySince, b->kernel.current->readySince);
  u32 runTime = a->system.ipu.r[29] + 0x10 + 84;
  CHECK(a->system.memory.read(4, runTime), b->system.memory.read(4, runTime));
  CHECK(a->system.memory.read(4, runTime) != c->system.memory.read(4, runTime), true);
  CHECK(saveState(*a) == saveState(*b), true);
  //with a thread of main's priority ready (woken as the go began, say), the turn's rotation would hand it the CPU:
  //not skipped, and that's what happens
  auto e = positioned();
  auto& ticker = *e->kernel.threads.rbegin()->second;
  CHECK(ticker.name == "ticker", true);
  ticker.status = Kernel::Status::Ready, ticker.priority = 0x20, ticker.wait = Kernel::Wait::None, ticker.wakeAt = 0;
  CHECK(anchor(*e, Turns), 0);
  CHECK(e->kernel.spin.wait > 0, true);
  u32 count = e->system.memory.read(4, Count);
  e->kernel.run(period);  //(the ticker gets the CPU at the rotation, moves the count on and waits again)
  CHECK(e->system.memory.read(4, Count), count + 1);
  //room for one turn and the margin: one skipped; for the margin alone, none; nor outside a go
  CHECK(anchor(*positioned(), 1), 1);
  CHECK(anchor(*positioned(), 0), 0);
  CHECK(anchor(*positioned(), Turns, false), 0);
  //where a turn may store
  u32 stack = c->system.ipu.r[29];
  CHECK(c->kernel.spinStorable(stack, 4), true);
  c->system.memory.watch(stack & ~0xfffu, 0x1000);
  CHECK(c->kernel.spinStorable(stack, 4), false);
  CHECK(c->kernel.spinStorable((stack & ~0xfffu) - 4, 8), false);  //(one byte on it is enough)
  auto compiled = machine([](KernelMachine& m) { idleLoop(m); }, true, false);
  compiled->kernel.run(Step);
  CHECK(compiled->kernel.spinStorable(0x0880'1ff0, 4), false);
  CHECK(compiled->kernel.spinStorable(compiled->system.ipu.r[29], 4), true);
}

//The GE asked after as it draws, skipped (thousands of turns) and every step's state the same as with every turn run:
//sceGeDrawSync(1), sceGeListSync(id, 1), and the first with the list stalled (3 till the mover moves it on, then 2)
//till done, each list's finish callback run and the clock noted after each, both moving on. With a register counting
//the turns the loop isn't one turn over and over, and isn't skipped.
static auto spinsGeLooks() -> void {
  for(bool recompile : {false, true}) {
    for(auto look : {Look::Draw, Look::List, Look::Stalled}) {
      u64 skipped = 0;
      CHECK(bothWays([look](KernelMachine& m) { geWait(m, look); }, recompile, 32, skipped), true);
      if(skipped <= 1000) std::printf("  look %u skipped %llu turns\n", u32(look), (unsigned long long)skipped);
      CHECK(skipped > 1000, true);
    }
    u64 skipped = 0;
    CHECK(bothWays([](KernelMachine& m) { geWait(m, Look::Counting); }, recompile, 32, skipped), true);
    CHECK(skipped, 0);
  }
  auto m = machine([](KernelMachine& m) { geWait(m, Look::Stalled); }, true, true);
  m->kernel.run(Step * 32);
  CHECK(m->kernel.exited, true);
  CHECK(m->system.memory.read(4, Finished), Rounds);
  for(u32 round = 2; round <= Rounds; round++) {
    CHECK(m->system.memory.read(4, Seen + round * 4) > m->system.memory.read(4, Seen + round * 4 - 4), true);
  }
}

auto spinTests() -> Tests {
  return {
    {"spins skipped as run", spinsSkipped}, {"spins refused", spinsRefused}, {"a spin skipped directly", spinsDirect},
    {"the GE's looks polled, skipped as run", spinsGeLooks},
  };
}

}
