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
  static constexpr u32 ErrorUnknownUID            = 0x8002'00cb;
  static constexpr u32 ErrorIllegalArgument       = 0x8002'00d2;
  static constexpr u32 ErrorIllegalAddress        = 0x8002'00d3;
  static constexpr u32 ErrorIllegalPartition      = 0x8002'00d6;
  static constexpr u32 ErrorAllocationFailed      = 0x8002'00d9;
  static constexpr u32 ErrorNotYetLinked          = 0x8002'013a;  //a function the kernel doesn't have
  static constexpr u32 ErrorIllegalContext        = 0x8002'0064;  //waiting, from an interrupt handler
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
  static constexpr u32 ErrorNotSupported          = 0x8000'0004;
  static constexpr u32 ErrorAlready               = 0x8000'0020;
  static constexpr u32 ErrorBusy                  = 0x8000'0021;
  static constexpr u32 ErrorOutOfMemory           = 0x8000'0022;
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

  static constexpr u64 CPUFrequency = 333'000'000;                   //cycles a second
  static constexpr u64 VblankCycles = CPUFrequency * 1001 / 60'000;  //59.94 frames a second
  static constexpr u32 Trampoline = 0x0800'0000;  //kernel memory: where a thread returns to when its entry function
                                                  //ends (and, 8 bytes on, a call into the program)
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
  static constexpr u32 ThreadReturnCode = 1, CallReturnCode = 2, FirstImportCode = 0x1000;

  //threads.cpp
  struct Context {  //a thread's registers while another runs
    u32 gpr[32], lo, hi, pc, pd;
    u32 fpr[32], fcsr;
    u32 vpr[128], pfxs, pfxt, pfxd, cc;
  };
  enum class Status : u32 { Running = 1, Ready = 2, Waiting = 4, Dormant = 16 };  //the PSP's numbers
  enum class Wait : u32 {
    None, Delay, Sleep, Semaphore, LwMutex, Vblank, ThreadEnd, Controller, EventFlag, GeList, GeDraw, Umd,
  };
  struct Thread {
    u32 uid;
    std::string name;
    u32 entry, priority, initialPriority, stackSize, stackBlock, attributes, gp;
    Status status = Status::Dormant;
    Context context{};
    Wait wait = Wait::None;
    u32 waitID = 0;        //the semaphore, mutex, thread, event flag or display list waited for
    u32 waitCount = 0;     //how many a semaphore or mutex wait needs; the bits an event flag wait needs
    u32 waitMode = 0;      //an event flag wait's mode
    u32 waitPointer = 0;   //where an event flag wait puts the bits it saw
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
  auto save(Context& context) -> void;
  auto restore(const Context& context) -> void;
  auto ready(Thread& thread, u32 returnValue) -> void;
  auto block(Wait wait, u32 id, u64 wakeAt, u32 timeoutPointer = 0) -> void;
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
  auto sceDisplaySetMode() -> void;
  auto sceDisplaySetFrameBuf() -> void;
  auto sceDisplayGetFrameBuf() -> void;
  auto sceDisplayWaitVblankStart() -> void;
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
  };
  std::map<u32, EventFlag> eventFlags;
  std::map<u32, Callback> callbacks;
  u32 exitCallback = 0;
  auto eventFlagWaiters(const EventFlag& flag) -> std::vector<Thread*>;
  auto eventFlagFor(u32 uid, u32 bits, u32 mode) -> EventFlag*;
  auto eventFlagTimedOut(Thread& thread) -> void;
  auto sceKernelCreateEventFlag() -> void;
  auto sceKernelDeleteEventFlag() -> void;
  auto sceKernelSetEventFlag() -> void;
  auto sceKernelClearEventFlag() -> void;
  auto sceKernelWaitEventFlag() -> void;
  auto sceKernelPollEventFlag() -> void;
  auto sceKernelReferEventFlagStatus() -> void;
  auto sceKernelCreateCallback() -> void;
  auto sceKernelDeleteCallback() -> void;
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

  //system.cpp: leaving, clocks, and the odds and ends a C library's start-up asks for
  u64 startTime = 0;  //the date when the PSP started, in microseconds since 1970
  auto result64(u64 value) -> void;
  auto sceKernelExitGame() -> void;
  auto sceKernelSelfStopUnloadModule() -> void;
  auto sceUtilityGetSystemParamInt() -> void;
  auto sceNetInetUnavailable() -> void;
  auto sceKernelGetSystemTimeWide() -> void;
  auto sceKernelGetSystemTime() -> void;
  auto sceKernelLibcGettimeofday() -> void;
  auto sceKernelLibcTime() -> void;
  auto sceRtcGetCurrentTick() -> void;
  auto sceRtcGetTickResolution() -> void;
  auto sceKernelCacheUnneeded() -> void;
};

}
