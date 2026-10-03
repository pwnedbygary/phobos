#include "allegrex.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace phobos::psp {

namespace {
  // Instruction fields.
  constexpr auto rs(uint32_t op) -> uint32_t { return op >> 21 & 31; }
  constexpr auto rt(uint32_t op) -> uint32_t { return op >> 16 & 31; }
  constexpr auto rd(uint32_t op) -> uint32_t { return op >> 11 & 31; }
  constexpr auto sa(uint32_t op) -> uint32_t { return op >> 6 & 31; }
  constexpr auto funct(uint32_t op) -> uint32_t { return op & 63; }
  constexpr auto simm(uint32_t op) -> uint32_t { return (uint32_t)(int32_t)(int16_t)op; }
  constexpr auto uimm(uint32_t op) -> uint32_t { return op & 0xffff; }
  // The FPU's register fields sit where rd and sa are for the integer unit.
  constexpr auto fs(uint32_t op) -> uint32_t { return op >> 11 & 31; }
  constexpr auto fd(uint32_t op) -> uint32_t { return op >> 6 & 31; }

  constexpr uint32_t Condition = 1u << 23;
  constexpr uint32_t FlushToZero = 1u << 24;

  auto asFloat(uint32_t bits) -> float { return std::bit_cast<float>(bits); }
  auto asBits(float value) -> uint32_t { return std::bit_cast<uint32_t>(value); }

  auto swapBytes(uint32_t value) -> uint32_t {
    return value >> 24 | (value >> 8 & 0xff00) | (value << 8 & 0xff0000) | value << 24;
  }

  auto reverseBits(uint32_t value) -> uint32_t {
    value = (value >> 1 & 0x55555555) | (value & 0x55555555) << 1;
    value = (value >> 2 & 0x33333333) | (value & 0x33333333) << 2;
    value = (value >> 4 & 0x0f0f0f0f) | (value & 0x0f0f0f0f) << 4;
    return swapBytes(value);
  }
}

auto Allegrex::reset(uint32_t entry) -> void {
  state = {};
  state.pc = entry;
  state.npc = entry + 4;
}

auto Allegrex::step() -> void {
  current = state.pc;
  if(current & 3) {
    state.cop0[8] = current;  // BadVAddr
    return raise(Exception::AddressLoad);
  }
  uint32_t op = bus.read32(current);
  state.pc = state.npc;
  state.npc += 4;
  execute(op);
  state.gpr[0] = 0;
}

auto Allegrex::run(uint64_t count) -> uint64_t {
  uint64_t ran = 0;
  while(ran < count && !state.halted) {
    step();
    ran++;
  }
  return ran;
}

auto Allegrex::raise(Exception exception) -> void {
  if(exceptionHook) return exceptionHook(exception, current);
  state.halted = true;
}

// Targets are relative to the delay slot, where pc already points.
auto Allegrex::branch(bool taken, uint32_t op) -> void {
  if(taken) state.npc = state.pc + (simm(op) << 2);
}

// A likely branch that isn't taken skips its delay slot.
auto Allegrex::branchLikely(bool taken, uint32_t op) -> void {
  if(taken) return branch(true, op);
  state.pc = state.npc;
  state.npc += 4;
}

auto Allegrex::loadAddress(uint32_t op) const -> uint32_t {
  return state.gpr[rs(op)] + simm(op);
}

auto Allegrex::execute(uint32_t op) -> void {
  auto& r = state.gpr;
  switch(op >> 26) {
  case 0x00: return special(op);
  case 0x01: return regimm(op);
  case 0x02: state.npc = (state.pc & 0xf0000000) | (op & 0x03ffffff) << 2; return;  // j
  case 0x03: r[31] = state.npc; state.npc = (state.pc & 0xf0000000) | (op & 0x03ffffff) << 2; return;  // jal
  case 0x04: return branch(r[rs(op)] == r[rt(op)], op);  // beq
  case 0x05: return branch(r[rs(op)] != r[rt(op)], op);  // bne
  case 0x06: return branch((int32_t)r[rs(op)] <= 0, op);  // blez
  case 0x07: return branch((int32_t)r[rs(op)] > 0, op);   // bgtz
  case 0x08: {  // addi
    int32_t sum;
    if(__builtin_add_overflow((int32_t)r[rs(op)], (int32_t)simm(op), &sum)) return raise(Exception::Overflow);
    return set(rt(op), (uint32_t)sum);
  }
  case 0x09: return set(rt(op), r[rs(op)] + simm(op));  // addiu
  case 0x0a: return set(rt(op), (int32_t)r[rs(op)] < (int32_t)simm(op));  // slti
  case 0x0b: return set(rt(op), r[rs(op)] < simm(op));  // sltiu: compares with the sign-extended immediate
  case 0x0c: return set(rt(op), r[rs(op)] & uimm(op));  // andi
  case 0x0d: return set(rt(op), r[rs(op)] | uimm(op));  // ori
  case 0x0e: return set(rt(op), r[rs(op)] ^ uimm(op));  // xori
  case 0x0f: return set(rt(op), uimm(op) << 16);  // lui
  case 0x10: return cop0(op);
  case 0x11: return cop1(op);
  case 0x14: return branchLikely(r[rs(op)] == r[rt(op)], op);  // beql
  case 0x15: return branchLikely(r[rs(op)] != r[rt(op)], op);  // bnel
  case 0x16: return branchLikely((int32_t)r[rs(op)] <= 0, op);  // blezl
  case 0x17: return branchLikely((int32_t)r[rs(op)] > 0, op);   // bgtzl
  case 0x1c: return special2(op);
  case 0x1f: return special3(op);
  case 0x20: return set(rt(op), (uint32_t)(int32_t)(int8_t)bus.read8(loadAddress(op)));  // lb
  case 0x21: {  // lh
    uint32_t address = loadAddress(op);
    if(address & 1) { state.cop0[8] = address; return raise(Exception::AddressLoad); }
    return set(rt(op), (uint32_t)(int32_t)(int16_t)bus.read16(address));
  }
  case 0x22: {  // lwl: the word's high bytes from the bytes at and below the address
    uint32_t address = loadAddress(op);
    uint32_t shift = (3 - (address & 3)) * 8;
    uint32_t word = bus.read32(address & ~3u);
    uint32_t keep = shift == 0 ? 0 : 0xffffffffu >> (32 - shift);
    return set(rt(op), (r[rt(op)] & keep) | word << shift);
  }
  case 0x23: {  // lw
    uint32_t address = loadAddress(op);
    if(address & 3) { state.cop0[8] = address; return raise(Exception::AddressLoad); }
    return set(rt(op), bus.read32(address));
  }
  case 0x24: return set(rt(op), bus.read8(loadAddress(op)));  // lbu
  case 0x25: {  // lhu
    uint32_t address = loadAddress(op);
    if(address & 1) { state.cop0[8] = address; return raise(Exception::AddressLoad); }
    return set(rt(op), bus.read16(address));
  }
  case 0x26: {  // lwr: the word's low bytes from the bytes at and above the address
    uint32_t address = loadAddress(op);
    uint32_t shift = (address & 3) * 8;
    uint32_t word = bus.read32(address & ~3u);
    uint32_t keep = shift == 0 ? 0 : ~(0xffffffffu >> shift);
    return set(rt(op), (r[rt(op)] & keep) | word >> shift);
  }
  case 0x28: return bus.write8(loadAddress(op), (uint8_t)r[rt(op)]);  // sb
  case 0x29: {  // sh
    uint32_t address = loadAddress(op);
    if(address & 1) { state.cop0[8] = address; return raise(Exception::AddressStore); }
    return bus.write16(address, (uint16_t)r[rt(op)]);
  }
  case 0x2a: {  // swl: the register's high bytes to the bytes at and below the address
    uint32_t address = loadAddress(op);
    uint32_t shift = (3 - (address & 3)) * 8;
    uint32_t word = bus.read32(address & ~3u);
    uint32_t keep = shift == 0 ? 0 : ~(0xffffffffu >> shift);
    return bus.write32(address & ~3u, (word & keep) | r[rt(op)] >> shift);
  }
  case 0x2b: {  // sw
    uint32_t address = loadAddress(op);
    if(address & 3) { state.cop0[8] = address; return raise(Exception::AddressStore); }
    return bus.write32(address, r[rt(op)]);
  }
  case 0x2e: {  // swr: the register's low bytes to the bytes at and above the address
    uint32_t address = loadAddress(op);
    uint32_t shift = (address & 3) * 8;
    uint32_t word = bus.read32(address & ~3u);
    uint32_t keep = shift == 0 ? 0 : 0xffffffffu >> (32 - shift);
    return bus.write32(address & ~3u, (word & keep) | r[rt(op)] << shift);
  }
  case 0x2f: return;  // cache: an interpreter has no caches to keep in step
  case 0x30: {  // ll: one core, so the reservation always holds
    uint32_t address = loadAddress(op);
    if(address & 3) { state.cop0[8] = address; return raise(Exception::AddressLoad); }
    return set(rt(op), bus.read32(address));
  }
  case 0x31: {  // lwc1
    uint32_t address = loadAddress(op);
    if(address & 3) { state.cop0[8] = address; return raise(Exception::AddressLoad); }
    state.fpr[rt(op)] = bus.read32(address);
    return;
  }
  case 0x38: {  // sc: always succeeds on one core
    uint32_t address = loadAddress(op);
    if(address & 3) { state.cop0[8] = address; return raise(Exception::AddressStore); }
    bus.write32(address, r[rt(op)]);
    return set(rt(op), 1);
  }
  case 0x39: {  // swc1
    uint32_t address = loadAddress(op);
    if(address & 3) { state.cop0[8] = address; return raise(Exception::AddressStore); }
    return bus.write32(address, state.fpr[rt(op)]);
  }
  }
  // The VFPU's opcodes (0x12, 0x18-0x1b, 0x32-0x37, 0x3a-0x3f) land here too, for now.
  raise(Exception::ReservedInstruction);
}

auto Allegrex::special(uint32_t op) -> void {
  auto& r = state.gpr;
  switch(funct(op)) {
  case 0x00: return set(rd(op), r[rt(op)] << sa(op));  // sll
  case 0x02:  // srl, or rotr when rs is 1
    if(rs(op) == 1) return set(rd(op), std::rotr(r[rt(op)], (int)sa(op)));
    return set(rd(op), r[rt(op)] >> sa(op));
  case 0x03: return set(rd(op), (uint32_t)((int32_t)r[rt(op)] >> sa(op)));  // sra
  case 0x04: return set(rd(op), r[rt(op)] << (r[rs(op)] & 31));  // sllv
  case 0x06:  // srlv, or rotrv when sa is 1
    if(sa(op) == 1) return set(rd(op), std::rotr(r[rt(op)], (int)(r[rs(op)] & 31)));
    return set(rd(op), r[rt(op)] >> (r[rs(op)] & 31));
  case 0x07: return set(rd(op), (uint32_t)((int32_t)r[rt(op)] >> (r[rs(op)] & 31)));  // srav
  case 0x08: state.npc = r[rs(op)]; return;  // jr
  case 0x09: {  // jalr: read the target first, in case rd and rs are the same register
    uint32_t target = r[rs(op)];
    set(rd(op), state.npc);
    state.npc = target;
    return;
  }
  case 0x0a: if(r[rt(op)] == 0) set(rd(op), r[rs(op)]); return;  // movz
  case 0x0b: if(r[rt(op)] != 0) set(rd(op), r[rs(op)]); return;  // movn
  case 0x0c: {  // syscall
    uint32_t code = op >> 6 & 0xfffff;
    if(syscallHook && syscallHook(code)) return;
    return raise(Exception::Syscall);
  }
  case 0x0d: return raise(Exception::Breakpoint);  // break
  case 0x0f: return;  // sync
  case 0x10: return set(rd(op), state.hi);  // mfhi
  case 0x11: state.hi = r[rs(op)]; return;  // mthi
  case 0x12: return set(rd(op), state.lo);  // mflo
  case 0x13: state.lo = r[rs(op)]; return;  // mtlo
  case 0x16: return set(rd(op), (uint32_t)std::countl_zero(r[rs(op)]));  // clz
  case 0x17: return set(rd(op), (uint32_t)std::countl_one(r[rs(op)]));   // clo
  case 0x18: {  // mult
    int64_t product = (int64_t)(int32_t)r[rs(op)] * (int32_t)r[rt(op)];
    state.lo = (uint32_t)product;
    state.hi = (uint32_t)((uint64_t)product >> 32);
    return;
  }
  case 0x19: {  // multu
    uint64_t product = (uint64_t)r[rs(op)] * r[rt(op)];
    state.lo = (uint32_t)product;
    state.hi = (uint32_t)(product >> 32);
    return;
  }
  case 0x1a: {  // div. Divide by zero isn't checked against hardware yet: this gives what MIPS cores commonly do.
    int32_t dividend = (int32_t)r[rs(op)], divisor = (int32_t)r[rt(op)];
    if(divisor == 0) {
      state.lo = dividend < 0 ? 1 : 0xffffffff;
      state.hi = (uint32_t)dividend;
    } else if(dividend == std::numeric_limits<int32_t>::min() && divisor == -1) {
      state.lo = (uint32_t)dividend;
      state.hi = 0;
    } else {
      state.lo = (uint32_t)(dividend / divisor);
      state.hi = (uint32_t)(dividend % divisor);
    }
    return;
  }
  case 0x1b: {  // divu
    uint32_t dividend = r[rs(op)], divisor = r[rt(op)];
    if(divisor == 0) {
      state.lo = 0xffffffff;
      state.hi = dividend;
    } else {
      state.lo = dividend / divisor;
      state.hi = dividend % divisor;
    }
    return;
  }
  case 0x1c: case 0x1d: case 0x2e: case 0x2f: {  // madd, maddu, msub, msubu
    bool isSigned = !(funct(op) & 1);
    bool subtract = funct(op) >= 0x2e;
    uint64_t product = isSigned ? (uint64_t)((int64_t)(int32_t)r[rs(op)] * (int32_t)r[rt(op)])
                                : (uint64_t)r[rs(op)] * r[rt(op)];
    uint64_t accumulator = (uint64_t)state.hi << 32 | state.lo;
    accumulator = subtract ? accumulator - product : accumulator + product;
    state.lo = (uint32_t)accumulator;
    state.hi = (uint32_t)(accumulator >> 32);
    return;
  }
  case 0x20: {  // add
    int32_t sum;
    if(__builtin_add_overflow((int32_t)r[rs(op)], (int32_t)r[rt(op)], &sum)) return raise(Exception::Overflow);
    return set(rd(op), (uint32_t)sum);
  }
  case 0x21: return set(rd(op), r[rs(op)] + r[rt(op)]);  // addu
  case 0x22: {  // sub
    int32_t difference;
    if(__builtin_sub_overflow((int32_t)r[rs(op)], (int32_t)r[rt(op)], &difference)) return raise(Exception::Overflow);
    return set(rd(op), (uint32_t)difference);
  }
  case 0x23: return set(rd(op), r[rs(op)] - r[rt(op)]);  // subu
  case 0x24: return set(rd(op), r[rs(op)] & r[rt(op)]);  // and
  case 0x25: return set(rd(op), r[rs(op)] | r[rt(op)]);  // or
  case 0x26: return set(rd(op), r[rs(op)] ^ r[rt(op)]);  // xor
  case 0x27: return set(rd(op), ~(r[rs(op)] | r[rt(op)]));  // nor
  case 0x2a: return set(rd(op), (int32_t)r[rs(op)] < (int32_t)r[rt(op)]);  // slt
  case 0x2b: return set(rd(op), r[rs(op)] < r[rt(op)]);  // sltu
  case 0x2c: return set(rd(op), (int32_t)r[rs(op)] > (int32_t)r[rt(op)] ? r[rs(op)] : r[rt(op)]);  // max
  case 0x2d: return set(rd(op), (int32_t)r[rs(op)] < (int32_t)r[rt(op)] ? r[rs(op)] : r[rt(op)]);  // min
  }
  raise(Exception::ReservedInstruction);
}

auto Allegrex::regimm(uint32_t op) -> void {
  int32_t value = (int32_t)state.gpr[rs(op)];
  switch(rt(op)) {
  case 0x00: return branch(value < 0, op);         // bltz
  case 0x01: return branch(value >= 0, op);        // bgez
  case 0x02: return branchLikely(value < 0, op);   // bltzl
  case 0x03: return branchLikely(value >= 0, op);  // bgezl
  // The link versions write ra whether or not they branch; the comparison used rs as it was before.
  case 0x10: set(31, state.npc); return branch(value < 0, op);         // bltzal
  case 0x11: set(31, state.npc); return branch(value >= 0, op);        // bgezal
  case 0x12: set(31, state.npc); return branchLikely(value < 0, op);   // bltzall
  case 0x13: set(31, state.npc); return branchLikely(value >= 0, op);  // bgezall
  }
  raise(Exception::ReservedInstruction);
}

// halt, mfic and mtic are the only SPECIAL2 instructions the Allegrex has.
auto Allegrex::special2(uint32_t op) -> void {
  if(op == 0x70000000) { state.halted = true; return; }  // halt
  switch(op & 0xffe007ff) {
  case 0x70000024: return set(rt(op), state.interrupts);  // mfic
  case 0x70000026: state.interrupts = state.gpr[rt(op)]; return;  // mtic
  }
  raise(Exception::ReservedInstruction);
}

auto Allegrex::special3(uint32_t op) -> void {
  auto& r = state.gpr;
  switch(funct(op)) {
  case 0x00: {  // ext: rd holds the size less one, sa the position
    uint32_t size = rd(op) + 1;
    uint64_t mask = ((uint64_t)1 << size) - 1;
    return set(rt(op), (uint32_t)((r[rs(op)] >> sa(op)) & mask));
  }
  case 0x04: {  // ins: rd holds the field's top bit, sa its position
    uint32_t position = sa(op), top = rd(op);
    if(top < position) return;  // unpredictable on MIPS; left unchanged here
    uint64_t field = ((uint64_t)1 << (top - position + 1)) - 1;
    uint32_t mask = (uint32_t)(field << position);
    return set(rt(op), (r[rt(op)] & ~mask) | ((r[rs(op)] << position) & mask));
  }
  case 0x20:  // bshfl: the operation is in sa
    if(rs(op) != 0) break;
    switch(sa(op)) {
    case 0x02: return set(rd(op), (r[rt(op)] & 0x00ff00ff) << 8 | (r[rt(op)] >> 8 & 0x00ff00ff));  // wsbh
    case 0x03: return set(rd(op), swapBytes(r[rt(op)]));  // wsbw
    case 0x10: return set(rd(op), (uint32_t)(int32_t)(int8_t)r[rt(op)]);   // seb
    case 0x14: return set(rd(op), reverseBits(r[rt(op)]));                 // bitrev
    case 0x18: return set(rd(op), (uint32_t)(int32_t)(int16_t)r[rt(op)]);  // seh
    }
    break;
  }
  raise(Exception::ReservedInstruction);
}

// HLE runs the game in user mode, so coprocessor 0 is only kept as registers, plus eret for completeness.
auto Allegrex::cop0(uint32_t op) -> void {
  switch(rs(op)) {
  case 0x00: return set(rt(op), state.cop0[rd(op)]);  // mfc0
  case 0x04: state.cop0[rd(op)] = state.gpr[rt(op)]; return;  // mtc0
  case 0x10:
    if(funct(op) == 0x18) {  // eret: EPC is cop0 register 14
      state.pc = state.cop0[14];
      state.npc = state.pc + 4;
      return;
    }
    break;
  }
  raise(Exception::ReservedInstruction);
}

auto Allegrex::cop1(uint32_t op) -> void {
  switch(rs(op)) {
  case 0x00: return set(rt(op), state.fpr[fs(op)]);  // mfc1
  case 0x02:  // cfc1: only the control and status register (31) holds anything
    return set(rt(op), fs(op) == 31 ? state.fcr31 : 0);
  case 0x04: state.fpr[fs(op)] = state.gpr[rt(op)]; return;  // mtc1
  case 0x06: if(fs(op) == 31) state.fcr31 = state.gpr[rt(op)]; return;  // ctc1
  case 0x08: {  // bc1f, bc1t, bc1fl, bc1tl
    bool condition = state.fcr31 & Condition;
    bool wantTrue = rt(op) & 1;
    if(rt(op) & 2) return branchLikely(condition == wantTrue, op);
    return branch(condition == wantTrue, op);
  }
  case 0x10: return fpuSingle(op);
  case 0x14:  // cvt.s.w
    if(funct(op) == 0x20) {
      state.fpr[fd(op)] = asBits(flush((float)(int32_t)state.fpr[fs(op)]));
      return;
    }
    break;
  }
  raise(Exception::ReservedInstruction);
}

auto Allegrex::flush(float value) const -> float {
  if((state.fcr31 & FlushToZero) && std::fpclassify(value) == FP_SUBNORMAL) return std::copysign(0.0f, value);
  return value;
}

auto Allegrex::fpuCondition(bool value) -> void {
  if(value) state.fcr31 |= Condition;
  else state.fcr31 &= ~Condition;
}

// Rounds to a word in `mode` (0 nearest even, 1 toward zero, 2 up, 3 down). NaN and out-of-range values give
// 0x7fffffff, MIPS's default result for an invalid conversion.
auto Allegrex::toWord(float value, uint32_t mode) const -> uint32_t {
  double rounded;
  switch(mode & 3) {
  case 0: {
    double whole = std::floor((double)value);
    double fraction = (double)value - whole;
    if(fraction > 0.5 || (fraction == 0.5 && std::fmod(whole, 2.0) != 0.0)) whole += 1.0;
    rounded = whole;
    break;
  }
  case 1: rounded = std::trunc((double)value); break;
  case 2: rounded = std::ceil((double)value); break;
  default: rounded = std::floor((double)value); break;
  }
  if(std::isnan(rounded) || rounded > 2147483647.0 || rounded < -2147483648.0) return 0x7fffffff;
  return (uint32_t)(int32_t)rounded;
}

auto Allegrex::fpuSingle(uint32_t op) -> void {
  float s = flush(asFloat(state.fpr[fs(op)]));
  float t = flush(asFloat(state.fpr[rt(op)]));  // ft sits in the rt field
  auto write = [&](float value) { state.fpr[fd(op)] = asBits(flush(value)); };
  uint32_t f = funct(op);
  if(f >= 0x30) {  // c.cond.s: bit 0 of the condition is unordered, bit 1 equal, bit 2 less than
    bool unordered = std::isnan(s) || std::isnan(t);
    bool result = unordered ? (f & 1) : ((f & 2) && s == t) || ((f & 4) && s < t);
    return fpuCondition(result);
  }
  switch(f) {
  case 0x00: return write(s + t);  // add.s
  case 0x01: return write(s - t);  // sub.s
  case 0x02: return write(s * t);  // mul.s
  case 0x03: return write(s / t);  // div.s
  case 0x04: return write(std::sqrt(s));  // sqrt.s
  case 0x05: state.fpr[fd(op)] = state.fpr[fs(op)] & 0x7fffffff; return;  // abs.s
  case 0x06: state.fpr[fd(op)] = state.fpr[fs(op)]; return;               // mov.s
  case 0x07: state.fpr[fd(op)] = state.fpr[fs(op)] ^ 0x80000000; return;  // neg.s
  case 0x0c: state.fpr[fd(op)] = toWord(s, 0); return;  // round.w.s
  case 0x0d: state.fpr[fd(op)] = toWord(s, 1); return;  // trunc.w.s
  case 0x0e: state.fpr[fd(op)] = toWord(s, 2); return;  // ceil.w.s
  case 0x0f: state.fpr[fd(op)] = toWord(s, 3); return;  // floor.w.s
  case 0x24: state.fpr[fd(op)] = toWord(s, state.fcr31); return;  // cvt.w.s, in the current rounding mode
  }
  raise(Exception::ReservedInstruction);
}

}
