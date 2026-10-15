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

//Dates as text: (where to put it, the tick, a time zone in minutes east of UTC), the LocalTime forms without the zone.
//The tick (microseconds since 0001-01-01 00:00:00 UTC, as sceRtcGetCurrentTick gives one) moved by the zone, written
//as RFC 2822 writes a date or RFC 3339 does, then a NUL; 0. As pspautotests' rtc/convert recorded with no zone:
//2012-01-30 02:12:15 and 900 microseconds is "Mon, 30 Jan 2012 02:12:15 +0000" and "2012-01-30T02:12:15.00Z",
//2010-09-20 07:12:15 and 500 microseconds alike, tick 0 "0001-01-01T00:00:00.00Z". Before the test called them
//rightly, it gave a 16-byte date as the text's address (the text running on into the buffer it then printed), that
//buffer as the tick and 256 as the zone, so each call's tick was the previous text's bytes 16 to 23: its four calls
//printed " 04:16:00 +0416", " 14:48:07 +0416", ":15.71+04:16" and ":07.29+04:16", which those ticks give exactly:
//the time moved by the zone, written as hours and minutes (RFC 3339's with a colon, UTC as Z), the seconds'
//hundredths cut (the last tick's 299,386 microseconds are .29), and 16 characters before the text's 17th (tick 0's
//"Mon, 01 Jan 0001": the day in two digits, the year in four), the year four characters for the years 110124 and
//109990 too. Which four, and such a date's day of the week, fell in the 16 bytes: chosen, the last four digits of
//the year sceRtcSetTick gives (its 16 bits; uOFW's rtc builds its local clock's date with sceRtcSetTick), and the
//week's day of the tick's own day. Chosen too: a zone west of UTC as '-' and its size, a zone of 100 hours or more
//in more digits. The LocalTime forms take the zone the system's settings give (localZone()), as uOFW's rtc does.
//Refused, with nothing written: an address with its top bit set, the kernel's, which uOFW's rtc refuses a program
//(PRIV_REQUIRED from RFC3339LocalTime, whose body it gives as checked against the firmware's code, INVALID_POINTER
//from RFC2822LocalTime, whose body it doesn't mark so; the two with a zone, which it hasn't worked out, chosen as the
//module's other functions refuse one: PRIV_REQUIRED); and an address the program's memory doesn't hold, where a PSP
//would fault, INVALID_POINTER, as the tick arithmetic's (chosen). MediEvil Resurrection writes the time it saves at
//into the save's details so (RFC2822LocalTime).
auto Kernel::rtcFormat(bool rfc3339, s32 zone, u32 refused) -> void {
  static constexpr const char* Days[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};  //(0001-01-01 a Monday)
  static constexpr const char* Months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct",
                                           "Nov", "Dec"};
  u32 text = arg(0), pointer = arg(1);
  if((text | pointer) & 0x8000'0000) return result(refused);
  if(!memory.reaches(pointer, 8)) return result(ErrorInvalidPointer);
  u64 tick = memory.read(4, pointer) | u64(memory.read(4, pointer + 4)) << 32;
  tick += u64(s64(zone) * 60'000'000);
  u64 seconds = tick / 1'000'000, days = seconds / 86'400;
  s64 year;
  u32 month, day;
  dateFromYearOne(s64(days), year, month, day);
  u32 hour = seconds / 3'600 % 24, minute = seconds / 60 % 60, second = seconds % 60;
  u32 size = u32(std::abs(s64(zone))), hundredths = tick % 1'000'000 / 10'000, digits = u16(year) % 10'000;
  char sign = zone < 0 ? '-' : '+', out[128];
  int length;
  if(rfc3339) {
    length = std::snprintf(out, sizeof(out), "%04u-%02u-%02uT%02u:%02u:%02u.%02u", digits, month, day, hour,
                           minute, second, hundredths);
    if(!zone) length += std::snprintf(out + length, sizeof(out) - length, "Z");
    else length += std::snprintf(out + length, sizeof(out) - length, "%c%02u:%02u", sign, size / 60, size % 60);
  } else {
    length = std::snprintf(out, sizeof(out), "%s, %02u %s %04u %02u:%02u:%02u %c%02u%02u", Days[days % 7], day,
                           Months[month - 1], digits, hour, minute, second, sign, size / 60, size % 60);
  }
  if(!memory.reaches(text, length + 1)) return result(ErrorInvalidPointer);
  memory.copyIn(text, out, length + 1);
  result(0);
}

auto Kernel::sceRtcFormatRFC2822() -> void { rtcFormat(false, s32(arg(2)), ErrorPrivilegeRequired); }
auto Kernel::sceRtcFormatRFC2822LocalTime() -> void { rtcFormat(false, localZone(), ErrorInvalidPointer); }
auto Kernel::sceRtcFormatRFC3339() -> void { rtcFormat(true, s32(arg(2)), ErrorPrivilegeRequired); }
auto Kernel::sceRtcFormatRFC3339LocalTime() -> void { rtcFormat(true, localZone(), ErrorPrivilegeRequired); }
