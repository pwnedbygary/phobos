#pragma once

//AArch64 NEON code for RSP multiply and multiply-accumulate instructions, as raw instruction
//words. The recompiler emits them with sljit_emit_op_custom; tests/rsp-vu-neon runs the same
//words against the SSE and scalar implementations in interpreter-vpu.cpp.
//
//Each sequence loads its operands from the VU struct, computes, and stores the results. The
//accumulator can instead stay in v19 (acch), v20 (accm) and v21 (accl) from one sequence to the
//next (accLoaded, keepAcc); the caller must then store it with flushAcc before anything else can
//read the VU struct's copy or clobber those registers. Only v16-v29 are used: they are caller-
//saved and left alone by sljit in integer code (sljit uses v30/v31 as float temporaries).
//
//Element n of a vector register is 16-bit lane 7 - n in memory (see r128::u16), which only
//matters for the element broadcast; everything else is lane-wise.

#include <cstdint>
#include <vector>

namespace RspVuNeon {

enum class Op : uint32_t { VMUDL, VMUDM, VMUDN, VMUDH, VMADL, VMADM, VMADN, VMADH };

//byte offsets from the VU base register
struct Layout {
  uint32_t r;  //r[0]; r[n] is at r + n * 16
  uint32_t acch, accm, accl;
};

namespace encode {
  //LDR/STR Qt, [Xn, #offset]; offset must be a multiple of 16 below 65536
  inline auto ldrq(uint32_t t, uint32_t n, uint32_t offset) -> uint32_t { return 0x3dc00000u | offset / 16 << 10 | n << 5 | t; }
  inline auto strq(uint32_t t, uint32_t n, uint32_t offset) -> uint32_t { return 0x3d800000u | offset / 16 << 10 | n << 5 | t; }
  inline auto rrr(uint32_t op, uint32_t d, uint32_t n, uint32_t m) -> uint32_t { return op | m << 16 | n << 5 | d; }
  inline auto rr(uint32_t op, uint32_t d, uint32_t n) -> uint32_t { return op | n << 5 | d; }

  constexpr uint32_t MUL_8H    = 0x4e609c00, MUL_4S    = 0x4ea09c00;
  constexpr uint32_t SMULL_4S  = 0x0e60c000, SMULL2_4S = 0x4e60c000;
  constexpr uint32_t UMULL_4S  = 0x2e60c000, UMULL2_4S = 0x6e60c000;
  constexpr uint32_t ADD_8H    = 0x4e608400, ADD_4S    = 0x4ea08400;
  constexpr uint32_t SUB_8H    = 0x6e608400, SUB_4S    = 0x6ea08400;
  constexpr uint32_t ZIP1_8H   = 0x4e403800, ZIP2_8H   = 0x4e407800;
  constexpr uint32_t UZP1_8H   = 0x4e401800, UZP2_8H   = 0x4e405800;
  constexpr uint32_t TRN1_8H   = 0x4e402800, TRN2_8H   = 0x4e406800;
  constexpr uint32_t CMHI_8H   = 0x6e603400, CMHI_4S   = 0x6ea03400;
  constexpr uint32_t CMEQ_8H   = 0x6e608c00;
  constexpr uint32_t CMEQZ_8H  = 0x4e609800;  //CMEQ Vd.8H, Vn.8H, #0
  constexpr uint32_t CMGEZ_8H  = 0x6e608800;  //CMGE Vd.8H, Vn.8H, #0
  constexpr uint32_t AND_16B   = 0x4e201c00, BSL_16B   = 0x6e601c00, ORR_16B = 0x4ea01c00;
  constexpr uint32_t SQXTN_4H  = 0x0e614800, SQXTN2_8H = 0x4e614800;
  constexpr uint32_t SXTL_4S   = 0x0f10a400, SXTL2_4S  = 0x4f10a400;
  constexpr uint32_t UXTL_4S   = 0x2f10a400, UXTL2_4S  = 0x6f10a400;
  constexpr uint32_t MOVI0_2D  = 0x6f00e400;  //MOVI Vd.2D, #0

  inline auto sshr8h(uint32_t d, uint32_t n, uint32_t shift) -> uint32_t { return 0x4f000400u | (32 - shift) << 16 | n << 5 | d; }
  inline auto sshr4s(uint32_t d, uint32_t n, uint32_t shift) -> uint32_t { return 0x4f000400u | (64 - shift) << 16 | n << 5 | d; }
  //DUP Vd.8H, Vn.H[lane]
  inline auto dup8h(uint32_t d, uint32_t n, uint32_t lane) -> uint32_t { return 0x4e000400u | (lane << 2 | 2) << 16 | n << 5 | d; }
  //INS Vd.D[1], Vn.D[1]
  inline auto insd1(uint32_t d, uint32_t n) -> uint32_t { return 0x6e000400u | 0x18u << 16 | 0x8u << 11 | n << 5 | d; }
}

//register assignment
namespace v {
  constexpr uint32_t vs = 16, vt = 17, vte = 18, acch = 19, accm = 20, accl = 21;
  constexpr uint32_t t0 = 22, t1 = 23, t2 = 24, t3 = 25, t4 = 26, t5 = 27, t6 = 28, t7 = 29;
}

//vt(e) of the register loaded into v::vt; returns the register holding the result
inline auto broadcast(std::vector<uint32_t>& out, uint32_t e) -> uint32_t {
  using namespace encode;
  if(e < 2) return v::vt;
  if(e < 4) {  //0q: element 2m+1 <- 2m (even lane <- odd lane above); 1q: element 2m <- 2m+1
    out.push_back(rrr(e == 2 ? TRN2_8H : TRN1_8H, v::vte, v::vt, v::vt));
  } else if(e < 8) {  //elements 0-3 <- element e-4 (lane 7-(e-4)); 4-7 <- element e (lane 3-(e-4))
    out.push_back(dup8h(v::vte, v::vt, 3 - (e - 4)));
    out.push_back(dup8h(v::t7, v::vt, 7 - (e - 4)));
    out.push_back(insd1(v::vte, v::t7));
  } else {  //all elements <- element e-8
    out.push_back(dup8h(v::vte, v::vt, 7 - (e - 8)));
  }
  return v::vte;
}

//32-bit products of the eight lanes: lanes 0-3 in lo, 4-7 in hi. vsSigned/vtSigned pick s16 or
//u16 operands; mixed signs widen both first, since NEON has no mixed-sign 16-bit multiply.
inline auto products(std::vector<uint32_t>& out, uint32_t vte, bool vsSigned, bool vtSigned, uint32_t lo, uint32_t hi) -> void {
  using namespace encode;
  if(vsSigned && vtSigned) {
    out.push_back(rrr(SMULL_4S,  lo, v::vs, vte));
    out.push_back(rrr(SMULL2_4S, hi, v::vs, vte));
  } else if(!vsSigned && !vtSigned) {
    out.push_back(rrr(UMULL_4S,  lo, v::vs, vte));
    out.push_back(rrr(UMULL2_4S, hi, v::vs, vte));
  } else {
    out.push_back(rr(vsSigned ? SXTL_4S  : UXTL_4S,  lo, v::vs));
    out.push_back(rr(vsSigned ? SXTL2_4S : UXTL2_4S, hi, v::vs));
    out.push_back(rr(vtSigned ? SXTL_4S  : UXTL_4S,  v::t6, vte));
    out.push_back(rr(vtSigned ? SXTL2_4S : UXTL2_4S, v::t7, vte));
    out.push_back(rrr(MUL_4S, lo, lo, v::t6));
    out.push_back(rrr(MUL_4S, hi, hi, v::t7));
  }
}

//vd for the "slice 0" clamp (VMADL, VMADN): accl where acch:accm fits in 16 signed bits, else
//0x0000 if negative and 0xffff if positive. Returns the register holding it.
inline auto clampLow(std::vector<uint32_t>& out, uint32_t accl) -> uint32_t {
  using namespace encode;
  out.push_back(sshr8h(v::t6, v::accm, 15));
  out.push_back(rrr(CMEQ_8H, v::t6, v::t6, v::acch));
  out.push_back(rr(CMGEZ_8H, v::t7, v::acch));
  out.push_back(rrr(BSL_16B, v::t6, accl, v::t7));
  return v::t6;
}

//vd for the "slice 1" clamp: acch:accm saturated to 16 signed bits
inline auto clampMid(std::vector<uint32_t>& out) -> uint32_t {
  using namespace encode;
  out.push_back(rrr(ZIP1_8H, v::t6, v::accm, v::acch));
  out.push_back(rrr(ZIP2_8H, v::t7, v::accm, v::acch));
  out.push_back(rr(SQXTN_4H,  v::t6, v::t6));
  out.push_back(rr(SQXTN2_8H, v::t6, v::t7));
  return v::t6;
}

//Stores an accumulator left in v19-v21 by keepAcc.
inline auto flushAcc(std::vector<uint32_t>& out, uint32_t base, const Layout& layout) -> void {
  using namespace encode;
  out.push_back(strq(v::acch, base, layout.acch));
  out.push_back(strq(v::accm, base, layout.accm));
  out.push_back(strq(v::accl, base, layout.accl));
}

//Appends the code for `op vd, vs, vt(e)`. base is the X register holding the VU address.
//accLoaded: the accumulator is already in v19-v21. keepAcc: leave it there instead of storing it.
inline auto emit(std::vector<uint32_t>& out, Op op, uint32_t e, uint32_t vd, uint32_t vs, uint32_t vt, uint32_t base,
                 const Layout& layout, bool accLoaded = false, bool keepAcc = false) -> void {
  using namespace encode;
  auto vreg = [&](uint32_t n) { return layout.r + n * 16; };
  auto loadAcc = [&](bool high, bool middle, bool low) {
    if(accLoaded) return;
    if(high)   out.push_back(ldrq(v::acch, base, layout.acch));
    if(middle) out.push_back(ldrq(v::accm, base, layout.accm));
    if(low)    out.push_back(ldrq(v::accl, base, layout.accl));
  };
  //the whole accumulator, with accl taken from the given register
  auto storeAcc = [&](uint32_t accl) {
    if(keepAcc) {
      if(accl != v::accl) out.push_back(rrr(ORR_16B, v::accl, accl, accl));
      return;
    }
    out.push_back(strq(v::acch, base, layout.acch));
    out.push_back(strq(v::accm, base, layout.accm));
    out.push_back(strq(accl, base, layout.accl));
  };
  out.push_back(ldrq(v::vs, base, vreg(vs)));
  out.push_back(ldrq(v::vt, base, vreg(vt)));
  uint32_t vte = broadcast(out, e);

  switch(op) {
  case Op::VMUDL: {  //acc = u16(vs) * u16(vt) >> 16; vd = accl
    products(out, vte, false, false, v::t0, v::t1);
    out.push_back(rrr(UZP2_8H, v::accl, v::t0, v::t1));
    out.push_back(MOVI0_2D | v::acch);
    out.push_back(MOVI0_2D | v::accm);
    storeAcc(v::accl);
    out.push_back(strq(v::accl, base, vreg(vd)));
    return;
  }

  case Op::VMUDM:    //acc = s16(vs) * u16(vt); vd = accm
  case Op::VMUDN: {  //acc = u16(vs) * s16(vt); vd = accl
    products(out, vte, op == Op::VMUDM, op == Op::VMUDN, v::t0, v::t1);
    out.push_back(rrr(UZP1_8H, v::accl, v::t0, v::t1));
    out.push_back(rrr(UZP2_8H, v::accm, v::t0, v::t1));
    out.push_back(sshr8h(v::acch, v::accm, 15));
    storeAcc(v::accl);
    out.push_back(strq(op == Op::VMUDM ? v::accm : v::accl, base, vreg(vd)));
    return;
  }

  case Op::VMUDH: {  //acc = s16(vs) * s16(vt) << 16; vd = clamp(acch:accm)
    products(out, vte, true, true, v::t0, v::t1);
    out.push_back(rrr(UZP1_8H, v::accm, v::t0, v::t1));
    out.push_back(rrr(UZP2_8H, v::acch, v::t0, v::t1));
    out.push_back(rr(SQXTN_4H,  v::t2, v::t0));
    out.push_back(rr(SQXTN2_8H, v::t2, v::t1));
    out.push_back(MOVI0_2D | v::accl);
    storeAcc(v::accl);
    out.push_back(strq(v::t2, base, vreg(vd)));
    return;
  }

  case Op::VMADH: {  //acch:accm += s16(vs) * s16(vt) (32-bit wrap); vd = clamp(acch:accm)
    products(out, vte, true, true, v::t0, v::t1);
    loadAcc(true, true, keepAcc);  //accl is unchanged, so it is only needed when kept
    out.push_back(rrr(ZIP1_8H, v::t2, v::accm, v::acch));
    out.push_back(rrr(ZIP2_8H, v::t3, v::accm, v::acch));
    out.push_back(rrr(ADD_4S, v::t2, v::t2, v::t0));
    out.push_back(rrr(ADD_4S, v::t3, v::t3, v::t1));
    out.push_back(rrr(UZP1_8H, v::accm, v::t2, v::t3));
    out.push_back(rrr(UZP2_8H, v::acch, v::t2, v::t3));
    out.push_back(rr(SQXTN_4H,  v::t4, v::t2));
    out.push_back(rr(SQXTN2_8H, v::t4, v::t3));
    if(!keepAcc) {
      out.push_back(strq(v::acch, base, layout.acch));
      out.push_back(strq(v::accm, base, layout.accm));
      if(accLoaded) out.push_back(strq(v::accl, base, layout.accl));  //an earlier kept op may have changed it
    }
    out.push_back(strq(v::t4, base, vreg(vd)));
    return;
  }

  case Op::VMADM:    //acc += s16(vs) * u16(vt); vd = clamp(acch:accm)
  case Op::VMADN: {  //acc += u16(vs) * s16(vt); vd = clampLow
    products(out, vte, op == Op::VMADM, op == Op::VMADN, v::t0, v::t1);
    loadAcc(true, true, true);
    //48-bit add of the sign-extended product: the low 32 bits (accm:accl) with the carry out
    //taken from an unsigned compare, the high 16 bits (acch) plus the product's sign.
    out.push_back(rrr(ZIP1_8H, v::t2, v::accl, v::accm));
    out.push_back(rrr(ZIP2_8H, v::t3, v::accl, v::accm));
    out.push_back(rrr(ADD_4S, v::t4, v::t2, v::t0));
    out.push_back(rrr(ADD_4S, v::t5, v::t3, v::t1));
    out.push_back(rrr(CMHI_4S, v::t2, v::t2, v::t4));
    out.push_back(rrr(CMHI_4S, v::t3, v::t3, v::t5));
    out.push_back(sshr4s(v::t0, v::t0, 31));
    out.push_back(sshr4s(v::t1, v::t1, 31));
    out.push_back(rrr(UZP1_8H, v::t2, v::t2, v::t3));  //-1 where the low 32 bits carried
    out.push_back(rrr(UZP1_8H, v::t0, v::t0, v::t1));  //-1 where the product is negative
    out.push_back(rrr(ADD_8H, v::acch, v::acch, v::t0));
    out.push_back(rrr(SUB_8H, v::acch, v::acch, v::t2));
    out.push_back(rrr(UZP1_8H, v::accl, v::t4, v::t5));
    out.push_back(rrr(UZP2_8H, v::accm, v::t4, v::t5));
    uint32_t result = op == Op::VMADN ? clampLow(out, v::accl) : clampMid(out);
    storeAcc(v::accl);
    out.push_back(strq(result, base, vreg(vd)));
    return;
  }

  case Op::VMADL: {  //acc += u16(vs) * u16(vt) >> 16; vd = clampLow
    products(out, vte, false, false, v::t0, v::t1);
    out.push_back(rrr(UZP2_8H, v::t0, v::t0, v::t1));
    loadAcc(true, true, true);
    out.push_back(rrr(ADD_8H, v::t2, v::accl, v::t0));
    out.push_back(rrr(CMHI_8H, v::t3, v::accl, v::t2));  //-1 where accl carried
    out.push_back(rrr(SUB_8H, v::accm, v::accm, v::t3));
    out.push_back(rr(CMEQZ_8H, v::t4, v::accm));
    out.push_back(rrr(AND_16B, v::t3, v::t3, v::t4));    //-1 where accm carried too
    out.push_back(rrr(SUB_8H, v::acch, v::acch, v::t3));
    uint32_t result = clampLow(out, v::t2);
    storeAcc(v::t2);
    out.push_back(strq(result, base, vreg(vd)));
    return;
  }
  }
}

}
