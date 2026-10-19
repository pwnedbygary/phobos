#pragma once

#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

//<termios.h>, which nall includes for its terminal code, defines VMIN (a terminal setting); here it's the VFPU's
//vmin instruction.
#undef VMIN

//On ARM64 the VFPU's matrix and dot products that compiled code calls run in NEON (recompiler-vfpu.cpp).
#if defined(ARCHITECTURE_ARM64) && !defined(COMPILER_MICROSOFT)
  #include <arm_neon.h>
  #define ALLEGREX_NEON
#endif

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
  //may only list plain memory, laid out as the PSP sees it (little-endian), and each piece of it once: memory
  //that appears at several addresses (VRAM's copies) is listed at one of them, the others left to read() and
  //write(), because compiled stores only steer clear of the pages holding compiled code, not of other addresses
  //for the same bytes. Code at addresses the table leaves out is interpreted, never compiled. Without a table,
  //compiled code calls the interpreter for every load and store. Changing it later needs a recompiler.reset().
  u8** pages = nullptr;

  //Pages, numbered as above, whose stores the owner must see (nonzero bytes): compiled stores go through write()
  //for them, as they do for pages holding compiled code. The PSP's memory map watches the pages of the textures the
  //GE keeps decoded (memory.hpp); the owner tells the recompiler of each page newly watched (recompiler.protect()).
  const u8* watched = nullptr;

  //Pages, numbered as above, whose entries in the page table never change (nonzero bytes), as the owner promises:
  //the PSP's main RAM and scratchpad, which nothing takes out or moves. A block goes on to a block in one of them
  //without looking at its entry again (emitChainTo()); without this, it always looks.
  const u8* steady = nullptr;

  //With the PSP's GE timing, the tags of the CPU's two caches, so its traffic with RAM can be counted (the GE's
  //contention with the CPU: Kernel::geContend(), docs/psp-core.md part 113): each 16 KiB of 64-byte lines (round 8's
  //cpu-memory: a chain of pointers 64 bytes apart hits within 16 KiB and misses every step over 32 KiB), in 128 sets
  //of two ways, the least recently used replaced (the ways aren't measured: two, as a MIPS core of its kind has). A
  //data line's key is its address's bits 6-31, so a cached mirror's lines and an uncached mirror's never meet (an
  //instruction line's, codeKey()); Dirty marks a data line a store has written since it came in. Compiled code looks a
  //line up in its set's most recently used way (recompiler.cpp and recompiler-memory.cpp, in blocks compiled for a
  //system started with the PSP's timing alone: "Fast" compiles none of it): the instructions' lines as a block starts
  //and as its code crosses into the next line, and each load's or store's. The rest is dataCacheMiss()'s and
  //instructionCacheMiss()'s: the other way, else a miss, the line read in (counted where it's RAM's) and the line it
  //replaces written back if dirty; an uncached access is counted as one of its own. The interpreter's instructions and
  //accesses, and those the recompiler leaves to it (misaligned, hardware's, stores to compiled code, a delay slot's
  //line), aren't counted. VRAM's and the scratchpad's lines are held like RAM's but not counted: telling them apart as
  //compiled code runs would cost what the look itself costs.
  struct Cache {
    static constexpr u32 Sets = 128, Dirty = 0x8000'0000, Empty = 0x7fff'ffff;  //(no line's key is Empty)
    bool enabled = false;  //the system started with the PSP's GE timing (System::power())
    u32 recent[Sets];      //each set's most recently used way's key (| Dirty), and the other's
    u32 older[Sets];
    u64 fills = 0, writebacks = 0, uncached = 0;  //lines read in from RAM, dirty ones written back to it, and uncached
                                                  //accesses to it, since the program started
    Cache() { reset(); }
    auto reset() -> void;
    static auto ram(u32 address) -> bool { return (address & 0x1fff'ffff) >> 26 == 2; }  //(0x0800'0000 on, 64 MiB)
    //An instruction line's key: its address's bits 13-31 (its set is its bits 6-12) less 0x4000, so the code a program
    //runs in RAM (0x0800'0000 to 0x09ff'ffff) has keys under 4096, which an ARM64 compare takes as it is
    //(emitInstructionCache()). Never Empty.
    static auto codeKey(u32 address) -> u32 { return (address >> 13) - 0x4000; }
  } dataCache, instructionCache;
  auto dataCacheMiss(u32 address, u32 store) -> u32;  //cache.cpp: returns the address
  auto instructionCacheMiss(u32 address) -> void;

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
  auto serialize(serializer& s) -> void;  //serialization.cpp: for save states
  auto instruction() -> void;
  auto execute(u32 address, u32 instruction) -> u32;
  auto executeSyscall(u32 address, u32 instruction) -> u32;
  auto run(u64 instructions, u64 from = 0) -> u64;
  //How many instructions the run under way has run so far, as its last block or instruction began. And how many it
  //runs in all: a syscall may bring that forward, to stop the run at something it made due sooner (a thread's wait
  //ending, a timer).
  u64 instructionsRun = 0;
  u64 runLimit = 0;
  //How many ran before the instruction executing now (the syscall the HLE kernel's clock catches up at), the same
  //count with either engine: inside a compiled block, the block's instructions before it count too.
  auto instructionsBefore() const -> u64;

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
  //changes a single bit of it, and its control/status register (FCR31): the rounding mode (bits 0-1), the exceptions'
  //flags, enables and causes (bits 2-17, kept but not acted on), the result of the last comparison (bit 23) and
  //flush-to-zero (bit 24).
  struct FPU {
    u32 r[32];
    n32 csr;
  } fpu;

  //Coprocessor 0 (system control). HLE runs the game in user mode, which barely touches it: its registers are
  //kept so mfc0 and mtc0 work, and the bad address of the last alignment error goes in register 8 (BadVAddr).
  //interrupts is the interrupt flag, which mfic reads and mtic sets: 1 with interrupts on, as a program starts, 0
  //with them held off (the HLE kernel's flag too: its sceKernelCpuSuspendIntr and ResumeIntr set it). halted is set
  //by the halt instruction, which waits for an interrupt (the HLE kernel wakes the CPU when it has something to run).
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
  template<typename Value, typename Operation> auto rounded(Operation operation, Value s, Value t) const -> f32;
  auto setArithmetic(u8 fd, f32 value, u32 s, u32 t) -> void;

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
  //prefix can clamp results to 0..1 or -1..1 ("saturate"), or leave lanes unwritten ("mask"). vnop, vsync, vflush,
  //the loads, stores, branches, and moves to and from integer and control registers don't use them: they stay for
  //the next instruction that does (but a move to a prefix's own control register, 128 to 130, sets it).
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
  //depends on the instruction (measured on a PSP with tools/psp-measure). Unknown leaves the host's NaN, for
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
  auto vfpuReadFour(u8 reg, u32 size, u32 prefix) const -> Vector;
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
  template<typename F> auto vfpuUnaryBits(u8 vd, u8 vs, u32 size, F function) -> void;
  template<typename F> auto vfpuLastLaneFirst(u8 vd, u8 vs, u32 size, F function, bool negating = false) -> void;

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

  //recompiler-vfpu.cpp: the VFPU's instructions compiled code calls with their operands worked out as the block was
  //compiled: the common ones with the prefixes at rest, their registers four to a word (a byte each); the
  //lane-by-lane ones with any prefixes (vfpuEach(): each lane described in 16 bits); and the matrix products of
  //matrices in place (on ARM64, in NEON)
  template<u32 Size, u32 Op> auto vfpuRestBinary(u32 vd, u32 vs, u32 vt) -> void;
  template<u32 Size> auto vfpuRestDot(u32 vd, u32 vs, u32 vt) -> void;
  template<u32 Size> auto vfpuRestScale(u32 vd, u32 vs, u32 vt) -> void;
  template<u32 Size> auto vfpuRestCompare(u32 condition, u32 vs, u32 vt) -> void;
  template<u32 Size, u32 Op> auto vfpuRestBits(u32 vd, u32 vs, u32) -> void;
  template<u32 Size> auto vfpuRestToFloat(u32 vd, u32 vs, u32 scale) -> void;
  template<u32 Size, bool Max> auto vfpuRestMinMax(u32 vd, u32 vs, u32 vt) -> void;
  template<u32 Size, bool Less> auto vfpuRestSet(u32 vd, u32 vs, u32 vt) -> void;
  template<u32 Size> auto vfpuRestHalfDot(u32 vd, u32 vs, u32 vt) -> void;
  template<u32 Op> auto vfpuRestPack(u32 d, u32 s, u32 size) -> void;
  template<u32 Size> auto vfpuRestConditionalMove(u32 d, u32 s, u32 which) -> void;
  template<u32 Op> auto vfpuRestCross(u32 d, u32 s, u32 t) -> void;
  template<u32 Size> auto vfpuRestRotate(u32 d, u32 s, u32 placement) -> void;
  template<u32 Op> auto vfpuLane(u32 s, u32 t) const -> u32;
  template<u32 Size, u32 Op> auto vfpuEach(u64 s, u64 t, u64 d) -> void;
  auto vfpuMatrix(u32 which, u32 registers, u32 size) -> void;
  template<u32 Size, bool Transposed, bool Homogeneous> auto vfpuTransform(u32 matrix, u32 t, u32 d) -> void;
  template<u32 Size, u32 Transposed> auto vfpuMultiply(u32 s, u32 t, u32 d) -> void;
  template<u32 Op> auto executeVFPU(u32 address, u32 instruction) -> u32;

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
      u8* bodies[SectionWords] = {};  //the same code past its prologue, where blocks go on to each other
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
    auto drop(u32 index) -> void;
    auto protect(u32 page) -> void;
    auto unwatched(u32 page) -> void;
    auto writable(u32 index) -> void;
    auto holds(u32 index) const -> bool { return index < sections.size() && sections[index]; }  //compiled code there
    auto run() -> u32;
    auto block(u32 address) -> u8*;
    auto emit(u32 address, u8*& body) -> u8*;
    auto emitEnd(u32 count, bool known, u32 target) -> void;
    auto emitChain(u32 count) -> void;
    auto emitChainTo(u32 count, u32 target) -> void;
    auto emitCounted(u32 count, std::vector<sljit_jump*>& leave) -> void;
    //Direct links (emitChainTo()): a block's way on to a block it knows as it's compiled is a jump that goes to the
    //code after it (the lookup) until that block is compiled, then straight to its body, and back to the lookup when
    //that block's section goes. Each such jump is an exit: where it is in the code, where its lookup is, the guest
    //address it goes to, and whether its own block's code is live (not once its section goes).
    struct Exit {
      u8* jump;
      u8* lookup;
      u32 target;
      bool live;
    };
    std::vector<Exit> exits;
    std::unordered_map<u32, std::vector<u32>> into;     //by section: the exits linked to its blocks
    std::unordered_map<u32, std::vector<u32>> waiting;  //by guest address: the exits to a block not compiled yet
    std::unordered_map<u32, std::vector<u32>> outOf;    //by section: its blocks' exits
    std::vector<std::pair<u32, sljit_label*>> linking;  //the exits of the block being compiled: targets and places
    //The block being compiled's first address, and with the PSP's GE timing, its body past the look its first
    //instruction's line takes in the instruction cache (emitInstructionCache()): where its exits going on to itself
    //go, that line still the most recent of its set (no other code runs in between but by way of run()).
    u32 blockStart = 0;
    sljit_label* lineHeld = nullptr;
    auto emitLink(u32 target) -> void;
    auto addExits(u32 index) -> void;
    auto link(u32 exit, u8* body) -> void;
    auto patch(u8* jump, u8* to) -> bool;
    auto emitPC(u32 address, bool delaySlot) -> void;
    auto emitInterpreter(u32 address, u32 instruction, u32 count, bool delaySlot) -> void;
    auto emitFallback(u32 address, u32 instruction, u32 count, bool delaySlot) -> void;
    //Code a native instruction rarely takes (an access off the fast path, an overflow, a division the host can't
    //make, an FPU mode or result its native code leaves to the interpreter, the VFPU's prefixes not at rest), kept
    //out of the block's straight line: emitted after the block's end, each from its jumps and back (emitCold()).
    std::vector<std::function<void ()>> cold;
    auto emitCold(const std::vector<sljit_jump*>& jumps, const std::function<void ()>& code) -> void;
    auto emitColdFallback(const std::vector<sljit_jump*>& jumps, u32 address, u32 instruction, u32 count,
                          bool delaySlot) -> void;
    auto isBranch(u32 instruction) const -> bool;
    //Where the delay slot being compiled goes after it, when that's known as it's compiled (emitDelaySlot()): pc and
    //pd aren't stored before such a delay slot, so its calls into the interpreter store them first (emitPC()).
    struct Destination {
      bool known = false;
      u32 target = 0;
    } destination;

    //recompiler.cpp: the game's registers a block holds in host registers, saved registers S3 on (emit()), and on
    //ARM64, whose ten saved registers leave room, the run's count in the next one (Counted: left())
    static constexpr bool Counted = Architecture::arm64;
    static constexpr u32 Held = SLJIT_NUMBER_OF_SAVED_REGISTERS - 3 - Counted < 7 ? SLJIT_NUMBER_OF_SAVED_REGISTERS - 3 - Counted : 7;
    //The run's count, where Counted: as compiled code runs, left() holds runLimit less instructionsRun, the
    //instructions the run has left, which each block's end counts down. instructionsRun in memory stays as it was
    //where the code began or last called the interpreter: it's brought up to date (storeCount()) before anything that
    //reads it runs (a call into the interpreter, whose syscalls read it and may move it and runLimit on, after which
    //left() is worked out again: loadCount()) and before the code leaves for run(). Otherwise each block's end counts
    //in instructionsRun itself.
    auto left() const -> sreg { return sreg(3 + Held); }
    auto storeCount() -> void;
    auto loadCount() -> void;
    auto emitCount(u32 count, std::vector<sljit_jump*>& leave) -> void;
    struct Holding {
      s8 slot[32];       //the host register holding each game register's value, or -1
      s8 held[Held];     //the game register each host register holds the value of, or -1
      s8 pending[Held];  //the game register each host register holds a new value for, not yet stored, or -1
      u32 used[Held];    //when each was last used: the least recently used is the first to go
      u32 pinned = 0;    //the host registers the instruction being compiled uses
      u32 clock = 0;
    } holding;
    auto take() -> u32;
    auto pinSources(u32 instruction) -> void;
    auto use(u32 index) -> op_base;
    auto def(u32 index) -> op_base;
    auto wrote() -> void;
    auto forget(u32 index) -> void;
    auto forgetAll() -> void;
    auto endsBlock(u32 instruction) const -> bool;

    //recompiler-ipu.cpp
    auto emitInstruction(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool;
    auto emitNative(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool;
    auto emitSPECIAL(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool;
    auto emitSPECIAL3(u32 instruction) -> bool;
    auto emitMultiply(u32 instruction, bool isSigned, s32 accumulate) -> void;
    auto emitTrapping(u32 address, u32 instruction, u32 count, bool delaySlot, u32 rd, bool subtract) -> void;
    //How a branch goes (emitBranch()): to target, known as it's compiled (Jump); where a register said, pc and pd
    //stored (Register); or to target if the taken jump is made, past the delay slot if not (Conditional; likely:
    //skipping the delay slot then). None for a branch with no native code.
    struct Branch {
      enum Kind : u32 { None, Jump, Register, Conditional } kind = None;
      u32 target = 0;
      sljit_jump* taken = nullptr;
      bool likely = false;
    };
    auto emitBranch(u32 address, u32 instruction) -> Branch;
    auto emitDelaySlot(u32 address, const Branch& branch, u32 count) -> void;
    auto emitCoprocessorBranch(u32 address, u32 instruction, u32 count) -> void;

    //recompiler-fpu.cpp
    auto emitFPU(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool;
    auto emitFlush(reg r, reg scratch) -> void;
    auto emitFlushedInputs(u32 instruction) -> void;
    auto emitFPUArithmetic(u32 address, u32 instruction, u32 count, bool delaySlot, s32 op) -> void;
    auto emitFPUCompare(u32 address, u32 instruction, u32 count, bool delaySlot) -> void;
    auto emitCOP2(u32 instruction) -> bool;

    //recompiler-vfpu.cpp
    auto emitVFPU(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool;
    auto emitVFPUCall(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool;
    auto emitEach(u32 address, u32 instruction, u32 count, bool delaySlot, u32 op, u32 size) -> bool;
    auto emitPrefixesBusy() -> sljit_jump*;
    auto prefixesAfter(u32 instruction) -> void;
    auto emitMFVC(u32 instruction) -> void;
    using Helper = void (Allegrex::*)(u32, u32, u32);
    auto matrixProduct(u32 which, u8 vd, u8 vs, u8 vt, u32 size, u32& first, u32& second, u32& third) -> Helper;
    //What the block being compiled knows of the prefixes here: each one's value, where it's known
    //(recompiler-vfpu.cpp).
    struct Prefixes {
      bool known[3] = {};  //pfxs, pfxt, pfxd
      u32 value[3] = {};
      auto atRest() const -> bool;
      auto busy() const -> bool;
    } prefixes;

    //recompiler-memory.cpp
    auto emitLoadStore(u32 address, u32 instruction, u32 count, bool delaySlot) -> bool;
    auto emitMemory(u32 address, u32 instruction, u32 count, bool delaySlot, bool store, u32 alignment, s16 offset,
                    const std::function<void ()>& access) -> void;
    auto emitDataCache(op_base base, bool store) -> void;
    auto emitInstructionCache(u32 address) -> void;

    bool enabled = false;
    bool chains = true;  //blocks go on to the next by themselves (emitChain()); tests turn it off to compare
    //The block being compiled calls the interpreter somewhere (emitInterpreter()), which may halt the CPU. (Calls
    //that halt it only by an exception, after which the block leaves, needn't say so: emitVFPUCall(), and a native
    //instruction's way to the interpreter, emitColdFallback().)
    bool calls = false;
    //How many instructions the last block ran, not yet counted in instructionsRun: each block sets it as it leaves
    //for run() (0 when it counted them itself, going on to the next: emitChain()), and as it calls the interpreter
    //for one (counting that one), so during that call it says how far the block has got.
    u32 executed = 0;
    bool inBlock = false;  //whether compiled code is running (it's what called execute(), if anything did)
    u32 codeMemory = 32_MiB;  //how much compiled code may fill before it all goes and compiling starts afresh
    bump_allocator allocator;
    std::vector<std::unique_ptr<Section>> sections;
    //The same sections, as plain pointers in a table of their own for compiled code to look the next block up in
    //(emitChain()): table points at sectionTable's first.
    std::vector<Section*> sectionTable;
    //A bit for each section there is (sections[index] set), 64 to a word: the only ones invalidateRange() visits.
    std::vector<u64> compiled;
    Section** table = nullptr;

    //The page table compiled stores use: the CPU's (pages above), except that pages holding compiled code are
    //left out, so stores there go through write() instead, whose owner then drops that code (invalidate()); and
    //so are the pages the owner watches (watched above).
    std::vector<u8*> writePages;
  } recompiler{*this};
};

inline auto Allegrex::instructionsBefore() const -> u64 {
  if(recompiler.inBlock) return instructionsRun + recompiler.executed - 1;
  return instructionsRun;
}

}
