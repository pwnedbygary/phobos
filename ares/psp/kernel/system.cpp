//Leaving the program, clocks, and the odds and ends a C library's start-up asks the system for.

//The program is over: back to the PSP's menu (here: the system stops running it).
auto Kernel::sceKernelExitGame() -> void {
  exited = true;
  switchTo(nullptr);
}

//(exit status, argument size, argument): the module stops and unloads itself, which for a program on its own is
//the same as leaving.
auto Kernel::sceKernelSelfStopUnloadModule() -> void {
  exited = true;
  switchTo(nullptr);
}

//(exit status, argument size, argument, where to put module_stop's status, options): the same, as later SDKs'
//start-up code calls it (a C++ program's abort() ends here too). The program has no module_stop to run: it's over.
auto Kernel::sceKernelStopUnloadSelfModuleWithStatus() -> void {
  exited = true;
  switchTo(nullptr);
}

//(which setting, where to put it): the system's settings (psputility_sysparam.h). Language 1 is English; button
//swap 1 means the cross button confirms, as outside Japan; anything else reads 0.
auto Kernel::sceUtilityGetSystemParamInt() -> void {
  u32 which = arg(0), value = 0;
  if(which == 8) value = 1;  //PSP_SYSTEMPARAM_ID_INT_LANGUAGE
  if(which == 9) value = 1;  //PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP
  if(arg(1)) memory.write(4, arg(1), value);
  result(0);
}

//No network yet.
auto Kernel::sceNetInetUnavailable() -> void {
  result(0xffff'ffff);
}

//A 64-bit result, in v0 (low half) and v1.
auto Kernel::result64(u64 value) -> void {
  cpu.ipu.r[2] = u32(value);
  cpu.ipu.r[3] = u32(value >> 32);
}

//Clocks. The PSP's own: microseconds since it started (the CPU's cycles at 333 MHz). The date: the host's when the
//program started, moving on with the PSP's own clock.
auto Kernel::sceKernelGetSystemTimeWide() -> void {
  result64(cycles / (CPUFrequency / 1'000'000));
}

//(where): the same, as a SceKernelSysClock (two words, low first).
auto Kernel::sceKernelGetSystemTime() -> void {
  u64 time = cycles / (CPUFrequency / 1'000'000);
  memory.write(4, arg(0), u32(time));
  memory.write(4, arg(0) + 4, u32(time >> 32));
  result(0);
}

//(timeval, timezone): seconds and microseconds since 1970; the time zone is left alone.
auto Kernel::sceKernelLibcGettimeofday() -> void {
  u64 now = startTime + cycles / (CPUFrequency / 1'000'000);
  if(arg(0)) {
    memory.write(4, arg(0), u32(now / 1'000'000));
    memory.write(4, arg(0) + 4, u32(now % 1'000'000));
  }
  result(0);
}

//(where, or 0): seconds since 1970.
auto Kernel::sceKernelLibcTime() -> void {
  u32 seconds = u32((startTime + cycles / (CPUFrequency / 1'000'000)) / 1'000'000);
  if(arg(0)) memory.write(4, arg(0), seconds);
  result(seconds);
}

//(where): the real-time clock's tick, a microsecond count from the start of year 1 (as the PSP's sceRtc counts).
auto Kernel::sceRtcGetCurrentTick() -> void {
  static constexpr u64 From1To1970 = 62'135'596'800ull * 1'000'000;  //microseconds from 0001-01-01 to 1970-01-01
  u64 tick = From1To1970 + startTime + cycles / (CPUFrequency / 1'000'000);
  memory.write(4, arg(0), u32(tick));
  memory.write(4, arg(0) + 4, u32(tick >> 32));
  result(0);
}

auto Kernel::sceRtcGetTickResolution() -> void {
  result(1'000'000);
}

//The days from 0001-01-01 to the given date, in the Gregorian calendar carried back (as the PSP's ticks count):
//years shifted to start in March, so a leap day falls at a year's end. This is Howard Hinnant's days_from_civil, from
//his public "chrono-Compatible Low-Level Date Algorithms", counting from year 1 where his counts from 1970.
static auto daysFromYearOne(s64 year, u32 month, u32 day) -> s64 {
  year -= month <= 2;
  s64 era = (year >= 0 ? year : year - 399) / 400;
  u32 yearOfEra = u32(year - era * 400);
  u32 monthOfYear = month > 2 ? month - 3 : month + 9;  //March 0 ... February 11
  u32 dayOfYear = (153 * monthOfYear + 2) / 5 + day - 1;
  u32 dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146'097 + dayOfEra - 306;  //day 0 of era 0 is 0000-03-01, 306 days before 0001-01-01
}

//(date, where to put its tick): a ScePspDateTime (psprtc.h: year, month, day, hour, minute, second as 16-bit numbers,
//then microseconds) as a tick, microseconds since 0001-01-01. A date that can't be is refused.
auto Kernel::sceRtcGetTick() -> void {
  u32 date = arg(0);
  u32 year = memory.read(2, date), month = memory.read(2, date + 2), day = memory.read(2, date + 4);
  u32 hour = memory.read(2, date + 6), minute = memory.read(2, date + 8), second = memory.read(2, date + 10);
  u32 microsecond = memory.read(4, date + 12);
  static constexpr u8 MonthDays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if(!year || !month || month > 12 || !day || day > MonthDays[month - 1] || hour > 23 || minute > 59 || second > 59
  || microsecond > 999'999) return result(ErrorInvalidValue);
  bool leap = (year % 4 == 0 && year % 100) || year % 400 == 0;
  if(month == 2 && day == 29 && !leap) return result(ErrorInvalidValue);
  u64 seconds = u64(daysFromYearOne(year, month, day)) * 86'400 + hour * 3'600 + minute * 60 + second;
  u64 tick = seconds * 1'000'000 + microsecond;
  memory.write(4, arg(1), u32(tick));
  memory.write(4, arg(1) + 4, u32(tick >> 32));
  result(0);
}

//(first tick, second tick): -1, 0 or 1 as the first is earlier, the same or later.
auto Kernel::sceRtcCompareTick() -> void {
  u64 first = memory.read(4, arg(0)) | u64(memory.read(4, arg(0) + 4)) << 32;
  u64 second = memory.read(4, arg(1)) | u64(memory.read(4, arg(1) + 4)) << 32;
  result(first < second ? -1 : first > second ? 1 : 0);
}

//C's clock(): the program's time so far, in microseconds (the PSP counts it from power on).
auto Kernel::sceKernelLibcClock() -> void {
  result(u32(cycles / (CPUFrequency / 1'000'000)));
}

//(SceKernelSysClock, where to put seconds, where to put microseconds): a 64-bit count of microseconds split.
auto Kernel::sceKernelSysClock2USec() -> void {
  u64 clock = memory.read(4, arg(0)) | u64(memory.read(4, arg(0) + 4)) << 32;
  if(arg(1)) memory.write(4, arg(1), u32(clock / 1'000'000));
  if(arg(2)) memory.write(4, arg(2), u32(clock % 1'000'000));
  result(0);
}

//(the count, a 64-bit number in a0 and a1; where to put seconds; where to put microseconds): the same.
auto Kernel::sceKernelSysClock2USecWide() -> void {
  u64 clock = arg(0) | u64(arg(1)) << 32;
  if(arg(2)) memory.write(4, arg(2), u32(clock / 1'000'000));
  if(arg(3)) memory.write(4, arg(3), u32(clock % 1'000'000));
  result(0);
}

//The Mersenne Twister (MT19937, as Matsumoto and Nishimura describe it), its state in the program's memory: a
//SceKernelUtilsMt19937Context (psputils.h), how many of its 624 words have been handed out, then the words.
//(context, seed): the words seeded, none handed out.
auto Kernel::sceKernelUtilsMt19937Init() -> void {
  u32 context = arg(0), word = arg(1);
  memory.write(4, context + 4, word);
  for(u32 n = 1; n < 624; n++) {
    word = 1'812'433'253 * (word ^ word >> 30) + n;
    memory.write(4, context + 4 + n * 4, word);
  }
  memory.write(4, context, 624);
  result(0);
}

//(context): the next number. When all 624 words are handed out, they're stirred into the next 624.
auto Kernel::sceKernelUtilsMt19937UInt() -> void {
  u32 context = arg(0), index = memory.read(4, context);
  auto state = [&](u32 n) { return memory.read(4, context + 4 + n * 4); };
  if(index >= 624) {
    for(u32 n = 0; n < 624; n++) {
      u32 y = (state(n) & 0x8000'0000) | (state((n + 1) % 624) & 0x7fff'ffff);
      u32 next = state((n + 397) % 624) ^ y >> 1 ^ (y & 1 ? 0x9908'b0df : 0);
      memory.write(4, context + 4 + n * 4, next);
    }
    index = 0;
  }
  u32 y = state(index);
  y ^= y >> 11;
  y ^= y << 7 & 0x9d2c'5680;
  y ^= y << 15 & 0xefc6'0000;
  y ^= y >> 18;
  memory.write(4, context, index + 1);
  result(y);
}

//(format, ...): the kernel's printf, to the program's output: %d, %i, %u, %x, %X, %p, %c, %s and %%, with a width
//(zeros first to pad with zeros); a long's l is skipped, a 64-bit number isn't read.
auto Kernel::sceKernelPrintf() -> void {
  std::string format = memory.readString(arg(0), 4_KiB), text;
  u32 next = 1;
  for(size_t n = 0; n < format.size(); n++) {
    if(format[n] != '%' || n + 1 == format.size()) { text += format[n]; continue; }
    std::string spec = "%";
    while(++n < format.size() && strchr("0123456789-", format[n])) spec += format[n];
    while(n < format.size() && format[n] == 'l') n++;
    if(n == format.size()) break;
    char kind = format[n], piece[64];
    u32 value = kind == '%' || next >= 8 ? 0 : arg(next++);
    if(kind == '%') text += '%';
    else if(kind == 's') {
      std::string string = memory.readString(value, 1_KiB);
      std::vector<char> padded(string.size() + 64);
      std::snprintf(padded.data(), padded.size(), (spec + "s").c_str(), string.c_str());
      text += padded.data();
    }
    else if(kind == 'c') text += char(value);
    else if(strchr("diuxXp", kind)) {
      if(kind == 'p') kind = 'x';
      std::snprintf(piece, sizeof(piece), (spec + kind).c_str(), kind == 'd' || kind == 'i' ? s32(value) : value);
      text += piece;
    } else text += spec + kind;
  }
  if(output) output(text);
  result(0);
}

//(value): the debug LEDs some PSPs have; nothing to light.
auto Kernel::sceKernelSetGPO() -> void {
  result(0);
}

//The wireless LAN: its switch is off (0), and its address is a made-up one (a locally administered MAC).
auto Kernel::sceWlanGetSwitchState() -> void {
  result(0);
}

auto Kernel::sceWlanGetEtherAddr() -> void {
  static constexpr u8 Address[6] = {0x02, 0x00, 0x00, 0x50, 0x53, 0x50};  //"PSP" in the last three
  if(!memory.copyIn(arg(0), Address, 6)) return result(ErrorInvalidPointer);
  result(0);
}

//(language, button that confirms): what the HOME menu's on-screen texts use; there's no HOME menu to tell.
auto Kernel::sceImposeSetLanguageMode() -> void {
  result(0);
}

//(destination, source, size): a copy by the DMA controller, done at once. Nothing, or memory that isn't there, is
//refused (PPSSPP's notes).
auto Kernel::sceDmacMemcpy() -> void {
  u32 destination = arg(0), source = arg(1), size = arg(2);
  if(!size) return result(ErrorInvalidSize);
  if(!memory.reaches(destination, size) || !memory.reaches(source, size)) return result(ErrorInvalidPointer);
  std::vector<u8> bytes(size);
  memory.copyOut(bytes.data(), source, size);
  memory.copyIn(destination, bytes.data(), size);
  result(0);
}

auto Kernel::sceKernelCacheUnneeded() -> void {
  result(0);
}
