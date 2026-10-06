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
    main.call("sceKernelGetThreadExitStatus");  //dormant, but never ran: its status is 0
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
    CHECK(m.system.memory.read(4, R + 0x00), 0);
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
    CHECK(m.notes.size(), 0);
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
  m.kernel.cycles = 333 * 1234;
  CHECK(m.call("sceKernelLibcClock", {}), 1234);
}

//The Mersenne Twister in the program's memory: seeded with 5489 (MT19937's own default), its first number and its
//10000th are the reference generator's (3499211612, and 4123659995 as C++'s std::mt19937 is required to give).
static auto mersenneTwister() -> void {
  KernelMachine m;
  constexpr u32 Context = R;
  CHECK(m.call("sceKernelUtilsMt19937Init", {Context, 5489}), 0);
  CHECK(m.call("sceKernelUtilsMt19937UInt", {Context}), 3'499'211'612u);
  u32 value = 0;
  for(u32 n = 2; n <= 10000; n++) value = m.call("sceKernelUtilsMt19937UInt", {Context});
  CHECK(value, 4'123'659'995u);
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
}

auto powerTests() -> Tests {
  return {
    {"power callbacks", powerCallbacks}, {"power clocks", powerClocks}, {"power volatile memory", volatileMemory},
    {"kernel thread control", threadControl}, {"kernel thread status", threadStatus}, {"kernel clocks", clocks},
    {"kernel mersenne twister", mersenneTwister}, {"kernel odds and ends", oddsAndEnds},
    {"kernel thread priorities", threadPriorities}, {"kernel thread stack free", stackFree},
  };
}

}
