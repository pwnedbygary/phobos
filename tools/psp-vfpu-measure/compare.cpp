//compare: checks Phobos's VFPU against what a real PSP computed (the results psp-vfpu-measure writes; see
//main.c for the files). Every input is made again exactly as the PSP program made it, run through the core's own
//instruction (the same instruction word the PSP ran), and the result compared bit for bit.
//usage: tools/psp-vfpu-measure/compare.sh <results folder>
//
//For each test it reports how many results match exactly, and for the others how far off they are in units in
//the last place (ulps: how many representable floats apart the two results are), plus a few examples.

#include "../../tests/allegrex/harness.hpp"

#include <cstring>
#include <fstream>
#include <string>

namespace allegrex_test {

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

enum class Inputs { FromOne, FromHalf, Fixed23, Spread };
enum class Kind { Unary, Binary, Dot };

static auto input(Inputs inputs, uint32_t k, Generator& generator) -> uint32_t {
  switch(inputs) {
  case Inputs::FromOne: return 0x3f800000u + k;
  case Inputs::FromHalf: return 0x3f000000u + k;
  case Inputs::Fixed23: return bits((float)k / 8388608.0f);
  case Inputs::Spread: return generator.next();
  }
  return 0;
}

struct Test {
  const char* name;
  uint32_t instruction;  //as psp-objdump showed it in the PSP program
  Kind kind;
  Inputs inputs;
  uint32_t count;
  uint32_t seed;
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

static auto compare(const std::string& folder, const Test& test) -> void {
  auto hardware = load(folder + "/" + test.name + ".bin");
  uint32_t results = test.count;
  if(hardware.size() != results) {
    std::printf("| %s | missing or short (%zu of %u words) | | | |\n", test.name, hardware.size(), results);
    return;
  }
  Machine m;
  m.cpu.power(Base);
  auto& v = m.cpu.vfpu.r;
  Generator generator{test.seed};
  Stats stats;
  uint32_t k = 0;
  if(test.kind == Kind::Dot) {
    for(uint32_t i = 0; i < results; i++) {
      uint32_t s[4], t[4];
      for(uint32_t lane = 0; lane < 4; lane++) s[lane] = input(test.inputs, k++, generator);
      for(uint32_t lane = 0; lane < 4; lane++) t[lane] = input(test.inputs, k++, generator);
      for(uint32_t lane = 0; lane < 4; lane++) v[ColumnS[lane]] = s[lane], v[ColumnT[lane]] = t[lane];
      m.cpu.execute(Base, test.instruction);
      stats.add(s[0], hardware[i], v[ColumnD[0]]);
    }
  } else {
    for(uint32_t quad = 0; quad < results / 4; quad++) {
      uint32_t s[4], t[4] = {};
      for(uint32_t lane = 0; lane < 4; lane++) s[lane] = input(test.inputs, k++, generator);
      if(test.kind == Kind::Binary) for(uint32_t lane = 0; lane < 4; lane++) t[lane] = input(test.inputs, k++, generator);
      for(uint32_t lane = 0; lane < 4; lane++) v[ColumnS[lane]] = s[lane], v[ColumnT[lane]] = t[lane];
      m.cpu.execute(Base, test.instruction);
      const uint32_t* out = test.kind == Kind::Binary ? ColumnD : ColumnT;
      for(uint32_t lane = 0; lane < 4; lane++) stats.add(s[lane], hardware[quad * 4 + lane], v[out[lane]]);
    }
  }
  std::printf("| %s | %llu | %llu (%.2f%%) | %llu | %s |\n", test.name, (unsigned long long)stats.total,
              (unsigned long long)stats.exact, 100.0 * stats.exact / stats.total, (unsigned long long)stats.maxUlps,
              stats.examples.empty() ? "" : stats.examples[0].c_str());
  for(size_t i = 1; i < stats.examples.size(); i++) std::printf("|  |  |  |  | %s |\n", stats.examples[i].c_str());
  if(stats.nans) std::printf("|  |  | (%llu more where both are NaN, with different bits) |  |  |\n", (unsigned long long)stats.nans);
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
    std::fprintf(stderr, "usage: compare <results folder>\n");
    return 1;
  }
  std::string folder = argv[1];
  std::printf("| test | results | exact | worst (ulps) | examples (input -> results) |\n");
  std::printf("| --- | --- | --- | --- | --- |\n");
  for(auto& test : tests) compare(folder, test);
  std::printf("\n");
  compareRandom(folder);
  return 0;
}
