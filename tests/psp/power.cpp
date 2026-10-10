//Power (ares/psp/kernel/power.cpp) and the small system functions retail games ask for besides: thread odds and ends
//(priorities, suspending, ending another thread, the stack's untouched bytes, a semaphore's status), clocks and
//dates, the Mersenne Twister, the kernel's printf, a DMA copy. Called directly, and in programs on both engines where
//threads take turns.
#include "kernel-machine.hpp"

#include <chrono>

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results;

auto floatBits(KernelMachine& m) -> float {
  float value;
  u32 bits = m.system.fpu.r[0];
  memcpy(&value, &bits, sizeof(value));
  return value;
}
}

//The power switch's callbacks: registered in a slot or the first free one, and told of the battery at once (on the
//charger, a battery in, full: 0x10e4), on the thread that made them, when it waits where they may run. Slots taken,
//none free, the system's and those out of range refused; unregistering an empty slot too.
static auto powerCallbacks() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler callback{m, 0x0880'3000};  //writes down its count and word
    callback.li(t0, R); callback.put(sw(a0, 0x10, t0)); callback.put(sw(a1, 0x14, t0));
    callback.li(v0, 0);
    callback.put(jr(ra));
    callback.put(nop);
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("power")); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.put(addu(s0, v0, zero));
    main.li(a0, u32(-1)); main.put(addu(a1, s0, zero));
    main.call("scePowerRegisterCallback");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.li(a0, 1000);
    main.call("sceKernelDelayThreadCB");  //it runs here
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.system.memory.read(4, R), 0);  //the first free slot
    CHECK(m.system.memory.read(4, R + 0x10), 1);
    CHECK(m.system.memory.read(4, R + 0x14), 0x10e4);
    CHECK(m.notes.size(), 0);
  }
  KernelMachine m;
  u32 callback = m.call("sceKernelCreateCallback", {m.string("c"), 0x0880'3000, 0});
  CHECK(m.call("scePowerRegisterCallback", {3, callback}), 0);
  CHECK(m.call("scePowerRegisterCallback", {3, callback}), Kernel::ErrorAlready);
  CHECK(m.call("scePowerRegisterCallback", {u32(-1), callback}), 0);  //slot 0 is free still
  CHECK(m.call("scePowerRegisterCallback", {16, callback}), Kernel::ErrorPrivilegeRequired);
  CHECK(m.call("scePowerRegisterCallback", {32, callback}), Kernel::ErrorInvalidIndex);
  CHECK(m.call("scePowerRegisterCallback", {u32(-2), callback}), Kernel::ErrorInvalidIndex);
  CHECK(m.call("scePowerRegisterCallback", {4, 0}), Kernel::ErrorInvalidID);
  for(u32 slot = 1; slot < 16; slot++) if(slot != 3) m.call("scePowerRegisterCallback", {slot, callback});
  CHECK(m.call("scePowerRegisterCallback", {u32(-1), callback}), Kernel::ErrorOutOfMemory);
  CHECK(m.call("scePowerUnregisterCallback", {3}), 0);
  CHECK(m.call("scePowerUnregisterCallback", {3}), Kernel::ErrorNotFound);
  CHECK(m.call("scePowerUnregisterCallback", {20}), Kernel::ErrorPrivilegeRequired);
  CHECK(m.call("scePowerGetBatteryLifePercent", {}), 100);
  CHECK(m.call("scePowerIsPowerOnline", {}), 1);
  CHECK(m.call("scePowerIsBatteryCharging", {}), 0);
}

//The clocks: 222 MHz (PLL 222, bus 111) at first; set to 333 the PLL goes to 333 and the bus to half of it, the
//calling thread waiting 16.6 ms meanwhile (between 266 and 333: 150 ms otherwise), and the getters read them back,
//the float ones in f0. Values out of range refused.
static auto powerClocks() -> void {
  KernelMachine m;
  CHECK(m.call("scePowerGetCpuClockFrequencyInt", {}), 222);
  CHECK(m.call("scePowerGetBusClockFrequencyInt", {}), 111);
  m.call("scePowerGetPllClockFrequencyFloat", {});
  CHECK(floatBits(m) == 222.0f, true);
  CHECK(m.call("scePowerSetClockFrequency", {333, 333, 166}), 0);  //no thread: nothing to wait
  CHECK(m.call("scePowerGetCpuClockFrequency", {}), 333);
  CHECK(m.call("scePowerGetPllClockFrequencyInt", {}), 333);
  CHECK(m.call("scePowerGetBusClockFrequency", {}), 166);
  m.call("scePowerGetCpuClockFrequencyFloat", {});
  CHECK(floatBits(m) == 333.0f, true);
  CHECK(m.call("scePowerSetClockFrequency", {300, 333, 166}), Kernel::ErrorInvalidValue);  //CPU above the PLL
  CHECK(m.call("scePowerSetClockFrequency", {333, 0, 166}), Kernel::ErrorInvalidValue);
  CHECK(m.call("scePowerSetClockFrequency", {333, 333, 167}), Kernel::ErrorInvalidValue);
  CHECK(m.call("scePowerSetClockFrequency", {334, 333, 166}), Kernel::ErrorInvalidValue);
  CHECK(m.call("scePowerSetCpuClockFrequency", {300}), 0);
  CHECK(m.call("scePowerGetCpuClockFrequencyInt", {}), 300);
  CHECK(m.call("scePowerSetCpuClockFrequency", {334}), Kernel::ErrorInvalidValue);
  CHECK(m.call("scePowerSetBusClockFrequency", {112}), Kernel::ErrorInvalidValue);
  for(bool recompile : {false, true}) {
    KernelMachine n;
    Assembler main{n, 0x0880'1000};
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s0, v0, zero));
    main.li(a0, 266); main.li(a1, 266); main.li(a2, 133);
    main.call("scePowerSetClockFrequency");  //222 to 266: 150 ms
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(s1, v0, s0));
    main.put(addu(s0, v0, zero));
    main.li(a0, 333); main.li(a1, 333); main.li(a2, 166);
    main.call("scePowerSetClockFrequency");  //266 to 333: 16.6 ms
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s0));
    main.li(t0, R); main.put(sw(s1, 0, t0)); main.put(sw(v0, 4, t0));
    main.call("sceKernelExitGame");
    n.runProgram(0x0880'1000, recompile);
    CHECK(n.kernel.exited, true);
    u32 first = n.system.memory.read(4, R), second = n.system.memory.read(4, R + 4);
    CHECK(first >= 150'000 && first < 150'100, true);
    CHECK(second >= 16'600 && second < 16'700, true);
  }
}

//The volatile memory: lent once (its address and size told), refused while lent, given back once.
static auto volatileMemory() -> void {
  KernelMachine m;
  CHECK(m.call("sceKernelVolatileMemTryLock", {1, R, R + 4}), Kernel::ErrorInvalidMode);
  CHECK(m.call("sceKernelVolatileMemTryLock", {0, R, R + 4}), 0);
  CHECK(m.system.memory.read(4, R), 0x0840'0000);
  CHECK(m.system.memory.read(4, R + 4), 0x0040'0000);
  CHECK(m.call("sceKernelVolatileMemTryLock", {0, R, R + 4}), Kernel::ErrorVolatileMemoryInUse);
  CHECK(m.call("sceKernelVolatileMemUnlock", {0}), 0);
  CHECK(m.call("sceKernelVolatileMemUnlock", {0}), Kernel::ErrorSemaphoreOverflow);
  CHECK(m.call("sceKernelPowerLock", {0}), 0);
  CHECK(m.call("sceKernelPowerLock", {1}), Kernel::ErrorInvalidMode);
  CHECK(m.call("sceKernelPowerTick", {0}), 0);
}

//The volatile memory locked as Burnout Dominator locks it as its first race loads (docs/psp-core.md's part 25): a
//thread's sceKernelVolatileMemLock (type 0) with nothing holding the memory returns 0 at once, a worse thread not
//running meanwhile, its address (0x08400000) and size (4 MiB) written; the memory there holds what's written; it's
//given back. (A state saved before the function was there had the game take its race's buffers from addresses
//without their top bits, and its GE spend each frame on a display list it couldn't finish.) On both engines, and the
//state round trip.
static auto volatileLockedAtOnce() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler worse{m, 0x0880'2000};
    worse.print("worse\n");
    worse.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("worse")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");
    main.li(a0, 0); main.li(a1, R + 4); main.li(a2, R + 8);
    main.call("sceKernelVolatileMemLock");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.print("locked\n");
    main.li(t0, R); main.put(lw(t1, 4, t0)); main.li(t2, 0x1234'5678); main.put(sw(t2, 0x100, t1));
    main.put(lw(t3, 0x100, t1)); main.put(sw(t3, 12, t0));
    main.li(a0, 0);
    main.call("sceKernelVolatileMemUnlock");
    main.li(t0, R); main.put(sw(v0, 16, t0));
    main.li(a0, 1000);
    main.call("sceKernelDelayThread");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.output == "locked\nworse\n", true);
    CHECK(m.system.memory.read(4, R), 0);
    CHECK(m.system.memory.read(4, R + 4), 0x0840'0000);
    CHECK(m.system.memory.read(4, R + 8), 0x0040'0000);
    CHECK(m.system.memory.read(4, R + 12), 0x1234'5678);
    CHECK(m.system.memory.read(4, R + 16), 0);
    CHECK(m.kernel.powerState.volatileLocked, false);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//The volatile memory waited for, as pspautotests' power/volatile/lock recorded: three threads of priorities 0x31,
//0x33 and 0x32 wait for it while main has it, and are served in the order they came, each giving it back as it's
//done (main's M, then 1, 2, 3); a type but 0 refused, the outputs left alone; with interrupts held off, and in an
//interrupt handler, refused without borrowing (CAN_NOT_WAIT, ILLEGAL_CONTEXT), the address and size written all the
//same, giving back afterwards refused as nothing's lent. A state saved while the three wait loads into another
//machine, which makes the same state and carries on alike. On both engines.
static auto volatileWaits() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    auto store = [&](Assembler& a, u32 offset) { a.li(t0, R + offset); a.put(sw(v0, 0, t0)); };
    auto mark = [&](Assembler& a, char c) {  //a byte at R + 0x104 on, their count at R + 0x100
      a.li(t0, R + 0x100); a.put(lw(t1, 0, t0)); a.put(addu(t2, t1, t0)); a.li(t3, u8(c)); a.put(sb(t3, 4, t2));
      a.put(addiu(t1, t1, 1)); a.put(sw(t1, 0, t0));
    };
    auto lock = [&](Assembler& a, u32 type, u32 outputs, u32 offset) {
      a.li(a0, type); a.li(a1, R + outputs); a.li(a2, R + outputs + 4);
      a.call("sceKernelVolatileMemLock");
      store(a, offset);
    };
    for(u32 n = 0; n < 3; n++) {  //waiter n: borrows it, marks its number, gives it back
      Assembler waiter{m, 0x0880'2000 + n * 0x100};
      lock(waiter, 0, 0x40 + n * 8, 0x20 + n * 4);
      mark(waiter, char('1' + n));
      waiter.li(a0, 0);
      waiter.call("sceKernelVolatileMemUnlock");
      waiter.call("sceKernelExitThread");
    }
    Assembler handler{m, 0x0880'3000};  //a vertical blank's
    handler.put(addiu(sp, sp, -16)); handler.put(sw(ra, 12, sp));
    lock(handler, 0, 0x90, 0x10);
    handler.put(lw(ra, 12, sp)); handler.put(addiu(sp, sp, 16));
    handler.li(v0, 0); handler.put(jr(ra)); handler.put(nop);
    for(u32 offset : {0x80u, 0x84u, 0x88u, 0x8cu, 0x90u, 0x94u}) m.system.memory.write(4, R + offset, 0x1337);
    Assembler main{m, 0x0880'1000};
    lock(main, 1, 0x80, 0x00);
    main.li(a0, 0); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelVolatileMemTryLock");
    for(u32 priority : {0x31u, 0x33u, 0x32u}) {  //each begins waiting as main delays
      u32 n = priority == 0x31 ? 0 : priority == 0x33 ? 1 : 2;
      main.li(a0, m.string("waiter")); main.li(a1, 0x0880'2000 + n * 0x100); main.li(a2, priority);
      main.li(a3, 0x1000); main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
      main.li(a0, 1000);
      main.call("sceKernelDelayThread");
    }
    main.li(a0, 5000);
    main.call("sceKernelDelayThread");
    mark(main, 'M');
    main.li(a0, 0);
    main.call("sceKernelVolatileMemUnlock");
    store(main, 0x04);
    main.li(a0, 10000);
    main.call("sceKernelDelayThread");
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s0, v0, zero));
    lock(main, 0, 0x88, 0x08);
    main.put(addu(a0, s0, zero));
    main.call("sceKernelCpuResumeIntr");
    main.li(a0, 0);
    main.call("sceKernelVolatileMemUnlock");
    store(main, 0x0c);
    main.li(a0, 30); main.li(a1, 0); main.li(a2, 0x0880'3000); main.li(a3, 0);
    main.call("sceKernelRegisterSubIntrHandler");
    main.li(a0, 30); main.li(a1, 0);
    main.call("sceKernelEnableSubIntr");
    main.call("sceDisplayWaitVblankStart");
    main.li(a0, 30); main.li(a1, 0);
    main.call("sceKernelReleaseSubIntrHandler");
    main.li(a0, 0);
    main.call("sceKernelVolatileMemUnlock");
    store(main, 0x14);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency * 4 / 1000);  //4 ms: the three wait
    u32 waiting = 0;
    for(auto& [uid, thread] : m.kernel.threads) if(thread->wait == Kernel::Wait::Volatile) waiting++;
    CHECK(waiting, 3);
    auto state = saveState(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    CHECK(loadState(n, state), true);
    CHECK(saveState(n) == state, true);
    for(auto* each : {&m, &n}) {
      each->kernel.run(Kernel::CPUFrequency / 10);
      auto word = [&](u32 offset) { return each->system.memory.read(4, R + offset); };
      CHECK(each->kernel.exited, true);
      CHECK(each->system.memory.readString(R + 0x104, 8) == "M123", true);
      CHECK(word(0x00), Kernel::ErrorInvalidMode);
      CHECK(word(0x80) == 0x1337 && word(0x84) == 0x1337, true);  //left alone
      for(u32 waiter = 0; waiter < 3; waiter++) {
        CHECK(word(0x20 + waiter * 4), 0);
        CHECK(word(0x40 + waiter * 8) == 0x0840'0000 && word(0x44 + waiter * 8) == 0x0040'0000, true);
      }
      CHECK(word(0x04), 0);
      CHECK(word(0x08), Kernel::ErrorCanNotWait);
      CHECK(word(0x88) == 0x0840'0000 && word(0x8c) == 0x0040'0000, true);
      CHECK(word(0x0c), Kernel::ErrorSemaphoreOverflow);  //not lent: the refused lock took nothing
      CHECK(word(0x10), Kernel::ErrorIllegalContext);
      CHECK(word(0x90) == 0x0840'0000 && word(0x94) == 0x0040'0000, true);
      CHECK(word(0x14), Kernel::ErrorSemaphoreOverflow);
    }
    if(m.system.memory.readString(R + 0x104, 8) != "M123") {
      std::printf("  [%s]\n", m.system.memory.readString(R + 0x104, 8).c_str());
    }
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//Delays as long as a PSP's, as pspautotests' threads/scheduling/delaylen recorded (rounded to 10 microseconds there):
//every delay from 1 to 209 microseconds about 230, longer ones about 25 more than asked (here 205 at least, plus 25),
//CB or not; a delay of 0 at once; and a worse thread runs during a delay of 1 (delayzero: "spinner runs almost
//always"). (Delays of 1 microsecond had made Brave Story's polling threads wake 230 times as often as on a PSP.) On
//both engines.
static auto delayLengths() -> void {
  static constexpr u32 Delays[] = {1, 100, 209, 210, 220, 300, 1000};
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler spinner{m, 0x0880'2000};  //counts as fast as it can
    u32 loop = spinner.here();
    spinner.li(t0, R + 0x90); spinner.put(lw(t1, 0, t0)); spinner.put(addiu(t1, t1, 1)); spinner.put(sw(t1, 0, t0));
    spinner.put(beq(zero, zero, int32_t(loop - (spinner.here() + 4)) / 4)); spinner.put(nop);
    Assembler main{m, 0x0880'1000};
    auto timed = [&](const char* function, u32 delay, u32 offset) {  //(t1 - t0 at offset)
      main.call("sceKernelGetSystemTimeLow");
      main.put(addu(s0, v0, zero));
      main.li(a0, delay);
      main.call(function);
      main.call("sceKernelGetSystemTimeLow");
      main.put(subu(v0, v0, s0));
      main.li(t0, R + offset); main.put(sw(v0, 0, t0));
    };
    for(u32 n = 0; n < 7; n++) {
      timed("sceKernelDelayThread", Delays[n], n * 4);
      timed("sceKernelDelayThreadCB", Delays[n], 0x40 + n * 4);
    }
    timed("sceKernelDelayThread", 0, 0x80);
    main.li(a0, m.string("spinner")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(s1, v0, zero));
    main.put(addu(a0, s1, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");
    main.li(a0, 1);
    main.call("sceKernelDelayThread");
    main.li(t0, R + 0x90); main.put(lw(t1, 0, t0)); main.li(t0, R + 0x94); main.put(sw(t1, 0, t0));
    main.put(addu(a0, s1, zero));
    main.call("sceKernelTerminateDeleteThread");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto word = [&](u32 offset) { return m.system.memory.read(4, R + offset); };
    for(u32 n = 0; n < 7; n++) {
      u32 expected = std::max(Delays[n], 205u) + 25;  //(the instructions around the calls add under a microsecond)
      check(__LINE__, "a delay's length", word(n * 4) == expected || word(n * 4) == expected + 1, true);
      check(__LINE__, "a CB delay's length", word(0x40 + n * 4) == expected || word(0x40 + n * 4) == expected + 1,
            true);
    }
    CHECK(word(0x80) <= 1, true);
    CHECK(word(0x94) > 0, true);  //the spinner ran meanwhile
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//Threads: another one's priority changed (one of higher priority than the caller's takes over at once), suspended
//(it doesn't run however ready, until resumed), ended by another (its waiter told so), its exit status read only
//once it has ended; the caller can't do these to itself.
static auto threadControl() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler worker{m, 0x0880'2000};  //prints, then sleeps until woken, prints again, returns 5
    worker.print("worker\n");
    worker.call("sceKernelSleepThread");
    worker.print("worker woke\n");
    worker.li(a0, 5);
    worker.call("sceKernelExitThread");
    Assembler victim{m, 0x0880'2800};  //sleeps for good
    victim.call("sceKernelSleepThread");

    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("worker")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(s0, v0, zero));
    main.put(addu(a0, s0, zero));
    main.call("sceKernelGetThreadExitStatus");  //dormant, never started: DORMANT (threads/threads/exitstatus)
    main.li(t0, R); main.put(sw(v0, 0x00, t0));
    main.put(addu(a0, s0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");  //lower priority: waits
    main.put(addu(a0, s0, zero));
    main.call("sceKernelGetThreadExitStatus");
    main.li(t0, R); main.put(sw(v0, 0x04, t0));
    main.print("main\n");
    main.put(addu(a0, s0, zero)); main.li(a1, 0x10);
    main.call("sceKernelChangeThreadPriority");  //above main's: it runs now, and sleeps
    main.print("main again\n");
    main.put(addu(a0, s0, zero));
    main.call("sceKernelSuspendThread");
    main.li(t0, R); main.put(sw(v0, 0x08, t0));
    main.put(addu(a0, s0, zero));
    main.call("sceKernelSuspendThread");
    main.li(t0, R); main.put(sw(v0, 0x0c, t0));
    main.put(addu(a0, s0, zero));
    main.call("sceKernelWakeupThread");  //ready, but suspended: it doesn't run
    main.li(a0, 1000);
    main.call("sceKernelDelayThread");
    main.print("main resumes it\n");
    main.put(addu(a0, s0, zero));
    main.call("sceKernelResumeThread");  //it runs now, and ends
    main.put(addu(a0, s0, zero));
    main.call("sceKernelGetThreadExitStatus");
    main.li(t0, R); main.put(sw(v0, 0x10, t0));
    main.li(a0, 0);
    main.call("sceKernelSuspendThread");  //the caller itself
    main.li(t0, R); main.put(sw(v0, 0x14, t0));
    //a thread that sleeps for good, terminated: dormant, its status the termination's; then deleted
    main.li(a0, m.string("victim")); main.li(a1, 0x0880'2800); main.li(a2, 0x10); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(s1, v0, zero));
    main.put(addu(a0, s1, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");
    main.put(addu(a0, s1, zero));
    main.call("sceKernelTerminateThread");
    main.li(t0, R); main.put(sw(v0, 0x18, t0));
    main.put(addu(a0, s1, zero));
    main.call("sceKernelGetThreadExitStatus");
    main.li(t0, R); main.put(sw(v0, 0x1c, t0));
    main.put(addu(a0, s1, zero));
    main.call("sceKernelTerminateDeleteThread");
    main.li(t0, R); main.put(sw(v0, 0x20, t0));
    main.put(addu(a0, s1, zero));
    main.call("sceKernelGetThreadExitStatus");
    main.li(t0, R); main.put(sw(v0, 0x24, t0));
    main.li(a0, 0);
    main.call("sceKernelTerminateDeleteThread");
    main.li(t0, R); main.put(sw(v0, 0x28, t0));
    main.li(a0, 0); main.li(a1, 0x07);
    main.call("sceKernelChangeThreadPriority");
    main.li(t0, R); main.put(sw(v0, 0x2c, t0));
    main.call("sceKernelExitGame");

    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.output == "main\nworker\nmain again\nmain resumes it\nworker woke\n", true);
    if(m.output != "main\nworker\nmain again\nmain resumes it\nworker woke\n") {
      std::printf("  output: [%s]\n", m.output.c_str());
    }
    CHECK(m.system.memory.read(4, R + 0x00), Kernel::ErrorDormant);
    CHECK(m.system.memory.read(4, R + 0x04), Kernel::ErrorNotDormant);
    CHECK(m.system.memory.read(4, R + 0x08), 0);
    CHECK(m.system.memory.read(4, R + 0x0c), Kernel::ErrorSuspended);
    CHECK(m.system.memory.read(4, R + 0x10), 5);
    CHECK(m.system.memory.read(4, R + 0x14), Kernel::ErrorIllegalThread);
    CHECK(m.system.memory.read(4, R + 0x18), 0);
    CHECK(m.system.memory.read(4, R + 0x1c), Kernel::ErrorThreadTerminated);
    CHECK(m.system.memory.read(4, R + 0x20), 0);
    CHECK(m.system.memory.read(4, R + 0x24), Kernel::ErrorUnknownThread);
    CHECK(m.system.memory.read(4, R + 0x28), Kernel::ErrorIllegalThread);
    CHECK(m.system.memory.read(4, R + 0x2c), Kernel::ErrorIllegalPriority);
  }
}

//A new thread's stack: filled with 0xff past its ID at the bottom (all of it free but those 16 bytes, until it
//runs), unless its attributes say not to, and read where it is: a stack of 4 GiB, as a state could once load, is
//answered at once (it was copied out whole first), with nothing past RAM to count. A semaphore's status; the
//attribute only the VFPU's may change.
static auto threadStatus() -> void {
  KernelMachine m;
  s32 filled = m.kernel.createThread("filled", 0x0880'1000, 0x20, 0x1000, 0, 0);
  s32 plain = m.kernel.createThread("plain", 0x0880'1000, 0x20, 0x1000, 0x0010'0000, 0);
  auto& thread = *m.kernel.threads[filled];
  CHECK(m.system.memory.read(4, thread.stackBlock), u32(filled));
  CHECK(m.system.memory.read(1, thread.stackBlock + 0xfff), 0xff);
  CHECK(m.call("sceKernelGetThreadStackFreeSize", {u32(filled)}), 0x1000 - 0x10);
  CHECK(m.call("sceKernelGetThreadStackFreeSize", {u32(plain)}), 0);
  m.system.memory.write(4, thread.stackBlock + 0x800, 0);  //touched half way up
  CHECK(m.call("sceKernelGetThreadStackFreeSize", {u32(filled)}), 0x800 - 0x10);
  CHECK(m.call("sceKernelGetThreadStackFreeSize", {0x7777}), Kernel::ErrorUnknownThread);
  thread.stackSize = 0xffff'f000;
  auto start = std::chrono::steady_clock::now();
  CHECK(m.call("sceKernelGetThreadStackFreeSize", {u32(filled)}), 0);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(100), true);
  thread.stackSize = 0x1000;
  u32 semaphore = m.call("sceKernelCreateSema", {m.string("status"), 0x100, 2, 9, 0});
  m.call("sceKernelPollSema", {semaphore, 1});
  m.system.memory.write(4, R, 52);
  CHECK(m.call("sceKernelReferSemaStatus", {semaphore, R}), 0);
  CHECK(m.system.memory.readString(R + 4, 32) == "status", true);
  CHECK(m.system.memory.read(4, R + 36), 0x100);
  CHECK(m.system.memory.read(4, R + 40), 2);  //initial
  CHECK(m.system.memory.read(4, R + 44), 1);  //current
  CHECK(m.system.memory.read(4, R + 48), 9);
  CHECK(m.system.memory.read(4, R + 52), 0);  //none waiting (its size, 56, is past the 52 given: not written)
  CHECK(m.call("sceKernelReferSemaStatus", {0x7777, R}), Kernel::ErrorUnknownSemaphore);
  m.kernel.current = &thread;
  CHECK(m.call("sceKernelChangeCurrentThreadAttr", {0, 0x4000}), 0);
  CHECK(thread.attributes, 0x4000);
  CHECK(m.call("sceKernelChangeCurrentThreadAttr", {0x4000, 0}), 0);
  CHECK(thread.attributes, 0);
  CHECK(m.call("sceKernelChangeCurrentThreadAttr", {0, 0x8000'0000}), Kernel::ErrorIllegalAttribute);
}

//Priorities, as pspautotests' threads/threads/change found them on a PSP: 0x08 to 0x77 for a user thread (7, 0x78
//and -1 refused), 0 for the caller's own; a thread not started can't be changed. A thread put level with the caller
//doesn't take over, but the caller changing its own priority, even to what it was, gives way to it. Started again,
//a thread is back at the priority it was made with.
static auto threadPriorities() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler worker{m, 0x0880'2000};
    worker.print("worker\n");
    worker.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    main.li(s1, R);
    auto change = [&](u32 priority, u32 offset) {  //the worker's (its ID in s0)
      main.put(addu(a0, s0, zero)); main.li(a1, priority);
      main.call("sceKernelChangeThreadPriority");
      main.put(sw(v0, offset, s1));
    };
    auto refer = [&](u32 info) {
      main.li(t0, info); main.li(t1, 104); main.put(sw(t1, 0, t0));
      main.put(addu(a0, s0, zero)); main.li(a1, info);
      main.call("sceKernelReferThreadStatus");
    };
    auto start = [&] {
      main.put(addu(a0, s0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
    };
    main.li(a0, m.string("worker")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(s0, v0, zero));
    change(0x20, 0x00);  //not started
    start();             //below main: it waits
    change(0x07, 0x04);
    change(0x78, 0x08);
    change(0xffff'ffff, 0x0c);
    change(0, 0x10);     //main's: level with main, it waits still
    refer(R + 0x100);
    main.print("main\n");
    main.li(a0, 0); main.li(a1, 0x20);
    main.call("sceKernelChangeThreadPriority");  //main's own, to what it was: the worker runs
    main.put(sw(v0, 0x14, s1));
    main.print("main again\n");
    start();             //back at 0x30: it waits
    refer(R + 0x200);
    change(0x08, 0x18);  //above main: it runs at once
    main.print("main last\n");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.output == "main\nworker\nmain again\nworker\nmain last\n", true);
    if(m.output != "main\nworker\nmain again\nworker\nmain last\n") std::printf("  output: [%s]\n", m.output.c_str());
    CHECK(m.system.memory.read(4, R + 0x00), Kernel::ErrorDormant);
    for(u32 offset : {0x04u, 0x08u, 0x0cu}) CHECK(m.system.memory.read(4, R + offset), Kernel::ErrorIllegalPriority);
    for(u32 offset : {0x10u, 0x14u, 0x18u}) CHECK(m.system.memory.read(4, R + offset), 0);
    CHECK(m.system.memory.read(4, R + 0x100 + 64), 0x20);  //its current priority: main's
    CHECK(m.system.memory.read(4, R + 0x200 + 60), 0x30);  //its first
    CHECK(m.system.memory.read(4, R + 0x200 + 64), 0x30);  //and its current, once started again
    CHECK(m.notes.size(), 0);
  }
}

//How much of a stack has never been used, as pspautotests' threads/threads/stackfree measured it on a PSP: threads
//with 4 KiB stacks, each making its frame as the PSP's compiler made them there (the return address at its foot,
//just above the locals) and asking about itself. A 16-byte frame leaves 0xea0 free; a 1 KiB array of zeros below
//the return address, 0xaa0; the same array of 0xff bytes, 0xea0 again (they look like the stack's fill).
static auto stackFree() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    auto thread = [&](u32 entry, u32 locals, u32 fill, u32 result) {
      Assembler a{m, entry};
      a.put(addiu(sp, sp, -s32(locals + 16)));
      a.put(sw(ra, locals, sp));
      if(locals) {  //the locals filled, a word at a time
        a.li(t2, fill * 0x0101'0101);
        a.put(addu(t0, sp, zero));
        a.put(addiu(t1, sp, locals));
        u32 loop = a.here();
        a.put(sw(t2, 0, t0));
        a.put(addiu(t0, t0, 4));
        a.put(bne(t0, t1, int32_t(loop - (a.here() + 4)) / 4));
        a.put(nop);
      }
      a.li(a0, 0);
      a.call("sceKernelGetThreadStackFreeSize");
      a.li(t0, result); a.put(sw(v0, 0, t0));
      a.call("sceKernelCheckThreadStack");
      a.li(t0, result); a.put(sw(v0, 0x10, t0));
      a.call("sceKernelExitThread");
    };
    thread(0x0880'2000, 0, 0, R);
    thread(0x0880'2400, 0x400, 0x00, R + 4);
    thread(0x0880'2800, 0x400, 0xff, R + 8);
    Assembler main{m, 0x0880'1000};
    for(u32 entry : {0x0880'2000u, 0x0880'2400u, 0x0880'2800u}) {
      main.li(a0, m.string("stack")); main.li(a1, entry); main.li(a2, 0x10); main.li(a3, 0x1000);
      main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");  //above main: it runs now
    }
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.system.memory.read(4, R), 0xea0);
    CHECK(m.system.memory.read(4, R + 4), 0xaa0);
    CHECK(m.system.memory.read(4, R + 8), 0xea0);
    //the room left below the stack pointer: 0xeb0 with a frame of 0x10, 0xab0 with 0x400 more, as on a PSP
    CHECK(m.system.memory.read(4, R + 0x10), 0xeb0);
    CHECK(m.system.memory.read(4, R + 0x14), 0xab0);
    CHECK(m.system.memory.read(4, R + 0x18), 0xab0);
    CHECK(m.notes.size(), 0);
  }
  KernelMachine m;
  CHECK(m.call("sceKernelCheckThreadStack", {}), 0);  //no thread calling
}

//sceHprm with nothing in the headphone socket: no headphones, remote or microphone, no key held, an empty latch, a
//buffer the answer can't go in refused; and sceRtcGetAccumulativeTime (by both its names) the PSP's time running,
//in microseconds, 64 bits in v0 and v1.
static auto remoteAndRunningTime() -> void {
  KernelMachine m;
  CHECK(m.call("sceHprmIsHeadphoneExist", {}), 0);
  CHECK(m.call("sceHprmIsRemoteExist", {}), 0);
  CHECK(m.call("sceHprmIsMicrophoneExist", {}), 0);
  m.system.memory.fill(R, 0xcc, 32);
  CHECK(m.call("sceHprmPeekCurrentKey", {R}), 0);
  CHECK(m.system.memory.read(4, R), 0);
  CHECK(m.system.memory.read(4, R + 4), 0xcccc'cccc);
  for(const char* latch : {"sceHprmPeekLatch", "sceHprmReadLatch"}) {
    m.system.memory.fill(R, 0xcc, 32);
    CHECK(m.call(latch, {R}), 0);
    for(u32 n = 0; n < 4; n++) CHECK(m.system.memory.read(4, R + n * 4), 0);
    CHECK(m.system.memory.read(4, R + 16), 0xcccc'cccc);
    CHECK(m.call(latch, {0}), Kernel::ErrorIllegalAddress);
  }
  CHECK(m.call("sceHprmPeekCurrentKey", {0}), Kernel::ErrorIllegalAddress);
  //callbacks for the remote's changes (never told: nothing is plugged in): -1 takes the first free slot and gives it
  //back, a slot given takes that one and gives 0; 16 slots; a used one ALREADY, none free OUT_OF_MEMORY, past them
  //INVALID_INDEX, no callback INVALID_ID; unregistering ("Unregitser", as Sony spelt it) an empty one NOT_FOUND
  CHECK(m.call("sceHprmRegisterCallback", {u32(-1), 0x123}), 0);
  CHECK(m.call("sceHprmRegisterCallback", {u32(-1), 0x124}), 1);
  CHECK(m.call("sceHprmRegisterCallback", {5, 0x125}), 0);
  CHECK(m.call("sceHprmRegisterCallback", {5, 0x126}), Kernel::ErrorAlready);
  CHECK(m.call("sceHprmRegisterCallback", {16, 0x126}), Kernel::ErrorInvalidIndex);
  CHECK(m.call("sceHprmRegisterCallback", {u32(-2), 0x126}), Kernel::ErrorInvalidIndex);
  CHECK(m.call("sceHprmRegisterCallback", {u32(-1), 0}), Kernel::ErrorInvalidID);
  CHECK(roundTrip(m), true);
  for(u32 slot = 2; slot < 16; slot++) if(slot != 5) CHECK(m.call("sceHprmRegisterCallback", {u32(-1), 0x200}), slot);
  CHECK(m.call("sceHprmRegisterCallback", {u32(-1), 0x200}), Kernel::ErrorOutOfMemory);
  CHECK(m.call("sceHprmUnregitserCallback", {5}), 0);
  CHECK(m.call("sceHprmUnregitserCallback", {5}), Kernel::ErrorNotFound);
  CHECK(m.call("sceHprmUnregitserCallback", {16}), Kernel::ErrorInvalidIndex);
  CHECK(m.call("sceHprmRegisterCallback", {u32(-1), 0x200}), 5);
  CHECK(m.kernel.hprmCallbacks[0] == 0x123 && m.kernel.hprmCallbacks[1] == 0x124, true);
  m.kernel.cycles = u64(Kernel::CPUFrequency) * 5'000;  //5,000 seconds: past 32 bits of microseconds
  for(const char* name : {"sceRtcGetAccumulativeTime", "sceRtcGetAccumlativeTime"}) {
    CHECK(m.call(name, {}), u32(5'000'000'000ull));
    CHECK(m.system.ipu.r[3], u32(5'000'000'000ull >> 32));
  }
}

//Clocks and dates: a date as a tick (microseconds since year 1), leap days, dates that can't be; ticks compared;
//a 64-bit count of microseconds split into seconds and microseconds, from memory and from registers.
static auto clocks() -> void {
  KernelMachine m;
  auto date = [&](u32 year, u32 month, u32 day, u32 hour, u32 minute, u32 second, u32 microsecond) {
    u32 values[] = {year, month, day, hour, minute, second};
    for(u32 n = 0; n < 6; n++) m.system.memory.write(2, R + n * 2, values[n]);
    m.system.memory.write(4, R + 12, microsecond);
    return m.call("sceRtcGetTick", {R, R + 0x20});
  };
  auto tick = [&] { return m.system.memory.read(4, R + 0x20) | u64(m.system.memory.read(4, R + 0x24)) << 32; };
  CHECK(date(1970, 1, 1, 0, 0, 0, 0), 0);
  CHECK(tick() == 62'135'596'800'000'000ull, true);
  CHECK(date(2004, 12, 12, 12, 34, 56, 789), 0);
  CHECK(tick() == 63'238'451'696'000'789ull, true);
  CHECK(date(2000, 2, 29, 23, 59, 59, 999'999), 0);
  CHECK(tick() == 63'087'465'599'999'999ull, true);
  CHECK(date(1900, 2, 29, 0, 0, 0, 0), Kernel::ErrorInvalidValue);  //1900 had no leap day
  CHECK(date(2004, 13, 1, 0, 0, 0, 0), Kernel::ErrorInvalidValue);
  CHECK(date(2004, 4, 31, 0, 0, 0, 0), Kernel::ErrorInvalidValue);
  CHECK(date(2004, 4, 30, 24, 0, 0, 0), Kernel::ErrorInvalidValue);
  CHECK(date(9999, 12, 31, 23, 59, 59, 99'999'998), 0);  //microseconds past a second added (rtc/arithmetic)
  CHECK(tick() == 315'537'897'698'999'998ull, true);
  CHECK(date(10000, 1, 1, 0, 0, 0, 0), Kernel::ErrorInvalidValue);  //past the year 9999 (rtc/convert)
  CHECK(tick() == 315'537'897'698'999'998ull, true);
  m.system.memory.write(4, R + 0x40, 5); m.system.memory.write(4, R + 0x44, 1);
  m.system.memory.write(4, R + 0x48, 6); m.system.memory.write(4, R + 0x4c, 0);
  CHECK(m.call("sceRtcCompareTick", {R + 0x40, R + 0x48}), 1);  //the high words decide
  CHECK(m.call("sceRtcCompareTick", {R + 0x48, R + 0x40}), u32(-1));
  CHECK(m.call("sceRtcCompareTick", {R + 0x40, R + 0x40}), 0);
  u64 clock = 4'321'987'654'321ull;
  m.system.memory.write(4, R + 0x50, u32(clock)); m.system.memory.write(4, R + 0x54, u32(clock >> 32));
  CHECK(m.call("sceKernelSysClock2USec", {R + 0x50, R + 0x58, R + 0x5c}), 0);
  CHECK(m.system.memory.read(4, R + 0x58), 4'321'987);
  CHECK(m.system.memory.read(4, R + 0x5c), 654'321);
  CHECK(m.call("sceKernelSysClock2USecWide", {u32(clock), u32(clock >> 32), R + 0x60, R + 0x64}), 0);
  CHECK(m.system.memory.read(4, R + 0x60), 4'321'987);
  CHECK(m.system.memory.read(4, R + 0x64), 654'321);
  //misc/timeconv's: -1 split, and 0x1337 with either place alone (the other left as it was)
  auto split = [&](u64 count, bool seconds, bool microseconds) {
    for(u32 at : {0x70u, 0x74u}) m.system.memory.write(4, R + at, 0xcccc'cccc);
    m.system.memory.write(4, R + 0x50, u32(count)); m.system.memory.write(4, R + 0x54, u32(count >> 32));
    u32 a = seconds ? R + 0x70 : 0, b = microseconds ? R + 0x74 : 0;
    u32 narrow = m.call("sceKernelSysClock2USec", {R + 0x50, a, b});
    u64 first = m.system.memory.read(4, R + 0x70) | u64(m.system.memory.read(4, R + 0x74)) << 32;
    for(u32 at : {0x70u, 0x74u}) m.system.memory.write(4, R + at, 0xcccc'cccc);
    u32 wide = m.call("sceKernelSysClock2USecWide", {u32(count), u32(count >> 32), a, b});
    u64 second = m.system.memory.read(4, R + 0x70) | u64(m.system.memory.read(4, R + 0x74)) << 32;
    CHECK(narrow == 0 && wide == 0 && first == second, true);
    return second;
  };
  CHECK(split(~0ull, true, true), 0x0008'6abf'f7a0'b5edull);
  CHECK(split(0x1337, false, true), 0x0000'1337'cccc'ccccull);
  CHECK(split(0x1337, true, false), 0xcccc'cccc'0000'0000ull);
  //with nowhere for the seconds, the whole count (its low 32 bits): Tekken: Dark Resurrection's clock rate
  CHECK(split(1'000'000, false, true), 0x000f'4240'cccc'ccccull);
  CHECK(split(clock, false, true), u64(u32(clock)) << 32 | 0xcccc'cccc);
  m.kernel.cycles = 333 * 1234;
  CHECK(m.call("sceKernelLibcClock", {}), 1234);
}

//The Mersenne Twister in the program's memory, the kernel's and sceMt19937's alike: seeded with 5489 (MT19937's own
//default), its first number and its 10000th are the reference generator's (3499211612, and 4123659995 as C++'s
//std::mt19937 is required to give). The context as hash/mt19937ctx recorded: a count of 0, then the 624 words
//already stirred once seeded; one draw makes the count 1 and changes the words; its seeds' first eight numbers; two
//contexts at once; a context copied goes on alike. And a context of the older layout (a count of 624 and its words
//only seeded, as a state saved before kept one) goes on with the same numbers.
static auto mersenneTwister() -> void {
  for(auto [init, draw] : {std::pair{"sceKernelUtilsMt19937Init", "sceKernelUtilsMt19937UInt"},
                           std::pair{"sceMt19937Init", "sceMt19937UInt"}}) {
    KernelMachine m;
    constexpr u32 Context = R, Other = R + 0x1000, Copy = R + 0x2000;
    CHECK(m.call(init, {Context, 5489}), 0);
    CHECK(m.call(draw, {Context}), 3'499'211'612u);
    u32 value = 0;
    for(u32 n = 2; n <= 10000; n++) value = m.call(draw, {Context});
    CHECK(value, 4'123'659'995u);
    std::mt19937 reference(0x1234'5678);
    std::vector<u32> seeded(624), stirred(624);
    seeded[0] = 0x1234'5678;
    for(u32 n = 1; n < 624; n++) seeded[n] = 1'812'433'253u * (seeded[n - 1] ^ seeded[n - 1] >> 30) + n;
    stirred = seeded;
    for(u32 n = 0; n < 624; n++) {
      u32 y = (stirred[n] & 0x8000'0000) | (stirred[(n + 1) % 624] & 0x7fff'ffff);
      stirred[n] = stirred[(n + 397) % 624] ^ y >> 1 ^ (y & 1 ? 0x9908'b0df : 0);
    }
    m.system.memory.fill(Context, 0xcc, 0xa00);
    CHECK(m.call(init, {Context, 0x1234'5678}), 0);
    CHECK(m.system.memory.read(4, Context), 0);
    bool same = true;
    for(u32 n = 0; n < 624; n++) same &= m.system.memory.read(4, Context + 4 + n * 4) == stirred[n];
    CHECK(same, true);
    CHECK(m.system.memory.read(4, Context + 4 + 624 * 4), 0xcccc'cccc);  //2500 bytes
    CHECK(m.call(draw, {Context}), 0xc697'9343);
    CHECK(m.system.memory.read(4, Context), 1);
    CHECK(m.system.memory.read(4, Context + 4) == stirred[0], false);
    CHECK(m.call(init, {Other, 0xdead'beef}), 0);
    const u32 first[] = {0x0962'd2fa, 0xa73a'24a4, 0xe118'a180, 0xb547'5abb, 0x6461'3c7c, 0x6f32'f4db, 0xf27b'f199};
    const u32 other[] = {0x3903'7a7d, 0xe505'2ed8, 0xc5dc'5c6e, 0x6ddc'cbe1, 0xa13a'ed6c, 0x2383'9b39, 0x37f0'a862};
    reference.discard(1);
    for(u32 n = 0; n < 7; n++) {
      CHECK(m.call(draw, {Context}), first[n]);
      CHECK(m.call(draw, {Other}), other[n]);
      CHECK(first[n], reference());
    }
    std::vector<u8> context(2500);
    m.system.memory.copyOut(context.data(), Context, 2500);
    m.system.memory.copyIn(Copy, context.data(), 2500);
    for(u32 n = 0; n < 700; n++) CHECK(m.call(draw, {Copy}), m.call(draw, {Context}));
    //the older layout
    m.system.memory.write(4, Copy, 624);
    for(u32 n = 0; n < 624; n++) m.system.memory.write(4, Copy + 4 + n * 4, seeded[n]);
    CHECK(m.call(init, {Context, 0x1234'5678}), 0);
    for(u32 n = 0; n < 1300; n++) CHECK(m.call(draw, {Copy}), m.call(draw, {Context}));
    //the older layout part way through a round (all its words this round's): the same numbers as this layout's from
    //there, and in the end the same words
    for(u32 count : {1u, 2u, 226u, 227u, 228u, 300u, 396u, 397u, 398u, 623u}) {
      m.system.memory.write(4, Copy, count);
      for(u32 n = 0; n < 624; n++) m.system.memory.write(4, Copy + 4 + n * 4, stirred[n]);
      CHECK(m.call(init, {Context, 0x1234'5678}), 0);
      for(u32 n = 0; n < count; n++) m.call(draw, {Context});
      bool same = true;
      for(u32 n = 0; n < 1300; n++) same &= m.call(draw, {Copy}) == m.call(draw, {Context});
      for(u32 n = 0; n <= 624; n++) {
        same &= m.system.memory.read(4, Copy + n * 4) == m.system.memory.read(4, Context + n * 4);
      }
      CHECK(same, true);
    }
  }
}

//The kernel's printf, to the program's output: with widths far past a field's room (a number's 63 characters, 63
//past a string's text) too, each field cut to its room at once (snprintf padded each to its full 2e9 first: these
//ten took minutes, built with the sanitizers), and a run of 20 digits taken as one more such width. A DMA copy
//(nothing, or memory that isn't there, refused); the wireless LAN's switch off and its address made up.
static auto oddsAndEnds() -> void {
  KernelMachine m;
  u32 format = m.string("%s=%d %5x|%-3u|%03X%c %p 100%%\n");
  m.call("sceKernelPrintf", {format, m.string("n"), u32(-7), 0xab, 4, 0x1f, 'z', 0x0880'0000});
  CHECK(m.output == "n=-7    ab|4  |01Fz 8800000 100%\n", true);
  if(m.output != "n=-7    ab|4  |01Fz 8800000 100%\n") std::printf("  printf: [%s]\n", m.output.c_str());
  std::string wide, expected;
  for(u32 n = 1; n <= 8; n++) {  //seven arguments, then 0
    wide += "%2000000000d|";
    expected += std::string(62, ' ') + char('0' + n % 8) + "|";
  }
  wide += "%-2000000000s|%2000000000x";  //a string at 0, which is nothing; 0
  expected += std::string(63, ' ') + "|" + std::string(62, ' ') + "0";
  m.output.clear();
  auto start = std::chrono::steady_clock::now();
  m.call("sceKernelPrintf", {m.string(wide), 1, 2, 3, 4, 5, 6, 7});
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(250), true);
  CHECK(m.output == expected, true);
  if(m.output == expected) {  //(not before: snprintf, given that width, would leave the field unwritten)
    m.output.clear();
    m.call("sceKernelPrintf", {m.string("%-99999999999999999999d|"), 5});
    CHECK(m.output == "5" + std::string(62, ' ') + "|", true);
  }
  m.system.memory.copyIn(R, "abcdefgh", 8);
  CHECK(m.call("sceDmacMemcpy", {R + 0x100, R, 8}), 0);
  CHECK(m.system.memory.readString(R + 0x100, 8) == "abcdefgh", true);
  CHECK(m.call("sceDmacMemcpy", {R + 0x100, R, 0}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceDmacMemcpy", {0x0010'0000, R, 8}), Kernel::ErrorInvalidPointer);
  CHECK(m.call("sceWlanGetSwitchState", {}), 0);
  CHECK(m.call("sceWlanGetEtherAddr", {R + 0x200}), 0);
  CHECK(m.system.memory.read(1, R + 0x200), 0x02);  //a locally administered address
  CHECK(m.call("sceUsbStart", {m.string("USBBusDriver"), 0, 0}), 0);
  CHECK(m.call("sceUsbActivate", {0x1c8}), 0);
  CHECK(m.call("sceUsbDeactivate", {0x1c8}), 0);
  CHECK(m.call("sceUsbStop", {m.string("USBBusDriver"), 0, 0}), 0);
  //the system's status: as much as its size asks for (28 bytes at most), its status and counts 0
  auto word = [&](u32 address) { return m.system.memory.read(4, address); };
  m.system.memory.fill(R + 0x300, 0xcc, 32);
  m.system.memory.write(4, R + 0x300, 28);
  CHECK(m.call("sceKernelReferSystemStatus", {R + 0x300}), 0);
  CHECK(word(R + 0x300) == 28 && word(R + 0x304) == 0 && word(R + 0x318) == 0, true);
  CHECK(word(R + 0x31c), 0xcccc'cccc);
  m.system.memory.fill(R + 0x300, 0xcc, 32);
  m.system.memory.write(4, R + 0x300, 8);
  CHECK(m.call("sceKernelReferSystemStatus", {R + 0x300}), 0);
  CHECK(word(R + 0x300) == 28 && word(R + 0x304) == 0 && word(R + 0x308) == 0xcccc'cccc, true);
}

//The fastest PLL the model allows with the wireless LAN on (scePowerCheckWlanCoexistenceClock, known by its NID,
//0xa85880d0): 1, a PSP-2000's 333 MHz.
static auto wlanClock() -> void {
  KernelMachine m;
  m.kernel.syscall(m.kernel.importCode("scePower", 0xa858'80d0));
  CHECK(m.system.ipu.r[2], 1);
  CHECK(m.notes.size(), 0);
}

//Ticks moved on (rtc.cpp) as rtc/arithmetic recorded, its lines' sources and destinations as ticks: ticks,
//microseconds, seconds and minutes by 64-bit amounts, hours, days and weeks by 32-bit ones, wrapping round either
//way; months and years in the calendar, a month's last day kept to the new month, a date outside the years 1 to 9999
//leaving the destination as it was; each returning 0.
static auto tickArithmetic() -> void {
  KernelMachine m;
  constexpr u32 Source = R + 0x100, Destination = R + 0x108;
  auto tickOf = [&](u32 year, u32 month, u32 day, u32 hour, u32 minute, u32 second, u32 microsecond) {
    u32 values[] = {year, month, day, hour, minute, second};
    for(u32 n = 0; n < 6; n++) m.system.memory.write(2, R + n * 2, values[n]);
    m.system.memory.write(4, R + 12, microsecond);
    m.call("sceRtcGetTick", {R, Source});
    return m.system.memory.read(4, Source) | u64(m.system.memory.read(4, Source + 4)) << 32;
  };
  auto add = [&](const char* function, u64 source, u64 amount) {
    m.system.memory.write(4, Source, u32(source)); m.system.memory.write(4, Source + 4, u32(source >> 32));
    m.system.memory.write(4, Destination, 0x1337); m.system.memory.write(4, Destination + 4, 0);
    CHECK(m.call(function, {Destination, Source, u32(amount), u32(amount >> 32)}), 0);
    return m.system.memory.read(4, Destination) | u64(m.system.memory.read(4, Destination + 4)) << 32;
  };
  u64 epoch = tickOf(1970, 1, 1, 0, 0, 0, 445), big = 62'135'596'800'000'445ull;
  CHECK(add("sceRtcTickAddTicks", big, -big), 0);
  CHECK(add("sceRtcTickAddTicks", big, big) == 124'271'193'600'000'890ull, true);
  CHECK(add("sceRtcTickAddTicks", big, 621'355'968'000ull) == 62'136'218'155'968'445ull, true);
  CHECK(add("sceRtcTickAddMicroseconds", epoch, u64(-2000)) == tickOf(1969, 12, 31, 23, 59, 59, 998'445), true);
  CHECK(add("sceRtcTickAddMicroseconds", tickOf(1, 1, 1, 0, 0, 0, 10), u64(-11)) == ~0ull, true);
  CHECK(add("sceRtcTickAddSeconds", epoch, u64(-2000)) == tickOf(1969, 12, 31, 23, 26, 40, 445), true);
  CHECK(add("sceRtcTickAddSeconds", epoch, -big) == epoch - big * 1'000'000, true);  //wrapping round
  CHECK(add("sceRtcTickAddSeconds", tickOf(9999, 12, 31, 23, 59, 50, 0), 10)
        == tickOf(9999, 12, 31, 23, 59, 50, 0) + 10'000'000, true);
  CHECK(add("sceRtcTickAddMinutes", epoch, 2000) == tickOf(1970, 1, 2, 9, 20, 0, 445), true);
  CHECK(add("sceRtcTickAddMinutes", epoch, big) == epoch + big * 60'000'000, true);
  CHECK(add("sceRtcTickAddHours", epoch, u64(-2000)) == tickOf(1969, 10, 9, 16, 0, 0, 445), true);
  //(a 32-bit amount: the test's 62135596800000445 hours reached the PSP as its low 32 bits)
  CHECK(add("sceRtcTickAddHours", epoch, big) == epoch + u64(s64(s32(u32(big)))) * 3'600'000'000ull, true);
  CHECK(add("sceRtcTickAddHours", epoch, big) == tickOf(383, 3, 16, 13, 0, 0, 445), true);
  CHECK(add("sceRtcTickAddDays", epoch, 2000) == tickOf(1975, 6, 24, 0, 0, 0, 445), true);
  CHECK(add("sceRtcTickAddDays", tickOf(1, 1, 10, 0, 0, 0, 0), u64(-10)) == u64(-86'400'000'000ll), true);
  CHECK(add("sceRtcTickAddWeeks", epoch, u64(-2000)) == tickOf(1931, 9, 3, 0, 0, 0, 445), true);
  CHECK(add("sceRtcTickAddWeeks", epoch, -big) == epoch + u64(s64(s32(u32(-big)))) * 604'800'000'000ull, true);
  CHECK(add("sceRtcTickAddMonths", epoch, u64(-2000)) == tickOf(1803, 5, 1, 0, 0, 0, 445), true);
  CHECK(add("sceRtcTickAddMonths", epoch, 14) == tickOf(1971, 3, 1, 0, 0, 0, 445), true);
  CHECK(add("sceRtcTickAddMonths", tickOf(1970, 1, 31, 1, 2, 3, 4), 1) == tickOf(1970, 2, 28, 1, 2, 3, 4), true);
  CHECK(add("sceRtcTickAddMonths", epoch, big), 0x1337);
  CHECK(add("sceRtcTickAddMonths", tickOf(1, 2, 1, 0, 0, 0, 0), u64(-1)) == tickOf(1, 1, 1, 0, 0, 0, 0), true);
  CHECK(add("sceRtcTickAddMonths", tickOf(1, 2, 1, 0, 0, 0, 0), u64(-2)), 0x1337);
  CHECK(add("sceRtcTickAddMonths", tickOf(9999, 11, 1, 0, 0, 0, 0), 2), 0x1337);
  CHECK(add("sceRtcTickAddYears", tickOf(2012, 2, 29, 0, 0, 0, 445), 1) == tickOf(2013, 2, 28, 0, 0, 0, 445), true);
  CHECK(add("sceRtcTickAddYears", tickOf(2012, 2, 29, 0, 0, 0, 445), 4) == tickOf(2016, 2, 29, 0, 0, 0, 445), true);
  CHECK(add("sceRtcTickAddYears", epoch, u64(-1969)) == tickOf(1, 1, 1, 0, 0, 0, 445), true);
  CHECK(add("sceRtcTickAddYears", epoch, u64(-2000)), 0x1337);
  CHECK(add("sceRtcTickAddYears", tickOf(9998, 1, 1, 0, 0, 0, 0), 2), 0x1337);
  CHECK(m.call("sceRtcTickAddSeconds", {0, Source, 1, 0}), Kernel::ErrorInvalidPointer);
  CHECK(m.notes.size(), 0);
}

auto powerTests() -> Tests {
  return {
    {"power callbacks", powerCallbacks}, {"power clocks", powerClocks}, {"power volatile memory", volatileMemory},
    {"power volatile memory waited for", volatileWaits},
    {"power volatile memory locked at once", volatileLockedAtOnce}, {"kernel thread delays' lengths", delayLengths},
    {"kernel thread control", threadControl}, {"kernel thread status", threadStatus}, {"kernel clocks", clocks},
    {"kernel mersenne twister", mersenneTwister}, {"kernel odds and ends", oddsAndEnds},
    {"kernel thread priorities", threadPriorities}, {"kernel thread stack free", stackFree},
    {"remote and running time", remoteAndRunningTime},
    {"power the clock beside the wireless LAN", wlanClock},
    {"kernel ticks moved on", tickArithmetic},
  };
}

}
