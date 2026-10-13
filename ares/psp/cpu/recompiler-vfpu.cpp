//The VFPU's common instructions, compiled. Whatever returns false here is compiled as a call into the interpreter
//instead (recompiler.cpp).
//
//The interpreter's VFPU instructions (interpreter-vfpu.cpp) take every operand through the prefixes: each lane may be
//swizzled, made absolute, negated or replaced by a constant, and each result clamped or left unwritten. Games
//rarely set them, and while they're at rest (pfxs and pfxt in their own order, pfxd nothing) all that does nothing:
//a lane is read as it is, and written as it is. So compiled code checks that they're at rest and calls a helper
//below made for that case, which reads and writes the lanes by the register numbers the block worked out when it was
//compiled, and does each lane's arithmetic exactly as the interpreter does (vfpuFloat(), vfpuBits(), vfpuNaN(), the
//same operations in the same order; a dot product through the adder, vfpuDot()). With the prefixes anything else
//the instruction is the interpreter's, as always, called by the block itself without the decoding (emitVFPUCall()).
//Either way they're at rest afterwards: the interpreter uses them up, and the helpers found them so. The prefix
//instructions are compiled too, each storing its value; the block keeps track of what it knows of each prefix
//(prefixes), checks only when it doesn't know (in practice once, at the first VFPU instruction of a block that sets
//no prefixes), and goes the interpreter's way at once after a vpfxs, vpfxt or vpfxd that sets one.
//
//The matrix instructions (vmmul, vtfm, vhtfm, vmscl, vmmov, vmidt, vmzero, vmone) don't read the prefixes at all;
//compiled code calls them directly (skipping only the decoding) and leaves the prefixes at rest, as the
//interpreter's decoder does after them.

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
  u32 a[4] = {}, b[4] = {};
  for(u32 i : range(Size)) a[i] = vfpu.r[laneOf(vs, i)], b[i] = vfpu.r[laneOf(vt, i)];
  vfpu.r[vd] = vfpuNaN(vfpuDot(a, b), NaNSign::Positive, 0);
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
  u32 a[4] = {}, b[4] = {};
  for(u32 i : range(Size)) a[i] = vfpu.r[laneOf(vs, i)], b[i] = vfpu.r[laneOf(vt, i)];
  a[Size - 1] = One;
  vfpu.r[vd] = vfpuNaN(vfpuDot(a, b), NaNSign::Positive, 0);
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

  if(op == 0x37 && (instruction >> 24 & 3) != 3) {  //vpfxs, vpfxt, vpfxd: the bits decoderVFPU5() keeps
    u32 which = instruction >> 24 & 3;
    mov32(field(which == 0 ? &self.vfpu.pfxs : which == 1 ? &self.vfpu.pfxt : &self.vfpu.pfxd),
          imm(instruction & (which == 2 ? 0xfff : 0xf'ffff)));
    return true;
  } else if(op == 0x37) {  //viim, vfim: their constant into rt's single, as VIIM() and VFIM() make it
    constant = true, written = 1, vd = vt;
    if(instruction >> 23 & 1) constants[0] = self.vfpuBits(std::bit_cast<f32>(halfToFloat(u16(instruction))));
    else constants[0] = self.vfpuBits(s16(instruction));
  } else if(op == 0x18 && (group == 0 || group == 1 || group == 7)) {  //vadd, vsub, vdiv
    if(group == 0) helper = Sized(vfpuRestBinary, 0);
    if(group == 1) helper = Sized(vfpuRestBinary, 1);
    if(group == 7) helper = Sized(vfpuRestBinary, 7);
  } else if(op == 0x19 && group == 0) {  //vmul
    helper = Sized(vfpuRestBinary, 8);
  } else if(op == 0x19 && group == 1) {  //vdot
    helper = Sized(vfpuRestDot), first = self.vfpuLine(vd, 1)[0];
  } else if(op == 0x19 && group == 2) {  //vscl
    helper = Sized(vfpuRestScale), third = self.vfpuLine(vt, 1)[0];
  } else if(op == 0x19 && group == 4) {  //vhdp
    helper = Sized(vfpuRestHalfDot), first = self.vfpuLine(vd, 1)[0];
  } else if(op == 0x1b && group == 0) {  //vcmp
    helper = Sized(vfpuRestCompare), first = instruction & 15;
  } else if(op == 0x1b && (group == 2 || group == 3)) {  //vmin, vmax
    helper = group == 2 ? Sized(vfpuRestMinMax, false) : Sized(vfpuRestMinMax, true);
  } else if(op == 0x1b && (group == 6 || group == 7)) {  //vsge, vslt
    helper = group == 6 ? Sized(vfpuRestSet, false) : Sized(vfpuRestSet, true);
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
    if(which >= 0) {
      callf(&Allegrex::vfpuMatrix, imm(which), imm(vd | vs << 8 | vt << 16), imm(matrix));
      return true;
    }
  } else if(instruction == 0xffff'0000 || instruction == 0xffff'0320 || instruction == 0xffff'040d) {
    return true;  //vnop, vsync, vflush: nothing to do or wait for, and the prefixes stay for the next instruction
  }
  #undef Sized
  if((!helper && !constant) || prefixes.busy()) return emitVFPUCall(address, instruction, count, delaySlot);

  auto busy = emitPrefixesBusy();
  if(helper) {
    callf(helper, imm(first), imm(second), imm(third));
  } else {
    auto lanes = self.vfpuLine(vd, written);
    for(u32 i : range(written)) mov32(field(&self.vfpu.r[lanes[i]]), imm(constants[i]));
  }
  if(busy) {
    auto done = jump();
    setLabel(busy);
    emitVFPUCall(address, instruction, count, delaySlot);
    setLabel(done);
  }
  return true;
}

//A VFPU instruction of an arithmetic group (or vpfx's, viim's and vfim's) with no native code above, or with the
//prefixes set: the interpreter's own, as execute() runs it, but for decoderEXECUTE()'s choice of the group, which the
//block makes (executeVFPU()). It writes no integer register, so what the host registers hold stays true, and it
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
  if(!delaySlot) {
    mov32(field(&self.ipu.pc), imm(address + 4));
    mov32(field(&self.ipu.pd), imm(address + 8));
  }
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
