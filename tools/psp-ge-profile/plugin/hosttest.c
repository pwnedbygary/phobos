//psp-ge-profile's plugin on the Mac: main.c, hooks.c and text.c with the firmware faked here, the memory stick and
//the disc being folders in a temporary one. A scripted game (lists queued, stall updates, syncs during which the
//GE's FINISH interrupt arrives, a SIGNAL, flips, vblank waits during which the vblank interrupt arrives) plays
//whenever the plugin's control thread sleeps. Checked: the module's start and the hooks it installs, the settings
//file, idle hooks recording nothing, the dry run (its counts and its report), a capture (every record, and the file,
//which the decoder's tests read back), a call asleep when its capture ends writing nothing, a full buffer, too little
//memory, the disc ID from PARAM.SFO when ARK can't give it, the formatter; and, all along, that no file is touched
//while the monitor is on (a capture or a dry run's window), that files are written only at the writing priority,
//and that nothing is written outside ms0:/PHOBOS. `make hosttest` in this folder builds and runs it.

#include "main.c"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

//Time (near the 32-bit wrap, which the decoder must handle), threads, interrupts.
static u32 fakeNow = 0xFFF00000u;
static SceUID fakeThread = 0x04A1B2C3;
static int interruptDepth, interruptsOff, currentPriority, threadCreated;

unsigned int sceKernelGetSystemTimeLow(void) { return fakeNow; }
int sceKernelGetThreadId(void) { return fakeThread; }
unsigned int sceKernelCpuSuspendIntr(void) {
  int was = interruptsOff;
  interruptDepth++;
  interruptsOff = 1;
  return was;
}
void sceKernelCpuResumeIntr(unsigned int flags) {
  interruptDepth--;
  interruptsOff = flags;
}
SceUID sceKernelCreateThread(const char* name, SceKernelThreadEntry entry, int priority, int stack, SceUInt attr,
                             SceKernelThreadOptParam* option) {
  (void)name, (void)entry, (void)stack, (void)attr, (void)option;
  threadCreated = 1;
  currentPriority = priority;
  return 0x0600000;
}
int sceKernelStartThread(SceUID thread, SceSize size, void* argp) { (void)thread, (void)size, (void)argp; return 0; }
int sceKernelChangeThreadPriority(SceUID thread, int priority) {
  assert(thread == 0);
  currentPriority = priority;
  return 0;
}
int sceKernelReferThreadStatus(SceUID thread, SceKernelThreadInfo* info) {
  assert(info->size == sizeof(*info));
  if(thread != 0x04A1B2C3) return -1;
  strcpy(info->name, "render");
  return 0;
}

//The memory stick and the disc: "ms0:/X" is <root>/ms0/X.
static char root[256];
static char written[32][96];
static int writtenCount;
static MonitorFunction monitorBefore, monitorAfter;

static void hostPath(char* out, const char* path) {
  const char* colon = strchr(path, ':');
  assert(colon);
  snprintf(out, 512, "%s/%.*s%s", root, (int)(colon - path), path, colon + 1);
}

static void noteWrite(const char* path) {
  assert(!monitorBefore);  //never while a capture or a dry run's window is on
  assert(writtenCount < 32);
  snprintf(written[writtenCount++], sizeof(written[0]), "%s", path);
}

int sceIoGetstat(const char* file, SceIoStat* status) {
  (void)status;
  assert(!monitorBefore);
  char path[512];
  hostPath(path, file);
  struct stat s;
  return stat(path, &s) == 0 ? 0 : -1;
}

SceUID sceIoOpen(const char* file, int flags, SceMode mode) {
  assert(!monitorBefore);
  char path[512];
  hostPath(path, file);
  int host = (flags & PSP_O_WRONLY) ? O_WRONLY : O_RDONLY;
  if(flags & PSP_O_CREAT) host |= O_CREAT;
  if(flags & PSP_O_TRUNC) host |= O_TRUNC;
  if(flags & PSP_O_EXCL) host |= O_EXCL;
  if(flags & PSP_O_WRONLY) {
    noteWrite(file);
    assert(currentPriority == WritingPriority);
  }
  int fd = open(path, host, mode);
  return fd >= 0 ? fd : -1;
}

int sceIoRead(SceUID fd, void* data, SceSize size) { return read(fd, data, size); }
int sceIoWrite(SceUID fd, const void* data, SceSize size) {
  assert(!monitorBefore && currentPriority == WritingPriority);
  return write(fd, data, size);
}
int sceIoClose(SceUID fd) { return close(fd); }
int sceIoMkdir(const char* dir, SceMode mode) {
  noteWrite(dir);
  char path[512];
  hostPath(path, dir);
  return mkdir(path, mode);
}

static void writeFile(const char* psp, const void* data, size_t size) {
  char path[512];
  hostPath(path, psp);
  FILE* file = fopen(path, "wb");
  assert(file);
  fwrite(data, 1, size, file);
  fclose(file);
}

static int readFile(const char* psp, char* data, size_t size) {
  char path[512];
  hostPath(path, psp);
  FILE* file = fopen(path, "rb");
  if(!file) return -1;
  int length = fread(data, 1, size - 1, file);
  data[length] = 0;
  fclose(file);
  return length;
}

//The firmware's other functions.
static u32 fakeVcount = 1000, fakeMaxFree = 1536 * 1024, nextId = 0x08A00000, lastList;
static int geQueued, geStalled, initParamWorks = 1, flushes, stopInsideWait, blocks;
static u32 insideCount, insideDropped;
static void* blockMemory;
volatile u32 fakeGeNextCommand;

enum { Period = 16683, List = 0x08900000 };

//The SIGNAL command the test's lists have before their offset 0x800.
u32 fakeWord(u32 physical) {
  return physical == lastList + 0x800 - 8 ? 0x0E01BEEF : 0;
}

//An interrupt arriving: the monitor around the game's handler, as sceKernelCallSubIntrHandler calls it.
static void interrupt(int number, int sub, int arg2, int arg3, int handlerTime) {
  if(monitorBefore) monitorBefore(number, sub, arg2, 0x1234, arg3, 0x08804000, 0);
  fakeNow += handlerTime;
  if(monitorAfter) monitorAfter(number, sub, arg2, 0x1234, arg3, 0, 0);
}

static int geEnQueue(const void* list, void* stall, int cbid, void* arg) {
  (void)stall, (void)cbid, (void)arg;
  fakeNow += 8;
  geQueued = 1;
  lastList = (u32)(uintptr_t)list;
  fakeGeNextCommand = lastList;
  return nextId += 0x40;
}

static int geUpdateStall(int id, void* stall) {
  (void)id, (void)stall;
  fakeNow += 3;
  return 0;
}

//mode 1 answers; mode 0 sleeps until the queue empties, the FINISH interrupt arriving meanwhile.
static int geDrawSync(int mode) {
  if(mode == 1) return geQueued ? (geStalled ? GeStalled : GeDrawing) : 0;
  fakeNow += 2000;
  interrupt(GeInterrupt, 0 * 2 + 1, 0, lastList + 0xC00, 20);
  geQueued = 0;
  fakeNow += 10;
  return 0;
}

static int setFrameBuf(void* top, int width, int format, int sync) {
  (void)top, (void)width, (void)format, (void)sync;
  fakeNow += 4;
  return 0;
}

//Sleeps until the next vblank, the vblank interrupt arriving meanwhile; the capture can be ended from inside, as the
//control thread would while the game sleeps here.
static int waitVblankStart(void) {
  if(stopInsideWait) {
    stopCapture(&insideCount, &insideDropped);
    stopInsideWait = 0;
  }
  fakeNow += Period - fakeNow % Period;
  fakeVcount++;
  interrupt(VblankInterrupt, 0x13, fakeVcount, 0, 0);
  fakeNow += 15;
  return 0;
}

static struct { void* original; void* hook; } table[64];
static int tableSize;

static void patchSyscall(void* original, void* replacement) {
  table[tableSize].original = original;
  table[tableSize++].hook = replacement;
}

static int querySystemCall(void* function) {
  for(int n = 0; n < tableSize; n++) if(table[n].hook == function) return 0x2000 + n;
  return -1;
}

static void* hooked(void* original) {
  for(int n = 0; n < tableSize; n++) if(table[n].original == original) return table[n].hook;
  fprintf(stderr, "hosttest: not hooked\n");
  exit(1);
}

static void registerMonitor(MonitorFunction before, MonitorFunction after) { monitorBefore = before, monitorAfter = after; }
static void releaseMonitor(void) { monitorBefore = monitorAfter = 0; }
static void flushCache(void) { flushes++; }
static int getInitParam(const char* name, u16* type, u32* length, void* buffer) {
  (void)type;
  if(!initParamWorks) return -1;
  const char* value = !strcmp(name, "DISC_ID") ? "UCUS98653" : !strcmp(name, "TITLE") ? "God of War: Chains of Olympus" : 0;
  if(!value || *length < strlen(value) + 1) return -1;
  strcpy(buffer, value);
  *length = strlen(value) + 1;
  return 0;
}
static int msIsEf(void) { return 0; }
static int henVersion(void) { return 0x1004; }
static int henMinorVersion(void) { return 0x3; }
static SceUID allocPartitionMemory(SceUID partition, const char* name, int type, SceSize size, void* address) {
  (void)name, (void)address;
  assert(partition == 1 && type == PSP_SMEM_High && !(size & 0xFFF));
  if(size > fakeMaxFree || blocks) return -1;
  blockMemory = calloc(1, size);
  blocks++;
  return 77;
}
static int freePartitionMemory(SceUID block) { assert(block == 77 && blocks == 1); free(blockMemory); blocks--; return 0; }
static void* getBlockHeadAddr(SceUID block) { assert(block == 77); return blockMemory; }
static int queryPartition(int partition, PartitionInfo* info) {
  assert(info->size == 16);
  if(partition == 1) *info = (PartitionInfo){16, 0x88000000, 3 << 20, 0xC};
  else if(partition == 2) *info = (PartitionInfo){16, 0x08800000, 24 << 20, 0xF};
  else return -1;
  return 0;
}
static SceSize partitionMaxFree(int partition) { assert(partition == 1 || partition == 2); return partition == 1 ? fakeMaxFree : 1 << 20; }
static SceSize partitionTotalFree(int partition) { return partitionMaxFree(partition) + 4096; }
static int getModel(void) { return 2; }
static int devkitVersion(void) { return 0x06060110; }
static int ctrlPeek(CtrlData* data, int count) { assert(count == 1); data->buttons = 0; return 1; }
static int cpuClock(void) { return 222; }
static int busClock(void) { return 111; }
static u32 getVcount(void) { return fakeVcount; }
static int unusedFakes[32];

//ARK's lookup: each function by its NID (none collide), and the module and library asked for checked.
Address sctrlHENFindFunction(const char* module, const char* library, u32 nid) {
  if(!strcmp(library, ArkLibrary)) assert(!strcmp(module, ArkModule));
  if(!strcmp(library, "sceGe_user")) assert(!strcmp(module, "sceGE_Manager"));
  if(!strcmp(library, "sceDisplay")) assert(!strcmp(module, "sceDisplay_Service"));
  switch(nid) {
  case NidPatchSyscall: return (Address)patchSyscall;
  case NidQuerySystemCall: return (Address)querySystemCall;
  case NidGetInitParam: return (Address)getInitParam;
  case NidMsIsEf: return (Address)msIsEf;
  case NidFlushCache: return (Address)flushCache;
  case NidHenVersion: return (Address)henVersion;
  case NidHenMinorVersion: return (Address)henMinorVersion;
  case NidFindFunctionOnSystem: case NidPatchSyscallOld: case NidPatchSyscallOlder: return 0;
  case NidAllocPartitionMemory: return (Address)allocPartitionMemory;
  case NidFreePartitionMemory: return (Address)freePartitionMemory;
  case NidGetBlockHeadAddr: return (Address)getBlockHeadAddr;
  case NidPartitionMaxFreeMemSize: return (Address)partitionMaxFree;
  case NidPartitionTotalFreeMemSize: return (Address)partitionTotalFree;
  case NidQueryMemoryPartitionInfo: return (Address)queryPartition;
  case NidGetModel: return (Address)getModel;
  case NidDevkitVersion: return (Address)devkitVersion;
  case NidRegisterSubIntrMonitor: return (Address)registerMonitor;
  case NidReleaseSubIntrMonitor: return (Address)releaseMonitor;
  case NidCtrlPeekBufferPositive: return (Address)ctrlPeek;
  case NidCpuClockInt: return (Address)cpuClock;
  case NidBusClockInt: return (Address)busClock;
  case NidDisplayGetVcount: return (Address)getVcount;
  case 0xAB49E76A: return (Address)geEnQueue;
  case 0xE0D68148: return (Address)geUpdateStall;
  case 0xB287BD61: return (Address)geDrawSync;
  case 0x289D82FE: return (Address)setFrameBuf;
  case 0x984C27E7: return (Address)waitVblankStart;
  }
  static int next;
  return (Address)&unusedFakes[next++ % 32];  //found, but never called by the test
}

typedef int (*EnQueueFunction)(const void*, void*, int, void*);
typedef int (*StallFunction)(int, void*);
typedef int (*SyncFunction)(int);
typedef int (*FrameBufFunction)(void*, int, int, int);
typedef int (*WaitFunction)(void);

//A frame: a list built while the GE runs it (three stall updates, the GE stalled at the first and third), a SIGNAL
//from it, a sync during which it finishes, a flip, the wait for the next vblank. Ten records.
static int frames;
static void frame(void) {
  u32 list = List + (frames & 1) * 0x10000;
  fakeNow += 1000;
  int id = ((EnQueueFunction)hooked((void*)geEnQueue))((void*)(uintptr_t)list, (void*)(uintptr_t)list, 0, 0);
  for(int k = 1; k <= 3; k++) {
    fakeNow += 500;
    geStalled = k & 1;
    ((StallFunction)hooked((void*)geUpdateStall))(id, (void*)(uintptr_t)(list + k * 0x400));
  }
  interrupt(GeInterrupt, 0 * 2, 0xBEEF, list + 0x800, 5);
  ((SyncFunction)hooked((void*)geDrawSync))(0);
  ((FrameBufFunction)hooked((void*)setFrameBuf))((void*)(uintptr_t)(0x04000000 + (frames & 1) * 0x88000), 512, 3, 1);
  ((WaitFunction)hooked((void*)waitVblankStart))();
  frames++;
}

//The control thread's sleeps: the game plays meanwhile.
static int gamePlaying;
int sceKernelDelayThread(SceUInt delay) {
  u32 until = fakeNow + delay;
  if(gamePlaying) while((s32)(until - fakeNow) > 0) frame();
  else fakeNow = until;
  return 0;
}

static void testText(void) {
  char buffer[64];
  Text text = {buffer, sizeof(buffer), 0};
  textFormat(&text, "[%5d|%-4s|%08X|%x|%u|%03d|%c%%|%-6u]", -42, "ab", 0xBEEFu, 255u, 4000000000u, 7, 'z', 12u);
  assert(!strcmp(buffer, "[  -42|ab  |0000BEEF|ff|4000000000|007|z%|12    ]"));
  Text tiny = {buffer, 6, 0};
  textFormat(&tiny, "%s", "overflowing");
  assert(!strcmp(buffer, "overf"));
  assert(textEqual("Mode", "MODE") && !textEqual("mode", "modes"));
}

//A PARAM.SFO with DISC_ID and TITLE, laid out as the firmware's.
static void writeParamSfo(const char* id, const char* name) {
  u8 sfo[512];
  memset(sfo, 0, sizeof(sfo));
  const char keys[] = "DISC_ID\0TITLE\0\0\0";
  u32 keyTable = 20 + 2 * 16, dataTable = keyTable + sizeof(keys) - 1;
  memcpy(sfo, "\0PSF\x01\x01\0\0", 8);
  memcpy(sfo + 8, &keyTable, 4);
  memcpy(sfo + 12, &dataTable, 4);
  sfo[16] = 2;
  u16 keyOffsets[2] = {0, 8};
  u32 lengths[2] = {strlen(id) + 1, strlen(name) + 1}, maxima[2] = {16, 128}, offsets[2] = {0, 16};
  for(int n = 0; n < 2; n++) {
    u8* entry = sfo + 20 + n * 16;
    memcpy(entry, &keyOffsets[n], 2);
    entry[2] = 0x04, entry[3] = 0x02;
    memcpy(entry + 4, &lengths[n], 4);
    memcpy(entry + 8, &maxima[n], 4);
    memcpy(entry + 12, &offsets[n], 4);
  }
  memcpy(sfo + keyTable, keys, sizeof(keys) - 1);
  strcpy((char*)sfo + dataTable, id);
  strcpy((char*)sfo + dataTable + 16, name);
  writeFile("disc0:/PSP_GAME/PARAM.SFO", sfo, dataTable + 16 + 128);
}

static void testRecords(void) {
  //idle: straight through, the monitor not registered
  frame();
  assert(!monitorBefore && interruptDepth == 0);

  //counting: nothing recorded
  startCounting();
  frame();
  u32 counts[KindCount];
  u32 total = stopCounting(counts);
  assert(counts[KindGeEnQueue] == 1 && counts[KindGeUpdateStallAddr] == 3 && counts[KindGeFinish] == 1);
  assert(counts[KindGeSignal] == 1 && counts[KindVblank] == 1 && counts[KindSetFrameBuf] == 1 && total == 10);

  //three frames captured, every record checked
  static ProfileRecord buffer[64];
  startCapture(buffer, 64);
  for(int n = 0; n < 3; n++) frame();
  u32 count, dropped;
  stopCapture(&count, &dropped);
  assert(count == 30 && dropped == 0 && interruptDepth == 0 && !interruptsOff);
  int kinds[KindCount] = {0};
  for(u32 n = 0; n < count; n++) {
    ProfileRecord* r = &buffer[n];
    kinds[r->kind]++;
    assert(r->exit != 0);
    int interruptKind = r->kind == KindGeSignal || r->kind == KindGeFinish || r->kind == KindVblank;
    assert(r->thread == (interruptKind ? 0 : 1));
    if(r->kind == KindGeEnQueue) assert((r->a == List || r->a == List + 0x10000) && r->result >= 0x08A00000 && r->c == 0);
    if(r->kind == KindGeUpdateStallAddr) {
      assert((r->ge & 3) == GeStalled || (r->ge & 3) == GeDrawing);
      assert((r->ge & ~3u) == List || (r->ge & ~3u) == List + 0x10000);
    }
    if(r->kind == KindGeSignal) assert(r->a == 0 && r->b == 0xBEEF && (r->c & 0xFFFF) == 0x0800 && r->result == 0x0E01BEEF);
    if(r->kind == KindGeFinish) assert(r->a == 0 && r->exit - r->entry == 20 && (r->c & 0xFFFF) == 0x0C00);
    if(r->kind == KindSetFrameBuf) assert(r->b == 512 && r->c == (3 | 1 << 8) && (r->a == 0x04000000 || r->a == 0x04088000));
    if(r->kind == KindGeDrawSync) assert(r->a == 0 && r->exit - r->entry > 2000);
    if(r->kind == KindVblank) assert((r->a & 0xFFFF) == r->vcount && r->b == 0x13);
  }
  assert(kinds[KindGeEnQueue] == 3 && kinds[KindGeUpdateStallAddr] == 9 && kinds[KindGeFinish] == 3);
  assert(kinds[KindGeSignal] == 3 && kinds[KindVblank] == 3 && kinds[KindWaitVblankStart] == 3);
  assert(kinds[KindGeDrawSync] == 3 && kinds[KindSetFrameBuf] == 3);

  //a game thread asleep in a vblank wait when its capture ends: its record keeps no return, and the vblank that
  //wakes it isn't recorded
  static ProfileRecord second[8];
  SceUID thread = fakeThread;
  fakeThread = 0x05000001;
  startCapture(second, 8);
  stopInsideWait = 1;
  ((WaitFunction)hooked((void*)waitVblankStart))();
  assert(insideCount == 1 && second[0].kind == KindWaitVblankStart && second[0].exit == 0 && second[1].kind == 0);
  fakeThread = thread;

  //a full buffer: the rest dropped and counted
  static ProfileRecord small[4];
  startCapture(small, 4);
  frame();
  assert(captureFull());
  u32 smallCount, smallDropped;
  stopCapture(&smallCount, &smallDropped);
  assert(smallCount == 4 && smallDropped == 6 && interruptDepth == 0);

  //the cost measure leaves the hooks idle
  u32 idleNs, captureNs, monitorNs;
  measureCosts(&idleNs, &captureNs, &monitorNs);
  frame();
  assert(!monitorBefore && interruptDepth == 0 && !interruptsOff);
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "hosttest.bin";
  testText();
  snprintf(root, sizeof(root), "%s", "/tmp/psp-ge-profile-hosttest-XXXXXX");
  assert(mkdtemp(root));
  char path[512];
  hostPath(path, "ms0:/");
  mkdir(path, 0777);
  hostPath(path, "ms0:/PHOBOS");
  mkdir(path, 0777);
  hostPath(path, "disc0:/");
  mkdir(path, 0777);
  hostPath(path, "disc0:/PSP_GAME");
  mkdir(path, 0777);
  const char settingsText[] =
    "# a comment\r\n  mode = dry   # trailing\r\nbuttons = L+R+START\nbuttons = L+BOGUS\ndelay=2\nstart = off\n"
    "length = 3\nbuffer = 512\nkeep = 256\nunknown = 5\n= 7\nlength\n";
  writeFile("ms0:/PHOBOS/prof.txt", settingsText, sizeof(settingsText) - 1);

  //the module's start: the functions found, the hooks in, the control thread made
  assert(module_start(0, 0) == 0);
  assert(threadCreated && currentPriority == WaitingPriority && flushes == 1);
  assert(hookStatus[KindGeEnQueue].installed && hookStatus[KindGeEnQueue].syscall >= 0x2000);
  assert(hookStatus[KindWaitVblankStartMultiCB].installed && monitorAvailable);
  assert(hooksInstalledMask() >> 31 && (hooksInstalledMask() >> KindSetFrameBuf & 1));

  testRecords();

  //the control thread's start, step by step (its loop waits for buttons for ever)
  device = exists("ms0:/PHOBOS") ? "ms0:" : exists("ef0:/PHOBOS") ? "ef0:" : 0;
  assert(device && !strcmp(device, "ms0:"));
  readSettings();
  assert(settings.read && settings.dry && settings.buttons == (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START));
  assert(settings.delay == 2 && settings.start == -1 && settings.length == 3 && settings.bufferKb == 512);
  assert(settings.keepKb == 256 && settings.partition == 1);
  readGameInfo();
  assert(!strcmp(discId, "UCUS98653") && !strcmp(title, "God of War: Chains of Olympus"));
  snapshotMemory(memoryAtStart);
  assert(memoryAtStart[1].valid && memoryAtStart[2].valid && !memoryAtStart[3].valid);

  //a dry run, the game playing through its window
  gamePlaying = 1;
  dryRun(1);
  assert(currentPriority == WaitingPriority && !monitorBefore);
  char report[8192];
  assert(readFile("ms0:/PHOBOS/PROF/UCUS98653-1.txt", report, sizeof(report)) > 0);
  assert(strstr(report, "dry run") && strstr(report, "game: UCUS98653  God of War: Chains of Olympus"));
  assert(strstr(report, "PSP-3000") && strstr(report, "firmware 0x06060110") && strstr(report, "ARK 0x1004.3"));
  assert(strstr(report, "mode dry, buttons L+R+START, delay 2 s, start off, length 3 s, buffer 512 KB, keep 256 KB"));
  assert(strstr(report, "window: 3.0 s"));
  char line[96];
  snprintf(line, sizeof(line), "%-34s %-24s", "sceGeListEnQueue", "in place, syscall 0x2000");
  assert(strstr(report, line));
  snprintf(line, sizeof(line), "%-34s %s", "GE FINISH callbacks", "monitor");
  assert(strstr(report, line));
  assert(strstr(report, "   1 at 0x88000000, attr C:   3072 |   1536   1540 |   1536   1540"));
  assert(strstr(report, "a capture's buffer now: 512 KB in partition 1"));
  int enqueues = 0;
  sscanf(strstr(report, "sceGeListEnQueue "), "sceGeListEnQueue %*s %*s %*s %*s %d", &enqueues);
  assert(enqueues >= 170 && enqueues <= 182);  //3 s of frames

  //a capture, written for the decoder's tests to read back
  settings.dry = 0;
  capture(0);
  assert(blocks == 0 && currentPriority == WaitingPriority && !monitorBefore);
  static char data[1 << 20];
  hostPath(path, "ms0:/PHOBOS/PROF/UCUS98653-2.bin");
  FILE* file = fopen(path, "rb");
  assert(file);
  size_t size = fread(data, 1, sizeof(data), file);
  fclose(file);
  ProfileHeader* header = (ProfileHeader*)data;
  assert(!memcmp(header->magic, "PHGEPROF", 8) && header->format == ProfileFormat && header->captureNumber == 2);
  assert(header->recordCount >= 1700 && header->recordCount <= 1820 && header->dropped == 0);
  assert(header->capacity == 512 * 1024 / ProfileRecordSize && header->bufferBytes == 512 * 1024);
  assert(header->endTime - header->startTime >= 3000000 && header->endTime - header->startTime < 3020000);
  assert(header->threadCount == 2 && header->cpuMhz == 222 && header->busMhzEnd == 111 && header->model == 2);
  assert(size == ProfileHeaderSize + header->recordCount * ProfileRecordSize + 2 * sizeof(ProfileThread));
  ProfileThread* threads = (ProfileThread*)(data + header->threadTableOffset);
  assert(!strcmp(threads[0].name, "interrupt") && !strcmp(threads[1].name, "render"));
  file = fopen(out, "wb");
  assert(file);
  fwrite(data, 1, size, file);
  fclose(file);

  //too little memory: a note instead
  fakeMaxFree = 300 * 1024;
  capture(0);
  assert(readFile("ms0:/PHOBOS/PROF/UCUS98653-3.txt", report, sizeof(report)) > 0);
  assert(strstr(report, "no capture: partition 1's largest free block is 300 KB; with keep = 256 KB"));
  fakeMaxFree = 1536 * 1024;

  //ARK can't give the disc ID: the disc's PARAM.SFO does, once the capture is over
  initParamWorks = 0;
  strcpy(discId, "UNKNOWN");
  title[0] = 0;
  readGameInfo();
  assert(!strcmp(discId, "UNKNOWN"));
  writeParamSfo("ULES00364", "Super Monkey Ball Adventure");
  capture(0);
  hostPath(path, "ms0:/PHOBOS/PROF/ULES00364-1.bin");
  struct stat s;
  assert(stat(path, &s) == 0 && !strcmp(title, "Super Monkey Ball Adventure"));

  //only ever under ms0:/PHOBOS
  for(int n = 0; n < writtenCount; n++) assert(!strncmp(written[n], "ms0:/PHOBOS/", 12));

  printf("hosttest: all checks passed; a capture of %lu records written to %s (files in %s)\n",
         (unsigned long)header->recordCount, out, root);
  return 0;
}
