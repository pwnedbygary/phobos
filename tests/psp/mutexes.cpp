//Kernel mutexes (ares/psp/kernel/mutexes.cpp), and threads let go of their waits (sceKernelReleaseWaitThread), their
//wakeups forgotten (sceKernelCancelWakeupThread), and semaphores' and event flags' waits cancelled: what each call
//takes and refuses, as pspautotests' threads/mutex, threads/threads/release, threads/semaphores/cancel and
//threads/events/cancel recorded on a PSP; who gets a mutex as it's freed (first come, or by priority), recursive
//counts, timeouts, callbacks in a wait, a holder ending, deletion and cancelling waking waiters; and states saved
//with threads waiting. Programs run on both engines; each group's machine, saved at its end, loads into another
//that makes the same state.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {

constexpr u32 R = KernelMachine::Results;
constexpr u32 Log = R + 0x100;  //words the threads write down in turn, R + 8 counting them

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

auto logged(KernelMachine& m) -> std::vector<u32> {
  std::vector<u32> words;
  for(u32 n = 0; n < word(m, R + 8); n++) words.push_back(word(m, Log + n * 4));
  return words;
}

//Writes value down in the log (t0-t2 used).
auto note(Assembler& a, u32 value) -> void {
  a.li(t0, R);
  a.put(lw(t1, 8, t0));
  a.put(sll(t2, t1, 2));
  a.put(addu(t2, t2, t0));
  a.li(t3, value);
  a.put(sw(t3, 0x100, t2));
  a.put(addiu(t1, t1, 1));
  a.put(sw(t1, 8, t0));
}

//Writes v0 down in the log.
auto noteResult(Assembler& a) -> void {
  a.li(t0, R);
  a.put(lw(t1, 8, t0));
  a.put(sll(t2, t1, 2));
  a.put(addu(t2, t2, t0));
  a.put(sw(v0, 0x100, t2));
  a.put(addiu(t1, t1, 1));
  a.put(sw(t1, 8, t0));
}

//Creates and starts a thread (entry, priority) from a program, its ID left in s0.
auto start(Assembler& a, KernelMachine& m, u32 entry, u32 priority) -> void {
  a.li(a0, m.string("thread")); a.li(a1, entry); a.li(a2, priority); a.li(a3, 0x1000); a.li(t0, 0); a.li(t1, 0);
  a.call("sceKernelCreateThread");
  a.put(addu(s0, v0, zero));
  a.put(addu(a0, s0, zero)); a.li(a1, 0); a.li(a2, 0);
  a.call("sceKernelStartThread");
}

auto delay(Assembler& a, u32 microseconds) -> void {
  a.li(a0, microseconds);
  a.call("sceKernelDelayThread");
}

//The mutex whose ID is at R into a0.
auto mutex(Assembler& a) -> void { a.li(t0, R); a.put(lw(a0, 0, t0)); }

//A machine with a thread of its own running, for calls made directly that need a caller (a mutex's holder).
auto withCaller(KernelMachine& m, const char* name = "main") -> u32 {
  s32 uid = m.kernel.createThread(name, 0x0880'1000, 0x20, 0x1000, 0, 0);
  m.kernel.current = m.kernel.threads[uid].get();
  m.kernel.current->status = Kernel::Status::Running;
  return uid;
}

//A mutex's status from sceKernelReferMutexStatus: (attributes, initial, count, holder, waiting).
auto status(KernelMachine& m, u32 uid) -> std::array<u32, 5> {
  m.system.memory.write(4, R + 0x200, 56);
  m.call("sceKernelReferMutexStatus", {uid, R + 0x200});
  return {word(m, R + 0x224), word(m, R + 0x228), word(m, R + 0x22c), word(m, R + 0x230), word(m, R + 0x234)};
}

}

//What each call takes and refuses, in the order threads/mutex's create, lock, try, unlock, cancel, refer and delete
//recorded: the mutex before the count, the count (more than 1 only for a recursive mutex) before who holds it; a
//holder locking again (recursive: counted, to 2^31 - 1; else RECURSIVE_NOT_ALLOWED); another's mutex tried
//(MUTEX_LOCKED) or unlocked (MUTEX_UNLOCKED); unlocking more than held (UNLOCK_UNDERFLOW); a status copied as far as
//the size asked; cancelling setting the count, a negative one the initial; deleting.
static auto mutexCalls() -> void {
  KernelMachine m;
  u32 self = withCaller(m);
  u32 name = m.string("mutex");
  auto create = [&](u32 attributes, s32 count) {
    return m.call("sceKernelCreateMutex", {name, attributes, u32(count), 0});
  };
  auto lock = [&](u32 uid, s32 count) { return m.call("sceKernelLockMutex", {uid, u32(count), 0}); };
  auto tryLock = [&](u32 uid, s32 count) { return m.call("sceKernelTryLockMutex", {uid, u32(count)}); };
  auto unlock = [&](u32 uid, s32 count) { return m.call("sceKernelUnlockMutex", {uid, u32(count)}); };
  CHECK(m.call("sceKernelCreateMutex", {0, 0, 0, 0}), Kernel::ErrorError);
  for(u32 attributes : {0x400u, 0xc00u, 0x1000u, 0x8000u, 0x1'0000u}) {
    CHECK(create(attributes, 0), Kernel::ErrorIllegalAttribute);
  }
  CHECK(create(0, -1), Kernel::ErrorIllegalCount);
  CHECK(create(0, 2), Kernel::ErrorIllegalCount);
  u32 big = create(0xbff, 0x7fff'ffff);  //every attribute it takes; recursive, so any count
  CHECK(big < 0x8000'0000, true);
  CHECK((status(m, big) == std::array<u32, 5>{0xbff, 0x7fff'ffff, 0x7fff'ffff, self, 0}), true);
  CHECK(lock(big, 1), Kernel::ErrorMutexOverflow);
  CHECK(tryLock(big, 1), Kernel::ErrorMutexOverflow);
  CHECK(m.system.memory.readString(R + 0x204, 32) == "mutex", true);
  CHECK(word(m, R + 0x200), 56);

  u32 plain = create(0, 0);
  CHECK((status(m, plain) == std::array<u32, 5>{0, 0, 0, 0xffff'ffff, 0}), true);
  CHECK(lock(0, 0), Kernel::ErrorUnknownMutex);  //the mutex before the count
  CHECK(lock(plain, 0), Kernel::ErrorIllegalCount);
  CHECK(lock(plain, 2), Kernel::ErrorIllegalCount);
  CHECK(lock(plain, -1), Kernel::ErrorIllegalCount);
  CHECK(unlock(plain, 1), Kernel::ErrorMutexUnlocked);
  CHECK(unlock(plain, 2), Kernel::ErrorIllegalCount);  //the count before the holder
  CHECK(lock(plain, 1), 0);
  CHECK((status(m, plain) == std::array<u32, 5>{0, 0, 1, self, 0}), true);
  CHECK(lock(plain, 1), Kernel::ErrorMutexRecursion);
  CHECK(tryLock(plain, 1), Kernel::ErrorMutexRecursion);
  CHECK(unlock(plain, 1), 0);
  CHECK(tryLock(plain, 1), 0);
  CHECK(unlock(plain, 1), 0);
  //a timeout of 0 on a free mutex takes it, its time left as it was
  m.system.memory.write(4, R + 0x40, 0);
  CHECK(m.call("sceKernelLockMutex", {plain, 1, R + 0x40}), 0);
  CHECK(unlock(plain, 1), 0);

  u32 recursive = create(0x200, 1);
  CHECK(lock(recursive, 0x7fff'fffe), 0);
  CHECK(status(m, recursive)[2], 0x7fff'ffff);
  CHECK(lock(recursive, 1), Kernel::ErrorMutexOverflow);
  CHECK(unlock(recursive, 0x7fff'fffe), 0);
  CHECK(unlock(recursive, 2), Kernel::ErrorUnlockUnderflow);
  CHECK(tryLock(recursive, 2), 0);
  CHECK(unlock(recursive, 3), 0);
  CHECK((status(m, recursive) == std::array<u32, 5>{0x200, 1, 0, 0xffff'ffff, 0}), true);

  //another thread's mutex: tried, MUTEX_LOCKED; unlocked, MUTEX_UNLOCKED
  CHECK(lock(plain, 1), 0);
  u32 other = withCaller(m, "other");
  CHECK(tryLock(plain, 1), Kernel::ErrorMutexLocked);
  CHECK(unlock(plain, 1), Kernel::ErrorMutexUnlocked);
  CHECK(m.call("sceKernelLockMutex", {plain, 1, R + 0x40}), Kernel::ErrorWaitTimeout);  //a timeout of 0: at once
  CHECK(word(m, R + 0x40), 0);
  m.kernel.current->status = Kernel::Status::Ready;
  m.kernel.current = m.kernel.threads[self].get();
  m.kernel.current->status = Kernel::Status::Running;

  //a status as far as its size: none for 0, the size's first byte for 1 (56 reads back), nothing past 56
  for(u32 size : {0u, 1u, 44u, 0xffff'ffffu}) {
    m.system.memory.fill(R + 0x200, 0xcc, 64);
    m.system.memory.write(4, R + 0x200, size);
    CHECK(m.call("sceKernelReferMutexStatus", {plain, R + 0x200}), 0);
    CHECK(word(m, R + 0x200), size == 0 ? 0 : 56);  //(a size of 1: its own first byte made 0x38)
    CHECK(word(m, R + 0x230), size == 0xffff'ffff ? self : 0xcccc'cccc);  //the holder, at 48
    CHECK(word(m, R + 0x238), 0xcccc'cccc);  //nothing past 56
  }

  //cancelling: a count more than 1 refused for a mutex that isn't recursive, nothing written; 1, the caller holding
  //it; 0, free; a negative one, the initial count (the recursive mutex's 1); the threads that waited counted
  m.system.memory.write(4, R + 0x44, 99);
  CHECK(m.call("sceKernelCancelMutex", {plain, 3, R + 0x44}), Kernel::ErrorIllegalCount);
  CHECK(word(m, R + 0x44), 99);
  CHECK(m.call("sceKernelCancelMutex", {plain, 0, R + 0x44}), 0);
  CHECK(word(m, R + 0x44), 0);
  CHECK((status(m, plain) == std::array<u32, 5>{0, 0, 0, 0xffff'ffff, 0}), true);
  CHECK(m.call("sceKernelCancelMutex", {plain, 1, 0}), 0);
  CHECK((status(m, plain) == std::array<u32, 5>{0, 0, 1, self, 0}), true);
  CHECK(m.call("sceKernelCancelMutex", {recursive, u32(-3), 0}), 0);
  CHECK((status(m, recursive) == std::array<u32, 5>{0x200, 1, 1, self, 0}), true);
  CHECK(m.call("sceKernelCancelMutex", {0, 0, 0}), Kernel::ErrorUnknownMutex);

  CHECK(m.call("sceKernelDeleteMutex", {plain}), 0);
  CHECK(m.call("sceKernelDeleteMutex", {plain}), Kernel::ErrorUnknownMutex);
  CHECK(lock(plain, 1), Kernel::ErrorUnknownMutex);
  CHECK(m.call("sceKernelReferMutexStatus", {0xdead'beef, R + 0x200}), Kernel::ErrorUnknownMutex);
  CHECK(other != self, true);
  CHECK(roundTrip(m), true);
}

//Who gets a mutex as its holder frees it, as threads/mutex/priority and threads/scheduling/mutexhandoff recorded:
//three threads worse than main wait for it, in the order A (0x30), B (0x28), C (0x2c), each started as the one
//before waits; each writes its letter down
//once it has it and frees it. First come first served by default ("ABC"); by priority with attribute 0x100 ("BCA").
//A thread better than main gets it at once ("D" before main's "U"). The waiting count is each wait's.
static auto mutexOrder() -> void {
  for(bool recompile : {false, true}) {
    for(u32 attributes : {0x000u, 0x100u}) {
      KernelMachine m;
      auto waiter = [&](u32 entry, u32 letter) {
        Assembler w{m, entry};
        mutex(w); w.li(a1, 1); w.li(a2, 0);
        w.call("sceKernelLockMutex");
        note(w, letter);
        mutex(w); w.li(a1, 1);
        w.call("sceKernelUnlockMutex");
        w.call("sceKernelExitThread");
      };
      waiter(0x0880'2000, 'A');
      waiter(0x0880'2100, 'B');
      waiter(0x0880'2200, 'C');
      waiter(0x0880'2300, 'D');
      Assembler main{m, 0x0880'1000};
      main.li(a0, m.string("mutex")); main.li(a1, attributes); main.li(a2, 1); main.li(a3, 0);
      main.call("sceKernelCreateMutex");  //held by main
      main.li(t0, R); main.put(sw(v0, 0, t0));
      start(main, m, 0x0880'2000, 0x30);
      delay(main, 1000);  //each waits before the next starts: A, B, C
      start(main, m, 0x0880'2100, 0x28);
      delay(main, 1000);
      start(main, m, 0x0880'2200, 0x2c);
      delay(main, 1000);
      mutex(main); main.li(a1, R + 0x200); main.li(t0, 56); main.put(sw(t0, 0, a1));
      main.call("sceKernelReferMutexStatus");
      mutex(main); main.li(a1, 1);
      main.call("sceKernelUnlockMutex");
      note(main, 'U');
      delay(main, 2000);  //they each get it and free it
      mutex(main); main.li(a1, 1); main.li(a2, 0);
      main.call("sceKernelLockMutex");
      start(main, m, 0x0880'2300, 0x10);  //better: waits for main
      mutex(main); main.li(a1, 1);
      main.call("sceKernelUnlockMutex");  //it takes over at once
      note(main, 'U');
      main.call("sceKernelExitGame");
      m.runProgram(0x0880'1000, recompile);
      CHECK(m.kernel.exited, true);
      auto order = attributes ? std::vector<u32>{'U', 'B', 'C', 'A', 'D', 'U'}
                              : std::vector<u32>{'U', 'A', 'B', 'C', 'D', 'U'};
      CHECK(logged(m) == order, true);
      CHECK(word(m, R + 0x234), 3);  //the three waiting, as main unlocked it
      CHECK(m.notes.size(), 0);
      CHECK(roundTrip(m), true);
    }
  }
}

//How waits end besides being given the mutex: a recursive mutex locked twice is freed by unlocking twice, not once;
//a 500-microsecond timeout runs out (WAIT_TIMEOUT, its time left 0); a thread terminated as it waits leaves the line;
//a holder that ends frees what it holds, to the next in line; cancelling wakes every waiter (WAIT_CANCEL) and makes
//main the holder; deleting tells those left (WAIT_DELETE).
static auto mutexWaits() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Timeout = R + 0x40, Ends = R + 0x48;
    //a waiter: locks (with a timeout, or not), writes down its letter and result, and sleeps (holding what it got)
    auto waiter = [&](u32 entry, u32 letter, bool timed) {
      Assembler w{m, entry};
      mutex(w); w.li(a1, 1); w.li(a2, timed ? Timeout : 0);
      w.call("sceKernelLockMutex");
      w.put(addu(s1, v0, zero));
      note(w, letter);
      w.put(addu(v0, s1, zero));
      noteResult(w);
      w.call("sceKernelSleepThread");
      w.call("sceKernelExitThread");
    };
    waiter(0x0880'2000, 'T', true);   //times out
    waiter(0x0880'2100, 'K', false);  //killed as it waits
    waiter(0x0880'2200, 'H', false);  //gets it as its holder ends
    waiter(0x0880'2300, 'C', false);  //cancelled
    waiter(0x0880'2400, 'X', false);  //cancelled, then told it's deleted
    Assembler holder{m, 0x0880'2500};  //takes the mutex, sleeps, and ends holding it
    mutex(holder); holder.li(a1, 1); holder.li(a2, 0);
    holder.call("sceKernelLockMutex");
    holder.call("sceKernelSleepThread");
    holder.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("mutex")); main.li(a1, 0x200); main.li(a2, 0); main.li(a3, 0);
    main.call("sceKernelCreateMutex");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    mutex(main); main.li(a1, 2); main.li(a2, 0);
    main.call("sceKernelLockMutex");  //twice over
    mutex(main); main.li(a1, 1);
    main.call("sceKernelUnlockMutex");  //still held, once
    main.li(t0, Timeout); main.li(t1, 500); main.put(sw(t1, 0, t0));
    start(main, m, 0x0880'2000, 0x30);
    start(main, m, 0x0880'2100, 0x30);
    main.put(addu(s2, s0, zero));
    delay(main, 1000);  //T's 500 microseconds run out
    main.put(addu(a0, s2, zero));
    main.call("sceKernelTerminateThread");  //K leaves the line
    mutex(main); main.li(a1, 1);
    main.call("sceKernelUnlockMutex");  //free, nobody waiting: K didn't get it
    start(main, m, 0x0880'2500, 0x18);  //takes it at once, and sleeps
    main.put(addu(s3, s0, zero));
    start(main, m, 0x0880'2200, 0x30);
    delay(main, 1000);  //H waits behind it
    main.put(addu(a0, s3, zero));
    main.call("sceKernelWakeupThread");  //the holder ends, handing the mutex to H
    delay(main, 1000);
    mutex(main); main.li(a1, 1);
    main.call("sceKernelTryLockMutex");  //H holds it
    noteResult(main);
    start(main, m, 0x0880'2300, 0x30);
    start(main, m, 0x0880'2400, 0x30);
    delay(main, 1000);  //C and X wait behind H
    mutex(main); main.li(a1, 1); main.li(a2, Ends);
    main.call("sceKernelCancelMutex");  //C and X cancelled: main holds it
    delay(main, 1000);
    start(main, m, 0x0880'2400, 0x30);  //X again: waits behind main
    delay(main, 1000);
    mutex(main);
    main.call("sceKernelDeleteMutex");
    delay(main, 1000);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::vector<u32> expected = {
      'T', Kernel::ErrorWaitTimeout, 'H', 0, Kernel::ErrorMutexLocked, 'C', Kernel::ErrorWaitCancelled,
      'X', Kernel::ErrorWaitCancelled, 'X', Kernel::ErrorWaitDeleted,
    };
    CHECK(logged(m) == expected, true);
    CHECK(word(m, Timeout), 0);
    CHECK(word(m, Ends), 2);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//sceKernelLockMutexCB: a callback notified while its thread waits for a mutex runs on it, and the wait goes on; one
//notified before the call runs even though the mutex is free (it needn't wait).
static auto mutexCallbacks() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler callback{m, 0x0880'3000};  //notes 'B'
    note(callback, 'B');
    callback.li(v0, 0);
    callback.put(jr(ra));
    callback.put(nop);
    Assembler waiter{m, 0x0880'2000};  //makes the callback, waits with callbacks, notes 'W' and its result
    waiter.li(a0, m.string("cb")); waiter.li(a1, 0x0880'3000); waiter.li(a2, 0);
    waiter.call("sceKernelCreateCallback");
    waiter.li(t0, R); waiter.put(sw(v0, 4, t0));
    mutex(waiter); waiter.li(a1, 1); waiter.li(a2, 0);
    waiter.call("sceKernelLockMutexCB");
    waiter.put(addu(s1, v0, zero));
    note(waiter, 'W');
    waiter.put(addu(v0, s1, zero));
    noteResult(waiter);
    mutex(waiter); waiter.li(a1, 1);
    waiter.call("sceKernelUnlockMutex");
    waiter.li(t0, R); waiter.put(lw(a0, 4, t0)); waiter.li(a1, 5);
    waiter.call("sceKernelNotifyCallback");  //its own, as it's about to lock a free mutex
    mutex(waiter); waiter.li(a1, 1); waiter.li(a2, 0);
    waiter.call("sceKernelLockMutexCB");
    noteResult(waiter);
    waiter.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("mutex")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 0);
    main.call("sceKernelCreateMutex");  //held by main
    main.li(t0, R); main.put(sw(v0, 0, t0));
    start(main, m, 0x0880'2000, 0x30);
    delay(main, 1000);
    main.li(t0, R); main.put(lw(a0, 4, t0)); main.li(a1, 1);
    main.call("sceKernelNotifyCallback");
    delay(main, 1000);  //the callback runs; the wait goes on
    note(main, 'U');
    mutex(main); main.li(a1, 1);
    main.call("sceKernelUnlockMutex");
    delay(main, 1000);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(logged(m) == std::vector<u32>({'B', 'U', 'W', 0, 'B', 0}), true);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//A state saved while a thread waits for a mutex main holds (with a timeout, so its time is in the state too) loads
//into a fresh machine, which makes the same state; both carry on alike: main frees it, the waiter gets it, what's
//left of its timeout written back as the PSP's waittimeouts has a timeout last (its 10000 microseconds and 35).
static auto mutexState() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler waiter{m, 0x0880'2000};
    mutex(waiter); waiter.li(a1, 1); waiter.li(a2, R + 0x40);
    waiter.call("sceKernelLockMutex");
    noteResult(waiter);
    waiter.call("sceKernelExitThread");  //ends holding it
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("mutex")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 0);
    main.call("sceKernelCreateMutex");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.li(t0, R + 0x40); main.li(t1, 10'000); main.put(sw(t1, 0, t0));
    start(main, m, 0x0880'2000, 0x30);
    delay(main, 3000);  //the state is saved in here
    mutex(main); main.li(a1, 1);
    main.call("sceKernelUnlockMutex");
    delay(main, 1000);
    mutex(main); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelTryLockMutex");  //free again: its holder ended
    noteResult(main);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency / 1000);
    CHECK(m.kernel.exited, false);
    CHECK(m.kernel.mutexes.size(), 1);
    auto state = saveState(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    CHECK(loadState(n, state), true);
    CHECK(saveState(n) == state, true);
    for(auto* each : {&m, &n}) {
      each->kernel.run(Kernel::CPUFrequency / 10);
      CHECK(each->kernel.exited, true);
      CHECK(logged(*each) == std::vector<u32>({0, 0}), true);
      u32 left = word(*each, R + 0x40);  //10035 less the 3000 and 25 of main's delay, and what main ran
      CHECK(left > 7000 && left <= 7010, true);
      CHECK(roundTrip(*each), true);
    }
  }
}

//sceKernelReleaseWaitThread, as threads/threads/release recorded: a thread delaying, sleeping, or waiting on a
//semaphore, a kernel mutex, a lightweight mutex (which counts one waiter fewer) or an event flag is let go
//(RELEASE_WAIT), running at once when it's better than the caller; one not waiting (started and ended, or never
//started) NOT_WAIT; 0 and the caller itself ILLEGAL_THID; a thread there isn't UNKNOWN_THID. And
//sceKernelCancelWakeupThread forgets the wakeups that came while a thread was awake, returning how many.
static auto releaseWaits() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Work = R + 0x80;  //a lightweight mutex's work area
    struct Kind { const char* call; u32 a0, a1, a2; } kinds[] = {
      {"sceKernelDelayThread", 10'000'000, 0, 0},
      {"sceKernelSleepThread", 0, 0, 0},
      {"sceKernelWaitSema", 0, 1, 0},           //(a0: the semaphore, at R + 4)
      {"sceKernelLockMutex", 0, 1, 0},          //(the mutex at R, which main holds)
      {"sceKernelLockLwMutex", Work, 1, 0},     //(main holds it)
      {"sceKernelWaitEventFlag", 0, 1, 0},      //(the flag at R + 12)
    };
    u32 entry = 0x0880'2000;
    for(auto& kind : kinds) {
      Assembler w{m, entry};
      if(kind.call == std::string{"sceKernelWaitSema"}) w.li(t0, R), w.put(lw(a0, 4, t0));
      else if(kind.call == std::string{"sceKernelLockMutex"}) mutex(w);
      else if(kind.call == std::string{"sceKernelWaitEventFlag"}) w.li(t0, R), w.put(lw(a0, 12, t0));
      else w.li(a0, kind.a0);
      w.li(a1, kind.a1); w.li(a2, kind.a2); w.li(a3, 0); w.li(t0, 0);
      w.call(kind.call);
      noteResult(w);
      w.call("sceKernelExitThread");
      entry += 0x100;
    }
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("mutex")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 0);
    main.call("sceKernelCreateMutex");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.li(a0, m.string("sema")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 1); main.li(t0, 0);
    main.call("sceKernelCreateSema");
    main.li(t0, R); main.put(sw(v0, 4, t0));
    main.li(a0, Work); main.li(a1, m.string("lw")); main.li(a2, 0); main.li(a3, 1); main.li(t0, 0);
    main.call("sceKernelCreateLwMutex");
    main.li(a0, m.string("flag")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 0);
    main.call("sceKernelCreateEventFlag");
    main.li(t0, R); main.put(sw(v0, 12, t0));
    for(u32 n = 0; n < 6; n++) {
      start(main, m, 0x0880'2000 + n * 0x100, 0x10);  //better than main: waits at once
      main.put(addu(a0, s0, zero));
      main.call("sceKernelReleaseWaitThread");  //it runs at once, before this returns
      noteResult(main);
      main.li(t0, R); main.put(sw(s0, 0x20 + n * 4, t0));
    }
    main.li(t0, Work + 12); main.put(lw(v0, 0, t0));
    noteResult(main);  //the lightweight mutex's waiters: none
    main.li(t0, R); main.put(lw(a0, 0x20, t0));  //the first: ended
    main.call("sceKernelReleaseWaitThread");
    noteResult(main);
    main.li(a0, 0);
    main.call("sceKernelReleaseWaitThread");
    noteResult(main);
    main.call("sceKernelGetThreadId");
    main.put(addu(a0, v0, zero));
    main.call("sceKernelReleaseWaitThread");
    noteResult(main);
    main.li(a0, 0xdead'beef);
    main.call("sceKernelReleaseWaitThread");
    noteResult(main);
    //wakeups while awake: two, forgotten (2), then none (0)
    main.call("sceKernelGetThreadId");
    main.put(addu(s1, v0, zero));
    main.put(addu(a0, s1, zero));
    main.call("sceKernelWakeupThread");
    main.put(addu(a0, s1, zero));
    main.call("sceKernelWakeupThread");
    main.li(a0, 0);
    main.call("sceKernelCancelWakeupThread");
    noteResult(main);
    main.put(addu(a0, s1, zero));
    main.call("sceKernelCancelWakeupThread");
    noteResult(main);
    main.li(a0, 0xdead'beef);
    main.call("sceKernelCancelWakeupThread");
    noteResult(main);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::vector<u32> expected;
    for(u32 n = 0; n < 6; n++) expected.push_back(Kernel::ErrorReleaseWait), expected.push_back(0);
    for(u32 value : {0u, Kernel::ErrorNotWait, Kernel::ErrorIllegalThread, Kernel::ErrorIllegalThread,
                     Kernel::ErrorUnknownThread, 2u, 0u, Kernel::ErrorUnknownThread}) {
      expected.push_back(value);
    }
    CHECK(logged(m) == expected, true);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//sceKernelCancelSema and sceKernelCancelEventFlag, as threads/semaphores/cancel and threads/events/cancel recorded:
//every waiter told WAIT_CANCEL (a flag's waiter told the new bits), how many there were written, the count or bits
//set; a semaphore's count past its maximum refused, nothing written; a negative count the initial one.
static auto cancelWaits() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler semaWaiter{m, 0x0880'2000};
    semaWaiter.li(t0, R); semaWaiter.put(lw(a0, 0, t0)); semaWaiter.li(a1, 1); semaWaiter.li(a2, 0);
    semaWaiter.call("sceKernelWaitSema");
    noteResult(semaWaiter);
    semaWaiter.call("sceKernelExitThread");
    Assembler flagWaiter{m, 0x0880'2100};
    flagWaiter.li(t0, R); flagWaiter.put(lw(a0, 4, t0)); flagWaiter.li(a1, 1); flagWaiter.li(a2, 0);
    flagWaiter.li(a3, R + 0x30); flagWaiter.li(t0, 0);
    flagWaiter.call("sceKernelWaitEventFlag");
    noteResult(flagWaiter);
    flagWaiter.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("sema")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 3); main.li(t0, 0);
    main.call("sceKernelCreateSema");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.li(a0, m.string("flag")); main.li(a1, 0x200); main.li(a2, 0); main.li(a3, 0);
    main.call("sceKernelCreateEventFlag");
    main.li(t0, R); main.put(sw(v0, 4, t0));
    main.li(t0, R); main.put(lw(a0, 0, t0)); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelWaitSema");  //the count, taken
    for(u32 n = 0; n < 2; n++) start(main, m, 0x0880'2000, 0x30), start(main, m, 0x0880'2100, 0x30);
    delay(main, 1000);  //two wait on each
    main.li(t0, R); main.put(lw(a0, 0, t0)); main.li(a1, 4); main.li(a2, R + 0x34);
    main.call("sceKernelCancelSema");  //past its maximum
    noteResult(main);
    main.li(t0, R); main.put(lw(a0, 0, t0)); main.li(a1, 2); main.li(a2, R + 0x34);
    main.call("sceKernelCancelSema");
    noteResult(main);
    main.li(t0, R); main.put(lw(a0, 4, t0)); main.li(a1, 0x7); main.li(a2, R + 0x38);
    main.call("sceKernelCancelEventFlag");
    noteResult(main);
    delay(main, 1000);  //the four run
    main.li(t0, R); main.put(lw(a0, 0, t0)); main.li(a1, u32(-1)); main.li(a2, 0);
    main.call("sceKernelCancelSema");  //the initial count
    main.li(t0, R); main.put(lw(a0, 0, t0)); main.li(a1, R + 0x200); main.li(t0, 56); main.put(sw(t0, 0, a1));
    main.call("sceKernelReferSemaStatus");
    main.li(a0, 0x7777); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelCancelSema");
    noteResult(main);
    main.li(a0, 0x7777); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelCancelEventFlag");
    noteResult(main);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto c = Kernel::ErrorWaitCancelled;
    CHECK(logged(m) == std::vector<u32>({Kernel::ErrorIllegalCount, 0, 0, c, c, c, c,
                                          Kernel::ErrorUnknownSemaphore, Kernel::ErrorUnknownEventFlag}), true);
    CHECK(word(m, R + 0x34), 2);
    CHECK(word(m, R + 0x38), 2);
    CHECK(word(m, R + 0x30), 7);  //the bits a flag's waiter was told: the new ones
    CHECK(word(m, R + 0x22c), 1);  //the semaphore's count: its initial one
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//Mutexes outside a thread: an alarm's handler trying, unlocking, cancelling or locking a kernel mutex, or trying or
//unlocking a lightweight one, is refused (ILLEGAL_CONTEXT), whether it went off with no thread running (main
//delaying) or interrupted main (spinning): neither mutex is left held, by nobody or by main, and main takes and frees
//both afterwards; the state saved at the end loads. Called directly with no thread running, the same.
static auto mutexesOutsideThreads() -> void {
  constexpr u32 Work = R + 0x80;  //the lightweight mutex's work area
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler handler{m, 0x0880'3000};
    handler.put(addiu(sp, sp, -16));
    handler.put(sw(ra, 12, sp));
    struct Call { const char* name; bool light; u32 a1, a2; } calls[] = {
      {"sceKernelTryLockMutex", false, 1, 0}, {"sceKernelUnlockMutex", false, 1, 0},
      {"sceKernelCancelMutex", false, 1, 0}, {"sceKernelLockMutex", false, 1, 0},
      {"sceKernelTryLockLwMutex", true, 1, 0}, {"sceKernelUnlockLwMutex", true, 1, 0},
    };
    for(auto& call : calls) {
      if(call.light) handler.li(a0, Work);
      else mutex(handler);
      handler.li(a1, call.a1); handler.li(a2, call.a2);
      handler.call(call.name);
      noteResult(handler);
    }
    handler.put(lw(ra, 12, sp));
    handler.li(v0, 0);  //not again
    handler.put(jr(ra));
    handler.put(addiu(sp, sp, 16));

    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("mutex")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 0);
    main.call("sceKernelCreateMutex");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.li(a0, Work); main.li(a1, m.string("lw")); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelCreateLwMutex");
    auto takeAndFree = [&] {
      mutex(main); main.li(a1, 1);
      main.call("sceKernelTryLockMutex");
      noteResult(main);
      mutex(main); main.li(a1, 1);
      main.call("sceKernelUnlockMutex");
      noteResult(main);
      main.li(a0, Work); main.li(a1, 1);
      main.call("sceKernelTryLockLwMutex");
      noteResult(main);
      main.li(a0, Work); main.li(a1, 1);
      main.call("sceKernelUnlockLwMutex");
      noteResult(main);
    };
    main.li(a0, 300); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelSetAlarm");
    delay(main, 2000);  //no thread runs as it goes off
    takeAndFree();
    main.li(a0, 300); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelSetAlarm");
    main.li(s1, 400'000);  //three instructions a round: some 3.6 ms, so it goes off in here
    u32 loop = main.here();
    main.put(addiu(s1, s1, -1));
    main.put(bne(s1, zero, int32_t(loop - (main.here() + 4)) / 4));
    main.put(nop);
    takeAndFree();
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::vector<u32> expected;
    for(u32 round = 0; round < 2; round++) {
      for(u32 n = 0; n < 6; n++) expected.push_back(Kernel::ErrorIllegalContext);
      for(u32 n = 0; n < 4; n++) expected.push_back(0);
    }
    CHECK(logged(m) == expected, true);
    u32 uid = word(m, R);
    CHECK(m.kernel.mutexes[uid].count, 0);
    CHECK(m.kernel.mutexes[uid].owner, 0);
    CHECK(word(m, Work), 0);
    CHECK(word(m, Work + 4), 0);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }

  KernelMachine m;  //no thread running
  u32 name = m.string("mutex");
  CHECK(m.call("sceKernelCreateMutex", {name, 0, 1, 0}), Kernel::ErrorIllegalContext);
  CHECK(m.call("sceKernelCreateLwMutex", {Work, name, 0, 1, 0}), Kernel::ErrorIllegalContext);
  u32 uid = m.call("sceKernelCreateMutex", {name, 0, 0, 0});
  CHECK(m.call("sceKernelCreateLwMutex", {Work, name, 0, 0, 0}), 0);
  CHECK(m.call("sceKernelTryLockMutex", {uid, 1}), Kernel::ErrorIllegalContext);
  CHECK(m.call("sceKernelLockMutex", {uid, 1, 0}), Kernel::ErrorIllegalContext);
  CHECK(m.call("sceKernelUnlockMutex", {uid, 1}), Kernel::ErrorIllegalContext);
  CHECK(m.call("sceKernelCancelMutex", {uid, 1, 0}), Kernel::ErrorIllegalContext);
  CHECK(m.call("sceKernelTryLockLwMutex", {Work, 1}), Kernel::ErrorIllegalContext);
  CHECK(m.call("sceKernelLockLwMutex", {Work, 1, 0}), Kernel::ErrorIllegalContext);
  CHECK(m.call("sceKernelUnlockLwMutex", {Work, 1}), Kernel::ErrorIllegalContext);
  CHECK((status(m, uid) == std::array<u32, 5>{0, 0, 0, 0xffff'ffff, 0}), true);
  CHECK(word(m, Work), 0);
  CHECK(roundTrip(m), true);
}

auto mutexTests() -> Tests {
  return {
    {"mutexes called directly", mutexCalls}, {"mutexes handed on in order", mutexOrder},
    {"mutexes waits ending", mutexWaits}, {"mutexes callbacks in a wait", mutexCallbacks},
    {"mutexes state with a waiter", mutexState}, {"threads released from waits", releaseWaits},
    {"semaphores and event flags cancelled", cancelWaits}, {"mutexes outside threads", mutexesOutsideThreads},
  };
}

}
