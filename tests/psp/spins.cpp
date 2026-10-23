//Spins (ares/psp/kernel/spins.cpp): a thread polling quiet or calm system functions in a loop that comes back to where
//it was, skipped by whole turns to just before the CPU's go ends. Programs run in steps of an eighth of a frame on both
//engines with the skip on and off, and after every step the machine's whole state (memory, the CPU, the GE, the kernel)
//is the same either way: the Need for Speeds' idle loop (also with its ticker at its own priority, and with a status of
//88 bytes), the intro movies' wait (also through the ring's library, one naming no ring, and with interrupts held off)
//and the GE asked after as it draws (also with its list stalled, behind another, past a SIGNAL, with interrupts held
//off, with a GE profile recording, and with the PSP's GE timing, each turn moving lines of the CPU's caches, or begun
//after a sleep and not skipped across the kink in its busy window's push; also beside a semaphore signalled and taken
//back with waits that succeed at once, as the EA games' loops do, with a CB wait, with waits and a signal refused, and
//with a signal that hands the CPU to no thread ready, and beside a callback check finding none due), skipped;
//loops that aren't one turn over and over (a count stored, a register counting, an FPU register adding up, the thread's
//run time read, the clock polled, a semaphore that counts, a turn taking one more of a semaphore than it gives, another
//priority's line rotated, a store into its own compiled code), or whose turn would hand the CPU to a better thread, or
//signals a semaphore a thread waits on, or waits, not skipped. And skips made directly, against the same turns run:
//every register and byte, the clock, the ready order's count and the run time the last turn wrote (to the cycle); the
//turns' boundaries, none outside a go or with a thread of the caller's priority ready, nor past a callback notified as
//a turn begins (its CB wait's, or its callback check's); and the pages a turn may store to.
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
//with its list held at the list's start (its stall address) till another thread moves that to its end; the first with
//a register counting the turns, which makes it a loop that can't be skipped; or the first with its list held past its
//first sprite instead, its sprites textured from RAM (the GE drawing that sprite meanwhile, reading RAM, then idle).
enum class Look : u32 { Draw, List, Stalled, Counting, Drawing };

constexpr u32 GeList = 0x0894'0000, GeVertices = 0x0896'0000, GeCallbacks = R + 0x300, Finished = R + 0x310;
constexpr u32 Pending = R + 0x320, GeTexels = 0x0898'0000;

//A list at at of 16 sprites covering the screen (their vertices at vertices), each a fill the GE takes its time over
//(textured: a 64x64 texture at GeTexels, read from RAM, stretched over it), then its FINISH; with signal, a SIGNAL
//(0x42, its callback run, the list going on) after the eighth. Where it ends; first: where its first sprite ends.
auto geSprites(KernelMachine& m, u32 at, u32 vertices, bool signal = false, u32* first = nullptr, bool textured = false)
  -> u32 {
  auto& memory = m.system.memory;
  auto put = [&](u32 command, u32 argument = 0) {
    memory.write(4, at, command << 24 | (argument & 0xff'ffff));
    at += 4;
  };
  put(GE::FrameBufferPointer, 0); put(GE::FrameBufferWidth, 512); put(GE::FrameBufferPixelFormat, 3);
  put(GE::Scissor1, 0); put(GE::Scissor2, 271 << 10 | 479); put(GE::Region2, 1023 << 10 | 1023);
  if(textured) {
    for(u32 n = 0; n < 64 * 64; n++) memory.write(4, GeTexels + n * 4, 0xff00'0000 | (n * 0x0305'0709 & 0xff'ffff));
    put(GE::VertexType, 0x80'0102);  //(through: 16-bit texture coordinates and positions)
    put(GE::TextureMappingEnable, 1); put(GE::TextureAddress0, GeTexels);
    put(GE::TextureBufferWidth0, (GeTexels >> 24) << 16 | 64); put(GE::TextureSize0, 6 << 8 | 6);
    put(GE::TextureFormat, 3); put(GE::TextureFunction, 3); put(GE::TextureFilter, 1 | 1 << 8);
  } else {
    put(GE::VertexType, 0x80'011c);  //(through: a color and a 16-bit x, y and z)
  }
  put(GE::Base, vertices >> 8 & 0xf'0000); put(GE::VertexAddress, vertices);
  for(u32 sprite = 0; sprite < 16; sprite++) {
    for(auto [x, y] : {std::pair<u32, u32>{0, 0}, {480, 272}}) {
      if(textured) {  //(u, v, x, y, z)
        for(u32 half : {x ? 64u : 0u, y ? 64u : 0u, x, y, 0u}) memory.write(2, vertices, half), vertices += 2;
        continue;
      }
      memory.write(4, vertices, 0xff00'00ff + sprite);
      memory.write(2, vertices + 4, x), memory.write(2, vertices + 6, y), memory.write(2, vertices + 8, 0);
      vertices += 12;
    }
    put(GE::Primitive, 6 << 16 | 2);  //(sprites)
    if(first && sprite == 0) *first = at;
    if(signal && sprite == 7) put(GE::Signal, 0x01 << 16 | 0x0042), put(GE::End);
  }
  put(GE::Finish); put(GE::End);
  return at;
}

//What each turn of a poll reads besides, for the PSP's GE timing's caches (Allegrex::Cache): nothing; a word of RAM
//through an uncached mirror (an uncached access a turn); or a word from each of three lines of one set, which two ways
//can't hold (three lines read in a turn).
enum class Traffic : u32 { None, Uncached, Missing };
constexpr u32 Words = 0x0940'0000;

//A wait for the GE as games poll it (Resistance asks sceGeListSync(id, 1); Need for Speed: Shift asks
//sceGeDrawSync(1), but also locks a semaphore each turn, which this leaves out): main queues a list of 16 sprites
//(geSprites()), a finish callback counting the lists done, and asks how the GE is doing till it says done, the clock
//noted after each; three lists. With the list stalled (at its start, or past its first sprite), a mover thread (every
//`every` microseconds, 2000 unless asked) sees it pending and moves its stall address to its end, after which the GE
//draws it. Each turn reads what traffic says besides.
auto geWait(KernelMachine& m, Look look, Traffic traffic = Traffic::None, u32 every = 2000) -> void {
  auto& memory = m.system.memory;
  u32 first = 0, end = geSprites(m, GeList, GeVertices, false, &first, look == Look::Drawing);
  bool stalled = look == Look::Stalled || look == Look::Drawing;
  u32 stall = look == Look::Stalled ? GeList : look == Look::Drawing ? first : 0;
  Assembler callback{m, 0x0880'4000};  //(the finish callback: one more list done)
  callback.li(t0, Finished); callback.put(lw(t1, 0, t0)); callback.put(addiu(t1, t1, 1)); callback.put(sw(t1, 0, t0));
  callback.put(jr(ra)); callback.put(nop);
  memory.write(4, GeCallbacks + 8, 0x0880'4000);
  Assembler mover{m, 0x0880'3000};
  u32 again = mover.here();
  mover.li(a0, every); mover.call("sceKernelDelayThread");
  mover.li(t0, Pending); mover.put(lw(a0, 0, t0)); mover.put(lw(t1, 4, t0));
  mover.put(beq(t1, zero, branchTo(mover, again))); mover.put(nop);
  mover.put(sw(zero, 4, t0));
  mover.li(a1, end); mover.call("sceGeListUpdateStallAddr");
  mover.put(beq(zero, zero, branchTo(mover, again))); mover.put(nop);
  Assembler main{m, 0x0880'1000};
  main.li(a0, GeCallbacks); main.call("sceGeSetCallback");
  main.put(addu(s1, v0, zero));
  if(stalled) startThread(main, m, "mover", 0x0880'3000);
  main.li(s4, 0);
  u32 round = main.here();
  main.put(addiu(s4, s4, 1));
  main.li(a0, GeList); main.li(a1, stall); main.put(addu(a2, s1, zero)); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  main.put(addu(s0, v0, zero));
  if(stalled) {
    main.li(t0, Pending); main.put(sw(s0, 0, t0)); main.li(t1, 1); main.put(sw(t1, 4, t0));
  }
  u32 poll = main.here();
  if(traffic == Traffic::Uncached) main.li(t4, Words | 0x4000'0000), main.put(lw(t5, 0, t4));
  if(traffic == Traffic::Missing) {
    main.li(t4, Words), main.put(lw(t5, 0, t4)), main.put(lw(t6, 0x2000, t4)), main.put(lw(t7, 0x4000, t4));
  }
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

//How a poll may go besides (one list of geSprites(), its finish callback counting): Behind, a second list queued
//behind it and asked after (sceGeListSync(id, 1)) till done, queued, then drawing; Signal, the list with a SIGNAL,
//its callback counting, asked after through sceGeDrawSync(1); Held, sceGeDrawSync(1) polled with interrupts held off
//till the vertical blank's count moves (the GE through the list long before, its FINISH not taken till they're on
//again), then let go and polled again till done. The clock is noted at the end.
enum class Poll : u32 { Behind, Signal, Held };
constexpr u32 Signaled = R + 0x330;

auto gePoll(KernelMachine& m, Poll poll) -> void {
  auto& memory = m.system.memory;
  geSprites(m, GeList, GeVertices, poll == Poll::Signal);
  if(poll == Poll::Behind) geSprites(m, GeList + 0x1000, GeVertices + 0x1000);
  Assembler callback{m, 0x0880'4000};  //(the finish callback: one more list done)
  callback.li(t0, Finished); callback.put(lw(t1, 0, t0)); callback.put(addiu(t1, t1, 1)); callback.put(sw(t1, 0, t0));
  callback.put(jr(ra)); callback.put(nop);
  Assembler signal{m, 0x0880'5000};  //(the signal callback: one more SIGNAL)
  signal.li(t0, Signaled); signal.put(lw(t1, 0, t0)); signal.put(addiu(t1, t1, 1)); signal.put(sw(t1, 0, t0));
  signal.put(jr(ra)); signal.put(nop);
  memory.write(4, GeCallbacks + 0, 0x0880'5000);
  memory.write(4, GeCallbacks + 8, 0x0880'4000);
  Assembler main{m, 0x0880'1000};
  main.li(a0, GeCallbacks); main.call("sceGeSetCallback");
  main.put(addu(s1, v0, zero));
  main.li(a0, GeList); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  main.put(addu(s0, v0, zero));
  if(poll == Poll::Behind) {
    main.li(a0, GeList + 0x1000); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.put(addu(s2, v0, zero));
    u32 look = main.here();
    main.put(addu(a0, s2, zero)); main.li(a1, 1); main.call("sceGeListSync");
    main.put(bne(v0, zero, branchTo(main, look))); main.put(nop);
  } else if(poll == Poll::Signal) {
    u32 look = main.here();
    main.li(a0, 1); main.call("sceGeDrawSync");
    main.put(bne(v0, zero, branchTo(main, look))); main.put(nop);
  } else {
    main.call("sceKernelCpuSuspendIntr"); main.put(addu(s2, v0, zero));
    main.call("sceDisplayGetVcount"); main.put(addu(s5, v0, zero));
    u32 look = main.here();
    main.li(a0, 1); main.call("sceGeDrawSync"); main.put(addu(s3, v0, zero));
    main.call("sceDisplayGetVcount"); main.put(addu(s6, v0, zero));
    u32 out = main.here();
    main.put(beq(s3, zero, 0)); main.put(nop);  //(done: out; its offset set below)
    main.put(beq(s5, s6, branchTo(main, look))); main.put(nop);
    memory.write(4, out, beq(s3, zero, s32(main.here() - out - 4) / 4));
    main.put(addu(a0, s2, zero)); main.call("sceKernelCpuResumeIntr");
    u32 again = main.here();
    main.li(a0, 1); main.call("sceGeDrawSync");
    main.put(bne(v0, zero, branchTo(main, again))); main.put(nop);
  }
  main.call("sceKernelGetSystemTimeLow");
  main.li(t2, Seen); main.put(sw(v0, 0, t2));
  main.call("sceKernelExitGame");
}

//A list at at of count one-pixel sprites, untextured, their corners (a color and a 16-bit x, y and z each) read from
//RAM at vertices, then its FINISH: nearly all of its clocks the front end's, reading RAM. Where it ends.
auto geDots(KernelMachine& m, u32 at, u32 vertices, u32 count) -> u32 {
  auto& memory = m.system.memory;
  auto put = [&](u32 command, u32 argument = 0) {
    memory.write(4, at, command << 24 | (argument & 0xff'ffff));
    at += 4;
  };
  put(GE::FrameBufferPointer, 0); put(GE::FrameBufferWidth, 512); put(GE::FrameBufferPixelFormat, 3);
  put(GE::Scissor1, 0); put(GE::Scissor2, 271 << 10 | 479); put(GE::Region2, 1023 << 10 | 1023);
  put(GE::VertexType, 0x80'011c);  //(through: a color and a 16-bit x, y and z)
  put(GE::Base, vertices >> 8 & 0xf'0000); put(GE::VertexAddress, vertices);
  for(u32 n = 0; n < count; n++) {
    u32 x = n % 480, y = n / 480 % 272;
    for(auto [dx, dy] : {std::pair<u32, u32>{0, 0}, {1, 1}}) {
      memory.write(4, vertices, 0xff00'0000 | n), memory.write(2, vertices + 4, x + dx);
      memory.write(2, vertices + 6, y + dy), memory.write(2, vertices + 8, 0);
      vertices += 12;
    }
  }
  for(u32 left = count * 2; left;) {  //(sprites, as many corners a PRIM as its count holds)
    u32 corners = std::min(left, 65534u);
    put(GE::Primitive, 6 << 16 | corners);
    left -= corners;
  }
  put(GE::Finish); put(GE::End);
  return at;
}

//A poll begun after a sleep beside the GE: main queues a list of 40,000 one-pixel sprites whose corners it reads from
//RAM (geDots()), its finish callback counting the lists done, reads a word from each of 256 lines it hasn't read
//before, sleeps 600 microseconds and asks after the list (sceGeListSync(id, 1)) till done, the clock noted after each;
//three lists.
auto geSlept(KernelMachine& m) -> void {
  auto& memory = m.system.memory;
  geDots(m, GeList, GeVertices, 40000);
  Assembler callback{m, 0x0880'4000};  //(the finish callback: one more list done)
  callback.li(t0, Finished); callback.put(lw(t1, 0, t0)); callback.put(addiu(t1, t1, 1)); callback.put(sw(t1, 0, t0));
  callback.put(jr(ra)); callback.put(nop);
  memory.write(4, GeCallbacks + 8, 0x0880'4000);
  Assembler main{m, 0x0880'1000};
  main.li(a0, GeCallbacks); main.call("sceGeSetCallback");
  main.put(addu(s1, v0, zero));
  main.li(s4, 0);
  u32 round = main.here();
  main.put(addiu(s4, s4, 1));
  main.li(a0, GeList); main.li(a1, 0); main.put(addu(a2, s1, zero)); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  main.put(addu(s0, v0, zero));
  main.put(sll(t4, s4, 14)); main.li(t5, Words); main.put(addu(t4, t4, t5));  //(each list its own lines)
  main.li(t6, 256);
  u32 read = main.here();
  main.put(lw(t5, 0, t4)); main.put(addiu(t4, t4, 64)); main.put(addiu(t6, t6, -1));
  main.put(bne(t6, zero, branchTo(main, read))); main.put(nop);
  main.li(a0, 600); main.call("sceKernelDelayThread");
  u32 poll = main.here();
  main.put(addu(a0, s0, zero)); main.li(a1, 1); main.call("sceGeListSync");
  main.put(bne(v0, zero, branchTo(main, poll))); main.put(nop);
  main.call("sceKernelGetSystemTimeLow");
  main.put(sll(t1, s4, 2)); main.li(t2, Seen); main.put(addu(t2, t2, t1)); main.put(sw(v0, 0, t2));
  main.put(sltiu(t3, s4, Rounds));
  main.put(bne(t3, zero, branchTo(main, round))); main.put(nop);
  main.call("sceKernelExitGame");
}

//A machine with a program, its main thread started at 0x0880'1000 (priority 0x20, a 16 KiB stack), with the GE timing
//"Fast" or the PSP's. Its date starts at a fixed time, not the host's (whole seconds, which two machines made a moment
//apart may not share).
auto machine(const std::function<void(KernelMachine&)>& program, bool recompile, bool spins, bool psp = false)
-> std::unique_ptr<KernelMachine> {
  auto m = std::make_unique<KernelMachine>();
  m->kernel.startTime = 1'700'000'000ull * 1'000'000;
  if(psp) m->pspTiming();
  program(*m);
  m->kernel.spins = spins;
  m->system.recompiler.enabled = recompile;
  m->system.power(0x0880'1000);
  s32 uid = m->kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
  m->kernel.startThread(*m->kernel.threads[uid], 0, 0);
  return m;
}

//The program run in steps (up to steps of them, or its end) with the skip on and off, on one engine: true if after
//every step the two machines' states are the same, and in the end the CPU's caches' counts. skipped: the turns
//skipped with it on; checks, if given: the tries each of the PSP's GE timing's checks refused with it on.
auto bothWays(const std::function<void(KernelMachine&)>& program, bool recompile, u32 steps, u64& skipped,
              bool psp = false, Kernel::SpinChecks* checks = nullptr) -> bool {
  auto on = machine(program, recompile, true, psp), off = machine(program, recompile, false, psp);
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
  if(checks) *checks = on->kernel.spinChecks;
  u64 counts[2][4];
  on->kernel.spinCounts(counts[0]), off->kernel.spinCounts(counts[1]);
  return same && std::equal(counts[0], counts[0] + 4, counts[1]);
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
  FastTimed fastTimed;  //(anchor() sets the detector as "Fast" runs it: with the PSP's timing its steady stays false)
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
//till done, each list's finish callback run and the clock noted after each, both moving on; the stalled phase itself
//skipped too, before the mover's turn. With a register counting the turns the loop isn't one turn over and over, and
//isn't skipped.
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
  //the turns skipped while the first list waits at its stall address (in steps of a 128th of a frame)
  for(bool recompile : {false, true}) {
    auto m = machine([](KernelMachine& m) { geWait(m, Look::Stalled); }, recompile, true);
    u64 stalled = 0;
    bool seen = false;
    for(u32 step = 0; step < 32 * 16 && !m->kernel.exited; step++) {
      m->kernel.run(Step / 16);
      auto& k = m->kernel;
      if(k.geRunning >= 0 && k.geAtStall(k.geRunning)) stalled = k.spun, seen = true;
      else if(seen) break;
    }
    CHECK(seen, true);
    CHECK(stalled > 1000, true);
  }
}

//With the PSP's GE timing (the CPU's caches counted, the GE slowed by the lines they move: part 113), the GE asked
//after (sceGeListSync(id, 1), and through sceGeDrawSync(1) with the list stalled) by a poll whose turn also reads a
//word of RAM through an uncached mirror, or a word from each of three lines one set can't hold (three lines read in
//each turn): skipped (thousands of turns), every step's state the same as with every turn run, and in the end the
//caches' counts too, each turn skipped counted as moving what the one before did; on both engines (the interpreter's
//accesses aren't counted). The recompiler's machine counts an uncached access for every turn, skipped or not.
static auto spinsGePsp() -> void {
  for(bool recompile : {false, true}) {
    for(auto traffic : {Traffic::Uncached, Traffic::Missing}) {
      for(auto look : {Look::List, Look::Stalled}) {
        u64 skipped = 0;
        CHECK(bothWays([=](KernelMachine& m) { geWait(m, look, traffic); }, recompile, 32, skipped, true), true);
        if(skipped <= 1000) {
          std::printf("  traffic %u look %u skipped %llu turns\n", u32(traffic), u32(look), (unsigned long long)skipped);
        }
        CHECK(skipped > 1000, true);
      }
    }
  }
  auto m = machine([](KernelMachine& m) { geWait(m, Look::List, Traffic::Uncached); }, true, true, true);
  m->kernel.run(Step * 32);
  CHECK(m->kernel.exited, true);
  CHECK(m->kernel.spun > 1000 && m->system.dataCache.uncached > m->kernel.spun, true);
}

//With the PSP's GE timing, a poll whose turns move no lines, begun after a sleep beside the GE (geSlept()): the sleep
//fills the GE's busy window with cycles passed and no instructions, so its time is the cycles passed, not the CPU's
//(geContendBound()), and the poll's turns, each longer for a PSP's CPU than the cycles it takes, make it the CPU's
//about ten thousand turns in. The push the window owes for the lines read before the sleep rises till then and falls
//after (the first list's: 34726 cycles as the poll begins, 36650 at the kink, 24810 had the turns a skip would take
//been one stretch), so the turns run one by one push the GE back further than one stretch for all of them would: a
//skip across that kink is refused (spinSkip()), and once past it the turns are skipped. The list reads its vertices
//from RAM: the hybrid charges texel reads a fixed time a line (GE::Cost), so a list reading only texels owes the most
//as the poll begins and the refusal would have nothing to guard; and those vertices' stalls make the window's time the
//CPU's at once after the shortest sleep, hence 600 microseconds. After every step the clock, the GE's time, its busy
//window and the rest of the state the same as with every turn run, and in the end the caches' counts; thousands of
//turns skipped. On the recompiler (the interpreter's accesses aren't counted, so its window owes nothing).
static auto spinsGeKink() -> void {
  auto on = machine(geSlept, true, true, true), off = machine(geSlept, true, false, true);
  bool same = true;
  for(u32 step = 0; step < 32 && !(on->kernel.exited && off->kernel.exited); step++) {
    on->kernel.run(Step), off->kernel.run(Step);
    bool states = saveState(*on) == saveState(*off);  //(first: with the GE on its thread, the runs folded)
    auto& a = on->kernel;
    auto& b = off->kernel;
    if(states && a.cycles == b.cycles && a.geDoneAt == b.geDoneAt &&
       !std::memcmp(&a.geWindow, &b.geWindow, sizeof(a.geWindow))) continue;
    if(same) {
      std::printf("  step %u: clock %llu against %llu, the GE done at %llu against %llu, pushed %llu against %llu\n",
                  step + 1, (unsigned long long)a.cycles, (unsigned long long)b.cycles,
                  (unsigned long long)a.geDoneAt, (unsigned long long)b.geDoneAt,
                  (unsigned long long)a.geWindow.pushed, (unsigned long long)b.geWindow.pushed);
    }
    same = false;
  }
  u64 counts[2][4];
  on->kernel.spinCounts(counts[0]), off->kernel.spinCounts(counts[1]);
  CHECK(same, true);
  CHECK(std::equal(counts[0], counts[0] + 4, counts[1]), true);
  CHECK(on->kernel.exited && off->kernel.exited, true);
  CHECK(on->system.memory.read(4, Finished), Rounds);
  CHECK(off->kernel.spun, 0);
  if(on->kernel.spun <= 1000) std::printf("  skipped %llu turns\n", (unsigned long long)on->kernel.spun);
  CHECK(on->kernel.spun > 1000, true);
  CHECK(on->kernel.spinChecks.kind > 0, true);  //(the skip across the kink refused by its check, the window's kind)
}

//The GE asked after as a poll may go besides (gePoll()), skipped (thousands of turns) and every step's state the same
//as with every turn run: a list queued behind another, a SIGNAL's callback mid-poll, interrupts held off past the
//FINISH's time. Each list's finish callback runs once, the SIGNAL's callback once.
static auto spinsGePolls() -> void {
  for(bool recompile : {false, true}) {
    for(auto poll : {Poll::Behind, Poll::Signal, Poll::Held}) {
      u64 skipped = 0;
      CHECK(bothWays([poll](KernelMachine& m) { gePoll(m, poll); }, recompile, 32, skipped), true);
      if(skipped <= 1000) std::printf("  poll %u skipped %llu turns\n", u32(poll), (unsigned long long)skipped);
      CHECK(skipped > 1000, true);
    }
  }
  for(auto poll : {Poll::Behind, Poll::Signal, Poll::Held}) {
    auto m = machine([poll](KernelMachine& m) { gePoll(m, poll); }, true, true);
    m->kernel.run(Step * 32);
    CHECK(m->kernel.exited, true);
    CHECK(m->system.memory.read(4, Finished), poll == Poll::Behind ? 2u : 1u);
    CHECK(m->system.memory.read(4, Signaled), poll == Poll::Signal ? 1u : 0u);
  }
}

//With a GE profile recording, the looks skipped or not leave the machine the same, and every record but the looks'
//the same; with the skip off every look is recorded, with it on a few in thousands (what the runner's --ge-profile
//turns it off for).
static auto spinsGeProfiled() -> void {
  auto looks = [](const GeProfile& profile) {
    u64 count = 0;
    for(auto& r : profile.records) count += r.kind == GeProfile::DrawSync || r.kind == GeProfile::ListSync;
    return count;
  };
  auto others = [](const GeProfile& profile) {
    std::vector<GeProfile::Record> kept;
    for(auto& r : profile.records) if(r.kind != GeProfile::DrawSync && r.kind != GeProfile::ListSync) kept.push_back(r);
    return kept;
  };
  for(bool recompile : {false, true}) {
    for(auto look : {Look::Draw, Look::List, Look::Stalled}) {
      auto on = machine([look](KernelMachine& m) { geWait(m, look); }, recompile, true);
      auto off = machine([look](KernelMachine& m) { geWait(m, look); }, recompile, false);
      GeProfile profileOn, profileOff;
      on->kernel.geProfileStart(profileOn), off->kernel.geProfileStart(profileOff);
      bool same = true;
      for(u32 step = 0; step < 32 && !(on->kernel.exited && off->kernel.exited); step++) {
        on->kernel.run(Step), off->kernel.run(Step);
        same &= saveState(*on) == saveState(*off);
      }
      on->kernel.geProfileStop(), off->kernel.geProfileStop();
      CHECK(same, true);
      CHECK(on->kernel.exited && off->kernel.exited, true);
      auto a = others(profileOn), b = others(profileOff);
      bool equal = a.size() == b.size();
      for(size_t n = 0; equal && n < a.size(); n++) equal = !std::memcmp(&a[n], &b[n], sizeof(a[n]));
      CHECK(equal, true);
      CHECK(profileOn.threads == profileOff.threads, true);
      CHECK(looks(profileOn) * 1000 < looks(profileOff), true);
    }
  }
}

//With the GE on its thread, held back at the start of every step (let go of a few milliseconds in), so that a list
//handed over isn't counted yet as the poll comes round and the GE's time is only its bound: the GE asked after as it
//draws, skipped, and every step's state the same as without the thread with every turn run. A skip ends before the
//go's limit, which goes no further than that bound (untilNextEvent()), and no FINISH falls due within it by the bound
//(spinSkip()); the FINISH comes at the GE's exact time, once the thread has counted the runs. Turns are skipped all the
//same (the thread let go of, the time is the exact one).
static auto spinsGeLooksThreaded() -> void {
  auto make = [](Look look, bool recompile, bool threaded) {
    auto m = std::make_unique<KernelMachine>();
    m->system.threadGE(threaded);
    m->kernel.startTime = 1'700'000'000ull * 1'000'000;
    geWait(*m, look);
    m->kernel.spins = threaded;
    m->system.recompiler.enabled = recompile;
    m->system.power(0x0880'1000);
    s32 uid = m->kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
    m->kernel.startThread(*m->kernel.threads[uid], 0, 0);
    return m;
  };
  for(bool recompile : {false, true}) {
    for(auto look : {Look::Draw, Look::List, Look::Stalled}) {
      auto held = make(look, recompile, true), every = make(look, recompile, false);
      std::atomic<bool> hold{false};
      held->system.geThreading->drain();  //(the thread reads its gate as it takes each record)
      held->system.geThreading->gate = [&](u64) { while(hold.load()) std::this_thread::yield(); };
      bool same = true;
      for(u32 step = 0; step < 32 && !(held->kernel.exited && every->kernel.exited); step++) {
        hold = true;
        std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); hold = false; });
        held->kernel.run(Step);
        release.join();
        every->kernel.run(Step);
        if(saveState(*held) != saveState(*every)) {
          if(same) std::printf("  look %u: states differ after step %u\n", u32(look), step + 1);
          same = false;
        }
      }
      held->system.geThreading->drain();
      held->system.geThreading->gate = nullptr;
      CHECK(same, true);
      CHECK(held->kernel.exited && every->kernel.exited, true);
      CHECK(held->system.memory.read(4, Finished), Rounds);
      CHECK(held->kernel.spun > 0, true);
    }
  }
}

//With the PSP's GE timing and the GE on its thread, the GE asked after (sceGeDrawSync(1)) by a poll whose turns move
//lines of the CPU's caches while its list waits at its stall address past its first sprite, the GE drawing that sprite
//and then idle (geWait()'s Look::Drawing: sprites textured from RAM, whose texels weigh the lines the CPU moves): with
//no FINISH due, nothing in a turn folds the run handed over, and the skip decides from the busy window after folding
//it (spinSkip(): the GE done part way through the turns it would skip refuses it). Skipped with the GE on its thread,
//run free and held back at the start of every step, against the GE on the emulation thread with every turn run: every
//step's state the same, and in the end the caches' counts too.
static auto spinsGeDrawingThreaded() -> void {
  auto make = [](Traffic traffic, bool threaded) {
    auto m = std::make_unique<KernelMachine>();
    m->system.threadGE(threaded);
    m->kernel.startTime = 1'700'000'000ull * 1'000'000;
    m->pspTiming();
    geWait(*m, Look::Drawing, traffic);
    m->kernel.spins = threaded;
    m->system.recompiler.enabled = true;
    m->system.power(0x0880'1000);
    s32 uid = m->kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
    m->kernel.startThread(*m->kernel.threads[uid], 0, 0);
    return m;
  };
  for(bool held : {false, true}) {
    for(auto traffic : {Traffic::Uncached, Traffic::Missing}) {
      auto on = make(traffic, true), every = make(traffic, false);
      std::atomic<bool> hold{false};
      if(held) {
        on->system.geThreading->drain();  //(the thread reads its gate as it takes each record)
        on->system.geThreading->gate = [&](u64) { while(hold.load()) std::this_thread::yield(); };
      }
      bool same = true;
      for(u32 step = 0; step < 32 && !(on->kernel.exited && every->kernel.exited); step++) {
        if(held) {
          hold = true;
          std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(5)); hold = false; });
          on->kernel.run(Step);
          release.join();
        } else {
          on->kernel.run(Step);
        }
        every->kernel.run(Step);
        if(saveState(*on) != saveState(*every)) {
          if(same) {
            std::printf("  held %u traffic %u: states differ after step %u\n", u32(held), u32(traffic), step + 1);
          }
          same = false;
        }
      }
      if(held) on->system.geThreading->drain(), on->system.geThreading->gate = nullptr;
      u64 counts[2][4];
      on->kernel.spinCounts(counts[0]), every->kernel.spinCounts(counts[1]);
      CHECK(same, true);
      CHECK(std::equal(counts[0], counts[0] + 4, counts[1]), true);
      CHECK(on->kernel.exited && every->kernel.exited, true);
      CHECK(on->system.memory.read(4, Finished), Rounds);
      CHECK(on->kernel.spun > 1000, true);
    }
  }
}

//With the PSP's GE timing, the hybrid's cap in the spin skip (spinSkip(): a cap binding at either end of the turns a
//skip would skip refuses it, Kernel::spinRefused(), counted). The list: a sprite over the screen textured from a
//512x512 8888 texture in RAM read wide, its texels nearly all of its clocks (the back end's kind, which costs the GE a
//fixed time a line). Beside it main reads a word of each of 4,096 lines, waits 1.8 ms (the window's time still the
//CPU's, its stalls beside the GE with it, about 1.5 times the cycles passed) and asks after the list (sceGeListSync(id,
//1)) till it's done: the window owes past the cap as the spin starts, and as its turns run the CPU's time grows faster
//than the cycles passed, so what the window owes falls through the cap and its push, held there, would fall after it.
//Every step's state the same with the skip on as off; on the recompiler, skips refused while the cap binds, then taken,
//turns skipped by the thousand, and the instructions counted as run in place of the refused skips those of the turns
//that ran till the next try (on the interpreter, whose accesses aren't counted, nothing is owed: its runs check that
//the skips are exact). The same with the poll's go ended a few microseconds in by a thread woken (Event::Thread) or an
//alarm's handler (Event::Alarm), either then running 3,002 instructions of its own: the refused skip the go's end cuts
//short counted for its own turns alone. And beside sprites textured from RAM (both kinds' parts), main reading three
//lines each turn: skipped as run, none refused.
static auto spinsGeCapped() -> void {
  constexpr u32 Texture = 0x0900'0000, Lines = 0x0940'0000;
  enum class Event : u32 { None, Thread, Alarm };
  u32 turn = 0;  //(the poll's instructions a turn: its loop's words and its function's stub's two, jr ra and syscall)
  auto capped = [&turn](Event event) { return [&turn, event](KernelMachine& m) {
    auto& memory = m.system.memory;
    for(u32 n = 0; n < 512 * 512; n++) memory.write(4, Texture + n * 4, 0xff00'0000 | (n * 0x0305'0709 & 0xff'ffff));
    u32 at = GeList;
    auto put = [&](u32 command, u32 argument = 0) {
      memory.write(4, at, command << 24 | (argument & 0xff'ffff));
      at += 4;
    };
    put(GE::FrameBufferPointer, 0); put(GE::FrameBufferWidth, 512); put(GE::FrameBufferPixelFormat, 3);
    put(GE::Scissor1, 0); put(GE::Scissor2, 271 << 10 | 479); put(GE::Region2, 1023 << 10 | 1023);
    put(GE::VertexType, 0x80'0102);  //(through: 16-bit texture coordinates and positions)
    put(GE::TextureMappingEnable, 1); put(GE::TextureAddress0, Texture);
    put(GE::TextureBufferWidth0, (Texture >> 24) << 16 | 512); put(GE::TextureSize0, 9 << 8 | 9);
    put(GE::TextureFormat, 3); put(GE::TextureFunction, 3);
    put(GE::Base, GeVertices >> 8 & 0xf'0000); put(GE::VertexAddress, GeVertices);
    u32 vertex = GeVertices;
    for(u32 half : {0u, 0u, 0u, 0u, 0u, 512u, 512u, 480u, 272u, 0u}) memory.write(2, vertex, half), vertex += 2;
    put(GE::Primitive, 6 << 16 | 2);  //(a sprite)
    put(GE::Finish); put(GE::End);
    Assembler main{m, 0x0880'1000};
    main.li(a0, GeList); main.li(a1, 0); main.li(a2, 0xffff'ffff); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.put(addu(s0, v0, zero));
    main.li(s1, Lines); main.li(s2, 256);  //(4,096 lines, 16 a turn of the loop)
    u32 reads = main.here();
    for(u32 n = 0; n < 16; n++) main.put(lw(t1, s16(n * 64), s1));
    main.put(addiu(s1, s1, 1024)); main.put(addiu(s2, s2, -1));
    main.put(bne(s2, zero, branchTo(main, reads))); main.put(nop);
    auto busy = [](Assembler& a) {  //(3,002 instructions: two, then a thousand turns of three)
      a.li(t0, 1000);
      u32 again = a.here();
      a.put(addiu(t0, t0, -1)); a.put(bne(t0, zero, branchTo(a, again))); a.put(nop);
    };
    if(event == Event::Thread) {  //(woken in the poll's go, its delay 6 microseconds longer than main's; then it ends)
      Assembler waker{m, 0x0880'3000};
      waker.li(a0, 1806); waker.call("sceKernelDelayThread");
      busy(waker);
      waker.call("sceKernelExitDeleteThread");
    }
    if(event == Event::Alarm) {  //(woken 210 microseconds before main, it sets an alarm due TimerLead on, and ends)
      Assembler waker{m, 0x0880'3000}, handler{m, 0x0880'3800};
      waker.li(a0, 1590); waker.call("sceKernelDelayThread");
      waker.li(a0, 1); waker.li(a1, handler.here()); waker.li(a2, 0); waker.call("sceKernelSetAlarm");
      waker.call("sceKernelExitDeleteThread");
      busy(handler);
      handler.put(addu(v0, zero, zero)); handler.put(jr(ra)); handler.put(nop);  //(0: the alarm ends)
    }
    if(event != Event::None) startThread(main, m, "waker", 0x0880'3000);  //(it runs at once, to its delay)
    main.li(a0, 1800); main.call("sceKernelDelayThread");
    u32 poll = main.here();
    main.put(addu(a0, s0, zero)); main.li(a1, 1); main.call("sceGeListSync");
    main.put(bne(v0, zero, branchTo(main, poll))); main.put(nop);
    turn = (main.here() - poll) / 4 + 2;
    main.call("sceKernelExitGame");
  }; };
  auto parts = [](KernelMachine& m) {
    geSprites(m, GeList, GeVertices, false, nullptr, true);
    Assembler main{m, 0x0880'1000};
    main.li(a0, GeList); main.li(a1, 0); main.li(a2, 0xffff'ffff); main.li(a3, 0);
    main.call("sceGeListEnQueue");
    main.put(addu(s0, v0, zero));
    u32 poll = main.here();
    main.li(t4, Words), main.put(lw(t5, 0, t4)), main.put(lw(t6, 0x2000, t4)), main.put(lw(t7, 0x4000, t4));
    main.put(addu(a0, s0, zero)); main.li(a1, 1); main.call("sceGeListSync");
    main.put(bne(v0, zero, branchTo(main, poll))); main.put(nop);
    main.call("sceKernelExitGame");
  };
  for(bool recompile : {false, true}) {
    u64 skipped = 0;
    for(Event event : {Event::None, Event::Thread, Event::Alarm}) {
      CHECK(bothWays(capped(event), recompile, 64, skipped, true), true);
      if(skipped <= 1000) {
        std::printf("  the cap's spin (event %u) skipped %llu turns\n", u32(event), (unsigned long long)skipped);
      }
      CHECK(skipped > 1000, true);
    }
    CHECK(bothWays(parts, recompile, 32, skipped, true), true);
    if(skipped <= 1000) std::printf("  both parts' spin skipped %llu turns\n", (unsigned long long)skipped);
    CHECK(skipped > 1000, true);
  }
  auto m = machine(capped(Event::None), true, true, true);
  m->kernel.run(Step * 64);
  CHECK(m->kernel.exited, true);
  auto& k = m->kernel;
  if(!k.geCapped || !k.geSkipsRefused || !k.geRefusedRun() || k.geRefusedRun() > k.geInstructions) {
    std::printf("  windows capped %llu; spin skips refused %llu, %llu instructions run in their place, of %llu\n",
                (unsigned long long)k.geCapped, (unsigned long long)k.geSkipsRefused,
                (unsigned long long)k.geRefusedRun(), (unsigned long long)k.geInstructions);
  }
  CHECK(k.geCapped, 1u);
  CHECK(k.geSkipsRefused > 0 && k.geRefusedRun() > 0, true);
  CHECK(k.geRefusedRun() < k.geInstructions, true);
  //The instructions run in place of the refused skips (Kernel::geRefusedRun()): a refusal leaves the detector's wait
  //as it was (the cap refuses before a proof could fail), so each is followed by SpinTurns turns of the poll till the
  //next try, and none cut short (the spin outlasts the cap), the refusals times that many turns of the poll.
  if(k.geRefusedRun() != k.geSkipsRefused * Kernel::SpinTurns * turn) {
    std::printf("  %llu instructions run in place of %llu refused skips, %u a turn\n",
                (unsigned long long)k.geRefusedRun(), (unsigned long long)k.geSkipsRefused, turn);
  }
  CHECK(k.geRefusedRun(), k.geSkipsRefused * Kernel::SpinTurns * turn);
  //With the go cut short. After the sleep the poll anchors at its first call, has its period at its second and tries a
  //skip every SpinTurns turns from its sixth, so its j-th try's call comes 15 + 36j instructions into the go (6 before
  //the first call, 9 a turn), with go - 16 - 36j instructions of room for the turns the skip would skip, the last
  //SpinMargin of them kept to run. The thread's wake ends the go 1,992 instructions in (its delay 6 microseconds of 333
  //cycles longer than main's, less the 6 instructions from its delay's call to main's); the alarm, 1,668 in (due
  //TimerLead after the waker's call, 9 instructions into its go, which began 210 microseconds and 6 cycles before
  //main's). So the last try in the go, the 54th or the 45th, has 32 instructions of room, 3 turns, and its skip would
  //have run 1 turn; the next try would come after the go's end, and so after the event's 3,002 instructions, none of
  //them the spin's. With every try till then refused, the instructions run in place of the refused skips are SpinTurns
  //turns of the poll for each but that one, and 1 turn for it.
  for(Event event : {Event::Thread, Event::Alarm}) {
    m = machine(capped(event), true, true, true);
    m->kernel.run(Step * 64);
    CHECK(m->kernel.exited, true);
    u64 tries = event == Event::Thread ? 54 : 45, refused = m->kernel.geSkipsRefused;
    u64 expected = refused ? ((refused - 1) * Kernel::SpinTurns + 1) * turn : 0;
    if(refused < tries || m->kernel.geRefusedRun() != expected) {
      std::printf("  %s: %llu instructions run in place of %llu refused skips (%llu tries in the go), %u a turn\n",
                  event == Event::Thread ? "a thread woken" : "an alarm's handler",
                  (unsigned long long)m->kernel.geRefusedRun(), (unsigned long long)refused,
                  (unsigned long long)tries, turn);
    }
    CHECK(refused >= tries, true);
    CHECK(m->kernel.geRefusedRun(), expected);
  }
  //On the interpreter, whose accesses aren't counted, the window owes nothing and no skip is refused: its runs above
  //check that the skips are exact, nothing of the cap.
  m = machine(capped(Event::None), false, true, true);
  m->kernel.run(Step * 64);
  CHECK(m->kernel.exited, true);
  CHECK(m->kernel.geCapped + m->kernel.geSkipsRefused, 0u);
  m = machine(parts, true, true, true);
  m->kernel.run(Step * 32);
  CHECK(m->kernel.exited, true);
  CHECK(m->kernel.geCapped + m->kernel.geSkipsRefused, 0u);
  CHECK(m->kernel.spun > 1000, true);
}

//With the PSP's GE timing, the GE asked after (sceGeDrawSync(1)) by a poll whose turns move lines of the CPU's caches
//while its list waits at its stall address past its first sprite (geWait()'s Look::Drawing: sprites textured from RAM,
//the GE drawing that sprite and then idle, no FINISH due, so the go goes on past the GE's end): a skip whose turns the
//GE would be done part way through is refused (spinSkip()). Its one stretch would count the share of the turns' lines
//that fell before the GE's end as it stood, where run one by one each turn's lines push that end back as they come, so
//that later turns fall before it too. Skips are taken once the GE is idle. The mover comes every 4000 microseconds, so
//steps end while the GE waits at the stall, its end as the turns left it (moved within the step, the GE's time would
//start afresh from the clock as it went on: geResume()). Every step's state the same with the skip on as off, and in
//the end the caches' counts; on both engines (the interpreter's accesses aren't counted).
static auto spinsGePartway() -> void {
  for(bool recompile : {false, true}) {
    for(auto traffic : {Traffic::Uncached, Traffic::Missing}) {
      u64 skipped = 0;
      Kernel::SpinChecks checks;
      CHECK(bothWays([traffic](KernelMachine& m) { geWait(m, Look::Drawing, traffic, 4000); }, recompile, 48, skipped,
                     true, &checks), true);
      if(skipped <= 1000 || !checks.partway) {
        std::printf("  traffic %u skipped %llu turns, %llu tries refused part way\n", u32(traffic),
                    (unsigned long long)skipped, (unsigned long long)checks.partway);
      }
      CHECK(skipped > 1000, true);
      CHECK(checks.partway > 0, true);  //(its refusal reached)
    }
  }
}

//A poll of two calls a turn begun after a sleep beside the GE: main queues a list of count one-pixel sprites whose
//corners it reads from RAM (geDots()), sleeps 600 microseconds and then, till the list is done, works out 200
//instructions, asks after the list (sceGeListSync(id, 1)), reads a word of RAM through an uncached mirror and asks its
//thread's ID. From the list's call, the spin's anchor (the first call after the sleep), the turn's line comes in its
//first stretch, none in its second.
auto geTwoCalls(KernelMachine& m, u32 count) -> void {
  geDots(m, GeList, GeVertices, count);
  Assembler main{m, 0x0880'1000};
  main.li(a0, GeList); main.li(a1, 0); main.li(a2, 0xffff'ffff); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  main.put(addu(s0, v0, zero));
  main.li(a0, 600); main.call("sceKernelDelayThread");
  main.li(s1, (Words + 0x8000) | 0x4000'0000);
  u32 poll = main.here();
  main.put(addu(t0, zero, zero));
  for(u32 n = 0; n < 100; n++) main.put(addiu(t0, t0, 3)), main.put(xor_(t1, t0, s2));
  main.put(addu(a0, s0, zero)); main.li(a1, 1); main.call("sceGeListSync");
  main.put(addu(s2, v0, zero));
  main.put(lw(t5, 0, s1));
  main.call("sceKernelGetThreadId");
  main.put(bne(s2, zero, branchTo(main, poll))); main.put(nop);
  main.call("sceKernelExitGame");
}

//With the PSP's GE timing, a poll of two calls a turn moving a line (geTwoCalls()), past the kink in its busy window's
//push: the window's time the CPU's but its cycles passed still mostly the sleep's, so the push it owes rises through
//each turn's first stretch (its line) and falls through its second (instructions only: the CPU's time growing faster
//than the cycles passed), rising from turn to turn. Run one by one the turns push the GE back to the most it owes,
//after the last first stretch; one stretch for the turns a skip would take, to what it owes at their end, after a
//second stretch: less (the skip below: 38806 cycles pushed against 38827). Such a skip is refused (spinSkip(): several
//calls a turn with lines moved), counted (Kernel::spinChecks). Every step's state the same with the skip on as off, and
//in the end the caches' counts; on both engines (the interpreter's accesses aren't counted: its window owes nothing,
//and its turns are skipped).
static auto spinsGeTwoCalls() -> void {
  for(bool recompile : {false, true}) {
    u64 skipped = 0;
    Kernel::SpinChecks checks;
    CHECK(bothWays([](KernelMachine& m) { geTwoCalls(m, 40000); }, recompile, 32, skipped, true, &checks), true);
    if(recompile) {
      CHECK(checks.calls > 0, true);  //(its refusal reached)
      continue;
    }
    if(skipped <= 1000) std::printf("  skipped %llu turns\n", (unsigned long long)skipped);
    CHECK(skipped > 1000, true);
  }
  //The turns after such a skip would make up what it fell short by, each first stretch's push the most yet, before a
  //step's end: so at once too, as spinsDirect makes a skip. Two machines run with the skip off to the list's call just
  //past the kink, the skip then on in one of them for the next five calls (the detector's turns, steady, the last
  //moving a line with two calls); its next call made in a go of its own with room for 20 turns and the margin, against
  //the other's running the turns that call skipped: the same at once. That try is refused by its check alone: what
  //the others need holds there, so that the check can't pass because another refusal stopped it.
  Unthreaded unthreaded;  //(a go made by hand: the synchronous GE's, no bound of the GE thread's in it)
  //(each step a go of one instruction, at most Steps of them: a timing change keeping the window short of its kink till
  //the list ends, after which the program ends and a step does nothing, fails here instead of hanging)
  constexpr u64 Steps = 200'000;
  auto positioned = [](bool spins) {
    auto m = machine([](KernelMachine& m) { geTwoCalls(m, 40000); }, true, false, true);
    u32 stub = m->stub("sceGeListSync");
    u64 steps = 0;
    auto toStub = [&] { do m->kernel.run(1); while(m->system.ipu.pc != stub && ++steps < Steps); };
    m->kernel.run(200'000);
    do toStub(); while(!m->kernel.geContendBound(m->kernel.geWindow) && steps < Steps);
    m->kernel.spins = spins;
    for(u32 visit = 0; visit <= Kernel::SpinTurns && steps < Steps; visit++) toStub();
    if(steps >= Steps) std::printf("  not at the list's call past the kink in %llu steps\n", (unsigned long long)steps);
    CHECK(steps < Steps, true);
    return m;
  };
  auto go = [](KernelMachine& m, u64 room) {  //(the stub's jr, then its syscall in a go: spinsDirect's anchor())
    auto& k = m.kernel;
    m.system.instruction();
    k.counted = 0, k.going = true;
    m.system.instructionsRun = 1;  //(the jr, counted as the syscall catches the clock up)
    m.system.runLimit = room;
    m.system.instruction();
    k.going = false;
    u64 ran = m.system.instructionsRun + 1;  //(and the syscall)
    k.cycles += ran - k.counted, k.counted = ran;
  };
  auto x = positioned(true), y = positioned(false);
  CHECK(saveState(*x) == saveState(*y), true);
  u64 room = (20 + Kernel::SpinMargin) * x->kernel.spin.period + 2;  //(as spinsDirect's, and the jr counted)
  auto& k = x->kernel;
  auto before = k.spinChecks;
  go(*x, room);
  auto& spin = k.spin;
  CHECK(spin.turns == 0 && spin.steady && spin.calls == 2 && spin.moved[2] == 1, true);  //(the try made)
  //and refused by its own check alone: what the others need holds (the GE busy past the 20 turns, and the window's
  //time the same kind at both ends, the turns' cycles and lines added as spinSkip() adds them), and only that check's
  //count went up
  u64 skips = 20 * spin.period;
  auto end = k.geWindow;
  end.passed += skips, end.instructions += skips;
  end.fills += 20 * (spin.moved[0] + spin.moved[3]), end.writebacks += 20 * spin.moved[1];
  end.uncached += 20 * spin.moved[2];
  CHECK(k.geDoneAt > k.cycles + skips, true);
  CHECK(k.geContendBound(k.geWindow) == k.geContendBound(end), true);
  CHECK(k.spinChecks.calls == before.calls + 1 && k.spinChecks.partway == before.partway &&
        k.spinChecks.unsteady == before.unsteady && k.spinChecks.kind == before.kind, true);
  u64 turns = x->kernel.spun;
  go(*y, room);
  if(turns) y->kernel.run(turns * x->kernel.spin.period);
  if(saveState(*x) != saveState(*y)) {
    std::printf("  at once: %llu turns skipped; the GE done at %llu against %llu, pushed %llu against %llu\n",
                (unsigned long long)turns, (unsigned long long)x->kernel.geDoneAt,
                (unsigned long long)y->kernel.geDoneAt, (unsigned long long)x->kernel.geWindow.pushed,
                (unsigned long long)y->kernel.geWindow.pushed);
  }
  CHECK(saveState(*x) == saveState(*y), true);
}

//A poll whose turns settle by its fifth: main queues a list of 16 sprites (geSprites()), then asks the GE how it's
//doing (sceGeDrawSync(1)) till done, each turn counting k = min(k + 1, 5) in memory (stored every turn, without a
//branch) and loading a word from a line chosen without a branch: on its fifth turn (k 4 on entry) a line it hasn't
//read before, else one it reads every turn. The word is 0 either way, and the registers that chose and loaded it are
//cleared later in the turn, so from the fifth turn on every turn leaves memory and the registers as it found them.
//Three lists, each with its own fresh line, k cleared before each.
auto settling(KernelMachine& m) -> void {
  constexpr u32 K = Spare, Resident = Words, Fresh = Words + 0x3000;
  geSprites(m, GeList, GeVertices);
  Assembler main{m, 0x0880'1000};
  main.li(s4, 0);
  u32 round = main.here();
  main.put(addiu(s4, s4, 1));
  main.li(t0, K); main.put(sw(zero, 0, t0));
  main.put(sll(s3, s4, 6)); main.li(t1, Fresh); main.put(addu(s3, s3, t1));  //(this list's fresh line)
  main.li(a0, GeList); main.li(a1, 0); main.li(a2, 0xffff'ffff); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  u32 poll = main.here();
  main.li(a0, 1); main.call("sceGeDrawSync");
  main.put(addu(s5, v0, zero));
  main.li(t0, K); main.put(lw(t1, 0, t0));
  main.put(xori(t2, t1, 4));  //(0 on the fifth turn)
  main.li(t3, Resident); main.put(movz(t3, s3, t2));
  main.put(lw(t5, 0, t3));
  main.put(addiu(t1, t1, 1)); main.li(t6, 5); main.put(min(t1, t1, t6));
  main.put(sw(t1, 0, t0));
  main.put(addu(t2, zero, zero)); main.put(addu(t3, zero, zero)); main.put(addu(t5, zero, zero));
  main.put(bne(s5, zero, branchTo(main, poll))); main.put(nop);
  main.put(sltiu(t3, s4, Rounds));
  main.put(bne(t3, zero, branchTo(main, round))); main.put(nop);
  main.call("sceKernelExitGame");
}

//With the PSP's GE timing, a poll whose turns settle by its fifth (settling()), its fifth turn reading a line in and
//none after: its first try comes after that fifth turn, the last two turns unlike, and is refused (spinSkip()): a skip
//there would count every turn it skips as moving what the fifth did. The next try, the last two turns alike, is taken.
//Every step's state the same with the skip on as off, and in the end the caches' counts; on both engines (the
//interpreter's accesses aren't counted: its turns alike from the first).
static auto spinsGeSettling() -> void {
  for(bool recompile : {false, true}) {
    u64 skipped = 0;
    Kernel::SpinChecks checks;
    CHECK(bothWays(settling, recompile, 32, skipped, true, &checks), true);
    if(skipped <= 1000) std::printf("  skipped %llu turns\n", (unsigned long long)skipped);
    CHECK(skipped > 1000, true);
    if(recompile) CHECK(checks.unsteady > 0, true);  //(its refusal reached; the interpreter's turns are all alike)
  }
}

constexpr u32 Answers = R + 0x400, Mark = R + 0x480, CallbackRuns = R + 0x490, Buffer = 0x0893'0000;

//Where a poll's anchor is, for a skip made directly: sceGeDrawSync's syscall, and where main goes on.
struct Anchor { u32 site = 0, back = 0; };

//What a poll's turn does with a semaphore besides asking the GE how it's doing (semaphorePoll()), storing each call's
//result:
//  Free        Need for Speed: Shift's: one at 0 (its most 1) signalled and taken back with a wait, twice
//  OneMore     one with a large count signalled once and taken twice: one more taken than given, each turn
//  WaitCB      one at 1 taken with sceKernelWaitSemaCB (the thread has a callback, not notified) and given back
//  CanNotWait  one at 1 waited on with interrupts held off round the wait (mtic): CAN_NOT_WAIT
//  Unknown     a deleted one's ID waited on: UNKNOWN_SEMAPHORE
//  NoCount     one at 1 waited on for 0: ILLEGAL_COUNT
//  PastMost    one at 1, its most, waited on for 2: ILLEGAL_COUNT
//  Held        one at 0 (its most 1) signalled (to 1), signalled again (refused past its most, the turn's own signal
//              counted) and taken back
//  Waiter      one another thread waits on for 2, signalled and taken back for 1 (that thread never wakes)
//  WouldWait   one at 0 that another thread signals every 500 microseconds, waited on: the wait waits
enum class Sema : u32 { Free, OneMore, WaitCB, CanNotWait, Unknown, NoCount, PastMost, Held, Waiter, WouldWait };

//The thread's callback (at 0x0880'4800): one more run counted at CallbackRuns, the clock it ran at after that count.
auto threadCallback(KernelMachine& m) -> void {
  Assembler callback{m, 0x0880'4800};
  callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
  callback.call("sceKernelGetSystemTimeLow");
  callback.li(t0, CallbackRuns); callback.put(lw(t1, 0, t0)); callback.put(addiu(t1, t1, 1));
  callback.put(sw(t1, 0, t0)); callback.put(sw(v0, 4, t0));
  callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
  callback.li(v0, 0); callback.put(jr(ra)); callback.put(nop);  //(0: kept)
}

//main starts an asynchronous read of DATA.BIN on the memory stick (stick: its host folder, mounted as ms0), with the
//callback in s7 as the file's: done between goes, 100 microseconds and its bytes at the stick's rate after it's made,
//which notifies the callback.
auto readAsync(KernelMachine& m, Assembler& main, const std::string& stick) -> void {
  m.kernel.mount("ms0", stick);
  main.li(a0, m.string("ms0:/DATA.BIN")); main.li(a1, 1); main.li(a2, 0); main.call("sceIoOpen");
  main.put(addu(s2, v0, zero));
  main.put(addu(a0, s2, zero)); main.put(addu(a1, s7, zero)); main.li(a2, 0x55); main.call("sceIoSetAsyncCallback");
  main.put(addu(a0, s2, zero)); main.li(a1, Buffer); main.li(a2, 4000); main.call("sceIoReadAsync");
}

//A poll of the GE that also uses a semaphore each turn (what), as the EA games' loops do (geWait() leaves that out):
//main makes the semaphore (and starts the thread that waits on it or signals it), then queues a list of 16 sprites
//(geSprites()) and, till the GE says it's done, asks sceGeDrawSync(1), the turn's first call and so its anchor, and
//makes the turn's semaphore calls, storing each result at Answers on; three lists, the clock noted after each. With
//stick (WaitCB), main first starts an asynchronous read whose callback is the thread's (readAsync()): the poll's next
//CB wait once it's done runs the callback.
auto semaphorePoll(KernelMachine& m, Sema what, const std::string& stick = {}) -> Anchor {
  threadCallback(m);
  Assembler main{m, 0x0880'1000};
  u32 count = what == Sema::OneMore ? 0x4000'0000 : what == Sema::Free || what == Sema::Held ||
              what == Sema::Waiter || what == Sema::WouldWait ? 0 : 1;
  u32 most = what == Sema::OneMore || what == Sema::Waiter ? 0x7fff'ffff : 1;
  main.li(a0, m.string("sema")); main.li(a1, 0); main.li(a2, count); main.li(a3, most); main.li(t0, 0);
  main.call("sceKernelCreateSema");
  main.put(addu(s6, v0, zero));
  main.li(t0, Spare); main.put(sw(s6, 0, t0));  //(for the other thread)
  if(what == Sema::Unknown) main.put(addu(a0, s6, zero)), main.call("sceKernelDeleteSema");
  if(what == Sema::Waiter || what == Sema::WouldWait) {  //(better than main: it runs at once, to its wait or delay)
    Assembler other{m, 0x0880'3000};
    u32 again = other.here();
    if(what == Sema::WouldWait) other.li(a0, 500), other.call("sceKernelDelayThread");
    other.li(t0, Spare); other.put(lw(a0, 0, t0));
    if(what == Sema::Waiter) other.li(a1, 2), other.li(a2, 0), other.call("sceKernelWaitSema");
    else other.li(a1, 1), other.call("sceKernelSignalSema");
    other.put(beq(zero, zero, branchTo(other, again))); other.put(nop);
    startThread(main, m, "other", 0x0880'3000);
  }
  if(what == Sema::WaitCB) {
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'4800); main.li(a2, 0); main.call("sceKernelCreateCallback");
    main.put(addu(s7, v0, zero));
    if(!stick.empty()) readAsync(m, main, stick);
  }
  geSprites(m, GeList, GeVertices);
  main.li(s4, 0);
  u32 round = main.here();
  main.put(addiu(s4, s4, 1));
  main.li(a0, GeList); main.li(a1, 0); main.li(a2, 0xffff'ffff); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  u32 poll = main.here();
  main.li(a0, 1); main.call("sceGeDrawSync");
  Anchor anchor{m.stub("sceGeDrawSync") + 4, main.here()};
  main.put(addu(s5, v0, zero));
  u32 answer = Answers;
  auto store = [&] { main.li(t0, answer); main.put(sw(v0, 0, t0)); answer += 4; };
  auto signal = [&] { main.put(addu(a0, s6, zero)); main.li(a1, 1); main.call("sceKernelSignalSema"); store(); };
  auto wait = [&](u32 count, const char* name = "sceKernelWaitSema") {
    main.put(addu(a0, s6, zero)); main.li(a1, count); main.li(a2, 0); main.call(name); store();
  };
  switch(what) {
  case Sema::Free: signal(); wait(1); signal(); wait(1); break;
  case Sema::OneMore: signal(); wait(1); wait(1); break;
  case Sema::WaitCB: wait(1, "sceKernelWaitSemaCB"); signal(); break;
  case Sema::CanNotWait: main.put(mtic(zero)); wait(1); main.li(t1, 1); main.put(mtic(t1)); break;
  case Sema::Unknown: case Sema::WouldWait: wait(1); break;
  case Sema::NoCount: wait(0); break;
  case Sema::PastMost: wait(2); break;
  case Sema::Held: signal(); signal(); wait(1); break;
  case Sema::Waiter: signal(); wait(1); break;
  }
  main.put(bne(s5, zero, branchTo(main, poll))); main.put(nop);
  main.call("sceKernelGetSystemTimeLow");
  main.put(sll(t1, s4, 2)); main.li(t2, Seen); main.put(addu(t2, t2, t1)); main.put(sw(v0, 0, t2));
  main.put(sltiu(t3, s4, Rounds));
  main.put(bne(t3, zero, branchTo(main, round))); main.put(nop);
  main.call("sceKernelExitGame");
  return anchor;
}

//Who else is ready as a poll's turn signals a semaphore with interrupts on (switching()): a thread better than main
//(0x10), its delay ended while main held interrupts off; one of main's own priority, ready all along; or a better one
//suspended in its delay (sceKernelSuspendThread), ready once it ends but passed over.
enum class Other : u32 { Better, Equal, Suspended };

//A poll (sceGeDrawSync(1), its turn's first call and so its anchor) that also takes a binary semaphore at 1 and gives
//it back, its signal made with interrupts held off (mtic) for its first five turns and on from then (a count stored
//each turn, without a branch), while another thread is ready (other); it ends once that thread has run (its mark) or
//two vertical blanks have passed. With interrupts on, the signal hands the CPU to a better thread there
//(reschedule()), to none of main's priority nor to a suspended one. With anchorOff, interrupts are held off instead
//from the turn's start to its wait, so that the anchor's call is made with them off and the signal with them on: the
//CPU's flag at the anchor isn't the turn's at the signal. (The programs of the design's review, finding 3.1 and its
//round two, the GE asked after.)
auto switching(KernelMachine& m, Other other, bool anchorOff = false) -> void {
  Assembler thread{m, 0x0880'3000};
  thread.li(a0, 100); thread.call("sceKernelDelayThread");
  thread.call("sceKernelGetSystemTimeLow");
  thread.put(ori(v0, v0, 1)); thread.li(t0, Mark); thread.put(sw(v0, 0, t0));
  u32 again = thread.here();
  thread.li(a0, 1'000'000); thread.call("sceKernelDelayThread");
  thread.put(beq(zero, zero, branchTo(thread, again))); thread.put(nop);
  Assembler main{m, 0x0880'1000};
  main.li(a0, m.string("binary")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 1); main.li(t0, 0);
  main.call("sceKernelCreateSema");
  main.put(addu(s0, v0, zero));
  main.call("sceDisplayGetVcount");
  main.put(addu(s4, v0, zero));
  main.li(a0, m.string("other")); main.li(a1, 0x0880'3000); main.li(a2, other == Other::Equal ? 0x20 : 0x10);
  main.li(a3, 0x1000); main.li(t0, 0); main.li(t1, 0);
  main.call("sceKernelCreateThread");
  main.put(addu(s3, v0, zero));
  main.put(addu(a0, s3, zero)); main.li(a1, 0); main.li(a2, 0);
  main.call("sceKernelStartThread");  //(a better one runs at once, to its delay; one of main's priority is ready)
  if(other == Other::Suspended) main.put(addu(a0, s3, zero)), main.call("sceKernelSuspendThread");
  main.put(mtic(zero));  //(held off past the better one's delay: it's ready, and main keeps the CPU)
  main.li(t0, 200'000);
  u32 busy = main.here();
  main.put(addiu(t0, t0, -1)); main.put(bne(t0, zero, branchTo(main, busy))); main.put(nop);
  main.li(t9, 1);
  if(!anchorOff) main.put(mtic(t9));
  main.li(s1, Count); main.li(s2, Mark);
  u32 loop = main.here();
  if(anchorOff) main.put(mtic(zero));
  main.li(a0, 1); main.call("sceGeDrawSync");
  main.call("sceDisplayGetVcount");
  main.put(subu(t6, v0, s4));
  if(anchorOff) main.put(mtic(t9));
  main.put(addu(a0, s0, zero)); main.li(a1, 1); main.li(a2, 0); main.call("sceKernelWaitSema");
  main.put(lw(t0, 0, s1));
  main.put(sltiu(t1, t0, 5)); main.put(xori(t2, t1, 1)); main.put(mtic(t2));  //(on from the sixth turn)
  main.put(addu(a0, s0, zero)); main.li(a1, 1); main.call("sceKernelSignalSema");
  if(!anchorOff) main.put(mtic(t9));
  main.put(addu(t3, t0, t1)); main.put(sw(t3, 0, s1));
  main.put(lw(t5, 0, s2));
  main.put(sltiu(t7, t6, 2)); main.put(xori(t7, t7, 1));
  main.put(sltu(t8, zero, t5)); main.put(or_(t8, t8, t7));
  main.put(addu(t0, zero, zero)); main.put(addu(t1, zero, zero)); main.put(addu(t2, zero, zero));
  main.put(addu(t3, zero, zero));
  main.put(beq(t8, zero, branchTo(main, loop))); main.put(nop);
  main.put(mtic(t9));
  main.call("sceKernelExitGame");
}

//The anchor's syscall made in a go of its own, as a go would make it (spinsDirect's anchor()): main about to make it,
//the detector a turn short of a proof with the turn's period, and room for turns turns and the margin. The turns
//skipped.
auto anchorGo(KernelMachine& m, const Anchor& anchor, u64 period, u64 turns) -> u64 {
  auto& k = m.kernel;
  k.spins = true;
  k.spin = {anchor.site, anchor.back, k.cycles - period, period, Kernel::SpinTurns - 1, 0};
  k.counted = 0;
  k.going = true;
  m.system.instructionsRun = 0;
  m.system.runLimit = (turns + Kernel::SpinMargin) * period + 1;
  m.system.instruction();
  k.going = false;
  u64 ran = m.system.instructionsRun + 1;  //(and the syscall)
  k.cycles += ran - k.counted, k.counted = ran;
  return k.spun;
}

//Machines for skips made directly on a poll's program (one returning its anchor): each run with the skip off till main
//is about to make the anchor's syscall at the second turn of its poll (the GE drawing the first list; the registers
//and the stored results as each turn leaves them); and the turn's period, stepped round once on one of them. Each step
//is a go of one instruction, at most Steps of them: a change keeping main from the anchor fails here instead of
//hanging.
struct AtAnchor {
  static constexpr u64 Steps = 1'000'000;
  std::function<Anchor(KernelMachine&)> program;
  Anchor anchor;
  auto at(KernelMachine& m) const -> bool { return m.system.ipu.pc == anchor.site && m.system.ipu.pd == anchor.back; }
  auto make() -> std::unique_ptr<KernelMachine> {
    auto m = machine([this](KernelMachine& m) { anchor = program(m); }, false, false);
    u64 steps = 0;
    while(!at(*m) && ++steps < Steps) m->kernel.run(1);
    do m->kernel.run(1); while(!at(*m) && ++steps < Steps);
    CHECK(at(*m), true);
    return m;
  }
  auto period(KernelMachine& m) const -> u64 {
    u64 period = 0;
    do m.kernel.run(1), period++; while(!at(m) && period < Kernel::SpinLongest);
    return period;
  }
};

//A callback notified as a poll's turn begins (as a file's request done between goes notifies it): skipped directly
//(atAnchor's machines, "Fast"'s timing and the synchronous GE, as spinsDirect makes them, with room for 500 turns),
//with none due the turns are skipped and the state is the same as with them run; with the thread's callback notified
//first the proof stops where the turn would run it (a CB wait, or a callback check), the detector waits longer, none is
//skipped, and the turn runs it. That the GE is busy past the turns (no FINISH falls due in them) is checked first.
auto notifiedDirectly(AtAnchor& atAnchor) -> void {
  constexpr u64 Turns = 500;
  auto a = atAnchor.make(), b = atAnchor.make(), c = atAnchor.make();
  u64 period = atAnchor.period(*c);
  if(period < 10 || period > 100) std::printf("  a turn of %llu instructions\n", (unsigned long long)period);
  CHECK(period >= 10 && period <= 100, true);
  CHECK(a->kernel.geDoneAt > a->kernel.cycles + (Turns + Kernel::SpinMargin + 1) * period, true);
  CHECK(anchorGo(*a, atAnchor.anchor, period, Turns), Turns);
  b->kernel.run(Turns * period + 1);
  CHECK(saveState(*a) == saveState(*b), true);
  auto d = atAnchor.make();
  auto& k = d->kernel;
  CHECK(k.callbacks.size(), 1u);
  CHECK(k.notifyCallback(k.callbacks.begin()->first, 1), true);
  CHECK(anchorGo(*d, atAnchor.anchor, period, Turns), 0);
  CHECK(k.spin.wait > 0, true);  //(the proof failed)
  CHECK(d->system.memory.read(4, CallbackRuns), 0u);
  k.run(period * 4);
  CHECK(d->system.memory.read(4, CallbackRuns), 1u);
}

//The EA games' loops (Need for Speed: Shift's, Burnout Dominator's and Legends'): the GE asked after, a semaphore
//signalled and taken back with a wait each turn, twice, the waits succeeding at once (semaphorePoll()'s Free): calm,
//worked out on the shadow, and the turns skipped (thousands) as run. A turn taking one more than it gives (OneMore)
//changes the semaphore: its proof fails at the counts' closing check, and it isn't skipped. Every step's state the same
//either way, on both engines.
static auto spinsSemaphore() -> void {
  for(bool recompile : {false, true}) {
    u64 skipped = 0;
    CHECK(bothWays([](KernelMachine& m) { semaphorePoll(m, Sema::Free); }, recompile, 32, skipped), true);
    if(skipped <= 1000) std::printf("  Free skipped %llu turns\n", (unsigned long long)skipped);
    CHECK(skipped > 1000, true);
    CHECK(bothWays([](KernelMachine& m) { semaphorePoll(m, Sema::OneMore); }, recompile, 32, skipped), true);
    if(skipped) std::printf("  OneMore skipped %llu turns\n", (unsigned long long)skipped);
    CHECK(skipped, 0);
  }
}

//A CB wait taking its semaphore at once, the thread's callback made but none notified (semaphorePoll()'s WaitCB), and
//the semaphore given back: calm, and the turns skipped as run. With an asynchronous read whose callback is the
//thread's, done between goes, the poll's next CB wait runs the callback, as with every turn run. And directly
//(notifiedDirectly()): a try with the callback notified isn't skipped, the proof stopping at the CB wait. Every step's
//state the same either way, on both engines.
static auto spinsSemaphoreCB() -> void {
  HostFolder stick;
  stick.put("DATA.BIN", std::string(4000, 'd'));
  auto read = [&](KernelMachine& m) { semaphorePoll(m, Sema::WaitCB, stick.path.string()); };
  for(bool recompile : {false, true}) {
    u64 skipped = 0;
    CHECK(bothWays([](KernelMachine& m) { semaphorePoll(m, Sema::WaitCB); }, recompile, 32, skipped), true);
    if(skipped <= 1000) std::printf("  WaitCB skipped %llu turns\n", (unsigned long long)skipped);
    CHECK(skipped > 1000, true);
    CHECK(bothWays(read, recompile, 32, skipped), true);
    if(skipped <= 1000) std::printf("  WaitCB with a read skipped %llu turns\n", (unsigned long long)skipped);
    CHECK(skipped > 1000, true);
    auto m = machine(read, recompile, true);
    m->kernel.run(Step * 32);
    CHECK(m->kernel.exited, true);
    CHECK(m->system.memory.read(4, CallbackRuns), 1u);
  }
  FastTimed fastTimed;
  Unthreaded unthreaded;
  AtAnchor atAnchor{[](KernelMachine& m) { return semaphorePoll(m, Sema::WaitCB); }, {}};
  notifiedDirectly(atAnchor);
}

//A semaphore's wait refused, its result stored each turn (semaphorePoll()): with interrupts held off round it
//(CAN_NOT_WAIT; the turn's first call, its anchor, made with them on, outside that), on a deleted semaphore
//(UNKNOWN_SEMAPHORE), for 0, and for more than its most (ILLEGAL_COUNT); and a signal refused past the semaphore's most
//only in the turn's view, after the turn's own signal (Held). Each refused call calm (but for ILLEGAL_CONTEXT, which
//comes only while interrupting, where nothing is skipped), worked out on the shadow as the kernel answers it, and the
//turns skipped as run. Every step's state the same either way, and each result the PSP's; on both engines.
static auto spinsSemaphoreRefused() -> void {
  struct Case { Sema what; std::vector<u32> answers; };
  std::vector<Case> cases{{Sema::CanNotWait, {Kernel::ErrorCanNotWait}},
                          {Sema::Unknown, {Kernel::ErrorUnknownSemaphore}},
                          {Sema::NoCount, {Kernel::ErrorIllegalCount}}, {Sema::PastMost, {Kernel::ErrorIllegalCount}},
                          {Sema::Held, {0, Kernel::ErrorSemaphoreOverflow, 0}}};
  for(bool recompile : {false, true}) {
    for(auto& item : cases) {
      Sema what = item.what;
      auto& answers = item.answers;
      u64 skipped = 0;
      auto program = [what](KernelMachine& m) { semaphorePoll(m, what); };
      CHECK(bothWays(program, recompile, 32, skipped), true);
      if(skipped <= 1000) std::printf("  case %u skipped %llu turns\n", u32(what), (unsigned long long)skipped);
      CHECK(skipped > 1000, true);
      auto m = machine(program, recompile, true);
      m->kernel.run(Step * 32);
      CHECK(m->kernel.exited, true);
      for(u32 n = 0; n < answers.size(); n++) CHECK(m->system.memory.read(4, Answers + n * 4), answers[n]);
    }
  }
}

//A semaphore's signal with interrupts on and another thread ready (switching()): where it would hand the CPU over (a
//better thread), no turn is skipped, the interrupts the turn's at the signal, not the CPU's at the anchor (anchorOff),
//and the better thread runs there as with every turn run; with one of main's priority ready, or a better one suspended,
//it hands over nothing, and the turns are skipped (thousands; a decline wider than reschedule()'s switch would lose
//them). Every step's state the same either way, on both engines.
static auto spinsSemaphoreSwitch() -> void {
  struct Case { Other other; bool anchorOff; };
  for(bool recompile : {false, true}) {
    for(auto item : {Case{Other::Better, false}, Case{Other::Better, true}, Case{Other::Equal, false},
                     Case{Other::Suspended, false}}) {
      Other other = item.other;
      bool anchorOff = item.anchorOff;
      u64 skipped = 0;
      auto program = [other, anchorOff](KernelMachine& m) { switching(m, other, anchorOff); };
      CHECK(bothWays(program, recompile, 48, skipped), true);
      bool better = other == Other::Better;
      if(better ? skipped != 0 : skipped <= 1000) {
        std::printf("  other %u, anchor off %u: skipped %llu turns\n", u32(other), anchorOff,
                    (unsigned long long)skipped);
      }
      CHECK(better ? skipped == 0 : skipped > 1000, true);
      auto m = machine(program, recompile, true);
      m->kernel.run(Step * 48);
      CHECK(m->kernel.exited, true);
      CHECK(m->system.memory.read(4, Mark) != 0, better);  //(the better thread ran; the others never do)
    }
  }
}

//A signal a thread waits on (Waiter: not worked out, the signal might wake it), and a wait that waits (WouldWait: not
//calm), each a poll's turn: not skipped, every step's state the same either way, on both engines.
static auto spinsSemaphoreWaits() -> void {
  for(bool recompile : {false, true}) {
    for(auto what : {Sema::Waiter, Sema::WouldWait}) {
      u64 skipped = 0;
      CHECK(bothWays([what](KernelMachine& m) { semaphorePoll(m, what); }, recompile, 32, skipped), true);
      if(skipped) std::printf("  case %u skipped %llu turns\n", u32(what), (unsigned long long)skipped);
      CHECK(skipped, 0);
    }
  }
}

//How a poll's callback check comes to find the thread's callback due (callbackPoll()): never (Nothing); notified by a
//timer's handler (Timer: an alarm 3 milliseconds on, between goes); or by an asynchronous read's completion, between
//goes (Read: readAsync()).
enum class Due : u32 { Nothing, Timer, Read };

//A poll of the GE that also asks the movie ring how full it is and checks the thread's callbacks each turn
//(sceKernelCheckCallback: Midnight Club: L.A. Remix's loop is such checks): main makes its callback (threadCallback())
//and queues a list of 16 sprites (geSprites()), and till the GE says it's done asks sceGeDrawSync(1), the turn's first
//call and so its anchor, then sceMpegRingbufferAvailableSize and sceKernelCheckCallback, storing each result at Answers
//on; three lists, the clock noted after each. A check finding the callback notified runs it. With stick (Read): the
//memory stick's host folder, for readAsync().
auto callbackPoll(KernelMachine& m, Due due, const std::string& stick = {}) -> Anchor {
  threadCallback(m);
  m.system.memory.write(4, Ring + 0, 64);  //packets
  m.system.memory.write(4, Ring + 12, 0);  //holding data
  m.system.memory.write(4, Ring + 40, 0);
  Assembler main{m, 0x0880'1000};
  main.li(a0, m.string("cb")); main.li(a1, 0x0880'4800); main.li(a2, 0); main.call("sceKernelCreateCallback");
  main.put(addu(s7, v0, zero));
  if(due == Due::Timer) {  //(its handler notifies the callback, whose ID is the alarm's argument)
    Assembler handler{m, 0x0880'3800};
    handler.put(addiu(sp, sp, -16)); handler.put(sw(ra, 12, sp));
    handler.li(a1, 1); handler.call("sceKernelNotifyCallback");
    handler.put(lw(ra, 12, sp)); handler.put(addiu(sp, sp, 16));
    handler.put(addu(v0, zero, zero)); handler.put(jr(ra)); handler.put(nop);  //(0: the alarm ends)
    main.li(a0, 3000); main.li(a1, 0x0880'3800); main.put(addu(a2, s7, zero)); main.call("sceKernelSetAlarm");
  }
  if(due == Due::Read) readAsync(m, main, stick);
  geSprites(m, GeList, GeVertices);
  main.li(s4, 0);
  u32 round = main.here();
  main.put(addiu(s4, s4, 1));
  main.li(a0, GeList); main.li(a1, 0); main.li(a2, 0xffff'ffff); main.li(a3, 0);
  main.call("sceGeListEnQueue");
  u32 poll = main.here();
  main.li(a0, 1); main.call("sceGeDrawSync");
  Anchor anchor{m.stub("sceGeDrawSync") + 4, main.here()};
  main.put(addu(s5, v0, zero));
  main.li(a0, Ring); main.call("sceMpegRingbufferAvailableSize");
  main.li(t0, Answers); main.put(sw(v0, 0, t0));
  main.call("sceKernelCheckCallback");
  main.li(t0, Answers + 4); main.put(sw(v0, 0, t0));
  main.put(bne(s5, zero, branchTo(main, poll))); main.put(nop);
  main.call("sceKernelGetSystemTimeLow");
  main.put(sll(t1, s4, 2)); main.li(t2, Seen); main.put(addu(t2, t2, t1)); main.put(sw(v0, 0, t2));
  main.put(sltiu(t3, s4, Rounds));
  main.put(bne(t3, zero, branchTo(main, round))); main.put(nop);
  main.call("sceKernelExitGame");
  return anchor;
}

//A callback check finding none due each turn of a poll (callbackPoll()): calm, worked out on the shadow, and the turns
//skipped (thousands) as run. With the thread's callback notified by a timer's handler, or by an asynchronous read's
//completion, between goes, the next check runs it, once, as with every turn run. And directly (notifiedDirectly()): a
//try with the callback notified isn't skipped, the proof stopping at the check. Every step's state the same either way,
//on both engines.
static auto spinsCallbackCheck() -> void {
  HostFolder stick;
  stick.put("DATA.BIN", std::string(4000, 'd'));
  for(bool recompile : {false, true}) {
    for(auto due : {Due::Nothing, Due::Timer, Due::Read}) {
      u64 skipped = 0;
      auto program = [&stick, due](KernelMachine& m) { callbackPoll(m, due, stick.path.string()); };
      CHECK(bothWays(program, recompile, 32, skipped), true);
      if(skipped <= 1000) std::printf("  due %u skipped %llu turns\n", u32(due), (unsigned long long)skipped);
      CHECK(skipped > 1000, true);
      auto m = machine(program, recompile, true);
      m->kernel.run(Step * 32);
      CHECK(m->kernel.exited, true);
      CHECK(m->system.memory.read(4, CallbackRuns), due == Due::Nothing ? 0u : 1u);
    }
  }
  FastTimed fastTimed;
  Unthreaded unthreaded;
  AtAnchor atAnchor{[](KernelMachine& m) { return callbackPoll(m, Due::Nothing); }, {}};
  notifiedDirectly(atAnchor);
}

auto spinTests() -> Tests {
  return {
    {"spins skipped as run", spinsSkipped}, {"spins refused", spinsRefused}, {"a spin skipped directly", spinsDirect},
    {"the GE's looks polled, skipped as run", spinsGeLooks},
    {"the GE's looks polled behind a list, past a SIGNAL, with interrupts held off", spinsGePolls},
    {"the GE's looks polled with the PSP's GE timing, each turn moving lines of the CPU's caches", spinsGePsp},
    {"the GE's looks polled with the PSP's GE timing after a sleep: no skip across its busy window's kink",
     spinsGeKink},
    {"the GE's looks polled under a GE profile", spinsGeProfiled},
    {"the GE's looks polled with the GE on its thread held back: skipped no further than its time's bound",
     spinsGeLooksThreaded},
    {"the GE's looks polled with the PSP's GE timing, its list stalled as the GE draws, with the GE on its thread: "
     "skipped as every turn run", spinsGeDrawingThreaded},
    {"the GE's looks polled with the PSP's GE timing beside a window held at the cap: skips refused, then taken",
     spinsGeCapped},
    {"the GE's looks polled with the PSP's GE timing, its list stalled: no skip past the GE's end", spinsGePartway},
    {"the GE's looks polled with the PSP's GE timing, two calls a turn moving lines: no skip of a push that zig-zags",
     spinsGeTwoCalls},
    {"the GE's looks polled with the PSP's GE timing, its turns settling: no skip till the last two turns move alike",
     spinsGeSettling},
    {"the GE's looks polled beside a semaphore signalled and taken back each turn: skipped as run, but not one taking "
     "more than it gives", spinsSemaphore},
    {"the GE's looks polled beside a CB wait taking a semaphore at once: skipped as run, not with its callback "
     "notified", spinsSemaphoreCB},
    {"the GE's looks polled beside a semaphore's waits and a signal refused: skipped as run", spinsSemaphoreRefused},
    {"the GE's looks polled beside a semaphore's signal with another thread ready: not skipped where it hands the CPU "
     "over", spinsSemaphoreSwitch},
    {"the GE's looks polled beside a semaphore another thread waits on, or a wait that waits: not skipped",
     spinsSemaphoreWaits},
    {"the GE's looks polled beside a callback check with none due: skipped as run, not with the callback notified",
     spinsCallbackCheck},
  };
}

}
