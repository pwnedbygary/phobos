//compare: checks Phobos's VFPU against what a real PSP computed (the results psp-measure's VFPU and FPU tests write
//into results/vfpu; see vfpu.c for the files). Every input is made again exactly as the PSP program made it, run
//through the core's own instruction (the same instruction word the PSP ran), and the result compared bit for bit.
//usage: tools/psp-measure/compare.sh <results folder> (it passes the folder's vfpu/ when there is one)
//
//For each test it reports how many results match exactly, and for the others how far off they are in units in
//the last place (ulps: how many representable floats apart the two results are), plus a few examples. Tests whose
//files aren't in the folder are left out, so a folder from any round (or several) works.

#include "../../tests/allegrex/harness.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace allegrex_test {

static auto present(const std::string& path) -> bool {
  return bool(std::ifstream(path, std::ios::binary));
}

static auto load(const std::string& path) -> std::vector<uint32_t> {
  std::ifstream file(path, std::ios::binary);
  std::vector<uint32_t> words;
  if(!file) return words;
  file.seekg(0, std::ios::end);
  words.resize(size_t(file.tellg()) / 4);
  file.seekg(0);
  file.read((char*)words.data(), words.size() * 4);
  return words;
}

//the PSP program's generator for spread-out inputs
struct Generator {
  uint32_t state;
  auto next() -> uint32_t { return state = state * 1664525u + 1013904223u; }
};

enum class Inputs { FromOne, FromHalf, Fixed23, Spread, Sweep };
enum class Kind { Unary, Binary, Dot, Sum };  //Sum: one result from a quad (vfad, vavg)
enum class Fill { None, DotOne, DotTwo, DotClose, DotShort, SumClose, SumTwo, Tiny };

static auto input(Inputs inputs, uint32_t k, Generator& generator, uint32_t first) -> uint32_t {
  switch(inputs) {
  case Inputs::FromOne: return 0x3f800000u + k;
  case Inputs::FromHalf: return 0x3f000000u + k;
  case Inputs::Fixed23: return bits((float)k / 8388608.0f);
  case Inputs::Spread: return generator.next();
  case Inputs::Sweep: return first + k;
  }
  return 0;
}

//The PSP program's inputs for the tests built to show how several numbers are added (vfpu.c: dotOne and the
//rest), drawn in the same order.
static auto ranged(Generator& g, uint32_t low, uint32_t width) -> uint32_t {
  uint32_t w = g.next();
  return (w & 0x807fffffu) | (low + (w >> 23 & ((1u << width) - 1))) << 23;
}

static auto twoLanes(Generator& g, uint32_t& j, uint32_t& k) -> void {
  uint32_t a = g.next() >> 30;
  uint32_t b = (a + 1 + g.next() % 3) & 3;
  j = std::min(a, b);
  k = std::max(a, b);
}

static auto fill(Fill kind, uint32_t s[4], uint32_t t[4], Generator& g) -> void {
  uint32_t j = 0, k = 0;
  switch(kind) {
  case Fill::None: return;
  case Fill::DotOne:
    j = g.next() >> 30;
    for(uint32_t i = 0; i < 4; i++) s[i] = ranged(g, 112, 5);
    for(uint32_t i = 0; i < 4; i++) t[i] = i == j ? ranged(g, 112, 5) : 0;
    return;
  case Fill::DotTwo:
    twoLanes(g, j, k);
    for(uint32_t i = 0; i < 4; i++) s[i] = ranged(g, 112, 5);
    for(uint32_t i = 0; i < 4; i++) t[i] = i == j || i == k ? ranged(g, 112, 5) : 0;
    return;
  case Fill::DotClose:
    for(uint32_t i = 0; i < 4; i++) s[i] = ranged(g, 125, 2);
    for(uint32_t i = 0; i < 4; i++) t[i] = ranged(g, 125, 2);
    return;
  case Fill::DotShort:
    for(uint32_t i = 0; i < 4; i++) s[i] = ranged(g, 120, 4) & 0xffff0000u;
    for(uint32_t i = 0; i < 4; i++) t[i] = ranged(g, 120, 4) & 0xffff0000u;
    return;
  case Fill::SumClose:
    for(uint32_t i = 0; i < 4; i++) s[i] = ranged(g, 124, 3);
    return;
  case Fill::SumTwo:
    twoLanes(g, j, k);
    for(uint32_t i = 0; i < 4; i++) s[i] = i == j || i == k ? ranged(g, 112, 5) : 0;
    return;
  case Fill::Tiny:  //round 3: s = 1 - j 2^-23 and t = 2^-126 (1 + j 2^-23), j from 1 to 4096 and the signs per lane
    for(uint32_t i = 0; i < 4; i++) {
      uint32_t w = g.next(), n = 1 + (w & 4095);
      s[i] = (w & 0x80000000u) | (0x3f800000u - 2 * n);
      t[i] = (w << 1 & 0x80000000u) | (0x00800000u + n);
    }
    return;
  }
}

struct Test {
  const char* name;
  uint32_t instruction;  //as psp-objdump showed it in the PSP program
  Kind kind;
  Inputs inputs;
  uint32_t count;
  uint32_t seed;
  uint32_t first = 0;      //Inputs::Sweep's first input
  Fill fill = Fill::None;  //instead of inputs
};

static const Test tests[] = {
  {"vrcp-1-2",      0xd0108081, Kind::Unary, Inputs::FromOne,  1u << 23, 0},
  {"vrsq-1-4",      0xd0118081, Kind::Unary, Inputs::FromOne,  1u << 24, 0},
  {"vsqrt-1-4",     0xd0168081, Kind::Unary, Inputs::FromOne,  1u << 24, 0},
  {"vexp2-1-2",     0xd0148081, Kind::Unary, Inputs::FromOne,  1u << 23, 0},
  {"vrexp2-1-2",    0xd01c8081, Kind::Unary, Inputs::FromOne,  1u << 23, 0},
  {"vlog2-half-2",  0xd0158081, Kind::Unary, Inputs::FromHalf, 1u << 24, 0},
  {"vsin-fixed",    0xd0128081, Kind::Unary, Inputs::Fixed23,  1u << 23, 0},
  {"vcos-fixed",    0xd0138081, Kind::Unary, Inputs::Fixed23,  1u << 23, 0},
  {"vasin-fixed",   0xd0178081, Kind::Unary, Inputs::Fixed23,  (1u << 23) + 4, 0},
  {"vrcp-spread",   0xd0108081, Kind::Unary, Inputs::Spread, 1u << 20, 1},
  {"vnrcp-spread",  0xd0188081, Kind::Unary, Inputs::Spread, 1u << 20, 2},
  {"vrsq-spread",   0xd0118081, Kind::Unary, Inputs::Spread, 1u << 20, 3},
  {"vsqrt-spread",  0xd0168081, Kind::Unary, Inputs::Spread, 1u << 20, 4},
  {"vexp2-spread",  0xd0148081, Kind::Unary, Inputs::Spread, 1u << 20, 5},
  {"vrexp2-spread", 0xd01c8081, Kind::Unary, Inputs::Spread, 1u << 20, 6},
  {"vlog2-spread",  0xd0158081, Kind::Unary, Inputs::Spread, 1u << 20, 7},
  {"vsin-spread",   0xd0128081, Kind::Unary, Inputs::Spread, 1u << 20, 8},
  {"vnsin-spread",  0xd01a8081, Kind::Unary, Inputs::Spread, 1u << 20, 9},
  {"vcos-spread",   0xd0138081, Kind::Unary, Inputs::Spread, 1u << 20, 10},
  {"vasin-spread",  0xd0178081, Kind::Unary, Inputs::Spread, 1u << 20, 11},
  {"vadd-spread",   0x60018082, Kind::Binary, Inputs::Spread, 1u << 18, 12},
  {"vsub-spread",   0x60818082, Kind::Binary, Inputs::Spread, 1u << 18, 13},
  {"vmul-spread",   0x64018082, Kind::Binary, Inputs::Spread, 1u << 18, 14},
  {"vdiv-spread",   0x63818082, Kind::Binary, Inputs::Spread, 1u << 18, 15},
  {"vdot-spread",   0x64818082, Kind::Dot,    Inputs::Spread, 1u << 18, 16},
  //round 2
  {"vlog2-4-8",     0xd0158081, Kind::Unary, Inputs::Sweep, 1u << 23, 0, 0x40800000},
  {"vlog2-16-32",   0xd0158081, Kind::Unary, Inputs::Sweep, 1u << 23, 0, 0x41800000},
  {"vlog2-256-512", 0xd0158081, Kind::Unary, Inputs::Sweep, 1u << 23, 0, 0x43800000},
  {"vlog2-2p16",    0xd0158081, Kind::Unary, Inputs::Sweep, 1u << 23, 0, 0x47800000},
  {"vlog2-2p32",    0xd0158081, Kind::Unary, Inputs::Sweep, 1u << 23, 0, 0x4f800000},
  {"vlog2-2p64",    0xd0158081, Kind::Unary, Inputs::Sweep, 1u << 23, 0, 0x5f800000},
  {"vdot-one",      0x64818082, Kind::Dot, Inputs::Spread, 1u << 18, 31, 0, Fill::DotOne},
  {"vdot-two",      0x64818082, Kind::Dot, Inputs::Spread, 1u << 20, 32, 0, Fill::DotTwo},
  {"vdot-close",    0x64818082, Kind::Dot, Inputs::Spread, 1u << 20, 33, 0, Fill::DotClose},
  {"vdot-short",    0x64818082, Kind::Dot, Inputs::Spread, 1u << 18, 34, 0, Fill::DotShort},
  {"vhdp-close",    0x66018082, Kind::Dot, Inputs::Spread, 1u << 18, 35, 0, Fill::DotClose},
  {"vfad-close",    0xd0468082, Kind::Sum, Inputs::Spread, 1u << 20, 36, 0, Fill::SumClose},
  {"vfad-two",      0xd0468082, Kind::Sum, Inputs::Spread, 1u << 18, 37, 0, Fill::SumTwo},
  {"vavg-close",    0xd0478082, Kind::Sum, Inputs::Spread, 1u << 18, 38, 0, Fill::SumClose},
  //round 3
  {"vmul-tiny",     0x64018082, Kind::Binary, Inputs::Spread, 1u << 18, 41, 0, Fill::Tiny},
};

//Where the PSP program kept its operands: quad columns of matrix 0 (C000, C010, C020), and S020 for vdot.
static constexpr uint32_t ColumnS[4] = {0, 32, 64, 96};
static constexpr uint32_t ColumnT[4] = {1, 33, 65, 97};
static constexpr uint32_t ColumnD[4] = {2, 34, 66, 98};

static auto isNaN(uint32_t x) -> bool { return (x & 0x7f800000) == 0x7f800000 && (x & 0x7fffff); }

//How many floats apart two results are: 0 for the same value (including +0 against -0).
static auto ulps(uint32_t a, uint32_t b) -> uint64_t {
  auto key = [](uint32_t x) -> int64_t { return x & 0x80000000 ? -int64_t(x & 0x7fffffff) : int64_t(x); };
  int64_t d = key(a) - key(b);
  return d < 0 ? -d : d;
}

struct Stats {
  uint64_t total = 0, exact = 0, nans = 0, maxUlps = 0, within1 = 0;
  std::vector<std::string> examples;

  auto add(uint32_t input, uint32_t hardware, uint32_t ours) -> void {
    total++;
    if(hardware == ours) { exact++; within1++; return; }
    if(isNaN(hardware) && isNaN(ours)) { nans++; return; }
    uint64_t distance = isNaN(hardware) || isNaN(ours) ? ~0ull : ulps(hardware, ours);
    if(distance <= 1) within1++;
    if(distance != ~0ull && distance > maxUlps) maxUlps = distance;
    if(examples.size() < 4) {
      char text[96];
      std::snprintf(text, sizeof(text), "%08x -> PSP %08x, Phobos %08x", input, hardware, ours);
      examples.push_back(text);
    }
  }
};

static auto report(const char* name, const Stats& stats) -> void {
  std::printf("| %s | %llu | %llu (%.2f%%) | %llu | %s |\n", name, (unsigned long long)stats.total,
              (unsigned long long)stats.exact, stats.total ? 100.0 * stats.exact / stats.total : 0.0,
              (unsigned long long)stats.maxUlps, stats.examples.empty() ? "" : stats.examples[0].c_str());
  for(size_t i = 1; i < stats.examples.size(); i++) std::printf("|  |  |  |  | %s |\n", stats.examples[i].c_str());
  if(stats.nans) std::printf("|  |  | (%llu more where both are NaN, with different bits) |  |  |\n", (unsigned long long)stats.nans);
}

static auto compare(const std::string& folder, const Test& test) -> void {
  if(!present(folder + "/" + test.name + ".bin")) return;
  auto hardware = load(folder + "/" + test.name + ".bin");
  //a shorter file (the PSP program's quick version for emulators, make SMOKE=1) is checked as far as it goes
  uint32_t results = std::min<uint32_t>(test.count, hardware.size());
  if(test.kind == Kind::Unary || test.kind == Kind::Binary) results &= ~3u;
  if(results < test.count) std::printf("| %s | (partial: %u of %u results) | | | |\n", test.name, results, test.count);
  Machine m;
  m.cpu.power(Base);
  auto& v = m.cpu.vfpu.r;
  Generator generator{test.seed};
  Stats stats;
  uint32_t k = 0;
  if(test.kind == Kind::Dot || test.kind == Kind::Sum) {
    for(uint32_t i = 0; i < results; i++) {
      uint32_t s[4] = {}, t[4] = {};
      if(test.fill != Fill::None) {
        fill(test.fill, s, t, generator);
      } else {
        for(uint32_t lane = 0; lane < 4; lane++) s[lane] = input(test.inputs, k++, generator, test.first);
        for(uint32_t lane = 0; lane < 4; lane++) t[lane] = input(test.inputs, k++, generator, test.first);
      }
      for(uint32_t lane = 0; lane < 4; lane++) v[ColumnS[lane]] = s[lane], v[ColumnT[lane]] = t[lane];
      m.cpu.execute(Base, test.instruction);
      stats.add(s[0], hardware[i], v[ColumnD[0]]);
    }
  } else {
    for(uint32_t quad = 0; quad < results / 4; quad++) {
      uint32_t s[4], t[4] = {};
      if(test.fill != Fill::None) {
        fill(test.fill, s, t, generator);
      } else {
        for(uint32_t lane = 0; lane < 4; lane++) s[lane] = input(test.inputs, k++, generator, test.first);
        if(test.kind == Kind::Binary) {
          for(uint32_t lane = 0; lane < 4; lane++) t[lane] = input(test.inputs, k++, generator, test.first);
        }
      }
      for(uint32_t lane = 0; lane < 4; lane++) v[ColumnS[lane]] = s[lane], v[ColumnT[lane]] = t[lane];
      m.cpu.execute(Base, test.instruction);
      const uint32_t* out = test.kind == Kind::Binary ? ColumnD : ColumnT;
      for(uint32_t lane = 0; lane < 4; lane++) stats.add(s[lane], hardware[quad * 4 + lane], v[out[lane]]);
    }
  }
  report(test.name, stats);
}

//vh2f-all.bin: vh2f.s of (2j + 1) << 16 | 2j for every j below 32768, lanes 0 and 1 (S010 and S011).
static auto compareHalves(const std::string& folder) -> void {
  if(!present(folder + "/vh2f-all.bin")) return;
  auto hardware = load(folder + "/vh2f-all.bin");
  if(hardware.size() != 65536) return (void)std::printf("| vh2f-all | short | | | |\n");
  Machine m;
  m.cpu.power(Base);
  auto& v = m.cpu.vfpu.r;
  Stats stats;
  for(uint32_t j = 0; j < 32768; j++) {
    v[0] = (2 * j + 1) << 16 | 2 * j;
    m.cpu.execute(Base, 0xd0330001);  //vh2f.s C010, S000
    stats.add(2 * j, hardware[2 * j], v[1]);
    stats.add(2 * j + 1, hardware[2 * j + 1], v[33]);
  }
  report("vh2f-all", stats);
}

//vf2h-spread.bin: vf2h.q of spread-out quads (seed 22) in C000, result lanes 0 and 1 (S010 and S011).
static auto compareFloatsToHalves(const std::string& folder) -> void {
  if(!present(folder + "/vf2h-spread.bin")) return;
  auto hardware = load(folder + "/vf2h-spread.bin");
  uint32_t quads = std::min<uint32_t>(1u << 18, hardware.size() / 2);
  Machine m;
  m.cpu.power(Base);
  auto& v = m.cpu.vfpu.r;
  Generator generator{22};
  Stats stats;
  for(uint32_t quad = 0; quad < quads; quad++) {
    for(uint32_t lane = 0; lane < 4; lane++) v[ColumnS[lane]] = generator.next();
    m.cpu.execute(Base, 0xd0328081);  //vf2h.q C010, C000
    stats.add(v[0], hardware[2 * quad], v[1]);
    stats.add(v[32], hardware[2 * quad + 1], v[33]);
  }
  report("vf2h-spread", stats);
}

//Records that carry their own inputs: integer division and the FPU in each rounding mode. Each instruction (and
//mode) gets its own row.
struct Records {
  std::map<std::string, Stats> rows;
  std::vector<std::string> order;
  auto add(const std::string& row, uint32_t input, uint32_t hardware, uint32_t ours) -> void {
    if(!rows.count(row)) order.push_back(row);
    rows[row].add(input, hardware, ours);
  }
  auto print() -> void {
    for(auto& row : order) report(row.c_str(), rows[row]);
  }
};

static auto compareDivide(const std::string& folder, Records& records) -> void {
  if(!present(folder + "/ipu-divide.bin")) return;
  auto w = load(folder + "/ipu-divide.bin");
  Machine m;
  m.cpu.power(Base);
  auto& cpu = m.cpu;
  for(size_t n = 0; n + 6 <= w.size(); n += 6) {
    cpu.ipu.r[4] = w[n];
    cpu.ipu.r[5] = w[n + 1];
    cpu.execute(Base, 0x0085001a);  //div a0, a1
    records.add("div lo", w[n], w[n + 2], cpu.ipu.lo);
    records.add("div hi", w[n], w[n + 3], cpu.ipu.hi);
    cpu.execute(Base, 0x0085001b);  //divu a0, a1
    records.add("divu lo", w[n], w[n + 4], cpu.ipu.lo);
    records.add("divu hi", w[n], w[n + 5], cpu.ipu.hi);
  }
}

static const char* const ModeNames[4] = {"nearest", "toward zero", "up", "down"};

//fpu-convert.bin (round 2) or fpu-convert-safe.bin (round 3): per input, x, then for each rounding mode the five
//conversions. upper is FCSR's bits above the causes as the PSP program found them (fpu-state.bin; flush to zero
//among them), which it kept for every instruction. label starts each row's name (round 3's say "safe").
static auto compareConvert(const std::string& folder, const std::string& name, uint32_t upper,
                           const std::string& label, Records& records) -> void {
  if(!present(folder + "/" + name + ".bin")) return;
  auto w = load(folder + "/" + name + ".bin");
  static const uint32_t instructions[5] = {0x460000a4, 0x4600008c, 0x4600008d, 0x4600008e, 0x4600008f};
  static const char* const names[5] = {"cvt.w.s", "round.w.s", "trunc.w.s", "ceil.w.s", "floor.w.s"};
  Machine m;
  m.cpu.power(Base);
  auto& cpu = m.cpu;
  for(size_t n = 0; n + 21 <= w.size(); n += 21) {
    for(uint32_t mode = 0; mode < 4; mode++) {
      for(uint32_t i = 0; i < 5; i++) {
        cpu.fpu.csr = upper | mode;
        cpu.fpu.r[0] = w[n];
        cpu.execute(Base, instructions[i]);  //$f2 from $f0
        records.add(label + names[i] + " (" + ModeNames[mode] + ")", w[n], w[n + 1 + mode * 5 + i], cpu.fpu.r[2]);
      }
    }
  }
}

//fpu-arith.bin (round 2) or fpu-arith-safe.bin (round 3, whose square roots are of a's size: absolute): per pair,
//a and b, then for each rounding mode the five operations. upper and label as for compareConvert.
static auto compareFpuArithmetic(const std::string& folder, const std::string& name, bool absolute, uint32_t upper,
                                 const std::string& label, Records& records) -> void {
  if(!present(folder + "/" + name + ".bin")) return;
  auto w = load(folder + "/" + name + ".bin");
  static const uint32_t instructions[5] = {0x46010080, 0x46010081, 0x46010082, 0x46010083, 0x46000084};
  static const char* const names[5] = {"add.s", "sub.s", "mul.s", "div.s", "sqrt.s"};
  Machine m;
  m.cpu.power(Base);
  auto& cpu = m.cpu;
  for(size_t n = 0; n + 22 <= w.size(); n += 22) {
    for(uint32_t mode = 0; mode < 4; mode++) {
      for(uint32_t i = 0; i < 5; i++) {
        cpu.fpu.csr = upper | mode;
        cpu.fpu.r[0] = i == 4 && absolute ? w[n] & 0x7fffffff : w[n];
        cpu.fpu.r[1] = w[n + 1];
        cpu.execute(Base, instructions[i]);  //$f2 from $f0 (and $f1)
        records.add(label + names[i] + " (" + ModeNames[mode] + ")", w[n], w[n + 2 + mode * 5 + i], cpu.fpu.r[2]);
      }
    }
  }
}

//Round 3: FCSR and FIR as the PSP program found them, and each FPU probe (probe-*.bin: a, b, the result, FCSR after
//it; a .stopped file instead when the probe stopped the PSP), its result beside the core's.
static auto reportFpu(const std::string& folder) -> void {
  if(present(folder + "/fpu-state.bin")) {
    auto w = load(folder + "/fpu-state.bin");
    if(w.size() >= 2) std::printf("\nFCSR as the program found it: %08x (flush to zero %s); FIR %08x\n", w[0],
                                  w[0] >> 24 & 1 ? "on" : "off", w[1]);
  }
  struct Probe { const char* name; uint32_t instruction; };
  static const Probe probes[] = {  //as vfpu.c's probes, in its order: add.s, mul.s, div.s, sqrt.s, cvt.w.s
    {"probe-div-zero", 0x46010083}, {"probe-mul-big", 0x46010082}, {"probe-sqrt-negative", 0x46000084},
    {"probe-add-infinity", 0x46010080}, {"probe-add-qnan", 0x46010080}, {"probe-add-snan", 0x46010080},
    {"probe-cvt-minus-2p31", 0x460000a4}, {"probe-cvt-infinity", 0x460000a4}, {"probe-cvt-qnan", 0x460000a4},
    {"probe-cvt-snan", 0x460000a4}, {"probe-cvt-2p31", 0x460000a4}, {"probe-mul-tiny", 0x46010082},
    {"probe-add-denormal", 0x46010080}, {"probe-cvt-denormal", 0x460000a4}, {"probe-add-denormal-fs", 0x46010080},
    {"probe-mul-tiny-fs", 0x46010082}, {"probe-cvt-denormal-fs", 0x460000a4},
  };
  bool header = false;
  for(auto& probe : probes) {
    std::string path = folder + "/" + probe.name;
    //a probe that stopped the PSP on the last start still has its .part: the next start renames it .stopped
    bool stopped = present(path + ".stopped") || present(path + ".part"), done = present(path + ".bin");
    if(!stopped && !done) continue;
    if(!header) std::printf("\n| probe | a, b | PSP result, FCSR after | Phobos result |\n| --- | --- | --- | --- |\n");
    header = true;
    if(!done) {
      std::printf("| %s | | stopped the PSP | |\n", probe.name);
      continue;
    }
    auto w = load(path + ".bin");
    if(w.size() < 4) continue;
    Machine m;
    m.cpu.power(Base);
    m.cpu.fpu.csr = w[3] & ~0x3ffffu;  //FCSR as the probe ran: the PSP's own bits above the causes (and the probe's), mode 0
    m.cpu.fpu.r[0] = w[0];
    m.cpu.fpu.r[1] = w[1];
    m.cpu.execute(Base, probe.instruction);  //$f2 from $f0 (and $f1)
    std::printf("| %s | %08x, %08x | %08x, %08x | %08x |\n", probe.name, w[0], w[1], w[2], w[3], m.cpu.fpu.r[2]);
  }
}

//What kind of difference one result word is, for the recorder's summary: a lane the core didn't write (it still
//holds its marker), a NaN on either side, a denormal on either side, rounding (two numbers of the same sign at most
//4 ulps apart), or anything else.
enum Difference : uint32_t { Unwritten, NaNs, Denormals, Rounding, Other, Kinds };
static const char* KindNames[Kinds] = {"unwritten", "NaN", "denormal", "rounding", "other"};
static auto kindOf(uint32_t theirs, uint32_t ours) -> Difference {
  auto nan = [](uint32_t x) { return (x & 0x7f80'0000) == 0x7f80'0000 && (x & 0x007f'ffff) != 0; };
  auto denormal = [](uint32_t x) { return (x & 0x7f80'0000) == 0 && (x & 0x007f'ffff) != 0; };
  auto finite = [](uint32_t x) { return (x & 0x7f80'0000) != 0x7f80'0000; };
  if((ours & 0xffff'0000) == 0xdead'0000 && (theirs & 0xffff'0000) != 0xdead'0000) return Unwritten;
  if(nan(theirs) || nan(ours)) return NaNs;
  if(denormal(theirs) || denormal(ours)) return Denormals;
  if((theirs ^ ours) >> 31 == 0 && finite(theirs) && finite(ours)) {
    if((theirs > ours ? theirs - ours : ours - theirs) <= 4) return Rounding;
  }
  return Other;
}

//ops.bin or ops3.bin, the instruction recorder (vfpu.c, measureOpsFrom): per entry, its words, then per run
//matrices 0 and 1, the condition codes as set, as read back, and after, and matrix 2 after. Each run is replayed
//from the condition codes as the PSP read them back, matrix 2 holding the same markers, and the prefixes as after
//any instruction that used them up; an entry matches when matrix 2 and the condition codes do, in every run. For
//the others, the differing words are counted by kind (condition codes that differ count as other).
static auto compareOps(const std::string& folder, const std::string& name) -> void {
  if(!present(folder + "/" + name + ".bin")) return;
  auto w = load(folder + "/" + name + ".bin");
  if(w.size() < 4 || w[0] != 0x53504f56) return (void)std::printf("%s.bin: not a recorder file\n", name.c_str());
  uint32_t entries = w[2], runs = w[3];
  std::map<uint32_t, std::string> names;
  std::ifstream list(folder + "/" + name + ".txt");
  for(std::string line; std::getline(list, line);) {
    auto colon = line.find(": ", line.find(')'));
    if(colon != std::string::npos) names[std::stoul(line)] = line.substr(colon + 2);
  }
  Machine m;
  m.cpu.power(Base);
  auto& vfpu = m.cpu.vfpu;
  size_t n = 4;
  uint32_t exactEntries = 0, roundingEntries = 0, recorded = 0;
  std::printf("\n| entry | instruction | runs that differ | differing words by kind | first difference (PSP, Phobos) |\n"
              "| --- | --- | --- | --- | --- |\n");
  for(uint32_t e = 0; e < entries && n + 5 + runs * 51 <= w.size(); e++, recorded++) {
    uint32_t count = w[n], words[4] = {w[n + 1], w[n + 2], w[n + 3], w[n + 4]};
    n += 5;
    uint32_t differ = 0, kinds[Kinds] = {};
    std::string first;
    for(uint32_t run = 0; run < runs; run++, n += 51) {
      const uint32_t* m0 = &w[n];
      const uint32_t* m1 = &w[n + 16];
      uint32_t before = w[n + 33], after = w[n + 34];
      const uint32_t* m2 = &w[n + 35];
      for(uint32_t c = 0; c < 4; c++) {
        for(uint32_t r = 0; r < 4; r++) {
          vfpu.r[r * 32 + c] = m0[c * 4 + r];
          vfpu.r[r * 32 + 4 + c] = m1[c * 4 + r];
          vfpu.r[r * 32 + 8 + c] = 0xdead0000 | c << 4 | r;
        }
      }
      vfpu.cc = before;
      vfpu.pfxs = vfpu.pfxt = 0xe4;  //the identity prefixes (PrefixIdentity), as after any instruction that used them
      vfpu.pfxd = 0;
      for(uint32_t i = 0; i < count; i++) m.cpu.execute(Base + 4 * i, words[i]);
      std::string difference;
      for(uint32_t c = 0; c < 4; c++) {
        for(uint32_t r = 0; r < 4; r++) {
          uint32_t ours = vfpu.r[r * 32 + 8 + c], theirs = m2[c * 4 + r];
          if(ours == theirs) continue;
          kinds[kindOf(theirs, ours)]++;
          if(!difference.empty()) continue;
          char text[96];
          std::snprintf(text, sizeof(text), "run %u, row %u column %u: %08x, %08x", run, r, c, theirs, ours);
          difference = text;
        }
      }
      if((vfpu.cc & 0x3f) != (after & 0x3f)) {
        kinds[Other]++;
        if(difference.empty()) {
          char text[96];
          std::snprintf(text, sizeof(text), "run %u, condition codes: %02x, %02x", run, after & 0x3f, vfpu.cc & 0x3f);
          difference = text;
        }
      }
      if(!difference.empty()) {
        differ++;
        if(first.empty()) first = difference;
      }
    }
    if(!differ) { exactEntries++; continue; }
    std::string byKind;
    for(uint32_t k = 0; k < Kinds; k++) {
      if(!kinds[k]) continue;
      if(!byKind.empty()) byKind += ", ";
      byKind += std::string(KindNames[k]) + " " + std::to_string(kinds[k]);
    }
    if(kinds[Rounding] && kinds[Rounding] == kinds[Unwritten] + kinds[NaNs] + kinds[Denormals] + kinds[Rounding] +
                                                kinds[Other]) roundingEntries++;
    std::printf("| %u | %s | %u of %u | %s | %s |\n", e, names.count(e) ? names[e].c_str() : "?", differ, runs,
                byKind.c_str(), first.c_str());
  }
  std::printf("\n%s: %u of %u recorded entries match in every run, and %u more differ only by rounding (%u entries in "
              "the file)\n", name.c_str(), exactEntries, recorded, roundingEntries, entries);
}

//vrnd.bin: the RCX registers at start and 256 draws; then per seed: the seed, the RCX registers after vrnds.s,
//4096 draws; then 256 vrndi.q after seeding 0x12345678.
static auto compareRandom(const std::string& folder) -> void {
  auto w = load(folder + "/vrnd.bin");
  if(w.size() < 8 + 256 + 1024) {
    std::printf("vrnd.bin missing or short\n");
    return;
  }
  Machine m;
  m.cpu.power(Base);
  auto& cpu = m.cpu;
  uint64_t checked = 0, matched = 0;
  auto expect = [&](uint32_t hardware, uint32_t ours, const char* what) {
    checked++;
    if(hardware == ours) { matched++; return; }
    if(checked - matched <= 4) std::printf("  mismatch: %s: PSP %08x, Phobos %08x\n", what, hardware, ours);
  };
  size_t n = 0;
  //the start: the core's power-on state against the PSP's, then draws from the PSP's own start state
  for(uint32_t i = 0; i < 8; i++) expect(w[n + i], cpu.vfpu.rcx[i], "RCX at start");
  for(uint32_t i = 0; i < 8; i++) cpu.vfpu.rcx[i] = w[n + i];
  n += 8;
  for(uint32_t i = 0; i < 256; i++) {
    cpu.execute(Base, 0xd0210004);  //vrndi.s S100
    expect(w[n++], cpu.vfpu.r[4], "draw from start");
  }
  //each seed
  uint32_t seeds = 0;
  while(w.size() - n > 1024) {
    uint32_t seed = w[n++];
    cpu.vfpu.r[4] = seed;
    cpu.execute(Base, 0xd0200400);  //vrnds.s S100
    for(uint32_t i = 0; i < 8; i++) expect(w[n + i], cpu.vfpu.rcx[i], "RCX after vrnds");
    n += 8;
    for(uint32_t i = 0; i < 4096; i++) {
      cpu.execute(Base, 0xd0210004);
      expect(w[n++], cpu.vfpu.r[4], "draw after a seed");
    }
    seeds++;
  }
  //vrndi.q's lanes after seeding 0x12345678
  cpu.vfpu.r[4] = 0x12345678;
  cpu.execute(Base, 0xd0200400);
  for(uint32_t i = 0; i < 256; i++) {
    cpu.execute(Base, 0xd0218088);  //vrndi.q C200
    for(uint32_t lane = 0; lane < 4; lane++) expect(w[n++], cpu.vfpu.r[8 + 32 * lane], "vrndi.q lane");
  }
  std::printf("vrnd: %llu of %llu words match (%u seeds, 4096 draws each, plus the start and vrndi.q)\n",
              (unsigned long long)matched, (unsigned long long)checked, seeds);
}

}

int main(int argc, char** argv) {
  using namespace allegrex_test;
  if(argc < 2) {
    std::fprintf(stderr, "usage: compare <folder of VFPU and FPU results, such as results/vfpu>\n");
    return 1;
  }
  std::string folder = argv[1];
  std::printf("| test | results | exact | worst (ulps) | examples (input -> results) |\n");
  std::printf("| --- | --- | --- | --- | --- |\n");
  for(auto& test : tests) compare(folder, test);
  compareHalves(folder);
  compareFloatsToHalves(folder);
  Records records;
  //FCSR's bits above the causes as the PSP program found them (round 3's fpu-state.bin), kept for every FPU test
  uint32_t upper = 0;
  if(present(folder + "/fpu-state.bin")) {
    auto state = load(folder + "/fpu-state.bin");
    if(!state.empty()) upper = state[0] & ~0x3ffffu;
  }
  compareDivide(folder, records);
  compareConvert(folder, "fpu-convert", upper, "", records);
  compareFpuArithmetic(folder, "fpu-arith", false, upper, "", records);
  records.print();
  Records safe;  //round 3's, in rows of their own, named "safe ..."
  compareConvert(folder, "fpu-convert-safe", upper, "safe ", safe);
  compareFpuArithmetic(folder, "fpu-arith-safe", true, upper, "safe ", safe);
  safe.print();
  reportFpu(folder);
  if(present(folder + "/vrnd.bin")) {
    std::printf("\n");
    compareRandom(folder);
  }
  compareOps(folder, "ops");
  compareOps(folder, "ops3");
  return 0;
}
