//psp-ge-profile's record path on the Mac: hooks.c and text.c with the firmware faked here, driven by a scripted game
//(lists queued, stall updates, syncs during which the GE's FINISH interrupt arrives, a SIGNAL, flips, vblank waits
//during which the vblank interrupt arrives), the records checked as they're made, then written as a capture through
//profile.h's structs for decoder/geprof.py to read back. Also: idle hooks record nothing, the dry run counts, a call
//asleep when its capture ends writes nothing, a full buffer drops and counts, the cost measure leaves the state as it
//found it, and the text formatter. `make hosttest` in this folder builds and runs it.

#include "plugin.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Firmware firmware;

//The fake firmware: the time (near the 32-bit wrap, which the decoder must handle), the running thread, interrupts,
//the vblank count, the GE's queue as sceGeDrawSync(1) reports it.
static u32 fakeNow = 0xFFFF0000u;
static SceUID fakeThread = 0x04A1B2C3;
static int interruptDepth, interruptsOff;
static u32 fakeVcount = 1000;
static int geQueued, geStalled;
static u32 nextId = 0x08A00000, lastList;
static MonitorFunction monitorBefore, monitorAfter;
static int stopInsideWait;
static u32 insideCount, insideDropped;
volatile u32 fakeGeNextCommand;

enum { Period = 16683, List = 0x08900000 };

//The SIGNAL command the test's lists have before their offset 0x800.
u32 fakeWord(u32 physical) {
  return physical == lastList + 0x800 - 8 ? 0x0E01BEEF : 0;
}

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

static int nothing(void) { return 0; }
static u32 getVcount(void) { return fakeVcount; }

//The syscall table: what the game calls, found by the original's address.
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

static void* syscall(void* original) {
  for(int n = 0; n < tableSize; n++) if(table[n].original == original) return table[n].hook;
  fprintf(stderr, "hosttest: not hooked\n");
  exit(1);
}

static void registerMonitor(MonitorFunction before, MonitorFunction after) { monitorBefore = before, monitorAfter = after; }
static void releaseMonitor(void) { monitorBefore = monitorAfter = 0; }

//Each function has a fake of its own, so the syscall table tells them apart.
static int unusedFakes[32];
void* findFunction(const char* module, const char* library, u32 nid) {
  (void)module, (void)library;
  switch(nid) {
  case 0xAB49E76A: return (void*)geEnQueue;
  case 0xE0D68148: return (void*)geUpdateStall;
  case 0xB287BD61: return (void*)geDrawSync;
  case 0x289D82FE: return (void*)setFrameBuf;
  case 0x984C27E7: return (void*)waitVblankStart;
  }
  static int next;
  (void)nothing;
  return &unusedFakes[next++ % 32];  //never called by the test
}

typedef int (*EnQueueFunction)(const void*, void*, int, void*);
typedef int (*StallFunction)(int, void*);
typedef int (*SyncFunction)(int);
typedef int (*FrameBufFunction)(void*, int, int, int);
typedef int (*WaitFunction)(void);

//A frame: a list built while the GE runs it (three stall updates, the GE stalled at the first and third), a SIGNAL
//from it, a sync during which it finishes, a flip, the wait for the next vblank. Ten records.
static void frame(int n) {
  u32 list = List + (n & 1) * 0x10000;
  fakeNow += 1000;
  int id = ((EnQueueFunction)syscall((void*)geEnQueue))((void*)(uintptr_t)list, (void*)(uintptr_t)list, 0, 0);
  for(int k = 1; k <= 3; k++) {
    fakeNow += 500;
    geStalled = k & 1;
    ((StallFunction)syscall((void*)geUpdateStall))(id, (void*)(uintptr_t)(list + k * 0x400));
  }
  interrupt(GeInterrupt, 0 * 2, 0xBEEF, list + 0x800, 5);
  ((SyncFunction)syscall((void*)geDrawSync))(0);
  ((FrameBufFunction)syscall((void*)setFrameBuf))((void*)(uintptr_t)(0x04000000 + (n & 1) * 0x88000), 512, 3, 1);
  ((WaitFunction)syscall((void*)waitVblankStart))();
}

static void writeCapture(const char* path, ProfileRecord* records, u32 count, u32 start, u32 end) {
  ProfileHeader header;
  memset(&header, 0, sizeof(header));
  memcpy(header.magic, "PHGEPROF", 8);
  header.format = ProfileFormat;
  header.headerSize = ProfileHeaderSize;
  header.recordSize = ProfileRecordSize;
  header.recordCount = count;
  header.flags = ProfileMonitor | ProfileGeSampled;
  strcpy(header.pluginVersion, ProfileVersion);
  strcpy(header.discId, "UCUS98653");
  strcpy(header.title, "hosttest");
  header.startTime = start;
  header.endTime = end;
  header.cpuMhz = header.cpuMhzEnd = 222;
  header.busMhz = header.busMhzEnd = 111;
  header.hooks = hooksInstalledMask();
  header.capacity = 4096;
  u32 uids[64];
  int threads = threadTable(uids, 64);
  header.threadTableOffset = ProfileHeaderSize + count * ProfileRecordSize;
  header.threadCount = threads;
  FILE* file = fopen(path, "wb");
  assert(file);
  fwrite(&header, sizeof(header), 1, file);
  fwrite(records, sizeof(ProfileRecord), count, file);
  for(int n = 0; n < threads; n++) {
    ProfileThread thread;
    memset(&thread, 0, sizeof(thread));
    thread.uid = uids[n];
    strcpy(thread.name, n ? "render" : "interrupt");
    fwrite(&thread, sizeof(thread), 1, file);
  }
  fclose(file);
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

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "hosttest.bin";
  firmware.patchSyscall = patchSyscall;
  firmware.querySystemCall = querySystemCall;
  firmware.registerSubIntrMonitor = registerMonitor;
  firmware.releaseSubIntrMonitor = releaseMonitor;
  firmware.displayGetVcount = getVcount;
  testText();
  installHooks();
  assert(hookStatus[KindGeEnQueue].installed && hookStatus[KindGeEnQueue].syscall >= 0x2000);
  assert(monitorAvailable && hooksInstalledMask() >> 31 && (hooksInstalledMask() >> KindSetFrameBuf & 1));

  //idle: straight through, the monitor not registered
  frame(0);
  assert(!monitorBefore && interruptDepth == 0);

  //dry: counted, nothing recorded
  startCounting();
  frame(1);
  u32 counts[KindCount];
  u32 total = stopCounting(counts);
  assert(counts[KindGeEnQueue] == 1 && counts[KindGeUpdateStallAddr] == 3 && counts[KindGeFinish] == 1);
  assert(counts[KindGeSignal] == 1 && counts[KindVblank] == 1 && counts[KindSetFrameBuf] == 1 && total == 10);
  assert(!monitorBefore);

  //a capture of three frames
  static ProfileRecord buffer[4096];
  u32 start = fakeNow;
  startCapture(buffer, 4096);
  for(int n = 2; n < 5; n++) frame(n);
  u32 count, dropped;
  stopCapture(&count, &dropped);
  u32 end = fakeNow;
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
  writeCapture(path, buffer, count, start, end);

  //a game thread asleep in a vblank wait when its capture ends: its record keeps no return, and the vblank that
  //wakes it isn't recorded
  static ProfileRecord second[8];
  fakeThread = 0x05000001;
  startCapture(second, 8);
  stopInsideWait = 1;
  ((WaitFunction)syscall((void*)waitVblankStart))();
  assert(insideCount == 1 && second[0].kind == KindWaitVblankStart && second[0].exit == 0 && second[1].kind == 0);

  //a full buffer: the rest dropped and counted
  static ProfileRecord small[4];
  startCapture(small, 4);
  frame(5);
  assert(captureFull());
  u32 smallCount, smallDropped;
  stopCapture(&smallCount, &smallDropped);
  assert(smallCount == 4 && smallDropped == 6 && interruptDepth == 0);

  //the cost measure, which runs only while idle, leaves it idle
  u32 idleNs, captureNs, monitorNs;
  measureCosts(&idleNs, &captureNs, &monitorNs);
  frame(6);
  assert(!monitorBefore && interruptDepth == 0 && !interruptsOff);

  printf("hosttest: all checks passed; a capture of %u records written to %s\n", count, path);
  return 0;
}
