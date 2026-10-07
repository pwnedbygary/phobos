//Exact arithmetic: what the software renderer (ares/psp/ge) works out in floating point, worked out here to the
//very same bits.
//
//The software renderer blends a triangle's colors, depth, fog and texture coordinates in floats, each step rounded
//as the host's CPU rounds it (IEEE 754 single precision: to the nearest float, ties to the even one). Most of those
//values then become whole numbers (a color's level, a depth, the texel a coordinate falls in), so a result a unit
//in its last place below a whole number (254.99998 for 255) comes out a level apart. A GPU rounds additions and
//multiplications the same way, but not necessarily the rest:
//  - Division: Vulkan lets a GPU's be 2.5 units in the last place off (Apple's GPUs through MoltenVK, with its fast
//    math, are a unit or two off in a third of quotients). divide() works the nearest quotient out from the GPU's.
//  - Fused multiply-adds: on ARM64 the host's compiler fuses a product into the sum it's part of (one rounding for
//    both), on x86-64 it doesn't (the fused constant says which this host does); a GPU's own fma() needn't be
//    fused at all. fmaExact() is, on every GPU: the GPU's own where it was found to be (the nativeFma constant),
//    else built from additions and multiplications.
//  - Conversions: a 64-bit whole number to a float (a triangle's edge functions), and floats to whole numbers held
//    the way the host's CPU holds them.
//  - Everything else is written so that nothing may be reordered or fused behind its back: every variable that
//    takes part is precise (GLSL's way of saying so; SPIR-V's NoContraction).
//The checks for numbers that aren't numbers look at their bits, as a GPU may assume floats are always numbers.

//64-bit whole numbers, as two 32-bit halves (x the low, y the high, two's complement): GLSL ES has none of its own.
uvec2 wide(int value) { return uvec2(uint(value), value < 0 ? 0xffffffffu : 0u); }
uvec2 wideAdd(uvec2 a, uvec2 b) {
  uint carry;
  uint low = uaddCarry(a.x, b.x, carry);
  return uvec2(low, a.y + b.y + carry);
}
uvec2 wideNegate(uvec2 a) {
  uint carry;
  uint low = uaddCarry(~a.x, 1u, carry);
  return uvec2(low, ~a.y + carry);
}
uvec2 wideProduct(int a, int b) {
  int high, low;
  imulExtended(a, b, high, low);
  return uvec2(uint(low), uint(high));
}
bool wideNegative(uvec2 a) { return (a.y >> 31) != 0u; }
bool wideZero(uvec2 a) { return (a.x | a.y) == 0u; }
//a >> shift (0-63), of the magnitude (no sign to keep)
uvec2 wideShiftRight(uvec2 a, int shift) {
  if(shift == 0) return a;
  if(shift >= 32) return uvec2(a.y >> uint(shift - 32), 0u);
  return uvec2(a.x >> uint(shift) | a.y << uint(32 - shift), a.y >> uint(shift));
}
uint wideBit(uvec2 a, int bit) { return bit < 32 ? a.x >> uint(bit) & 1u : a.y >> uint(bit - 32) & 1u; }
//whether any of bits 0 to count - 1 is set
bool wideAnyBelow(uvec2 a, int count) {
  if(count <= 0) return false;
  if(count < 32) return (a.x & ((1u << uint(count)) - 1u)) != 0u;
  if(count == 32) return a.x != 0u;
  return a.x != 0u || (a.y & ((1u << uint(count - 32)) - 1u)) != 0u;
}

//The nearest float to a 64-bit whole number, ties to the even one: what the host's conversion gives. (Below 2^24
//every whole number is a float already.)
float wideFloat(uvec2 a) {
  bool negative = wideNegative(a);
  uvec2 magnitude = negative ? wideNegate(a) : a;
  if(magnitude.y == 0u && magnitude.x < 0x1000000u) {
    float exact = float(magnitude.x);
    return negative ? -exact : exact;
  }
  int top = magnitude.y != 0u ? 32 + findMSB(magnitude.y) : findMSB(magnitude.x);
  int dropped = top - 23;  //the bits below a float's 24
  uint kept = wideShiftRight(magnitude, dropped).x;
  if(wideBit(magnitude, dropped - 1) == 1u && (wideAnyBelow(magnitude, dropped - 1) || (kept & 1u) == 1u)) kept++;
  if(kept == 0x1000000u) kept >>= 1, dropped++;  //(rounded up to the next power of two)
  float result = ldexp(float(kept), dropped);
  return negative ? -result : result;
}

bool notANumber(float value) { return (floatBitsToUint(value) & 0x7fffffffu) > 0x7f800000u; }
bool finiteNumber(float value) { return (floatBitsToUint(value) & 0x7f800000u) != 0x7f800000u; }

//Error-free steps (Knuth's and Dekker's): each gives a rounded result and exactly what rounding left out, using only
//additions and multiplications, each rounded to the nearest. For fmaExact() where the GPU's fma() isn't fused.
void twoSum(float a, float b, out float sum, out float error) {
  precise float s = a + b;
  precise float bPart = s - a;
  precise float aPart = s - bPart;
  precise float e = (a - aPart) + (b - bPart);
  sum = s, error = e;
}
void splitFloat(float a, out float high, out float low) {  //Veltkamp's: a's top 12 bits, and the rest
  precise float c = 4097.0 * a;
  precise float h = c - (c - a);
  precise float l = a - h;
  high = h, low = l;
}
void twoProduct(float a, float b, out float product, out float error) {
  precise float p = a * b;
  float aHigh, aLow, bHigh, bLow;
  splitFloat(a, aHigh, aLow);
  splitFloat(b, bHigh, bLow);
  precise float e = ((aHigh * bHigh - p) + aHigh * bLow + aLow * bHigh) + aLow * bLow;
  product = p, error = e;
}
//a + b rounded to odd: exact where it's a float, else the neighbour on its far side whose last bit is 1. (Adjacent
//floats of a sign have adjacent bit patterns, so their last bits alternate.)
float addToOdd(float a, float b) {
  float sum, error;
  twoSum(a, b, sum, error);
  uint bits = floatBitsToUint(sum);
  if(error == 0.0 || (bits & 1u) == 1u) return sum;
  bool grows = (error > 0.0) == ((bits >> 31) == 0u);  //the exact sum lies further from zero than the rounded one
  return uintBitsToFloat(grows ? bits + 1u : bits - 1u);
}

//a * b + c with one rounding, to the nearest (ties to even). The emulation is Boldo and Melquiond's ("Emulation of
//FMA and correctly rounded sums: proved algorithms using rounding to odd", 2008): the exact product as two floats,
//the exact sum of its high part and c as two more, the two low parts added rounded to odd, and the last sum rounded
//to the nearest; exact while nothing overflows or falls below the normal floats, which the GE's values don't.
float fmaExact(float a, float b, float c) {
  if(nativeFma != 0u) {
    precise float fused = fma(a, b, c);
    return fused;
  }
  precise float product = a * b;
  precise float plain = product + c;
  if(!finiteNumber(plain) || !finiteNumber(product)) return plain;
  float productHigh, productLow, sumHigh, sumLow;
  twoProduct(a, b, productHigh, productLow);
  twoSum(c, productHigh, sumHigh, sumLow);
  float odd = addToOdd(sumLow, productLow);
  precise float result = sumHigh + odd;
  return result;
}

//a / b rounded to the nearest, ties to the even one, as the host divides. The GPU's quotient is brought within a
//unit in the last place by one step from its remainder; then of it and its two neighbours, the one whose remainder
//a - q * b is least is the nearest (each such remainder fits a float, so fmaExact() gives it exactly), the even one
//where two are as near. Zeros, infinities and what isn't a number come out of the GPU's own division as IEEE 754 has
//them.
float divide(float a, float b) {
  precise float q = a / b;
  if(a == 0.0 || q == 0.0 || !finiteNumber(q) || !finiteNumber(b)) return q;
  precise float inverse = 1.0 / b;
  q = fmaExact(fmaExact(-q, b, a), inverse, q);
  uint bits = floatBitsToUint(q);
  float lower = uintBitsToFloat(bits - 1u), higher = uintBitsToFloat(bits + 1u);
  precise float best = q;
  precise float least = abs(fmaExact(-q, b, a));
  precise float lowerLeft = abs(fmaExact(-lower, b, a));
  precise float higherLeft = abs(fmaExact(-higher, b, a));
  if(lowerLeft < least || (lowerLeft == least && (floatBitsToUint(lower) & 1u) == 0u)) {
    best = lower, least = lowerLeft;
  }
  if(higherLeft < least || (higherLeft == least && (floatBitsToUint(higher) & 1u) == 0u)) best = higher;
  return best;
}

//first * w0 + second * w1 + third * w2, rounded as the host's compiler rounds the C++ expression: fused (ARM64,
//clang), the middle product by itself, then the first and the third fused into it in turn; else every product and
//sum rounded apart, left to right. (raster.cpp's blendColor() and blend(), and the perspective's sums.)
float sum3(float first, float second, float third, float w0, float w1, float w2) {
  precise float sum;
  if(fused != 0u) {
    sum = second * w1;
    sum = fmaExact(first, w0, sum);
    sum = fmaExact(third, w2, sum);
  } else {
    precise float a = first * w0;
    precise float b = second * w1;
    precise float c = third * w2;
    sum = (a + b) + c;
  }
  return sum;
}
//(first * w0 + second * w1 + third * w2) / total
float blend3(float first, float second, float third, float w0, float w1, float w2, float total) {
  return divide(sum3(first, second, third, w0, w1, w2), total);
}

//s32(value), as ARM64 converts it: toward zero, held to the 32-bit numbers, 0 for what isn't a number
int truncated(float value) {
  if(notANumber(value)) return 0;
  if(value >= 2147483648.0) return 2147483647;
  if(value < -2147483648.0) return -2147483647 - 1;
  return int(value);
}
//u32(std::clamp(value, 0.0f, 65535.0f)): a depth, 0 for what isn't a number
uint depthOf(float value) {
  if(notANumber(value)) return 0u;
  return uint(clamp(value, 0.0, 65535.0));
}
//The fog at a pixel, 0-255, from its 0-1 (draw.cpp's fogAmount()): 0 with the sign bit set, 255 from 1 up (and for
//what isn't a number), else rounded down in 256ths.
uint fogAmount(float fog) {
  if((floatBitsToUint(fog) >> 31) != 0u) return 0u;
  if(!finiteNumber(fog) || fog >= 1.0) return 255u;
  precise float scaled = fog * 256.0;
  return uint(scaled);
}
