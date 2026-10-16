//psp-ge-profile's hooks. The game's GE and display calls are seen by patching their syscalls (ARK's
//sctrlHENPatchSyscall): each hook runs in the game's thread, in the syscall's kernel mode, around the firmware's own
//function, which it calls with the game's arguments and k1 unchanged. The GE's SIGNAL and FINISH callbacks and the
//vertical blanks are seen by the interrupt manager's sub-interrupt monitor, which is registered only while a capture
//or a dry run's window is on, so the game's callbacks are never wrapped or moved.
//
//While idle, a hook checks one word and calls the firmware's function. While capturing, it takes the time, the GE's
//state and the vblank count, reserves a record with interrupts off, and fills in the return time and result after
//the call. Records are reserved with the capture's generation, and a call that returns after its capture ended (a
//thread asleep in a vblank wait, say) writes nothing, so a buffer is never touched once main.c has taken it back.

#include "plugin.h"
#include <pspintrman.h>
#include <pspthreadman.h>

HookStatus hookStatus[HookCount];
int monitorAvailable;

enum { Idle, Counting, Capturing };
static volatile int state = Idle;
static ProfileRecord* records;
static u32 capacity, count, dropped, generation;
static volatile int full;
static u32 counts[KindCount];

//The threads seen in this capture; a record's thread byte is an index here. 0 is interrupt context, and the last
//slot stands for every thread past the table's end.
enum { ThreadSlots = 64 };
static SceUID threads[ThreadSlots];
static int threadCount = 1;
static SceUID lastThread = -1;
static int lastThreadIndex;

static ProfileRecord* pendingCallback;  //the GE callback the monitor saw start, finished by monitorAfter
static u32 pendingGeneration;
static u32 lastVblank = ~0u;

//The firmware's functions behind the hooks, called with the game's own arguments.
static int (*geListEnQueue)(const void* list, void* stall, int cbid, void* arg);
static int (*geListEnQueueHead)(const void* list, void* stall, int cbid, void* arg);
static int (*geListDeQueue)(int id);
static int (*geListUpdateStallAddr)(int id, void* stall);
static int (*geListSync)(int id, int mode);
static int (*geDrawSync)(int mode);
static int (*geBreak)(int mode, void* parameter);
static int (*geContinue)(void);
static int (*geSetCallback)(void* callback);
static int (*geUnsetCallback)(int id);
static int (*displaySetFrameBuf)(void* top, int width, int format, int sync);
static int (*displayWaitVblankStart)(void);
static int (*displayWaitVblankStartCB)(void);
static int (*displayWaitVblank)(void);
static int (*displayWaitVblankCB)(void);
static int (*displayWaitVblankStartMulti)(int vblanks);
static int (*displayWaitVblankStartMultiCB)(int vblanks);

//The GE's state: the firmware's own sceGeDrawSync(1), and when it says a list is drawing or stalled, the GE's next
//command, which that call has just read itself. The GE's registers are read only then: its bus clock is off while
//the queue is empty, and the firmware never reads them blind either.
u32 geState(void) {
  if(!geDrawSync) return GeNotSampled;
  u32 interrupts = sceKernelCpuSuspendIntr();
  int status = geDrawSync(1);
  u32 ge = GeNotSampled;
  if(status == GeDrawing || status == GeStalled) ge = (GeNextCommand & ~3u) | status;
  else if(status == 0) ge = GeIdle;
  sceKernelCpuResumeIntr(interrupts);
  return ge;
}

//A word of a display list, read as the GE read it (uncached), if the address is in the 32 MB of RAM every model has,
//or in VRAM.
static u32 readListWord(u32 address) {
  u32 physical = address & 0x1FFFFFFF;
  if(address & 3) return 0;
  if((physical >= 0x08000000 && physical < 0x0A000000) || (physical >= 0x04000000 && physical < 0x04200000))
    return UncachedWord(physical);
  return 0;
}

static u32 vcount(void) {
  return firmware.displayGetVcount ? firmware.displayGetVcount() : 0;
}

//With interrupts off.
static int threadIndex(SceUID thread) {
  if(thread == lastThread) return lastThreadIndex;
  int index = ThreadSlots - 1;
  for(int n = 1; n < threadCount; n++) {
    if(threads[n] == thread) { index = n; break; }
  }
  if(index == ThreadSlots - 1 && threadCount < ThreadSlots - 1) {
    index = threadCount++;
    threads[index] = thread;
  }
  lastThread = thread;
  lastThreadIndex = index;
  return index;
}

//With interrupts off: the next record, or 0 when the buffer is full.
static ProfileRecord* reserve(void) {
  if(count < capacity) return &records[count++];
  dropped++;
  full = 1;
  return 0;
}

static void fill(ProfileRecord* record, int kind, int thread, u32 vcount, u32 entry, u32 ge, u32 a, u32 b, u32 c) {
  record->kind = kind;
  record->thread = thread;
  record->vcount = vcount;
  record->entry = entry;
  record->exit = 0;
  record->ge = ge;
  record->a = a;
  record->b = b;
  record->c = c;
  record->result = 0;
}

typedef struct { ProfileRecord* record; u32 generation; } Call;

//The hooks' idle path is begin's and end's first lines, inlined: a load and a branch each.
static void __attribute__((noinline)) beginActive(Call* call, int kind, u32 a, u32 b, u32 c) {
  if(state == Counting) { counts[kind]++; return; }
  u32 entry = sceKernelGetSystemTimeLow();
  u32 ge = geState();
  u32 vblanks = vcount();
  SceUID thread = sceKernelGetThreadId();
  u32 interrupts = sceKernelCpuSuspendIntr();
  if(state == Capturing) {
    ProfileRecord* record = reserve();
    if(record) {
      fill(record, kind, threadIndex(thread), vblanks, entry, ge, a, b, c);
      call->record = record;
      call->generation = generation;
    }
  }
  sceKernelCpuResumeIntr(interrupts);
}

static void __attribute__((noinline)) endActive(Call* call, int result) {
  u32 exit = sceKernelGetSystemTimeLow();
  u32 interrupts = sceKernelCpuSuspendIntr();
  if(call->generation == generation) {
    call->record->exit = exit ? exit : 1;
    call->record->result = result;
  }
  sceKernelCpuResumeIntr(interrupts);
}

static inline void begin(Call* call, int kind, u32 a, u32 b, u32 c) {
  call->record = 0;
  if(state != Idle) beginActive(call, kind, a, b, c);
}

static inline void end(Call* call, int result) {
  if(call->record) endActive(call, result);
}

static int hookGeListEnQueue(const void* list, void* stall, int cbid, void* arg) {
  Call call;
  begin(&call, KindGeEnQueue, (u32)list, (u32)stall, cbid);
  int result = geListEnQueue(list, stall, cbid, arg);
  end(&call, result);
  return result;
}

static int hookGeListEnQueueHead(const void* list, void* stall, int cbid, void* arg) {
  Call call;
  begin(&call, KindGeEnQueueHead, (u32)list, (u32)stall, cbid);
  int result = geListEnQueueHead(list, stall, cbid, arg);
  end(&call, result);
  return result;
}

static int hookGeListDeQueue(int id) {
  Call call;
  begin(&call, KindGeDeQueue, id, 0, 0);
  int result = geListDeQueue(id);
  end(&call, result);
  return result;
}

static int hookGeListUpdateStallAddr(int id, void* stall) {
  Call call;
  begin(&call, KindGeUpdateStallAddr, id, (u32)stall, 0);
  int result = geListUpdateStallAddr(id, stall);
  end(&call, result);
  return result;
}

static int hookGeListSync(int id, int mode) {
  Call call;
  begin(&call, KindGeListSync, id, mode, 0);
  int result = geListSync(id, mode);
  end(&call, result);
  return result;
}

static int hookGeDrawSync(int mode) {
  Call call;
  begin(&call, KindGeDrawSync, mode, 0, 0);
  int result = geDrawSync(mode);
  end(&call, result);
  return result;
}

static int hookGeBreak(int mode, void* parameter) {
  Call call;
  begin(&call, KindGeBreak, mode, (u32)parameter, 0);
  int result = geBreak(mode, parameter);
  end(&call, result);
  return result;
}

static int hookGeContinue(void) {
  Call call;
  begin(&call, KindGeContinue, 0, 0, 0);
  int result = geContinue();
  end(&call, result);
  return result;
}

//The callback data's handlers are read only when the address is in the user partition every model has, where the
//game's own data is (the firmware checks the pointer itself, after this).
static int hookGeSetCallback(void* callback) {
  Call call;
  u32 address = (u32)callback, signal = 0, finish = 0;
  if(state == Capturing && address >= 0x08800000 && address < 0x0A000000 && !(address & 3)) {
    signal = ((u32*)callback)[0];
    finish = ((u32*)callback)[2];
  }
  begin(&call, KindGeSetCallback, signal, finish, address);
  int result = geSetCallback(callback);
  end(&call, result);
  return result;
}

static int hookGeUnsetCallback(int id) {
  Call call;
  begin(&call, KindGeUnsetCallback, id, 0, 0);
  int result = geUnsetCallback(id);
  end(&call, result);
  return result;
}

//The notices are drawn here, after the record is closed, into a frame buffer the firmware has accepted.
static int hookDisplaySetFrameBuf(void* top, int width, int format, int sync) {
  Call call;
  begin(&call, KindSetFrameBuf, (u32)top, width, (format & 0xFF) | (sync & 0xFF) << 8);
  int result = displaySetFrameBuf(top, width, format, sync);
  end(&call, result);
  if(result >= 0) noticeDraw(top, width, format);
  return result;
}

static int hookDisplayWaitVblankStart(void) {
  Call call;
  begin(&call, KindWaitVblankStart, 0, 0, 0);
  int result = displayWaitVblankStart();
  end(&call, result);
  return result;
}

static int hookDisplayWaitVblankStartCB(void) {
  Call call;
  begin(&call, KindWaitVblankStartCB, 0, 0, 0);
  int result = displayWaitVblankStartCB();
  end(&call, result);
  return result;
}

static int hookDisplayWaitVblank(void) {
  Call call;
  begin(&call, KindWaitVblank, 0, 0, 0);
  int result = displayWaitVblank();
  end(&call, result);
  return result;
}

static int hookDisplayWaitVblankCB(void) {
  Call call;
  begin(&call, KindWaitVblankCB, 0, 0, 0);
  int result = displayWaitVblankCB();
  end(&call, result);
  return result;
}

static int hookDisplayWaitVblankStartMulti(int vblanks) {
  Call call;
  begin(&call, KindWaitVblankStartMulti, vblanks, 0, 0);
  int result = displayWaitVblankStartMulti(vblanks);
  end(&call, result);
  return result;
}

static int hookDisplayWaitVblankStartMultiCB(int vblanks) {
  Call call;
  begin(&call, KindWaitVblankStartMultiCB, vblanks, 0, 0);
  int result = displayWaitVblankStartMultiCB(vblanks);
  end(&call, result);
  return result;
}

//The sub-interrupt monitor, with interrupts off. The GE's callbacks are its sub-interrupts 2n (SIGNAL) and 2n + 1
//(FINISH) for callback id n, given the command's low 16 bits and the list address after the SIGNAL/END or FINISH/END
//pair. A FINISH's callback runs before the GE driver starts the next list, so the record's exit is when it could.
//Several handlers can run in one vblank interrupt; the first whose vblank count is new is kept.
static s32 monitorBefore(s32 interrupt, s32 subInterrupt, s32 arg2, s32 handlerArg, s32 arg3, s32 handler, s8 option) {
  (void)handlerArg, (void)handler, (void)option;
  if(interrupt == GeInterrupt) {
    int kind = subInterrupt & 1 ? KindGeFinish : KindGeSignal;
    if(state == Counting) { counts[kind]++; return 0; }
    if(state != Capturing) return 0;
    u32 entry = sceKernelGetSystemTimeLow();
    u32 ge = geState();
    u32 vblanks = vcount();
    u32 command = kind == KindGeSignal ? readListWord((u32)arg3 - 8) : 0;
    u32 interrupts = sceKernelCpuSuspendIntr();
    ProfileRecord* record = reserve();
    if(record) {
      fill(record, kind, 0, vblanks, entry, ge, subInterrupt >> 1, arg2 & 0xFFFF, arg3);
      record->result = command;
      pendingCallback = record;
      pendingGeneration = generation;
    }
    sceKernelCpuResumeIntr(interrupts);
  } else if(interrupt == VblankInterrupt) {
    u32 vblanks = vcount();
    if(vblanks == lastVblank) return 0;
    lastVblank = vblanks;
    if(state == Counting) { counts[KindVblank]++; return 0; }
    if(state != Capturing) return 0;
    u32 entry = sceKernelGetSystemTimeLow();
    u32 ge = geState();
    u32 interrupts = sceKernelCpuSuspendIntr();
    ProfileRecord* record = reserve();
    if(record) {
      fill(record, KindVblank, 0, vblanks, entry, ge, vblanks, subInterrupt, 0);
      record->exit = entry ? entry : 1;
    }
    sceKernelCpuResumeIntr(interrupts);
  }
  return 0;
}

static s32 monitorAfter(s32 interrupt, s32 subInterrupt, s32 arg2, s32 handlerArg, s32 arg3, s32 result, s8 option) {
  (void)subInterrupt, (void)arg2, (void)handlerArg, (void)arg3, (void)result, (void)option;
  if(interrupt != GeInterrupt || !pendingCallback) return 0;
  u32 exit = sceKernelGetSystemTimeLow();
  u32 interrupts = sceKernelCpuSuspendIntr();
  if(pendingCallback && pendingGeneration == generation) pendingCallback->exit = exit ? exit : 1;
  pendingCallback = 0;
  sceKernelCpuResumeIntr(interrupts);
  return 0;
}

//The monitor is the interrupt manager's one slot; it's set and cleared with interrupts off, so a handler never sees
//half of it.
static void monitorOn(void) {
  if(monitorAvailable) firmware.registerSubIntrMonitor(monitorBefore, monitorAfter);
}

static void monitorOff(void) {
  if(monitorAvailable) firmware.releaseSubIntrMonitor();
}

#define Ge "sceGE_Manager", "sceGe_user"
#define Display "sceDisplay_Service", "sceDisplay"
typedef struct {
  int kind;
  const char* module;
  const char* library;
  u32 nid;
  void* hook;
  void** original;
  const char* name;
} Hook;

static const Hook hooks[] = {
  {KindGeEnQueue, Ge, 0xAB49E76A, hookGeListEnQueue, (void**)&geListEnQueue, "sceGeListEnQueue"},
  {KindGeEnQueueHead, Ge, 0x1C0D95A6, hookGeListEnQueueHead, (void**)&geListEnQueueHead, "sceGeListEnQueueHead"},
  {KindGeDeQueue, Ge, 0x5FB86AB0, hookGeListDeQueue, (void**)&geListDeQueue, "sceGeListDeQueue"},
  {KindGeUpdateStallAddr, Ge, 0xE0D68148, hookGeListUpdateStallAddr, (void**)&geListUpdateStallAddr,
   "sceGeListUpdateStallAddr"},
  {KindGeListSync, Ge, 0x03444EB4, hookGeListSync, (void**)&geListSync, "sceGeListSync"},
  {KindGeDrawSync, Ge, 0xB287BD61, hookGeDrawSync, (void**)&geDrawSync, "sceGeDrawSync"},
  {KindGeBreak, Ge, 0xB448EC0D, hookGeBreak, (void**)&geBreak, "sceGeBreak"},
  {KindGeContinue, Ge, 0x4C06E472, hookGeContinue, (void**)&geContinue, "sceGeContinue"},
  {KindGeSetCallback, Ge, 0xA4FC06A4, hookGeSetCallback, (void**)&geSetCallback, "sceGeSetCallback"},
  {KindGeUnsetCallback, Ge, 0x05DB22CE, hookGeUnsetCallback, (void**)&geUnsetCallback, "sceGeUnsetCallback"},
  {KindSetFrameBuf, Display, 0x289D82FE, hookDisplaySetFrameBuf, (void**)&displaySetFrameBuf,
   "sceDisplaySetFrameBuf"},
  {KindWaitVblankStart, Display, 0x984C27E7, hookDisplayWaitVblankStart, (void**)&displayWaitVblankStart,
   "sceDisplayWaitVblankStart"},
  {KindWaitVblankStartCB, Display, 0x46F186C3, hookDisplayWaitVblankStartCB, (void**)&displayWaitVblankStartCB,
   "sceDisplayWaitVblankStartCB"},
  {KindWaitVblank, Display, 0x36CDFADE, hookDisplayWaitVblank, (void**)&displayWaitVblank, "sceDisplayWaitVblank"},
  {KindWaitVblankCB, Display, 0x8EB9EC49, hookDisplayWaitVblankCB, (void**)&displayWaitVblankCB,
   "sceDisplayWaitVblankCB"},
  {KindWaitVblankStartMulti, Display, 0x40F1469C, hookDisplayWaitVblankStartMulti,
   (void**)&displayWaitVblankStartMulti, "sceDisplayWaitVblankStartMulti"},
  {KindWaitVblankStartMultiCB, Display, 0x77ED8B3A, hookDisplayWaitVblankStartMultiCB,
   (void**)&displayWaitVblankStartMultiCB, "sceDisplayWaitVblankStartMultiCB"},
};
enum { HookSpecs = sizeof(hooks) / sizeof(hooks[0]) };

//Each original is set before its syscall is patched, so a call that comes at once already has it. Whether a patch
//took is asked of sctrlKernelQuerySystemCall, which finds the hook in the syscall table; the dry run's counts show
//it too.
void installHooks(void) {
  for(int n = 0; n < HookCount; n++) hookStatus[n].syscall = -1;
  hookStatus[KindGeSignal].name = "GE SIGNAL callbacks";
  hookStatus[KindGeFinish].name = "GE FINISH callbacks";
  hookStatus[KindVblank].name = "vblanks";
  for(int n = 0; n < HookSpecs; n++) {
    const Hook* hook = &hooks[n];
    HookStatus* status = &hookStatus[hook->kind];
    status->name = hook->name;
    status->original = findFunction(hook->module, hook->library, hook->nid);
    if(!status->original) {
      if(lastLookup) traceLine("hook %s: refused, the lookup gave 0x%08lX", hook->name, (unsigned long)lastLookup);
      else traceLine("hook %s: not found", hook->name);
      continue;
    }
    *hook->original = status->original;
    if(!firmware.patchSyscall) {
      traceLine("hook %s: found at 0x%08lX, not patched (no sctrlHENPatchSyscall)", hook->name,
                (unsigned long)(Address)status->original);
      continue;
    }
    firmware.patchSyscall(status->original, hook->hook);
    if(firmware.querySystemCall) {
      status->syscall = firmware.querySystemCall(hook->hook);
      status->installed = status->syscall >= 0;
    } else {
      status->installed = 1;
    }
    unsigned long address = (unsigned long)(Address)status->original;
    if(!firmware.querySystemCall) traceLine("hook %s: found at 0x%08lX, patched (unconfirmed)", hook->name, address);
    else if(status->installed) traceLine("hook %s: found at 0x%08lX, patched, syscall 0x%X", hook->name, address,
                                         status->syscall);
    else traceLine("hook %s: found at 0x%08lX, patch not confirmed (0x%08X)", hook->name, address, status->syscall);
  }
  if(firmware.flushCache) firmware.flushCache();
  monitorAvailable = firmware.registerSubIntrMonitor && firmware.releaseSubIntrMonitor;
  traceLine("sub-interrupt monitor: %s", monitorAvailable ? "available" : "missing (no FINISH, SIGNAL or vblank records)");
  hookStatus[KindGeSignal].installed = monitorAvailable;
  hookStatus[KindGeFinish].installed = monitorAvailable;
  hookStatus[KindVblank].installed = monitorAvailable;
}

u32 hooksInstalledMask(void) {
  u32 mask = monitorAvailable ? 1u << 31 : 0;
  for(int kind = 1; kind < KindCount; kind++) {
    if(hookStatus[kind].installed) mask |= 1u << kind;
  }
  return mask;
}

void startCounting(void) {
  u32 interrupts = sceKernelCpuSuspendIntr();
  memset(counts, 0, sizeof(counts));
  lastVblank = ~0u;
  state = Counting;
  monitorOn();
  sceKernelCpuResumeIntr(interrupts);
}

u32 stopCounting(u32 out[KindCount]) {
  u32 interrupts = sceKernelCpuSuspendIntr();
  state = Idle;
  monitorOff();
  memcpy(out, counts, sizeof(counts));
  sceKernelCpuResumeIntr(interrupts);
  u32 total = 0;
  for(int kind = 1; kind < KindCount; kind++) total += out[kind];
  return total;
}

void startCapture(ProfileRecord* buffer, u32 size) {
  u32 interrupts = sceKernelCpuSuspendIntr();
  records = buffer;
  capacity = size;
  count = dropped = 0;
  full = 0;
  generation++;
  threadCount = 1;
  lastThread = -1;
  pendingCallback = 0;
  lastVblank = ~0u;
  state = Capturing;
  monitorOn();
  sceKernelCpuResumeIntr(interrupts);
}

int captureFull(void) {
  return full;
}

void stopCapture(u32* recordCount, u32* droppedCount) {
  u32 interrupts = sceKernelCpuSuspendIntr();
  state = Idle;
  monitorOff();
  generation++;
  pendingCallback = 0;
  *recordCount = count;
  *droppedCount = dropped;
  records = 0;
  capacity = 0;
  sceKernelCpuResumeIntr(interrupts);
}

//After stopCapture, when no hook writes the table any more.
int threadTable(u32 uids[], int size) {
  int n = 0;
  for(int index = 0; index < threadCount && n < size; index++) uids[n++] = index ? threads[index] : 0;
  return n;
}

//The dry run's cost measure: a hook around a function that does nothing, called while idle and while capturing
//(into a scratch buffer), and the monitor's pair for one FINISH. Interrupts stay off for the half millisecond it
//takes, so no game thread and no interrupt can reach the real hooks meanwhile; it runs only while they're idle.
static int __attribute__((noinline)) testOriginal(int value) {
  __asm__ volatile("" ::: "memory");
  return value;
}

static int __attribute__((noinline)) testHook(int value) {
  Call call;
  begin(&call, KindGeListSync, value, 1, 0);
  int result = testOriginal(value);
  end(&call, result);
  return result;
}

void measureCosts(u32* idleNanoseconds, u32* captureNanoseconds, u32* monitorNanoseconds) {
  static ProfileRecord scratch[2];
  enum { IdleCalls = 1000, CaptureCalls = 100 };
  u32 interrupts = sceKernelCpuSuspendIntr();
  int savedState = state;
  state = Idle;
  u32 t0 = sceKernelGetSystemTimeLow();
  for(int n = 0; n < IdleCalls; n++) testOriginal(n);
  u32 t1 = sceKernelGetSystemTimeLow();
  for(int n = 0; n < IdleCalls; n++) testHook(n);
  u32 t2 = sceKernelGetSystemTimeLow();
  records = scratch;
  capacity = 2;
  generation++;
  state = Capturing;
  u32 t3 = sceKernelGetSystemTimeLow();
  for(int n = 0; n < CaptureCalls; n++) {
    count = 0;
    testHook(n);
  }
  u32 t4 = sceKernelGetSystemTimeLow();
  for(int n = 0; n < CaptureCalls; n++) {
    count = 0;
    monitorBefore(GeInterrupt, 1, 0, 0, 0, 0, 0);
    monitorAfter(GeInterrupt, 1, 0, 0, 0, 0, 0);
  }
  u32 t5 = sceKernelGetSystemTimeLow();
  state = savedState;
  records = 0;
  capacity = count = dropped = 0;
  full = 0;
  generation++;
  threadCount = 1;
  lastThread = -1;
  pendingCallback = 0;
  sceKernelCpuResumeIntr(interrupts);
  u32 direct = t1 - t0, hooked = t2 - t1;
  *idleNanoseconds = hooked > direct ? (hooked - direct) * 1000 / IdleCalls : 0;
  *captureNanoseconds = (t4 - t3) * 1000 / CaptureCalls;
  *monitorNanoseconds = (t5 - t4) * 1000 / CaptureCalls;
}
