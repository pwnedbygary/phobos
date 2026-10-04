//The VFPU (coprocessor 2). See VFPU in allegrex.hpp for how its registers, operand sizes and prefixes work.
//
//Most instructions follow one pattern: read the operands' lanes (through the source prefixes), do the same math on
//each lane, and write the results (through the destination prefix). vfpuUnary() and vfpuBinary() do that, given
//the math for one lane. After each VFPU instruction the prefixes go back to doing nothing; the decoder sees to
//that (vfpuPrefixesUsed()), except after vpfxs, vpfxt and vpfxd, which set them.
//
//The VFPU has no denormals (numbers too tiny for the normal float format): they count as zero, keeping their
//sign, both read and written. It always rounds to the nearest. On the hardware its functions (sine, 2^x and so
//on) are approximations; here they're computed in double precision, so results can differ from a PSP's in the
//last bits. Its random number generator is the hardware's (vfpuRandom()).
//
//Sources: pspdev's VFPU documentation (https://pspdev.github.io/vfpu-docs/) for the encodings and what each
//instruction does, and binutils' Allegrex support for the encodings; for the random number generator, what fp64
//worked out from a PSP's output (PPSSPP issue 16946); PPSSPP's behavior where these are silent, as noted.

//The prefixes' resting state: the lanes in their own order (x, y, z, w), nothing else changed.
static constexpr u32 PrefixIdentity = 0xe4;

//The constants a source prefix can put in a lane: 0, 1, 2, 1/2, 3, 1/3, 1/4, 1/6.
static constexpr u32 PrefixConstants[8] = {
  0x0000'0000, 0x3f80'0000, 0x4000'0000, 0x3f00'0000, 0x4040'0000, 0x3eaa'aaab, 0x3e80'0000, 0x3e2a'aaab,
};

//vcst's constants, numbered from 1 (0, and numbers past the table, give 0): the largest float, sqrt(2),
//sqrt(1/2), 2/sqrt(pi), 2/pi, 1/pi, pi/4, pi/2, pi, e, log2(e), log10(e), ln(2), ln(10), 2pi, pi/6, log10(2),
//log2(10), sqrt(3)/2.
static constexpr u32 ConstantTable[20] = {
  0x0000'0000, 0x7f7f'ffff, 0x3fb5'04f3, 0x3f35'04f3, 0x3f90'6ebb, 0x3f22'f983, 0x3ea2'f983, 0x3f49'0fdb,
  0x3fc9'0fdb, 0x4049'0fdb, 0x402d'f854, 0x3fb8'aa3b, 0x3ede'5bd9, 0x3f31'7218, 0x4013'5d8e, 0x40c9'0fdb,
  0x3f06'0a92, 0x3e9a'209b, 0x4054'9a78, 0x3f5d'b3d7,
};

static constexpr u32 One = 0x3f80'0000;  //1.0

//The math functions as a PSP computes them, worked out from results measured on one (docs/psp-vfpu-measurements.md)
//by tools/psp-vfpu-measure/fit.py. Each function turns its argument into a 23-bit index into one quadratic
//interpolator: the index's top 7 bits pick one of 128 segments, each with its own integers c0, m, n and binade e;
//the low 16 bits, x2, enter a linear term in full, and a squared term sees only their top 10 bits, as a distance t
//from the segment's middle, squared and rounded up to a multiple of 256 (u). The result, in units of 2^(e - 150)
//(a float's 24-bit significand when its exponent is e), is c0 + floor(m * x2 / 2^17) + floor(n * u / 2^Q),
//truncated to a multiple of 4: 22 bits. vfpu-segments.hpp holds the fitted tables.
struct VFPUSegment { s32 c0, m, n, e; };
#include "vfpu-segments.hpp"

//A table's result at a 23-bit index, as float bits. The tables cover rcp over [1, 2), rsq and sqrt over [1, 4)
//(the index drops the input's lowest bit), exp2 over [1, 2), cos over a quarter turn and asin over [0, 1); each
//function scales from there. (log2's table is fixed point instead: vfpuLog2() reads it.)
static auto interpolate(const VFPUSegment* segments, u32 index) -> u32 {
  auto& segment = segments[index >> 16];
  s64 x2 = index & 0xffff;
  s64 t = (x2 >> 6) - 512;
  s64 u = (t * t + 255) >> 8;
  s64 value = segment.c0 + (segment.m * x2 >> 17) + (segment.n * u >> InterpolatorScale);
  value &= ~3ll;
  if(value <= 0) return 0;
  s32 e = segment.e;
  while(value >= 1 << 24) value >>= 1, e++;  //carried into the next binade
  while(value < 1 << 23) value <<= 1, e--;   //below the segment's binade
  return e << 23 | (value & 0x7f'ffff);
}

//bits times 2^k, by moving the exponent: too big gives infinity, too small zero (the VFPU has no denormals).
static auto scaleBits(u32 bits, s32 k) -> u32 {
  if(!(bits & 0x7fff'ffff)) return bits;
  s32 e = s32(bits >> 23 & 0xff) + k;
  if(e >= 255) return (bits & 0x8000'0000) | 0x7f80'0000;
  if(e <= 0) return bits & 0x8000'0000;
  return (bits & 0x807f'ffff) | u32(e) << 23;
}

//The parts of a float: its sign bit, exponent and mantissa, a denormal counting as zero.
struct FloatParts {
  FloatParts(u32 bits) : sign(bits & 0x8000'0000), exponent(bits >> 23 & 0xff), mantissa(bits & 0x7f'ffff) {
    if(exponent == 0) mantissa = 0;
  }
  auto zero() const -> bool { return exponent == 0; }
  auto infinite() const -> bool { return exponent == 0xff && !mantissa; }
  auto nan() const -> bool { return exponent == 0xff && mantissa; }
  u32 sign, exponent, mantissa;
};

//|x| as a fixed-point number with 23 bits after the point, the rest dropped; clamped at 2^40 (far past where exp2
//overflows), so large inputs stay large.
static auto fixed23(FloatParts x) -> s64 {
  if(x.zero()) return 0;
  s64 shift = s64(x.exponent) - 127;
  s64 significand = x.mantissa | 0x80'0000;
  if(shift >= 17) return s64(1) << 40;
  if(shift >= 0) return significand << shift;
  if(shift > -24) return significand >> -shift;
  return 0;
}

static auto vfpuReciprocal(u32 bits) -> u32 {
  FloatParts x{bits};
  if(x.nan()) return 0x7f80'0001 | x.sign;
  if(x.infinite()) return x.sign;
  if(x.zero()) return x.sign | 0x7f80'0000;
  return x.sign | scaleBits(interpolate(rcpSegments, x.mantissa), 127 - s32(x.exponent));
}

//x = 2^p * (1 + f): an even p takes the index's lower half ([1, 2)), an odd one its upper half ([2, 4)), and
//the result is scaled by 2^-floor(p / 2) for rsq, 2^floor(p / 2) for sqrt.
static auto vfpuReciprocalSqrt(u32 bits) -> u32 {
  FloatParts x{bits};
  if(x.nan()) return 0x7f80'0001 | x.sign;
  if(x.zero()) return x.sign | 0x7f80'0000;
  if(x.sign) return 0xff80'0001;
  if(x.infinite()) return 0;
  s32 p = s32(x.exponent) - 127;
  u32 index = (p & 1 ? 1 << 22 : 0) | x.mantissa >> 1;
  return scaleBits(interpolate(rsqSegments, index), -(p >> 1));
}

static auto vfpuSqrt(u32 bits) -> u32 {
  FloatParts x{bits};
  if(x.nan()) return 0x7f80'0001;
  if(x.zero()) return 0;  //-0 too
  if(x.sign) return 0x7f80'0001;
  if(x.infinite()) return bits;
  s32 p = s32(x.exponent) - 127;
  u32 index = (p & 1 ? 1 << 22 : 0) | x.mantissa >> 1;
  return scaleBits(interpolate(sqrtSegments, index), p >> 1);
}

//2^y, which exp2 (y = x) and rexp2 (y = -x) share. |y| is split into a whole part n and a 23-bit fraction. A
//positive y (or zero) reads the table at the fraction: 2^y = 2^(1 + fraction) * 2^(n - 1). A negative y reads it
//backwards, the fraction's bits inverted: 2^y = 2^(1 + ~fraction) * 2^(-n - 2).
static auto vfpuPower2(u32 bits, bool negate) -> u32 {
  FloatParts x{bits};
  if(x.nan()) return 0x7f80'0001;
  bool negative = !x.zero() && (x.sign != 0) != negate;
  s64 fixed = fixed23(FloatParts{bits & 0x7fff'ffff});
  s64 n = fixed >> 23;
  if(!negative) {
    if(n > 128) return 0x7f80'0000;
    return scaleBits(interpolate(exp2Segments, u32(fixed & 0x7f'ffff)), s32(n - 1));
  }
  if(n > 150) return 0;
  return scaleBits(interpolate(exp2Segments, u32(~fixed & 0x7f'ffff)), s32(-n - 2));
}

static auto vfpuExp2(u32 bits) -> u32 { return vfpuPower2(bits, false); }
static auto vfpuReciprocalExp2(u32 bits) -> u32 { return vfpuPower2(bits, true); }

//Cosine and sine of x quarter turns, both from cosine's table: sine is it read backwards (sin f = cos(1 - f)).
//|x| becomes 23-bit fixed point; the low two bits of its whole part pick the quadrant, its fraction the point in
//it. Very large arguments come out strangely, as on the hardware: the shift that makes the fixed-point value has
//only 5 bits, so past 2^31 it wraps around, and a shift of exactly 32, 64 or 96 shifts everything out, leaving 0.
static auto quarterTurns(u32 bits) -> s64 {
  FloatParts x{bits & 0x7fff'ffff};
  if(x.zero()) return 0;
  s32 shift = s32(x.exponent) - 127;
  s64 significand = x.mantissa | 0x80'0000;
  if(shift >= 32) {
    shift &= 31;
    if(shift == 0) return 0;
  }
  if(shift >= 0) return significand << shift;
  if(shift > -24) return significand >> -shift;
  return 0;
}

static auto cosineAt(u32 f) -> u32 { return interpolate(cosSegments, f); }
static auto sineAt(u32 f) -> u32 { return f ? interpolate(cosSegments, (1 << 23) - f) : 0; }

//An infinity is no angle: its sine and cosine are NaNs, like a NaN's (measured, docs/psp-vfpu-measurements.md,
//round 2), though quarterTurns() would make it 0.
static auto vfpuSine(u32 bits) -> u32 {
  FloatParts x{bits};
  if(x.nan() || x.infinite()) return 0x7f80'0001 | x.sign;
  s64 k = quarterTurns(bits);
  u32 f = k & 0x7f'ffff;
  u32 result;
  switch(k >> 23 & 3) {
  case 0: result = sineAt(f); break;
  case 1: result = cosineAt(f); break;
  case 2: result = sineAt(f) ^ 0x8000'0000; break;
  default: result = cosineAt(f) ^ 0x8000'0000; break;
  }
  return result ^ x.sign;  //sin(-x) = -sin(x)
}

static auto vfpuCosine(u32 bits) -> u32 {
  FloatParts x{bits};
  if(x.nan() || x.infinite()) return 0x7f80'0001;
  s64 k = quarterTurns(bits);  //cos(-x) = cos(x)
  u32 f = k & 0x7f'ffff;
  switch(k >> 23 & 3) {
  case 0: return cosineAt(f);
  case 1: return sineAt(f) ^ 0x8000'0000;
  case 2: return cosineAt(f) ^ 0x8000'0000;
  default: return sineAt(f);
  }
}

//arcsine in quarter turns: |x| as a 23-bit fixed-point index, the result taking x's sign; 1 itself gives 1, and
//past 1 there's no arcsine.
static auto vfpuArcsine(u32 bits) -> u32 {
  FloatParts x{bits};
  if(x.nan() || x.exponent > 127 || (x.exponent == 127 && x.mantissa)) return 0x7f80'0001 | x.sign;
  if(x.exponent == 127) return x.sign | 0x3f80'0000;
  return x.sign | interpolate(asinSegments, u32(fixed23(FloatParts{bits & 0x7fff'ffff})));
}

//value * 2^-point as float bits, for a positive value of at most 24 significant bits (so it's exact).
static auto fixedBits(u64 value, s32 point) -> u32 {
  if(!value) return 0;
  s32 top = 63 - std::countl_zero(value);  //where the leading one is
  u32 mantissa = u32(top > 23 ? value >> (top - 23) : value << (23 - top)) & 0x7f'ffff;
  return u32(top - point + 127) << 23 | mantissa;
}

//log2 of x = 2^p * (1 + f) is p + log2(1 + f), and log2's table holds log2(1 + f) in fixed point, in units of
//2^-24 in every segment. The result is fixed point too, so how precise it is depends on its size, not as a float:
//- From 1 up (p >= 0): p plus the table's value, truncated to 22 bits after the point, and to 23 significant bits
//  once p needs some of them. (From 4 up the PSP sometimes gives one unit less: not worked out yet.)
//- Below 1 (p < 0): the PSP takes a cheaper path, a straight line through the segment: its first value cut to 17
//  bits after the point, its slope with the low 9 bits dropped, and no squared term. The result's magnitude,
//  |p| - log2(1 + f), is truncated to 15 bits after the point whatever its size, so just below 1 it's -0.
static auto vfpuLog2(u32 bits) -> u32 {
  FloatParts x{bits};
  if(x.nan() || (x.sign && !x.zero())) return 0x7f80'0001;  //negative numbers have no logarithm
  if(x.zero()) return 0xff80'0000;                            //-infinity, for either zero (denormals too)
  if(x.infinite()) return 0x7f80'0000;
  s64 p = s64(x.exponent) - 127;
  auto& segment = log2Segments[x.mantissa >> 16];
  s64 x2 = x.mantissa & 0xffff;
  if(p < 0) {
    s64 first = segment.c0 + 2 * segment.n;  //the segment's first value: its squared term is 2n there (u = 1024)
    s64 line = (first >> 7 << 15) + (segment.m >> 9) * x2;  //log2(1 + f), in units of 2^-32
    s64 magnitude = ((-p << 32) - line) >> 17;              //|log2 x|, in units of 2^-15
    return 0x8000'0000 | fixedBits(magnitude, 15);
  }
  s64 t = (x2 >> 6) - 512;
  s64 u = (t * t + 255) >> 8;
  s64 value = (p << 24) + segment.c0 + (segment.m * x2 >> 17) + (segment.n * u >> InterpolatorScale);
  s32 drop = std::max(2, 63 - std::countl_zero(u64(value)) - 22);  //22 bits after the point, 23 significant
  return fixedBits(u64(value) >> drop << drop, 24);
}

//Half-precision floats (16 bits: sign, 5-bit exponent, 10-bit mantissa), as vfim, vf2h and vh2f use them. Neither
//direction has denormals; infinity and NaN keep their low mantissa bits.
static auto halfToFloat(u32 half) -> u32 {
  u32 sign = (half & 0x8000) << 16, exponent = half >> 10 & 0x1f, mantissa = half & 0x3ff;
  if(exponent == 0) return sign;
  if(exponent == 31) return sign | 0xff << 23 | mantissa;
  return sign | (exponent + 127 - 15) << 23 | mantissa << 13;
}

static auto floatToHalf(u32 bits) -> u32 {
  u32 sign = bits >> 16 & 0x8000, exponent = bits >> 23 & 0xff, mantissa = bits & 0x7f'ffff;
  if(exponent == 0) return sign;
  if(exponent == 255) return sign | 31 << 10 | (mantissa & 0x3ff);
  if(exponent <= 112) return sign;             //too small for a half: zero
  if(exponent >= 143) return sign | 31 << 10;  //too big: infinity
  return sign | (exponent - 127 + 15) << 10 | mantissa >> 13;
}

//vf2in, vf2iz, vf2iu, vf2id: a float times 2^scale, rounded as the mode says (to the nearest, toward zero, up,
//down) to a signed integer; out-of-range values give the nearest integer that fits, and NaN gives 0x7fff'ffff.
static auto toInteger(f32 value, u32 scale, u32 mode) -> u32 {
  f64 x = std::ldexp((f64)value, (s32)scale);
  if(std::isnan(x)) return 0x7fff'ffff;
  switch(mode) {
  case 0: x = std::nearbyint(x); break;  //the host rounds to the nearest, ties to even, by default
  case 1: x = std::trunc(x); break;
  case 2: x = std::ceil(x); break;
  default: x = std::floor(x); break;
  }
  if(x >= 2147483647.0) return 0x7fff'ffff;
  if(x <= -2147483648.0) return 0x8000'0000;
  return s32(x);
}

//vcmp's conditions, by number: false, ==, <, <=, true, !=, >=, >, then tests of one value: is zero, is NaN, is
//infinite, is NaN or infinite, and the four opposites of those.
static auto compare(u32 condition, f32 s, f32 t) -> bool {
  switch(condition & 15) {
  case  0: return false;
  case  1: return s == t;
  case  2: return s < t;
  case  3: return s <= t;
  case  4: return true;
  case  5: return s != t;
  case  6: return s >= t;
  case  7: return s > t;
  case  8: return s == 0.0f;
  case  9: return std::isnan(s);
  case 10: return std::isinf(s);
  case 11: return std::isnan(s) || std::isinf(s);
  case 12: return s != 0.0f;
  case 13: return !std::isnan(s);
  case 14: return !std::isinf(s);
  default: return !std::isnan(s) && !std::isinf(s);
  }
}

//Where a pair (two further on) or a triple (one further on) starts along its row or column when bit 6 is set.
static auto lineOffset(u8 reg, u32 size) -> u32 {
  if(!(reg & 0x40)) return 0;
  return size == 2 ? 2 : size == 3 ? 1 : 0;
}

auto Allegrex::vfpuFloat(u32 bits) const -> f32 {
  if((bits & 0x7f80'0000) == 0) bits &= 0x8000'0000;  //a denormal counts as zero
  return std::bit_cast<f32>(bits);
}

auto Allegrex::vfpuBits(f64 value) const -> u32 {
  u32 bits = std::bit_cast<u32>((f32)value);
  if((bits & 0x7f80'0000) == 0) bits &= 0x8000'0000;
  return bits;
}

//The registers along a vector operand: its first lane, then the next ones along its row or column. Lanes past the
//operand's size carry on round the row or column; only a swizzle reaches them. A single goes down its column.
auto Allegrex::vfpuLine(u8 reg, u32 size) const -> std::array<u8, 4> {
  std::array<u8, 4> lanes;
  u32 matrix = (reg >> 2 & 7) * 4;
  if(size == 1) {
    u32 column = reg & 3, row = reg >> 5 & 3;
    for(u32 k : range(4)) lanes[k] = matrix + column + 32 * ((row + k) & 3);
    return lanes;
  }
  u32 index = reg & 3, start = lineOffset(reg, size);
  bool row = reg & 0x20;
  for(u32 k : range(4)) {
    u32 along = (start + k) & 3;
    lanes[k] = row ? matrix + along + 32 * index : matrix + index + 32 * along;
  }
  return lanes;
}

//The registers of a size x size matrix operand, element (row r, column c) at index r * size + c. A transposed
//matrix (bit 5) lists them the other way round, and its low bits and bit 6 swap roles.
auto Allegrex::vfpuSquare(u8 reg, u32 size) const -> std::array<u8, 16> {
  std::array<u8, 16> elements{};
  u32 matrix = (reg >> 2 & 7) * 4, low = reg & 3, offset = lineOffset(reg, size);
  bool transposed = reg & 0x20;
  u32 firstColumn = transposed ? offset : low;
  u32 firstRow = transposed ? low : offset;
  for(u32 r : range(size)) {
    for(u32 c : range(size)) {
      u8 index = matrix + ((firstColumn + c) & 3) + 32 * ((firstRow + r) & 3);
      elements[transposed ? c * size + r : r * size + c] = index;
    }
  }
  return elements;
}

//Reads a vector operand's lanes through a source prefix. For each lane i the prefix says which lane to take (two
//bits at 2i: the swizzle), whether to take its absolute value (bit 8 + i), whether to use a constant instead
//(bit 12 + i; the swizzle and absolute bits then pick one of the eight constants), and whether to negate it
//(bit 16 + i).
auto Allegrex::vfpuRead(u8 reg, u32 size, u32 prefix) const -> Vector {
  auto index = vfpuLine(reg, size);
  Vector value{};
  for(u32 i : range(size)) {
    u32 swizzle = prefix >> (2 * i) & 3;
    u32 absolute = prefix >> (8 + i) & 1;
    u32 lane;
    if(prefix >> (12 + i) & 1) lane = PrefixConstants[swizzle | absolute << 2];
    else lane = absolute ? vfpu.r[index[swizzle]] & 0x7fff'ffff : vfpu.r[index[swizzle]];
    if(prefix >> (16 + i) & 1) lane ^= 0x8000'0000;
    value.lane[i] = lane;
  }
  return value;
}

//Writes a vector operand's lanes through a destination prefix. For each lane i the prefix can clamp the result
//(bit 2i: to 0..1, which also turns -0 into 0; with bit 2i + 1 as well, to -1..1), or leave the lane as it was
//(bit 8 + i, the write mask).
auto Allegrex::vfpuWrite(u8 reg, u32 size, const Vector& value, u32 prefix) -> void {
  auto index = vfpuLine(reg, size);
  for(u32 i : range(size)) {
    if(prefix >> (8 + i) & 1) continue;
    u32 lane = value.lane[i];
    if(prefix >> (2 * i) & 1) {
      f32 x = std::bit_cast<f32>(lane);
      f32 low = prefix >> (2 * i + 1) & 1 ? -1.0f : 0.0f;
      if(x > 1.0f) lane = One;
      else if(x <= low) lane = std::bit_cast<u32>(low);
    }
    vfpu.r[index[i]] = lane;
  }
}

//Matrix instructions don't use the prefixes.
auto Allegrex::vfpuReadMatrix(u8 reg, u32 size) const -> Matrix {
  auto index = vfpuSquare(reg, size);
  Matrix value{};
  for(u32 i : range(size * size)) value.element[i] = vfpu.r[index[i]];
  return value;
}

auto Allegrex::vfpuWriteMatrix(u8 reg, u32 size, const Matrix& value) -> void {
  auto index = vfpuSquare(reg, size);
  for(u32 i : range(size * size)) vfpu.r[index[i]] = value.element[i];
}

//An instruction has used the prefixes, so they go back to doing nothing. One that raised an exception instead of
//running (a reserved encoding, say) leaves them as they were.
auto Allegrex::vfpuPrefixesUsed() -> void {
  if(pipeline.exception) return;
  vfpu.pfxs = PrefixIdentity;
  vfpu.pfxt = PrefixIdentity;
  vfpu.pfxd = 0;
}

//The control registers, numbered from 128: the three prefixes (only their meaningful bits are kept), the
//condition codes, and from 136 the random number generator's state. The others read as zero.
auto Allegrex::vfpuControl(u8 index) const -> u32 {
  switch(index) {
  case 0: return vfpu.pfxs;
  case 1: return vfpu.pfxt;
  case 2: return vfpu.pfxd;
  case 3: return vfpu.cc;
  }
  if(index >= 8 && index < 16) return vfpu.rcx[index - 8];
  return 0;
}

auto Allegrex::vfpuSetControl(u8 index, u32 value) -> void {
  switch(index) {
  case 0: vfpu.pfxs = value & 0xf'ffff; return;
  case 1: vfpu.pfxt = value & 0xf'ffff; return;
  case 2: vfpu.pfxd = value & 0xfff; return;
  case 3: vfpu.cc = value & 0x3f; return;
  }
  //the random number generator's registers keep 20 bits; the rest always read as 0x3f8 (see vfpuRandom())
  if(index >= 8 && index < 16) vfpu.rcx[index - 8] = 0x3f80'0000 | (value & 0xf'ffff);
}

//The random number generator, as fp64 worked it out from a PSP's output (PPSSPP issue 16946). It combines four
//simple generators and adds up their outputs:
//  - a: a linear congruential generator, each step a * 69069 + 1;
//  - b: a xorshift generator (Marsaglia's, with shifts of 13, 17 and 5);
//  - c and d: a sequence like the Pell numbers, each new term twice the last plus the one before (2d + c), plus
//  - e: a carry, set when c + d / 2 + e overflows 32 bits, and added into the next term.
//Each draw steps all of them and gives a + b + d.
//
//Their state lives in the eight RCX control registers, laid out oddly: registers 0-3 hold the low halves of a, b,
//c and d, registers 4-7 the high halves, each register four bits of e above that (register n holding bits 4n to
//4n + 3), and the top bits always read 0x3f8, so each register looks like a float from 1 up to 1.015625. The
//carry rule is the one that matched every one of fp64's datasets; how it really works isn't known.
auto Allegrex::vfpuRandom() -> u32 {
  auto& rcx = vfpu.rcx;
  u32 a = (rcx[0] & 0xffff) | rcx[4] << 16;
  u32 b = (rcx[1] & 0xffff) | rcx[5] << 16;
  u32 c = (rcx[2] & 0xffff) | rcx[6] << 16;
  u32 d = (rcx[3] & 0xffff) | rcx[7] << 16;
  u32 e = 0;
  for(u32 n : range(8)) e |= (rcx[n] >> 16 & 15) << (4 * n);

  a = a * 69069 + 1;
  b ^= b << 13;
  b ^= b >> 17;
  b ^= b << 5;
  u32 next = 2 * d + c + e;
  e = (u64(c) + (d >> 1) + e) >> 32;
  c = d;
  d = next;

  const u32 parts[4] = {a, b, c, d};
  for(u32 n : range(8)) rcx[n] = 0x3f80'0000 | (e >> (4 * n) & 15) << 16 | (parts[n % 4] >> (16 * (n / 4)) & 0xffff);
  return a + b + d;
}

//How vmin, vmax, the vsrt passes, vsgn and vscmp put values in order (measured, docs/psp-vfpu-measurements.md,
//round 2): as numbers, except that denormals count as zero (and -0 the same as +0), and a NaN counts as past the
//infinity of its sign, further out the bigger its mantissa. That's the order of the bits read as a sign and a
//magnitude, with the denormals squashed to zero. Those instructions give back the lanes themselves, bits and all,
//so a denormal that wins comes out as it went in.
static auto vfpuOrder(u32 bits) -> s64 {
  s64 magnitude = bits & 0x7f80'0000 ? bits & 0x7fff'ffff : 0;
  return bits >> 31 ? -magnitude : magnitude;
}

//A NaN result becomes the VFPU's own NaN (see NaNSign in allegrex.hpp); s and t are the inputs it came from.
auto Allegrex::vfpuNaN(u32 result, NaNSign sign, u32 s, u32 t) const -> u32 {
  bool isNaN = (result & 0x7f80'0000) == 0x7f80'0000 && (result & 0x7f'ffff);
  if(!isNaN || sign == NaNSign::Unknown) return result;
  switch(sign) {
  case NaNSign::Input: return 0x7f80'0001 | (s & 0x8000'0000);
  case NaNSign::Negated: return 0x7f80'0001 | (~s & 0x8000'0000);
  case NaNSign::Product: return 0x7f80'0001 | ((s ^ t) & 0x8000'0000);
  default: return 0x7f80'0001;
  }
}

template<typename F> auto Allegrex::vfpuUnary(u8 vd, u8 vs, u32 size, F function, NaNSign sign) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = vfpuNaN(vfpuBits(function(vfpuFloat(s.lane[i]))), sign, s.lane[i]);
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//For functions that work on a lane's bits directly (the exact math functions above).
template<typename F> auto Allegrex::vfpuUnaryBits(u8 vd, u8 vs, u32 size, F function) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = function(s.lane[i]);
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

template<typename F> auto Allegrex::vfpuBinary(u8 vd, u8 vs, u8 vt, u32 size, F function, NaNSign sign) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto t = vfpuRead(vt, size, vfpu.pfxt);
  Vector d{};
  for(u32 i : range(size)) {
    u32 result = vfpuBits(function(vfpuFloat(s.lane[i]), vfpuFloat(t.lane[i])));
    d.lane[i] = vfpuNaN(result, sign, s.lane[i], t.lane[i]);
  }
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//bvf and bvt branch when a condition code bit is false or true; bvfl and bvtl are their likely forms.
auto Allegrex::BV(bool value, bool likely, u8 bit, s16 imm) -> void {
  bool taken = (vfpu.cc >> bit & 1) == value;
  if(taken) return branch(ipu.pc + imm * 4);
  if(likely) skipDelaySlot();
}

//lvl.q and lvr.q load a quad that isn't aligned to 16 bytes, in two halves, as lwl and lwr do for a word. lvr.q
//fills the first lanes with the words from its address to the end of that 16-byte block; lvl.q fills the last
//lanes with the words from the block's start up to its address. "lvr.q at A, then lvl.q at A + 12" loads the
//quad at A. Their address must still be a multiple of four.
auto Allegrex::LVLQ(u8 vt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressLoad, address);
  auto lanes = vfpuLine(vt, 4);
  u32 block = address & ~15, word = address >> 2 & 3;
  for(u32 j : range(word + 1)) vfpu.r[lanes[3 - word + j]] = read(Word, block + 4 * j);
}

//lv.q and sv.q move a quad (a column or a row) to or from 16 bytes of memory, which must be aligned to 16 bytes.
auto Allegrex::LVQ(u8 vt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 15) return addressError(Exception::AddressLoad, address);
  auto lanes = vfpuLine(vt, 4);
  for(u32 k : range(4)) vfpu.r[lanes[k]] = read(Word, address + 4 * k);
}

auto Allegrex::LVRQ(u8 vt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressLoad, address);
  auto lanes = vfpuLine(vt, 4);
  u32 block = address & ~15, word = address >> 2 & 3;
  for(u32 j : range(4 - word)) vfpu.r[lanes[j]] = read(Word, block + 4 * (word + j));
}

//lv.s and sv.s move one register to or from a word of memory, which must be a multiple of four.
auto Allegrex::LVS(u8 vt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressLoad, address);
  vfpu.r[vt] = read(Word, address);
}

//mfv and mtv copy a VFPU register to or from an integer register, bits unchanged; mfvc and mtvc do the same with
//a control register.
auto Allegrex::MFV(u32& rt, u8 vd) -> void {
  rt = vfpu.r[vd];
}

auto Allegrex::MFVC(u32& rt, u8 index) -> void {
  rt = vfpuControl(index);
}

auto Allegrex::MTV(cu32& rt, u8 vd) -> void {
  vfpu.r[vd] = rt;
}

auto Allegrex::MTVC(cu32& rt, u8 index) -> void {
  vfpuSetControl(index, rt);
}

auto Allegrex::SVLQ(u8 vt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressStore, address);
  auto lanes = vfpuLine(vt, 4);
  u32 block = address & ~15, word = address >> 2 & 3;
  for(u32 j : range(word + 1)) write(Word, block + 4 * j, vfpu.r[lanes[3 - word + j]]);
}

auto Allegrex::SVQ(u8 vt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 15) return addressError(Exception::AddressStore, address);
  auto lanes = vfpuLine(vt, 4);
  for(u32 k : range(4)) write(Word, address + 4 * k, vfpu.r[lanes[k]]);
}

auto Allegrex::SVRQ(u8 vt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressStore, address);
  auto lanes = vfpuLine(vt, 4);
  u32 block = address & ~15, word = address >> 2 & 3;
  for(u32 j : range(4 - word)) write(Word, block + 4 * (word + j), vfpu.r[lanes[j]]);
}

auto Allegrex::SVS(u8 vt, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressStore, address);
  write(Word, address, vfpu.r[vt]);
}

//vabs and vneg clear or flip the sign bit, and touch nothing else (not even a denormal).
auto Allegrex::VABS(u8 vd, u8 vs, u32 size) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  for(u32 i : range(size)) s.lane[i] &= 0x7fff'ffff;
  vfpuWrite(vd, size, s, vfpu.pfxd);
}

auto Allegrex::VADD(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  vfpuBinary(vd, vs, vt, size, [](f32 s, f32 t) { return s + t; }, NaNSign::Positive);
}

//arcsine, in quarter turns
auto Allegrex::VASIN(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, vfpuArcsine);
}

//vfad adds up the lanes into one value; vavg averages them.
auto Allegrex::VAVG(u8 vd, u8 vs, u32 size) -> void {
  if(size == 1) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  f64 sum = 0;
  for(u32 i : range(size)) sum += vfpuFloat(s.lane[i]);
  Vector d{{vfpuNaN(vfpuBits(sum / size), NaNSign::Positive, 0)}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

//vbfy1 and vbfy2 are the "butterfly" steps of a fast Fourier transform: sums and differences of pairs of lanes,
//neighbours for vbfy1 (x + y, x - y, z + w, z - w), two apart for vbfy2 (x + z, y + w, x - z, y - w).
auto Allegrex::VBFY1(u8 vd, u8 vs, u32 size) -> void {
  if(size != 2 && size != 4) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  auto bits = [&](f32 value) { return vfpuNaN(vfpuBits(value), NaNSign::Positive, 0); };
  for(u32 i = 0; i < size; i += 2) {
    f32 x = vfpuFloat(s.lane[i]), y = vfpuFloat(s.lane[i + 1]);
    d.lane[i] = bits(x + y);
    d.lane[i + 1] = bits(x - y);
  }
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

auto Allegrex::VBFY2(u8 vd, u8 vs, u32 size) -> void {
  if(size != 4) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto bits = [&](f32 value) { return vfpuNaN(vfpuBits(value), NaNSign::Positive, 0); };
  f32 x[4];
  for(u32 i : range(4)) x[i] = vfpuFloat(s.lane[i]);
  Vector d{{bits(x[0] + x[2]), bits(x[1] + x[3]), bits(x[0] - x[2]), bits(x[1] - x[3])}};
  vfpuWrite(vd, 4, d, vfpu.pfxd);
}

//vc2i.s: rs's four bytes into the top byte of four lanes (as integers scaled up by 2^24).
auto Allegrex::VC2I(u8 vd, u8 vs, u32) -> void {
  auto s = vfpuRead(vs, 1, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(4)) d.lane[i] = (s.lane[0] >> (8 * i) & 0xff) << 24;
  vfpuWrite(vd, 4, d, vfpu.pfxd);
}

//vcmovt and vcmovf copy each lane of rs to rd only where a condition code bit is set (vcmovt) or clear (vcmovf).
//bit picks the condition code bit; 6 means each lane's own bit.
auto Allegrex::VCMOV(u8 vd, u8 vs, u32 size, bool onFalse, u8 bit) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto index = vfpuLine(vd, size);
  Vector d{};
  for(u32 i : range(size)) {
    bool condition = vfpu.cc >> (bit == 6 ? i : bit) & 1;
    d.lane[i] = condition != onFalse ? s.lane[i] : vfpu.r[index[i]];
  }
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//vcmp compares rs and rt lane by lane and sets a condition code bit for each lane, bit 4 if any lane passed and
//bit 5 if every lane did. Bits of lanes past the operand's size keep their value.
auto Allegrex::VCMP(u8 condition, u8 vs, u8 vt, u32 size) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto t = vfpuRead(vt, size, vfpu.pfxt);
  u32 bits = 0, any = 0, every = 1;
  for(u32 i : range(size)) {
    u32 result = compare(condition, vfpuFloat(s.lane[i]), vfpuFloat(t.lane[i]));
    bits |= result << i;
    any |= result;
    every &= result;
  }
  u32 affected = ((1 << size) - 1) | 0x30;
  vfpu.cc = (vfpu.cc & ~affected) | ((bits | any << 4 | every << 5) & affected);
}

//cosine, of quarter turns (the VFPU measures angles in quarter turns: cos(1) is the cosine of 90 degrees)
auto Allegrex::VCOS(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, vfpuCosine);
}

//vcrs.t: the two halves of a cross product, (sy * tz, sz * tx, sx * ty); subtracting the other half (vcrs with
//the operands swapped) completes it. vcrsp.t computes the whole cross product at once.
auto Allegrex::VCRS(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  if(size != 3) return INVALID();
  auto s = vfpuRead(vs, 3, vfpu.pfxs);
  auto t = vfpuRead(vt, 3, vfpu.pfxt);
  auto product = [&](u32 i, u32 j) {
    return vfpuNaN(vfpuBits(vfpuFloat(s.lane[i]) * vfpuFloat(t.lane[j])), NaNSign::Product, s.lane[i], t.lane[j]);
  };
  Vector d{{product(1, 2), product(2, 0), product(0, 1)}};
  vfpuWrite(vd, 3, d, vfpu.pfxd);
}

auto Allegrex::VCRSP(u8 vd, u8 vs, u8 vt, u32) -> void {
  auto s = vfpuRead(vs, 3, vfpu.pfxs);
  auto t = vfpuRead(vt, 3, vfpu.pfxt);
  f64 a[3], b[3];
  for(u32 i : range(3)) {
    a[i] = vfpuFloat(s.lane[i]);
    b[i] = vfpuFloat(t.lane[i]);
  }
  auto bits = [&](f64 value) { return vfpuNaN(vfpuBits(value), NaNSign::Positive, 0); };
  Vector d{{bits(a[1] * b[2] - a[2] * b[1]), bits(a[2] * b[0] - a[0] * b[2]), bits(a[0] * b[1] - a[1] * b[0])}};
  vfpuWrite(vd, 3, d, vfpu.pfxd);
}

//vcst puts one of the VFPU's built-in constants (pi, e, sqrt(2) and others) in every lane.
auto Allegrex::VCST(u8 vd, u32 size, u8 constant) -> void {
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = ConstantTable[constant < 20 ? constant : 0];
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//vdet.p: the determinant of the 2x2 matrix whose rows are rs and rt, sx * ty - sy * tx.
auto Allegrex::VDET(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  if(size != 2) return INVALID();
  auto s = vfpuRead(vs, 2, vfpu.pfxs);
  auto t = vfpuRead(vt, 2, vfpu.pfxt);
  f64 determinant = (f64)vfpuFloat(s.lane[0]) * vfpuFloat(t.lane[1]) - (f64)vfpuFloat(s.lane[1]) * vfpuFloat(t.lane[0]);
  Vector d{{vfpuNaN(vfpuBits(determinant), NaNSign::Positive, 0)}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

auto Allegrex::VDIV(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  vfpuBinary(vd, vs, vt, size, [](f32 s, f32 t) { return s / t; }, NaNSign::Product);
}

//vdot: the dot product of rs and rt (each lane of one times the same lane of the other, all added up), into one
//value.
auto Allegrex::VDOT(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto t = vfpuRead(vt, size, vfpu.pfxt);
  f64 sum = 0;
  for(u32 i : range(size)) sum += (f64)vfpuFloat(s.lane[i]) * vfpuFloat(t.lane[i]);
  Vector d{{vfpuNaN(vfpuBits(sum), NaNSign::Positive, 0)}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

//2 to the power of x
auto Allegrex::VEXP2(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, vfpuExp2);
}

//vf2h: pairs of floats into pairs of half floats packed in one lane, so a quad becomes a pair.
auto Allegrex::VF2H(u8 vd, u8 vs, u32 size) -> void {
  if(size != 2 && size != 4) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(size / 2)) d.lane[i] = floatToHalf(s.lane[2 * i]) | floatToHalf(s.lane[2 * i + 1]) << 16;
  vfpuWrite(vd, size / 2, d, vfpu.pfxd);
}

//vf2in, vf2iz, vf2iu, vf2id: floats to integers, scaled by 2^scale first (see toInteger()).
auto Allegrex::VF2I(u8 vd, u8 vs, u32 size, u8 scale, u32 mode) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = toInteger(vfpuFloat(s.lane[i]), scale, mode);
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

auto Allegrex::VFAD(u8 vd, u8 vs, u32 size) -> void {
  if(size == 1) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  f64 sum = 0;
  for(u32 i : range(size)) sum += vfpuFloat(s.lane[i]);
  Vector d{{vfpuNaN(vfpuBits(sum), NaNSign::Positive, 0)}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

//vfim and viim put a constant from the instruction into one register: a half float (vfim), or a 16-bit signed
//integer turned into a float (viim). They write through the destination prefix, and use the prefixes up
//themselves, as their decoder leaves the prefixes alone.
auto Allegrex::VFIM(u8 vd, u16 half) -> void {
  Vector d{{vfpuBits(std::bit_cast<f32>(halfToFloat(half)))}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
  vfpuPrefixesUsed();
}

//vh2f: the reverse of vf2h, each lane's two half floats into two floats, so a pair becomes a quad.
auto Allegrex::VH2F(u8 vd, u8 vs, u32 size) -> void {
  if(size > 2) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(2 * size)) d.lane[i] = halfToFloat(s.lane[i / 2] >> (16 * (i % 2)) & 0xffff);
  vfpuWrite(vd, 2 * size, d, vfpu.pfxd);
}

//vhdp: a "homogeneous" dot product, whose last term is rt's last lane on its own: sx * tx + sy * ty + ... + tw.
//That's how a point (x, y, z, 1) is dotted with a row of a transformation matrix.
auto Allegrex::VHDP(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto t = vfpuRead(vt, size, vfpu.pfxt);
  f64 sum = vfpuFloat(t.lane[size - 1]);
  for(u32 i : range(size - 1)) sum += (f64)vfpuFloat(s.lane[i]) * vfpuFloat(t.lane[i]);
  Vector d{{vfpuNaN(vfpuBits(sum), NaNSign::Positive, 0)}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

//vhtfm2-4: vtfm for a point one lane short, as if its missing last lane were 1, so each result also gets the last
//element of the matrix's column. The size is the matrix's.
auto Allegrex::VHTFM(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  auto m = vfpuReadMatrix(vs, size);
  auto t = vfpuRead(vt, size - 1, PrefixIdentity);
  Vector d{};
  for(u32 i : range(size)) {
    f64 sum = vfpuFloat(m.element[size * (size - 1) + i]);
    for(u32 k : range(size - 1)) sum += (f64)vfpuFloat(m.element[size * k + i]) * vfpuFloat(t.lane[k]);
    d.lane[i] = vfpuNaN(vfpuBits(sum), NaNSign::Positive, 0);
  }
  vfpuWrite(vd, size, d, 0);
}

//vi2c.q and vi2uc.q pack four lanes into four bytes of one lane: vi2c takes each lane's top byte; vi2uc treats
//lanes as positive numbers scaled up by 2^23 and takes the byte below the sign bit, negative ones giving 0.
auto Allegrex::VI2C(u8 vd, u8 vs, u32 size) -> void {
  if(size != 4) return INVALID();
  auto s = vfpuRead(vs, 4, vfpu.pfxs);
  u32 packed = 0;
  for(u32 i : range(4)) packed |= (s.lane[i] >> 24) << (8 * i);
  Vector d{{packed}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

//vi2f: integers to floats, divided by 2^scale.
auto Allegrex::VI2F(u8 vd, u8 vs, u32 size, u8 scale) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = vfpuBits(std::ldexp((f64)s32(s.lane[i]), -(s32)scale));
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//vi2s and vi2us pack pairs of lanes into halfwords, two to a lane: vi2s takes each lane's top halfword; vi2us
//treats lanes as positive numbers scaled up by 2^15, negative ones giving 0.
auto Allegrex::VI2S(u8 vd, u8 vs, u32 size) -> void {
  if(size != 2 && size != 4) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(size / 2)) d.lane[i] = (s.lane[2 * i] >> 16) | (s.lane[2 * i + 1] >> 16) << 16;
  vfpuWrite(vd, size / 2, d, vfpu.pfxd);
}

auto Allegrex::VI2UC(u8 vd, u8 vs, u32 size) -> void {
  if(size != 4) return INVALID();
  auto s = vfpuRead(vs, 4, vfpu.pfxs);
  u32 packed = 0;
  for(u32 i : range(4)) {
    u32 byte = s.lane[i] & 0x8000'0000 ? 0 : s.lane[i] >> 23 & 0xff;
    packed |= byte << (8 * i);
  }
  Vector d{{packed}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

auto Allegrex::VI2US(u8 vd, u8 vs, u32 size) -> void {
  if(size != 2 && size != 4) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto half = [](u32 x) -> u32 { return x & 0x8000'0000 ? 0 : x >> 15 & 0xffff; };
  Vector d{};
  for(u32 i : range(size / 2)) d.lane[i] = half(s.lane[2 * i]) | half(s.lane[2 * i + 1]) << 16;
  vfpuWrite(vd, size / 2, d, vfpu.pfxd);
}

//vidt: the row or column of the identity matrix that rd names: 1 where it crosses the diagonal, 0 elsewhere.
auto Allegrex::VIDT(u8 vd, u32 size) -> void {
  if(size != 2 && size != 4) return INVALID();
  u32 position = vd & 3, start = lineOffset(vd, size);
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = ((start + i) & 3) == position ? One : 0;
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

auto Allegrex::VIIM(u8 vd, s16 value) -> void {
  Vector d{{vfpuBits(value)}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
  vfpuPrefixesUsed();
}

//vlgb.s: the binary exponent of the value (logb: 8 gives 3, 0.5 gives -1); zero and denormals give -infinity.
//A NaN gives a NaN of its sign whose mantissa is the input's low byte moved up 16 bits: so came out all 7 NaNs the
//PSP was given (measured, docs/psp-vfpu-measurements.md, round 2).
auto Allegrex::VLGB(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, [this](u32 s) -> u32 {
    if(FloatParts{s}.nan()) return (s & 0x8000'0000) | 0x7f80'0000 | (s & 0xff) << 16;
    return vfpuBits(std::logb(vfpuFloat(s)));
  });
}

auto Allegrex::VLOG2(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, vfpuLog2);
}

//vmax and vmin: the larger or smaller of each pair of lanes, in vfpuOrder()'s order, the lane coming out as it
//went in. A tie (two zeros or denormals, which count as zero) gives rt's lane.
auto Allegrex::VMAX(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto t = vfpuRead(vt, size, vfpu.pfxt);
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = vfpuOrder(s.lane[i]) > vfpuOrder(t.lane[i]) ? s.lane[i] : t.lane[i];
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//vmfvc and vmtvc move a control register to or from a VFPU register. Unlike the other VFPU instructions they
//leave the prefixes alone (see decoderEXECUTE()), so vmtvc can set one.
auto Allegrex::VMFVC(u8 vd, u8 index) -> void {
  vfpu.r[vd] = vfpuControl(index);
}

auto Allegrex::VMIDT(u8 vd, u32 size) -> void {
  if(size == 1) return INVALID();
  Matrix d{};
  for(u32 i : range(size * size)) d.element[i] = i % size == i / size ? One : 0;
  vfpuWriteMatrix(vd, size, d);
}

auto Allegrex::VMIN(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto t = vfpuRead(vt, size, vfpu.pfxt);
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = vfpuOrder(s.lane[i]) < vfpuOrder(t.lane[i]) ? s.lane[i] : t.lane[i];
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

auto Allegrex::VMMOV(u8 vd, u8 vs, u32 size) -> void {
  if(size == 1) return INVALID();
  vfpuWriteMatrix(vd, size, vfpuReadMatrix(vs, size));
}

//vmmul: a matrix multiplication, rd[r][c] = the sum over k of rs[k][r] * rt[k][c]: rs turned on its side (its
//columns taken as rows) times rt. That's what a PSP does (measured, docs/psp-vfpu-measurements.md, round 2; not the
//order pspdev's documentation gives), and why pspdev's assembler sets rs's transpose bit itself: written
//"vmmul.q M200, M000, M100", it multiplies M000 by M100, the C registers being the matrices' columns.
auto Allegrex::VMMUL(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  if(size == 1) return INVALID();
  auto s = vfpuReadMatrix(vs, size);
  auto t = vfpuReadMatrix(vt, size);
  Matrix d{};
  for(u32 r : range(size)) {
    for(u32 c : range(size)) {
      f64 sum = 0;
      for(u32 k : range(size)) sum += (f64)vfpuFloat(s.element[k * size + r]) * vfpuFloat(t.element[k * size + c]);
      d.element[r * size + c] = vfpuNaN(vfpuBits(sum), NaNSign::Positive, 0);
    }
  }
  vfpuWriteMatrix(vd, size, d);
}

auto Allegrex::VMONE(u8 vd, u32 size) -> void {
  if(size == 1) return INVALID();
  Matrix d{};
  for(u32 i : range(size * size)) d.element[i] = One;
  vfpuWriteMatrix(vd, size, d);
}

//vmov copies the lanes' bits (through the prefixes): a denormal or a NaN comes out as it went in.
auto Allegrex::VMOV(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, [](u32 s) { return s; });
}

//vmscl: a matrix times rt's single value.
auto Allegrex::VMSCL(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  if(size == 1) return INVALID();
  auto m = vfpuReadMatrix(vs, size);
  u32 scale = vfpuRead(vt, 1, PrefixIdentity).lane[0];
  Matrix d{};
  for(u32 i : range(size * size)) {
    u32 product = vfpuBits(vfpuFloat(m.element[i]) * vfpuFloat(scale));
    d.element[i] = vfpuNaN(product, NaNSign::Product, m.element[i], scale);
  }
  vfpuWriteMatrix(vd, size, d);
}

auto Allegrex::VMTVC(u8 index, u8 vs) -> void {
  vfpuSetControl(index, vfpu.r[vs]);
}

auto Allegrex::VMUL(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  vfpuBinary(vd, vs, vt, size, [](f32 s, f32 t) { return s * t; }, NaNSign::Product);
}

auto Allegrex::VMZERO(u8 vd, u32 size) -> void {
  if(size == 1) return INVALID();
  vfpuWriteMatrix(vd, size, Matrix{});
}

auto Allegrex::VNEG(u8 vd, u8 vs, u32 size) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  for(u32 i : range(size)) s.lane[i] ^= 0x8000'0000;
  vfpuWrite(vd, size, s, vfpu.pfxd);
}

//vnop does nothing, but it does use up the prefixes: pspdev's documentation says so.
auto Allegrex::VNOP() -> void {
  vfpuPrefixesUsed();
}

//-1/x
auto Allegrex::VNRCP(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, [](u32 s) { return vfpuReciprocal(s) ^ 0x8000'0000; });
}

//minus the sine, of quarter turns
auto Allegrex::VNSIN(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, [](u32 s) { return vfpuSine(s) ^ 0x8000'0000; });
}

//1 - x ("one's complement" of a value between 0 and 1)
auto Allegrex::VOCP(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnary(vd, vs, size, [](f32 s) { return 1.0f - s; }, NaNSign::Positive);
}

auto Allegrex::VONE(u8 vd, u32 size) -> void {
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = One;
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//vpfxd, vpfxs and vpfxt set the prefixes for the next instruction (see VFPU in allegrex.hpp).
auto Allegrex::VPFXD(u32 prefix) -> void {
  vfpu.pfxd = prefix;
}

auto Allegrex::VPFXS(u32 prefix) -> void {
  vfpu.pfxs = prefix;
}

auto Allegrex::VPFXT(u32 prefix) -> void {
  vfpu.pfxt = prefix;
}

//vqmul.q: the product of two quaternions (the four-number form of a rotation), x, y, z then w.
auto Allegrex::VQMUL(u8 vd, u8 vs, u8 vt, u32) -> void {
  auto s = vfpuRead(vs, 4, vfpu.pfxs);
  auto t = vfpuRead(vt, 4, vfpu.pfxt);
  f64 a[4], b[4];
  for(u32 i : range(4)) {
    a[i] = vfpuFloat(s.lane[i]);
    b[i] = vfpuFloat(t.lane[i]);
  }
  auto bits = [&](f64 value) { return vfpuNaN(vfpuBits(value), NaNSign::Positive, 0); };
  Vector d{{bits(a[3] * b[0] - a[2] * b[1] + a[1] * b[2] + a[0] * b[3]),
            bits(a[3] * b[1] + a[2] * b[0] + a[1] * b[3] - a[0] * b[2]),
            bits(a[3] * b[2] + a[2] * b[3] - a[1] * b[0] + a[0] * b[1]),
            bits(a[3] * b[3] - a[2] * b[2] - a[1] * b[1] - a[0] * b[0])}};
  vfpuWrite(vd, 4, d, vfpu.pfxd);
}

//1/x
auto Allegrex::VRCP(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, vfpuReciprocal);
}

//2 to the power of -x
auto Allegrex::VREXP2(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, vfpuReciprocalExp2);
}

//vrndf1 and vrndf2: random floats from 1 up to 2, or from 2 up to 4 (a random mantissa under a fixed exponent).
//vrndi: random 32-bit integers. Both fill the lanes from the last one back (the first number drawn goes in the
//last lane), and their destination prefix only applies to the last lane, with lane 0's settings: as fp64's data
//and PPSSPP have it.
auto Allegrex::VRNDF(u8 vd, u32 size, u32 exponent) -> void {
  Vector d{};
  for(u32 i = size; i-- > 0;) d.lane[i] = exponent | (vfpuRandom() & 0x7f'ffff);
  u32 last = size - 1;
  vfpuWrite(vd, size, d, (vfpu.pfxd & 0x100) << last | (vfpu.pfxd & 3) << (2 * last));
}

auto Allegrex::VRNDI(u8 vd, u32 size) -> void {
  Vector d{};
  for(u32 i = size; i-- > 0;) d.lane[i] = vfpuRandom();
  u32 last = size - 1;
  vfpuWrite(vd, size, d, (vfpu.pfxd & 0x100) << last | (vfpu.pfxd & 3) << (2 * last));
}

//vrnds.s: seeds the random number generator with rs's bits. Each RCX register gets one half of the seed (the low
//half in registers 0-3, the high half in 4-7) and one of its nibbles above that (register n nibble n).
auto Allegrex::VRNDS(u8 vs, u32) -> void {
  u32 seed = vfpuRead(vs, 1, vfpu.pfxs).lane[0];
  for(u32 n : range(8)) {
    vfpu.rcx[n] = 0x3f80'0000 | (seed >> (4 * n) & 15) << 16 | (seed >> (16 * (n / 4)) & 0xffff);
  }
}

//vrot: the sine and cosine of rs (in quarter turns) placed in the lanes of rd that the placement field names,
//for building rotation matrices a row at a time. Bits 0-1 pick the cosine's lane and bits 2-3 the sine's
//(negated when bit 4 is set); the other lanes get 0. If both name the same lane, that lane gets the cosine and
//every other lane the sine. Its sine and cosine are vsin's and vcos's (not measured on their own).
auto Allegrex::VROT(u8 vd, u8 vs, u32 size, u8 placement) -> void {
  if(size == 1) return INVALID();
  u32 angle = vfpuRead(vs, 1, vfpu.pfxs).lane[0];
  u32 sine = vfpuSine(angle), cosine = vfpuCosine(angle);
  if(placement & 0x10) sine ^= 0x8000'0000;
  u32 cosineLane = placement & 3, sineLane = placement >> 2 & 3;
  Vector d{};
  for(u32 i : range(size)) d.lane[i] = i == cosineLane ? cosine : cosineLane == sineLane || i == sineLane ? sine : 0;
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//1/sqrt(x)
auto Allegrex::VRSQ(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, vfpuReciprocalSqrt);
}

//vs2i and vus2i: each lane's two halfwords into two lanes, as the top halfword of an integer (vs2i), or scaled up
//by 2^15 as a positive number (vus2i), so a pair becomes a quad.
auto Allegrex::VS2I(u8 vd, u8 vs, u32 size) -> void {
  if(size > 2) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(2 * size)) d.lane[i] = (s.lane[i / 2] >> (16 * (i % 2)) & 0xffff) << 16;
  vfpuWrite(vd, 2 * size, d, vfpu.pfxd);
}

//vsat0 clamps each lane to 0..1, vsat1 to -1..1, as a PSP does (measured, docs/psp-vfpu-measurements.md, round 2):
//a NaN comes out as it went in, and so does anything already in the range, denormals included; but vsat0 makes
//anything with its sign bit set 0, -0 and negative denormals too.
auto Allegrex::VSAT0(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, [](u32 s) -> u32 {
    if(FloatParts{s}.nan()) return s;
    if(s >> 31) return 0;
    return vfpuOrder(s) > vfpuOrder(One) ? One : s;
  });
}

auto Allegrex::VSAT1(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, [](u32 s) -> u32 {
    if(FloatParts{s}.nan()) return s;
    if(vfpuOrder(s) > vfpuOrder(One)) return One;
    if(vfpuOrder(s) < -vfpuOrder(One)) return One | 0x8000'0000;  //-1
    return s;
  });
}

//vsbn.s: rs with its exponent replaced by rt's integer value (plus the float format's bias of 127), so rs's
//mantissa scaled to 2^rt. Zero, infinity and NaN stay as they are.
auto Allegrex::VSBN(u8 vd, u8 vs, u8 vt, u32) -> void {
  u32 s = vfpuRead(vs, 1, vfpu.pfxs).lane[0];
  u32 t = vfpuRead(vt, 1, vfpu.pfxt).lane[0];
  u32 exponent = s >> 23 & 0xff;
  if(exponent != 0 && exponent != 0xff) s = (s & 0x807f'ffff) | ((t + 127) & 0xff) << 23;
  Vector d{{s}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

//vsbz.s: rs with its exponent set to 0, giving its mantissa as a value from 1 up to 2. Zero, denormals (measured,
//docs/psp-vfpu-measurements.md, round 2) and NaN stay as they are.
auto Allegrex::VSBZ(u8 vd, u8 vs, u32) -> void {
  u32 s = vfpuRead(vs, 1, vfpu.pfxs).lane[0];
  u32 exponent = s >> 23 & 0xff;
  bool keep = exponent == 0 || (exponent == 0xff && (s & 0x7f'ffff));
  Vector d{{keep ? s : (s & 0x007f'ffff) | One}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

//vscl: rs times rt's single value.
auto Allegrex::VSCL(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  u32 scale = vfpuRead(vt, 1, vfpu.pfxt).lane[0];
  Vector d{};
  for(u32 i : range(size)) {
    u32 product = vfpuBits(vfpuFloat(s.lane[i]) * vfpuFloat(scale));
    d.lane[i] = vfpuNaN(product, NaNSign::Product, s.lane[i], scale);
  }
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//vscmp: for each lane, 1 where s is above t, -1 where it's below, 0 where neither, in vfpuOrder()'s order (so a
//NaN is past the infinity of its sign, and a denormal is zero).
auto Allegrex::VSCMP(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto t = vfpuRead(vt, size, vfpu.pfxt);
  Vector d{};
  for(u32 i : range(size)) {
    s64 a = vfpuOrder(s.lane[i]), b = vfpuOrder(t.lane[i]);
    d.lane[i] = a > b ? One : a < b ? One | 0x8000'0000 : 0;
  }
  vfpuWrite(vd, size, d, vfpu.pfxd);
}

//vsge and vslt: 1 where s >= t (or s < t), else 0.
auto Allegrex::VSGE(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  vfpuBinary(vd, vs, vt, size, [](f32 s, f32 t) { return s >= t ? 1.0f : 0.0f; });
}

//vsgn: the sign of each lane, -1, 0 or 1, as vscmp against zero has it (so a NaN gets its sign bit's, and a
//denormal is 0).
auto Allegrex::VSGN(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, [](u32 s) -> u32 {
    s64 a = vfpuOrder(s);
    return a > 0 ? One : a < 0 ? One | 0x8000'0000 : 0;
  });
}

//sine, of quarter turns
auto Allegrex::VSIN(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, vfpuSine);
}

auto Allegrex::VSLT(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  vfpuBinary(vd, vs, vt, size, [](f32 s, f32 t) { return s < t ? 1.0f : 0.0f; });
}

//vsocp.s and vsocp.p: each lane x into two, 1 - x and x, each clamped to 0..1. A NaN gives the VFPU's NaN in
//both (measured, docs/psp-vfpu-measurements.md, round 2), where clamping would have made it a number.
auto Allegrex::VSOCP(u8 vd, u8 vs, u32 size) -> void {
  if(size > 2) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  auto clamp = [](f32 x) { return std::fmin(std::fmax(x, 0.0f), 1.0f); };
  Vector d{};
  for(u32 i : range(size)) {
    f32 x = vfpuFloat(s.lane[i]);
    bool nan = std::isnan(x);
    d.lane[2 * i] = nan ? 0x7f80'0001 : vfpuBits(clamp(1.0f - x));
    d.lane[2 * i + 1] = nan ? 0x7f80'0001 : vfpuBits(clamp(x));
  }
  vfpuWrite(vd, 2 * size, d, vfpu.pfxd);
}

auto Allegrex::VSQRT(u8 vd, u8 vs, u32 size) -> void {
  vfpuUnaryBits(vd, vs, size, vfpuSqrt);
}

//vsrt1-vsrt4.q: the four passes of a sorting network over a quad's lanes, each putting pairs of lanes in order
//(the smaller first for vsrt1 and vsrt2, the larger first for vsrt3 and vsrt4), in vfpuOrder()'s order, the lanes
//coming out as they went in. The two lanes of a pair are worked out separately, so a tie (two zeros or denormals)
//puts the same lane in both: the pair's first for vsrt1 and vsrt2, its second for vsrt3 and vsrt4 (measured,
//docs/psp-vfpu-measurements.md, round 2).
auto Allegrex::VSRT(u8 vd, u8 vs, u32 size, u32 pass) -> void {
  if(size != 4) return INVALID();
  auto s = vfpuRead(vs, 4, vfpu.pfxs);
  auto& x = s.lane;
  //the smaller and the larger of a and b, a on a tie
  auto lower = [](u32 a, u32 b) { return vfpuOrder(b) < vfpuOrder(a) ? b : a; };
  auto upper = [](u32 a, u32 b) { return vfpuOrder(a) < vfpuOrder(b) ? b : a; };
  Vector d{};
  switch(pass) {
  case 1: d = {{lower(x[0], x[1]), upper(x[0], x[1]), lower(x[2], x[3]), upper(x[2], x[3])}}; break;
  case 2: d = {{lower(x[0], x[3]), lower(x[1], x[2]), upper(x[1], x[2]), upper(x[0], x[3])}}; break;
  case 3: d = {{upper(x[1], x[0]), lower(x[1], x[0]), upper(x[3], x[2]), lower(x[3], x[2])}}; break;
  default: d = {{upper(x[3], x[0]), upper(x[2], x[1]), lower(x[2], x[1]), lower(x[3], x[0])}}; break;
  }
  vfpuWrite(vd, 4, d, vfpu.pfxd);
}

auto Allegrex::VSUB(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  vfpuBinary(vd, vs, vt, size, [](f32 s, f32 t) { return s - t; }, NaNSign::Positive);
}

//vsync and vflush wait for the VFPU's pipeline, or its writes to memory, to finish: there's nothing to wait for
//here. pspdev's documentation doesn't say whether they use up the prefixes; they don't in PPSSPP, nor here. (PPSSPP
//keeps the prefixes through vnop too, but the documentation, which rests on tests on hardware, says otherwise.)
auto Allegrex::VSYNC() -> void {
}

//vt4444.q, vt5551.q, vt5650.q: four 32-bit colors (8 bits each of red, green, blue and alpha) into four 16-bit
//colors, two to a lane, in the formats the PSP's graphics chip reads: 4 bits a channel, 5 bits a color and 1 of
//alpha, or 5, 6 and 5 bits of color and no alpha.
auto Allegrex::VT4444(u8 vd, u8 vs, u32 size) -> void {
  if(size != 4) return INVALID();
  auto s = vfpuRead(vs, 4, vfpu.pfxs);
  auto pack = [](u32 c) -> u32 { return (c >> 4 & 0xf) | (c >> 8 & 0xf0) | (c >> 12 & 0xf00) | (c >> 16 & 0xf000); };
  Vector d{{pack(s.lane[0]) | pack(s.lane[1]) << 16, pack(s.lane[2]) | pack(s.lane[3]) << 16}};
  vfpuWrite(vd, 2, d, vfpu.pfxd);
}

auto Allegrex::VT5551(u8 vd, u8 vs, u32 size) -> void {
  if(size != 4) return INVALID();
  auto s = vfpuRead(vs, 4, vfpu.pfxs);
  auto pack = [](u32 c) -> u32 { return (c >> 3 & 0x1f) | (c >> 6 & 0x3e0) | (c >> 9 & 0x7c00) | (c >> 16 & 0x8000); };
  Vector d{{pack(s.lane[0]) | pack(s.lane[1]) << 16, pack(s.lane[2]) | pack(s.lane[3]) << 16}};
  vfpuWrite(vd, 2, d, vfpu.pfxd);
}

auto Allegrex::VT5650(u8 vd, u8 vs, u32 size) -> void {
  if(size != 4) return INVALID();
  auto s = vfpuRead(vs, 4, vfpu.pfxs);
  auto pack = [](u32 c) -> u32 { return (c >> 3 & 0x1f) | (c >> 5 & 0x7e0) | (c >> 8 & 0xf800); };
  Vector d{{pack(s.lane[0]) | pack(s.lane[1]) << 16, pack(s.lane[2]) | pack(s.lane[3]) << 16}};
  vfpuWrite(vd, 2, d, vfpu.pfxd);
}

//vtfm2-4: a vector transformed by a matrix: result lane i is the matrix's column i (its C register) dotted with rt,
//as a PSP does it (measured, docs/psp-vfpu-measurements.md, round 2). So a matrix whose columns are in C registers
//transforms by its transpose, and naming it transposed (E000 for M000) gives the usual matrix-times-vector. This is
//how vertices are moved, rotated and projected. The size is the matrix's.
auto Allegrex::VTFM(u8 vd, u8 vs, u8 vt, u32 size) -> void {
  auto m = vfpuReadMatrix(vs, size);
  auto t = vfpuRead(vt, size, PrefixIdentity);
  Vector d{};
  for(u32 i : range(size)) {
    f64 sum = 0;
    for(u32 k : range(size)) sum += (f64)vfpuFloat(m.element[size * k + i]) * vfpuFloat(t.lane[k]);
    d.lane[i] = vfpuNaN(vfpuBits(sum), NaNSign::Positive, 0);
  }
  vfpuWrite(vd, size, d, 0);
}

//vuc2ifs.s: rs's four bytes into four lanes as positive numbers scaled up by 2^31: each byte repeated down the
//lane's bits, so 0xff becomes nearly 2^31.
auto Allegrex::VUC2I(u8 vd, u8 vs, u32) -> void {
  auto s = vfpuRead(vs, 1, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(4)) {
    u32 byte = s.lane[0] >> (8 * i) & 0xff;
    d.lane[i] = byte << 23 | byte << 15 | byte << 7 | byte >> 1;
  }
  vfpuWrite(vd, 4, d, vfpu.pfxd);
}

auto Allegrex::VUS2I(u8 vd, u8 vs, u32 size) -> void {
  if(size > 2) return INVALID();
  auto s = vfpuRead(vs, size, vfpu.pfxs);
  Vector d{};
  for(u32 i : range(2 * size)) d.lane[i] = ((s.lane[i / 2] >> (16 * (i % 2)) & 0xffff) << 15) & 0x7fff'ffff;
  vfpuWrite(vd, 2 * size, d, vfpu.pfxd);
}

//vwbn.s: rs rescaled so its exponent is the instruction's scale field, shifting its mantissa (with the leading 1
//made visible) to match. Zero, infinity and NaN just get the new exponent.
auto Allegrex::VWBN(u8 vd, u8 vs, u32, u8 scale) -> void {
  u32 s = vfpuRead(vs, 1, vfpu.pfxs).lane[0];
  u32 exponent = s >> 23 & 0xff;
  u32 result;
  if(exponent == 0 || exponent == 0xff) {
    result = s | u32(scale) << 23;
  } else {
    u32 mantissa = (s & 0x7f'ffff) | 0x80'0000;
    if(scale > exponent) mantissa >>= (scale - exponent) & 15;
    else mantissa <<= (exponent - scale) & 15;
    result = (s & 0x8000'0000) | (mantissa & 0x7f'ffff) | u32(scale) << 23;
  }
  Vector d{{result}};
  vfpuWrite(vd, 1, d, vfpu.pfxd);
}

auto Allegrex::VZERO(u8 vd, u32 size) -> void {
  vfpuWrite(vd, size, Vector{}, vfpu.pfxd);
}
