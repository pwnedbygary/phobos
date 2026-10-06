//psp-vfpu-measure: records what a real PSP's VFPU computes, so Phobos's PSP core can be made to match it exactly
//(see docs/psp-core.md, "Part 3: the VFPU", and docs/psp-vfpu-measurements.md).
//
//It works in rounds, picked when it starts, each writing its own files:
//- Round 1 (O): the VFPU's math functions over every input of the range each one reduces its argument to, plus a
//  million inputs spread over all of them, the random number generator from a range of seeds, and some arithmetic
//  on spread-out inputs. About 450 MB.
//- Round 2 (X): what round 1 couldn't settle. vlog2 over whole binades above 4; dot products, sums and averages
//  built to show how the VFPU adds several numbers; every half float through vh2f, and a million floats through
//  vf2h; the integer divide, and the FPU's conversions and rounding modes; and the instruction recorder, which runs
//  every VFPU instruction pspdev's assembler knows (ops.h) on random register states. About 230 MB.
//It computes nothing itself: the files only hold what the hardware gave (and for the smaller tests, the inputs),
//and the host works out the rest.
//
//Build with pspdev's toolchain (https://github.com/pspdev/pspdev): make, which gives EBOOT.PBP.
//Run: copy EBOOT.PBP to a folder under PSP/GAME on the memory stick (say PSP/GAME/VFPUMEASURE) and start it from
//the XMB; it needs custom firmware that runs homebrew. Results go to results/ beside EBOOT.PBP: one file per
//test, raw little-endian 32-bit words in the order each test describes below, plus manifest.txt (round 1) or
//manifest2.txt (round 2). A test whose file exists is skipped, so a round can be stopped and started again. A test
//that didn't finish runs once more; if it doesn't finish that time either, the next start gives up on it (see
//begin), so a test that stops the PSP can't hold up the rest. Each test's line on the screen says when it's
//running, and the program keeps telling the PSP it's busy, so the power-save timer doesn't put it to sleep.

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <psppower.h>
#include <psputils.h>
#include <string.h>
#include <stdio.h>

PSP_MODULE_INFO("VFPUMEASURE", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define print pspDebugScreenPrintf

//make SMOKE=1 builds a quick version for trying the program in an emulator before a PSP runs it (PPSSPP's
//PPSSPPHeadless, which has no buttons to press): it runs round 2 straight away with its big tests cut short, and
//leaves when done. Its results say nothing about a PSP.
#ifdef SMOKE
enum { Shrink = 8 };
#else
enum { Shrink = 0 };
#endif

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

//One result per quad, from all of its lanes (vfad, vavg), stored four to an output quad.
#define REDUCE(name, instruction) \
  static void name(const Quad* s, const Quad* t, Quad* d, int quads) { \
    (void)t; \
    for(int i = 0; i < quads; i++) { \
      __asm__ volatile("lv.q C000, %1\n" instruction " S020, C000\n" "sv.s S020, %0\n" \
                       : "=m"(d[i / 4].lane[i % 4]) : "m"(s[i]) : "memory"); \
    } \
  }

//One result per pair of quads (vdot, vhdp), stored four to an output quad.
#define REDUCE2(name, instruction) \
  static void name(const Quad* s, const Quad* t, Quad* d, int quads) { \
    for(int i = 0; i < quads; i++) { \
      __asm__ volatile("lv.q C000, %1\n" "lv.q C010, %2\n" instruction " S020, C000, C010\n" "sv.s S020, %0\n" \
                       : "=m"(d[i / 4].lane[i % 4]) : "m"(s[i]), "m"(t[i]) : "memory"); \
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
REDUCE2(vdot, "vdot.q")
REDUCE2(vhdp, "vhdp.q")
REDUCE(vfad, "vfad.q")
REDUCE(vavg, "vavg.q")

typedef struct Test Test;

//The inputs, by index k (and the generator's state, for spread-out ones).
typedef unsigned int (*Input)(const Test* test, unsigned int k, unsigned int* state);

//Or a whole result's inputs at once, for the tests built to show how several numbers are added: s's quad, and
//for two-input instructions t's.
typedef void (*Fill)(Quad* s, Quad* t, unsigned int* state);

struct Test {
  const char* name;    //results/<name>.bin
  void (*run)(const Quad*, const Quad*, Quad*, int);
  Input input;         //the inputs, and for two-input instructions the first of each pair, the second following it
  unsigned int count;  //how many results: one per input lane, or for reductions one per quad (or pair of quads)
  unsigned int seed;   //for spread-out inputs
  int pairs;           //two inputs per result: each test's generator gives s then t, lane by lane
  const char* about;
  unsigned int first;  //for sweeps: the first input
  Fill fill;           //instead of input, for the tests whose inputs are built a quad at a time
  int reduces;         //one result per quad (or pair of quads), stored four to an output quad
};

static unsigned int fromOne(const Test* test, unsigned int k, unsigned int* state) {  //floats from 1 up
  (void)test; (void)state;
  return 0x3f800000u + k;
}

static unsigned int fromHalf(const Test* test, unsigned int k, unsigned int* state) {  //floats from 1/2 up
  (void)test; (void)state;
  return 0x3f000000u + k;
}

static unsigned int fixed23(const Test* test, unsigned int k, unsigned int* state) {  //k / 2^23, exactly
  (void)test; (void)state;
  float x = (float)k / 8388608.0f;
  unsigned int bits;
  memcpy(&bits, &x, sizeof(bits));
  return bits;
}

static unsigned int spread(const Test* test, unsigned int k, unsigned int* state) {  //the generator's next 32 bits
  (void)test; (void)k;
  return next(state);
}

static unsigned int sweep(const Test* test, unsigned int k, unsigned int* state) {  //every float from first up
  (void)state;
  return test->first + k;
}

//A float with a random sign and mantissa, and an exponent from low up to low + 2^bits - 1: one draw.
static unsigned int ranged(unsigned int* state, unsigned int low, unsigned int bits) {
  unsigned int w = next(state);
  return (w & 0x807fffffu) | (low + (w >> 23 & ((1u << bits) - 1))) << 23;
}

//Two different lanes, j below k: two draws.
static void twoLanes(unsigned int* state, unsigned int* j, unsigned int* k) {
  unsigned int a = next(state) >> 30, b = (a + 1 + next(state) % 3) & 3;
  *j = a < b ? a : b;
  *k = a < b ? b : a;
}

//The adding tests' inputs, drawn in the order written (lane 0 first). Exponents 112-143 put the products of a dot
//product up to 62 binades apart; 125-128 keeps them close, so that they overlap and cancel.
static void dotOne(Quad* s, Quad* t, unsigned int* state) {  //one product: t is zero but for one lane
  unsigned int j = next(state) >> 30;
  for(int i = 0; i < 4; i++) s->lane[i] = ranged(state, 112, 5);
  for(int i = 0; i < 4; i++) t->lane[i] = i == (int)j ? ranged(state, 112, 5) : 0;
}

static void dotTwo(Quad* s, Quad* t, unsigned int* state) {  //two products: t is zero but for two lanes
  unsigned int j, k;
  twoLanes(state, &j, &k);
  for(int i = 0; i < 4; i++) s->lane[i] = ranged(state, 112, 5);
  for(int i = 0; i < 4; i++) t->lane[i] = i == (int)j || i == (int)k ? ranged(state, 112, 5) : 0;
}

static void dotClose(Quad* s, Quad* t, unsigned int* state) {  //four products of similar size
  for(int i = 0; i < 4; i++) s->lane[i] = ranged(state, 125, 2);
  for(int i = 0; i < 4; i++) t->lane[i] = ranged(state, 125, 2);
}

static void dotShort(Quad* s, Quad* t, unsigned int* state) {  //8-bit significands: every product is exact
  for(int i = 0; i < 4; i++) s->lane[i] = ranged(state, 120, 4) & 0xffff0000u;
  for(int i = 0; i < 4; i++) t->lane[i] = ranged(state, 120, 4) & 0xffff0000u;
}

static void sumClose(Quad* s, Quad* t, unsigned int* state) {  //four numbers of similar size
  (void)t;
  for(int i = 0; i < 4; i++) s->lane[i] = ranged(state, 124, 3);
}

static void sumTwo(Quad* s, Quad* t, unsigned int* state) {  //two numbers, up to 31 binades apart; the rest zero
  (void)t;
  unsigned int j, k;
  twoLanes(state, &j, &k);
  for(int i = 0; i < 4; i++) s->lane[i] = i == (int)j || i == (int)k ? ranged(state, 112, 5) : 0;
}

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
  {"vdot-spread", vdot, spread, 1u << 18, 16, 1, "vdot.q of spread-out pairs of quads, seed 16: per result, the 4 lanes of s, then of t",
   0, 0, 1},
};

//Round 2's tests that run through measure(). (The rest of round 2 has functions of its own, below.)
static const Test tests2[] = {
  //vlog2 over a whole binade for each size of result above 4 (23 significant bits: 2^-21 up to 2^-16 apart)
  {"vlog2-4-8",     vlog2, sweep, (1u << 23) >> Shrink, 0, 0, "vlog2.q of 0x40800000 + k, k < 2^23 (from 4 up to 8)", 0x40800000u},
  {"vlog2-16-32",   vlog2, sweep, (1u << 23) >> Shrink, 0, 0, "vlog2.q of 0x41800000 + k, k < 2^23 (from 16 up to 32)", 0x41800000u},
  {"vlog2-256-512", vlog2, sweep, (1u << 23) >> Shrink, 0, 0, "vlog2.q of 0x43800000 + k, k < 2^23 (from 2^8 up to 2^9)", 0x43800000u},
  {"vlog2-2p16",    vlog2, sweep, (1u << 23) >> Shrink, 0, 0, "vlog2.q of 0x47800000 + k, k < 2^23 (from 2^16 up to 2^17)", 0x47800000u},
  {"vlog2-2p32",    vlog2, sweep, (1u << 23) >> Shrink, 0, 0, "vlog2.q of 0x4f800000 + k, k < 2^23 (from 2^32 up to 2^33)", 0x4f800000u},
  {"vlog2-2p64",    vlog2, sweep, (1u << 23) >> Shrink, 0, 0, "vlog2.q of 0x5f800000 + k, k < 2^23 (from 2^64 up to 2^65)", 0x5f800000u},
  //adding several numbers: dot products (vdot, vhdp), sums (vfad) and averages (vavg) of inputs built for it
  {"vdot-one",   vdot, 0, (1u << 18) >> Shrink, 31, 1, "vdot.q, seed 31: one product (dotOne)", 0, dotOne, 1},
  {"vdot-two",   vdot, 0, (1u << 20) >> Shrink, 32, 1, "vdot.q, seed 32: two products (dotTwo)", 0, dotTwo, 1},
  {"vdot-close", vdot, 0, (1u << 20) >> Shrink, 33, 1, "vdot.q, seed 33: four products of similar size (dotClose)", 0, dotClose, 1},
  {"vdot-short", vdot, 0, (1u << 18) >> Shrink, 34, 1, "vdot.q, seed 34: exact products of 8-bit significands (dotShort)", 0, dotShort, 1},
  {"vhdp-close", vhdp, 0, (1u << 18) >> Shrink, 35, 1, "vhdp.q, seed 35: as vdot-close (dotClose)", 0, dotClose, 1},
  {"vfad-close", vfad, 0, (1u << 20) >> Shrink, 36, 0, "vfad.q, seed 36: four numbers of similar size (sumClose)", 0, sumClose, 1},
  {"vfad-two",   vfad, 0, (1u << 18) >> Shrink, 37, 0, "vfad.q, seed 37: two numbers, the rest zero (sumTwo)", 0, sumTwo, 1},
  {"vavg-close", vavg, 0, (1u << 18) >> Shrink, 38, 0, "vavg.q, seed 38: as vfad-close (sumClose)", 0, sumClose, 1},
};

static int exists(const char* path) {
  SceIoStat status;
  return sceIoGetstat(path, &status) >= 0;
}

//Tells the PSP it's in use, so its power-save timer (Auto Sleep, Backlight Auto-Off) starts over: a long round with
//nothing pressed would otherwise be put to sleep part way.
static void awake(void) { scePowerTick(PSP_POWER_TICK_ALL); }

static int mark(const char* path) {
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return 0;
  sceIoClose(file);
  return 1;
}

//A test's file is written as results/<name>.part and renamed to .bin once complete, so a stopped test leaves no
//.bin behind and runs again next time. If its .part is there when it starts, the last run didn't finish it (the PSP
//stopped, or was stopped, during it): it runs once more, with <name>.again marking that; and if it doesn't finish
//then either, the next start gives up on it, renaming what it wrote to <name>.stopped, and the round goes on
//without it.
typedef struct {
  char done[320], part[320], again[320];
  SceUID file;
  int line;
} Output;

static int begin(Output* out, const char* name) {
  char stopped[320];
  snprintf(out->done, sizeof(out->done), "%s/%s.bin", folder, name);
  snprintf(out->part, sizeof(out->part), "%s/%s.part", folder, name);
  snprintf(out->again, sizeof(out->again), "%s/%s.again", folder, name);
  snprintf(stopped, sizeof(stopped), "%s/%s.stopped", folder, name);
  awake();
  if(exists(out->done)) {
    print("%-14s already done\n", name);
    return 0;
  }
  if(exists(stopped)) {
    print("%-14s given up on (it stopped twice)\n", name);
    return 0;
  }
  int retrying = exists(out->part);
  if(retrying && exists(out->again)) {
    sceIoRename(out->part, stopped);
    sceIoRemove(out->again);
    print("%-14s stopped twice: given up on\n", name);
    return 0;
  }
  out->file = sceIoOpen(out->part, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(out->file < 0) {
    print("%-14s can't write %s\n", name, out->part);
    return -1;
  }
  if(retrying) {  //marked only once it's really running again, so a file that can't be written costs no retry
    if(!mark(out->again)) {  //unmarked, a test that stops the PSP every time would never be given up on
      sceIoClose(out->file);
      print("%-14s can't write %s (memory stick full?)\n", name, out->again);
      return -1;
    }
    print("%-14s didn't finish last time: once more\n", name);
  }
  out->line = pspDebugScreenGetY();
  print("%-14s running", name);
  return 1;
}

static int writeOut(Output* out, const char* name, const void* data, int bytes) {
  awake();  //every chunk a long test writes (256 KiB), so no stretch of work goes without telling the PSP
  if(sceIoWrite(out->file, data, bytes) == bytes) return 1;
  print("%-14s write failed (memory stick full?)\n", name);
  sceIoClose(out->file);
  sceIoRemove(out->again);  //a failed write isn't the PSP stopping: the next start tries it again, not giving up
  return 0;
}

static void progress(Output* out, const char* name, unsigned int done, unsigned int total) {
  awake();
  pspDebugScreenSetXY(0, out->line);
  print("%-14s %3u%%   ", name, (unsigned int)(100ull * done / total));
}

static int finish(Output* out, const char* name) {
  sceIoClose(out->file);
  sceIoRename(out->part, out->done);
  sceIoRemove(out->again);
  pspDebugScreenSetXY(0, out->line);
  print("%-14s done   \n", name);
  return 1;
}

//Runs one test into its file.
static int measure(const Test* test) {
  Output out;
  int started = begin(&out, test->name);
  if(started <= 0) return started == 0;
  unsigned int state = test->seed, k = 0;
  unsigned int quads = test->reduces ? test->count : test->count / 4;
  for(unsigned int first = 0; first < quads; first += ChunkQuads) {
    int count = quads - first < ChunkQuads ? quads - first : ChunkQuads;
    for(int i = 0; i < count; i++) {
      if(test->fill) {
        test->fill(&inputS[i], &inputT[i], &state);
        continue;
      }
      for(int lane = 0; lane < 4; lane++) inputS[i].lane[lane] = test->input(test, k++, &state);
      if(test->pairs) for(int lane = 0; lane < 4; lane++) inputT[i].lane[lane] = test->input(test, k++, &state);
    }
    test->run(inputS, inputT, output, count);
    if(!writeOut(&out, test->name, output, test->reduces ? count * 4 : count * 16)) return 0;
    if(first % (ChunkQuads * 16) == 0) progress(&out, test->name, first, quads);
  }
  return finish(&out, test->name);
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
  Output out;
  int started = begin(&out, "vrnd");
  if(started <= 0) return started == 0;
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

  if(!writeOut(&out, "vrnd", w, n * 4)) return 0;
  return finish(&out, "vrnd");
}

//Every half float through vh2f.s: input word j (j < 32768) holds halves 2j (low 16 bits) and 2j + 1 (high), so
//all 65536 halves appear once; per word, the two results (lanes 0 and 1).
static int measureHalves(void) {
  Output out;
  int started = begin(&out, "vh2f-all");
  if(started <= 0) return started == 0;
  static unsigned int results[65536] __attribute__((aligned(16)));
  for(unsigned int j = 0; j < 32768; j++) {
    unsigned int word = (2 * j + 1) << 16 | 2 * j;
    __asm__ volatile("mtv %2, S000\n" "vh2f.s C010, S000\n" "sv.s S010, %0\n" "sv.s S011, %1\n"
                     : "=m"(results[2 * j]), "=m"(results[2 * j + 1]) : "r"(word) : "memory");
  }
  if(!writeOut(&out, "vh2f-all", results, sizeof(results))) return 0;
  return finish(&out, "vh2f-all");
}

//A million spread-out floats through vf2h.q (seed 22): per quad of inputs, the two result words (lanes 0 and 1),
//each holding two halves.
static int measureFloatsToHalves(void) {
  Output out;
  int started = begin(&out, "vf2h-spread");
  if(started <= 0) return started == 0;
  unsigned int state = 22, quads = (1u << 18) >> Shrink;
  for(unsigned int first = 0; first < quads; first += ChunkQuads) {
    int count = quads - first < ChunkQuads ? quads - first : ChunkQuads;
    for(int i = 0; i < count; i++) {
      for(int lane = 0; lane < 4; lane++) inputS[i].lane[lane] = next(&state);
      __asm__ volatile("lv.q C000, %2\n" "vf2h.q C010, C000\n" "sv.s S010, %0\n" "sv.s S011, %1\n"
                       : "=m"(output[i / 2].lane[i % 2 * 2]), "=m"(output[i / 2].lane[i % 2 * 2 + 1])
                       : "m"(inputS[i]) : "memory");
    }
    if(!writeOut(&out, "vf2h-spread", output, count * 8)) return 0;
    progress(&out, "vf2h-spread", first, quads);
  }
  return finish(&out, "vf2h-spread");
}

//Values worth trying anywhere: zeros, denormals, the smallest and largest normal numbers, infinities, NaNs (quiet
//and signaling, either sign), and numbers at the edges of what integers and the math functions reach.
static const unsigned int specials[32] = {
  0x00000000, 0x80000000, 0x00000001, 0x807fffff, 0x00800000, 0x80800000, 0x3f800000, 0xbf800000,
  0x40000000, 0x3f000000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000,
  0x7f800001, 0xff800001, 0x7fffffff, 0x4b800000, 0x4f000000, 0x4f800000, 0xcf000000, 0x3f7fffff,
  0x3f800001, 0x40490fdb, 0x3fc90fdb, 0x47000000, 0xc7000000, 0x477fff00, 0x437f0000, 0x3effffff,
};

//One input word of many kinds, from two draws: the first picks the kind (its top three bits), the second fills
//it in.
static unsigned int mixed(unsigned int* state) {
  unsigned int kind = next(state) >> 29, w = next(state);
  switch(kind) {
  case 0: return w;                                                         //any 32 bits
  case 1: case 2: return (w & 0x807fffffu) | (120 + (w >> 23 & 15)) << 23;  //a float from 2^-7 up to 2^9
  case 3: return (w & 0x807fffffu) | (124 + (w >> 23 & 3)) << 23;           //a float from 1/8 up to 2
  case 4: return (unsigned int)(int)(short)w;                               //a small integer, either sign
  case 5: return specials[w & 31];
  case 6: return (w & 0x807fffffu) | (110 + (w >> 23 & 15)) << 23;          //a float from 2^-17 up to 2^-1
  default: return w & 0xffff;                                               //a small positive integer
  }
}

//Integer division: div and divu of 1024 pairs of chosen numbers (each with each), then 7168 pairs of spread-out
//ones (seed 21). Per pair: a, b, then div's lo and hi, then divu's lo and hi.
static const unsigned int chosenIntegers[32] = {
  0, 1, 2, 3, 7, 10, 0xffffffff, 0xfffffffe, 0xfffffff9, 0xfffffff6, 0x80000000, 0x80000001, 0x7fffffff,
  0x7ffffffe, 0x40000000, 0xc0000000, 0x0000ffff, 0xffff0000, 0x00010000, 0xffff8000, 0x00007fff, 0x00008000,
  0x12345678, 0x87654321, 0x55555555, 0xaaaaaaaa, 100, 1000, 0xffffff9c, 0xfffffc18, 0x3b9aca00, 0xc4653600,
};

static int measureDivide(void) {
  Output out;
  int started = begin(&out, "ipu-divide");
  if(started <= 0) return started == 0;
  static unsigned int w[8192 * 6];
  unsigned int state = 21;
  for(int i = 0; i < 8192; i++) {
    unsigned int a = i < 1024 ? chosenIntegers[i / 32] : next(&state);
    unsigned int b = i < 1024 ? chosenIntegers[i % 32] : next(&state);
    unsigned int lo, hi, ulo, uhi;
    __asm__ volatile(".set push\n" ".set noreorder\n" ".set nomacro\n"
                     "div $0, %4, %5\n" "mflo %0\n" "mfhi %1\n"
                     "divu $0, %4, %5\n" "mflo %2\n" "mfhi %3\n"
                     ".set pop\n"
                     : "=&r"(lo), "=&r"(hi), "=&r"(ulo), "=&r"(uhi) : "r"(a), "r"(b) : "hi", "lo");
    unsigned int* p = &w[i * 6];
    p[0] = a; p[1] = b; p[2] = lo; p[3] = hi; p[4] = ulo; p[5] = uhi;
  }
  if(!writeOut(&out, "ipu-divide", w, sizeof(w))) return 0;
  return finish(&out, "ipu-divide");
}

//The FPU under a rounding mode (FCSR bits 0-1: 0 nearest, 1 toward zero, 2 up, 3 down), set just for the one
//instruction. Its flags, enables and causes (bits 2-17) are cleared for it, so no exception can be enabled; the
//bits above (flush to zero among them) stay as they were. All of FCSR is put back after.
static unsigned int fcsrFor(unsigned int mode) {
  unsigned int fcsr;
  __asm__ volatile("cfc1 %0, $31\n" : "=r"(fcsr));
  return (fcsr & ~0x3ffffu) | mode;
}

#define FPU_UNARY(name, instruction) \
  static unsigned int name(unsigned int mode, unsigned int x) { \
    unsigned int result, saved, fcsr = fcsrFor(mode); \
    __asm__ volatile(".set push\n" ".set noreorder\n" \
                     "cfc1 %1, $31\n" "nop\n" "ctc1 %3, $31\n" "nop\n" "mtc1 %2, $f0\n" "nop\n" \
                     instruction " $f2, $f0\n" "nop\n" "mfc1 %0, $f2\n" "nop\n" "ctc1 %1, $31\n" "nop\n" \
                     ".set pop\n" \
                     : "=&r"(result), "=&r"(saved) : "r"(x), "r"(fcsr) : "$f0", "$f2"); \
    return result; \
  }

#define FPU_BINARY(name, instruction) \
  static unsigned int name(unsigned int mode, unsigned int x, unsigned int y) { \
    unsigned int result, saved, fcsr = fcsrFor(mode); \
    __asm__ volatile(".set push\n" ".set noreorder\n" \
                     "cfc1 %1, $31\n" "nop\n" "ctc1 %4, $31\n" "nop\n" "mtc1 %2, $f0\n" "mtc1 %3, $f1\n" "nop\n" \
                     instruction " $f2, $f0, $f1\n" "nop\n" "mfc1 %0, $f2\n" "nop\n" "ctc1 %1, $31\n" "nop\n" \
                     ".set pop\n" \
                     : "=&r"(result), "=&r"(saved) : "r"(x), "r"(y), "r"(fcsr) : "$f0", "$f1", "$f2"); \
    return result; \
  }

FPU_UNARY(cvtws, "cvt.w.s")
FPU_UNARY(roundws, "round.w.s")
FPU_UNARY(truncws, "trunc.w.s")
FPU_UNARY(ceilws, "ceil.w.s")
FPU_UNARY(floorws, "floor.w.s")
FPU_UNARY(sqrts, "sqrt.s")
FPU_BINARY(adds, "add.s")
FPU_BINARY(subs, "sub.s")
FPU_BINARY(muls, "mul.s")
FPU_BINARY(divs, "div.s")

//Conversions to integers: per input (the 32 specials, then 4064 of many kinds from seed 23), the input, then for
//each rounding mode: cvt.w.s (which follows the mode), round.w.s, trunc.w.s, ceil.w.s and floor.w.s.
static int measureConvert(void) {
  Output out;
  int started = begin(&out, "fpu-convert");
  if(started <= 0) return started == 0;
  static unsigned int w[4096 * 21];
  unsigned int state = 23;
  for(int i = 0; i < 4096; i++) {
    unsigned int x = i < 32 ? specials[i] : mixed(&state);
    unsigned int* p = &w[i * 21];
    *p++ = x;
    for(unsigned int mode = 0; mode < 4; mode++) {
      *p++ = cvtws(mode, x); *p++ = roundws(mode, x); *p++ = truncws(mode, x);
      *p++ = ceilws(mode, x); *p++ = floorws(mode, x);
    }
  }
  if(!writeOut(&out, "fpu-convert", w, sizeof(w))) return 0;
  return finish(&out, "fpu-convert");
}

//Arithmetic in each rounding mode: per pair (4096, of many kinds, from seed 24), a and b, then for each mode:
//add.s, sub.s, mul.s, div.s, and sqrt.s of a.
static int measureArithmetic(void) {
  Output out;
  int started = begin(&out, "fpu-arith");
  if(started <= 0) return started == 0;
  static unsigned int w[4096 * 22];
  unsigned int state = 24;
  for(int i = 0; i < 4096; i++) {
    unsigned int a = mixed(&state), b = mixed(&state);
    unsigned int* p = &w[i * 22];
    *p++ = a; *p++ = b;
    for(unsigned int mode = 0; mode < 4; mode++) {
      *p++ = adds(mode, a, b); *p++ = subs(mode, a, b); *p++ = muls(mode, a, b);
      *p++ = divs(mode, a, b); *p++ = sqrts(mode, a);
    }
  }
  if(!writeOut(&out, "fpu-arith", w, sizeof(w))) return 0;
  return finish(&out, "fpu-arith");
}

//The instruction recorder. Each entry of ops.h is one VFPU instruction (after up to three prefix instructions)
//that reads matrix 0 (and matrix 1) and writes matrix 2. Its words are copied into stub, a tiny function that
//returns right after them, and run OpRuns times. Before each run, matrices 0 and 1 get inputs of many kinds,
//matrix 2 a marker in every lane (0xdead0000 + its place, so lanes an instruction leaves alone show), and the
//condition codes six random bits. After it, matrix 2 and the condition codes are kept.
typedef struct { unsigned char count; unsigned int words[4]; const char* text; } Op;
#include "ops.h"

enum { OpRuns = 64, RunWords = 51 };
static unsigned int stub[8] __attribute__((aligned(64)));

//One run. m0, m1 and m2 are matrices 0-2, by column (m[c].lane[r] is row r of column c). Matrix 3 is scratch,
//for moving the condition codes (VFPU control register 131), which is how they're set and read back.
static void runOp(const Quad* m0, const Quad* m1, Quad* m2, unsigned int cc, unsigned int* before,
                  unsigned int* after) {
  __asm__ volatile(
    ".set push\n" ".set noreorder\n"
    "lv.q C000, 0(%2)\n" "lv.q C010, 16(%2)\n" "lv.q C020, 32(%2)\n" "lv.q C030, 48(%2)\n"
    "lv.q C100, 0(%3)\n" "lv.q C110, 16(%3)\n" "lv.q C120, 32(%3)\n" "lv.q C130, 48(%3)\n"
    "lv.q C200, 0(%4)\n" "lv.q C210, 16(%4)\n" "lv.q C220, 32(%4)\n" "lv.q C230, 48(%4)\n"
    "mtv %5, S300\n" "vmtvc $131, S300\n" "vmfvc S301, $131\n" "mfv %0, S301\n"
    "jalr %6\n" "nop\n"
    "vmfvc S302, $131\n" "mfv %1, S302\n"
    "sv.q C200, 0(%4)\n" "sv.q C210, 16(%4)\n" "sv.q C220, 32(%4)\n" "sv.q C230, 48(%4)\n"
    ".set pop\n"
    : "=&r"(*before), "=&r"(*after)
    : "r"(m0), "r"(m1), "r"(m2), "r"(cc), "r"(stub)
    : "memory", "$31");
}

//ops.bin: "VOPS", version 1, the number of entries and runs per entry; then per entry its word count and four
//words (unused ones zero), then per run: matrix 0 and matrix 1 (16 words each, by column), the condition codes
//set and as read back before the instruction, the condition codes after it, and matrix 2 after it (by column).
//Run r of entry e draws its inputs from seed 0x10000 + e: matrix 0 then matrix 1 from mixed(), column by column,
//then the condition codes as the top six bits of one more draw.
static int measureOps(void) {
  Output out;
  int started = begin(&out, "ops");
  if(started <= 0) return started == 0;
  unsigned int header[4] = {0x53504f56, 1, OP_COUNT, OpRuns};
  if(!writeOut(&out, "ops", header, sizeof(header))) return 0;
  static unsigned int block[5 + OpRuns * RunWords];
  static Quad m0[4], m1[4], m2[4];
  for(int e = 0; e < OP_COUNT; e++) {
    const Op* op = &ops[e];
    awake();
    pspDebugScreenSetXY(0, out.line);
    print("ops %4d/%d %-40.40s", e + 1, OP_COUNT, op->text);
    int n = 0;
    for(int i = 0; i < op->count; i++) stub[n++] = op->words[i];
    stub[n++] = 0x03e00008;  //jr ra
    stub[n++] = 0x00000000;  //nop, in its delay slot
    sceKernelDcacheWritebackInvalidateRange(stub, sizeof(stub));
    sceKernelIcacheInvalidateRange(stub, sizeof(stub));
    unsigned int* b = block;
    *b++ = op->count;
    for(int i = 0; i < 4; i++) *b++ = op->words[i];
    unsigned int state = 0x10000 + e;
    for(int run = 0; run < OpRuns; run++) {
      for(int c = 0; c < 4; c++) for(int r = 0; r < 4; r++) m0[c].lane[r] = mixed(&state);
      for(int c = 0; c < 4; c++) for(int r = 0; r < 4; r++) m1[c].lane[r] = mixed(&state);
      unsigned int cc = next(&state) >> 26, before, after;
      for(int c = 0; c < 4; c++) for(int r = 0; r < 4; r++) m2[c].lane[r] = 0xdead0000 | c << 4 | r;
      runOp(m0, m1, m2, cc, &before, &after);
      memcpy(b, m0, 64); b += 16;
      memcpy(b, m1, 64); b += 16;
      *b++ = cc; *b++ = before; *b++ = after;
      memcpy(b, m2, 64); b += 16;
    }
    if(!writeOut(&out, "ops", block, sizeof(block))) return 0;
  }
  return finish(&out, "ops");
}

//ops.txt: what each entry of ops.bin is, for people reading the file.
static void writeOpsList(void) {
  char path[320], text[160];
  snprintf(path, sizeof(path), "%s/ops.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  for(int e = 0; e < OP_COUNT; e++) {
    int length = snprintf(text, sizeof(text), "%d: %08x %08x %08x %08x (%d): %s\n", e, ops[e].words[0],
                          ops[e].words[1], ops[e].words[2], ops[e].words[3], ops[e].count, ops[e].text);
    sceIoWrite(file, text, length);
  }
  sceIoClose(file);
}

static void writeLine(SceUID file, const char* text) {
  sceIoWrite(file, text, strlen(text));
}

static void writeManifest(int round, const Test* list, unsigned int count) {
  char path[320], text[512];
  snprintf(path, sizeof(path), "%s/%s", folder, round == 1 ? "manifest.txt" : "manifest2.txt");
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  snprintf(text, sizeof(text),
    "psp-vfpu-measure round %d\nfirmware devkit version %08x\n"
    "spread-out inputs: state = state * 1664525 + 1013904223 from the seed, taking each new state\n",
    round, sceKernelDevkitVersion());
  writeLine(file, text);
  for(unsigned int i = 0; i < count; i++) {
    snprintf(text, sizeof(text), "%s.bin: %u results; %s\n", list[i].name, list[i].count, list[i].about);
    writeLine(file, text);
  }
  if(round == 1) {
    writeLine(file,
      "vrnd.bin: the RCX registers (8 words) at start and 256 vrndi.s draws; then for 64 seeds (16 chosen, then the "
      "generator from 17): the seed, the RCX registers after vrnds.s, 4096 vrndi.s draws; then after seeding "
      "0x12345678, 256 vrndi.q (4 words each, lane 0 first)\n");
  } else {
    writeLine(file,
      "the adding tests draw each result's inputs lane 0 first, s then t (see main.c: dotOne, dotTwo, dotClose, "
      "dotShort, sumClose, sumTwo); one result per quad or pair of quads\n"
      "vh2f-all.bin: vh2f.s of (2j + 1) << 16 | 2j for j < 32768: per j, lanes 0 and 1\n"
      "vf2h-spread.bin: vf2h.q of spread-out quads, seed 22: per quad, result lanes 0 and 1\n"
      "ipu-divide.bin: per pair (32 chosen numbers each with each, then the generator from 21): a, b, div lo, div hi, "
      "divu lo, divu hi\n"
      "fpu-convert.bin: per input (32 specials, then mixed() from 23): x, then for FCSR rounding modes 0-3: cvt.w.s, "
      "round.w.s, trunc.w.s, ceil.w.s, floor.w.s\n"
      "fpu-arith.bin: per pair (mixed() from 24): a, b, then for rounding modes 0-3: add.s, sub.s, mul.s, div.s, "
      "sqrt.s of a\n"
      "ops.bin: the instruction recorder (main.c, measureOps; ops.txt lists its entries)\n"
      "<name>.stopped: a test that didn't finish twice (the PSP stopped during it), given up on: what it wrote\n");
  }
  sceIoClose(file);
}

int main(int argc, char** argv) {
  pspDebugScreenInit();
  print("psp-vfpu-measure: what this PSP's VFPU computes\n\n");
  print("X: round 2 (the second measurements, about 230 MB)\n");
  print("O: round 1 (the first measurements again, about 450 MB)\n\n");
  SceCtrlData pad;
  int round = Shrink ? 2 : 0;
  while(!round) {
    sceCtrlReadBufferPositive(&pad, 1);
    if(pad.Buttons & PSP_CTRL_CROSS) round = 2;
    if(pad.Buttons & PSP_CTRL_CIRCLE) round = 1;
  }

  //results/ beside EBOOT.PBP
  strncpy(folder, argc > 0 ? argv[0] : "ms0:/PSP/GAME/VFPUMEASURE/EBOOT.PBP", sizeof(folder) - 16);
  char* slash = strrchr(folder, '/');
  if(slash) *slash = 0;
  strcat(folder, "/results");
  sceIoMkdir(folder, 0777);
  print("round %d, writing to %s\n\n", round, folder);

  int ok = 1;
  if(round == 1) {
    writeManifest(1, tests, sizeof(tests) / sizeof(tests[0]));
    for(unsigned int i = 0; ok && i < sizeof(tests) / sizeof(tests[0]); i++) ok = measure(&tests[i]);
    if(ok) ok = measureRandom();
  } else {
    writeManifest(2, tests2, sizeof(tests2) / sizeof(tests2[0]));
    writeOpsList();
    for(unsigned int i = 0; ok && i < sizeof(tests2) / sizeof(tests2[0]); i++) ok = measure(&tests2[i]);
    if(ok) ok = measureHalves();
    if(ok) ok = measureFloatsToHalves();
    if(ok) ok = measureDivide();
    if(ok) ok = measureOps();  //late: if an instruction stops the program, the rest is already saved
    if(ok) ok = measureConvert();     //last: the FPU on not-a-numbers, infinities and the smallest numbers,
    if(ok) ok = measureArithmetic();  //never run on a PSP before (a run once stopped about here)
  }

  print(ok ? "\nAll done. Press X to leave.\n" : "\nStopped. Press X to leave.\n");
  if(Shrink) {
    sceKernelExitGame();
    return 0;
  }
  do {
    sceCtrlReadBufferPositive(&pad, 1);
  } while(pad.Buttons & PSP_CTRL_CROSS);  //let go of the X that started round 2 first
  do {
    sceCtrlReadBufferPositive(&pad, 1);
  } while(!(pad.Buttons & PSP_CTRL_CROSS));
  sceKernelExitGame();
  return 0;
}
