//The thread manager's reports (ares/psp/kernel/threads.cpp, events.cpp, display.cpp): a thread's status and run
//status (sizes, the SDK's rule, exit statuses, the run figures counted as threads run, are interrupted, preempted and
//released), the lists of the thread manager's objects by kind and an object's kind, the status structures' size
//words, the lightweight mutex's status, and the display's accumulated count of lines adjusted; as pspautotests'
//threads/threads (refer, threadend, exitstatus, threadmanidlist, threadmanidtype), threads/events/refer,
//threads/semaphores/refer, threads/lwmutex (refer, create, unlock) and display/hcount recorded on a PSP. Programs run
//on both engines; each group's machine, saved at its end, loads into another that makes the same state.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {

constexpr u32 R = KernelMachine::Results;

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

//A thread of its own running, as if started, for calls made directly that need a caller.
auto caller(KernelMachine& m, const char* name = "main", u32 priority = 0x20) -> u32 {
  s32 uid = m.kernel.createThread(name, 0x0880'1000, priority, 0x1000, 0, 0);
  m.kernel.current = m.kernel.threads[uid].get();
  m.kernel.current->status = Kernel::Status::Running;
  m.kernel.current->exitStatus = s32(Kernel::ErrorNotDormant);
  return uid;
}

//Spins until the system's time has moved on by microseconds (s6 and s7 used).
auto spin(Assembler& a, u32 microseconds) -> void {
  a.call("sceKernelGetSystemTimeLow");
  a.put(addu(s7, v0, zero));
  a.li(s6, microseconds);
  u32 loop = a.here();
  a.call("sceKernelGetSystemTimeLow");
  a.put(subu(t0, v0, s7));
  a.put(sltu(t1, t0, s6));
  a.put(bne(t1, zero, int32_t(loop - (a.here() + 4)) / 4));
  a.put(nop);
}

//A run status of the thread in register `thread` (0: the caller) into memory at `at`, its size word 44 (t0, t1).
auto runStatus(Assembler& a, u32 thread, u32 at) -> void {
  a.li(t0, at); a.li(t1, 44); a.put(sw(t1, 0, t0));
  a.put(addu(a0, thread, zero)); a.li(a1, at);
  a.call("sceKernelReferThreadRunStatus");
}

}

//sceKernelReferThreadStatus and sceKernelReferThreadRunStatus called directly. A thread's exit status is DORMANT
//before it first starts, NOT_DORMANT once started and what it ended with after (threads/threads/threadend, refer and
//exitstatus, where thread 0 is no thread to sceKernelGetThreadExitStatus). Each status is copied as far as its size
//word says: 0 nothing, 1 the size's first byte (the whole size reads back), 82 the half of the exit status's word
//below it; the full status is 104 bytes for a program built with an SDK up to 2.60 and 108 after it, when a larger
//size is refused, nothing written (threads/threads/refer); the run status 44, any size taken. Thread 0 is the caller;
//one there isn't UNKNOWN_THID.
static auto threadStatusCalls() -> void {
  KernelMachine m;
  u32 self = caller(m);
  u32 other = m.call("sceKernelCreateThread", {m.string("other"), 0x0880'2000, 0x30, 0x1000, 0, 0});
  constexpr u32 Info = R + 0x200;
  auto refer = [&](const char* function, u32 thread, u32 size) {
    m.system.memory.fill(Info, 0xcc, 128);
    m.system.memory.write(4, Info, size);
    return m.call(function, {thread, Info});
  };
  CHECK(refer("sceKernelReferThreadStatus", other, 104), 0);
  CHECK(word(m, Info + 80), Kernel::ErrorDormant);
  CHECK(m.call("sceKernelGetThreadExitStatus", {other}), Kernel::ErrorDormant);
  CHECK(m.call("sceKernelGetThreadExitStatus", {0}), Kernel::ErrorUnknownThread);
  CHECK(m.call("sceKernelGetThreadExitStatus", {self}), Kernel::ErrorNotDormant);

  //up to 2.60 (no SDK given counts as such): any size, 104 at most copied, the size reading 104
  for(u32 size : {0u, 1u, 82u, 84u, 104u, 1024u, 0xffff'ffffu}) {
    CHECK(refer("sceKernelReferThreadStatus", 0, size), 0);
    CHECK(word(m, Info), size ? 104 : 0);
    CHECK(word(m, Info + 80), size >= 84 ? Kernel::ErrorNotDormant : size == 82 ? 0xcccc'01a4 : 0xcccc'cccc);
    CHECK(word(m, Info + 104), 0xcccc'cccc);
  }
  CHECK(m.system.memory.readString(Info + 4, 32) == "main", true);
  //after 2.60: 108 bytes, a last word of 0, and a larger size refused
  m.kernel.sdkVersion = 0x0206'0011;
  for(u32 size : {0u, 1u, 108u, 109u, 0xffff'ffffu}) {
    bool refused = size > 108;
    CHECK(refer("sceKernelReferThreadStatus", self, size), refused ? Kernel::ErrorIllegalSize : 0);
    CHECK(word(m, Info), size == 0 || refused ? size : 108);
    CHECK(word(m, Info + 104), size == 108 ? 0 : 0xcccc'cccc);
  }
  m.kernel.sdkVersion = 0x0206'0010;
  CHECK(refer("sceKernelReferThreadStatus", self, 0xffff'ffff), 0);
  CHECK(word(m, Info), 104);

  //the run status: 44 bytes whatever the size; the caller's is running, at its priority
  for(u32 size : {0u, 1u, 8u, 44u, 0xffff'ffffu}) {
    CHECK(refer("sceKernelReferThreadRunStatus", 0, size), 0);
    CHECK(word(m, Info), size ? 44 : 0);
    CHECK(word(m, Info + 4), size >= 8 ? 1 : 0xcccc'cccc);
    CHECK(word(m, Info + 44), 0xcccc'cccc);
  }
  CHECK(word(m, Info + 8), 0x20);
  CHECK(refer("sceKernelReferThreadRunStatus", other, 44), 0);
  std::array<u32, 10> dormant = {16, 0x30, 0, 0, 0, 0, 0, 0, 0, 0};
  for(u32 n = 0; n < 10; n++) CHECK(word(m, Info + 4 + n * 4), dormant[n]);
  CHECK(refer("sceKernelReferThreadRunStatus", 0xdead'beef, 44), Kernel::ErrorUnknownThread);
  CHECK(refer("sceKernelReferThreadStatus", 0xdead'beef, 104), Kernel::ErrorUnknownThread);

  //started (worse than the caller, so ready), its exit status NOT_DORMANT; terminated, THREAD_TERMINATED
  CHECK(m.call("sceKernelStartThread", {other, 0, 0}), 0);
  CHECK(m.call("sceKernelGetThreadExitStatus", {other}), Kernel::ErrorNotDormant);
  CHECK(refer("sceKernelReferThreadRunStatus", other, 44), 0);
  CHECK(word(m, Info + 4), 2);
  CHECK(m.call("sceKernelTerminateThread", {other}), 0);
  CHECK(m.call("sceKernelGetThreadExitStatus", {other}), Kernel::ErrorThreadTerminated);
  CHECK(m.call("sceKernelStartThread", {other, 0, 0}), 0);
  CHECK(refer("sceKernelReferThreadStatus", other, 104), 0);
  CHECK(word(m, Info + 80), Kernel::ErrorNotDormant);
  CHECK(roundTrip(m), true);
}

//The run figures as threads run. Main (0x20) has a vertical blank handler, and starts a better thread (0x10) that
//delays 1000 microseconds three times and then sleeps; main spins 20 ms meanwhile. Main's run status: running, about
//20 ms on the CPU, interrupted once (the blank at 16.7 ms), preempted four times (by the better thread's start, and
//each of its wakings); the better thread's: sleeping, a few microseconds on the CPU, never interrupted or preempted.
//Main lets it go of its sleep: it runs at once, counting one release. Ended, it keeps its figures; started again,
//they count afresh. The state saved part way through main's spin, in a fresh machine, carries on to the same figures.
static auto runFigures() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Handled = R + 0x10;
    Assembler handler{m, 0x0880'3000};
    handler.li(t0, Handled); handler.put(lw(t1, 0, t0)); handler.put(addiu(t1, t1, 1)); handler.put(sw(t1, 0, t0));
    handler.put(jr(ra));
    handler.put(nop);
    Assembler better{m, 0x0880'2000};
    for(u32 n = 0; n < 3; n++) {
      better.li(a0, 1000);
      better.call("sceKernelDelayThread");
    }
    better.call("sceKernelSleepThread");
    runStatus(better, zero, R + 0x100);  //released: its own, running
    better.li(a0, 0);
    better.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, 30); main.li(a1, 0); main.li(a2, 0x0880'3000); main.li(a3, 0);
    main.call("sceKernelRegisterSubIntrHandler");
    main.li(a0, 30); main.li(a1, 0);
    main.call("sceKernelEnableSubIntr");
    main.li(a0, m.string("better")); main.li(a1, 0x0880'2000); main.li(a2, 0x10); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(s0, v0, zero));
    main.put(addu(a0, s0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");  //it runs at once, and delays
    spin(main, 20'000);  //the state is saved in here
    runStatus(main, zero, R + 0x200);
    runStatus(main, s0, R + 0x300);
    main.put(addu(a0, s0, zero));
    main.call("sceKernelReleaseWaitThread");  //it runs at once, and ends
    runStatus(main, s0, R + 0x400);
    main.put(addu(a0, s0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");  //it runs at once, and delays
    runStatus(main, s0, R + 0x500);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency / 100);  //10 ms
    CHECK(m.kernel.exited, false);
    auto state = saveState(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    CHECK(loadState(n, state), true);
    CHECK(saveState(n) == state, true);
    std::vector<std::vector<u32>> figures;
    for(auto* each : {&m, &n}) {
      each->kernel.run(Kernel::CPUFrequency / 10);
      CHECK(each->kernel.exited, true);
      auto at = [&](u32 address) { return word(*each, address); };
      //main: running at 0x20, nothing waited for, about 20 ms of microseconds, once interrupted, four times preempted
      CHECK(at(R + 0x204), 1);
      CHECK(at(R + 0x208), 0x20);
      CHECK(at(R + 0x20c), 0);
      CHECK(at(R + 0x218) > 19'900 && at(R + 0x218) < 20'100, true);
      CHECK(at(R + 0x21c), 0);
      CHECK(at(R + 0x220), 1);
      CHECK(at(R + 0x224), 4);
      CHECK(at(R + 0x228), 0);
      //the better thread asleep: waiting (4) to sleep (1), a few microseconds of its own
      CHECK(at(R + 0x304), 4);
      CHECK(at(R + 0x308), 0x10);
      CHECK(at(R + 0x30c), 1);
      CHECK(at(R + 0x318) < 20, true);
      CHECK(at(R + 0x320), 0);
      CHECK(at(R + 0x324), 0);
      CHECK(at(R + 0x328), 0);
      //released: running, one release; ended: dormant, its figures kept; started again: delaying (2), counted afresh
      CHECK(at(R + 0x104), 1);
      CHECK(at(R + 0x128), 1);
      CHECK(at(R + 0x404), 16);
      CHECK(at(R + 0x428), 1);
      CHECK(at(R + 0x504), 4);
      CHECK(at(R + 0x50c), 2);
      CHECK(at(R + 0x528), 0);
      CHECK(at(R + 0x518) < 20, true);
      CHECK(at(Handled), 1);
      CHECK(each->notes.size(), 0);
      CHECK(roundTrip(*each), true);
      std::vector<u32> words;
      for(u32 address = R + 0x100; address < R + 0x530; address += 4) words.push_back(at(address));
      figures.push_back(words);
    }
    CHECK(figures[0] == figures[1], true);
  }
}

//sceKernelGetThreadmanIdList and sceKernelGetThreadmanIdType, as threads/threads/threadmanidlist and threadmanidtype
//recorded. One object of each kind (two semaphores), each listed by its kind in the order it was made, and threads by
//state: dormant, sleeping, delaying, suspended (a ready one). Kinds 9 and 14 are taken and list nothing here; 0, 15,
//0x44, 0x80 and 0x80000000 are refused (ILLEGAL_TYPE), as a negative size is after them (ILLEGAL_ADDR), neither
//writing the count. A size of 0 gives the count alone, with no buffer; a smaller buffer gets as many as it takes,
//the count all of them; the count's pointer may be 0; a buffer the IDs can't go in is refused. An object's kind is
//its list's number; anything else (a deleted thread, -1, 0, 1, a memory block) is ILLEGAL_ARGUMENT.
static auto idLists() -> void {
  KernelMachine m;
  u32 self = caller(m);
  auto thread = [&](const char* name, u32 priority) {
    return m.call("sceKernelCreateThread", {m.string(name), 0x0880'2000, priority, 0x1000, 0, 0});
  };
  u32 dormant = thread("dormant", 0x30), sleeping = thread("sleeping", 0x30), delaying = thread("delaying", 0x30);
  u32 suspended = thread("suspended", 0x30);
  m.kernel.threads[sleeping]->status = Kernel::Status::Waiting;
  m.kernel.threads[sleeping]->wait = Kernel::Wait::Sleep;
  m.kernel.threads[delaying]->status = Kernel::Status::Waiting;
  m.kernel.threads[delaying]->wait = Kernel::Wait::Delay;
  m.kernel.threads[delaying]->wakeAt = Kernel::CPUFrequency;
  CHECK(m.call("sceKernelStartThread", {suspended, 0, 0}), 0);
  CHECK(m.call("sceKernelSuspendThread", {suspended}), 0);
  u32 name = m.string("object");
  u32 sema = m.call("sceKernelCreateSema", {name, 0, 0, 1, 0});
  u32 sema2 = m.call("sceKernelCreateSema", {name, 0, 0, 1, 0});
  u32 flag = m.call("sceKernelCreateEventFlag", {name, 0, 0, 0});
  u32 mailbox = m.call("sceKernelCreateMbx", {name, 0, 0});
  u32 vpl = m.call("sceKernelCreateVpl", {name, 2, 0, 0x100, 0});
  u32 fpl = m.call("sceKernelCreateFpl", {name, 2, 0, 16, 2, 0});
  u32 pipe = m.call("sceKernelCreateMsgPipe", {name, 2, 0, 0x100, 0});
  u32 callback = m.call("sceKernelCreateCallback", {name, 0x0880'3000, 0});
  u32 alarm = m.call("sceKernelSetAlarm", {100'000, 0x0880'3000, 0});
  u32 vtimer = m.call("sceKernelCreateVTimer", {name, 0});
  u32 mutex = m.call("sceKernelCreateMutex", {name, 0, 0, 0});
  CHECK(m.call("sceKernelCreateLwMutex", {R + 0x80, name, 0, 0, 0}), 0);
  u32 lwMutex = word(m, R + 0x90);
  u32 block = m.call("sceKernelAllocPartitionMemory", {2, name, 0, 0x100, 0});
  constexpr u32 Buffer = R + 0x400, Count = R + 0x3fc;
  auto list = [&](u32 type, u32 size = 32) {
    m.system.memory.fill(Buffer, 0xff, 32 * 4);
    m.system.memory.write(4, Count, 0xdead'beef);
    u32 result = m.call("sceKernelGetThreadmanIdList", {type, Buffer, size, Count});
    std::vector<u32> ids;
    for(u32 n = 0; n < 32 && word(m, Buffer + n * 4) != 0xffff'ffff; n++) ids.push_back(word(m, Buffer + n * 4));
    CHECK(s32(result) < 0 || result == ids.size(), true);
    return ids;
  };
  CHECK(list(1) == std::vector<u32>({self, dormant, sleeping, delaying, suspended}), true);
  CHECK(word(m, Count), 5);
  CHECK(list(2) == std::vector<u32>({sema, sema2}), true);
  CHECK(list(3) == std::vector<u32>({flag}), true);
  CHECK(list(4) == std::vector<u32>({mailbox}), true);
  CHECK(list(5) == std::vector<u32>({vpl}), true);
  CHECK(list(6) == std::vector<u32>({fpl}), true);
  CHECK(list(7) == std::vector<u32>({pipe}), true);
  CHECK(list(8) == std::vector<u32>({callback}), true);
  CHECK(list(9).empty() && word(m, Count) == 0, true);
  CHECK(list(10) == std::vector<u32>({alarm}), true);
  CHECK(list(11) == std::vector<u32>({vtimer}), true);
  CHECK(list(12) == std::vector<u32>({mutex}), true);
  CHECK(list(13) == std::vector<u32>({lwMutex}), true);
  CHECK(list(14).empty() && word(m, Count) == 0, true);
  CHECK(list(64) == std::vector<u32>({sleeping}), true);
  CHECK(list(65) == std::vector<u32>({delaying}), true);
  CHECK(list(66) == std::vector<u32>({suspended}), true);
  CHECK(list(67) == std::vector<u32>({dormant}), true);
  m.system.memory.write(4, Count, 0xdead'beef);
  for(u32 type : {0u, 15u, 0x44u, 0x80u, 0x8000'0000u}) {
    CHECK(m.call("sceKernelGetThreadmanIdList", {type, Buffer, 32, Count}), Kernel::ErrorIllegalType);
    CHECK(word(m, Count), 0xdead'beef);
  }
  CHECK(m.call("sceKernelGetThreadmanIdList", {0, Buffer, u32(-1), Count}), Kernel::ErrorIllegalType);
  CHECK(m.call("sceKernelGetThreadmanIdList", {1, Buffer, u32(-1), Count}), Kernel::ErrorIllegalAddress);
  CHECK(word(m, Count), 0xdead'beef);
  CHECK(m.call("sceKernelGetThreadmanIdList", {1, 0, 0, Count}), 0);
  CHECK(word(m, Count), 5);
  CHECK(list(1, 2) == std::vector<u32>({self, dormant}), true);
  CHECK(word(m, Count), 5);
  CHECK(m.call("sceKernelGetThreadmanIdList", {2, Buffer, 32, 0}), 2);
  CHECK(m.call("sceKernelGetThreadmanIdList", {1, 0, 32, Count}), Kernel::ErrorIllegalAddress);
  CHECK(m.call("sceKernelGetThreadmanIdList", {9, 0, 32, Count}), 0);  //nothing to write: no buffer needed

  std::pair<u32, u32> kinds[] = {
    {self, 1}, {dormant, 1}, {sleeping, 1}, {suspended, 1}, {sema, 2}, {flag, 3}, {mailbox, 4}, {vpl, 5}, {fpl, 6},
    {pipe, 7}, {callback, 8}, {alarm, 10}, {vtimer, 11}, {mutex, 12}, {lwMutex, 13},
  };
  for(auto [uid, kind] : kinds) CHECK(m.call("sceKernelGetThreadmanIdType", {uid}), kind);
  CHECK(m.call("sceKernelTerminateDeleteThread", {dormant}), 0);
  for(u32 uid : {dormant, u32(-1), 0u, 1u, block}) {
    CHECK(m.call("sceKernelGetThreadmanIdType", {uid}), Kernel::ErrorIllegalArgument);
  }
  CHECK(roundTrip(m), true);
}

//The status structures' size words, as threads/events/refer, threads/semaphores/refer and threads/lwmutex/refer
//recorded: whatever size is asked (0 aside, which copies nothing), it reads back as the structure's, 52 for an event
//flag, 56 for a semaphore and 64 for a lightweight mutex (by its work area or its ID); fields past the size are left
//as they were. The lightweight mutex's status: its name, attributes, ID, work area, first and present counts, its
//holder or -1, its waiters; one deleted (or any ID not one) is LWMUTEX_NOTFOUND. As threads/lwmutex/create and unlock
//recorded: a NULL name is ERROR, attributes past 0x3ff ILLEGAL_ATTR, before the count; the work area's holder is 0
//while it's free, as made and as unlocked, and its last three words 0. As threads/events/create and
//threads/semaphores/create recorded: a NULL name is ERROR for both; an event flag refuses 0x100 and anything past
//0x2ff, a semaphore anything past 0x1ff (ILLEGAL_ATTR); a semaphore takes any counts.
static auto statusSizes() -> void {
  KernelMachine m;
  u32 self = caller(m);
  constexpr u32 Info = R + 0x200, Work = R + 0x80;
  CHECK(m.call("sceKernelCreateEventFlag", {0, 0, 0, 0}), Kernel::ErrorError);
  for(u32 attributes : {0x100u, 0x122u, 0x300u, 0x400u, 0x900u, 0x1200u}) {
    CHECK(m.call("sceKernelCreateEventFlag", {m.string("bad"), attributes, 0, 0}), Kernel::ErrorIllegalAttribute);
  }
  CHECK(m.call("sceKernelCreateEventFlag", {m.string("odd"), 0x2ff, 0, 0}) < 0x8000'0000, true);
  CHECK(m.call("sceKernelCreateSema", {0, 0, 0, 1, 0}), Kernel::ErrorError);
  for(u32 attributes : {0x200u, 0x400u, 0x900u, 0x1'0000u}) {
    CHECK(m.call("sceKernelCreateSema", {m.string("bad"), attributes, 0, 1, 0}), Kernel::ErrorIllegalAttribute);
  }
  for(auto [initial, maximum] : {std::pair{-1, 2}, {3, 2}, {0, -1}, {65537, 0}}) {
    u32 odd = m.call("sceKernelCreateSema", {m.string("odd"), 0x1ff, u32(initial), u32(maximum), 0});
    CHECK(odd < 0x8000'0000 && m.kernel.semaphores[odd].count == initial, true);
    CHECK(m.kernel.semaphores[odd].maximum, maximum);
  }
  u32 flag = m.call("sceKernelCreateEventFlag", {m.string("flag"), 0x200, 5, 0});
  u32 sema = m.call("sceKernelCreateSema", {m.string("sema"), 0, 1, 3, 0});
  m.system.memory.fill(Work, 0xcc, 32);
  CHECK(m.call("sceKernelCreateLwMutex", {Work, 0, 0, 0, 0}), Kernel::ErrorError);
  CHECK(m.call("sceKernelCreateLwMutex", {Work, m.string("lw"), 0x400, u32(-1), 0}), Kernel::ErrorIllegalAttribute);
  CHECK(m.call("sceKernelCreateLwMutex", {Work, m.string("lw"), 0, 2, 0}), Kernel::ErrorIllegalCount);
  CHECK(m.call("sceKernelCreateLwMutex", {Work, m.string("lw"), 0x3ff, 2, 0}), 0);
  u32 lw = word(m, Work + 16);
  CHECK(word(m, Work + 4), self);
  for(u32 n = 20; n < 32; n += 4) CHECK(word(m, Work + n), 0);
  struct Kind { const char* function; u32 id, size; } kinds[] = {
    {"sceKernelReferEventFlagStatus", flag, 52}, {"sceKernelReferSemaStatus", sema, 56},
    {"sceKernelReferLwMutexStatus", Work, 64}, {"sceKernelReferLwMutexStatusByID", lw, 64},
  };
  for(auto& kind : kinds) {
    for(u32 size : {0u, 1u, 2u, 8u, 44u, 0x80u, 0x8000'0001u, 0xffff'ffffu}) {
      m.system.memory.fill(Info, 0xcc, 0x80);
      m.system.memory.write(4, Info, size);
      CHECK(m.call(kind.function, {kind.id, Info}), 0);
      CHECK(word(m, Info), size ? kind.size : 0);
      CHECK(word(m, Info + 36), size >= 40 ? (kind.size == 64 ? 0x3ff : kind.size == 52 ? 0x200 : 0) : 0xcccc'cccc);
      CHECK(word(m, Info + kind.size), 0xcccc'cccc);
      u32 last = kind.size - 4;
      CHECK(word(m, Info + last) == 0xcccc'cccc, size < kind.size);
    }
  }
  //the lightweight mutex's whole status: held twice by the caller
  m.system.memory.write(4, Info, 64);
  CHECK(m.call("sceKernelReferLwMutexStatusByID", {lw, Info}), 0);
  CHECK(m.system.memory.readString(Info + 4, 32) == "lw", true);
  std::array<u32, 7> held = {0x3ff, lw, Work, 2, 2, self, 0};
  for(u32 n = 0; n < 7; n++) CHECK(word(m, Info + 36 + n * 4), held[n]);
  CHECK(m.call("sceKernelUnlockLwMutex", {Work, 2}), 0);
  CHECK(word(m, Work + 4), 0);
  CHECK(m.call("sceKernelReferLwMutexStatus", {Work, Info}), 0);
  CHECK(word(m, Info + 52), 0);
  CHECK(word(m, Info + 56), 0xffff'ffff);
  //an event flag's and a semaphore's whole statuses
  CHECK(m.call("sceKernelReferEventFlagStatus", {flag, Info}), 0);
  CHECK(m.system.memory.readString(Info + 4, 32) == "flag", true);
  CHECK(word(m, Info + 40), 5);
  CHECK(word(m, Info + 44), 5);
  CHECK(m.call("sceKernelReferSemaStatus", {sema, Info}), 0);
  CHECK(m.system.memory.readString(Info + 4, 32) == "sema", true);
  CHECK(word(m, Info + 40), 1);
  CHECK(word(m, Info + 48), 3);
  CHECK(roundTrip(m), true);
  CHECK(m.call("sceKernelDeleteLwMutex", {Work}), 0);
  CHECK(m.call("sceKernelReferLwMutexStatus", {Work, Info}), Kernel::ErrorLwMutexNotFound);
  for(u32 id : {lw, 0u, 0xdead'beefu}) {
    CHECK(m.call("sceKernelReferLwMutexStatusByID", {id, Info}), Kernel::ErrorLwMutexNotFound);
  }
  CHECK(roundTrip(m), true);
}

//The display's lines, as display/hcount and hcountwrap recorded: sceDisplayAdjustAccumulatedHcount refuses a
//negative count (INVALID_VALUE) and sets the accumulated count, which counts on from it each line and goes from
//0x7fffffff to 0; the line the display is on is the same as before (0 as a blank starts); the count set is kept in
//states.
static auto hcountAdjusted() -> void {
  KernelMachine m;
  u64 start = m.kernel.nextVblank - Kernel::VblankCycles;
  m.kernel.cycles = start;
  CHECK(m.call("sceDisplayGetCurrentHcount", {}), 0);
  for(u32 count : {u32(-1), 0x8000'0000u}) {
    CHECK(m.call("sceDisplayAdjustAccumulatedHcount", {count}), Kernel::ErrorInvalidValue);
  }
  m.kernel.cycles = start + Kernel::LineCycles * 5 + 7;
  CHECK(m.call("sceDisplayAdjustAccumulatedHcount", {0x7fff'ffff}), 0);
  CHECK(m.call("sceDisplayGetAccumulatedHcount", {}), 0x7fff'ffff);
  m.kernel.cycles = start + Kernel::LineCycles * 6 - 1;
  CHECK(m.call("sceDisplayGetAccumulatedHcount", {}), 0x7fff'ffff);
  m.kernel.cycles = start + Kernel::LineCycles * 6;
  CHECK(m.call("sceDisplayGetAccumulatedHcount", {}), 0);
  CHECK(m.call("sceDisplayAdjustAccumulatedHcount", {1000}), 0);
  m.kernel.cycles = start + Kernel::LineCycles * 10;
  CHECK(m.call("sceDisplayGetAccumulatedHcount", {}), 1004);
  CHECK(m.call("sceDisplayGetCurrentHcount", {}), 10);
  CHECK(roundTrip(m), true);
  auto state = saveState(m);
  KernelMachine n;
  CHECK(loadState(n, state), true);
  CHECK(n.call("sceDisplayGetAccumulatedHcount", {}), 1004);
  //across a frame: 286 lines more
  m.kernel.cycles += Kernel::VblankCycles;
  m.kernel.events();
  CHECK(m.call("sceDisplayGetAccumulatedHcount", {}), 1004 + 286);
}

auto threadmanTests() -> Tests {
  return {
    {"thread status sizes and exit", threadStatusCalls}, {"thread run figures", runFigures},
    {"threadman ID lists", idLists}, {"status size words and creates", statusSizes},
    {"accumulated hcount adjusted", hcountAdjusted},
  };
}

}
