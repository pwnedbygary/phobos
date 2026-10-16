//psp-ge-profile's plugin: what its parts share. main.c starts it, reads the settings, waits for a trigger and writes
//the files; hooks.c holds the hooks, the interrupt monitor and the record buffer; notice.c draws the on-screen
//notices; trace.c keeps the trace; text.c formats text.
//
//The firmware's and ARK's functions are found at run time by sctrlHENFindFunction (the one static import from ARK:
//systemctrl.S), so a NID this firmware doesn't have leaves that one feature off, named in the dry run's report,
//instead of stopping the plugin from loading. The prototypes are written here from pspsdk's headers, uOFW and
//ARK-4's documented API; the NIDs are firmware 6.60/6.61's (uOFW's export lists and pspsdk's *_660 libraries
//agree on each kernel one; the user ones haven't changed since the first firmware).

#pragma once
#include <pspkernel.h>
#include <stdarg.h>
#include "profile.h"

//An address the firmware gives: 32 bits on the PSP, a pointer's size in hosttest.c's build on the Mac (HOST), which
//compiles main.c without the PSP's module info. A function the lookup gives is taken only if it's plausibly one:
//firmware functions live in kernel memory, from 0x88000000, while a PSP error code (0x8002xxxx) or 0 doesn't, so a
//lookup that fails either way leaves its feature off instead of being called.
#ifdef HOST
#include <stdint.h>
typedef uintptr_t Address;
#undef PSP_MODULE_INFO
#define PSP_MODULE_INFO(name, attributes, major, minor)
int hostPlausible(Address address);
#define PlausibleFunction(address) hostPlausible(address)
#else
typedef u32 Address;
#define PlausibleFunction(address) ((address) >= 0x88000000u && (address) < 0x90000000u && !((address) & 3))
#endif

//ARK's SystemControl (SystemCtrlForKernel). Its NIDs are the first four bytes of each name's SHA-1, as PRO's were;
//sctrlHENPatchSyscall has two older ones too, tried in turn.
Address sctrlHENFindFunction(const char* module, const char* library, u32 nid);
#define ArkModule "SystemControl"
#define ArkLibrary "SystemCtrlForKernel"
enum {
  NidPatchSyscall = 0xF988C1DC, NidPatchSyscallOld = 0x826668E9, NidPatchSyscallOlder = 0x02BFCB5F,
  NidQuerySystemCall = 0x56CEAF00, NidGetInitParam = 0xFCE44FB8, NidMsIsEf = 0xAD9849FE, NidFlushCache = 0x17691875,
  NidHenVersion = 0x1090A2E1, NidHenMinorVersion = 0x5328B431, NidFindFunctionOnSystem = 0xBC66A655,
};

//Kernel memory (sceSystemMemoryManager's SysMemForKernel) and the firmware's version (SysMemUserForUser).
enum {
  NidAllocPartitionMemory = 0x7158CE7E, NidFreePartitionMemory = 0xC1A26C6F, NidGetBlockHeadAddr = 0xF12A62F7,
  NidPartitionMaxFreeMemSize = 0x13F4A0DE, NidPartitionTotalFreeMemSize = 0x0115B0F8,
  NidQueryMemoryPartitionInfo = 0xC4EEAF20, NidGetModel = 0x07C586A1, NidDevkitVersion = 0x3FC9AE6A,
};

//The interrupt manager's sub-interrupt monitor: called before and after every handler sceKernelCallSubIntrHandler
//runs (the GE's SIGNAL and FINISH callbacks, the vblank's handlers), with interrupts off.
enum { NidRegisterSubIntrMonitor = 0xDBD52A5D, NidReleaseSubIntrMonitor = 0x19596CD3 };
enum { GeInterrupt = 25, VblankInterrupt = 30 };
typedef s32 (*MonitorFunction)(s32 interrupt, s32 subInterrupt, s32 arg2, s32 handlerArg, s32 arg3, s32 handlerOrResult,
                               s8 option);

//The controller (sceController_Service's sceCtrl), the clocks (scePower_Service's scePower), the display's vblank
//count and frame buffer (sceDisplay_Service's sceDisplay).
enum { NidCtrlPeekBufferPositive = 0x3A622550, NidCpuClockInt = 0xFDB5BFE9, NidBusClockInt = 0xBD681969 };
enum { NidDisplayGetVcount = 0x9C6EAAD7, NidDisplayGetFrameBuf = 0xEEDA2E54 };
typedef struct { u32 timeStamp, buttons; u8 lx, ly, reserved[6]; } CtrlData;

typedef struct { u32 size, startAddr, memSize, attr; } PartitionInfo;

typedef struct {
  void (*patchSyscall)(void* original, void* replacement);
  int (*querySystemCall)(void* function);
  int (*getInitParam)(const char* name, u16* type, u32* length, void* buffer);
  int (*msIsEf)(void);
  void (*flushCache)(void);
  int (*henVersion)(void);
  int (*henMinorVersion)(void);
  Address (*findFunctionOnSystem)(const char* library, u32 nid, int userModulesOnly);
  SceUID (*allocPartitionMemory)(SceUID partition, const char* name, int type, SceSize size, void* address);
  int (*freePartitionMemory)(SceUID block);
  void* (*getBlockHeadAddr)(SceUID block);
  SceSize (*partitionMaxFreeMemSize)(int partition);
  SceSize (*partitionTotalFreeMemSize)(int partition);
  int (*queryMemoryPartitionInfo)(int partition, PartitionInfo* info);
  int (*getModel)(void);
  int (*devkitVersion)(void);
  void (*registerSubIntrMonitor)(MonitorFunction before, MonitorFunction after);
  void (*releaseSubIntrMonitor)(void);
  int (*ctrlPeekBufferPositive)(CtrlData* data, int count);
  int (*cpuClock)(void);
  int (*busClock)(void);
  u32 (*displayGetVcount)(void);
  int (*displayGetFrameBuf)(void** top, int* width, int* format, int sync);
} Firmware;
extern Firmware firmware;

void* findFunction(const char* module, const char* library, u32 nid);
extern Address lastLookup;  //what the last lookup returned, taken or not (for the trace)

//The GE's next-command register, and a word of memory as the GE reads it (uncached); hosttest.c, which runs hooks.c
//on the Mac (HOST), has fakes of its own.
//So is a frame buffer's memory, written through the uncached address so the display sees the writes at once.
#ifdef HOST
extern volatile u32 fakeGeNextCommand;
u32 fakeWord(u32 physical);
volatile u8* hostFrameBuffer(u32 physical);
#define GeNextCommand fakeGeNextCommand
#define UncachedWord(physical) fakeWord(physical)
#define UncachedBytes(physical) hostFrameBuffer(physical)
#else
#define GeNextCommand (*(volatile u32*)0xBD400108)
#define UncachedWord(physical) (*(volatile u32*)((physical) | 0xA0000000))
#define UncachedBytes(physical) ((volatile u8*)((physical) | 0xA0000000))
#endif

//hooks.c
enum { HookCount = KindCount };
typedef struct {
  const char* name;
  void* original;     //the firmware's function, 0 if it wasn't found
  int syscall;        //its syscall number once patched (from sctrlKernelQuerySystemCall), < 0 if unknown
  int installed;
} HookStatus;
extern HookStatus hookStatus[HookCount];
extern int monitorAvailable;

void installHooks(void);
u32 hooksInstalledMask(void);
u32 geState(void);
void startCounting(void);
u32 stopCounting(u32 counts[KindCount]);
void startCapture(ProfileRecord* buffer, u32 capacity);
int captureFull(void);
void stopCapture(u32* recordCount, u32* dropped);
int threadTable(u32 uids[], int capacity);
void measureCosts(u32* idleNanoseconds, u32* captureNanoseconds, u32* monitorNanoseconds);

//notice.c: a few lines of text, or a small corner marker while a capture or dry run counts, drawn into the frame
//buffer the game hands sceDisplaySetFrameBuf (noticeDraw, from that hook, once its record is closed); when the hook
//sees no flips, the control thread draws the text through sceDisplayGetFrameBuf instead (noticeFallback).
void noticeEnable(int on);
void noticeShow(const char* line1, const char* line2, const char* line3, u32 milliseconds);
void noticeClear(void);
void noticeMarker(int on);
void noticeDraw(void* top, int width, int format);
int noticeWantsFallback(void);
void noticeFallback(void);
u32 noticeFirstFlip(void);  //when the hook first saw a flip (sceKernelGetSystemTimeLow), 0 if it hasn't

//trace.c: a line per step, kept in memory and appended to <device>:/PHOBOS/trace.txt whenever the control thread may
//open a file (never during a capture or a dry run's window).
void traceStart(u32 time);
void traceLine(const char* format, ...) __attribute__((format(printf, 1, 2)));
int traceWrite(const char* device);

//text.c: a small formatter (%s %c %d %u %x %X, with a width and 0 fill) for the report and file names.
typedef struct { char* data; int size, length; } Text;
void textFormat(Text* text, const char* format, ...) __attribute__((format(printf, 2, 3)));
void textFormatList(Text* text, const char* format, va_list args);
int textEqual(const char* a, const char* b);
#ifdef HOST
#include <string.h>
#else
void* memset(void* target, int value, unsigned int size);
void* memcpy(void* target, const void* source, unsigned int size);
#endif
