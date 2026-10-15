//The VFPU's common instructions, compiled. Whatever returns false here is compiled as a call into the interpreter
//instead (recompiler.cpp).
//
//The interpreter's VFPU instructions (interpreter-vfpu.cpp) take every operand through the prefixes: each lane may be
//swizzled, made absolute, negated or replaced by a constant, and each result clamped or left unwritten. Games
//rarely set them, and while they're at rest (pfxs and pfxt in their own order, pfxd nothing) all that does nothing:
//a lane is read as it is, and written as it is. So for most instructions compiled code checks that they're at rest
//and calls a helper below made for that case, which reads and writes the lanes by the register numbers the block
//worked out when it was compiled, and does each lane's arithmetic exactly as the interpreter does (vfpuFloat(),
//vfpuBits(), vfpuNaN(), the same operations in the same order; a dot product through the adder, vfpuDot(), or the
//same in NEON on ARM64). The lane-by-lane instructions take any prefixes the block knows: vfpuEach() reads and writes
//each lane as the block worked it out from them. Any other instruction with the prefixes set is the interpreter's,
//as always, called by the block itself without the decoding (emitVFPUCall()). Either way they're at rest afterwards:
//the interpreter uses them up, the helpers found them so or put them so. The prefix instructions are compiled too,
//each storing its value; the block keeps track of what it knows of each prefix (prefixes), and checks as it runs only
//what it doesn't know (in practice once, at the first VFPU instruction of a block that sets no prefixes).
//
//The matrix instructions (vmmul, vtfm, vhtfm, vmscl, vmmov, vmidt, vmzero, vmone) don't read the prefixes at all;
//compiled code calls them directly (skipping the decoding: the interpreter's own, or on ARM64 the products of
//matrices in place in NEON) and leaves the prefixes at rest, as the interpreter's decoder does after them.

//A vector operand's four lanes' register numbers (vfpuLine()), a byte each, as the helpers take them.
static auto packLanes(const std::array<u8, 4>& lanes) -> u32 {
  return lanes[0] | lanes[1] << 8 | lanes[2] << 16 | lanes[3] << 24;
}
static auto laneOf(u32 packed, u32 i) -> u32 { return packed >> 8 * i & 0xff; }

//vadd, vsub, vdiv (op 0, 1, 7) and vmul (op 8): vfpuBinary() with the prefixes at rest. Every input lane is read
//before any result is written, as there (vd may be vs or vt).
template<u32 Size, u32 Op> auto Allegrex::vfpuRestBinary(u32 vd, u32 vs, u32 vt) -> void {
  u32 s[Size], t[Size];
  for(u32 i : range(Size)) s[i] = vfpu.r[laneOf(vs, i)], t[i] = vfpu.r[laneOf(vt, i)];
  for(u32 i : range(Size)) {
    f32 a = vfpuFloat(s[i]), b = vfpuFloat(t[i]);
    u32 result;
    if constexpr(Op == 0) result = vfpuNaN(vfpuBits(a + b), NaNSign::Positive, s[i], t[i]);
    if constexpr(Op == 1) result = vfpuNaN(vfpuBits(a - b), NaNSign::Positive, s[i], t[i]);
    if constexpr(Op == 7) result = vfpuNaN(vfpuBits((f64)a / b), NaNSign::Product, s[i], t[i]);
    if constexpr(Op == 8) result = vfpuNaN(vfpuBits((f64)a * b), NaNSign::Product, s[i], t[i]);
    vfpu.r[laneOf(vd, i)] = result;
  }
}

#if defined(ALLEGREX_NEON)
//The adder (vfpuDot()) on four lanes at once, in NEON's integer instructions: lane i of the result is vfpuDot() of
//the terms' lane i, a[k] times b[k] for each k below Terms (the terms past those are zeros, which add nothing there
//either). Each step is the scalar adder's on four lanes: the significands' 48-bit products cut to 27 bits and
//rounded to odd; zeros and denormals left out; each aligned to the largest exponent and truncated; added up with
//their signs; then rounded as vfpuDotRound() rounds. special gets the lanes with an infinity or a NaN among their
//inputs, which only the scalar adder works out.
template<u32 Terms> static inline auto vfpuDots(const uint32x4_t a[], const uint32x4_t b[], uint32x4_t& special)
  -> uint32x4_t {
  const uint32x4_t byte = vdupq_n_u32(0xff), fraction = vdupq_n_u32(0x7f'ffff), hidden = vdupq_n_u32(0x80'0000);
  const uint64x2_t below = vdupq_n_u64((1u << 21) - 1);
  const int32x4_t lowest = vdupq_n_s32(-2 * 127);
  uint32x4_t product[Terms], negative[Terms];
  int32x4_t exponent[Terms], highest = lowest;
  special = vdupq_n_u32(0);
  for(u32 k = 0; k < Terms; k++) {
    uint32x4_t ea = vandq_u32(vshrq_n_u32(a[k], 23), byte), eb = vandq_u32(vshrq_n_u32(b[k], 23), byte);
    special = vorrq_u32(special, vorrq_u32(vceqq_u32(ea, byte), vceqq_u32(eb, byte)));
    uint32x4_t ma = vorrq_u32(vandq_u32(a[k], fraction), hidden), mb = vorrq_u32(vandq_u32(b[k], fraction), hidden);
    uint64x2_t low = vmull_u32(vget_low_u32(ma), vget_low_u32(mb)), high = vmull_high_u32(ma, mb);
    uint32x4_t p = vcombine_u32(vshrn_n_u64(low, 21), vshrn_n_u64(high, 21));
    uint32x4_t inexact = vcombine_u32(vmovn_u64(vtstq_u64(low, below)), vmovn_u64(vtstq_u64(high, below)));
    p = vorrq_u32(p, vshrq_n_u32(inexact, 31));  //round to odd
    uint32x4_t zero = vorrq_u32(vceqzq_u32(ea), vceqzq_u32(eb));
    product[k] = vbicq_u32(p, zero);
    int32x4_t e = vsubq_s32(vreinterpretq_s32_u32(vaddq_u32(ea, eb)), vdupq_n_s32(2 * 127));
    exponent[k] = vbslq_s32(zero, lowest, e);
    negative[k] = vreinterpretq_u32_s32(vshrq_n_s32(vreinterpretq_s32_u32(veorq_u32(a[k], b[k])), 31));
    highest = vmaxq_s32(highest, exponent[k]);
  }
  int32x4_t sum = vdupq_n_s32(0);
  for(u32 k = 0; k < Terms; k++) {
    int32x4_t shift = vminq_s32(vsubq_s32(highest, exponent[k]), vdupq_n_s32(28));
    int32x4_t term = vreinterpretq_s32_u32(vshlq_u32(product[k], vnegq_s32(shift)));
    int32x4_t n = vreinterpretq_s32_u32(negative[k]);
    sum = vaddq_s32(sum, vsubq_s32(veorq_s32(term, n), n));
  }
  //vfpuDotRound(): the magnitude's top 24 bits (adjust: where its leading one is past bit 23), rounded to nearest
  //even where bits are shifted out, a carry out of that rounding taking the exponent up
  const uint32x4_t one = vdupq_n_u32(1);
  uint32x4_t sign = vandq_u32(vreinterpretq_u32_s32(sum), vdupq_n_u32(0x8000'0000));
  uint32x4_t magnitude = vshrq_n_u32(vreinterpretq_u32_s32(vabsq_s32(sum)), 2);
  int32x4_t adjust = vsubq_s32(vdupq_n_s32(8), vreinterpretq_s32_u32(vclzq_u32(magnitude)));
  uint32x4_t mantissa = vshlq_u32(magnitude, vnegq_s32(adjust));  //(shifted left where adjust is negative)
  uint32x4_t dropped = vandq_u32(magnitude, vsubq_u32(vshlq_u32(one, adjust), one));
  uint32x4_t half = vshlq_u32(one, vsubq_s32(adjust, vreinterpretq_s32_u32(one)));
  uint32x4_t up = vcgtq_u32(vaddq_u32(dropped, vandq_u32(mantissa, one)), half);
  mantissa = vsubq_u32(mantissa, vandq_u32(up, vcgtzq_s32(adjust)));
  uint32x4_t carry = vshrq_n_u32(mantissa, 24);  //(rounded up to exactly 2^24, whose fraction is 0 like 2^23's)
  int32x4_t e = vaddq_s32(vaddq_s32(highest, adjust), vreinterpretq_s32_u32(carry));
  //(a sum of 0 flushes whatever its exponent, so the flush comes after the overflow)
  uint32x4_t flush = vorrq_u32(vcleq_s32(e, vdupq_n_s32(-127)), vceqzq_u32(magnitude));
  uint32x4_t infinite = vcgeq_s32(e, vdupq_n_s32(128));
  uint32x4_t bits = vshlq_n_u32(vreinterpretq_u32_s32(vaddq_s32(e, vdupq_n_s32(127))), 23);
  bits = vbslq_u32(infinite, vdupq_n_u32(0x7f80'0000), vorrq_u32(bits, vandq_u32(mantissa, fraction)));
  return vorrq_u32(vbicq_u32(bits, flush), sign);
}

//vfpuDots(), its lanes below Lanes that have an infinity or a NaN among their inputs worked out by the scalar adder.
template<u32 Terms, u32 Lanes> static inline auto vfpuDotsExact(const uint32x4_t a[], const uint32x4_t b[])
  -> uint32x4_t {
  uint32x4_t special;
  uint32x4_t result = vfpuDots<Terms>(a, b, special);
  if constexpr(Lanes < 4) {
    static constexpr u32 within[4] = {~0u, Lanes > 1 ? ~0u : 0, Lanes > 2 ? ~0u : 0, 0};
    special = vandq_u32(special, vld1q_u32(within));
  }
  if(vmaxvq_u32(special)) [[unlikely]] {
    u32 x[4][4], y[4][4], out[4];
    for(u32 k = 0; k < Terms; k++) vst1q_u32(x[k], a[k]), vst1q_u32(y[k], b[k]);
    vst1q_u32(out, result);
    for(u32 i = 0; i < Lanes; i++) {
      u32 s[4] = {}, t[4] = {};
      for(u32 k = 0; k < Terms; k++) s[k] = x[k][i], t[k] = y[k][i];
      out[i] = vfpuDot(s, t);
    }
    result = vld1q_u32(out);
  }
  return result;
}

//vfpuDot() of the four terms in a's and b's lanes (zeros past the operands' size): vfpuDots()'s steps across the
//lanes, then vfpuDotRound(); an infinity or a NaN goes to the scalar adder.
static inline auto vfpuDotAcross(uint32x4_t a, uint32x4_t b) -> u32 {
  const uint32x4_t byte = vdupq_n_u32(0xff), fraction = vdupq_n_u32(0x7f'ffff), hidden = vdupq_n_u32(0x80'0000);
  const uint64x2_t below = vdupq_n_u64((1u << 21) - 1);
  uint32x4_t ea = vandq_u32(vshrq_n_u32(a, 23), byte), eb = vandq_u32(vshrq_n_u32(b, 23), byte);
  if(vmaxvq_u32(vorrq_u32(vceqq_u32(ea, byte), vceqq_u32(eb, byte)))) [[unlikely]] {
    u32 s[4], t[4];
    vst1q_u32(s, a), vst1q_u32(t, b);
    return vfpuDot(s, t);
  }
  uint32x4_t ma = vorrq_u32(vandq_u32(a, fraction), hidden), mb = vorrq_u32(vandq_u32(b, fraction), hidden);
  uint64x2_t low = vmull_u32(vget_low_u32(ma), vget_low_u32(mb)), high = vmull_high_u32(ma, mb);
  uint32x4_t p = vcombine_u32(vshrn_n_u64(low, 21), vshrn_n_u64(high, 21));
  uint32x4_t inexact = vcombine_u32(vmovn_u64(vtstq_u64(low, below)), vmovn_u64(vtstq_u64(high, below)));
  p = vorrq_u32(p, vshrq_n_u32(inexact, 31));
  uint32x4_t zero = vorrq_u32(vceqzq_u32(ea), vceqzq_u32(eb));
  uint32x4_t product = vbicq_u32(p, zero);
  int32x4_t e = vsubq_s32(vreinterpretq_s32_u32(vaddq_u32(ea, eb)), vdupq_n_s32(2 * 127));
  int32x4_t exponent = vbslq_s32(zero, vdupq_n_s32(-2 * 127), e);
  int32x4_t negative = vshrq_n_s32(vreinterpretq_s32_u32(veorq_u32(a, b)), 31);
  s32 highest = vmaxvq_s32(exponent);
  int32x4_t shift = vminq_s32(vsubq_s32(vdupq_n_s32(highest), exponent), vdupq_n_s32(28));
  int32x4_t term = vreinterpretq_s32_u32(vshlq_u32(product, vnegq_s32(shift)));
  return vfpuDotRound(vaddvq_s32(vsubq_s32(veorq_s32(term, negative), negative)), highest);
}

//An operand's first Size lanes, from their registers (a byte each: laneOf()), and zeros past them.
template<u32 Size> static inline auto vfpuLanes(const u32* r, u32 packed) -> uint32x4_t {
  uint32x4_t v = vdupq_n_u32(0);
  v = vld1q_lane_u32(r + laneOf(packed, 0), v, 0);
  if constexpr(Size > 1) v = vld1q_lane_u32(r + laneOf(packed, 1), v, 1);
  if constexpr(Size > 2) v = vld1q_lane_u32(r + laneOf(packed, 2), v, 2);
  if constexpr(Size > 3) v = vld1q_lane_u32(r + laneOf(packed, 3), v, 3);
  return v;
}
#endif

//vdot: VDOT() with the prefixes at rest, where the lanes past the size are the ones left out of the sum.
template<u32 Size> auto Allegrex::vfpuRestDot(u32 vd, u32 vs, u32 vt) -> void {
  #if defined(ALLEGREX_NEON)
  vfpu.r[vd] = vfpuDotAcross(vfpuLanes<Size>(vfpu.r, vs), vfpuLanes<Size>(vfpu.r, vt));
  #else
  u32 a[4] = {}, b[4] = {};
  for(u32 i : range(Size)) a[i] = vfpu.r[laneOf(vs, i)], b[i] = vfpu.r[laneOf(vt, i)];
  vfpu.r[vd] = vfpuNaN(vfpuDot(a, b), NaNSign::Positive, 0);
  #endif
}

//vscl: VSCL() with the prefixes at rest; vt is the scale's register.
template<u32 Size> auto Allegrex::vfpuRestScale(u32 vd, u32 vs, u32 vt) -> void {
  u32 s[Size], scale = vfpu.r[vt];
  for(u32 i : range(Size)) s[i] = vfpu.r[laneOf(vs, i)];
  for(u32 i : range(Size)) {
    u32 product = vfpuBits((f64)vfpuFloat(s[i]) * vfpuFloat(scale));
    vfpu.r[laneOf(vd, i)] = vfpuNaN(product, NaNSign::Product, s[i], scale);
  }
}

//vcmp: VCMP() with the prefixes at rest, the condition in the first argument.
template<u32 Size> auto Allegrex::vfpuRestCompare(u32 condition, u32 vs, u32 vt) -> void {
  u32 bits = 0, any = 0, every = 1;
  for(u32 i : range(Size)) {
    u32 result = compare(condition, vfpuFloat(vfpu.r[laneOf(vs, i)]), vfpuFloat(vfpu.r[laneOf(vt, i)]));
    bits |= result << i;
    any |= result;
    every &= result;
  }
  u32 affected = ((1 << Size) - 1) | 0x30;
  vfpu.cc = (vfpu.cc & ~affected) | ((bits | any << 4 | every << 5) & affected);
}

//vmov, vabs, vneg (op 0, 1, 2): the lanes' bits, as they are, without the sign, or with it turned round.
template<u32 Size, u32 Op> auto Allegrex::vfpuRestBits(u32 vd, u32 vs, u32) -> void {
  u32 s[Size];
  for(u32 i : range(Size)) s[i] = vfpu.r[laneOf(vs, i)];
  for(u32 i : range(Size)) {
    if constexpr(Op == 1) s[i] &= 0x7fff'ffff;
    if constexpr(Op == 2) s[i] ^= 0x8000'0000;
    vfpu.r[laneOf(vd, i)] = s[i];
  }
}

//vi2f: VI2F() with the prefixes at rest, the scale in the last argument.
template<u32 Size> auto Allegrex::vfpuRestToFloat(u32 vd, u32 vs, u32 scale) -> void {
  u32 s[Size];
  for(u32 i : range(Size)) s[i] = vfpu.r[laneOf(vs, i)];
  for(u32 i : range(Size)) vfpu.r[laneOf(vd, i)] = vfpuBits(std::ldexp((f64)s32(s[i]), -(s32)scale));
}

//vmin and vmax (Max): VMIN() and VMAX() with the prefixes at rest, every input lane read before any result is
//written.
template<u32 Size, bool Max> auto Allegrex::vfpuRestMinMax(u32 vd, u32 vs, u32 vt) -> void {
  u32 s[Size], t[Size];
  for(u32 i : range(Size)) s[i] = vfpu.r[laneOf(vs, i)], t[i] = vfpu.r[laneOf(vt, i)];
  for(u32 i : range(Size)) {
    if constexpr(Max) vfpu.r[laneOf(vd, i)] = vfpuOrder(s[i]) > vfpuOrder(t[i]) ? s[i] : t[i];
    else vfpu.r[laneOf(vd, i)] = vfpuOrder(s[i]) < vfpuOrder(t[i]) ? s[i] : t[i];
  }
}

//vsge and vslt (Less): VSGE() and VSLT(), vfpuBinary() with the prefixes at rest.
template<u32 Size, bool Less> auto Allegrex::vfpuRestSet(u32 vd, u32 vs, u32 vt) -> void {
  u32 s[Size], t[Size];
  for(u32 i : range(Size)) s[i] = vfpu.r[laneOf(vs, i)], t[i] = vfpu.r[laneOf(vt, i)];
  for(u32 i : range(Size)) {
    f32 a = vfpuFloat(s[i]), b = vfpuFloat(t[i]);
    f32 set = Less ? (a < b ? 1.0f : 0.0f) : (a >= b ? 1.0f : 0.0f);
    vfpu.r[laneOf(vd, i)] = vfpuNaN(vfpuBits(set), NaNSign::Unknown, s[i], t[i]);
  }
}

//vhdp: VHDP() with the prefixes at rest, where rs's last lane is the constant 1 and the lanes past the size are the
//ones left out of the sum; vd is the result's register.
template<u32 Size> auto Allegrex::vfpuRestHalfDot(u32 vd, u32 vs, u32 vt) -> void {
  #if defined(ALLEGREX_NEON)
  auto a = vsetq_lane_u32(One, vfpuLanes<Size>(vfpu.r, vs), Size - 1);
  vfpu.r[vd] = vfpuDotAcross(a, vfpuLanes<Size>(vfpu.r, vt));
  #else
  u32 a[4] = {}, b[4] = {};
  for(u32 i : range(Size)) a[i] = vfpu.r[laneOf(vs, i)], b[i] = vfpu.r[laneOf(vt, i)];
  a[Size - 1] = One;
  vfpu.r[vd] = vfpuNaN(vfpuDot(a, b), NaNSign::Positive, 0);
  #endif
}

//The conversions that move lanes' bits about (Op, their rt field: vf2h 0x32, vh2f 0x33, vuc2i 0x38, vc2i 0x39, vus2i
//0x3a, vs2i 0x3b, vi2uc 0x3c, vi2c 0x3d, vi2us 0x3e, vi2s 0x3f): VF2H() and the rest with the prefixes at rest, at the
//sizes their decoder takes (the instruction's in size); s's lanes as many as they read, d's as many as they write
//(packLanes()). Every input lane is read before any result is written.
template<u32 Op> auto Allegrex::vfpuRestPack(u32 d, u32 s, u32 size) -> void {
  u32 in[4] = {}, out[4] = {}, lanes = 0;
  for(u32 i : range(Op == 0x38 || Op == 0x39 ? 1 : size)) in[i] = vfpu.r[laneOf(s, i)];
  auto half = [&](u32 i) { return in[i / 2] >> 16 * (i % 2) & 0xffff; };
  if constexpr(Op == 0x32) {
    lanes = size / 2;
    for(u32 i : range(lanes)) out[i] = floatToHalf(in[2 * i]) | floatToHalf(in[2 * i + 1]) << 16;
  }
  if constexpr(Op == 0x33) {
    lanes = 2 * size;
    for(u32 i : range(lanes)) out[i] = halfToFloat(half(i));
  }
  if constexpr(Op == 0x38) {
    lanes = 4;
    for(u32 i : range(4)) {
      u32 byte = in[0] >> (8 * i) & 0xff;
      out[i] = byte << 23 | byte << 15 | byte << 7 | byte >> 1;
    }
  }
  if constexpr(Op == 0x39) {
    lanes = 4;
    for(u32 i : range(4)) out[i] = (in[0] >> (8 * i) & 0xff) << 24;
  }
  if constexpr(Op == 0x3a) {
    lanes = 2 * size;
    for(u32 i : range(lanes)) out[i] = (half(i) << 15) & 0x7fff'ffff;
  }
  if constexpr(Op == 0x3b) {
    lanes = 2 * size;
    for(u32 i : range(lanes)) out[i] = half(i) << 16;
  }
  if constexpr(Op == 0x3c) {
    lanes = 1;
    for(u32 i : range(4)) out[0] |= (in[i] & 0x8000'0000 ? 0 : in[i] >> 23 & 0xff) << (8 * i);
  }
  if constexpr(Op == 0x3d) {
    lanes = 1;
    for(u32 i : range(4)) out[0] |= (in[i] >> 24) << (8 * i);
  }
  if constexpr(Op == 0x3e) {
    lanes = size / 2;
    auto top = [](u32 x) -> u32 { return x & 0x8000'0000 ? 0 : x >> 15 & 0xffff; };
    for(u32 i : range(lanes)) out[i] = top(in[2 * i]) | top(in[2 * i + 1]) << 16;
  }
  if constexpr(Op == 0x3f) {
    lanes = size / 2;
    for(u32 i : range(lanes)) out[i] = (in[2 * i] >> 16) | (in[2 * i + 1] >> 16) << 16;
  }
  for(u32 i : range(lanes)) vfpu.r[laneOf(d, i)] = out[i];
}

//vcmovt and vcmovf: VCMOV() with the prefixes at rest, which, in the condition codes' bit 0-5 or each lane's own (6),
//and whether on false (bit 3) in the last argument.
template<u32 Size> auto Allegrex::vfpuRestConditionalMove(u32 d, u32 s, u32 which) -> void {
  u32 bit = which & 7;
  bool onFalse = which >> 3 & 1;
  u32 in[Size];
  for(u32 i : range(Size)) in[i] = vfpu.r[laneOf(s, i)];
  for(u32 i : range(Size)) {
    bool condition = vfpu.cc >> (bit == 6 ? i : bit) & 1;
    if(condition != onFalse) vfpu.r[laneOf(d, i)] = in[i];
  }
}

//vcrs.t, vcrsp.t and vqmul.q (Op 0, 1, 2): VCRS(), VCRSP() and VQMUL() with the prefixes at rest. Every input lane is
//read before any result is written.
template<u32 Op> auto Allegrex::vfpuRestCross(u32 d, u32 s, u32 t) -> void {
  constexpr u32 Size = Op == 2 ? 4 : 3;
  u32 a[Size], b[Size], out[Size];
  for(u32 i : range(Size)) a[i] = vfpu.r[laneOf(s, i)], b[i] = vfpu.r[laneOf(t, i)];
  auto dot = [&](u32 a0, u32 a1, u32 a2, u32 a3, u32 b0, u32 b1, u32 b2, u32 b3) -> u32 {
    u32 x[4] = {a0, a1, a2, a3}, y[4] = {b0, b1, b2, b3};
    return vfpuNaN(vfpuDot(x, y), NaNSign::Positive, 0);
  };
  if constexpr(Op == 0) {
    auto product = [&](u32 i, u32 j) {
      return vfpuNaN(vfpuBits((f64)vfpuFloat(a[i]) * vfpuFloat(b[j])), NaNSign::Product, a[i], b[j]);
    };
    out[0] = product(1, 2), out[1] = product(2, 0), out[2] = product(0, 1);
  }
  if constexpr(Op == 1) {
    auto cross = [&](u32 i, u32 j) { return dot(a[i], a[j], 0, 0, b[j], b[i] ^ 0x8000'0000, 0, 0); };
    out[0] = cross(1, 2), out[1] = cross(2, 0), out[2] = cross(0, 1);
  }
  if constexpr(Op == 2) {
    auto neg = [](u32 x) -> u32 { return x ^ 0x8000'0000; };
    out[0] = dot(a[3], a[2], a[1], a[0], b[0], neg(b[1]), b[2], b[3]);
    out[1] = dot(a[3], a[2], a[1], a[0], b[1], b[0], b[3], neg(b[2]));
    out[2] = dot(a[3], a[2], a[1], a[0], b[2], b[3], neg(b[0]), b[1]);
    out[3] = dot(a[3], a[2], a[1], a[0], b[3], neg(b[2]), neg(b[1]), neg(b[0]));
  }
  for(u32 i : range(Size)) vfpu.r[laneOf(d, i)] = out[i];
}

//vrot: VROT() with the prefixes at rest, rs's single in s, the placement field in the last argument's low five bits.
template<u32 Size> auto Allegrex::vfpuRestRotate(u32 d, u32 s, u32 placement) -> void {
  u32 angle = vfpu.r[s];
  u32 sine = vfpuSine(angle), cosine = vfpuCosine(angle);
  if(placement & 0x10) sine ^= 0x8000'0000;
  u32 cosineLane = placement & 3, sineLane = placement >> 2 & 3;
  for(u32 i : range(Size)) {
    vfpu.r[laneOf(d, i)] = i == cosineLane ? cosine : cosineLane == sineLane || i == sineLane ? sine : 0;
  }
}

//One lane of the instructions that work lane by lane, as the interpreter works it out (vfpuBinary(), VMIN(), VMAX(),
//vfpuUnaryBits(), VI2F(), VF2I(), the math functions): Op 0 vadd, 1 vsub, 7 vdiv, 8 vmul (and vscl), 10 vmin,
//11 vmax, 12 vsge, 13 vslt; 20 vmov, 21 vabs, 22 vneg; 30 vi2f (t the scale), 31 vf2i (t the scale, the rounding
//mode above it); from 40, the math functions by their rt field (40 + rt - 0x10).
template<u32 Op> auto Allegrex::vfpuLane(u32 s, u32 t) const -> u32 {
  if constexpr(Op == 20) return s;
  else if constexpr(Op == 21) return s & 0x7fff'ffff;
  else if constexpr(Op == 22) return s ^ 0x8000'0000;
  else if constexpr(Op == 10) return vfpuOrder(s) < vfpuOrder(t) ? s : t;
  else if constexpr(Op == 11) return vfpuOrder(s) > vfpuOrder(t) ? s : t;
  else if constexpr(Op == 30) return vfpuBits(std::ldexp((f64)s32(s), -(s32)t));
  else if constexpr(Op == 31) return toInteger(vfpuFloat(s), t & 0xff, t >> 8);
  else if constexpr(Op == 40) return vfpuReciprocal(s);
  else if constexpr(Op == 41) return vfpuReciprocalSqrt(s);
  else if constexpr(Op == 42) return vfpuSine(s);
  else if constexpr(Op == 43) return vfpuCosine(s);
  else if constexpr(Op == 44) return vfpuExp2(s);
  else if constexpr(Op == 45) return vfpuLog2(s);
  else if constexpr(Op == 46) return vfpuSqrt(s);
  else if constexpr(Op == 47) return vfpuArcsine(s);
  else if constexpr(Op == 48) return vfpuReciprocal(s) ^ 0x8000'0000;
  else if constexpr(Op == 50) return vfpuSine(s) ^ 0x8000'0000;
  else if constexpr(Op == 52) return vfpuReciprocalExp2(s);
  else {
    f32 a = vfpuFloat(s), b = vfpuFloat(t);
    if constexpr(Op == 0) return vfpuNaN(vfpuBits(a + b), NaNSign::Positive, s, t);
    if constexpr(Op == 1) return vfpuNaN(vfpuBits(a - b), NaNSign::Positive, s, t);
    if constexpr(Op == 7) return vfpuNaN(vfpuBits((f64)a / b), NaNSign::Product, s, t);
    if constexpr(Op == 8) return vfpuNaN(vfpuBits((f64)a * b), NaNSign::Product, s, t);
    if constexpr(Op == 12) return vfpuNaN(vfpuBits(a >= b ? 1.0f : 0.0f), NaNSign::Unknown, s, t);
    if constexpr(Op == 13) return vfpuNaN(vfpuBits(a < b ? 1.0f : 0.0f), NaNSign::Unknown, s, t);
  }
}

//An operand's lane as compiled code describes it to vfpuEach(), its prefix applied as vfpuReadFour() applies one:
//bits 0-6 its register, 7 taken without its sign, 8 negated, 9 a constant instead, numbered in bits 10-12.
static auto describedLane(const u32* r, u32 lane) -> u32 {
  u32 value = lane >> 9 & 1 ? PrefixConstants[lane >> 10 & 7] : r[lane & 0x7f] & ~((lane >> 7 & 1) << 31);
  return value ^ (lane >> 8 & 1) << 31;
}

//A result's lane written as compiled code describes it to vfpuEach(), through the destination's prefix as
//vfpuWrite() writes one: bits 0-6 its register, 7 masked (left as it was), 8 clamped (to 0..1; with 9 to -1..1).
static auto describedWrite(u32* r, u32 lane, u32 value) -> void {
  if(lane >> 7 & 1) return;
  if(lane >> 8 & 1) {
    f32 x = std::bit_cast<f32>(value);
    f32 low = lane >> 9 & 1 ? -1.0f : 0.0f;
    if(x > 1.0f) value = One;
    else if(x <= low) value = std::bit_cast<u32>(low);
  }
  r[lane & 0x7f] = value;
}

//An instruction that works lane by lane (Op: vfpuLane()), each lane's reading and writing worked out as the block
//was compiled, prefixes and all (emitEach()): s's lanes, and t's for the instructions with a second operand (Op below
//20), a lane's description in each 16 bits (describedLane()); the others take t as it is. d says how each result is
//written (describedWrite()), and its bit 10 a lane whose prefix took a lane past the size, whose result is 0. Every
//lane is read before any is written; then the prefixes are at rest, as the interpreter's decoder leaves them.
template<u32 Size, u32 Op> auto Allegrex::vfpuEach(u64 s, u64 t, u64 d) -> void {
  u32 result[Size];
  for(u32 i : range(Size)) {
    u32 a = describedLane(vfpu.r, s >> 16 * i & 0xffff), b = u32(t);
    if constexpr(Op < 20) b = describedLane(vfpu.r, t >> 16 * i & 0xffff);
    result[i] = d >> (16 * i + 10) & 1 ? 0 : vfpuLane<Op>(a, b);
  }
  for(u32 i : range(Size)) describedWrite(vfpu.r, d >> 16 * i & 0xffff, result[i]);
  vfpu.pfxs = PrefixIdentity;
  vfpu.pfxt = PrefixIdentity;
  vfpu.pfxd = 0;
}

//The matrix instructions, which leave the prefixes alone as they run: the interpreter's own, then the prefixes at
//rest, as its decoder leaves them (vfpuPrefixesUsed(), after an instruction that raised no exception, as these
//can't once decoded: emitVFPU() calls them only for the sizes they have). which: 0 vmmul, 1 vtfm, 2 vhtfm, 3 vmscl,
//4 vmmov, 5 vmidt, 6 vmzero, 7 vmone.
auto Allegrex::vfpuMatrix(u32 which, u32 registers, u32 size) -> void {
  u8 vd = registers & 0x7f, vs = registers >> 8 & 0x7f, vt = registers >> 16 & 0x7f;
  if(which == 0) VMMUL(vd, vs, vt, size);
  if(which == 1) VTFM(vd, vs, vt, size);
  if(which == 2) VHTFM(vd, vs, vt, size);
  if(which == 3) VMSCL(vd, vs, vt, size);
  if(which == 4) VMMOV(vd, vs, size);
  if(which == 5) VMIDT(vd, size);
  if(which == 6) VMZERO(vd, size);
  if(which == 7) VMONE(vd, size);
  vfpu.pfxs = PrefixIdentity;
  vfpu.pfxt = PrefixIdentity;
  vfpu.pfxd = 0;
}

#if defined(ALLEGREX_NEON)
//The matrix products of matrices in place: each operand starting at its matrix's first row and column, so that its
//elements' rows are the register file's (row r of the matrix at register matrix: r[matrix + 32r] on, four
//registers), or its columns when it's transposed. These leave the prefixes alone (emitVFPU() puts them at rest).

//The four rows of the register file's matrix at register matrix, and those turned on their side (lane c of row r
//to lane r of row c).
static inline auto vfpuRows(const u32* r, u32 matrix, uint32x4_t q[4]) -> void {
  for(u32 j = 0; j < 4; j++) q[j] = vld1q_u32(r + matrix + 32 * j);
}
static inline auto vfpuTurn(uint32x4_t q[4]) -> void {
  uint64x2_t t0 = vreinterpretq_u64_u32(vtrn1q_u32(q[0], q[1])), t1 = vreinterpretq_u64_u32(vtrn2q_u32(q[0], q[1]));
  uint64x2_t t2 = vreinterpretq_u64_u32(vtrn1q_u32(q[2], q[3])), t3 = vreinterpretq_u64_u32(vtrn2q_u32(q[2], q[3]));
  q[0] = vreinterpretq_u32_u64(vtrn1q_u64(t0, t2)), q[1] = vreinterpretq_u32_u64(vtrn1q_u64(t1, t3));
  q[2] = vreinterpretq_u32_u64(vtrn2q_u64(t0, t2)), q[3] = vreinterpretq_u32_u64(vtrn2q_u64(t1, t3));
}

//v's first Size lanes into registers along a line from r on: every stride-th register (1 along a row, 32 down a
//column).
template<u32 Size> static inline auto vfpuStore(u32* r, u32 stride, uint32x4_t v) -> void {
  if(stride == 1) {
    if constexpr(Size == 4) return vst1q_u32(r, v);
    vst1_u32(r, vget_low_u32(v));
    if constexpr(Size == 3) vst1q_lane_u32(r + 2, v, 2);
    return;
  }
  vst1q_lane_u32(r, v, 0);
  vst1q_lane_u32(r + stride, v, 1);
  if constexpr(Size > 2) vst1q_lane_u32(r + 2 * stride, v, 2);
  if constexpr(Size > 3) vst1q_lane_u32(r + 3 * stride, v, 3);
}

//vtfm, and vhtfm (Homogeneous): VTFM() and VHTFM() for a Size x Size matrix in place (Transposed: its columns in the
//register file's rows), each result lane one lane of vfpuDots(). rt's lanes and rd's are along lines: t and d are
//each line's first register, and its stride above that (1 or 32). Every register is read before any is written.
template<u32 Size, bool Transposed, bool Homogeneous> auto Allegrex::vfpuTransform(u32 matrix, u32 t, u32 d) -> void {
  //term k: the matrix's row k (its element in column i in lane i) times rt's lane k (in every lane)
  uint32x4_t a[4], b[4];
  vfpuRows(vfpu.r, matrix, a);
  if constexpr(Transposed) vfpuTurn(a);
  for(u32 k = 0; k < (Homogeneous ? Size - 1 : Size); k++) b[k] = vld1q_dup_u32(vfpu.r + (t & 0xff) + (t >> 8) * k);
  if constexpr(Homogeneous) b[Size - 1] = vdupq_n_u32(One);
  vfpuStore<Size>(vfpu.r + (d & 0xff), d >> 8, vfpuDotsExact<Size, Size>(a, b));
}

//vmmul: VMMUL() for three Size x Size matrices in place at registers s, t and d (Transposed: bit 0 for rs, 1 for rt,
//2 for rd), a row of rd at a time, each from vfpuDots(). Every register is read before any is written.
template<u32 Size, u32 Transposed> auto Allegrex::vfpuMultiply(u32 s, u32 t, u32 d) -> void {
  //term k of rd's row r: rs's element (k, r) in every lane, times rt's row k
  uint32x4_t b[4], rows[4] = {};
  vfpuRows(vfpu.r, t, b);
  if constexpr(Transposed & 2) vfpuTurn(b);
  for(u32 row = 0; row < Size; row++) {
    uint32x4_t a[4];
    for(u32 k = 0; k < Size; k++) a[k] = vld1q_dup_u32(vfpu.r + s + (Transposed & 1 ? k + 32 * row : row + 32 * k));
    rows[row] = vfpuDotsExact<Size, Size>(a, b);
  }
  if constexpr(Transposed & 4) vfpuTurn(rows);
  for(u32 j = 0; j < Size; j++) vfpuStore<Size>(vfpu.r + d + 32 * j, 1, rows[j]);
}
#endif

//execute() for a VFPU instruction of a group the block found as it was compiled (Op, its top six bits): the same,
//but for decoderEXECUTE()'s choice of the group. Not for vmfvc and vmtvc (0x34), which decoderEXECUTE() runs itself.
template<u32 Op> auto Allegrex::executeVFPU(u32 address, u32 instruction) -> u32 {
  pipeline.address = address;
  pipeline.instruction = instruction;
  pipeline.exception = 0;
  if constexpr(Op == 0x18) decoderVFPU0(), vfpuPrefixesUsed();
  if constexpr(Op == 0x19) decoderVFPU1(), vfpuPrefixesUsed();
  if constexpr(Op == 0x1b) decoderVFPU3(), vfpuPrefixesUsed();
  if constexpr(Op == 0x34) decoderVFPU4(), vfpuPrefixesUsed();
  if constexpr(Op == 0x37) decoderVFPU5();
  if constexpr(Op == 0x3c) decoderVFPU6(), vfpuPrefixesUsed();
  ipu.r[0] = 0;
  return pipeline.exception;
}

#define RTn (instruction >> 16 & 31)
#define Rt  gpr(RTn)

auto Allegrex::Recompiler::Prefixes::atRest() const -> bool {
  return known[0] && known[1] && known[2] && value[0] == PrefixIdentity && value[1] == PrefixIdentity && !value[2];
}

//(one of them known to be set)
auto Allegrex::Recompiler::Prefixes::busy() const -> bool {
  return (known[0] && value[0] != PrefixIdentity) || (known[1] && value[1] != PrefixIdentity) || (known[2] && value[2]);
}

//Whether the prefixes may not be at rest: a jump taken then (to the interpreter's way), or none when the block knows
//they are.
auto Allegrex::Recompiler::emitPrefixesBusy() -> sljit_jump* {
  if(prefixes.atRest()) return nullptr;
  mov32(reg(0), field(&self.vfpu.pfxs));
  mov32(reg(1), field(&self.vfpu.pfxt));
  xor32(reg(0), reg(0), imm(PrefixIdentity));
  xor32(reg(1), reg(1), imm(PrefixIdentity));
  or32(reg(0), reg(0), reg(1));
  or32(reg(0), reg(0), field(&self.vfpu.pfxd));
  return cmp32_jump(reg(0), imm(0), flag_ne);
}

//What an instruction leaves the prefixes as: at rest after one that uses them up (an instruction that raised an
//exception instead has ended the block); vpfxs's, vpfxt's or vpfxd's own value in its prefix (the bits
//decoderVFPU5() keeps); unknown after one that may set them otherwise (mtvc and vmtvc, which write control
//registers); as they were after anything else.
auto Allegrex::Recompiler::prefixesAfter(u32 instruction) -> void {
  auto rest = [&] { prefixes = {{true, true, true}, {PrefixIdentity, PrefixIdentity, 0}}; };
  switch(instruction >> 26) {
  case 0x18: case 0x19: case 0x1b: case 0x3c:
    return rest();
  case 0x34:
    if(instruction >> 16 == 0xd051) prefixes = {};  //vmtvc
    else if(instruction >> 16 != 0xd050) rest();    //(vmfvc reads)
    return;
  case 0x37: {
    u32 which = instruction >> 24 & 3;
    if(which == 3) return rest();  //viim and vfim use them up
    prefixes.known[which] = true;
    prefixes.value[which] = instruction & (which == 2 ? 0xfff : 0xf'ffff);
    return;
  }
  case 0x3f:  //vnop, vsync and vflush leave them alone
    return;
  case 0x12:
    if((instruction >> 21 & 31) == 0x07 && instruction & 0x80) prefixes = {};  //mtvc
    return;
  }
}

auto Allegrex::Recompiler::emitVFPU(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool {
  u32 op = instruction >> 26, group = instruction >> 23 & 7;
  u8 vd = instruction & 0x7f, vs = instruction >> 8 & 0x7f, vt = instruction >> 16 & 0x7f;
  u32 size = 1 + (instruction >> 7 & 1) + 2 * (instruction >> 15 & 1);
  u32 d = packLanes(self.vfpuLine(vd, size)), s = packLanes(self.vfpuLine(vs, size));
  u32 t = packLanes(self.vfpuLine(vt, size));

  //(one helper of each kind for each size)
  using Helper = void (Allegrex::*)(u32, u32, u32);
  auto sized = [&](Helper one, Helper two, Helper three, Helper four) {
    return size == 1 ? one : size == 2 ? two : size == 3 ? three : four;
  };
  #define Sized(name, ...) sized(&Allegrex::name<1 __VA_OPT__(,) __VA_ARGS__>, &Allegrex::name<2 __VA_OPT__(,) \
    __VA_ARGS__>, &Allegrex::name<3 __VA_OPT__(,) __VA_ARGS__>, &Allegrex::name<4 __VA_OPT__(,) __VA_ARGS__>)
  Helper helper = nullptr;
  u32 first = d, second = s, third = t;
  std::array<u32, 4> constants{};  //vzero, vone, vidt, viim, vfim: the lanes' values, written by the compiled code
  bool constant = false;
  u32 written = size;  //how many of vd's lanes the constants go in
  //an instruction emitEach() can compile (its op as vfpuLane() numbers them, and 9 for vscl): with its prefixes as they
  //are, or, for the math functions (which take prefixes their own way: vfpuLastLaneFirst()), at rest only
  s32 each = -1;
  bool restOnly = false;

  if(op == 0x37 && (instruction >> 24 & 3) != 3) {  //vpfxs, vpfxt, vpfxd: the bits decoderVFPU5() keeps
    u32 which = instruction >> 24 & 3;
    mov32(field(which == 0 ? &self.vfpu.pfxs : which == 1 ? &self.vfpu.pfxt : &self.vfpu.pfxd),
          imm(instruction & (which == 2 ? 0xfff : 0xf'ffff)));
    return true;
  } else if(op == 0x37) {  //viim, vfim: their constant into rt's single, as VIIM() and VFIM() make it
    constant = true, written = 1, vd = vt;
    if(instruction >> 23 & 1) constants[0] = halfToFloat(u16(instruction));
    else constants[0] = self.vfpuBits(s16(instruction));
  } else if(op == 0x18 && (group == 0 || group == 1 || group == 7)) {  //vadd, vsub, vdiv
    if(group == 0) helper = Sized(vfpuRestBinary, 0);
    if(group == 1) helper = Sized(vfpuRestBinary, 1);
    if(group == 7) helper = Sized(vfpuRestBinary, 7);
    each = group;
  } else if(op == 0x19 && group == 0) {  //vmul
    helper = Sized(vfpuRestBinary, 8), each = 8;
  } else if(op == 0x19 && group == 1) {  //vdot
    helper = Sized(vfpuRestDot), first = self.vfpuLine(vd, 1)[0];
  } else if(op == 0x19 && group == 2) {  //vscl
    helper = Sized(vfpuRestScale), third = self.vfpuLine(vt, 1)[0], each = 9;
  } else if(op == 0x19 && group == 4) {  //vhdp
    helper = Sized(vfpuRestHalfDot), first = self.vfpuLine(vd, 1)[0];
  } else if(op == 0x19 && group == 5 && size == 3) {  //vcrs.t
    helper = &Allegrex::vfpuRestCross<0>;
  } else if(op == 0x1b && group == 0) {  //vcmp
    helper = Sized(vfpuRestCompare), first = instruction & 15;
  } else if(op == 0x1b && (group == 2 || group == 3)) {  //vmin, vmax
    helper = group == 2 ? Sized(vfpuRestMinMax, false) : Sized(vfpuRestMinMax, true);
    each = 8 + group;
  } else if(op == 0x1b && (group == 6 || group == 7)) {  //vsge, vslt
    helper = group == 6 ? Sized(vfpuRestSet, false) : Sized(vfpuRestSet, true);
    each = 6 + group;
  } else if(op == 0x34 && group == 0 && instruction >> 24 != 0xd3 && instruction >> 16 != 0xd050 &&
            instruction >> 16 != 0xd051) {
    if(vt == 0x00) helper = Sized(vfpuRestBits, 0);  //vmov
    if(vt == 0x01) helper = Sized(vfpuRestBits, 1);  //vabs
    if(vt == 0x02) helper = Sized(vfpuRestBits, 2);  //vneg
    if(vt <= 0x02) each = 20 + vt;
    //vrcp, vrsq, vsin, vcos, vexp2, vlog2, vsqrt, vasin, vnrcp, vnsin, vrexp2
    if((vt >= 0x10 && vt <= 0x18) || vt == 0x1a || vt == 0x1c) each = 40 + vt - 0x10, restOnly = true;
    if(vt == 0x06) constant = true;                   //vzero
    if(vt == 0x07) constant = true, constants.fill(One);  //vone
    if(vt == 0x03 && (size == 2 || size == 4)) {      //vidt (other sizes are reserved)
      constant = true;
      u32 position = vd & 3, start = lineOffset(vd, size);
      for(u32 i : range(size)) constants[i] = ((start + i) & 3) == position ? One : 0;
    }
    if(vt >= 0x60) constant = true, constants.fill(ConstantTable[(vt & 31) < 20 ? vt & 31 : 0]);  //vcst
    //vf2h, vh2f, vuc2i, vc2i, vus2i, vs2i, vi2uc, vi2c, vi2us, vi2s, at the sizes their decoder takes: the lanes each
    //writes (the rest are reserved)
    u32 out = 0;
    if((vt == 0x32 || vt == 0x3e || vt == 0x3f) && (size == 2 || size == 4)) out = size / 2;
    if((vt == 0x33 || vt == 0x3a || vt == 0x3b) && size <= 2) out = 2 * size;
    if((vt == 0x3c || vt == 0x3d) && size == 4) out = 1;
    if(vt == 0x38 || vt == 0x39) out = 4;
    if(out) {
      #define Pack(n) if(vt == n) helper = &Allegrex::vfpuRestPack<n>;
      Pack(0x32) Pack(0x33) Pack(0x38) Pack(0x39) Pack(0x3a) Pack(0x3b) Pack(0x3c) Pack(0x3d) Pack(0x3e) Pack(0x3f)
      #undef Pack
      first = packLanes(self.vfpuLine(vd, out));
      second = packLanes(self.vfpuLine(vs, vt == 0x38 || vt == 0x39 ? 1 : size));
      third = size;
    }
  } else if(op == 0x34 && group == 5 && (instruction >> 21 & 3) == 0) {  //vi2f
    helper = Sized(vfpuRestToFloat), third = instruction >> 16 & 31, each = 30;
  } else if(op == 0x34 && group == 5 && (instruction >> 20 & 7) == 2) {  //vcmovt, vcmovf
    helper = Sized(vfpuRestConditionalMove), third = instruction >> 16 & 15;
  } else if(op == 0x34 && group == 4) {  //vf2in, vf2iz, vf2iu, vf2id
    each = 31;
  } else if(op == 0x3c) {
    //The matrix instructions, if the size is one they have, as decoderVFPU6() picks them: vmmul (any but 1), vtfm
    //and vhtfm (the matrix's size, or one less), vmscl (any but 1), and group 7's vmmov, vmidt, vmzero and vmone
    //(any but 1). The rest of the group goes to the interpreter's way, below.
    s32 which = -1;
    u32 matrix = size;
    if(group == 0 && size != 1) which = 0;
    if(group >= 1 && group <= 3 && (size == group + 1 || size == group)) {
      which = size == group + 1 ? 1 : 2, matrix = group + 1;
    }
    if(group == 4 && size != 1) which = 3;
    if(group == 7 && size != 1) which = vt == 0x00 ? 4 : vt == 0x03 ? 5 : vt == 0x06 ? 6 : vt == 0x07 ? 7 : -1;
    //vcrsp.t and vqmul.q, and vrot (group 7, rt's top two bits 01: its placement below them), which take prefixes
    if(group == 5 && (size == 3 || size == 4)) helper = size == 3 ? &Allegrex::vfpuRestCross<1> : &Allegrex::vfpuRestCross<2>;
    if(group == 7 && (instruction >> 21 & 3) == 1 && size != 1) {
      helper = Sized(vfpuRestRotate), second = self.vfpuLine(vs, 1)[0], third = instruction >> 16 & 31;
    }
    #if defined(ALLEGREX_NEON)
    if(which >= 0 && which <= 2) {
      if(auto product = matrixProduct(which, vd, vs, vt, matrix, first, second, third)) {
        callf(product, imm(first), imm(second), imm(third));
        if(!prefixes.atRest()) {
          mov32(field(&self.vfpu.pfxs), imm(PrefixIdentity));
          mov32(field(&self.vfpu.pfxt), imm(PrefixIdentity));
          mov32(field(&self.vfpu.pfxd), imm(0));
        }
        return true;
      }
    }
    #endif
    if(which >= 0) {
      callf(&Allegrex::vfpuMatrix, imm(which), imm(vd | vs << 8 | vt << 16), imm(matrix));
      return true;
    }
  } else if(instruction == 0xffff'0000 || instruction == 0xffff'0320 || instruction == 0xffff'040d) {
    return true;  //vnop, vsync, vflush: nothing to do or wait for, and the prefixes stay for the next instruction
  }
  #undef Sized
  if(each >= 0 && (prefixes.busy() ? !restOnly : !helper)) {
    return emitEach(address, instruction, count, delaySlot, each, size);
  }
  if((!helper && !constant) || prefixes.busy()) return emitVFPUCall(address, instruction, count, delaySlot);

  auto busy = emitPrefixesBusy();
  if(helper) {
    callf(helper, imm(first), imm(second), imm(third));
  } else {
    auto lanes = self.vfpuLine(vd, written);
    for(u32 i : range(written)) mov32(field(&self.vfpu.r[lanes[i]]), imm(constants[i]));
  }
  if(busy) emitCold({busy}, [=, this] {
    [[maybe_unused]] bool called = emitVFPUCall(address, instruction, count, delaySlot);
    assert(called);
  });
  return true;
}

//A lane-by-lane VFPU instruction run by vfpuEach() (op: vfpuLane()'s), each lane's reading and writing worked out here
//from the prefixes as the interpreter's vfpuRead(), outsideLanes() and vfpuWrite() use them: their values where the
//block knows them, else at rest, checked as it runs (otherwise it's the interpreter's way, emitVFPUCall(), as cold
//code). vscl reads rt's single in every lane, its prefix's absolute, constant and negate settings each lane's own, as
//VSCL() does.
auto Allegrex::Recompiler::emitEach(u32 address, u32 instruction, u32 count, bool delaySlot, u32 op, u32 size)
  -> bool {
  u8 vd = instruction & 0x7f, vs = instruction >> 8 & 0x7f, vt = instruction >> 16 & 0x7f;
  u32 value[3];
  std::vector<sljit_jump*> busy;
  for(u32 p : range(3)) {
    u32 rest = p == 2 ? 0 : PrefixIdentity;
    value[p] = prefixes.known[p] ? prefixes.value[p] : rest;
    if(prefixes.known[p]) continue;
    auto prefix = field(p == 0 ? &self.vfpu.pfxs : p == 1 ? &self.vfpu.pfxt : &self.vfpu.pfxd);
    busy.push_back(cmp32_jump(prefix, imm(rest), flag_ne));
  }
  //(describedLane())
  auto described = [&](u8 reg, u32 prefix) -> u64 {
    auto index = self.vfpuLine(reg, size);
    u64 lanes = 0;
    for(u32 i : range(size)) {
      u32 swizzle = prefix >> 2 * i & 3, absolute = prefix >> (8 + i) & 1, negate = prefix >> (16 + i) & 1;
      u32 lane = prefix >> (12 + i) & 1 ? 1 << 9 | (swizzle | absolute << 2) << 10 : index[swizzle] | absolute << 7;
      lanes |= u64(lane | negate << 8) << 16 * i;
    }
    return lanes;
  };
  u64 s = described(vs, value[0]), t = 0;
  u32 outside = outsideLanes(value[0], size);
  if(op <= 8 || (op >= 10 && op <= 13)) {
    t = described(vt, value[1]);
    outside |= outsideLanes(value[1], size);
  } else if(op == 9) {
    u32 scale = self.vfpuLine(vt, 1)[0];
    for(u32 i : range(size)) {
      u32 absolute = value[1] >> (8 + i) & 1, negate = value[1] >> (16 + i) & 1;
      u32 lane = value[1] >> (12 + i) & 1 ? 1 << 9 | (absolute << 2) << 10 : scale | absolute << 7;
      t |= u64(lane | negate << 8) << 16 * i;
    }
    op = 8;  //(a lane of vmul)
  } else if(op == 30) {
    t = instruction >> 16 & 31;
  } else if(op == 31) {
    t = (instruction >> 16 & 31) | (instruction >> 21 & 3) << 8;
  }
  //(describedWrite(), and the lanes past the size)
  auto index = self.vfpuLine(vd, size);
  u64 d = 0;
  for(u32 i : range(size)) {
    u32 lane = index[i] | (value[2] >> (8 + i) & 1) << 7 | (value[2] >> 2 * i & 3) << 8 | (outside >> i & 1) << 10;
    d |= u64(lane) << 16 * i;
  }
  using Each = void (Allegrex::*)(u64, u64, u64);
  #define Op(n) case n: helper = size == 1 ? (Each)&Allegrex::vfpuEach<1, n> : size == 2 ? &Allegrex::vfpuEach<2, n> \
    : size == 3 ? &Allegrex::vfpuEach<3, n> : &Allegrex::vfpuEach<4, n>; break;
  Each helper = nullptr;
  switch(op) {
  Op(0) Op(1) Op(7) Op(8) Op(10) Op(11) Op(12) Op(13) Op(20) Op(21) Op(22) Op(30) Op(31)
  Op(40) Op(41) Op(42) Op(43) Op(44) Op(45) Op(46) Op(47) Op(48) Op(50) Op(52)
  }
  #undef Op
  callf(helper, imm64(s), imm64(t), imm64(d));
  if(!busy.empty()) emitCold(busy, [=, this] {
    [[maybe_unused]] bool called = emitVFPUCall(address, instruction, count, delaySlot);
    assert(called);
  });
  return true;
}

#if defined(ALLEGREX_NEON)
//The NEON helper for vmmul (which 0), vtfm (1) or vhtfm (2) of a size x size matrix, and its arguments, when every
//operand is in place (vfpuTransform(), vfpuMultiply()): each matrix starting at its matrix's first row and column,
//each vector along a row or a column. Null for any other, which goes to vfpuMatrix().
auto Allegrex::Recompiler::matrixProduct(u32 which, u8 vd, u8 vs, u8 vt, u32 size, u32& first, u32& second,
                                         u32& third) -> Helper {
  //a matrix operand: 0 in place, 1 in place and transposed, -1 neither; its matrix's first register in base
  auto placed = [&](u8 reg, u32& base) -> s32 {
    auto square = self.vfpuSquare(reg, size);
    base = (reg >> 2 & 7) * 4;
    bool plain = true, turned = true;
    for(u32 r : range(size)) {
      for(u32 c : range(size)) {
        plain &= square[r * size + c] == base + c + 32 * r;
        turned &= square[r * size + c] == base + r + 32 * c;
      }
    }
    return plain ? 0 : turned ? 1 : -1;
  };
  //a vector operand of n lanes: its first register, and the stride above that (1 along a row, 32 down a column)
  auto line = [&](u8 reg, u32 n, u32& packed) -> bool {
    auto lanes = self.vfpuLine(reg, n);
    u32 stride = n > 1 ? lanes[1] - lanes[0] : 1;
    for(u32 k : range(n)) if(lanes[k] != lanes[0] + stride * k) return false;
    packed = lanes[0] | stride << 8;
    return stride == 1 || stride == 32;
  };
  if(which == 0) {
    u32 s, t, d;
    s32 ts = placed(vs, s), tt = placed(vt, t), td = placed(vd, d);
    if(ts < 0 || tt < 0 || td < 0) return nullptr;
    #define Multiply(n) {&Allegrex::vfpuMultiply<n, 0>, &Allegrex::vfpuMultiply<n, 1>, &Allegrex::vfpuMultiply<n, 2>, \
      &Allegrex::vfpuMultiply<n, 3>, &Allegrex::vfpuMultiply<n, 4>, &Allegrex::vfpuMultiply<n, 5>, \
      &Allegrex::vfpuMultiply<n, 6>, &Allegrex::vfpuMultiply<n, 7>}
    static constexpr Helper multiply[3][8] = {Multiply(2), Multiply(3), Multiply(4)};
    #undef Multiply
    first = s, second = t, third = d;
    return multiply[size - 2][ts | tt << 1 | td << 2];
  }
  u32 matrix, t, d;
  bool homogeneous = which == 2;
  s32 turned = placed(vs, matrix);
  if(turned < 0 || !line(vt, homogeneous ? size - 1 : size, t) || !line(vd, size, d)) return nullptr;
  #define Transform(n) {{&Allegrex::vfpuTransform<n, false, false>, &Allegrex::vfpuTransform<n, true, false>}, \
    {&Allegrex::vfpuTransform<n, false, true>, &Allegrex::vfpuTransform<n, true, true>}}
  static constexpr Helper transform[3][2][2] = {Transform(2), Transform(3), Transform(4)};
  #undef Transform
  first = matrix, second = t, third = d;
  return transform[size - 2][homogeneous][turned];
}
#endif

//A VFPU instruction of an arithmetic group (or vpfx's, viim's and vfim's) with no native code above, or with prefixes
//set that its native code doesn't take: the interpreter's own, as execute() runs it, but for decoderEXECUTE()'s choice
//of the group, which the block makes (executeVFPU()). It writes no integer register, so what the host registers hold stays true, and it
//halts the CPU only by an exception, after which the block leaves. False for anything else (vmfvc and vmtvc, and
//vnop's group), which goes to execute() as ever.
auto Allegrex::Recompiler::emitVFPUCall(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool {
  using Call = u32 (Allegrex::*)(u32, u32);
  Call call = nullptr;
  switch(instruction >> 26) {
  case 0x18: call = &Allegrex::executeVFPU<0x18>; break;
  case 0x19: call = &Allegrex::executeVFPU<0x19>; break;
  case 0x1b: call = &Allegrex::executeVFPU<0x1b>; break;
  case 0x34:
    if(instruction >> 16 != 0xd050 && instruction >> 16 != 0xd051) call = &Allegrex::executeVFPU<0x34>;
    break;
  case 0x37: call = &Allegrex::executeVFPU<0x37>; break;
  case 0x3c: call = &Allegrex::executeVFPU<0x3c>; break;
  }
  if(!call) return false;
  storeCount();  //(for the block's leaving after an exception)
  emitPC(address, delaySlot);
  mov32(field(&executed), imm(count));
  callf(call, imm(address), imm(instruction));
  testJumpEpilog();
  return true;
}

//MFVC Rt,index: a control register's bits, as vfpuControl() reads them: the prefixes, the condition codes, the
//random number generator's state, and 0 for the rest.
auto Allegrex::Recompiler::emitMFVC(u32 instruction) -> void {
  if(!RTn) return;
  u32 index = instruction & 0x7f;
  if(index < 4) mov32(reg(0), field(index == 0 ? &self.vfpu.pfxs : index == 1 ? &self.vfpu.pfxt :
                                    index == 2 ? &self.vfpu.pfxd : &self.vfpu.cc));
  else if(index >= 8 && index < 16) mov32(reg(0), field(&self.vfpu.rcx[index - 8]));
  else mov32(reg(0), imm(0));
  mov32(Rt, reg(0));
}

#undef RTn
#undef Rt
