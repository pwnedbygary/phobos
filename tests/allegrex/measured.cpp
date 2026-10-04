//The VFPU against a real PSP: results tools/psp-vfpu-measure recorded on the user's PSP (firmware 6.61), which the
//core must reproduce bit for bit. run-tests.sh unpacks them from tests/allegrex/measured/ and passes the folder in
//ALLEGREX_MEASURED; docs/psp-vfpu-measurements.md describes them, and tools/psp-vfpu-measure/compare.sh checks
//the rest of the measurements, which are kept outside the repository.
//
//Covered here: the random number generator (its start, 64 seeds of 4096 draws, vrndi.q's lanes); vadd, vsub,
//vmul and vdiv on a million spread-out inputs between them (NaN results included); the first 16384 spread-out
//results of each math function the core computes exactly (vrcp, vnrcp, vrsq, vsqrt, vexp2, vrexp2, vsin, vnsin,
//vcos, vasin: the *-spread-16k files, which start the full *-spread files); and vlog2, except from 4 up, where the
//PSP sometimes gives one unit less than the core (its spread-out results, and every 1024th result from 1/2 up to
//2). vdot's file is kept for when the core sums its products as the PSP does. From round 2: div and divu on 8192
//pairs, dividing by zero included.

#include "harness.hpp"

#include <cstdlib>
#include <fstream>
#include <string>

namespace allegrex_test {

static auto loadMeasured(const char* name) -> std::vector<uint32_t> {
  const char* folder = std::getenv("ALLEGREX_MEASURED");
  std::vector<uint32_t> words;
  if(!folder) return words;
  std::ifstream file(std::string(folder) + "/" + name, std::ios::binary);
  if(!file) return words;
  file.seekg(0, std::ios::end);
  words.resize(size_t(file.tellg()) / 4);
  file.seekg(0);
  file.read((char*)words.data(), words.size() * 4);
  return words;
}

//vrnd.bin: the RCX registers at start and 256 draws; then per seed, the seed, the RCX registers after vrnds.s and
//4096 draws; then after seeding 0x12345678, 256 vrndi.q (lane 0 first). Draws go to S100 (register 4) as on the
//PSP; vrndi.q to C200 (registers 8, 40, 72, 104).
static auto measuredRandom() -> void {
  auto w = loadMeasured("vrnd.bin");
  CHECK(w.size(), 264008);
  if(w.size() != 264008) return;
  Machine m;
  m.cpu.power(Base);
  auto& cpu = m.cpu;
  uint64_t checked = 0, matched = 0;
  auto expect = [&](uint32_t hardware, uint32_t ours) { checked++; matched += hardware == ours; };
  size_t n = 0;
  for(uint32_t i = 0; i < 8; i++) expect(w[n + i], cpu.vfpu.rcx[i]);  //the power-on state
  n += 8;
  for(uint32_t i = 0; i < 256; i++) {
    cpu.execute(Base, 0xd0210004);  //vrndi.s S100
    expect(w[n++], cpu.vfpu.r[4]);
  }
  for(uint32_t seed = 0; seed < 64; seed++) {
    cpu.vfpu.r[4] = w[n++];
    cpu.execute(Base, 0xd0200400);  //vrnds.s S100
    for(uint32_t i = 0; i < 8; i++) expect(w[n + i], cpu.vfpu.rcx[i]);
    n += 8;
    for(uint32_t i = 0; i < 4096; i++) {
      cpu.execute(Base, 0xd0210004);
      expect(w[n++], cpu.vfpu.r[4]);
    }
  }
  cpu.vfpu.r[4] = 0x12345678;
  cpu.execute(Base, 0xd0200400);
  for(uint32_t i = 0; i < 256; i++) {
    cpu.execute(Base, 0xd0218088);  //vrndi.q C200
    for(uint32_t lane = 0; lane < 4; lane++) expect(w[n++], cpu.vfpu.r[8 + 32 * lane]);
  }
  CHECK(matched, checked);
}

//An arithmetic file: the results of the instruction on quads whose lanes came from the PSP program's generator,
//state * 1664525 + 1013904223 from the seed: per quad, s's four lanes, then t's. s was in C000, t in C010, the
//result in C020.
static auto measuredArithmetic(const char* name, uint32_t instruction, uint32_t seed) -> void {
  auto hardware = loadMeasured(name);
  CHECK(hardware.size(), 1u << 18);
  if(hardware.size() != 1u << 18) return;
  Machine m;
  m.cpu.power(Base);
  auto& v = m.cpu.vfpu.r;
  uint32_t state = seed, exact = 0;
  auto next = [&] { return state = state * 1664525u + 1013904223u; };
  for(uint32_t quad = 0; quad < hardware.size() / 4; quad++) {
    for(uint32_t lane = 0; lane < 4; lane++) v[32 * lane] = next();
    for(uint32_t lane = 0; lane < 4; lane++) v[1 + 32 * lane] = next();
    m.cpu.execute(Base, instruction);
    for(uint32_t lane = 0; lane < 4; lane++) exact += hardware[quad * 4 + lane] == v[2 + 32 * lane];
  }
  CHECK(exact, hardware.size());
}

//A math function's first results on spread-out inputs: per quad, four inputs from the generator in C000, the
//results in C010. checked picks the inputs whose results the core must match (all of them if not given).
static auto measuredFunction(const char* name, uint32_t instruction, uint32_t seed,
                             bool (*checked)(uint32_t input) = nullptr) -> void {
  auto hardware = loadMeasured(name);
  CHECK(hardware.size(), 16384);
  if(hardware.size() != 16384) return;
  Machine m;
  m.cpu.power(Base);
  auto& v = m.cpu.vfpu.r;
  uint32_t state = seed, input[4], count = 0, exact = 0;
  for(uint32_t quad = 0; quad < hardware.size() / 4; quad++) {
    for(uint32_t lane = 0; lane < 4; lane++) v[32 * lane] = input[lane] = state = state * 1664525u + 1013904223u;
    m.cpu.execute(Base, instruction);
    for(uint32_t lane = 0; lane < 4; lane++) {
      if(checked && !checked(input[lane])) continue;
      count++;
      exact += hardware[quad * 4 + lane] == v[1 + 32 * lane];
    }
  }
  CHECK(exact, count);
}

//Every 1024th result of a sweep over consecutive inputs from first (the *-1k files). The instruction works out
//each lane on its own, so they're replayed four to a quad.
static auto measuredSweep(const char* name, uint32_t instruction, uint32_t first) -> void {
  auto hardware = loadMeasured(name);
  CHECK(hardware.size(), 16384);
  if(hardware.size() != 16384) return;
  Machine m;
  m.cpu.power(Base);
  auto& v = m.cpu.vfpu.r;
  uint32_t exact = 0;
  for(uint32_t quad = 0; quad < hardware.size() / 4; quad++) {
    for(uint32_t lane = 0; lane < 4; lane++) v[32 * lane] = first + (quad * 4 + lane) * 1024;
    m.cpu.execute(Base, instruction);
    for(uint32_t lane = 0; lane < 4; lane++) exact += hardware[quad * 4 + lane] == v[1 + 32 * lane];
  }
  CHECK(exact, hardware.size());
}

//ipu-divide.bin (round 2): per pair, a and b, then div's lo and hi, then divu's lo and hi. The first 1024 pairs are
//32 chosen numbers each with each (zero, the most negative number and -1 among them), the rest spread out.
static auto measuredDivide() -> void {
  auto hardware = loadMeasured("ipu-divide.bin");
  CHECK(hardware.size(), 8192u * 6);
  if(hardware.size() != 8192u * 6) return;
  Machine m;
  m.cpu.power(Base);
  uint32_t exact = 0;
  for(uint32_t pair = 0; pair < 8192; pair++) {
    const uint32_t* p = &hardware[pair * 6];
    m.cpu.ipu.r[t0] = p[0];
    m.cpu.ipu.r[t1] = p[1];
    m.cpu.execute(Base, div_(t0, t1));
    bool same = m.cpu.ipu.lo == p[2] && m.cpu.ipu.hi == p[3];
    m.cpu.execute(Base, divu(t0, t1));
    exact += same && m.cpu.ipu.lo == p[4] && m.cpu.ipu.hi == p[5];
  }
  CHECK(exact, 8192u);
}

auto measured() -> void {
  if(!std::getenv("ALLEGREX_MEASURED")) {
    std::printf("(no ALLEGREX_MEASURED folder: run-tests.sh sets it)\n");
    failures++;
    return;
  }
  measuredRandom();
  measuredArithmetic("vadd-spread.bin", 0x60018082, 12);  //vadd.q C020, C000, C010
  measuredArithmetic("vsub-spread.bin", 0x60818082, 13);
  measuredArithmetic("vmul-spread.bin", 0x64018082, 14);
  measuredArithmetic("vdiv-spread.bin", 0x63818082, 15);
  measuredFunction("vrcp-spread-16k.bin", 0xd0108081, 1);  //vrcp.q C010, C000
  measuredFunction("vnrcp-spread-16k.bin", 0xd0188081, 2);
  measuredFunction("vrsq-spread-16k.bin", 0xd0118081, 3);
  measuredFunction("vsqrt-spread-16k.bin", 0xd0168081, 4);
  measuredFunction("vexp2-spread-16k.bin", 0xd0148081, 5);
  measuredFunction("vrexp2-spread-16k.bin", 0xd01c8081, 6);
  measuredFunction("vsin-spread-16k.bin", 0xd0128081, 8);
  measuredFunction("vnsin-spread-16k.bin", 0xd01a8081, 9);
  measuredFunction("vcos-spread-16k.bin", 0xd0138081, 10);
  measuredFunction("vasin-spread-16k.bin", 0xd0178081, 11);
  auto belowFour = [](uint32_t x) { return x < 0x4080'0000 || x >= 0x7f80'0000; };  //all but positive x >= 4
  measuredFunction("vlog2-spread-16k.bin", 0xd0158081, 7, belowFour);
  measuredSweep("vlog2-half-2-1k.bin", 0xd0158081, 0x3f00'0000);
  measuredDivide();
}

}
