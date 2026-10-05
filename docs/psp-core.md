# PSP core

**Status (2026-10-03):** started, at the user's request. Part 1, the Allegrex CPU's interpreter (integer and FPU
instructions) with host tests, is on branch `cursor/psp-core-2b67`; part 2, the recompiler, on
`cursor/psp-recompiler-2b67` on top of it; part 3, the VFPU, on `cursor/psp-vfpu-ares-2b67` on top of that; part 4,
compiled loads and stores straight to RAM, on `cursor/psp-fastmem-2b67`; the VFPU's measurements on a real PSP
after it; part 5, the memory map, on `cursor/psp-memory-2b67`; part 6, the loader, on `cursor/psp-loader-2b67`;
part 7, the first HLE functions, on `cursor/psp-hle-2b67`, which run pspdev's hello world from start to end on the
host; part 8, files and controls, on `cursor/psp-files-2b67`; part 9, the GE's display lists, on
`cursor/psp-ge-2b67`; part 10, drawing in 2D, on `cursor/psp-draw-2b67`; the program measuring the GE and the
controller on a PSP, on `cursor/psp-ge-measure-2b67`; part 11, drawing in 3D, on `cursor/psp-3d-2b67`, with its
measurements on `cursor/psp-3d-measure-2b67`; part 12, lighting, on `cursor/psp-lighting-2b67`. The user asked for the
whole feature to be stacked and merged at once (GitHub stack #106).
Part 13, the PSP in Phobos (an ares system, mia's medium, the Android app's entry), is on `cursor/psp-app-2b67`:
homebrew runs in the app on the RP6. Part 14, disc images (ISO and CSO, the disc's files, the drive), is on
`cursor/psp-umd-2b67`; part 15, save states, on `cursor/psp-states-2b67`. Parts 18 and 19, decryption and loading
modules, are on `cursor/psp-decrypt-2b67`.

## Decisions (the user's, 2026-10-03)

- **An original core, written for Phobos.** PPSSPP's code can't come in: it is GPL-2.0-or-later, and the firmware
  ares compiles into the native library can't be under the GPL (see LICENSE and the plan's row "Licenses of bundled
  resources"). A translation or rewording of its code would still be its code.
- **PPSSPP as a reference.** When the documentation runs out, or a design question gets stuck, PPSSPP's code and
  its test programs (pspautotests) may be read for how the PSP behaves or how a problem can be approached. The code
  here is written independently; nothing is copied, translated or reworded from PPSSPP or JPCSP.
- **Retail executables are decrypted with the published keys**, as PPSSPP does. The user accepted the risk: in the
  US the DMCA's anti-circumvention rule covers breaking encryption separately from copyright, and Nintendo's suit
  against Yuzu relied partly on it; Sony hasn't acted against PPSSPP in over a decade. The keys are facts; the
  scheme (the KIRK engine, the `~PSP` header) is written from the PSP Developer Wiki's description.
- **High-level emulation (HLE) of the operating system.** The game's code runs on the emulated Allegrex and its
  display lists on the emulated GE, but the PSP's operating system isn't run: the functions games import are
  Phobos's own. No firmware is needed. Booting Sony's kernel (low-level emulation) would need emulating chips that
  are largely undocumented (the Media Engine, the crypto engines, NAND, the system controller, whose firmware isn't
  in a firmware dump), and no PSP emulator has made that work, while HLE reaches nearly the whole library.
- **Firmware as an option, per module family**, as Flycast offers HLE or the real BIOS: the HLE kernel always runs,
  and a setting can load Sony's own library modules from the user's firmware where running them is easier than
  reimplementing them (the system fonts in `flash0:/font`, the audio codec libraries, perhaps video playback).
  The decryption keys can't come from there: they are in the PSP's crypto hardware, not in the firmware files.
  [`tools/psp-flash0-dump`](../tools/psp-flash0-dump/README.md) copies flash0's files from the user's PSP to its
  memory stick (2026-10-04, at the user's request); the copies stay on the user's devices, never in the repository.
- **A recompiler from the start, the interpreter as its fallback.** Speed and accuracy are the project's aims, so
  the CPU gets a dynamic recompiler (dynarec) early rather than as a late optimization. The interpreter stays: it is
  the reference the recompiler is tested against, and runs whatever the recompiler doesn't handle. The recompiler's
  MIPS-generic parts are kept apart so other MIPS systems can reuse them (ares's PS1 CPU is interpreter-only).
- **Code in the style of ares and near**, so it reads as part of the same codebase: plain structs with public
  members, `auto f() -> T`, one function per instruction named after its mnemonic, decoder tables built with
  macros as in ares's PS1 and N64 cores. Everything important is explained in plain language in the comments, for
  someone who has never seen a MIPS CPU.
- **What comes next** (2026-10-05): decryption and module loading (KIRK, the `~PSP` header, the published keys
  committed as PPSSPP does, at the user's word), then the HLE functions games ask for, then sound (sceAudio, then
  sceSas), then sceFont from the user's own flash0 fonts. The user's games to test against first: GTA Vice City
  Stories and Liberty City Stories, Metal Gear Solid: Peace Walker, Burnout Legends and Dominator, Lumines, Midnight
  Club 3, SOCOM: Fireteam Bravo, Snoopy vs. the Red Baron and Gunhound EX (kept as CHDs on the RP6's SD card).

## Sources

- **MIPS32 Architecture for Programmers, Volume II** (MIPS Technologies): the base instruction set. The Allegrex is
  a MIPS II core with a single-precision FPU and some MIPS32r2 instructions.
- **pspdev's PSP VFPU documentation** (<https://pspdev.github.io/vfpu-docs/>): the Allegrex's own instructions, and
  the VFPU, with tests behind its statements.
- **binutils' Allegrex support** (David Guillen Fandos, 2023, `gas/testsuite/gas/mips/allegrex.d`): encodings, and
  assembled examples that the CPU tests use as vectors.
- **Yet Another PlayStation Portable Documentation** (yapspd, hitmen): memory map, coprocessor registers, `halt`,
  `mfic` and `mtic`.
- **PSP Developer Wiki** (<https://www.psdevwiki.com/psp/>): the Allegrex, hardware registers, the PRX format and
  KIRK.
- **PSPSDK** (BSD-licensed headers): the operating system's functions, structures and NIDs.
- **PPSSPP and pspautotests**, for behavior the above don't cover; each use is recorded where the code depends on
  it.

## Design

- Under `ares/psp/` (namespace `ares::PlayStationPortable`), as ares's systems are: around the CPU and the other
  parts goes an ares system, which the runner presents the way it presents the others (picture, sound, controls,
  states). The CPU uses only nall and ares's integer types, so it's tested on the host without the rest of ares
  (`tests/allegrex/`, whose `prelude.hpp` stands in for `ares.hpp`).
- The CPU reads and writes memory through virtual `read()` and `write()`, as ares's ARM7TDMI does for the Game Boy
  Advance; the PSP's memory map (scratchpad, VRAM, main RAM, hardware registers) implements them.
- Imports: the loader writes each imported function's stub as `jr ra` with `syscall n` in its delay slot, `n`
  standing for the function's NID. The CPU passes the code to `syscallHook`; by then `pc` already holds the return
  address, so the HLE kernel can return a value in `v0` or switch threads by saving and restoring the CPU's state.
- Graphics: the GE runs the game's display lists into the emulated VRAM with a software renderer first; the GPU
  later.

## The recompiler

Part 2 (`ares/psp/cpu/recompiler.cpp`, `recompiler-ipu.cpp`), built on ares's recompiler framework
(`nall::recompiler::generic`, over sljit, for ARM64 and x86-64) as ares's N64 CPU is:

- **Blocks.** From an address, instructions are compiled up to a branch and its delay slot, the end of the 4 KiB
  section, or an instruction after which compiled code mustn't go on by itself (`syscall`, whose HLE handler may
  switch threads; `break`, `eret`, `halt`). The code works on the interpreter's own registers, so either engine
  can carry on where the other stopped, and leaves `pc` and `pd` exactly as the interpreter would.
- **Native and interpreted.** The common integer instructions that can't raise exceptions, and the branches and
  jumps, become native code. Every other instruction is compiled as a call to `execute()`, the interpreter's own
  path, so it behaves identically; that includes everything that can raise an exception, after which the block
  leaves (`pipeline.exception`). The FPU's branches go to the interpreter too, as does a branch in a section's last
  word (whose delay slot is in the next section).
- **Where blocks can't start**, between a branch and its delay slot (a thread the HLE kernel switched to may have
  stopped there), at a misaligned address, or in memory the page table leaves out (hardware registers, VRAM's
  other copies), the interpreter takes one step.
- **The cache** files blocks by the section they start in, by physical address (the low 29 bits), and remembers
  which mirror they were compiled for: code run through another mirror is compiled afresh, as its return
  addresses differ. When the code memory (32 MiB) runs low, everything is thrown away and compiled again.
- **Invalidation.** Whoever writes memory that can hold code calls `recompiler.invalidate()` (or
  `invalidateRange()`), which drops the whole section: the PSP's memory map will for every write, the CPU's and
  the ones it doesn't make (DMA, the HLE kernel loading a module); the tests' machine does it for every write.
- **Loads and stores straight to RAM** (part 4, `recompiler-memory.cpp`). The CPU's owner gives it a page table
  (`Allegrex::pages`): for each 4 KiB physical page, where it is in host memory, or nothing for memory read()
  and write() must handle (hardware registers). Compiled `lb`, `lbu`, `lh`, `lhu`, `lw`, `sb`, `sh`, `sw`,
  `lwc1`, `swc1`, `lv.s`, `sv.s`, `lv.q` and `sv.q` check the alignment, look up the page and access host memory
  directly; anything else (a misaligned address, a page without an entry) runs the instruction through the
  interpreter. Stores use a copy of the table without the pages that hold compiled code (`writePages`), so a
  store there goes through write(), which drops that code, and then the page is fast again. `lwl`, `lwr`,
  `swl`, `swr`, `ll`, `sc` and the VFPU's unaligned quads still go to the interpreter.
- **The MIPS-generic parts** (the cache by physical address, delay slots, the calls into the interpreter) are
  plain MIPS and would serve another MIPS CPU without a TLB, such as the PS1's R3000A; only the native
  instructions are Allegrex's.

Not yet: linking blocks to each other (each block returns to the dispatcher), keeping MIPS registers in host
registers, native FPU and VFPU arithmetic, tracking code finer than 4 KiB (a game that writes data next to its
code recompiles that code each time), and code that rewrites itself further on in the block it's running (the
rest of that block runs as compiled; the next run gets the new code).

Speed (`SANITIZE= ALLEGREX_BENCHMARK=1 tests/allegrex/run-tests.sh`, a loop of loads, stores and arithmetic, the
test build at -O1), in millions of instructions a second: on the Mac (ARM64), the interpreter 150, the recompiler
without a page table 505, with it 1206; in the Linux container (ARM64, 4 cores), 135, 219 and 1212. The PSP's CPU
runs at up to 333 MHz.

Tests: every test group runs on both engines, plus recompiler cases (blocks kept in the cache, code that rewrites
a function it already ran, a branch across a section, a thread switch at a syscall in a delay slot to a thread
stopped in another delay slot, a function called through two mirrors, an exception mid-block) and 500 generated
programs (forward branches of every kind, loads and stores, FPU instructions, overflows and misaligned accesses)
that must end in exactly the same state on both: every register, `pc`, `pd`, the FPU, the memory and the
exceptions. Four deliberately broken versions of the recompiler (a wrong compare, a wrong address after a
branch not taken, `nor` without the not, delay-slot instructions given the wrong `pc`) each failed them.

## Phases

1. The Allegrex's integer and FPU instructions in the interpreter, host tests (part 1).
2. The recompiler, with differential tests against the interpreter (part 2; its later steps are listed above).
3. The VFPU: registers, prefixes, instructions, tested against the pspdev documentation's descriptions (part 3).
4. Memory map (part 5); loading an unencrypted `EBOOT.PBP`, ELF or PRX (part 6); the first HLE functions (module
   start, threads, display, memory, standard output) and a homebrew test program run on the host (part 7); files
   and controls (part 8).
5. The GE: display lists, clearing and block transfers, and the picture (part 9); drawing in 2D (part 10); 3D
   (part 11); lighting (part 12); then mipmaps, lines, curved surfaces.
6. In Phobos: the system's entry, ISO and CSO images, a PSP touch layout, saves in a memory stick folder, states.
7. Retail executables: `~PSP` decryption.
8. Audio (`sceAudio`, then ATRAC3+ and MP3), video (PSMF), the optional firmware modules.

## Part 1: the Allegrex CPU

`ares/psp/cpu/`: `allegrex.hpp` (the registers and every instruction's declaration), `allegrex.cpp` (fetch and
run), `interpreter.cpp` (the decoder tables), `interpreter-ipu.cpp`, `interpreter-fpu.cpp`, `interpreter-scc.cpp`
(the instructions) and `exceptions.cpp`. An interpreter with branch delay slots (a taken branch sets the address
after the delay slot), likely branches that skip their delay slot when not taken, the Allegrex's encodings for
`clz`, `clo`, `madd`, `maddu`, `msub` and `msubu`, its `min`, `max`, `bitrev`, `wsbw`, `halt`, `mfic` and `mtic`,
unaligned loads and stores (`lwl`, `lwr`, `swl`, `swr`, little-endian), `ll` and `sc`, and the FPU: arithmetic,
compares and their branches, and conversions with the four rounding modes. FPU registers hold bit patterns, so moves
keep NaN payloads. Exceptions go to `exceptionHook` (HLE has no exception vectors), and VFPU instructions raise
ReservedInstruction until part 2.

Not checked against hardware: the result of dividing by zero (it gives what MIPS cores commonly do: `lo` = −1 or 1
by the dividend's sign, `hi` = the dividend), FPU arithmetic in rounding modes other than nearest (the host's
nearest is used), and conversions of NaN or out-of-range values (0x7fffffff, MIPS's default). (Since measured, and
followed now: division by zero in round 2; in round 3, the arithmetic rounds as FCSR's mode says, the host's
rounding switched for the one operation when it isn't the nearest (`cvt.s.w` too, as MIPS documents, though no round
measured it); the NaN an operation gives is picked as the PSP's is, not left to the host, as x86 hosts differ;
conversions give 0x80000000 for negative numbers out of range and -infinity; FIR reads 0x00003351; and a thread
starts with FCSR 0x00000e00. FCSR's exception flags, enables and causes are kept but not set or acted on: on a PSP
the enabled ones end the program, which no working game does. See psp-vfpu-measurements.md.)

Tests: `tests/allegrex/run-tests.sh` (14 groups, with the undefined-behavior and address sanitizers; until part 15
the address sanitizer ran on Linux only, as its runtime hung at start on macOS then. The PSP Core Tests workflow runs
them for changes to `ares/psp/`, nall or ares's types).
`harness.hpp` holds the test machine (a CPU over 64 KiB of RAM) and the instruction encoders.

## Part 3: the VFPU

`ares/psp/cpu/interpreter-vfpu.cpp`, with its decoder tables in `interpreter.cpp`: all of the VFPU's instructions,
one function each, named after their mnemonics. The register file is eight 4x4 matrices of floats; a 7-bit
register number names a single, a row or column vector (pair, triple, quad) or a matrix, by the operand size
(`vfpuLine()`, `vfpuSquare()`). Source prefixes swizzle, take absolute values, negate or substitute constants;
the destination prefix saturates or masks; every VFPU instruction but the prefix ones uses them up. As a PSP does
it, the math functions take the prefixes on their last lane alone (`vfpuLastLaneFirst()`); a swizzle past an
operand's size gives 0 in its lane, or leaves that lane's product out of the adders (`vdot`, `vhdp`, `vfad` and
`vavg`), which add up all four lanes whatever the size (`outsideLanes()`, `vfpuReadFour()`).
Denormals count as zero both ways: a result is rounded to a float's 24 bits as if exponents went on below 2^-126, the
smallest normal number, then flushed to a zero of its sign if it's still below (`vfpuBits()`; measured on products,
taken to hold for quotients). Only round-to-nearest exists. The rest was measured too (psp-vfpu-measurements.md,
rounds 2 and 3). The recompiler runs the VFPU through the interpreter for now (its branches included), as it does
the FPU.

The random number generator is the hardware's, as fp64 worked it out from a PSP's output (PPSSPP issue 16946):
a linear congruential generator, a xorshift and a Pell-like sequence with a carry, added together, their state
packed into the eight RCX registers (`vfpuRandom()`); `vrnds` spreads its seed over them; `vrndi` and `vrndf`
fill lanes from the last back, and their destination prefix only reaches the last lane. Implemented from that
description (no PPSSPP code), and checked against the user's PSP: its state at power on and the numbers of 64
seeds all match, including those that exercise the carry (psp-vfpu-measurements.md).

The math functions `vrcp`, `vnrcp`, `vrsq`, `vsqrt`, `vexp2`, `vrexp2`, `vsin`, `vcos`, `vnsin` and `vasin` (and
`vrot`'s sine and cosine) are the PSP's own: its quadratic interpolator with coefficients fitted from our
measurements (`vfpu-segments.hpp`, from `tools/psp-measure/fit.py`), exact on every measured input; see
psp-vfpu-measurements.md. So is `vlog2` below 4, from a fixed-point table and a cheaper straight-line path below 1;
from 4 up about half its results are one unit above the PSP's, which drops more precision there.

Not checked against hardware: `vrot` on its own (it uses vsin's and vcos's), `vwbn` (implemented from its
description; no test), and what reserved size combinations do (they raise ReservedInstruction, and leave the
prefixes). Which instructions use up the prefixes: every VFPU
arithmetic instruction, and `vnop`, as pspdev's documentation says (PPSSPP keeps them through `vnop`, but the
documentation rests on tests on hardware); not `vsync`, `vflush`, `vmfvc` and `vmtvc`, which the documentation
doesn't cover, as in PPSSPP; nor the loads, stores and moves to integer registers.

Tests: `tests/allegrex/vfpu.cpp`, twelve groups (addressing, arithmetic, prefixes, products, comparisons,
conversions, functions, matrices, moves, more instructions worked out by hand from the descriptions, the random
number generator, and edge cases as a PSP gives them, some of them runs it recorded), run on both engines; the
generated programs that compare the engines include VFPU instructions and its branches, and compare its registers,
prefixes and condition codes.

### Measuring the VFPU on a PSP

To make the math functions exact from our own data (the user chose this over adopting PPSSPP's GPL tables, and
has a PSP to run it on), `tools/psp-measure` records what a real PSP computes (it began as `tools/psp-vfpu-measure`,
a program of its own: see the end of this section). It's a homebrew program, built with pspdev's toolchain (`make`
in that folder, with `psp-config` on the PATH; the `phobos-linux` container has the toolchain in `/opt/pspdev`). On
a PSP with custom firmware its first round wrote, beside its EBOOT.PBP in `results/` (about 450 MB, resumable):

- every input of the range each math function reduces its argument to (`vrcp` over [1, 2), `vrsq` and `vsqrt` over
  [1, 4), `vexp2` and `vrexp2` over [1, 2), `vlog2` over [1/2, 2), `vsin`, `vcos` and `vasin` over k / 2^23), and a
  million spread-out inputs for each of them and their negated forms;
- the random number generator: its state and numbers at start, then the state after `vrnds` and 4096 draws for 64
  seeds (some chosen to exercise the carry), and `vrndi.q`'s lane order;
- `vadd`, `vsub`, `vmul`, `vdiv` and `vdot` on spread-out inputs (every sign, size, denormal, infinity and NaN).

`manifest.txt` says how each file's inputs are made; the files hold only the hardware's results. The user ran it
on 2026-10-03 (firmware 6.61): [psp-vfpu-measurements.md](psp-vfpu-measurements.md) has the results and findings.
The random number generator matched every word; `vadd`, `vsub`, `vmul` and `vdiv` matched every non-NaN result,
and match NaNs too now that the core gives the VFPU's own NaN (`NaNSign`); the math functions and `vdot` are next.
`tests/allegrex/measured/` keeps the generator and arithmetic files, which the tests check the core against.

A second round (about 230 MB, `manifest2.txt`) took what the first couldn't settle:

- `vlog2` over a whole binade for each size its results take above 4 (from 4, 16, 2^8, 2^16, 2^32 and 2^64);
- dot products, sums and averages built to show how the VFPU adds several numbers: one product, two, four of a
  similar size, exact products of short significands (`vdot`, `vhdp`, `vfad`, `vavg`);
- every half float through `vh2f`, and a million floats through `vf2h`;
- `div` and `divu` (division by zero included), and the FPU's conversions and arithmetic in each rounding mode;
- the instruction recorder: every VFPU instruction, size and immediate pspdev's assembler accepts (1216 entries in
  `ops.h`, from `ops.py`: `vwbn`'s 256 immediates, `vrot`'s 32 placements, every `vcmp` condition and constant,
  and 240 random prefix combinations), each copied into a tiny function and run on 64 random register states,
  recording matrix 2 and the condition codes. A real PSP stops a program at an instruction it doesn't have, which
  is why only what the assembler accepts is in it, and why it runs late.

The user's first run of round 2 (2026-10-04) switched the PSP off around the divide step, which itself is safe (its
`div` is the bare instruction, no divide-by-zero trap after it, as the built program shows). So the program now keeps
the PSP awake (`scePowerTick` as each test starts and with every chunk it writes, against the power-save timer); each
test's line says "running" as it starts, so the last line names the test the PSP stopped in; the FPU's conversions
and arithmetic, never run on a PSP before, come last, after the recorder; and a test that didn't finish runs once
more, then, if it stops again, is given up on: its `.part` becomes `<name>.stopped` and the round goes on without it.
A write that fails doesn't count as the PSP stopping, and a retry whose marker (`<name>.again`) can't be written
stops the round and says so rather than running unmarked. The results are unchanged: in PPSSPPHeadless the new
version's files are the old one's, byte for byte.

A third round (about 6 MB, `manifest3.txt`) is for what the second left open, and the FPU probes try one value
each, each given up on after a single stop: see [the measurements](psp-vfpu-measurements.md) and
[the tool's README](../tools/psp-measure/README.md).

Since 2026-10-04, at the user's request, this program and the GE's (below, "Measuring the GE and the controller on
a PSP") are one, `tools/psp-measure`: one EBOOT.PBP whose menu (up and down pick a line, X runs it) offers every
round of both, starting afresh, and leaving, and comes back after each round. Starting afresh asks first, then
renames `results` to `results-1` (or the next number that's free) and starts an empty one, so every test runs
again; nothing is deleted. The VFPU's and the FPU's files go to `results/vfpu`, the GE's to `results/ge`, and every
test of both is written, run once more and given up on the same way (`results.c`). The results are unchanged: in
PPSSPPHeadless the combined program's files are the two old programs', byte for byte, but for the manifests' wording
and `controller-timing.bin`, whose times are measured (the same reads waited). What round 1 and round 3 record "as
found" (the random number generator's state, FCSR) is taken when the program starts, so an earlier round in the same
session can't change it: round 1's random number test runs only while the generator is as it started.

`make SMOKE=1` builds a quick version (round 3's VFPU and FPU tests, the FPU probes and the GE's tests straight
away, the big tests cut short) for trying the program in PPSSPP's PPSSPPHeadless first (with `-i` and
`--graphics=software`), which `phobos-linux` has in `/opt/tools/ppsspp`; it says nothing about a PSP. `compare.sh`
checks whatever VFPU and FPU files a folder has, from any round.

## Part 5: the memory map

`ares/psp/memory/`: what each address reaches. The Allegrex has no TLB, so the four windows (user, uncached,
kernel, kernel uncached: an address's top three bits) all reach the same physical memory, the low 29 bits; under
HLE nothing keeps the game out of the kernel's. Physical memory is the scratchpad (16 KiB at `0x00010000`), VRAM
(2 MiB at `0x04000000`, seen four times in a row up to `0x047fffff`) and main RAM (32 MiB at `0x08000000`, or 64
MiB as on later models). Anything else (the hardware registers, the boot ROM) is empty for now: reading gives 0,
writing goes nowhere, and `unmapped()` is told, since under HLE that means a bug or something not emulated yet.
The bytes are kept little-endian, as the PSP sees them. VRAM's first and third copies show it as it is; the second
and fourth rearrange it in 32-byte pieces, for the GE's depth buffer, which the GE itself reaches as the fourth copy
shows it, in order (`vramOffset()`, `vramSeen()`; measured: see "Round 3's results").

Every change, the CPU's stores and the loader's or HLE functions' copies alike, is reported to `written()`, which
the CPU's owner points at the recompiler so that code compiled from there is dropped. A change to VRAM is reported
for all four copies, as they're four physical addresses for the same bytes, each where that copy sees it (past one
32-byte piece, every 16 KiB it touches wherever a rearranging copy is either side: the rearranging stays inside
those 16 KiB). `buildPages()` fills the CPU's page table for compiled loads and stores, listing VRAM at its first
copy only: compiled stores only steer clear of the pages holding compiled code, so a second address for the same
bytes would let a store change compiled code unseen. The other copies go through `read()` and `write()`, and the
recompiler interprets code at any address the table leaves out. `power()` keeps the buffers while their sizes stay
the same, so the table stays valid. `copyIn()`, `copyOut()`, `fill()` and `readString()` serve the loader and the
HLE functions, piece by piece through the rearranging copies; a range that crosses an area's end fails rather than
running over.

Not yet: the hardware registers HLE may still need (the GE's, for one).

Tests: `tests/psp/run-tests.sh`, the PSP system's own suite, built like the CPU's with the same sanitizers and run
by the PSP Core Tests workflow: the windows, each area's edges and what's past them, the hooks, the copies, the page
table, and the CPU on the memory map on both engines, including code that rewrites a function it already ran, in
RAM and in VRAM through another copy, both ways; and VRAM's rearranging copies, each way the other's reverse and the
depth buffer seen through them as the PSP showed it. Four deliberately broken versions (stores not reported; VRAM's
copies not shared; all four copies in the page table; code at unlisted addresses compiled) each failed them, and four
more of the copies (none rearranging, the fourth like the second, bulk copies in one piece, changes reported at one
offset) failed them too.

## Part 6: the loader

`ares/psp/kernel/loader.cpp`: puts a program in memory, as the PSP's kernel does before starting it. PSP programs
are MIPS ELF files: static executables (type 2, linked at fixed addresses; pspdev links them at `0x08804000`) and
relocatable modules (PRX, type `0xffa0`, linked as if at 0). Games and newer homebrew are PRXs, usually inside an
`EBOOT.PBP`, whose header gives the offsets of its eight parts; the program is the seventh, `DATA.PSP`
(`programInPBP()`).

- **Segments**: each loadable program header is copied to memory (a PRX's moved to the base the kernel picks) and
  its zeroed part filled.
- **Relocations** (PRX only), from the relocation sections (type `0x700000a0`), or the program headers of that type
  when there are no such sections: a word address for `j`/`jal`, whole pointers, and addresses built by `lui` and an
  instruction adding a signed lower half, where moving the address may carry into the upper half, so each `lui`
  waits for the lower half after it (one `lui` can serve several). A plain 16-bit relocation gives the same 16
  bits and completes a waiting `lui` too, as some retail modules pair them. The info word's segment numbers are program
  header indexes: the offset counts from the first, and the second's address is added. pspdev's PRXs have a single
  segment at 0, where adding that segment's address and adding how far it moved agree; retail modules with more
  segments will settle which is meant (phase 7). The packed form (`0x700000a1`) isn't read yet: a program with it is
  refused, whatever else it has.
- **The module info**: its section (`.rodata.sceModuleInfo`), or in a stripped PRX the first program header's
  physical address, which holds its file offset. Its name, version, attributes, `gp`, and its two tables:
- **Imports**: per library, the NIDs of the functions called and a stub for each, which becomes `jr ra` with
  `syscall n` in its delay slot; the kernel picks `n` for each library and NID (`ImportCode`). Variable imports are
  listed in `skipped`.
- **Exports**: per library, the NIDs and addresses of its functions and variables; the module's own entry (no
  library name) has `module_start` (NID `0xd632acdb`) and `module_info` (`0xf01d73a7`).
- **Refused, with the reason**: anything that isn't a MIPS ELF program, a segment that runs past the file or doesn't
  fit in memory, an unknown relocation type or segment number, packed relocations, no module info, tables pointing
  outside memory, and encrypted programs (`~PSP`) that can't be decrypted. (An encrypted program is decrypted first,
  and the ELF inside it loaded: part 18.)

Sources: pspsdk's headers (`psploadcore.h`, `pspmoduleinfo.h`, `pspimport.s`) for the tables, its `psp-prxgen` for
how pspdev writes a PRX's relocations, the PSP Developer Wiki's "PRX File Format" (which the user saved for us when
the wiki's bot check blocked fetching it) for the `~PSP` header, and programs built with pspdev's toolchain, read with
`psp-readelf`.

Tests (`tests/psp/loader.cpp`): programs built in the test by `elf.hpp` (no binaries in the repository): a PRX with
every relocation type, whose moved code then runs on both engines and builds the right addresses (including one where
only the move makes the lower half carry, and a `lui` shared by two lower halves); the same PRX stripped of its
sections; relocations in a program header next to kept sections; a static executable; a PBP; and each refusal.
Three broken versions (no carry, `jal` targets not moved, a `lui` adjusted twice) each failed them, as did the two
mistakes Bugbot's review found in the first version (a `lui` left waiting when its lower half came as a 16-bit
relocation; packed relocations not noticed when sections were kept). `tools/psp-test-programs/build.sh <folder>` builds a real hello world
(static, PRX and EBOOT) with pspdev's toolchain; with `PSP_TEST_PROGRAMS=<folder>` the tests load those too.

## Part 7: the first HLE functions

`ares/psp/kernel/kernel.cpp` (with `threads.cpp`, `sysmem.cpp`, `io.cpp`, `display.cpp`, `system.cpp`): Phobos's own
version of the PSP's operating system, as far as a program sees it. The loader asks it for a syscall code per import
(`importCode()`); the CPU passes each syscall to `syscall()`, which runs the function, a member named after it
(`sceKernelCreateThread()`), with the arguments in a0-a3 and t0-t3 and the result in v0.

- **NIDs are worked out from names**: a function's NID is the first four bytes of the SHA-1 hash of its name, as a
  little-endian word (`nid()`, SHA-1 written from FIPS 180-1, as nall has none). All 46 imports of pspdev's hello
  world matched names in pspsdk's headers that way, so the functions are listed by name, with no table of numbers
  copied from anywhere. A function the kernel doesn't have is noted once and returns
  `SCE_KERNEL_ERROR_LIBRARY_NOT_YET_LINKED`.
- **Threads**: each has its own registers (integer, FPU, VFPU), put aside while another runs. The ready thread with
  the highest priority runs, the one ready longest among equals; one that's woken with a higher priority takes over.
  A thread's entry function returns to a trampoline in kernel memory (`syscall` with the kernel's own code), which
  ends it. A function that makes its thread wait sets its result when the thread wakes.
- **Time**: cycles at 333 MHz, one per instruction; the vertical blank 59.94 times a second. When every thread waits,
  the clock jumps to the next delay, timeout or blank; if none is coming, the kernel says every thread waits on
  another, and stops.
- **The program**: `load()` starts afresh, as the PSP does when a game is chosen (whatever an earlier program left,
  having exited included, goes; a load that fails leaves nothing behind either). It takes a PBP or an ELF, puts a PRX
  at the start of the user partition, reserves the program's memory exactly where it is, one block from its first
  segment to the end of its last as the PSP's loader gives a module (two segments may share a 256-byte step, as a
  retail PRX's data starts right after its code; a program whose segments overlap is refused), and starts its first
  thread at the entry
  point with the path as its argument (argv[0]), its `gp`, a 256 KiB stack, and the top 256 bytes of the stack as
  the kernel's area (`k0`). A thread's start argument must fit on its stack and be readable, or starting it fails;
  the program's path may be up to 4 KiB.
- **Functions** (54): threads (create, start, exit, delete, delay, sleep and wakeup, wait for an end, status),
  semaphores, lightweight mutexes (their state in the program's own memory, `SceLwMutexWorkarea`), the clock, the
  user partition's memory (blocks from the lowest place, the highest or an address, 256-byte aligned; stacks from the
  top), standard input, output and error (output goes to `output()`), the display's frame buffer and vertical blank,
  the GE's memory, leaving the program, system settings (English, cross confirms), and failing network calls.
  Files come next: until then opening one says it isn't there.

Error codes come from pspsdk's `pspkerror.h`, and those it lacks (the lightweight mutex's, the allocation type's,
file not found) from uOFW's `errors.h`.

Tests (`tests/psp/kernel.cpp`): NIDs; two programs written in the test, run on both engines: threads that start,
preempt, delay and wait for each other's end, and a semaphore, a mutex held across threads, and sleep and wakeup,
each checked by what they print and leave in memory; the user partition; start arguments; the program's memory; a
program run to its exit, loaded again and run again; unknown functions; the vertical blank and the clock. Four broken versions (priorities inverted, results lost on
waking, a SHA-1 constant off by one, threads not ending at their return) each failed them, as did the first
version's mistakes the reviews found: a start argument copied past its stack, or from an unreadable address,
without an error; the program's memory reserved elsewhere when its own place was taken; and a second program loaded
after one exited running nothing, with a failed load leaving memory reserved; and the program's path passed to its
first thread unchecked. With `PSP_TEST_PROGRAMS`, pspdev's hello world runs from start to end
as a static executable, a PRX and an EBOOT on both engines: it prints `hello from a PSP program 42`, draws it on the
debug screen (pixels lit in the frame buffer it gave the display) and leaves.

## Part 8: files and controls

`ares/psp/kernel/io.cpp` and `ctrl.cpp`.

- **Devices are host folders.** `mount("ms0", folder)` makes the memory stick a folder on the host (and `disc0` the
  game's disc, until disc images come in phase 6); `umd0:` and `fatms0:` are the PSP's other names for them. A path
  is worked out in the kernel: relative paths start from the working folder (the program's own at first, however
  its path is written), either slash separates names, `.` and `..` are resolved there, and a path that would climb
  above its device, or holds a colon (which a host might read as a drive), is refused; and what a path really names,
  symbolic links followed, must be inside the folder, so a link in it can't lead out either. The PSP's FAT ignores
  case, so each name is found whatever its case on the host (on a case-sensitive host such as Linux; macOS's file
  system ignores case itself); a name not there yet keeps the case it's given.
- **Files**: `sceIoOpen` with the PSP's flags (read, write, create, truncate, append, exclusive), `sceIoRead`,
  `sceIoWrite`, `sceIoLseek` (64-bit, in a2 and a3) and `sceIoLseek32`, `sceIoClose`; each open file keeps its own
  position. Folders: `sceIoDopen`, `sceIoDread` (`.` and `..` first except at a device's top, then the names
  alphabetically; with the entry's private part asked for, the 8.3 short name and the long one), `sceIoDclose`,
  `sceIoMkdir`, `sceIoRmdir`, `sceIoRemove`, `sceIoRename`, `sceIoChdir`, `sceIoGetstat` (`SceIoStat`: kind, size,
  and the times, from nall's `inode::timestamp`). Error codes are the PSP's errno ones, from uOFW's `errors.h`.
  Not yet: the asynchronous versions many games use, `sceIoDevctl` and `sceIoIoctl`, and disc images.
- **Controls**, following uOFW's reading of the PSP's controller driver (`ctrl.c`): the system sets
  `controller.buttons` (pspsdk's `PSP_CTRL_*` bits) and the stick; they're sampled at each vertical blank, or, if
  the program sets a sampling cycle (5555 to 20000 microseconds; anything else is refused), on that timer instead.
  The last 64 samples are kept, each with its time, and the stick if sampling was analog (else it reads centred).
  A game sees only its own buttons (the pad, face buttons, shoulders, start, select, hold, and the bit telling it
  the system took the controls), not volume or the screen button.
  - The peek functions give the last samples at once, oldest first. The read ones give the samples that came since
    the last read and how many; they wait only if none has, so a program that waits for the vertical blank and then
    reads finds that frame's sample at once. Only one thread may wait to read: the PSP waits on an event flag made
    for one waiter, and refuses a second (`EVF_MULTI`). Negative versions invert the buttons. A count is a byte,
    and 64 or more is refused.
  - The latch gathers, between reads, the buttons pressed, those let go, and those held and not held at some
    sample. Reading it starts it afresh and doesn't wait. pspsdk's notes say a second read in one sampling cycle
    waits for the next sample, which uOFW's code doesn't do; the PSP itself could settle it. uOFW's code for a
    game's latch also differs from its code for the system's latch, and from pspsdk's descriptions, in what counts
    as pressed and as held; we follow the system's, which agrees with pspsdk.

`Kernel::run()` now takes a budget of the PSP's time in cycles, not instructions: while every thread waits, the clock
jumps, and that time counts, so running a frame's worth runs one frame.

Tests (`tests/psp/files.cpp`, each with its own temporary host folder): every open mode, reading, writing and seeking;
folders, status, listing, renaming, removing, relative paths; paths kept inside their device, aliases, backslashes,
unmounted devices; short names; the controller's samples peeked (times, order, stick, buttons games don't see),
read (how many were new, more than asked for, at most 63 waiting), latched, on a sampling cycle, and waited for by
two threads at once; and programs that read it each frame until the cross button, pressed on the tenth, by reading
alone or by waiting for the frame and then reading. With `PSP_TEST_PROGRAMS`, `tools/psp-test-programs/system` runs through newlib:
it writes a file and reads it back, reads one the host put there, lists the folder, then waits for the cross button
and reports the stick; its output and the file it wrote are checked. Broken versions each failed them: `..`
passed through to the host, names matched with their case (on Linux, whose file system keeps case; CI runs there),
the controller never sampled, the sampling cycle ignored, the program's folder found by its last `/` only, and nine
for the controller (reads always waiting, which halves the frame-reading program's speed; the latch not started
afresh; a second waiter allowed; held buttons not gathered; system buttons shown; times taken at the read; peeks
of the latest only; no limit on waiting samples; reads in the wrong order). Bugbot's reviews, read in full, found symbolic links leading out of the
folder, a sample count whose size could overflow its check, a sampling cycle stored but never used, and the program's
folder missed when its path used backslashes: fixed, with tests the old code fails. The last review found the
latch emptied for a second thread waiting on it; uOFW's driver showed the latch read doesn't wait at all, and that
buffer reads wait only when no sample is new (always waiting would have halved the speed of a program reading each
frame after the vertical blank), so the controller was reworked to match it. One claim, that
`sceIoLseek`'s 64-bit offset comes in a1 and a2, was wrong: psp-gcc's own calls put it in a2 and a3 (EABI aligns it
to an even register pair) and `whence` in t0, and the system program's `fseek` through newlib confirms it.

## Part 9: the GE's display lists

`ares/psp/ge/` (the GE) and `ares/psp/kernel/ge.cpp` (its driver), with the kernel pieces they needed.

- **The GE** reads display lists by itself: 32-bit commands, the top 8 bits saying which, the low 24 its argument.
  It keeps each command's last word (what `sceGeGetCmd` reads), the matrices (each element a float's top 24 bits, as
  `sceGeGetMtx` hands them back) and its list registers: where it is, where it must stop (the stall address, which the
  program moves on as it writes), the offset, and two levels of CALL. JUMP, CALL and the vertex and index addresses
  take BASE's top four bits and add the offset (ORIGIN, OFFSET_ADDR); the next PRIM carries on where the last left
  off. FINISH or SIGNAL, then END, stop it for the driver; a third CALL or a stray RET faults. Commands come from
  pspsdk's GU library (BSD), which writes them, and the registers from uOFW's reading of the driver.
- **Vertices** (`vertex.cpp`): every layout the vertex type describes (weights, texture coordinates, color, normal,
  position; 8-bit, 16-bit or float; indices; morph targets counted in the size), each part at a multiple of its own
  size. Through mode (2D) keeps positions in pixels, depth unsigned. Not yet: morphing (the first target is used).
- **Drawn so far**: clearing (a sprite in clear mode fills its rectangle, inside the scissor and the drawing region,
  writing color, alpha (where the stencil lives) and depth as CLEAR_MODE says, in each frame buffer format), and block
  transfers (rectangles of 16- or 32-bit pixels between images, `sceGuCopyImage`). The rest of drawing (textures,
  blending and the tests, triangles, 3D) comes next.
- **The driver** (sceGe_user), following uOFW's reading of the PSP's own: a queue of up to 64 lists, at its end or
  (paused, for `sceGeContinue`) its front; lists freed are reused oldest first, as the PSP's free list does; stall
  addresses; `sceGeListSync` and `sceGeDrawSync`, their states and their waits; finish and signal callbacks (a
  finish callback runs before the next list starts, and before anyone waiting is told);
  SIGNALs that suspend the GE until their callback returns (so it can rewrite the list ahead), let it go on, pause it
  at the next FINISH, stop the next FINISH ending the list, or jump, call and return using the list's own stack;
  saving and restoring the GE's state, in Phobos's own layout in the program's 2 KiB. Not yet: `sceGeBreak`, the
  debugger's breakpoints, SIGNALs that patch texture or CLUT addresses, and what uOFW shows differs for programs
  built with SDKs before 2.0.
- **Calls into the program** (`kernel/interrupts.cpp`): the GE's callbacks run as the PSP runs interrupt handlers,
  on top of whatever thread is running (or none): its registers are put aside, the function runs on the kernel's
  interrupt stack with (id, argument, list) and the global pointer it was registered with, and returns through a
  trampoline that puts them back. A call starts at the end of the system function that caused it, or as the kernel's
  loop goes round; not during another, nor while the program holds interrupts off (`sceKernelCpuSuspendIntr`). A
  handler can't wait (`ILLEGAL_CONTEXT`, as on the PSP); a thread it wakes runs once it returns.
- **The kernel also gained** event flags (all or any bits, clearing, one waiter or several, first come or by
  priority, timeouts; the order errors are checked in, and when the bits are told, as pspautotests' results from a PSP
  show), callbacks as far as registering an exit callback, the clocks (`sceKernelGetSystemTimeWide`,
  `gettimeofday`, `time`, `sceRtc`'s ticks; the date the host's when the program started), and the cache functions,
  which have nothing to do.
- **The picture**: `Kernel::picture()` gives the frame buffer the program set, as the screen shows it, in 8888.

PPSSPP was consulted for what no public source says: the masks on the frame buffer's and transfers' addresses and
widths (which it has from tests on the PSP), how the offset joins BASE in an address, and through mode's unsigned
depth. Worth measuring on the PSP: which pixels a sprite with fractional corners covers, and those masks.

Tests (`tests/psp/ge.cpp`): lists written in the test, run by the GE alone (registers and matrices, moving about with
CALL, RET, JUMP, ORIGIN and OFFSET_ADDR, stopping at the stall address and at END, running out of budget), vertex
layouts and values, clearing in each format with each mask, transfers of both sizes; the driver called directly
(queue, sync states, stall moves, freed lists' order, errors, 64 lists at most, BASE kept from one list to the next,
saving and restoring the GE's state, 16 callbacks at most); programs written in the test, on both engines where they differ, whose callbacks note how they
were called and in what order, whose SIGNAL callback rewrites the list (suspended: it takes; not: too late), whose
finish callback rewrites the list queued next, whose callback tries to wait and wakes a thread, with interrupts held off and let back on, and whose list pauses and is
continued; event flags polled (the bits told on failing, errors in order, a zero timeout), and waited on by several
threads and by two on a flag for one, timing out and deleted under a waiter; the picture in each format. With
`PSP_TEST_PROGRAMS`: `tools/psp-test-programs/gu` drives the GE through pspsdk's GU library (clear, a signal and its
callback, a called list, FINISH with an id and its callback, a copy), and pspsdk's own sample `copy` (built from the
toolchain's samples, not kept here) runs until both its frame buffers hold its picture, the depth buffer cleared.
Twenty-six broken versions each failed them (the offset ignored or not restored at a RET, ORIGIN a word late, the
stall address ignored, vertex parts not aligned, through mode's depth signed, the first vertex's color, no scissor,
alpha always cleared, transfer widths not in eights, freed lists reused newest first, SUSPEND not waiting, callbacks
told the list's start, calls ignoring interrupts held off, rescheduling during a call, CLEAR clearing everything, two
waiters on a flag for one, 5650's green read as five bits, the PAUSE callback given the whole SIGNAL word, a failed
poll or a timed-out or deleted wait not told the bits, a zero timeout waiting, the flag looked up before the bits,
BASE cleared for a list that starts after another, the next list run before the finish callback).

Bugbot's reviews, read in full: the PAUSE callback was given the whole SIGNAL word (the driver keeps only its 16-bit
id: fixed); a failed poll shouldn't tell the bits (pspautotests' results from a PSP say it does, and showed the check
order and the timeout, deletion and zero-timeout cases, now done too); a list enqueued at the head un-pauses the one it
displaces, and a list the GE gave up on leaves its waiters waiting (both as uOFW's reading of the PSP's driver has
them; the code now says so); a list starting after another had BASE cleared (the driver restores BASE by running the
word it saved as a command, and a list that never ran saved 0, a NOP: fixed, with a test); the next list ran before
the last one's finish callback (the PSP's driver calls it first: fixed, with a test whose callback rewrites the next
list).

## Part 10: drawing in 2D

`ares/psp/ge/`: `draw.cpp` (primitives), `texture.cpp`, `pixel.cpp` (the pixel pipeline). Through mode, where
positions are pixels (in sixteenths) and texture coordinates texels.

- **Primitives**: sprites (rectangles between pairs of vertices, covering the pixels whose middles are inside, the
  left and top edges included, the right and bottom ones not, and the left edge reaching a sixteenth further left;
  the second vertex's color and depth; corners bottom-left and top-right turn the texture a quarter), triangles,
  strips and fans (sampled at each pixel's middle; pixels exactly on right or bottom edges left to the neighbour;
  colors, depth and texture coordinates blended across, or the last vertex's color with flat shading), and points. A
  vertex without a color takes the material's ambient color. Not yet: lines, and 3D. (The edges and sample points
  are as the user's PSP drew them, measured after this part, which first had PPSSPP's: see "Results from the
  user's PSP".)
- **Textures**: 5650, 5551, 4444, 8888, and 4-, 8-, 16- and 32-bit palette indices (the palette copied into the GE's
  own 1 KiB by CLUT_LOAD, its index shifted, masked and offset); swizzled storage (pspsdk's layout: blocks 16 bytes by
  8 rows); repeat or clamp each way; nearest or filtered (four texels by sixteenths, half a texel in), chosen by
  whether the texture is enlarged or shrunk. Not yet: DXT, mipmaps.
- **Texture functions**: modulate, decal, blend (with the environment color), replace, add; the texture's alpha or
  not; color doubling.
- **The pixel pipeline**, in the PSP's order: alpha test, color test, stencil test and depth test (with the stencil
  operations, stopping at each format's ends, and the depth written unless masked), blending (every factor and
  operation), dithering (before the color is held to 0-255), logic operations (leaving the stencil), and the write,
  sparing the masked bits. The alpha written is the stencil, kept as it was without the stencil test. Clear mode goes
  through the same writes.

The arithmetic (rounding in the texture functions and blending, the filter's weights, which pixels a primitive covers,
when the stencil is written) is PPSSPP's software renderer's, which its authors checked against tests on the PSP;
the commands' layouts come from pspsdk's GU library, and the meanings from pspgu.h. It all wants measuring on the PSP.

Tests (`tests/psp/draw.cpp`), with expected values worked out by hand from those rules: sprites 1:1, turned and
mirrored; each texture format, the palette's shift, mask and offset, and a swizzled texture against the plain one
(swizzled the way pspsdk's own sample code does it); the filter's weights, repeat and clamp, the filter chosen by
enlarging or shrinking; each texture function, with alpha and doubling; each test and stencil operation, in 8888 and
4444; each blend operation and factor, with a case only the PSP's rounding gets right; dithering, logic operations,
write masks; triangle coverage, colors blended and flat, two triangles sharing an edge (one through a column's sample
points) drawing each pixel once; points; the ambient color. With `PSP_TEST_PROGRAMS`, pspsdk's samples: "blit" (a
texture drawn as one sprite, from its swizzled copy, and in strips, the buttons pressed to switch) and "doublelist"
(the same through sent lists) leave the picture they must, pixel for pixel; "clut" and "blend" run and draw.
`tools/psp-test-programs/compare-ppsspp.sh` compares those two with PPSSPP's software renderer after the same second:
"blend" within 2 levels on every pixel (mean 0.01), "clut" on 99.94% (the rest, up to 13 levels, sit where the filter
rounds at a boundary, which the two step to differently). That was before the core took the PSP's measured rules
where PPSSPP's differ ("Measuring the GE..."); with round 3's fixes, "blend" is within 2 levels on 99.88% of its
pixels and "clut" on 99.64%, at most 17 and 16 levels apart. Fifteen broken versions each failed the tests (among them
the texture functions' and blending's rounding, the filter's half texel, swizzled rows, the palette's shift, sprites
never turning, edges all inclusive, 4444's stencil counting by ones, the alpha test the wrong way round, the depth
mask ignored, the dither matrix unsigned, no ambient color, the stencil overwritten by alpha, flat shading taking
the first vertex). A review's claim that a pixel at VRAM's end could run past it was disproved (the offsets stay
multiples of the pixel's size; the code now says so). Found on the way: the address sanitizer's check for stack use
after return made the per-pixel functions hundreds of times slower (18 minutes for the samples), so the test script
turns that one check off (`ASAN_OPTIONS`); the run takes 95 seconds.

### Measuring the GE and the controller on a PSP

The rules above that came from PPSSPP or uOFW rather than from measurements of our own are what
the GE and controller half of `tools/psp-measure` records on a real PSP (it began as `tools/psp-ge-measure`, a
program of its own; since 2026-10-04 the VFPU's measurements and these are one homebrew program, as "Measuring the
VFPU on a PSP" says). It draws each case into VRAM, reads the pixels back as they are and writes them to
`results/ge` beside its EBOOT.PBP: 64 result files and a manifest, about 15 MB, in a few seconds. The cases:

- every blend operation and factor, over every source color and alpha;
- the texture functions, every vertex color against every texel, with alpha and doubling;
- the filter enlarging (repeating, clamped, at an odd scale) and shrinking;
- which pixels sprites and triangles cover with their corners at each sixteenth of a pixel, and two triangles sharing
  an edge;
- the sprite corners' quarter turn, dithering in 8888 and 5650, the stencil's steps in 4444 and 5551;
- every texel of the 16-bit texture formats (color and alpha), and 8-bit colors narrowed into each 16-bit frame
  buffer (with what the alpha bits get when the stencil test is off);
- colors across triangles, and the texel each pixel takes when a texture is shrunk or stretched, as a sprite and as
  triangles;
- the controller's timing: whether a second `sceCtrlReadLatch`, a `sceCtrlReadBufferPositive` just after a vertical
  blank, and a second `sceCtrlReadBufferPositive` wait;
- in 3D (part 11): a floor receding in perspective, for the texel each pixel takes, the depths written and the fog
  across it; a 3D sprite whose corners lie at different depths, textured and fogged; the GE's rounding onto the
  screen (edges moved in 256ths of a pixel past a sample point); a triangle cut at the near plane, with
  DEPTH_CLIP_ENABLE on and off; which depths stop triangles, points and sprites, with it on and off; and culling
  either way, in 3D and through mode;
- lighting (part 12), each case 256 cells of one lit color: diffuse across the angles on a material and on the
  vertex's color standing for it; the shine across the angles with coefficients 2 and 7; a spotlight's pool with its
  direction toward the light and away from it (which way the GE takes it); a point light's fading with all three
  terms; and environment mapping's coordinates over a hemisphere of normals, from a plain light and a shining one.

The program computes nothing itself. `tests/psp/measure.cpp` runs the same program in this core (with
`PSP_TEST_PROGRAMS`), through its menu as a person would: the GE's tests; starting afresh; the tests again, which
must draw the same; a third time, which must skip them all; the GE's round 3; the FPU probes; and leaving. It
checks that every file is written, and with `PSP_GE_RESULTS` set to a results folder (`results/ge`) lists what
differs from it (`PSP_GE_OURS` keeps this core's files for a closer look). Against PPSSPP's software renderer (its
headless build running the smoke version), 61 of the 64 files match: 60 pictures identical, and the controller's
timing agreeing on what waits (a second latch read doesn't, a buffer read after a vertical blank doesn't either, and a
second buffer read waits a frame). The three that differ are the PSP's to settle:

- a sprite whose right or bottom edge runs exactly through pixel middles (127 pixels): this core draws them, while
  PPSSPP draws them or not depending on where the other corners are, in code its authors mark as unverified;
- a shrunk sprite's texels (4240 pixels, a texel apart): this core takes a sprite's texture coordinates at each
  pixel's middle, PPSSPP at 7/16 in, as for triangles;
- the 3D sprite's fog (10152 pixels): this core takes the second corner's fog for the whole sprite, while PPSSPP
  splits it across the sprite's middle (which, its comments say, seems to be the way). (Since measured: the PSP
  splits it as PPSSPP does, and the core does now; see "Round 3's results".)

The 3D cases found one difference that was this core's to fix: a triangle cut at the near plane had 7292 pixels a
level apart from PPSSPP's, because the cut's corners were blended from the other end of the edge, and their colors'
256ths rounded the other way (part 11 then blended them from the corner past the plane, as PPSSPP does; the PSP turned
out to blend them from the kept corner, so the core does that now: see "Fixed since"). An earlier count here, "49 of
the 51 files", miscounted: it was 48 of 50, the 51st file being the manifest.

#### Results from the user's PSP (2026-10-04)

The user ran the program (with the lighting cases) on their PSP (firmware 6.61). The SHA-256 of every file and the
manifest are in [`tests/psp/measured/`](../tests/psp/measured/); the files themselves are kept outside the
repository. `tests/psp/measure.cpp` with `PSP_GE_RESULTS` gives what follows.

**The controller is as uOFW reads the firmware:** a second `sceCtrlReadLatch` took 1-2 µs (it doesn't wait), a
`sceCtrlReadBufferPositive` just after a vertical blank 9-12 µs (it doesn't either), and a second
`sceCtrlReadBufferPositive` 16.6-16.8 ms (it waits a frame), all 16 times each.

**40 of the 63 picture files are identical to the core's:** every blend operation and factor, every texture function
(with alpha and doubling), dithering in 8888 and 5650, the stencil's steps in 4444 and 5551, every texel of the 16-bit
texture formats (color and alpha), 8-bit colors narrowed into the 16-bit frame buffers (and what the alpha bits get
with the stencil test off), the sprite corners' quarter turn, the filter shrinking, a stretched sprite's texels, and a
triangle past the near plane with DEPTH_CLIP_ENABLE off. The pixel pipeline's arithmetic (blending, texture
functions, dithering, the stencil, formats), as parts 10-12 have it, is the PSP's.

**The other 23 differ, and in 21 of them PPSSPP's software renderer differs from the PSP at exactly the same
pixels** (the core follows it there); in the other two, the 2D sprite cases, the two differ from the PSP in different
places. So the PSP's own data is what settles them:

| file | pixels that differ | by | what it shows |
| --- | --- | --- | --- |
| `filter-magnify`, `-clamp`, `-37` | 2880, 1620, 991 of 4096 | 1 (13 at most at the odd scale) | the filter's weights or rounding |
| `gouraud` | 8432 | 1 | colors blended across triangles round otherwise |
| `texels-triangles-shrunk`, `-stretched` | 1928, 7936 | a texel | texture coordinates across triangles |
| `texels-sprite-shrunk` | 7424 (PPSSPP 3312) | a texel | which texel a shrunk sprite takes: neither's rule |
| `coverage-sprites` | 344 (PPSSPP 217) | whole pixels | which pixels sprites cover (below) |
| `coverage-triangles`, `shared-edges` | 567, 382 | whole pixels | which pixels triangles cover, edges included |
| `3d-rounding` | 1500 | whole pixels | the rounding onto the screen (below) |
| `3d-cull`, `3d-rules`, `3d-floor-texels`, `3d-floor-fog` | 48, 146, 2708, 6321 | edges, texels, levels | edges and rounding in 3D |
| `3d-clip` | 8496 | 1 | colors across a triangle cut at the near plane |
| `3d-sprite` | 10152 | up to 254 | a 3D sprite's fog and texels: neither's rule |
| `3d-floor-depth` | 17344 | | not yet comparable (below) |
| `light-diffuse`, `-specular`, `-spot`, `-point` | 28160, 14848, 2560, 30720 | 1 | lighting's rounding |
| `light-environment` | 512 | a texel | environment mapping's coordinates |

What the data already says:

- **Sprites** (through mode, corners on sixteenths): the core covers the pixels whose middles (8/16 in) are inside,
  edges counting, and PPSSPP nearly so. The PSP draws a column while the left edge is at most 9/16 into it, and
  from the right edge being 9/16 into it; a row while the top edge is at most 8/16 into it (as the core has it), and
  from the bottom edge being 9/16 into it. The same in all 16 cells of each offset.
- **3D positions aren't rounded up the way PPSSPP has it** (+0.375 of a sixteenth, which the core copied): the PSP
  draws the pixel in all 256 cells, with its left and top edges up to 15/256 of a pixel past the sample point the
  program assumed (7/16 in), where that rounding drops it from 10/256. Either the GE truncates positions to the
  sixteenth, or its sample point is further into the pixel than 7/16 (as the sprites' are); a case with edges
  past 8/16 will tell which. The triangle coverage settles it: see below.
- **The spotlight's direction is toward the light**, as PPSSPP reads it: the pool has the same shape on the PSP, the
  differences only a level of rounding.
- **The depth buffer doesn't read back in the order the program assumed:** none of the floor's 15264 depths match,
  9616 of them read 0, and 2080 pixels off the floor read something. The PSP's depth buffer is evidently arranged
  differently in VRAM when the CPU reads it at its normal address (PPSSPP reads depth through a separate mirror),
  so this file needs that layout worked out before it says anything about depths. (Round 3 worked it out: see
  "Round 3's results".)
- **With DEPTH_CLIP_ENABLE off, a triangle reaching past the near plane isn't drawn at all**, as the core has it
  (`3d-clip-unclamped` is empty on both).

**Fixed since, from the same files** (ares/psp/ge/draw.cpp):

- **Triangles are sampled at the pixel's middle,** not 7/16 in as PPSSPP has it: with the middle and the usual rule
  for edges (left and top drawn, right and bottom not), all 256 cells of `coverage-triangles` come out as the
  PSP's, every pixel, where 7/16 left 567 pixels apart. That also explains `3d-rounding` (the edges it moves stay
  short of the middle), so the rounding onto the screen can stay as it is until a case tells it apart. Colors,
  depth and texture coordinates are blended at the middle too, which makes `gouraud` identical.
- **Sprites** follow the same rule at the middle (left and top edges drawn, right and bottom not), but with the left
  edge reaching a sixteenth further left, as measured.
- **Texture coordinates are stepped from the primitive's leftmost corner** (the topmost of two; a sprite's top
  left), by a step per pixel cut short to a 65536th of a texel when it isn't exact: so a coordinate that should
  land exactly on a texel boundary falls just short of it, on that corner's side. That reproduces every pixel of
  all four `texels-*` files (256 texels over 240 pixels lands on boundaries; 200 over 256 never does) and keeps
  `filter-shrink`'s exact step exact. How many bits the step keeps, and which way a step going left or up is cut,
  aren't pinned down by these files.

- **The filter** blends the top two texels, then the bottom two, then those two results, each step dropping its
  fraction (ares/psp/ge/texture.cpp), where the core truncated once at the end: all three `filter-magnify` files are
  the PSP's, every pixel (the 37-pixel one also needs the stepped coordinates above).
- **A triangle cut at the near plane** has its new corners blended from the kept corner toward the one past the
  plane (ares/psp/ge/transform.cpp), the other way round from PPSSPP: `3d-clip` goes from 8410 pixels a level apart
  to 1535.

With these, 52 of the 63 pictures are identical to the PSP's (40 before). Still apart, each by a level or a texel:
lighting (every light), fog across the 3D floor, colors across the clipped triangle (1535 pixels), perspective-correct
texels on the 3D floor (291 pixels); and `3d-rules` (6 pixels), the 3D sprite's fog and texels, and the depth
buffer's layout. These files don't pin the rest down:

- Lighting fits neither PPSSPP's arithmetic (colors as 2c + 1, a share in 512ths rounded up) nor any of the simple
  alternatives tried (other encodings of the colors, shares in 256ths to 65536ths rounded each way): the best still
  misses about 60 of the 768 values in `light-diffuse`. The PSP's cosine itself seems to come out a little off, low
  in some cells and high in others (by up to about 0.3%), as an approximate normalization would; round 3 (below)
  can tell, with normals whose cosines are exact and several material colors. (These cases were plain diffuse,
  the GE's light kind 0: pspsdk's `sceGuLight` turns `GU_DIFFUSE` into kind 0, and only `GU_POWERED_DIFFUSE` (8)
  into kind 2, the powered diffuse.) Round 3's exact cosines settled the share: 256ths (see "Round 3's results").
  What's left of `light-diffuse` isn't the cosines after all: its white channel matches the PSP in every cell, so the
  shares are right, and the rest is in the 64 and 192 channels, a few products a level lower on the PSP, as in
  `light-materials`.
- Colors across the clipped triangle and fog across the floor are a level off in scattered pixels, whose values
  land on or near a whole level: like the texture coordinates' short steps, but stepping the colors the same way (in
  65536ths or 256ths) doesn't reproduce them. Cases with a single color ramp across a triangle, at several slopes,
  would show how the GE steps colors (round 3's ramps).

The open questions above, settled: a sprite edge through pixel middles follows neither the core nor PPSSPP (the rule
above); a shrunk sprite's texels follow neither, PPSSPP's nearer; a 3D sprite's fog and texels together follow
neither (round 3 took them apart: the fog is PPSSPP's split, the texels neither's); the spotlight's direction is
PPSSPP's reading; a second latch read doesn't wait. Next: fit each rule from these files (the coverage and sample
points, the rounding onto the screen, interpolation, the filter, lighting's rounding, the depth layout), one at a
time, each fix checked against them.

#### Round 3, ready for the PSP

What these files couldn't settle is round 3's GE line on the menu (about 7 MB, `results/ge`, `manifest3.txt`;
`tools/psp-measure/ge.c` and its README say what each case draws):

- **Lighting with cosines known exactly:** normals built from Pythagorean triples and quadruples, so each cosine is
  an exact fraction, also scaled (by 2, 1/2, 64, 1/64) and turned toward y, with the light's direction 1 and 3
  long; plain diffuse, powered diffuse (kind 2, never measured) and the shine at powers 1 and 2; material, light and
  ambient colors at levels where rounding shows. Whether the cosine or the color arithmetic is off will show apart.
- **Color and fog ramps:** 16 slopes each, in through mode along x and along y, in 3D with corners between pixels,
  and fog in 3D without perspective.
- **3D:** edges 0 to 15 256ths past the pixel middle (`3d-rounding-middle`), which tells truncation to the sixteenth
  from rounding; a wall receding along x, for perspective-correct texels in the other direction; round 2's 3D sprite
  as its fog alone, its texels alone, and both at one depth.
- **Curved surfaces:** Bézier patches (flat, curved, cut 4x4 and 8x8) and splines with edge types 0 and 3, for when
  the core draws them.
- **The depth buffer's layout:** each pixel of a 256x64 area given its own depth, read back through each of VRAM's
  four copies, so the copy that reads depth in its plain order, if one does, shows.

The core already agrees with PPSSPP's software renderer on the six lighting cases, the flat 3D sprite and the four
depth reads, and differs on the ramps, the 3D edges, the wall's texels and the sprite's fog and texels, where it now
samples and steps as round 2 showed, or neither is measured yet, and on the curved surfaces, which the core doesn't
draw: the PSP's files will say which is right.

#### Round 3's results (2026-10-04)

The user ran round 3 on the same PSP, and round 2 again: all 63 of round 2's pictures came out identical to the first
session's, byte for byte. The SHA-256 of round 3's 24 files and its manifest are in
[`tests/psp/measured/`](../tests/psp/measured/) (`SHA256SUMS3`, `manifest3.txt`); the files stay outside the
repository. Against the core (`tests/psp/measure.cpp` with `PSP_GE_RESULTS`):

- **`light-ambient` is identical:** two colors multiplied (the light's ambient by the material's) round as the core
  has it.
- **`light-cosines` is identical but for 2 of its 256 cells:** with exact cosines and white light on white, plain
  diffuse is the core's arithmetic, but for the cosine 7/25 (twice), where 255 × 0.28 = 71.4 comes out 71 on the PSP
  and 72 in the core. (That read round 2's white-light cases as apart mostly for their cosines; the share in 256ths,
  found since, makes round 2's shine identical and takes most of the spotlight's and the point light's away.)
- **`light-powered` and `light-shine` differ in 15 cells each, `light-materials` and `light-colors` in 61 each:**
  every one a level lower on the PSP, where the core's arithmetic (PPSSPP's) rounds up.
- **The ramps:** `ramp-colors`, `ramp-colors-vertical`, `ramp-colors-3d` and `ramp-fog` are a level apart in 1359 to
  18976 pixels, the PSP lower but for the vertical ramps (both ways): how the GE steps colors and fog.
- **`3d-rounding-middle` settles the rounding onto the screen:** the GE truncates a position's offset from screen
  coordinate 2048 to the sixteenth, toward 2048. Left of and above it, an edge a few 256ths past a pixel's middle is
  moved further past it, so the pixel isn't drawn; right of and below it, the edge comes back onto the middle, so the
  pixel is (left and top edges counting). Every one of the 256 cells of both pixels the case watches fits; rounding
  to the nearest sixteenth, or up from 0.625 of one (PPSSPP, and the core then), doesn't. Here 2048 is also the
  viewport's center and the middle of the GE's 4096-wide space: a case with another viewport center would tell which
  the GE truncates toward.
- **`3d-wall-texels`:** 307 pixels a texel apart, like the floor's 291 (perspective-correct texels).
- **3D sprites:** `3d-sprite-fog` shows the PSP splits a 3D sprite's fog across its middle column, as PPSSPP does
  (the two files are identical): here the left half takes the far corner's fog (all of it) and the right half the
  near corner's (none), where the core takes the second corner's for the whole sprite. `3d-sprite-texels` follows
  neither the core's rule nor PPSSPP's: across x, u / w and 1 / w go from the left corner's values to the right
  corner's; down y, v / w goes from the top's to the bottom's; each pixel takes (u / w) / (1 / w) and
  (v / w) / (1 / w), which is within a texel of every one of the 20304 pixels. So down the left edge, where 1/w is
  the near corner's, v reaches only a quarter of the texture. `3d-sprite-flat` (both corners at one depth, so one
  fog) is 1108 pixels a level lower on the PSP.
- **Curved surfaces:** the PSP draws all five. PPSSPP's differ from them: its flat Bézier patch by up to 2 levels in
  35155 pixels, its curved ones in their outlines too.
- **The depth buffer's layout:** only VRAM's fourth copy (0x04600000) reads the depth buffer in order, pixel (x, y)
  at y × 512 + x. The second (0x04200000) spreads each 16-pixel piece of a row to every 32nd column:
  y × 512 + (x >> 4) × 32 + (x & 15). The first and third (0x04000000, the address programs use, and 0x04400000) also
  swap the pieces in pairs and the rows in eights: (y ^ 8) × 512 + ((x >> 4) ^ 1) × 32 + (x & 15). Each formula fits
  all 16384 values. The case drew only the left 256 columns, so where columns 256 to 511 go (most likely the gaps)
  isn't measured. That's why round 2's `3d-floor-depth`, read at the first copy, didn't come back in order.

Against PPSSPP's software renderer, the PSP agrees only on `light-ambient`, `3d-sprite-fog` and `depth-layout-3`.

**Fixed since, in the core:** the rounding onto the screen (toward 2048), 3D sprites (the fog's halves; texels as
measured), and the depth buffer's layout, as VRAM's copies rearranging it (memory.hpp: in address bits, the second
copy flips bits 6 and 13, and the fourth first turns bits 5-9 round by one; columns 256-511 land in the gaps). Now
identical: `3d-rounding-middle`, `3d-sprite-fog`, `3d-sprite-flat` and all four `depth-layout` files;
`3d-sprite-texels` is 72 pixels a level apart. Round 2's files gained too: `3d-sprite` from 10152 pixels apart to 53
(a level each), `3d-floor-depth`, read at the first copy, from 17424 (by up to 255) to 1388 (by 1), and `3d-clip` from
1535 to 1512. Every other file is as it was.

**And the lighting's share** counts 256ths, not PPSSPP's 512ths and one more: fitted to the values with normals
(a, 0, b), whose cosines are exact (`light-cosines` rows 0 and 8, and all of `light-materials` and `light-colors`):
1620 of 1632 channel values. No offset to the rounding fits the last 12: two cases (level 32 at a cosine of 56/65,
64 at 72/97), each in six cells of either file, one channel each, a level lower on the PSP. No measured share lands
on a whole 256th, so rounding up there (rather than down and one more) is assumed (PPSSPP's rule gives the same
results at the whole 256ths the tests use, 0.5 and 0.75). Now identical: `light-cosines`, `light-powered`,
`light-shine`, and round 2's `light-specular`. `light-materials` and `light-colors` are 1536 pixels apart (were
15616); round 2's `light-diffuse` 12288 (was 28160), in its 64 and 192 channels at shares its white channel
confirms, as `light-materials`' 12; `light-point` 9728 (was 30720) and `light-spot` 768 (was 2560), whose normals
are exact but not the direction to the light, nor its fading or spot; all a level each.

## Part 11: drawing in 3D

`ares/psp/ge/transform.cpp`, and 3D paths in `vertex.cpp`, `draw.cpp` and `pixel.cpp`. Outside through mode:

- **Vertices**: 8- and 16-bit numbers are fractions (128ths and 32768ths). With morph targets a vertex is their sum,
  each weighted by its MORPH_WEIGHT. With skinning (a vertex type with weights), its position and normal go through
  the bone matrices its weights pick, and the results are added up, weighted.
- **The transform**: the world, view and projection matrices in turn (each element the float its 24-bit DATA word
  holds), then the viewport and the screen offset, rounded to the sixteenth as the GE rounds: a position's distance
  from screen coordinate 2048 cut toward 2048 (measured: see "Round 3's results").
- **What isn't drawn**: a primitive with a vertex off the 4096x4096 screen. Depths outside 0-65535 are held to that
  range with DEPTH_CLIP_ENABLE on, and count as off the screen with it off. A z / w past 1 (by 2^-15) drops a
  triangle or sprite with any such vertex (DEPTH_CLIP_ENABLE off) or with all of them past the same end (on); points
  aren't judged by it (as PPSSPP has it). A triangle with every w below zero isn't drawn either.
- **Clipping**: a triangle is cut at the near plane (z < -w) only, never at the screen's edges (the scissor does
  those). The new corners are blended in clip space from the kept corner toward the one past the plane (colors in
  256ths, which the way round decides: the PSP's way, measured later) and put on the screen again; with flat shading
  every piece keeps the last vertex's color.
- **Culling** (CULL_FACE_ENABLE, not in clear mode, through mode too): CULL 1 draws the triangles running clockwise on
  the screen, 0 those running counterclockwise; every other triangle of a strip counts the other way round.
- **Texture coordinates**: perspective-correct across triangles (blended as u/w and 1/w, then divided); colors, depth
  and fog are blended straight. Mode 0 takes the vertex's, times TEX_SCALE plus TEX_OFFSET; mode 1 the texture
  matrix's result from the position, the texture coordinates or the normal, its q dividing at each pixel.
- **Fog**: each vertex's (view z + FOG1) × FOG2, blended across, 0-255, mixed in after the alpha test as
  (color × f + fog color × (255 − f) + 255) / 256. A FOG1 or FOG2 that isn't a number to a float is a huge number to
  the GE.
- **The depth range test** (MIN_Z to MAX_Z), in 3D only, clear mode included.
- **Sprites in 3D**: both corners transformed and checked by the same rules, then drawn as in 2D but for two things:
  the texture coordinates follow the perspective (u / w and 1 / w across x, v / w down y, then divided: measured), and
  the fog is split at the middle column, each half taking the fog of the corner on the other side (measured with the
  first corner at the top left; either order, and where the middle falls to the sixteenth, as PPSSPP has it).

Not yet: lighting (noted; vertices keep their colors), environment mapping (which comes from lighting), PRIM's kind 7
(going on with the last primitive's vertices), lines, mipmaps, curved surfaces. Nor these smaller details:
- a 3D sprite's texture projection (its q is ignored);
- the texture coordinates and normal PPSSPP lets a vertex without them keep from the last one read.

The rules are PPSSPP's software renderer's, for behavior only, but for what the PSP was measured doing (the
rounding, 3D sprites, the cut's colors, and more since: see "Measuring the GE..."). The commands' layouts come from
pspsdk's GU library: `sceGuSetMatrix`'s element order, `sceGuViewport`, `sceGuDepthRange`, `sceGuFog` and
`sceGuFrontFace`.

Tests (`tests/psp/draw3d.cpp`) are worked out by hand on a scene where every matrix is the identity and the viewport
puts a model's x and y on the screen's pixels. They cover:
- the matrices in order, and the rounding to the sixteenth;
- each "isn't drawn" rule, for triangles, sprites and points, with DEPTH_CLIP_ENABLE on and off, and every w below
  zero on its own;
- a triangle cut at the near plane, the rows and colors it leaves, and flat shading's color on every piece;
- culling either way, strips, clear mode and through mode;
- perspective-correct texture coordinates, with q, and across a 3D sprite;
- fog's rounding, its blending across a triangle, its distance being the view's, and a 3D sprite's halves;
- the depth range test;
- both texture coordinate modes, morphing and skinning.

pspsdk's "cube" sample (a textured cube turning in perspective, its back faces culled, depth-tested) runs, and its
picture was within 1 level of PPSSPP's software renderer on every pixel (`compare-ppsspp.sh`); with the PSP's
measured rules since, 99.44% of its pixels are within 2 levels (mean 0.09). Twenty-six broken versions each failed
the tests, among them:
- the rounding without its 0.375, the depth clamp's rules swapped or dropped, a 3D sprite left unchecked;
- no clipping, or the cut's colors not blended;
- texture coordinates blended straight in 3D;
- culling off, swapped, or not flipped along a strip;
- fog without its rounding up, from the world's z, or 255ths;
- the view matrix skipped, the projection's move lost;
- morphing or skinning ignored.

Five of them first got through (every w below zero, the texture matrix taken for mode 0, an off-screen point, fog
from the world's z, flat shading after a cut); the tests that now catch them are in the list above.

Found on the way: a 3D scene without a depth range set draws nothing, since MIN_Z and MAX_Z are both 0 at power-on
(games set them with `sceGuDepthRange`). And `PSP_TEST_PROGRAMS` set but empty made the loader's test read a missing
file and index an empty module (undefined behavior); every test now treats an empty value as unset.

## Part 12: lighting

`ares/psp/ge/lighting.cpp`. With LIGHTING_ENABLE, each vertex's color is worked out once, after the transform, from
the material and up to four lights. Its normal is first turned into the world by the world matrix (NORMAL_REVERSE
turns it round), and made one long.

- **The material**: emissive, ambient (with alpha), diffuse and specular colors, and the specular coefficient.
  MATERIAL_COLOR lets the vertex's own color stand for the ambient, the diffuse or the specular.
- **The ambient light**, lighting everything evenly, and **four lights**: directional, point or spot; doing ambient
  and diffuse, those and specular, or a "powered" diffuse (sharpened like the shine). All but directional ones fade
  with distance; a spotlight lights only its cone (the cutoff), brighter toward the middle (the exponent).
- **The arithmetic**, as the GE does it: a color c counts as 2c + 1, so white times white is white; two colors
  multiply and shift down 10 bits; a light's share counts 256ths, rounded up, and three numbers shift down 18 (the
  256ths measured in round 3, where PPSSPP has 512ths and one more; rounding up at a whole 256th assumed; a few
  products still come out a level lower on the PSP, unexplained). "To the power of" is the GE's quick approximation
  (exact at powers of two, a little low between them), and the coefficient keeps only the top four bits of its
  fraction. Each channel ends held to 0-255.
- **The shine kept apart** (LIGHT_MODE 1): a second color, blended across triangles like the first and added after
  texturing, so a dark texture doesn't dull it.
- **Environment mapping** (TEXTURE_MAP_MODE 2): texture coordinates from two lights (TEXTURE_SHADE_MAPPING), lit or
  not: (the cosine between the normal and the direction to the light + 1) / 2, a shining light's direction taken half
  way to the viewer's.

The rules are PPSSPP's software renderer's (its lighting, and its notes on the PSP's power function and shade
mapping from tests on the hardware), for behavior only, but for the share's 256ths, which the PSP showed. The
layouts come from pspsdk's `sceGuLight`, `sceGuLightAtt`, `sceGuLightColor`, `sceGuLightSpot`, `sceGuLightMode`,
`sceGuMaterial`, `sceGuModelColor`, `sceGuSpecular`, `sceGuAmbient`, `sceGuColorMaterial` and `sceGuTexMapMode`.

Tests (five more groups in `tests/psp/draw3d.cpp`) work each color out by hand from those rules:
- the ambient part with emissive, and the vertex's color standing for the ambient (red held to 255);
- a directional light's diffuse: squarely, at a cosine of 0.8, with a normal not one long, a direction not one long,
  the light off, from behind, powered, at a cosine where the share's rounding up shows (0.501), at two where 256ths
  and PPSSPP's 512ths differ (0.28 on white, red 32 at 0.8), and the vertex's color standing for the diffuse;
- a point light's fading, a spotlight's cone either way, and a spotlight darkening past its direction (cutoff -1);
- the shine: the quick power (0.75 squared is 0.5), the coefficient's cut fraction (1.03125 counts as 1), a turned view,
  and kept apart, added after the texture;
- environment mapping from two lights, one of them shining.

pspsdk's "celshading" (shaded through environment mapping) matched PPSSPP's software renderer pixel for pixel, and
"envmap" (lit and environment-mapped) was within 1 level on every pixel (`compare-ppsspp.sh`); with the PSP's
measured rules since, 99.10% and 98.42% of their pixels are within 2 levels (the lighting's 256ths leave both
percentages as they were). Twenty-one broken versions each failed the tests, among them:
- colors without their + 1, the ambient shifted a bit too far;
- shares rounded down or without their one more (the core had 512ths then; now rounded down or to the nearest, or
  with PPSSPP's one more);
- the true power in place of the GE's quick one, the coefficient's whole fraction kept;
- NORMAL_REVERSE, MATERIAL_COLOR, fading, cones or the powered diffuse ignored;
- the viewer left out of the half-way direction, or taken from the wrong column;
- the shine never kept apart, or never added after the texture;
- environment mapping's u and v swapped, or blind to shining lights;
- every light on, or lighting without LIGHTING_ENABLE;
- normals or directions not made one long.

Two of them (the shares) first got through: no case crossed a level by one share, and the cosine of 0.501 now does.
PPSSPP's 512ths, which the core had until round 3, fail them too.

Not yet: lines, mipmaps, curved surfaces (BEZIER, SPLINE), bounding boxes, PRIM's kind 7.

## Part 13: the PSP in Phobos

`ares/psp/psp.hpp`, `psp.cpp` (the whole core as one translation unit, as Phobos builds each core) and
`ares/psp/system/`: the PSP as an ares system, which Phobos's front ends load, list and run like any other; mia's
PSP medium (`mia/medium/playstation-portable.cpp`); and the Android app's entry for it.

- **The node tree**, what front ends see: the system "PlayStation Portable" ("[Sony] PlayStation Portable"); a
  480x272 screen at 59.94 Hz, its pixels as the GE keeps them (red in the low byte) through a palette of 2^24 colors;
  stereo sound at 44.1 kHz (silence until sceAudio); the controls, named as the PlayStation's are (Up, Down, Left,
  Right, Triangle, Circle, Cross, Square, L, R, Select, Start, and the stick as "L-Stick X" and "L-Stick Y"), so
  front ends map them alike; and a "UMD Drive" port (type "Universal Media Disc") taking a "PlayStation Portable
  Disc" (front ends give a peripheral whose name ends in "Disc" the game's medium).
- **A frame**: `run()` reads the controls into the HLE kernel (pspsdk's PSP_CTRL_* bits; the stick from -32768 to
  32767 down to 0 to 255, 128 in the middle), runs the kernel for a frame's time, copies the picture the program
  shows to the screen, and owes the speakers 735.7 sound frames.
- **The model**, the user's choices: a PSP-2000/3000 with 64 MiB, English, X confirms.
- **The game**: mia's medium takes an EBOOT.PBP, ELF or PRX (as `program.pbp`, `.elf` or `.prx`), recognized by its
  contents, since PlayStation games share .pbp: a PBP isn't a PlayStation game converted to run on a PSP (CATEGORY
  "ME" in its PARAM.SFO), an ELF or PRX is for MIPS; and ISO and CSO images (part 14). A PBP's title comes from its
  PARAM.SFO. A program starts with the path a PSP would give it: from the memory stick (`ms0:/PSP/GAME/...`) when it's
  in the memory stick folder already, else from its own folder, which stands for its disc (disc0:, also called
  umd0:).
- **The memory stick**: a host folder (option "Memory Stick"), formatted as a PSP formats one (PSP/GAME,
  PSP/SAVEDATA). The devices are the system's to give at each power-on: none is left from the game before.
- **Unloading** frees the machine until the next game: the files the program left open, its threads, its memory, the
  compiled code.
- **The recompiler** runs the CPU (option "Recompiler"), the interpreter its fallback: where the host refuses memory
  that code may run from, the recompiler turns itself off and the interpreter runs everything. (nall's
  `memory::map()` now returns null when `mmap` fails, as its callers expect; it passed `MAP_FAILED` on.)
- **A crash**: an exception nothing handles is reported once, and the game ends there.
- **States**: part 15.

In the Android app: the system "PlayStation Portable" with .pbp and .elf (an EBOOT.PBP goes by its Library folder,
as the PlayStation takes .pbp too; other apps' "psp" names it; disc images wait for the core to read them, and .prx
files are mostly modules beside an EBOOT.PBP); the runner loads it with the memory stick: one shared folder,
`<saves>/PlayStation Portable/Memory Stick`, for every game, or the folder picked in Settings, Global Paths, PSP
Memory Stick; a program copied into the app's cache gets a folder of its own there (its folder is its disc0:), and a
zipped one keeps its kind (.pbp or .elf, from its first bytes); a touch layout (the d-pad, the face buttons, L, R,
Select, Start, and the stick beside the d-pad, hidden when held upright, as the PlayStation's sticks are); an icon
of its own (the Systematic pack has none, so it shows the PlayStation's); "PSP" in the performance HUD. Two menu
entries that matched "PlayStation" anywhere in a name (the disc button, the DualShock section) now match it exactly.

On the RP6 (2026-10-05): pspsdk's cube, beginobject and controller samples and the hello program run at 60 frames a
second (cube's frame takes about 8 ms of the 16.7), the controller sample sees a button pressed, and the memory stick
folder is made beside the saves. pspsdk's font sample stays black: sceLibFont, the PSP's font library (it reads
flash0's fonts), isn't there yet. A program started from another app's intent is read where it is when the app can
read the path; otherwise Phobos copies it into its cache first, as it does every game, so a program's own files
beside it aren't there.

Tests: `tests/psp/ares/run-tests.sh` builds the whole core as Phobos does, with ares's node tree and a test platform
standing in for the front end (93 checks):
- the PSP's name, and no other;
- the node tree: the screen, the sound, every control's name, the drive and what it takes;
- hello.elf in the drive, on the recompiler (which must have compiled code) and on the interpreter: the memory stick
  formatted, ms0: and disc0: mounted, the program starting in its disc's folder as `disc0:/hello.elf`, printing its
  line and leaving; the frame the front end gets is the display's frame buffer pixel for pixel; each button its own
  bit as the system hands the controls to the kernel; the stick's corner and middle; an empty state (a check part
  15 replaced with its own);
- a program on the memory stick: it starts as `ms0:/PSP/GAME/HELLO/hello.elf`, with no disc left from the game
  before; in the memory stick's top folder, as `ms0:/hello.elf`;
- no memory for compiled code (the host refusing it): the recompiler turns itself off and hello runs all the same.
sceDisplaySetMode now refuses any mode but the LCD's and any size but 480x272 (PPSSPP's notes), so no program can
size the picture past its buffer (`tests/psp/ge.cpp`'s display group checks it).
The app's unit tests check the PSP's launch names and extensions, its touch layout (the face buttons as the native
mapping reads them, one shoulder each side, the stick) and its icon.

Next: ISO and CSO images (disc0: and umd0: from the image, sceUmd), save states, sceAudio.

## Part 14: the disc

`ares/psp/kernel/disc.hpp` and `disc.cpp`, the disc's paths in `io.cpp`, and `umd.cpp`: a UMD as an image of it, its
files through the kernel, and the drive. Behavior from PPSSPP's notes on the hardware where the PSP's own isn't
documented (the ioctl and devctl codes, the drive's states and timeouts, `sce_lbn` paths); our own code.

- **The image**: an ISO, the disc's 2048-byte sectors one after another, or a CSO, the same compressed a block at a
  time: a 24-byte header, an index of where each block starts, each block deflated or stored as it is (version 1 marks
  stored blocks with the index entry's top bit; version 2 by their taking a whole block's room; encoders pad the last
  block out to a whole one). An index that doesn't fit the file, goes backwards or points past the file's end is
  refused before anything is read through it. (CSO version 2's LZ4 blocks, and the scene's other forms, are part
  16's.) A trimmed ISO, ending part way into its last sector, still reads to its last byte. The image is read as the
  game asks, never copied: on the host from the file mapped into memory; on Android, when the app has no path it can
  read, through the descriptor it was given.
- **ISO 9660**: the volume descriptor at sector 16 gives the root folder; a folder is a run of directory records
  (where each file starts, its size, whether it's a folder, its date, its name), found whatever their case. A
  damaged record ends its folder, and no folder is read past 256 sectors or the disc's end, so a damaged folder size
  can't have every lookup read the whole disc.
- **disc0:**, the disc's file system: files opened by path, read only (opening to write fails with ErrorInvalidFlag;
  the other flags, creating or emptying, are let be; removing or making anything fails with ErrorReadOnly), read,
  seeked. A file's status gives its size, a read-only mode, the recording date, and its first sector in
  st_private[0], where games look for it. Folders list the disc's order, with no "." or "..", and no short (8.3)
  names. Games also read the disc by sector numbers: `sce_lbn<first sector>_size<bytes>`, both numbers hexadecimal
  with or without "0x", anything after them ignored (slashes too: "sce_lbn10/_size800" is one), opens that run of
  the disc, and its status is a file that size starting there. A host folder standing for the disc (a homebrew
  program's own) comes first.
- **umd0:** (and umd1:, umd:) is the whole disc whatever the path after it, its positions and sizes counted in
  sectors (its status and its size through an ioctl too); its listing is empty.
- **Requests** (sceIoIoctl, sceIoDevctl): a disc file's first sector, size, position, seek (not past its end),
  reads, the volume descriptor, the path table, the sector size; umd0:'s reads (at least one sector) and seeks in
  sectors, a seek past the disc failing with ErrorInvalidFileSize. The drive: a game disc, the region matching, its
  last sector, anything asked to be read ahead done at once. The memory stick: in, formatted, writable, up to 1 GiB
  free (games add sizes up in 32 bits), callbacks for it going in and out kept (it never does). Anything else isn't
  supported. On every device, the status of a device's top ("ms0:/") is refused, and a status leaves its last five
  words (st_private, all six off the disc) as the program had them, as the PSP does.
- **The drive** (sceUmdUser): a disc present, ready and readable from the start, as games expect; activating it
  checks its mode and the name "disc0:". A thread waiting for a state the drive isn't in waits until its timeout or
  for good, unless the wait is cancelled; the PSP makes a timeout of 1 microsecond 25, and any other of at most 209
  microseconds 240 (but 240 for 1 too when the wait lets callbacks run). The disc's kind is a game's; a callback for
  the drive's changes is kept (none come).
- **Booting a disc** (`ares/psp/system`): the image goes in the drive, and PSP_GAME/SYSDIR/EBOOT.BIN starts, from
  `disc0:/PSP_GAME/SYSDIR`. A shop-bought game's is encrypted ("~PSP"), and decrypted first (part 18); a plain
  BOOT.BIN beside it stands in only when it can't start. Only an ELF, an EBOOT.PBP or an encrypted program is started,
  so a blank BOOT.BIN is passed over. A truncated image that cuts its program short still boots it, read as far as
  the image goes (no further than 64 MiB), as PPSSPP lets truncated images boot.
- **In the app**: the PSP takes .iso and .cso again. A disc image Phobos can't read where it is (told by its URI's
  extension, or for a URI that doesn't end in its name, the game's) isn't copied into its cache (a gigabyte or two):
  the runner hands mia the app's descriptor as `/proc/self/fd/<n>` (if it's a file; anything else is copied), and
  mia's medium reads it through that descriptor (`nall::vfs::descriptor`, new: its own copy of the descriptor, the
  file mapped into memory where the system allows, else read a piece at a time) rather than opening the name again,
  which the system may refuse. What a medium is comes from its contents (a PRX by its name), so a name with no
  extension does.

Tests:
- `tests/psp/disc.cpp` (four groups) builds disc images itself (`tests/psp/disc-image.hpp`: an ISO 9660 writer, and a
  CSO packer with zlib that pads the last block as maxcso does), and checks:
  - the reader on an ISO and on three CSOs (small blocks; 16 KiB blocks with the index shifted; version 2): its size,
    a path found whatever its case, sizes, dates, contents, reads across blocks, folders' order, an empty file, a path
    through a file, reading past the end; images ending part way into a 16 KiB block, in both versions; and damaged
    or odd images: no volume descriptor, no block size, an index cut short, one shifted past the file, one going
    backwards, a block whose stream starts well and then turns invalid (and the block read before it still reading
    right afterwards), a trimmed ISO (its last file and a run over its last part sector, through the kernel too), a
    damaged record ending its folder (a good one in the folder's next sector not listed), and a folder record
    claiming 4 GB over 512 sectors of good records (read no further than 256);
  - files through the kernel: reading, seeking, refusals to write, a read-only open with other flags, status (the
    spare words left, devices' tops refused), listings (and short names left alone), relative paths, runs of sectors
    in five spellings (slashes among them) and with no digits, a run's status, runs past the disc's end, umd0: as the
    whole disc (opened with a path after it too, its status, its listing, no working folder there), a host folder
    first;
  - every ioctl and devctl above (umd0:'s size in sectors, a seek past it, a read of no sectors), and the
    unsupported;
  - the drive: its state with and without a disc, activation, the disc's kind, callbacks, and a thread's waits: at
    once, until a timeout of 100 microseconds (240), of 1 (25 alone, 240 with callbacks), and for good until
    cancelled, on both engines.
- `tests/psp/ares` boots `tools/psp-test-programs`' new `disc` program from an ISO and from a CSO the test makes,
  through the system as Phobos runs it: every line it prints (the drive's state, a file's fingerprint, the ioctl's
  sector, its status, an `sce_lbn` run, umd0:, a listing, a relative path, the refusal to write) matches what the
  image holds. A program cut short by a truncated image still boots. An encrypted EBOOT.BIN alone doesn't start, nor
  with a blank BOOT.BIN beside it; with a plain one, BOOT.BIN does. It also checks `vfs::descriptor` (mapped and not,
  zeros past the end, still readable after the descriptor it was given closes, nothing for a bad descriptor or a
  folder) and boots a disc read through one, unmapped (127 checks).
- Thirteen broken versions each failed: a version 1 CSO's stored blocks taken for deflated ones, umd0: counted in
  bytes, the first sector left out of a file's status, short drive timeouts left as given, a folder's own records
  listed, runs read past the disc's end, `sce_lbn` numbers read as decimal, a failed block left in the cache, short
  names written for the disc, read-only opens that create refused, folders read past 256 sectors, and damaged
  records skipped, or ending only their sector, rather than ending their folder. Six got through the first tests
  (the buffer still held what a read should have put there; no run went past the end; the bad block failed before
  writing anything; the damaged folder was on a disc too small to show any of its three), which were tightened until
  they failed.

On the RP6 (2026-10-05): the `disc` program booted from an ISO and from a CSO printed exactly what the host test
expects, and pspsdk's cube runs from a CSO at 60 frames a second. The app could read the SD card's paths there, so
the descriptor route ran on the host only: Android wouldn't let the shell hand the app a document to force it.

## Part 15: save states

`serialize()` in each part (`ares/psp/cpu/serialization.cpp`, `memory/memory.cpp`, `ge/ge.cpp`,
`kernel/serialization.cpp`) and `System::serialize()`/`unserialize()`: everything the PSP was doing, to carry on from
exactly there.

- **What a state holds**: a header (a signature, "PSPS"; the version of its layout; RAM's size; and the program it
  was made with, an FNV-1a hash of the program's bytes as it started, from the game's folder or its disc), all of
  which must be the machine's, as another game's memory, threads and files mean nothing to this one; then memory,
  the CPU, the GE and the kernel.
- **Memory**: the scratchpad, VRAM and RAM, 4 KiB at a time, each piece's bytes only if it holds anything but zeros:
  games leave much of their 64 MiB untouched. Cube's state is about 1.6 MB, most of it VRAM (its two frame buffers
  and its depth buffer).
- **The CPU**: every register a program can see, and whether it's halted. Compiled code isn't saved: the recompiler
  starts afresh after a load.
- **The GE**: its commands' last words (each draw works its state out from them), the palette, the list it's running,
  where its vertices and indices are, the matrices, and what its next END means.
- **The kernel**: the program (its module, and its imports in the order its syscall codes count them, each found
  again in the kernel's table by its NID); its threads, each one's registers while it isn't running and what it
  waits for; the semaphores, mutexes, event flags, callbacks and memory handed out; the controller's samples; the
  display; the calls into the program; the GE driver's lists and the commands the GE has left in the frame; the
  clock. Open files are saved by their PSP paths and
  opened again on loading, where the devices are then, at the position they had; one gone from the host since is
  dropped (the program's next use of it fails as for any bad file), as is a file on the disc when no disc image is
  in the drive. Not saved: the devices and the disc (the system gives them, the same game's), and the kernel's table
  of functions.
- **A bad state**: one that isn't this machine's or this program's is refused before anything is touched. Otherwise
  loading checks what it reads, and refuses a state that ends part way or holds what no machine could:
  - a place past the end of the controller's ring of 64 samples; a display mode but the LCD's 480x272;
  - the GE's CALLs more than two deep (in the GE, in a display list or on its stack); a list's stack deeper than the
    list allows; display lists the driver couldn't have queued as they are (each list in the queue or free, once, as
    its state says; one running or completed that never started; the GE running or finishing one that isn't queued,
    or finishing one that hasn't completed);
  - a disc folder's names without their entries; a file on the disc open for writing; a host folder's names as no
    listing of it makes them ("." and ".." first, but not at a device's top, then single names a PSP path can name,
    with no '/', '\', ':' or NUL: reading the folder joins each to its place on the host, so a path, or ".." at the
    top, would reach outside the device's folder);
  - an ID or a file number at or past the next one to be handed out, or a map's key that isn't its object's ID; IDs
    or file numbers counted past 2^31 (they're positive 32-bit numbers, and stop short of that: newUID(), newFile());
    a running thread that isn't there;
  - what would hang the machine, or keep it busy for hours: a sound frame or more owed, a clock past a century, the
    next vertical blank more than a frame ahead or a frame or more behind, a sampling cycle sceCtrlSetSamplingCycle
    refuses or its next sample more than a cycle ahead or a frame or more behind, a wait for 64 controller samples or
    more.

  The machine is then put back from a state of itself made first, its open files kept just as they were (a file the
  program removed or renamed while it had it open couldn't be opened again by its path); should even that fail, the
  game starts afresh rather than run on from half of each. Lists have no limits of their own: their items are read
  one at a time, and a list that claims more than the rest of the state holds runs out of state part way. So no list
  the machine can make is too long to load, and however damaged a count, what loading takes in memory stays a small
  multiple of the state's own size (an item takes a few times its bytes in the state).
- **A program that has ended** (by leaving, or by crashing) makes no state: there's nothing to carry on from, and a
  state of it would only bring back where it stopped. The app saves states through `PhobosCore.trySaveState()`, which
  holds the core paused from its look to its write, waits two seconds at most for the frame to end (as unloading does
  before abandoning a stuck core), and says when there's no state to save: the auto-save as the game quits passes over
  that without a word, rather than tell the player the save failed, or wait for ever on a core that's stuck, while a
  save the player asks for (into any slot, Auto too) says it failed.
- **In the app**: Save, Load, the state hotkeys and Auto-Save State work for the PSP as for any system (part 13's
  stand-in, which left them out, is gone). States go by the game's name, and homebrew comes as an `EBOOT.PBP` in a
  folder named after it, so a PSP program in an `EBOOT.PBP` takes its folder's name, in the Library and when another
  app launches it (`Cube/EBOOT.PBP` is "Cube.pbp"; the folder comes from the file's path, its storage document's ID,
  or the launch URI's own path where that ends in the file's name): each program's states are its own. One whose
  folder can't be told (a document whose ID is only a number, a provider whose paths are numbers) stays
  "EBOOT.PBP". PlayStation games that come as an `EBOOT.PBP` keep their names, and with
  them their memory cards and states.
- **Found on the way**: restoring the GE's state from the program's buffer (`sceGeRestoreContext`, and a list's own
  context as it ends) let a buffer the program had written over set the GE three CALLs deep, past its two return
  slots. It's held at two now. Two places that trusted what only a damaged state could break now check: writing to a
  file refuses one on the disc, and taking a display list out of the queue leaves one that isn't there alone. A
  display list that never ends held everything up: with no thread to run, `Kernel::idle()` let the GE run again and
  again without time passing, and a list that stopped every few commands (at SIGNALs that jump, or SYNCs and
  FINISHes) got a fresh million commands at each stop, as it did each time a thread woke. Now the GE runs a million
  commands a frame at most, however many goes it has and however often it stops (each vertical blank gives it
  another million), time goes on while it works, and a list that SIGNALs for a callback over and over has the GE wait
  while one still waits its turn or runs (the PSP takes the GE's next interrupt only once the last one's handler has
  returned), so the calls into the program can't pile up. (The budget counts commands, not what they draw: a list
  that draws big primitives over and over can still make a frame slow to end, until the GE's timing counts its drawing
  too.) IDs and file numbers, which counted up without end, stop
  short of 2^31: what would need another fails, out of memory or with too many files open, rather than hand out one in
  use. And a folder's listing leaves out host names no PSP path can name ('\' or ':' in them), which made the
  machine's own states unloadable.

Tests:
- `tests/psp/ares` (163 checks): cube saved after 30 frames and loaded carries on exactly as it did (each of 20
  frames' picture, time and program counter, then RAM and VRAM), on the recompiler and the interpreter, and in a
  fresh session of the same game; its state holds the memory that's used and little else; a state with the wrong
  signature, one cut 4 KiB short (the kernel's lists run out of state), one cut 4 bytes short (only its end shows
  it) and one owing endless sound are refused, every byte of the machine's state as it was, and a file the program
  had open, removed from the host since, still open and reading; another program (pspsdk's blend) refuses cube's
  state, and is as it was; hello, once it has left, makes no state; cube booted from a disc image takes its state
  back, and a disc holding another program refuses it. It also passes built with the address sanitizer.
- `tests/psp/states.cpp`, three groups. "kernel states" takes the kernel's part from one machine to another: open
  files coming back at their positions and reading the host file as it is then, one deleted from the host dropped,
  a disc file and a folder half listed coming back, a semaphore, an event flag, a block of memory and a thread (its
  registers, its global pointer, its name), new descriptors going on from the old; a list and a text longer than the
  rest of the state refused; a disc file dropped where no disc image is in the drive (none, or a host folder standing
  for disc0:); host names with '\' or ':' left out of a listing. "state fields": a machine with one of everything has
  each of 212 fields (the CPU's, the GE's and the
  kernel's) changed in turn, each change leaving a value a fresh machine doesn't have, and each must change the
  state; the state, every field changed, then loads into a fresh machine, which must make the very same state; last,
  54 values no machine could hold are refused one at a time, the machine as it was after each (among them each kind
  of object's ID past the next to be handed out, IDs counted past 2^31, and folder names reaching outside their
  folder). "kernel ids run out": at 2^31, objects and files can't be made, on the memory stick or the disc (a
  thread's stack goes again), a file that would be made or emptied is left alone, and the machine, every ID and file
  number handed out, still saves and loads. "ge endless list" (in `tests/psp/ge.cpp`): lists that never end (a JUMP
  back, a SIGNAL that jumps back, SYNCs and FINISHes over and over, a dot drawn over and over), with no thread to run,
  still let the frame end, each vertical blank giving the GE its million commands; with a thread waking every 10
  microseconds, a frame still runs a million GE commands at most; one SIGNALing for a
  callback over and over keeps two calls waiting at most; and a callback that moves the stall address on finds the
  list waiting at the next SIGNAL.
- Broken versions each failed: a field left out, a check left out (the display's mode, the list a finish belongs
  to, a thread's ID, the folder names, ".." among them, '\' in them, IDs counted past 2^31), the bounds made one too
  tight for a machine that has handed out every ID or file number, the disc's file opened with no number left, the
  end check, the sound check, the disc program's hash, the open files put back after a refused load, the rule against
  states of a program that has ended, the listing that left out unnameable names, the callbacks' bound (with a
  callback waiting, and with one running), the clock held still for an endless display list and the GE's budget
  renewed at every stop (the test's alarm stops each, saying why), the GE's budget renewed at every go rather than
  every frame, or never renewed, or left out of the state, or unchecked, and the running thread left pointing at a
  thread already freed. The address sanitizer catches that
  last one: both test scripts (`tests/allegrex`, `tests/psp`) now build with it on macOS too, as on Linux, since its
  runtime no longer hangs at
  start there.
- The app's `LaunchSystemsTest`: an `EBOOT.PBP` takes its folder's name, and any other file, or an `EBOOT.PBP` whose
  folder can't be told, keeps its own; a launch's folder from its path, its document's ID, or another app's URI that
  ends in the file's name (nothing from a URI whose path is IDs).

## Part 16: more disc image forms

`ares/psp/kernel/disc.cpp` and `unpack.cpp` read the PSP scene's other compressed forms of an ISO, and nall's CHD
reader (`nall/nall/decode/chd.hpp`) now reads a DVD's CHD, as PSP games are kept in, which the system hands the disc
as an ISO's bytes. The formats as their tools write them (maxcso's description of CSO version 2 and ZSO; lz4's of its
block format; Linux's `lzo.rst` of LZO1X, there being no specification), cross-checked against the reference packers;
our own code.

- **CSO version 2**: version 1's layout, but a block that takes a whole block's room is stored as it is, and the
  index entry's top bit marks a block packed with LZ4 rather than deflate.
- **ZSO** ("ZISO"): version 1's layout, its blocks LZ4-packed (the top bit: stored as it is). With an index shift,
  a CSO's or a ZSO's blocks start at multiples of its power of two, each padded out to the next with any byte (NUL;
  ziso writes 'X'): LZ4's unpacking ends once the block is whole, leaving the padding alone, as maxcso's own does.
- **DAX**: a 32-byte header ("DAX\0", the disc's size, the version, how many areas are left uncompressed), each 8 KiB
  frame's start and packed size, and (from version 1) the areas left uncompressed; a frame is zlib's (deflate, with
  its 2-byte header and checksum), or as it is inside an uncompressed area. Every frame must lie inside the file, and
  the areas inside the disc, adding up to no more frames than it has, so a damaged count can't ask for a huge table
  or a long time to go through it.
- **JSO** ("JISO"): a 48-byte header (the block size, whether blocks have headers, the packing, LZO or zlib), then
  each block's start and one for the end. A block that takes a whole block's room is stored; zlib's blocks are read as
  zlib's or as raw deflate, as tools differ; a short last block stored at its own length is read too. Block headers
  (an option of the JSO tool) aren't read: such an image is refused saying so.
- **The unpackers** (`unpack.cpp`): LZ4's block format, and LZO1X as minilzo writes it. Each fills an output of a
  known size and checks every length and distance against both buffers, so damaged bytes can't make it read or write
  outside them; a block that unpacks short of its size fails its read.
- **CHD**: nall's reader, which read CD images (the PlayStation's, the Mega CD's, ...) opened by name, also reads a
  DVD's now (chdman `createdvd`: 2048-byte units, no tracks), and can read an image through a function rather than
  open it by name: the PSP's disc is read through the file mia's medium gives, on Android the app's descriptor, so a
  game of a gigabyte or two is read a hunk at a time, never copied. A DVD's damaged hunk reads as nothing (the read
  fails), not as the last hunk's bytes. A CD's CHD is refused as a PSP disc, saying a PSP game's is made with
  `createdvd` (mia says so, and the app's "Game Didn't Start" shows it: the runner passes on what a medium says when
  it refuses a game, `PhobosCore.loadProblem()`), and one that only holds its differences from a parent CHD is refused
  saying so; nall's CD reader (`vfs::cdrom`, the CD systems') refuses a DVD's, as it did. CHDs are read in builds with
  `ARES_ENABLE_CHD`, as all Phobos's are; others say they don't read them.
- **mia and the app**: mia's PSP medium tells each by what it starts with ("ZISO", "DAX", "JISO"; a CHD by
  "MComprHD" and, in version 5's header, its 2048-byte units), as `disc.zso`, `disc.dax`, `disc.jso` or `disc.chd`.
  The app takes .zso, .dax, .jso and .chd for the PSP, read through the descriptor like an ISO. A .chd goes to the
  PSP by its folder (or the launching app's hint), as the CD systems take CHDs too. The PSP's discs aren't gathered
  into multi-disc sets, as the CD systems' are: they can't be swapped yet.

Tests:
- `tests/psp/disc-formats.cpp`, three groups. "disc formats": a disc whose blocks pack every way (noise, stored as it
  is; text; zeros; noise repeated 20 KiB on), made into CSO version 2 (2 and 8 KiB blocks; aligned to 4 bytes with
  NUL padding and to 64 with 'X'), ZSO (2 and 8 KiB; padded the same two ways), DAX
  (with and without uncompressed areas) and JSO (LZO in 2 and 32 KiB blocks, zlib, raw deflate in 8 KiB blocks, LZO
  and zlib with a short last block stored), each read back byte for byte the same as the ISO; and each image holds
  blocks of every packing it's meant to test. "disc formats damaged": a CSO version 2 LZ4 block that runs past its
  end, a DAX frame whose zlib header is wrong and a JSO block whose LZO end is damaged fail their reads while the
  other blocks read; a ZSO index going back, a DAX cut short in its tables, with a frame past the file's end, with
  more uncompressed areas than frames, an area past the last frame or areas adding up past the disc, and a JSO with
  block headers, an unknown packing or a block bigger than a block are refused, each with its message.
  "unpackers": blocks minilzo 2.06 and the lz4 tool (1.10, `-12`) packed from three inputs (2 KiB; 40000 bytes with
  matches 16 to 32 KiB back; 50000 with one 32 to 48 KiB back) in `tests/psp/unpack-vectors.hpp`, and two LZO streams
  written by hand for the 2-byte and 2 to 3 KiB matches minilzo's packing of them doesn't use (minilzo's own unpacker
  unpacks them the same), unpacked exactly, and an LZ4 block with NUL or 'X' padding after it too; cut short at every
  length, never all of the block; into one byte too little room, nothing; matches before the start, LZ4's distance
  of 0, and counts past the output refused.
- `tests/psp/ares` (195 checks): the disc program boots and reads its disc exactly from a CSO version 2, a ZSO, a
  DAX, a JSO and CHDs (hunks of one sector and of four), as from the ISO and the CSO; a CD's CHD, and one needing its
  parent, isn't taken as the disc. Its script builds libchdr as the app does.
- The app's `LaunchSystemsTest`: .zso, .dax and .jso go to the PSP alone; a .chd by its folder.

## Part 18: decryption

`ares/psp/kernel/aes.cpp`, `kirk.cpp`, `keys.cpp` and `decrypt.cpp` (declared in `crypto.hpp`): a shop-bought game's
programs come encrypted (a disc's EBOOT.BIN, and most PRXs it loads), in Sony's `~PSP` format, and the kernel
decrypts them with the published keys before loading them, as the PSP's own kernel does with its crypto chip, KIRK.
Written from the PSP Developer Wiki's "Kirk", "Keys" and "PRX File Format" pages (as the Wayback Machine keeps them),
FIPS-197 and FIPS 180-1. Where the wiki stops (how each type of `~PSP` file hides KIRK's header), PPSSPP's
decrypter was read to learn the steps, which are set down below in our own words; the code was written from this
description.

- **AES-128** (FIPS-197), one block at a time or a run of them with ECB or CBC, each way. nall has none. The S-box is
  made from its definition (reciprocals in GF(2^8), then the affine transformation) rather than typed out.
- **SHA-1** (FIPS 180-1), moved out of `Kernel::nid()`, which takes its digest's first four bytes.
- **KIRK** is given a command number, an input that starts with the command's header, and an output; it answers 0 or
  an error number (the wiki's). Three of its commands:
  - **1, decrypt private**: a 0x90-byte header, a padding, then data. The header's first 16 bytes are the data's AES
    key, encrypted (one block) with KIRK's command 1 key; 0x60 holds 1 (the command), 0x64 whether the header is
    signed with a CMAC (0) or ECDSA (1), 0x70 the data's size and 0x74 the padding's. The data is AES-128-CBC with a
    zero IV. The PSP checks the signature first; Phobos doesn't (an ECDSA one would take Sony's private key to make,
    so no test could, and the `~PSP` file's own check below already tells a damaged one).
  - **7, decrypt static**: a 0x14-byte header (0x00: 5, the mode for decrypting; 0x0c: the keyseed; 0x10: the data's
    size), then data in AES-128-CBC with a zero IV under KIRK's key 4 + keyseed (the wiki lists them by keyseed). A
    PSP changes the keys of keyseeds 0x20-0x2f and 0x6c-0x7b with its own fuse ID: they're refused, and no tag uses
    them.
  - **0xb**: the SHA-1 digest of the data after a 4-byte size.
- **The `~PSP` header**, 0x150 bytes, then the encrypted program. In the clear: 0x00 "~PSP"; 0x04 the module's
  attributes; 0x06 bit 0, the program is compressed (bits 8-11 say how: 0 gzip, 1 "2RLZ", 2 "KL4E"); 0x0a the
  module's name (28 bytes); 0x28 the program's size once decrypted and unpacked; 0x2c the file's size; 0x7c the
  decryption mode (1 a kernel module, 4 a user module, 9 a game disc's EBOOT.BIN, ...). From 0x80 to 0x150: the
  pieces of KIRK command 1's header, hidden, the tag at 0xd0, and a SHA-1 digest to check them by. Where each piece
  is depends on the type.
- **The tag** names the key. A table (`keys.cpp`) gives for each tag a key (144 bytes for the first years' tags, 16
  for later ones), the keyseed for KIRK command 7, and the type: the steps that rebuild KIRK's header. The keys are
  the wiki's; which tag goes with which key, keyseed and type is PPSSPP's tables', checked against the wiki's list of
  types by tag (read from the PSP's own `mesg_led` module) and against the user's games. A tag listed twice (a kernel
  module's 0x00000000 and 0x4467415d, with a 144-byte and a 16-byte key) is tried both ways: the check says which.
- **Type 0** (a 144-byte key; the first games): KIRK's header is stored in two pieces, its first 0x40 bytes at
  0x110-0x150 and its last 0x50 at 0x80-0xd0 (so the data's size and padding are at 0xb0 and 0xb4, in the clear).
  The check: the SHA-1 digest of the key's first 0x14 bytes, 0xe8-0x110, the header's two pieces in order and the
  file's first 0x80 bytes must be the 20 bytes at 0xd4. Then the header's first 0x70 bytes are XORed with the key's
  bytes 0x14-0x84, decrypted with KIRK command 7 under the tag's keyseed, and XORed with the key's bytes 0x20-0x90.
- **Type 1**: type 0 behind one more lock: before anything else, the 0xa0 bytes made of 0xe0-0x150 then 0x80-0xb0
  are decrypted with KIRK command 7 and put back where they were.
- **Type 2** (a 16-byte key): the key is first spread into a 0x90-byte pad: nine copies of it, copy n with its first
  byte replaced by n, decrypted together with KIRK command 7. KIRK's header has its first 0x40 bytes stored at
  0x80-0xb0 and 0xc0-0xd0, and its 0x70-0x80 (the sizes, in the clear) at 0xb0-0xc0; the rest isn't stored: zeros,
  but 1 at 0x60. The 0x60 bytes made of 0x140-0x150, 0x12c-0x140, 0x80-0xb0 and 0xc0-0xcc are decrypted with KIRK
  command 7 and put back. The check: 0xd4-0x10c holds zeros, and the SHA-1 digest of the tag, the pad's first 0x10
  bytes, 0xd4-0x12c, 0x140-0x150, the header's 0x40 stored bytes (0x80-0xb0, 0xc0-0xd0), 0xb0-0xc0 and the file's
  first 0x80 bytes must be the 20 bytes at 0x12c. Then the header's first 0x40 bytes are XORed with the pad's bytes
  0x10-0x50, decrypted with KIRK command 7, and XORed with the pad's 0x50-0x90.
- **Type 6**: type 2 signed with ECDSA, the signature's last 0x20 bytes at 0x10c-0x12c, where type 2 has zeros: both
  are hashed alike, and one routine reads the two. (KIRK's header is then in its ECDSA form, which only the signature
  check reads.)
- **Type 5**: type 2 with a 16-byte XOR key of the tag's own: first, the 0x50 bytes made of 0x80-0xb0, 0xc0-0xd0 and
  0x12c-0x13c are XORed with it (over and over) and decrypted with KIRK command 7; then the 0x60 bytes type 2
  decrypts are XORed with it just before they are. 0xd4 may hold anything (0xd5-0x12c holds zeros), and the digest
  counts 0xd4-0x12c as zeros. (A PSN game's programs mix a second key, from its license, into the pad and the first
  step; a disc's never do.)
- **The program**: KIRK command 1 on the rebuilt header, the file's first 0x80 bytes as its padding (0x74 must say
  0x80) and the file from 0x150 as its data. If the header says compressed with gzip (RFC 1952), the data is
  unpacked with nall's inflate, and must come to the size at 0x28, with gzip's own CRC-32 and size right.
- **Not read**: the other types (3, 4 and 7 to 10: DRM, demos and applications, updates kept on the memory stick, and
  the firmware's types 9 and 10), tags whose key the wiki hasn't published, and the "2RLZ" and "KL4E" packings
  (KL4E packs the 880-byte splash screen module, OPNSSMP.BIN, some discs carry for the PSP's menu): each is refused,
  saying which.
- **A "~SCE" header** comes before the ~PSP one in some programs (64 bytes, its own length at 4; GTA Liberty City
  Stories' modules have one, seen in the game's files): nothing in it is needed, and it's passed over.
- **Where**: the loader (`Loader::load()`) decrypts an encrypted program before reading it as an ELF, so whatever
  loads a program (the kernel's `load()` and `start()`, an EBOOT.PBP's DATA.PSP, a module, part 19) takes one, and
  refuses one it can't decrypt with the decrypter's reason (the module's name, its tag, and why). A disc's EBOOT.BIN
  starts decrypted; a plain BOOT.BIN beside it only when EBOOT.BIN can't start, and the system reports why EBOOT.BIN
  couldn't ("its tag names a key Phobos doesn't have", say), whether or not BOOT.BIN then does. Each program is tried
  with the kernel's `load()`, so one that fails leaves nothing behind. AES decrypts about 46 MB a second on the Mac
  (-O2): a game's 5 MB EBOOT.BIN takes a tenth of a second.

Checked against the user's games (CHDs read in place on the Mac with a scratch tool built on these files, nothing
decrypted kept): every EBOOT.BIN and every module on the disc decrypts at the first key its tag names, into a MIPS ELF
whose program headers lie inside it:
- Lumines: EBOOT.BIN tag 0x08000000 (type 0); its four kernel modules, tag 0 (type 0, three packed with gzip).
- Burnout Legends, GTA Liberty City Stories, Midnight Club 3 (v2.02), SOCOM Fireteam Bravo, Snoopy vs. the Red
  Baron: EBOOT.BIN tag 0xc0cb167c (type 1); Burnout's seven and Snoopy's nine kernel modules, tag 0 (type 0, mostly
  gzip), and Snoopy's rinit.prx, 0x03000000 (type 0, gzip); SOCOM's five kernel modules, 0x4467415d (type 1, its
  144-byte key), and its libatrac3plus and mpeg, 0x3ace4dce (type 1).
- Gunhound EX and Peace Walker (v2.00): EBOOT.BIN tag 0xd91613f0 (type 2); Gunhound's a static executable.
- Refused, as described: each disc's firmware updater (UPDATE/EBOOT.BIN, tag 0x02000000, a VSH module's, left out of
  the table) and Gunhound's and Peace Walker's OPNSSMP.BIN (tag 0x457b0cf0, type 2: it unlocks, but is packed with
  KL4E).

Tests:
- `tests/psp/crypto.cpp`, four groups: "crypto aes" (FIPS-197's examples, NIST SP 800-38A's ECB and CBC ones, each
  way; a thousand chained blocks, whose last OpenSSL gave; a partial last block left alone); "crypto sha-1" (FIPS
  180-1's examples, a NID, command 0xb and its refusals); "crypto kirk static" (data encrypted as command 4 does comes
  back under every keyseed not made per console, in place too; per-console and missing keyseeds, other modes, no
  data, data not in whole blocks, inputs and outputs too small: refused); "crypto kirk private" (data encrypted as
  Sony's tools do, in whole blocks or not, with and without padding, CMAC or ECDSA headers, in place too; another
  command, no data, too small an output, an input cut short, huge sizes and paddings: refused).
- `tests/psp/decrypt.cpp`, five groups, on programs encrypted by `tests/psp/encrypt.hpp`, which runs part 18's steps
  backwards with the core's AES, SHA-1 and tables: "decrypt types" (a PRX under tags of types 0, 1, 2, 5 and 6, with
  and without gzip, a signature's end of zeros and not, and the two keys of a tag listed twice: decrypted exactly,
  then loaded and run on both engines); "decrypt every tag" (each tag in the tables round-trips; none names a keyseed
  made per console); "decrypt refusals" (not a ~PSP file; cut short at every length of its header, and in its
  program; an unknown tag, and another tag's; every byte of the header damaged, for a tag of each type: refused, but
  for type 5's 0xd4, which nothing reads; the program said to be elsewhere, or bigger than the file; 2RLZ, KL4E and
  an unknown packing); "decrypt packing" (gzip's stream, CRC and size damaged, a stream cut short, a name that never
  ends, not deflate, a wrong size in the ~PSP header: refused; a header with every optional field: read; a damaged
  program comes out different, as the PSP's signature over it isn't checked, unless it's packed); "decrypt kernel
  loads" (the kernel loads an encrypted program, and refuses one with an unknown tag with the decrypter's reason,
  leaving nothing behind). The loader's refusals pass on the decrypter's reasons.
- `tests/psp/ares` (220 checks): the disc program encrypted under tags of types 1 and 2 (the second packed with gzip),
  a plain BOOT.BIN beside it: EBOOT.BIN starts and reads its disc exactly as the plain one does. One cut short, and
  one whose tag names no key: alone, beside a blank BOOT.BIN, and beside a plain one, which starts; each time the
  system reports why EBOOT.BIN couldn't, and whether BOOT.BIN started instead.
- Broken versions each failed a test: type 6's signature left out of the digest, type 1's extra lock skipped, gzip's
  CRC unchecked, KIRK command 1's partial last block chained from the IV, type 5's zeros unchecked, and only the
  first key of a tag listed twice tried.

## Part 19: modules

`ares/psp/kernel/modules.cpp` (ModuleMgrForUser): the modules (PRXs) a program loads besides itself, from the disc
or the memory stick. The functions, their arguments and errors are pspsdk's (`pspmodulemgr.h`, `pspkerror.h`);
PPSSPP's module manager was read for how games use them (Sony's modules loaded beside a game, a "~SCE" header before
a module); our own code.

- **Loading** (`sceKernelLoadModule(path, flags, options)`): the file is read whole, from the disc by its path or as
  a run of sectors (`sce_lbn...`, as games name modules too), or from a host folder standing for a device.
  `sceKernelLoadModuleByID(file, flags, options)` reads one from a file already open, from where it's been seeked
  to, as games keep modules in archives of their own: an encrypted one is as long as its ~PSP header says, a plain one
  runs to the file's end (16 MiB at most). An encrypted module is decrypted (part 18). A PRX goes into a block of the
  user partition, the lowest free place, a static module where it was linked; the loader relocates it there and
  reads its imports and exports. Its ID comes back.
- **Sony's modules are stood in for**, not run. Early games carried Sony's libraries for sound, video and the network
  (sceSAScore, sceATRAC3plus_Library, sceMpeg_library, sceNet_Library, and kernel drivers such as
  sceAudiocodec_Driver), which run on top of Sony's kernel and its hardware; the HLE kernel answers their functions
  itself. They're told by their names, "sce" or "Sce" first as all Sony's are, or by being kernel modules (attribute
  0x1000), which a game's own never are; an encrypted one's name is in its ~PSP header, in the clear, so it needn't
  even be decrypted. A stand-in has an ID and a name, nothing in memory, and starts and stops at once.
- **Linking**: after each load and unload, every module's imports (the program's too) are linked to the functions
  loaded modules export in their libraries. An import the HLE kernel has no function for, which a module exports,
  becomes `j address; nop` in place of the kernel's syscall (the caller's `jal` left `ra` pointing back at it, so the
  function returns straight there); every other import is the kernel's syscall, so a stub linked to a module since
  unloaded goes back to the kernel. A stub already right isn't written again, as that would throw away the code
  compiled around it. Variables imported from other modules aren't linked yet.
- **Starting and stopping** (`sceKernelStartModule` and `sceKernelStopModule(module, argument size, argument, where
  to put the result, options)`): the module's `module_start` (or `module_stop`), exported for itself (NIDs
  0xd632acdb and 0xcee8593c), runs on a thread made for it, its argument copied onto the thread's stack, with the
  options' stack size, priority and attributes when they give them (else 256 KiB, 0x20, and user mode with the VFPU,
  as the program's first thread has). It returns to a third syscall in the kernel's trampoline, which ends and
  deletes its thread, as the PSP's module manager deletes the thread it made. The calling thread waits meanwhile, and
  then gets the module's ID, the function's result written where it asked. A module without the function starts (or
  stops) at once.
- **Unloading** (`sceKernelUnloadModule`): a module never started, or stopped; its memory goes back to the user
  partition, and stubs linked to it go back to the kernel.
- **IDs**: the program is a module too, its ID handed out once it's loaded. `sceKernelGetModuleIdByAddress` gives the
  module holding an address, in any of memory's windows; `sceKernelGetModuleId` the caller's (the module holding the
  code that called, else the program's); `sceKernelGetModuleIdList` all of them, the program's first.
  `sceKernelQueryModuleInfo` fills a SceKernelModuleInfo as far as its size says: segments, entry, gp, attributes,
  version, name. The loader keeps each segment's size in memory but not its file and zeroed parts, so the text is the
  first segment, the data the others, and the bss 0.
- **Refused**, with pspkerror.h's errors: a file that isn't there, or a folder (the file system's errors); what isn't
  a module (illegal object); an encrypted module that can't be decrypted (unsupported PRX type, and the decrypter's
  reason noted); no room (no memory); an unknown ID; starting a module twice; stopping one not started, or stopped
  already; unloading one that's running.
- **States** carry the loaded modules (each one's ID, file, whether it's a stand-in, its memory block, its status,
  the thread running its function, and its module as the program's is saved) and the program's ID. The state's
  version is now 2, so a state made before is refused rather than misread.

Checked against the user's games (the system run on the Mac on their CHDs, nothing kept): Burnout Legends loads
fourteen modules from its disc by path (seven kernel drivers, encrypted, and seven libraries, not), each Sony's and
stood in for, and runs on further than before (its GE ran twice the commands in its first 300 frames); GTA
Liberty City Stories loads three by `sceKernelLoadModuleByID` from runs of sectors, each behind a ~SCE header
(sceAudiocodec_Driver, sceATRAC3plus_Library, sceSAScore), stood in for; Gunhound EX finds its module's ID by address
and as the caller's. Each game then stops at other functions the HLE kernel doesn't have yet (fixed-size memory
pools, the power library, the SDK version calls).

Tests (`tests/psp/modules.cpp`, five groups, on PRXs built in the test: TESTLIB exports a library's function and has a
module_start and a module_stop; TESTUSER imports the function):
- "modules start and link": a program on both engines loads both from the memory stick, the second encrypted;
  starts the first (its result comes back), then the second with an argument (it sees the argument; its import
  reaches the first's function); stops the first and unloads it: the second's stub goes back to the kernel, the
  first's memory to the user partition, and each function's thread is gone once it returned.
- "modules linking": a module loaded before what it imports is linked once that comes; loaded from the disc by path
  and as a run of sectors.
- "modules stand-ins": Sony's modules, one encrypted under a tag Phobos has no key for, one a kernel module, one plain
  and named "Sce...": IDs and nothing in memory, started and stopped at once; `sceKernelLoadModuleByID` from inside
  an archive, behind ~SCE headers: a stand-in, and a module loaded for real.
- "modules identities": by address (in another window too), the caller's, the list, the information (only as far as
  its size says).
- "modules refusals": each refusal above; a module that can't fit leaves nothing behind.
- "state fields" changes each new field and refuses a module under another's ID, IDs not handed out yet (the
  module's, its block's, its thread's, the program's) and a status there isn't; "decrypt kernel loads" loads a
  program behind a ~SCE header.
- Broken versions each failed a test: imports never linked to exports, module_start's result written wrong, an
  unloaded module's stubs left linked, Sony's modules loaded like any other, module_start's thread kept, a loaded
  module's own fields left out of states, and ~SCE headers not passed over.
