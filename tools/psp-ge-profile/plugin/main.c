//psp-ge-profile: a kernel plugin for ARK's plugin loader that records, in microseconds, how a commercial game drives
//the GE (its display lists, stall updates, syncs, SIGNAL and FINISH callbacks) and the display (frame buffer flips,
//vblank waits), so Phobos's GE timing can be checked against a real PSP. See ../README.md.
//
//This file: the module's start, the settings (<device>:/PHOBOS/prof.txt), the control thread that waits for a
//trigger (a button combination held for a second, or a time after the game started) and runs a capture or a dry run,
//the notices it puts on screen (notice.c draws them), and the files it writes once that's over
//(<device>:/PHOBOS/PROF/<disc id>-<n>.bin, or .txt, and the trace, <device>:/PHOBOS/trace.txt). The device is looked
//for again before every run and every write, so a memory stick not ready at the game's boot doesn't stop anything. No
//file is opened while a capture runs; nothing is written, renamed or deleted outside <device>:/PHOBOS.

#include "plugin.h"
#include <pspctrl.h>
#include <pspintrman.h>
#include <pspiofilemgr.h>
#include <pspsysmem.h>
#include <pspthreadman.h>

PSP_MODULE_INFO("PhobosGeProfile", PSP_MODULE_KERNEL | PSP_MODULE_NO_STOP, 0, 1);

Firmware firmware;

//The control thread waits at a priority above most game threads, so it notices the buttons and ends a capture on
//time (it wakes for a few microseconds every 50 ms while idle, every 250 ms while capturing); it writes files at
//the lowest priority, when the game's threads are waiting.
enum { WaitingPriority = 0x18, WritingPriority = 0x7E, ThreadStack = 0x2000 };
enum { Second = 1000000, PollInterval = 50000, CaptureCheck = 250000, HeldPolls = 20, FallbackInterval = 10000 };
enum { StartNotice = 4000, CountdownNotice = 30000, SavedNotice = 5000, FailedNotice = 8000, EarlyTrace = 6 * Second };

typedef struct {
  int dry;          //mode = dry (the default) or capture
  u32 buttons;      //the combination held for a second
  int delay;        //seconds from the buttons' release to the start
  int start;        //seconds after the game started for a timed run; < 0: none
  int length;       //seconds
  int bufferKb;     //the most kernel memory a capture's buffer takes
  int keepKb;       //what it leaves free in its partition
  int partition;
  int notices;      //on-screen notices on (the default) or off
  int read;         //prof.txt was found
} Settings;

static Settings settings = {1, PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT, 1, 20, 10, 2048, 256, 1, 1, 0};

//In the order the report names them, so the default reads as prof.txt writes it.
static const struct { const char* name; u32 mask; } buttonNames[] = {
  {"L", PSP_CTRL_LTRIGGER}, {"R", PSP_CTRL_RTRIGGER}, {"SELECT", PSP_CTRL_SELECT}, {"START", PSP_CTRL_START},
  {"UP", PSP_CTRL_UP}, {"RIGHT", PSP_CTRL_RIGHT}, {"DOWN", PSP_CTRL_DOWN}, {"LEFT", PSP_CTRL_LEFT},
  {"TRIANGLE", PSP_CTRL_TRIANGLE}, {"CIRCLE", PSP_CTRL_CIRCLE}, {"CROSS", PSP_CTRL_CROSS},
  {"SQUARE", PSP_CTRL_SQUARE}, {"NOTE", PSP_CTRL_NOTE}, {"SCREEN", PSP_CTRL_SCREEN}, {"VOLUP", PSP_CTRL_VOLUP},
  {"VOLDOWN", PSP_CTRL_VOLDOWN},
};
enum { ButtonNames = sizeof(buttonNames) / sizeof(buttonNames[0]) };

static u32 pluginStart;
static const char* device;  //"ms0:" or "ef0:", whichever has a PHOBOS folder
static char discId[16] = "UNKNOWN";
static char title[64];

typedef struct { int valid; u32 start, size, attr, maxFree, totalFree; } Partition;
enum { Partitions = 13 };
static Partition memoryAtStart[Partitions], memoryNow[Partitions];

static char scratch[8192];  //prof.txt, PARAM.SFO, the report
static SceKernelThreadInfo threadInfo;

static u32 now(void) {
  return sceKernelGetSystemTimeLow();
}

static u32 vblankCount(void) {
  return firmware.displayGetVcount ? firmware.displayGetVcount() : 0;
}

Address lastLookup;

static void* arkFunction(u32 nid) {
  lastLookup = sctrlHENFindFunction(ArkModule, ArkLibrary, nid);
  return PlausibleFunction(lastLookup) ? (void*)lastLookup : 0;
}

//A function by its module and library; failing that, by library alone if this ARK can search every module.
void* findFunction(const char* module, const char* library, u32 nid) {
  Address address = sctrlHENFindFunction(module, library, nid);
  if(!PlausibleFunction(address) && firmware.findFunctionOnSystem) address = firmware.findFunctionOnSystem(library, nid, 0);
  lastLookup = address;
  return PlausibleFunction(address) ? (void*)address : 0;
}

//A lookup's result for the trace: where it was found, or what came back instead.
static void* found(const char* name, void* function) {
  if(function) traceLine("found %s at 0x%08lX", name, (unsigned long)(Address)function);
  else if(lastLookup) traceLine("refused %s: the lookup gave 0x%08lX", name, (unsigned long)lastLookup);
  else traceLine("not found: %s", name);
  return function;
}

#define SysMem "sceSystemMemoryManager", "SysMemForKernel"
#define Interrupts "sceInterruptManager", "InterruptManagerForKernel"
static void findFirmware(void) {
  firmware.findFunctionOnSystem = found("sctrlHENFindFunctionOnSystem", arkFunction(NidFindFunctionOnSystem));
  firmware.patchSyscall = arkFunction(NidPatchSyscall);
  if(!firmware.patchSyscall) firmware.patchSyscall = arkFunction(NidPatchSyscallOld);
  if(!firmware.patchSyscall) firmware.patchSyscall = arkFunction(NidPatchSyscallOlder);
  found("sctrlHENPatchSyscall", firmware.patchSyscall);
  firmware.querySystemCall = found("sctrlKernelQuerySystemCall", arkFunction(NidQuerySystemCall));
  firmware.getInitParam = found("sctrlGetInitPARAM", arkFunction(NidGetInitParam));
  firmware.msIsEf = found("sctrlKernelMsIsEf", arkFunction(NidMsIsEf));
  firmware.flushCache = found("sctrlFlushCache", arkFunction(NidFlushCache));
  firmware.henVersion = found("sctrlHENGetVersion", arkFunction(NidHenVersion));
  firmware.henMinorVersion = found("sctrlHENGetMinorVersion", arkFunction(NidHenMinorVersion));
  firmware.allocPartitionMemory = found("sceKernelAllocPartitionMemory", findFunction(SysMem, NidAllocPartitionMemory));
  firmware.freePartitionMemory = found("sceKernelFreePartitionMemory", findFunction(SysMem, NidFreePartitionMemory));
  firmware.getBlockHeadAddr = found("sceKernelGetBlockHeadAddr", findFunction(SysMem, NidGetBlockHeadAddr));
  firmware.partitionMaxFreeMemSize =
    found("sceKernelPartitionMaxFreeMemSize", findFunction(SysMem, NidPartitionMaxFreeMemSize));
  firmware.partitionTotalFreeMemSize =
    found("sceKernelPartitionTotalFreeMemSize", findFunction(SysMem, NidPartitionTotalFreeMemSize));
  firmware.queryMemoryPartitionInfo =
    found("sceKernelQueryMemoryPartitionInfo", findFunction(SysMem, NidQueryMemoryPartitionInfo));
  firmware.getModel = found("sceKernelGetModel", findFunction(SysMem, NidGetModel));
  firmware.devkitVersion =
    found("sceKernelDevkitVersion", findFunction("sceSystemMemoryManager", "SysMemUserForUser", NidDevkitVersion));
  firmware.registerSubIntrMonitor = found("RegisterSubIntrruptMonitor", findFunction(Interrupts, NidRegisterSubIntrMonitor));
  firmware.releaseSubIntrMonitor = found("ReleaseSubIntrruptMonitor", findFunction(Interrupts, NidReleaseSubIntrMonitor));
  firmware.ctrlPeekBufferPositive =
    found("sceCtrlPeekBufferPositive", findFunction("sceController_Service", "sceCtrl", NidCtrlPeekBufferPositive));
  firmware.cpuClock = found("scePowerGetCpuClockFrequencyInt", findFunction("scePower_Service", "scePower", NidCpuClockInt));
  firmware.busClock = found("scePowerGetBusClockFrequencyInt", findFunction("scePower_Service", "scePower", NidBusClockInt));
  firmware.displayGetVcount =
    found("sceDisplayGetVcount", findFunction("sceDisplay_Service", "sceDisplay", NidDisplayGetVcount));
  firmware.displayGetFrameBuf =
    found("sceDisplayGetFrameBuf", findFunction("sceDisplay_Service", "sceDisplay", NidDisplayGetFrameBuf));
}

static int exists(const char* path) {
  SceIoStat stat;
  return sceIoGetstat(path, &stat) >= 0;
}

static int number(const char* s, int fallback) {
  int value = 0, negative = *s == '-', digits = 0;
  if(negative) s++;
  for(; *s >= '0' && *s <= '9'; s++, digits++) value = value * 10 + *s - '0';
  return digits ? (negative ? -value : value) : fallback;
}

static int clamp(int value, int low, int high) {
  return value < low ? low : value > high ? high : value;
}

//Button names joined by '+' (or spaces or commas); 0 if any isn't known.
static u32 parseButtons(char* value) {
  u32 mask = 0;
  while(*value) {
    while(*value == '+' || *value == ' ' || *value == ',') value++;
    char* name = value;
    while(*value && *value != '+' && *value != ' ' && *value != ',') value++;
    char end = *value;
    *value = 0;
    if(*name) {
      u32 bit = 0;
      for(int n = 0; n < ButtonNames; n++) {
        if(textEqual(name, buttonNames[n].name)) bit = buttonNames[n].mask;
      }
      if(!bit) return 0;
      mask |= bit;
    }
    *value = end;
  }
  return mask;
}

static char* trim(char* s) {
  while(*s == ' ' || *s == '\t') s++;
  char* end = s;
  while(*end) end++;
  while(end > s && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
  return s;
}

//prof.txt: "name = value" lines; '#' starts a comment. Unknown names and values are ignored (the report shows what
//was used).
static void readSettings(void) {
  if(!device) return;
  char path[48];
  Text text = {path, sizeof(path), 0};
  textFormat(&text, "%s/PHOBOS/prof.txt", device);
  SceUID file = sceIoOpen(path, PSP_O_RDONLY, 0);
  if(file < 0) return;
  int size = sceIoRead(file, scratch, sizeof(scratch) - 1);
  sceIoClose(file);
  if(size <= 0) return;
  scratch[size] = 0;
  settings.read = 1;
  for(char* line = scratch; *line;) {
    char* next = line;
    while(*next && *next != '\n') next++;
    if(*next) *next++ = 0;
    for(char* c = line; *c; c++) {
      if(*c == '#' || *c == '\r') { *c = 0; break; }
    }
    char* equals = line;
    while(*equals && *equals != '=') equals++;
    if(*equals) {
      *equals = 0;
      char* name = trim(line);
      char* value = trim(equals + 1);
      if(textEqual(name, "mode")) {
        if(textEqual(value, "dry")) settings.dry = 1;
        if(textEqual(value, "capture")) settings.dry = 0;
      } else if(textEqual(name, "buttons")) {
        u32 mask = parseButtons(value);
        if(mask) settings.buttons = mask;
      } else if(textEqual(name, "delay")) {
        settings.delay = clamp(number(value, settings.delay), 0, 30);
      } else if(textEqual(name, "start")) {
        settings.start = textEqual(value, "off") ? -1 : clamp(number(value, settings.start), -1, 3600);
      } else if(textEqual(name, "length")) {
        settings.length = clamp(number(value, settings.length), 1, 60);
      } else if(textEqual(name, "buffer")) {
        settings.bufferKb = clamp(number(value, settings.bufferKb), 64, 16384);
      } else if(textEqual(name, "keep")) {
        settings.keepKb = clamp(number(value, settings.keepKb), 0, 8192);
      } else if(textEqual(name, "partition")) {
        settings.partition = clamp(number(value, settings.partition), 1, 12);
      } else if(textEqual(name, "notices")) {
        if(textEqual(value, "on")) settings.notices = 1;
        if(textEqual(value, "off")) settings.notices = 0;
      }
    }
    line = next;
  }
}

static void traceSettings(void) {
  if(!settings.read) {
    traceLine("settings: prof.txt not read yet, defaults (mode %s, start %d s, notices %s)",
              settings.dry ? "dry" : "capture", settings.start, settings.notices ? "on" : "off");
    return;
  }
  traceLine("settings read: mode %s, buttons 0x%lX, delay %d s, start %d s, length %d s, buffer %d KB, keep %d KB, "
            "partition %d, notices %s", settings.dry ? "dry" : "capture", (unsigned long)settings.buttons,
            settings.delay, settings.start, settings.length, settings.bufferKb, settings.keepKb, settings.partition,
            settings.notices ? "on" : "off");
}

//The device with a PHOBOS folder, looked for again each time (a memory stick may not be ready at the game's boot);
//the settings are read the first time it's there.
static int deviceLooked;
static const char* ensureDevice(void) {
  const char* seen = exists("ms0:/PHOBOS") ? "ms0:" : exists("ef0:/PHOBOS") ? "ef0:" : 0;
  if(!deviceLooked || seen != device) traceLine("device: %s", seen ? seen : "no PHOBOS folder on ms0: or ef0:");
  deviceLooked = 1;
  device = seen;
  if(device && !settings.read) {
    readSettings();
    noticeEnable(settings.notices);
    traceSettings();
  }
  return device;
}

//At the writing priority, as every file is written.
static void flushTrace(void) {
  sceKernelChangeThreadPriority(0, WritingPriority);
  traceWrite(ensureDevice());
  sceKernelChangeThreadPriority(0, WaitingPriority);
}

//Letters and digits only, so a disc id can't make a path.
static void setDiscId(const char* id) {
  int n = 0;
  for(; *id && n < (int)sizeof(discId) - 1; id++) {
    char c = *id;
    if(c >= 'a' && c <= 'z') c -= 32;
    if((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) discId[n++] = c;
  }
  if(n) discId[n] = 0;
}

static void setTitle(const char* name, int length) {
  int n = 0;
  for(; n < length && name[n] && n < (int)sizeof(title) - 1; n++) title[n] = name[n] >= ' ' ? name[n] : ' ';
  title[n] = 0;
}

//ARK knows the running game's PARAM.SFO.
static void readGameInfo(void) {
  static char value[256];
  u16 type;
  u32 length = sizeof(value);
  if(!firmware.getInitParam) return;
  memset(value, 0, sizeof(value));
  if(firmware.getInitParam("DISC_ID", &type, &length, value) >= 0) setDiscId(value);
  memset(value, 0, sizeof(value));
  length = sizeof(value);
  if(firmware.getInitParam("TITLE", &type, &length, value) >= 0) setTitle(value, sizeof(value) - 1);
}

//Failing that, the disc's own PARAM.SFO, read only once a capture is over.
static void readParamSfo(void) {
  if(discId[0] != 'U' || discId[1] != 'N' || discId[2] != 'K') return;
  SceUID file = sceIoOpen("disc0:/PSP_GAME/PARAM.SFO", PSP_O_RDONLY, 0);
  if(file < 0) return;
  int size = sceIoRead(file, scratch, sizeof(scratch) - 1);
  sceIoClose(file);
  if(size < 20 || scratch[1] != 'P' || scratch[2] != 'S' || scratch[3] != 'F') return;
  scratch[size] = 0;
  u8* sfo = (u8*)scratch;
  u32 keys = sfo[8] | sfo[9] << 8 | sfo[10] << 16, data = sfo[12] | sfo[13] << 8 | sfo[14] << 16;
  u32 entries = sfo[16] | sfo[17] << 8;
  for(u32 n = 0; n < entries && 20 + n * 16 + 16 <= (u32)size; n++) {
    u8* entry = sfo + 20 + n * 16;
    u32 key = keys + (entry[0] | entry[1] << 8);
    u32 length = entry[4] | entry[5] << 8, offset = data + (entry[12] | entry[13] << 8 | entry[14] << 16);
    if(key >= (u32)size || offset + length > (u32)size) continue;
    if(textEqual(scratch + key, "DISC_ID")) setDiscId(scratch + offset);
    if(textEqual(scratch + key, "TITLE") && !title[0]) setTitle(scratch + offset, length);
  }
}

//Only partitions the memory manager confirms are asked their free sizes: those two calls don't check the id.
static void snapshotMemory(Partition partitions[Partitions]) {
  for(int id = 1; id < Partitions; id++) {
    Partition* partition = &partitions[id];
    memset(partition, 0, sizeof(*partition));
    PartitionInfo info = {sizeof(info), 0, 0, 0};
    if(!firmware.queryMemoryPartitionInfo || firmware.queryMemoryPartitionInfo(id, &info) < 0 || !info.memSize) continue;
    partition->valid = 1;
    partition->start = info.startAddr;
    partition->size = info.memSize;
    partition->attr = info.attr;
    if(firmware.partitionMaxFreeMemSize) partition->maxFree = firmware.partitionMaxFreeMemSize(id);
    if(firmware.partitionTotalFreeMemSize) partition->totalFree = firmware.partitionTotalFreeMemSize(id);
  }
}

//A capture's buffer: what the settings allow, leaving keep KB of the partition's largest free block, in whole 4 KB.
static SceUID block = -1;
static ProfileRecord* allocateBuffer(u32* bytes, Text* why) {
  Partition partitions[Partitions];
  int id = settings.partition;
  if(!firmware.allocPartitionMemory || !firmware.getBlockHeadAddr || !firmware.freePartitionMemory ||
     !firmware.partitionMaxFreeMemSize || !firmware.queryMemoryPartitionInfo) {
    textFormat(why, "the firmware's kernel memory functions weren't found\n");
    return 0;
  }
  snapshotMemory(partitions);
  if(!partitions[id].valid) {
    textFormat(why, "memory partition %d doesn't exist\n", id);
    return 0;
  }
  u32 free = partitions[id].maxFree, keep = settings.keepKb * 1024u, size = free > keep ? free - keep : 0;
  if(size > settings.bufferKb * 1024u) size = settings.bufferKb * 1024u;
  size &= ~0xFFFu;
  if(size < 64 * 1024) {
    textFormat(why, "partition %d's largest free block is %lu KB; with keep = %d KB that leaves under 64 KB\n", id,
               free / 1024, settings.keepKb);
    return 0;
  }
  block = firmware.allocPartitionMemory(id, "PhobosGeProfile", PSP_SMEM_High, size, 0);
  if(block < 0) {
    textFormat(why, "allocating %lu KB in partition %d failed (0x%08X)\n", size / 1024, id, block);
    return 0;
  }
  *bytes = size;
  return firmware.getBlockHeadAddr(block);
}

static void freeBuffer(void) {
  if(block >= 0) firmware.freePartitionMemory(block);
  block = -1;
}

static u32 readButtons(void) {
  CtrlData pad;
  if(!firmware.ctrlPeekBufferPositive) return 0;
  memset(&pad, 0, sizeof(pad));
  firmware.ctrlPeekBufferPositive(&pad, 1);
  return pad.buttons;
}

static void waitForRelease(void) {
  while(settings.buttons && (readButtons() & settings.buttons) == settings.buttons) sceKernelDelayThread(PollInterval);
}

//Why a file wasn't written, as the notice and the trace say it.
enum { Saved, NoFolder, NoProfFolder, NoNumber, OpenFailed, WriteFailed, NoMemory };
static const char* const reasons[] = {
  "SAVED", "NO PHOBOS FOLDER", "CANNOT MAKE PHOBOS/PROF", "NO FREE FILE NUMBER", "CANNOT CREATE THE FILE",
  "WRITE FAILED", "NOT ENOUGH MEMORY",
};

//<device>:/PHOBOS/PROF/<disc id>-<n>.<extension> with the first n neither a .bin nor a .txt has, the device looked for
//again first; PROF is made inside an existing PHOBOS folder, and nothing else is ever made. Returns a reason (Saved:
//path and number are set).
static int newFile(char* path, int size, const char* extension, int* number) {
  char other[64];
  if(!ensureDevice()) return NoFolder;
  Text text = {path, size, 0};
  textFormat(&text, "%s/PHOBOS/PROF", device);
  if(!exists(path)) sceIoMkdir(path, 0777);
  if(!exists(path)) return NoProfFolder;
  for(int n = 1; n < 10000; n++) {
    Text binary = {other, sizeof(other), 0};
    textFormat(&binary, "%s/PHOBOS/PROF/%s-%d.bin", device, discId, n);
    if(exists(other)) continue;
    binary.length = 0;
    textFormat(&binary, "%s/PHOBOS/PROF/%s-%d.txt", device, discId, n);
    if(exists(other)) continue;
    text.length = 0;
    textFormat(&text, "%s/PHOBOS/PROF/%s-%d.%s", device, discId, n, extension);
    *number = n;
    return Saved;
  }
  return NoNumber;
}

static void fileName(char* name, int size, int number, const char* extension) {
  Text text = {name, size, 0};
  name[0] = 0;
  textFormat(&text, "%s-%d.%s", discId, number, extension);
}

//In pieces, with a pause between them, so the game's own reads of the stick get turns.
static int writeAll(SceUID file, const void* data, u32 size) {
  const u8* p = data;
  while(size) {
    u32 piece = size < 32768 ? size : 32768;
    if(sceIoWrite(file, p, piece) != (int)piece) return 0;
    p += piece;
    size -= piece;
    if(size) sceKernelDelayThread(2000);
  }
  return 1;
}

//Returns a reason; name is the file's (when one was made).
static int writeText(const char* text, int length, char* name, int nameSize) {
  char path[64];
  int number = 0, reason = newFile(path, sizeof(path), "txt", &number);
  name[0] = 0;
  if(reason != Saved) return reason;
  fileName(name, nameSize, number, "txt");
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT, 0777);
  if(file < 0) return OpenFailed;
  int ok = writeAll(file, text, length);
  sceIoClose(file);
  return ok ? Saved : WriteFailed;
}

//The notice after a run, and its trace line.
static void showResult(const char* what, int reason, const char* name, const char* extra) {
  if(reason == Saved) {
    traceLine("%s: wrote %s%s%s", what, name, extra[0] ? ", " : "", extra);
    noticeShow("SAVED", name, extra, SavedNotice);
  } else {
    traceLine("%s: not saved: %s%s%s", what, reasons[reason], name[0] ? ", " : "", name);
    noticeShow("NOT SAVED", reasons[reason], extra, FailedNotice);
  }
}

static const char* modelName(int model) {
  switch(model) {
  case 0: return "PSP-1000";
  case 1: return "PSP-2000";
  case 2: case 3: case 6: case 8: return "PSP-3000";
  case 4: return "PSP Go";
  case 10: return "PSP Street (E1000)";
  }
  return "unknown model";
}

static void describeSystem(Text* text) {
  int model = firmware.getModel ? firmware.getModel() : -1;
  textFormat(text, "game: %s  %s\n", discId, title);
  textFormat(text, "firmware 0x%08X, %s (generation value %d), ARK 0x%X.%X, files on %s%s\n",
             firmware.devkitVersion ? firmware.devkitVersion() : 0, modelName(model), model,
             firmware.henVersion ? firmware.henVersion() : 0, firmware.henMinorVersion ? firmware.henMinorVersion() : 0,
             device ? device : "(none)", firmware.msIsEf && firmware.msIsEf() == 1 ? " (ms0 is redirected to ef0)" : "");
  textFormat(text, "clocks now: CPU %d MHz, bus %d MHz\n", firmware.cpuClock ? firmware.cpuClock() : 0,
             firmware.busClock ? firmware.busClock() : 0);
  textFormat(text, "settings: %s; mode %s, buttons", settings.read ? "prof.txt read" : "prof.txt not found, defaults",
             settings.dry ? "dry" : "capture");
  int first = 1;
  for(int n = 0; n < ButtonNames; n++) {
    if(!(settings.buttons & buttonNames[n].mask)) continue;
    textFormat(text, "%c%s", first ? ' ' : '+', buttonNames[n].name);
    first = 0;
  }
  textFormat(text, ", delay %d s, start ", settings.delay);
  if(settings.start < 0) textFormat(text, "off");
  else textFormat(text, "%d s", settings.start);
  textFormat(text, ", length %d s, buffer %d KB, keep %d KB, partition %d, notices %s\n", settings.length,
             settings.bufferKb, settings.keepKb, settings.partition, settings.notices ? "on" : "off");
}

static void describeMemory(Text* text) {
  textFormat(text, "\nmemory partitions (KB: size, largest free block, all free; at the game's start, then now):\n");
  for(int id = 1; id < Partitions; id++) {
    Partition* a = &memoryAtStart[id];
    Partition* b = &memoryNow[id];
    if(!a->valid && !b->valid) continue;
    Partition* p = b->valid ? b : a;
    textFormat(text, "  %2d at 0x%08lX, attr %lX: %6lu | %6lu %6lu | %6lu %6lu\n", id, p->start, p->attr, p->size / 1024,
               a->maxFree / 1024, a->totalFree / 1024, b->maxFree / 1024, b->totalFree / 1024);
  }
  if(!firmware.queryMemoryPartitionInfo) textFormat(text, "  (the memory manager's query functions weren't found)\n");
}

//The dry run: the hooks only count calls for the window, then the report says what's in place, how often each was
//called, what a capture would take, and what the hooks cost here.
static void dryRun(int timed) {
  u32 counts[KindCount];
  noticeClear();
  noticeMarker(1);
  u32 start = now();
  startCounting();
  sceKernelDelayThread(settings.length * Second);
  u32 total = stopCounting(counts);
  u32 elapsed = now() - start;
  noticeMarker(0);
  u32 idleNs, captureNs, monitorNs;
  measureCosts(&idleNs, &captureNs, &monitorNs);
  snapshotMemory(memoryNow);
  u32 ge = geState();
  sceKernelChangeThreadPriority(0, WritingPriority);
  readParamSfo();

  Text text = {scratch, sizeof(scratch), 0};
  scratch[0] = 0;
  textFormat(&text, "Phobos GE profiler %s: dry run (hooks counting only; nothing recorded)\n", ProfileVersion);
  describeSystem(&text);
  u32 tenths = elapsed / 100000, after = (start - pluginStart) / 100000;
  textFormat(&text, "window: %lu.%lu s, from %lu.%lu s after the game started (%s)\n\n", tenths / 10, tenths % 10,
             after / 10, after % 10, timed ? "timed" : "buttons");
  textFormat(&text, "%-34s %-24s %8s\n", "hook", "state", "calls");
  for(int kind = 1; kind < KindCount; kind++) {
    HookStatus* status = &hookStatus[kind];
    char state[32];
    Text cell = {state, sizeof(state), 0};
    state[0] = 0;
    if(kind >= KindGeSignal && kind <= KindGeFinish) textFormat(&cell, monitorAvailable ? "monitor" : "no monitor");
    else if(kind == KindVblank) textFormat(&cell, monitorAvailable ? "monitor" : "no monitor");
    else if(!status->original) textFormat(&cell, "not found");
    else if(!firmware.patchSyscall) textFormat(&cell, "no sctrlHENPatchSyscall");
    else if(status->installed && status->syscall >= 0) textFormat(&cell, "in place, syscall 0x%X", status->syscall);
    else if(status->installed) textFormat(&cell, "patched");
    else textFormat(&cell, "patch not confirmed");
    textFormat(&text, "%-34s %-24s %8lu\n", status->name ? status->name : "?", state, counts[kind]);
  }
  u32 seconds10 = elapsed / 100000 ? elapsed / 100000 : 1;
  u32 perSecond = total * 10 / seconds10, bytesPerSecond = perSecond * ProfileRecordSize;
  textFormat(&text, "\nrecords: %lu in the window, %lu a second, %lu KB a second; a %d s capture needs %lu KB\n", total,
             perSecond, bytesPerSecond / 1024, settings.length, bytesPerSecond * settings.length / 1024);
  textFormat(&text, "cost per call: %lu ns idle (the hook's own), %lu ns capturing; %lu ns per callback (monitor)\n",
             idleNs, captureNs, monitorNs);
  textFormat(&text, "GE now: %s\n", (ge & 3) == GeIdle ? "idle" : (ge & 3) == GeDrawing ? "drawing" :
                                    (ge & 3) == GeStalled ? "stalled" : "not sampled");
  describeMemory(&text);
  Partition* p = &memoryNow[settings.partition];
  if(p->valid) {
    u32 keep = settings.keepKb * 1024u, size = p->maxFree > keep ? p->maxFree - keep : 0;
    if(size > settings.bufferKb * 1024u) size = settings.bufferKb * 1024u;
    size &= ~0xFFFu;
    textFormat(&text, "a capture's buffer now: %lu KB in partition %d", size / 1024, settings.partition);
    if(bytesPerSecond) textFormat(&text, ", about %lu s of this window's calls", size / bytesPerSecond);
    textFormat(&text, "\n");
  }
  textFormat(&text, "functions found: patch %d, query %d, flush %d, init param %d, alloc %d, monitor %d, ctrl %d, "
             "clocks %d, vcount %d, frame buffer %d, find on system %d\n", !!firmware.patchSyscall,
             !!firmware.querySystemCall, !!firmware.flushCache, !!firmware.getInitParam, !!firmware.allocPartitionMemory,
             monitorAvailable, !!firmware.ctrlPeekBufferPositive, !!firmware.cpuClock, !!firmware.displayGetVcount,
             !!firmware.displayGetFrameBuf, !!firmware.findFunctionOnSystem);
  u32 flip = noticeFirstFlip();
  if(flip) textFormat(&text, "first flip through the sceDisplaySetFrameBuf hook: %lu ms after the start\n",
                      (flip - pluginStart) / 1000);
  else textFormat(&text, "no flip through the sceDisplaySetFrameBuf hook yet (notices drawn through %s)\n",
                  firmware.displayGetFrameBuf ? "sceDisplayGetFrameBuf" : "nothing: not shown");
  char name[32];
  showResult("dry run", writeText(scratch, text.length, name, sizeof(name)), name, "");
  sceKernelChangeThreadPriority(0, WaitingPriority);
}

//Why a capture couldn't run, in a .txt of its own when that can be written. Returns the reason for the notice.
static int note(const char* why, char* name, int nameSize) {
  sceKernelChangeThreadPriority(0, WritingPriority);
  snapshotMemory(memoryNow);
  readParamSfo();
  Text text = {scratch, sizeof(scratch), 0};
  scratch[0] = 0;
  textFormat(&text, "Phobos GE profiler %s: no capture: %s", ProfileVersion, why);
  describeSystem(&text);
  describeMemory(&text);
  int reason = writeText(scratch, text.length, name, nameSize);
  sceKernelChangeThreadPriority(0, WaitingPriority);
  return reason;
}

static int writeCapture(ProfileHeader* header, const ProfileRecord* records, char* name, int nameSize) {
  static u32 uids[64];
  static ProfileThread entry;
  char path[64];
  int threads = threadTable(uids, 64);
  int number = 0, reason = newFile(path, sizeof(path), "bin", &number);
  name[0] = 0;
  if(reason != Saved) return reason;
  fileName(name, nameSize, number, "bin");
  header->captureNumber = number;
  header->threadTableOffset = ProfileHeaderSize + header->recordCount * ProfileRecordSize;
  header->threadCount = threads;
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT, 0777);
  if(file < 0) return OpenFailed;
  int ok = writeAll(file, header, ProfileHeaderSize) &&
           writeAll(file, records, header->recordCount * ProfileRecordSize);
  for(int index = 0; ok && index < threads; index++) {
    memset(&entry, 0, sizeof(entry));
    entry.uid = uids[index];
    memset(&threadInfo, 0, sizeof(threadInfo));
    threadInfo.size = sizeof(threadInfo);
    if(index && sceKernelReferThreadStatus(uids[index], &threadInfo) >= 0) memcpy(entry.name, threadInfo.name, 31);
    else memcpy(entry.name, index ? "?" : "interrupt", index ? 2 : 10);
    ok = writeAll(file, &entry, sizeof(entry));
  }
  sceIoClose(file);
  return ok ? Saved : WriteFailed;
}

static void copyString(char* target, const char* source, int size) {
  int n = 0;
  for(; n < size - 1 && source[n]; n++) target[n] = source[n];
  for(; n < size; n++) target[n] = 0;
}

static void capture(int timed) {
  static ProfileHeader header;
  char reason[160];
  Text why = {reason, sizeof(reason), 0};
  reason[0] = 0;
  u32 bytes = 0;
  ProfileRecord* records = allocateBuffer(&bytes, &why);
  if(!records) {
    char name[32], extra[32];
    Text see = {extra, sizeof(extra), 0};
    extra[0] = 0;
    traceLine("capture: no buffer: %s", reason);
    if(note(reason, name, sizeof(name)) == Saved) textFormat(&see, "SEE %s", name);
    showResult("capture", NoMemory, "", extra);
    return;
  }
  memset(&header, 0, sizeof(header));
  memcpy(header.magic, "PHGEPROF", 8);
  header.format = ProfileFormat;
  header.headerSize = ProfileHeaderSize;
  header.recordSize = ProfileRecordSize;
  copyString(header.pluginVersion, ProfileVersion, sizeof(header.pluginVersion));
  header.cpuMhz = firmware.cpuClock ? firmware.cpuClock() : 0;
  header.busMhz = firmware.busClock ? firmware.busClock() : 0;
  header.firmware = firmware.devkitVersion ? firmware.devkitVersion() : 0;
  header.arkVersion = firmware.henVersion ? firmware.henVersion() : 0;
  header.arkMinor = firmware.henMinorVersion ? firmware.henMinorVersion() : 0;
  header.model = firmware.getModel ? (u32)firmware.getModel() : ~0u;
  header.hooks = hooksInstalledMask();
  header.pluginStartTime = pluginStart;
  header.capacity = bytes / ProfileRecordSize;
  header.partition = settings.partition;
  header.bufferBytes = bytes;
  header.flags = (timed ? ProfileTimed : 0) | (monitorAvailable ? ProfileMonitor : 0) | ProfileGeSampled;
  header.startVcount = vblankCount();
  noticeClear();
  noticeMarker(1);
  header.startTime = now();
  startCapture(records, header.capacity);
  u32 length = settings.length * Second;
  for(u32 elapsed = 0; elapsed < length && !captureFull(); elapsed = now() - header.startTime) {
    u32 left = length - elapsed;
    sceKernelDelayThread(left < CaptureCheck ? left : CaptureCheck);
  }
  stopCapture(&header.recordCount, &header.dropped);
  header.endTime = now();
  noticeMarker(0);
  header.endVcount = vblankCount();
  header.cpuMhzEnd = firmware.cpuClock ? firmware.cpuClock() : 0;
  header.busMhzEnd = firmware.busClock ? firmware.busClock() : 0;
  if(header.dropped) header.flags |= ProfileFull;
  sceKernelChangeThreadPriority(0, WritingPriority);
  readParamSfo();
  copyString(header.discId, discId, sizeof(header.discId));
  copyString(header.title, title, sizeof(header.title));
  char name[32], extra[32];
  Text detail = {extra, sizeof(extra), 0};
  extra[0] = 0;
  if(header.dropped) textFormat(&detail, "BUFFER FILLED: %lu LOST", header.dropped);
  else textFormat(&detail, "%lu RECORDS", header.recordCount);
  showResult("capture", writeCapture(&header, records, name, sizeof(name)), name, extra);
  freeBuffer();
  sceKernelChangeThreadPriority(0, WaitingPriority);
}

//Sleeps; while the hook sees no flips and there's text to show, wakes every 10 ms to draw it the other way.
static void rest(u32 microseconds) {
  u32 start = now();
  for(u32 elapsed = 0; elapsed < microseconds; elapsed = now() - start) {
    u32 left = microseconds - elapsed;
    if(!noticeWantsFallback()) {
      sceKernelDelayThread(left);
      return;
    }
    noticeFallback();
    sceKernelDelayThread(left < FallbackInterval ? left : FallbackInterval);
  }
}

static int hooksMissing(void) {
  int missing = !monitorAvailable;
  for(int kind = 1; kind < KindCount; kind++) {
    if(kind != KindGeSignal && kind != KindGeFinish && kind != KindVblank && !hookStatus[kind].installed) missing++;
  }
  return missing;
}

//For the game's first seconds on screen: the plugin is on, for which game, in which mode, and the first thing in the
//way if anything is.
static int startShown, startShownWithDevice;
static void showStart(void) {
  startShown = 1;
  startShownWithDevice = device != 0;
  char line2[32], line3[32];
  Text second = {line2, sizeof(line2), 0}, third = {line3, sizeof(line3), 0};
  line2[0] = line3[0] = 0;
  textFormat(&second, "%s %s", discId, settings.dry ? "DRY RUN" : "CAPTURE");
  int missing = hooksMissing();
  if(!device) textFormat(&third, "NO PHOBOS FOLDER YET");
  else if(!firmware.patchSyscall) textFormat(&third, "NO HOOKS: SEE TRACE.TXT");
  else if(missing) textFormat(&third, "%d HOOKS MISSING: SEE TRACE", missing);
  noticeShow("PHOBOS PROFILER ON", line2, line3, StartNotice);
}

static int discKnown(void) {
  return discId[0] != 'U' || discId[1] != 'N' || discId[2] != 'K';
}

//The control thread starts with the plugin, as the game boots, before the game runs: ARK reads the game's PARAM.SFO
//from a disc that may not be readable yet (the owner's PSP gave no disc ID this early), and nothing is on the screen.
//So the trace goes out before ARK is asked, and the start notice waits for the game's first flip through the hook
//(firstFlipSeen()), unless that hook isn't in place: then it's shown now, for the other way of drawing to show.
static void controlStart(void) {
  traceLine("control thread running");
  ensureDevice();
  if(!settings.read) traceSettings();
  flushTrace();
  readGameInfo();
  traceLine("disc id %s, title \"%s\"%s", discId, title, firmware.getInitParam ? "" : " (no sctrlGetInitPARAM)");
  noticeEnable(settings.notices);
  snapshotMemory(memoryAtStart);
  if(!hookStatus[KindSetFrameBuf].installed) showStart();
  flushTrace();
}

//The game's first flip through the hook: the game runs, its disc readable, so the disc ID is asked for again if the
//start didn't get it; and the start notice is shown, the game's next flip drawing it.
static void firstFlipSeen(void) {
  traceLine("first flip through the sceDisplaySetFrameBuf hook, %lu ms after the start",
            (noticeFirstFlip() - pluginStart) / 1000);
  if(!discKnown()) {
    readGameInfo();
    traceLine("disc id %s, title \"%s\" (asked again at the first flip)", discId, title);
  }
  if(!startShown) showStart();
}

//A run: the countdown notice through the delay when the buttons started it, the dry run or the capture, the trace.
static void trigger(int timed) {
  ensureDevice();
  traceLine("trigger (%s): %s", timed ? "timed" : "buttons", settings.dry ? "dry run" : "capture");
  if(!timed) {
    char line[32];
    Text text = {line, sizeof(line), 0};
    line[0] = 0;
    textFormat(&text, "%s IN %d S", settings.dry ? "DRY RUN" : "CAPTURE", settings.delay);
    noticeShow(line, "", "", CountdownNotice);
    waitForRelease();
    rest(settings.delay * Second);
  }
  if(settings.dry) dryRun(timed);
  else capture(timed);
  flushTrace();
}

static int controlThread(SceSize args, void* argp) {
  (void)args, (void)argp;
  controlStart();
  int timedDone = 0, earlyTrace = 1, flipTraced = 0, held = 0;
  u32 lastRunEnd = pluginStart;
  for(;;) {
    rest(PollInterval);
    if(!flipTraced && noticeFirstFlip()) {
      firstFlipSeen();
      flipTraced = 1;
    }
    if(earlyTrace && (s32)(now() - pluginStart) >= EarlyTrace) {
      if(!flipTraced) traceLine("no flip through the sceDisplaySetFrameBuf hook yet");
      flushTrace();
      earlyTrace = 0;
      //shown now if no flip came through the hook (the other way draws it), or again if the PHOBOS folder turned up
      //after it was
      if(!startShown || (!startShownWithDevice && device)) showStart();
    }
    int timed = 0;
    if(!timedDone && settings.start >= 0) {
      u32 timedAt = pluginStart + (u32)settings.start * Second;
      if((s32)(now() - timedAt) >= 0) {
        timedDone = 1;
        timed = (s32)(timedAt - lastRunEnd) >= 0;  //skipped if a run was on when its time came
      }
    }
    if(!timed) {
      if(settings.buttons && (readButtons() & settings.buttons) == settings.buttons) held++;
      else held = 0;
      if(held < HeldPolls) continue;
      held = 0;
    }
    trigger(timed);
    lastRunEnd = now();
    waitForRelease();
  }
  return 0;
}

//The trace is written from here too, in the module manager's thread at the game's boot (before the game runs, so not
//at the writing priority): first thing, so trace.txt shows the module started even if nothing after works; then
//with the functions and hooks found; and once more if the control thread, which writes the rest, can't run. Nothing
//after the thread's start is traced here: the thread, at a higher priority, may already be running.
int module_start(SceSize args, void* argp) {
  (void)args, (void)argp;
  pluginStart = now();
  traceStart(pluginStart);
  traceLine("---- Phobos GE profiler %s: module_start", ProfileVersion);
  traceWrite(ensureDevice());
  findFirmware();
  installHooks();
  SceUID thread = sceKernelCreateThread("PhobosGeProfile", controlThread, WaitingPriority, ThreadStack, 0, 0);
  traceLine(thread >= 0 ? "control thread made (0x%08X)" : "control thread not made (0x%08X)", thread);
  traceWrite(ensureDevice());
  if(thread >= 0) {
    int started = sceKernelStartThread(thread, 0, 0);
    if(started < 0) {
      traceLine("control thread not started (0x%08X)", started);
      traceWrite(ensureDevice());
    }
  }
  return 0;
}
