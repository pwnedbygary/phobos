#include "kernel.hpp"
#include "../cpu/allegrex.hpp"
#include "../memory/memory.hpp"
#include "../ge/ge.hpp"

namespace ares::PlayStationPortable {

#include "threads.cpp"
#include "interrupts.cpp"
#include "events.cpp"
#include "sysmem.cpp"
#include "aes.cpp"
#include "keys.cpp"
#include "kirk.cpp"
#include "decrypt.cpp"
#include "unpack.cpp"
#include "disc.cpp"
#include "io.cpp"
#include "umd.cpp"
#include "ctrl.cpp"
#include "display.cpp"
#include "ge.cpp"
#include "system.cpp"
#include "serialization.cpp"

Kernel::Kernel(Allegrex& cpu, Memory& memory, GE& ge) : cpu(cpu), memory(memory), ge(ge) {
  auto add = [&](const char* library, const char* name, auto (Kernel::*handler)() -> void) {
    functions.push_back({library, name, handler, nid(name)});
  };
  add("ThreadManForUser",  "sceKernelCreateThread",         &Kernel::sceKernelCreateThread);
  add("ThreadManForUser",  "sceKernelStartThread",          &Kernel::sceKernelStartThread);
  add("ThreadManForUser",  "sceKernelExitThread",           &Kernel::sceKernelExitThread);
  add("ThreadManForUser",  "sceKernelExitDeleteThread",     &Kernel::sceKernelExitDeleteThread);
  add("ThreadManForUser",  "sceKernelDeleteThread",         &Kernel::sceKernelDeleteThread);
  add("ThreadManForUser",  "sceKernelGetThreadId",          &Kernel::sceKernelGetThreadId);
  add("ThreadManForUser",  "sceKernelReferThreadStatus",    &Kernel::sceKernelReferThreadStatus);
  add("ThreadManForUser",  "sceKernelDelayThread",          &Kernel::sceKernelDelayThread);
  add("ThreadManForUser",  "sceKernelSleepThread",          &Kernel::sceKernelSleepThread);
  add("ThreadManForUser",  "sceKernelWakeupThread",         &Kernel::sceKernelWakeupThread);
  add("ThreadManForUser",  "sceKernelWaitThreadEnd",        &Kernel::sceKernelWaitThreadEnd);
  add("ThreadManForUser",  "sceKernelCreateSema",           &Kernel::sceKernelCreateSema);
  add("ThreadManForUser",  "sceKernelDeleteSema",           &Kernel::sceKernelDeleteSema);
  add("ThreadManForUser",  "sceKernelSignalSema",           &Kernel::sceKernelSignalSema);
  add("ThreadManForUser",  "sceKernelWaitSema",             &Kernel::sceKernelWaitSema);
  add("ThreadManForUser",  "sceKernelPollSema",             &Kernel::sceKernelPollSema);
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
  add("ThreadManForUser",  "sceKernelWaitEventFlagCB",      &Kernel::sceKernelWaitEventFlag);
  add("ThreadManForUser",  "sceKernelPollEventFlag",        &Kernel::sceKernelPollEventFlag);
  add("ThreadManForUser",  "sceKernelReferEventFlagStatus", &Kernel::sceKernelReferEventFlagStatus);
  add("ThreadManForUser",  "sceKernelCreateCallback",       &Kernel::sceKernelCreateCallback);
  add("ThreadManForUser",  "sceKernelDeleteCallback",       &Kernel::sceKernelDeleteCallback);
  add("ThreadManForUser",  "sceKernelSleepThreadCB",        &Kernel::sceKernelSleepThreadCB);
  add("ThreadManForUser",  "sceKernelCheckCallback",        &Kernel::sceKernelCheckCallback);
  add("Kernel_Library",    "sceKernelLockLwMutex",          &Kernel::sceKernelLockLwMutex);
  add("Kernel_Library",    "sceKernelTryLockLwMutex",       &Kernel::sceKernelTryLockLwMutex);
  add("Kernel_Library",    "sceKernelUnlockLwMutex",        &Kernel::sceKernelUnlockLwMutex);
  add("Kernel_Library",    "sceKernelCpuSuspendIntr",       &Kernel::sceKernelCpuSuspendIntr);
  add("Kernel_Library",    "sceKernelCpuResumeIntr",        &Kernel::sceKernelCpuResumeIntr);
  add("UtilsForUser",      "sceKernelLibcGettimeofday",     &Kernel::sceKernelLibcGettimeofday);
  add("UtilsForUser",      "sceKernelLibcTime",             &Kernel::sceKernelLibcTime);
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
  add("SysMemUserForUser", "sceKernelAllocPartitionMemory", &Kernel::sceKernelAllocPartitionMemory);
  add("SysMemUserForUser", "sceKernelFreePartitionMemory",  &Kernel::sceKernelFreePartitionMemory);
  add("SysMemUserForUser", "sceKernelGetBlockHeadAddr",     &Kernel::sceKernelGetBlockHeadAddr);
  add("SysMemUserForUser", "sceKernelMaxFreeMemSize",       &Kernel::sceKernelMaxFreeMemSize);
  add("SysMemUserForUser", "sceKernelTotalFreeMemSize",     &Kernel::sceKernelTotalFreeMemSize);
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
  add("sceUtility",        "sceUtilityGetSystemParamInt",   &Kernel::sceUtilityGetSystemParamInt);
  //newlib's sockets: no network yet, so every call fails
  add("sceNetInet",        "sceNetInetClose",               &Kernel::sceNetInetUnavailable);
  add("sceNetInet",        "sceNetInetRecv",                &Kernel::sceNetInetUnavailable);
  add("sceNetInet",        "sceNetInetSend",                &Kernel::sceNetInetUnavailable);
  add("sceNetInet",        "sceNetInetGetErrno",            &Kernel::sceNetInetUnavailable);
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
  eventFlags.clear();
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
