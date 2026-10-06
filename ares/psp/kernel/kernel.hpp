#pragma once

#include <algorithm>
#include <cctype>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "loader.hpp"
#include "disc.hpp"
#include "crypto.hpp"
#include "../ge/ge.hpp"

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
//
//Calls the other way: at times the kernel calls a function of the program's (the GE's callbacks, when a display list
//finishes or signals). It does so as the PSP calls interrupt handlers, on top of whichever thread is running
//(interrupts.cpp).

namespace ares::PlayStationPortable {

struct Allegrex;
struct Memory;
struct GE;

struct Kernel {
  //Error codes: pspsdk's pspkerror.h, and those it lacks (the lightweight mutex's, the allocation type's, files',
  //the GE driver's) from uOFW's errors.h.
  static constexpr u32 ErrorError                 = 0x8002'0001;
  static constexpr u32 ErrorUnknownUID            = 0x8002'00cb;
  static constexpr u32 ErrorIllegalPermission     = 0x8002'00d1;
  static constexpr u32 ErrorUnknownVpl            = 0x8002'019c;
  static constexpr u32 ErrorUnknownFpl            = 0x8002'019d;
  static constexpr u32 ErrorIllegalMemoryBlock    = 0x8002'01b6;
  static constexpr u32 ErrorIllegalMemorySize     = 0x8002'01b7;
  static constexpr u32 ErrorIllegalArgument       = 0x8002'00d2;
  static constexpr u32 ErrorIllegalAddress        = 0x8002'00d3;
  static constexpr u32 ErrorIllegalPartition      = 0x8002'00d6;
  static constexpr u32 ErrorAllocationFailed      = 0x8002'00d9;
  static constexpr u32 ErrorIllegalAlignmentSize  = 0x8002'00e4;  //an aligned block's alignment not a power of two
  static constexpr u32 ErrorNotYetLinked          = 0x8002'013a;  //a function the kernel doesn't have
  static constexpr u32 ErrorIllegalContext        = 0x8002'0064;  //waiting, from an interrupt handler
  static constexpr u32 ErrorCanNotWait            = 0x8002'01a7;  //waiting, with interrupts held off
  static constexpr u32 ErrorIllegalInterruptCode  = 0x8002'0065;
  static constexpr u32 ErrorHandlerFound          = 0x8002'0067;
  static constexpr u32 ErrorHandlerNotFound       = 0x8002'0068;
  static constexpr u32 ErrorIllegalAttribute      = 0x8002'0191;
  static constexpr u32 ErrorIllegalMode           = 0x8002'0195;
  static constexpr u32 ErrorUnknownEventFlag      = 0x8002'019a;
  static constexpr u32 ErrorUnknownCallback       = 0x8002'01a1;
  static constexpr u32 ErrorEventFlagCondition    = 0x8002'01af;  //a poll whose bits aren't set
  static constexpr u32 ErrorEventFlagPattern      = 0x8002'01b1;  //waiting for no bits at all
  static constexpr u32 ErrorNoMemory              = 0x8002'0190;
  static constexpr u32 ErrorTooManyFiles          = 0x8002'0320;
  static constexpr u32 ErrorIllegalPriority       = 0x8002'0193;
  static constexpr u32 ErrorIllegalStackSize      = 0x8002'0194;
  static constexpr u32 ErrorIllegalThread         = 0x8002'0197;
  static constexpr u32 ErrorUnknownThread         = 0x8002'0198;
  static constexpr u32 ErrorUnknownSemaphore      = 0x8002'0199;
  static constexpr u32 ErrorNotDormant            = 0x8002'01a4;
  static constexpr u32 ErrorWaitTimeout           = 0x8002'01a8;
  static constexpr u32 ErrorWaitCancelled         = 0x8002'01a9;
  static constexpr u32 ErrorSemaphoreZero         = 0x8002'01ad;
  static constexpr u32 ErrorSemaphoreOverflow     = 0x8002'01ae;
  static constexpr u32 ErrorEventFlagMulti        = 0x8002'01b0;  //a second thread waiting where only one may
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
  //uOFW's errors.h
  static constexpr u32 ErrorNotImplemented        = 0x8000'0003;
  static constexpr u32 ErrorNotSupported          = 0x8000'0004;
  static constexpr u32 ErrorAlready               = 0x8000'0020;
  static constexpr u32 ErrorBusy                  = 0x8000'0021;
  static constexpr u32 ErrorOutOfMemory           = 0x8000'0022;
  static constexpr u32 ErrorPrivilegeRequired     = 0x8000'0023;
  static constexpr u32 ErrorNotFound              = 0x8000'0025;
  static constexpr u32 ErrorInvalidID             = 0x8000'0100;
  static constexpr u32 ErrorInvalidIndex          = 0x8000'0102;
  static constexpr u32 ErrorInvalidPointer        = 0x8000'0103;
  static constexpr u32 ErrorInvalidSize           = 0x8000'0104;
  static constexpr u32 ErrorInvalidMode           = 0x8000'0107;
  static constexpr u32 ErrorInvalidValue          = 0x8000'01fe;
  //files' (uOFW's errors.h)
  static constexpr u32 ErrorIOError               = 0x8001'0005;
  static constexpr u32 ErrorNoPermission          = 0x8001'000d;
  static constexpr u32 ErrorFileExists            = 0x8001'0011;
  static constexpr u32 ErrorCrossDevice           = 0x8001'0012;
  static constexpr u32 ErrorDeviceNotFound        = 0x8001'0013;
  static constexpr u32 ErrorNotDirectory          = 0x8001'0014;
  static constexpr u32 ErrorIsDirectory           = 0x8001'0015;
  static constexpr u32 ErrorInvalidArgument       = 0x8001'0016;
  static constexpr u32 ErrorDirectoryNotEmpty     = 0x8001'005a;
  static constexpr u32 ErrorReadOnly              = 0x8001'001e;  //writing to the disc
  static constexpr u32 ErrorFunctionNotSupported  = 0x8001'b000;  //an ioctl or devctl the device doesn't have
  static constexpr u32 ErrorInvalidFileSize       = 0x8001'b003;  //seeking umd0: past the disc
  static constexpr u32 ErrorInvalidFlag           = 0x8001'b004;  //opening a file on the disc to write it
  static constexpr u32 ErrorDevctlBadParameters   = 0x8022'0081;  //a devctl's buffers too small or misplaced
  static constexpr u32 ErrorVolatileMemoryInUse   = 0x802b'0200;  //the volatile memory lent already
  //sceAudio's (uOFW's errors.h)
  static constexpr u32 ErrorAudioChannelNotInitialized  = 0x8026'0001;
  static constexpr u32 ErrorAudioChannelBusy            = 0x8026'0002;
  static constexpr u32 ErrorAudioInvalidChannel         = 0x8026'0003;
  static constexpr u32 ErrorAudioNoChannels             = 0x8026'0005;
  static constexpr u32 ErrorAudioSampleCount            = 0x8026'0006;  //not a multiple of 64, or out of range
  static constexpr u32 ErrorAudioInvalidFormat          = 0x8026'0007;
  static constexpr u32 ErrorAudioChannelNotReserved     = 0x8026'0008;
  static constexpr u32 ErrorAudioInvalidFrequency       = 0x8026'000a;
  static constexpr u32 ErrorAudioInvalidVolume          = 0x8026'000b;
  static constexpr u32 ErrorAudioChannelAlreadyReserved = 0x8026'8002;
  //pspkerror.h's, for threads
  static constexpr u32 ErrorDormant               = 0x8002'01a2;
  static constexpr u32 ErrorSuspended             = 0x8002'01a3;
  static constexpr u32 ErrorNotSuspended          = 0x8002'01a5;
  static constexpr u32 ErrorThreadTerminated      = 0x8002'01ac;
  //modules' (pspkerror.h)
  static constexpr u32 ErrorIllegalObject         = 0x8002'012d;  //not a module, or one the loader refuses
  static constexpr u32 ErrorUnknownModule         = 0x8002'012e;
  static constexpr u32 ErrorAlreadyStarted        = 0x8002'0133;
  static constexpr u32 ErrorNotStarted            = 0x8002'0134;
  static constexpr u32 ErrorAlreadyStopped        = 0x8002'0135;
  static constexpr u32 ErrorNotStopped            = 0x8002'0137;
  static constexpr u32 ErrorUnsupportedPrxType    = 0x8002'0148;  //an encrypted module that can't be decrypted

  static constexpr u64 CPUFrequency = 333'000'000;                   //cycles a second
  static constexpr u64 VblankCycles = CPUFrequency * 1001 / 60'000;  //59.94 frames a second
  static constexpr u32 Trampoline = 0x0800'0000;  //kernel memory: where a thread returns to when its entry function
                                                  //ends (8 bytes on, a call into the program; 16 on, a module's
                                                  //module_start or module_stop; 24 on, a thread's callback)
  static constexpr u32 InterruptStack = 0x0802'0000;  //kernel memory: the top of the stack calls into the program use
  static constexpr u32 UserMemory = 0x0880'0000;  //the user partition, games' memory, runs from here to the end of RAM

  Kernel(Allegrex& cpu, Memory& memory, GE& ge);

  //What the program writes to its standard output and error; and the kernel's own notes, worth reporting (a
  //function it doesn't have, a thread that will never wake). The system shows or logs them.
  std::function<auto (const std::string& text) -> void> output;
  std::function<auto (const std::string& text) -> void> log;

  //kernel.cpp
  static auto nid(const std::string& name) -> u32;
  auto power() -> void;
  auto load(const u8* data, u64 size, const std::string& path, std::string& error) -> bool;
  auto start(const u8* data, u64 size, const std::string& path, std::string& error) -> bool;
  auto run(u64 budget) -> u64;
  auto importCode(const std::string& library, u32 nid) -> u32;
  auto syscall(u32 code) -> bool;
  auto arg(u32 n) const -> u32;
  auto result(u32 value) -> void;
  auto note(const std::string& text) -> void;
  auto serialize(serializer& s) -> bool;  //serialization.cpp: for save states; false if a state is damaged

  Allegrex& cpu;
  Memory& memory;
  GE& ge;
  Module module;           //the program
  bool exited = false;     //it called sceKernelExitGame (or unloaded itself)
  bool stuck = false;      //nothing will run again, which has been noted (once, not every frame; not saved)
  u64 cycles = 0;          //time since power on
  u32 nextUID = 0x100;
  static constexpr u32 LastUID = 0x7fff'ffff;  //IDs are positive 32-bit numbers: a top bit set reads as an error
  auto newUID() -> u32;

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
  static constexpr u32 ThreadReturnCode = 1, CallReturnCode = 2, ModuleReturnCode = 3, CallbackReturnCode = 4;
  static constexpr u32 FirstImportCode = 0x1000;

  //threads.cpp
  struct Context {  //a thread's registers while another runs
    u32 gpr[32], lo, hi, pc, pd;
    u32 fpr[32], fcsr;
    u32 vpr[128], pfxs, pfxt, pfxd, cc;
  };
  enum class Status : u32 { Running = 1, Ready = 2, Waiting = 4, Dormant = 16 };  //the PSP's numbers
  enum class Wait : u32 {
    None, Delay, Sleep, Semaphore, LwMutex, Vblank, ThreadEnd, Controller, EventFlag, GeList, GeDraw, Umd, Audio,
    Fpl, Vpl, Module,
  };
  struct WaitState {  //a thread's wait, put aside while its callbacks run (they may wait themselves)
    Wait wait = Wait::None;
    u32 id = 0, count = 0, mode = 0, pointer = 0, timeoutPointer = 0;
    u64 wakeAt = 0;
    bool callbacks = false;
  };
  struct Thread {
    u32 uid;
    std::string name;
    u32 entry, priority, initialPriority, stackSize, stackBlock, attributes, gp;
    Status status = Status::Dormant;
    Context context{};
    Wait wait = Wait::None;
    u32 waitID = 0;        //the semaphore, mutex, thread, event flag, display list or module waited for; the sound
                           //channel (0-7 a mixer channel's, Audio::WaitSrc or WaitSrcDrain the SRC channel's)
    u32 waitCount = 0;     //how many a semaphore or mutex wait needs; the bits an event flag wait needs; a mixer
                           //output's left volume; the samples an SRC output's buffer was armed with
    u32 waitMode = 0;      //an event flag wait's mode; a mixer output's right volume
    u32 waitPointer = 0;   //where an event flag wait puts the bits it saw, and a module wait the function's result;
                           //the buffer a mixer output hands over
    u64 wakeAt = 0;        //for a delay or timeout: the cycle to wake at (0: none)
    u32 timeoutPointer = 0;
    u64 readySince = 0;    //to keep first-come order among equal priorities
    s32 exitStatus = 0;
    u32 wakeupCount = 0;
    bool callbacks = false;    //its wait lets its callbacks run (it called a function whose name ends in CB)
    bool inCallback = false;   //it's running one of them, its own registers and wait put aside till they're done
    u32 callbackID = 0;        //which
    Context beforeCallback{};  //the thread as its callbacks found it: in its wait, or in sceKernelCheckCallback
    WaitState waitBeforeCallback;
    bool suspended = false;    //another thread suspended it: it doesn't run, whatever its state, until resumed
  };
  struct Semaphore {
    u32 uid;
    std::string name;
    u32 attributes;
    s32 count, maximum;
    s32 initial = 0;
  };
  std::map<u32, std::unique_ptr<Thread>> threads;
  std::map<u32, Semaphore> semaphores;
  std::map<u32, u32> lwMutexes;  //uid -> work area address
  Thread* current = nullptr;
  u64 readySequence = 0;
  u64 nextVblank = VblankCycles;
  u32 vblanks = 0;

  auto createThread(const std::string& name, u32 entry, u32 priority, u32 stackSize, u32 attributes, u32 gp) -> s32;
  auto startThread(Thread& thread, u32 argumentLength, u32 argumentPointer, u32 returnAddress = Trampoline) -> void;
  auto argumentFits(const Thread& thread, u32 length) const -> bool;
  auto save(Context& context) -> void;
  auto restore(const Context& context) -> void;
  auto ready(Thread& thread, u32 returnValue) -> void;
  auto block(Wait wait, u32 id, u64 wakeAt, u32 timeoutPointer = 0, bool callbacks = false) -> void;
  auto timeout(u32 pointer) const -> u64;
  auto reschedule() -> void;
  auto switchTo(Thread* thread) -> void;
  auto events() -> void;
  auto idle(u64 end) -> bool;
  auto untilNextEvent() const -> u64;
  auto endThread(Thread& thread, s32 status) -> void;
  auto threadReturned() -> void;
  auto signalSemaphores(Semaphore& semaphore) -> void;
  auto unlockLwMutex(u32 workArea) -> void;
  auto findThread(u32 uid) -> Thread*;
  auto deleteThread(Thread& thread) -> void;
  auto delay(u32 microseconds, bool callbacks) -> void;
  auto sleep(bool callbacks) -> void;
  auto waitThreadEnd(bool callbacks) -> void;
  auto waitSemaphore(bool callbacks) -> void;

  auto sceKernelCreateThread() -> void;
  auto sceKernelStartThread() -> void;
  auto sceKernelExitThread() -> void;
  auto sceKernelExitDeleteThread() -> void;
  auto sceKernelDeleteThread() -> void;
  auto sceKernelGetThreadId() -> void;
  auto sceKernelReferThreadStatus() -> void;
  auto sceKernelDelayThread() -> void;
  auto sceKernelDelayThreadCB() -> void;
  auto sceKernelSleepThread() -> void;
  auto sceKernelWakeupThread() -> void;
  auto sceKernelWaitThreadEnd() -> void;
  auto sceKernelWaitThreadEndCB() -> void;
  auto sceKernelCreateSema() -> void;
  auto sceKernelDeleteSema() -> void;
  auto sceKernelSignalSema() -> void;
  auto sceKernelWaitSema() -> void;
  auto sceKernelWaitSemaCB() -> void;
  auto sceKernelPollSema() -> void;
  auto sceKernelReferSemaStatus() -> void;
  auto sceKernelChangeThreadPriority() -> void;
  auto sceKernelGetThreadExitStatus() -> void;
  auto sceKernelTerminateThread() -> void;
  auto sceKernelTerminateDeleteThread() -> void;
  auto sceKernelSuspendThread() -> void;
  auto sceKernelResumeThread() -> void;
  auto sceKernelChangeCurrentThreadAttr() -> void;
  auto sceKernelGetThreadStackFreeSize() -> void;
  auto sceKernelReferThreadProfiler() -> void;
  auto sceKernelCreateLwMutex() -> void;
  auto sceKernelDeleteLwMutex() -> void;
  auto sceKernelLockLwMutex() -> void;
  auto sceKernelTryLockLwMutex() -> void;
  auto sceKernelUnlockLwMutex() -> void;
  auto sceKernelGetSystemTimeLow() -> void;

  //sysmem.cpp: the user partition's memory, handed out in blocks; and what a program tells the system about itself
  struct Block {
    u32 uid;
    std::string name;
    u32 address, size;
  };
  std::vector<Block> blocks;  //by address
  bool largeMemory = false;   //the program asked for all of RAM (its PARAM.SFO's MEMSIZE): see userEnd()
  u32 sdkVersion = 0;         //the SDK the program was built with, as its start-up code tells the system
  u32 compilerVersion = 0;    //and the version of the compiler that built it
  auto allocate(u32 size, u32 type, u32 address, const std::string& name) -> Block*;
  auto release(u32 uid) -> bool;
  auto blockHeld(const Block& block) const -> bool;
  auto userEnd() const -> u32;
  auto largestFree() const -> u32;
  auto programParameters(const u8* data, u64 size, const std::string& path) -> std::vector<u8>;

  auto sceKernelAllocPartitionMemory() -> void;
  auto sceKernelFreePartitionMemory() -> void;
  auto sceKernelGetBlockHeadAddr() -> void;
  auto sceKernelMaxFreeMemSize() -> void;
  auto sceKernelTotalFreeMemSize() -> void;
  auto sceKernelSetCompiledSdkVersion() -> void;
  auto sceKernelGetCompiledSdkVersion() -> void;
  auto sceKernelSetCompilerVersion() -> void;

  //io.cpp: files and folders on the host folders standing for the PSP's devices, or on the disc in the drive;
  //standard input, output and error
  struct OpenFile {
    std::string path;      //the PSP's name for it, normalized
    std::string host;
    bool folder = false;
    u32 flags = 0;         //what it was opened for (PSP_O_*)
    u64 position = 0;      //where the next read or write goes
    std::unique_ptr<std::fstream> stream;
    std::vector<std::string> entries;  //a folder's names, handed out one at a time
    u32 nextEntry = 0;
    //on the disc: where it starts and how long it is. With `sectors` (through umd0:, the whole disc), positions and
    //sizes count sectors rather than bytes.
    bool onDisc = false, sectors = false;
    u32 sector = 0;
    u64 size = 0;
    std::vector<Disc::Entry> discEntries;  //a folder on the disc's entries, beside their names
  };
  std::map<std::string, std::string> devices;  //"ms0" -> the host folder standing for it
  std::shared_ptr<Disc> disc;  //the disc image in the drive (disc0: and umd0:, unless a host folder stands for it)
  std::map<u32, OpenFile> files;
  u32 nextFile = 3;  //after standard input, output and error
  auto newFile() -> u32;
  std::string workingDirectory;
  std::vector<u32> memoryStickCallbacks;  //callbacks the program registered for the memory stick going in and out
  auto mount(const std::string& device, const std::string& folder) -> void;
  auto split(const std::string& path, std::string& device, std::string& rest) const -> bool;
  auto onDisc(const std::string& path) const -> bool;
  auto resolve(const std::string& path, std::string& host, std::string& normalized) -> u32;
  auto resolveOnDisc(const std::string& path, Disc::Entry& entry, std::string& normalized,
                     std::vector<std::string>& names) -> u32;
  auto writeStat(u32 address, const std::string& host) -> void;
  auto writeStat(u32 address, const Disc::Entry& entry) -> void;
  auto openOnDisc(const std::string& path, u32 flags) -> u32;
  auto runOnDisc(const std::string& path, u32& first, u64& bytes) const -> bool;
  auto readFile(u32 file, u32 data, u32 size) -> u32;
  auto seek(u32 file, s64 offset, u32 whence, u64& position) -> u32;
  auto sceKernelStdin() -> void;
  auto sceKernelStdout() -> void;
  auto sceKernelStderr() -> void;
  auto sceIoOpen() -> void;
  auto sceIoClose() -> void;
  auto sceIoRead() -> void;
  auto sceIoWrite() -> void;
  auto sceIoLseek() -> void;
  auto sceIoLseek32() -> void;
  auto sceIoRemove() -> void;
  auto sceIoMkdir() -> void;
  auto sceIoRmdir() -> void;
  auto sceIoRename() -> void;
  auto sceIoChdir() -> void;
  auto sceIoGetstat() -> void;
  auto sceIoDopen() -> void;
  auto sceIoDread() -> void;
  auto sceIoDclose() -> void;
  auto sceIoIoctl() -> void;
  auto sceIoDevctl() -> void;

  //umd.cpp: the disc drive
  static constexpr u32 UmdNotPresent = 0x01, UmdPresent = 0x02, UmdChanged = 0x04, UmdNotReady = 0x08,
                       UmdReady = 0x10, UmdReadable = 0x20;
  u32 umdCallback = 0;  //the callback the program registered for the drive's changes
  auto umdState() const -> u32;
  auto umdWait(u32 stat, u32 timeout, bool callbacks) -> void;
  auto sceUmdCheckMedium() -> void;
  auto sceUmdActivate() -> void;
  auto sceUmdDeactivate() -> void;
  auto sceUmdGetDriveStat() -> void;
  auto sceUmdWaitDriveStat() -> void;
  auto sceUmdWaitDriveStatWithTimer() -> void;
  auto sceUmdWaitDriveStatCB() -> void;
  auto sceUmdCancelWaitDriveStat() -> void;
  auto sceUmdGetErrorStat() -> void;
  auto sceUmdGetDiscInfo() -> void;
  auto sceUmdRegisterUMDCallBack() -> void;
  auto sceUmdUnRegisterUMDCallBack() -> void;
  auto sceUmdReplacePermit() -> void;

  //ctrl.cpp: the buttons and the analog stick
  struct Controller {
    struct Sample {
      u32 time = 0;              //when it was taken, in microseconds
      u32 buttons = 0;
      u8 x = 128, y = 128;       //the stick (centred unless sampling was analog)
    };
    struct Latch {                 //since the latch was last read:
      u32 made = 0, broken = 0;    //buttons pressed, and let go
      u32 held = 0, released = 0;  //buttons held at some sample, and not held at some sample
      u32 samples = 0;             //how many samples that was
    };
    u32 buttons = 0;                  //what the player holds now (PSP_CTRL_* bits): the system sets these
    u8 analogX = 128, analogY = 128;  //the stick, 0-255 on each axis, 128 when left alone
    u32 cycle = 0, mode = 0;          //sampling cycle (0: at each vertical blank; else microseconds) and mode
    u64 nextSample = 0;               //with a cycle: when the next sample is due
    Sample samples[64];               //the last 64 samples, a ring: the next one goes over samples[next], the oldest
    u32 next = 0;
    u32 unread = 0;                   //samples since the buffer was last read (at most 63)
    u32 sampled = 0;                  //the buttons at the last sample
    Latch latch;
  } controller;
  auto sampleController() -> bool;
  auto writeSamples(u32 address, u32 first, u32 count, bool negative) -> void;
  auto readSamples(u32 address, u32 count, bool negative) -> u32;
  auto writeLatch(u32 address) -> u32;
  auto peekController(bool negative) -> void;
  auto readController(bool negative) -> void;
  auto sceCtrlSetSamplingCycle() -> void;
  auto sceCtrlGetSamplingCycle() -> void;
  auto sceCtrlSetSamplingMode() -> void;
  auto sceCtrlGetSamplingMode() -> void;
  auto sceCtrlPeekBufferPositive() -> void;
  auto sceCtrlPeekBufferNegative() -> void;
  auto sceCtrlReadBufferPositive() -> void;
  auto sceCtrlReadBufferNegative() -> void;
  auto sceCtrlPeekLatch() -> void;
  auto sceCtrlReadLatch() -> void;

  //display.cpp: where the program's frame is, and the vertical blank
  struct Display {
    u32 mode = 0, width = 480, height = 272;
    u32 frameBuffer = 0, bufferWidth = 0, pixelFormat = 0;
  } display;
  static constexpr u64 LineCycles = CPUFrequency * 525 / 9'000'000;  //a line: 525 dots at 9 MHz (286 to a frame)
  static constexpr u64 VblankLength = CPUFrequency * 77 / 100'000;    //the vertical blank lasts 0.77 ms
  auto inVblank() const -> bool;
  auto waitVblank(bool callbacks) -> void;
  auto sceDisplaySetMode() -> void;
  auto sceDisplaySetFrameBuf() -> void;
  auto sceDisplayGetFrameBuf() -> void;
  auto sceDisplayWaitVblankStart() -> void;
  auto sceDisplayWaitVblankStartCB() -> void;
  auto sceDisplayWaitVblank() -> void;
  auto sceDisplayWaitVblankCB() -> void;
  auto sceDisplayIsVblank() -> void;
  auto sceDisplayGetCurrentHcount() -> void;
  auto sceDisplayGetVcount() -> void;
  auto picture(std::vector<u32>& pixels) -> void;

  //interrupts.cpp: calls into the program, as interrupt handlers
  struct Call {
    u32 function, gp;
    u32 arguments[3];
    bool resumesGe;  //the GE waits for it (a SIGNAL that suspends the list)
  };
  std::deque<Call> calls;       //waiting their turn
  bool interrupting = false;    //one is running
  bool interruptsEnabled = true;  //sceKernelCpuSuspendIntr holds calls back until sceKernelCpuResumeIntr
  bool rescheduleAfter = false;   //a thread woke during the call: pick who runs once it's over
  Context interrupted{};        //the CPU as the call found it
  bool interruptedHalted = false;
  bool callResumesGe = false;
  auto queueCall(u32 function, u32 gp, u32 a0, u32 a1, u32 a2, bool resumesGe = false) -> void;
  auto startCall() -> void;
  auto callReturned() -> void;
  auto mayWait() -> bool;
  auto sceKernelCpuSuspendIntr() -> void;
  auto sceKernelCpuResumeIntr() -> void;
  //Sub-interrupt handlers: the program's functions an interrupt calls, 32 to an interrupt, on the two interrupts a
  //program may use: the vertical blank's (30), called at each blank, and the GE's (25), which nothing raises yet.
  struct SubHandler {
    u32 function = 0;      //0: none registered
    u32 argument = 0;      //what it's called with, after its number
    u32 gp = 0;            //the global pointer it was registered with
    bool enabled = false;  //sceKernelEnableSubIntr's say: registering leaves it as it is, releasing clears it
  };
  SubHandler vblankSubs[32], geSubs[32];
  bool vblankPending = false;  //a vertical blank came while its handlers couldn't run: they run once, when they can
  auto subHandlers(u32 interrupt) -> SubHandler*;
  auto queueVblankHandlers() -> void;
  auto vblankInterrupt() -> void;
  auto vblankHandlers() const -> bool;
  auto sceKernelRegisterSubIntrHandler() -> void;
  auto sceKernelReleaseSubIntrHandler() -> void;
  auto sceKernelEnableSubIntr() -> void;
  auto sceKernelDisableSubIntr() -> void;

  //events.cpp: event flags, and callbacks
  struct EventFlag {
    u32 uid;
    std::string name;
    u32 attributes, initial, pattern;
  };
  struct Callback {
    u32 uid;
    std::string name;
    u32 function, argument, thread;
    u32 notifyCount = 0, notifyArg = 0;  //times it was notified since it last ran, and the last notification's word
  };
  std::map<u32, EventFlag> eventFlags;
  std::map<u32, Callback> callbacks;
  u32 exitCallback = 0;
  auto eventFlagWaiters(const EventFlag& flag) -> std::vector<Thread*>;
  auto eventFlagFor(u32 uid, u32 bits, u32 mode) -> EventFlag*;
  auto eventFlagTimedOut(Thread& thread) -> void;
  auto wakeEventFlagWaiters(EventFlag& flag) -> void;
  auto waitEventFlag(bool callbacks) -> void;
  auto notifyCallback(u32 uid, u32 argument) -> bool;
  auto pendingCallback(const Thread& thread) -> Callback*;
  auto wakeForCallbacks(Thread& thread) -> void;
  auto callbacksOnReturn(bool callbacks) -> void;
  auto runCallbacks(Thread& thread) -> void;
  auto callNextCallback(Thread& thread) -> bool;
  auto callbackReturned() -> void;
  auto backFromCallbacks(Thread& thread) -> void;
  auto resumeWait(Thread& thread) -> void;
  auto deleteCallback(u32 uid) -> bool;
  auto sceKernelCreateEventFlag() -> void;
  auto sceKernelDeleteEventFlag() -> void;
  auto sceKernelSetEventFlag() -> void;
  auto sceKernelClearEventFlag() -> void;
  auto sceKernelWaitEventFlag() -> void;
  auto sceKernelWaitEventFlagCB() -> void;
  auto sceKernelPollEventFlag() -> void;
  auto sceKernelReferEventFlagStatus() -> void;
  auto sceKernelCreateCallback() -> void;
  auto sceKernelDeleteCallback() -> void;
  auto sceKernelNotifyCallback() -> void;
  auto sceKernelCancelCallback() -> void;
  auto sceKernelGetCallbackCount() -> void;
  auto sceKernelReferCallbackStatus() -> void;
  auto sceKernelRegisterExitCallback() -> void;
  auto sceKernelSleepThreadCB() -> void;
  auto sceKernelCheckCallback() -> void;

  //ge.cpp: the GE driver
  static constexpr u32 GeListIDs = 0x4745'0000;  //the first display list's ID ("GE"), then one up for each
  static constexpr u64 GeBudget = 1'000'000;     //commands the GE runs in a frame at most (geLeft)
  struct GeStackEntry {  //where a list was when a SIGNAL called elsewhere
    GE::Registers registers;
    u32 base;
  };
  struct GeList {
    enum class State : u32 { None, Queued, Running, Completed, Paused };  //uOFW's numbers
    State state = State::None;
    bool started = false;      //it has run, so its registers are its own
    bool pausing = false;      //a PAUSE signal: it pauses at its next FINISH
    bool syncing = false;      //a SYNC signal: its next FINISH doesn't end it
    GE::Registers registers;   //where it is while not in the GE
    u32 base = 0;              //BASE's word, kept with them
    s32 callback = -1;         //its callbacks (sceGeSetCallback's number), or none
    u32 context = 0;           //where the GE's state is saved as it starts, to be restored as it ends (0: nowhere)
    u32 stackLimit = 0;        //how deep SIGNAL calls may go
    std::vector<GeStackEntry> stack;
    u32 signalID = 0;          //the PAUSE signal's id, for the callback when the pause takes effect
  };
  struct GeCallback {
    bool used = false;
    u32 signalFunction = 0, signalArgument = 0, finishFunction = 0, finishArgument = 0, gp = 0;
  };
  GeList geLists[64];
  std::deque<u32> geQueue;  //the lists queued, in order: the first is the one the GE has, or will have next
  std::deque<u32> geFree;   //the lists not queued, the one freed longest ago first: the next enqueued takes it
  GeCallback geCallbacks[16];
  s32 geRunning = -1;       //the list the GE is running, or none
  bool geBusy = false;      //it ran out of the frame's commands, and goes on after the next vertical blank
  bool geSuspended = false; //it waits for a callback to return (a SIGNAL that suspends, a FINISH)
  s32 geFinishing = -1;     //the list whose finish callback runs: the rest of its ending waits for it
  u64 geLeft = GeBudget;    //commands the GE may still run this frame (each vertical blank gives it GeBudget again)
  u64 geCommands = 0;       //commands the GE has run since power on (not saved: a count for tests and notes)
  auto geIndex(u32 id) const -> s32;
  auto geEnqueue(bool head) -> void;
  auto geLoad(GeList& list) -> void;
  auto geRestoreBase(u32 word) -> void;
  auto geRun() -> void;
  auto geFinished() -> void;
  auto geEnded() -> void;
  auto geSignaled() -> void;
  auto geStartNext() -> void;
  auto geCall(GeList& list, bool finish, u32 id, bool suspends = false) -> bool;
  auto geStalled() const -> bool;
  auto geDrawSynced() -> void;
  auto geRemove(u32 index) -> void;
  auto geWake(Wait wait, u32 index) -> void;
  auto geSaveContext(u32 address) -> void;
  auto geRestoreContext(u32 address) -> void;
  auto sceGeEdramGetAddr() -> void;
  auto sceGeEdramGetSize() -> void;
  auto sceGeListEnQueue() -> void;
  auto sceGeListEnQueueHead() -> void;
  auto sceGeListDeQueue() -> void;
  auto sceGeListUpdateStallAddr() -> void;
  auto sceGeListSync() -> void;
  auto sceGeDrawSync() -> void;
  auto sceGeSetCallback() -> void;
  auto sceGeUnsetCallback() -> void;
  auto sceGeContinue() -> void;
  auto sceGeGetCmd() -> void;
  auto sceGeGetMtx() -> void;
  auto sceGeSaveContext() -> void;
  auto sceGeRestoreContext() -> void;

  //pools.cpp: memory pools a program hands out itself, in blocks of one size (FPL) or of any (VPL)
  struct Pool {
    u32 uid;
    std::string name;
    u32 attributes = 0;
    bool variable = false;
    u32 block = 0;             //its memory, a block of the user partition
    u32 address = 0, size = 0; //what it hands out from
    u32 blockSize = 0;         //a fixed pool's blocks' size
    std::vector<u8> used;      //a fixed pool's blocks handed out
    std::map<u32, u32> pieces; //a variable pool's pieces handed out: where each starts (its header), how long
  };
  std::map<u32, Pool> pools;
  auto createPool(bool variable) -> void;
  auto poolTake(Pool& pool, u32 size) -> u32;
  auto poolFree(const Pool& pool) const -> u32;
  auto poolWaiters(const Pool& pool) -> std::vector<Thread*>;
  auto poolWake(Pool& pool) -> void;
  auto poolAllocate(bool variable, bool callbacks) -> void;
  auto poolTryAllocate(bool variable) -> void;
  auto poolRelease(bool variable) -> void;
  auto poolDelete(bool variable) -> void;
  auto poolCancel(bool variable) -> void;
  auto poolStatus(bool variable) -> void;
  auto sceKernelCreateFpl() -> void;
  auto sceKernelDeleteFpl() -> void;
  auto sceKernelAllocateFpl() -> void;
  auto sceKernelAllocateFplCB() -> void;
  auto sceKernelTryAllocateFpl() -> void;
  auto sceKernelFreeFpl() -> void;
  auto sceKernelCancelFpl() -> void;
  auto sceKernelReferFplStatus() -> void;
  auto sceKernelCreateVpl() -> void;
  auto sceKernelDeleteVpl() -> void;
  auto sceKernelAllocateVpl() -> void;
  auto sceKernelAllocateVplCB() -> void;
  auto sceKernelTryAllocateVpl() -> void;
  auto sceKernelFreeVpl() -> void;
  auto sceKernelCancelVpl() -> void;
  auto sceKernelReferVplStatus() -> void;

  //audio.cpp: sound output. Eight mixer channels holding a buffer each, read a block of 64 samples at a time by the
  //mixer's DMA; and the SRC channel (sceAudioOutput2*, sceAudioSRC*), a ninth output at a rate of its own, with two
  //buffers armed at most. Their timing is the PSP's; the samples aren't mixed into the system's sound yet.
  struct Audio {
    //A block, 64 samples at 44.1 kHz, isn't a whole number of the CPU's cycles at 333 MHz: it's 483,265 and 15/49.
    static constexpr u64 BlockCycles = CPUFrequency * 64 / 44'100;
    static constexpr u32 BlockFraction = CPUFrequency * 64 * 49 / 44'100 % 49;
    static_assert((BlockCycles * 49 + BlockFraction) * 44'100 == CPUFrequency * 64 * 49);
    static constexpr u32 WaitSrc = 8, WaitSrcDrain = 9;  //waitIDs on the SRC channel (0-7: the mixer channels)
    struct Channel {
      bool reserved = false;
      u32 sampleCount = 0;            //samples in each buffer handed over: a multiple of 64, from 64 to 65472
      u32 format = 0;                 //0x00 stereo, 0x10 mono
      u32 leftVolume = 0, rightVolume = 0;
      u32 buffer = 0;                 //the slot: the buffer in it (0: the slot is free)
      u32 length = 0;                 //its samples
      u32 remaining = 0;              //how many of them the DMA hasn't taken yet (a null buffer sets this too)
    } channels[8];
    struct Dma {                      //the mixer's: a block from every channel with a buffer, every 64/44100 s
      bool running = false;
      u64 nextBlock = 0;              //when it takes the next block
      u32 fraction = 0;               //and how far past that cycle the block really comes, in 49ths of one
    } dma;
    struct SrcChannel {
      bool reserved = false;
      u32 sampleCount = 0;            //samples in each buffer handed over: 17 to 4111
      u32 rate = 44'100;              //its samples a second
      struct Buffer {
        u32 address = 0, sampleCount = 0, volume = 0;
      } buffers[2];                   //the buffers armed: the first plays, the second follows it
      u32 armed = 0;                  //how many there are
      u64 retireAt = 0;               //when the first one's transfer ends
      bool completion = false;        //a completion no output has taken yet
    } src;
  } audio;
  auto audioWaiter(u32 waitID) -> Thread*;
  auto audioWaitRefused() const -> u32;
  auto handOver(u32 number, u32 buffer, s32 left, s32 right) -> void;
  auto mixerBlock() -> bool;
  auto mixerOutput(u32 number, u32 buffer, s32 left, s32 right, bool blocking) -> void;
  auto srcDuration(u32 samples) const -> u64;
  auto srcRetire() -> bool;
  auto srcReserve(u32 samples, u32 rate, u32 channels) -> void;
  auto srcRelease() -> void;
  auto srcOutput() -> void;
  auto audioEvents() -> bool;
  auto nextAudioEvent() const -> u64;
  auto sceAudioChReserve() -> void;
  auto sceAudioChRelease() -> void;
  auto sceAudioOutputBlocking() -> void;
  auto sceAudioOutputPannedBlocking() -> void;
  auto sceAudioOutput() -> void;
  auto sceAudioOutputPanned() -> void;
  auto sceAudioGetChannelRestLen() -> void;
  auto sceAudioGetChannelRestLength() -> void;
  auto sceAudioSetChannelDataLen() -> void;
  auto sceAudioChangeChannelConfig() -> void;
  auto sceAudioChangeChannelVolume() -> void;
  auto sceAudioOutput2Reserve() -> void;
  auto sceAudioOutput2OutputBlocking() -> void;
  auto sceAudioOutput2ChangeLength() -> void;
  auto sceAudioOutput2GetRestSample() -> void;
  auto sceAudioOutput2Release() -> void;
  auto sceAudioSRCChReserve() -> void;
  auto sceAudioSRCOutputBlocking() -> void;
  auto sceAudioSRCChRelease() -> void;

  //utility.cpp: the system's dialogs, one at a time, and the optional modules loaded
  struct Dialog {
    u32 kind = 0;          //the last started (0: none yet)
    u32 status = 0;        //0 none, 1 starting, 2 running, 3 finished, 4 closing
    u32 next = 0;          //the status it goes to at changeAt (0: no change coming)
    u64 changeAt = 0;
    u32 parameters = 0;    //where its parameters are
  } dialog;
  std::vector<u32> utilityModules;  //the optional modules loaded (psputility_modules.h's numbers)
  auto dialogDue() -> void;
  auto dialogStart(u32 kind) -> void;
  auto dialogStatus(u32 kind) -> void;
  auto dialogUpdate(u32 kind) -> void;
  auto dialogShutdown(u32 kind) -> void;
  static auto hexWord(u32 value) -> std::string;
  auto savePath(const std::string& folder, const std::string& file = {}) -> std::string;
  auto savedata(u32 parameters) -> u32;
  auto savedataList(u32 parameters, const std::string& game) -> u32;
  auto sceUtilitySavedataInitStart() -> void;
  auto sceUtilitySavedataGetStatus() -> void;
  auto sceUtilitySavedataUpdate() -> void;
  auto sceUtilitySavedataShutdownStart() -> void;
  auto sceUtilityMsgDialogInitStart() -> void;
  auto sceUtilityMsgDialogGetStatus() -> void;
  auto sceUtilityMsgDialogUpdate() -> void;
  auto sceUtilityMsgDialogShutdownStart() -> void;
  auto sceUtilityOskInitStart() -> void;
  auto sceUtilityOskGetStatus() -> void;
  auto sceUtilityOskUpdate() -> void;
  auto sceUtilityOskShutdownStart() -> void;
  auto sceUtilityNetconfInitStart() -> void;
  auto sceUtilityNetconfGetStatus() -> void;
  auto sceUtilityNetconfUpdate() -> void;
  auto sceUtilityNetconfShutdownStart() -> void;
  auto sceUtilityGameSharingInitStart() -> void;
  auto sceUtilityGameSharingGetStatus() -> void;
  auto sceUtilityGameSharingUpdate() -> void;
  auto sceUtilityGameSharingShutdownStart() -> void;
  auto sceUtilityHtmlViewerInitStart() -> void;
  auto sceUtilityHtmlViewerGetStatus() -> void;
  auto sceUtilityHtmlViewerUpdate() -> void;
  auto sceUtilityHtmlViewerShutdownStart() -> void;
  auto sceUtilityLoadModule() -> void;
  auto sceUtilityUnloadModule() -> void;
  auto sceUtilityLoadNetModule() -> void;
  auto sceUtilityUnloadNetModule() -> void;
  auto sceUtilityGetSystemParamString() -> void;
  auto sceUtilitySetSystemParamString() -> void;

  //power.cpp: the battery, the clocks, the power switch's callbacks, the volatile memory
  struct Power {
    u32 callbacks[16] = {};       //the callbacks registered in each slot (0: none)
    u32 pll = 222, cpu = 222, bus = 111;  //the clocks asked for, in MHz (a PSP starts at these)
    bool volatileLocked = false;  //the volatile memory is lent to the game
  } powerState;
  auto resultFloat(float value) -> void;
  auto scePowerRegisterCallback() -> void;
  auto scePowerUnregisterCallback() -> void;
  auto scePowerIsPowerOnline() -> void;
  auto scePowerIsBatteryExist() -> void;
  auto scePowerIsBatteryCharging() -> void;
  auto scePowerGetBatteryChargingStatus() -> void;
  auto scePowerIsLowBattery() -> void;
  auto scePowerGetBatteryLifePercent() -> void;
  auto scePowerGetBatteryLifeTime() -> void;
  auto scePowerTick() -> void;
  auto scePowerSetClockFrequency() -> void;
  auto scePowerSetCpuClockFrequency() -> void;
  auto scePowerSetBusClockFrequency() -> void;
  auto scePowerGetCpuClockFrequency() -> void;
  auto scePowerGetBusClockFrequency() -> void;
  auto scePowerGetPllClockFrequencyInt() -> void;
  auto scePowerGetCpuClockFrequencyFloat() -> void;
  auto scePowerGetBusClockFrequencyFloat() -> void;
  auto scePowerGetPllClockFrequencyFloat() -> void;
  auto sceKernelPowerTick() -> void;
  auto sceKernelPowerLock() -> void;
  auto sceKernelPowerUnlock() -> void;
  auto sceKernelVolatileMemTryLock() -> void;
  auto sceKernelVolatileMemUnlock() -> void;

  //system.cpp: leaving, clocks, and the odds and ends a C library's start-up asks for
  u64 startTime = 0;  //the date when the PSP started, in microseconds since 1970
  auto result64(u64 value) -> void;
  auto sceKernelLibcClock() -> void;
  auto sceKernelSysClock2USec() -> void;
  auto sceKernelSysClock2USecWide() -> void;
  auto sceRtcGetTick() -> void;
  auto sceRtcCompareTick() -> void;
  auto sceKernelUtilsMt19937Init() -> void;
  auto sceKernelUtilsMt19937UInt() -> void;
  auto sceKernelPrintf() -> void;
  auto sceKernelSetGPO() -> void;
  auto sceWlanGetSwitchState() -> void;
  auto sceWlanGetEtherAddr() -> void;
  auto sceImposeSetLanguageMode() -> void;
  auto sceDmacMemcpy() -> void;
  auto sceKernelExitGame() -> void;
  auto sceKernelSelfStopUnloadModule() -> void;
  auto sceKernelStopUnloadSelfModuleWithStatus() -> void;
  auto sceUtilityGetSystemParamInt() -> void;
  auto sceNetInetUnavailable() -> void;
  auto sceKernelGetSystemTimeWide() -> void;
  auto sceKernelGetSystemTime() -> void;
  auto sceKernelLibcGettimeofday() -> void;
  auto sceKernelLibcTime() -> void;
  auto sceRtcGetCurrentTick() -> void;
  auto sceRtcGetTickResolution() -> void;
  auto sceKernelCacheUnneeded() -> void;

  //modules.cpp: the modules (PRXs) a program loads besides itself
  //(Unloading: its module_stop runs as it unloads itself, and it goes once that ends)
  enum class ModuleStatus : u32 { Loaded, Starting, Started, Stopping, Stopped, Unloading };
  struct LoadedModule {
    u32 uid = 0;
    std::string path;      //the file it was loaded from
    bool standIn = false;  //one of Sony's, which the HLE kernel stands in for: nothing of it is in memory
    u32 block = 0;         //the memory block it was loaded into (its ID), or 0
    ModuleStatus status = ModuleStatus::Loaded;
    u32 thread = 0;        //the thread running its module_start or module_stop
    Module module;         //what the loader found (for a stand-in, its name and attributes)
  };
  std::map<u32, LoadedModule> modules;
  u32 programUID = 0;      //the program's own module ID
  auto readWhole(const std::string& path, std::vector<u8>& data) -> u32;
  auto readOpenFile(OpenFile& open, u64 size, std::vector<u8>& data) -> u32;
  auto loadModule(const std::vector<u8>& file, const std::string& path) -> u32;
  auto standIn(const std::string& name, u32 attributes, const std::string& path) -> u32;
  auto unloadModule(u32 uid) -> void;
  auto unloadSelf(s32 exitStatus, u32 length, u32 argument, u32 options) -> void;
  auto linkImports() -> void;
  auto moduleAt(u32 address) const -> u32;
  auto moduleFunction(const LoadedModule& loaded, u32 nid) const -> u32;
  auto makeModuleThread(const LoadedModule& loaded, u32 entry, u32 parameters, u32 length, u32 argument,
                        u32 options) -> s32;
  auto madeForModule(u32 thread) const -> bool;
  auto runModuleFunction(LoadedModule& loaded, u32 nid, ModuleStatus during) -> void;
  auto moduleThreadEnded(Thread& thread, s32 status) -> void;
  auto moduleReturned() -> void;
  auto sceKernelLoadModule() -> void;
  auto sceKernelLoadModuleByID() -> void;
  auto sceKernelStartModule() -> void;
  auto sceKernelStopModule() -> void;
  auto sceKernelUnloadModule() -> void;
  auto sceKernelGetModuleIdByAddress() -> void;
  auto sceKernelGetModuleId() -> void;
  auto sceKernelGetModuleIdList() -> void;
  auto sceKernelQueryModuleInfo() -> void;
};

}
