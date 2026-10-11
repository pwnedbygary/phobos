//psp-measure's VFPU and FPU tests: what a real PSP's VFPU computes (and its FPU, where round 2 found questions), so
//Phobos's PSP core can be made to match it exactly (see docs/psp-core.md, "Part 3: the VFPU", and
//docs/psp-vfpu-measurements.md).
//
//They come in rounds, each picked from the menu (main.c) and writing its own files into results/vfpu:
//- Round 1: the VFPU's math functions over every input of the range each one reduces its argument to, plus a
//  million inputs spread over all of them, the random number generator from a range of seeds, and some arithmetic
//  on spread-out inputs. About 450 MB.
//- Round 2: what round 1 couldn't settle. vlog2 over whole binades above 4; dot products, sums and averages built
//  to show how the VFPU adds several numbers; every half float through vh2f, and a million floats through vf2h;
//  the integer divide, and the FPU's conversions and rounding modes; and the instruction recorder, which runs
//  every VFPU instruction pspdev's assembler knows (ops.h) on random register states. About 230 MB.
//- Round 3: what round 2 left open. The FPU's conversions and arithmetic on inputs it's safe with (round 2's FPU
//  tests stopped the PSP), FCSR as a program finds it, products just below the smallest normal number, and a
//  second recorder list (ops3.h): the math functions with prefixes, swizzles past an operand's size in the
//  instructions that don't work lane by lane, vavg and vfad with t prefixes. About 6 MB.
//- The FPU probes: the FPU on one value at a time of the kinds that may stop the PSP. Each probe that does is given
//  up on at the next start, which goes on with the next probe.
//
//Next VFPU probes (not wired to the menu yet; for a round that settles ops3.bin's 24 open entries — swizzles past
//an operand's size in instructions that combine lanes, which compilers don't produce). Each should write a small
//.bin of (prefixes, rs, rt, result / condition codes) for hand cases, so the host can see the rule without another
//full recorder pass:
//- vcrs / vcrsp / vdet: .t/.p with s or t swizzled past the size (one lane past, both past, constant in the past
//  lane, negate on the past lane). Does the past lane read as 0, drop out of the sum, or take another lane's value?
//- vsocp: .s/.p with a swizzle past the size and with negate; does the pair of outputs stay 0 / -0 as vh2f's split
//  does, or does only one of the two lanes move?
//- vcmp: each condition with a destination prefix that masks some lanes — which condition-code bits change for the
//  masked lanes, and does "any"/"every" count them? Also vcmp with a swizzle past the size on s or t.
//They compute nothing themselves: the files only hold what the hardware gave (and for the smaller tests, the
//inputs), and the host works out the rest. Each file is raw little-endian 32-bit words in the order its test
//describes below, and manifest.txt (round 1), manifest2.txt (round 2) or manifest3.txt (round 3 and the probes)
//says how each file's inputs are made. How a round can be stopped and started again, and gives up on a test that
//stops the PSP, is results.c's.

#include "measure.h"
#include <psputils.h>
#include <string.h>
#include <stdio.h>

//Four 32-bit words, aligned for the VFPU's quad loads and stores (lv.q, sv.q).
typedef struct { unsigned int lane[4]; } __attribute__((aligned(16))) Quad;

enum { ChunkQuads = 16384 };  //how many quads are worked on and written at a time (256 KiB)
static Quad inputS[ChunkQuads], inputT[ChunkQuads], output[ChunkQuads];

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
  const char* name;    //results/vfpu/<name>.bin
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

//Round 3: products just below the smallest normal number, 2^-126, by the sliver that decides how the VFPU treats a
//result too small to be normal (round 2's recorder had a product that rounds up to exactly 2^-126 come out as 0).
//t is 2^-126 (1 + j 2^-23) and s is 1 - j 2^-23 (as bits 0x00800000 + j and 0x3f800000 - 2j), so the product is
//2^-126 (1 - j^2 2^-46). Three answers can be told apart: seen to be too small before it's rounded, it's 0 for every
//j; rounded to 24 bits first and then flushed, it's exactly 2^-126 for j up to 1448; rounded as IEEE's denormals
//would be and then flushed (what the core does now), exactly 2^-126 for j up to 2048. One draw per lane (lane 0
//first): j from 1 to 4096 in its low 12 bits, s's sign from bit 31 and t's from bit 30.
static void tinyProducts(Quad* s, Quad* t, unsigned int* state) {
  for(int i = 0; i < 4; i++) {
    unsigned int w = next(state), j = 1 + (w & 4095);
    s->lane[i] = (w & 0x80000000u) | (0x3f800000u - 2 * j);
    t->lane[i] = (w << 1 & 0x80000000u) | (0x00800000u + j);
  }
}

//Round 3's tests that run through measure().
static const Test tests3[] = {
  {"vmul-tiny", vmul, 0, (1u << 18) >> Shrink, 41, 1,
   "vmul.q, seed 41: products a sliver below the smallest normal number (tinyProducts)", 0, tinyProducts, 0},
};

//Runs one test into its file (results.c: begin, writeOut, finish).
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

//The generator's state and FCSR as the program found them when it started (vfpuStart, before any round could
//change them): round 1 records the generator from where it starts, round 3 FCSR as the program found it.
static unsigned int startState[8], startFcsr;

//The random number generator: its state at start, and its numbers before any seed; then for 64 seeds, the
//state right after vrnds.s and 4096 draws with vrndi.s; then for one seed, 1024 draws with vrndi.q, whose lanes
//are filled in their own order. The first part needs the generator as the program started: once this test has
//drawn or seeded (round 1 already run since the program started), it waits for the next start.
static int measureRandom(void) {
  Output out;
  int started = begin(&out, "vrnd");
  if(started <= 0) return started == 0;
  unsigned int now[8];
  readState(now);
  if(memcmp(now, startState, sizeof(now))) {
    drop(&out);
    pspDebugScreenSetXY(0, out.line);
    print("%-26s skipped: start the program again first\n", "vrnd");
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

  if(!writeOut(&out, "vrnd", w, n * 4)) return 0;
  return finish(&out, "vrnd");
}

//Every half float through vh2f.s: input word j (j < 32768) holds halves 2j (low 16 bits) and 2j + 1 (high), so
//all 65536 halves appear once; per word, the two results (lanes 0 and 1).
static int measureHalves(void) {
  Output out;
  int started = begin(&out, "vh2f-all");
  if(started <= 0) return started == 0;
  static unsigned int halves[65536] __attribute__((aligned(16)));
  for(unsigned int j = 0; j < 32768; j++) {
    unsigned int word = (2 * j + 1) << 16 | 2 * j;
    __asm__ volatile("mtv %2, S000\n" "vh2f.s C010, S000\n" "sv.s S010, %0\n" "sv.s S011, %1\n"
                     : "=m"(halves[2 * j]), "=m"(halves[2 * j + 1]) : "r"(word) : "memory");
  }
  if(!writeOut(&out, "vh2f-all", halves, sizeof(halves))) return 0;
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

//Conversions to integers: per input (4096 of them), the input, then for each rounding mode: cvt.w.s (which follows
//the mode), round.w.s, trunc.w.s, ceil.w.s and floor.w.s.
typedef unsigned int (*ConvertInput)(int i, unsigned int* state);

static int measureConvertFrom(const char* name, unsigned int seed, ConvertInput input) {
  Output out;
  int started = begin(&out, name);
  if(started <= 0) return started == 0;
  static unsigned int w[4096 * 21];
  unsigned int state = seed;
  for(int i = 0; i < 4096; i++) {
    unsigned int x = input(i, &state);
    unsigned int* p = &w[i * 21];
    *p++ = x;
    for(unsigned int mode = 0; mode < 4; mode++) {
      *p++ = cvtws(mode, x); *p++ = roundws(mode, x); *p++ = truncws(mode, x);
      *p++ = ceilws(mode, x); *p++ = floorws(mode, x);
    }
  }
  if(!writeOut(&out, name, w, sizeof(w))) return 0;
  return finish(&out, name);
}

//Arithmetic in each rounding mode: per pair (4096 of them), a and b, then for each mode: add.s, sub.s, mul.s,
//div.s, and sqrt.s of a (or with absolute set, of a's size, its sign bit cleared).
typedef void (*ArithmeticInput)(int i, unsigned int* state, unsigned int* a, unsigned int* b);

static int measureArithmeticFrom(const char* name, unsigned int seed, ArithmeticInput input, int absolute) {
  Output out;
  int started = begin(&out, name);
  if(started <= 0) return started == 0;
  static unsigned int w[4096 * 22];
  unsigned int state = seed;
  for(int i = 0; i < 4096; i++) {
    unsigned int a, b;
    input(i, &state, &a, &b);
    unsigned int* p = &w[i * 22];
    *p++ = a; *p++ = b;
    for(unsigned int mode = 0; mode < 4; mode++) {
      *p++ = adds(mode, a, b); *p++ = subs(mode, a, b); *p++ = muls(mode, a, b);
      *p++ = divs(mode, a, b); *p++ = sqrts(mode, absolute ? a & 0x7fffffffu : a);
    }
  }
  if(!writeOut(&out, name, w, sizeof(w))) return 0;
  return finish(&out, name);
}

//Round 2's inputs: the 32 specials, then many kinds from seed 23; pairs of many kinds from seed 24.
static unsigned int anyConvertInput(int i, unsigned int* state) { return i < 32 ? specials[i] : mixed(state); }

static void anyArithmeticInput(int i, unsigned int* state, unsigned int* a, unsigned int* b) {
  (void)i;
  *a = mixed(state);
  *b = mixed(state);
}

static int measureConvert(void) { return measureConvertFrom("fpu-convert", 23, anyConvertInput); }
static int measureArithmetic(void) { return measureArithmeticFrom("fpu-arith", 24, anyArithmeticInput, 0); }

//Round 3: the FPU on inputs it's safe with. Round 2's tests stopped the PSP, most likely at the not-a-numbers,
//infinities, denormals and numbers too big for an integer they started with; these leave all of those out (the
//probes, below, try them one at a time). The conversions: zeros and normal numbers below 2^31 in size (not -2^31
//itself, which a probe tries: an FPU that judges the range by the exponent alone would refuse it), chosen ones
//first (halves and their neighbours, where the rounding modes part, and the edges of the integer range), then a
//mix from seed 43: any size below 2^31, whole numbers and a half, whole numbers, and small numbers, each sign.
static const unsigned int safeConvertChosen[32] = {
  0x00000000, 0x80000000, 0x3f000000, 0xbf000000, 0x3f800000, 0xbf800000, 0x3fc00000, 0xbfc00000,
  0x40200000, 0xc0200000, 0x3effffff, 0xbeffffff, 0x3f000001, 0xbf000001, 0x3f7fffff, 0xbf7fffff,
  0x4effffff, 0xceffffff, 0xcefffffe, 0x4b000000, 0xcb000000, 0x4b000001, 0x4affffff, 0xcaffffff,
  0x4b7fffff, 0x00800000, 0x80800000, 0x3e800000, 0xbe800000, 0x40400000, 0x40600000, 0xc0600000,
};

static unsigned int floatBits(float x) {
  unsigned int bits;
  memcpy(&bits, &x, sizeof(bits));
  return bits;
}

static unsigned int safeConvertInput(int i, unsigned int* state) {
  if(i < 32) return safeConvertChosen[i];
  unsigned int w = next(state), sign = w & 0x80000000u, r = next(state);
  switch(w >> 29 & 3) {
  case 0: return sign | (100 + w % 58) << 23 | (r & 0x7fffffu);              //2^-27 up to 2^31, any mantissa
  case 1: return sign | floatBits((float)(int)(r >> 10) + 0.5f);             //a whole number and a half
  case 2: return sign | floatBits((float)(int)(r >> 8));                     //a whole number below 2^24
  default: return sign | (118 + (w >> 23 & 15)) << 23 | (r & 0x7fffffu);    //2^-9 up to 2^7
  }
}

//The arithmetic: pairs of normal numbers within 40 binades of 1, so that no sum, difference, product, quotient or
//square root (of the first's size) comes out too big, too small or not a number. Chosen pairs first (ties, where
//rounding to the nearest goes to the even neighbour, and a cancellation, which gives +0 or -0 by the mode), then
//from seed 44: a quarter of them nearly cancelling (b about -a), the rest any two.
static const unsigned int safeArithmeticChosen[12][2] = {
  {0x3f800000, 0x3f800000}, {0x3f800000, 0xbf800000}, {0x3f800000, 0x33800000}, {0x3f800000, 0xb3800000},
  {0x3fc00000, 0x33800000}, {0x3f800001, 0x33800000}, {0x4b000000, 0x3f000000}, {0x4b000001, 0x3f000000},
  {0x3f800000, 0x40400000}, {0xbf800000, 0x40400000}, {0x40490fdb, 0x402df854}, {0x53800000, 0x2b800000},
};

static unsigned int within40(unsigned int* state) {  //a random sign and mantissa, within 40 binades of 1
  unsigned int w = next(state);
  return (w & 0x807fffffu) | (87 + next(state) % 81) << 23;
}

static void safeArithmeticInput(int i, unsigned int* state, unsigned int* a, unsigned int* b) {
  if(i < 12) {
    *a = safeArithmeticChosen[i][0];
    *b = safeArithmeticChosen[i][1];
    return;
  }
  *a = within40(state);
  unsigned int w = next(state);
  *b = w >> 30 ? within40(state) : (*a ^ 0x80000000u) ^ (w & 0xffu);
}

static int measureConvertSafe(void) { return measureConvertFrom("fpu-convert-safe", 43, safeConvertInput); }
static int measureArithmeticSafe(void) { return measureArithmeticFrom("fpu-arith-safe", 44, safeArithmeticInput, 1); }

//Round 3: FCSR (FPU control register 31) as the program found it when it started, and FIR (register 0, which says
//what FPU it is). FCSR's bit 24, flush to zero, decides what the FPU does with denormals; nothing in the program
//changes it, so it's also the bit the tests after this one run with.
static int measureFpuState(void) {
  Output out;
  int started = begin(&out, "fpu-state");
  if(started <= 0) return started == 0;
  unsigned int w[2] = {startFcsr, 0};
  __asm__ volatile("cfc1 %0, $0\n" : "=r"(w[1]));
  if(!writeOut(&out, "fpu-state", w, sizeof(w))) return 0;
  return finish(&out, "fpu-state");
}

//The FPU probes (a choice of their own on the menu): one value at a time of the kinds round 2's FPU tests started
//with, to find which stop the PSP. Each probe runs one instruction once, in rounding mode 0 with FCSR's flags,
//enables and causes cleared and flush to zero (bit 24) off, unless the probe turns it on (the -fs ones), and writes
//its inputs, its result, and FCSR after it (whose cause and flag bits say what happened). A probe that stops the PSP
//isn't tried again: the next start gives up on it (its .stopped file says which it was) and goes on with the next.
//They're in order from the least likely to stop the PSP to the most.
//NaNs are in MIPS's encoding, the other way round from most processors': a quiet NaN has its top fraction bit
//(22) clear (0x7fbfffff, or the VFPU's own 0x7f800001), a signaling one has it set (0x7fc00000).
#define FPU_PROBE(name, instruction) \
  static void name(unsigned int fcsr, unsigned int x, unsigned int y, unsigned int* result, unsigned int* after) { \
    unsigned int saved; \
    __asm__ volatile(".set push\n" ".set noreorder\n" \
                     "cfc1 %2, $31\n" "nop\n" "ctc1 %5, $31\n" "nop\n" "mtc1 %3, $f0\n" "mtc1 %4, $f1\n" "nop\n" \
                     instruction "\n" "nop\n" "mfc1 %0, $f2\n" "cfc1 %1, $31\n" "nop\n" "ctc1 %2, $31\n" "nop\n" \
                     ".set pop\n" \
                     : "=&r"(*result), "=&r"(*after), "=&r"(saved) : "r"(x), "r"(y), "r"(fcsr) \
                     : "$f0", "$f1", "$f2"); \
  }

FPU_PROBE(probeAdd, "add.s $f2, $f0, $f1")
FPU_PROBE(probeMul, "mul.s $f2, $f0, $f1")
FPU_PROBE(probeDiv, "div.s $f2, $f0, $f1")
FPU_PROBE(probeSqrt, "sqrt.s $f2, $f0")
FPU_PROBE(probeConvert, "cvt.w.s $f2, $f0")

enum { ProbeAdd, ProbeMul, ProbeDiv, ProbeSqrt, ProbeConvert };
typedef struct { const char* name; int op; unsigned int a, b, fcsrSet; const char* about; } Probe;
static const Probe probes[] = {
  {"probe-div-zero",        ProbeDiv,     0x3f800000, 0x00000000, 0, "div.s 1 / 0"},
  {"probe-mul-big",         ProbeMul,     0x7e800000, 0x7e800000, 0, "mul.s 2^126 * 2^126 (too big)"},
  {"probe-sqrt-negative",   ProbeSqrt,    0xbf800000, 0,          0, "sqrt.s -1"},
  {"probe-add-infinity",    ProbeAdd,     0x7f800000, 0x3f800000, 0, "add.s infinity + 1"},
  {"probe-add-qnan",        ProbeAdd,     0x7fbfffff, 0x3f800000, 0, "add.s a quiet NaN (MIPS's, 0x7fbfffff) + 1"},
  {"probe-add-snan",        ProbeAdd,     0x7fc00000, 0x3f800000, 0, "add.s a signaling NaN (MIPS's, 0x7fc00000) + 1"},
  {"probe-cvt-minus-2p31",  ProbeConvert, 0xcf000000, 0,          0, "cvt.w.s -2^31 (the smallest integer, exactly)"},
  {"probe-cvt-infinity",    ProbeConvert, 0x7f800000, 0,          0, "cvt.w.s infinity"},
  {"probe-cvt-qnan",        ProbeConvert, 0x7fbfffff, 0,          0, "cvt.w.s a quiet NaN (MIPS's, 0x7fbfffff)"},
  {"probe-cvt-snan",        ProbeConvert, 0x7fc00000, 0,          0, "cvt.w.s a signaling NaN (MIPS's, 0x7fc00000)"},
  {"probe-cvt-2p31",        ProbeConvert, 0x4f000000, 0,          0, "cvt.w.s 2^31 (one past the largest integer)"},
  {"probe-mul-tiny",        ProbeMul,     0x0d800000, 0x30800000, 0, "mul.s 2^-100 * 2^-30 (a denormal result)"},
  {"probe-add-denormal",    ProbeAdd,     0x00000001, 0x3f800000, 0, "add.s the smallest denormal + 1"},
  {"probe-cvt-denormal",    ProbeConvert, 0x00000001, 0,          0, "cvt.w.s the smallest denormal"},
  {"probe-add-denormal-fs", ProbeAdd,     0x00000001, 0x3f800000, 1u << 24,
   "add.s the smallest denormal + 1, with FCSR's flush-to-zero bit (24) set"},
  {"probe-mul-tiny-fs",     ProbeMul,     0x0d800000, 0x30800000, 1u << 24,
   "mul.s 2^-100 * 2^-30 (a denormal result), with FCSR's flush-to-zero bit (24) set"},
  {"probe-cvt-denormal-fs", ProbeConvert, 0x00000001, 0,          1u << 24,
   "cvt.w.s the smallest denormal, with FCSR's flush-to-zero bit (24) set"},
};

static int measureProbe(const Probe* probe) {
  Output out;
  int started = beginTrying(&out, probe->name, 0);
  if(started <= 0) return started == 0;
  unsigned int w[4] = {probe->a, probe->b, 0, 0}, fcsr = (fcsrFor(0) & ~(1u << 24)) | probe->fcsrSet;
  switch(probe->op) {
  case ProbeAdd: probeAdd(fcsr, probe->a, probe->b, &w[2], &w[3]); break;
  case ProbeMul: probeMul(fcsr, probe->a, probe->b, &w[2], &w[3]); break;
  case ProbeDiv: probeDiv(fcsr, probe->a, probe->b, &w[2], &w[3]); break;
  case ProbeSqrt: probeSqrt(fcsr, probe->a, probe->b, &w[2], &w[3]); break;
  default: probeConvert(fcsr, probe->a, probe->b, &w[2], &w[3]); break;
  }
  if(!writeOut(&out, probe->name, w, sizeof(w))) return 0;
  return finish(&out, probe->name);
}

//The instruction recorder. Each entry of ops.h is one VFPU instruction (after up to three prefix instructions)
//that reads matrix 0 (and matrix 1) and writes matrix 2. Its words are copied into stub, a tiny function that
//returns right after them, and run OpRuns times. Before each run, matrices 0 and 1 get inputs of many kinds,
//matrix 2 a marker in every lane (0xdead0000 + its place, so lanes an instruction leaves alone show), and the
//condition codes six random bits. After it, matrix 2 and the condition codes are kept.
typedef struct { unsigned char count; unsigned int words[4]; const char* text; } Op;
#include "ops.h"
#include "ops3.h"  //round 3's list

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
//then the condition codes as the top six bits of one more draw. Round 3's ops3.bin is the same for ops3.h's
//entries, from seed 0x30000 + e.
static int measureOpsFrom(const char* name, const Op* table, int count, unsigned int seeds) {
  Output out;
  int started = begin(&out, name);
  if(started <= 0) return started == 0;
  unsigned int header[4] = {0x53504f56, 1, count, OpRuns};
  if(!writeOut(&out, name, header, sizeof(header))) return 0;
  static unsigned int block[5 + OpRuns * RunWords];
  static Quad m0[4], m1[4], m2[4];
  for(int e = 0; e < count; e++) {
    const Op* op = &table[e];
    awake();
    pspDebugScreenSetXY(0, out.line);
    print("%s %4d/%d %-40.40s", name, e + 1, count, op->text);
    int n = 0;
    for(int i = 0; i < op->count; i++) stub[n++] = op->words[i];
    stub[n++] = 0x03e00008;  //jr ra
    stub[n++] = 0x00000000;  //nop, in its delay slot
    sceKernelDcacheWritebackInvalidateRange(stub, sizeof(stub));
    sceKernelIcacheInvalidateRange(stub, sizeof(stub));
    unsigned int* b = block;
    *b++ = op->count;
    for(int i = 0; i < 4; i++) *b++ = op->words[i];
    unsigned int state = seeds + e;
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
    if(!writeOut(&out, name, block, sizeof(block))) return 0;
  }
  return finish(&out, name);
}

static int measureOps(void) { return measureOpsFrom("ops", ops, OP_COUNT, 0x10000); }
static int measureOps3(void) { return measureOpsFrom("ops3", ops3, OP3_COUNT, 0x30000); }

//ops.txt (ops3.txt): what each entry of ops.bin (ops3.bin) is, for people reading the file.
static void writeOpsList(const char* name, const Op* table, int count) {
  char path[320], text[160];
  snprintf(path, sizeof(path), "%s/%s.txt", folder, name);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  for(int e = 0; e < count; e++) {
    int length = snprintf(text, sizeof(text), "%d: %08x %08x %08x %08x (%d): %s\n", e, table[e].words[0],
                          table[e].words[1], table[e].words[2], table[e].words[3], table[e].count, table[e].text);
    sceIoWrite(file, text, length);
  }
  sceIoClose(file);
}

static void writeLine(SceUID file, const char* text) {
  sceIoWrite(file, text, strlen(text));
}

static void writeManifest(int round, const Test* list, unsigned int count) {
  char path[320], text[512];
  const char* name = round == 1 ? "manifest.txt" : round == 2 ? "manifest2.txt" : "manifest3.txt";
  snprintf(path, sizeof(path), "%s/%s", folder, name);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  snprintf(text, sizeof(text),
    "psp-measure's VFPU and FPU tests (tools/psp-measure/vfpu.c in Phobos), round %d\n"
    "firmware devkit version %08x\n"
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
  } else if(round == 3) {
    writeLine(file,
      "vmul-tiny's inputs (vfpu.c: tinyProducts): one draw per lane, lane 0 first; j = 1 + its low 12 bits, s = "
      "0x3f800000 - 2j with the draw's bit 31 for its sign, t = 0x00800000 + j with bit 30 for its sign\n"
      "fpu-state.bin: FCSR (FPU control register 31) as the program found it when it started, then FIR (control "
      "register 0)\n"
      "fpu-convert-safe.bin: as fpu-convert.bin, for 4096 safe inputs (vfpu.c: safeConvertInput, seed 43): x, then "
      "for FCSR rounding modes 0-3: cvt.w.s, round.w.s, trunc.w.s, ceil.w.s, floor.w.s\n"
      "fpu-arith-safe.bin: as fpu-arith.bin, for 4096 safe pairs (vfpu.c: safeArithmeticInput, seed 44): a, b, then "
      "for rounding modes 0-3: add.s, sub.s, mul.s, div.s, and sqrt.s of a's size (its sign bit cleared)\n"
      "ops3.bin: the instruction recorder (as ops.bin) for ops3.h's entries, from seed 0x30000 + entry; ops3.txt "
      "lists them\n"
      "the FPU probes (a menu choice of their own), <name>.bin for each below: a, b, the result, and FCSR after "
      "the instruction; run "
      "once in rounding mode 0, FCSR's flags, enables and causes cleared, flush to zero (bit 24) off but where set:\n");
    for(unsigned int i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
      snprintf(text, sizeof(text), "  %s: %s\n", probes[i].name, probes[i].about);
      writeLine(file, text);
    }
    writeLine(file, "<name>.stopped: a test that stopped the PSP (twice, or once for a probe), given up on\n");
  } else {
    writeLine(file,
      "the adding tests draw each result's inputs lane 0 first, s then t (see vfpu.c: dotOne, dotTwo, dotClose, "
      "dotShort, sumClose, sumTwo); one result per quad or pair of quads\n"
      "vh2f-all.bin: vh2f.s of (2j + 1) << 16 | 2j for j < 32768: per j, lanes 0 and 1\n"
      "vf2h-spread.bin: vf2h.q of spread-out quads, seed 22: per quad, result lanes 0 and 1\n"
      "ipu-divide.bin: per pair (32 chosen numbers each with each, then the generator from 21): a, b, div lo, div hi, "
      "divu lo, divu hi\n"
      "fpu-convert.bin: per input (32 specials, then mixed() from 23): x, then for FCSR rounding modes 0-3: cvt.w.s, "
      "round.w.s, trunc.w.s, ceil.w.s, floor.w.s\n"
      "fpu-arith.bin: per pair (mixed() from 24): a, b, then for rounding modes 0-3: add.s, sub.s, mul.s, div.s, "
      "sqrt.s of a\n"
      "ops.bin: the instruction recorder (vfpu.c, measureOps; ops.txt lists its entries)\n"
      "<name>.stopped: a test that didn't finish twice (the PSP stopped during it), given up on: what it wrote\n");
  }
  sceIoClose(file);
}

//---- the rounds

//main.c calls this first, before any round could draw from the generator or change FCSR.
void vfpuStart(void) {
  readState(startState);
  __asm__ volatile("cfc1 %0, $31\n" : "=r"(startFcsr));
}

//Runs a round (4: the probes) into results/vfpu. Returns 0 if it had to stop (a file it couldn't write).
int vfpuRound(int round) {
  useFolder("vfpu");
  int ok = 1;
  if(round == 1) {
    writeManifest(1, tests, sizeof(tests) / sizeof(tests[0]));
    for(unsigned int i = 0; ok && i < sizeof(tests) / sizeof(tests[0]); i++) ok = measure(&tests[i]);
    if(ok) ok = measureRandom();
  } else if(round >= 3) {
    writeManifest(3, tests3, sizeof(tests3) / sizeof(tests3[0]));
    if(round == 3) {
      writeOpsList("ops3", ops3, OP3_COUNT);
      ok = measureFpuState();
      for(unsigned int i = 0; ok && i < sizeof(tests3) / sizeof(tests3[0]); i++) ok = measure(&tests3[i]);
      if(ok) ok = measureConvertSafe();
      if(ok) ok = measureArithmeticSafe();
      if(ok) ok = measureOps3();
    }
    if(round == 4) {
      for(unsigned int i = 0; ok && i < sizeof(probes) / sizeof(probes[0]); i++) ok = measureProbe(&probes[i]);
    }
  } else {
    writeManifest(2, tests2, sizeof(tests2) / sizeof(tests2[0]));
    writeOpsList("ops", ops, OP_COUNT);
    for(unsigned int i = 0; ok && i < sizeof(tests2) / sizeof(tests2[0]); i++) ok = measure(&tests2[i]);
    if(ok) ok = measureHalves();
    if(ok) ok = measureFloatsToHalves();
    if(ok) ok = measureDivide();
    if(ok) ok = measureOps();  //late: if an instruction stops the program, the rest is already saved
    if(ok) ok = measureConvert();     //last: the FPU on not-a-numbers, infinities and the smallest numbers,
    if(ok) ok = measureArithmetic();  //never run on a PSP before (a run once stopped about here)
  }
  return ok;
}
