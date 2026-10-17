//psp-measure's tests of the CPU alone (round 8; docs/psp-core.md, part 107). In every scene the owner captured, a
//PSP's CPU took 7 to 9 times Phobos's time between a loop's waits (part 103), and Phobos counts each instruction as a
//cycle at 333 MHz whatever clock a game sets. These say where a PSP's cycles go: ALU work dependent and not,
//branches, loads and stores (cpu-alu); multiplies and divides, the FPU's and the VFPU's latencies (cpu-latency);
//memory at working sets from 1 KiB to 8 MiB, cached and not, the scratchpad and VRAM (cpu-memory); system calls, a
//thread switch, and the GE's and the display's calls (cpu-syscalls); and what clocks the power service grants
//(cpu-clocks). Each file holds records as round 6's timing tests do: the case, its size, the clocks (CPU MHz << 16 |
//bus MHz, as scePower gives them back), repeats | values << 16, then each run's `values` words: the run's time in
//microseconds (sceKernelGetSystemTimeLow), but in cpu-clocks (below). The loops are written in assembly, so what
//each runs is what its line says, whatever the compiler.

#include "measure.h"
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspintrman.h>
#include <psppower.h>
#include <psputils.h>
#include <stdio.h>
#include <string.h>

static Output test;
static int failed;
static unsigned int records[16384], recorded;
static void record(unsigned int word) {
  if(recorded < sizeof(records) / 4) records[recorded++] = word;
}
static volatile unsigned int sink;  //what the loops give, kept so nothing is left out

//The clock settings, as ge.c's round 8 has them: the CPU at 222 MHz with the bus at 111, 333 with 166, 222 with 166
//(the PLL at 333), 266 with 133; each test's `settings` bits say which it runs at. The clocks as found are put back.
enum { At222 = 1, At333 = 2, At333Cpu222 = 4, At266 = 8, AtTwo = At222 | At333, AtAll = 15 };
static int foundCpu, foundBus;
static void setClocks(int setting) {
  static const short clocks[4][3] = {{222, 222, 111}, {333, 333, 166}, {333, 222, 166}, {266, 266, 133}};
  scePowerSetClockFrequency(clocks[setting][0], clocks[setting][1], clocks[setting][2]);
}
static void putClocksBack(void) {
  scePowerSetClockFrequency(foundCpu > 2 * foundBus ? foundCpu : 2 * foundBus, foundCpu, foundBus);
}
static unsigned int clocksNow(void) {
  return scePowerGetCpuClockFrequencyInt() << 16 | scePowerGetBusClockFrequencyInt();
}

//---- the loops: each runs `n` passes of its body and gives back what it computed

//The loop's own instructions: addiu, bnez and its delay slot.
#define LOOP(body, ...)                                                                                              \
  __asm__ volatile(".set push\n.set noreorder\n1:\n" body "addiu %[n], %[n], -1\nbnez %[n], 1b\nnop\n.set pop\n"     \
                   : [n] "+r"(n), __VA_ARGS__)
static unsigned int emptyLoop(unsigned int n) {
  unsigned int x = 0;
  LOOP("", [x] "+r"(x));
  return x;
}
static unsigned int addChain(unsigned int n) {
  unsigned int x = 0, one = 1;
  LOOP(".rept 64\naddu %[x], %[x], %[one]\n.endr\n", [x] "+r"(x) : [one] "r"(one));
  return x;
}
static unsigned int addChains(unsigned int n) {
  unsigned int a = 0, b = 0, c = 0, d = 0, one = 1;
  LOOP(".rept 16\naddu %[a], %[a], %[one]\naddu %[b], %[b], %[one]\naddu %[c], %[c], %[one]\naddu %[d], %[d], %[one]\n"
       ".endr\n", [a] "+r"(a), [b] "+r"(b), [c] "+r"(c), [d] "+r"(d) : [one] "r"(one));
  return a + b + c + d;
}
static unsigned int branches(unsigned int n) {
  unsigned int x = 0;
  LOOP(".rept 64\nb .+8\nnop\n.endr\n", [x] "+r"(x));
  return x;
}
static unsigned int __attribute__((aligned(64))) cells[16];  //words in the cache for the loads and stores
static unsigned int loadChain(unsigned int n) {
  cells[0] = (unsigned int)cells;
  unsigned int x = (unsigned int)cells;
  LOOP(".rept 64\nlw %[x], 0(%[x])\n.endr\n", [x] "+r"(x) : : "memory");
  return x;
}
static unsigned int loads(unsigned int n) {
  unsigned int x = 0;
  LOOP(".rept 16\nlw $8, 0(%[at])\nlw $9, 4(%[at])\nlw $10, 8(%[at])\nlw $11, 12(%[at])\n.endr\n", [x] "+r"(x)
       : [at] "r"(cells) : "$8", "$9", "$10", "$11", "memory");
  return x;
}
static unsigned int stores(unsigned int n) {
  unsigned int x = 0;
  LOOP(".rept 16\nsw %[x], 0(%[at])\nsw %[x], 4(%[at])\nsw %[x], 8(%[at])\nsw %[x], 12(%[at])\n.endr\n", [x] "+r"(x)
       : [at] "r"(cells) : "memory");
  return x;
}
static unsigned int loadUse(unsigned int n) {
  unsigned int x = 0;
  LOOP(".rept 64\nlw $8, 0(%[at])\naddu %[x], %[x], $8\n.endr\n", [x] "+r"(x) : [at] "r"(cells) : "$8", "memory");
  return x;
}
static unsigned int multiplyChain(unsigned int n) {
  unsigned int x = 3, by = 0x7fff7fff;
  LOOP(".rept 32\nmult %[x], %[by]\nmflo %[x]\n.endr\n", [x] "+r"(x) : [by] "r"(by) : "hi", "lo");
  return x;
}
static unsigned int multiplies(unsigned int n) {
  unsigned int x = 3, by = 0x7fff7fff;
  LOOP(".rept 32\nmult %[x], %[by]\n.endr\nmflo %[x]\n", [x] "+r"(x) : [by] "r"(by) : "hi", "lo");
  return x;
}
static unsigned int multiplyAdds(unsigned int n) {
  unsigned int x = 3, by = 0x7fff7fff;
  LOOP(".rept 32\nmadd %[x], %[by]\n.endr\nmflo %[x]\n", [x] "+r"(x) : [by] "r"(by) : "hi", "lo");
  return x;
}
static unsigned int divideChain(unsigned int n) {  //the dividend kept big: each quotient ORed with 2^30
  unsigned int x = 0x7fffffff, by = 3, big = 0x40000000;
  LOOP(".rept 16\ndiv $zero, %[x], %[by]\nmflo %[x]\nor %[x], %[x], %[big]\n.endr\n", [x] "+r"(x)
       : [by] "r"(by), [big] "r"(big) : "hi", "lo");
  return x;
}
static unsigned int divides(unsigned int n) {
  unsigned int x = 0x7fffffff, by = 3;
  LOOP(".rept 16\ndivu $zero, %[x], %[by]\n.endr\nmflo %[x]\nor %[x], %[x], %[by]\n", [x] "+r"(x) : [by] "r"(by)
       : "hi", "lo");
  return x;
}
static unsigned int fpuAddChain(unsigned int n) {
  float a = 1, zero = 0;
  LOOP(".rept 64\nadd.s %[a], %[a], %[zero]\n.endr\n", [a] "+f"(a) : [zero] "f"(zero));
  return (unsigned int)a;
}
static unsigned int fpuAdds(unsigned int n) {
  float a = 1, b = 1, c = 1, d = 1, zero = 0;
  LOOP(".rept 16\nadd.s %[a], %[a], %[zero]\nadd.s %[b], %[b], %[zero]\nadd.s %[c], %[c], %[zero]\n"
       "add.s %[d], %[d], %[zero]\n.endr\n", [a] "+f"(a), [b] "+f"(b), [c] "+f"(c), [d] "+f"(d) : [zero] "f"(zero));
  return (unsigned int)(a + b + c + d);
}
static unsigned int fpuMultiplyChain(unsigned int n) {
  float a = 1, one = 1;
  LOOP(".rept 64\nmul.s %[a], %[a], %[one]\n.endr\n", [a] "+f"(a) : [one] "f"(one));
  return (unsigned int)a;
}
static unsigned int fpuDivideChain(unsigned int n) {
  float a = 1, one = 1;
  LOOP(".rept 16\ndiv.s %[a], %[a], %[one]\n.endr\n", [a] "+f"(a) : [one] "f"(one));
  return (unsigned int)a;
}
static unsigned int fpuRootChain(unsigned int n) {
  float a = 1;
  LOOP(".rept 16\nsqrt.s %[a], %[a]\n.endr\n", [a] "+f"(a));
  return (unsigned int)a;
}
static unsigned int fpuLoadsStores(unsigned int n) {
  unsigned int x = 0;
  LOOP(".rept 32\nlwc1 $f0, 0(%[at])\nswc1 $f0, 4(%[at])\n.endr\n", [x] "+r"(x) : [at] "r"(cells) : "$f0", "memory");
  return x;
}
static unsigned int fpuMoves(unsigned int n) {
  unsigned int x = 1;
  LOOP(".rept 32\nmtc1 %[x], $f0\nmfc1 %[x], $f0\n.endr\n", [x] "+r"(x) : : "$f0");
  return x;
}
//The VFPU's: C000 is (1, 0, 0, 0) and C010 (1, 1, 1, 1) to start, so every chain stays where it starts.
static float __attribute__((aligned(16))) vfpuStartValues[8] = {1, 0, 0, 0, 1, 1, 1, 1};
static void vfpuReady(void) {
  __asm__ volatile("lv.q C000, 0(%0)\nlv.q C010, 16(%0)\n" : : "r"(vfpuStartValues) : "memory");
}
static unsigned int vfpuAddChain(unsigned int n) {
  unsigned int x = 0;
  vfpuReady();
  LOOP(".rept 64\nvadd.s S000, S000, S001\n.endr\n", [x] "+r"(x));
  return x;
}
static unsigned int vfpuMultiplyChain(unsigned int n) {
  unsigned int x = 0;
  vfpuReady();
  LOOP(".rept 64\nvmul.q C000, C000, C010\n.endr\n", [x] "+r"(x));
  return x;
}
static unsigned int vfpuDotChain(unsigned int n) {
  unsigned int x = 0;
  vfpuReady();
  LOOP(".rept 64\nvdot.q S000, C000, C010\n.endr\n", [x] "+r"(x));
  return x;
}
static unsigned int vfpuSineChain(unsigned int n) {
  unsigned int x = 0;
  vfpuReady();
  LOOP(".rept 16\nvsin.s S000, S000\n.endr\n", [x] "+r"(x));
  return x;
}
static unsigned int vfpuRootChain(unsigned int n) {
  unsigned int x = 0;
  vfpuReady();
  LOOP(".rept 16\nvrsq.s S000, S000\n.endr\n", [x] "+r"(x));
  return x;
}
static unsigned int vfpuLoadsStores(unsigned int n) {
  unsigned int x = 0;
  vfpuReady();
  LOOP(".rept 16\nlv.q C020, 0(%[at])\nsv.q C020, 16(%[at])\n.endr\n", [x] "+r"(x) : [at] "r"(cells) : "memory");
  return x;
}
#undef LOOP

//---- memory

unsigned int cpuRead(const void* from, unsigned int bytes) {
  const volatile unsigned int* w = from;
  unsigned int sum = 0;
  for(unsigned int n = 0; n < bytes / 4; n += 8) {
    sum += w[n] + w[n + 1] + w[n + 2] + w[n + 3] + w[n + 4] + w[n + 5] + w[n + 6] + w[n + 7];
  }
  return sum;
}
static void cpuWrite(void* to, unsigned int bytes) {
  volatile unsigned int* w = to;
  for(unsigned int n = 0; n < bytes / 4; n += 8) {
    w[n] = n, w[n + 1] = n, w[n + 2] = n, w[n + 3] = n, w[n + 4] = n, w[n + 5] = n, w[n + 6] = n, w[n + 7] = n;
  }
}
//Node n points to node (5n + 12345) mod nodes (nodes a power of two): a full cycle, each step far from the last.
void cpuChain(void* at, unsigned int nodes, unsigned int apart) {
  unsigned char* base = at;
  for(unsigned int n = 0; n < nodes; n++) {
    *(unsigned int*)(base + n * apart) = (unsigned int)(base + ((5 * n + 12345) & (nodes - 1)) * apart);
  }
}
unsigned int cpuChase(const void* at, unsigned int steps) {
  const void* p = at;
  while(steps--) p = *(const void* volatile*)p;
  return (unsigned int)p;
}

//Where the memory tests run: RAM from the user partition (8 MiB, or 4 if that much isn't free: the records' sizes say),
//the scratchpad (16 KiB at 0x00010000), and VRAM past the GE tests' depth buffer (its last 256 KiB).
static unsigned char* memory;
static unsigned int memoryBytes;
#define UNCACHED(p) ((unsigned char*)((unsigned int)(p) | 0x40000000))
#define SCRATCHPAD ((unsigned char*)0x00010000)
#define FREE_VRAM ((unsigned char*)0x041c0000)

//The memory cases: what each runs (run, with size the working set in bytes) after its chain is made (prepare).
static unsigned int chaseSteps;
static unsigned char* chased;
static void prepareChase(unsigned char* at, unsigned int size, unsigned int steps) {
  if(at == UNCACHED(at)) sceKernelDcacheWritebackInvalidateAll();  //(no cached line of it to land over the chain)
  cpuChain(at, size / 64, 64);
  sceKernelDcacheWritebackAll();
  chased = at, chaseSteps = steps;
}
static void chaseRam(unsigned int size) { prepareChase(memory, size, 65536); }
static void chaseUncached(unsigned int size) { prepareChase(UNCACHED(memory), size, 16384); }
static void chaseScratchpad(unsigned int size) { prepareChase(SCRATCHPAD, size, 65536); }
static void chaseVram(unsigned int size) { prepareChase(FREE_VRAM, size, 65536); }
static void chaseVramUncached(unsigned int size) { prepareChase(UNCACHED(FREE_VRAM), size, 16384); }
static unsigned int runChase(unsigned int size) {
  (void)size;
  return cpuChase(chased, chaseSteps);
}
static unsigned int readRam(unsigned int size) {
  unsigned int sum = 0;
  for(unsigned int done = 0; done < 8u << 20; done += size) sum += cpuRead(memory, size);
  return sum;
}
static unsigned int writeRam(unsigned int size) {
  for(unsigned int done = 0; done < 8u << 20; done += size) cpuWrite(memory, size);
  return 0;
}
static unsigned int readUncached(unsigned int size) {
  unsigned int sum = 0;
  for(unsigned int done = 0; done < 1u << 20; done += size) sum += cpuRead(UNCACHED(memory), size);
  return sum;
}
static unsigned int writeUncached(unsigned int size) {
  for(unsigned int done = 0; done < 1u << 20; done += size) cpuWrite(UNCACHED(memory), size);
  return 0;
}
static unsigned int copyRam(unsigned int size) {
  memcpy(memory + size, memory, size);
  return memory[size];
}
static unsigned int readVram(unsigned int size) {
  unsigned int sum = 0;
  for(unsigned int done = 0; done < 1u << 20; done += size) sum += cpuRead(UNCACHED(FREE_VRAM), size);
  return sum;
}
static unsigned int writeVram(unsigned int size) {
  for(unsigned int done = 0; done < 1u << 20; done += size) cpuWrite(UNCACHED(FREE_VRAM), size);
  return 0;
}

//---- system calls, a thread switch, the GE's and the display's calls

static SceUID ping = -1, pong = -1, partner = -1;
static volatile int partnerStops;
static int partnerThread(SceSize length, void* argument) {
  (void)length, (void)argument;
  for(;;) {
    sceKernelWaitSema(ping, 1, 0);
    if(partnerStops) return 0;
    sceKernelSignalSema(pong, 1);
  }
}
//Lists for the GE's calls: 32 empty ones (FINISH, END), 16 bytes apart, and one of two NOPs before its FINISH and END,
//which waits at its stall address, its start, till the last call moves it on.
static unsigned int __attribute__((aligned(64))) geLists[32 * 4 + 4];
#define GE_LISTS ((unsigned int*)UNCACHED(geLists))
static void prepareLists(unsigned int calls) {
  (void)calls;
  sceKernelDcacheWritebackInvalidateAll();
  for(int k = 0; k < 32; k++) GE_LISTS[k * 4] = 0x0fu << 24, GE_LISTS[k * 4 + 1] = 0x0cu << 24;
  unsigned int* waiting = GE_LISTS + 32 * 4;
  waiting[0] = 0, waiting[1] = 0, waiting[2] = 0x0fu << 24, waiting[3] = 0x0cu << 24;
}
static unsigned int timeOfDay(unsigned int calls) {
  unsigned int x = 0;
  while(calls--) x += sceKernelGetSystemTimeLow();
  return x;
}
static unsigned int threadId(unsigned int calls) {
  unsigned int x = 0;
  while(calls--) x += sceKernelGetThreadId();
  return x;
}
static unsigned int semaphore(unsigned int calls) {
  while(calls--) sceKernelSignalSema(pong, 1), sceKernelPollSema(pong, 1);
  return 0;
}
static unsigned int delayZero(unsigned int calls) {
  while(calls--) sceKernelDelayThread(0);
  return 0;
}
static unsigned int threadSwitches(unsigned int calls) {  //a round trip: two switches (none without the partner)
  while(calls-- && partner >= 0) {
    SceUInt timeout = 100000;
    sceKernelSignalSema(ping, 1);
    sceKernelWaitSema(pong, 1, &timeout);
  }
  return 0;
}
static unsigned int interruptsOff(unsigned int calls) {
  while(calls--) sceKernelCpuResumeIntr(sceKernelCpuSuspendIntr());
  return 0;
}
static unsigned int writebackAll(unsigned int calls) {
  while(calls--) sceKernelDcacheWritebackAll();
  return 0;
}
static unsigned int writebackInvalidateAll(unsigned int calls) {
  while(calls--) sceKernelDcacheWritebackInvalidateAll();
  return 0;
}
static unsigned int writebackRange(unsigned int calls) {
  while(calls--) sceKernelDcacheWritebackRange(memory, 65536);
  return 0;
}
static unsigned int dirtyWriteback(unsigned int calls) {  //16 KiB written, then the whole cache written back
  while(calls--) cpuWrite(memory, 16384), sceKernelDcacheWritebackAll();
  return 0;
}
static unsigned int dirtyAlone(unsigned int calls) {  //the writes alone
  while(calls--) cpuWrite(memory, 16384);
  return 0;
}
static unsigned int enqueueAndSync(unsigned int calls) {
  while(calls--) sceGeListSync(sceGeListEnQueue(GE_LISTS, 0, -1, 0), 0);
  return 0;
}
static unsigned int enqueues(unsigned int calls) {  //(calls 32 at most) queued back to back, then waited for
  for(unsigned int k = 0; k < calls; k++) sceGeListEnQueue(GE_LISTS + k * 4, 0, -1, 0);
  unsigned int queued = sceKernelGetSystemTimeLow();
  sceGeDrawSync(0);
  return queued;
}
static int waitingList = -1;
static void prepareWaiting(unsigned int calls) {
  prepareLists(calls);
  waitingList = sceGeListEnQueue(GE_LISTS + 32 * 4, GE_LISTS + 32 * 4, -1, 0);
}
static void endWaiting(void) {
  if(waitingList >= 0) sceGeListUpdateStallAddr(waitingList, GE_LISTS + 32 * 4 + 4), sceGeListSync(waitingList, 0);
  waitingList = -1;
}
static unsigned int stallUpdates(unsigned int calls) {
  while(calls--) sceGeListUpdateStallAddr(waitingList, GE_LISTS + 32 * 4);
  return 0;
}
static unsigned int listLooks(unsigned int calls) {
  unsigned int x = 0;
  while(calls--) x += sceGeListSync(waitingList, 1);
  return x;
}
static unsigned int drawLooks(unsigned int calls) {
  unsigned int x = 0;
  while(calls--) x += sceGeDrawSync(1);
  return x;
}
static unsigned int frameBuffers(unsigned int calls) {  //the screen as it is, the text's frame buffer
  while(calls--) {
    sceDisplaySetFrameBuf((void*)0x04000000, 512, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
  }
  return 0;
}
static unsigned int vcounts(unsigned int calls) {
  unsigned int x = 0;
  while(calls--) x += sceDisplayGetVcount();
  return x;
}
static unsigned int pads(unsigned int calls) {
  SceCtrlData pad;
  unsigned int x = 0;
  while(calls--) sceCtrlPeekBufferPositive(&pad, 1), x += pad.Buttons;
  return x;
}
static unsigned int vramAddresses(unsigned int calls) {
  unsigned int x = 0;
  while(calls--) x += (unsigned int)sceGeEdramGetAddr();
  return x;
}

//---- the tests

typedef struct {
  const char* name; const char* unit;
  unsigned int (*run)(unsigned int size);
  void (*prepare)(unsigned int size);  //before each size's runs, or none
  int count; unsigned int sizes[10];
} CpuCase;

static const CpuCase aluCases[] = {
  {"the loop alone (addiu, bnez, a nop)", "passes", emptyLoop, 0, 2, {65536, 131072}},
  {"64 additions a pass, each needing the one before (addu)", "passes", addChain, 0, 2, {16384, 32768}},
  {"64 additions a pass in four chains", "passes", addChains, 0, 2, {16384, 32768}},
  {"64 branches a pass, taken, to the instruction past their delay slot (a nop)", "passes", branches, 0, 2,
   {8192, 16384}},
  {"64 loads a pass, each from the address the one before loaded (a word in the cache pointing to itself)", "passes",
   loadChain, 0, 2, {8192, 16384}},
  {"64 loads a pass, from four words in the cache, none needing another", "passes", loads, 0, 2, {8192, 16384}},
  {"64 stores a pass, to four words in the cache", "passes", stores, 0, 2, {8192, 16384}},
  {"64 loads a pass, each used by the next instruction (lw, then addu)", "passes", loadUse, 0, 2, {8192, 16384}},
};
static const CpuCase latencyCases[] = {
  {"32 multiplies a pass, each needing the one before (mult, mflo)", "passes", multiplyChain, 0, 2, {2048, 4096}},
  {"32 multiplies a pass, none needing another (mult), then one mflo", "passes", multiplies, 0, 2, {2048, 4096}},
  {"32 multiply-adds a pass (madd), then one mflo", "passes", multiplyAdds, 0, 2, {2048, 4096}},
  {"16 divides a pass, each needing the one before (div, mflo, or)", "passes", divideChain, 0, 2, {1024, 2048}},
  {"16 unsigned divides a pass, none needing another (divu), then one mflo", "passes", divides, 0, 2, {1024, 2048}},
  {"64 FPU additions a pass, each needing the one before (add.s)", "passes", fpuAddChain, 0, 2, {2048, 4096}},
  {"64 FPU additions a pass in four chains", "passes", fpuAdds, 0, 2, {2048, 4096}},
  {"64 FPU multiplies a pass, each needing the one before (mul.s)", "passes", fpuMultiplyChain, 0, 2, {2048, 4096}},
  {"16 FPU divides a pass, each needing the one before (div.s)", "passes", fpuDivideChain, 0, 2, {1024, 2048}},
  {"16 FPU square roots a pass, each needing the one before (sqrt.s)", "passes", fpuRootChain, 0, 2, {1024, 2048}},
  {"32 FPU loads and 32 stores a pass, a word in the cache (lwc1, swc1)", "passes", fpuLoadsStores, 0, 2,
   {2048, 4096}},
  {"32 moves to the FPU and 32 back a pass (mtc1, mfc1)", "passes", fpuMoves, 0, 2, {2048, 4096}},
  {"64 VFPU additions a pass, each needing the one before (vadd.s)", "passes", vfpuAddChain, 0, 2, {2048, 4096}},
  {"64 VFPU multiplies of four lanes a pass, each needing the one before (vmul.q)", "passes", vfpuMultiplyChain, 0, 2,
   {2048, 4096}},
  {"64 VFPU dot products of four lanes a pass, each needing the one before (vdot.q)", "passes", vfpuDotChain, 0, 2,
   {2048, 4096}},
  {"16 VFPU sines a pass, each needing the one before (vsin.s)", "passes", vfpuSineChain, 0, 2, {1024, 2048}},
  {"16 VFPU reciprocal square roots a pass, each needing the one before (vrsq.s)", "passes", vfpuRootChain, 0, 2,
   {1024, 2048}},
  {"16 VFPU loads and 16 stores of four lanes a pass, in the cache (lv.q, sv.q)", "passes", vfpuLoadsStores, 0, 2,
   {2048, 4096}},
};
static const CpuCase memoryCases[] = {
  {"65536 steps of a chain of pointers 64 bytes apart (cpuChain) over this many bytes of RAM", "bytes", runChase,
   chaseRam, 10, {1024, 4096, 8192, 16384, 32768, 65536, 262144, 1048576, 4194304, 8388608}},
  {"16384 steps of the chain through RAM's uncached addresses", "bytes", runChase, chaseUncached, 2,
   {4096, 1048576}},
  {"65536 steps of the chain in the scratchpad", "bytes", runChase, chaseScratchpad, 1, {4096}},
  {"65536 steps of the chain in VRAM", "bytes", runChase, chaseVram, 1, {65536}},
  {"16384 steps of the chain through VRAM's uncached addresses", "bytes", runChase, chaseVramUncached, 1, {65536}},
  {"8 MiB of words read, this many bytes over and over (8 words a pass)", "bytes", readRam, 0, 3,
   {16384, 262144, 8388608}},
  {"8 MiB of words written, this many bytes over and over", "bytes", writeRam, 0, 3, {16384, 262144, 8388608}},
  {"1 MiB of words read through RAM's uncached addresses, this many bytes over and over", "bytes", readUncached, 0, 1,
   {262144}},
  {"1 MiB of words written through RAM's uncached addresses", "bytes", writeUncached, 0, 1, {262144}},
  {"memcpy, this many bytes", "bytes", copyRam, 0, 1, {1048576}},
  {"1 MiB of words read from VRAM (uncached), this many bytes over and over", "bytes", readVram, 0, 1, {262144}},
  {"1 MiB of words written to VRAM (uncached)", "bytes", writeVram, 0, 1, {262144}},
};
static const CpuCase syscallCases[] = {
  {"sceKernelGetSystemTimeLow", "calls", timeOfDay, 0, 2, {2048, 8192}},
  {"sceKernelGetThreadId", "calls", threadId, 0, 2, {2048, 8192}},
  {"sceKernelSignalSema and sceKernelPollSema (no thread waiting)", "pairs", semaphore, 0, 2, {1024, 4096}},
  {"sceKernelDelayThread(0)", "calls", delayZero, 0, 2, {256, 1024}},
  {"a thread switch there and back: sceKernelSignalSema to a thread of higher priority waiting, which signals back",
   "round trips", threadSwitches, 0, 2, {256, 1024}},
  {"sceKernelCpuSuspendIntr and sceKernelCpuResumeIntr", "pairs", interruptsOff, 0, 2, {1024, 4096}},
  {"sceKernelDcacheWritebackAll, nothing to write back", "calls", writebackAll, 0, 2, {64, 256}},
  {"sceKernelDcacheWritebackInvalidateAll", "calls", writebackInvalidateAll, 0, 2, {64, 256}},
  {"sceKernelDcacheWritebackRange over 64 KiB", "calls", writebackRange, 0, 2, {64, 256}},
  {"16 KiB written, then sceKernelDcacheWritebackAll", "calls", dirtyWriteback, 0, 2, {64, 256}},
  {"16 KiB written alone", "times", dirtyAlone, 0, 2, {64, 256}},
  {"sceGeListEnQueue of an empty list (FINISH, END), then sceGeListSync(id, 0)", "calls", enqueueAndSync,
   prepareLists, 2, {64, 256}},
  {"sceGeListEnQueue of empty lists back to back (then sceGeDrawSync(0), outside the time: see below)", "calls",
   enqueues, prepareLists, 2, {8, 32}},
  {"sceGeListUpdateStallAddr, a list waiting at its stall address, left there", "calls", stallUpdates,
   prepareWaiting, 2, {1024, 4096}},
  {"sceGeListSync(id, 1) of that list", "calls", listLooks, prepareWaiting, 2, {1024, 4096}},
  {"sceGeDrawSync(1), the GE idle", "calls", drawLooks, 0, 2, {1024, 4096}},
  {"sceDisplaySetFrameBuf, the frame buffer shown", "calls", frameBuffers, 0, 2, {256, 1024}},
  {"sceDisplayGetVcount", "calls", vcounts, 0, 2, {2048, 8192}},
  {"sceCtrlPeekBufferPositive", "calls", pads, 0, 2, {256, 1024}},
  {"sceGeEdramGetAddr", "calls", vramAddresses, 0, 2, {2048, 8192}},
};

static void save(const char* name) {
  if(!writeOut(&test, name, records, recorded * 4) || !finish(&test, name)) failed = 1;
}
//Each case at each setting asked, each size `repeats` times (a size past `limit`, if there is one, cut to it), one
//word a run: its time. (A case that gives back a time of its own, enqueues', has its end there: the GE's waiting left
//out.)
static void cpuCases(const char* name, const CpuCase* cases, int count, int repeats, int settings, unsigned int limit) {
  int started = begin(&test, name);
  if(started < 0) failed = 1;
  if(started <= 0) return;
  recorded = 0;
  int total = 0, done = 0;
  for(int setting = 0; setting < 4; setting++) total += (settings >> setting & 1) * count;
  for(int setting = 0; setting < 4; setting++) {
    if(!(settings >> setting & 1)) continue;
    setClocks(setting);
    unsigned int clocks = clocksNow();
    for(int c = 0; c < count; c++) {
      for(int s = 0; s < cases[c].count; s++) {
        unsigned int size = limit && cases[c].sizes[s] > limit ? limit : cases[c].sizes[s];
        if(cases[c].prepare) cases[c].prepare(size);
        record(c), record(size), record(clocks), record(repeats | 1 << 16);
        for(int r = 0; r < repeats; r++) {
          unsigned int began = sceKernelGetSystemTimeLow(), gave = cases[c].run(size);
          unsigned int ended = cases[c].run == enqueues ? gave : sceKernelGetSystemTimeLow();
          sink += gave;
          record(ended - began);
        }
        if(cases[c].prepare == prepareWaiting) endWaiting();
      }
      progress(&test, name, ++done, total);
    }
  }
  putClocksBack();
  save(name);
}

//What the power service grants (part 104: a stock PSP refuses a CPU faster than the PLL and a bus faster than half
//of it; the owner's PSP ran Activision Hits Remixed at 333/166 under ARK's overclock): each attempt from a known
//setting, then what the call gave back, the clocks after (CPU and bus MHz), and the time of 65536 passes of
//cpu-alu's dependent additions (4.19 million instructions) at them, in microseconds: 1 record each, 4 values.
typedef struct { const char* name; short from, pll, cpu, bus, call; } ClockAttempt;  //call 0: all three, 1: CPU, 2: bus
static const ClockAttempt clockAttempts[] = {
  {"scePowerSetClockFrequency(333, 333, 166) from 222/222/111", 0, 333, 333, 166, 0},
  {"scePowerSetClockFrequency(333, 222, 166) from 222/222/111", 0, 333, 222, 166, 0},
  {"scePowerSetClockFrequency(266, 266, 133) from 222/222/111", 0, 266, 266, 133, 0},
  {"scePowerSetClockFrequency(222, 222, 111) from 333/333/166", 1, 222, 222, 111, 0},
  {"scePowerSetClockFrequency(190, 190, 95) from 222/222/111", 0, 190, 190, 95, 0},
  {"scePowerSetClockFrequency(333, 333, 111) from 222/222/111", 0, 333, 333, 111, 0},
  {"scePowerSetClockFrequency(300, 300, 150) from 222/222/111", 0, 300, 300, 150, 0},
  {"scePowerSetCpuClockFrequency(333) from 222/222/111", 0, 0, 333, 0, 1},
  {"scePowerSetBusClockFrequency(166) from 222/222/111", 0, 0, 0, 166, 2},
  {"scePowerSetCpuClockFrequency(266) from 333/333/166", 1, 0, 266, 0, 1},
  {"scePowerSetCpuClockFrequency(100) from 333/333/166", 1, 0, 100, 0, 1},
  {"scePowerSetBusClockFrequency(111) from 333/333/166", 1, 0, 0, 111, 2},
  {"scePowerSetBusClockFrequency(133) from 333/333/166", 1, 0, 0, 133, 2},
};
static void cpuClocks(const char* name) {
  int started = begin(&test, name);
  if(started < 0) failed = 1;
  if(started <= 0) return;
  recorded = 0;
  int count = sizeof(clockAttempts) / sizeof(clockAttempts[0]);
  for(int c = 0; c < count; c++) {
    const ClockAttempt* a = &clockAttempts[c];
    setClocks(a->from);
    unsigned int before = clocksNow();
    int gave = a->call == 0 ? scePowerSetClockFrequency(a->pll, a->cpu, a->bus)
             : a->call == 1 ? scePowerSetCpuClockFrequency(a->cpu) : scePowerSetBusClockFrequency(a->bus);
    unsigned int cpu = scePowerGetCpuClockFrequencyInt(), bus = scePowerGetBusClockFrequencyInt();
    unsigned int began = sceKernelGetSystemTimeLow();
    sink += addChain(65536);
    unsigned int time = sceKernelGetSystemTimeLow() - began;
    record(c), record(0), record(before), record(1 | 4 << 16);
    record(gave), record(cpu), record(bus), record(time);
    progress(&test, name, c + 1, count);
  }
  putClocksBack();
  save(name);
}

int cpuTests(void) {
  failed = 0;
  foundCpu = scePowerGetCpuClockFrequencyInt();
  foundBus = scePowerGetBusClockFrequencyInt();
  cpuCases("cpu-alu", aluCases, sizeof(aluCases) / sizeof(aluCases[0]), 8, AtAll, 0);
  cpuCases("cpu-latency", latencyCases, sizeof(latencyCases) / sizeof(latencyCases[0]), 8, AtAll, 0);
  SceUID block = -1;
  for(memoryBytes = 8u << 20; memoryBytes >= 4u << 20 && block < 0; memoryBytes /= 2) {
    block = sceKernelAllocPartitionMemory(2, "cpu-memory", PSP_SMEM_Low, memoryBytes + 64, 0);
  }
  if(block >= 0) {
    memoryBytes *= 2;  //(the loop halved it once more)
    memory = (unsigned char*)(((unsigned int)sceKernelGetBlockHeadAddr(block) + 63) & ~63u);
    cpuCases("cpu-memory", memoryCases, sizeof(memoryCases) / sizeof(memoryCases[0]), 4, AtAll, memoryBytes);
    ping = sceKernelCreateSema("ping", 0, 0, 1, 0), pong = sceKernelCreateSema("pong", 0, 0, 1, 0);
    partnerStops = 0;
    partner = sceKernelCreateThread("partner", partnerThread, 0x18, 0x1000, 0, 0);
    if(partner >= 0) sceKernelStartThread(partner, 0, 0);
    cpuCases("cpu-syscalls", syscallCases, sizeof(syscallCases) / sizeof(syscallCases[0]), 4, AtTwo, 0);
    if(partner >= 0) {
      partnerStops = 1;
      sceKernelSignalSema(ping, 1);
      SceUInt timeout = 1000000;
      sceKernelWaitThreadEnd(partner, &timeout);
      sceKernelDeleteThread(partner);
    }
    sceKernelDeleteSema(ping), sceKernelDeleteSema(pong);
    sceKernelFreePartitionMemory(block);
  } else {
    print("No 4 MiB of memory free: cpu-memory and cpu-syscalls left out.\n");
  }
  cpuClocks("cpu-clocks");
  return !failed;
}

void cpuManifest(SceUID file) {
  static const char text[] =
    "cpu-alu, cpu-latency, cpu-memory: each run's time in microseconds; cpu-alu and cpu-latency at all four settings\n"
    "(each case's instructions are the loop's addiu, bnez and nop besides what it says), cpu-memory too (RAM from the\n"
    "user partition: a record's size is the working set; 8 MiB where that much was free), cpu-syscalls at 222/111\n"
    "and 333/166 (its enqueues case's time ends when the last sceGeListEnQueue returns). cpu-clocks: one record an\n"
    "attempt (case, 0, the clocks before, 1 | 4 << 16), then what the call gave back, the CPU's and the bus's MHz\n"
    "after, and the time of 65536 passes of cpu-alu's case 1 at them (us). The cases (test case: what; size):\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  static const struct { const char* test; const CpuCase* cases; int count; } tests[] = {
    {"cpu-alu", aluCases, sizeof(aluCases) / sizeof(aluCases[0])},
    {"cpu-latency", latencyCases, sizeof(latencyCases) / sizeof(latencyCases[0])},
    {"cpu-memory", memoryCases, sizeof(memoryCases) / sizeof(memoryCases[0])},
    {"cpu-syscalls", syscallCases, sizeof(syscallCases) / sizeof(syscallCases[0])},
  };
  char line[320];
  for(unsigned int t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
    for(int c = 0; c < tests[t].count; c++) {
      int length = snprintf(line, sizeof(line), "%s %d: %s; %s\n", tests[t].test, c, tests[t].cases[c].name,
                            tests[t].cases[c].unit);
      sceIoWrite(file, line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
    }
  }
  for(unsigned int c = 0; c < sizeof(clockAttempts) / sizeof(clockAttempts[0]); c++) {
    int length = snprintf(line, sizeof(line), "cpu-clocks %u: %s\n", c, clockAttempts[c].name);
    sceIoWrite(file, line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
  }
}
