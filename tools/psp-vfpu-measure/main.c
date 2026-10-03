//psp-vfpu-measure: records what a real PSP's VFPU computes, so Phobos's PSP core can be made to match it exactly
//(see docs/psp-core.md, "Part 3: the VFPU").
//
//It runs the VFPU's math functions over every input of the range each one reduces its argument to, plus a
//million inputs spread over all of them, the random number generator from a range of seeds, and some arithmetic
//on spread-out inputs, and writes every result to a file. It computes nothing itself: the files only hold what
//the hardware gave, and the host works out the rest.
//
//Build with pspdev's toolchain (https://github.com/pspdev/pspdev): make, which gives EBOOT.PBP.
//Run: copy EBOOT.PBP to a folder under PSP/GAME on the memory stick (say PSP/GAME/VFPUMEASURE) and start it from
//the XMB; it needs custom firmware that runs homebrew. Results go to results/ beside EBOOT.PBP: one file per
//test, raw little-endian 32-bit words in the order of the inputs each test describes below, plus manifest.txt.
//A test whose file exists is skipped, so it can be stopped and started again. It writes about 450 MB.

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <string.h>
#include <stdio.h>

PSP_MODULE_INFO("VFPUMEASURE", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define print pspDebugScreenPrintf

//Four 32-bit words, aligned for the VFPU's quad loads and stores (lv.q, sv.q).
typedef struct { unsigned int lane[4]; } __attribute__((aligned(16))) Quad;

enum { ChunkQuads = 16384 };  //how many quads are worked on and written at a time (256 KiB)
static Quad inputS[ChunkQuads], inputT[ChunkQuads], output[ChunkQuads];
static char folder[256];

//The spread-out inputs come from this linear congruential generator, which the host repeats to know what they
//were: state = state * 1664525 + 1013904223, starting from each test's seed.
static unsigned int next(unsigned int* state) {
  return *state = *state * 1664525u + 1013904223u;
}

//One VFPU instruction over quads: each lane of the result from the same lane of the input(s).
#define UNARY(name, instruction) \
  static void name(const Quad* s, const Quad* t, Quad* d, int quads) { \
    (void)t; \
    for(int i = 0; i < quads; i++) { \
      __asm__ volatile("lv.q C000, %1\n" instruction " C010, C000\n" "sv.q C010, %0\n" \
                       : "=m"(d[i]) : "m"(s[i]) : "memory"); \
    } \
  }

#define BINARY(name, instruction) \
  static void name(const Quad* s, const Quad* t, Quad* d, int quads) { \
    for(int i = 0; i < quads; i++) { \
      __asm__ volatile("lv.q C000, %1\n" "lv.q C010, %2\n" instruction " C020, C000, C010\n" "sv.q C020, %0\n" \
                       : "=m"(d[i]) : "m"(s[i]), "m"(t[i]) : "memory"); \
    } \
  }

UNARY(vrcp, "vrcp.q")
UNARY(vnrcp, "vnrcp.q")
UNARY(vrsq, "vrsq.q")
UNARY(vsqrt, "vsqrt.q")
UNARY(vexp2, "vexp2.q")
UNARY(vrexp2, "vrexp2.q")
UNARY(vlog2, "vlog2.q")
UNARY(vsin, "vsin.q")
UNARY(vnsin, "vnsin.q")
UNARY(vcos, "vcos.q")
UNARY(vasin, "vasin.q")
BINARY(vadd, "vadd.q")
BINARY(vsub, "vsub.q")
BINARY(vmul, "vmul.q")
BINARY(vdiv, "vdiv.q")

//vdot.q gives one value per pair of quads; they're stored four to an output quad.
static void vdot(const Quad* s, const Quad* t, Quad* d, int quads) {
  for(int i = 0; i < quads; i++) {
    __asm__ volatile("lv.q C000, %1\n" "lv.q C010, %2\n" "vdot.q S020, C000, C010\n" "sv.s S020, %0\n"
                     : "=m"(d[i / 4].lane[i % 4]) : "m"(s[i]), "m"(t[i]) : "memory");
  }
}

//The inputs, by index k (and the generator's state, for spread-out ones).
typedef unsigned int (*Input)(unsigned int k, unsigned int* state);

static unsigned int fromOne(unsigned int k, unsigned int* state) {  //floats from 1 up: 0x3f800000 + k
  (void)state;
  return 0x3f800000u + k;
}

static unsigned int fromHalf(unsigned int k, unsigned int* state) {  //floats from 1/2 up: 0x3f000000 + k
  (void)state;
  return 0x3f000000u + k;
}

static unsigned int fixed23(unsigned int k, unsigned int* state) {  //k / 2^23, exactly
  (void)state;
  float x = (float)k / 8388608.0f;
  unsigned int bits;
  memcpy(&bits, &x, sizeof(bits));
  return bits;
}

static unsigned int spread(unsigned int k, unsigned int* state) {  //the generator's next 32 bits
  (void)k;
  return next(state);
}

typedef struct {
  const char* name;  //results/<name>.bin
  void (*run)(const Quad*, const Quad*, Quad*, int);
  Input input;       //the inputs, and for two-input instructions the first of each pair, the second following it
  unsigned int count;  //how many results: one per input lane, or for vdot one per pair of quads; a multiple of four
  unsigned int seed;   //for spread-out inputs
  int pairs;           //two inputs per result: each test's generator gives s then t, lane by lane
  const char* about;
} Test;

static const Test tests[] = {
  //every input each function reduces its argument to
  {"vrcp-1-2",     vrcp,   fromOne,  1u << 23, 0, 0, "vrcp.q of 0x3f800000 + k, k < 2^23 (every float from 1 up to 2)"},
  {"vrsq-1-4",     vrsq,   fromOne,  1u << 24, 0, 0, "vrsq.q of 0x3f800000 + k, k < 2^24 (from 1 up to 4)"},
  {"vsqrt-1-4",    vsqrt,  fromOne,  1u << 24, 0, 0, "vsqrt.q of 0x3f800000 + k, k < 2^24 (from 1 up to 4)"},
  {"vexp2-1-2",    vexp2,  fromOne,  1u << 23, 0, 0, "vexp2.q of 0x3f800000 + k, k < 2^23 (from 1 up to 2)"},
  {"vrexp2-1-2",   vrexp2, fromOne,  1u << 23, 0, 0, "vrexp2.q of 0x3f800000 + k, k < 2^23 (from 1 up to 2)"},
  {"vlog2-half-2", vlog2,  fromHalf, 1u << 24, 0, 0, "vlog2.q of 0x3f000000 + k, k < 2^24 (from 1/2 up to 2)"},
  {"vsin-fixed",   vsin,   fixed23,  1u << 23, 0, 0, "vsin.q of k / 2^23, k < 2^23 (a quarter turn in 2^23 steps)"},
  {"vcos-fixed",   vcos,   fixed23,  1u << 23, 0, 0, "vcos.q of k / 2^23, k < 2^23"},
  {"vasin-fixed",  vasin,  fixed23,  (1u << 23) + 4, 0, 0, "vasin.q of k / 2^23, k < 2^23 + 4 (0 to 1, and a little past)"},
  //a million inputs spread over everything: every sign, size, zero, infinity and NaN
  {"vrcp-spread",   vrcp,   spread, 1u << 20, 1,  0, "vrcp.q of spread-out inputs, seed 1"},
  {"vnrcp-spread",  vnrcp,  spread, 1u << 20, 2,  0, "vnrcp.q of spread-out inputs, seed 2"},
  {"vrsq-spread",   vrsq,   spread, 1u << 20, 3,  0, "vrsq.q of spread-out inputs, seed 3"},
  {"vsqrt-spread",  vsqrt,  spread, 1u << 20, 4,  0, "vsqrt.q of spread-out inputs, seed 4"},
  {"vexp2-spread",  vexp2,  spread, 1u << 20, 5,  0, "vexp2.q of spread-out inputs, seed 5"},
  {"vrexp2-spread", vrexp2, spread, 1u << 20, 6,  0, "vrexp2.q of spread-out inputs, seed 6"},
  {"vlog2-spread",  vlog2,  spread, 1u << 20, 7,  0, "vlog2.q of spread-out inputs, seed 7"},
  {"vsin-spread",   vsin,   spread, 1u << 20, 8,  0, "vsin.q of spread-out inputs, seed 8"},
  {"vnsin-spread",  vnsin,  spread, 1u << 20, 9,  0, "vnsin.q of spread-out inputs, seed 9"},
  {"vcos-spread",   vcos,   spread, 1u << 20, 10, 0, "vcos.q of spread-out inputs, seed 10"},
  {"vasin-spread",  vasin,  spread, 1u << 20, 11, 0, "vasin.q of spread-out inputs, seed 11"},
  //arithmetic on spread-out pairs
  {"vadd-spread", vadd, spread, 1u << 18, 12, 1, "vadd.q of spread-out pairs, seed 12: per quad, the 4 lanes of s, then of t"},
  {"vsub-spread", vsub, spread, 1u << 18, 13, 1, "vsub.q of spread-out pairs, seed 13: per quad, the 4 lanes of s, then of t"},
  {"vmul-spread", vmul, spread, 1u << 18, 14, 1, "vmul.q of spread-out pairs, seed 14: per quad, the 4 lanes of s, then of t"},
  {"vdiv-spread", vdiv, spread, 1u << 18, 15, 1, "vdiv.q of spread-out pairs, seed 15: per quad, the 4 lanes of s, then of t"},
  {"vdot-spread", vdot, spread, 1u << 18, 16, 1, "vdot.q of spread-out pairs of quads, seed 16: per result, the 4 lanes of s, then of t"},
};

static int exists(const char* path) {
  SceIoStat status;
  return sceIoGetstat(path, &status) >= 0;
}

//Runs one test into results/<name>.part, renamed to .bin once complete.
static int measure(const Test* test) {
  char done[320], part[320];
  snprintf(done, sizeof(done), "%s/%s.bin", folder, test->name);
  snprintf(part, sizeof(part), "%s/%s.part", folder, test->name);
  if(exists(done)) {
    print("%-14s already done\n", test->name);
    return 1;
  }
  SceUID file = sceIoOpen(part, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) {
    print("%-14s can't write %s\n", test->name, part);
    return 0;
  }
  unsigned int state = test->seed, k = 0;
  unsigned int quads = test->run == vdot ? test->count : test->count / 4;  //vdot: a result per pair of quads
  int line = pspDebugScreenGetY();
  for(unsigned int first = 0; first < quads; first += ChunkQuads) {
    int count = quads - first < ChunkQuads ? quads - first : ChunkQuads;
    for(int i = 0; i < count; i++) {
      for(int lane = 0; lane < 4; lane++) inputS[i].lane[lane] = test->input(k++, &state);
      if(test->pairs) for(int lane = 0; lane < 4; lane++) inputT[i].lane[lane] = test->input(k++, &state);
    }
    test->run(inputS, inputT, output, count);
    int bytes = test->run == vdot ? count * 4 : count * 16;
    if(sceIoWrite(file, output, bytes) != bytes) {
      print("%-14s write failed (memory stick full?)\n", test->name);
      sceIoClose(file);
      return 0;
    }
    if(first % (ChunkQuads * 16) == 0) {
      pspDebugScreenSetXY(0, line);
      print("%-14s %3u%%", test->name, (unsigned int)(100ull * first / quads));
    }
  }
  sceIoClose(file);
  sceIoRename(part, done);
  pspDebugScreenSetXY(0, line);
  print("%-14s done\n", test->name);
  return 1;
}

//The random number generator's state registers (VFPU control registers 136-143), copied into VFPU registers
//with vmfvc (whose control register number is part of the instruction) and stored through an aligned buffer,
//as sv.q needs.
static void readState(unsigned int* rcx) {
  static Quad state[2];
  __asm__ volatile(
    "vmfvc S000, $136\n" "vmfvc S001, $137\n" "vmfvc S002, $138\n" "vmfvc S003, $139\n"
    "vmfvc S010, $140\n" "vmfvc S011, $141\n" "vmfvc S012, $142\n" "vmfvc S013, $143\n"
    "sv.q C000, 0(%0)\n" "sv.q C010, 16(%0)\n"
    :: "r"(state) : "memory");
  memcpy(rcx, state, sizeof(state));
}

static unsigned int draw(void) {
  unsigned int value;
  __asm__ volatile("vrndi.s S100\n" "mfv %0, S100\n" : "=r"(value));
  return value;
}

static void seedWith(unsigned int seed) {
  __asm__ volatile("mtv %0, S100\n" "vrnds.s S100\n" :: "r"(seed));
}

//The random number generator: its state at start, and its numbers before any seed; then for 64 seeds, the
//state right after vrnds.s and 4096 draws with vrndi.s; then for one seed, 1024 draws with vrndi.q, whose lanes
//are filled in their own order.
static int measureRandom(void) {
  char done[320], part[320];
  snprintf(done, sizeof(done), "%s/vrnd.bin", folder);
  snprintf(part, sizeof(part), "%s/vrnd.part", folder);
  if(exists(done)) {
    print("%-14s already done\n", "vrnd");
    return 1;
  }
  static Quad words[1 + 64 * (2 + 1024) + 1024];  //enough for every part below
  unsigned int* w = &words[0].lane[0];
  unsigned int n = 0;

  readState(&w[n]); n += 8;
  for(int i = 0; i < 256; i++) w[n++] = draw();

  static const unsigned int chosen[] = {0, 1, 2, 3, 0x80000000, 0x7fffffff, 0xffffffff, 0x12345678,
                                        0xdeadbeef, 0x3f800000, 0x40000000, 0xc0000000, 0x0000ffff, 0xffff0000,
                                        0x55555555, 0xaaaaaaaa};
  unsigned int state = 17;
  for(int s = 0; s < 64; s++) {
    unsigned int seed = s < 16 ? chosen[s] : next(&state);
    w[n++] = seed;
    seedWith(seed);
    readState(&w[n]); n += 8;
    for(int i = 0; i < 4096 / 4; i++) {
      w[n++] = draw(); w[n++] = draw(); w[n++] = draw(); w[n++] = draw();
    }
    if(n > sizeof(words) / 4 - 4200) break;
  }

  seedWith(0x12345678);
  static Quad drawn;
  for(int i = 0; i < 256; i++) {
    __asm__ volatile("vrndi.q C200\n" "sv.q C200, %0\n" : "=m"(drawn) :: "memory");
    memcpy(&w[n], &drawn, sizeof(drawn));
    n += 4;
  }

  SceUID file = sceIoOpen(part, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0 || sceIoWrite(file, w, n * 4) != (int)(n * 4)) {
    print("%-14s write failed\n", "vrnd");
    if(file >= 0) sceIoClose(file);
    return 0;
  }
  sceIoClose(file);
  sceIoRename(part, done);
  print("%-14s done\n", "vrnd");
  return 1;
}

static void writeManifest(void) {
  char path[320], text[512];
  snprintf(path, sizeof(path), "%s/manifest.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  int length = snprintf(text, sizeof(text),
    "psp-vfpu-measure 1\nfirmware devkit version %08x\n"
    "spread-out inputs: state = state * 1664525 + 1013904223 from the seed, taking each new state\n",
    sceKernelDevkitVersion());
  sceIoWrite(file, text, length);
  for(unsigned int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    length = snprintf(text, sizeof(text), "%s.bin: %u results; %s\n", tests[i].name, tests[i].count, tests[i].about);
    sceIoWrite(file, text, length);
  }
  length = snprintf(text, sizeof(text),
    "vrnd.bin: the RCX registers (8 words) at start and 256 vrndi.s draws; then for 64 seeds (16 chosen, then the "
    "generator from 17): the seed, the RCX registers after vrnds.s, 4096 vrndi.s draws; then after seeding "
    "0x12345678, 256 vrndi.q (4 words each, lane 0 first)\n");
  sceIoWrite(file, text, length);
  sceIoClose(file);
}

int main(int argc, char** argv) {
  pspDebugScreenInit();
  print("psp-vfpu-measure: what this PSP's VFPU computes\n\n");

  //results/ beside EBOOT.PBP
  strncpy(folder, argc > 0 ? argv[0] : "ms0:/PSP/GAME/VFPUMEASURE/EBOOT.PBP", sizeof(folder) - 16);
  char* slash = strrchr(folder, '/');
  if(slash) *slash = 0;
  strcat(folder, "/results");
  sceIoMkdir(folder, 0777);
  print("writing to %s\n\n", folder);
  writeManifest();

  int ok = 1;
  for(unsigned int i = 0; ok && i < sizeof(tests) / sizeof(tests[0]); i++) ok = measure(&tests[i]);
  if(ok) ok = measureRandom();

  print(ok ? "\nAll done. Press X to leave.\n" : "\nStopped. Press X to leave.\n");
  SceCtrlData pad;
  do {
    sceCtrlReadBufferPositive(&pad, 1);
  } while(!(pad.Buttons & PSP_CTRL_CROSS));
  sceKernelExitGame();
  return 0;
}
