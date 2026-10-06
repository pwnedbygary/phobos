//Power (scePower and sceSuspendForUser): the battery, the clocks, callbacks for the power switch, and the volatile
//memory the system lends a game.
//
//The PSP here is always on its charger with a full battery: the power switch is never touched, so its callbacks are
//told that much once, as they're registered (as the PSP tells them: PPSSPP's notes), and never again.
//
//The clocks: a PSP's CPU runs at 222 MHz unless a game asks for more (up to 333), its bus at half the PLL that drives
//both. Games set them and read them back. The emulated CPU's time always counts 333 MHz cycles, one per instruction,
//whatever is asked: a game asking for 222 MHz gets its instructions done faster than a PSP would, which games wait
//out by the vertical blank. The values asked for are kept to be read back.
//
//Volatile memory: the 4 MiB from 0x08400000, which the system keeps for itself but lends a game that asks
//(sceKernelVolatileMemLock), one at a time.

namespace {
  //psppower.h's PSP_POWER_CB_* bits: on the charger, a battery in, and its charge (a percentage) in the low 7 bits
  constexpr u32 PowerOnCharger = 0x1000, PowerBattery = 0x80, PowerBatteryFull = 100;
  //the PLL runs at one of four speeds; a speed asked for is taken to the next of them (PPSSPP's notes)
  auto pllSpeed(u32 mhz) -> u32 {
    if(mhz <= 190) return 190;
    if(mhz <= 222) return 222;
    if(mhz <= 266) return 266;
    return 333;
  }
}

//(slot 0-15, or -1 for the first free one; callback): the callback is told of the power switch from now on, and of
//the battery at once. The slot taken (with -1), or 0. Slots 16-31 are the system's.
auto Kernel::scePowerRegisterCallback() -> void {
  s32 slot = s32(arg(0));
  u32 callback = arg(1);
  if(slot < -1 || slot >= 32) return result(ErrorInvalidIndex);
  if(slot >= 16) return result(ErrorPrivilegeRequired);
  if(!callback) return result(ErrorInvalidID);
  u32 taken = 0;
  if(slot == -1) {
    while(taken < 16 && powerState.callbacks[taken]) taken++;
    if(taken == 16) return result(ErrorOutOfMemory);
    result(taken);
  } else {
    if(powerState.callbacks[slot]) return result(ErrorAlready);
    taken = slot;
    result(0);
  }
  powerState.callbacks[taken] = callback;
  notifyCallback(callback, PowerOnCharger | PowerBattery | PowerBatteryFull);
  reschedule();
}

//(slot)
auto Kernel::scePowerUnregisterCallback() -> void {
  s32 slot = s32(arg(0));
  if(slot < 0 || slot >= 32) return result(ErrorInvalidIndex);
  if(slot >= 16) return result(ErrorPrivilegeRequired);
  if(!powerState.callbacks[slot]) return result(ErrorNotFound);
  powerState.callbacks[slot] = 0;
  result(0);
}

//The battery: always there, always full, on the charger and not charging.
auto Kernel::scePowerIsPowerOnline() -> void { result(1); }
auto Kernel::scePowerIsBatteryExist() -> void { result(1); }
auto Kernel::scePowerIsBatteryCharging() -> void { result(0); }
auto Kernel::scePowerGetBatteryChargingStatus() -> void { result(0); }
auto Kernel::scePowerIsLowBattery() -> void { result(0); }
auto Kernel::scePowerGetBatteryLifePercent() -> void { result(PowerBatteryFull); }
auto Kernel::scePowerGetBatteryLifeTime() -> void { result(0); }  //minutes left: on the charger, none to count

//Tells the system the player is still there, so it doesn't dim the screen or sleep (it never does here).
auto Kernel::scePowerTick() -> void {
  result(0);
}

//(PLL, CPU, bus, in MHz): the PLL is set to the next of its four speeds, the bus to half of it. Out of range:
//refused.
//Changing the PLL takes the PSP a while (PPSSPP's measurements: 150 ms, but 15.7 between 190 and 222 MHz and 16.6
//between 266 and 333), in which the calling thread waits.
auto Kernel::scePowerSetClockFrequency() -> void {
  u32 pll = arg(0), speed = arg(1), bus = arg(2);
  if(pll < 19 || pll < speed || pll > 333) return result(ErrorInvalidValue);
  if(!speed || speed > 333) return result(ErrorInvalidValue);
  if(!bus || bus > 166) return result(ErrorInvalidValue);
  u32 before = powerState.pll;
  powerState.pll = pllSpeed(pll);
  powerState.bus = powerState.pll / 2;
  powerState.cpu = speed;
  result(0);
  if(powerState.pll == before || interrupting || !current) return;
  u32 microseconds = 150'000;
  if(std::min(before, powerState.pll) == 190 && std::max(before, powerState.pll) == 222) microseconds = 15'700;
  if(std::min(before, powerState.pll) == 266 && std::max(before, powerState.pll) == 333) microseconds = 16'600;
  block(Wait::Delay, 0, cycles + u64(microseconds) * (CPUFrequency / 1'000'000));
}

//(MHz): the CPU alone, at most the PLL's speed.
auto Kernel::scePowerSetCpuClockFrequency() -> void {
  if(!arg(0) || arg(0) > 333 || arg(0) > powerState.pll) return result(ErrorInvalidValue);
  powerState.cpu = arg(0);
  result(0);
}

//(MHz): the bus follows the PLL whatever is asked (half its speed); only the range is checked.
auto Kernel::scePowerSetBusClockFrequency() -> void {
  if(!arg(0) || arg(0) > 111) return result(ErrorInvalidValue);
  result(0);
}

auto Kernel::scePowerGetCpuClockFrequency() -> void { result(powerState.cpu); }
auto Kernel::scePowerGetBusClockFrequency() -> void { result(powerState.bus); }
auto Kernel::scePowerGetPllClockFrequencyInt() -> void { result(powerState.pll); }

//A float result goes in the FPU's f0, as the PSP's C compiler returns floats.
auto Kernel::resultFloat(float value) -> void {
  u32 bits;
  memcpy(&bits, &value, sizeof(bits));
  cpu.fpu.r[0] = bits;
}

auto Kernel::scePowerGetCpuClockFrequencyFloat() -> void { resultFloat(float(powerState.cpu)); }
auto Kernel::scePowerGetBusClockFrequencyFloat() -> void { resultFloat(float(powerState.bus)); }
auto Kernel::scePowerGetPllClockFrequencyFloat() -> void { resultFloat(float(powerState.pll)); }

//sceSuspendForUser. (flags): tells the system the player is still there; (type 0): holds off, and lets go of, the
//system's suspending the PSP (which never happens here). Other types are refused.
auto Kernel::sceKernelPowerTick() -> void {
  result(0);
}

auto Kernel::sceKernelPowerLock() -> void {
  result(arg(0) ? ErrorInvalidMode : 0);
}

auto Kernel::sceKernelPowerUnlock() -> void {
  result(arg(0) ? ErrorInvalidMode : 0);
}

//(type 0, where to put its address, where to put its size): borrows the volatile memory, waiting while it's lent,
//those waiting served in the order they came: pspautotests' power/volatile/lock recorded three threads of priorities
//0x31, 0x33 and 0x32 served in that order. As lock recorded, a type but 0 is refused with the outputs left alone;
//else the address and size are written first, and a thread that may not wait (in an interrupt handler, with
//interrupts or dispatching held off) is refused without borrowing, whether the memory is lent or not. (No thread
//running, as when a test calls the kernel itself: lent already, it's refused as TryLock refuses.) Burnout Dominator
//borrows it so.
auto Kernel::sceKernelVolatileMemLock() -> void {
  if(arg(0)) return result(ErrorInvalidMode);
  if(arg(1)) memory.write(4, arg(1), 0x0840'0000);
  if(arg(2)) memory.write(4, arg(2), 0x0040'0000);
  if(!mayWait()) return;
  if(!powerState.volatileLocked) {
    powerState.volatileLocked = true;
    return result(0);
  }
  if(!current) return result(ErrorVolatileMemoryInUse);
  result(0);
  current->readySince = ++readySequence;  //its place in line
  block(Wait::Volatile, 0, 0);
}

//(type 0, where to put its address, where to put its size): borrows the volatile memory, or fails at once if it's
//lent already (ErrorVolatileMemoryInUse).
auto Kernel::sceKernelVolatileMemTryLock() -> void {
  if(arg(0)) return result(ErrorInvalidMode);
  if(powerState.volatileLocked) return result(ErrorVolatileMemoryInUse);
  if(arg(1)) memory.write(4, arg(1), 0x0840'0000);
  if(arg(2)) memory.write(4, arg(2), 0x0040'0000);
  powerState.volatileLocked = true;
  result(0);
}

//(type 0): gives it back; a thread waiting for it, the first to have come, borrows it then. Giving back what isn't
//lent is refused as a semaphore's overflow is (pspautotests' power/volatile/lock recorded it).
auto Kernel::sceKernelVolatileMemUnlock() -> void {
  if(arg(0)) return result(ErrorInvalidMode);
  if(!powerState.volatileLocked) return result(ErrorSemaphoreOverflow);
  result(0);
  Thread* next = nullptr;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != Wait::Volatile) continue;
    if(!next || thread->readySince < next->readySince) next = thread.get();
  }
  if(!next) {
    powerState.volatileLocked = false;
    return;
  }
  ready(*next, 0);
  reschedule();
}
