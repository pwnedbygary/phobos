//The FPU (coprocessor 1): math on single-precision floats, numbers with a fractional part like 1.5 or -0.25.
//
//Its registers hold raw bit patterns (see FPU in allegrex.hpp); f() and setF() read and write them as floats. Its
//control/status register (fpu.csr, FCR31) holds:
//  - bits 0-1, the rounding mode for cvt.w.s: 0 to the nearest (ties to even), 1 toward zero, 2 up, 3 down;
//  - bit 23, the condition the last compare set, which bc1t and bc1f branch on;
//  - bit 24, flush to zero: subnormals (numbers too tiny for the normal format) count as zero, in and out.

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
//don't fit, and NaN ("not a number", what 0/0 gives), become 0x7fff'ffff: MIPS's answer for an invalid conversion
//when its exception is off. The rounding is done in double precision, which holds any float exactly.
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
  if(std::isnan(rounded) || rounded > 2147483647.0 || rounded < -2147483648.0) return 0x7fff'ffff;
  return s32(rounded);
}

//abs, neg and mov only touch bits (the sign, or nothing), so NaN payloads survive them unchanged.
auto Allegrex::FABS(u8 fd, u8 fs) -> void {
  fpu.r[fd] = fpu.r[fs] & 0x7fff'ffff;
}

//Arithmetic rounds to the nearest, whatever the mode bits say; only cvt.w.s reads them here.
auto Allegrex::FADD(u8 fd, u8 fs, u8 ft) -> void {
  setF(fd, f(fs) + f(ft));
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

auto Allegrex::FCVTSW(u8 fd, u8 fs) -> void {
  setF(fd, (f32)s32(fpu.r[fs]));
}

auto Allegrex::FCVTWS(u8 fd, u8 fs) -> void {
  fpu.r[fd] = toWord(f(fs), fpu.csr);
}

auto Allegrex::FDIV(u8 fd, u8 fs, u8 ft) -> void {
  setF(fd, f(fs) / f(ft));
}

auto Allegrex::FFLOOR(u8 fd, u8 fs) -> void {
  fpu.r[fd] = toWord(f(fs), 3);
}

auto Allegrex::FMOV(u8 fd, u8 fs) -> void {
  fpu.r[fd] = fpu.r[fs];
}

auto Allegrex::FMUL(u8 fd, u8 fs, u8 ft) -> void {
  setF(fd, f(fs) * f(ft));
}

auto Allegrex::FNEG(u8 fd, u8 fs) -> void {
  fpu.r[fd] = fpu.r[fs] ^ 0x8000'0000;
}

auto Allegrex::FROUND(u8 fd, u8 fs) -> void {
  fpu.r[fd] = toWord(f(fs), 0);
}

auto Allegrex::FSQRT(u8 fd, u8 fs) -> void {
  setF(fd, std::sqrt(f(fs)));
}

auto Allegrex::FSUB(u8 fd, u8 fs, u8 ft) -> void {
  setF(fd, f(fs) - f(ft));
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

//Only control register 31 (csr) holds anything; the others read as zero and ignore writes.
auto Allegrex::CFC1(u32& rt, u8 rd) -> void {
  rt = rd == 31 ? (u32)fpu.csr : 0;
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
