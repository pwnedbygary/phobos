//The FPU (coprocessor 1): math on single-precision floats, numbers with a fractional part like 1.5 or -0.25.
//
//Its registers hold raw bit patterns (see FPU in allegrex.hpp); f() and setF() read and write them as floats. Its
//control/status register (fpu.csr, FCR31) holds:
//  - bits 0-1, the rounding mode: 0 to the nearest (ties to even), 1 toward zero, 2 up, 3 down. The arithmetic and
//    the conversions to integers follow it, as they do on a PSP (measured: docs/psp-vfpu-measurements.md, round 3);
//    cvt.s.w is taken to follow it too, as MIPS documents (not measured);
//  - bits 2-17, the exceptions' flags, enables and causes: kept as written, but nothing here sets or acts on them.
//    On a PSP each operation sets them as IEEE says (but overflow without inexact, and underflow even for an exact
//    result), and a program starts with FCSR 0x00000e00: the overflow, divide-by-zero and invalid exceptions enabled,
//    so an operation raising one ends the program, which no working game does;
//  - bit 23, the condition the last compare set, which bc1t and bc1f branch on;
//  - bit 24, flush to zero: subnormals (numbers too tiny for the normal format) count as zero, in and out, in every
//    rounding mode here (MIPS documents the smallest normal number instead when rounding toward it, which no round
//    has measured).
//NaNs ("not a number", what 0/0 gives) follow IEEE 754-2008: a quiet NaN has its top fraction bit set. Which NaN an
//operation gives is picked here (setArithmetic), not left to the host, since hosts differ.

auto Allegrex::f(u32 index) const -> f32 {
  return flushed(std::bit_cast<f32>(fpu.r[index]));
}

auto Allegrex::setF(u32 index, f32 value) -> void {
  fpu.r[index] = std::bit_cast<u32>(flushed(value));
}

auto Allegrex::flushed(f32 value) const -> f32 {
  if(fpu.csr.bit(24) && std::fpclassify(value) == FP_SUBNORMAL) return std::copysign(0.0f, value);
  return value;
}

//Converts a float to a 32-bit integer, rounding as the mode says (the same numbering as csr bits 0-1). Values that
//don't fit become the end of the range they fell off: 0x7fff'ffff for numbers too big, and NaN, and 0x8000'0000 for
//numbers too negative, -infinity among them (measured on a PSP, round 3; MIPS documents 0x7fff'ffff for all). The
//rounding is done in double precision, which holds any float exactly.
auto Allegrex::toWord(f32 value, u32 mode) const -> u32 {
  f64 rounded;
  switch(mode & 3) {
  case 0: {  //nearest; a value exactly halfway goes to the even neighbour (2.5 becomes 2, 3.5 becomes 4)
    f64 whole = std::floor((f64)value);
    f64 fraction = (f64)value - whole;
    if(fraction > 0.5 || (fraction == 0.5 && std::fmod(whole, 2.0) != 0.0)) whole += 1.0;
    rounded = whole;
    break;
  }
  case 1: rounded = std::trunc((f64)value); break;
  case 2: rounded = std::ceil((f64)value); break;
  default: rounded = std::floor((f64)value); break;
  }
  if(std::isnan(rounded) || rounded > 2147483647.0) return 0x7fff'ffff;
  if(rounded < -2147483648.0) return 0x8000'0000;
  return s32(rounded);
}

//The host's names for the four rounding modes, in csr's order.
static const int HostRoundings[4] = {FE_TONEAREST, FE_TOWARDZERO, FE_UPWARD, FE_DOWNWARD};

//operation(s, t) rounded in csr's mode. Programs nearly always leave the mode at the nearest, the host's own, which
//costs nothing; for the others the host's mode is switched for the one operation. The compiler doesn't know that
//the switch changes how arithmetic rounds, so the inputs and the result go through volatile variables, which keeps
//the arithmetic between the switches.
template<typename Value, typename Operation>
auto Allegrex::rounded(Operation operation, Value s, Value t) const -> f32 {
  if(!(fpu.csr & 3)) return operation(s, t);
  int host = std::fegetround();
  std::fesetround(HostRoundings[fpu.csr & 3]);
  volatile Value a = s, b = t;
  volatile f32 result = operation(a, b);
  std::fesetround(host);
  return result;
}

//The NaN an arithmetic instruction gives, from its inputs' bits: the first signaling NaN among them (s, then t) made
//quiet, else the first quiet NaN, else 0x7fc00000, for a NaN made from numbers (0 / 0, infinity - infinity, the
//square root of a negative number). That's every NaN the PSP gave in fpu-arith.bin (round 3). ARM hosts happen to
//do the same; x86 hosts give 0xffc00000 for an invalid operation and pass on their first operand's NaN.
static auto pickNaN(u32 s, u32 t) -> u32 {
  auto isNaN = [](u32 x) { return (x & 0x7f80'0000) == 0x7f80'0000 && (x & 0x007f'ffff); };
  auto isQuiet = [](u32 x) { return bool(x & 0x0040'0000); };
  if(isNaN(s) && !isQuiet(s)) return s | 0x0040'0000;
  if(isNaN(t) && !isQuiet(t)) return t | 0x0040'0000;
  if(isNaN(s)) return s;
  if(isNaN(t)) return t;
  return 0x7fc0'0000;
}

//An arithmetic instruction's result into fd: the value, or if it isn't a number, the NaN pickNaN() gives for its
//inputs s and t (their bits).
auto Allegrex::setArithmetic(u8 fd, f32 value, u32 s, u32 t) -> void {
  if(std::isnan(value)) fpu.r[fd] = pickNaN(s, t);
  else setF(fd, value);
}

//abs, neg and mov only touch bits (the sign, or nothing), so NaN payloads survive them unchanged.
auto Allegrex::FABS(u8 fd, u8 fs) -> void {
  fpu.r[fd] = fpu.r[fs] & 0x7fff'ffff;
}

auto Allegrex::FADD(u8 fd, u8 fs, u8 ft) -> void {
  setArithmetic(fd, rounded([](f32 s, f32 t) { return s + t; }, f(fs), f(ft)), fpu.r[fs], fpu.r[ft]);
}

auto Allegrex::FCEIL(u8 fd, u8 fs) -> void {
  fpu.r[fd] = toWord(f(fs), 2);
}

//c.cond.s compares two floats and stores the answer in csr bit 23. The condition's low three bits say which
//outcomes count as true: bit 0 "unordered" (either side is NaN, so neither is less, equal or greater), bit 1 equal,
//bit 2 less than. So c.lt.s is "less than", c.ule.s "unordered, less or equal", and so on.
auto Allegrex::FCOMPARE(u8 fs, u8 ft, u8 condition) -> void {
  f32 s = f(fs), t = f(ft);
  bool unordered = std::isnan(s) || std::isnan(t);
  bool result = unordered ? condition & 1 : (condition & 2 && s == t) || (condition & 4 && s < t);
  fpu.csr.bit(23) = result;
}

//An integer past 2^24 doesn't fit a float's 24 bits exactly, so this rounds too.
auto Allegrex::FCVTSW(u8 fd, u8 fs) -> void {
  setF(fd, rounded([](s32 s, s32) { return (f32)s; }, s32(fpu.r[fs]), 0));
}

auto Allegrex::FCVTWS(u8 fd, u8 fs) -> void {
  fpu.r[fd] = toWord(f(fs), fpu.csr);
}

auto Allegrex::FDIV(u8 fd, u8 fs, u8 ft) -> void {
  setArithmetic(fd, rounded([](f32 s, f32 t) { return s / t; }, f(fs), f(ft)), fpu.r[fs], fpu.r[ft]);
}

auto Allegrex::FFLOOR(u8 fd, u8 fs) -> void {
  fpu.r[fd] = toWord(f(fs), 3);
}

auto Allegrex::FMOV(u8 fd, u8 fs) -> void {
  fpu.r[fd] = fpu.r[fs];
}

auto Allegrex::FMUL(u8 fd, u8 fs, u8 ft) -> void {
  setArithmetic(fd, rounded([](f32 s, f32 t) { return s * t; }, f(fs), f(ft)), fpu.r[fs], fpu.r[ft]);
}

auto Allegrex::FNEG(u8 fd, u8 fs) -> void {
  fpu.r[fd] = fpu.r[fs] ^ 0x8000'0000;
}

auto Allegrex::FROUND(u8 fd, u8 fs) -> void {
  fpu.r[fd] = toWord(f(fs), 0);
}

auto Allegrex::FSQRT(u8 fd, u8 fs) -> void {
  setArithmetic(fd, rounded([](f32 s, f32) { return std::sqrt(s); }, f(fs), 0.0f), fpu.r[fs], 0);
}

auto Allegrex::FSUB(u8 fd, u8 fs, u8 ft) -> void {
  setArithmetic(fd, rounded([](f32 s, f32 t) { return s - t; }, f(fs), f(ft)), fpu.r[fs], fpu.r[ft]);
}

auto Allegrex::FTRUNC(u8 fd, u8 fs) -> void {
  fpu.r[fd] = toWord(f(fs), 1);
}

//Branch on the compare condition: bc1t when it's true, bc1f when false, and their likely forms.
auto Allegrex::BC1(bool value, bool likely, s16 imm) -> void {
  bool taken = fpu.csr.bit(23) == value;
  if(taken) return branch(ipu.pc + imm * 4);
  if(likely) skipDelaySlot();
}

//Control register 31 is csr, and 0 is FIR, which says what FPU it is: 0x00003351 on a PSP (measured, round 3). The
//others read as zero and ignore writes.
auto Allegrex::CFC1(u32& rt, u8 rd) -> void {
  rt = rd == 31 ? (u32)fpu.csr : rd == 0 ? 0x0000'3351 : 0;
}

auto Allegrex::CTC1(cu32& rt, u8 rd) -> void {
  if(rd == 31) fpu.csr = rt;
}

auto Allegrex::LWC1(u8 ft, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressLoad, address);
  fpu.r[ft] = read(Word, address);
}

//Moves between the integer and float registers copy bits; nothing is converted.
auto Allegrex::MFC1(u32& rt, u8 fs) -> void {
  rt = fpu.r[fs];
}

auto Allegrex::MTC1(cu32& rt, u8 fs) -> void {
  fpu.r[fs] = rt;
}

auto Allegrex::SWC1(u8 ft, cu32& rs, s16 imm) -> void {
  u32 address = rs + imm;
  if(address & 3) return addressError(Exception::AddressStore, address);
  write(Word, address, fpu.r[ft]);
}
