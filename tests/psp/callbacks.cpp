//Callbacks (ares/psp/kernel/events.cpp): notified, they run on their own thread when it waits in a function whose
//name ends in CB or calls sceKernelCheckCallback, then the thread goes back into its wait, which may have ended
//meanwhile. Programs written here run on both engines; the display's vertical blank functions are called directly.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {

constexpr u32 R = KernelMachine::Results;
constexpr u32 Log = R + 0x100;  //what the callbacks were called with: (count, word) pairs, R + 8 counting the words

//A callback that writes down how it was called, prints "callback", and returns its own argument (so one made with 1
//is deleted once it has run). With pause, it first delays 2 ms: a wait of its own inside the callback.
auto loggingCallback(KernelMachine& m, u32 address, bool pause = false) -> void {
  Assembler a{m, address};
  a.put(addiu(sp, sp, -16));
  a.put(sw(ra, 12, sp));
  a.put(sw(a2, 8, sp));
  a.li(t0, R);
  a.put(lw(t1, 8, t0));
  a.put(sll(t2, t1, 2));
  a.put(addu(t2, t2, t0));
  a.put(sw(a0, 0x100, t2));
  a.put(sw(a1, 0x104, t2));
  a.put(addiu(t1, t1, 2));
  a.put(sw(t1, 8, t0));
  if(pause) {
    a.li(a0, 2000);
    a.call("sceKernelDelayThread");
  }
  a.print("callback\n");
  a.put(lw(v0, 8, sp));
  a.put(lw(ra, 12, sp));
  a.put(jr(ra));
  a.put(addiu(sp, sp, 16));
}

auto logged(KernelMachine& m) -> std::vector<u32> {
  std::vector<u32> words;
  for(u32 n = 0; n < m.system.memory.read(4, R + 8); n++) words.push_back(m.system.memory.read(4, Log + n * 4));
  return words;
}

//Creates and starts a thread (name, entry, priority) from a program; its ID in s0.
auto startThread(Assembler& a, KernelMachine& m, const char* name, u32 entry, u32 priority) -> void {
  a.li(a0, m.string(name)); a.li(a1, entry); a.li(a2, priority); a.li(a3, 0x1000); a.li(t0, 0); a.li(t1, 0);
  a.call("sceKernelCreateThread");
  a.put(addu(s0, v0, zero));
  a.put(addu(a0, s0, zero)); a.li(a1, 0); a.li(a2, 0);
  a.call("sceKernelStartThread");
}

}

//The main thread sleeps where its callback may run; a worker of lower priority notifies the callback, which runs on
//the main thread at once (its priority is the higher), each time with a count of 1 and that notification's word, the
//main thread going back to sleep after each; the worker wakes it. Then, while it runs, two notifications add up: the
//callback runs once, with a count of 2 and the last word, when the main thread checks; a second check finds nothing.
static auto runInWaits() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    loggingCallback(m, 0x0880'3000);

    Assembler worker{m, 0x0880'2000};
    worker.print("worker\n");
    worker.li(t0, R); worker.put(lw(a0, 0, t0)); worker.li(a1, 5);
    worker.call("sceKernelNotifyCallback");
    worker.print("worker notified\n");
    worker.li(t0, R); worker.put(lw(a0, 0, t0)); worker.li(a1, 7);
    worker.call("sceKernelNotifyCallback");
    worker.li(t0, R); worker.put(lw(a0, 4, t0));
    worker.call("sceKernelWakeupThread");
    worker.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    main.call("sceKernelGetThreadId");
    main.li(t0, R); main.put(sw(v0, 4, t0));
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    startThread(main, m, "worker", 0x0880'2000, 0x30);
    main.print("main sleeps\n");
    main.call("sceKernelSleepThreadCB");
    main.li(t0, R); main.put(sw(v0, 0x20, t0));
    main.print("main woke\n");
    main.li(t0, R); main.put(lw(a0, 0, t0)); main.li(a1, 11);
    main.call("sceKernelNotifyCallback");
    main.li(t0, R); main.put(lw(a0, 0, t0)); main.li(a1, 12);
    main.call("sceKernelNotifyCallback");
    main.li(t0, R); main.put(lw(a0, 0, t0));
    main.call("sceKernelGetCallbackCount");
    main.li(t0, R); main.put(sw(v0, 0x24, t0));
    main.call("sceKernelCheckCallback");
    main.li(t0, R); main.put(sw(v0, 0x28, t0));
    main.call("sceKernelCheckCallback");
    main.li(t0, R); main.put(sw(v0, 0x2c, t0));
    main.call("sceKernelExitGame");

    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.output == "main sleeps\nworker\ncallback\nworker notified\ncallback\nmain woke\ncallback\n", true);
    if(m.output != "main sleeps\nworker\ncallback\nworker notified\ncallback\nmain woke\ncallback\n") {
      std::printf("  output: [%s]\n", m.output.c_str());
    }
    CHECK(logged(m) == std::vector<u32>({1, 5, 1, 7, 2, 12}), true);
    CHECK(m.system.memory.read(4, R + 0x20), 0);  //sceKernelSleepThreadCB, once woken
    CHECK(m.system.memory.read(4, R + 0x24), 2);
    CHECK(m.system.memory.read(4, R + 0x28), 1);
    CHECK(m.system.memory.read(4, R + 0x2c), 0);
    CHECK(m.notes.size(), 0);
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
  }
}

//What ends a wait while its thread runs a callback ends it once the callback is done: a semaphore signalled while
//the callback waits by itself (2 ms) is taken then; a semaphore wait whose 500 microseconds ran out during it times
//out, its timeout left at 0; a delay longer than the callback goes on to its end; a vertical blank that came during
//it ends that wait. A callback that returns 1 is deleted after it has run.
static auto waitsGoOn() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    loggingCallback(m, 0x0880'3000, true);   //pausing; kept
    loggingCallback(m, 0x0880'3800, false);  //deleted once it has run (made with 1)
    constexpr u32 Semaphore = R + 0x10, Timeout = R + 0x14, Once = R + 0x18;

    //the worker: at each step it notifies the pausing callback, signals the semaphore at the first, and sleeps
    Assembler worker{m, 0x0880'2000};
    for(u32 step = 0; step < 4; step++) {
      worker.li(t0, R); worker.put(lw(a0, 0, t0)); worker.li(a1, 100 + step);
      worker.call("sceKernelNotifyCallback");
      if(step == 0) {
        worker.li(t0, Semaphore); worker.put(lw(a0, 0, t0)); worker.li(a1, 1);
        worker.call("sceKernelSignalSema");
      }
      worker.call("sceKernelSleepThread");
    }
    worker.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("pausing")); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.li(a0, m.string("s")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 1); main.li(t0, 0);
    main.call("sceKernelCreateSema");
    main.li(t0, Semaphore); main.put(sw(v0, 0, t0));
    startThread(main, m, "worker", 0x0880'2000, 0x30);
    main.put(addu(s1, s0, zero));  //the worker
    //1: the semaphore, signalled during the callback
    main.li(t0, Semaphore); main.put(lw(a0, 0, t0)); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelWaitSemaCB");
    main.li(t0, R); main.put(sw(v0, 0x20, t0));
    main.li(t0, Semaphore); main.put(lw(a0, 0, t0)); main.li(a1, 1);
    main.call("sceKernelPollSema");  //taken: nothing left
    main.li(t0, R); main.put(sw(v0, 0x24, t0));
    //2: a timeout running out during the callback
    main.put(addu(a0, s1, zero));
    main.call("sceKernelWakeupThread");
    main.li(t0, Timeout); main.li(t1, 500); main.put(sw(t1, 0, t0));
    main.li(t0, Semaphore); main.put(lw(a0, 0, t0)); main.li(a1, 1); main.li(a2, Timeout);
    main.call("sceKernelWaitSemaCB");
    main.li(t0, R); main.put(sw(v0, 0x28, t0));
    //3: a delay of 10 ms, the callback taking 2 of them
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s2, v0, zero));
    main.put(addu(a0, s1, zero));
    main.call("sceKernelWakeupThread");
    main.li(a0, 10000);
    main.call("sceKernelDelayThreadCB");
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s2));
    main.li(t0, R); main.put(sw(v0, 0x2c, t0));
    //4: a vertical blank during the callback: the wait starts 1.7 ms before one, the callback takes 2
    main.call("sceDisplayWaitVblankStart");
    main.call("sceDisplayGetVcount");
    main.put(addu(s2, v0, zero));
    main.li(a0, 15000);
    main.call("sceKernelDelayThread");
    main.put(addu(a0, s1, zero));
    main.call("sceKernelWakeupThread");
    main.call("sceDisplayWaitVblankStartCB");
    main.call("sceDisplayGetVcount");
    main.put(subu(v0, v0, s2));
    main.li(t0, R); main.put(sw(v0, 0x30, t0));
    //a callback returning 1, run by a check: deleted
    main.li(a0, m.string("once")); main.li(a1, 0x0880'3800); main.li(a2, 1);
    main.call("sceKernelCreateCallback");
    main.li(t0, Once); main.put(sw(v0, 0, t0));
    main.put(addu(a0, v0, zero)); main.li(a1, 9);
    main.call("sceKernelNotifyCallback");
    main.call("sceKernelCheckCallback");
    main.li(t0, Once); main.put(lw(a0, 0, t0));
    main.call("sceKernelGetCallbackCount");
    main.li(t0, R); main.put(sw(v0, 0x34, t0));
    main.call("sceKernelExitGame");

    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency);
    CHECK(m.kernel.exited, true);
    CHECK(m.system.memory.read(4, R + 0x20), 0);                         //the semaphore taken after the callback
    CHECK(m.system.memory.read(4, R + 0x24), Kernel::ErrorSemaphoreZero);
    CHECK(m.system.memory.read(4, R + 0x28), Kernel::ErrorWaitTimeout);  //500 microseconds ran out in 2 ms
    CHECK(m.system.memory.read(4, Timeout), 0);
    u32 delayed = m.system.memory.read(4, R + 0x2c);
    CHECK(delayed >= 10000 && delayed < 10200, true);                    //the delay's own length, not more
    CHECK(m.system.memory.read(4, R + 0x30), 1);                         //it came during the callback: no more
    CHECK(m.system.memory.read(4, R + 0x34), Kernel::ErrorUnknownCallback);
    CHECK(logged(m) == std::vector<u32>({1, 100, 1, 101, 1, 102, 1, 103, 1, 9}), true);
    CHECK(m.output == "callback\ncallback\ncallback\ncallback\ncallback\n", true);
    CHECK(m.notes.size(), 0);
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
  }
}

//A wait that lets callbacks run but needn't wait, what it waits for being there already, still runs the callbacks
//notified by then, and returns what it got once they're done: a semaphore's count there (taken: none left after), a
//wakeup that came first, an event flag's bits set, a fixed pool's free block, a thread that has ended (its exit
//status), the drive ready, and the vertical blank it's in (1).
static auto waitsAtOnce() -> void {
  for(bool recompile : {false, true}) {
    HostFolder disc;
    KernelMachine m;
    m.kernel.mount("disc0", disc.path.string());  //a folder standing for the disc: the drive is ready
    loggingCallback(m, 0x0880'3000);
    Assembler ender{m, 0x0880'2000};
    ender.li(a0, 0x55);
    ender.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    auto notify = [&](u32 word) {
      main.li(t0, R); main.put(lw(a0, 0, t0)); main.li(a1, word);
      main.call("sceKernelNotifyCallback");
    };
    //what wait n returned, at R + 0x20 + 4n, and how many words the callbacks had logged by then, at R + 0x60 + 4n
    auto record = [&](u32 n) {
      main.li(t0, R + 0x20 + n * 4); main.put(sw(v0, 0, t0));
      main.li(t0, R); main.put(lw(t1, 8, t0)); main.li(t0, R + 0x60 + n * 4); main.put(sw(t1, 0, t0));
    };
    main.li(a0, m.string("s")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 1); main.li(t0, 0);
    main.call("sceKernelCreateSema");
    main.put(addu(s1, v0, zero));
    notify(1);
    main.put(addu(a0, s1, zero)); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelWaitSemaCB");
    record(1);
    main.put(addu(a0, s1, zero)); main.li(a1, 1);
    main.call("sceKernelPollSema");
    main.li(t0, R + 0x40); main.put(sw(v0, 0, t0));
    main.call("sceKernelGetThreadId");
    main.put(addu(a0, v0, zero));
    main.call("sceKernelWakeupThread");
    notify(2);
    main.call("sceKernelSleepThreadCB");
    record(2);
    main.li(a0, m.string("f")); main.li(a1, 0); main.li(a2, 1); main.li(a3, 0);
    main.call("sceKernelCreateEventFlag");
    main.put(addu(s1, v0, zero));
    notify(3);
    main.put(addu(a0, s1, zero)); main.li(a1, 1); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelWaitEventFlagCB");
    record(3);
    main.li(a0, m.string("p")); main.li(a1, 2); main.li(a2, 0); main.li(a3, 16); main.li(t0, 1); main.li(t1, 0);
    main.call("sceKernelCreateFpl");
    main.put(addu(s1, v0, zero));
    notify(4);
    main.put(addu(a0, s1, zero)); main.li(a1, R + 0x44); main.li(a2, 0);
    main.call("sceKernelAllocateFplCB");
    record(4);
    startThread(main, m, "ender", 0x0880'2000, 0x10);  //it runs and ends at once, its priority the higher
    notify(5);
    main.put(addu(a0, s0, zero)); main.li(a1, 0);
    main.call("sceKernelWaitThreadEndCB");
    record(5);
    notify(6);
    main.li(a0, Kernel::UmdReadable); main.li(a1, 0);
    main.call("sceUmdWaitDriveStatCB");
    record(6);
    main.call("sceDisplayWaitVblankStart");
    notify(7);
    main.call("sceDisplayWaitVblankCB");
    record(7);
    main.call("sceKernelExitGame");

    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::vector<u32> results, words;
    for(u32 n = 1; n <= 7; n++) results.push_back(m.system.memory.read(4, R + 0x20 + n * 4));
    for(u32 n = 1; n <= 7; n++) words.push_back(m.system.memory.read(4, R + 0x60 + n * 4));
    CHECK(results == std::vector<u32>({0, 0, 0, 0, 0x55, 0, 1}), true);
    CHECK(words == std::vector<u32>({2, 4, 6, 8, 10, 12, 14}), true);  //each wait's callback ran before it returned
    CHECK(logged(m) == std::vector<u32>({1, 1, 1, 2, 1, 3, 1, 4, 1, 5, 1, 6, 1, 7}), true);
    CHECK(m.system.memory.read(4, R + 0x40), Kernel::ErrorSemaphoreZero);  //the wait took the count
    CHECK(m.system.memory.read(4, R + 0x44) >= Kernel::UserMemory, true);  //the pool's block
    CHECK(m.notes.size(), 0);
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
  }
}

//A callback notified for a thread of lower priority waiting where it may run doesn't run until the notifying thread
//waits; one notified for a thread waiting where callbacks can't run waits for the next CB wait; a check from inside a
//callback is refused.
static auto byPriority() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    loggingCallback(m, 0x0880'3000);
    //a callback that checks for callbacks from inside one
    Assembler checking{m, 0x0880'3800};
    checking.put(addiu(sp, sp, -16));
    checking.put(sw(ra, 12, sp));
    checking.call("sceKernelCheckCallback");
    checking.li(t0, R); checking.put(sw(v0, 0x30, t0));
    checking.put(lw(ra, 12, sp));
    checking.li(v0, 0);
    checking.put(jr(ra));
    checking.put(addiu(sp, sp, 16));

    //the low thread: makes the callbacks, then waits where they may run
    Assembler low{m, 0x0880'2000};
    low.li(a0, m.string("cb")); low.li(a1, 0x0880'3000); low.li(a2, 0);
    low.call("sceKernelCreateCallback");
    low.li(t0, R); low.put(sw(v0, 0, t0));
    low.li(a0, m.string("checking")); low.li(a1, 0x0880'3800); low.li(a2, 0);
    low.call("sceKernelCreateCallback");
    low.li(t0, R); low.put(sw(v0, 4, t0));
    low.print("low sleeps\n");
    low.call("sceKernelSleepThreadCB");
    low.print("low woke\n");
    low.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    startThread(main, m, "low", 0x0880'2000, 0x30);
    main.put(addu(s1, s0, zero));
    main.li(a0, 1000);
    main.call("sceKernelDelayThread");  //the low thread makes its callbacks and sleeps
    main.li(t0, R); main.put(lw(a0, 0, t0)); main.li(a1, 3);
    main.call("sceKernelNotifyCallback");
    main.print("main notified\n");       //before the callback: main's priority is the higher
    main.li(t0, R); main.put(lw(a0, 4, t0)); main.li(a1, 4);
    main.call("sceKernelNotifyCallback");
    main.li(a0, 1000);
    main.call("sceKernelDelayThread");  //now they run
    main.print("main again\n");
    main.put(addu(a0, s1, zero));
    main.call("sceKernelWakeupThread");
    main.li(a0, 1000);
    main.call("sceKernelDelayThread");
    main.call("sceKernelExitGame");

    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.output == "low sleeps\nmain notified\ncallback\nmain again\nlow woke\n", true);
    if(m.output != "low sleeps\nmain notified\ncallback\nmain again\nlow woke\n") {
      std::printf("  output: [%s]\n", m.output.c_str());
    }
    CHECK(m.system.memory.read(4, R + 0x30), Kernel::ErrorIllegalContext);
    CHECK(logged(m) == std::vector<u32>({1, 3}), true);
  }
}

//Called directly: refusals of a function address with its top bits set and of unknown callbacks; counts, cancelling,
//the status as far as its size says; a thread's callbacks deleted with it; a check with no thread running.
static auto callbackCalls() -> void {
  KernelMachine m;
  u32 name = m.string("direct");
  CHECK(m.call("sceKernelCreateCallback", {name, 0x1880'3000, 0}), Kernel::ErrorIllegalAddress);
  u32 callback = m.call("sceKernelCreateCallback", {name, 0x0880'3000, 0x55});
  CHECK(callback < 0x8000'0000, true);
  for(const char* function : {"sceKernelNotifyCallback", "sceKernelCancelCallback", "sceKernelGetCallbackCount",
                              "sceKernelReferCallbackStatus", "sceKernelDeleteCallback"}) {
    CHECK(m.call(function, {0x7777, 0x0892'0000}), Kernel::ErrorUnknownCallback);
  }
  CHECK(m.call("sceKernelNotifyCallback", {callback, 1}), 0);
  CHECK(m.call("sceKernelNotifyCallback", {callback, 2}), 0);
  CHECK(m.call("sceKernelGetCallbackCount", {callback}), 2);
  constexpr u32 Info = R;
  m.system.memory.write(4, Info, 56);
  CHECK(m.call("sceKernelReferCallbackStatus", {callback, Info}), 0);
  CHECK(m.system.memory.readString(Info + 4, 32) == "direct", true);
  CHECK(m.system.memory.read(4, Info + 36), 0);  //made with no thread running
  CHECK(m.system.memory.read(4, Info + 40), 0x0880'3000);
  CHECK(m.system.memory.read(4, Info + 44), 0x55);
  CHECK(m.system.memory.read(4, Info + 48), 2);
  CHECK(m.system.memory.read(4, Info + 52), 2);
  m.system.memory.write(4, R + 0x40, 8);  //a size of 8: the name's first four bytes alone
  m.system.memory.write(4, R + 0x48, 0xdead'beef);
  CHECK(m.call("sceKernelReferCallbackStatus", {callback, R + 0x40}), 0);
  CHECK(m.system.memory.readString(R + 0x44, 4) == "dire", true);
  CHECK(m.system.memory.read(4, R + 0x48), 0xdead'beef);
  CHECK(m.call("sceKernelCancelCallback", {callback}), 0);
  CHECK(m.call("sceKernelGetCallbackCount", {callback}), 0);
  CHECK(m.call("sceKernelCheckCallback", {}), 0);  //no thread
  //made by a thread: deleted with it
  s32 thread = m.kernel.createThread("owner", 0x0880'1000, 0x20, 0x1000, 0, 0);
  m.kernel.current = m.kernel.threads[thread].get();
  u32 owned = m.call("sceKernelCreateCallback", {name, 0x0880'3000, 0});
  m.kernel.current = nullptr;
  CHECK(m.kernel.callbacks.count(owned), 1);
  CHECK(m.call("sceKernelDeleteThread", {u32(thread)}), 0);
  CHECK(m.kernel.callbacks.count(owned), 0);
  CHECK(m.kernel.callbacks.count(callback), 1);
  CHECK(m.call("sceKernelDeleteCallback", {callback}), 0);
  CHECK(m.call("sceKernelGetCallbackCount", {callback}), Kernel::ErrorUnknownCallback);
}

//The vertical blank: in it for its first 0.77 ms after it starts, the line counted from its start (286 lines a
//frame), and sceDisplayWaitVblank not waiting while it's in it (returning 1).
static auto vblankTiming() -> void {
  KernelMachine m;
  auto at = [&](u64 cycles) { m.kernel.cycles = cycles; };
  at(0);
  CHECK(m.call("sceDisplayIsVblank", {}), 1);
  CHECK(m.call("sceDisplayGetCurrentHcount", {}), 0);
  CHECK(m.call("sceDisplayWaitVblank", {}), 1);
  at(Kernel::VblankLength - 1);
  CHECK(m.call("sceDisplayIsVblank", {}), 1);
  CHECK(m.call("sceDisplayGetCurrentHcount", {}), 13);
  at(Kernel::VblankLength);
  CHECK(m.call("sceDisplayIsVblank", {}), 0);
  at(Kernel::VblankCycles - 1);  //the next starts at VblankCycles
  CHECK(m.call("sceDisplayIsVblank", {}), 0);
  CHECK(m.call("sceDisplayGetCurrentHcount", {}), 285);
  CHECK(Kernel::VblankCycles / Kernel::LineCycles, 286);
}

//A vertical blank handler (sub-interrupt 2): called at each blank with (2, its argument) and the global pointer it
//was registered with, while enabled, not while interrupts are held off (it runs once they're let back on), and no
//more once released. Its semaphore wakes a thread that waits on nothing else (time goes on for the handler). Then
//the refusals, in their order.
static auto vblankHandler() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Count = R + 0x10, Semaphore = R + 0x14, Seen = R + 0x40;
    Assembler handler{m, 0x0880'3000};
    handler.li(t0, Count); handler.put(lw(t1, 0, t0)); handler.put(addiu(t1, t1, 1)); handler.put(sw(t1, 0, t0));
    handler.li(t0, Seen); handler.put(sw(a0, 0, t0)); handler.put(sw(a1, 4, t0)); handler.put(sw(gp, 8, t0));
    handler.put(addiu(sp, sp, -16)); handler.put(sw(ra, 12, sp));
    handler.li(t0, Semaphore); handler.put(lw(a0, 0, t0)); handler.li(a1, 1);
    handler.call("sceKernelSignalSema");
    handler.put(lw(ra, 12, sp)); handler.put(addiu(sp, sp, 16));
    handler.put(jr(ra));
    handler.put(nop);

    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("s")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 100); main.li(t0, 0);
    main.call("sceKernelCreateSema");
    main.li(t0, Semaphore); main.put(sw(v0, 0, t0));
    main.li(gp, 0x0889'0000);  //the global pointer the handler gets
    main.li(a0, 30); main.li(a1, 2); main.li(a2, 0x0880'3000); main.li(a3, 0x42);
    main.call("sceKernelRegisterSubIntrHandler");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.li(gp, 0);
    main.li(a0, 30); main.li(a1, 2);
    main.call("sceKernelEnableSubIntr");
    for(u32 n = 0; n < 3; n++) {  //three blanks, each waking main through the semaphore alone
      main.li(t0, Semaphore); main.put(lw(a0, 0, t0)); main.li(a1, 1); main.li(a2, 0);
      main.call("sceKernelWaitSema");
    }
    main.li(t0, Count); main.put(lw(t1, 0, t0)); main.li(t0, R); main.put(sw(t1, 4, t0));
    //held off across a blank: nothing; let back on: it runs
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s0, v0, zero));
    main.li(a0, 20000);
    main.call("sceKernelDelayThread");
    main.li(t0, Count); main.put(lw(t1, 0, t0)); main.li(t0, R); main.put(sw(t1, 8, t0));
    main.put(addu(a0, s0, zero));
    main.call("sceKernelCpuResumeIntr");
    main.li(t0, Count); main.put(lw(t1, 0, t0)); main.li(t0, R); main.put(sw(t1, 12, t0));
    main.li(a0, 30); main.li(a1, 2);
    main.call("sceKernelReleaseSubIntrHandler");
    main.li(a0, 40000);
    main.call("sceKernelDelayThread");
    main.li(t0, Count); main.put(lw(t1, 0, t0)); main.li(t0, R); main.put(sw(t1, 0x20, t0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.system.memory.read(4, R), 0);
    CHECK(m.system.memory.read(4, R + 4), 3);
    CHECK(m.system.memory.read(4, R + 8), 3);    //held off
    CHECK(m.system.memory.read(4, R + 12), 4);   //let back on: the blank that came meanwhile
    CHECK(m.system.memory.read(4, R + 0x20), 4);  //released
    CHECK(m.system.memory.read(4, Seen), 2);
    CHECK(m.system.memory.read(4, Seen + 4), 0x42);
    CHECK(m.system.memory.read(4, Seen + 8), 0x0889'0000);
    CHECK(m.notes.size(), 0);
  }
  KernelMachine m;
  auto registers = [&](u32 interrupt, u32 sub) {
    return m.call("sceKernelRegisterSubIntrHandler", {interrupt, sub, 0x0880'3000, 0});
  };
  CHECK(registers(67, 0), Kernel::ErrorIllegalInterruptCode);
  CHECK(registers(8, 0), Kernel::ErrorHandlerNotFound);       //no handler on the PSP for it
  CHECK(registers(4, 0), Kernel::ErrorIllegalInterruptCode);  //the kernel's alone
  CHECK(registers(30, 32), Kernel::ErrorIllegalInterruptCode);
  CHECK(registers(30, 19), Kernel::ErrorHandlerFound);        //held by the display driver
  CHECK(registers(30, 16), Kernel::ErrorIllegalInterruptCode);
  CHECK(registers(30, 15), 0);
  CHECK(registers(30, 15), Kernel::ErrorHandlerFound);
  CHECK(registers(25, 0), 0);
  CHECK(m.call("sceKernelReleaseSubIntrHandler", {30, 14}), Kernel::ErrorHandlerNotFound);
  CHECK(m.call("sceKernelReleaseSubIntrHandler", {30, 15}), 0);
  CHECK(m.call("sceKernelEnableSubIntr", {30, 33}), Kernel::ErrorIllegalInterruptCode);
  CHECK(m.call("sceKernelDisableSubIntr", {25, 0}), 0);
}

//Interrupts held off for ten seconds, about 600 vertical blanks: nothing piles up meanwhile, and when they're let
//back on, the handler runs once for all of them (as the PSP's interrupt controller keeps an interrupt pending, not a
//count). By a program; then called directly, to see the queue itself: with two handlers, the first runs as
//interrupts come back on, and the second alone waits its turn.
static auto heldOffOnce() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Count = R + 0x10;
    Assembler handler{m, 0x0880'3000};  //counts its calls
    handler.li(t0, Count); handler.put(lw(t1, 0, t0)); handler.put(addiu(t1, t1, 1)); handler.put(sw(t1, 0, t0));
    handler.put(jr(ra));
    handler.put(nop);

    Assembler main{m, 0x0880'1000};
    auto count = [&](u32 offset) {  //the handler's calls so far, written down
      main.li(t0, Count); main.put(lw(t1, 0, t0)); main.li(t0, R + offset); main.put(sw(t1, 0, t0));
    };
    main.li(a0, 30); main.li(a1, 2); main.li(a2, 0x0880'3000); main.li(a3, 0);
    main.call("sceKernelRegisterSubIntrHandler");
    main.li(a0, 30); main.li(a1, 2);
    main.call("sceKernelEnableSubIntr");
    main.call("sceDisplayWaitVblankStart");  //a blank: the handler runs once
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s0, v0, zero));
    count(0);
    main.li(a0, 10'000'000);
    main.call("sceKernelDelayThread");       //blank after blank, held off
    count(4);
    main.put(addu(a0, s0, zero));
    main.call("sceKernelCpuResumeIntr");     //as this returns, the handler runs: once
    count(8);
    main.call("sceDisplayGetVcount");
    main.li(t0, R); main.put(sw(v0, 12, t0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency * 11);
    CHECK(m.kernel.exited, true);
    CHECK(m.system.memory.read(4, R), 1);
    CHECK(m.system.memory.read(4, R + 4), 1);
    CHECK(m.system.memory.read(4, R + 8), 2);
    CHECK(m.system.memory.read(4, R + 12), 600);  //blanks since power on: the first, then 599 held off
    CHECK(m.kernel.calls.size(), 0);
    CHECK(m.notes.size(), 0);
  }
  KernelMachine d;
  CHECK(d.call("sceKernelRegisterSubIntrHandler", {30, 0, 0x0880'3000, 1}), 0);
  CHECK(d.call("sceKernelRegisterSubIntrHandler", {30, 5, 0x0880'3100, 2}), 0);
  CHECK(d.call("sceKernelEnableSubIntr", {30, 0}), 0);
  CHECK(d.call("sceKernelEnableSubIntr", {30, 5}), 0);
  CHECK(d.call("sceKernelCpuSuspendIntr", {}), 1);
  d.kernel.cycles = 600 * Kernel::VblankCycles;
  d.kernel.events();
  CHECK(d.kernel.vblanks, 600);
  CHECK(d.kernel.calls.size(), 0);
  CHECK(d.kernel.vblankPending, true);
  CHECK(d.call("sceKernelCpuResumeIntr", {1}), 0);
  CHECK(d.kernel.vblankPending, false);
  CHECK(d.kernel.interrupting, true);
  CHECK(d.system.ipu.pc, 0x0880'3000);
  CHECK(d.system.ipu.r[4], 0);  //(its number, its argument)
  CHECK(d.system.ipu.r[5], 1);
  CHECK(d.kernel.calls.size(), 1);
  if(d.kernel.calls.size() == 1) {
    CHECK(d.kernel.calls[0].function, 0x0880'3100);
    CHECK(d.kernel.calls[0].arguments[0], 5);
    CHECK(d.kernel.calls[0].arguments[1], 2);
  }
}

//Vertical blank handlers that take longer than a frame: two of 20 ms each, called directly, the clock moved on by
//hand to each blank and each return. A blank that comes while the last blank's handlers are still queued or running
//waits, once, so they run back to back, about 50 times each in 120 frames, and the queue never holds more than one
//blank's two. (The old code queued both again whenever a call returned, a blank always pending by then: 97 calls
//waited by frame 120, all of them saved in states.)
static auto longHandlers() -> void {
  KernelMachine d;
  for(u32 sub : {0u, 1u}) {
    CHECK(d.call("sceKernelRegisterSubIntrHandler", {30, sub, 0x0880'3000 + sub * 0x100, 0}), 0);
    CHECK(d.call("sceKernelEnableSubIntr", {30, sub}), 0);
  }
  constexpr u64 Length = Kernel::CPUFrequency / 50;  //20 ms
  u64 returnsAt = 0;
  u32 ran[2] = {}, most = 0;
  auto started = [&] {  //a handler may have taken the CPU just now: it returns 20 ms on
    if(!d.kernel.interrupting) return;
    ran[d.system.ipu.r[4] & 1]++;  //(its number, its argument)
    returnsAt = d.kernel.cycles + Length;
  };
  while(d.kernel.vblanks < 120) {
    bool returning = d.kernel.interrupting && returnsAt <= d.kernel.nextVblank;
    d.kernel.cycles = returning ? returnsAt : d.kernel.nextVblank;
    if(returning) {
      d.kernel.callReturned();  //and the next call waiting starts
      started();
    }
    d.kernel.events();
    most = std::max<u32>(most, d.kernel.calls.size());
    if(!d.kernel.interrupting) {
      d.kernel.startCall();
      started();
    }
  }
  CHECK(most, 2);
  CHECK(ran[0] >= 49 && ran[0] <= 50 && ran[1] >= 49 && ran[1] <= 50, true);
  if(ran[0] < 49 || ran[0] > 50 || ran[1] < 49 || ran[1] > 50) std::printf("  ran %u and %u times\n", ran[0], ran[1]);
  CHECK(d.kernel.vblankPending, true);
}

//Which interrupts and sub-interrupts a program may hang handlers on: every number pspautotests' intr/registersub and
//intr/releasesub tried on a PSP, registered then released (-2 to 69 with sub-interrupt 0; the vertical blank's
//sub-interrupts -2 to 69); null handlers; enabling and disabling (intr/enablesub), which look at the numbers alone.
//Called directly; then a vertical blank queues what's registered and enabled, in the order of its numbers.
static auto interruptTable() -> void {
  KernelMachine m;
  constexpr u32 Illegal = Kernel::ErrorIllegalInterruptCode, Found = Kernel::ErrorHandlerFound;
  constexpr u32 NotFound = Kernel::ErrorHandlerNotFound, Handler = 0x0880'3000;
  auto registers = [&](u32 interrupt, u32 sub, u32 function) {
    return m.call("sceKernelRegisterSubIntrHandler", {interrupt, sub, function, 0xdead'beef});
  };
  auto releases = [&](u32 interrupt, u32 sub) { return m.call("sceKernelReleaseSubIntrHandler", {interrupt, sub}); };
  auto enables = [&](u32 interrupt, u32 sub) { return m.call("sceKernelEnableSubIntr", {interrupt, sub}); };
  auto disables = [&](u32 interrupt, u32 sub) { return m.call("sceKernelDisableSubIntr", {interrupt, sub}); };
  auto single = [](s32 interrupt) {  //handlers without sub-interrupts
    for(s32 n : {7, 10, 12, 15, 16, 17, 18, 19, 20, 22, 23, 24, 26, 31, 36, 50, 56, 57, 58, 59, 60, 61, 65}) {
      if(interrupt == n) return true;
    }
    return false;
  };
  for(s32 interrupt = -2; interrupt < 70; interrupt++) {
    u32 registered = NotFound, released = NotFound;  //no handler at all
    if(interrupt < 0 || interrupt >= 67 || single(interrupt)) registered = released = Illegal;
    else if(interrupt == 25 || interrupt == 30) registered = released = 0;
    else if(interrupt == 4 || interrupt == 6 || interrupt == 21) registered = Illegal;  //the kernel's
    auto number = std::to_string(interrupt);
    check(__LINE__, ("register on " + number).c_str(), registers(u32(interrupt), 0, Handler), registered);
    check(__LINE__, ("release on " + number).c_str(), releases(u32(interrupt), 0), released);
  }
  for(s32 sub = -2; sub < 70; sub++) {
    u32 registered = Illegal, released = NotFound;
    if(sub < 0 || sub >= 32) released = Illegal;
    else if(sub < 16) registered = released = 0;
    else if((sub >= 18 && sub <= 20) || (sub >= 24 && sub <= 26)) registered = Found;  //the display driver's
    auto number = std::to_string(sub);
    check(__LINE__, ("register sub " + number).c_str(), registers(30, u32(sub), Handler), registered);
    check(__LINE__, ("release sub " + number).c_str(), releases(30, u32(sub)), released);
  }
  CHECK(registers(30, 1, 0), 0);  //a null handler takes no place
  CHECK(registers(30, 1, 0), 0);
  CHECK(releases(30, 1), NotFound);
  CHECK(registers(30, 1, Handler), 0);
  CHECK(registers(30, 1, Handler), Found);
  CHECK(registers(30, 1, 0), Found);
  CHECK(releases(30, 1), 0);
  CHECK(releases(30, 1), NotFound);
  CHECK(enables(70, 1), Illegal);
  CHECK(enables(30, 70), Illegal);
  CHECK(disables(70, 1), Illegal);
  CHECK(disables(30, 70), Illegal);
  CHECK(registers(30, 1, Handler), 0);
  CHECK(enables(30, 1), 0);
  CHECK(enables(30, 1), 0);
  CHECK(disables(30, 3), 0);  //no handler there: 0 all the same
  CHECK(disables(30, 3), 0);
  CHECK(enables(30, 2), 0);   //enabled first, registered after: it runs
  CHECK(registers(30, 2, Handler + 0x100), 0);
  CHECK(enables(30, 3), 0);   //enabled with no handler: nothing runs
  CHECK(registers(30, 4, Handler + 0x200), 0);
  CHECK(enables(30, 4), 0);   //released while enabled, registered again: disabled
  CHECK(releases(30, 4), 0);
  CHECK(registers(30, 4, Handler + 0x200), 0);
  m.kernel.cycles = Kernel::VblankCycles;
  m.kernel.events();
  CHECK(m.kernel.calls.size(), 2);
  if(m.kernel.calls.size() == 2) {
    CHECK(m.kernel.calls[0].function, Handler);
    CHECK(m.kernel.calls[0].arguments[0], 1);
    CHECK(m.kernel.calls[0].arguments[1], 0xdead'beef);
    CHECK(m.kernel.calls[1].function, Handler + 0x100);
    CHECK(m.kernel.calls[1].arguments[0], 2);
  }
}

auto callbackTests() -> Tests {
  return {
    {"callbacks in waits", runInWaits}, {"callbacks and waits going on", waitsGoOn},
    {"callbacks in waits ending at once", waitsAtOnce},
    {"callbacks by priority", byPriority}, {"callbacks called directly", callbackCalls},
    {"display vblank timing", vblankTiming}, {"interrupts vblank handler", vblankHandler},
    {"interrupts held off, delivered once", heldOffOnce}, {"interrupts handlers longer than a frame", longHandlers},
    {"interrupts numbers", interruptTable},
  };
}

}
