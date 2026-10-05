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

auto callbackTests() -> Tests {
  return {
    {"callbacks in waits", runInWaits}, {"callbacks and waits going on", waitsGoOn},
    {"callbacks by priority", byPriority}, {"callbacks called directly", callbackCalls},
    {"display vblank timing", vblankTiming}, {"interrupts vblank handler", vblankHandler},
  };
}

}
