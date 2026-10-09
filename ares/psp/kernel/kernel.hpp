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
#include "disc-info.hpp"
#include "crypto.hpp"
#include "pgf.hpp"
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

//codec.cpp: the decoders under the PSP's music and movies. The libraries (atrac.cpp, mp3.cpp, mpeg.cpp) take the
//PSP's containers apart themselves and hand a decoder one frame, or one picture's access unit, at a time; builds with
//ARES_ENABLE_FFMPEG decode them with FFmpeg's LGPL decoders, and builds without have none, where the libraries refuse
//the streams as they did before there were decoders.
struct AudioDecoder {
  enum class Codec : u32 { Atrac3, Atrac3plus, Mp3 };
  struct Format {
    static constexpr u32 MaxExtra = 64;  //the most of extra a file keeps (and a state holds)
    Codec codec = Codec::Atrac3plus;
    u32 channels = 2;        //the samples a frame decodes to, per sample
    u32 rate = 44'100;       //samples a second
    u32 frameBytes = 0;      //an ATRAC frame's size (the file's block alignment); MP3 frames say theirs
    std::vector<u8> extra;   //the codec's own parameters from the container (a RIFF fmt chunk's last bytes)
  };
  virtual ~AudioDecoder() = default;
  //One whole frame into 16-bit samples, the channels interleaved, at most room samples a channel: how many a channel
  //came out, or -1 for a frame that can't be decoded.
  virtual auto decode(const u8* data, u32 size, s16* samples, u32 room) -> s32 = 0;
  //Forget the frames before: the next one starts afresh (a jump elsewhere in the stream).
  virtual auto reset() -> void = 0;
};

struct VideoDecoder {
  //The widest and tallest picture a movie shows (and a state holds): more than the PSP's (480 by 272) and UMD
  //Video's (720 by 480); the decoder makes none larger than this squared, and larger ones either way are passed over.
  static constexpr u32 MaxSide = 1024;
  //A picture in 8-bit Y, Cb and Cr planes, the colour ones half its size each way (4:2:0).
  struct Picture {
    u32 width = 0, height = 0;
    const u8* planes[3] = {};
    u32 strides[3] = {};
  };
  virtual ~VideoDecoder() = default;
  //One H.264 access unit: true if a picture came out of the decoder, which picture() holds until the next decode()
  //or reset().
  virtual auto decode(const u8* data, u32 size) -> bool = 0;
  virtual auto picture() const -> const Picture& = 0;
  virtual auto reset() -> void = 0;
};

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
  static constexpr u32 ErrorNotWait               = 0x8002'01a6;  //a thread let go of a wait it isn't in
  static constexpr u32 ErrorWaitTimeout           = 0x8002'01a8;
  static constexpr u32 ErrorWaitCancelled         = 0x8002'01a9;
  static constexpr u32 ErrorReleaseWait           = 0x8002'01aa;  //what a thread let go of its wait is told
  static constexpr u32 ErrorUnknownAlarm          = 0x8002'019f;
  static constexpr u32 ErrorUnknownVTimer         = 0x8002'01be;
  static constexpr u32 ErrorIllegalVTimer         = 0x8002'01bf;
  //the mutexes': as pspautotests' threads/mutex recorded them (pspkerror.h names none, nor do these names)
  static constexpr u32 ErrorUnknownMutex          = 0x8002'01c3;
  static constexpr u32 ErrorMutexLocked           = 0x8002'01c4;  //a try on a mutex another thread holds
  static constexpr u32 ErrorMutexUnlocked         = 0x8002'01c5;  //unlocking a mutex the caller doesn't hold
  static constexpr u32 ErrorMutexOverflow         = 0x8002'01c6;  //a recursive lock counted past 2^31 - 1
  static constexpr u32 ErrorUnlockUnderflow       = 0x8002'01c7;  //unlocking more than the holder holds
  static constexpr u32 ErrorMutexRecursion        = 0x8002'01c8;  //its holder locking a mutex that isn't recursive
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
  static constexpr u32 ErrorUnknownMailbox        = 0x8002'019b;
  static constexpr u32 ErrorUnknownMessagePipe    = 0x8002'019e;
  static constexpr u32 ErrorMailboxEmpty          = 0x8002'01b2;  //a poll that finds no message
  static constexpr u32 ErrorPipeFull              = 0x8002'01b3;  //a try that can't send
  static constexpr u32 ErrorPipeEmpty             = 0x8002'01b4;  //a try that can't receive
  static constexpr u32 ErrorIllegalSize           = 0x8002'01bc;
  static constexpr u32 ErrorIllegalType           = 0x8002'01bb;  //a kind of object there isn't (pspkerror.h)
  static constexpr u32 ErrorMessageQueued         = 0x8002'01c9;  //a mailbox's packet sent again while queued
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
  static constexpr u32 ErrorCrossDevice           = 0x8002'0322;  //pspkerror.h's XDEV: a rename to another device
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
  static constexpr u32 ErrorAsyncBusy             = 0x8002'0329;  //a file's asynchronous request still under way
  static constexpr u32 ErrorNoAsync               = 0x8002'032a;  //no asynchronous request to wait for or poll
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
  static constexpr u32 ErrorCanNotStop            = 0x8002'0136;
  static constexpr u32 ErrorNotStopped            = 0x8002'0137;
  static constexpr u32 ErrorUnsupportedPrxType    = 0x8002'0148;  //an encrypted module that can't be decrypted

  static constexpr u64 CPUFrequency = 333'000'000;                   //cycles a second
  static constexpr u64 VblankCycles = CPUFrequency * 1001 / 60'000;  //59.94 frames a second
  static constexpr u32 Trampoline = 0x0800'0000;  //kernel memory: where a thread returns to when its entry function
                                                  //ends (8 bytes on, a call into the program; 16 on, a module's
                                                  //module_start or module_stop; 24 on, a thread's callback; 32 on,
                                                  //the program's alloc or free called by sceLibFont)
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
  auto start(const u8* data, u64 size, const std::string& path, std::string& error,
             const std::vector<u8>* given = nullptr) -> bool;
  auto run(u64 budget) -> u64;
  auto importCode(const std::string& library, u32 nid) -> u32;
  auto syscall(u32 code) -> bool;
  auto dispatch(u32 code) -> bool;
  auto runUntilNextEvent() -> void;
  auto arg(u32 n) const -> u32;
  auto result(u32 value) -> void;
  auto note(const std::string& text) -> void;
  auto serialize(serializer& s) -> bool;  //serialization.cpp: for save states; false if a state is damaged

  Allegrex& cpu;
  Memory& memory;
  GE& ge;
  Module module;           //the program
  bool exited = false;     //it called sceKernelExitGame
  bool stuck = false;      //nothing will run again, which has been noted (once, not every frame; not saved)
  u64 cycles = 0;          //time since power on
  u64 counted = 0;         //how many of the CPU's instructions in its go under way cycles has taken in (syscall())
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
  static constexpr u32 ThreadReturnCode = 1, CallReturnCode = 2, ModuleReturnCode = 3, CallbackReturnCode = 4,
                       FontReturnCode = 5, MpegReturnCode = 6;
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
    Fpl, Vpl, Module, Async, PipeSend, PipeReceive, Mailbox,
    File,  //a synchronous read or write, for the time its file's device takes (io.cpp)
    Volatile,  //the volatile memory, lent to another (power.cpp)
    Codec,     //the Media Engine decoding for it (codec.cpp); the call returns waitCount as it ends
    Mutex,     //a kernel mutex another thread holds (mutexes.cpp)
    Psmf,      //the movie player's work (psmfplayer.cpp: opening a movie, a picture, making or deleting the player);
               //the call returns waitCount as it ends
  };
  struct WaitState {  //a thread's wait, put aside while its callbacks run (they may wait themselves)
    Wait wait = Wait::None;
    u32 id = 0, count = 0, mode = 0, pointer = 0, timeoutPointer = 0;
    u64 wakeAt = 0;
    bool callbacks = false;
    u32 done = 0, resultPointer = 0;
  };
  struct Thread {
    u32 uid;
    std::string name;
    u32 entry, priority, initialPriority, stackSize, stackBlock, attributes, gp;
    Status status = Status::Dormant;
    Context context{};
    Wait wait = Wait::None;
    u32 waitID = 0;        //the semaphore, mutex (a kernel one's ID, a lightweight one's work area), thread, event
                           //flag, display list, module, message pipe or mailbox waited for; the sound channel (0-7
                           //a mixer channel's, Audio::WaitSrc or WaitSrcDrain the SRC channel's); the file whose
                           //asynchronous request is waited for, or that a synchronous read or write went to
    u32 waitCount = 0;     //how many a semaphore or mutex wait needs (the count a mutex is to be held with); the
                           //bits an event flag wait needs; a mixer output's left volume; the samples an SRC output's
                           //buffer was armed with; the bytes a message pipe's send or receive asked for; what a
                           //synchronous read or write returns; the count of vertical blanks a blank's wait ends at
    u32 waitMode = 0;      //an event flag wait's mode; a mixer output's right volume; a message pipe's mode
    u32 waitPointer = 0;   //where an event flag wait puts the bits it saw, a module wait the function's result, an
                           //asynchronous wait the request's result, a mailbox wait the message; the buffer a mixer
                           //output hands over, or a message pipe's send or receive reads or fills
    u32 waitDone = 0;      //the bytes a message pipe's send or receive has moved so far
    u32 waitResult = 0;    //where a message pipe's send or receive puts how many it moved
    u64 wakeAt = 0;        //for a delay or timeout: the cycle to wake at (0: none)
    u32 timeoutPointer = 0;
    u64 readySince = 0;    //to keep first-come order among equal priorities
    s32 exitStatus = s32(ErrorDormant);  //what it ended with: DORMANT until it first starts, then NOT_DORMANT
    //what its status's run figures count, since it was last started (threads.cpp): the cycles it has had the CPU,
    //the times a call into the program interrupted it and a better thread took the CPU from it, and the times
    //sceKernelReleaseWaitThread let it go of a wait
    u64 runCycles = 0;
    u32 interruptPreempts = 0, threadPreempts = 0, releases = 0;
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
  struct LwMutex {  //a lightweight mutex: its state is in the program's work area; these are for its status
    u32 workArea = 0;
    std::string name;
    u32 attributes = 0;
    s32 initial = 0;  //the count it was made with
  };
  std::map<u32, std::unique_ptr<Thread>> threads;
  std::map<u32, Semaphore> semaphores;
  std::map<u32, LwMutex> lwMutexes;
  Thread* current = nullptr;
  u64 ranSince = 0;  //when the running thread got the CPU, or a call into the program on top of it ended
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
  static constexpr u64 TimeoutLeast = 205, TimeoutLate = 35, TimeoutAtOnce = 30;  //microseconds (blockTimed())
  auto blockTimed(Wait wait, u32 id, u32 pointer, bool callbacks) -> void;
  auto reschedule() -> void;
  auto switchTo(Thread* thread) -> void;
  auto events() -> void;
  auto timeUp(const Thread& thread) const -> u32;
  auto leaveWait(Thread& thread, u32 value) -> void;
  auto waiterLeft(Wait wait, u32 id) -> void;
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

  struct Report {  //a status structure built for the program, as report() copies it
    std::vector<u8> bytes;
    explicit Report(u32 size) : bytes(size) { word(0, size); }
    auto word(u32 offset, u32 value) -> void { for(u32 n = 0; n < 4; n++) bytes[offset + n] = value >> n * 8; }
    auto name(u32 offset, const std::string& text) -> void {  //a 32-byte field, its last byte a NUL
      std::copy_n(text.begin(), std::min<size_t>(text.size(), 31), bytes.begin() + offset);
    }
  };
  auto report(u32 address, const Report& structure) -> void;
  auto threadStatus(const Thread& thread) const -> u32;
  auto threadWaitType(const Thread& thread) const -> u32;
  auto threadRunTime(const Thread& thread) const -> u64;
  auto threadmanIDs(u32 type, std::vector<u32>& ids) -> bool;

  auto sceKernelCreateThread() -> void;
  auto sceKernelStartThread() -> void;
  auto sceKernelExitThread() -> void;
  auto sceKernelExitDeleteThread() -> void;
  auto sceKernelDeleteThread() -> void;
  auto sceKernelGetThreadId() -> void;
  auto sceKernelReferThreadStatus() -> void;
  auto sceKernelReferThreadRunStatus() -> void;
  auto sceKernelReferSystemStatus() -> void;
  auto sceKernelGetThreadmanIdList() -> void;
  auto sceKernelGetThreadmanIdType() -> void;
  auto sceKernelDelayThread() -> void;
  auto sceKernelDelayThreadCB() -> void;
  auto sceKernelSleepThread() -> void;
  auto sceKernelWakeupThread() -> void;
  auto sceKernelCancelWakeupThread() -> void;
  auto sceKernelReleaseWaitThread() -> void;
  auto sceKernelWaitThreadEnd() -> void;
  auto sceKernelWaitThreadEndCB() -> void;
  auto sceKernelCreateSema() -> void;
  auto sceKernelDeleteSema() -> void;
  auto sceKernelSignalSema() -> void;
  auto sceKernelWaitSema() -> void;
  auto sceKernelWaitSemaCB() -> void;
  auto sceKernelPollSema() -> void;
  auto sceKernelCancelSema() -> void;
  auto sceKernelReferSemaStatus() -> void;
  auto sceKernelChangeThreadPriority() -> void;
  auto sceKernelGetThreadExitStatus() -> void;
  auto sceKernelTerminateThread() -> void;
  auto sceKernelTerminateDeleteThread() -> void;
  auto sceKernelSuspendThread() -> void;
  auto sceKernelResumeThread() -> void;
  auto sceKernelChangeCurrentThreadAttr() -> void;
  auto sceKernelGetThreadStackFreeSize() -> void;
  auto sceKernelCheckThreadStack() -> void;
  auto sceKernelReferThreadProfiler() -> void;
  auto sceKernelGetThreadCurrentPriority() -> void;
  auto sceKernelRotateThreadReadyQueue() -> void;
  bool dispatchSuspended = false;  //sceKernelSuspendDispatchThread: the running thread keeps the CPU
  auto sceKernelSuspendDispatchThread() -> void;
  auto sceKernelResumeDispatchThread() -> void;
  auto sceKernelCreateLwMutex() -> void;
  auto lwMutexStatus(u32 uid, u32 address) -> void;
  auto sceKernelReferLwMutexStatus() -> void;
  auto sceKernelReferLwMutexStatusByID() -> void;
  auto sceKernelDeleteLwMutex() -> void;
  auto lockLwMutex(bool callbacks) -> void;
  auto sceKernelLockLwMutex() -> void;
  auto sceKernelLockLwMutexCB() -> void;
  auto sceKernelTryLockLwMutex() -> void;
  auto sceKernelUnlockLwMutex() -> void;
  auto sceKernelGetSystemTimeLow() -> void;

  //mutexes.cpp: kernel mutexes, a lock one thread holds at a time (recursively, if made so), waited for in line
  struct Mutex {
    u32 uid;
    std::string name;
    u32 attributes;  //0x100: waiters served by priority (else first come); 0x200: its holder may lock it again
    s32 initial;     //the count it was made with
    s32 count;       //how many times it's locked (0: free)
    u32 owner;       //the thread holding it (0 while it's free)
  };
  std::map<u32, Mutex> mutexes;
  auto mutexWaiters(const Mutex& mutex) -> std::vector<Thread*>;
  auto mutexHandOver(Mutex& mutex) -> void;
  auto mutexesFreed(u32 thread) -> void;
  auto lockMutex(bool callbacks) -> void;
  auto sceKernelCreateMutex() -> void;
  auto sceKernelDeleteMutex() -> void;
  auto sceKernelLockMutex() -> void;
  auto sceKernelLockMutexCB() -> void;
  auto sceKernelTryLockMutex() -> void;
  auto sceKernelUnlockMutex() -> void;
  auto sceKernelCancelMutex() -> void;
  auto sceKernelReferMutexStatus() -> void;

  //timers.cpp: alarms and virtual timers, whose handlers the system's timer calls as calls into the program
  static constexpr u64 TimerLead = 215;  //microseconds: the soonest a timer a system call sets going goes off
  static constexpr u32 TimerClocks = Trampoline + 0x80;  //kernel memory: a virtual timer handler's two clocks, its
                                                         //schedule and its time now (16 bytes)
  struct Alarm {
    u32 uid;
    u64 schedule;      //when it's due, in the system's time (microseconds since power on)
    u64 earliest;      //and the soonest it may go off (a cycle): TimerLead after the call that set it
    u32 handler, common, gp;
    bool calling = false;  //its handler waits its turn among the calls into the program, or runs
  };
  struct VTimer {
    u32 uid;
    std::string name;
    bool active = false;   //started
    u64 base = 0;          //the system's time when it was started (0 while it's stopped)
    u64 elapsed = 0;       //its time as it was started, stopped or set; running, it's this plus the system's time
                           //since base
    u64 schedule = 0;      //its time at which its handler is called
    u32 handler = 0, common = 0, gp = 0;
    u64 earliest = 0;      //the soonest its handler may be called (a cycle): TimerLead after a call set it going
    bool calling = false;  //its handler waits its turn among the calls into the program, or runs
  };
  std::map<u32, Alarm> alarms;
  std::map<u32, VTimer> vtimers;
  auto systemTime() const -> u64;
  auto timerEarliest() const -> u64;
  auto alarmDue(const Alarm& alarm) const -> u64;
  auto vtimerTime(const VTimer& timer) const -> u64;
  auto vtimerDue(const VTimer& timer) const -> u64;
  auto nextTimerEvent() const -> u64;
  auto timerEvents() -> void;
  auto timerCallsDropped(u32 kind, u32 uid) -> void;
  auto vtimerInHandler() const -> u32;
  auto vtimerClocks(u32 uid) -> void;
  auto timerReturned(u32 kind, u32 uid, u32 again) -> void;
  auto setAlarm(u64 schedule, u32 handler, u32 common) -> void;
  auto vtimerFor(u32 uid) -> VTimer*;
  auto vtimerAt(u32 uid) -> VTimer*;
  auto vtimerSet(VTimer& timer, u64 time) -> u64;
  auto vtimerHandler(VTimer& timer, u64 schedule, u32 handler, u32 common) -> void;
  auto sceKernelSetAlarm() -> void;
  auto sceKernelSetSysClockAlarm() -> void;
  auto sceKernelCancelAlarm() -> void;
  auto sceKernelReferAlarmStatus() -> void;
  auto sceKernelCreateVTimer() -> void;
  auto sceKernelDeleteVTimer() -> void;
  auto sceKernelGetVTimerBase() -> void;
  auto sceKernelGetVTimerBaseWide() -> void;
  auto sceKernelGetVTimerTime() -> void;
  auto sceKernelGetVTimerTimeWide() -> void;
  auto sceKernelSetVTimerTime() -> void;
  auto sceKernelSetVTimerTimeWide() -> void;
  auto sceKernelStartVTimer() -> void;
  auto sceKernelStopVTimer() -> void;
  auto sceKernelSetVTimerHandler() -> void;
  auto sceKernelSetVTimerHandlerWide() -> void;
  auto sceKernelCancelVTimerHandler() -> void;
  auto sceKernelReferVTimerStatus() -> void;

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
  auto programBlockAt() const -> u32;
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
  auto sceKernelAllocMemoryBlock() -> void;
  auto sceKernelFreeMemoryBlock() -> void;
  auto sceKernelGetMemoryBlockPtr() -> void;
  auto sceKernelDevkitVersion() -> void;

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
    //Its asynchronous request (async.cpp): none; one under way, done at asyncDoneAt; or one done whose result the
    //program hasn't taken yet. The result is 64 bits: a count, a position, or an error (sign-extended).
    enum class Async : u32 { None, Pending, Done } async = Async::None;
    u64 asyncDoneAt = 0;
    u64 asyncResult = 0;
    u32 asyncCallback = 0, asyncArgument = 0;  //sceIoSetAsyncCallback's: notified as each request is done
    bool resultOnly = false;  //nothing is open (an asynchronous close, or an asynchronous open that failed): the
                              //descriptor stays only to hand over its request's result
  };
  std::map<std::string, std::string> devices;  //"ms0" -> the host folder standing for it
  std::shared_ptr<Disc> disc;  //the disc image in the drive (disc0: and umd0:, unless a host folder stands for it)
  std::map<u32, OpenFile> files;
  u32 nextFile = 3;  //after standard input, output and error
  auto newFile() -> u32;
  std::string workingDirectory;
  std::vector<u32> memoryStickCallbacks;  //callbacks the program registered for the memory stick going in and out
  //the memory stick's size and free space, as every function that tells them reports them (io.cpp)
  static constexpr u32 StickSectorSize = 512, StickClusterSize = 32_KiB;
  static constexpr u32 StickClusters = 61'440, StickFreeClusters = 57'344;
  auto stickText(u32 at, u64 kilobytes) -> void;
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
  auto openFile(const std::string& path, u32 flags) -> u32;
  auto readFile(u32 file, u32 data, u32 size) -> u32;
  auto writeFile(u32 file, u32 data, u32 size) -> u32;
  auto fileWaitRefused() const -> u32;
  auto fileWait(u32 file, u32 value, bool onDisc, u64 bytes) -> void;
  auto seek(u32 file, s64 offset, u32 whence, u64& position) -> u32;
  auto ioctl(u32 file, u32 command, u32 in, u32 inLength, u32 out, u32 outLength, u64* moved = nullptr) -> u32;
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
  auto sceIoAssign() -> void;
  auto sceIoGetstat() -> void;
  auto sceIoDopen() -> void;
  auto sceIoDread() -> void;
  auto sceIoDclose() -> void;
  auto sceIoIoctl() -> void;
  auto sceIoDevctl() -> void;
  auto sceNpDrmSetLicenseeKey() -> void;
  auto sceNpDrmClearLicenseeKey() -> void;
  auto sceNpDrmRenameCheck() -> void;
  auto sceNpDrmEdataSetupKey() -> void;
  auto sceNpDrmEdataGetDataSize() -> void;

  //async.cpp: files' asynchronous requests, done after the time their device takes
  auto asyncBusy(u32 file) const -> bool;
  auto asyncDuration(bool onDisc, u64 bytes) const -> u64;
  auto asyncIssue(u32 file) -> OpenFile*;
  auto asyncStart(OpenFile& open, s64 result, u64 bytes) -> void;
  auto asyncTake(u32 file, u32 pointer) -> void;
  auto asyncWaiters(u32 file) -> std::vector<Thread*>;
  auto asyncResume(Thread& thread) -> bool;
  auto asyncPoll(u32 file, u32 pointer) -> void;
  auto asyncWait(u32 file, u32 pointer, bool callbacks) -> void;
  auto asyncEvents() -> bool;
  auto nextAsyncEvent() const -> u64;
  auto sceIoOpenAsync() -> void;
  auto sceIoCloseAsync() -> void;
  auto sceIoReadAsync() -> void;
  auto sceIoWriteAsync() -> void;
  auto sceIoLseekAsync() -> void;
  auto sceIoLseek32Async() -> void;
  auto sceIoIoctlAsync() -> void;
  auto sceIoPollAsync() -> void;
  auto sceIoWaitAsync() -> void;
  auto sceIoWaitAsyncCB() -> void;
  auto sceIoGetAsyncStat() -> void;
  auto sceIoChangeAsyncPriority() -> void;
  auto sceIoSetAsyncCallback() -> void;

  //umd.cpp: the disc drive
  static constexpr u32 UmdNotPresent = 0x01, UmdPresent = 0x02, UmdChanged = 0x04, UmdNotReady = 0x08,
                       UmdReady = 0x10, UmdReadable = 0x20;
  u32 umdCallback = 0;  //the callback the program registered for the drive's changes
  bool umdDeactivated = false;  //the program deactivated the drive (sceUmdDeactivate): not readable till activated
  auto umdState() const -> u32;
  auto umdChanged(u32 notified) -> void;
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
    s32 idleReset = -1, idleBack = -1;  //how far the stick moves to put off the idle timer, and to wake from idle
                                        //(sceCtrlSetIdleCancelThreshold: -1 never, 0 always, else 1-128)
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
  auto sceHprmIsHeadphoneExist() -> void;
  auto sceHprmIsRemoteExist() -> void;
  auto sceHprmIsMicrophoneExist() -> void;
  auto sceHprmPeekCurrentKey() -> void;
  auto sceHprmPeekLatch() -> void;
  auto sceHprmReadLatch() -> void;
  auto sceCtrlSetIdleCancelThreshold() -> void;
  auto sceCtrlGetIdleCancelThreshold() -> void;

  //display.cpp: where the program's frame is, and the vertical blank
  struct Display {
    u32 mode = 0, width = 480, height = 272;
    u32 frameBuffer = 0, bufferWidth = 0, pixelFormat = 0;
    u32 hcountBase = 0;  //what the accumulated count of lines adds (sceDisplayAdjustAccumulatedHcount)
  } display;
  static constexpr u64 LineCycles = CPUFrequency * 525 / 9'000'000;  //a line: 525 dots at 9 MHz (286 to a frame)
  static constexpr u64 VblankLength = CPUFrequency * 77 / 100'000;    //the vertical blank lasts 0.77 ms
  auto inVblank() const -> bool;
  auto waitVblank(bool callbacks, u32 count = 1) -> void;
  auto sceDisplaySetMode() -> void;
  auto sceDisplaySetFrameBuf() -> void;
  auto sceDisplayGetFrameBuf() -> void;
  auto sceDisplayWaitVblankStart() -> void;
  auto sceDisplayWaitVblankStartCB() -> void;
  auto sceDisplayWaitVblank() -> void;
  auto sceDisplayWaitVblankCB() -> void;
  auto sceDisplayWaitVblankStartMulti() -> void;
  auto sceDisplayWaitVblankStartMultiCB() -> void;
  auto sceDisplayIsVblank() -> void;
  auto sceDisplayIsForeground() -> void;
  auto hcountLines() const -> u32;
  auto sceDisplayGetCurrentHcount() -> void;
  auto sceDisplayGetAccumulatedHcount() -> void;
  auto sceDisplayAdjustAccumulatedHcount() -> void;
  auto sceDisplayGetFramePerSec() -> void;
  auto sceDisplayGetVcount() -> void;
  auto picture(std::vector<u32>& pixels) -> void;

  //interrupts.cpp: calls into the program, as interrupt handlers
  struct Call {
    enum : u32 { Plain, Alarm, VTimer, Ge };  //what its return value means: nothing, or when a timer goes off
                                               //again; or a GE callback (sceGeBreak drops those)
    u32 function, gp;
    u32 arguments[4];
    bool resumesGe;       //the GE waits for it (a SIGNAL that suspends the list)
    bool vblank = false;  //a vertical blank's handler
    u32 kind = Plain;     //a timer's handler (timers.cpp), and which timer
    u32 id = 0;
  };
  std::deque<Call> calls;       //waiting their turn
  bool interrupting = false;    //one is running
  u32 callKind = Call::Plain, callID = 0;  //the one running: a timer's handler?
  u32& interruptsEnabled;       //the CPU's interrupt flag (mfic and mtic's, cpu.scc.interrupts): 1 on, 0 held off
                                //(sceKernelCpuSuspendIntr), calls held back and the CPU kept for the running thread
  bool rescheduleAfter = false;   //a thread woke during the call: pick who runs once it's over
  Context interrupted{};        //the CPU as the call found it
  bool interruptedHalted = false;
  bool callResumesGe = false;
  auto queueCall(u32 function, u32 gp, u32 a0, u32 a1, u32 a2, bool resumesGe = false) -> void;
  auto startCall() -> void;
  auto callReturned() -> void;
  auto mayWait() -> bool;
  auto fromThread() -> bool;
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
  auto vblankQueued() const -> bool;
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
  auto sceKernelCancelEventFlag() -> void;
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
  u32 geTranslation = 0x400;  //sceGeEdramSetAddrTranslation's width (the PSP starts at 0x400)
  auto sceGeEdramGetAddr() -> void;
  auto sceGeEdramGetSize() -> void;
  auto sceGeEdramSetAddrTranslation() -> void;
  auto sceGeListEnQueue() -> void;
  auto sceGeListEnQueueHead() -> void;
  auto sceGeListDeQueue() -> void;
  auto sceGeListUpdateStallAddr() -> void;
  auto sceGeListSync() -> void;
  auto sceGeDrawSync() -> void;
  auto sceGeSetCallback() -> void;
  auto sceGeUnsetCallback() -> void;
  auto sceGeContinue() -> void;
  auto sceGeBreak() -> void;
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
  //buffers armed at most. Their timing is the PSP's, and what they play is added up, as it's heard, in the output
  //the system hands to the speakers.
  struct Audio {
    //A block, 64 samples at 44.1 kHz, isn't a whole number of the CPU's cycles at 333 MHz: it's 483,265 and 15/49.
    static constexpr u64 BlockCycles = CPUFrequency * 64 / 44'100;
    static constexpr u32 BlockFraction = CPUFrequency * 64 * 49 / 44'100 % 49;
    static_assert((BlockCycles * 49 + BlockFraction) * 44'100 == CPUFrequency * 64 * 49);
    //Sound frames (a left and a right sample) are counted from power on, 44,100 a second: frame n is heard at cycle
    //n * CPUFrequency / 44100, which is n * FrameCycles / FrameRate with the fraction cut down (370000 / 49).
    static constexpr u64 FrameCycles = 370'000, FrameRate = 49;
    static_assert(FrameCycles * 44'100 == CPUFrequency * FrameRate);
    static constexpr u32 WaitSrc = 8, WaitSrcDrain = 9;  //waitIDs on the SRC channel (0-7: the mixer channels)
    //An SRC buffer's slot frees as its transfer ends, 100 microseconds before its last samples are heard
    //(srcOutput()); the shortest buffer, 17 samples at 48 kHz, plays for 354.
    static constexpr u64 SrcLead = CPUFrequency / 10'000;
    static_assert(17 * CPUFrequency / 48'000 > SrcLead);
    //A buffer is heard to its end in the output as its slot frees (srcRetire()), so the SRC channel adds to frames
    //ahead of the clock: SrcLead's 4.41 frames, and under two more where its ends fall between frames (it starts at
    //the frame heard as it's armed, up to a frame before then, and ends on the first frame past its last sample).
    //SrcAhead at most: SrcLead's frames rounded up, and two.
    static constexpr u64 SrcAhead = (SrcLead * FrameRate + FrameCycles - 1) / FrameCycles + 2;
    static_assert(SrcAhead == 7);
    //The output: what the channels play, added up frame by frame where it's heard, until the system takes it (each
    //frame of the PSP's 735.7 sound frames). A ring of OutputFrames frames, frame n in slot n % OutputFrames: far
    //more than a frame's worth, plus the block or so the mixer adds ahead of the clock.
    static constexpr u32 OutputFrames = 4096;
    struct Output {
      std::vector<s32> samples;       //left and right, side by side, OutputFrames pairs (power() makes them)
      u64 start = 0;                  //the first frame the system hasn't taken: the ring holds start onwards
      u64 end = 0;                    //one past the last frame anything was added to (start, if nothing was)
    } output;
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
      //Its samples converted to the output's 44.1 kHz (srcRender()): the next output frame it adds to (SrcAhead
      //frames past the clock at most), and where that frame falls in the armed buffers' samples, in 44100ths of a
      //sample from the first buffer's start (past its end, into the second's). Each output frame moves it on by the
      //channel's rate.
      u64 renderedTo = 0;
      u64 position = 0;
    } src;
  } audio;
  auto sampleFrame(u64 cycle, u32 fraction = 0) const -> u64;
  auto frameCycle(u64 frame) const -> u64;
  auto outputRoom(u64 last) -> u64;
  auto audioWaiter(u32 waitID) -> Thread*;
  auto audioWaitRefused() const -> u32;
  auto handOver(u32 number, u32 buffer, s32 left, s32 right) -> void;
  auto mixBlock(const Audio::Channel& channel, s32* block) -> void;
  auto mixerBlock() -> bool;
  auto mixerOutput(u32 number, u32 buffer, s32 left, s32 right, bool blocking) -> void;
  auto srcDuration(u32 samples) const -> u64;
  auto srcSample(u64 index, s32& left, s32& right, u32& volume) -> bool;
  auto srcRender(u64 until, bool wholeBuffer = false) -> void;
  auto srcRetire() -> bool;
  auto audioOutput(std::vector<s16>& frames) -> void;
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

  //messages.cpp: message pipes (bytes from thread to thread, through a buffer or straight across) and mailboxes
  //(packets the program keeps in its own memory, handed over by address)
  struct MessagePipe {
    u32 uid;
    std::string name;
    u32 attributes = 0;
    u32 block = 0;              //its buffer's block of the user partition (0: no buffer)
    u32 address = 0, size = 0;  //the buffer
    u32 start = 0, used = 0;    //a ring: where its oldest byte is, and how many it holds
  };
  struct Mailbox {
    u32 uid;
    std::string name;
    u32 attributes = 0;
    std::deque<u32> messages;   //the packets queued, the next to be received first
  };
  std::map<u32, MessagePipe> pipes;
  std::map<u32, Mailbox> mailboxes;
  auto pipeWaiters(const MessagePipe& pipe, Wait wait) -> std::vector<Thread*>;
  auto pipeCopy(u32 to, u32 from, u32 bytes) -> void;
  auto pipeMove(MessagePipe& pipe, u32 address, u32 bytes, bool in) -> void;
  auto pipeServe(MessagePipe& pipe) -> void;
  auto pipeFor(u32 uid, u32 address, u32 size, u32 mode, bool wait) -> MessagePipe*;
  auto pipeSend(bool wait, bool callbacks) -> void;
  auto pipeReceive(bool wait, bool callbacks) -> void;
  auto mailboxWaiters(const Mailbox& mailbox) -> std::vector<Thread*>;
  auto mailboxLink(const Mailbox& mailbox) -> void;
  auto mailboxReceive(bool callbacks) -> void;
  auto sceKernelCreateMsgPipe() -> void;
  auto sceKernelDeleteMsgPipe() -> void;
  auto sceKernelSendMsgPipe() -> void;
  auto sceKernelSendMsgPipeCB() -> void;
  auto sceKernelTrySendMsgPipe() -> void;
  auto sceKernelReceiveMsgPipe() -> void;
  auto sceKernelReceiveMsgPipeCB() -> void;
  auto sceKernelTryReceiveMsgPipe() -> void;
  auto sceKernelCancelMsgPipe() -> void;
  auto sceKernelReferMsgPipeStatus() -> void;
  auto sceKernelCreateMbx() -> void;
  auto sceKernelDeleteMbx() -> void;
  auto sceKernelSendMbx() -> void;
  auto sceKernelReceiveMbx() -> void;
  auto sceKernelReceiveMbxCB() -> void;
  auto sceKernelPollMbx() -> void;
  auto sceKernelCancelReceiveMbx() -> void;
  auto sceKernelReferMbxStatus() -> void;

  //sas.cpp: sceSasCore, the sound library's software synthesizer: VAG (ADPCM) and PCM voices at their pitches,
  //under their envelopes, mixed into the grain the game asks for (noise, waves and ATRAC3 voices stay silent, and the
  //effect adds no reverb)
  struct Sas {
    enum class Source : u32 { None, Vag, Noise, Triangle, Steep, Pcm, Atrac };  //pspautotests' sascore.h's numbers
    enum class Phase : u32 { Attack, Decay, Sustain, Release };
    struct Voice {
      Source source = Source::None;
      u32 address = 0;             //its samples (VAG's ADPCM blocks, 16-bit PCM)
      u32 size = 0;                //VAG's bytes, or PCM's samples
      s32 loop = 0;                //VAG: whether its loop marks loop (0 or 1); PCM: where it loops to (-1: never)
      u32 pitch = 0x1000;          //samples taken per sample made, in 4096ths
      s32 volumes[4] = {0x1000, 0x1000, 0x1000, 0x1000};  //left, right, and the effect's left and right
      s32 rates[4] = {};           //attack, decay, sustain, release: each phase's rate
      u32 curves[4] = {0, 1, 0, 1};  //and its curve (pspsdk's PSP_SAS_ADSR_CURVE_MODE_*)
      s32 sustainLevel = 0;
      bool on = false;             //keyed on, not keyed off (nor ended by itself)
      bool playing = false;        //not ended: its end flag clear from the next __sceSasCore on
      Phase phase = Phase::Attack;
      s32 height = 0;              //the envelope, 0 to 0x40000000
      u32 delay = 0;               //samples still to go before a voice keyed on starts
      u64 position = 0;            //where it is in its samples, in 4096ths of a sample
      u32 loopBlock = 0;           //VAG: the block its loop goes back to
      s16 decoded[2] = {};         //VAG: the samples decoded before its place and at it (the decoder's history)
      bool started = false;        //VAG: whether decoded[1] is the sample at its place yet (not once keyed on)
    } voices[32];
    bool initialized = false;
    u32 core = 0;                  //the SasCore it was initialized in
    u32 grain = 256, voiceCount = 32, outputMode = 0;
    u32 paused = 0;                //the voices paused, a bit each
    u32 endFlags = ~0u;            //the voices ended, a bit each, as the last __sceSasCore left them
    s32 effectType = -1;
    u32 effectDelay = 0, effectFeedback = 0, effectLeft = 0, effectRight = 0;
    u32 effectDry = 1, effectWet = 0;  //whether the voices are heard as they are, and through the effect
  } sas;
  std::vector<s32> sasSums;        //a grain's dry and wet sums as it's made (working room, not saved)
  std::vector<s16> sasSamples;     //and the 16-bit samples it writes
  auto sasVoice(u32 number) -> Sas::Voice*;
  auto sasEnvelope(Sas::Voice& voice) -> void;
  auto sasBlock(Sas::Voice& voice, u32 block, u8* bytes) -> bool;
  auto sasNext(Sas::Voice& voice, u8* bytes) -> bool;
  auto sasSample(const Sas::Voice& voice) -> s32;
  auto sasGrain(Sas::Voice& voice, u32 from, s32* sums) -> void;
  auto sasMix(bool mix) -> void;
  auto __sceSasInit() -> void;
  auto __sceSasCore() -> void;
  auto __sceSasCoreWithMix() -> void;
  auto __sceSasGetEndFlag() -> void;
  auto __sceSasSetVolume() -> void;
  auto __sceSasSetPitch() -> void;
  auto __sceSasSetVoice() -> void;
  auto __sceSasSetVoicePCM() -> void;
  auto __sceSasSetNoise() -> void;
  auto __sceSasSetTrianglarWave() -> void;
  auto __sceSasSetSteepWave() -> void;
  auto __sceSasSetVoiceATRAC3() -> void;
  auto __sceSasConcatenateATRAC3() -> void;
  auto __sceSasUnsetATRAC3() -> void;
  auto __sceSasSetADSR() -> void;
  auto __sceSasSetADSRmode() -> void;
  auto __sceSasSetSimpleADSR() -> void;
  auto __sceSasSetSL() -> void;
  auto __sceSasGetEnvelopeHeight() -> void;
  auto __sceSasGetAllEnvelopeHeights() -> void;
  auto __sceSasSetKeyOn() -> void;
  auto __sceSasSetKeyOff() -> void;
  auto __sceSasSetPause() -> void;
  auto __sceSasGetPauseFlag() -> void;
  auto __sceSasGetGrain() -> void;
  auto __sceSasSetGrain() -> void;
  auto __sceSasGetOutputmode() -> void;
  auto __sceSasSetOutputmode() -> void;
  auto __sceSasRevType() -> void;
  auto __sceSasRevParam() -> void;
  auto __sceSasRevEVOL() -> void;
  auto __sceSasRevVON() -> void;

  //mpeg.cpp: sceMpeg, movies fed, taken apart into their pictures' and sound's access units, and decoded
  struct MpegStream {      //what a library holds beyond its memory (the PSP's Media Engine's): by its address
    std::vector<u8> unit;  //the picture access unit sceMpegGetAvcAu took last, for the next decode
    std::vector<u8> audio; //sound from packets freed for the pictures before it was asked for
    std::vector<std::pair<u32, u64>> audioStamps;  //where PES time stamps fall in it, and the stamps
    u32 audioTaken = 0;    //sound bytes handed out from the packets still in the ring, from its first
    u64 audioTime = ~0ull; //the last sound access unit's time stamp (-1: none yet)
    u64 audioCarry = ~0ull;  //a time stamp for the next sound access unit, its PES packet having started inside
                             //the last one (-1: none)
    std::vector<u8> held, shown;   //the picture the decoder holds back, and the one it gave last (4:2:0)
    u32 heldWidth = 0, heldHeight = 0, shownWidth = 0, shownHeight = 0;
    bool keyframe = false; //a decoder made afresh (after a state was loaded): pictures wait for a key frame
    std::vector<u8> soundLast;  //the last sound frame decoded (less its header): a decoder made afresh is primed
                                //with it
    std::unique_ptr<VideoDecoder> video;   //not saved: made afresh
    std::unique_ptr<AudioDecoder> sound;   //not saved: made afresh
  };
  std::map<u32, MpegStream> mpegStreams;
  struct MpegCall {        //sceMpegRingbufferPut part way through, calling the ringbuffer's own callback
    Context caller{};      //the thread where it called Put: put back as Put returns
    u32 ringbuffer = 0;    //the ringbuffer being fed
    u32 left = 0;          //packets still to ask for
    u32 asked = 0;         //packets the callback was asked for just now
    u32 put = 0;           //packets it gave so far
  };
  std::map<u32, MpegCall> mpegCalls;  //by thread
  auto mpegNext(MpegCall& call) -> void;
  auto mpegReturned() -> void;
  auto mpegFinish(MpegCall& call) -> void;
  auto mpegAbandoned(u32 thread) -> void;
  auto mpegLibrary(u32 handle) -> u32;
  auto mpegFrameWidth(u32 handle, u32 frameWidth) -> u32;
  auto mpegOwnLibrary() -> bool;
  auto mpegDecoded(u32 handle, u32 au, u32 frame, u32 pixels, u32 frameWidth) -> void;
  auto mpegConvert(MpegStream& stream, u32 library, u32 destination, u32 frameWidth, u32 x, u32 y, u32 width,
                   u32 height) -> void;
  auto sceMpegInit() -> void;
  auto sceMpegFinish() -> void;
  auto sceMpegRingbufferQueryMemSize() -> void;
  auto sceMpegQueryMemSize() -> void;
  auto sceMpegRingbufferConstruct() -> void;
  auto sceMpegRingbufferDestruct() -> void;
  auto sceMpegRingbufferAvailableSize() -> void;
  auto sceMpegRingbufferPut() -> void;
  auto sceMpegCreate() -> void;
  auto sceMpegDelete() -> void;
  auto sceMpegQueryStreamOffset() -> void;
  auto sceMpegQueryStreamSize() -> void;
  auto sceMpegRegistStream() -> void;
  auto sceMpegUnRegistStream() -> void;
  auto sceMpegMallocAvcEsBuf() -> void;
  auto sceMpegFreeAvcEsBuf() -> void;
  auto sceMpegInitAu() -> void;
  auto sceMpegFlushAllStream() -> void;
  auto sceMpegFlushStream() -> void;
  auto sceMpegGetAvcAu() -> void;
  auto sceMpegGetAtracAu() -> void;
  auto sceMpegGetPcmAu() -> void;
  auto sceMpegAvcDecodeMode() -> void;
  auto sceMpegChangeGetAuMode() -> void;
  auto sceMpegQueryAtracEsSize() -> void;
  auto sceMpegAvcDecode() -> void;
  auto sceMpegAvcDecodeStop() -> void;
  auto sceMpegAvcDecodeFlush() -> void;
  auto sceMpegAvcQueryYCbCrSize() -> void;
  auto sceMpegAvcInitYCbCr() -> void;
  auto sceMpegAvcDecodeYCbCr() -> void;
  auto sceMpegAvcDecodeStopYCbCr() -> void;
  auto sceMpegAvcDecodeDetail() -> void;
  auto sceMpegAvcCsc() -> void;
  auto sceMpegAtracDecode() -> void;
  auto pictureConvert(const std::vector<u8>& planes, u32 pictureWidth, u32 pictureHeight, u32 format, bool opaque,
                      u32 destination, u32 frameWidth, u32 x, u32 y, u32 width, u32 height) -> void;
  auto atracUnitDecode(std::unique_ptr<AudioDecoder>& decoder, std::vector<u8>& last, const std::vector<u8>& unit,
                       std::vector<s16>& out) -> void;

  //psmf.cpp: scePsmf, a PSMF movie's header read for the game, which keeps the header and a structure of its own in
  //its memory; and the reading of headers the player shares
  enum : u32 { PsmfAvc = 0, PsmfAtrac = 1, PsmfPcm = 2, PsmfUserData = 3, PsmfAnyAudio = 15 };  //streams' kinds
  struct PsmfStream {          //an entry of a header's stream table
    u8 id = 0, privateID = 0;  //its PES stream ID (0xe0 + channel: video; 0xbd: private stream 1) and private ID
    u32 kind = 0, channel = 0;
    u32 epOffset = 0, epCount = 0;  //its EP map: where (from the PSMF's start) and how many entries (0: none)
    u32 width = 0, height = 0;      //a video stream's picture, in pixels
    u32 channels = 0, rate = 0;     //a sound stream's channel configuration (1 mono, 2 stereo) and rate code
                                    //(2: 44.1 kHz)
  };
  struct PsmfHeader {
    u32 version = 0;                //its four ASCII digits, as a little-endian word
    u32 streamOffset = 0, streamSize = 0;
    u64 startTime = 0, endTime = 0; //the presentation's, in the 90 kHz clock's ticks
    std::vector<PsmfStream> streams;
  };
  struct PsmfEntry { u32 time, pack; u8 first, second; };  //an EP map's: a picture's time, the pack it starts in
  static auto psmfParse(const u8* bytes, u32 size, PsmfHeader& header) -> bool;
  static auto psmfEntry(const u8* bytes) -> PsmfEntry;
  auto psmfRead(u32 address, PsmfHeader& header) -> bool;
  auto psmfStructure(u32 structure, PsmfHeader& header, u32 notSet) -> u32;
  auto psmfSelect(u32 structure, const PsmfHeader& header, u32 number) -> void;
  auto psmfMap(u32 structure, const PsmfHeader& header, u32& at) -> u32;
  auto psmfCurrent(u32 structure, PsmfHeader& header, u32 kind, PsmfStream& stream) -> u32;
  auto psmfWriteEntry(u32 at, u32 address) -> void;
  auto psmfFindEntry(u32 structure, u32 time, u32& at, u32& index) -> u32;
  auto scePsmfSetPsmf() -> void;
  auto scePsmfVerifyPsmf() -> void;
  auto scePsmfQueryStreamOffset() -> void;
  auto scePsmfQueryStreamSize() -> void;
  auto scePsmfGetPsmfVersion() -> void;
  auto scePsmfGetHeaderSize() -> void;
  auto scePsmfGetStreamSize() -> void;
  auto scePsmfGetPresentationStartTime() -> void;
  auto scePsmfGetPresentationEndTime() -> void;
  auto scePsmfGetNumberOfStreams() -> void;
  auto scePsmfGetNumberOfSpecificStreams() -> void;
  auto scePsmfSpecifyStream() -> void;
  auto scePsmfSpecifyStreamWithStreamType() -> void;
  auto scePsmfSpecifyStreamWithStreamTypeNumber() -> void;
  auto scePsmfGetCurrentStreamNumber() -> void;
  auto scePsmfGetCurrentStreamType() -> void;
  auto scePsmfGetVideoInfo() -> void;
  auto scePsmfGetAudioInfo() -> void;
  auto scePsmfGetNumberOfEPentries() -> void;
  auto scePsmfCheckEPmap() -> void;
  auto scePsmfGetEPWithId() -> void;
  auto scePsmfGetEPWithTimestamp() -> void;
  auto scePsmfGetEPidWithTimestamp() -> void;
  auto scePsmfGetNumberOfPsmfMarks() -> void;
  auto scePsmfGetPsmfMark() -> void;

  //psmfplayer.cpp: scePsmfPlayer, a movie played from its file: read, taken apart and decoded ahead, and handed to
  //the game a picture and a sound frame at a time, paced by its calls (one player at a time)
  struct PsmfPicture {
    std::vector<u8> planes;   //4:2:0, packed one after another (empty: none)
    u32 width = 0, height = 0;
    u64 time = 0;             //its presentation time, in absolute ticks
  };
  struct PsmfPlayer {
    u32 status = 0;           //0: no player; else 1 made, 2 a movie set, 4 playing, 0x200 played to its end
    u32 priority = 0;         //what scePsmfPlayerCreate gave the player's threads
    u32 tempBuffer = 0, tempSize = 0;  //scePsmfPlayerSetTempBuf's (the file is read without them)
    bool looping = false;
    u32 pixelFormat = 3;      //of the pictures handed out: 0 5650, 1 5551, 2 4444, 3 8888
    s32 mode = 0, speed = 1;  //the play mode (0 play, 1 slow motion, 2 step frame, 3 pause, 4 fast forward, 5 fast
                              //rewind) and speed
    s32 videoCodec = -1, videoStream = -1, audioCodec = -1, audioStream = -1;  //as scePsmfPlayerStart chose them
    //the movie
    u32 file = 0;             //the descriptor it's read through (files: the game's, as a PSP's library's is)
    u64 offset = 0;           //where the PSMF begins in its file
    std::vector<u8> header;   //its header, to where the program stream begins
    //playing it
    u8 videoID = 0xe0;        //the PES stream ID of the video stream played
    s32 audioID = -1;         //the private ID of the ATRAC3plus stream played (-1: no sound)
    u32 nextPack = 0;         //the program stream's next pack to read
    std::vector<u8> video, audio;  //the streams' data read and not yet taken (sound less its packets' headers)
    std::vector<std::pair<u32, u64>> videoStamps, audioStamps;  //where PES time stamps fall in them, and the stamps
    u64 videoTime = ~0ull, audioTime = ~0ull;  //the last access unit's and sound frame's times (-1: none yet)
    std::deque<PsmfPicture> pictures;  //decoded, waiting their turn (three at most, as a PSP's decodes ahead)
    PsmfPicture shown;        //the picture handed out last
    u32 calls = 0;            //scePsmfPlayerGetVideoData's calls since playing (re)started
    bool started = false;     //its start-up is over: pictures come
    u32 due = 0;              //Updates until the next picture is due (0: due; ~0: none, paused)
    u64 from = 0;             //where playing started (absolute ticks): pictures before it decoded, not handed out
    bool silence = false;     //the next sound frame decoded comes out silent (the first after a start)
    bool keyframe = false;    //pictures wait for one a decoder can start from (a decoder made afresh)
    bool ending = false;      //the movie has ended, for the player's threads to notice once they get the CPU
    u32 endingAt = 0;         //the vertical blank's count when it ended
    u32 entry = 0;            //fast forward and rewind: the EP entry shown last
    std::vector<u8> soundLast;  //the last sound frame decoded (less its header): primes a decoder made afresh
    std::unique_ptr<VideoDecoder> decoder;   //not saved: made afresh
    std::unique_ptr<AudioDecoder> sound;     //not saved: made afresh
    std::vector<u8> chunk;    //the file read ahead (not saved: read again)
    u32 chunkPack = 0;        //the pack it starts with
  } psmfPlayer;
  auto psmfPlayerFor(u32 handle) -> PsmfPlayer*;
  auto psmfPlayerWait(u32 value, u64 length, bool callbacks = false) -> void;
  auto psmfPlayerHeader(PsmfHeader& header) const -> bool;
  auto psmfPlayerOpen(const std::string& path, u64 offset) -> u64;
  auto psmfPlayerClose() -> void;
  auto psmfPlayerPack(u32 pack, u8* data) -> bool;
  auto psmfPlayerRead() -> bool;
  auto psmfPlayerDone() const -> bool;
  auto psmfPlayerUnit(std::vector<u8>& unit, u64& time) -> bool;
  auto psmfPlayerFrame(u32& from, u32& bytes, u64& time) -> bool;
  auto psmfPlayerDecode() -> bool;
  auto psmfPlayerReady() -> bool;
  auto psmfPlayerRestart(u64 time) -> void;
  auto psmfPlayerAdvance() -> void;
  auto psmfPlayerEnd() -> void;
  auto psmfPlayerEnded() -> void;
  auto psmfPlayerStep() -> void;
  auto psmfPlayerStreams(u32 kind) -> u32;
  auto psmfPlayerSelect(u32 kind, u32 number) -> void;
  auto psmfPlayerSetPsmf(bool offset, bool callbacks) -> void;
  auto psmfPlayerSelectNext(u32 kind) -> void;
  auto psmfPlayerSelectSpecific(u32 kind) -> void;
  auto scePsmfPlayerCreate() -> void;
  auto scePsmfPlayerDelete() -> void;
  auto scePsmfPlayerSetTempBuf() -> void;
  auto scePsmfPlayerSetPsmf() -> void;
  auto scePsmfPlayerSetPsmfCB() -> void;
  auto scePsmfPlayerSetPsmfOffset() -> void;
  auto scePsmfPlayerSetPsmfOffsetCB() -> void;
  auto scePsmfPlayerReleasePsmf() -> void;
  auto scePsmfPlayerGetPsmfInfo() -> void;
  auto scePsmfPlayerConfigPlayer() -> void;
  auto scePsmfPlayerStart() -> void;
  auto scePsmfPlayerStop() -> void;
  auto scePsmfPlayerUpdate() -> void;
  auto scePsmfPlayerGetVideoData() -> void;
  auto scePsmfPlayerGetAudioData() -> void;
  auto scePsmfPlayerGetAudioOutSize() -> void;
  auto scePsmfPlayerGetCurrentStatus() -> void;
  auto scePsmfPlayerGetCurrentPts() -> void;
  auto scePsmfPlayerGetCurrentPlayMode() -> void;
  auto scePsmfPlayerChangePlayMode() -> void;
  auto scePsmfPlayerGetCurrentVideoStream() -> void;
  auto scePsmfPlayerGetCurrentAudioStream() -> void;
  auto scePsmfPlayerSelectVideo() -> void;
  auto scePsmfPlayerSelectAudio() -> void;
  auto scePsmfPlayerSelectSpecificVideo() -> void;
  auto scePsmfPlayerSelectSpecificAudio() -> void;
  auto scePsmfPlayerBreak() -> void;
  auto scePsmfPlayerUnknown() -> void;

  //codec.cpp: the decoders the system makes (FFmpeg's, where the build has them; null otherwise). Tests may put
  //their own in.
  std::function<auto (const AudioDecoder::Format& format) -> std::unique_ptr<AudioDecoder>> audioDecoders;
  std::function<auto () -> std::unique_ptr<VideoDecoder>> videoDecoders;
  auto codecWait(u32 microseconds) -> void;

  //atrac.cpp: sceAtrac3plus, the library games play their music and long sounds with: ATRAC3 and ATRAC3plus in RIFF
  //WAVE files, the whole file in one buffer or streamed through a smaller one, decoded a frame a call
  struct Atrac {
    u32 codec = 0;              //what the ID was handed out for: 0x1000 ATRAC3plus, 0x1001 ATRAC3; 0 not handed out
    u32 state = 0;              //its data, in the PSP's numbers: 0 none (the PSP's 1), 2 the whole file in its
                                //buffer, 3 a buffer that will hold the whole file, filled as the game goes, 4-6 the
                                //file streamed through a smaller buffer: 4 with no loop, 5 looping at its last frame,
                                //6 looping before it, a second buffer holding what follows the loop
    //the file, from its header
    u32 channels = 0;           //the header's
    u32 outputChannels = 2;     //what decoding writes a sample: 2, or 1 for the mono output (SetMOut...)
    u32 frameBytes = 0, frameSamples = 0, delay = 0;
    u32 dataOff = 0, fileDataEnd = 0;  //where the frames start in the file, and where they end
    u32 firstValidSample = 0, endSample = 0;  //the first and last samples worth hearing, on the decoder's count
    bool looped = false;
    u32 loopStart = 0, loopEnd = 0;    //on the decoder's count, the end the loop's last sample
    bool monoFrames = false;    //ATRAC3 frames decoded as mono whatever the header's channels (atrac.cpp)
    std::vector<u8> extra;      //the codec's parameters from the fmt chunk
    //the buffers
    u32 buffer = 0, bufferByte = 0, secondBuffer = 0, secondBufferByte = 0;
    u32 loaded = 0;             //a halfway buffer's bytes of the file so far
    //where decoding is
    u32 decodePos = 0;          //the next sample, on the decoder's count
    u32 curFileOff = 0;         //the next frame's place in the file
    u32 streamOff = 0;          //and in its buffer
    u32 streamDataByte = 0;     //the bytes of the stream in the buffer from there on
    u32 framesToSkip = 0;       //frames to decode unheard before the next one's samples
    u32 curBuffer = 0;          //1: decoding has gone on into the second buffer (2: and ended there)
    u32 secondStreamOff = 0;    //where it is there
    s32 loopNum = 0;            //loops still to play: -1 for ever
    bool ended = false;         //every sample decoded
    u32 error = 0;              //the decoder's last error (sceAtracGetInternalErrorInfo)
    //a streamed file's buffer: a ring of frames, the first time round from where the header put them, after that
    //from its start (atrac.cpp)
    u32 firstEnd = 0, lapEnd = 0;      //where the ring ends the first time round, and every time after
    u32 readLap = 0;            //the times round decoding has wrapped
    u32 writeLap = 0, writeOff = 0;    //where the game's next bytes go, the times round that has wrapped
    u32 writeFileOff = 0;       //the place in the file those bytes come from
    u32 loopsAhead = 0;         //jumps back to the loop's start the game has streamed and decoding hasn't reached
    std::vector<u8> recent;     //the last frame decoded: the frame a decoder made afresh is primed with
    std::unique_ptr<AudioDecoder> decoder;  //not saved: made afresh from the stream after a state is loaded
  };
  Atrac atracs[6];
  u32 atracPlusIDs = 2, atracClassicIDs = 2;  //how sceAtracReinit shares the six IDs between the codecs
  u32 atracContexts = 0;        //the memory _sceAtracGetContextAddress hands out (made the first time it's asked)
  auto atracFind(u32 id, bool needData = true) -> Atrac*;
  auto atracParse(Atrac& atrac, u32 buffer, u32 size) -> u32;
  auto atracSet(u32 id, u32 buffer, u32 readSize, u32 bufferSize, bool mono) -> u32;
  auto atracFrame(Atrac& atrac, u32& address) -> u32;
  auto atracConsume(Atrac& atrac) -> void;
  auto atracDecodeFrame(Atrac& atrac, s16* samples) -> s32;
  auto atracPrime(Atrac& atrac) -> bool;
  auto atracRemain(Atrac& atrac) -> s32;
  auto atracWritable(Atrac& atrac) -> u32;
  auto atracWriterAt(Atrac& atrac) -> void;
  auto atracNextSamples(Atrac& atrac) -> u32;
  auto atracContext(u32 id) -> void;
  auto sceAtracGetAtracID() -> void;
  auto sceAtracReleaseAtracID() -> void;
  auto sceAtracReinit() -> void;
  auto sceAtracSetData() -> void;
  auto sceAtracSetDataAndGetID() -> void;
  auto sceAtracSetHalfwayBuffer() -> void;
  auto sceAtracSetHalfwayBufferAndGetID() -> void;
  auto sceAtracSetMOutData() -> void;
  auto sceAtracSetMOutDataAndGetID() -> void;
  auto sceAtracSetMOutHalfwayBuffer() -> void;
  auto sceAtracSetMOutHalfwayBufferAndGetID() -> void;
  auto sceAtracDecodeData() -> void;
  auto sceAtracGetRemainFrame() -> void;
  auto sceAtracGetStreamDataInfo() -> void;
  auto sceAtracAddStreamData() -> void;
  auto sceAtracGetNextDecodePosition() -> void;
  auto sceAtracGetNextSample() -> void;
  auto sceAtracGetMaxSample() -> void;
  auto sceAtracGetSoundSample() -> void;
  auto sceAtracGetChannel() -> void;
  auto sceAtracGetOutputChannel() -> void;
  auto sceAtracGetBitrate() -> void;
  auto sceAtracSetLoopNum() -> void;
  auto sceAtracGetLoopStatus() -> void;
  auto sceAtracGetInternalErrorInfo() -> void;
  auto sceAtracGetBufferInfoForResetting() -> void;
  auto sceAtracResetPlayPosition() -> void;
  auto sceAtracIsSecondBufferNeeded() -> void;
  auto sceAtracGetSecondBufferInfo() -> void;
  auto sceAtracSetSecondBuffer() -> void;
  auto _sceAtracGetContextAddress() -> void;

  //mp3.cpp: sceMp3, MP3 streams fed through a buffer and decoded a frame a call
  struct Mp3 {
    bool reserved = false, initialized = false;
    bool loopSet = false;       //sceMp3SetLoopNum came before sceMp3Init, which then keeps its count
    u64 start = 0, end = 0;     //where the stream lies in its file
    u32 buffer = 0, bufferSize = 0, pcm = 0, pcmSize = 0;
    u64 filePos = 0;            //where in the file the game's next bytes come from
    u64 readFilePos = 0;        //and where the next frame decoded lies
    u32 readPos = 0, writePos = 0, writeLimit = 0;  //in the buffer's ring: the next frame, the next bytes, and the
                                                    //end of the half being filled
    u32 available = 0;          //bytes in the ring not decoded yet
    u32 pcmHalf = 0;            //the sample buffer's half the next frame goes to
    s32 loopNum = -1;
    u32 sumDecoded = 0, version = 0, rate = 0, channels = 0, bitrate = 0, frames = 0;
    std::unique_ptr<AudioDecoder> decoder;  //not saved: made afresh after a state is loaded
  };
  Mp3 mp3s[2];
  bool mp3Terminated = false;   //sceMp3TermResource: no handles until sceMp3InitResource
  auto mp3Find(u32 handle, u32 notReserved = 0x8067'1102) -> Mp3*;
  auto mp3Writable(Mp3& mp3) -> u32;
  auto mp3Frame(Mp3& mp3, std::vector<u8>& frame) -> bool;
  auto sceMp3InitResource() -> void;
  auto sceMp3TermResource() -> void;
  auto sceMp3ReserveMp3Handle() -> void;
  auto sceMp3ReleaseMp3Handle() -> void;
  auto sceMp3GetInfoToAddStreamData() -> void;
  auto sceMp3NotifyAddStreamData() -> void;
  auto sceMp3CheckStreamDataNeeded() -> void;
  auto sceMp3Init() -> void;
  auto sceMp3Decode() -> void;
  auto sceMp3SetLoopNum() -> void;
  auto sceMp3GetLoopNum() -> void;
  auto sceMp3GetSumDecodedSample() -> void;
  auto sceMp3GetMaxOutputSample() -> void;
  auto sceMp3GetSamplingRate() -> void;
  auto sceMp3GetBitRate() -> void;
  auto sceMp3GetMp3ChannelNum() -> void;
  auto sceMp3GetFrameNum() -> void;
  auto sceMp3GetMPEGVersion() -> void;
  auto sceMp3ResetPlayPosition() -> void;
  auto sceMp3ResetPlayPositionByFrame() -> void;

  //font.cpp (and pgf.cpp): sceLibFont, the system's font library, drawing the PSP's own fonts (the owner's, from
  //their PSP's flash0: a host folder the system gives, fontsFrom()) and the PGFs games carry
  struct SystemFont {
    std::string file;           //"jpn0.pgf"
    bool present = false;       //in the folder (and with no pgf, damaged)
    std::shared_ptr<PGF> pgf;   //null: not there, or damaged
    u64 hash = 0;               //of its bytes: a state's system fonts must be these
  };
  std::vector<SystemFont> systemFonts;  //the PSP's eighteen, in its order: the system's to give, as devices are
  struct FontLibrary {          //one sceFontNewLib made
    u32 address = 0;            //its 76 bytes in the program's memory: the handle the program holds
    u32 slots = 0;              //how many fonts it may have open at once (numFonts, 9 at most)
    u32 handles = 0, data = 0, list = 0;  //its other memory: the handles (76 bytes each), their data (560 each) and
                                          //the list of the system's fonts (18 styles)
    u32 fonts[9] = {};          //each handle's font: the one it last opened (an OpenFont's ID; 0: none yet)
    bool open[9] = {};          //whether the handle is open (or being opened)
  };
  struct OpenFont {             //a font's data, loaded once however many of its library's handles open it
    u32 id = 0, library = 0;    //its ID, and its library's address
    u32 source = 0;             //0 a system font, 1 a file of the program's, 2 the program's memory
    u32 index = 0;              //a system font's place in the list
    std::string path;           //a file's PSP path
    u32 address = 0, length = 0;  //the program's memory holding it
    u32 mode = 0;               //1: read whole into the program's memory (else a piece at a time)
    u32 references = 0;         //handles holding it open
    std::vector<u32> blocks;    //the memory it took from the program, given back as its last handle closes
    u64 hash = 0;               //its bytes' (a system font's or a file's): a state's must be the same
    std::shared_ptr<PGF> pgf;   //its glyphs (not saved: read again when a state is loaded)
  };
  struct FontCall {             //a library function part way through, calling the program's own alloc or free
    enum Kind : u32 { NewLib = 1, Open, Give };  //Give: only gives memory back (a close, the library done with)
    u32 kind = 0;
    Context caller{};           //the thread where it called the library: put back as the function returns
    u32 library = 0;            //the library (sceFontNewLib's: its parameters)
    u32 errorAt = 0;            //where its error goes (0: nowhere)
    u32 slot = 0, slots = 0;    //an open's handle; a new library's number of handles
    u32 userData = 0, alloc = 0, free = 0;  //the program's functions it calls, and what each is given first
    std::vector<u32> asks;      //sizes to ask alloc for, in order
    std::vector<u32> got;       //what alloc gave so far
    std::vector<u32> frees;     //blocks to give back to free, in order
    bool ended = false;         //what it makes is made (fontEnd()): what's left is giving back, then returning
    u32 result = 0, error = 0;  //what it returns, and its error
    OpenFont opening;           //an open's font, made once its memory has all come
  };
  std::map<u32, FontLibrary> fontLibraries;  //by address
  std::map<u32, OpenFont> openFonts;         //by ID
  std::map<u32, FontCall> fontCalls;         //by thread
  u32 nextFontID = 1;
  float fontResolution[2] = {128.0f, 128.0f};  //the stand-in's dots an inch, across and down (no fonts installed)
  auto fontsFrom(const std::string& folder) -> void;
  auto fontsInstalled() const -> bool;
  auto fontLibraryAt(u32 address) -> FontLibrary*;
  auto fontHandle(u32 handle, FontLibrary*& library, u32& slot) -> bool;
  auto fontFor(u32 handle) -> OpenFont*;
  auto fontCount(u32 library) -> u32;
  auto fontStyle(u32 address, const PGF& pgf, s32 system) -> void;
  auto systemFontStyle(u32 address, u32 index) -> void;
  auto fontGlyph(const OpenFont& font, u32 code, PGF::Glyph& glyph, bool shadow) -> void;
  auto fontDraw(const PGF::Glyph& glyph, u32 image, s64 clipLeft, s64 clipTop, u64 clipWidth, u64 clipHeight)
    -> void;
  auto fontNext(FontCall& call) -> void;
  auto fontReturned() -> void;
  auto fontRelease(FontCall& call) -> void;
  auto fontAbandoned(u32 thread) -> void;
  auto fontFinish() -> void;
  auto fontStart(FontCall call) -> bool;
  auto fontEnd(FontCall& call) -> void;
  auto fontGiveBack(const OpenFont& font) -> std::vector<u32>;
  auto fontOpen(FontLibrary& library, OpenFont font, u32 errorAt) -> void;
  auto fontCallable(u32 function) -> bool;
  auto fontMemory(u32 address, u32 length) -> std::vector<u8>;
  auto fontReload(OpenFont& font) -> bool;
  auto fontFits(u32 library, u32 style, std::vector<u32>& matches, std::vector<float>& distances, bool& sized)
    -> bool;
  auto fontCharInfo(const PGF::Glyph& glyph, u32 info) -> void;
  auto fontCharacter(bool shadow, u32 what) -> void;
  auto fontScale(bool toPixels, u32 axis) -> void;
  auto standInNewLib() -> void;
  auto standInCount(u32 errorAt) -> void;
  auto standInFind(u32 errorAt) -> void;
  auto standInOpen(u32 errorAt) -> void;
  auto standInResolution() -> void;
  auto sceFontNewLib() -> void;
  auto sceFontDoneLib() -> void;
  auto sceFontClose() -> void;
  auto sceFontOpen() -> void;
  auto sceFontOpenUserMemory() -> void;
  auto sceFontOpenUserFile() -> void;
  auto sceFontGetNumFontList() -> void;
  auto sceFontGetFontList() -> void;
  auto sceFontGetFontInfoByIndexNumber() -> void;
  auto sceFontFindOptimumFont() -> void;
  auto sceFontFindFont() -> void;
  auto sceFontGetFontInfo() -> void;
  auto sceFontGetCharInfo() -> void;
  auto sceFontGetCharImageRect() -> void;
  auto sceFontGetCharGlyphImage() -> void;
  auto sceFontGetCharGlyphImage_Clip() -> void;
  auto sceFontGetShadowInfo() -> void;
  auto sceFontGetShadowImageRect() -> void;
  auto sceFontGetShadowGlyphImage() -> void;
  auto sceFontGetShadowGlyphImage_Clip() -> void;
  auto sceFontFlush() -> void;
  auto sceFontSetAltCharacterCode() -> void;
  auto sceFontSetResolution() -> void;
  auto sceFontPointToPixelH() -> void;
  auto sceFontPointToPixelV() -> void;
  auto sceFontPixelToPointH() -> void;
  auto sceFontPixelToPointV() -> void;

  //net.cpp: the network libraries, with the wireless LAN switched off
  auto sceNetDone() -> void;
  auto sceNetUnavailable() -> void;
  auto sceNetGetLocalEtherAddr() -> void;
  auto sceNetEtherNtostr() -> void;
  auto sceNetEtherStrton() -> void;
  auto sceNetAdhocctlGetState() -> void;
  auto sceNetEmptyList() -> void;

  //utility.cpp: the system's dialogs, one at a time, and the optional modules loaded
  struct Dialog {
    u32 kind = 0;          //the last started (0: none yet)
    u32 status = 0;        //0 none, 1 starting, 2 running, 3 finished, 4 closing
    u32 next = 0;          //the status it goes to at changeAt (0: no change coming)
    u64 changeAt = 0;
    u32 parameters = 0;    //where its parameters are
    u32 abortUpdates = 0;  //an aborted message's Updates still to come before it finishes (0: not aborted)
    u32 runningUpdates = 0;  //Updates while Running, before an abort (utility/dialog/abort's fade length)
  } dialog;
  u32 stateLayout = 17;  //save-state layout while loading (System::header); writes always use the current one
  std::vector<u32> utilityModules;  //the optional modules loaded (psputility_modules.h's numbers)
  auto dialogDue() -> void;
  auto dialogStart(u32 kind) -> void;
  auto dialogStatus(u32 kind) -> void;
  auto dialogUpdate(u32 kind) -> void;
  auto dialogShutdown(u32 kind) -> void;
  auto keyboard(u32 parameters) -> u32;
  static auto hexWord(u32 value) -> std::string;
  auto savePath(const std::string& folder, const std::string& file = {}) -> std::string;
  auto savedata(u32 parameters) -> u32;
  auto savedataClusters(const std::string& folder) -> u64;
  auto savedataNeeded(u32 parameters, u32 dataSize) -> u64;
  auto savedataList(u32 parameters, const std::string& game) -> u32;
  auto sceUtilitySavedataInitStart() -> void;
  auto sceUtilitySavedataGetStatus() -> void;
  auto sceUtilitySavedataUpdate() -> void;
  auto sceUtilitySavedataShutdownStart() -> void;
  auto sceUtilityMsgDialogInitStart() -> void;
  auto sceUtilityMsgDialogGetStatus() -> void;
  auto sceUtilityMsgDialogUpdate() -> void;
  auto sceUtilityMsgDialogShutdownStart() -> void;
  auto sceUtilityMsgDialogAbort() -> void;
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
  auto sceUtilityGamedataInstallInitStart() -> void;
  auto sceUtilityGamedataInstallGetStatus() -> void;
  auto sceUtilityGamedataInstallUpdate() -> void;
  auto sceUtilityGamedataInstallShutdownStart() -> void;
  auto sceUtilityGamedataInstallAbort() -> void;
  auto sceUtilityLoadModule() -> void;
  auto sceUtilityUnloadModule() -> void;
  auto sceUtilityLoadNetModule() -> void;
  auto sceUtilityUnloadNetModule() -> void;
  auto sceUtilityLoadAvModule() -> void;
  auto sceUtilityUnloadAvModule() -> void;
  auto sceUtilityLoadUsbModule() -> void;
  auto sceUtilityUnloadUsbModule() -> void;
  auto sceUtilityGetSystemParamString() -> void;
  auto sceUtilitySetSystemParamString() -> void;
  auto sceUtilitySetSystemParamInt() -> void;

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
  auto sceKernelVolatileMemLock() -> void;
  auto sceKernelVolatileMemTryLock() -> void;
  auto sceKernelVolatileMemUnlock() -> void;

  //system.cpp: leaving, clocks, and the odds and ends a C library's start-up asks for
  u64 startTime = 0;  //the date when the PSP started, in microseconds since 1970
  auto result64(u64 value) -> void;
  auto sceKernelLibcClock() -> void;
  auto sceKernelSysClock2USec() -> void;
  auto sceKernelSysClock2USecWide() -> void;
  auto sceKernelUSec2SysClock() -> void;
  auto sceKernelUSec2SysClockWide() -> void;
  auto sceRtcGetTick() -> void;
  auto sceRtcCompareTick() -> void;
  auto sceKernelUtilsMt19937Init() -> void;
  auto sceKernelUtilsMt19937UInt() -> void;
  auto sceKernelPrintf() -> void;
  auto sceKernelSetGPO() -> void;
  auto sceKernelGetGPI() -> void;
  auto sceWlanGetSwitchState() -> void;
  auto sceWlanGetEtherAddr() -> void;
  auto sceUsbStart() -> void;
  auto sceUsbStop() -> void;
  auto sceUsbActivate() -> void;
  auto sceUsbDeactivate() -> void;
  u32 imposeLanguage = 1, imposeButton = 1;  //sceImposeSetLanguageMode's: English, the cross button confirming
  auto sceImposeSetLanguageMode() -> void;
  auto sceImposeGetLanguageMode() -> void;
  auto sceImposeGetBatteryIconStatus() -> void;
  auto sceImposeSetUMDPopup() -> void;
  auto sceDmacMemcpy() -> void;
  auto sceKernelExitGame() -> void;
  auto sceKernelSelfStopUnloadModule() -> void;
  auto sceKernelStopUnloadSelfModuleWithStatus() -> void;
  auto sceKernelStopUnloadSelfModule() -> void;
  auto sceUtilityGetSystemParamInt() -> void;
  auto sceNetInetUnavailable() -> void;
  auto sceKernelGetSystemTimeWide() -> void;
  auto sceKernelGetSystemTime() -> void;
  auto sceKernelLibcGettimeofday() -> void;
  auto sceKernelLibcTime() -> void;
  auto sceRtcGetCurrentTick() -> void;
  auto sceRtcGetTickResolution() -> void;
  auto sceRtcGetAccumulativeTime() -> void;
  auto sceKernelCacheUnneeded() -> void;
  auto writeDate(u32 address, u64 microseconds, bool local) -> bool;
  struct Exec {  //sceKernelLoadExec's program, put in as the kernel's loop next goes round (never kept in states)
    bool pending = false;
    std::string path;
    std::vector<u8> program;   //decrypted, or a PBP holding it
    std::vector<u8> argument;  //its first thread's (none, if empty)
    bool pathArgument = false; //its path as that argument instead, as with no parameters
  } exec;
  auto sceKernelLoadExec() -> void;
  auto loadExec() -> void;
  auto sceRtcGetCurrentClock() -> void;
  auto sceRtcGetCurrentClockLocalTime() -> void;
  auto dateSeconds(u32 date) -> s64;
  auto sceRtcGetTime_t() -> void;
  auto sceRtcGetTime64_t() -> void;
  auto sceRtcGetDayOfWeek() -> void;
  auto sceRtcGetLastAdjustedTime() -> void;
  auto sceRtcGetLastReincarnatedTime() -> void;
  auto sceRtcGetDosTime() -> void;
  auto sceRtcSetDosTime() -> void;
  auto sceRtcGetWin32FileTime() -> void;
  auto sceRtcSetTick() -> void;
  auto sceOpenPSIDGetOpenPSID() -> void;
  auto sceKernelIsCpuIntrEnable() -> void;
  auto sceKernelMemset() -> void;
  auto sceKernelMemcpy() -> void;

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
  u32 programUID = 0;      //the program's own module ID (0 once it has unloaded itself: programAsModule())
  auto readWhole(const std::string& path, std::vector<u8>& data) -> u32;
  auto readOpenFile(OpenFile& open, u64 size, std::vector<u8>& data) -> u32;
  auto loadModule(const std::vector<u8>& file, const std::string& path) -> u32;
  auto standIn(const std::string& name, u32 attributes, const std::string& path) -> u32;
  auto unloadModule(u32 uid) -> void;
  auto programAsModule() -> void;
  auto unloadSelf(s32 exitStatus, u32 length, u32 argument, u32 options) -> void;
  auto linkImports() -> void;
  auto moduleAt(u32 address) const -> u32;
  auto moduleFunction(const LoadedModule& loaded, u32 nid) const -> u32;
  auto threadParameters(const Module& module, u32 parameters, u32& priority, u32& stackSize, u32& attributes) const
    -> void;
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
