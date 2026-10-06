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
`cursor/psp-umd-2b67`; part 15, save states, on `cursor/psp-states-2b67`. Part 17, the functions the retail games that
now start ask for (callbacks, sound output's timing, power, interrupt handlers, memory pools, the system's dialogs and
saves), is on `cursor/psp-hle-games-2b67`, on top of `cursor/psp-retail-load-2b67`; parts 18 and 19, decryption and
loading modules, are on `cursor/psp-decrypt-2b67` (#144), which `cursor/psp-hle-games-2b67` has since merged.
Part 21, sound (sceAudio's channels mixed into the system's stream, sceSasCore's voices heard), is on
`cursor/psp-sound-2b67`, on top of part 20's `cursor/psp-hle-games2-2b67`. Part 23, the system fonts (sceLibFont
drawing the owner's own flash0 fonts), is on `cursor/psp-fonts-2b67`, on top of part 22's
`cursor/psp-hle-games3-2b67`; part 24, making the GE fast, on `cursor/psp-ge-speed-2b67`, on top of part 22's too,
with part 23 merged in.

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
- **The GE's renderers** (2026-10-06): make the software renderer as fast as humanly possible first; later add both
  a Vulkan and an OpenGL renderer as options for speed, still as accurate as possible. Accuracy is not negotiable in
  the software renderer: it is the reference, and every speedup must draw exactly the pixels it drew before (part
  24).
- **The owner's choices of 2026-10-06 (night)**:
  - The GE's drawing threads: by default as many as the device has cores, but one; a setting changes it (part 24:
    the app's PSP setting "Drawing threads").
  - The system fonts: found by themselves in the device's `Download/FLASH0DUMP` (the dumper's folder) where it's
    there, plus Settings' picker for any other folder; never in a backup (part 23).
  - Game music and movies (ATRAC3+, MP3, the PSP's video): through FFmpeg's LGPL decoders, built in an
    LGPL-compliant way.
  - The order from here: after the fonts and speed, the stuck games further (Burnout Dominator's GE hang, Peace
    Walker after its install, the GTAs into play), then the codecs, then later the Vulkan and OpenGL renderers.

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
flash0's fonts), isn't there yet (part 23 adds it). A program started from another app's intent is read where it is
when the app can read the path; otherwise Phobos copies it into its cache first, as it does every game, so a
program's own files beside it aren't there.

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
  - memory as the kernel never hands it out (since part 17's final review): a block outside the user partition, a
    thread's stack that isn't a block of its own, of its size, or a block two owners claim (threads' stacks, memory
    pools, modules and the program);
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
  each of its fields (212 then, more with parts 17's and 19's; the CPU's, the GE's and the
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

## Part 17: the functions retail games ask for

On branch `cursor/psp-hle-games-2b67`, on top of `cursor/psp-retail-load-2b67` and its "one block for a program's
memory" (2026-10-05), and since then of parts 18 and 19 (`cursor/psp-decrypt-2b67`, #144), merged in: see the end
of this part. Lumines,
Space Invaders Extreme, Brave Story: New Traveler, GTA: Sindacco Chronicles (a GTA LCS mod with a plain EBOOT) and
the Street Fighter III 3rd Strike port load from the user's CHDs and run their own code; this part gives them what
they asked for next, as a scratch host runner (never committed: the system as `tests/psp/ares` builds it, booting a
CHD, tracing every system call, dumping frames) showed it, function by function. Behavior comes from pspsdk's
headers and from PPSSPP's reading of the PSP (its tests on the hardware, pspautotests), which the code cites where it
depends on it; our own code. Sound output, the sub-interrupt handlers (with the vertical blank's dispatch),
sceKernelChangeThreadPriority and sceKernelGetThreadStackFreeSize were then rewritten clean-room, from a behavior
specification drawn from pspautotests' recorded results and pspsdk alone, without reading any emulator's code or
the code they replace.

- **NIDs that aren't their names' hashes.** Sony gave some later functions random NIDs, so the kernel can list a
  function by the NID games import (`addNID` in kernel.cpp): sceKernelSetCompiledSdkVersion370 and its siblings for
  later SDKs (342061e5...), sceKernelStopUnloadSelfModuleWithStatus (8f2df740), under the names the homebrew scene
  gave them. Every other new function is listed by name, and each name's hash was checked against the NIDs the
  games import.
- **Memory** (sysmem.cpp). sceKernelAllocPartitionMemory's aligned types: 3, the lowest place starting on a multiple
  of an alignment (the fifth argument, a power of two), and 4, the highest; Sony's SDK makes its heaps with them,
  and refusing them was Space Invaders Extreme's C++ abort and Brave Story's "can't allocate memory". The type is
  checked first, then the alignment, then the partition. The PSP-2000/3000 emulated has 64 MiB, but gives a program
  a user partition of 24 MiB (0x08800000 to 0x0a000000, as much as a PSP-1000's 32 MiB leaves), the first thread's
  stack at its top as on a PSP, unless the program's PARAM.SFO (its EBOOT.PBP's, or the disc's PSP_GAME/PARAM.SFO)
  asks for all of RAM with MEMSIZE 1, as the Street Fighter III port does; the shop-bought games don't. Lumines'
  23 MiB program leaves it about 1 MiB, in which it makes its threads and a 64 KiB block, as on a PSP. The SDK and
  compiler versions a program's start-up code sets are kept to be read back.
- **Callbacks** (events.cpp). A thread's callbacks, once notified (sceKernelNotifyCallback, or the system: the power
  switch's are told of the battery as they're registered), run on that thread when it waits in a function whose
  name ends in CB (sceKernelSleepThreadCB, DelayThreadCB, WaitSemaCB, WaitEventFlagCB, WaitThreadEndCB,
  sceDisplayWaitVblankStartCB, sceUmdWaitDriveStatCB, AllocateFplCB/VplCB) or calls sceKernelCheckCallback, by its
  priority: it's made ready with its wait kept, its registers and wait are put aside, each callback runs on its stack
  as fn(times notified, the last word, its argument), one returning non-zero is deleted, and the thread goes back
  into its wait, which ends at once if its time ran out (the clock doesn't stop for callbacks) or what it waited for
  came or was deleted meanwhile. Callbacks may wait themselves. A thread's callbacks go with it. A CB function that
  needn't wait, what it waits for being there already (a semaphore's count, a wakeup that came first, an event flag's
  bits, a pool's room, a thread that has ended, the drive ready, sceDisplayWaitVblankCB inside the blank), still runs
  the callbacks notified by then: they run at once, as sceKernelCheckCallback's do, and the function returns what it
  got when they're done (it skipped them; a wait made to wait for them instead could time out in a long callback
  after getting what it asked for, and sceDisplayWaitVblankCB's 1 would become a wait for the next blank).
- **The display** (display.cpp): sceDisplayWaitVblankStartCB, WaitVblank (not waiting, returning 1, inside the
  vertical blank, which lasts 0.77 ms as pspautotests measured), IsVblank, GetCurrentHcount (lines of 525 dots at
  9 MHz, counted from the blank's start).
- **Interrupt handlers** (interrupts.cpp): sceKernelRegisterSubIntrHandler, ReleaseSubIntrHandler, EnableSubIntr
  and DisableSubIntr, written (clean-room, from a behavior specification) after pspautotests' intr/registersub,
  intr/releasesub and intr/enablesub, whose results were recorded on a PSP (firmware 6.61, under PSPLink), and
  pspsdk's pspintrman.h. A program may register only on the GE's interrupt (25) and the vertical blank's (30). The
  other numbers below 67 are restated in a table in numeric order, as three kinds: no handler (NOTFOUND_HANDLER for
  both calls), a handler without sub-interrupts (7, 10, 12, 15-20, 22-24, 26, 31, 36, 50, 56-61, 65: ILLEGAL_INTRCODE
  for both), and sub-interrupts the kernel keeps (4, 6, 21: registering is ILLEGAL_INTRCODE, releasing finds nothing,
  NOTFOUND_HANDLER); 67 and up are illegal. On the vertical blank, sub-interrupts 0-15 are the program's, the display
  driver holds 18-20 and 24-26 (FOUND_HANDLER), the rest up to 31 are illegal to register and not found to release.
  A null handler takes no place. Enabling and disabling look at the numbers alone, so a sub-interrupt enabled before
  its handler is registered runs; releasing disables it. The vertical blank's handlers run at each blank, by number,
  as calls into the program (part 9), with (their number, their argument) and the global pointer they were
  registered with; time goes on for them while every thread waits (Lumines' main task waits on its handler). While
  they can't run (interrupts held off, or another call running), the blank stays pending, once, as the PSP's
  interrupt controller keeps an interrupt: however many blanks go by, each handler runs once when they can. (A
  review had found the previous code queueing a call for every blank held off: 600 after 600.) It stays pending too
  while the last blank's handlers still wait their turn or run (each call says whether it's a vertical blank's), so
  handlers slower than a frame run back to back, the queue never holding more than one blank's: the final review
  found two 20 ms handlers queued again at every return, 97 calls waiting by frame 120, all of them saved in states.
  Untested on a PSP: the GE's sub-interrupts beyond 0 (all 32 are the program's here), and enabling on interrupts
  other than 30.
- **Sound output** (audio.cpp), rewritten clean-room from a behavior specification drawn from pspautotests' audio/*
  and intr/waits results (recorded on a PSP) and pspsdk's pspaudio.h. The eight mixer channels: reserving (-1 or any
  negative number for the highest free channel, one released but still playing out passed over), releasing (refused
  while a thread waits; a buffer in the slot plays out), blocking and non-blocking outputs, panned or not, each with
  its own volume rule (a negative volume keeps the channel's, except for the panned blocking output, which refuses
  it), the two rest lengths (sceAudioGetChannelRestLen counting a null buffer's count, RestLength not), the data
  length, format and volume changes, each call's errors in the order the tests show. A channel holds one buffer,
  which the mixer's DMA reads a block of 64 samples at a time from every channel with one, every 64/44100 s (exact
  to the cycle: a block is 483,265 and 15/49 cycles). The first buffer to an idle DMA loses its first block at once;
  one joining a running DMA waits for the next boundary; after the last samples the DMA runs one more block, then
  stops. A blocking output into a busy channel waits until its buffer has been taken (one waiter per channel; a
  second is told BUSY at once), so a stream of blocking outputs returns a buffer's time apart, and 64-sample ones a
  block apart: 100 of them take 98 blocks, about 142 ms, never less (the previous code didn't pace them at all). A
  blocking null buffer is how a program waits for a channel to drain. The SRC channel (sceAudioOutput2* and
  sceAudioSRC*, one channel reached two ways, at 8-48 kHz or 0 for 44.1): two buffers armed at most, transferred one
  after the other at its rate; an output arms its buffer and waits for a completion (one pending returns at once, and
  starting from idle makes one), so a steady stream returns a buffer apart; a null output waits until everything
  armed has played; releasing is refused while anything is armed. A buffer's slot frees as its transfer ends, 100 µs
  before it has been heard (the behavior specification's 8.4; pspautotests' audio/output2/rest has a 64-sample
  buffer, 1451 µs long, read as gone after "13XX µs"): the first buffer after an idle stretch retires 100 µs short of
  its length, and each one chained after it a whole buffer later, so the stream's pace and audio/output2/release's
  results (still armed 1 ms on, released 2 ms on) are as they were. From an interrupt handler or with interrupts held
  off, an output that would have to wait gets ILLEGAL_CONTEXT or CAN_NOT_WAIT (an SRC output's buffer stays armed and
  plays). **The samples aren't mixed yet: the speakers get silence**, but every buffer takes its playing time, which
  is what paces the games' sound threads (Lumines' spun at full speed without it, starving its game). The mixer takes
  each block where a mixer would read it (its address, format and volumes are at hand), so mixing into the system's
  stream comes next. Not done: sceAudioOneshotOutput, input, sceAudioSetFrequency, and the time the calls themselves
  take on a PSP (100 µs to 1 ms for an output that starts the DMA); the driver's defect that leaves a channel marked
  as waited on for good after a refused wait isn't copied.
- **Power** (power.cpp): the battery (on the charger, full), callbacks for the power switch (16 slots), the clocks
  as games set them and read them back (floats in f0; changing the PLL makes the caller wait as PPSSPP measured: 150
  ms, 16.6 between 266 and 333 MHz). The CPU's time still counts 333 MHz cycles, one per instruction, whatever is
  asked: a game at 222 MHz gets its work done faster than a PSP would (it waits for the vertical blank anyway).
  sceSuspendForUser's power tick and locks, and the volatile memory (0x08400000, 4 MiB) lent once at a time.
- **Memory pools** (pools.cpp): fixed (FPL) and variable (VPL), created from the user partition, handing out the
  lowest free block or place (a VPL keeps 32 bytes for itself and 8 before each piece, as the PSP's do; how the PSP
  chooses places isn't known here, so free sizes may differ), threads waiting in order or by priority, timeouts,
  deletion and cancelling. Creation's refusals and their order, and a VPL too small to hold anything made 4 KiB, are
  what pspautotests' threads/fpl and threads/vpl tests expect. A waiter that leaves without being served (its
  timeout runs out, or it's terminated, or it ends in a callback that put its wait aside) has the pool serve those
  behind it then, as semaphores do theirs (`waiterLeft()`, since the final review): one that didn't fit had held
  them up, and they waited for the next piece given back.
- **The system's dialogs** (utility.cpp): one at a time, their statuses (starting, running, finished, closing) with
  PPSSPP's timings. Saves are real: a folder per save in the memory stick's PSP/SAVEDATA (`<game><save>`) holding the
  data file the game hands over, loaded back, sized (the stick's free space, 1 GiB, the save's size), listed and
  deleted (modes 6, 7, 9, 10 and 21 take the save's folder; erasing, 19 and 20, the data file named alone); a load
  with none says so (the PSP's "no data"). The names become a folder and a file on the host, so the game's (which
  can't be empty), the save's (a list's first too) and the data file's must each be one plain name ending inside its
  field (13, 20 and 13 bytes): not "." or "..", no '/', '\', ':' or control character. The save's folder must then
  be one of SAVEDATA's own and its file one of the folder's, links followed (one leading elsewhere is no save): the
  memory stick's own check only kept paths on the stick, so a game's name of "" or ".." had a delete take every save
  or PSP/ with its homebrew, and a data file's name could read /etc/hosts. The buffer must be in the program's memory
  as far as what's copied (a save from nowhere wrote zeros); a load reads no more than the buffer takes and tells
  how much it read. Anything else is refused as the mode's group's bad parameter (0x80110308 for loads, 0x80110388
  saves, 0x80110348 deletes, 0x801103c8 sizes, 0x80110328 the rest), before anything is touched, and nothing throws
  (the sizes and list modes read folders through the host's error codes). Not yet: the encryption a PSP applies, the
  save's PARAM.SFO and icons, and drawing the dialogs. A message dialog is answered Yes at once (its text noted); the
  keyboard, network settings, game sharing and the browser are cancelled. sceUtilityLoadModule (and the net module
  versions) marks the optional libraries the HLE provides loaded.
- **Threads and clocks**: sceKernelChangeThreadPriority (0x08-0x77, 0 for the caller's own, a dormant thread
  refused; the thread goes to the back of its new priority's line, so the caller gives way to its equals; a thread
  started again is back at its first priority: pspautotests' threads/threads/change), GetThreadExitStatus,
  TerminateThread, TerminateDeleteThread, Suspend/ResumeThread (a suspended thread isn't scheduled whatever its
  state), ChangeCurrentThreadAttr, GetThreadStackFreeSize (new stacks are filled with 0xff and the thread's ID written
  at their bottom, as on a PSP; the 0xff bytes are counted up from 16 bytes above the bottom, which gives
  threads/threads/stackfree's 0xea0 and 0xaa0, counted in place rather than in a copy of the whole stack, which a
  damaged state's 4 GiB stack had made 4 GiB), ReferSemaStatus, the profilers (a retail PSP has none),
  SysClock2USec(Wide), sceKernelLibcClock, sceRtcGetTick and CompareTick, the Mersenne Twister in the program's
  memory, sceKernelPrintf (a field never wider than its room, 63 characters for a number and 63 past a string's
  text, the width cut to that before snprintf sees it, which pads a field in full before cutting it: two fields
  400,000,000 wide took 129 ms), sceDmacMemcpy, the WLAN switch (off), sceImposeSetLanguageMode, and
  sceKernelStopUnloadSelfModuleWithStatus, which ends the program when the program calls it (a C++ abort ends
  there); called from a module, it unloads that module, as part 19's sceKernelSelfStopUnloadModule does.
- **Save states** carry all of it; the state fields test has every new field, and refuses what no machine could
  hold (a callback, a waiter or a pool ID not handed out yet; a buffer in a slot with the DMA stopped, more left of
  it than it holds, the next block over a block away; three buffers on the SRC channel, a rate it doesn't take, one
  retiring after its own time; a thread waiting on a channel that nothing will wake; a handler on a sub-interrupt
  the display driver holds; and pools that don't hold together: a fixed pool's blocks of 0 bytes, which giving one
  back divided by, or not adding up to its size, or with pieces; a variable pool with a block size or blocks; a pool
  outside its block, or whose block isn't there; pieces outside their pool, empty or overlapping). Since the final
  review, memory as the kernel hands it out too: every block inside the user partition; each thread's stack a block
  of its own, of its size (0x200 bytes at least); and every block one owner at most among threads' stacks, pools,
  modules and the program (whose block is the one `start()` gives it, at its first segment's 256-byte step, and must
  be there), so two pools on one block, or a pool inside a stack or the program's block, are refused.
  `sceKernelFreePartitionMemory` refuses a block the kernel holds for one of those (ILLEGAL_PERMISSION), as freeing
  it would leave its owner in memory handed out again, and the machine's own state then unloadable. The kernel's
  layout changed (threads, semaphores, callbacks, sound), so states became version 2 on this branch: one of version
  1 is refused by its header before anything is touched. They're version 3 since parts 18 and 19 merged, as their
  branch had made a version 2 of its own, and version 4 since the final review (each call into the program says
  whether it's a vertical blank's handler).

What the games do now, on the host (the frames are the runner's PNGs, kept outside the repository):

- **Lumines**: the Bandai logo, then its title screen ("PRESS START BUTTON", writing its save,
  `PSP/SAVEDATA/ULUS10002LUMINES`); with Start and Cross pressed it reaches its menu. Still asking for: sceSasCore
  (its music and effects; __sceSasInit fails and it carries on), sceKernelLoadModule for its own kernel modules
  (network and sound, from `USRDIR/kmodule`; it prints "False" and carries on).
- **Street Fighter III 3rd Strike (port)**: the CAPCOM logo, its intro art, and its title screen ("PRESS START
  BUTTON") at 60 frames a second, four sound threads paced by their channels; no function missing in 1200 frames.
- **Space Invaders Extreme**: its heap allocated, the utility modules loaded, its channels reserved; __sceSasInit
  fails, and it then retries sceKernelLoadModule (a module of its own on the disc) in a loop for good: the module
  manager's, part 19's, merged since (the game hasn't been run with it).
- **Brave Story**: its graphics library starts ("SGX system initialize"), then __sceSasInit fails ("failed to
  initialize SAS"), a load from address 8 follows (a pointer that sound would have set), and it asks for
  sceIoReadAsync; the screen stays black.
- **GTA: Sindacco Chronicles**: past its power callbacks, it retries sceKernelLoadModuleByID every half second for
  `USRDIR/PRX2_6_0/KMOD/AUDIOCODEC.PRX`, one of Sony's library modules (encrypted), which the module manager must
  recognize as provided by the HLE (part 19's stands in for Sony's modules; not run with this game yet); also
  sceIoChangeAsyncPriority and the asynchronous file functions after it.

Next: sceSasCore (a silent one first, voices ending as they're keyed, then the real synthesizer), the asynchronous
file functions (sceIoReadAsync, WaitAsync, PollAsync, ChangeAsyncPriority...), message pipes and mailboxes (the
port and Brave Story import them), and playing the sound. (The module manager's loading, with decryption, is parts
18 and 19, merged since.)

Tests (`tests/psp/run-tests.sh`, 136 groups; `tests/psp/ares` 199 checks): `kernel.cpp` (aligned blocks, the
user partition from a PBP's and a disc's PARAM.SFO, SDK versions, the self-unload), and new files, programs run on
both engines: `callbacks.cpp` (callbacks in waits, waits going on after them, seven kinds of CB wait that end at once
running the callbacks notified first, by priority, called directly, the vertical blank's timing, a vertical blank
handler held off and released, ten seconds of blanks held off delivered once, every interrupt and sub-interrupt
number intr/registersub and intr/releasesub tried, null handlers, enabling), `power.cpp` (power callbacks, clocks,
volatile memory, thread control, stack fill, priorities as threads/threads/change has them, the free stack sizes of
threads/threads/stackfree, clocks and dates against known values: MT19937's 10000th number, ticks of known dates),
`audio.cpp` (channels, the blocking outputs returning after 0, 15, 31 and 47 blocks, the SRC channel's queue, 100
blocking 64-sample outputs taking 98 blocks, the mixer's timeline (a second channel not read early, the extra block,
a channel playing out after its release), draining, waits refused in a handler and with interrupts held off),
`utility.cpp` (a save made, loaded, sized, listed, erased and deleted; names reaching out of the save's folder or the
stick, among them each the review found, and links to elsewhere, refused with nothing touched, in a stick folder
inside the test's own so any escape shows; buffers outside the program's memory; links that loop, in the sizes and
list modes; dialogs; modules), `pools.cpp` (fixed and variable pools, waits, timeouts, deletion, a block size of 0
given a block back). `states.cpp` refuses 13 kinds of pool that don't hold together, and `tests/psp/ares` a version
1 state. A broken version that never resumed a wait after its callbacks failed 18 checks of `callbacks.cpp`. The
review's fixes each failed their tests first: the savedata groups (a load read the file beside the stick and
/etc/hosts, a save wrote over homebrew, deletes took PSP/ and the stick), the waits ending at once (no callback ran),
a free from a pool with blocks of 0 bytes (the undefined-behavior sanitizer's division by zero), the 13 pool states
(loaded), the version 1 state (loaded); and an unpaced mixer and a queued call per held-off blank failed the pacing
and delivery tests.

Uncertain: the vertical blank's exact length (0.77 ms from a measurement of a wait's end), the hcount's origin;
whether the PSP notifies a power callback as it's registered (PPSSPP's reading); the VPL's placement; the dialogs'
timings; the CPU clock not slowing at 222 MHz; the errors taken from PPSSPP's tables where pspsdk has none; which
group of errors the savedata modes after 11 report from (the bad parameters follow the groups their other errors
already came from); and whether a CB wait that ends at once runs its callbacks before or after taking what it waited
for (here, after). For sound and interrupts: the 64-sample pacing (K outputs taking K - 2 blocks) follows from the
measured block model, but only its first two outputs were timed on a PSP (intr/waits); which of a mixer output's
volume and channel checks comes first, and an SRC output's volume and reservation; a null SRC output on two armed
buffers (BUSY here; intr/waits' later results show only that it doesn't wait for them); the volumes after reserving
(0) and the volume scale (0x8000 full, pspsdk's PSP_AUDIO_VOLUME_MAX) for when the samples are mixed; how long a null
buffer's count stays; the interrupt kinds, recorded once under PSPLink; and which of a bad priority and a bad thread
sceKernelChangeThreadPriority checks first. Other waits (sceKernelDelayThread and the rest) don't refuse yet with
interrupts held off, as intr/waits shows a PSP does: only sound's do. Since the final review: the SRC channel's
100 µs (the specification's "about 100 µs", from a result bucketed as "13XX"), and whether the buffers chained after
the first keep it (here they do, so a stream's pace is a buffer's length); and what sceKernelFreePartitionMemory
says of a block the kernel holds (ILLEGAL_PERMISSION here, not tried on a PSP).

Merged with parts 18 and 19 (`cursor/psp-decrypt-2b67`, #144), which now sit under this part. Each branch had made
the state's layout version 2, its own way, so the merged layout is version 3, and a state of version 1 or 2 is
refused by its header. sceKernelStopUnloadSelfModuleWithStatus calls part 19's `Kernel::unloadSelf()` with its exit
status, argument and options (its fifth argument; the fourth, where module_stop's status would go, isn't written, as
nothing waits for it): a module calling it (Gunhound EX does) stops and unloads itself and the program runs on; the
program calling it still ends. sceKernelTerminateThread deletes a thread running a module's module_start or
module_stop, as sceKernelExitThread does since part 19 (TerminateDeleteThread deleted it already), so neither it nor
its stack is left behind. Part 19's thread deletion became this part's `deleteThread()`, so a module's thread takes
its callbacks with it as any thread does; a callback returns to the trampoline's fourth syscall, as part 19 took the
third for module_start and module_stop; and a state with a module whose block is a memory pool's is refused. Tests:
"modules unload with a status" (both engines; the options' stack size and priority reach module_stop's thread, and
the program calling it leaves) and "modules terminated threads" (both functions, both engines) in
`tests/psp/modules.cpp`, the pool's block in "state fields", and a version 2 state in `tests/psp/ares`. The merged
tree passes its 158 groups and 228 checks; with the old sceKernelStopUnloadSelfModuleWithStatus, its options taken
from the fourth argument, sceKernelTerminateThread not deleting, or no check of the pool's block, the new tests fail.

Review (the final one): a general-purpose reviewer of the merged branch found one medium and five low findings, all
fixed, each with a test that failed before its fix; the merge, the clean-room code and the savedata fixes checked
out. The medium: states took each thread's stack size and address unchecked, and sceKernelGetThreadStackFreeSize
copied the whole stack out of guest memory at each call (a state with a 0xfffff000-byte stack loaded, and the call
would have allocated 4 GiB): the stack is read in place now, and loading checks stacks and blocks (save states,
above). The lows: a pool's or semaphore's waiter leaving unserved left those behind it blocked (memory pools); a
pending vertical blank queued its handlers again while the last blank's still waited (interrupt handlers); a pool's
block was checked against nothing but the pool, so two pools on one block, or a pool inside a stack or the
program's block, loaded (save states); the SRC channel's buffers retired at their full length, where a PSP's slot
frees about 100 µs earlier (sound output); and sceKernelPrintf handed any width to snprintf, which padded a field
2e9 characters wide in full before cutting it (threads and clocks). Tests: "kernel semaphores served past waiters
that left", "pools served past waiters that left", "interrupts handlers longer than a frame" (two 20 ms handlers
over 120 frames: two calls queued at most, where the old rule reached 100) and "audio src rest" (audio/output2/rest's
timing, 13XX µs on both families) are new; "state fields" refuses 10 more states (thread stacks that aren't blocks
of their own and their size, a 4 GiB stack with its block, a block below the partition, two owners for a block, the
program's block missing) and changes the calls' new flag; "kernel thread status" (a 4 GiB stack answered in under
100 ms), "kernel partitions" and "kernel program memory" (held blocks not freed, the state still loading), "kernel
odds and ends" (ten fields 2e9 wide printed at once, each cut to its room), "audio src channel" and "audio draining"
(the earlier retires) check the rest; `tests/psp/ares` refuses a version 3 state. The tree passes 162 groups and 232
checks. Found on the way, not changed: a program spinning on the clock (sceKernelGetSystemTime and its siblings)
sees it move only between the CPU's runs, each to the next thing due, not with each instruction, so the rest test's
loop waits a microsecond a round where the PSP's spun.

On the RP6 (the whole stack, launched from the user's CHDs): all ten of the user's priority games now start (nine
tried with build 104648, decrypting their programs; Lumines with build 104647). SOCOM Fireteam Bravo reaches its own
"No SOCOM Fireteam Bravo Data was found on the Memory Stick Duo" screen at 60 frames a second (then asks for
sceAtrac3plus); Burnout Legends reaches its LOADING screen (asks for sceIoChangeAsyncPriority, sceIoPollAsync,
sceIoReadAsync); Lumines runs to its log-in menus at about 40 frames a second (asks for sceSasCore); GTA Vice City
Stories and Liberty City Stories, and Midnight Club 3, ask for sceMpeg (video) and the asynchronous file functions;
Peace Walker asks for sceRtc e7c27d1b, sceOpenPSID, sceDisplay 210eab3a and message pipes (ThreadManForUser 7c0dc2a0
and 74829b76); Burnout Dominator and Snoopy vs. the Red Baron ask for the asynchronous file functions and ad-hoc
networking (sceNet*); Gunhound EX asks for sceLibFont and scePower 469989ad. Next: the asynchronous file functions, a
silent sceSas, message pipes, sceMpeg stubs that let games skip their videos, and networking reported off.

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
- `tests/psp/crypto.cpp`, five groups: "crypto aes" (FIPS-197's examples, NIST SP 800-38A's ECB and CBC ones, each
  way; a thousand chained blocks, whose last OpenSSL gave; a partial last block left alone); "crypto sha-1" (FIPS
  180-1's examples, a NID, command 0xb and its refusals); "crypto kirk static" (data encrypted as command 4 does comes
  back under every keyseed not made per console, in place too; per-console and missing keyseeds, other modes, no
  data, data not in whole blocks, inputs and outputs too small: refused); "crypto kirk private" (data encrypted as
  Sony's tools do, in whole blocks or not, with and without padding, CMAC or ECDSA headers, in place too; another
  command, no data, too small an output, an input cut short, huge sizes and paddings: refused); "crypto keys" (the
  keys' SHA-1 digests, pinned: KIRK command 1's key, commands 4 and 7's 128 as one run and the eleven the tags use
  one at a time, and each tag's key, a 144-byte one as its words' bytes, with its type and keyseed, and type 5's XOR
  key. They were worked out from `keys.cpp` once a reviewer had checked its bytes against the wiki's "Keys" page:
  the round trips encrypt with the very tables they test, so a key typed wrong would still pass them).
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
  first key of a tag listed twice tried. A byte changed in a tag's key and in a keyseed's key failed only "crypto
  keys": every round trip still passed.

## Part 19: modules

`ares/psp/kernel/modules.cpp` (ModuleMgrForUser): the modules (PRXs) a program loads besides itself, from the disc
or the memory stick. The functions, their arguments and errors are pspsdk's (`pspmodulemgr.h`, `pspkerror.h`);
PPSSPP's module manager was read for how games use them (Sony's modules loaded beside a game, a "~SCE" header before
a module); our own code.

- **Loading** (`sceKernelLoadModule(path, flags, options)`): the file is read whole, from the disc by its path or as
  a run of sectors (`sce_lbn...`, as games name modules too), or from a host folder standing for a device.
  `sceKernelLoadModuleByID(file, flags, options)` reads one from a file already open, from where it's been seeked
  to, as games keep modules in archives of their own: an encrypted one is as long as its ~PSP header says (one said
  to be shorter than that header, or longer than 64 MiB, the PSP's memory, is refused as an illegal object, the file
  left where it was), a plain one runs to the file's end (16 MiB at most). Reading an open file asks the host for no
  more than the file has left, and 64 MiB at most, so a size a damaged header claims costs nothing (a 4 KiB file
  whose header claimed 0xfffffff0 bytes had 4 GiB asked for, which on Android would end the app). An encrypted
  module is decrypted (part 18). A PRX goes into a block of the user partition, the lowest free place, a static module
  where it was linked; the loader relocates it there and reads its imports and exports. Its ID comes back.
- **Sony's modules are stood in for**, not run. Early games carried Sony's libraries for sound, video and the network
  (sceSAScore, sceATRAC3plus_Library, sceMpeg_library, sceNet_Library, and kernel drivers such as
  sceAudiocodec_Driver), which run on top of Sony's kernel and its hardware; the HLE kernel answers their functions
  itself. They're told by their names, "sce" or "Sce" first as all Sony's are, or by being kernel modules (attribute
  0x1000), which a game's own never are; an encrypted one's name is in its ~PSP header, in the clear, so it needn't
  even be decrypted. A stand-in has an ID and a name, nothing in memory, and starts and stops at once.
- **Linking**: after each load and unload, every module's imports (the program's too) are linked to the functions
  the program and loaded modules export in their libraries (a game's module may import from its EBOOT.BIN). An
  import the HLE kernel has no function for, which one of them exports, becomes `j address; nop` in place of the
  kernel's syscall (the caller's `jal` left `ra` pointing back at it, so the function returns straight there); every
  other import is the kernel's syscall, so a stub linked to a module since unloaded goes back to the kernel. A stub
  already right isn't written again, as that would throw away the code compiled around it. Variables imported from
  other modules aren't linked yet.
- **Starting and stopping** (`sceKernelStartModule` and `sceKernelStopModule(module, argument size, argument, where
  to put the result, options)`): the module's `module_start` (or `module_stop`), exported for itself (NIDs
  0xd632acdb and 0xcee8593c); a module that exports no `module_start` starts at its ELF header's entry point, where
  its code begins, when that's in the module (an entry of 0, or outside it, means there's nothing to run). It runs on
  a thread made for it, its argument copied onto the thread's stack. The thread has the program's first thread's
  priority, stack size and attributes (0x20, 256 KiB, user mode with the VFPU) unless the module's own thread
  parameters give others, or the caller's options do (SceKernelSMOption's stack size, priority and attributes), a 0
  in either leaving the value as it was. The parameters are variables a module exports for itself,
  `module_start_thread_parameter` and `module_stop_thread_parameter` (NIDs 0x0f7c276c and 0xcf0cc697): how many
  values follow (Sony's SDK writes 3), then the priority, the stack size and the attributes, as pspsdk's SceModule
  keeps them for each function's thread and uOFW's SceModuleEntryThread lays them out. The function returns to a
  third syscall in the kernel's trampoline, which ends and deletes its thread; one that ends with
  `sceKernelExitThread` instead, or that another thread terminates (part 17's `sceKernelTerminateThread` and
  `sceKernelTerminateDeleteThread`), is deleted all the same, as the PSP's module manager deletes the thread it made
  however it ends. The calling thread waits meanwhile, and then gets the module's ID, the function's result (or its
  exit status) written where it asked. A module without the function starts (or stops) at once.
- **Unloading** (`sceKernelUnloadModule`): a module never started, or stopped; its memory goes back to the user
  partition, and stubs linked to it go back to the kernel.
- **Unloading itself** (`sceKernelSelfStopUnloadModule(exit status, argument size, argument)`, through
  `Kernel::unloadSelf()`, which later SDKs' `sceKernelStopUnloadSelfModuleWithStatus` (part 17; its options go to
  `module_stop`'s thread) calls too): the module is the one holding the code that called (`ra` points back
  into it). The calling thread ends with the exit status (threads waiting for its end are
  given it) and is deleted, as the code it would return to is going. If the module is running, its `module_stop`
  then runs with the argument on a thread of its own, as `sceKernelStopModule` would run it, and the module goes once
  that ends (its status says it's unloading meanwhile); one that isn't running, or has no `module_stop`, goes at
  once. Called from its `module_start`, the thread that started the module gets its ID, the exit status as the
  result. Nothing waits for `module_stop`'s result: the thread that asked is gone. Refused while another thread runs
  the module's `module_start` or `module_stop` (not stopped), and from a call into the program, which runs on top of
  whichever thread was running. The program itself, or code no module holds, unloading itself is the program
  leaving, as it was for every caller before (Splinter Cell Essentials, by PPSSPP's notes, unloads a module of its
  own this way as play starts and ends, which ended the game).
- **IDs**: the program is a module too, its ID handed out once it's loaded. `sceKernelGetModuleIdByAddress` gives the
  module holding an address, in any of memory's windows; `sceKernelGetModuleId` the caller's (the module holding the
  code that called, else the program's); `sceKernelGetModuleIdList` all of them, the program's first.
  `sceKernelQueryModuleInfo` fills a SceKernelModuleInfo as far as its size says: segments, entry, gp, attributes,
  version, name. The loader keeps each segment's size in memory but not its file and zeroed parts, so the text is the
  first segment, the data the others, and the bss 0.
- **Refused**, with pspkerror.h's errors: a file that isn't there, or a folder (the file system's errors); what isn't
  a module, or a ~PSP header's size no module has (illegal object); an encrypted module that can't be decrypted
  (unsupported PRX type, and the decrypter's reason noted); no room (no memory); an unknown ID; starting a module
  twice; stopping one not started, or stopped already; unloading one that's running.
- **States** carry the loaded modules (each one's ID, file, whether it's a stand-in, its memory block, its status,
  the thread running its function, and its module as the program's is saved) and the program's ID. The state's
  version went to 2 with them, and is 3 since part 17's branch, which had a version 2 of its own, merged them: a
  state made before is refused rather than misread. Loading checks each module as part 15 checks the rest: under its
  own ID, not the program's; a thread exactly while its `module_start` or `module_stop` runs, and one that's there;
  no block for a stand-in, and for another module a block of the user partition, or none, but never a thread's
  stack, a memory pool's block (part 17's pools, since the merge) or another module's block (since part 17's final
  review, one rule for every block: one owner at most, the program's block among them).

Checked against the user's games (the system run on the Mac on their CHDs, nothing kept): Burnout Legends loads
fourteen modules from its disc by path (seven kernel drivers, encrypted, and seven libraries, not), each Sony's and
stood in for, and runs on further than before (its GE ran twice the commands in its first 300 frames); GTA
Liberty City Stories loads three by `sceKernelLoadModuleByID` from runs of sectors, each behind a ~SCE header
(sceAudiocodec_Driver, sceATRAC3plus_Library, sceSAScore), stood in for; Gunhound EX finds its module's ID by address
and as the caller's. Each game then stops at other functions the HLE kernel didn't have yet (fixed-size memory
pools, the power library, the SDK version calls: part 17's, merged since).

Tests (`tests/psp/modules.cpp`, ten groups, on PRXs built in the test: TESTLIB exports a library's function and has a
module_start and a module_stop; TESTUSER imports the function):
- "modules start and link": a program on both engines loads both from the memory stick, the second encrypted;
  starts the first (its result comes back), then the second with an argument (it sees the argument; its import
  reaches the first's function); stops the first and unloads it: the second's stub goes back to the kernel, the
  first's memory to the user partition, and each function's thread is gone once it returned.
- "modules linking": a module loaded before what it imports is linked once that comes; loaded from the disc by path
  and as a run of sectors; a module importing from a library the program exports is linked to it.
- "modules stand-ins": Sony's modules, one encrypted under a tag Phobos has no key for, one a kernel module, one plain
  and named "Sce...": IDs and nothing in memory, started and stopped at once; `sceKernelLoadModuleByID` from inside
  an archive, behind ~SCE headers: a stand-in, and a module loaded for real.
- "modules identities": by address (in another window too), the caller's, the list, the information (only as far as
  its size says).
- "modules refusals": each refusal above; a module that can't fit leaves nothing behind.
- "modules sizes": `sceKernelLoadModuleByID` on a 4 KiB file whose ~PSP header claims 0x14f bytes, 64 MiB and one,
  or 0xfffffff0: refused, nothing left behind; reading an open file, a 4 KiB file asked for 16 MiB from 96 bytes in
  gives its 4000 bytes, with room made for no more, and a file of 64 MiB and 4 KiB (with nothing written in it, so it
  takes no room on the host) gives 64 MiB.
- "modules unload themselves" (TESTSELF's quit() calls `sceKernelSelfStopUnloadModule(1, 4, argument)`): a thread
  calling it is deleted and the thread waiting for its end gets 1; module_stop runs with the argument; the module and
  its memory are gone and the rest runs on, on both engines. Its module_start being quit() itself: the thread that
  started it gets its ID and 1. Refused while its module_start sleeps on another thread. The program, or code in no
  module, calling it leaves.
- "modules exit threads": module_start and module_stop ending with `sceKernelExitThread(5)` and `(6)`: the caller
  gets 5 and 6 as their results, and both threads and their stacks are gone, on both engines.
- "modules entry points": a module without a module_start runs its ELF entry point when that's in it, and starts at
  once when it isn't; a module's thread parameters (three for module_start, two for module_stop) make its functions'
  threads, the options going over them but for their zeros.
- "modules state": a state saved while module_start waits in a delay (its caller waiting for it) loads into a fresh
  machine, which saves the very same state, and both machines carry on alike to the program's end: module_start's
  result reaches its caller, its thread is gone. On both engines.
- "state fields" changes each new field and refuses a module under another's ID, IDs not handed out yet (the
  module's, its block's, its thread's, the program's) and a status there isn't; and (its module now in a block of
  its own, its module_start on a thread, one of Sony's beside it) a module under the program's ID, with a thread
  while none of its functions runs, without one while one does, with a thread that isn't there, a block that isn't
  one, a thread's stack as its block, a memory pool's block as its block (since the merge with part 17), a block two
  modules have, and a stand-in with a block. "decrypt kernel loads"
  loads a program behind a ~SCE header.
- Broken versions each failed a test: imports never linked to exports, module_start's result written wrong, an
  unloaded module's stubs left linked, Sony's modules loaded like any other, module_start's thread kept, a loaded
  module's own fields left out of states, and ~SCE headers not passed over. The review's fixes below were each tested
  first: before them, the new checks failed 52 times over in "modules linking", "modules sizes", "modules unload
  themselves", "modules exit threads", "modules entry points" and "state fields"; with them, all pass.

Review: a general-purpose reviewer found parts 18 and 19's code an independent implementation in a clean-room audit,
and two medium and five low findings, all fixed (from pspsdk's and uOFW's headers and the reviewer's notes, without
PPSSPP's code): `sceKernelLoadModuleByID` took the module's size from an unchecked ~PSP header and made room for it
before reading (bounded now, with every read of an open file); a module unloading itself ended the game (now it
goes, and the program runs on); the program's exports weren't offered to modules' imports; a module without a
module_start never ran its entry point, and modules' own thread parameters were ignored; a module_start or
module_stop ending in `sceKernelExitThread` kept its thread and stack for good; states' modules were checked no
further than their IDs; and the decrypter's round-trip tests shared its key tables, so the keys' digests are now
pinned (part 18).

## Part 20: more functions games ask for

On branch `cursor/psp-hle-games2-2b67`, on top of part 17's (`cursor/psp-hle-games-2b67`, #145, itself on parts 18
and 19's #144). The RP6 run of the whole stack had each of the owner's games stop at a function the kernel didn't
have; this part gives them those, found game by game with a scratch host runner (never committed: the system as
`tests/psp/ares` builds it, booting the owner's CHDs, tracing system calls, dumping frames and memory). What each
function does comes from pspsdk's headers (names, arguments, structures, pspkerror.h's errors), pspautotests' test
programs and the results they recorded on a PSP, and the games' own behavior; the PSP Developer Wiki has nothing on
these libraries. No other emulator's code was read. Where a behavior isn't known, the code says what was chosen and
why (what games accept).

- **Asynchronous file requests** (`async.cpp`): sceIoOpenAsync, CloseAsync, ReadAsync, WriteAsync, LseekAsync,
  Lseek32Async and IoctlAsync, then sceIoPollAsync, WaitAsync, WaitAsyncCB and GetAsyncStat for the result, plus
  ChangeAsyncPriority and SetAsyncCallback. A file takes one request at a time. The request is done as it's made (the
  bytes are in the program's memory at once), but its result (64 bits: what the synchronous call returns, an error
  sign-extended) is held back for the time the device takes: 100 microseconds plus the bytes at the UMD drive's top
  rate, 11 Mbit/s (1,375,000 bytes a second), for the disc, or 4 MB a second for the memory stick (or a host folder
  standing for the disc); the drive's seeks aren't counted; an ioctl takes the time of what it put in its output.
  Meanwhile a poll answers 1 and a wait blocks the thread (the kernel's loop treats a request coming due as an event,
  like a vertical blank); then the result waits on the file for the program to take, the threads waiting ending
  their waits at once (the first to begin waiting takes it, any others finding none), and the file's callback is
  notified (with a CB wait, it runs before the wait ends: should it take the result itself, polling, the wait finds
  none). Every other call on a file with a request under way is refused (ASYNC_BUSY), as is a second request; a
  poll or wait with nothing to take gets NOASYNC (both pspkerror.h's; intr/waits recorded NOASYNC for a wait on a
  request whose result had been taken, so a wait that ends finding none is told that too). An
  asynchronous open gives its descriptor at once, which is also its result; one that fails leaves a descriptor holding
  only the error, gone once that's taken, as a file closed asynchronously is. pspautotests has no test of these
  functions, so all of that is chosen as games accept it: Burnout Legends polls before its first read (NOASYNC); the
  priority (0x08-0x77, -1 for files to come: Burnout passes 0x65, GTA 0x40, SOCOM 0x13) is checked but not used.
- **sceSasCore, silent** (`sas.cpp`): every function. Its voices (VAG ADPCM blocks, 16-bit PCM, noise, waves,
  ATRAC3) keep their parameters, their ADSR envelope's height and where they are in their samples, and end as they
  would: a VAG voice at a block marked as its end (flags 1, 7, 0x41, 0x87 in audio/sascore/vag; 3 loops when the
  voice loops, 4 marks where to) or at its data's last byte, read block by block as it gets there (so data a game
  streams ahead counts); a PCM voice at its last sample unless it loops; any voice when its release reaches 0. The
  end flags are refreshed by __sceSasCore alone; its buffer gets silence, __sceSasCoreWithMix's is left as it was.
  What each call takes and refuses is audio/sascore's recordings: __sceSasInit's checks in order (the core 64-byte
  aligned, a grain of 64-2048 in 64s, 1-32 voices, output mode 0 or 1, 44100 Hz), volumes of -0x1000 to 0x1000,
  pitches to 0x4000, VAG sizes in 16s (negative ones taken), PCM counts of 1-65536, each envelope phase's curves,
  keying on and off (INVALID_STATE on, twice, off unkeyed, either paused). The envelope moves a sample at a time,
  a voice keyed on starting 32 samples into the next grain (keyon: 0x60000 after one grain of 128 at 0x1000); the
  linear curves, exponent rev (height * rate / 2^32 a sample, rounded up) and linear bent (the rate up to three
  quarters of the top, inclusive, a quarter above) give adsrcurve's figures exactly; exponent (attack and sustain
  only) is an approximation. __sceSasSetSimpleADSR's SPU words become rates by formulas worked out from setadsr's
  table. A playing voice given new samples of its kind goes on from where it is, a PCM voice brought inside them
  (past their end, to where its loop would have taken it, or else to its last sample, ending as the next grain moves
  it on); given samples of another kind, it starts from their beginning. Lumines, Brave Story and Space Invaders
  Extreme had stopped or failed their sound at __sceSasInit.
- **Message pipes and mailboxes** (`messages.cpp`): sceKernelCreateMsgPipe, Delete, Send, SendCB, TrySend, Receive,
  ReceiveCB, TryReceive, Cancel and ReferMsgPipeStatus; CreateMbx, DeleteMbx, SendMbx, ReceiveMbx, ReceiveMbxCB,
  PollMbx, CancelReceiveMbx and ReferMbxStatus. A pipe's buffer (any size, none too) is a ring taken from the user
  partition; a send gives waiting receivers their bytes straight across, then fills the buffer; a receive empties the
  buffer, then takes from waiting senders straight across, the buffer taking in what the next senders hold. Each
  waits for its whole message (mode 0) or anything (1), in line: a sender whose message doesn't fit holds up a later
  small one, and whatever a thread moved is written where it asked however its wait ends (done, timed out,
  cancelled, deleted). As threads/msgpipe's create, send, receive, trysend, tryreceive and cancel recorded, down to
  a receiver given half its message by one send timing out with that half. A mailbox queues packets (in order, or by
  their priority byte with attribute 0x400; threads by priority with 0x100), keeping their first words written as the
  PSP's ring (threads/mbx: a packet alone points at itself); the queue itself is the kernel's, so a program writing
  over those words loses nothing (on a PSP it can); a packet sent while queued is refused (0x800201c9, as mbx/send
  recorded). Not shown by the tests, chosen: which of a bad mode and a bad size comes first; a message or buffer
  without memory behind all of it refused (ILLEGAL_ADDR), after the checks recorded (msgpipe/send leaves its null
  message with a length out), so bytes go straight across from memory to memory, one move each, with no copy of their
  own; pipe attributes 0x100 and 0x1000 lining up receivers and senders by priority, 0x4000 taking the buffer from
  the partition's top. A send or receive with callbacks that needn't wait runs the caller's callbacks as it returns,
  once the caller has the CPU again, not those of a thread it woke. Peace Walker's file system ("kfs io0" and the
  rest) runs on them, with the asynchronous requests.
- **Movies** (`mpeg.cpp`): sceMpeg set up with the sizes video/mpeg recorded (a ringbuffer 0x868 bytes a packet,
  the library 0x10000; sceMpegRingbufferConstruct filled in as ringbuffer/construct shows, refusing over 4096 packets
  or a negative size; sceMpegCreate's "LIBMPEG"), then sceMpegQueryStreamOffset can't read any movie's header
  (0x80610022, construct's error for a value refused) and every stream is empty (sceMpegGetAvcAu: 0x80618001,
  basic's "no data"; sceMpegAtracDecode 0x807f00fd). Games skip their movies either way: GTA gives the movie up at
  the header, Burnout Legends and Dominator play on and find it ended at once. With every sceMpeg call missing,
  Burnout had taken a ringbuffer size of 0x8002013a and crashed writing through a null pointer.
- **ATRAC3plus** (`atrac.cpp`): sceAtrac3plus's six IDs handed out and back, but every stream refused as one the
  library can't read (0x80630006, audio/atrac/setdata's for zeroed data), the rest refusing their ID (0x80630005,
  setdata's for an ID not handed out); none free: 0x80630007. Games go without music: SOCOM prints "snd_stream:
  couldn't get ATRACID" and carries on.
- **The network** (`net.cpp`): sceNet, sceNetAdhoc, sceNetAdhocctl, sceNetAdhocMatching, sceNetInet,
  sceNetResolver and sceNetApctl with the wireless LAN switched off (sceWlanGetSwitchState says so already): the
  libraries start and stop (their init and term succeed, as setting up the stack needs no radio), and whatever would
  reach another PSP or an access point fails with NOT_SUPPORTED (uOFW's errors.h; what a PSP returns isn't known
  here); lists come back empty, the state disconnected. Snoopy vs. the Red Baron left at once when its starts failed;
  it and Burnout Dominator start them at boot.
- **Fonts** (`font.cpp`): sceLibFont with no fonts installed, until the owner's flash0 provides the PGF files: the
  library starts, lists none, finds and opens none (NOT_FOUND written where an error's address is given), a font's
  details refused; points and pixels convert at 128 dots an inch, or sceFontSetResolution's (above 0 and below 10^9;
  anything else, not-a-number too, refused with uOFW's INVALID_VALUE, pspfont.h giving the library no errors). Peace
  Walker and Gunhound EX ask for fonts and draw their text without them. (Part 23 reads the owner's fonts; this
  stays the library for a PSP without them.)
- **Odds and ends**: sceRtcGetCurrentClock (a time zone), GetCurrentClockLocalTime (the host's), GetTime_t, and
  GetDosTime/SetDosTime (rtc/convert's figures: 2107-09-11 24:00 is 4281057280; before 1980 or after 2107, -1);
  sceOpenPSIDGetOpenPSID (one made-up console: "PHOBOS"); sceDisplayGetAccumulatedHcount (286 lines a frame) and
  GetFramePerSec (59.94); sceKernelIsCpuIntrEnable; Kernel_Library's sceKernelMemset and sceKernelMemcpy (as memmove);
  scePower's 0x469989ad and 0xebd177d6, later SDKs' scePowerSetClockFrequency (Gunhound EX calls the first with
  333, 333, 166, Peace Walker the second); sceKernelGetGPI (no debug switches); sceAtracReinit;
  sceUtilityLoadAvModule and UnloadAvModule (the modules 0x300-0x307); sceKernelGetThreadCurrentPriority;
  sceKernelRotateThreadReadyQueue (0 or 0x08-0x77, threads/threads/rotate's refusals); sceKernelSuspendDispatchThread
  and ResumeDispatchThread (the running thread keeps the CPU, rotating its own line too; what they return is chosen:
  1, then 0, the pair working whichever way the state is read); sceKernelLockLwMutexCB. With dispatching held off, a
  function that waits is refused (CAN_NOT_WAIT) before it changes anything, whether it would have had to wait or not,
  as pspautotests' intr/waits recorded (a free lightweight mutex, an event flag's bits set already, a thread that has
  ended, the drive ready); only the arguments that test shows checked ahead of that come first (a pipe's negative
  size, an event flag's mode, a controller read's count, a file and its request, the drive's state bits). Sound's
  blocking outputs are refused as with interrupts held off (a mixer output only when it would have to wait).
- **Two fixes Peace Walker found.** Its power callback, notified as it's registered (power's recorded run shows the
  PSP does that), ran inside sceKernelLockLwMutexCB on a free mutex while the game held the lock the callback takes,
  and deadlocked it. The lightweight mutexes are Kernel_Library's, a user-mode library whose free lock never enters
  the kernel: such a lock no longer runs callbacks (the CB functions that do enter it still run the callbacks
  notified, waiting or not, as part 17 has them). Then its C library read a pointer for the thread at k0 + 4 from the
  0xff a new stack is filled with, and crashed: a thread's top 256 bytes, the kernel's, where k0 points, now start
  zeroed (the free stack counted from the bottom is unchanged).
- **States** carry all of it (files' requests, sceSas, pipes and their buffers' blocks, mailboxes, threads'
  transfers, the ATRAC IDs, dispatch held off, the font resolution), each checked on loading: a request on a folder
  or due later than any can take (64 MiB from the disc: under a minute), a descriptor for a result it hasn't got, a
  thread waiting on a file whose request isn't under way (one whose callbacks run ends its wait as it then finds the
  file); sceSas settings and voices its functions never leave (a curve or phase there isn't, a PCM voice past its
  samples, a VAG size not in 16s, a voice on that isn't playing); a pipe's ring past its buffer, its buffer not its
  block or another's (the one-owner rule now counts pipes), a thread waiting on a pipe for more than it holds, or
  with no memory behind the rest of its message; a mailbox's packet queued twice or where there's no memory. A file
  dropped on loading (gone from the host, or no disc) tells a thread waiting on its request it's a bad file. The
  layout is version 5: a state of version 1 to 4 is refused by its header.

What the games do now, on the host (frames from the scratch runner, kept outside the repository, under
`/tmp/hle2-runner/out`):

- **Lumines**: through its title to its gameplay demo ("Use directional buttons to move left or right"), sound
  initialized (its voices silent); nothing missing in 2400 frames.
- **Burnout Legends**: past its loading screen (asynchronous reads) and its movie to "PRESS START BUTTON TO
  CONTINUE"; with Start, its profile screen ("LOAD PROFILE / NEW PROFILE"). Nothing missing.
- **Burnout Dominator**: "Press START button", then its profile screen ("Load Profile / Create Profile").
- **Midnight Club 3**: its title ("PRESS START BUTTON"), then "Load Profile / Create Profile / Delete Save Data".
- **SOCOM Fireteam Bravo**: "No SOCOM Fireteam Bravo Data was found on the Memory Stick Duo"; confirmed, on to its
  credits ("Developed ..."). No music (ATRAC3plus).
- **Snoopy vs. the Red Baron**: its "No Snoopy vs. the Red Baron save file found" warning (RETRY / CONTINUE WITHOUT
  LOADING / CREATE NEW FILE); it had exited at its network starts.
- **Space Invaders Extreme**: its title screen ("PRESS START BUTTON"), for the first time.
- **Metal Gear Solid Peace Walker**: its title screen (the logo and Konami's line; its text, drawn with the system's
  fonts, missing); nothing missing on the way (it had asked for sceKernelGetGPI, sceAtracReinit and scePower
  0xebd177d6).
- **GTA Liberty City Stories, Vice City Stories, Sindacco Chronicles**: past their movies (skipped) to their loading
  screens, the bar at about 85%, where they stay: the main thread waits for a queue of 16-byte entries
  (0x08e6258c-0x08e62590 in LCS: one left) to drain, polling event flag 0x115; what empties it isn't found yet.
- **Gunhound EX**: its Dracue logo, then a black screen, drawing (asking for its fonts' details thousands of times);
  likely text in the system's fonts.
- **Brave Story**: past __sceSasInit and its asynchronous reads (16 KiB at a time, polled with 1-microsecond delays),
  on its Game Republic logo for at least 6000 frames, reading slowly; the next step isn't found yet.
- **Street Fighter III port**: as before (its intro art).

Tests (`tests/psp/run-tests.sh`, 180 groups, both sanitizers; `tests/psp/ares` 236 checks):
- `async.cpp`: requests called directly (a read's bytes and its result held back exactly 100 microseconds plus its
  bytes at 4 MB a second, polls before and after, every other call refused meanwhile, seeks, a read past the end, a
  write refused as its result, an ioctl, a result never taken overwritten, the asynchronous close and failed open
  and their descriptors, the disc's rate, the priority's and callback's checks); a program waiting (1100
  microseconds exactly, a worker running meanwhile, the callback run in the CB wait before its result); a state saved
  while waiting on the disc, and with no disc the waiter told.
- `sas.cpp`: every refusal above from audio/sascore's recordings; envelopes grain by grain (keyon's, keyoff's,
  adsrcurve's attack, bent attack and exponent rev decay, exactly); voices ending in a program on both engines (VAG
  unmarked, VAG looping, PCM at two pitches, PCM looping).
- `messages.cpp`: create's refusals; sends and receives through the ring (wrapping), ASAP's part, the try
  functions'; a program with receivers waiting part way and timing out with their half, senders in line, a pipe
  without a buffer and its deletion; mailboxes in order and by priority, their linked words, waiting threads,
  cancelling, deleting, a timeout; a state with a receiver waiting part way, carried on in another machine.
- `media.cpp`: sceMpeg's sizes, ringbuffer and refusals; ATRAC IDs and refusals; the network off; the date, DOS
  times, the OpenPSID, the line count, the rate, memcpy and memset, the clock setter, the AV modules, the priority
  refusals; in a program, rotating, the current priority, dispatch held off and resumed, the free lightweight mutex's
  CB lock not running callbacks; fonts missing; the kernel's 256 bytes zeroed.
- `states.cpp`'s "state fields" changes every new field and refuses 36 more states (above).
- Broken versions each failed: requests done at once (the async groups and "state fields"), a sender not waiting in
  line and transfers' counts not written ("message pipes waited for", "message pipes state"), sceSas's 32-sample
  start left out ("sas envelopes"), the kernel's 256 bytes left as 0xff ("threads kernel area zeroed").

Review: a general-purpose reviewer of the branch; the clean-room spot check found every file independent; three
medium and six low findings, all fixed (each with a test that failed before its fix) or recorded. The mediums: a
pipe without a buffer copied a direct transfer through a host buffer as big as the counts asked, before any address
check (a 32 MiB machine moved 1 GiB, the host's memory growing by as much): a message or buffer must now have memory
behind all of it, bytes go straight across from memory to memory, and loading wants memory behind the rest of each
pipe waiter's message; a send or receive with callbacks that woke a thread of higher priority ran that thread's
notified callbacks (it was current by then), though it waited without them: the caller's start first now;
__sceSasSetVoicePCM left a voice given fewer samples past them, a state the machine's own loading refused: it's
brought inside them (and __sceSasSetVoice starts a voice that played other samples from the new data's beginning).
The lows: two threads waiting on one request (only the first woke), and a CB wait whose callback took the result by
polling, each left a thread waiting for good and the state refused: every waiter's wait ends now, and loading wants
only a thread actually waiting to find its request under way; sceIoIoctlAsync timed its result by the output's
length (0xffffffff: due in 52 minutes, the state refused), by the bytes it put there now; sceFontSetResolution kept
1e10 and the infinities, which loading refuses, and now refuses them (and NaN) itself; with dispatching held off,
waits were refused only as they'd block, after a lightweight mutex's waiter count had gone up or a pipe's bytes had
moved, and rotating the caller's line switched threads: waits are refused first now, as intr/waits recorded, and
rotating leaves the caller the CPU; a thread back from its callbacks to a lightweight mutex deleted meanwhile waited
for good, and its wait ends deleted now. The sixth, whether pipe attribute 0x100 lines up receivers and 0x1000
senders or the other way round, stays as it was: pspsdk's pspthreadman.h names no pipe attributes (its
sceKernelCreateMsgPipe's attr is "Set to 0?"), so it's among the uncertainties below. Each of this branch's test
groups now ends with its machine saved, loaded into another and saved again, the two states the same, which would
have caught the sceSas, request and font findings. New groups: "message pipes memory", "message pipes callbacks on
return", "async files two waiters", "async files callback takes the result" (a state saved as the callback waits,
carried on in another machine), "async files ioctl timing", "sas voices given new samples", "threads dispatching
held off", "threads lightweight mutex deleted in a callback"; "fonts missing" tries the resolutions refused, and
"state fields" refuses three more states (a pipe waiter with no memory behind its buffer, or behind the rest of its
message as its callbacks run; a thread waiting on a request already done). The tree passes 188 groups and 236
checks, the layout unchanged (version 5).

On the RP6 (build 104649, the whole stack from the user's CHDs): Space Invaders Extreme reaches its title screen at
60 frames a second; Peace Walker reaches its title scene, but drawn garbled (striped) and at 9.5 frames a second
(16%), a GE drawing and speed problem to look into; Burnout Legends, Midnight Club 3, SOCOM and Lumines ran with no
missing functions noted.

Uncertain: asynchronous requests' timing and every choice listed with them; the async priority unused; which of
several threads waiting on one request takes its result (the first to begin waiting), and NOASYNC for the others
and for a wait whose callback took the result (intr/waits recorded it for a wait begun after the result was taken);
sceSas's exponent curve, the reading of VAG flags beyond those recorded, a keyed-on voice with no samples ending at
the next grain, and where a voice given new samples goes on from; pipe attributes (pspsdk names none: 0x100 and
0x1000 may order senders and receivers the other way round), and where a pipe's missing memory is checked; mpeg's
and the network's error numbers for headers and radios, sceLibFont's errors (INVALID_VALUE for a resolution among
them); the dispatch functions' return values, and a rotation of the caller's line with dispatching held off (no
change here); and whether the PSP zeroes the whole of a thread's kernel area (Peace Walker needs k0 + 4 zero).

## Part 21: sound

On branch `cursor/psp-sound-2b67` (#147), on top of part 20's (`cursor/psp-hle-games2-2b67`); part 22's branch,
`cursor/psp-hle-games3-2b67`, has since merged it and sits on top of it (the end of part 22 says how). Games are
heard: what sceAudio's channels play goes into the system's sound stream, and sceSasCore's voices make sound.
Written from pspsdk's pspaudio.h (PSP_AUDIO_VOLUME_MAX), pspautotests' audio/sascore programs and the results they
recorded on a PSP (their samples, reproduced exactly), the notes in that suite's sascore.h on a SasCore's fields as
a PSP leaves them, psx-spx's description of the PlayStation SPU's ADPCM, and part 17's behavior specification of
sceAudio's timing; no other emulator's code was read.

- **The output** (`audio.cpp`). Think of the sound as a strip of frames, a left and a right sample each, 44,100 a
  second from power on: frame n is heard at cycle n * 370000 / 49 (`sampleFrame()`). Every channel adds what it
  plays to the frames where it's heard. At the end of each frame of the PSP's, the system takes the frames heard by
  then (`Kernel::audioOutput()`, from `System::run()`), each sum clamped to 16 bits, and hands them to the ares
  stream; silence makes up any the kernel's clock didn't reach (the program ended, or nothing will run again). The
  stream runs at the PSP's own 44.1 kHz, so the core resamples nothing: ares converts it to the host's rate. Frames
  not taken yet wait in a ring of 4096 (the mixer adds a block or so ahead of the clock). Room for frames within a
  block of the clock is made by dropping the oldest, which only happens with no system taking them (the kernel's
  tests); frames further ahead, which no channel adds, never push out frames not taken, and the first frame not taken
  never passes the clock: what doesn't fit is left out at the far end (`outputRoom()`).
- **The mixer channels.** At each block boundary of part 17's timing model, the DMA reads the next 64 samples of
  every channel with a buffer and adds them to the 64 frames heard from there: each sample times its side's volume
  over 0x8000 (pspsdk's PSP_AUDIO_VOLUME_MAX: as it is; 0x4000 half; up to 0xFFFF, nearly double), rounded down; a
  mono sample on both sides, each at its own volume. A buffer with no memory behind it is silence.
- **The SRC channel** (sceAudioOutput2, sceAudioSRC). Its buffers are heard one after another from the moment the
  first is armed after an idle stretch, at its rate. Each output frame falls somewhere between two of its samples
  and takes the straight line between them (linear interpolation), at its buffer's volume (0x8000 as it is): at
  22050 Hz a buffer fills twice as many frames as it has samples, every other one a sample as it is, the ones
  between halfway. A buffer is read while it plays (as the frames are taken), and finished before its slot is handed
  back to the program (part 17's 100 microseconds before it's heard: its last samples are added that far ahead). A
  buffer armed once both slots have freed, while the last one's final 100 microseconds are still to be heard (a
  program draining the channel with a null output before each buffer arms its next just then), is heard straight
  after them, from the frame after the last one's end, and retires 100 microseconds before its own end: a whole
  buffer after the last one's end, as a buffer chained behind another does. It starts on that frame, so at rates
  other than 44.1 kHz it leaves out the fraction of a frame the last one ended on (4096 samples at 48 kHz fill 3764
  frames, not 3763.2). The channel so adds to frames 7 at most ahead of the clock (`SrcAhead`: the 100 microseconds'
  4.41 frames rounded up, and two for where a buffer's ends fall between frames).
- **sceSasCore's voices** (`sas.cpp`), a grain sample by sample, for each voice playing and not paused:
  - Its sample where it is. A PCM voice's are 16-bit numbers in memory. A VAG voice's come packed as the
    PlayStation SPU's ADPCM: 16-byte blocks of 28 samples, 4 bits each. Four bits can't hold a sample, so they hold
    a correction: the decoder guesses each sample from the two before it (the last times the filter's first
    coefficient, less the one before times its second, over 64), and adds the 4 bits times 4096 shifted right by the
    block's shift (big corrections for loud passages, small for quiet). A block's first byte picks the filter and the
    shift, its second holds flags (where a loop starts and ends, where the data ends). Samples are decoded in turn as
    the voice moves on, across blocks and round its loop.
  - Its pitch (0x1000 a sample per sample made, its samples at 44.1 kHz; 0x2000 two, an octave up): where it is
    counts in 4096ths of a sample, and between two samples the one made is the straight line between them.
  - Its envelope, the ADSR height part 20 tracks (unchanged), multiplied in as it is before this sample's step; its
    volumes over 0x1000, rounded down: left and right make its dry sound, the effect's left and right its wet sound.
  - The grain is written clamped to 16 bits: in stereo (output mode 0) as left and right pairs, the dry sums when the
    voices are heard dry (on from __sceSasInit) plus, when they're heard through the effect and one is chosen, the
    wet sums at the effect's volumes; in multichannel (mode 1) as four planes of a grain each (dry left, dry right,
    wet left, wet right) for the game to mix. __sceSasCoreWithMix adds the voices to what the game's buffer holds,
    that scaled by its own left and right volumes first.
- **What the recordings fix, and this follows exactly** (audio/sascore's .expected files): VAG's guess rounds down,
  with no half added as psx-spx's SPU adds one (vag's music.vag samples 0x2d4b and 0x2cb4 only come out so);
  filters 5-15 read past the PSP's table of five into what follows it (vag's predict_nr 5-15: filter 7 guesses with
  52 and 0, 9 with 60 and 125, 13 with 2 and 216...); a VAG voice's samples are heard a sample late, where a PCM
  voice's are heard at its place (vag: silence then the first sample decoded; pcm: the 254th sample as the 254th
  made); the first sample after a key-on is silent (the envelope's 0); the four multichannel planes and the volumes'
  rounding (outputmode: 0x1000, 0xC00, 0x800, 0x400 give -33, -25, -17 and -9 of a -33); __sceSasCoreWithMix's
  volumes scaling the buffer (0 and 0 leave the voices alone) and its refusing multichannel (NOT_SUPPORTED,
  0x80000004, the buffer untouched); dry sound with no __sceSasRevVON (pcm, outputmode; sascore.h found its flags 1,
  dry, in a fresh SasCore, and the effect's volumes 0); and VAG's end marks read as whole bytes: music.vag's header,
  played as blocks, has flags 0x41 and 0x75 and plays on, where part 20's `& 7` would have ended it at its first
  block.
- **Chosen, as no recording shows it**: the straight line between samples (at pitches other than 0x1000, and the
  SRC channel's rates); the envelope's and volumes' rounding between the recorded values (each multiplication
  rounded down); shifts 13-15 as 9, as psx-spx says of the SPU; VAG flag bytes 1 and 7 ending the voice, 3 looping
  (or ending, not looping), 4 and 6 marking the loop's start, every other byte going on (0x87 too, which part 20 had
  ending); paused voices silent; the effect passing the wet sound through unchanged, and none with no effect chosen
  (type -1); and the mixer's and SRC channel's volume law (times the volume over 0x8000, as part 17's specification
  assumed).
- **Not done**: reverb, echo and delay (__sceSasRevType and its fellows keep their settings; the wet sound passes
  through as it is, which none of the games tried turns on); noise, triangle and steep waves (silent, their
  parameters kept); ATRAC3 voices in sas (silent: no decoder yet); sceAudioOneshotOutput and the rest part 17 lists.
- **Cheap**: no allocation per sample or per frame (the ring is made at power-on, a grain's sums reuse their room);
  a grain leaves out the planes it won't write (the wet pair, as most games have the wet sound off). On the host the
  scratch runner ran Lumines' 2400 frames in 48-50 s with sound as without.
- **States**: the output not taken yet (its frames, at most a ring's worth, each sum within what the channels at
  their loudest make, 2^21, so adding to it can't overflow), the SRC channel's place in its samples and the frame it
  adds to next, and each VAG voice's decoder (the two samples before its place, and whether it has started). Loading
  refuses frames ending before they start, more than a ring, frames taken past the clock, a sum past 2^21, the SRC
  channel past its samples, somewhere with none armed, or more than 7 frames ahead of the clock (`SrcAhead`), and an
  SRC buffer retiring later than one armed now could (heard from 7 frames on). The layout is version 6: a state of
  version 1 to 5 is refused by its header.

What the games sound like on the host (the scratch runner writing the system's stream to a WAV, 40 s each, kept
outside the repository; per second: RMS in dBFS, peak, clipped samples, zero crossings, the strongest frequency):
- **Lumines** (sas): silent through its logos and title screen (the game keys no voice there), then its demo stage
  from 25 s: effects (VAGs whose headers say 44.1 kHz, played at pitch 0x1000) and a music segment voice, -18 to -34
  dBFS a second, peak 19255, nothing clipped; the spectrogram shows regular beats.
- **Street Fighter III port** (four stereo sceAudio channels of 1024 samples at 0x8000, each from a thread of its
  own): heard from the start, a tonal jingle (harmonics) for 8 s, then its title music, -17.5 to -31 dBFS, peak 23517,
  nothing clipped; its 6900 outputs in 40 s are 44.1 kHz's pace exactly.
- **Space Invaders Extreme** (sas through __sceSasCoreWithMix into the SRC channel at 44.1 kHz): its effects, 9 of
  40 seconds, -19 to -27 dBFS, peak 7999, nothing clipped; its music is ATRAC3+ (no decoder: silent).
- **Burnout Legends** and **SOCOM**: silent at their titles, whose music is ATRAC3+; with Start (and Cross) pressed,
  their menus' effects through sas (Burnout's at pitch 0x800, SOCOM's at 0x1000): peaks 8006 and 9979, -29 to -51
  dBFS, nothing clipped.

Tests (`tests/psp/run-tests.sh`, 197 groups, both sanitizers; `tests/psp/ares` 250 checks):
- `audio.cpp`: "audio mixed" (a stereo buffer at 0x8000 and 0x4000 heard frame by frame, rounded down; mono at two
  volumes, and at 0xFFFF clamped at both ends; two channels adding up and clamping, the second joining the running
  DMA heard from frame 64; a buffer handed over 1 ms on heard from frame 44; a null buffer silent), "audio src heard"
  (22050 Hz: 64 samples fill 128 frames, as they are and halfway between; 44.1 kHz at half volume; 480 samples at
  48 kHz fill 441 frames, a ramp staying a ramp; two buffers back to back with a mixer channel beside them), "audio
  output in states" (part way through a mixer buffer and an SRC buffer: saved, loaded, the same state, the same
  frames after).
- `sas.cpp`: "sas vag as recorded" (vag.expected's filters 0-15 and five flag bytes, sample for sample), "sas vag
  decoded" (blocks worked out by hand from the format's definition: corrections at shifts 0 and 12, filters 1, 2 and
  4 with a negative guess rounding down and a clamp, shifts 13 and 15 as 9, the end mark, a loop, a 0x41 block going
  on), "sas pcm heard" (pcm.expected's frames and end flags for three loops; every sample at 0x1000; 0x2000 and
  0x800), "sas output modes" (outputmode.expected's twelve pairs in stereo, multichannel, stereo mixing and refused
  multichannel mixing), "sas voices mixed" (two voices clamped, an inverted volume, a half-way envelope, a paused
  voice, dry off, the wet sound through an effect and with none, mixing at 0x800), "sas voices in states" (a VAG and
  a PCM voice part way: saved, loaded, the same next grains).
- `states.cpp`'s "state fields" changes every new field and refuses 7 more states (above); `tests/psp/ares` refuses a
  version 5 state, and hears the system's stream (taken at 44.1 kHz, where ares's resampler hands samples on as they
  are): 735.7 frames a frame, cube silent, a buffer handed to a mixer channel as cube runs heard sample for sample
  from the frame it was handed over at (its right at half), and silence at that rate once hello has ended.
- Broken versions each failed: VAG's guess with a half added (vag's recorded filter 9, 0xb39c for 0xb39b, and a
  hand-worked sample), part 20's flags `& 7` (the 0x41 block ended its voice), VAG heard without its lag (every
  recorded VAG sample), the envelope applied after its step (pcm's and outputmode's recorded samples), dry off from
  __sceSasInit, multichannel's planes interleaved, the mixer's volume rounded towards zero, the sums unclamped, the
  SRC channel's nearest sample for the straight line, and its place left out of states ("state fields"); and the
  system's silent stream of before (`tests/psp/ares`).

Review: a general-purpose reviewer of the branch; the clean-room spot check found the code independent; one medium
and one low finding, both fixed, each with a test that failed before its fix. The medium: the SRC channel drifted
without bound. A buffer armed once the slots had freed was timed as heard from that moment, but its frames went
after the last buffer's, still to be heard for the 100 microseconds that buffer had retired early: a program
draining the channel before each buffer (an output, then a null output, in a loop) put each buffer 4.4 frames
further ahead of the clock, until the output's ring, making room, moved its first frame not taken past the clock
(with 1024-sample buffers at 44.1 kHz, silence for good on every channel from 21.9 s, and a state the machine's own
loading refused). Such a buffer is now timed from where it's heard, straight after the last one, so it retires a
whole buffer after the last one's end (the SRC channel, above); the ring never moves its first frame not taken past
the clock, leaving out what doesn't fit at its far end instead (the output, above); and loading wants the channel
7 frames ahead at most (states, above). Part 17's output2 results still hold: "audio src rest" and "audio draining"
now wait a millisecond where their programs armed a buffer in the last one's final 100 microseconds (on a PSP their
pspautotests runs print lines there, and its calls take over 100 microseconds each), so each first buffer from idle
retires 100 microseconds short as before: 13XX on both families, the release refused 1 ms on and done 2 ms on, the
drained 4096 samples, the 48 kHz and rate 0 buffers. The low: no test pinned the standard VAG ending, a block marked
1 and then a block of its own, 00 07 77 77..., which would sound as 28 samples of 28672 should the voice reach it.
New groups: "audio src drained between buffers" (the drain-and-arm loop for 30 s, 1024 samples at 44.1 kHz and 4096
at 48 kHz: never more than 7 frames ahead, every frame taken, each what the buffers joined end to end make, the
drains a buffer apart, the state saved where the channel is furthest ahead and at the end loading and saving the
same; the usual two-buffer stream as before), "audio output kept from a channel ahead" (a channel moved on by hand to
a ring ahead: a mixer buffer playing meanwhile taken whole, the first frame not taken never past the clock), "sas
vag endings" (the block marked 1 and the 00 07 77 77 block after it: silence and the voice's end right after the
first, nothing in the next grain; a block marked 3 ending a voice that doesn't loop; a loop going back to a block
marked 4); "state fields" now has the SRC channel 7 frames ahead and retiring as late as it can, and refuses a frame
further and a cycle later. Before the fixes, the drained loop lost 344,038 of 1,324,323 frames at 44.1 kHz in its
30 s, its state at the end refused, and the drains came 100 microseconds early at both rates; the ring dropped 583
of the mixer's frames not taken; and the old loading refused a state the machine now leaves. The sas group failed
with each of three readings broken: a block marked 1 not ending its voice (the 28-sample burst heard), a 3 not ending
a voice that doesn't loop, a 4 not marking the loop's start (the groups before it passed all three). The usual
two-buffer stream makes the same frames and return times as before at eight sizes and rates (a scratch harness, not
committed, run on the old code and the new), and the scratch runner's 40 s captures of Space Invaders Extreme (sas
into the SRC channel), the Street Fighter III port and Lumines are the old code's bit for bit. The tree passes 200
groups and 250 checks; the layout is unchanged (version 6).

Uncertain: everything listed as chosen above; whether sceAudio's mixer rounds a sample times a volume down (as
here) or towards zero, and clamps once, as the sums are taken (here), or after each channel it adds (which differs
where loud channels of opposite sign meet); which sample of a VAG voice its last block's lag leaves unheard as it
ends (here its data's last); and how the PSP's interpolation shapes pitches other than 0x1000 (audio/sascore's
pitch test prints no samples). Since the review: whether an SRC buffer armed in the last one's final 100
microseconds is heard straight after it (here it is: its transfer ends 100 microseconds before what's heard, as part
17's specification has every buffer's; no recording arms one then), and whether the PSP's converter keeps its place
across such a join (here the buffer starts on a frame).

## Part 22: the games, further

On branch `cursor/psp-hle-games3-2b67`, on top of part 20's (`cursor/psp-hle-games2-2b67`, #146), and since then
of part 21's (sound, `cursor/psp-sound-2b67`, #147, the sound worker's), merged in: see the end of this part. Part
20 left the owner's games stopped at places it couldn't explain: the three GTAs at 85% of their loading bar, Brave
Story on its Game Republic logo. This part traced each to what the kernel did that a PSP doesn't, then followed the
games on through their menus. The scratch host runner (never committed) grew what that took: write and read
watchpoints (the interpreter's stores and loads, through a subclass of the system's CPU), the import stubs and
exports as symbols for llvm-objdump over memory dumps, states saved at a menu and loaded to try its buttons, the
stick, and a memory stick folder per run. Sources as before: pspsdk's headers, pspautotests' programs and the
results they recorded on a PSP (threads/scheduling, intr, io/file, rtc, power/volatile), and the games' own
behavior; no other emulator's code was read.

- **Synchronous reads and writes wait** (io.cpp). sceIoRead and sceIoWrite on a file (the disc, the memory stick, a
  host folder) did their work and returned at once. GTA's streaming thread (priority 0x20) reads a request's data in
  64 KiB pieces and, as the last one ends, calls the request's callback; the callback checks the request against the
  one the main thread (0x38) noted when it made it, and drops any other as cancelled ("StreamingCallback: CALLED WHEN
  STREAMING WAS CANCELLED", found among the game's strings by a watchpoint on the counter that never fell). With
  reads taking no time, the streaming thread finished every piece before the main thread ran again to note its
  request, the callback dropped it, and the main thread waited for good for its queue (one entry) to empty, polling
  event flag 0x115. On a PSP a read waits for the drive while other threads run. pspautotests' intr/waits recorded
  both functions on a memory stick file as functions that wait: refused in an interrupt handler (ILLEGAL_CONTEXT)
  and with interrupts or dispatching held off (CAN_NOT_WAIT), a bad file refused first. So now each takes the time
  its asynchronous twin takes (part 20's 100 microseconds plus the bytes at the device's rate: 1,375,000 bytes a
  second from the disc, 4 MB a second on the memory stick), its bytes moved at once, the thread waiting meanwhile
  (a new wait, Wait::File, returning the call's result as its time is up). Standard input, output and error take
  no time. Called by a test with no thread running, it's done at once, as before. Since the review, sceIoIoctl's two
  disc reads (0x01030008's bytes, 0x01f30003's sectors of umd0:) are refused and timed the same way.
- **Interrupts held off keep the CPU** (interrupts.cpp, threads.cpp). sceKernelCpuSuspendIntr is the CPU's own
  interrupt flag (pspautotests' intr/mfic: `mfic v0, $0; mtic zero, $0`, only its lowest bit counting, so resuming
  with 2 leaves interrupts off), so on a PSP nothing can take the CPU from the thread holding them off: the timer's,
  the sound DMA's and the vertical blank's interrupts are what would wake another thread. Here a thread whose wait
  ended took the CPU all the same, with the one global flag still off. Brave Story holds interrupts off around its
  own lock; its sound thread (0x10) took the CPU in there as a buffer ended, found every blocking output refused
  (CAN_NOT_WAIT: interrupts were off), and spun for good at the top priority, starving the game on its logo. Now the
  running thread keeps the CPU while it holds interrupts off, as with dispatching held off, even as it rotates its
  own priority's line or changes its own priority (since the review); turned back on, the calls held back run, then
  the scheduler picks. Every function that waits refuses with interrupts held off (intr/waits recorded each alike
  with interrupts and with dispatching held off: until now only sound's did). A thread taking the CPU has interrupts
  on, the flag being its holder's (it can't lose the CPU meanwhile but by ending), and a handler that leaves them off
  doesn't pass that on (since the review). The kernel's flag is the CPU's own since the review, so the program's own
  mfic and mtic see and set it: on as a program starts (intr/mfic read 1 first), mtic keeping its lowest bit alone.
- **The keyboard** (utility.cpp: sceUtilityOsk*) was answered as cancelled. Each field's text is now accepted as it
  stands (UNCHANGED), and an empty field, or one of spaces, gets the console's nickname, "PSP" (CHANGED), as a player
  asked for a name would type one; each as far as the field's room (outtextlength, its NUL among it) and limit
  (outtextlimit) allow, in UTF-16 (psputility_osk.h): a limit of 0 is none, and a field with no room gets nothing
  written (since the review). Peace Walker asks for its player's name in an empty field, refuses an empty answer
  ("Please enter at least 1 characters") and asked again for good.
- **Renaming** (io.cpp: sceIoRename), as pspautotests' io/file/rename recorded: the file takes the new path's last
  name and stays in its own folder, whatever folder the new path names ("../t2.txt" from ms0:/PSP renamed to
  "t2a.txt", or to "ms0:/PSP/t3a.txt", lands in ms0:/); a name taken there, the old one itself among them, is
  FILE_EXISTS, so a new path into a folder that isn't there finds the old file's own name taken; another device is
  pspkerror.h's XDEV (0x80020322, where uOFW's 0x80010012 stood); an old file that isn't there FILE_NOT_FOUND;
  wildcards INVALID_ARGUMENT. Peace Walker installs its data writing a temporary file and renaming it to a bare
  "TDLSFILE.SYS", which had been looked for in the working folder, on the disc, and refused as read-only.
- **Three functions the games asked for next**: sceRtcGetWin32FileTime (Midnight Club 3, making a profile), a date
  as 100-nanosecond steps since 1601, earlier dates 0 and INVALID_VALUE, no pointer INVALID_VALUE (rtc/convert's
  results, among them a day past November's end counting on into December); sceRtcSetTick (Peace Walker), a tick
  back into a date by Howard Hinnant's civil_from_days, as rtc/convert recorded; sceKernelVolatileMemLock (Burnout
  Dominator), part 17's volatile memory borrowed waiting while it's lent, those waiting served in the order they
  came (power/volatile/lock: three threads of priorities 0x31, 0x33 and 0x32 served in that order), refused without
  borrowing where a thread can't wait, its address and size written first (a new wait, Wait::Volatile).
- **A delay lasts as long as a PSP's** (threads.cpp). sceKernelDelayThread and DelayThreadCB waited exactly what
  they were asked. pspautotests' threads/scheduling/delaylen recorded every delay from 1 to 209 microseconds taking
  about 230 and longer ones about 25 more than asked (220 about 250, 300 about 330, 1000 about 1030), CB or not, and
  preemptuser's 1000-microsecond delays took 1020 to 1040: the thread manager wakes a thread no sooner than about 205
  microseconds on, and waking it takes about 25 more. So a delay now lasts its time, 205 at least, plus 25. A delay
  of 0 still only gives the CPU up for a moment (delayzero: it returns at once, or lets a worse thread in). Brave
  Story's threads poll with delays of 1 microsecond and woke 230 times as often as on a PSP: about 34 million delays
  in 10 seconds of its menus, which took the host 29 seconds, 22 now. Wait timeouts are left as they were, though
  waittimeouts recorded them alike (max(timeout, 205) plus about 35; a timeout of 0 or 1 ends at once, its time not
  written back).
- **States** carry both new waits (their fields were there already), and loading checks them: a file wait waiting,
  with no callbacks, due within the longest request (64 MiB from the disc, under a minute) and not a frame overdue;
  a volatile wait only while the memory is lent, with no time limit or callbacks; neither put aside for callbacks.
  The layout was unchanged, but the review found the interrupt flag's meaning changed under it, and the flag is the
  CPU's alone since (the kernel keeps no copy, and loading wants it 0 or 1): version 6, a state of version 1 to 5
  refused; version 7 since part 21 merged (this part's end).

What the games do now, on the host (frames under `/tmp/hle3-runner/final`, outside the repository; the runner's
saved states under `/tmp/hle3-runner/states` take each back to where it got):

- **GTA Liberty City Stories**: past its loading bar into the city; its opening plays (Toni with his suitcase, the
  phone call, the taxi, Salvatore's office) for 9000 frames without a press.
- **GTA Vice City Stories**: past its loading bar into its opening scene at the army base.
- **GTA Sindacco Chronicles**: past its loading bar to "Press X to choose the soldier difficulty or O to choose the
  boss difficulty" over the city.
- **Brave Story**: past its logos (XSEED, Game Republic), its intro and title, New Game, the hero's and the leading
  lady's names (the game's own keyboard: down to OK), into "Prologue: Doorway to Destiny", its first scene talking.
- **Gunhound EX**: past its logos (G.rev, Dracue, CRIWARE) to its title and menu (Circle confirms, as in Japan), Game
  Start, the area map, Mission 01's briefing, and the mission's start ("Plant Assault"): its text is its own.
- **Snoopy vs. the Red Baron**: past its no-save warning (continue without loading), "Autosave disabled", its title,
  a new profile (its own letter picker: up, then Done, then Left to YES), loading, and in the game: Marcie's flying
  lesson over the town.
- **SOCOM Fireteam Bravo**: past its no-data screen, its autosave notice and credits, a new profile (its own
  keyboard, which wants a button held: right along the top row, down twice to ENTER), the profile saved, its main
  menu, Campaign, New Campaign, a difficulty, the campaign saved, its first mission's briefing, down its tabs to
  Deploy, loading, and in the mission: the soldier in the Andes, the game's help tips showing (no music: ATRAC3plus).
- **Burnout Legends**: Start, a new profile (its own keyboard: DONE), saved (YES), World Tour, Compact class,
  Interstate Loop, a race, a car, and racing: lap 1 of 3, Cross held accelerating (61-88 mph, "Extreme shunt").
- **Burnout Dominator**: Start, a new profile saved, its main menu, World Tour, and its first race loading; then its
  GE runs off the end of a display list (below).
- **Midnight Club 3**: Start, Create Profile (sceRtcGetWin32FileTime), its main menu, a cutscene skipped (Triangle),
  and a race: the timer running, Cross held accelerating through the city.
- **Metal Gear Solid Peace Walker**: Start, NEW GAME, its player's name ("PSP", from the keyboard), Right then Cross
  to accept it, its button configuration, and its data install, which writes and renames its first files (the
  install's own menus are drawn in the system's fonts, which aren't there: going on means guessing buttons).
- Lumines, Space Invaders Extreme and the Street Fighter III port reach what they did before.

Seen on the way, not changed:
- **Burnout Dominator's GE**: once its race has loaded, the GE reads textures from addresses with no top bits
  (0x00f79aac: a texture's 24 bits without TEXTURE_BUFFER_WIDTH0's 0x08), then spends its million commands a frame on
  one list that never finishes, the game waiting on sceGeDrawSync. Walked by hand, the two lists it alternates
  (0x09e05900, 0x09e01980) end cleanly (595 and 867 commands, every texture's top bits set), so the GE takes another
  path than they say: a GE question for later.
- **Peace Walker on the handheld** (garbled, 9.5 frames a second): here, with the same core on ARM64, its title draws
  correctly, and runs at 11.5 frames a second; a profile puts nearly all the time in the GE's drawing (drawPixel,
  texel, sample, Memory::read from texel fetches, rectangle), the CPU's recompiler under 1%, the kernel's calls few
  (no polling). So the speed is the GE's per-pixel cost of its layered, filtered sprites; the garbling, which
  doesn't show here, is something of the Android build or front end's to look into.
- Recorded by pspautotests, not done here: a thread started with dispatching held off runs at once
  (dispatchwake's "TMR"; here it waits); sceIoOpen with interrupts or dispatching held off returns -1 and in a
  handler ILLEGAL_CONTEXT (intr/delays, which records __sceSasCore refused alike: done once part 21's sas.cpp merged,
  at this part's end); the wait timeouts above.

Tests (`tests/psp/run-tests.sh`: 196 groups, both sanitizers; `tests/psp/ares` 236 checks):
- `async.cpp`: "files synchronous reads wait" (both engines: a 4000-byte read from the memory stick taking 1100
  microseconds while a worse thread runs, a 1000-byte write 350, 2750 bytes from the disc 2100, 11 of umd0:'s sectors
  16484; a read to nowhere at once; refusals with interrupts and dispatching held off and in a vertical blank's
  handler, a bad file first, nothing moved) and "files synchronous wait state" (a state saved mid-read carried on in
  another machine, and with no disc).
- `callbacks.cpp`: "interrupts held off keep the CPU" (both engines: a better thread whose delay ends while main
  holds interrupts off runs as they come back on, with its own interrupts on and its delay not refused; every wait
  refused meanwhile; resuming with 2; a state saved with the better thread ready); the vertical blank groups now spin
  with interrupts off, a delay there being refused.
- `utility.cpp`: "utility keyboard" (accepted, empty, blank, limited, roomless and outputless fields).
- `files.cpp`: "files rename" (io/file/rename's cases, and Peace Walker's form).
- `media.cpp`: "rtc file times and ticks" (rtc/convert's values, a leap day, the last microsecond of 9999).
- `power.cpp`: "power volatile memory waited for" (both engines: three waiters served in order, the refusals, a
  state saved with them waiting) and "kernel thread delays' lengths" (both engines: delaylen's lengths, CB or not).
- `states.cpp`'s "state fields" refuses nine more states (six impossible file waits, three volatile ones), each
  beside one that loads.
- Broken versions each failed: synchronous I/O done at once (34 failures in the two new groups), interrupts held
  off as before (16, "TMR" where the PSP gives "MTR"), the volatile memory served by priority ("M132").

Uncertain: the timing of synchronous requests (part 20's device rates, seeks not counted), and whether a PSP refuses
a read in an interrupt handler only as intr/waits shows for the memory stick (the disc's not tried); a rename of a
file open, and of a folder (taken as a file is); the keyboard's answer for an empty field (the nickname is a choice,
as is accepting each field at once); which of a cross-device rename and a missing file comes first; the delay's
minimum and its 25 microseconds (fitted to delaylen's figures, rounded to 10 there), and a new thread's first delay,
which delayzero found can be short; and with interrupts held off, whether a woken thread of higher priority would
take the CPU from a system call (here, as with dispatching held off, it waits). Since the review: whether a thread
rotating its own line, or changing its own priority, with interrupts or dispatching held off gives way to its
equals once they're back (here the rotation is dropped, as part 20 chose for dispatching); what an mtic of the
program's own that turns interrupts back on lets in at once (here the calls held back come at the kernel's next
look, a better thread at the scheduler's next pick); and the keyboard's limit of 0 (no limit here).

Review: a general-purpose reviewer of the branch; the clean-room spot check found every area independent; one
medium and five low findings, all fixed, each with a test that failed before its fix. The medium: a thread holding
interrupts (or dispatching) off still lost the CPU when it rotated its own priority's line or changed its own
priority: both make the caller ready, the scheduler kept only a running thread, and the switch turned interrupts
on, handing the holder's critical section to another thread. reschedule() now keeps the caller running whenever
they're held off and it's running or ready; it gives way once they're back, to a thread better than it then. The
lows: a handler that held interrupts off and returned left them off for the thread it had interrupted, every wait
of its refused (callReturned() turns them back on: calls start only with them on); the keyboard took a limit of 0
as no characters, the nickname cut to nothing but still CHANGED, and wrote a NUL into a field with no room (0 is no
limit now, and a roomless field gets nothing); sceIoIoctl's disc reads still took no time and went ahead where a
thread can't wait (refused and timed as sceIoRead's now); the interrupt flag's meaning had changed under the same
state version, so a version 5 state holding them off for a thread that wasn't the holder would spin it for good
(version 6, older ones refused, the flag checked on loading); and mfic and mtic worked on a flag of the CPU's apart
from the kernel's, reading 0 as a program started where intr/mfic recorded 1 (the kernel's flag is a reference to
the CPU's now, on at power, mtic keeping its lowest bit as intr/mfic read 2 and 0x80000000 back as 0, and
Kernel_Library's sceKernelCpuResumeIntrWithSync is listed as sceKernelCpuResumeIntr). New groups: "interrupts back
on after a handler", and "interrupts one flag, mfic's and the kernel's" (both engines: intr/mfic's thirteen values
in its order, then a delay refused after an mtic of 0 and a better thread kept out until the resume, "MTR"). Grown:
"interrupts held off keep the CPU" (the holder rotating and lowering its priority with interrupts and with
dispatching held off: "ABP", interrupts 0 inside, where the old code gave "APB" and 1), "utility keyboard" (four
more fields), "files synchronous reads wait" (the ioctl reads, their refusals and an answer still at once), "state
fields" (a flag of 2 refused; its kernel copy gone from the fields), the CPU tests (the flag on at power, mtic's
lowest bit), and `tests/psp/ares` (a version 5 state refused). The tree passed 198 groups and 240 checks.

Merged with part 21 (sound, `cursor/psp-sound-2b67`, #147), which now sits under this part. The code merged by
itself: sound rewrote audio.cpp, sas.cpp and the system's stream, which this part doesn't touch, and its additions
to the kernel's header, power-on and states sit beside this part's. What clashed: the state's layout, which each
branch had made version 6 its own way (part 21's for the output not yet taken, the SRC channel's place and VAG
voices' decoders; this part's for the interrupt flag, the CPU's alone, its meaning changed), so the merged layout is
version 7 and a state of version 1 to 6 is refused by its header (`tests/psp/ares` tries each); and the docs, part
21 placed before this one and the handoff's entries newest first. "state fields" has both branches' fields and
refusals. Sound's blocking outputs already refused where a thread can't wait, as this part's waits do. The merged
tree passes 210 groups and 254 checks, nothing the merge broke having needed a fix. Then, with sas.cpp merged, this
part's rule for functions that wait covers it too: __sceSasCore is refused where no thread may wait, as intr/delays
recorded (ILLEGAL_CONTEXT in a handler, CAN_NOT_WAIT with interrupts or dispatching held off), before the buffer or
a voice moves; __sceSasCoreWithMix, which waits alike, is taken the same (not recorded; intr/waits has no sas
call). New group "sas core refused where no thread may wait" (both engines; without the check its six refusals
returned 0 and its voice ended). The tree passes 211 groups (both sanitizers) and 254 checks. On the host (a
scratch runner from boot, no buttons pressed, its frames and WAVs outside the repository): GTA Liberty City Stories
reaches the city by frame 1200, Brave Story its intro past its logos, Burnout Legends "PRESS START BUTTON TO
CONTINUE", as before; all three silent there (Burnout's title music is ATRAC3+, Brave Story's ATRAC3 data is
refused, and GTA's sound thread makes its sas grains and blocking outputs at their pace with no voice keyed on).
The Street Fighter III port's sound is part 21's capture second for second, but starts about 4 s later: its 4.4 MB
of reads as it boots now take the disc's 3.2 s (this part's synchronous reads).

## Part 23: the system fonts

On branch `cursor/psp-fonts-2b67`, on top of part 22's `cursor/psp-hle-games3-2b67`. Games that print with the
PSP's own fonts now show their text. Those fonts are the PGF files in flash0:/font, part of Sony's firmware, which
Phobos never has: they come from the owner's own PSP (a flash0 dump, such as `tools/psp-flash0-dump` makes). The app
copies them into its own files, from the dump's folder in the device's Download folder by itself, or from a folder
the owner picks, and the core reads them from there as the PSP powers on. Nothing of Sony's is in the repository,
the APK, a commit or a backup: the tests make fonts of their own. Without the fonts, sceLibFont is part 20's
stand-in, which starts and finds none, as before.

Sources: the owner's eighteen fonts (firmware 6.61), read field by field with scratch scripts outside the repository
until every glyph and shadow in them decoded to exactly its record's length; pspautotests' font programs (tests/font:
newlib, fontlist, open, openfile, openmem, optimum, find, fontinfo, fontinfobyindex, charinfo, shadowinfo, the glyph
images whole, clipped and at fractions of a pixel, the image rectangles, altcharcode, resolution, fonttest) and the
results they recorded on a PSP; pspautotests' libfont.h and vitasdk's libpgf headers (the PS Vita's font library is
the same one) for the structures and errors. No other emulator's code was read: PPSSPP and JPCSP each have a PGF
reader, and neither was opened.

- **The PGF reader** (`pgf.hpp`, `pgf.cpp`; the header's comment explains the format and its RLE in plain words). A
  PGF holds one font at one size: a header, four tables of measurements (dimensions, x and y adjustments, advances,
  each entry two numbers in 64ths of a pixel), a shadow map, the Korean font's lists of code ranges (revision 3), a
  character map from Unicode codes to glyph numbers, the glyphs' pointers, and the glyphs. All but the header are
  packed: a number takes as many bits as the font says, lowest bit first. A glyph's record gives its picture's size
  and place, says which measurements are table entries and which are written out, names its shadow, and holds its
  picture: 16 shades a pixel, row by row or column by column, run-length encoded (a nibble below 8 repeats the next
  one that many times plus one; 8 or more is followed by 16 minus it shades as they are). A composite (a Korean
  syllable) names up to three glyphs drawn at their own places instead. A shadow, a blurred picture drawn under a
  character, is carried in the record of the character the shadow map names. The fonts are the user's files, so
  nothing in them is trusted: opening checks the header and that every table lies inside the file, and each glyph is
  checked as it's read (its record inside the file, its table entries in their tables), so a damaged glyph is missing
  on its own and the rest still draw.
- **The library** (`font.cpp`, rewritten): sceFontNewLib and DoneLib; GetNumFontList, GetFontList and
  GetFontInfoByIndexNumber; FindOptimumFont and FindFont; Open (a system font), OpenUserFile (a game's PGF file, read
  whole or a piece at a time), OpenUserMemory and Close; GetFontInfo; GetCharInfo, GetCharImageRect,
  GetCharGlyphImage and GetCharGlyphImage_Clip, and the same four of a character's shadow; SetAltCharacterCode;
  Flush; SetResolution and the four conversions between points and pixels.
- **It works in the game's memory, as Sony's does**: libfont is a user module the game carries, and sceFontNewLib's
  parameters name the game's own alloc and free. The library asks them for every block it keeps, in the sizes and
  order newlib and open recorded: its own 76 bytes (the library handle, which games read and write), the handles
  (76 bytes each, up to 9), their data (560 each), the list of fonts (168 bytes each); for a system font its nine
  tables; for a file read whole, the file, then a 12-byte record; for a font in the game's memory, the record. A
  font already open in the library is shared and asks for nothing. Close gives an open font's blocks back in the
  order recorded, DoneLib each font's, then its own. The calls are made as callbacks are (events.cpp): the thread
  that called the library runs the game's function on its own stack, returning to a new trampoline syscall (the
  sixth), and the library carries on from there. An alloc that gives nothing ends the call: what was given goes
  back, and the call fails out of memory, as newlib recorded.
- **Finding**: the eighteen fonts are listed in the PSP's order (jpn0, ltn0 to ltn15, kr0), each with its family,
  style, language and country. FindOptimumFont gives, of the fonts with the most matches to what the style asks for
  (size, family, style, sub-style, language, region, country, name, file name), the one nearest the size asked for
  (the first of those as near), else the last of them, and font 0 for a style that matches none and gives no size;
  FindFont gives the first font with everything asked for, the size exactly, or -1. The size asked for is the
  smaller of the two (across and down) at the style's resolution, or the library's, measured in points at the font's
  own; a height alone asks for 0, as both recorded.
- **Measuring**: GetFontInfo is the header's biggest measurements (as written and in pixels), the widest and tallest
  picture, the glyph and shadow counts (shadows 0 for a font read into memory, as fontinfo recorded), the font's
  style and 4 bits a pixel. A character's info is its picture's size and place and its measurements: the ascender is
  its y adjustment across, the descender that less its height (charinfo). A code below the font's first gives an
  empty glyph; one the font hasn't got, the alternative character ('_' to start with, kept in the library, as
  altcharcode recorded); a missing alternative, an empty glyph. A shadow has its character's measurements and its own
  picture and place (shadowinfo).
- **Drawing**: the picture is added into the game's buffer, each pixel up to the brightest (never erasing), in the
  two formats that draw: 4 bits a pixel, the left one in the low nibble, and a byte a pixel (shade n as 17n). The
  other three (4 bits the other way round, 24 and 32 bits) leave the buffer untouched, as charglyphimagexfrac
  recorded. A position's fraction of a pixel across shares each pixel with the next column, which gets its shade
  times the fraction over 64, rounded down; the fraction down is dropped (charglyphimagexfrac). Every write keeps to
  the buffer, the clip rectangle (_Clip's, its width and height taken as unsigned) and the memory there is.
- **Resolution**: SetResolution keeps each library's resolution in it (where games read it), refusing 0, negatives,
  minus infinity and not-a-number; the conversions use the library's, as resolution recorded.

The rest of Phobos:
- **The core's option** `option("Fonts", folder)` (`system.cpp`): the host folder holding the fonts. As the PSP
  powers on, `fontsFrom()` reads the eighteen by name, any case, each whole, checked and remembered by a hash, and
  none bigger than 4 MiB (the biggest of the PSP's, jpn0.pgf, is 1.5 MB); one missing, damaged or too big keeps its
  place (opening it fails, and a note says which), and with none the stand-in stays.
- **In the app** (`PspFonts.kt`): the copies are in the app's own files, `firmware/PlayStation Portable/font`, and
  the runner hands that folder to the core as a game loads (`setPspFontsPath`), or nothing when it's empty. They
  come two ways, each copying only the eighteen, by name (any case), of 1 byte to 4 MiB each, every one through a
  file beside it renamed over the old copy once whole; a font that can't be copied (unreadable, empty, too big,
  whatever goes wrong) is left out on its own, its part copy removed, and named.
  - **By themselves**: at the app's start, before a PSP game loads and as Settings, Firmware opens, while the app
    has fewer than the eighteen, it looks in the device's `Download/FLASH0DUMP` (the dumper's folder: its
    `flash0/font`, its `font`, or the folder itself) and copies those it hasn't got; with all eighteen it reads
    nothing outside its own files. The app has no access to all files: Android lets it read there through a folder
    grant that covers it (as it reads games in place, through the folders picked for them), or where the device's
    own Android allows it without one (the RP6 does: below), and a font there must open, not only be listed. Where
    the app can't read there, nothing happens and the picker remains. The row then says "N of 18 fonts, found in
    Download/FLASH0DUMP".
  - **Picked**: Settings, Firmware, the row "PSP fonts (from your PSP's flash0)" picks a folder (the system's folder
    picker): flash0's `font` folder, or the dump or flash0 folder above it. The row says how many of the eighteen
    are there ("N of 18 fonts copied"), and the picker's message names the fonts it couldn't copy, apart from
    finding none.
- **Never in a backup**: Android's backups, to the cloud and from device to device, leave the app's `firmware/`
  folder out (`res/xml/backup_rules.xml`, `data_extraction_rules.xml`), so the copies never leave the device through
  Phobos. After a restore the fonts are imported again: by themselves while they're in Download/FLASH0DUMP and the
  app can read it there, else with the picker.
- **States** (version 8; 1 to 7 refused): the libraries, the fonts open in them (where each came from and its hash,
  not its bytes) and any call into the game part way through. Loading reads each font again from where it came from
  (the system's from the folder, a file of the game's whole, a font in memory from the game's memory) and refuses the
  state if one is missing or differs, as it does every handle, count and call that doesn't add up.

What the games do now, on the host with the owner's fonts (frames under `/tmp/fonts-runner/final`, outside the
repository):
- **Gunhound EX**: its save notice (nine lines of Japanese in jpn0: the game saves, needs 160 KB, saves by itself,
  and loads only as it starts), which was a black screen, then NOW LOADING and its logos as before.
- **Metal Gear Solid Peace Walker**: "Checking Memory Stick™", the disclaimer on Militaires Sans Frontières (its
  accented letters too), its player's name ("PSP / Is this name OK?" with CANCEL and OK), the control scheme and its
  BUTTON CONFIG, and the DATA INSTALL screen and "Installing... (Progress: 6%)", which part 22 had to guess its way
  through: the game's own pictures showed, but none of its words.
- On the RP6, the settings row was given the dump's own folder (`Download/FLASH0DUMP`), found `flash0/font` in it
  and copied the eighteen; the games weren't started there.
- On the RP6 again, with the review's fixes (installed over the app, data kept, the picker's grant on the dump's
  folder gone with the update): at start the app found all eighteen it had copied and looked for nothing ("all 18
  here, none looked for"); as Settings, Firmware opened, the row said "18 of 18 fonts copied", Complete, and the
  dump's folder was found by itself and read without any grant ("18 in
  /storage/emulated/0/Download/FLASH0DUMP/flash0/font, readable": the app has no storage permission there, and the
  files are a file manager's, not media, so the RP6's own Android allows it). The automatic copy itself wasn't seen
  there: the app already had all eighteen, which nothing may remove on the device; the host's tests copy them.

Tests (`tests/psp/run-tests.sh`: 221 groups, both sanitizers; `tests/psp/ares`: 264 checks):
- `font-maker.hpp` builds PGFs from scratch (glyphs by rows and columns, table entries and written-out
  measurements, shadows, revision 3's ranges and composites), and the eighteen system fonts as stand-ins: each a few
  glyphs, its sizes and names as the PSP's list has them, font 1 drawing the 'A' pspautotests' ltn0.pgf (made from
  Liberation Sans) drew on a PSP.
- `font.cpp`: "pgf read back" (every field and picture), "pgf damaged" (every length the file can be cut to, and a
  thousand random bytes changed, under both sanitizers: refused or read, never past the file), "fonts memory" (every
  block's size and order, the library's fields, too many fonts, the order blocks go back, an alloc that fails at each
  step), "fonts found" (every case optimum and find recorded), "fonts measured" (fontinfo's, charinfo's and
  shadowinfo's values), "fonts drawn" (the recorded rows at 10.5 and 10 63/64 pixels, both formats that draw, the
  three that don't, adding up, lines of 0 and 1 byte, the clip cases), "fonts of the program's own" (files read
  whole and a piece at a time, fonts in memory; a file opened by a path relative to the working folder, loaded into
  a fresh machine; a font in memory opened with -1, read no further than 8 MiB, and opened again where it's open
  without its memory read again), "fonts resolution", "fonts states" (saved part way through the game's alloc at
  three points, loaded into another machine, carrying on; refused without the fonts or with a different kr0; a call
  opening a font of the game's memory, refused as read whole) and "fonts folder" (a font over 4 MiB not read).
- `states.cpp`'s "state fields": a library with an open font and a call part way through, each field changed, and
  eighteen states refused (too many handles, a handle past the count, counts that don't add up, a font nobody
  holds, memory that isn't a PGF, a font of the game's memory read whole, a font that differs, a call on a thread
  that isn't there, a machine without the fonts, and so on).
- `tests/psp/ares`: the option (no fonts without it; with it, the eighteen places, one read, one missing; none kept
  once the game unloads), and a version 7 state refused.
- The app: `PspFontsTest` (only the eighteen are fonts, any case; 1 byte to 4 MiB, as the size says and as the copy
  finds it; any failure kept to its own font, its part copy removed, the rest copied; the picker's message telling
  failures apart from none found; the folder found from flash0 or the dump above it; and, with fake storage, the
  dump's fonts found by themselves in `flash0/font` or `font`, only those missing copied, nothing read with all
  eighteen, nothing from a dump the app can't read, the found mark taken away by a pick).
- Broken versions each failed: the fraction's share rounded up, pixels stored rather than added, blocks given back in
  another order, the optimum's ties, a font's hash not checked on loading, columns read as rows, the list's count
  not capped, the descender, a call's blocks not saved, the fraction down kept, codes below the first not empty, and
  table entries not checked (found by the address sanitizer).

Review: a general-purpose reviewer; the clean-room spot check found `pgf.cpp` and `font.cpp` independent. One medium
and four low findings, all fixed, each with a test that failed before the fix; the state's layout didn't change (still
version 8).
- Medium: a state could say a font of the game's memory was read whole into it (mode 1), which
  sceFontOpenUserMemory never makes. Loaded, a call part way through opening one ended as a whole file's open does,
  writing its record through a second block it never asked for (the address sanitizer's heap overflow). Such a state
  is refused now, and an open's ending asks where the font came from before its mode.
- Low: a game's font file opened by a path relative to its working folder (after sceIoChdir) was kept as given, and
  loading reads fonts again before it puts the working folder back, so a fresh session refused the state. The path
  is kept whole as the font opens.
- Low: a font in memory given a length past its end (openmem's -1) was copied to the end of RAM at every open and
  every state loaded. 8 MiB at most are read now, and one the library has open at that address is shared without
  its memory being read again.
- Low: the picker copied any .pgf, whatever its name or size, and one file it couldn't read stopped the rest and
  was reported as no fonts there. Now: the eighteen by name only, 1 byte to 4 MiB each, every failure kept to its
  own file (its part copy removed) and told apart from finding none; and the core doesn't read a font file over
  4 MiB.
- Low: Android's backups took the copied fonts along (the rules excluded nothing); the firmware folder is left out
  of them now.

Uncertain: a shadow's flags are reported as the file has them (the PSP gave other values for the same font: what
they mean isn't known); the Korean font's country (3 is a guess); codes kept for composites' parts aren't looked up
on their own; where a composite's parts overlap, their shades add; what's in a font's 12-byte record, and the order
of a file's two blocks; a file opened a piece at a time is read through the kernel's files, not the game's own
callbacks (so a game reading its fonts from an archive of its own wouldn't find them); drawing asks the game for no
memory (a PSP borrows two blocks a font till it closes); a closed handle answers while its font stays open, and a
second close is refused; the order blocks of a font opened from a file or memory go back in; the library refusing
calls made from an interrupt handler or with no thread; codes below the first giving nothing, where a PSP might
draw something for control codes.

## Part 24: making the GE fast

On branch `cursor/psp-ge-speed-2b67`, on top of part 22's `cursor/psp-hle-games3-2b67`, with part 23 (the fonts',
`cursor/psp-fonts-2b67`) merged underneath since: see "The review, and since" at the end of this part. The
owner's decision (2026-10-06, in Decisions): the software renderer as fast as humanly possible first, the GPU
renderers later; the software renderer is the reference, so it must draw exactly the pixels it drew before. On the
RP6, GTA Liberty City Stories drew at 17.7 frames a second, Peace Walker's title at 9.5, one core busy and the rest
idle; on the host a profile put nearly all of Peace Walker's time in the GE's drawing.

**Where the time went** (`sample` on the host, the core built with the Android build's flags: `-O3 -flto=thin
-ftree-vectorize -funroll-loops -fno-math-errno -fno-trapping-math`): looking texels up (`texel()`, `Memory::read()`,
`sample()`, widening 16-bit colors) took 47-58% of the time in five of the six scenes below (Gunhound 26%), the
pixel pipeline (`drawPixel()`) 17-38%, triangle and sprite loops most of the rest; the CPU's recompiled code, the
kernel and setting primitives up a few percent. A scene's frame is mostly one long run of the GE: GTA, Peace Walker,
Lumines, Burnout and Midnight Club 3 draw all of their pixels in runs of 200,000 pixels or more (one or two a frame),
Gunhound 54% (in 53 runs a frame). They draw a lot: Midnight Club 3 2.7 million pixels a frame (21 screens), Peace
Walker 1.8 million.

**What was done**, each step measured and proved identical before the next (ares/psp/ge):

1. **Textures kept decoded** (texture.cpp). A texture is decoded once into 8888 texels, each exactly what `texel()`
   reads (one routine for both), and drawing takes a texel in one step. The copy is good only while its memory stays
   as it was, so the GE watches the pages it came from (memory.hpp: `watch()`; `changed()` reports a write to a
   watched page, as do power and a state loaded), and the CPU's compiled stores keep off watched pages as they keep
   off pages holding compiled code (`Allegrex::watched`, `recompiler.protect()`), so they go through `write()` too.
   A palette texture is kept per palette (its hash, checked byte for byte). Read from memory as before: DXT, a
   texture not wholly in memory (its reads still reported), and a primitive that may draw over its own texture.
2. **Primitives set up once, drawn row by row** (draw.cpp, raster.cpp). Setting a primitive up works out everything
   that doesn't change from pixel to pixel into a Job; drawing works each pixel out from it with the same operations
   on the same numbers in the same order, so any rows can be drawn on their own. Meanwhile: a triangle's rows found
   by division and its edges stepped by adding (whole numbers), values worked out only where the pipeline uses them,
   a 2D sprite's texel coordinates once a column and once a row, and the pixel pipeline once for each frame buffer
   format.
3. **Several threads, in bands of rows** (threads.cpp). While the GE runs a list, primitives wait in a batch; the
   batch is cut into bands of 8 rows, which the GE's thread and its workers take in turn, each band drawing every job
   that reaches it in the list's order. A pixel is only ever drawn by its band, so every pixel is drawn exactly as
   one after another would draw it. A batch keeps to one render target, and to an area where no two pixels in
   different rows share a byte (frame and depth buffers apart, rows not reaching each other); anything else is drawn
   by itself, in order. Option "GE Threads" (system.cpp): how many threads draw; 0, the default, one fewer than the
   host's cores; 1, the GE's own alone, every primitive drawn at once as before; no more than twice the host's cores,
   nor 64 (since the review). In the app it's Settings' "PSP Drawing Threads" (below). Workers may run on any core:
   the Android front end pins the emulation thread to the fastest, and threads start on their maker's cores.
4. **In 2D, where a primitive draws and which texture rows it reaches, exactly** (draw.cpp): its region is the
   pixels its sprites, triangles or points can cover between its outermost vertices, not the scissor rectangle; its
   texture's rows those its vertices' v reach (a sprite turned a quarter a little further, since the review), when
   nothing repeats round. Midnight Club 3 drew 247,000 pixels a frame by themselves (a 512-row texture whose picture
   is a frame buffer's 272, a render target narrower than its scissor); 22,000 now, all drawing over their own
   textures.
5. **A leaner pixel pipeline, texture function and filter** (pixel.cpp, texture.cpp): the frame buffer read and
   written at its own size, the blend factors and the texture function chosen once a pixel instead of once a channel,
   the filter's four channels blended side by side in 16 bits each of one 64-bit number (none can spill into the
   next), divisions of numbers never below zero by powers of two as shifts, all inline in the loops; a 3D sprite's
   perspective worked out once a column and once a row, one division left a pixel.
6. **Drawing goes on while the CPU runs** (threads.cpp, memory.hpp): a batch, once done (the list stops, or the
   render target changes), is drawn by the workers while the GE's thread goes on, setting up the next batch in the
   other of two (drawn after the first, in order), or running the CPU. Nobody sees it half drawn: `Memory::pointer()`
   (behind every read, write and copy, the screen's picture among them), states and power wait for it when they
   touch the VRAM pages it draws over or reads, and the CPU's owner takes those pages out of the CPU's page tables
   meanwhile (`Memory::vramGuard`), so compiled loads and stores go through `pointer()` too. The next list, and
   whatever the GE's own thread reads from those pages (a texture, a palette, vertices, the list), wait as well. A
   machine without a guard draws a list's batches before `run()` returns, as before.
7. **Setting up for less** (vertex.cpp, display.cpp): a vertex read through one host pointer when its bytes lie side
   by side, its vertices kept in one vector, and the screen's picture read a row at a time.

**Measured** on the host (Apple M1, 4 fast and 4 slow cores), the core built with the Android build's flags,
300 frames from a state in each scene (scratch runner and states in `/tmp`, never committed), host frames a second:

| scene | before | after: 1 thread | after: 7 threads | speedup |
| --- | --- | --- | --- | --- |
| GTA Liberty City Stories, in the city | 20.4 | 33.5 | 83.4 | 4.1x |
| Peace Walker's title | 13.5 | 29.0 | 129.8 | 9.6x |
| Lumines' demo | 31.3 | 57.7 | 192.8 | 6.2x |
| Burnout Legends, racing | 26.8 | 42.2 | 85.9 | 3.2x |
| Midnight Club 3, racing | 6.2 | 11.4 | 27.9 | 4.5x |
| Gunhound EX, in a mission | 123.2 | 167.4 | 229.1 | 1.9x |

Before and after were run in turn, best of three, picture hashes only (so drawing may go on across frames, as in the
app). By step (a busy machine, other work sharing it, so within about 10%): textures kept decoded took GTA from 19.7
to 25.2, Peace Walker 12.9 to 17.9, Lumines 29.4 to 42.4, Burnout 25.9 to 32.4, Midnight Club 5.8 to 7.8; jobs, to
28.9, 20.6, 47.0, 35.6, 9.2 (Gunhound 113 to 146); threads (7), to 68.9, 72.3, 158.7, 73.0, 20.8 (Gunhound 211), and
with 1, 2 and 4 threads GTA drew 28.0, 39.9 and 63.4, Peace Walker 21.4, 39.9 and 59.7, Midnight Club 9.6, 14.4 and
18.4; exact 2D regions, Midnight Club 20.4 to 22.5; the leaner pipeline, single-threaded, GTA 28.8 to 31.6, Peace
Walker 21.0 to 27.7, Lumines 49.2 to 54.4, Burnout 36.9 to 39.2, Midnight Club 9.9 to 10.6; drawing while the CPU
runs, Peace Walker 108 to 131, Lumines 173 to 181. In a 4-core Linux arm64 container (Docker on the same M1), the
core built for Android with the NDK and run as a static executable: Peace Walker 27.7 frames a second with 1 thread,
73.0 with 3; GTA 33.8 and 62.8.

**How it's known to draw the same pixels:**

- The six scenes record a hash of every frame's picture and of all of VRAM, and of RAM at the end; with 1, 2, 4, 7
  and 8 threads, and on the interpreter, every hash is the old core's (and with the review's fixes; the edge rule
  rewritten since changes a few pixels of slivers in three of them, as told at the end).
- A differential fuzzer (scratch, never committed) links the old GE beside the new and feeds both the same random
  display lists into identical memories: every pixel pipeline setting, every texture format with palettes,
  swizzling, filtering, wrapping, both texture coordinate kinds, lighting, morphing and skinning, 3D with clipping,
  points, sprites and triangles, render to texture and drawing over its own texture, transfers, palettes loaded
  from VRAM, writes between lists; then compares every byte of VRAM and RAM. Thousands of cases at a time, at 1, 4
  and 8 threads, batches shared however small and drawn past the list's end; with the address, undefined-behavior
  and thread sanitizers. Deliberately broken versions (a texture function off by one for one input, a triangle edge
  rule changed for one sixteenth, invalidation missing, the feedback check missing, a band's rows off by one...)
  failed it within seconds.
- The parts' tests (213 groups, the measured comparisons with the owner's PSP among them, unchanged) with the
  address and undefined-behavior sanitizers, and under ThreadSanitizer; the ares system's 254 checks.
- New groups: "draw textures kept decoded" (a texture drawn again after its memory changes, by the CPU's compiled and
  interpreted stores, after the recompiler starts afresh and while compiled code keeps running, by a write, copy and
  fill from outside the CPU, the GE drawing into it, a block transfer; its palette changing and coming back; a state
  loaded; drawing over its own texture; a 512-row texture used for its first 16 kept decoded, and read as drawn where
  the sprite draws over the rows it reads); "ge drawn on several threads" (a frame's worth of primitives with render
  to texture, a transfer, a palette and a texture of pixels just drawn, render targets a row apart, drawing over its
  own texture and a 16-bit frame buffer, the same at 1, 2, 4 and 8 threads; stopped part way, the CPU reading what's
  drawn and a state carried on in another machine; the CPU's compiled store and load landing after drawing still
  going on). Broken versions each failed them: compiled stores reaching a watched page, a fresh recompiler making
  watched pages writable, writes not heard, a loaded state's memory taken for the old, feedback not seen, palette
  changes not seen, the row limit gone; the list's end not waiting, transfers, palettes and textures before what they
  read, another render target in a batch, bands not waited for, a band's rows off by one, `pointer()` not waiting,
  the page tables keeping VRAM.

**The arithmetic's own platform dependence**, found on the way: the GE's float sums, such as a triangle's blended
colors `(a * w0 + b * w1 + c * w2) / total`, are fused into multiply-adds by clang on ARM64 (Apple's and the NDK's
alike), but not on x86-64 without FMA, so the two kinds of host already round some pixels differently. The rewrite
keeps every float expression's form (only whole values hoisted, no product split from its sum), so each compiler
fuses exactly as before; the integer arithmetic (filter, texture function, blending, fog) is free to be rewritten,
and was. ARM64, the RP6 and the host Mac, is the reference.

**Peace Walker's stripes on the handheld** don't come from the core's arithmetic under the device's compiler: the
core built with the NDK's clang (r28.2, clang 19, as AGP 9.3's default NDK) and the Android flags, the "modern"
flavor's `-march=armv8.2-a+fp16+dotprod` included, as a static Android executable run in a Linux arm64 container,
draws every frame identical to the host's, 300 from the title's state and 1801 from boot (RAM at the end differs
between any two boots, the host's clock in it, and between two host boots alike). Under the undefined-behavior
sanitizer, at `-O3`, the six scenes run with nothing reported in the core (libchdr's bit reader shifts into an int's
sign bit, reported, and outside it). So the stripes come after the core: how the front end presents the picture
(the title is a picture of one-pixel horizontal lines, which scaling 272 rows to the screen's 1080, near 3.97 times,
turns into uneven bands), a frame handed over while still being written, or a state the device reached that these
runs don't. Since then, on the RP6, it was the presentation: see "The review, and since".

**The review, and since.** A general-purpose reviewer of the branch: the clean-room spot check found the new code
original; one medium and four low findings, all fixed, each with a test that failed before its fix; and a function
from before this part, found to mirror an older open-source rasterizer almost token for token, rewritten clean-room.

- Medium: a sprite turned a quarter (corners bottom-left and top-right, so v runs across x) whose left edge is 9/16
  into its first column takes that column's v at its middle, a sixteenth of a pixel left of the left corner: a
  sixteenth of a pixel's step past the corner's v. The rows a 2D primitive reaches (draw.cpp) took the vertices' v
  alone, so such a column could take texels from rows the decoded copy didn't hold: below 0, repeated round to the
  last row (the reviewer's 16x256 texture, corners (0.5625, 8, v 0) and (8.5625, 0, v 8): the address sanitizer saw
  a read 15 KB past a 1 KiB copy, and 24 pixels came out unlike memory's), or more than the two rows' margin past
  the highest v when v falls over 32 texels a pixel to the right. Each turned pair now widens the reach by |dv| over
  the corners' distance in sixteenths (and a 65536th of a texel for rounding), and fetch() asserts in debug builds
  that no row past a primitive's reach is taken from its decoded copy. New groups: "draw turned sprites kept
  decoded" (the reviewer's three cases and a steep one, against memory), and "draw textures kept decoded against
  memory", 4000 random 2D primitives (sprites upright, turned and mirrored, triangles, strips, fans and points, their
  corners on any sixteenth and often 9/16 in; every texture format but DXT, with palettes, swizzled or not, up to 512
  rows, repeating or clamped, filtered or not, coordinates inside, on texel boundaries, outside and steep; textures in
  VRAM where the primitives draw, and drawn again after their memory changed) drawn by a machine keeping textures
  decoded and by one without `watching()`, which decodes nothing, pixel for pixel. Comparing thread counts couldn't
  catch this: every count draws from the same copy. Before the fix it found 249 of the 4000 apart; it also failed
  broken versions (`drawsOver()` never true, invalidation missing, the triangles' margin of two rows dropped, the
  turned widening dropped), and 800,000 cases on sixteen more seeds pass.
- Low: the screen's picture (display.cpp) formed `bytesOf + x * bytes` before testing bytesOf, which is null for a
  frame buffer seen through VRAM's second copy: null plus an offset, undefined behavior. The pointer is now formed
  only where there is one ("display picture" shows such a frame buffer).
- Low: the decoded textures' key held the rows reached, so each count decoded the texture again from its first row
  and was kept beside the others, and going over the budget scanned every copy. Now a texture is kept once, as many
  rows as any primitive has reached: one reaching fewer draws from it as it is, one reaching more gets a longer copy
  (the rows kept already copied into it, the rest decoded); a write to any page it came from drops it, as before; a
  list, the last used first, gives the one unused longest ("draw textures kept decoded, their rows").
- Low: "GE Threads" was taken as given (1000 made 1000 threads; 2^32 + 1 narrowed to 1). It's held to twice the
  host's cores (64 where it can't tell) and to 64 now; 0 is still all the cores but one (tests/psp/ares: "the GE's
  drawing threads").
- Low: Decisions had the renderers' decision twice (one kept), and texture.cpp said a primitive drawing over its
  own texture sees it change as it draws "as on the PSP", which wasn't measured (it says what's known now).

**Which pixels on a triangle's edges it draws, rewritten clean-room** (raster.cpp). A triangle covers the pixels at
whose middles each edge's function is at least its least: the functions are whole numbers there, so the test is
exact; and a middle exactly on an edge goes, by the usual top-left convention, to the triangle whose left edge it is
(its inside to the edge's right) or, for a level edge, whose top edge it is (its inside below): least 0 there, 1 on
right and bottom edges, which the triangle on the other side draws. The edge's direction alone decides, with no
division. The old code worked out where the edge was at the third corner's height with a division cut short toward
zero, and for a sliver whose third corner lay less than a sixteenth of a pixel left of an edge running down to the
right, it took that right edge for a left one and drew its pixels too (never fewer). Before and after: the
comparison with the owner's PSP is the same, line for line, in all 87 files (coverage-triangles, shared-edges,
3d-rules, 3d-cull among them), and every draw and draw3d group passes, so the measurements don't tell the two apart.
The six scenes: Peace Walker, Lumines and Gunhound identical; GTA Liberty City Stories differs in 24 of its 300
frames, Burnout Legends in 54, Midnight Club 3 in 38, each frame by itself (the next ones the same again) and by 1
to 4 pixels (sampled: 1 to 3 by up to 2 levels in GTA, 1 by 28 and by 42 in Burnout, 2 to 4 by up to 8 in Midnight
Club): pixels exactly on slivers' right edges, which the old code drew as well as the neighbour did. A new case in
"draw triangles", a sliver sharing an edge with a wider triangle to its right, the edge through a pixel's middle and
the sliver's third corner half a sixteenth left of it, draws that pixel once; the old code drew it twice. The
measurements show the convention for ordinary triangles (each pixel of a shared edge drawn once, as shared-edges
has it), and only the exact test keeps it for slivers, so it's kept. Where slivers meet on a PSP isn't measured.

**Merged with part 23** (the fonts, `cursor/psp-fonts-2b67`), which now sits underneath. Only system.cpp's
options and the docs clashed; the state's layout changed on the fonts' side alone, so it stays version 8.

**In the app**: Settings, Emulation, Performance, "PSP Drawing Threads": Auto (all the device's cores but one, the
owner's default), 1, 2, 4, 6 or 8, handed to the core as "GE Threads" as a game loads (`PspDrawingThreads` in
util/PspVideo.kt, PhobosRunner.cpp); the log says what it handed over.

**Peace Walker's stripes were the presentation.** On the RP6 the app handed Android each 480x272 frame as a window
buffer of that size, and the compositor scaled it to the view, 1906x1080 sideways (3.97 times), bilinearly: the
screenshot's rows step smoothly from one to the next, where nearest-neighbour would repeat each row 3 or 4 times.
Each of the title's one-pixel lines became a soft band, its strength drifting as its middle fell on or between the
screen's rows, which looked striped. The PSP's picture now goes into the window at the whole multiple of its size
nearest the view's, each pixel repeated (4 times sideways: 1920x1088, which the compositor makes 0.993 as big; 2
times upright; with the aspect setting "Integer Scaled", exactly the view's size), so the compositor's own scaling is
slight: each line is 3 even rows and a blended one, sharp, all down the screen ("sharp bilinear"; util/PspVideo.kt's
`pictureMultiple()`, and PhobosRunner's `video()` for PSP frames alone). "Integer Scaled" (the pause menu's aspect
setting) gives even lines too, 3 times as big (1440x816) and smaller on the screen. Screenshots before and after,
outside the repository: `/tmp/gefix-device/pw-before.png`, `pw-after-25s.png` (the same moment of the title) and
their crops in `crops/`.

**On the RP6** (installed over the app, data kept; each game launched from its CHD, a screenshot of the performance
overlay after about 40 seconds, then stopped), frames a second before (the fonts' build, without this part) and
after: Peace Walker's title 10.6 -> 60.0 (also 60 at 15, 25 and 35 seconds), GTA Liberty City Stories in the city
16.6 -> 60.0, Lumines' demo 24.0 -> 59.9, Burnout Legends' title 60 -> 60 (it stays on its title without a press, so
it says little). Before tonight's work: GTA 17.7 and Peace Walker 9.5 (this part's opening), Lumines about 40.

Checks: tests/psp 226 groups with the address and undefined-behavior sanitizers (the comparison with the owner's PSP
the same, line for line), and under ThreadSanitizer, nothing reported; tests/psp/ares 274 checks; the app's 262
unit tests, and its release build.

**What's still slow, and next:**

- The GE's thread: setting primitives up (vertices, transforms, clipping, jobs) and the CPU's own work are now the
  longest path in GTA and Burnout; a batch that needs the last batch's picture (post effects) waits for it. Next:
  cheaper setup (vertices decoded a batch at a time, jobs smaller), and drawing a batch's bands as its primitives
  arrive instead of when it's done.
- The pixel loops: a triangle's color, depth, fog and perspective (several divisions a pixel) and the filter work a
  pixel at a time; four pixels at a time with SIMD (the same per-lane arithmetic) is next for Peace Walker, Midnight
  Club and Lumines, which are bound by drawing.
- Drawing over its own texture (Midnight Club's 22,000 pixels a frame, some post effects) stays one thread, in order.
- The Vulkan and OpenGL renderers (the owner's decision), measured against this one.
