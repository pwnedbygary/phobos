//Alarms and virtual timers: functions of the program's that the system's timer calls when a moment comes, as
//interrupt handlers (calls into the program, interrupts.cpp: on top of whichever thread runs, unable to wait,
//sceKernelGetThreadId telling them they're no thread). What the handler returns says when it's called again.
//
//Times are microseconds of the system's clock (sceKernelGetSystemTime's: since power on), 64 bits wide.
//
//An alarm (sceKernelSetAlarm, sceKernelSetSysClockAlarm) goes off once its time comes, calling handler(common); a
//handler returning 0 ends it, any other number has it go off again that many microseconds after the moment it was
//due. A virtual timer (sceKernelCreateVTimer) is a clock of its own that runs only while it's started, and may be
//set to any time; its handler is called when its time reaches the handler's schedule, as handler(timer, &schedule,
//&its time now, common), and returning a number puts the schedule that much later in the timer's time (0 drops the
//handler, the schedule kept). Every handler runs with the global pointer of the thread that set it.
//
//pspsdk's pspthreadman.h gives the functions and structures. What they check and answer, in what order, how a
//status is copied, when handlers run and what they're told, are as pspautotests' threads/alarm (alarm, set, cancel,
//refer), threads/vtimers (vtimer, create, delete, start, stop, gettime, settime, getbase, sethandler, cancelhandler,
//refer, interrupt) and threads/scheduling/alarmcosts recorded them on a PSP. Among them:
//- A timer a system call sets going (an alarm set, a handler set, a timer started or set to another time) doesn't go
//  off sooner than about 215 microseconds after that call (alarmcosts: "it never goes off sooner than about 215us
//  after being set"; the tests' handlers due at once haven't run by the next call). What a call itself costs on a
//  PSP (about 40 microseconds for sceKernelSetAlarm) isn't counted, as no call's is here.
//- A virtual timer's handler due again by the time it returns (its schedule passed meanwhile) is called again at
//  once, until it catches up (sethandler's "Passed handler": three calls in a millisecond). An alarm's isn't: alarm's
//  handler, due again 100 microseconds on after interrupts were held off for milliseconds, was called once, and so an
//  alarm due again in the past goes off its number of microseconds from now instead.
//- SetVTimerHandlerWide calls its handler the way SetVTimerHandler does, the two times by address (sethandler:
//  "pspsdk is wrong, they are the same handler").
//- On the virtual timer whose handler is running, or on timer 0 outside any, starting, stopping, setting or
//  cancelling a handler is refused as an illegal timer (ILLEGAL_VTID: cancelhandler's handler cancelling its own
//  timer, and timer 0 everywhere but in a handler, where it's merely unknown); any other unknown timer is
//  UNKNOWN_VTID.
//- From an interrupt handler, making or deleting a timer is refused (ILLEGAL_CONTEXT, before the timer is looked
//  for), and SetVTimerTimeWide answers -1; the rest work.

//The cycle at which the system's clock reaches a moment (as late as can be for one past a cycle count's reach: an
//alarm 2^63 microseconds away, which sceKernelSetSysClockAlarm takes, never comes).
static auto cycleAt(u64 microseconds) -> u64 {
  constexpr u64 PerMicrosecond = Kernel::CPUFrequency / 1'000'000;
  return microseconds >= ~0ull / PerMicrosecond ? ~0ull : microseconds * PerMicrosecond;
}

//The system's clock now, in microseconds.
auto Kernel::systemTime() const -> u64 {
  return cycles / (CPUFrequency / 1'000'000);
}

//The soonest a timer a system call sets going now may go off.
auto Kernel::timerEarliest() const -> u64 {
  return cycles + TimerLead * (CPUFrequency / 1'000'000);
}

//When an alarm goes off (the cycle), or never (~0) while its handler waits its turn or runs.
auto Kernel::alarmDue(const Alarm& alarm) const -> u64 {
  if(alarm.calling) return ~0ull;
  return std::max(cycleAt(alarm.schedule), alarm.earliest);
}

//A virtual timer's time now.
auto Kernel::vtimerTime(const VTimer& timer) const -> u64 {
  return timer.active ? timer.elapsed + (systemTime() - timer.base) : timer.elapsed;
}

//When a virtual timer's handler is due (the cycle): when its time reaches the schedule, at once if it has passed
//it; never (~0) while it's stopped, has no handler, or its handler waits its turn or runs.
auto Kernel::vtimerDue(const VTimer& timer) const -> u64 {
  if(!timer.active || !timer.handler || timer.calling) return ~0ull;
  u64 at = timer.base;  //(the system's time when the timer's own reaches the schedule)
  if(timer.schedule > timer.elapsed) {
    u64 ahead = timer.schedule - timer.elapsed;
    at = ahead > ~0ull - at ? ~0ull : at + ahead;
  }
  return std::max(cycleAt(at), timer.earliest);
}

//The next cycle a timer goes off (~0: none will).
auto Kernel::nextTimerEvent() const -> u64 {
  u64 next = ~0ull;
  for(auto& [uid, alarm] : alarms) next = std::min(next, alarmDue(alarm));
  for(auto& [uid, timer] : vtimers) next = std::min(next, vtimerDue(timer));
  return next;
}

//What's due by now (events()): each timer that has gone off has its handler join the calls into the program.
auto Kernel::timerEvents() -> void {
  for(auto& [uid, alarm] : alarms) {
    if(cycles < alarmDue(alarm)) continue;
    alarm.calling = true;
    calls.push_back({alarm.handler, alarm.gp, {alarm.common, 0, 0, 0}, false, false, Call::Alarm, uid});
  }
  for(auto& [uid, timer] : vtimers) {
    if(cycles < vtimerDue(timer)) continue;
    timer.calling = true;
    calls.push_back({timer.handler, timer.gp, {uid, TimerClocks, TimerClocks + 8, timer.common}, false, false,
                     Call::VTimer, uid});
  }
}

//A timer's call into the program that hasn't started yet goes, the timer gone (cancelled or deleted).
auto Kernel::timerCallsDropped(u32 kind, u32 uid) -> void {
  std::erase_if(calls, [&](const Call& call) { return call.kind == kind && call.id == uid; });
}

//The virtual timer whose handler is running, or 0 outside any.
auto Kernel::vtimerInHandler() const -> u32 {
  return interrupting && callKind == Call::VTimer ? callID : 0;
}

//A virtual timer's handler starts (startCall()): it's told the schedule and the timer's time now through two
//SceKernelSysClocks in kernel memory, as its second and third arguments point.
auto Kernel::vtimerClocks(u32 uid) -> void {
  auto found = vtimers.find(uid);
  if(found == vtimers.end()) return;
  u64 schedule = found->second.schedule, now = vtimerTime(found->second);
  memory.write(4, TimerClocks + 0, u32(schedule));
  memory.write(4, TimerClocks + 4, u32(schedule >> 32));
  memory.write(4, TimerClocks + 8, u32(now));
  memory.write(4, TimerClocks + 12, u32(now >> 32));
}

//A timer's handler returned (callReturned()) what says when it's called again.
auto Kernel::timerReturned(u32 kind, u32 uid, u32 again) -> void {
  if(kind == Call::Alarm) {
    auto found = alarms.find(uid);
    if(found == alarms.end()) return;  //cancelled meanwhile
    auto& alarm = found->second;
    alarm.calling = false;
    if(!again) return (void)alarms.erase(found);
    alarm.schedule += again;
    if(cycleAt(alarm.schedule) <= cycles) alarm.schedule = systemTime() + again;
  }
  if(kind == Call::VTimer) {
    auto found = vtimers.find(uid);
    if(found == vtimers.end()) return;
    auto& timer = found->second;
    timer.calling = false;
    if(!again) timer.handler = 0;
    else timer.schedule += again;
  }
}

//Sets an alarm schedule microseconds of the system's clock from now: its ID. A null handler is refused
//(ILLEGAL_ADDR).
auto Kernel::setAlarm(u64 schedule, u32 handler, u32 common) -> void {
  if(!handler) return result(ErrorIllegalAddress);
  u32 uid = newUID();
  if(!uid) return result(ErrorNoMemory);
  alarms[uid] = {uid, systemTime() + schedule, timerEarliest(), handler, common, cpu.ipu.r[28]};
  result(uid);
}

//(microseconds from now, handler, common)
auto Kernel::sceKernelSetAlarm() -> void {
  setAlarm(arg(0), arg(1), arg(2));
}

//(where a SceKernelSysClock gives the microseconds from now, handler, common): the same, 64 bits wide (a sum past
//2^64 wraps round, as on a PSP: an alarm 2^64 - 2 microseconds away is due 2 microseconds ago, and goes off as soon
//as an alarm can).
auto Kernel::sceKernelSetSysClockAlarm() -> void {
  if(!arg(1)) return result(ErrorIllegalAddress);
  setAlarm(memory.read(4, arg(0)) | u64(memory.read(4, arg(0) + 4)) << 32, arg(1), arg(2));
}

//(alarm): it's gone, before or after it went off (an alarm whose handler ended it is gone already: UNKNOWN_ALMID).
auto Kernel::sceKernelCancelAlarm() -> void {
  if(!alarms.erase(arg(0))) return result(ErrorUnknownAlarm);
  timerCallsDropped(Call::Alarm, arg(0));
  result(0);
}

//(alarm, info): a SceKernelAlarmInfo (its size, 20; when it's due; its handler; its common), copied as far as the
//size in its first word: 0 copies nothing, 1 to 4 only the size, 5 the schedule's first byte too, and so on.
auto Kernel::sceKernelReferAlarmStatus() -> void {
  auto found = alarms.find(arg(0));
  if(found == alarms.end()) return result(ErrorUnknownAlarm);
  auto& alarm = found->second;
  u32 info = arg(1), size = memory.read(4, info);
  u8 status[20] = {};
  auto word = [&](u32 offset, u32 value) { for(u32 n : range(4)) status[offset + n] = value >> n * 8; };
  word(0, sizeof(status));
  word(4, u32(alarm.schedule));
  word(8, u32(alarm.schedule >> 32));
  word(12, alarm.handler);
  word(16, alarm.common);
  memory.copyIn(info, status, std::min<u32>(size, sizeof(status)));
  result(0);
}

//What starting, stopping, setting a handler and cancelling one check first: the timer whose handler is running (or
//timer 0, outside any), then whether it's there. Null, with the error for the result, if it isn't one to change.
auto Kernel::vtimerFor(u32 uid) -> VTimer* {
  if(uid == vtimerInHandler()) return result(ErrorIllegalVTimer), nullptr;
  auto found = vtimers.find(uid);
  if(found == vtimers.end()) return result(ErrorUnknownVTimer), nullptr;
  return &found->second;
}

//A timer there is: null, with UNKNOWN_VTID for the result, if not. (Timer 0 is merely unknown here.)
auto Kernel::vtimerAt(u32 uid) -> VTimer* {
  auto found = vtimers.find(uid);
  if(found == vtimers.end()) return result(ErrorUnknownVTimer), nullptr;
  return &found->second;
}

//(name, options): a timer, stopped, its time 0, with no handler. A name is needed (ERROR); the options are read for
//nothing (any size is taken).
auto Kernel::sceKernelCreateVTimer() -> void {
  if(interrupting) return result(ErrorIllegalContext);
  if(!arg(0)) return result(ErrorError);
  u32 uid = newUID();
  if(!uid) return result(ErrorNoMemory);
  vtimers[uid] = {uid, memory.readString(arg(0), 31)};
  result(uid);
}

//(timer): it's gone, started or not; its handler, waiting its turn to be called, goes with it.
auto Kernel::sceKernelDeleteVTimer() -> void {
  if(interrupting) return result(ErrorIllegalContext);
  if(!vtimers.erase(arg(0))) return result(ErrorUnknownVTimer);
  timerCallsDropped(Call::VTimer, arg(0));
  result(0);
}

//(timer, where to put it): the system's time when it was started, 0 while it's stopped.
auto Kernel::sceKernelGetVTimerBase() -> void {
  auto timer = vtimerAt(arg(0));
  if(!timer) return;
  memory.write(4, arg(1), u32(timer->base));
  memory.write(4, arg(1) + 4, u32(timer->base >> 32));
  result(0);
}

//(timer): the same, in v0 and v1; -1 for a timer there isn't.
auto Kernel::sceKernelGetVTimerBaseWide() -> void {
  auto found = vtimers.find(arg(0));
  result64(found == vtimers.end() ? ~0ull : found->second.base);
}

//(timer, where to put it): its time now.
auto Kernel::sceKernelGetVTimerTime() -> void {
  auto timer = vtimerAt(arg(0));
  if(!timer) return;
  u64 time = vtimerTime(*timer);
  memory.write(4, arg(1), u32(time));
  memory.write(4, arg(1) + 4, u32(time >> 32));
  result(0);
}

//(timer): the same, in v0 and v1; -1 for a timer there isn't.
auto Kernel::sceKernelGetVTimerTimeWide() -> void {
  auto found = vtimers.find(arg(0));
  result64(found == vtimers.end() ? ~0ull : vtimerTime(found->second));
}

//The timer's time becomes time; returns the time it had. A started timer goes on from it (its base, the system's
//time now: whether a PSP moves its base or keeps it isn't known), its handler no sooner than a call allows.
auto Kernel::vtimerSet(VTimer& timer, u64 time) -> u64 {
  u64 was = vtimerTime(timer);
  timer.elapsed = time;
  if(timer.active) timer.base = systemTime();
  timer.earliest = timerEarliest();
  return was;
}

//(timer, where a SceKernelSysClock gives its new time, and gets the time it had)
auto Kernel::sceKernelSetVTimerTime() -> void {
  if(interrupting) return result(ErrorIllegalContext);
  auto timer = vtimerAt(arg(0));
  if(!timer) return;
  u32 clock = arg(1);
  u64 was = vtimerSet(*timer, memory.read(4, clock) | u64(memory.read(4, clock + 4)) << 32);
  memory.write(4, clock, u32(was));
  memory.write(4, clock + 4, u32(was >> 32));
  result(0);
}

//(timer, its new time, 64 bits in a2 and a3, as EABI aligns them): the time it had, in v0 and v1; -1 for a timer
//there isn't, or from an interrupt handler.
auto Kernel::sceKernelSetVTimerTimeWide() -> void {
  auto found = vtimers.find(arg(0));
  if(interrupting || found == vtimers.end()) return result64(~0ull);
  result64(vtimerSet(found->second, arg(2) | u64(arg(3)) << 32));
}

//(timer): it runs from its time on: 0, or 1 if it was running already.
auto Kernel::sceKernelStartVTimer() -> void {
  auto timer = vtimerFor(arg(0));
  if(!timer) return;
  if(timer->active) return result(1);
  timer->active = true;
  timer->base = systemTime();
  timer->earliest = timerEarliest();
  result(0);
}

//(timer): it stops, keeping its time: 1, or 0 if it was stopped already.
auto Kernel::sceKernelStopVTimer() -> void {
  auto timer = vtimerFor(arg(0));
  if(!timer) return;
  if(!timer->active) return result(0);
  timer->elapsed = vtimerTime(*timer);
  timer->active = false;
  timer->base = 0;
  result(1);
}

//The timer's handler, called as its time reaches schedule. A null handler only drops the handler there was: the
//schedule and common stay as they were, and the time isn't read (an interrupt handler passes none).
auto Kernel::vtimerHandler(VTimer& timer, u64 schedule, u32 handler, u32 common) -> void {
  timer.handler = handler;
  if(handler) {
    timer.schedule = schedule;
    timer.common = common;
    timer.gp = cpu.ipu.r[28];
    timer.earliest = timerEarliest();
  }
  result(0);
}

//(timer, where a SceKernelSysClock gives the schedule, handler, common)
auto Kernel::sceKernelSetVTimerHandler() -> void {
  auto timer = vtimerFor(arg(0));
  if(!timer) return;
  u32 clock = arg(1), handler = arg(2);
  u64 schedule = handler ? memory.read(4, clock) | u64(memory.read(4, clock + 4)) << 32 : 0;
  vtimerHandler(*timer, schedule, handler, arg(3));
}

//(timer, the schedule, 64 bits in a2 and a3, handler in t0, common in t1)
auto Kernel::sceKernelSetVTimerHandlerWide() -> void {
  auto timer = vtimerFor(arg(0));
  if(!timer) return;
  vtimerHandler(*timer, arg(2) | u64(arg(3)) << 32, arg(4), arg(5));
}

//(timer): its handler is dropped; its schedule and common stay.
auto Kernel::sceKernelCancelVTimerHandler() -> void {
  auto timer = vtimerFor(arg(0));
  if(!timer) return;
  timer->handler = 0;
  result(0);
}

//(timer, info): a SceKernelVTimerInfo (its size, 72; the name; whether it runs; its base, its time and its schedule,
//64 bits each; its handler; its common), copied as far as the size in its first word (0 copies nothing).
auto Kernel::sceKernelReferVTimerStatus() -> void {
  auto timer = vtimerAt(arg(0));
  if(!timer) return;
  u32 info = arg(1), size = memory.read(4, info);
  u8 status[72] = {};
  auto word = [&](u32 offset, u32 value) { for(u32 n : range(4)) status[offset + n] = value >> n * 8; };
  auto wide = [&](u32 offset, u64 value) { word(offset, u32(value)); word(offset + 4, u32(value >> 32)); };
  word(0, sizeof(status));
  memcpy(status + 4, timer->name.data(), std::min<size_t>(timer->name.size(), 31));
  word(36, timer->active);
  wide(40, timer->base);
  wide(48, vtimerTime(*timer));
  wide(56, timer->schedule);
  word(64, timer->handler);
  word(68, timer->common);
  memory.copyIn(info, status, std::min<u32>(size, sizeof(status)));
  result(0);
}
