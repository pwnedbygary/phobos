#pragma once

#include <array>
#include <bit>
#include <cmath>
#include <limits>

//<termios.h>, which nall includes for its terminal code, defines VMIN (a terminal setting); here it's the VFPU's
//vmin instruction.
#undef VMIN

//Sony Allegrex: the PSP's main CPU.
//
//What a CPU does: it reads one instruction at a time from memory and does what that instruction says ("add these
//two numbers", "load a word from this address", "jump over there"). Every instruction is one 32-bit word.
//
//The Allegrex speaks MIPS, the same family of instructions as the PS1's and the N64's CPUs (ares/ps1/cpu and
//ares/n64/cpu), with some differences:
//  - it's a 32-bit MIPS II core with a few newer MIPS32r2 instructions (ext, ins, seb, seh, wsbh, rotr, movz, movn);
//  - Sony added instructions of its own (min, max, bitrev, wsbw, halt, mfic, mtic), and put clz, clo, madd and
//    msub at different encodings than standard MIPS uses;
//  - its FPU (coprocessor 1) only works with single-precision floats, never doubles;
//  - coprocessor 2 is the VFPU, a vector unit that does math on up to four floats at once (see VFPU below);
//  - there's no TLB (the hardware that remaps addresses on other MIPS chips), so an address is just an address.
//
//Phobos emulates the PSP at a high level (HLE): the game's own code runs here, instruction by instruction, but the
//PSP's operating system doesn't run at all. When a game wants something from the system (open a file, start a
//thread, draw the screen), it executes a syscall instruction. Instead of jumping into Sony's kernel, the CPU passes
//the request to syscallHook, where Phobos's own implementation of that system function answers it.
//
//The system around the CPU decides what each address reaches (RAM, video memory, hardware registers), by
//implementing read() and write(), the way ares's ARM7TDMI is used by the Game Boy Advance.

namespace ares::PlayStationPortable {

struct Allegrex {
  using cu32 = const u32;
  using cs32 = const s32;

  enum : u32 { Byte = 1, Half = 2, Word = 4 };

  //memory: implemented by whoever owns the CPU (the PSP system, or a test).
  virtual auto read(u32 size, u32 address) -> u32 = 0;
  virtual auto write(u32 size, u32 address, u32 data) -> void = 0;

  //The page table, also set up by the owner: for each 4 KiB page of physical memory (the address's low 29 bits),
  //where that page is in the host's memory, or nullptr for memory the owner handles in read() and write() (such as
  //hardware registers). Compiled code loads and stores straight through it, skipping read() and write(), so it
  //may only list plain memory, laid out as the PSP sees it (little-endian). Without it, compiled code calls the
  //interpreter for every load and store. Changing it later needs a recompiler.reset().
  u8** pages = nullptr;

  //Why the CPU stopped a program. The numbers are the ones MIPS uses in its Cause register.
  enum class Exception : u32 {
    AddressLoad         =  4,  //an instruction fetch or load from an address that isn't aligned to its size
    AddressStore        =  5,  //the same for a store
    Syscall             =  8,  //a syscall the HLE kernel didn't answer
    Breakpoint          =  9,  //the break instruction
    ReservedInstruction = 10,  //a word that isn't an instruction this CPU has
    Overflow            = 12,  //add, addi or sub overflowed (addu, addiu and subu wrap around instead)
  };

  //The HLE kernel's entry point: called with a syscall's 20-bit code. It returns true when it handled the call.
  //By then pc already points at the instruction where the game continues, so the kernel can set a return value
  //in v0, or put the whole CPU state aside and switch to another thread.
  std::function<auto (u32 code) -> bool> syscallHook;

  //Exceptions nobody handles. HLE doesn't emulate the PSP's exception handlers, so these are bugs or
  //unimplemented features worth reporting; without a hook, the CPU simply halts.
  std::function<auto (Exception, u32 address) -> void> exceptionHook;

  //allegrex.cpp
  auto power(u32 entry) -> void;
  auto instruction() -> void;
  auto execute(u32 address, u32 instruction) -> u32;
  auto run(u64 instructions) -> u64;

  //The integer unit: 32 general registers (r0 always reads as zero), hi and lo (where multiply and divide put their
  //results), and the program counter.
  //
  //MIPS has "branch delay slots": the instruction right after a jump or branch always runs before the jump takes
  //effect, because the CPU has already fetched it by the time it knows where to go. So two addresses are kept:
  //pc, the instruction about to run, and pd, the one after it. Running an instruction moves pc to pd and pd on by
  //four; a taken branch changes pd only, so its delay slot (already in pc) still runs first.
  struct IPU {
    u32 r[32];
    u32 lo;
    u32 hi;
    u32 pc;  //the instruction about to run
    u32 pd;  //the instruction after it
  } ipu;

  //The FPU (coprocessor 1): 32 single-precision registers, kept as raw bit patterns so that moving a value never
  //changes a single bit of it, and its control/status register (FCR31): the rounding mode (bits 0-1), the result of
  //the last comparison (bit 23) and flush-to-zero (bit 24).
  struct FPU {
    u32 r[32];
    n32 csr;
  } fpu;

  //Coprocessor 0 (system control). HLE runs the game in user mode, which barely touches it: its registers are
  //kept so mfc0 and mtc0 work, and the bad address of the last alignment error goes in register 8 (BadVAddr).
  //interrupts is what mfic reads and mtic sets; halted is set by the halt instruction, which waits for an
  //interrupt (the HLE kernel wakes the CPU when it has something to run).
  struct SCC {
    u32 r[32];
    u32 interrupts;
    n1  halted;
  } scc;

  struct Pipeline {
    u32 address;      //where the instruction being executed came from
    n32 instruction;  //the instruction word itself
    u32 exception;    //set if it raised an exception, so compiled code knows to stop right after it
  } pipeline;

  //interpreter.cpp: picks the operation an instruction word names
  auto decoderEXECUTE() -> void;
  auto decoderSPECIAL() -> void;
  auto decoderREGIMM() -> void;
  auto decoderSPECIAL2() -> void;
  auto decoderSPECIAL3() -> void;
  auto decoderSCC() -> void;
  auto decoderFPU() -> void;
  auto decoderCOP2() -> void;
  auto decoderVFPU0() -> void;
  auto decoderVFPU1() -> void;
  auto decoderVFPU3() -> void;
  auto decoderVFPU4() -> void;
  auto decoderVFPU5() -> void;
  auto decoderVFPU6() -> void;
  auto decoderVFPU7() -> void;
  auto INVALID() -> void;

  //interpreter-ipu.cpp
  auto branch(u32 target) -> void;
  auto skipDelaySlot() -> void;

  auto ADD(u32& rd, cu32& rs, cu32& rt) -> void;
  auto ADDI(u32& rt, cu32& rs, s16 imm) -> void;
  auto ADDIU(u32& rt, cu32& rs, s16 imm) -> void;
  auto ADDU(u32& rd, cu32& rs, cu32& rt) -> void;
  auto AND(u32& rd, cu32& rs, cu32& rt) -> void;
  auto ANDI(u32& rt, cu32& rs, u16 imm) -> void;
  auto BEQ(cu32& rs, cu32& rt, s16 imm) -> void;
  auto BEQL(cu32& rs, cu32& rt, s16 imm) -> void;
  auto BGEZ(cs32& rs, s16 imm) -> void;
  auto BGEZAL(cs32& rs, s16 imm) -> void;
  auto BGEZALL(cs32& rs, s16 imm) -> void;
  auto BGEZL(cs32& rs, s16 imm) -> void;
  auto BGTZ(cs32& rs, s16 imm) -> void;
  auto BGTZL(cs32& rs, s16 imm) -> void;
  auto BITREV(u32& rd, cu32& rt) -> void;
  auto BLEZ(cs32& rs, s16 imm) -> void;
  auto BLEZL(cs32& rs, s16 imm) -> void;
  auto BLTZ(cs32& rs, s16 imm) -> void;
  auto BLTZAL(cs32& rs, s16 imm) -> void;
  auto BLTZALL(cs32& rs, s16 imm) -> void;
  auto BLTZL(cs32& rs, s16 imm) -> void;
  auto BNE(cu32& rs, cu32& rt, s16 imm) -> void;
  auto BNEL(cu32& rs, cu32& rt, s16 imm) -> void;
  auto BREAK() -> void;
  auto CACHE() -> void;
  auto CLO(u32& rd, cu32& rs) -> void;
  auto CLZ(u32& rd, cu32& rs) -> void;
  auto DIV(cs32& rs, cs32& rt) -> void;
  auto DIVU(cu32& rs, cu32& rt) -> void;
  auto EXT(u32& rt, cu32& rs, u32 lsb, u32 size) -> void;
  auto INS(u32& rt, cu32& rs, u32 lsb, u32 msb) -> void;
  auto J(u32 imm) -> void;
  auto JAL(u32 imm) -> void;
  auto JALR(u32& rd, cu32& rs) -> void;
  auto JR(cu32& rs) -> void;
  auto LB(u32& rt, cu32& rs, s16 imm) -> void;
  auto LBU(u32& rt, cu32& rs, s16 imm) -> void;
  auto LH(u32& rt, cu32& rs, s16 imm) -> void;
  auto LHU(u32& rt, cu32& rs, s16 imm) -> void;
  auto LL(u32& rt, cu32& rs, s16 imm) -> void;
  auto LUI(u32& rt, u16 imm) -> void;
  auto LW(u32& rt, cu32& rs, s16 imm) -> void;
  auto LWL(u32& rt, cu32& rs, s16 imm) -> void;
  auto LWR(u32& rt, cu32& rs, s16 imm) -> void;
  auto MADD(cs32& rs, cs32& rt) -> void;
  auto MADDU(cu32& rs, cu32& rt) -> void;
  auto MAX(u32& rd, cs32& rs, cs32& rt) -> void;
  auto MFHI(u32& rd) -> void;
  auto MFLO(u32& rd) -> void;
  auto MIN(u32& rd, cs32& rs, cs32& rt) -> void;
  auto MOVN(u32& rd, cu32& rs, cu32& rt) -> void;
  auto MOVZ(u32& rd, cu32& rs, cu32& rt) -> void;
  auto MSUB(cs32& rs, cs32& rt) -> void;
  auto MSUBU(cu32& rs, cu32& rt) -> void;
  auto MTHI(cu32& rs) -> void;
  auto MTLO(cu32& rs) -> void;
  auto MULT(cs32& rs, cs32& rt) -> void;
  auto MULTU(cu32& rs, cu32& rt) -> void;
  auto NOR(u32& rd, cu32& rs, cu32& rt) -> void;
  auto OR(u32& rd, cu32& rs, cu32& rt) -> void;
  auto ORI(u32& rt, cu32& rs, u16 imm) -> void;
  auto ROTR(u32& rd, cu32& rt, u8 sa) -> void;
  auto ROTRV(u32& rd, cu32& rt, cu32& rs) -> void;
  auto SB(cu32& rt, cu32& rs, s16 imm) -> void;
  auto SC(u32& rt, cu32& rs, s16 imm) -> void;
  auto SEB(u32& rd, cu32& rt) -> void;
  auto SEH(u32& rd, cu32& rt) -> void;
  auto SH(cu32& rt, cu32& rs, s16 imm) -> void;
  auto SLL(u32& rd, cu32& rt, u8 sa) -> void;
  auto SLLV(u32& rd, cu32& rt, cu32& rs) -> void;
  auto SLT(u32& rd, cs32& rs, cs32& rt) -> void;
  auto SLTI(u32& rt, cs32& rs, s16 imm) -> void;
  auto SLTIU(u32& rt, cu32& rs, s16 imm) -> void;
  auto SLTU(u32& rd, cu32& rs, cu32& rt) -> void;
  auto SRA(u32& rd, cs32& rt, u8 sa) -> void;
  auto SRAV(u32& rd, cs32& rt, cu32& rs) -> void;
  auto SRL(u32& rd, cu32& rt, u8 sa) -> void;
  auto SRLV(u32& rd, cu32& rt, cu32& rs) -> void;
  auto SUB(u32& rd, cu32& rs, cu32& rt) -> void;
  auto SUBU(u32& rd, cu32& rs, cu32& rt) -> void;
  auto SW(cu32& rt, cu32& rs, s16 imm) -> void;
  auto SWL(cu32& rt, cu32& rs, s16 imm) -> void;
  auto SWR(cu32& rt, cu32& rs, s16 imm) -> void;
  auto SYNC() -> void;
  auto SYSCALL() -> void;
  auto WSBH(u32& rd, cu32& rt) -> void;
  auto WSBW(u32& rd, cu32& rt) -> void;
  auto XOR(u32& rd, cu32& rs, cu32& rt) -> void;
  auto XORI(u32& rt, cu32& rs, u16 imm) -> void;

  //interpreter-scc.cpp
  auto ERET() -> void;
  auto HALT() -> void;
  auto MFC0(u32& rt, u8 rd) -> void;
  auto MFIC(u32& rt) -> void;
  auto MTC0(cu32& rt, u8 rd) -> void;
  auto MTIC(cu32& rt) -> void;

  //interpreter-fpu.cpp
  auto f(u32 index) const -> f32;
  auto setF(u32 index, f32 value) -> void;
  auto flushed(f32 value) const -> f32;
  auto toWord(f32 value, u32 mode) const -> u32;

  auto FABS(u8 fd, u8 fs) -> void;
  auto FADD(u8 fd, u8 fs, u8 ft) -> void;
  auto FCEIL(u8 fd, u8 fs) -> void;
  auto FCOMPARE(u8 fs, u8 ft, u8 condition) -> void;
  auto FCVTSW(u8 fd, u8 fs) -> void;
  auto FCVTWS(u8 fd, u8 fs) -> void;
  auto FDIV(u8 fd, u8 fs, u8 ft) -> void;
  auto FFLOOR(u8 fd, u8 fs) -> void;
  auto FMOV(u8 fd, u8 fs) -> void;
  auto FMUL(u8 fd, u8 fs, u8 ft) -> void;
  auto FNEG(u8 fd, u8 fs) -> void;
  auto FROUND(u8 fd, u8 fs) -> void;
  auto FSQRT(u8 fd, u8 fs) -> void;
  auto FSUB(u8 fd, u8 fs, u8 ft) -> void;
  auto FTRUNC(u8 fd, u8 fs) -> void;
  auto BC1(bool value, bool likely, s16 imm) -> void;
  auto CFC1(u32& rt, u8 rd) -> void;
  auto CTC1(cu32& rt, u8 rd) -> void;
  auto LWC1(u8 ft, cu32& rs, s16 imm) -> void;
  auto MFC1(u32& rt, u8 fs) -> void;
  auto MTC1(cu32& rt, u8 fs) -> void;
  auto SWC1(u8 ft, cu32& rs, s16 imm) -> void;

  //The VFPU (coprocessor 2): a vector unit for 3D math, which PSP games lean on to move vertices, light them and
  //animate them.
  //
  //Its 128 registers hold single-precision floats (as raw bits, like the FPU's), arranged as eight 4x4 matrices:
  //picture a stack of eight grids, each 4 rows by 4 columns. Register number r is row r / 32, matrix r / 4 % 8,
  //column r % 4. An instruction names each operand with a 7-bit register number and says how many lanes it works
  //on (1 to 4: a single, a pair, a triple or a quad), and the same number names a different shape depending on
  //that size:
  //  - a single is just that register;
  //  - a vector is a column or a row of a matrix: bit 5 picks a row instead of a column, the low two bits which
  //    one, and bit 6 where along it a pair (two further on) or a triple (one further on) starts;
  //  - a matrix (2x2, 3x3 or 4x4) uses the same bits, bit 5 then meaning "transposed": rows read as columns.
  //
  //The prefixes change how the next instruction reads its operands and writes its result, and are used up by it:
  //vpfxs sets the one for rs, vpfxt for rt and vpfxd for rd. A source prefix can reorder the lanes (a "swizzle"),
  //take their absolute values, negate them, or put a constant such as 1 or 1/2 in a lane instead; the destination
  //prefix can clamp results to 0..1 or -1..1 ("saturate"), or leave lanes unwritten ("mask").
  //
  //The condition codes (cc) hold vcmp's results, a bit per lane plus "any lane" and "every lane", which bvt and
  //bvf branch on and vcmovt and vcmovf choose by.
  struct VFPU {
    u32 r[128];
    u32 pfxs;
    u32 pfxt;
    u32 pfxd;
    u32 cc;
    u32 rcx[8];  //the random number generator's state: control registers 136 to 143 (see vfpuRandom())
  } vfpu;

  //The lanes of a vector operand, and the elements of a matrix operand (row r, column c at r * size + c).
  struct Vector { u32 lane[4]; };
  struct Matrix { u32 element[16]; };

  //The VFPU never passes a NaN through: whatever went in, a NaN result is always 0x7f800001 with a sign that
  //depends on the instruction (measured on a PSP with tools/psp-vfpu-measure). Unknown leaves the host's NaN, for
  //instructions not measured yet.
  enum class NaNSign : u32 {
    Unknown,   //not measured: the host's NaN
    Positive,  //always positive
    Input,     //the input's sign
    Negated,   //the opposite of the input's sign
    Product,   //the two inputs' signs multiplied (negative when exactly one is)
  };

  //interpreter-vfpu.cpp
  auto vfpuFloat(u32 bits) const -> f32;
  auto vfpuBits(f64 value) const -> u32;
  auto vfpuLine(u8 reg, u32 size) const -> std::array<u8, 4>;
  auto vfpuSquare(u8 reg, u32 size) const -> std::array<u8, 16>;
  auto vfpuRead(u8 reg, u32 size, u32 prefix) const -> Vector;
  auto vfpuWrite(u8 reg, u32 size, const Vector& value, u32 prefix) -> void;
  auto vfpuReadMatrix(u8 reg, u32 size) const -> Matrix;
  auto vfpuWriteMatrix(u8 reg, u32 size, const Matrix& value) -> void;
  auto vfpuPrefixesUsed() -> void;
  auto vfpuControl(u8 index) const -> u32;
  auto vfpuSetControl(u8 index, u32 value) -> void;
  auto vfpuRandom() -> u32;
  auto vfpuNaN(u32 result, NaNSign sign, u32 s, u32 t = 0) const -> u32;
  template<typename F> auto vfpuUnary(u8 vd, u8 vs, u32 size, F function, NaNSign sign = NaNSign::Unknown) -> void;
  template<typename F> auto vfpuBinary(u8 vd, u8 vs, u8 vt, u32 size, F function, NaNSign sign = NaNSign::Unknown) -> void;

  auto BV(bool value, bool likely, u8 bit, s16 imm) -> void;
  auto LVLQ(u8 vt, cu32& rs, s16 imm) -> void;
  auto LVQ(u8 vt, cu32& rs, s16 imm) -> void;
  auto LVRQ(u8 vt, cu32& rs, s16 imm) -> void;
  auto LVS(u8 vt, cu32& rs, s16 imm) -> void;
  auto MFV(u32& rt, u8 vd) -> void;
  auto MFVC(u32& rt, u8 index) -> void;
  auto MTV(cu32& rt, u8 vd) -> void;
  auto MTVC(cu32& rt, u8 index) -> void;
  auto SVLQ(u8 vt, cu32& rs, s16 imm) -> void;
  auto SVQ(u8 vt, cu32& rs, s16 imm) -> void;
  auto SVRQ(u8 vt, cu32& rs, s16 imm) -> void;
  auto SVS(u8 vt, cu32& rs, s16 imm) -> void;
  auto VABS(u8 vd, u8 vs, u32 size) -> void;
  auto VADD(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VASIN(u8 vd, u8 vs, u32 size) -> void;
  auto VAVG(u8 vd, u8 vs, u32 size) -> void;
  auto VBFY1(u8 vd, u8 vs, u32 size) -> void;
  auto VBFY2(u8 vd, u8 vs, u32 size) -> void;
  auto VC2I(u8 vd, u8 vs, u32 size) -> void;
  auto VCMOV(u8 vd, u8 vs, u32 size, bool onFalse, u8 bit) -> void;
  auto VCMP(u8 condition, u8 vs, u8 vt, u32 size) -> void;
  auto VCOS(u8 vd, u8 vs, u32 size) -> void;
  auto VCRS(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VCRSP(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VCST(u8 vd, u32 size, u8 constant) -> void;
  auto VDET(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VDIV(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VDOT(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VEXP2(u8 vd, u8 vs, u32 size) -> void;
  auto VF2H(u8 vd, u8 vs, u32 size) -> void;
  auto VF2I(u8 vd, u8 vs, u32 size, u8 scale, u32 mode) -> void;
  auto VFAD(u8 vd, u8 vs, u32 size) -> void;
  auto VFIM(u8 vd, u16 half) -> void;
  auto VH2F(u8 vd, u8 vs, u32 size) -> void;
  auto VHDP(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VHTFM(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VI2C(u8 vd, u8 vs, u32 size) -> void;
  auto VI2F(u8 vd, u8 vs, u32 size, u8 scale) -> void;
  auto VI2S(u8 vd, u8 vs, u32 size) -> void;
  auto VI2UC(u8 vd, u8 vs, u32 size) -> void;
  auto VI2US(u8 vd, u8 vs, u32 size) -> void;
  auto VIDT(u8 vd, u32 size) -> void;
  auto VIIM(u8 vd, s16 value) -> void;
  auto VLGB(u8 vd, u8 vs, u32 size) -> void;
  auto VLOG2(u8 vd, u8 vs, u32 size) -> void;
  auto VMAX(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VMFVC(u8 vd, u8 index) -> void;
  auto VMIDT(u8 vd, u32 size) -> void;
  auto VMIN(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VMMOV(u8 vd, u8 vs, u32 size) -> void;
  auto VMMUL(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VMONE(u8 vd, u32 size) -> void;
  auto VMOV(u8 vd, u8 vs, u32 size) -> void;
  auto VMSCL(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VMTVC(u8 index, u8 vs) -> void;
  auto VMUL(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VMZERO(u8 vd, u32 size) -> void;
  auto VNEG(u8 vd, u8 vs, u32 size) -> void;
  auto VNOP() -> void;
  auto VNRCP(u8 vd, u8 vs, u32 size) -> void;
  auto VNSIN(u8 vd, u8 vs, u32 size) -> void;
  auto VOCP(u8 vd, u8 vs, u32 size) -> void;
  auto VONE(u8 vd, u32 size) -> void;
  auto VPFXD(u32 prefix) -> void;
  auto VPFXS(u32 prefix) -> void;
  auto VPFXT(u32 prefix) -> void;
  auto VQMUL(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VRCP(u8 vd, u8 vs, u32 size) -> void;
  auto VREXP2(u8 vd, u8 vs, u32 size) -> void;
  auto VRNDF(u8 vd, u32 size, u32 exponent) -> void;
  auto VRNDI(u8 vd, u32 size) -> void;
  auto VRNDS(u8 vs, u32 size) -> void;
  auto VROT(u8 vd, u8 vs, u32 size, u8 placement) -> void;
  auto VRSQ(u8 vd, u8 vs, u32 size) -> void;
  auto VS2I(u8 vd, u8 vs, u32 size) -> void;
  auto VSAT0(u8 vd, u8 vs, u32 size) -> void;
  auto VSAT1(u8 vd, u8 vs, u32 size) -> void;
  auto VSBN(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VSBZ(u8 vd, u8 vs, u32 size) -> void;
  auto VSCL(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VSCMP(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VSGE(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VSGN(u8 vd, u8 vs, u32 size) -> void;
  auto VSIN(u8 vd, u8 vs, u32 size) -> void;
  auto VSLT(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VSOCP(u8 vd, u8 vs, u32 size) -> void;
  auto VSQRT(u8 vd, u8 vs, u32 size) -> void;
  auto VSRT(u8 vd, u8 vs, u32 size, u32 pass) -> void;
  auto VSUB(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VSYNC() -> void;
  auto VT4444(u8 vd, u8 vs, u32 size) -> void;
  auto VT5551(u8 vd, u8 vs, u32 size) -> void;
  auto VT5650(u8 vd, u8 vs, u32 size) -> void;
  auto VTFM(u8 vd, u8 vs, u8 vt, u32 size) -> void;
  auto VUC2I(u8 vd, u8 vs, u32 size) -> void;
  auto VUS2I(u8 vd, u8 vs, u32 size) -> void;
  auto VWBN(u8 vd, u8 vs, u32 size, u8 scale) -> void;
  auto VZERO(u8 vd, u32 size) -> void;

  //exceptions.cpp
  auto exception(Exception) -> void;
  auto addressError(Exception, u32 address) -> void;

  //The recompiler (dynamic recompiler, or dynarec): it translates the game's MIPS code into the host's own machine
  //code (ARM64 or x86-64) a block at a time, and runs that instead, which is far faster than decoding every
  //instruction again each time it runs. A block is the straight run of instructions from an address up to a branch
  //and its delay slot. Common instructions become native code; every other instruction is compiled as a call into
  //the interpreter, so the two always agree, and the interpreter stays the reference the recompiler is tested
  //against. It's built on ares's sljit framework (nall/recompiler/generic), as the N64's CPU is.
  struct Recompiler : recompiler::generic {
    Allegrex& self;
    Recompiler(Allegrex& self) : generic(allocator), self(self) {}

    //Compiled blocks are filed by the 4 KiB of memory (the "section") they start in, so a write to memory can
    //throw away exactly the code compiled from there. The Allegrex reaches the same memory through mirrors
    //(cached, uncached, kernel) that differ only in an address's top three bits, so sections are indexed by the
    //physical address, the 29 bits below those.
    enum : u32 {
      SectionSize  = 4_KiB,
      SectionWords = SectionSize / 4,
      SectionCount = 512_MiB / SectionSize,
    };

    struct Section {
      u32 mirror = 0;                 //the top three address bits its blocks were compiled for
      u8* blocks[SectionWords] = {};  //the compiled code starting at each word, or nullptr
    };

    //The compiled code finds the CPU's state through sljit's saved register S0, which holds the CPU's address:
    //a field is at that address plus its offset within the CPU.
    auto field(const void* member) const -> mem {
      return mem(sreg(0), (const u8*)member - (const u8*)&self);
    }
    auto gpr(u32 index) const -> mem { return field(&self.ipu.r[index]); }

    //recompiler.cpp
    auto reset() -> void;
    auto invalidate(u32 address) -> void;
    auto invalidateRange(u32 address, u32 size) -> void;
    auto run() -> u32;
    auto block(u32 address) -> u8*;
    auto emit(u32 address) -> u8*;
    auto emitInterpreter(u32 address, u32 instruction, u32 count, bool delaySlot) -> void;
    auto isBranch(u32 instruction) const -> bool;
    auto endsBlock(u32 instruction) const -> bool;

    //recompiler-ipu.cpp
    auto emitInstruction(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool;
    auto emitSPECIAL(u32 instruction) -> bool;
    auto emitBranch(u32 address, u32 instruction, u32 count) -> bool;
    auto emitBranchOutcome(sljit_jump* taken, u32 address, u32 target, bool likely, u32 count) -> void;
    auto emitJump(u32 target) -> void;

    //recompiler-memory.cpp
    auto emitLoadStore(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool;
    auto emitMemory(u32 address, u32 instruction, u32 count, bool delaySlot, bool store, u32 alignment, s16 offset,
                    const std::function<void ()>& access) -> void;

    bool enabled = false;
    u32 executed = 0;  //how many instructions the last block ran: each block sets it as it leaves
    bump_allocator allocator;
    std::vector<std::unique_ptr<Section>> sections;

    //The page table compiled stores use: the CPU's (pages above), except that pages holding compiled code are
    //left out, so stores there go through write() instead, whose owner then drops that code (invalidate()).
    std::vector<u8*> writePages;
  } recompiler{*this};
};

}
