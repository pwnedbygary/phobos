//The N64 RSP's recompiler against its interpreter, on the RSP as the desktop program builds and links it
//(phobos_core's objects, CMakeLists.txt's target rsp-recompiler): VMOV for every element and destination element,
//with Vd and Vt apart and the same, then every COP2 instruction and the vector loads and stores the recompiler
//compiles (LWC2 and SWC2, their base register known to it or not). Each runs once recompiled and once interpreted
//from the same seeded random state (the VU's registers, accumulators and flags, the scalar registers, DMEM), and the
//two states after it are compared whole. The stack below the run is filled with a pattern first, so a value read from
//a stale stack slot (as GCC compiled RSP::VMOV without ares's -fno-strict-aliasing) is the same in every run.
//usage: rsp-recompiler [states per form] [seed]
#include <n64/n64.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>

namespace ares {
  //what the runner (android/app/src/main/cpp/PhobosRunner.cpp) gives the core, without the runner
  auto n64DebugLoggingEnabled() -> bool { return false; }
  static Platform testPlatform;
  Platform* platform = &testPlatform;
}

using namespace ares::Nintendo64;

namespace {

struct State {
  u8 vr[32][16];
  u8 acc[3][16];    //ACCH, ACCM, ACCL
  u8 flags[5][16];  //VCOH, VCOL, VCCH, VCCL, VCE
  s16 divin, divout;
  bool divdp;
  u32 gpr[32];
  u8 dmem[4096];
};

auto save(State& s) -> void {
  for(u32 n = 0; n < 32; n++) memcpy(s.vr[n], &rsp.vpu.r[n], 16);
  memcpy(s.acc[0], &rsp.vpu.acch, 16), memcpy(s.acc[1], &rsp.vpu.accm, 16), memcpy(s.acc[2], &rsp.vpu.accl, 16);
  memcpy(s.flags[0], &rsp.vpu.vcoh, 16), memcpy(s.flags[1], &rsp.vpu.vcol, 16);
  memcpy(s.flags[2], &rsp.vpu.vcch, 16), memcpy(s.flags[3], &rsp.vpu.vccl, 16), memcpy(s.flags[4], &rsp.vpu.vce, 16);
  s.divin = rsp.vpu.divin, s.divout = rsp.vpu.divout, s.divdp = rsp.vpu.divdp;
  for(u32 n = 0; n < 32; n++) s.gpr[n] = rsp.ipu.r[n].u32;
  memcpy(s.dmem, rsp.dmem.data, 4096);
}

auto load(const State& s) -> void {
  for(u32 n = 0; n < 32; n++) memcpy(&rsp.vpu.r[n], s.vr[n], 16);
  memcpy(&rsp.vpu.acch, s.acc[0], 16), memcpy(&rsp.vpu.accm, s.acc[1], 16), memcpy(&rsp.vpu.accl, s.acc[2], 16);
  memcpy(&rsp.vpu.vcoh, s.flags[0], 16), memcpy(&rsp.vpu.vcol, s.flags[1], 16);
  memcpy(&rsp.vpu.vcch, s.flags[2], 16), memcpy(&rsp.vpu.vccl, s.flags[3], 16), memcpy(&rsp.vpu.vce, s.flags[4], 16);
  rsp.vpu.divin = s.divin, rsp.vpu.divout = s.divout, rsp.vpu.divdp = s.divdp;
  for(u32 n = 0; n < 32; n++) rsp.ipu.r[n].u32 = s.gpr[n];
  memcpy(rsp.dmem.data, s.dmem, 4096);
}

//the stack below the caller, where the run's frames go, filled with 0xa5
__attribute__((noinline)) auto fillStack() -> void {
  u8 pattern[32 * 1024];
  memset(pattern, 0xa5, sizeof(pattern));
  asm volatile("" : : "r"(pattern) : "memory");
}

//the program (its words, then BREAK and BREAK's delay slot) run from start until BREAK halts the RSP
auto run(const State& start, bool recompiled, State& end) -> bool {
  load(start);
  rsp.ipu.pc = 0;
  rsp.branch.setPc(0);
  rsp.pipeline = {};
  rsp.status.halted = 0;
  rsp.status.broken = 0;
  rsp.recompiler.enabled = recompiled;
  if(recompiled) rsp.recompiler.block(0);  //compiled now: the run below finds it compiled
  fillStack();
  for(u32 steps = 0; !rsp.status.halted; steps++) {
    if(steps == 64) return false;
    rsp.instruction();
  }
  save(end);
  return true;
}

std::mt19937_64 rng;
auto pick(u32 range) -> u32 { return u32(rng() % range); }

//a 16-bit lane: often one of the values instructions treat apart
auto lane() -> u16 {
  static const u16 special[] = {0x0000, 0x0001, 0x7fff, 0x8000, 0x8001, 0xffff, 0x00ff, 0xff00};
  return pick(4) ? u16(rng()) : special[pick(8)];
}

auto randomState(State& s) -> void {
  for(auto& r : s.vr) for(u32 n = 0; n < 16; n += 2) { u16 v = lane(); memcpy(&r[n], &v, 2); }
  for(auto& r : s.acc) for(u32 n = 0; n < 16; n += 2) { u16 v = lane(); memcpy(&r[n], &v, 2); }
  for(auto& r : s.flags) for(u32 n = 0; n < 16; n += 2) { u16 v = pick(2) ? 0xffff : 0; memcpy(&r[n], &v, 2); }
  s.divin = s16(lane()), s.divout = s16(lane()), s.divdp = pick(2);
  s.gpr[0] = 0;
  for(u32 n = 1; n < 32; n++) s.gpr[n] = u32(rng());
  for(auto& byte : s.dmem) byte = u8(rng());
}

auto describe(u32 word) -> std::string {
  rsp.disassembler.showColors = 0;
  rsp.disassembler.showValues = 0;
  char text[160];
  snprintf(text, sizeof(text), "%08x %s", word, (const char*)rsp.disassembler.disassemble(0, word));
  return text;
}

//the instruction a case tests (its last word): the disassembler's mnemonic, or an invalid COP2 one's function
auto instructionName(u32 word) -> std::string {
  rsp.disassembler.showColors = 0;
  rsp.disassembler.showValues = 0;
  std::string text = (const char*)rsp.disassembler.disassemble(0, word);
  std::string name = text.substr(0, text.find(' '));
  if(name == "invalid" && word >> 26 == 0x12 && word >> 25 & 1) {
    char function[32];
    snprintf(function, sizeof(function), "COP2 function 0x%02x", word & 0x3f);
    name = function;
  }
  return name;
}

u32 cases = 0, differences = 0, shown = 0;
std::map<std::string, std::pair<u32, u32>> instructions;  //the group's cases and differences, by instruction

//one case: the words (BREAK and a NOP appended) recompiled and interpreted from the same state
auto check(const char* group, std::initializer_list<u32> words, const State& start) -> bool {
  u32 address = 0;
  for(u32 word : words) rsp.imem.write<Word>(address, word), address += 4;
  rsp.imem.write<Word>(address, 0x0000'000d), rsp.imem.write<Word>(address + 4, 0);  //BREAK, NOP
  if(cases % 256 == 0) rsp.recompiler.allocator.release();
  rsp.recompiler.reset();
  cases++;
  auto& counts = instructions[instructionName(*(words.end() - 1))];
  counts.first++;
  static State recompiled, interpreted;
  bool ran = run(start, true, recompiled) && run(start, false, interpreted);
  std::string found;
  auto differs = [&](const char* name, const u8* a, const u8* b, u32 size) {
    if(!memcmp(a, b, size)) return;
    char text[96];
    for(u32 n = 0; n < size; n += 2) {
      u16 x, y;
      memcpy(&x, a + n, 2), memcpy(&y, b + n, 2);
      if(x == y) continue;
      snprintf(text, sizeof(text), " %s byte %u: %04x against %04x;", name, n, x, y);
      found += text;
      return;
    }
  };
  if(!ran) found = " didn't reach BREAK";
  else {
    char name[16];
    for(u32 n = 0; n < 32; n++) {
      snprintf(name, sizeof(name), "v%u", n);
      differs(name, recompiled.vr[n], interpreted.vr[n], 16);
    }
    static const char* accs[] = {"ACCH", "ACCM", "ACCL"};
    for(u32 n = 0; n < 3; n++) differs(accs[n], recompiled.acc[n], interpreted.acc[n], 16);
    static const char* flags[] = {"VCOH", "VCOL", "VCCH", "VCCL", "VCE"};
    for(u32 n = 0; n < 5; n++) differs(flags[n], recompiled.flags[n], interpreted.flags[n], 16);
    if(recompiled.divin != interpreted.divin || recompiled.divout != interpreted.divout
    || recompiled.divdp != interpreted.divdp) found += " DIVIN, DIVOUT or DIVDP;";
    for(u32 n = 0; n < 32; n++) {
      if(recompiled.gpr[n] == interpreted.gpr[n]) continue;
      char text[64];
      snprintf(text, sizeof(text), " r%u: %08x against %08x;", n, recompiled.gpr[n], interpreted.gpr[n]);
      found += text;
    }
    for(u32 n = 0; n < 4096; n++) {
      if(recompiled.dmem[n] == interpreted.dmem[n]) continue;
      char text[64];
      snprintf(text, sizeof(text), " DMEM %03x: %02x against %02x;", n, recompiled.dmem[n], interpreted.dmem[n]);
      found += text;
      break;
    }
  }
  if(found.empty()) return true;
  differences++;
  counts.second++;
  if(shown++ < 5) {
    printf("%s: ", group);
    for(u32 word : words) printf("[%s] ", describe(word).c_str());
    printf("recompiled against interpreted:%s\n", found.c_str());
  }
  return false;
}

auto cop2(u32 funct, u32 e, u32 vt, u32 vs, u32 vd) -> u32 {
  return 0x12u << 26 | 1u << 25 | e << 21 | vt << 16 | vs << 11 | vd << 6 | funct;
}

}

int main(int argc, char** argv) {
  u32 states = argc > 1 ? u32(strtoul(argv[1], nullptr, 0)) : 16;
  u64 seed = argc > 2 ? strtoull(argv[2], nullptr, 0) : 0x5253505245ull;
  rng.seed(seed);
  printf("rsp-recompiler: built by %s, %u states per form, seed 0x%llx\n", __VERSION__, states,
    (unsigned long long)seed);

  auto root = ares::Core::Object::create();
  rsp.load(root);
  rsp.power(false);

  static State start;
  auto group = [&](const char* name, auto&& body) {
    u32 before = cases, failed = differences;
    shown = 0;
    instructions.clear();
    body();
    printf("%s: %u cases, %u differ\n", name, cases - before, differences - failed);
    for(auto& [instruction, counts] : instructions) {
      if(counts.second) printf("  %s: %u of %u differ\n", instruction.c_str(), counts.second, counts.first);
    }
  };

  //VMOV Vd(de),Vt(e): de is Vs's low three bits
  group("VMOV", [&] {
    for(u32 e = 0; e < 16; e++)
    for(u32 de = 0; de < 8; de++)
    for(u32 same = 0; same < 2; same++)
    for(u32 n = 0; n < states; n++) {
      u32 vd = pick(32), vt = same ? vd : (vd + 1 + pick(31)) % 32, vs = de | pick(4) << 3;
      randomState(start);
      check("VMOV", {cop2(0x33, e, vt, vs, vd)}, start);
    }
  });

  //every COP2 computational function, the invalid ones too
  group("COP2 functions", [&] {
    for(u32 funct = 0; funct < 64; funct++)
    for(u32 e = 0; e < 16; e++)
    for(u32 n = 0; n < states; n++) {
      u32 vd = pick(32), vs = pick(4) ? pick(32) : vd, vt = pick(4) ? pick(32) : vs;
      randomState(start);
      check("COP2 functions", {cop2(funct, e, vt, vs, vd)}, start);
    }
  });

  //MFC2, CFC2, MTC2 and CTC2
  group("COP2 moves", [&] {
    for(u32 rs : {0u, 2u, 4u, 6u})
    for(u32 e = 0; e < 16; e++)
    for(u32 n = 0; n < states * 2; n++) {
      u32 rt = pick(32), rd = rs == 2 || rs == 6 ? pick(4) : pick(32);
      randomState(start);
      check("COP2 moves", {0x12u << 26 | rs << 21 | rt << 16 | rd << 11 | e << 7}, start);
    }
  });

  //LWC2 and SWC2, LBV to LTV and SBV to STV: the base register's value unknown to the recompiler, then set by an
  //ADDIU before it in the same block (so the recompiler knows it). Half the addresses near DMEM's end, where they wrap.
  group("LWC2 and SWC2", [&] {
    for(u32 opcode : {0x32u, 0x3au})
    for(u32 funct = 0; funct < 12; funct++)
    for(u32 e = 0; e < 16; e++)
    for(u32 known = 0; known < 2; known++)
    for(u32 n = 0; n < states; n++) {
      u32 base = 1 + pick(31), vt = pick(32), offset = pick(128);
      u32 address = pick(2) ? 0xf80 + pick(0x80) : pick(0x1000);
      randomState(start);
      u32 word = opcode << 26 | base << 21 | vt << 16 | funct << 11 | e << 7 | offset;
      if(known) {
        check("LWC2 and SWC2", {0x09u << 26 | base << 16 | address, word}, start);  //ADDIU base, r0, address
      } else {
        start.gpr[base] = address | u32(rng()) << 12;
        check("LWC2 and SWC2", {word}, start);
      }
    }
  });

  printf("rsp-recompiler: %u cases, %u differ\n", cases, differences);
  return differences ? 1 : 0;
}
