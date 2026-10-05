#include "kernel.hpp"
#include "../cpu/allegrex.hpp"
#include "../memory/memory.hpp"
#include "../ge/ge.hpp"

namespace ares::PlayStationPortable {

#include "threads.cpp"
#include "interrupts.cpp"
#include "events.cpp"
#include "sysmem.cpp"
#include "unpack.cpp"
#include "disc.cpp"
#include "io.cpp"
#include "umd.cpp"
#include "ctrl.cpp"
#include "display.cpp"
#include "ge.cpp"
#include "pools.cpp"
#include "audio.cpp"
#include "utility.cpp"
#include "power.cpp"
#include "system.cpp"
#include "serialization.cpp"

Kernel::Kernel(Allegrex& cpu, Memory& memory, GE& ge) : cpu(cpu), memory(memory), ge(ge) {
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
  add("ThreadManForUser",  "sceKernelWaitThreadEnd",        &Kernel::sceKernelWaitThreadEnd);
  add("ThreadManForUser",  "sceKernelWaitThreadEndCB",      &Kernel::sceKernelWaitThreadEndCB);
  add("ThreadManForUser",  "sceKernelCreateSema",           &Kernel::sceKernelCreateSema);
  add("ThreadManForUser",  "sceKernelDeleteSema",           &Kernel::sceKernelDeleteSema);
  add("ThreadManForUser",  "sceKernelSignalSema",           &Kernel::sceKernelSignalSema);
  add("ThreadManForUser",  "sceKernelWaitSema",             &Kernel::sceKernelWaitSema);
  add("ThreadManForUser",  "sceKernelWaitSemaCB",           &Kernel::sceKernelWaitSemaCB);
  add("ThreadManForUser",  "sceKernelPollSema",             &Kernel::sceKernelPollSema);
  add("ThreadManForUser",  "sceKernelReferSemaStatus",      &Kernel::sceKernelReferSemaStatus);
  add("ThreadManForUser",  "sceKernelChangeThreadPriority", &Kernel::sceKernelChangeThreadPriority);
  add("ThreadManForUser",  "sceKernelGetThreadExitStatus",  &Kernel::sceKernelGetThreadExitStatus);
  add("ThreadManForUser",  "sceKernelTerminateThread",      &Kernel::sceKernelTerminateThread);
  add("ThreadManForUser",  "sceKernelTerminateDeleteThread", &Kernel::sceKernelTerminateDeleteThread);
  add("ThreadManForUser",  "sceKernelSuspendThread",        &Kernel::sceKernelSuspendThread);
  add("ThreadManForUser",  "sceKernelResumeThread",         &Kernel::sceKernelResumeThread);
  add("ThreadManForUser",  "sceKernelChangeCurrentThreadAttr", &Kernel::sceKernelChangeCurrentThreadAttr);
  add("ThreadManForUser",  "sceKernelGetThreadStackFreeSize", &Kernel::sceKernelGetThreadStackFreeSize);
  add("ThreadManForUser",  "sceKernelReferThreadProfiler",  &Kernel::sceKernelReferThreadProfiler);
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
  add("ThreadManForUser",  "sceKernelSysClock2USec",        &Kernel::sceKernelSysClock2USec);
  add("ThreadManForUser",  "sceKernelSysClock2USecWide",    &Kernel::sceKernelSysClock2USecWide);
  add("ThreadManForUser",  "sceKernelCreateLwMutex",        &Kernel::sceKernelCreateLwMutex);
  add("ThreadManForUser",  "sceKernelDeleteLwMutex",        &Kernel::sceKernelDeleteLwMutex);
  add("ThreadManForUser",  "sceKernelGetSystemTimeLow",     &Kernel::sceKernelGetSystemTimeLow);
  add("ThreadManForUser",  "sceKernelGetSystemTimeWide",    &Kernel::sceKernelGetSystemTimeWide);
  add("ThreadManForUser",  "sceKernelGetSystemTime",        &Kernel::sceKernelGetSystemTime);
  add("ThreadManForUser",  "sceKernelCreateEventFlag",      &Kernel::sceKernelCreateEventFlag);
  add("ThreadManForUser",  "sceKernelDeleteEventFlag",      &Kernel::sceKernelDeleteEventFlag);
  add("ThreadManForUser",  "sceKernelSetEventFlag",         &Kernel::sceKernelSetEventFlag);
  add("ThreadManForUser",  "sceKernelClearEventFlag",       &Kernel::sceKernelClearEventFlag);
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
  add("Kernel_Library",    "sceKernelCpuSuspendIntr",       &Kernel::sceKernelCpuSuspendIntr);
  add("Kernel_Library",    "sceKernelCpuResumeIntr",        &Kernel::sceKernelCpuResumeIntr);
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
  add("sceSuspendForUser", "sceKernelPowerTick",            &Kernel::sceKernelPowerTick);
  add("sceSuspendForUser", "sceKernelPowerLock",            &Kernel::sceKernelPowerLock);
  add("sceSuspendForUser", "sceKernelPowerUnlock",          &Kernel::sceKernelPowerUnlock);
  add("sceSuspendForUser", "sceKernelVolatileMemTryLock",   &Kernel::sceKernelVolatileMemTryLock);
  add("sceSuspendForUser", "sceKernelVolatileMemUnlock",    &Kernel::sceKernelVolatileMemUnlock);
  add("sceWlanDrv",        "sceWlanGetSwitchState",         &Kernel::sceWlanGetSwitchState);
  add("sceWlanDrv",        "sceWlanGetEtherAddr",           &Kernel::sceWlanGetEtherAddr);
  add("sceImpose",         "sceImposeSetLanguageMode",      &Kernel::sceImposeSetLanguageMode);
  add("sceDmac",           "sceDmacMemcpy",                 &Kernel::sceDmacMemcpy);
  add("SysMemUserForUser", "sceKernelAllocPartitionMemory", &Kernel::sceKernelAllocPartitionMemory);
  add("SysMemUserForUser", "sceKernelFreePartitionMemory",  &Kernel::sceKernelFreePartitionMemory);
  add("SysMemUserForUser", "sceKernelGetBlockHeadAddr",     &Kernel::sceKernelGetBlockHeadAddr);
  add("SysMemUserForUser", "sceKernelMaxFreeMemSize",       &Kernel::sceKernelMaxFreeMemSize);
  add("SysMemUserForUser", "sceKernelTotalFreeMemSize",     &Kernel::sceKernelTotalFreeMemSize);
  add("SysMemUserForUser", "sceKernelSetCompiledSdkVersion", &Kernel::sceKernelSetCompiledSdkVersion);
  add("SysMemUserForUser", "sceKernelGetCompiledSdkVersion", &Kernel::sceKernelGetCompiledSdkVersion);
  add("SysMemUserForUser", "sceKernelSetCompilerVersion",   &Kernel::sceKernelSetCompilerVersion);
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
  add("sceGe_user",        "sceGeEdramGetAddr",             &Kernel::sceGeEdramGetAddr);
  add("sceGe_user",        "sceGeEdramGetSize",             &Kernel::sceGeEdramGetSize);
  add("sceGe_user",        "sceGeListEnQueue",              &Kernel::sceGeListEnQueue);
  add("sceGe_user",        "sceGeListEnQueueHead",          &Kernel::sceGeListEnQueueHead);
  add("sceGe_user",        "sceGeListDeQueue",              &Kernel::sceGeListDeQueue);
  add("sceGe_user",        "sceGeListUpdateStallAddr",      &Kernel::sceGeListUpdateStallAddr);
  add("sceGe_user",        "sceGeListSync",                 &Kernel::sceGeListSync);
  add("sceGe_user",        "sceGeDrawSync",                 &Kernel::sceGeDrawSync);
  add("sceGe_user",        "sceGeSetCallback",              &Kernel::sceGeSetCallback);
  add("sceGe_user",        "sceGeUnsetCallback",            &Kernel::sceGeUnsetCallback);
  add("sceGe_user",        "sceGeContinue",                 &Kernel::sceGeContinue);
  add("sceGe_user",        "sceGeGetCmd",                   &Kernel::sceGeGetCmd);
  add("sceGe_user",        "sceGeGetMtx",                   &Kernel::sceGeGetMtx);
  add("sceGe_user",        "sceGeSaveContext",              &Kernel::sceGeSaveContext);
  add("sceGe_user",        "sceGeRestoreContext",           &Kernel::sceGeRestoreContext);
  add("LoadExecForUser",   "sceKernelExitGame",             &Kernel::sceKernelExitGame);
  add("LoadExecForUser",   "sceKernelRegisterExitCallback", &Kernel::sceKernelRegisterExitCallback);
  add("ModuleMgrForUser",  "sceKernelSelfStopUnloadModule", &Kernel::sceKernelSelfStopUnloadModule);
  addNID("ModuleMgrForUser", "sceKernelStopUnloadSelfModuleWithStatus", 0x8f2d'f740,
         &Kernel::sceKernelStopUnloadSelfModuleWithStatus);
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
  //newlib's sockets: no network yet, so every call fails
  add("sceNetInet",        "sceNetInetClose",               &Kernel::sceNetInetUnavailable);
  add("sceNetInet",        "sceNetInetRecv",                &Kernel::sceNetInetUnavailable);
  add("sceNetInet",        "sceNetInetSend",                &Kernel::sceNetInetUnavailable);
  add("sceNetInet",        "sceNetInetGetErrno",            &Kernel::sceNetInetUnavailable);
  cpu.syscallHook = [this](u32 code) { return syscall(code); };
  ge.log = [this](const std::string& text) { note("GE: " + text); };
}

//A function's NID: the first four bytes of the SHA-1 hash of its name, as a little-endian word. SHA-1 as FIPS
//180-1 defines it: the message padded to a multiple of 64 bytes (a 1 bit, zeros, then its length in bits), each
//64-byte chunk stirred into five words through 80 rounds.
auto Kernel::nid(const std::string& name) -> u32 {
  std::vector<u8> message(name.begin(), name.end());
  u64 bits = u64(message.size()) * 8;
  message.push_back(0x80);
  while(message.size() % 64 != 56) message.push_back(0);
  for(s32 shift = 56; shift >= 0; shift -= 8) message.push_back(u8(bits >> shift));
  u32 hash[5] = {0x6745'2301, 0xefcd'ab89, 0x98ba'dcfe, 0x1032'5476, 0xc3d2'e1f0};
  for(size_t chunk = 0; chunk < message.size(); chunk += 64) {
    u32 w[80];
    for(u32 i = 0; i < 16; i++) {
      const u8* b = &message[chunk + i * 4];
      w[i] = u32(b[0]) << 24 | u32(b[1]) << 16 | u32(b[2]) << 8 | u32(b[3]);
    }
    for(u32 i = 16; i < 80; i++) w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    u32 a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4];
    for(u32 i = 0; i < 80; i++) {
      u32 f, k;
      if(i < 20)      f = (b & c) | (~b & d),          k = 0x5a82'7999;
      else if(i < 40) f = b ^ c ^ d,                   k = 0x6ed9'eba1;
      else if(i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8f1b'bcdc;
      else            f = b ^ c ^ d,                   k = 0xca62'c1d6;
      u32 t = std::rotl(a, 5) + f + e + k + w[i];
      e = d; d = c; c = std::rotl(b, 30); b = a; a = t;
    }
    hash[0] += a; hash[1] += b; hash[2] += c; hash[3] += d; hash[4] += e;
  }
  u32 first = hash[0];  //the hash's first four bytes, most significant first; read them as a little-endian word
  return first >> 24 | (first >> 8 & 0xff00) | (first << 8 & 0xff'0000) | first << 24;
}

//Back to how the PSP is when it has just started a program's loading: no threads, no memory handed out, the clock
//at zero. The memory map must have been powered first: the trampoline goes into kernel memory.
auto Kernel::power() -> void {
  module = {};
  exited = false;
  stuck = false;
  cycles = 0;
  nextUID = 0x100;
  imports.clear();
  threads.clear();
  semaphores.clear();
  lwMutexes.clear();
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
  dialog = {};
  utilityModules.clear();
  files.clear();
  nextFile = 3;
  workingDirectory = "ms0:/";
  controller = {};
  display = {};
  calls.clear();
  interrupting = false;
  interruptsEnabled = true;
  rescheduleAfter = false;
  callResumesGe = false;
  for(auto& handler : vblankSubs) handler = {};
  for(auto& handler : geSubs) handler = {};
  vblankPending = false;
  eventFlags.clear();
  pools.clear();
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
  memory.write(4, Trampoline + 16, CallbackReturnCode << 6 | 0x0c);  //syscall: a thread's callback returned
  memory.write(4, Trampoline + 20, 0x0000'000d);
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
    cycles += cpu.run(std::min(end - cycles, std::max<u64>(1, untilNextEvent())));
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

//The CPU's syscall instruction: a library function's code, or the kernel's own (a thread's entry returning).
auto Kernel::syscall(u32 code) -> bool {
  if(code == ThreadReturnCode) {
    threadReturned();
    return true;
  }
  if(code == CallReturnCode) {
    callReturned();
    return true;
  }
  if(code == CallbackReturnCode) {
    callbackReturned();
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
