//Alarms and virtual timers (ares/psp/kernel/timers.cpp): what their functions take and refuse and what a status
//copies, as pspautotests' threads/alarm and threads/vtimers recorded on a PSP; handlers called as interrupts (no
//thread, no waiting, the global pointer they were set with), no sooner than 215 microseconds after the call that set
//them going, again by what they return (an alarm from its moment, a virtual timer's schedule in its own time, at
//once while it has fallen behind), held back while interrupts are off; and states saved with timers armed, and with
//a handler's call waiting. Programs run on both engines; each group's machine, saved at its end, loads into another
//that makes the same state.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {

constexpr u32 R = KernelMachine::Results;
constexpr u32 Log = R + 0x100;  //words written down in turn, R + 8 counting them

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

auto logged(KernelMachine& m) -> std::vector<u32> {
  std::vector<u32> words;
  for(u32 n = 0; n < word(m, R + 8); n++) words.push_back(word(m, Log + n * 4));
  return words;
}

//Writes register value down in the log (t0-t2 used).
auto note(Assembler& a, u32 value) -> void {
  a.li(t0, R);
  a.put(lw(t1, 8, t0));
  a.put(sll(t2, t1, 2));
  a.put(addu(t2, t2, t0));
  a.put(sw(value, 0x100, t2));
  a.put(addiu(t1, t1, 1));
  a.put(sw(t1, 8, t0));
}

auto delay(Assembler& a, u32 microseconds) -> void {
  a.li(a0, microseconds);
  a.call("sceKernelDelayThread");
}

//The program spins on the clock for so many microseconds, waiting in nothing (s6 and s7 hold the start and the
//length).
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

//A handler's start and end: a frame on the interrupt stack holding ra and s0-s2 (the handler's arguments kept there).
auto enter(Assembler& a) -> void {
  a.put(addiu(sp, sp, -32));
  a.put(sw(ra, 28, sp)); a.put(sw(s0, 24, sp)); a.put(sw(s1, 20, sp)); a.put(sw(s2, 16, sp));
}

auto leave(Assembler& a) -> void {
  a.put(lw(ra, 28, sp)); a.put(lw(s0, 24, sp)); a.put(lw(s1, 20, sp)); a.put(lw(s2, 16, sp));
  a.put(jr(ra));
  a.put(addiu(sp, sp, 32));
}

//An alarm's or a virtual timer's status, copied with the size given (the buffer filled with 0xcc first).
auto refer(KernelMachine& m, const char* function, u32 uid, u32 size) -> u32 {
  m.system.memory.fill(R + 0x200, 0xcc, 0x60);
  m.system.memory.write(4, R + 0x200, size);
  return m.call(function, {uid, R + 0x200});
}

}

//Alarms called directly, as threads/alarm's set, cancel and refer recorded: a null handler refused (ILLEGAL_ADDR,
//sceKernelSetSysClockAlarm's too, before its time is read); the schedule the system's time plus the time asked (64
//bits: 2^64 - 2 microseconds is 2 microseconds ago); a status copied as far as its size (0 none, 1-4 the size alone,
//5 the schedule's first byte); cancelling one there isn't, or cancelled already, UNKNOWN_ALMID.
static auto alarmCalls() -> void {
  KernelMachine m;
  m.kernel.cycles = 5000 * (Kernel::CPUFrequency / 1'000'000);  //5000 microseconds since power on
  CHECK(m.call("sceKernelSetAlarm", {100, 0, 0}), Kernel::ErrorIllegalAddress);
  CHECK(m.call("sceKernelSetSysClockAlarm", {0, 0, 0}), Kernel::ErrorIllegalAddress);
  u32 alarm = m.call("sceKernelSetAlarm", {100, 0x0880'3000, 0x1234});
  CHECK(alarm < 0x8000'0000, true);
  CHECK(refer(m, "sceKernelReferAlarmStatus", alarm, 20), 0);
  CHECK(word(m, R + 0x200) == 20 && word(m, R + 0x204) == 5100 && word(m, R + 0x208) == 0, true);
  CHECK(word(m, R + 0x20c) == 0x0880'3000 && word(m, R + 0x210) == 0x1234 && word(m, R + 0x214) == 0xcccc'cccc, true);
  CHECK(refer(m, "sceKernelReferAlarmStatus", alarm, 0), 0);
  CHECK(word(m, R + 0x200) == 0 && word(m, R + 0x204) == 0xcccc'cccc, true);
  refer(m, "sceKernelReferAlarmStatus", alarm, 1);
  CHECK(word(m, R + 0x200) == 20 && word(m, R + 0x204) == 0xcccc'cccc, true);  //(1 made 20 by its first byte)
  refer(m, "sceKernelReferAlarmStatus", alarm, 5);
  CHECK(word(m, R + 0x200) == 20 && word(m, R + 0x204) == 0xcccc'cc00 + (5100 & 0xff), true);
  refer(m, "sceKernelReferAlarmStatus", alarm, 13);
  CHECK(word(m, R + 0x20c), 0xcccc'cc00);  //the handler's first byte
  m.system.memory.write(4, R + 0x40, 0xffff'fffe);
  m.system.memory.write(4, R + 0x44, 0xffff'ffff);
  u32 behind = m.call("sceKernelSetSysClockAlarm", {R + 0x40, 0x0880'3000, 0});
  refer(m, "sceKernelReferAlarmStatus", behind, 20);
  CHECK(word(m, R + 0x204) == 4998 && word(m, R + 0x208) == 0, true);
  m.system.memory.write(4, R + 0x40, 0xffff'ffff);
  m.system.memory.write(4, R + 0x44, 0x7fff'ffff);
  u32 never = m.call("sceKernelSetSysClockAlarm", {R + 0x40, 0x0880'3000, 0});
  refer(m, "sceKernelReferAlarmStatus", never, 20);
  CHECK(word(m, R + 0x204) == 5000 - 1 + 0 && word(m, R + 0x208) == 0x8000'0000, true);  //2^63 - 1 on: never
  CHECK(m.kernel.alarmDue(m.kernel.alarms[never]), ~0ull);
  CHECK(roundTrip(m), true);
  CHECK(m.call("sceKernelCancelAlarm", {alarm}), 0);
  CHECK(m.call("sceKernelCancelAlarm", {alarm}), Kernel::ErrorUnknownAlarm);
  CHECK(m.call("sceKernelReferAlarmStatus", {alarm, R + 0x200}), Kernel::ErrorUnknownAlarm);
  CHECK(m.call("sceKernelCancelAlarm", {0}), Kernel::ErrorUnknownAlarm);
}

//Alarms going off, in a program. The handler writes down its argument, its global pointer, what
//sceKernelGetThreadId and a delay tell it (ILLEGAL_CONTEXT: an interrupt handler is no thread and can't wait), and
//the time; it returns 1000 twice, then 0. Set 1000 microseconds on, it goes off then, and 1000 after each moment it
//was due, then is gone (cancelling it: UNKNOWN_ALMID). An alarm set to go off at once does so 215 microseconds on.
//One due while interrupts are held off goes off as they come back on, once, and, its next moment (100 on from the
//last) passed already, 100 from then.
static auto alarmsGoOff() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Count = R + 0x40;
    Assembler handler{m, 0x0880'3000};
    enter(handler);
    handler.put(addu(s0, a0, zero));
    note(handler, s0);
    note(handler, gp);
    handler.call("sceKernelGetThreadId");
    note(handler, v0);
    handler.li(a0, 100);
    handler.call("sceKernelDelayThread");
    note(handler, v0);
    handler.call("sceKernelGetSystemTimeLow");
    note(handler, v0);
    handler.li(t0, Count); handler.put(lw(t1, 0, t0)); handler.put(addiu(t1, t1, 1)); handler.put(sw(t1, 0, t0));
    handler.li(v0, 1000);
    handler.put(sltiu(t1, t1, 3));
    handler.put(bne(t1, zero, 2));
    handler.put(nop);
    handler.li(v0, 0);  //(two words: the third call returns 0)
    leave(handler);
    Assembler quick{m, 0x0880'3800};  //writes down the time, returns R + 0x44's word
    enter(quick);
    quick.call("sceKernelGetSystemTimeLow");
    note(quick, v0);
    quick.li(t0, R + 0x44); quick.put(lw(v0, 0, t0));
    leave(quick);

    Assembler main{m, 0x0880'1000};
    main.li(gp, 0x0880'7770);  //the handler is called with this
    main.call("sceKernelGetSystemTimeLow");
    note(main, v0);
    main.li(a0, 1000); main.li(a1, 0x0880'3000); main.li(a2, 0x1234);
    main.call("sceKernelSetAlarm");
    main.put(addu(s0, v0, zero));
    delay(main, 5000);
    main.put(addu(a0, s0, zero));
    main.call("sceKernelCancelAlarm");  //gone, its handler having returned 0
    note(main, v0);
    //at once: 215 microseconds on (main spins on the clock meanwhile)
    main.li(t0, R + 0x44); main.put(sw(zero, 0, t0));
    main.call("sceKernelGetSystemTimeLow");
    note(main, v0);
    main.li(a0, 0); main.li(a1, 0x0880'3800); main.li(a2, 0);
    main.call("sceKernelSetAlarm");
    spin(main, 400);
    //due while interrupts are held off: once as they come back on, then 100 from then
    main.li(t0, R + 0x44); main.li(t1, 100); main.put(sw(t1, 0, t0));
    main.li(a0, 300); main.li(a1, 0x0880'3800); main.li(a2, 0);
    main.call("sceKernelSetAlarm");
    main.put(addu(s0, v0, zero));
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s1, v0, zero));
    spin(main, 2000);
    main.call("sceKernelGetSystemTimeLow");
    note(main, v0);
    main.put(addu(a0, s1, zero));
    main.call("sceKernelCpuResumeIntr");  //it goes off as this returns
    spin(main, 150);  //and again, 100 on
    main.put(addu(a0, s0, zero));
    main.call("sceKernelCancelAlarm");
    note(main, v0);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto log = logged(m);
    CHECK(log.size(), 1 + 3 * 5 + 1 + 1 + 1 + 1 + 2 + 1);
    if(log.size() != 1 + 3 * 5 + 1 + 1 + 1 + 1 + 2 + 1) return;
    u32 set = log[0];
    for(u32 n = 0; n < 3; n++) {
      auto call = &log[1 + n * 5];
      CHECK(call[0] == 0x1234 && call[1] == 0x0880'7770, true);
      CHECK(call[2] == Kernel::ErrorIllegalContext && call[3] == Kernel::ErrorIllegalContext, true);
      CHECK(call[4] - set >= 1000 * (n + 1) && call[4] - set <= 1000 * (n + 1) + 2, true);  //1000 on, each
    }
    CHECK(log[16], Kernel::ErrorUnknownAlarm);
    CHECK(log[18] - log[17] >= 215 && log[18] - log[17] <= 217, true);  //at once: 215 microseconds on
    u32 resumed = log[19];          //(as interrupts came back on, give or take the instructions in between)
    CHECK(log[20] - resumed <= 1, true);
    CHECK(log[21] - log[20] >= 100 && log[21] - log[20] <= 102, true);
    CHECK(log[22], 0);  //still there (it returned 100)
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//Virtual timers called directly, as threads/vtimers' create, start, stop, gettime, settime, getbase, sethandler,
//cancelhandler and refer recorded: starting answers whether it ran already, stopping whether it was running; timer 0
//is an illegal one there (ILLEGAL_VTID), any other unknown UNKNOWN_VTID (getting, setting, referring, deleting: 0 is
//merely unknown); its time runs only while it's started, the base the system's time it started at; setting the time
//hands back the old (the wide form returns it, -1 for a timer there isn't); a null handler drops the handler, the
//schedule and common kept; a status copied as far as its size.
static auto vtimerCalls() -> void {
  KernelMachine m;
  auto& k = m.kernel;
  constexpr u64 Microsecond = Kernel::CPUFrequency / 1'000'000;
  k.cycles = 7000 * Microsecond;
  CHECK(m.call("sceKernelCreateVTimer", {0, 0}), Kernel::ErrorError);
  u32 timer = m.call("sceKernelCreateVTimer", {m.string("vtimer"), 0});
  CHECK(timer < 0x8000'0000, true);
  CHECK(m.call("sceKernelStartVTimer", {0}), Kernel::ErrorIllegalVTimer);
  CHECK(m.call("sceKernelStartVTimer", {0xdead'beef}), Kernel::ErrorUnknownVTimer);
  CHECK(m.call("sceKernelStopVTimer", {0}), Kernel::ErrorIllegalVTimer);
  CHECK(m.call("sceKernelCancelVTimerHandler", {0}), Kernel::ErrorIllegalVTimer);
  CHECK(m.call("sceKernelReferVTimerStatus", {0, R + 0x200}), Kernel::ErrorUnknownVTimer);
  CHECK(m.call("sceKernelDeleteVTimer", {0}), Kernel::ErrorUnknownVTimer);
  m.system.memory.write(4, R + 0x40, 0x1337);
  CHECK(m.call("sceKernelGetVTimerTime", {0, R + 0x40}), Kernel::ErrorUnknownVTimer);
  CHECK(word(m, R + 0x40), 0x1337);  //left as it was
  m.call("sceKernelGetVTimerTimeWide", {0});
  CHECK(m.system.ipu.r[2] == 0xffff'ffff && m.system.ipu.r[3] == 0xffff'ffff, true);

  CHECK(m.call("sceKernelStopVTimer", {timer}), 0);  //stopped already
  CHECK(m.call("sceKernelStartVTimer", {timer}), 0);
  CHECK(m.call("sceKernelStartVTimer", {timer}), 1);  //running already
  m.call("sceKernelGetVTimerBaseWide", {timer});
  CHECK(m.system.ipu.r[2] == 7000 && m.system.ipu.r[3] == 0, true);
  k.cycles += 2500 * Microsecond;
  m.call("sceKernelGetVTimerTimeWide", {timer});
  CHECK(m.system.ipu.r[2] == 2500 && m.system.ipu.r[3] == 0, true);
  CHECK(m.call("sceKernelStopVTimer", {timer}), 1);
  k.cycles += 1000 * Microsecond;  //stopped: its time stays
  CHECK(m.call("sceKernelGetVTimerTime", {timer, R + 0x40}), 0);
  CHECK(word(m, R + 0x40) == 2500 && word(m, R + 0x44) == 0, true);
  CHECK(m.call("sceKernelGetVTimerBase", {timer, R + 0x40}), 0);
  CHECK(word(m, R + 0x40) == 0 && word(m, R + 0x44) == 0, true);  //no base while stopped
  //setting: the old time handed back, by address or as the wide form's result
  m.system.memory.write(4, R + 0x40, 0x6789'0123);
  m.system.memory.write(4, R + 0x44, 0x0001'2345);
  CHECK(m.call("sceKernelSetVTimerTime", {timer, R + 0x40}), 0);
  CHECK(word(m, R + 0x40) == 2500 && word(m, R + 0x44) == 0, true);
  m.call("sceKernelSetVTimerTimeWide", {timer, 0, 0xffff'ffff, 0xffff'ffff});
  CHECK(m.system.ipu.r[2] == 0x6789'0123 && m.system.ipu.r[3] == 0x0001'2345, true);
  m.call("sceKernelSetVTimerTimeWide", {timer, 0, 10, 0});
  CHECK(m.system.ipu.r[2] == 0xffff'ffff && m.system.ipu.r[3] == 0xffff'ffff, true);
  m.call("sceKernelSetVTimerTimeWide", {0x7777, 0, 10, 0});
  CHECK(m.system.ipu.r[2] == 0xffff'ffff && m.system.ipu.r[3] == 0xffff'ffff, true);
  //handlers: set (the wide form's schedule in a2 and a3, its handler in t0, common in t1); a null one dropping the
  //handler alone
  m.system.memory.write(4, R + 0x40, 400);
  m.system.memory.write(4, R + 0x44, 0);
  CHECK(m.call("sceKernelSetVTimerHandler", {timer, R + 0x40, 0x0880'3000, 0xdead'beef}), 0);
  CHECK(k.vtimers[timer].schedule == 400 && k.vtimers[timer].common == 0xdead'beef, true);
  CHECK(m.call("sceKernelSetVTimerHandlerWide", {timer, 0, 900, 1, 0x0880'3100, 0x1234}), 0);
  CHECK(k.vtimers[timer].schedule == 0x1'0000'0384ull && k.vtimers[timer].handler == 0x0880'3100, true);
  CHECK(m.call("sceKernelSetVTimerHandler", {timer, 0, 0, 0x5555}), 0);  //the time isn't read
  CHECK(k.vtimers[timer].handler == 0 && k.vtimers[timer].common == 0x1234, true);
  CHECK(k.vtimers[timer].schedule, 0x1'0000'0384ull);
  CHECK(m.call("sceKernelSetVTimerHandlerWide", {timer, 0, 50, 0, 0x0880'3000, 0x99}), 0);
  CHECK(m.call("sceKernelCancelVTimerHandler", {timer}), 0);
  CHECK(k.vtimers[timer].handler == 0 && k.vtimers[timer].common == 0x99 && k.vtimers[timer].schedule == 50, true);
  //its status as far as its size: 72 for any size but 0 (0xffffffff too), common's low half alone for 70
  CHECK(refer(m, "sceKernelReferVTimerStatus", timer, 72), 0);
  CHECK(word(m, R + 0x200) == 72 && m.system.memory.readString(R + 0x204, 32) == "vtimer", true);
  CHECK(word(m, R + 0x224) == 0 && word(m, R + 0x228) == 0 && word(m, R + 0x230) == 10, true);
  CHECK(word(m, R + 0x238) == 50 && word(m, R + 0x240) == 0 && word(m, R + 0x244) == 0x99, true);
  refer(m, "sceKernelReferVTimerStatus", timer, 70);
  CHECK(word(m, R + 0x244), 0xcccc'0099);
  refer(m, "sceKernelReferVTimerStatus", timer, 0xffff'ffff);
  CHECK(word(m, R + 0x200) == 72 && word(m, R + 0x248) == 0xcccc'cccc, true);
  refer(m, "sceKernelReferVTimerStatus", timer, 0);
  CHECK(word(m, R + 0x200) == 0 && word(m, R + 0x204) == 0xcccc'cccc, true);
  CHECK(roundTrip(m), true);
  CHECK(m.call("sceKernelDeleteVTimer", {timer}), 0);
  CHECK(m.call("sceKernelDeleteVTimer", {timer}), Kernel::ErrorUnknownVTimer);
  CHECK(m.call("sceKernelStartVTimer", {timer}), Kernel::ErrorUnknownVTimer);
}

//A virtual timer's handler, in a program. Set to 10000 and stopped, it's given a handler due at 9000 (passed
//already), then started: the handler runs 215 microseconds later, told (its timer, &schedule, &its time now,
//common), and returns 1000: due again at 10000, passed too, it runs at once; then at 11000, 1000 microseconds after
//the start; that third call returns 0, which drops the handler and keeps the schedule. Inside, the handler is told
//its own timer is an illegal one to cancel (ILLEGAL_VTID), timer 0 an unknown one, that making or deleting a timer
//is ILLEGAL_CONTEXT, and SetVTimerTimeWide -1. A stopped timer's handler doesn't run.
static auto vtimerHandlers() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Count = R + 0x40;
    Assembler handler{m, 0x0880'3000};
    enter(handler);
    handler.put(addu(s0, a0, zero));
    handler.put(addu(s1, a1, zero));
    handler.put(addu(s2, a2, zero));
    note(handler, s0);
    handler.put(lw(t4, 0, s1)); note(handler, t4);  //its schedule
    handler.put(lw(t4, 0, s2)); note(handler, t4);  //its timer's time
    note(handler, a3);
    handler.call("sceKernelGetSystemTimeLow");
    note(handler, v0);
    handler.li(t0, Count); handler.put(lw(t1, 0, t0));
    u32 skip = handler.here();  //the first call alone tries the rest (the branch over them written below)
    handler.put(nop);
    handler.put(nop);
    handler.put(addu(a0, s0, zero));
    handler.call("sceKernelCancelVTimerHandler");
    note(handler, v0);
    handler.li(a0, 0);
    handler.call("sceKernelCancelVTimerHandler");
    note(handler, v0);
    handler.put(addu(a0, s0, zero));
    handler.call("sceKernelDeleteVTimer");
    note(handler, v0);
    handler.li(a0, m.string("x")); handler.li(a1, 0);
    handler.call("sceKernelCreateVTimer");
    note(handler, v0);
    handler.put(addu(a0, s0, zero)); handler.li(a2, 5); handler.li(a3, 0);
    handler.call("sceKernelSetVTimerTimeWide");
    note(handler, v1);
    m.system.memory.write(4, skip, bne(t1, zero, int32_t(handler.here() - (skip + 4)) / 4));
    handler.li(t0, Count); handler.put(lw(t1, 0, t0)); handler.put(addiu(t1, t1, 1)); handler.put(sw(t1, 0, t0));
    handler.li(v0, 1000);
    handler.put(sltiu(t1, t1, 3));
    handler.put(bne(t1, zero, 2));
    handler.put(nop);
    handler.li(v0, 0);
    leave(handler);

    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("vtimer")); main.li(a1, 0);
    main.call("sceKernelCreateVTimer");
    main.put(addu(s0, v0, zero));
    main.li(t0, R); main.put(sw(s0, 0, t0));
    main.put(addu(a0, s0, zero)); main.li(a2, 10'000); main.li(a3, 0);
    main.call("sceKernelSetVTimerTimeWide");
    main.put(addu(a0, s0, zero)); main.li(a2, 9000); main.li(a3, 0); main.li(t0, 0x0880'3000); main.li(t1, 0x1337);
    main.call("sceKernelSetVTimerHandlerWide");
    main.call("sceKernelGetSystemTimeLow");
    note(main, v0);
    main.put(addu(a0, s0, zero));
    main.call("sceKernelStartVTimer");
    delay(main, 2000);
    main.put(addu(a0, s0, zero));
    main.call("sceKernelStopVTimer");
    main.put(addu(a0, s0, zero)); main.li(a1, R + 0x200); main.li(t0, 72); main.put(sw(t0, 0, a1));
    main.call("sceKernelReferVTimerStatus");
    //stopped, with a handler due at once: it doesn't run
    main.put(addu(a0, s0, zero)); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0x0880'3000); main.li(t1, 0x1337);
    main.call("sceKernelSetVTimerHandlerWide");
    delay(main, 2000);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto log = logged(m);
    u32 timer = word(m, R);
    CHECK(log.size(), 1 + 5 + 5 + 5 + 5);
    if(log.size() != 1 + 5 + 5 + 5 + 5) return;
    u32 started = log[0];
    auto near = [](u32 value, u32 expected) { return value >= expected && value <= expected + 2; };
    CHECK(log[1] == timer && log[2] == 9000 && near(log[3], 10'215) && log[4] == 0x1337, true);
    CHECK(near(log[5] - started, 215), true);
    CHECK(log[6] == Kernel::ErrorIllegalVTimer && log[7] == Kernel::ErrorUnknownVTimer, true);
    CHECK(log[8] == Kernel::ErrorIllegalContext && log[9] == Kernel::ErrorIllegalContext, true);
    CHECK(log[10], 0xffff'ffff);
    CHECK(log[11] == timer && log[12] == 10'000 && near(log[13], 10'215), true);  //at once
    CHECK(near(log[15] - started, 215), true);
    CHECK(log[16] == timer && log[17] == 11'000 && near(log[18], 11'000), true);
    CHECK(near(log[20] - started, 1000), true);
    CHECK(word(m, Count), 3);
    CHECK(word(m, R + 0x240) == 0 && word(m, R + 0x238) == 11'000 && word(m, R + 0x244) == 0x1337, true);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//States saved with timers armed load into a fresh machine, which makes the same state, and both carry on alike: an
//alarm and a running virtual timer, saved before they're due, go off at the same moments in both; and a state saved
//while interrupts are held off, an alarm's handler waiting its turn, loads with it waiting, and it runs in both as
//they come back on.
static auto timerStates() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    for(u32 timer : {0, 1}) {  //write down their common word (an alarm's first argument, a timer's fourth) and the
                               //time, and return 0
      Assembler handler{m, 0x0880'3000 + timer * 0x100};
      enter(handler);
      note(handler, timer ? a3 : a0);
      handler.call("sceKernelGetSystemTimeLow");
      note(handler, v0);
      handler.li(v0, 0);
      leave(handler);
    }
    Assembler main{m, 0x0880'1000};
    main.li(a0, 3000); main.li(a1, 0x0880'3000); main.li(a2, 0xa1);
    main.call("sceKernelSetAlarm");
    main.li(a0, m.string("vtimer")); main.li(a1, 0);
    main.call("sceKernelCreateVTimer");
    main.put(addu(s0, v0, zero));
    main.put(addu(a0, s0, zero)); main.li(a2, 4000); main.li(a3, 0); main.li(t0, 0x0880'3100); main.li(t1, 0xb2);
    main.call("sceKernelSetVTimerHandlerWide");
    main.put(addu(a0, s0, zero));
    main.call("sceKernelStartVTimer");
    delay(main, 6000);  //the first state is saved in here, before both are due
    main.li(a0, 500); main.li(a1, 0x0880'3000); main.li(a2, 0xc3);
    main.call("sceKernelSetAlarm");
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s1, v0, zero));
    spin(main, 2000);  //the second state is saved in here, the alarm's handler waiting
    main.put(addu(a0, s1, zero));
    main.call("sceKernelCpuResumeIntr");
    delay(main, 1000);
    main.call("sceKernelExitGame");
    m.system.recompiler.enabled = recompile;
    m.system.power(0x0880'1000);
    s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
    m.kernel.run(1000 * (Kernel::CPUFrequency / 1'000'000));
    auto first = saveState(m);
    m.kernel.run(6000 * (Kernel::CPUFrequency / 1'000'000));  //into the spin: the 500's alarm due
    CHECK(m.kernel.calls.size(), 1);
    auto second = saveState(m);
    m.kernel.run(Kernel::CPUFrequency / 10);
    CHECK(m.kernel.exited, true);
    auto log = logged(m);
    CHECK(log.size(), 6);
    if(log.size() == 6) CHECK(log[0] == 0xa1 && log[2] == 0xb2 && log[4] == 0xc3, true);
    for(auto* state : {&first, &second}) {
      KernelMachine n;
      n.system.recompiler.enabled = recompile;
      CHECK(loadState(n, *state), true);
      CHECK(saveState(n) == *state, true);
      n.kernel.run(Kernel::CPUFrequency / 10);
      CHECK(n.kernel.exited, true);
      CHECK(logged(n) == log, true);
      CHECK(roundTrip(n), true);
    }
  }
}

//The clock read inside a block: a syscall made straight from the program (not through a stub's jr ra, which would
//end the block before it) after 900 instructions of its own block reads the same time on both engines, the
//instructions before it counted alike; 900 at 333 MHz are near 3 microseconds, so a block counted from its start
//would read earlier.
static auto clockInBlock() -> void {
  u32 times[2][2];
  for(bool recompile : {false, true}) {
    KernelMachine m;
    u32 code = m.kernel.importCode("test", Kernel::nid("sceKernelGetSystemTimeWide"));
    Assembler main{m, 0x0880'1000};
    main.put(syscall(code));
    main.li(t0, R); main.put(sw(v0, 0x10, t0));
    for(u32 n = 0; n < 900; n++) main.put(addiu(t1, t1, 1));
    main.put(syscall(code));
    main.li(t0, R); main.put(sw(v0, 0x14, t0));
    main.call("sceKernelExitGame");
    CHECK(main.here() < 0x0880'2000, true);  //one section: nothing ends the second block before its syscall
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    times[recompile][0] = word(m, R + 0x10);
    times[recompile][1] = word(m, R + 0x14);
    CHECK(times[recompile][1] - times[recompile][0] >= 2, true);
    CHECK(roundTrip(m), true);
  }
  CHECK(times[0][0], times[1][0]);
  CHECK(times[0][1], times[1][1]);
}

auto timerTests() -> Tests {
  return {
    {"alarms called directly", alarmCalls}, {"alarms going off", alarmsGoOff},
    {"vtimers called directly", vtimerCalls}, {"vtimers' handlers", vtimerHandlers},
    {"timers in states", timerStates}, {"the clock inside a block", clockInBlock},
  };
}

}
