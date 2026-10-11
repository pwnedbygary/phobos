//sceRtc's tick arithmetic: a tick (microseconds since 0001-01-01, as system.cpp's sceRtcGetTick and sceRtcSetTick
//count them) moved on by an amount of some unit, into another tick, as pspautotests' rtc/arithmetic recorded each:
//  - ticks, microseconds, seconds and minutes take a 64-bit amount, hours, days and weeks a 32-bit one (pspsdk's
//    psprtc.h; the test's 64-bit amounts reach them cut to their low 32 bits), multiplied out and added in 64 bits,
//    wrapping round either way: 11 microseconds before 0001-01-01 00:00:00.000010 is 2^64 - 1, which sceRtcSetTick
//    shows as the year 60267 (its year a 16-bit field);
//  - months and years move the date, keeping its time of day, its day of the month cut to the new month's last
//    (2012-02-29 and a year is 2013-02-28); a date that would leave the years 1 to 9999 leaves the destination as it
//    was, and still returns 0.
//Tekken 6 asks for seconds and minutes.

//(destination, source): the source tick moved on by amount, into the destination; 0.
auto Kernel::rtcTickAdd(u64 amount) -> void {
  u32 destination = arg(0), source = arg(1);
  if(!memory.reaches(destination, 8) || !memory.reaches(source, 8)) return result(ErrorInvalidPointer);
  u64 tick = memory.read(4, source) | u64(memory.read(4, source + 4)) << 32;
  tick += amount;
  memory.write(4, destination, u32(tick));
  memory.write(4, destination + 4, u32(tick >> 32));
  result(0);
}

//The same, a number of months on in the calendar.
auto Kernel::rtcTickAddMonths(s64 months) -> void {
  u32 destination = arg(0), source = arg(1);
  if(!memory.reaches(destination, 8) || !memory.reaches(source, 8)) return result(ErrorInvalidPointer);
  constexpr u64 Day = 86'400'000'000;
  u64 tick = memory.read(4, source) | u64(memory.read(4, source + 4)) << 32;
  s64 year;
  u32 month, day;
  dateFromYearOne(s64(tick / Day), year, month, day);
  s64 total = year * 12 + (month - 1) + months;
  s64 newYear = total >= 0 ? total / 12 : (total - 11) / 12;
  u32 newMonth = u32(total - newYear * 12) + 1;
  if(newYear < 1 || newYear > 9999) return result(0);
  static constexpr u8 MonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (newYear % 4 == 0 && newYear % 100) || newYear % 400 == 0;
  day = std::min<u32>(day, MonthDays[newMonth - 1] + (newMonth == 2 && leap));
  tick = u64(daysFromYearOne(newYear, newMonth, day)) * Day + tick % Day;
  memory.write(4, destination, u32(tick));
  memory.write(4, destination + 4, u32(tick >> 32));
  result(0);
}

auto Kernel::sceRtcTickAddTicks() -> void { rtcTickAdd(arg(2) | u64(arg(3)) << 32); }
auto Kernel::sceRtcTickAddMicroseconds() -> void { rtcTickAdd(arg(2) | u64(arg(3)) << 32); }
auto Kernel::sceRtcTickAddSeconds() -> void { rtcTickAdd((arg(2) | u64(arg(3)) << 32) * 1'000'000); }
auto Kernel::sceRtcTickAddMinutes() -> void { rtcTickAdd((arg(2) | u64(arg(3)) << 32) * 60'000'000); }
auto Kernel::sceRtcTickAddHours() -> void { rtcTickAdd(u64(s64(s32(arg(2)))) * 3'600'000'000); }
auto Kernel::sceRtcTickAddDays() -> void { rtcTickAdd(u64(s64(s32(arg(2)))) * 86'400'000'000); }
auto Kernel::sceRtcTickAddWeeks() -> void { rtcTickAdd(u64(s64(s32(arg(2)))) * 604'800'000'000); }
auto Kernel::sceRtcTickAddMonths() -> void { rtcTickAddMonths(s32(arg(2))); }
auto Kernel::sceRtcTickAddYears() -> void { rtcTickAddMonths(s64(s32(arg(2))) * 12); }
