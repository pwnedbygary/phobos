#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "loader.hpp"

//The HLE kernel: Phobos's own version of the PSP's operating system, as far as a game can see it.
//
//A PSP game doesn't talk to the hardware much by itself: it asks the operating system to start threads, hand out
//memory, open files, show a frame. Rather than run Sony's operating system (which would need a firmware dump and
//chips nobody has documented), Phobos answers those requests itself: high-level emulation, HLE. The loader points
//every function a program imports at a syscall instruction with a code (Loader::ImportCode); when the program calls
//one, the CPU hands the code to syscall() here, which finds the function by its library and NID and runs Phobos's
//version of it, with the program's arguments in its registers.
//
//NIDs: each system function is known by a 32-bit number, the first four bytes of the SHA-1 hash of its name read
//as a little-endian word (sceKernelExitGame is 0x05572a5f). So the functions here are listed by name and their NIDs
//worked out (nid()), with no table of numbers copied from anywhere.
//
//Threads: a PSP program runs as several threads, each with its own registers and stack, of which one runs at a
//time: the ready one with the highest priority (the lowest number), first come among equals. When a thread waits
//(for a delay, a semaphore, the next frame), the kernel puts its registers aside and loads another's. When none is
//ready, the CPU idles and time jumps to the next thing that will wake one.
//
//Time: counted in the CPU's cycles at 333 MHz, one per instruction. The display's vertical blank, when a new frame
//starts, comes 59.94 times a second.
//
//Arguments come in a0-a3 and then t0-t3, the result goes in v0 (v0 and v1 for 64 bits), as the PSP's C compiler
//passes them.

namespace ares::PlayStationPortable {

struct Allegrex;
struct Memory;

struct Kernel {
  //Error codes: pspsdk's pspkerror.h, and those it lacks (the lightweight mutex's, the allocation type's, file not
  //found) from uOFW's errors.h.
  static constexpr u32 ErrorUnknownUID            = 0x8002'00cb;
  static constexpr u32 ErrorIllegalArgument       = 0x8002'00d2;
  static constexpr u32 ErrorIllegalAddress        = 0x8002'00d3;
  static constexpr u32 ErrorIllegalPartition      = 0x8002'00d6;
  static constexpr u32 ErrorAllocationFailed      = 0x8002'00d9;
  static constexpr u32 ErrorNotYetLinked          = 0x8002'013a;  //a function the kernel doesn't have
  static constexpr u32 ErrorNoMemory              = 0x8002'0190;
  static constexpr u32 ErrorIllegalPriority       = 0x8002'0193;
  static constexpr u32 ErrorIllegalStackSize      = 0x8002'0194;
  static constexpr u32 ErrorIllegalThread         = 0x8002'0197;
  static constexpr u32 ErrorUnknownThread         = 0x8002'0198;
  static constexpr u32 ErrorUnknownSemaphore      = 0x8002'0199;
  static constexpr u32 ErrorNotDormant            = 0x8002'01a4;
  static constexpr u32 ErrorWaitTimeout           = 0x8002'01a8;
  static constexpr u32 ErrorSemaphoreZero         = 0x8002'01ad;
  static constexpr u32 ErrorSemaphoreOverflow     = 0x8002'01ae;
  static constexpr u32 ErrorIllegalCount          = 0x8002'01bd;
  static constexpr u32 ErrorBadFile               = 0x8002'0323;
  static constexpr u32 ErrorFileNotFound          = 0x8001'0002;
  static constexpr u32 ErrorIllegalAllocationType = 0x8002'00d8;
  static constexpr u32 ErrorWaitDeleted           = 0x8002'01b5;  //what a thread waiting on something that got deleted is told
  static constexpr u32 ErrorLwMutexNotFound       = 0x8002'01ca;
  static constexpr u32 ErrorLwMutexLocked         = 0x8002'01cb;
  static constexpr u32 ErrorLwMutexUnlocked       = 0x8002'01cc;
  static constexpr u32 ErrorLwMutexUnderflow      = 0x8002'01ce;
  static constexpr u32 ErrorLwMutexRecursion      = 0x8002'01cf;

  static constexpr u64 CPUFrequency = 333'000'000;                   //cycles a second
  static constexpr u64 VblankCycles = CPUFrequency * 1001 / 60'000;  //59.94 frames a second
  static constexpr u32 Trampoline = 0x0800'0000;  //kernel memory: where a thread returns to when its entry function ends
  static constexpr u32 UserMemory = 0x0880'0000;  //the user partition, games' memory, runs from here to the end of RAM

  Kernel(Allegrex& cpu, Memory& memory);

  //What the program writes to its standard output and error; and the kernel's own notes, worth reporting (a
  //function it doesn't have, a thread that will never wake). The system shows or logs them.
  std::function<auto (const std::string& text) -> void> output;
  std::function<auto (const std::string& text) -> void> log;

  //kernel.cpp
  static auto nid(const std::string& name) -> u32;
  auto power() -> void;
  auto load(const u8* data, u64 size, const std::string& path, std::string& error) -> bool;
  auto start(const u8* data, u64 size, const std::string& path, std::string& error) -> bool;
  auto run(u64 instructions) -> u64;
  auto importCode(const std::string& library, u32 nid) -> u32;
  auto syscall(u32 code) -> bool;
  auto arg(u32 n) const -> u32;
  auto result(u32 value) -> void;
  auto note(const std::string& text) -> void;

  Allegrex& cpu;
  Memory& memory;
  Module module;           //the program
  bool exited = false;     //it called sceKernelExitGame (or unloaded itself)
  u64 cycles = 0;          //time since power on
  u32 nextUID = 0x100;

  //The system functions, by library and name; a syscall code stands for one import (a library and NID), which may
  //be a function the kernel doesn't have.
  struct Function {
    const char* library;
    const char* name;
    auto (Kernel::*handler)() -> void;
    u32 nid;
  };
  struct Import {
    std::string library;
    u32 nid;
    const Function* function;  //nullptr when the kernel doesn't have it
    bool reported = false;
  };
  std::vector<Function> functions;
  std::vector<Import> imports;  //by syscall code minus FirstImportCode
  static constexpr u32 ThreadReturnCode = 1, FirstImportCode = 0x1000;

  //threads.cpp
  struct Context {  //a thread's registers while another runs
    u32 gpr[32], lo, hi, pc, pd;
    u32 fpr[32], fcsr;
    u32 vpr[128], pfxs, pfxt, pfxd, cc;
  };
  enum class Status : u32 { Running = 1, Ready = 2, Waiting = 4, Dormant = 16 };  //the PSP's numbers
  enum class Wait : u32 { None, Delay, Sleep, Semaphore, LwMutex, Vblank, ThreadEnd };
  struct Thread {
    u32 uid;
    std::string name;
    u32 entry, priority, initialPriority, stackSize, stackBlock, attributes, gp;
    Status status = Status::Dormant;
    Context context{};
    Wait wait = Wait::None;
    u32 waitID = 0;        //the semaphore, mutex or thread waited for
    u32 waitCount = 0;     //how many a semaphore or mutex wait needs
    u64 wakeAt = 0;        //for a delay or timeout: the cycle to wake at (0: none)
    u32 timeoutPointer = 0;
    u64 readySince = 0;    //to keep first-come order among equal priorities
    s32 exitStatus = 0;
    u32 wakeupCount = 0;
  };
  struct Semaphore {
    u32 uid;
    std::string name;
    u32 attributes;
    s32 count, maximum;
  };
  std::map<u32, std::unique_ptr<Thread>> threads;
  std::map<u32, Semaphore> semaphores;
  std::map<u32, u32> lwMutexes;  //uid -> work area address
  Thread* current = nullptr;
  u64 readySequence = 0;
  u64 nextVblank = VblankCycles;
  u32 vblanks = 0;

  auto createThread(const std::string& name, u32 entry, u32 priority, u32 stackSize, u32 attributes, u32 gp) -> s32;
  auto startThread(Thread& thread, u32 argumentLength, u32 argumentPointer) -> void;
  auto argumentFits(const Thread& thread, u32 length) const -> bool;
  auto save(Thread& thread) -> void;
  auto restore(Thread& thread) -> void;
  auto ready(Thread& thread, u32 returnValue) -> void;
  auto block(Wait wait, u32 id, u64 wakeAt, u32 timeoutPointer = 0) -> void;
  auto reschedule() -> void;
  auto switchTo(Thread* thread) -> void;
  auto events() -> void;
  auto idle() -> bool;
  auto untilNextEvent() const -> u64;
  auto endThread(Thread& thread, s32 status) -> void;
  auto threadReturned() -> void;
  auto signalSemaphores(Semaphore& semaphore) -> void;
  auto unlockLwMutex(u32 workArea) -> void;
  auto findThread(u32 uid) -> Thread*;

  auto sceKernelCreateThread() -> void;
  auto sceKernelStartThread() -> void;
  auto sceKernelExitThread() -> void;
  auto sceKernelExitDeleteThread() -> void;
  auto sceKernelDeleteThread() -> void;
  auto sceKernelGetThreadId() -> void;
  auto sceKernelReferThreadStatus() -> void;
  auto sceKernelDelayThread() -> void;
  auto sceKernelSleepThread() -> void;
  auto sceKernelWakeupThread() -> void;
  auto sceKernelWaitThreadEnd() -> void;
  auto sceKernelCreateSema() -> void;
  auto sceKernelDeleteSema() -> void;
  auto sceKernelSignalSema() -> void;
  auto sceKernelWaitSema() -> void;
  auto sceKernelPollSema() -> void;
  auto sceKernelCreateLwMutex() -> void;
  auto sceKernelDeleteLwMutex() -> void;
  auto sceKernelLockLwMutex() -> void;
  auto sceKernelTryLockLwMutex() -> void;
  auto sceKernelUnlockLwMutex() -> void;
  auto sceKernelGetSystemTimeLow() -> void;

  //sysmem.cpp: the user partition's memory, handed out in blocks
  struct Block {
    u32 uid;
    std::string name;
    u32 address, size;
  };
  std::vector<Block> blocks;  //by address
  auto allocate(u32 size, u32 type, u32 address, const std::string& name) -> Block*;
  auto release(u32 uid) -> bool;
  auto userEnd() const -> u32;
  auto largestFree() const -> u32;

  auto sceKernelAllocPartitionMemory() -> void;
  auto sceKernelFreePartitionMemory() -> void;
  auto sceKernelGetBlockHeadAddr() -> void;
  auto sceKernelMaxFreeMemSize() -> void;
  auto sceKernelTotalFreeMemSize() -> void;

  //io.cpp: standard input, output and error; files come later
  std::string workingDirectory;
  auto sceKernelStdin() -> void;
  auto sceKernelStdout() -> void;
  auto sceKernelStderr() -> void;
  auto sceIoWrite() -> void;
  auto sceIoRead() -> void;
  auto sceIoClose() -> void;
  auto sceIoLseek() -> void;
  auto sceIoOpen() -> void;
  auto sceIoDopen() -> void;
  auto sceIoDread() -> void;
  auto sceIoDclose() -> void;
  auto sceIoChdir() -> void;
  auto sceIoGetstat() -> void;

  //display.cpp: where the program's frame is, and the vertical blank
  struct Display {
    u32 mode = 0, width = 480, height = 272;
    u32 frameBuffer = 0, bufferWidth = 0, pixelFormat = 0;
  } display;
  auto sceDisplaySetMode() -> void;
  auto sceDisplaySetFrameBuf() -> void;
  auto sceDisplayGetFrameBuf() -> void;
  auto sceDisplayWaitVblankStart() -> void;
  auto sceDisplayGetVcount() -> void;
  auto sceGeEdramGetAddr() -> void;
  auto sceGeEdramGetSize() -> void;

  //system.cpp: leaving, and the odds and ends a C library's start-up asks for
  auto sceKernelExitGame() -> void;
  auto sceKernelSelfStopUnloadModule() -> void;
  auto sceUtilityGetSystemParamInt() -> void;
  auto sceNetInetUnavailable() -> void;
};

}
