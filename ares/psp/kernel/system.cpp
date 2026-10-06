//Leaving the program, clocks, and the odds and ends a C library's start-up asks the system for.

//The program is over: back to the PSP's menu (here: the system stops running it).
auto Kernel::sceKernelExitGame() -> void {
  exited = true;
  switchTo(nullptr);
}

//(exit status, argument size, argument): the module holding the code that called stops and unloads itself, its
//calling thread ending (modules.cpp, unloadSelf()); the program doing it is leaving.
auto Kernel::sceKernelSelfStopUnloadModule() -> void {
  unloadSelf(s32(arg(0)), arg(1), arg(2), 0);
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

auto Kernel::sceKernelCacheUnneeded() -> void {
  result(0);
}
