//The VFPU's common instructions, compiled. Whatever returns false here is compiled as a call into the interpreter
//instead (recompiler.cpp).
//
//The interpreter's VFPU instructions (interpreter-vfpu.cpp) take every operand through the prefixes: each lane may be
//swizzled, made absolute, negated or replaced by a constant, and each result clamped or left unwritten. Games
//rarely set them, and while they're at rest (pfxs and pfxt in their own order, pfxd nothing) all that does nothing:
//a lane is read as it is, and written as it is. So compiled code checks that they're at rest and calls a helper
//below made for that case, which reads and writes the lanes by the register numbers the block worked out when it was
//compiled, and does each lane's arithmetic exactly as the interpreter does (vfpuFloat(), vfpuBits(), vfpuNaN(), the
//same operations in the same order: a dot product's sums lane by lane, in doubles, from lane 0). With the prefixes
//anything else the instruction goes to the interpreter, as always. Either way they're at rest afterwards: the
//interpreter uses them up, and the helpers found them so. The block keeps track (prefixesAtRest) and checks only
//when it doesn't know: in practice once, at the first VFPU instruction of a block that sets no prefixes.
//
//The matrix instructions (vmmul, vtfm, vhtfm) don't read the prefixes at all; compiled code calls them directly
//(skipping only the decoding) and leaves the prefixes at rest, as the interpreter's decoder does after them.

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

//vdot: VDOT() with the prefixes at rest, where the lanes past the size are the ones left out of the sum.
template<u32 Size> auto Allegrex::vfpuRestDot(u32 vd, u32 vs, u32 vt) -> void {
  f64 sum = 0;
  for(u32 i : range(Size)) sum += (f64)vfpuFloat(vfpu.r[laneOf(vs, i)]) * vfpuFloat(vfpu.r[laneOf(vt, i)]);
  vfpu.r[vd] = vfpuNaN(vfpuBits(sum), NaNSign::Positive, 0);
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

//vmmul, vtfm and vhtfm, which leave the prefixes alone as they run: the interpreter's own, then the prefixes at
//rest, as its decoder leaves them (vfpuPrefixesUsed(), after an instruction that raised no exception, as these
//can't once decoded). which: 0 vmmul, 1 vtfm, 2 vhtfm.
auto Allegrex::vfpuMatrix(u32 which, u32 registers, u32 size) -> void {
  u8 vd = registers & 0x7f, vs = registers >> 8 & 0x7f, vt = registers >> 16 & 0x7f;
  if(which == 0) VMMUL(vd, vs, vt, size);
  if(which == 1) VTFM(vd, vs, vt, size);
  if(which == 2) VHTFM(vd, vs, vt, size);
  vfpu.pfxs = PrefixIdentity;
  vfpu.pfxt = PrefixIdentity;
  vfpu.pfxd = 0;
}

#define RTn (instruction >> 16 & 31)
#define Rt  gpr(RTn)

//Whether the prefixes may not be at rest: a jump taken then (to the interpreter), or none when the block knows they
//are (prefixesAtRest).
auto Allegrex::Recompiler::emitPrefixesBusy() -> sljit_jump* {
  if(prefixesAtRest) return nullptr;
  mov32(reg(0), field(&self.vfpu.pfxs));
  mov32(reg(1), field(&self.vfpu.pfxt));
  xor32(reg(0), reg(0), imm(PrefixIdentity));
  xor32(reg(1), reg(1), imm(PrefixIdentity));
  or32(reg(0), reg(0), reg(1));
  or32(reg(0), reg(0), field(&self.vfpu.pfxd));
  return cmp32_jump(reg(0), imm(0), flag_ne);
}

//What an instruction leaves the prefixes as, for prefixesAtRest: at rest after one that uses them up (an
//instruction that raised an exception instead has ended the block); unknown after one that may set them (vpfxs,
//vpfxt, vpfxd, mtvc and vmtvc, which write control registers); as they were after anything else.
auto Allegrex::Recompiler::prefixesAfter(u32 instruction) -> void {
  switch(instruction >> 26) {
  case 0x18: case 0x19: case 0x1b: case 0x3c:
    prefixesAtRest = true;
    return;
  case 0x34:
    if(instruction >> 16 == 0xd051) prefixesAtRest = false;  //vmtvc
    else if(instruction >> 16 != 0xd050) prefixesAtRest = true;  //(vmfvc reads)
    return;
  case 0x37:
    prefixesAtRest = (instruction >> 24 & 3) == 3;  //viim and vfim use them up; vpfxs, vpfxt and vpfxd set them
    return;
  case 0x3f:
    if(instruction == 0xffff'0000) prefixesAtRest = true;  //vnop (vsync and vflush leave them alone)
    return;
  case 0x12:
    if((instruction >> 21 & 31) == 0x07 && instruction & 0x80) prefixesAtRest = false;  //mtvc
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
  std::array<u32, 4> constants{};  //vzero, vone, vidt: the lanes' values, written by the compiled code itself
  bool constant = false;

  if(op == 0x18 && (group == 0 || group == 1 || group == 7)) {  //vadd, vsub, vdiv
    if(group == 0) helper = Sized(vfpuRestBinary, 0);
    if(group == 1) helper = Sized(vfpuRestBinary, 1);
    if(group == 7) helper = Sized(vfpuRestBinary, 7);
  } else if(op == 0x19 && group == 0) {  //vmul
    helper = Sized(vfpuRestBinary, 8);
  } else if(op == 0x19 && group == 1) {  //vdot
    helper = Sized(vfpuRestDot), first = self.vfpuLine(vd, 1)[0];
  } else if(op == 0x19 && group == 2) {  //vscl
    helper = Sized(vfpuRestScale), third = self.vfpuLine(vt, 1)[0];
  } else if(op == 0x1b && group == 0) {  //vcmp
    helper = Sized(vfpuRestCompare), first = instruction & 15;
  } else if(op == 0x34 && group == 0 && instruction >> 24 != 0xd3 && instruction >> 16 != 0xd050 &&
            instruction >> 16 != 0xd051) {
    if(vt == 0x00) helper = Sized(vfpuRestBits, 0);  //vmov
    if(vt == 0x01) helper = Sized(vfpuRestBits, 1);  //vabs
    if(vt == 0x02) helper = Sized(vfpuRestBits, 2);  //vneg
    if(vt == 0x06) constant = true;                   //vzero
    if(vt == 0x07) constant = true, constants.fill(One);  //vone
    if(vt == 0x03 && (size == 2 || size == 4)) {      //vidt (other sizes are reserved)
      constant = true;
      u32 position = vd & 3, start = lineOffset(vd, size);
      for(u32 i : range(size)) constants[i] = ((start + i) & 3) == position ? One : 0;
    }
  } else if(op == 0x34 && group == 5 && (instruction >> 21 & 3) == 0) {  //vi2f
    helper = Sized(vfpuRestToFloat), third = instruction >> 16 & 31;
  } else if(op == 0x3c && group <= 3) {  //vmmul, vtfm and vhtfm, if the size is one they have
    u32 matrix = group + 1, which = group == 0 ? 0 : size == matrix ? 1 : size == matrix - 1 ? 2 : 3;
    if((group == 0 && size == 1) || which == 3) return false;
    callf(&Allegrex::vfpuMatrix, imm(which), imm(vd | vs << 8 | vt << 16), imm(group == 0 ? size : matrix));
    prefixesAtRest = true;
    return true;
  } else if(instruction == 0xffff'0000) {  //vnop: the prefixes go back to rest
    mov32(field(&self.vfpu.pfxs), imm(PrefixIdentity));
    mov32(field(&self.vfpu.pfxt), imm(PrefixIdentity));
    mov32(field(&self.vfpu.pfxd), imm(0));
    prefixesAtRest = true;
    return true;
  } else if(instruction == 0xffff'0320 || instruction == 0xffff'040d) {  //vsync, vflush: nothing to wait for
    return true;
  }
  #undef Sized
  if(!helper && !constant) return false;

  auto busy = emitPrefixesBusy();
  if(helper) {
    callf(helper, imm(first), imm(second), imm(third));
  } else {
    auto lanes = self.vfpuLine(vd, size);
    for(u32 i : range(size)) mov32(field(&self.vfpu.r[lanes[i]]), imm(constants[i]));
  }
  if(busy) {
    auto done = jump();
    setLabel(busy);
    emitInterpreter(address, instruction, count, delaySlot);
    setLabel(done);
  }
  prefixesAtRest = true;
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
