//Runs the NEON sequences of ares/n64/rsp/vu-neon.hpp on random register and accumulator states
//and compares the whole VU state with the SSE (through sse2neon, as ares runs on ARM) and scalar
//implementations of the same instructions, copied from ares/n64/rsp/interpreter-vpu.cpp.
//AArch64 hosts only (macOS or Linux).
//usage: rsp-vu-neon [states per sequence] [seed]

#include "sse2neon.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sys/mman.h>
#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

#include "../../ares/n64/rsp/vu-neon.hpp"

using RspVuNeon::Op;

struct alignas(16) VU {
  uint16_t r[32][8];
  uint16_t acch[8], accm[8], accl[8];
};
static const RspVuNeon::Layout layout{offsetof(VU, r), offsetof(VU, acch), offsetof(VU, accm), offsetof(VU, accl)};

static const char* opName(Op op) {
  static const char* names[] = {"VMUDL", "VMUDM", "VMUDN", "VMUDH", "VMADL", "VMADM", "VMADN", "VMADH"};
  return names[(uint32_t)op];
}

//scalar reference (Accuracy::RSP::SISD); element n is lane 7 - n
namespace sisd {
  static auto u16(const uint16_t* v, int n) -> uint16_t { return v[7 - n]; }
  static auto s16(const uint16_t* v, int n) -> int16_t { return (int16_t)v[7 - n]; }
  static auto set(uint16_t* v, int n, uint16_t x) -> void { v[7 - n] = x; }

  static auto broadcast(const uint16_t* src, int e, uint16_t* v) -> void {
    memcpy(v, src, 16);
    auto g = [&](int n) { return u16(v, n); };
    auto s = [&](int n, uint16_t x) { set(v, n, x); };
    switch(e) {
    case 2: s(1, g(0)); s(3, g(2)); s(5, g(4)); s(7, g(6)); break;
    case 3: s(0, g(1)); s(2, g(3)); s(4, g(5)); s(6, g(7)); break;
    case 4: { auto a = g(0), b = g(4); s(1, a); s(2, a); s(3, a); s(5, b); s(6, b); s(7, b); break; }
    case 5: { auto a = g(1), b = g(5); s(0, a); s(2, a); s(3, a); s(4, b); s(6, b); s(7, b); break; }
    case 6: { auto a = g(2), b = g(6); s(0, a); s(1, a); s(3, a); s(4, b); s(5, b); s(7, b); break; }
    case 7: { auto a = g(3), b = g(7); s(0, a); s(1, a); s(2, a); s(4, b); s(5, b); s(6, b); break; }
    default: if(e >= 8) { auto a = g(e - 8); for(int n = 0; n < 8; n++) s(n, a); } break;
    }
  }

  static auto accGet(const VU& s, int n) -> uint64_t {
    return (uint64_t)u16(s.acch, n) << 32 | (uint64_t)u16(s.accm, n) << 16 | (uint64_t)u16(s.accl, n);
  }
  static auto accSet(VU& s, int n, uint64_t x) -> void {
    set(s.acch, n, x >> 32); set(s.accm, n, x >> 16); set(s.accl, n, x);
  }
  static auto accSaturate(const VU& s, int n, bool slice, uint16_t negative, uint16_t positive) -> uint16_t {
    if(s16(s.acch, n) < 0) {
      if(u16(s.acch, n) != 0xffff) return negative;
      if(s16(s.accm, n) >= 0) return negative;
    } else {
      if(u16(s.acch, n) != 0x0000) return positive;
      if(s16(s.accm, n) < 0) return positive;
    }
    return !slice ? u16(s.accl, n) : u16(s.accm, n);
  }

  //unsigned 16x16 products are formed in uint32_t: ares computes them in int, which overflows
  //for large operands (undefined, in practice the same bits)
  static auto run(VU& s, Op op, int e, int d, int vsi, int vti) -> void {
    uint16_t vte[8];
    broadcast(s.r[vti], e, vte);
    uint16_t* vd = s.r[d];
    const uint16_t* vs = s.r[vsi];
    switch(op) {
    case Op::VMUDL:
      for(int n = 0; n < 8; n++) accSet(s, n, (uint16_t)((uint32_t)u16(vs, n) * u16(vte, n) >> 16));
      memcpy(vd, s.accl, 16);
      break;
    case Op::VMUDM:
      for(int n = 0; n < 8; n++) accSet(s, n, (uint64_t)(int64_t)(int32_t)(s16(vs, n) * (int32_t)u16(vte, n)));
      memcpy(vd, s.accm, 16);
      break;
    case Op::VMUDN:
      for(int n = 0; n < 8; n++) accSet(s, n, (uint64_t)(int64_t)(int32_t)((int32_t)u16(vs, n) * s16(vte, n)));
      memcpy(vd, s.accl, 16);
      break;
    case Op::VMUDH:
      for(int n = 0; n < 8; n++) {
        accSet(s, n, (uint64_t)((int64_t)(s16(vs, n) * s16(vte, n)) * 65536));
        set(vd, n, accSaturate(s, n, 1, 0x8000, 0x7fff));
      }
      break;
    case Op::VMADL:
      for(int n = 0; n < 8; n++) {
        accSet(s, n, accGet(s, n) + ((uint32_t)u16(vs, n) * u16(vte, n) >> 16));
        set(vd, n, accSaturate(s, n, 0, 0x0000, 0xffff));
      }
      break;
    case Op::VMADM:
      for(int n = 0; n < 8; n++) {
        accSet(s, n, accGet(s, n) + (uint64_t)(int64_t)(s16(vs, n) * (int32_t)u16(vte, n)));
        set(vd, n, accSaturate(s, n, 1, 0x8000, 0x7fff));
      }
      break;
    case Op::VMADN:
      for(int n = 0; n < 8; n++) {
        accSet(s, n, accGet(s, n) + (uint64_t)(int64_t)((int32_t)u16(vs, n) * s16(vte, n)));
        set(vd, n, accSaturate(s, n, 0, 0x0000, 0xffff));
      }
      break;
    case Op::VMADH:
      for(int n = 0; n < 8; n++) {
        int32_t result = (int32_t)(uint32_t)((accGet(s, n) >> 16) + (uint64_t)(int64_t)(s16(vs, n) * s16(vte, n)));
        set(s.acch, n, (uint32_t)result >> 16);
        set(s.accm, n, (uint32_t)result);
        set(vd, n, accSaturate(s, n, 1, 0x8000, 0x7fff));
      }
      break;
    }
  }
}

//SSE reference (Accuracy::RSP::SIMD), the code ares runs on ARM through sse2neon
namespace simd {
  static const __m128i shuffle[16] = {
    _mm_set_epi8(15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0),
    _mm_set_epi8(15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0),
    _mm_set_epi8(15,14,15,14,11,10,11,10, 7, 6, 7, 6, 3, 2, 3, 2),
    _mm_set_epi8(13,12,13,12, 9, 8, 9, 8, 5, 4, 5, 4, 1, 0, 1, 0),
    _mm_set_epi8(15,14,15,14,15,14,15,14, 7, 6, 7, 6, 7, 6, 7, 6),
    _mm_set_epi8(13,12,13,12,13,12,13,12, 5, 4, 5, 4, 5, 4, 5, 4),
    _mm_set_epi8(11,10,11,10,11,10,11,10, 3, 2, 3, 2, 3, 2, 3, 2),
    _mm_set_epi8( 9, 8, 9, 8, 9, 8, 9, 8, 1, 0, 1, 0, 1, 0, 1, 0),
    _mm_set_epi8(15,14,15,14,15,14,15,14,15,14,15,14,15,14,15,14),
    _mm_set_epi8(13,12,13,12,13,12,13,12,13,12,13,12,13,12,13,12),
    _mm_set_epi8(11,10,11,10,11,10,11,10,11,10,11,10,11,10,11,10),
    _mm_set_epi8( 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8, 9, 8),
    _mm_set_epi8( 7, 6, 7, 6, 7, 6, 7, 6, 7, 6, 7, 6, 7, 6, 7, 6),
    _mm_set_epi8( 5, 4, 5, 4, 5, 4, 5, 4, 5, 4, 5, 4, 5, 4, 5, 4),
    _mm_set_epi8( 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2),
    _mm_set_epi8( 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0),
  };

  static auto load(const uint16_t* p) -> __m128i { return _mm_load_si128((const __m128i*)p); }
  static auto store(uint16_t* p, __m128i v) -> void { _mm_store_si128((__m128i*)p, v); }

  static auto run(VU& st, Op op, int e, int d, int vsi, int vti) -> void {
    const __m128i zero = _mm_setzero_si128();
    __m128i vs = load(st.r[vsi]), vte = _mm_shuffle_epi8(load(st.r[vti]), shuffle[e]), vd;
    __m128i ACCH = load(st.acch), ACCM = load(st.accm), ACCL = load(st.accl);
    __m128i lo, hi, sign, vsa, vta, omask, nhi, nmd, shi, smd, cmask, cval;
    switch(op) {
    case Op::VMUDL:
      ACCL = _mm_mulhi_epu16(vs, vte);
      ACCM = zero;
      ACCH = zero;
      vd   = ACCL;
      break;
    case Op::VMUDM:
      ACCL = _mm_mullo_epi16(vs, vte);
      ACCM = _mm_mulhi_epu16(vs, vte);
      sign = _mm_srai_epi16(vs, 15);
      vta  = _mm_and_si128(vte, sign);
      ACCM = _mm_sub_epi16(ACCM, vta);
      ACCH = _mm_srai_epi16(ACCM, 15);
      vd   = ACCM;
      break;
    case Op::VMUDN:
      ACCL = _mm_mullo_epi16(vs, vte);
      ACCM = _mm_mulhi_epu16(vs, vte);
      sign = _mm_srai_epi16(vte, 15);
      vsa  = _mm_and_si128(vs, sign);
      ACCM = _mm_sub_epi16(ACCM, vsa);
      ACCH = _mm_srai_epi16(ACCM, 15);
      vd   = ACCL;
      break;
    case Op::VMUDH:
      ACCL = zero;
      ACCM = _mm_mullo_epi16(vs, vte);
      ACCH = _mm_mulhi_epi16(vs, vte);
      lo   = _mm_unpacklo_epi16(ACCM, ACCH);
      hi   = _mm_unpackhi_epi16(ACCM, ACCH);
      vd   = _mm_packs_epi32(lo, hi);
      break;
    case Op::VMADL:
      hi    = _mm_mulhi_epu16(vs, vte);
      omask = _mm_adds_epu16(ACCL, hi);
      ACCL  = _mm_add_epi16(ACCL, hi);
      omask = _mm_cmpeq_epi16(ACCL, omask);
      omask = _mm_cmpeq_epi16(omask, zero);
      hi    = _mm_sub_epi16(zero, omask);
      omask = _mm_adds_epu16(ACCM, hi);
      ACCM  = _mm_add_epi16(ACCM, hi);
      omask = _mm_cmpeq_epi16(ACCM, omask);
      omask = _mm_cmpeq_epi16(omask, zero);
      ACCH  = _mm_sub_epi16(ACCH, omask);
      nhi   = _mm_srai_epi16(ACCH, 15);
      nmd   = _mm_srai_epi16(ACCM, 15);
      shi   = _mm_cmpeq_epi16(nhi, ACCH);
      smd   = _mm_cmpeq_epi16(nhi, nmd);
      cmask = _mm_and_si128(smd, shi);
      cval  = _mm_cmpeq_epi16(nhi, zero);
      vd    = _mm_blendv_epi8(cval, ACCL, cmask);
      break;
    case Op::VMADM:
      lo    = _mm_mullo_epi16(vs, vte);
      hi    = _mm_mulhi_epu16(vs, vte);
      sign  = _mm_srai_epi16(vs, 15);
      vta   = _mm_and_si128(vte, sign);
      hi    = _mm_sub_epi16(hi, vta);
      omask = _mm_adds_epu16(ACCL, lo);
      ACCL  = _mm_add_epi16(ACCL, lo);
      omask = _mm_cmpeq_epi16(ACCL, omask);
      omask = _mm_cmpeq_epi16(omask, zero);
      hi    = _mm_sub_epi16(hi, omask);
      omask = _mm_adds_epu16(ACCM, hi);
      ACCM  = _mm_add_epi16(ACCM, hi);
      omask = _mm_cmpeq_epi16(ACCM, omask);
      omask = _mm_cmpeq_epi16(omask, zero);
      hi    = _mm_srai_epi16(hi, 15);
      ACCH  = _mm_add_epi16(ACCH, hi);
      ACCH  = _mm_sub_epi16(ACCH, omask);
      lo    = _mm_unpacklo_epi16(ACCM, ACCH);
      hi    = _mm_unpackhi_epi16(ACCM, ACCH);
      vd    = _mm_packs_epi32(lo, hi);
      break;
    case Op::VMADN:
      lo    = _mm_mullo_epi16(vs, vte);
      hi    = _mm_mulhi_epu16(vs, vte);
      sign  = _mm_srai_epi16(vte, 15);
      vsa   = _mm_and_si128(vs, sign);
      hi    = _mm_sub_epi16(hi, vsa);
      omask = _mm_adds_epu16(ACCL, lo);
      ACCL  = _mm_add_epi16(ACCL, lo);
      omask = _mm_cmpeq_epi16(ACCL, omask);
      omask = _mm_cmpeq_epi16(omask, zero);
      hi    = _mm_sub_epi16(hi, omask);
      omask = _mm_adds_epu16(ACCM, hi);
      ACCM  = _mm_add_epi16(ACCM, hi);
      omask = _mm_cmpeq_epi16(ACCM, omask);
      omask = _mm_cmpeq_epi16(omask, zero);
      hi    = _mm_srai_epi16(hi, 15);
      ACCH  = _mm_add_epi16(ACCH, hi);
      ACCH  = _mm_sub_epi16(ACCH, omask);
      nhi   = _mm_srai_epi16(ACCH, 15);
      nmd   = _mm_srai_epi16(ACCM, 15);
      shi   = _mm_cmpeq_epi16(nhi, ACCH);
      smd   = _mm_cmpeq_epi16(nhi, nmd);
      cmask = _mm_and_si128(smd, shi);
      cval  = _mm_cmpeq_epi16(nhi, zero);
      vd    = _mm_blendv_epi8(cval, ACCL, cmask);
      break;
    case Op::VMADH:
      lo    = _mm_mullo_epi16(vs, vte);
      hi    = _mm_mulhi_epi16(vs, vte);
      omask = _mm_adds_epu16(ACCM, lo);
      ACCM  = _mm_add_epi16(ACCM, lo);
      omask = _mm_cmpeq_epi16(ACCM, omask);
      omask = _mm_cmpeq_epi16(omask, zero);
      hi    = _mm_sub_epi16(hi, omask);
      ACCH  = _mm_add_epi16(ACCH, hi);
      lo    = _mm_unpacklo_epi16(ACCM, ACCH);
      hi    = _mm_unpackhi_epi16(ACCM, ACCH);
      vd    = _mm_packs_epi32(lo, hi);
      break;
    }
    store(st.acch, ACCH); store(st.accm, ACCM); store(st.accl, ACCL);
    store(st.r[d], vd);
  }
}

//generated code, called with the VU address in x0
using Function = void (*)(VU*);

struct CodeBuffer {
  uint8_t* base = nullptr;
  size_t capacity = 0, used = 0;

  explicit CodeBuffer(size_t size) : capacity(size) {
    int flags = MAP_PRIVATE | MAP_ANON;
    #if defined(__APPLE__)
    flags |= MAP_JIT;
    #endif
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
    if(p == MAP_FAILED) { perror("mmap"); exit(2); }
    base = (uint8_t*)p;
  }

  auto add(std::vector<uint32_t> words) -> Function {
    words.push_back(0xd65f03c0);  //ret
    size_t bytes = words.size() * 4;
    if(used + bytes > capacity) { fprintf(stderr, "code buffer full\n"); exit(2); }
    uint8_t* at = base + used;
    #if defined(__APPLE__)
    pthread_jit_write_protect_np(0);
    memcpy(at, words.data(), bytes);
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(at, bytes);
    #else
    memcpy(at, words.data(), bytes);
    __builtin___clear_cache((char*)at, (char*)at + bytes);
    #endif
    used += bytes;
    return (Function)at;
  }
};

//16-bit lane values, biased toward carry, sign and saturation boundaries
static auto randomLane(std::mt19937_64& rng) -> uint16_t {
  static const uint16_t edges[] = {0x0000, 0x0001, 0x0002, 0x7ffe, 0x7fff, 0x8000, 0x8001, 0xfffe, 0xffff, 0x00ff, 0xff00};
  uint64_t x = rng();
  if((x & 3) == 0) return edges[(x >> 8) % (sizeof(edges) / sizeof(edges[0]))];
  return (uint16_t)(x >> 16);
}

static auto randomState(std::mt19937_64& rng, VU& s) -> void {
  for(auto& reg : s.r) for(auto& lane : reg) lane = randomLane(rng);
  for(int n = 0; n < 8; n++) {
    s.acch[n] = randomLane(rng); s.accm[n] = randomLane(rng); s.accl[n] = randomLane(rng);
  }
}

static auto dump(const char* label, const VU& s, int reg) -> void {
  fprintf(stderr, "  %-6s vd:", label);
  for(int n = 0; n < 8; n++) fprintf(stderr, " %04x", s.r[reg][7 - n]);
  fprintf(stderr, "  acc:");
  for(int n = 0; n < 8; n++) fprintf(stderr, " %04x%04x%04x", s.acch[7 - n], s.accm[7 - n], s.accl[7 - n]);
  fprintf(stderr, "\n");
}

struct Step { Op op; int e, vd, vs, vt; };

int main(int argc, char** argv) {
  int states = argc > 1 ? atoi(argv[1]) : 3000;
  uint64_t seed = argc > 2 ? strtoull(argv[2], nullptr, 0) : 0x5253505655ull;
  std::mt19937_64 rng(seed);

  //register choices: distinct, each aliasing case, and the ends of the register file
  const int regs[][3] = {{1, 2, 3}, {4, 4, 5}, {6, 7, 6}, {8, 9, 9}, {10, 10, 10}, {0, 31, 17}, {31, 0, 31}, {30, 29, 0}};
  const Op ops[] = {Op::VMUDL, Op::VMUDM, Op::VMUDN, Op::VMUDH, Op::VMADL, Op::VMADM, Op::VMADN, Op::VMADH};

  CodeBuffer code(64 << 20);
  uint64_t checks = 0, failures = 0, referenceMismatches = 0;

  //runs `f` and the references on random states; steps are applied in order
  auto check = [&](Function f, const std::vector<Step>& steps, int count, const char* mode) {
    for(int i = 0; i < count; i++) {
      VU initial;
      randomState(rng, initial);
      VU a = initial, b = initial, c = initial;
      for(auto& s : steps) {
        sisd::run(a, s.op, s.e, s.vd, s.vs, s.vt);
        simd::run(b, s.op, s.e, s.vd, s.vs, s.vt);
      }
      f(&c);
      checks++;
      auto& last = steps.back();
      if(memcmp(&a, &b, sizeof(VU)) != 0 && referenceMismatches++ < 5) {
        fprintf(stderr, "scalar and SSE references differ (%s): %s e=%d vd=%d vs=%d vt=%d\n", mode, opName(last.op), last.e, last.vd, last.vs, last.vt);
        dump("scalar", a, last.vd); dump("sse", b, last.vd);
      }
      if(memcmp(&b, &c, sizeof(VU)) != 0 && failures++ < 10) {
        fprintf(stderr, "NEON differs from SSE (%s, %zu steps, last %s e=%d vd=%d vs=%d vt=%d)\n", mode, steps.size(), opName(last.op), last.e, last.vd, last.vs, last.vt);
        dump("input", initial, last.vd); dump("sse", b, last.vd); dump("neon", c, last.vd);
      }
    }
  };

  //every instruction alone: stored as usual, kept in v19-v21 and then flushed, and with the
  //accumulator already loaded into v19-v21
  enum Mode { Plain, Keep, Preloaded };
  const char* modeNames[] = {"plain", "kept", "preloaded"};
  for(Op op : ops) {
    for(int e = 0; e < 16; e++) {
      for(auto& r : regs) {
        for(int mode : {Plain, Keep, Preloaded}) {
          std::vector<uint32_t> words;
          if(mode == Preloaded) {
            words.push_back(RspVuNeon::encode::ldrq(RspVuNeon::v::acch, 0, layout.acch));
            words.push_back(RspVuNeon::encode::ldrq(RspVuNeon::v::accm, 0, layout.accm));
            words.push_back(RspVuNeon::encode::ldrq(RspVuNeon::v::accl, 0, layout.accl));
          }
          RspVuNeon::emit(words, op, e, r[0], r[1], r[2], 0, layout, mode == Preloaded, mode == Keep);
          if(mode == Keep) RspVuNeon::flushAcc(words, 0, layout);
          check(code.add(words), {{op, e, r[0], r[1], r[2]}}, states, modeNames[mode]);
        }
      }
    }
  }
  uint64_t singleChecks = checks;

  //chains of 2-5 random instructions with the accumulator kept between them, ending either with
  //the last instruction storing it or with a flush
  for(int chain = 0; chain < 4000; chain++) {
    std::vector<Step> steps(2 + rng() % 4);
    for(auto& s : steps) s = {ops[rng() % 8], (int)(rng() % 16), (int)(rng() % 32), (int)(rng() % 32), (int)(rng() % 32)};
    bool endFlush = rng() & 1;
    std::vector<uint32_t> words;
    for(size_t i = 0; i < steps.size(); i++) {
      auto& s = steps[i];
      RspVuNeon::emit(words, s.op, s.e, s.vd, s.vs, s.vt, 0, layout, i > 0, i + 1 < steps.size() || endFlush);
    }
    if(endFlush) RspVuNeon::flushAcc(words, 0, layout);
    check(code.add(words), steps, states / 10 + 1, endFlush ? "chain, flushed" : "chain");
  }

  printf("%llu checks, seed 0x%llx: %llu single-instruction (8 instructions x 16 elements x %zu register choices x "
         "3 accumulator modes x %d states) and %llu for 4000 chains of 2-5 instructions: "
         "%llu NEON mismatches, %llu scalar/SSE reference mismatches\n",
         (unsigned long long)checks, (unsigned long long)seed, (unsigned long long)singleChecks,
         sizeof(regs) / sizeof(regs[0]), states, (unsigned long long)(checks - singleChecks),
         (unsigned long long)failures, (unsigned long long)referenceMismatches);
  return failures || referenceMismatches ? 1 : 0;
}
