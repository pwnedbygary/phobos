//Leaving the program, clocks, and the odds and ends a C library's start-up asks the system for.

//The program is over: back to the PSP's menu (here: the system stops running it).
auto Kernel::sceKernelExitGame() -> void {
  exited = true;
  switchTo(nullptr);
}

//(path, parameters or 0: a SceKernelLoadExecParam, psploadexec.h: its size, the argument's length, where the
//argument is, a key): the program ends, and the one at path starts in its place, as a game made of several programs
//starts the next (WipEout Portable Collection starts each of its games so, disc0:/PSP_GAME/USRDIR/ELF/FX300.BIN with
//no parameters, and had gone back to its menu while the function was missing). Its first thread gets the argument
//given, or none, or with no parameters its path, as a program the system starts gets. Like pspautotests'
//modules/loadexec/loader, whose "[2]" after the call never printed, it doesn't return: the calling thread stops, and
//the kernel's loop puts the new program in as it next goes round (loadExec()). What can be checked is checked first,
//the old program left running when the new one can't start: a file there isn't (its error), one that isn't a program
//or that can't be decrypted (ILLEGAL_OBJECT, UNSUPPORTED_PRX_TYPE: chosen), an argument over 4 KiB (ILLEGAL_SIZE:
//chosen). From an interrupt handler, ILLEGAL_CONTEXT.
auto Kernel::sceKernelLoadExec() -> void {
  if(interrupting) return result(ErrorIllegalContext);
  std::string path = memory.readString(arg(0), 256);
  u32 parameters = arg(1);
  std::vector<u8> argument;
  if(parameters && memory.reaches(parameters, 12)) {
    u32 length = memory.read(4, parameters + 4), at = memory.read(4, parameters + 8);
    if(length > 4_KiB) return result(ErrorIllegalSize);
    if(length && at) {
      argument.resize(length);
      if(!memory.copyOut(argument.data(), at, length)) return result(ErrorIllegalAddress);
    }
  }
  std::vector<u8> file;
  if(u32 error = readWhole(path, file)) return result(error);
  const u8* data = file.data();
  u64 size = file.size();
  std::vector<u8> program;
  if(size >= 4 && !memcmp(data, "\0PBP", 4)) {
    program = std::move(file);  //(start() finds the program inside)
  } else {
    unwrapProgram(data, size);
    std::vector<u8> decrypted;
    if(encryptedProgram(data, size)) {
      if(auto why = decryptProgram(data, size, decrypted); !why.empty()) {
        note("sceKernelLoadExec: can't start " + path + ": " + why);
        return result(ErrorUnsupportedPrxType);
      }
      data = decrypted.data(), size = decrypted.size();
    }
    u32 low = 0, high = 0;
    bool relocatable = false;
    if(!Loader::extent(data, size, low, high, relocatable)) return result(ErrorIllegalObject);
    program.assign(data, data + size);
  }
  exec.pending = true;
  exec.path = path;
  exec.program = std::move(program);
  exec.argument = std::move(argument);
  exec.pathArgument = !parameters;
  switchTo(nullptr);  //the caller stops here (the CPU's go ends with it)
}

//The program sceKernelLoadExec asked for, in the old one's place: everything of the old program goes, as at power on
//(its threads, memory and modules, open files, the calls into it, sound, the GE's lists, the clock), the user
//partition is cleared, and the new program starts as the system starts one. The devices, the disc and the system
//fonts stay the system's. One that can't start after all ends the program, noted.
auto Kernel::loadExec() -> void {
  auto request = std::move(exec);
  power();
  u64 top = 0x0800'0000 + u64(memory.ram.size());
  memory.fill(UserMemory, 0, top > UserMemory ? u32(top - UserMemory) : 0);
  std::string error;
  auto given = request.pathArgument ? nullptr : &request.argument;
  if(!start(request.program.data(), request.program.size(), request.path, error, given)) {
    note("sceKernelLoadExec: can't start " + request.path + ": " + error);
    power();
    exited = true;
  }
}

//(exit status, argument size, argument): the module holding the code that called stops and unloads itself, its
//calling thread ending (modules.cpp, unloadSelf()); the program itself does so as any module does.
auto Kernel::sceKernelSelfStopUnloadModule() -> void {
  unloadSelf(s32(arg(0)), arg(1), arg(2), 0);
}

//(exit status, argument size, argument, where to put module_stop's status, options): the same, as later SDKs' code
//calls it (Gunhound EX does; a C++ program's abort() ends here too), the options going to module_stop's thread, the
//program itself unloading as any module does. module_stop's status isn't written: nothing waits for it
//(unloadSelf()).
auto Kernel::sceKernelStopUnloadSelfModuleWithStatus() -> void {
  unloadSelf(s32(arg(0)), arg(1), arg(2), arg(4));
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

namespace {
  constexpr u64 From1To1970 = 62'135'596'800ull * 1'000'000;  //microseconds from 0001-01-01 to 1970-01-01
}

//(where): the real-time clock's tick, a microsecond count from the start of year 1 (as the PSP's sceRtc counts).
auto Kernel::sceRtcGetCurrentTick() -> void {
  u64 tick = From1To1970 + startTime + cycles / (CPUFrequency / 1'000'000);
  memory.write(4, arg(0), u32(tick));
  memory.write(4, arg(0) + 4, u32(tick >> 32));
  result(0);
}

auto Kernel::sceRtcGetTickResolution() -> void {
  result(1'000'000);
}

//The time the PSP has been running, in v0 and v1: here, microseconds since it started, as the system time. uOFW's rtc
//names 0x011f03c1 and Sony's spelling 0x029ca3b3 (sceRtcGetAccumlativeTime) as one function, its body not yet worked
//out beyond reading the system time; no pspautotests program calls it (chosen, the unit and what it counts from).
auto Kernel::sceRtcGetAccumulativeTime() -> void {
  result64(cycles / (CPUFrequency / 1'000'000));
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

//The other way: the date a count of days from 0001-01-01 falls on, by Howard Hinnant's civil_from_days, from the same
//public algorithms.
static auto dateFromYearOne(s64 days, s64& year, u32& month, u32& day) -> void {
  s64 shifted = days + 306;  //days since 0000-03-01
  s64 era = (shifted >= 0 ? shifted : shifted - 146'096) / 146'097;
  u32 dayOfEra = u32(shifted - era * 146'097);
  u32 yearOfEra = (dayOfEra - dayOfEra / 1'460 + dayOfEra / 36'524 - dayOfEra / 146'096) / 365;
  u32 dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  u32 monthOfYear = (5 * dayOfYear + 2) / 153;  //March 0 ... February 11
  day = dayOfYear - (153 * monthOfYear + 2) / 5 + 1;
  month = monthOfYear < 10 ? monthOfYear + 3 : monthOfYear - 9;
  year = yearOfEra + era * 400 + (month <= 2);
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

//(where to put the date, its tick): the tick as a date, as pspautotests' rtc/convert recorded (835072 is 0001-01-01
//and 835072 microseconds; 62135596800000000 is 1970-01-01; a date's tick comes back as the date). Peace Walker asks.
auto Kernel::sceRtcSetTick() -> void {
  u32 date = arg(0), pointer = arg(1);
  if(!memory.reaches(date, 16) || !memory.reaches(pointer, 8)) return result(ErrorInvalidPointer);
  u64 tick = memory.read(4, pointer) | u64(memory.read(4, pointer + 4)) << 32;
  u64 seconds = tick / 1'000'000;
  s64 year;
  u32 month, day;
  dateFromYearOne(s64(seconds / 86'400), year, month, day);
  u16 fields[6] = {u16(year), u16(month), u16(day), u16(seconds / 3'600 % 24), u16(seconds / 60 % 60),
                   u16(seconds % 60)};
  for(u32 n = 0; n < 6; n++) memory.write(2, date + n * 2, fields[n]);
  memory.write(4, date + 12, u32(tick % 1'000'000));
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

//(microseconds, where to put them as a SceKernelSysClock): the system's clock counts microseconds, so the number as
//it is, 64 bits wide.
auto Kernel::sceKernelUSec2SysClock() -> void {
  memory.write(4, arg(1), arg(0));
  memory.write(4, arg(1) + 4, 0);
  result(0);
}

//(microseconds): the same, in v0 and v1.
auto Kernel::sceKernelUSec2SysClockWide() -> void {
  result64(arg(0));
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

//(format, ...): the kernel's printf, to the program's output: %d, %i, %u, %x, %X, %p, %c, %s and %%, with flags ('-'
//to pad on the right, '0' to pad a number with zeros) and a width; a long's l is skipped, a 64-bit number isn't read.
//A field is never wider than its room, 63 characters for a number and 63 past a string's text, where it was always
//cut: the width is cut to that before the field is made, as snprintf pads a field to its full width before cutting
//it (two fields 400,000,000 wide took 129 ms, and a format full of fields 2e9 wide would hold the thread a minute).
auto Kernel::sceKernelPrintf() -> void {
  std::string format = memory.readString(arg(0), 4_KiB), text;
  u32 next = 1;
  for(size_t n = 0; n < format.size(); n++) {
    if(format[n] != '%' || n + 1 == format.size()) { text += format[n]; continue; }
    size_t start = n;
    std::string flags;
    while(++n < format.size() && (format[n] == '-' || format[n] == '0')) flags += format[n];
    u32 width = 0;
    for(; n < format.size() && format[n] >= '0' && format[n] <= '9'; n++) {
      width = std::min<u32>(width * 10 + (format[n] - '0'), 4_KiB);  //(past 4 KiB is past any field's room)
    }
    std::string spec = format.substr(start, n - start);  //as written, for a conversion there isn't
    while(n < format.size() && format[n] == 'l') n++;
    if(n == format.size()) break;
    char kind = format[n], piece[64];
    u32 value = kind == '%' || next >= 8 ? 0 : arg(next++);
    if(kind == '%') text += '%';
    else if(kind == 's') {
      std::string string = memory.readString(value, 1_KiB);
      u32 wide = std::min<u32>(width, string.size() + 63);
      std::string pad(wide > string.size() ? wide - string.size() : 0, ' ');
      text += flags.find('-') == std::string::npos ? pad + string : string + pad;
    }
    else if(kind == 'c') text += char(value);
    else if(strchr("diuxXp", kind)) {
      if(kind == 'p') kind = 'x';
      width = std::min<u32>(width, sizeof(piece) - 1);
      std::string cut = "%" + flags + (width ? std::to_string(width) : "") + kind;
      std::snprintf(piece, sizeof(piece), cut.c_str(), kind == 'd' || kind == 'i' ? s32(value) : value);
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

//The debug switches some PSPs have: none set (Peace Walker reads them).
auto Kernel::sceKernelGetGPI() -> void {
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

//(language, button that confirms): what the HOME menu's on-screen texts use (pspsdk's pspimpose.h, whose values
//aren't known: taken as the system's settings number them, psputility_sysparam.h's, 1 English and 1 the cross
//button); there's no HOME menu to tell, but they're kept to be read back.
auto Kernel::sceImposeSetLanguageMode() -> void {
  imposeLanguage = arg(0);
  imposeButton = arg(1);
  result(0);
}

//(where to put the language, where to put the button): as set, else the console's own settings (English, the cross
//button confirming, as sceUtilityGetSystemParamInt has them).
auto Kernel::sceImposeGetLanguageMode() -> void {
  if(arg(0)) memory.write(4, arg(0), imposeLanguage);
  if(arg(1)) memory.write(4, arg(1), imposeButton);
  result(0);
}

//(where to put whether it's charging, where to put the battery icon's state): pspsdk's headers name the function
//only, not its arguments or their values, and no recording shows them, so these are the natural answer with no
//battery to show: not charging (0), and the icon a full battery's (3, taken as full because the PSP's icon shows up
//to three bars; that it counts so is a guess). They agree with the power functions here: on the charger, not
//charging, the battery full.
auto Kernel::sceImposeGetBatteryIconStatus() -> void {
  if(arg(0)) memory.write(4, arg(0), 0);
  if(arg(1)) memory.write(4, arg(1), 3);
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

//A ScePspDateTime (psprtc.h: year, month, day, hour, minute and second as 16-bit numbers, then microseconds) at an
//address, from microseconds since 1970: in UTC, or in the host's time zone (local). False if it can't be written.
auto Kernel::writeDate(u32 address, u64 microseconds, bool local) -> bool {
  if(!memory.reaches(address, 16)) return false;
  std::time_t seconds = std::time_t(microseconds / 1'000'000);
  std::tm when{};
  #if defined(PLATFORM_WINDOWS)
  //Windows' thread-safe pair: the arguments the other way round, 0 for success
  if(local ? localtime_s(&when, &seconds) : gmtime_s(&when, &seconds)) return false;
  #else
  if(!(local ? localtime_r(&seconds, &when) : gmtime_r(&seconds, &when))) return false;
  #endif
  u16 fields[6] = {u16(when.tm_year + 1900), u16(when.tm_mon + 1), u16(when.tm_mday), u16(when.tm_hour),
                   u16(when.tm_min), u16(when.tm_sec)};
  for(u32 n = 0; n < 6; n++) memory.write(2, address + n * 2, fields[n]);
  memory.write(4, address + 12, u32(microseconds % 1'000'000));
  return true;
}

//(where to put the date, the time zone in minutes east of UTC): the date and time now, in that time zone.
auto Kernel::sceRtcGetCurrentClock() -> void {
  s64 now = s64(startTime + cycles / (CPUFrequency / 1'000'000)) + s64(s32(arg(1))) * 60'000'000;
  result(writeDate(arg(0), u64(std::max<s64>(now, 0)), false) ? 0 : ErrorInvalidPointer);
}

//(where to put the date): the date and time now, the player's (the host's time zone).
auto Kernel::sceRtcGetCurrentClockLocalTime() -> void {
  u64 now = startTime + cycles / (CPUFrequency / 1'000'000);
  result(writeDate(arg(0), now, true) ? 0 : ErrorInvalidPointer);
}

//A ScePspDateTime's seconds since 1970, its microseconds left out.
auto Kernel::dateSeconds(u32 date) -> s64 {
  s64 days = daysFromYearOne(memory.read(2, date), memory.read(2, date + 2), memory.read(2, date + 4))
           - daysFromYearOne(1970, 1, 1);
  return days * 86'400 + memory.read(2, date + 6) * 3'600 + memory.read(2, date + 8) * 60 + memory.read(2, date + 10);
}

//(date, where to put a time_t): the date as seconds since 1970.
auto Kernel::sceRtcGetTime_t() -> void {
  if(arg(1)) memory.write(4, arg(1), u32(dateSeconds(arg(0))));
  result(0);
}

//(date, where to put a 64-bit time_t): the same, 64 bits wide, as pspautotests' rtc/convert recorded
//(2012-09-20 07:12:15.500 is 1348125135; its high word, which held 0 already, chosen). LittleBigPlanet asks.
auto Kernel::sceRtcGetTime64_t() -> void {
  u64 seconds = dateSeconds(arg(0));
  if(arg(1)) {
    memory.write(4, arg(1), u32(seconds));
    memory.write(4, arg(1) + 4, u32(seconds >> 32));
  }
  result(0);
}

//(year, month, day): the day of the week, 0 Sunday to 6 Saturday (psprtc.h says 0 is Monday, but pspautotests'
//rtc/lookup recorded 2 for 2010-04-27, a Tuesday). By Zeller's congruence, which gives the PSP's answers for the
//dates that can't be that rtc/lookup recorded too: January and February counted as the year before's 13th and
//14th months, a month of 0 or past 12 taken as it is, the days counted on from the month's start (2000-01-00 is a
//Friday, 2001-00-00 a Tuesday, 166970016-1024-00 a Wednesday). Juiced 2 asks each frame.
auto Kernel::sceRtcGetDayOfWeek() -> void {
  s64 year = s32(arg(0)), month = s32(arg(1)), day = s32(arg(2));
  if(month == 1 || month == 2) month += 12, year--;
  s64 century = year / 100, ofCentury = year % 100;
  s64 saturday0 = (day + 13 * (month + 1) / 5 + ofCentury + ofCentury / 4 + century / 4 + 5 * century) % 7;
  result(u32((saturday0 + 13) % 7));
}

//(where to put a tick): when the clock was last set, and when it was last started afresh (as when the battery came
//out): both when the PSP started here, the clock set from the host's. Kurohyou asks for both.
auto Kernel::sceRtcGetLastAdjustedTime() -> void {
  u64 tick = From1To1970 + startTime;
  if(!memory.reaches(arg(0), 8)) return result(ErrorInvalidPointer);
  memory.write(4, arg(0), u32(tick));
  memory.write(4, arg(0) + 4, u32(tick >> 32));
  result(0);
}

auto Kernel::sceRtcGetLastReincarnatedTime() -> void {
  sceRtcGetLastAdjustedTime();
}

//(date, where to put a DOS time): the date as FAT keeps one: years since 1980 in bits 25-31, the month in 21-24, the
//day in 16-20, the hour in 11-15, the minute in 5-10, half the seconds in 0-4. Years before 1980 or after 2107 don't
//fit: -1. As pspautotests' rtc/convert recorded (an hour of 24 goes in as it is).
auto Kernel::sceRtcGetDosTime() -> void {
  u32 date = arg(0), year = memory.read(2, date);
  if(year < 1980 || year > 2107) return result(0xffff'ffff);
  u32 time = (year - 1980) << 25 | memory.read(2, date + 2) << 21 | memory.read(2, date + 4) << 16
           | memory.read(2, date + 6) << 11 | memory.read(2, date + 8) << 5 | memory.read(2, date + 10) >> 1;
  if(arg(1)) memory.write(4, arg(1), time);
  result(0);
}

//(where to put the date, DOS time): the other way, its microseconds 0.
auto Kernel::sceRtcSetDosTime() -> void {
  u32 date = arg(0), time = arg(1);
  if(!memory.reaches(date, 16)) return result(ErrorInvalidPointer);
  u16 fields[6] = {u16(1980 + (time >> 25)), u16(time >> 21 & 15), u16(time >> 16 & 31), u16(time >> 11 & 31),
                   u16(time >> 5 & 63), u16((time & 31) * 2)};
  for(u32 n = 0; n < 6; n++) memory.write(2, date + n * 2, fields[n]);
  memory.write(4, date + 12, 0);
  result(0);
}

//(date, where to put a Win32 file time): the date as Windows' files keep times, in 100-nanosecond steps since
//1601-01-01. As pspautotests' rtc/convert recorded: no place to put it, INVALID_VALUE; a date before 1601 (a zeroed
//one among them) gives 0 and INVALID_VALUE; a day past its month's end counts on into the next (2005-11-31 13:01:00
//and 1 microsecond is 127779156600000010, December's first). Midnight Club 3 asks for it as it makes a profile.
auto Kernel::sceRtcGetWin32FileTime() -> void {
  u32 date = arg(0), pointer = arg(1);
  if(!pointer) return result(ErrorInvalidValue);
  u32 year = memory.read(2, date);
  u64 time = 0;
  if(year >= 1601) {
    s64 days = daysFromYearOne(year, memory.read(2, date + 2), memory.read(2, date + 4))
             - daysFromYearOne(1601, 1, 1);
    s64 seconds = days * 86'400 + memory.read(2, date + 6) * 3'600 + memory.read(2, date + 8) * 60
                + memory.read(2, date + 10);
    time = (u64(seconds) * 1'000'000 + memory.read(4, date + 12)) * 10;
  }
  memory.write(4, pointer, u32(time));
  memory.write(4, pointer + 4, u32(time >> 32));
  result(year >= 1601 ? 0 : ErrorInvalidValue);
}

//(where to put it): the console's OpenPSID, 16 bytes a PSP keeps for each console (pspopenpsid.h's PspOpenPSID);
//every Phobos PSP is the same made-up console: "PHOBOS" after a two-byte header, its last byte 1.
auto Kernel::sceOpenPSIDGetOpenPSID() -> void {
  static constexpr u8 PSID[16] = {0x10, 0x02, 'P', 'H', 'O', 'B', 'O', 'S', 0, 0, 0, 0, 0, 0, 0, 1};
  if(!memory.copyIn(arg(0), PSID, 16)) return result(ErrorInvalidPointer);
  result(0);
}

//Whether calls into the program (interrupts) are let through now: the CPU's interrupt flag, which
//sceKernelCpuSuspendIntr (or the program's own mtic) clears.
auto Kernel::sceKernelIsCpuIntrEnable() -> void {
  result(interruptsEnabled);
}

//(destination, byte, size): the C library's memset, done by the kernel; returns the destination.
auto Kernel::sceKernelMemset() -> void {
  u32 destination = arg(0), size = arg(2);
  if(size && memory.reaches(destination, size)) memory.fill(destination, u8(arg(1)), size);
  result(destination);
}

//(destination, source, size): memcpy, overlapping as memmove may; returns the destination.
auto Kernel::sceKernelMemcpy() -> void {
  u32 destination = arg(0), source = arg(1), size = arg(2);
  if(size && memory.reaches(destination, size) && memory.reaches(source, size)) {
    std::vector<u8> bytes(size);
    memory.copyOut(bytes.data(), source, size);
    memory.copyIn(destination, bytes.data(), size);
  }
  result(destination);
}
