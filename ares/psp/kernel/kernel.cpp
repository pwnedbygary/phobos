#include "kernel.hpp"
#include "../cpu/allegrex.hpp"
#include "../memory/memory.hpp"

namespace ares::PlayStationPortable {

#include "threads.cpp"
#include "sysmem.cpp"
#include "io.cpp"
#include "display.cpp"
#include "system.cpp"

Kernel::Kernel(Allegrex& cpu, Memory& memory) : cpu(cpu), memory(memory) {
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
  add("Kernel_Library",    "sceKernelLockLwMutex",          &Kernel::sceKernelLockLwMutex);
  add("Kernel_Library",    "sceKernelTryLockLwMutex",       &Kernel::sceKernelTryLockLwMutex);
  add("Kernel_Library",    "sceKernelUnlockLwMutex",        &Kernel::sceKernelUnlockLwMutex);
  add("SysMemUserForUser", "sceKernelAllocPartitionMemory", &Kernel::sceKernelAllocPartitionMemory);
  add("SysMemUserForUser", "sceKernelFreePartitionMemory",  &Kernel::sceKernelFreePartitionMemory);
  add("SysMemUserForUser", "sceKernelGetBlockHeadAddr",     &Kernel::sceKernelGetBlockHeadAddr);
  add("SysMemUserForUser", "sceKernelMaxFreeMemSize",       &Kernel::sceKernelMaxFreeMemSize);
  add("SysMemUserForUser", "sceKernelTotalFreeMemSize",     &Kernel::sceKernelTotalFreeMemSize);
  add("StdioForUser",      "sceKernelStdin",                &Kernel::sceKernelStdin);
  add("StdioForUser",      "sceKernelStdout",               &Kernel::sceKernelStdout);
  add("StdioForUser",      "sceKernelStderr",               &Kernel::sceKernelStderr);
  add("IoFileMgrForUser",  "sceIoWrite",                    &Kernel::sceIoWrite);
  add("IoFileMgrForUser",  "sceIoRead",                     &Kernel::sceIoRead);
  add("IoFileMgrForUser",  "sceIoClose",                    &Kernel::sceIoClose);
  add("IoFileMgrForUser",  "sceIoLseek",                    &Kernel::sceIoLseek);
  add("IoFileMgrForUser",  "sceIoOpen",                     &Kernel::sceIoOpen);
  add("IoFileMgrForUser",  "sceIoDopen",                    &Kernel::sceIoDopen);
  add("IoFileMgrForUser",  "sceIoDread",                    &Kernel::sceIoDread);
  add("IoFileMgrForUser",  "sceIoDclose",                   &Kernel::sceIoDclose);
  add("IoFileMgrForUser",  "sceIoChdir",                    &Kernel::sceIoChdir);
  add("IoFileMgrForUser",  "sceIoGetstat",                  &Kernel::sceIoGetstat);
  add("sceDisplay",        "sceDisplaySetMode",             &Kernel::sceDisplaySetMode);
  add("sceDisplay",        "sceDisplaySetFrameBuf",         &Kernel::sceDisplaySetFrameBuf);
  add("sceDisplay",        "sceDisplayGetFrameBuf",         &Kernel::sceDisplayGetFrameBuf);
  add("sceDisplay",        "sceDisplayWaitVblankStart",     &Kernel::sceDisplayWaitVblankStart);
  add("sceDisplay",        "sceDisplayGetVcount",           &Kernel::sceDisplayGetVcount);
  add("sceGe_user",        "sceGeEdramGetAddr",             &Kernel::sceGeEdramGetAddr);
  add("sceGe_user",        "sceGeEdramGetSize",             &Kernel::sceGeEdramGetSize);
  add("LoadExecForUser",   "sceKernelExitGame",             &Kernel::sceKernelExitGame);
  add("ModuleMgrForUser",  "sceKernelSelfStopUnloadModule", &Kernel::sceKernelSelfStopUnloadModule);
  add("sceUtility",        "sceUtilityGetSystemParamInt",   &Kernel::sceUtilityGetSystemParamInt);
  //newlib's sockets: no network yet, so every call fails
  add("sceNetInet",        "sceNetInetClose",               &Kernel::sceNetInetUnavailable);
  add("sceNetInet",        "sceNetInetRecv",                &Kernel::sceNetInetUnavailable);
  add("sceNetInet",        "sceNetInetSend",                &Kernel::sceNetInetUnavailable);
  add("sceNetInet",        "sceNetInetGetErrno",            &Kernel::sceNetInetUnavailable);
  cpu.syscallHook = [this](u32 code) { return syscall(code); };
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
  workingDirectory = "ms0:/";
  display = {};
  memory.write(4, Trampoline, ThreadReturnCode << 6 | 0x0c);  //syscall: the thread's entry function returned
  memory.write(4, Trampoline + 4, 0x0000'000d);                //break: never reached
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
  for(auto& segment : module.segments) {
    u32 start = segment.address & ~255u;  //blocks start on 256 bytes: round down, and keep the segment's end inside
    auto block = allocate(segment.address + segment.size - start, 2, start, module.name);
    if(!block || block->address != start) {  //it must be exactly where the program is
      error = "the program's memory overlaps memory already handed out";
      return false;
    }
  }
  for(auto& skipped : module.skipped) note("the loader left out " + skipped);

  cpu.power(module.entry);
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

//Runs the program for up to instructions instructions (fewer if it ends, or every thread waits on something that
//will never come), keeping time and waking threads as their moments come. Returns how many ran.
auto Kernel::run(u64 instructions) -> u64 {
  u64 done = 0;
  while(done < instructions && !exited) {
    if(!current) {
      reschedule();
      if(!current && !idle()) break;
      continue;
    }
    u64 ran = cpu.run(std::min(instructions - done, std::max<u64>(1, untilNextEvent())));
    done += ran;
    cycles += ran;
    if(current && cpu.scc.halted) {  //the CPU stopped by itself: a halt instruction, or an exception nobody handled
      note("the CPU stopped in thread " + current->name);
      break;
    }
    events();
  }
  return done;
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
