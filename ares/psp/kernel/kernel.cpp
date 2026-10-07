#include "kernel.hpp"
#include "../cpu/allegrex.hpp"
#include "../memory/memory.hpp"
#include "../ge/ge.hpp"

//FFmpeg's decoders, in builds that have them (codec.cpp)
#if defined(ARES_ENABLE_FFMPEG)
extern "C" {
  #include <libavcodec/avcodec.h>
  #include <libavutil/channel_layout.h>
  #include <libavutil/log.h>
  #include <libavutil/mem.h>
}
#endif

namespace ares::PlayStationPortable {

#include "threads.cpp"
#include "mutexes.cpp"
#include "interrupts.cpp"
#include "timers.cpp"
#include "events.cpp"
#include "sysmem.cpp"
#include "aes.cpp"
#include "keys.cpp"
#include "kirk.cpp"
#include "decrypt.cpp"
#include "unpack.cpp"
#include "disc.cpp"
#include "io.cpp"
#include "async.cpp"
#include "umd.cpp"
#include "ctrl.cpp"
#include "display.cpp"
#include "ge.cpp"
#include "pools.cpp"
#include "messages.cpp"
#include "audio.cpp"
#include "sas.cpp"
#include "codec.cpp"
#include "mpeg.cpp"
#include "psmf.cpp"
#include "psmfplayer.cpp"
#include "atrac.cpp"
#include "mp3.cpp"
#include "net.cpp"
#include "pgf.cpp"
#include "font.cpp"
#include "utility.cpp"
#include "power.cpp"
#include "system.cpp"
#include "modules.cpp"
#include "serialization.cpp"

Kernel::Kernel(Allegrex& cpu, Memory& memory, GE& ge)
: cpu(cpu), memory(memory), ge(ge), interruptsEnabled(cpu.scc.interrupts) {
  auto add = [&](const char* library, const char* name, auto (Kernel::*handler)() -> void) {
    functions.push_back({library, name, handler, nid(name)});
  };
  //A function whose NID isn't its name's hash: Sony gave some later functions random NIDs, and the names these go by
  //are the ones the homebrew scene gave them. Each is listed by the NID games import, as PPSSPP's tables name it.
  auto addNID = [&](const char* library, const char* name, u32 nid, auto (Kernel::*handler)() -> void) {
    functions.push_back({library, name, handler, nid});
  };
  add("ThreadManForUser",  "sceKernelCreateThread",         &Kernel::sceKernelCreateThread);
  add("ThreadManForUser",  "sceKernelStartThread",          &Kernel::sceKernelStartThread);
  add("ThreadManForUser",  "sceKernelExitThread",           &Kernel::sceKernelExitThread);
  add("ThreadManForUser",  "sceKernelExitDeleteThread",     &Kernel::sceKernelExitDeleteThread);
  add("ThreadManForUser",  "sceKernelDeleteThread",         &Kernel::sceKernelDeleteThread);
  add("ThreadManForUser",  "sceKernelGetThreadId",          &Kernel::sceKernelGetThreadId);
  add("ThreadManForUser",  "sceKernelReferThreadStatus",    &Kernel::sceKernelReferThreadStatus);
  add("ThreadManForUser",  "sceKernelDelayThread",          &Kernel::sceKernelDelayThread);
  add("ThreadManForUser",  "sceKernelDelayThreadCB",        &Kernel::sceKernelDelayThreadCB);
  add("ThreadManForUser",  "sceKernelSleepThread",          &Kernel::sceKernelSleepThread);
  add("ThreadManForUser",  "sceKernelWakeupThread",         &Kernel::sceKernelWakeupThread);
  add("ThreadManForUser",  "sceKernelCancelWakeupThread",   &Kernel::sceKernelCancelWakeupThread);
  add("ThreadManForUser",  "sceKernelReleaseWaitThread",    &Kernel::sceKernelReleaseWaitThread);
  add("ThreadManForUser",  "sceKernelWaitThreadEnd",        &Kernel::sceKernelWaitThreadEnd);
  add("ThreadManForUser",  "sceKernelWaitThreadEndCB",      &Kernel::sceKernelWaitThreadEndCB);
  add("ThreadManForUser",  "sceKernelCreateSema",           &Kernel::sceKernelCreateSema);
  add("ThreadManForUser",  "sceKernelDeleteSema",           &Kernel::sceKernelDeleteSema);
  add("ThreadManForUser",  "sceKernelSignalSema",           &Kernel::sceKernelSignalSema);
  add("ThreadManForUser",  "sceKernelWaitSema",             &Kernel::sceKernelWaitSema);
  add("ThreadManForUser",  "sceKernelWaitSemaCB",           &Kernel::sceKernelWaitSemaCB);
  add("ThreadManForUser",  "sceKernelPollSema",             &Kernel::sceKernelPollSema);
  add("ThreadManForUser",  "sceKernelCancelSema",           &Kernel::sceKernelCancelSema);
  add("ThreadManForUser",  "sceKernelReferSemaStatus",      &Kernel::sceKernelReferSemaStatus);
  add("ThreadManForUser",  "sceKernelCreateMutex",          &Kernel::sceKernelCreateMutex);
  add("ThreadManForUser",  "sceKernelDeleteMutex",          &Kernel::sceKernelDeleteMutex);
  add("ThreadManForUser",  "sceKernelLockMutex",            &Kernel::sceKernelLockMutex);
  add("ThreadManForUser",  "sceKernelLockMutexCB",          &Kernel::sceKernelLockMutexCB);
  add("ThreadManForUser",  "sceKernelTryLockMutex",         &Kernel::sceKernelTryLockMutex);
  add("ThreadManForUser",  "sceKernelUnlockMutex",          &Kernel::sceKernelUnlockMutex);
  add("ThreadManForUser",  "sceKernelCancelMutex",          &Kernel::sceKernelCancelMutex);
  add("ThreadManForUser",  "sceKernelReferMutexStatus",     &Kernel::sceKernelReferMutexStatus);
  add("ThreadManForUser",  "sceKernelSetAlarm",             &Kernel::sceKernelSetAlarm);
  add("ThreadManForUser",  "sceKernelSetSysClockAlarm",     &Kernel::sceKernelSetSysClockAlarm);
  add("ThreadManForUser",  "sceKernelCancelAlarm",          &Kernel::sceKernelCancelAlarm);
  add("ThreadManForUser",  "sceKernelReferAlarmStatus",     &Kernel::sceKernelReferAlarmStatus);
  add("ThreadManForUser",  "sceKernelCreateVTimer",         &Kernel::sceKernelCreateVTimer);
  add("ThreadManForUser",  "sceKernelDeleteVTimer",         &Kernel::sceKernelDeleteVTimer);
  add("ThreadManForUser",  "sceKernelGetVTimerBase",        &Kernel::sceKernelGetVTimerBase);
  add("ThreadManForUser",  "sceKernelGetVTimerBaseWide",    &Kernel::sceKernelGetVTimerBaseWide);
  add("ThreadManForUser",  "sceKernelGetVTimerTime",        &Kernel::sceKernelGetVTimerTime);
  add("ThreadManForUser",  "sceKernelGetVTimerTimeWide",    &Kernel::sceKernelGetVTimerTimeWide);
  add("ThreadManForUser",  "sceKernelSetVTimerTime",        &Kernel::sceKernelSetVTimerTime);
  add("ThreadManForUser",  "sceKernelSetVTimerTimeWide",    &Kernel::sceKernelSetVTimerTimeWide);
  add("ThreadManForUser",  "sceKernelStartVTimer",          &Kernel::sceKernelStartVTimer);
  add("ThreadManForUser",  "sceKernelStopVTimer",           &Kernel::sceKernelStopVTimer);
  add("ThreadManForUser",  "sceKernelSetVTimerHandler",     &Kernel::sceKernelSetVTimerHandler);
  add("ThreadManForUser",  "sceKernelSetVTimerHandlerWide", &Kernel::sceKernelSetVTimerHandlerWide);
  add("ThreadManForUser",  "sceKernelCancelVTimerHandler",  &Kernel::sceKernelCancelVTimerHandler);
  add("ThreadManForUser",  "sceKernelReferVTimerStatus",    &Kernel::sceKernelReferVTimerStatus);
  add("ThreadManForUser",  "sceKernelChangeThreadPriority", &Kernel::sceKernelChangeThreadPriority);
  add("ThreadManForUser",  "sceKernelGetThreadExitStatus",  &Kernel::sceKernelGetThreadExitStatus);
  add("ThreadManForUser",  "sceKernelTerminateThread",      &Kernel::sceKernelTerminateThread);
  add("ThreadManForUser",  "sceKernelTerminateDeleteThread", &Kernel::sceKernelTerminateDeleteThread);
  add("ThreadManForUser",  "sceKernelSuspendThread",        &Kernel::sceKernelSuspendThread);
  add("ThreadManForUser",  "sceKernelResumeThread",         &Kernel::sceKernelResumeThread);
  add("ThreadManForUser",  "sceKernelChangeCurrentThreadAttr", &Kernel::sceKernelChangeCurrentThreadAttr);
  add("ThreadManForUser",  "sceKernelGetThreadStackFreeSize", &Kernel::sceKernelGetThreadStackFreeSize);
  add("ThreadManForUser",  "sceKernelReferThreadProfiler",  &Kernel::sceKernelReferThreadProfiler);
  add("ThreadManForUser",  "sceKernelGetThreadCurrentPriority", &Kernel::sceKernelGetThreadCurrentPriority);
  add("ThreadManForUser",  "sceKernelRotateThreadReadyQueue", &Kernel::sceKernelRotateThreadReadyQueue);
  add("ThreadManForUser",  "sceKernelSuspendDispatchThread", &Kernel::sceKernelSuspendDispatchThread);
  add("ThreadManForUser",  "sceKernelResumeDispatchThread", &Kernel::sceKernelResumeDispatchThread);
  add("ThreadManForUser",  "sceKernelReferGlobalProfiler",  &Kernel::sceKernelReferThreadProfiler);
  add("ThreadManForUser",  "sceKernelCreateFpl",            &Kernel::sceKernelCreateFpl);
  add("ThreadManForUser",  "sceKernelDeleteFpl",            &Kernel::sceKernelDeleteFpl);
  add("ThreadManForUser",  "sceKernelAllocateFpl",          &Kernel::sceKernelAllocateFpl);
  add("ThreadManForUser",  "sceKernelAllocateFplCB",        &Kernel::sceKernelAllocateFplCB);
  add("ThreadManForUser",  "sceKernelTryAllocateFpl",       &Kernel::sceKernelTryAllocateFpl);
  add("ThreadManForUser",  "sceKernelFreeFpl",              &Kernel::sceKernelFreeFpl);
  add("ThreadManForUser",  "sceKernelCancelFpl",            &Kernel::sceKernelCancelFpl);
  add("ThreadManForUser",  "sceKernelReferFplStatus",       &Kernel::sceKernelReferFplStatus);
  add("ThreadManForUser",  "sceKernelCreateVpl",            &Kernel::sceKernelCreateVpl);
  add("ThreadManForUser",  "sceKernelDeleteVpl",            &Kernel::sceKernelDeleteVpl);
  add("ThreadManForUser",  "sceKernelAllocateVpl",          &Kernel::sceKernelAllocateVpl);
  add("ThreadManForUser",  "sceKernelAllocateVplCB",        &Kernel::sceKernelAllocateVplCB);
  add("ThreadManForUser",  "sceKernelTryAllocateVpl",       &Kernel::sceKernelTryAllocateVpl);
  add("ThreadManForUser",  "sceKernelFreeVpl",              &Kernel::sceKernelFreeVpl);
  add("ThreadManForUser",  "sceKernelCancelVpl",            &Kernel::sceKernelCancelVpl);
  add("ThreadManForUser",  "sceKernelReferVplStatus",       &Kernel::sceKernelReferVplStatus);
  add("ThreadManForUser",  "sceKernelCreateMsgPipe",        &Kernel::sceKernelCreateMsgPipe);
  add("ThreadManForUser",  "sceKernelDeleteMsgPipe",        &Kernel::sceKernelDeleteMsgPipe);
  add("ThreadManForUser",  "sceKernelSendMsgPipe",          &Kernel::sceKernelSendMsgPipe);
  add("ThreadManForUser",  "sceKernelSendMsgPipeCB",        &Kernel::sceKernelSendMsgPipeCB);
  add("ThreadManForUser",  "sceKernelTrySendMsgPipe",       &Kernel::sceKernelTrySendMsgPipe);
  add("ThreadManForUser",  "sceKernelReceiveMsgPipe",       &Kernel::sceKernelReceiveMsgPipe);
  add("ThreadManForUser",  "sceKernelReceiveMsgPipeCB",     &Kernel::sceKernelReceiveMsgPipeCB);
  add("ThreadManForUser",  "sceKernelTryReceiveMsgPipe",    &Kernel::sceKernelTryReceiveMsgPipe);
  add("ThreadManForUser",  "sceKernelCancelMsgPipe",        &Kernel::sceKernelCancelMsgPipe);
  add("ThreadManForUser",  "sceKernelReferMsgPipeStatus",   &Kernel::sceKernelReferMsgPipeStatus);
  add("ThreadManForUser",  "sceKernelCreateMbx",            &Kernel::sceKernelCreateMbx);
  add("ThreadManForUser",  "sceKernelDeleteMbx",            &Kernel::sceKernelDeleteMbx);
  add("ThreadManForUser",  "sceKernelSendMbx",              &Kernel::sceKernelSendMbx);
  add("ThreadManForUser",  "sceKernelReceiveMbx",           &Kernel::sceKernelReceiveMbx);
  add("ThreadManForUser",  "sceKernelReceiveMbxCB",         &Kernel::sceKernelReceiveMbxCB);
  add("ThreadManForUser",  "sceKernelPollMbx",              &Kernel::sceKernelPollMbx);
  add("ThreadManForUser",  "sceKernelCancelReceiveMbx",     &Kernel::sceKernelCancelReceiveMbx);
  add("ThreadManForUser",  "sceKernelReferMbxStatus",       &Kernel::sceKernelReferMbxStatus);
  add("ThreadManForUser",  "sceKernelSysClock2USec",        &Kernel::sceKernelSysClock2USec);
  add("ThreadManForUser",  "sceKernelSysClock2USecWide",    &Kernel::sceKernelSysClock2USecWide);
  add("ThreadManForUser",  "sceKernelUSec2SysClock",        &Kernel::sceKernelUSec2SysClock);
  add("ThreadManForUser",  "sceKernelUSec2SysClockWide",    &Kernel::sceKernelUSec2SysClockWide);
  add("ThreadManForUser",  "sceKernelCreateLwMutex",        &Kernel::sceKernelCreateLwMutex);
  add("ThreadManForUser",  "sceKernelDeleteLwMutex",        &Kernel::sceKernelDeleteLwMutex);
  add("ThreadManForUser",  "sceKernelGetSystemTimeLow",     &Kernel::sceKernelGetSystemTimeLow);
  add("ThreadManForUser",  "sceKernelGetSystemTimeWide",    &Kernel::sceKernelGetSystemTimeWide);
  add("ThreadManForUser",  "sceKernelGetSystemTime",        &Kernel::sceKernelGetSystemTime);
  add("ThreadManForUser",  "sceKernelCreateEventFlag",      &Kernel::sceKernelCreateEventFlag);
  add("ThreadManForUser",  "sceKernelDeleteEventFlag",      &Kernel::sceKernelDeleteEventFlag);
  add("ThreadManForUser",  "sceKernelSetEventFlag",         &Kernel::sceKernelSetEventFlag);
  add("ThreadManForUser",  "sceKernelClearEventFlag",       &Kernel::sceKernelClearEventFlag);
  add("ThreadManForUser",  "sceKernelCancelEventFlag",      &Kernel::sceKernelCancelEventFlag);
  add("ThreadManForUser",  "sceKernelWaitEventFlag",        &Kernel::sceKernelWaitEventFlag);
  add("ThreadManForUser",  "sceKernelWaitEventFlagCB",      &Kernel::sceKernelWaitEventFlagCB);
  add("ThreadManForUser",  "sceKernelPollEventFlag",        &Kernel::sceKernelPollEventFlag);
  add("ThreadManForUser",  "sceKernelReferEventFlagStatus", &Kernel::sceKernelReferEventFlagStatus);
  add("ThreadManForUser",  "sceKernelCreateCallback",       &Kernel::sceKernelCreateCallback);
  add("ThreadManForUser",  "sceKernelDeleteCallback",       &Kernel::sceKernelDeleteCallback);
  add("ThreadManForUser",  "sceKernelNotifyCallback",       &Kernel::sceKernelNotifyCallback);
  add("ThreadManForUser",  "sceKernelCancelCallback",       &Kernel::sceKernelCancelCallback);
  add("ThreadManForUser",  "sceKernelGetCallbackCount",     &Kernel::sceKernelGetCallbackCount);
  add("ThreadManForUser",  "sceKernelReferCallbackStatus",  &Kernel::sceKernelReferCallbackStatus);
  add("ThreadManForUser",  "sceKernelSleepThreadCB",        &Kernel::sceKernelSleepThreadCB);
  add("ThreadManForUser",  "sceKernelCheckCallback",        &Kernel::sceKernelCheckCallback);
  add("Kernel_Library",    "sceKernelLockLwMutex",          &Kernel::sceKernelLockLwMutex);
  add("Kernel_Library",    "sceKernelTryLockLwMutex",       &Kernel::sceKernelTryLockLwMutex);
  add("Kernel_Library",    "sceKernelUnlockLwMutex",        &Kernel::sceKernelUnlockLwMutex);
  add("Kernel_Library",    "sceKernelLockLwMutexCB",        &Kernel::sceKernelLockLwMutexCB);
  add("Kernel_Library",    "sceKernelCpuSuspendIntr",       &Kernel::sceKernelCpuSuspendIntr);
  add("Kernel_Library",    "sceKernelCpuResumeIntr",        &Kernel::sceKernelCpuResumeIntr);
  add("Kernel_Library",    "sceKernelCpuResumeIntrWithSync", &Kernel::sceKernelCpuResumeIntr);
  add("Kernel_Library",    "sceKernelIsCpuIntrEnable",      &Kernel::sceKernelIsCpuIntrEnable);
  add("Kernel_Library",    "sceKernelMemset",               &Kernel::sceKernelMemset);
  add("Kernel_Library",    "sceKernelMemcpy",               &Kernel::sceKernelMemcpy);
  add("InterruptManager",  "sceKernelRegisterSubIntrHandler", &Kernel::sceKernelRegisterSubIntrHandler);
  add("InterruptManager",  "sceKernelReleaseSubIntrHandler", &Kernel::sceKernelReleaseSubIntrHandler);
  add("InterruptManager",  "sceKernelEnableSubIntr",        &Kernel::sceKernelEnableSubIntr);
  add("InterruptManager",  "sceKernelDisableSubIntr",       &Kernel::sceKernelDisableSubIntr);
  add("UtilsForUser",      "sceKernelLibcGettimeofday",     &Kernel::sceKernelLibcGettimeofday);
  add("UtilsForUser",      "sceKernelLibcTime",             &Kernel::sceKernelLibcTime);
  add("UtilsForUser",      "sceKernelLibcClock",            &Kernel::sceKernelLibcClock);
  add("UtilsForUser",      "sceKernelUtilsMt19937Init",     &Kernel::sceKernelUtilsMt19937Init);
  add("UtilsForUser",      "sceKernelUtilsMt19937UInt",     &Kernel::sceKernelUtilsMt19937UInt);
  add("UtilsForUser",      "sceKernelSetGPO",               &Kernel::sceKernelSetGPO);
  add("UtilsForUser",      "sceKernelGetGPI",               &Kernel::sceKernelGetGPI);
  //the CPU's caches: an emulator has none to write back or throw away
  add("UtilsForUser",      "sceKernelDcacheWritebackAll",   &Kernel::sceKernelCacheUnneeded);
  add("UtilsForUser",      "sceKernelDcacheWritebackRange", &Kernel::sceKernelCacheUnneeded);
  add("UtilsForUser",      "sceKernelDcacheWritebackInvalidateAll",   &Kernel::sceKernelCacheUnneeded);
  add("UtilsForUser",      "sceKernelDcacheWritebackInvalidateRange", &Kernel::sceKernelCacheUnneeded);
  add("UtilsForUser",      "sceKernelDcacheInvalidateRange", &Kernel::sceKernelCacheUnneeded);
  add("UtilsForUser",      "sceKernelIcacheInvalidateAll",  &Kernel::sceKernelCacheUnneeded);
  add("UtilsForUser",      "sceKernelIcacheInvalidateRange", &Kernel::sceKernelCacheUnneeded);
  add("sceRtc",            "sceRtcGetCurrentTick",          &Kernel::sceRtcGetCurrentTick);
  add("sceRtc",            "sceRtcGetTickResolution",       &Kernel::sceRtcGetTickResolution);
  add("sceRtc",            "sceRtcGetTick",                 &Kernel::sceRtcGetTick);
  add("sceRtc",            "sceRtcCompareTick",             &Kernel::sceRtcCompareTick);
  add("sceRtc",            "sceRtcGetCurrentClock",         &Kernel::sceRtcGetCurrentClock);
  add("sceRtc",            "sceRtcGetCurrentClockLocalTime", &Kernel::sceRtcGetCurrentClockLocalTime);
  add("sceRtc",            "sceRtcGetTime_t",               &Kernel::sceRtcGetTime_t);
  add("sceRtc",            "sceRtcGetDosTime",              &Kernel::sceRtcGetDosTime);
  add("sceRtc",            "sceRtcSetDosTime",              &Kernel::sceRtcSetDosTime);
  add("sceRtc",            "sceRtcGetWin32FileTime",        &Kernel::sceRtcGetWin32FileTime);
  add("sceRtc",            "sceRtcSetTick",                 &Kernel::sceRtcSetTick);
  add("sceOpenPSID",       "sceOpenPSIDGetOpenPSID",        &Kernel::sceOpenPSIDGetOpenPSID);
  add("SysMemUserForUser", "sceKernelPrintf",               &Kernel::sceKernelPrintf);
  add("scePower",          "scePowerRegisterCallback",      &Kernel::scePowerRegisterCallback);
  add("scePower",          "scePowerUnregisterCallback",    &Kernel::scePowerUnregisterCallback);
  add("scePower",          "scePowerUnregitserCallback",    &Kernel::scePowerUnregisterCallback);  //Sony's spelling
  add("scePower",          "scePowerIsPowerOnline",         &Kernel::scePowerIsPowerOnline);
  add("scePower",          "scePowerIsBatteryExist",        &Kernel::scePowerIsBatteryExist);
  add("scePower",          "scePowerIsBatteryCharging",     &Kernel::scePowerIsBatteryCharging);
  add("scePower",          "scePowerGetBatteryChargingStatus", &Kernel::scePowerGetBatteryChargingStatus);
  add("scePower",          "scePowerIsLowBattery",          &Kernel::scePowerIsLowBattery);
  add("scePower",          "scePowerGetBatteryLifePercent", &Kernel::scePowerGetBatteryLifePercent);
  add("scePower",          "scePowerGetBatteryLifeTime",    &Kernel::scePowerGetBatteryLifeTime);
  add("scePower",          "scePowerTick",                  &Kernel::scePowerTick);
  add("scePower",          "scePowerSetClockFrequency",     &Kernel::scePowerSetClockFrequency);
  //later SDKs' scePowerSetClockFrequency, under NIDs of their own: Gunhound EX calls the first with (333, 333, 166),
  //Peace Walker the second
  addNID("scePower",       "scePower_469989AD",             0x4699'89ad, &Kernel::scePowerSetClockFrequency);
  addNID("scePower",       "scePower_EBD177D6",             0xebd1'77d6, &Kernel::scePowerSetClockFrequency);
  add("scePower",          "scePowerSetCpuClockFrequency",  &Kernel::scePowerSetCpuClockFrequency);
  add("scePower",          "scePowerSetBusClockFrequency",  &Kernel::scePowerSetBusClockFrequency);
  add("scePower",          "scePowerGetCpuClockFrequency",  &Kernel::scePowerGetCpuClockFrequency);
  add("scePower",          "scePowerGetCpuClockFrequencyInt", &Kernel::scePowerGetCpuClockFrequency);
  add("scePower",          "scePowerGetBusClockFrequency",  &Kernel::scePowerGetBusClockFrequency);
  add("scePower",          "scePowerGetBusClockFrequencyInt", &Kernel::scePowerGetBusClockFrequency);
  add("scePower",          "scePowerGetPllClockFrequencyInt", &Kernel::scePowerGetPllClockFrequencyInt);
  add("scePower",          "scePowerGetCpuClockFrequencyFloat", &Kernel::scePowerGetCpuClockFrequencyFloat);
  add("scePower",          "scePowerGetBusClockFrequencyFloat", &Kernel::scePowerGetBusClockFrequencyFloat);
  add("scePower",          "scePowerGetPllClockFrequencyFloat", &Kernel::scePowerGetPllClockFrequencyFloat);
  add("sceAudio",          "sceAudioChReserve",             &Kernel::sceAudioChReserve);
  add("sceAudio",          "sceAudioChRelease",             &Kernel::sceAudioChRelease);
  add("sceAudio",          "sceAudioOutputBlocking",        &Kernel::sceAudioOutputBlocking);
  add("sceAudio",          "sceAudioOutputPannedBlocking",  &Kernel::sceAudioOutputPannedBlocking);
  add("sceAudio",          "sceAudioOutput",                &Kernel::sceAudioOutput);
  add("sceAudio",          "sceAudioOutputPanned",          &Kernel::sceAudioOutputPanned);
  add("sceAudio",          "sceAudioGetChannelRestLen",     &Kernel::sceAudioGetChannelRestLen);
  add("sceAudio",          "sceAudioGetChannelRestLength",  &Kernel::sceAudioGetChannelRestLength);
  add("sceAudio",          "sceAudioSetChannelDataLen",     &Kernel::sceAudioSetChannelDataLen);
  add("sceAudio",          "sceAudioChangeChannelConfig",   &Kernel::sceAudioChangeChannelConfig);
  add("sceAudio",          "sceAudioChangeChannelVolume",   &Kernel::sceAudioChangeChannelVolume);
  add("sceAudio",          "sceAudioOutput2Reserve",        &Kernel::sceAudioOutput2Reserve);
  add("sceAudio",          "sceAudioOutput2OutputBlocking", &Kernel::sceAudioOutput2OutputBlocking);
  add("sceAudio",          "sceAudioOutput2ChangeLength",   &Kernel::sceAudioOutput2ChangeLength);
  add("sceAudio",          "sceAudioOutput2GetRestSample",  &Kernel::sceAudioOutput2GetRestSample);
  add("sceAudio",          "sceAudioOutput2Release",        &Kernel::sceAudioOutput2Release);
  add("sceAudio",          "sceAudioSRCChReserve",          &Kernel::sceAudioSRCChReserve);
  add("sceAudio",          "sceAudioSRCOutputBlocking",     &Kernel::sceAudioSRCOutputBlocking);
  add("sceAudio",          "sceAudioSRCChRelease",          &Kernel::sceAudioSRCChRelease);
  add("sceSasCore",        "__sceSasInit",                  &Kernel::__sceSasInit);
  add("sceSasCore",        "__sceSasCore",                  &Kernel::__sceSasCore);
  add("sceSasCore",        "__sceSasCoreWithMix",           &Kernel::__sceSasCoreWithMix);
  add("sceSasCore",        "__sceSasGetEndFlag",            &Kernel::__sceSasGetEndFlag);
  add("sceSasCore",        "__sceSasSetVolume",             &Kernel::__sceSasSetVolume);
  add("sceSasCore",        "__sceSasSetPitch",              &Kernel::__sceSasSetPitch);
  add("sceSasCore",        "__sceSasSetVoice",              &Kernel::__sceSasSetVoice);
  add("sceSasCore",        "__sceSasSetVoicePCM",           &Kernel::__sceSasSetVoicePCM);
  add("sceSasCore",        "__sceSasSetNoise",              &Kernel::__sceSasSetNoise);
  add("sceSasCore",        "__sceSasSetTrianglarWave",      &Kernel::__sceSasSetTrianglarWave);
  add("sceSasCore",        "__sceSasSetTriangularWave",     &Kernel::__sceSasSetTrianglarWave);
  add("sceSasCore",        "__sceSasSetSteepWave",          &Kernel::__sceSasSetSteepWave);
  add("sceSasCore",        "__sceSasSetVoiceATRAC3",        &Kernel::__sceSasSetVoiceATRAC3);
  add("sceSasCore",        "__sceSasConcatenateATRAC3",     &Kernel::__sceSasConcatenateATRAC3);
  add("sceSasCore",        "__sceSasUnsetATRAC3",           &Kernel::__sceSasUnsetATRAC3);
  add("sceSasCore",        "__sceSasSetADSR",               &Kernel::__sceSasSetADSR);
  add("sceSasCore",        "__sceSasSetADSRmode",           &Kernel::__sceSasSetADSRmode);
  add("sceSasCore",        "__sceSasSetSimpleADSR",         &Kernel::__sceSasSetSimpleADSR);
  add("sceSasCore",        "__sceSasSetSL",                 &Kernel::__sceSasSetSL);
  add("sceSasCore",        "__sceSasGetEnvelopeHeight",     &Kernel::__sceSasGetEnvelopeHeight);
  add("sceSasCore",        "__sceSasGetAllEnvelopeHeights", &Kernel::__sceSasGetAllEnvelopeHeights);
  add("sceSasCore",        "__sceSasSetKeyOn",              &Kernel::__sceSasSetKeyOn);
  add("sceSasCore",        "__sceSasSetKeyOff",             &Kernel::__sceSasSetKeyOff);
  add("sceSasCore",        "__sceSasSetPause",              &Kernel::__sceSasSetPause);
  add("sceSasCore",        "__sceSasGetPauseFlag",          &Kernel::__sceSasGetPauseFlag);
  add("sceSasCore",        "__sceSasGetGrain",              &Kernel::__sceSasGetGrain);
  add("sceSasCore",        "__sceSasSetGrain",              &Kernel::__sceSasSetGrain);
  add("sceSasCore",        "__sceSasGetOutputmode",         &Kernel::__sceSasGetOutputmode);
  add("sceSasCore",        "__sceSasSetOutputmode",         &Kernel::__sceSasSetOutputmode);
  add("sceSasCore",        "__sceSasRevType",               &Kernel::__sceSasRevType);
  add("sceSasCore",        "__sceSasRevParam",              &Kernel::__sceSasRevParam);
  add("sceSasCore",        "__sceSasRevEVOL",               &Kernel::__sceSasRevEVOL);
  add("sceSasCore",        "__sceSasRevVON",                &Kernel::__sceSasRevVON);
  add("sceSuspendForUser", "sceKernelPowerTick",            &Kernel::sceKernelPowerTick);
  add("sceSuspendForUser", "sceKernelPowerLock",            &Kernel::sceKernelPowerLock);
  add("sceSuspendForUser", "sceKernelPowerUnlock",          &Kernel::sceKernelPowerUnlock);
  add("sceSuspendForUser", "sceKernelVolatileMemLock",      &Kernel::sceKernelVolatileMemLock);
  add("sceSuspendForUser", "sceKernelVolatileMemTryLock",   &Kernel::sceKernelVolatileMemTryLock);
  add("sceSuspendForUser", "sceKernelVolatileMemUnlock",    &Kernel::sceKernelVolatileMemUnlock);
  add("sceWlanDrv",        "sceWlanGetSwitchState",         &Kernel::sceWlanGetSwitchState);
  add("sceWlanDrv",        "sceWlanGetEtherAddr",           &Kernel::sceWlanGetEtherAddr);
  add("sceImpose",         "sceImposeSetLanguageMode",      &Kernel::sceImposeSetLanguageMode);
  add("sceImpose",         "sceImposeGetLanguageMode",      &Kernel::sceImposeGetLanguageMode);
  add("sceImpose",         "sceImposeGetBatteryIconStatus", &Kernel::sceImposeGetBatteryIconStatus);
  add("sceDmac",           "sceDmacMemcpy",                 &Kernel::sceDmacMemcpy);
  add("SysMemUserForUser", "sceKernelAllocPartitionMemory", &Kernel::sceKernelAllocPartitionMemory);
  add("SysMemUserForUser", "sceKernelFreePartitionMemory",  &Kernel::sceKernelFreePartitionMemory);
  add("SysMemUserForUser", "sceKernelGetBlockHeadAddr",     &Kernel::sceKernelGetBlockHeadAddr);
  add("SysMemUserForUser", "sceKernelMaxFreeMemSize",       &Kernel::sceKernelMaxFreeMemSize);
  add("SysMemUserForUser", "sceKernelTotalFreeMemSize",     &Kernel::sceKernelTotalFreeMemSize);
  add("SysMemUserForUser", "sceKernelSetCompiledSdkVersion", &Kernel::sceKernelSetCompiledSdkVersion);
  add("SysMemUserForUser", "sceKernelGetCompiledSdkVersion", &Kernel::sceKernelGetCompiledSdkVersion);
  add("SysMemUserForUser", "sceKernelSetCompilerVersion",   &Kernel::sceKernelSetCompilerVersion);
  add("SysMemUserForUser", "sceKernelDevkitVersion",        &Kernel::sceKernelDevkitVersion);
  //later SDKs' memory blocks, whose NIDs aren't their names' hashes either (as pspautotests' sysmem-imports.S lists
  //them)
  addNID("SysMemUserForUser", "sceKernelAllocMemoryBlock",  0xfe70'7fdf, &Kernel::sceKernelAllocMemoryBlock);
  addNID("SysMemUserForUser", "sceKernelFreeMemoryBlock",   0x50f6'1d8a, &Kernel::sceKernelFreeMemoryBlock);
  addNID("SysMemUserForUser", "sceKernelGetMemoryBlockPtr", 0xdb83'a952, &Kernel::sceKernelGetMemoryBlockPtr);
  //one for each range of SDK versions from 3.7 on, all doing the same
  addNID("SysMemUserForUser", "sceKernelSetCompiledSdkVersion370",     0x3420'61e5,
         &Kernel::sceKernelSetCompiledSdkVersion);
  addNID("SysMemUserForUser", "sceKernelSetCompiledSdkVersion380_390", 0x315a'd3a0,
         &Kernel::sceKernelSetCompiledSdkVersion);
  addNID("SysMemUserForUser", "sceKernelSetCompiledSdkVersion395",     0xebd5'c3e6,
         &Kernel::sceKernelSetCompiledSdkVersion);
  addNID("SysMemUserForUser", "sceKernelSetCompiledSdkVersion401_402", 0x057e'7380,
         &Kernel::sceKernelSetCompiledSdkVersion);
  addNID("SysMemUserForUser", "sceKernelSetCompiledSdkVersion500_505", 0x91de'343c,
         &Kernel::sceKernelSetCompiledSdkVersion);
  addNID("SysMemUserForUser", "sceKernelSetCompiledSdkVersion507",     0x7893'f79a,
         &Kernel::sceKernelSetCompiledSdkVersion);
  addNID("SysMemUserForUser", "sceKernelSetCompiledSdkVersion600_602", 0x3566'9d4c,
         &Kernel::sceKernelSetCompiledSdkVersion);
  addNID("SysMemUserForUser", "sceKernelSetCompiledSdkVersion603_605", 0x1b42'17bc,
         &Kernel::sceKernelSetCompiledSdkVersion);
  addNID("SysMemUserForUser", "sceKernelSetCompiledSdkVersion606",     0x358c'a1bb,
         &Kernel::sceKernelSetCompiledSdkVersion);
  add("StdioForUser",      "sceKernelStdin",                &Kernel::sceKernelStdin);
  add("StdioForUser",      "sceKernelStdout",               &Kernel::sceKernelStdout);
  add("StdioForUser",      "sceKernelStderr",               &Kernel::sceKernelStderr);
  add("IoFileMgrForUser",  "sceIoOpen",                     &Kernel::sceIoOpen);
  add("IoFileMgrForUser",  "sceIoClose",                    &Kernel::sceIoClose);
  add("IoFileMgrForUser",  "sceIoRead",                     &Kernel::sceIoRead);
  add("IoFileMgrForUser",  "sceIoWrite",                    &Kernel::sceIoWrite);
  add("IoFileMgrForUser",  "sceIoLseek",                    &Kernel::sceIoLseek);
  add("IoFileMgrForUser",  "sceIoLseek32",                  &Kernel::sceIoLseek32);
  add("IoFileMgrForUser",  "sceIoRemove",                   &Kernel::sceIoRemove);
  add("IoFileMgrForUser",  "sceIoMkdir",                    &Kernel::sceIoMkdir);
  add("IoFileMgrForUser",  "sceIoRmdir",                    &Kernel::sceIoRmdir);
  add("IoFileMgrForUser",  "sceIoRename",                   &Kernel::sceIoRename);
  add("IoFileMgrForUser",  "sceIoChdir",                    &Kernel::sceIoChdir);
  add("IoFileMgrForUser",  "sceIoGetstat",                  &Kernel::sceIoGetstat);
  add("IoFileMgrForUser",  "sceIoDopen",                    &Kernel::sceIoDopen);
  add("IoFileMgrForUser",  "sceIoDread",                    &Kernel::sceIoDread);
  add("IoFileMgrForUser",  "sceIoDclose",                   &Kernel::sceIoDclose);
  add("IoFileMgrForUser",  "sceIoIoctl",                    &Kernel::sceIoIoctl);
  add("IoFileMgrForUser",  "sceIoDevctl",                   &Kernel::sceIoDevctl);
  add("IoFileMgrForUser",  "sceIoOpenAsync",                &Kernel::sceIoOpenAsync);
  add("IoFileMgrForUser",  "sceIoCloseAsync",               &Kernel::sceIoCloseAsync);
  add("IoFileMgrForUser",  "sceIoReadAsync",                &Kernel::sceIoReadAsync);
  add("IoFileMgrForUser",  "sceIoWriteAsync",               &Kernel::sceIoWriteAsync);
  add("IoFileMgrForUser",  "sceIoLseekAsync",               &Kernel::sceIoLseekAsync);
  add("IoFileMgrForUser",  "sceIoLseek32Async",             &Kernel::sceIoLseek32Async);
  add("IoFileMgrForUser",  "sceIoIoctlAsync",               &Kernel::sceIoIoctlAsync);
  add("IoFileMgrForUser",  "sceIoPollAsync",                &Kernel::sceIoPollAsync);
  add("IoFileMgrForUser",  "sceIoWaitAsync",                &Kernel::sceIoWaitAsync);
  add("IoFileMgrForUser",  "sceIoWaitAsyncCB",              &Kernel::sceIoWaitAsyncCB);
  add("IoFileMgrForUser",  "sceIoGetAsyncStat",             &Kernel::sceIoGetAsyncStat);
  add("IoFileMgrForUser",  "sceIoChangeAsyncPriority",      &Kernel::sceIoChangeAsyncPriority);
  add("IoFileMgrForUser",  "sceIoSetAsyncCallback",         &Kernel::sceIoSetAsyncCallback);
  add("sceUmdUser",        "sceUmdCheckMedium",             &Kernel::sceUmdCheckMedium);
  add("sceUmdUser",        "sceUmdActivate",                &Kernel::sceUmdActivate);
  add("sceUmdUser",        "sceUmdDeactivate",              &Kernel::sceUmdDeactivate);
  add("sceUmdUser",        "sceUmdGetDriveStat",            &Kernel::sceUmdGetDriveStat);
  add("sceUmdUser",        "sceUmdWaitDriveStat",           &Kernel::sceUmdWaitDriveStat);
  add("sceUmdUser",        "sceUmdWaitDriveStatWithTimer",  &Kernel::sceUmdWaitDriveStatWithTimer);
  add("sceUmdUser",        "sceUmdWaitDriveStatCB",         &Kernel::sceUmdWaitDriveStatCB);
  add("sceUmdUser",        "sceUmdCancelWaitDriveStat",     &Kernel::sceUmdCancelWaitDriveStat);
  add("sceUmdUser",        "sceUmdGetErrorStat",            &Kernel::sceUmdGetErrorStat);
  add("sceUmdUser",        "sceUmdGetDiscInfo",             &Kernel::sceUmdGetDiscInfo);
  add("sceUmdUser",        "sceUmdRegisterUMDCallBack",     &Kernel::sceUmdRegisterUMDCallBack);
  add("sceUmdUser",        "sceUmdUnRegisterUMDCallBack",   &Kernel::sceUmdUnRegisterUMDCallBack);
  add("sceUmdUser",        "sceUmdReplacePermit",           &Kernel::sceUmdReplacePermit);
  add("sceUmdUser",        "sceUmdReplaceProhibit",         &Kernel::sceUmdReplacePermit);
  add("sceCtrl",           "sceCtrlSetSamplingCycle",       &Kernel::sceCtrlSetSamplingCycle);
  add("sceCtrl",           "sceCtrlGetSamplingCycle",       &Kernel::sceCtrlGetSamplingCycle);
  add("sceCtrl",           "sceCtrlSetSamplingMode",        &Kernel::sceCtrlSetSamplingMode);
  add("sceCtrl",           "sceCtrlGetSamplingMode",        &Kernel::sceCtrlGetSamplingMode);
  add("sceCtrl",           "sceCtrlPeekBufferPositive",     &Kernel::sceCtrlPeekBufferPositive);
  add("sceCtrl",           "sceCtrlPeekBufferNegative",     &Kernel::sceCtrlPeekBufferNegative);
  add("sceCtrl",           "sceCtrlReadBufferPositive",     &Kernel::sceCtrlReadBufferPositive);
  add("sceCtrl",           "sceCtrlReadBufferNegative",     &Kernel::sceCtrlReadBufferNegative);
  add("sceCtrl",           "sceCtrlPeekLatch",              &Kernel::sceCtrlPeekLatch);
  add("sceCtrl",           "sceCtrlReadLatch",              &Kernel::sceCtrlReadLatch);
  add("sceCtrl",           "sceCtrlSetIdleCancelThreshold", &Kernel::sceCtrlSetIdleCancelThreshold);
  add("sceCtrl",           "sceCtrlGetIdleCancelThreshold", &Kernel::sceCtrlGetIdleCancelThreshold);
  add("sceDisplay",        "sceDisplaySetMode",             &Kernel::sceDisplaySetMode);
  add("sceDisplay",        "sceDisplaySetFrameBuf",         &Kernel::sceDisplaySetFrameBuf);
  add("sceDisplay",        "sceDisplayGetFrameBuf",         &Kernel::sceDisplayGetFrameBuf);
  add("sceDisplay",        "sceDisplayWaitVblankStart",     &Kernel::sceDisplayWaitVblankStart);
  add("sceDisplay",        "sceDisplayWaitVblankStartCB",   &Kernel::sceDisplayWaitVblankStartCB);
  add("sceDisplay",        "sceDisplayWaitVblank",          &Kernel::sceDisplayWaitVblank);
  add("sceDisplay",        "sceDisplayWaitVblankCB",        &Kernel::sceDisplayWaitVblankCB);
  add("sceDisplay",        "sceDisplayIsVblank",            &Kernel::sceDisplayIsVblank);
  add("sceDisplay",        "sceDisplayGetCurrentHcount",    &Kernel::sceDisplayGetCurrentHcount);
  add("sceDisplay",        "sceDisplayGetVcount",           &Kernel::sceDisplayGetVcount);
  add("sceDisplay",        "sceDisplayGetAccumulatedHcount", &Kernel::sceDisplayGetAccumulatedHcount);
  add("sceDisplay",        "sceDisplayGetFramePerSec",      &Kernel::sceDisplayGetFramePerSec);
  add("sceGe_user",        "sceGeEdramGetAddr",             &Kernel::sceGeEdramGetAddr);
  add("sceGe_user",        "sceGeEdramGetSize",             &Kernel::sceGeEdramGetSize);
  add("sceGe_user",        "sceGeEdramSetAddrTranslation",  &Kernel::sceGeEdramSetAddrTranslation);
  add("sceGe_user",        "sceGeListEnQueue",              &Kernel::sceGeListEnQueue);
  add("sceGe_user",        "sceGeListEnQueueHead",          &Kernel::sceGeListEnQueueHead);
  add("sceGe_user",        "sceGeListDeQueue",              &Kernel::sceGeListDeQueue);
  add("sceGe_user",        "sceGeListUpdateStallAddr",      &Kernel::sceGeListUpdateStallAddr);
  add("sceGe_user",        "sceGeListSync",                 &Kernel::sceGeListSync);
  add("sceGe_user",        "sceGeDrawSync",                 &Kernel::sceGeDrawSync);
  add("sceGe_user",        "sceGeSetCallback",              &Kernel::sceGeSetCallback);
  add("sceGe_user",        "sceGeUnsetCallback",            &Kernel::sceGeUnsetCallback);
  add("sceGe_user",        "sceGeContinue",                 &Kernel::sceGeContinue);
  add("sceGe_user",        "sceGeBreak",                    &Kernel::sceGeBreak);
  add("sceGe_user",        "sceGeGetCmd",                   &Kernel::sceGeGetCmd);
  add("sceGe_user",        "sceGeGetMtx",                   &Kernel::sceGeGetMtx);
  add("sceGe_user",        "sceGeSaveContext",              &Kernel::sceGeSaveContext);
  add("sceGe_user",        "sceGeRestoreContext",           &Kernel::sceGeRestoreContext);
  add("LoadExecForUser",   "sceKernelExitGame",             &Kernel::sceKernelExitGame);
  add("LoadExecForUser",   "sceKernelRegisterExitCallback", &Kernel::sceKernelRegisterExitCallback);
  add("ModuleMgrForUser",  "sceKernelSelfStopUnloadModule", &Kernel::sceKernelSelfStopUnloadModule);
  addNID("ModuleMgrForUser", "sceKernelStopUnloadSelfModuleWithStatus", 0x8f2d'f740,
         &Kernel::sceKernelStopUnloadSelfModuleWithStatus);
  add("ModuleMgrForUser",  "sceKernelLoadModule",           &Kernel::sceKernelLoadModule);
  add("ModuleMgrForUser",  "sceKernelLoadModuleByID",       &Kernel::sceKernelLoadModuleByID);
  add("ModuleMgrForUser",  "sceKernelStartModule",          &Kernel::sceKernelStartModule);
  add("ModuleMgrForUser",  "sceKernelStopModule",           &Kernel::sceKernelStopModule);
  add("ModuleMgrForUser",  "sceKernelUnloadModule",         &Kernel::sceKernelUnloadModule);
  add("ModuleMgrForUser",  "sceKernelGetModuleIdByAddress", &Kernel::sceKernelGetModuleIdByAddress);
  add("ModuleMgrForUser",  "sceKernelGetModuleId",          &Kernel::sceKernelGetModuleId);
  add("ModuleMgrForUser",  "sceKernelGetModuleIdList",      &Kernel::sceKernelGetModuleIdList);
  add("ModuleMgrForUser",  "sceKernelQueryModuleInfo",      &Kernel::sceKernelQueryModuleInfo);
  add("sceUtility",        "sceUtilityGetSystemParamInt",   &Kernel::sceUtilityGetSystemParamInt);
  add("sceUtility",        "sceUtilityGetSystemParamString", &Kernel::sceUtilityGetSystemParamString);
  add("sceUtility",        "sceUtilitySetSystemParamString", &Kernel::sceUtilitySetSystemParamString);
  add("sceUtility",        "sceUtilityLoadModule",          &Kernel::sceUtilityLoadModule);
  add("sceUtility",        "sceUtilityUnloadModule",        &Kernel::sceUtilityUnloadModule);
  add("sceUtility",        "sceUtilityLoadNetModule",       &Kernel::sceUtilityLoadNetModule);
  add("sceUtility",        "sceUtilityUnloadNetModule",     &Kernel::sceUtilityUnloadNetModule);
  add("sceUtility",        "sceUtilitySavedataInitStart",   &Kernel::sceUtilitySavedataInitStart);
  add("sceUtility",        "sceUtilitySavedataGetStatus",   &Kernel::sceUtilitySavedataGetStatus);
  add("sceUtility",        "sceUtilitySavedataUpdate",      &Kernel::sceUtilitySavedataUpdate);
  add("sceUtility",        "sceUtilitySavedataShutdownStart", &Kernel::sceUtilitySavedataShutdownStart);
  add("sceUtility",        "sceUtilityMsgDialogInitStart",  &Kernel::sceUtilityMsgDialogInitStart);
  add("sceUtility",        "sceUtilityMsgDialogGetStatus",  &Kernel::sceUtilityMsgDialogGetStatus);
  add("sceUtility",        "sceUtilityMsgDialogUpdate",     &Kernel::sceUtilityMsgDialogUpdate);
  add("sceUtility",        "sceUtilityMsgDialogShutdownStart", &Kernel::sceUtilityMsgDialogShutdownStart);
  add("sceUtility",        "sceUtilityOskInitStart",        &Kernel::sceUtilityOskInitStart);
  add("sceUtility",        "sceUtilityOskGetStatus",        &Kernel::sceUtilityOskGetStatus);
  add("sceUtility",        "sceUtilityOskUpdate",           &Kernel::sceUtilityOskUpdate);
  add("sceUtility",        "sceUtilityOskShutdownStart",    &Kernel::sceUtilityOskShutdownStart);
  add("sceUtility",        "sceUtilityNetconfInitStart",    &Kernel::sceUtilityNetconfInitStart);
  add("sceUtility",        "sceUtilityNetconfGetStatus",    &Kernel::sceUtilityNetconfGetStatus);
  add("sceUtility",        "sceUtilityNetconfUpdate",       &Kernel::sceUtilityNetconfUpdate);
  add("sceUtility",        "sceUtilityNetconfShutdownStart", &Kernel::sceUtilityNetconfShutdownStart);
  add("sceUtility",        "sceUtilityGameSharingInitStart", &Kernel::sceUtilityGameSharingInitStart);
  add("sceUtility",        "sceUtilityGameSharingGetStatus", &Kernel::sceUtilityGameSharingGetStatus);
  add("sceUtility",        "sceUtilityGameSharingUpdate",   &Kernel::sceUtilityGameSharingUpdate);
  add("sceUtility",        "sceUtilityGameSharingShutdownStart", &Kernel::sceUtilityGameSharingShutdownStart);
  add("sceUtility",        "sceUtilityHtmlViewerInitStart", &Kernel::sceUtilityHtmlViewerInitStart);
  add("sceUtility",        "sceUtilityHtmlViewerGetStatus", &Kernel::sceUtilityHtmlViewerGetStatus);
  add("sceUtility",        "sceUtilityHtmlViewerUpdate",    &Kernel::sceUtilityHtmlViewerUpdate);
  add("sceUtility",        "sceUtilityHtmlViewerShutdownStart", &Kernel::sceUtilityHtmlViewerShutdownStart);
  add("sceUtility",        "sceUtilityLoadAvModule",        &Kernel::sceUtilityLoadAvModule);
  add("sceUtility",        "sceUtilityUnloadAvModule",      &Kernel::sceUtilityUnloadAvModule);
  //newlib's sockets: no network yet, so every call fails
  for(auto name : {"sceNetInetClose", "sceNetInetRecv", "sceNetInetSend", "sceNetInetGetErrno", "sceNetInetSocket",
                   "sceNetInetBind", "sceNetInetConnect", "sceNetInetListen", "sceNetInetAccept", "sceNetInetSendto",
                   "sceNetInetRecvfrom", "sceNetInetSetsockopt", "sceNetInetGetsockopt", "sceNetInetGetsockname",
                   "sceNetInetGetpeername", "sceNetInetShutdown", "sceNetInetPoll"}) {
    add("sceNetInet", name, &Kernel::sceNetInetUnavailable);
  }
  //the network libraries with the wireless LAN switched off (net.cpp): started and stopped, nothing reached
  for(auto [library, name] : std::initializer_list<std::pair<const char*, const char*>>{
      {"sceNet", "sceNetInit"}, {"sceNet", "sceNetTerm"}, {"sceNet", "sceNetFreeThreadinfo"},
      {"sceNet", "sceNetThreadAbort"}, {"sceNetAdhoc", "sceNetAdhocInit"}, {"sceNetAdhoc", "sceNetAdhocTerm"},
      {"sceNetAdhocctl", "sceNetAdhocctlInit"}, {"sceNetAdhocctl", "sceNetAdhocctlTerm"},
      {"sceNetAdhocctl", "sceNetAdhocctlDisconnect"}, {"sceNetAdhocctl", "sceNetAdhocctlExitGameMode"},
      {"sceNetAdhocctl", "sceNetAdhocctlAddHandler"}, {"sceNetAdhocctl", "sceNetAdhocctlDelHandler"},
      {"sceNetAdhocMatching", "sceNetAdhocMatchingInit"}, {"sceNetAdhocMatching", "sceNetAdhocMatchingTerm"},
      {"sceNetInet", "sceNetInetInit"}, {"sceNetInet", "sceNetInetTerm"},
      {"sceNetResolver", "sceNetResolverInit"}, {"sceNetResolver", "sceNetResolverTerm"},
      {"sceNetApctl", "sceNetApctlInit"}, {"sceNetApctl", "sceNetApctlTerm"},
      {"sceNetApctl", "sceNetApctlDisconnect"}, {"sceNetApctl", "sceNetApctlAddHandler"},
      {"sceNetApctl", "sceNetApctlDelHandler"}}) {
    add(library, name, &Kernel::sceNetDone);
  }
  for(auto [library, name] : std::initializer_list<std::pair<const char*, const char*>>{
      {"sceNetAdhoc", "sceNetAdhocPdpCreate"}, {"sceNetAdhoc", "sceNetAdhocPdpDelete"},
      {"sceNetAdhoc", "sceNetAdhocPdpSend"}, {"sceNetAdhoc", "sceNetAdhocPdpRecv"},
      {"sceNetAdhoc", "sceNetAdhocPtpOpen"}, {"sceNetAdhoc", "sceNetAdhocPtpConnect"},
      {"sceNetAdhoc", "sceNetAdhocPtpListen"}, {"sceNetAdhoc", "sceNetAdhocPtpAccept"},
      {"sceNetAdhoc", "sceNetAdhocPtpSend"}, {"sceNetAdhoc", "sceNetAdhocPtpRecv"},
      {"sceNetAdhoc", "sceNetAdhocPtpFlush"}, {"sceNetAdhoc", "sceNetAdhocPtpClose"},
      {"sceNetAdhocctl", "sceNetAdhocctlConnect"}, {"sceNetAdhocctl", "sceNetAdhocctlCreate"},
      {"sceNetAdhocctl", "sceNetAdhocctlJoin"}, {"sceNetAdhocctl", "sceNetAdhocctlScan"},
      {"sceNetAdhocctl", "sceNetAdhocctlCreateEnterGameMode"}, {"sceNetAdhocctl", "sceNetAdhocctlJoinEnterGameMode"},
      {"sceNetAdhocctl", "sceNetAdhocctlGetParameter"}, {"sceNetAdhocctl", "sceNetAdhocctlGetAdhocId"},
      {"sceNetAdhocMatching", "sceNetAdhocMatchingCreate"}, {"sceNetAdhocMatching", "sceNetAdhocMatchingStart"},
      {"sceNetAdhocMatching", "sceNetAdhocMatchingStop"}, {"sceNetAdhocMatching", "sceNetAdhocMatchingDelete"},
      {"sceNetAdhocMatching", "sceNetAdhocMatchingSelectTarget"},
      {"sceNetAdhocMatching", "sceNetAdhocMatchingCancelTarget"},
      {"sceNetAdhocMatching", "sceNetAdhocMatchingSendData"},
      {"sceNetResolver", "sceNetResolverCreate"}, {"sceNetResolver", "sceNetResolverStartNtoA"},
      {"sceNetResolver", "sceNetResolverStop"}, {"sceNetResolver", "sceNetResolverDelete"},
      {"sceNetApctl", "sceNetApctlConnect"}, {"sceNetApctl", "sceNetApctlGetInfo"}}) {
    add(library, name, &Kernel::sceNetUnavailable);
  }
  add("sceNet",            "sceNetGetLocalEtherAddr",       &Kernel::sceNetGetLocalEtherAddr);
  add("sceNet",            "sceNetEtherNtostr",             &Kernel::sceNetEtherNtostr);
  add("sceNet",            "sceNetEtherStrton",             &Kernel::sceNetEtherStrton);
  add("sceNetAdhocctl",    "sceNetAdhocctlGetState",        &Kernel::sceNetAdhocctlGetState);
  add("sceNetApctl",       "sceNetApctlGetState",           &Kernel::sceNetAdhocctlGetState);
  add("sceNetAdhoc",       "sceNetAdhocGetPdpStat",         &Kernel::sceNetEmptyList);
  add("sceNetAdhoc",       "sceNetAdhocGetPtpStat",         &Kernel::sceNetEmptyList);
  add("sceNetAdhocctl",    "sceNetAdhocctlGetScanInfo",     &Kernel::sceNetEmptyList);
  add("sceNetAdhocctl",    "sceNetAdhocctlGetPeerList",     &Kernel::sceNetEmptyList);
  //movies (mpeg.cpp) and ATRAC3plus streams (atrac.cpp), set up but never played
  add("sceMpeg",           "sceMpegInit",                   &Kernel::sceMpegInit);
  add("sceMpeg",           "sceMpegFinish",                 &Kernel::sceMpegFinish);
  add("sceMpeg",           "sceMpegRingbufferQueryMemSize", &Kernel::sceMpegRingbufferQueryMemSize);
  add("sceMpeg",           "sceMpegQueryMemSize",           &Kernel::sceMpegQueryMemSize);
  add("sceMpeg",           "sceMpegRingbufferConstruct",    &Kernel::sceMpegRingbufferConstruct);
  add("sceMpeg",           "sceMpegRingbufferDestruct",     &Kernel::sceMpegRingbufferDestruct);
  add("sceMpeg",           "sceMpegRingbufferAvailableSize", &Kernel::sceMpegRingbufferAvailableSize);
  add("sceMpeg",           "sceMpegRingbufferPut",          &Kernel::sceMpegRingbufferPut);
  add("sceMpeg",           "sceMpegCreate",                 &Kernel::sceMpegCreate);
  add("sceMpeg",           "sceMpegDelete",                 &Kernel::sceMpegDelete);
  add("sceMpeg",           "sceMpegQueryStreamOffset",      &Kernel::sceMpegQueryStreamOffset);
  add("sceMpeg",           "sceMpegQueryStreamSize",        &Kernel::sceMpegQueryStreamSize);
  add("sceMpeg",           "sceMpegRegistStream",           &Kernel::sceMpegRegistStream);
  add("sceMpeg",           "sceMpegUnRegistStream",         &Kernel::sceMpegUnRegistStream);
  add("sceMpeg",           "sceMpegMallocAvcEsBuf",         &Kernel::sceMpegMallocAvcEsBuf);
  add("sceMpeg",           "sceMpegFreeAvcEsBuf",           &Kernel::sceMpegFreeAvcEsBuf);
  add("sceMpeg",           "sceMpegInitAu",                 &Kernel::sceMpegInitAu);
  add("sceMpeg",           "sceMpegFlushAllStream",         &Kernel::sceMpegFlushAllStream);
  add("sceMpeg",           "sceMpegFlushStream",            &Kernel::sceMpegFlushStream);
  add("sceMpeg",           "sceMpegGetAvcAu",               &Kernel::sceMpegGetAvcAu);
  add("sceMpeg",           "sceMpegGetAtracAu",             &Kernel::sceMpegGetAtracAu);
  add("sceMpeg",           "sceMpegGetPcmAu",               &Kernel::sceMpegGetPcmAu);
  add("sceMpeg",           "sceMpegAvcDecodeMode",          &Kernel::sceMpegAvcDecodeMode);
  add("sceMpeg",           "sceMpegChangeGetAuMode",        &Kernel::sceMpegChangeGetAuMode);
  add("sceMpeg",           "sceMpegQueryAtracEsSize",       &Kernel::sceMpegQueryAtracEsSize);
  add("sceMpeg",           "sceMpegAvcDecode",              &Kernel::sceMpegAvcDecode);
  add("sceMpeg",           "sceMpegAvcDecodeStop",          &Kernel::sceMpegAvcDecodeStop);
  add("sceMpeg",           "sceMpegAvcDecodeFlush",         &Kernel::sceMpegAvcDecodeFlush);
  add("sceMpeg",           "sceMpegAvcQueryYCbCrSize",      &Kernel::sceMpegAvcQueryYCbCrSize);
  add("sceMpeg",           "sceMpegAvcInitYCbCr",           &Kernel::sceMpegAvcInitYCbCr);
  add("sceMpeg",           "sceMpegAvcDecodeYCbCr",         &Kernel::sceMpegAvcDecodeYCbCr);
  add("sceMpeg",           "sceMpegAvcDecodeStopYCbCr",     &Kernel::sceMpegAvcDecodeStopYCbCr);
  add("sceMpeg",           "sceMpegAvcDecodeDetail",        &Kernel::sceMpegAvcDecodeDetail);
  add("sceMpeg",           "sceMpegAvcCsc",                 &Kernel::sceMpegAvcCsc);
  add("sceMpeg",           "sceMpegAtracDecode",            &Kernel::sceMpegAtracDecode);
  add("sceMpeg",           "sceMpegGetAvcEsAu",             &Kernel::sceMpegGetAvcAu);
  //movies' headers (psmf.cpp) and the movie player (psmfplayer.cpp), libraries games ship as modules of their own,
  //which the kernel stands in for
  for(auto [name, handler] : std::initializer_list<std::pair<const char*, auto (Kernel::*)() -> void>>{
        {"scePsmfSetPsmf", &Kernel::scePsmfSetPsmf}, {"scePsmfVerifyPsmf", &Kernel::scePsmfVerifyPsmf},
        {"scePsmfQueryStreamOffset", &Kernel::scePsmfQueryStreamOffset},
        {"scePsmfQueryStreamSize", &Kernel::scePsmfQueryStreamSize},
        {"scePsmfGetPsmfVersion", &Kernel::scePsmfGetPsmfVersion},
        {"scePsmfGetHeaderSize", &Kernel::scePsmfGetHeaderSize},
        {"scePsmfGetStreamSize", &Kernel::scePsmfGetStreamSize},
        {"scePsmfGetPresentationStartTime", &Kernel::scePsmfGetPresentationStartTime},
        {"scePsmfGetPresentationEndTime", &Kernel::scePsmfGetPresentationEndTime},
        {"scePsmfGetNumberOfStreams", &Kernel::scePsmfGetNumberOfStreams},
        {"scePsmfGetNumberOfSpecificStreams", &Kernel::scePsmfGetNumberOfSpecificStreams},
        {"scePsmfSpecifyStream", &Kernel::scePsmfSpecifyStream},
        {"scePsmfSpecifyStreamWithStreamType", &Kernel::scePsmfSpecifyStreamWithStreamType},
        {"scePsmfSpecifyStreamWithStreamTypeNumber", &Kernel::scePsmfSpecifyStreamWithStreamTypeNumber},
        {"scePsmfGetCurrentStreamNumber", &Kernel::scePsmfGetCurrentStreamNumber},
        {"scePsmfGetCurrentStreamType", &Kernel::scePsmfGetCurrentStreamType},
        {"scePsmfGetVideoInfo", &Kernel::scePsmfGetVideoInfo}, {"scePsmfGetAudioInfo", &Kernel::scePsmfGetAudioInfo},
        {"scePsmfGetNumberOfEPentries", &Kernel::scePsmfGetNumberOfEPentries},
        {"scePsmfCheckEPmap", &Kernel::scePsmfCheckEPmap}, {"scePsmfGetEPWithId", &Kernel::scePsmfGetEPWithId},
        {"scePsmfGetEPWithTimestamp", &Kernel::scePsmfGetEPWithTimestamp},
        {"scePsmfGetEPidWithTimestamp", &Kernel::scePsmfGetEPidWithTimestamp},
        {"scePsmfGetNumberOfPsmfMarks", &Kernel::scePsmfGetNumberOfPsmfMarks},
        {"scePsmfGetPsmfMark", &Kernel::scePsmfGetPsmfMark}}) {
    add("scePsmf", name, handler);
  }
  for(auto [name, handler] : std::initializer_list<std::pair<const char*, auto (Kernel::*)() -> void>>{
        {"scePsmfPlayerCreate", &Kernel::scePsmfPlayerCreate}, {"scePsmfPlayerDelete", &Kernel::scePsmfPlayerDelete},
        {"scePsmfPlayerSetTempBuf", &Kernel::scePsmfPlayerSetTempBuf},
        {"scePsmfPlayerSetPsmf", &Kernel::scePsmfPlayerSetPsmf},
        {"scePsmfPlayerSetPsmfCB", &Kernel::scePsmfPlayerSetPsmfCB},
        {"scePsmfPlayerSetPsmfOffset", &Kernel::scePsmfPlayerSetPsmfOffset},
        {"scePsmfPlayerSetPsmfOffsetCB", &Kernel::scePsmfPlayerSetPsmfOffsetCB},
        {"scePsmfPlayerReleasePsmf", &Kernel::scePsmfPlayerReleasePsmf},
        {"scePsmfPlayerGetPsmfInfo", &Kernel::scePsmfPlayerGetPsmfInfo},
        {"scePsmfPlayerConfigPlayer", &Kernel::scePsmfPlayerConfigPlayer},
        {"scePsmfPlayerStart", &Kernel::scePsmfPlayerStart}, {"scePsmfPlayerStop", &Kernel::scePsmfPlayerStop},
        {"scePsmfPlayerUpdate", &Kernel::scePsmfPlayerUpdate},
        {"scePsmfPlayerGetVideoData", &Kernel::scePsmfPlayerGetVideoData},
        {"scePsmfPlayerGetAudioData", &Kernel::scePsmfPlayerGetAudioData},
        {"scePsmfPlayerGetAudioOutSize", &Kernel::scePsmfPlayerGetAudioOutSize},
        {"scePsmfPlayerGetCurrentStatus", &Kernel::scePsmfPlayerGetCurrentStatus},
        {"scePsmfPlayerGetCurrentPts", &Kernel::scePsmfPlayerGetCurrentPts},
        {"scePsmfPlayerGetCurrentPlayMode", &Kernel::scePsmfPlayerGetCurrentPlayMode},
        {"scePsmfPlayerChangePlayMode", &Kernel::scePsmfPlayerChangePlayMode},
        {"scePsmfPlayerGetCurrentVideoStream", &Kernel::scePsmfPlayerGetCurrentVideoStream},
        {"scePsmfPlayerGetCurrentAudioStream", &Kernel::scePsmfPlayerGetCurrentAudioStream},
        {"scePsmfPlayerSelectVideo", &Kernel::scePsmfPlayerSelectVideo},
        {"scePsmfPlayerSelectAudio", &Kernel::scePsmfPlayerSelectAudio},
        {"scePsmfPlayerSelectSpecificVideo", &Kernel::scePsmfPlayerSelectSpecificVideo},
        {"scePsmfPlayerSelectSpecificAudio", &Kernel::scePsmfPlayerSelectSpecificAudio},
        {"scePsmfPlayerBreak", &Kernel::scePsmfPlayerBreak}}) {
    add("scePsmfPlayer", name, handler);
  }
  //listed by one of the specification's accounts without a name: its NID alone
  addNID("scePsmfPlayer",  "scePsmfPlayer_340C12CB",        0x340c'12cb, &Kernel::scePsmfPlayerUnknown);
  //the system's fonts (font.cpp)
  add("sceLibFont",        "sceFontNewLib",                 &Kernel::sceFontNewLib);
  add("sceLibFont",        "sceFontDoneLib",                &Kernel::sceFontDoneLib);
  add("sceLibFont",        "sceFontClose",                  &Kernel::sceFontClose);
  add("sceLibFont",        "sceFontFlush",                  &Kernel::sceFontFlush);
  add("sceLibFont",        "sceFontSetAltCharacterCode",    &Kernel::sceFontSetAltCharacterCode);
  add("sceLibFont",        "sceFontGetNumFontList",         &Kernel::sceFontGetNumFontList);
  add("sceLibFont",        "sceFontGetFontList",            &Kernel::sceFontGetFontList);
  add("sceLibFont",        "sceFontGetFontInfoByIndexNumber", &Kernel::sceFontGetFontInfoByIndexNumber);
  add("sceLibFont",        "sceFontFindOptimumFont",        &Kernel::sceFontFindOptimumFont);
  add("sceLibFont",        "sceFontFindFont",               &Kernel::sceFontFindFont);
  add("sceLibFont",        "sceFontOpen",                   &Kernel::sceFontOpen);
  add("sceLibFont",        "sceFontOpenUserMemory",         &Kernel::sceFontOpenUserMemory);
  add("sceLibFont",        "sceFontOpenUserFile",           &Kernel::sceFontOpenUserFile);
  add("sceLibFont",        "sceFontGetFontInfo",            &Kernel::sceFontGetFontInfo);
  add("sceLibFont",        "sceFontGetCharInfo",            &Kernel::sceFontGetCharInfo);
  add("sceLibFont",        "sceFontGetCharImageRect",       &Kernel::sceFontGetCharImageRect);
  add("sceLibFont",        "sceFontGetCharGlyphImage",      &Kernel::sceFontGetCharGlyphImage);
  add("sceLibFont",        "sceFontGetCharGlyphImage_Clip", &Kernel::sceFontGetCharGlyphImage_Clip);
  add("sceLibFont",        "sceFontGetShadowInfo",          &Kernel::sceFontGetShadowInfo);
  add("sceLibFont",        "sceFontGetShadowImageRect",     &Kernel::sceFontGetShadowImageRect);
  add("sceLibFont",        "sceFontGetShadowGlyphImage",    &Kernel::sceFontGetShadowGlyphImage);
  add("sceLibFont",        "sceFontGetShadowGlyphImage_Clip", &Kernel::sceFontGetShadowGlyphImage_Clip);
  add("sceLibFont",        "sceFontSetResolution",          &Kernel::sceFontSetResolution);
  add("sceLibFont",        "sceFontPointToPixelH",          &Kernel::sceFontPointToPixelH);
  add("sceLibFont",        "sceFontPointToPixelV",          &Kernel::sceFontPointToPixelV);
  add("sceLibFont",        "sceFontPixelToPointH",          &Kernel::sceFontPixelToPointH);
  add("sceLibFont",        "sceFontPixelToPointV",          &Kernel::sceFontPixelToPointV);
  add("sceAtrac3plus",     "sceAtracGetAtracID",            &Kernel::sceAtracGetAtracID);
  add("sceAtrac3plus",     "sceAtracReleaseAtracID",        &Kernel::sceAtracReleaseAtracID);
  add("sceAtrac3plus",     "sceAtracReinit",                &Kernel::sceAtracReinit);
  add("sceAtrac3plus",     "sceAtracSetData",               &Kernel::sceAtracSetData);
  add("sceAtrac3plus",     "sceAtracSetDataAndGetID",       &Kernel::sceAtracSetDataAndGetID);
  add("sceAtrac3plus",     "sceAtracSetHalfwayBuffer",      &Kernel::sceAtracSetHalfwayBuffer);
  add("sceAtrac3plus",     "sceAtracSetHalfwayBufferAndGetID", &Kernel::sceAtracSetHalfwayBufferAndGetID);
  add("sceAtrac3plus",     "sceAtracSetMOutData",           &Kernel::sceAtracSetMOutData);
  add("sceAtrac3plus",     "sceAtracSetMOutDataAndGetID",   &Kernel::sceAtracSetMOutDataAndGetID);
  add("sceAtrac3plus",     "sceAtracSetMOutHalfwayBuffer",  &Kernel::sceAtracSetMOutHalfwayBuffer);
  add("sceAtrac3plus",     "sceAtracSetMOutHalfwayBufferAndGetID", &Kernel::sceAtracSetMOutHalfwayBufferAndGetID);
  add("sceAtrac3plus",     "sceAtracDecodeData",            &Kernel::sceAtracDecodeData);
  add("sceAtrac3plus",     "sceAtracGetRemainFrame",        &Kernel::sceAtracGetRemainFrame);
  add("sceAtrac3plus",     "sceAtracGetStreamDataInfo",     &Kernel::sceAtracGetStreamDataInfo);
  add("sceAtrac3plus",     "sceAtracAddStreamData",         &Kernel::sceAtracAddStreamData);
  add("sceAtrac3plus",     "sceAtracGetNextDecodePosition", &Kernel::sceAtracGetNextDecodePosition);
  add("sceAtrac3plus",     "sceAtracGetNextSample",         &Kernel::sceAtracGetNextSample);
  add("sceAtrac3plus",     "sceAtracGetMaxSample",          &Kernel::sceAtracGetMaxSample);
  add("sceAtrac3plus",     "sceAtracGetSoundSample",        &Kernel::sceAtracGetSoundSample);
  add("sceAtrac3plus",     "sceAtracGetChannel",            &Kernel::sceAtracGetChannel);
  add("sceAtrac3plus",     "sceAtracGetOutputChannel",      &Kernel::sceAtracGetOutputChannel);
  add("sceAtrac3plus",     "sceAtracGetBitrate",            &Kernel::sceAtracGetBitrate);
  add("sceAtrac3plus",     "sceAtracSetLoopNum",            &Kernel::sceAtracSetLoopNum);
  add("sceAtrac3plus",     "sceAtracGetLoopStatus",         &Kernel::sceAtracGetLoopStatus);
  add("sceAtrac3plus",     "sceAtracGetInternalErrorInfo",  &Kernel::sceAtracGetInternalErrorInfo);
  //both spellings: games import either (the GTAs the second)
  add("sceAtrac3plus",     "sceAtracGetBufferInfoForReseting", &Kernel::sceAtracGetBufferInfoForResetting);
  add("sceAtrac3plus",     "sceAtracGetBufferInfoForResetting", &Kernel::sceAtracGetBufferInfoForResetting);
  add("sceAtrac3plus",     "sceAtracResetPlayPosition",     &Kernel::sceAtracResetPlayPosition);
  add("sceAtrac3plus",     "sceAtracIsSecondBufferNeeded",  &Kernel::sceAtracIsSecondBufferNeeded);
  add("sceAtrac3plus",     "sceAtracGetSecondBufferInfo",   &Kernel::sceAtracGetSecondBufferInfo);
  add("sceAtrac3plus",     "sceAtracSetSecondBuffer",       &Kernel::sceAtracSetSecondBuffer);
  add("sceAtrac3plus",     "_sceAtracGetContextAddress",    &Kernel::_sceAtracGetContextAddress);
  for(auto [name, handler] : std::initializer_list<std::pair<const char*, auto (Kernel::*)() -> void>>{
        {"sceMp3InitResource", &Kernel::sceMp3InitResource}, {"sceMp3TermResource", &Kernel::sceMp3TermResource},
        {"sceMp3ReserveMp3Handle", &Kernel::sceMp3ReserveMp3Handle},
        {"sceMp3ReleaseMp3Handle", &Kernel::sceMp3ReleaseMp3Handle},
        {"sceMp3GetInfoToAddStreamData", &Kernel::sceMp3GetInfoToAddStreamData},
        {"sceMp3NotifyAddStreamData", &Kernel::sceMp3NotifyAddStreamData},
        {"sceMp3CheckStreamDataNeeded", &Kernel::sceMp3CheckStreamDataNeeded}, {"sceMp3Init", &Kernel::sceMp3Init},
        {"sceMp3Decode", &Kernel::sceMp3Decode}, {"sceMp3SetLoopNum", &Kernel::sceMp3SetLoopNum},
        {"sceMp3GetLoopNum", &Kernel::sceMp3GetLoopNum},
        {"sceMp3GetSumDecodedSample", &Kernel::sceMp3GetSumDecodedSample},
        {"sceMp3GetMaxOutputSample", &Kernel::sceMp3GetMaxOutputSample},
        {"sceMp3GetSamplingRate", &Kernel::sceMp3GetSamplingRate}, {"sceMp3GetBitRate", &Kernel::sceMp3GetBitRate},
        {"sceMp3GetMp3ChannelNum", &Kernel::sceMp3GetMp3ChannelNum},
        {"sceMp3GetFrameNum", &Kernel::sceMp3GetFrameNum},
        {"sceMp3GetMPEGVersion", &Kernel::sceMp3GetMPEGVersion},
        {"sceMp3ResetPlayPosition", &Kernel::sceMp3ResetPlayPosition},
        {"sceMp3ResetPlayPositionByFrame", &Kernel::sceMp3ResetPlayPositionByFrame}}) {
    add("sceMp3", name, handler);
  }
  codecDecoders(*this);
  cpu.syscallHook = [this](u32 code) { return syscall(code); };
  ge.log = [this](const std::string& text) { note("GE: " + text); };
}

//A function's NID: the first four bytes of the SHA-1 digest of its name (kirk.cpp), as a little-endian word.
auto Kernel::nid(const std::string& name) -> u32 {
  auto digest = sha1((const u8*)name.data(), name.size());
  return digest[0] | digest[1] << 8 | digest[2] << 16 | u32(digest[3]) << 24;
}

//Back to how the PSP is when it has just started a program's loading: no threads, no memory handed out, the clock
//at zero. The memory map must have been powered first: the trampoline goes into kernel memory.
auto Kernel::power() -> void {
  module = {};
  modules.clear();
  programUID = 0;
  exited = false;
  stuck = false;
  cycles = 0;
  nextUID = 0x100;
  imports.clear();
  threads.clear();
  semaphores.clear();
  lwMutexes.clear();
  mutexes.clear();
  alarms.clear();
  vtimers.clear();
  current = nullptr;
  readySequence = 0;
  nextVblank = VblankCycles;
  vblanks = 0;
  blocks.clear();
  largeMemory = false;
  sdkVersion = 0;
  compilerVersion = 0;
  powerState = {};
  audio = {};
  audio.output.samples.assign(Audio::OutputFrames * 2, 0);
  sas = {};
  for(auto& atrac : atracs) atrac = {};
  atracPlusIDs = atracClassicIDs = 2;
  atracContexts = 0;
  for(auto& mp3 : mp3s) mp3 = {};
  mp3Terminated = false;
  dispatchSuspended = false;
  fontResolution[0] = fontResolution[1] = 128.0f;
  fontLibraries.clear();
  openFonts.clear();
  fontCalls.clear();
  nextFontID = 1;
  mpegCalls.clear();
  mpegStreams.clear();
  psmfPlayer = {};
  dialog = {};
  utilityModules.clear();
  imposeLanguage = imposeButton = 1;
  geTranslation = 0x400;
  files.clear();
  nextFile = 3;
  workingDirectory = "ms0:/";
  controller = {};
  display = {};
  calls.clear();
  interrupting = false;
  callKind = Call::Plain;
  callID = 0;
  interruptsEnabled = true;
  rescheduleAfter = false;
  callResumesGe = false;
  for(auto& handler : vblankSubs) handler = {};
  for(auto& handler : geSubs) handler = {};
  vblankPending = false;
  eventFlags.clear();
  pools.clear();
  pipes.clear();
  mailboxes.clear();
  callbacks.clear();
  exitCallback = 0;
  memoryStickCallbacks.clear();
  umdCallback = 0;
  ge.power();  //the GE starts afresh with the program, its driver too
  for(auto& list : geLists) list = {};
  geQueue.clear();
  geFree.clear();
  for(u32 index = 0; index < 64; index++) geFree.push_back(index);
  for(auto& callback : geCallbacks) callback = {};
  geRunning = -1;
  geBusy = false;
  geSuspended = false;
  geFinishing = -1;
  geLeft = GeBudget;
  geCommands = 0;
  startTime = u64(std::time(nullptr)) * 1'000'000;
  memory.write(4, Trampoline, ThreadReturnCode << 6 | 0x0c);    //syscall: the thread's entry function returned
  memory.write(4, Trampoline + 4, 0x0000'000d);                  //break: never reached
  memory.write(4, Trampoline + 8, CallReturnCode << 6 | 0x0c);  //syscall: a call into the program returned
  memory.write(4, Trampoline + 12, 0x0000'000d);
  memory.write(4, Trampoline + 16, ModuleReturnCode << 6 | 0x0c);  //syscall: a module_start or module_stop returned
  memory.write(4, Trampoline + 20, 0x0000'000d);
  memory.write(4, Trampoline + 24, CallbackReturnCode << 6 | 0x0c);  //syscall: a thread's callback returned
  memory.write(4, Trampoline + 28, 0x0000'000d);
  memory.write(4, Trampoline + 32, FontReturnCode << 6 | 0x0c);  //syscall: the font library's call returned
  memory.write(4, Trampoline + 36, 0x0000'000d);
  memory.write(4, Trampoline + 40, MpegReturnCode << 6 | 0x0c);  //syscall: a ringbuffer's callback returned
  memory.write(4, Trampoline + 44, 0x0000'000d);
}

//Loads a program (an EBOOT.PBP, or an ELF on its own) and starts its first thread, as the PSP does when a game is
//chosen: the thread runs the module's entry point with the program's path as its argument (what C sees as argv[0]),
//its global pointer set, and a 256 KiB stack. It starts afresh, as the PSP does: whatever an earlier program left
//(threads, memory handed out, having exited) goes first. Returns false, with error saying why, if it can't, and
//leaves nothing of the program behind but what the loader wrote to memory.
auto Kernel::load(const u8* data, u64 size, const std::string& path, std::string& error) -> bool {
  power();
  bool loaded = start(data, size, path, error);
  if(!loaded) power();
  return loaded;
}

auto Kernel::start(const u8* data, u64 size, const std::string& path, std::string& error) -> bool {
  largeMemory = parameterNumber(programParameters(data, size, path), "MEMSIZE", 0) == 1;
  u64 offset = 0, length = size;
  if(Loader::programInPBP(data, size, offset, length)) {
    data += offset;
    size = length;
  }
  //A PRX goes at the start of the user partition (nothing is there yet); a static executable where it was linked.
  error = Loader::load(memory, data, size, UserMemory, [this](const std::string& library, u32 nid) {
    return importCode(library, nid);
  }, module);
  if(!error.empty()) return false;
  //The program's memory: one block from its first segment to the end of its last, as the PSP's loader gives a module
  //one. Segments may share a 256-byte step (blocks start on one: Lumines' data starts 8 bytes after its code ends),
  //but not bytes.
  auto parts = module.segments;
  std::sort(parts.begin(), parts.end(), [](auto& a, auto& b) { return a.address < b.address; });
  u32 low = ~0u, high = 0;
  for(auto& segment : parts) {
    if(!segment.size) continue;
    if(segment.address < high) {
      error = "the program's segments overlap each other";
      return false;
    }
    low = std::min(low, segment.address & ~255u);
    high = segment.address + segment.size;  //the loader saw that each fits in memory
  }
  if(high) {
    auto block = allocate(high - low, 2, low, module.name);
    if(!block || block->address != low) {  //it must be exactly where the program is
      error = "the program's memory overlaps memory already handed out";
      return false;
    }
  }
  for(auto& skipped : module.skipped) note("the loader left out " + skipped);
  programUID = newUID();  //the program is a module too, the first

  cpu.power(module.entry);
  if(auto folder = programFolder(path); !folder.empty()) workingDirectory = folder;  //relative paths start there
  u32 pathLength = path.size() + 1;  //the path, with its terminating zero
  if(pathLength > 4_KiB) {
    error = "the program's path is too long";
    return false;
  }
  u32 argument = Trampoline + 0x100;  //put where the new thread's start can copy it from
  memory.copyIn(argument, path.c_str(), pathLength);
  s32 uid = createThread(module.name, module.entry, 0x20, 256_KiB, 0x8000'4000, module.gp);  //user mode, uses the VFPU
  if(uid < 0) {
    error = "no memory for the program's first thread";
    return false;
  }
  if(!argumentFits(*threads[uid], pathLength)) {
    error = "the program's path is too long";
    return false;
  }
  startThread(*threads[uid], pathLength, argument);
  return true;
}

//Runs the program for up to budget cycles of the PSP's time (less if it ends, or every thread waits on something that
//will never come): threads run, and while they all wait, time jumps to the next thing due, waking threads as their
//moments come. A frame's worth (VblankCycles) is a frame, however much of it the program spent waiting. Returns how
//many cycles passed.
auto Kernel::run(u64 budget) -> u64 {
  u64 start = cycles, end = cycles + budget;
  while(cycles < end && !exited) {
    if(geBusy) geRun();
    startCall();  //a call into the program waiting its turn runs on whatever's in the CPU, a thread or nothing
    if(!current && !interrupting) {
      reschedule();
      if(!current && !idle(end)) break;
      continue;
    }
    stuck = false;  //something runs: should nothing run again later, that's worth a note again
    counted = 0;
    u64 ran = cpu.run(std::min(end - cycles, std::max<u64>(1, untilNextEvent())));
    cycles += ran - counted;
    counted = ran;  //(what cpu.instructionsRun stays at till the next run: a call made outside one adds nothing)
    if((current || interrupting) && cpu.scc.halted) {  //it stopped by itself: a halt, or an exception nobody handled
      note(interrupting ? "the CPU stopped in a call into the program" : "the CPU stopped in thread " + current->name);
      break;
    }
    events();
  }
  return cycles - start;
}

//The code a library's function gets: the same for every import of the same NID, so a stub works whichever module
//it's in. NIDs are names' hashes, so a function is found by its NID alone.
auto Kernel::importCode(const std::string& library, u32 nid) -> u32 {
  for(u32 n = 0; n < imports.size(); n++) {
    if(imports[n].nid == nid) return FirstImportCode + n;
  }
  const Function* function = nullptr;
  for(auto& candidate : functions) if(candidate.nid == nid) function = &candidate;
  imports.push_back({library, nid, function, false});
  return FirstImportCode + imports.size() - 1;
}

//The CPU's syscall instruction: a library function's code, or the kernel's own (a thread's entry returning). The
//clock first catches up to the instructions the CPU has run so far in this go (run() adds the rest as it ends): a
//function reading or setting a time between two waits sees the time it's called at, not the time the go began
//(which a thread running from one wait to the next a whole frame long had seen till then: a timer set 2.5 ms on
//went off at once, its moment already passed). The count is the instructions before the syscall, which both engines
//give alike (the recompiler's block as far as it has got), so a program sees the same time with either. Then the go
//ends by whatever the function made due.
auto Kernel::syscall(u32 code) -> bool {
  u64 before = cpu.instructionsBefore();
  if(before > counted) cycles += before - counted;
  counted = before;
  bool handled = dispatch(code);
  runUntilNextEvent();
  return handled;
}

//The function a syscall's code stands for.
auto Kernel::dispatch(u32 code) -> bool {
  if(code == ThreadReturnCode) {
    threadReturned();
    return true;
  }
  if(code == CallReturnCode) {
    callReturned();
    return true;
  }
  if(code == ModuleReturnCode) {
    moduleReturned();
    return true;
  }
  if(code == CallbackReturnCode) {
    callbackReturned();
    return true;
  }
  if(code == FontReturnCode) {
    fontReturned();
    return true;
  }
  if(code == MpegReturnCode) {
    mpegReturned();
    return true;
  }
  if(code < FirstImportCode || code - FirstImportCode >= imports.size()) {
    note("a syscall (code " + std::to_string(code) + ") that no import stands for");
    return false;
  }
  auto& import = imports[code - FirstImportCode];
  if(!import.function) {
    if(!import.reported) {
      char text[96];
      std::snprintf(text, sizeof(text), "not implemented yet: %s function %08x", import.library.c_str(), import.nid);
      note(text);
      import.reported = true;
    }
    result(ErrorNotYetLinked);
    return true;
  }
  (this->*import.function->handler)();
  startCall();  //what the function set off (a display list finishing) may call into the program now
  return true;
}

//The CPU's go stops by the next thing due (run() works out what comes when as the go begins, and what's due comes as
//it ends): a system function may have made something due sooner than the go was to last, a better thread's delay or
//timeout, or a timer, while a worse thread runs on without waiting; it comes on time. (Before, the worse thread kept
//the CPU to the end of its go, a frame at most: pspautotests' threads/scheduling/preemptuser had a better thread's
//1000-microsecond delays take 8 to 17 ms while main spun.)
auto Kernel::runUntilNextEvent() -> void {
  u64 limit = cpu.instructionsBefore() + untilNextEvent();
  if(limit < cpu.runLimit) cpu.runLimit = limit;
}

//The n-th argument of the function being called (a0-a3, then t0-t3).
auto Kernel::arg(u32 n) const -> u32 {
  return n < 4 ? cpu.ipu.r[4 + n] : cpu.ipu.r[8 + n - 4];
}

//The function's result, in v0. A function that makes its thread wait sets the result when the thread wakes
//instead (ready()), as by then another thread's registers are in the CPU.
auto Kernel::result(u32 value) -> void {
  cpu.ipu.r[2] = value;
}

auto Kernel::note(const std::string& text) -> void {
  if(log) log(text);
}

}
