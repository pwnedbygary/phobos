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
`cursor/psp-hle-games3-2b67`; part 24, making the GE fast, is on `cursor/psp-ge-speed-2b67`, on top of part 22's
too, with part 23 merged in. Part 25, the games further still (Burnout Dominator's GE hang explained, movies fed and
taken apart so that Space Invaders Extreme plays), is on `cursor/psp-hle-games4-2b67`, part 24's with part 23's
merged in. Part 26, music and movies (FFmpeg's LGPL decoders under sceAtrac3plus, sceMp3 and sceMpeg), is on
`cursor/psp-codecs-2b67`, on top of #152 (`cursor/psp-test-data-2b67`: part 25's, with master merged in). Part 27,
the PSP in the desktop program (its library, settings, controls and picture, and FFmpeg built for Linux, macOS and
Windows), is on `cursor/psp-desktop-2b67`, on top of part 26's. Part 28, the functions 266 games ask for (kernel
mutexes, alarms, virtual timers, and the clock kept as the PSP keeps it in system calls), is on
`cursor/psp-hle-games5-2b67`, on top of part 27's. Part 29, the GE features games are missing (lines, bounding
boxes, compressed DXT textures, sceGeBreak), is on `cursor/psp-ge-features-2b67`, on top of part 28's. Part 30, the
GE faster again (four pixels at a time, every pixel the same), is on `cursor/psp-ge-speed2-2b67`, on top of part 29's.
Part 31, movies through scePsmf and scePsmfPlayer, written in a clean room, is on `cursor/psp-psmf-2b67`,
on top of part 30's.
Part 32, the next functions games stop at (a thread's run status, the thread manager's ID lists, the memory
stick's free space told alike everywhere, and part 28's differences the recordings settle), is on
`cursor/psp-hle-games6-2b67`, on top of part 31's.
Part 33, curved surfaces (BEZIER and SPLINE), is on `cursor/psp-ge-curves-2b67`, on top of part 32's.
Part 37, the stuck games (a boot program unloading itself, the gamedata install dialog, the drive's callback, depths
kept at one depth, points past z / w ±1) and the runner's presses, is on `cursor/psp-hle-games7-2b67`, on top of
part 33's.
Part 42, Killzone's movie (a game's own sceMpeg library gives each picture at once) and the dialogs' sizes and
statuses (with sceUtilityMsgDialogAbort), is on `cursor/psp-hle-games8-2b67`, on top of part 37's.

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
- **The GPU renderers' direction** (2026-10-07, part 36): the exact compute renderer (parts 34-35) stopped; instead
  a hardware renderer as PPSSPP has one (the GPU's rasterizer, texture units and blending, shaders from the GE's
  state, upscaling), Vulkan first, then OpenGL. The software renderer stays the exact one and the default. PPSSPP's
  GPU backends may be read for their architecture, never copied or translated, and the docs say so; the HLE
  kernel's clean room is unchanged. The PSP renderer takes Vulkan from the host's `loadVulkan` (the system's driver,
  or a custom one through libadrenotools), with its own function tables, leaving paraLLEl-RDP's volk alone.

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
- **FFmpeg** (<https://ffmpeg.org>, LGPL 2.1): its decoders, as shared libraries built from its release, decode the
  games' music and movies (part 26); its API documentation is the source for using them.

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
rounds 2 and 3). The recompiler does the common instructions with the prefixes at rest through helpers with the
interpreter's arithmetic, and some natively (`recompiler-vfpu.cpp`, part 38's item 6), and hands the rest to it.

The random number generator is the hardware's, as fp64 worked it out from a PSP's output (PPSSPP issue 16946):
a linear congruential generator, a xorshift and a Pell-like sequence with a carry, added together, their state
packed into the eight RCX registers (`vfpuRandom()`); `vrnds` spreads its seed over them; `vrndi` and `vrndf`
fill lanes from the last back, and their destination prefix only reaches the last lane. Implemented from that
description (no PPSSPP code), and checked against the user's PSP: its state at power on and the numbers of 64
seeds all match, including those that exercise the carry (psp-vfpu-measurements.md).

The math functions `vrcp`, `vnrcp`, `vrsq`, `vsqrt`, `vexp2`, `vrexp2`, `vsin`, `vcos`, `vnsin`, `vasin` and `vlog2`
(and `vrot`'s sine and cosine) are the PSP's own: its quadratic interpolator with coefficients fitted from our
measurements (`vfpu-segments.hpp`, from `tools/psp-measure/fit.py`), exact on every measured input; see
psp-vfpu-measurements.md. `vlog2` works in fixed point from the same table, truncating to a step that grows with the
result's size and cutting coefficients to match above 4 (and below 1). The adders (`vdot`, `vhdp`, `vfad`, `vavg`,
`vdet`, `vcrsp`, `vqmul`, and the matrix products) share one circuit (`vfpuDot`, the recompiler's `vdot` too):
products with two extra bits and round-to-odd, aligned and truncated, then a nearest-even sum whose carry raises the
exponent (without it a sum rounding up to a power of two came out half of it, 1/2 for 1, and GTA: Liberty City
Stories' characters twitched).

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
  16 KiB into the user partition (its block from the partition's start: part 51), reserves the program's memory
  exactly where it is, one block from its first segment to the end of its last as the PSP's loader gives a module
  (two segments may share a 256-byte step, as a retail PRX's data starts right after its code; a program whose
  segments overlap is refused), and starts its first thread at the entry
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

**And fog's amount** is fixed at each corner before blending (see Part 11): `ramp-fog` from 18976 pixels a level
apart to 240 (the same scattered shortfalls color ramps still show); round 2's `3d-floor-fog` from 6257 to 497.
Stepping u/w and 1/w like 2D's shortStep made `3d-wall-texels` worse (307 → 2423), so perspective texels stay as
the edge weights have them. Color ramps and those last lighting products are still open (round 5's probes).

**And colors, fog and depth are stepped** from a corner, as the PSP steps them (part 48): round 3's color and fog
ramps and `bezier-flat`, and round 2's `3d-clip` (with the cut's four corners split the PSP's way), `3d-floor-fog`
and `3d-floor-depth`, are now identical. Lighting's last products and perspective texels stay open: part 48 says what
the data show of them, and round 5 measures them.

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
  256ths, which the way round decides: the PSP's way, measured later) and put on the screen again; four corners left
  (two kept, two cuts, in the order of a walk round the edges) are drawn as corners 0, 1, 3 and 1, 2, 3, the PSP's
  split where the third corner is past the plane, the one case measured (part 48); with flat shading every piece
  keeps the last vertex's color.
- **Culling** (CULL_FACE_ENABLE, not in clear mode, through mode too): CULL 1 draws the triangles running clockwise on
  the screen, 0 those running counterclockwise; every other triangle of a strip counts the other way round.
- **Texture coordinates**: perspective-correct across triangles (blended as u/w and 1/w, then divided); colors and
  depth aren't: they're stepped from a corner, straight across the screen (part 48). Mode 0 takes the vertex's, times
  TEX_SCALE plus TEX_OFFSET; mode 1 the texture matrix's result from the position, the texture coordinates or the
  normal, its q dividing at each pixel.
- **Fog**: each vertex's (view z + FOG1) × FOG2, turned into 0-255 there (rounded down, 1 or more giving 255), then
  that amount stepped across the primitive as a color channel is (measured: round 3's `ramp-fog`, and part 48;
  blending the 0-1 and converting at each pixel left the first pixel of a near-to-far ramp unfogged). Mixed in after
  the alpha test as (color × f + fog color × (255 − f) + 255) / 256. A FOG1 or FOG2 that isn't a number to a float is
  a huge number to the GE.
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
  products still come out a level lower on the PSP: part 48 found the share finer than 256ths, the GE's own cosine,
  which round 5 measures). "To the power of" is the GE's quick approximation (exact at powers of two, a little low
  between them), and the coefficient keeps only the top four bits of its fraction. Each channel ends held to 0-255.
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
  after getting what it asked for, and sceDisplayWaitVblankCB's 1 would become a wait for the next blank). A
  semaphore's CB wait with no timeout and nobody else in line runs them first instead, in its wait, and takes the
  count after them if it's still there, ahead of any thread that queued meanwhile (part 51: Patapon 2's callback
  takes that semaphore itself); a callback deleting the semaphore ends the wait as deleted.
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
for (here, after, but before for a semaphore's with no timeout and nobody else in line: part 51). For sound and
interrupts: the 64-sample pacing (K outputs taking K - 2 blocks) follows from the measured block model, but only its
first two outputs were timed on a PSP (intr/waits); which of a mixer output's volume and channel checks comes first, and
an SRC output's volume and reservation; a null SRC output on two armed buffers (BUSY here; intr/waits' later results
show only that it doesn't wait for them); the volumes after reserving (0) and the volume scale (0x8000 full, pspsdk's
PSP_AUDIO_VOLUME_MAX) for when the samples are mixed; how long a null buffer's count stays; the interrupt kinds,
recorded once under PSPLink; and which of a bad priority and a bad thread sceKernelChangeThreadPriority checks first.
Other waits (sceKernelDelayThread and the rest) don't refuse yet with interrupts held off, as intr/waits shows a PSP
does: only sound's do. Since the final review: the SRC channel's 100 µs (the specification's "about 100 µs", from a
result bucketed as "13XX"), and whether the buffers chained after the first keep it (here they do, so a stream's pace is
a buffer's length); and what sceKernelFreePartitionMemory says of a block the kernel holds (ILLEGAL_PERMISSION here, not
tried on a PSP).

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

## Part 25: the games, further still

On branch `cursor/psp-hle-games4-2b67`: part 24's GE made fast (`cursor/psp-ge-speed-2b67`) with part 23's fonts
(`cursor/psp-fonts-2b67`) merged in, both sides kept (the state version 8, only the fonts having changed the
layout). The owner's priority (2026-10-06): the games that were stuck, further. Sources as before: pspsdk's headers,
pspautotests' programs and the results they recorded on a PSP (here video/mpeg's movie, `test.pmf`, frame by
frame), the PSP Developer Wiki, and the games' own behavior; no other emulator's code was read. The scratch host
runner (never committed) gained a trace of the GE's commands (a hook in a scratch copy of the tree), the kernel's
calls filtered by name, and builds with one function taken out, to show what its absence did.

- **Burnout Dominator's GE hang wasn't the GE's.** Part 22's runner had resumed the game from a state saved before
  sceKernelVolatileMemLock existed. At frame 3908, as its first race loads, the game asks for the volatile memory
  (the 4 MiB at 0x08400000) to hold the race; the kernel, not having the function, answered NOT_YET_LINKED and wrote
  no address, and the game laid the race's buffers out from the address it never got. Textures were then read
  without their top bits (0x00f79aac, where nothing is), and the race's data overlapped its display lists: by the
  time the GE ran list 0x09e05900 (frame 5496 on), its memory held 16-byte records (vertex data by the look of
  them, an ORIGIN command every fourth word), which the GE ran as NOPs and ORIGINs until it reached the tail of a
  real list, whose JUMP, made relative by the last ORIGIN, sent it to 0x03c0de64, where it ran zeros until each
  frame's million commands were spent, the game waiting in sceGeDrawSync. The lists walked by hand ended cleanly
  because the GE didn't read what they had been: by the time it ran them, the race's data was written over them
  (at frame 5300 the same list still ran its 595 commands and ended). Shown with a build that has the function
  taken out (and a trace of the GE's commands): from a state at frame 2999, with the same presses, it notes the
  function missing at frame 3908, loads from 0x00f79aac and its neighbours, and from frame 5496 the GE takes that
  path every frame, the screen black; the same build with the function (part 22's) races (timer running, 113 to 139
  mph). Nothing in the GE was at fault: an ORIGIN does make a JUMP relative. With this branch the first race plays
  from boot (frames 6300 to 15000, its timer from 122 seconds down to 47). New group "power volatile memory locked
  at once" locks the memory as Dominator does (no test had locked it with sceKernelVolatileMemLock while it was
  free: TryLock, and threads waiting while another held it, were).
- **Space Invaders Extreme stopped at "NOW LOADING"** (Arcade Mode) because of its stage's background movie. It
  keeps the movie in memory and plays it on a thread of the best priority (0x11): the game ignores part 20's refused
  header, makes the thread's event flag with the request already set, and the thread asks for an access unit and,
  while there's none, feeds the ringbuffer and asks again, never waiting (its callback copies packets from memory,
  starting over at the movie's end). With nothing ever put in, it kept every other thread from running for good.
  On a PSP the access units come (and decoding one waits a moment: basic's decodes let another thread run), and the
  thread then waits for the next request. The same loop would have hung the game at the stage's end too: stopping
  the movie sets the flag's quit bit and waits for the thread to end, which only the flag's wait sees. And no
  answer but "no data" could end the loop without breaking Burnout Legends, which asks again at once, for good,
  after any other error.
- **So movies are fed and taken apart now, but not shown** (mpeg.cpp), as video/mpeg/basic recorded:
  sceMpegRingbufferPut calls the ringbuffer's callback on the calling thread (where the packets go, how many, its
  argument), a run at a time that stops at the ring's end, and again for what's left as long as it gives some (basic's
  gave 9 of 24 at its file's end and was asked for the other 15); the callback may wait (basic's read a file), and a
  state saved meanwhile carries it (the trampoline's seventh syscall brings each return back). sceMpegGetAvcAu reads
  the packets as MPEG-2 program stream packs and takes the video of stream 0xe0 apart into access units, from one
  access unit delimiter to the next, each coming once the next one's delimiter is in (or, the file having ended,
  with its data): its size, the time stamps of a PES packet that begins with it (else -1), and the packets whose
  video it took the last of free again. Decoding one, at once, gives a picture from the second on, the decoder
  holding one back, and draws nothing. Run through the HLE with basic's own movie, as basic fed it (a scratch
  harness), all 180 frames came out as recorded: the free packets before and after each feeding, every access
  unit's size and time stamps, the pictures, and the callback's 181 calls. sceMpegRingbufferAvailableSize counts
  the free packets, those holding no data (ringbuffer/avail), sceMpegInitAu sets an access unit's buffer and clears
  the rest (basic's ATRAC3plus one), and sceMpegAvcDecodeDetail (named by pspautotests' imports, nothing of it
  recorded) answers 0 and gives the newest picture's width and height at 8 and 12, the rest of the details left as
  they were (part 51: Spectral Souls draws its movies by them): Space Invaders Extreme waits for its movie's first
  picture, and asks it of each, before the stage starts. The library's own state (the video taken from the ring's
  first packet, the picture held back, the file having ended) is kept in the memory the game gave it, where Sony's
  library keeps its own.
- **The ring as games keep it**: the callback runs with the global pointer of Put's caller. pspautotests'
  SceMpegRingbuffer2 keeps the one Construct found at offset 44, but pspsdk's SceMpegRingbuffer is 44 bytes, the
  word after it the game's own (in every case seen, the word at 44 and the caller's are the same). And a ring whose
  fields, the game's to write, couldn't be a ring's (no packets or more than 4096, the next to write not among them,
  more holding data than there are) is given nothing, as sceMpegGetAvcAu takes nothing from one.
- **Who it changes**: only games that play on after the refused header and whose callback gives packets. The GTAs,
  Midnight Club 3 and Snoopy give their movies up at the header as before; Burnout Legends and Dominator play on,
  but their callbacks give nothing (they set their file up only once the header is read), so their movies end at
  once as before; Gunhound EX, Brave Story and Lumines ask for none in their first minute. Space Invaders Extreme's
  movie now plays, unseen, behind its stages.
- **States**: version 9, a ringbuffer's callback part way through being part of a state (a state of version 8
  refused); loading checks such a call: a thread that's there, a run of 1 to what's left, no more than a ring's 4096
  packets, a ringbuffer in memory.

What the games do now, on the host (frames under `/tmp/games4-runner/out`, outside the repository; the runner's
states under `/tmp/games4-runner/states`):

- **Burnout Dominator**: from boot through its profile and main menu to its first race, racing it (`dom-c`, frames
  6300 to 15000, the timer running down from 122 seconds to 47, 82 to 139 mph).
- **Metal Gear Solid Peace Walker**: past its name, its button configuration and its install ("Installing...",
  which finishes), the optional DATA INSTALL (answered NO), the autosave notice, to its first scene on the beach and
  the training that follows: Snake under the player's control ("Free Control Time", "Camera Controls", `pw-f`,
  `pw-g`).
- **GTA Liberty City Stories**: past its opening into its first mission: Toni driving Vincenzo to the safehouse
  through the woods and the town's edge, the radar and blips showing (`lcs-e`).
- **GTA Vice City Stories**: past its opening to Vic at the army base, walking under the player's control (`vcs-d`).
- **GTA Sindacco Chronicles**: past its difficulty choice and opening into Paulie's car, driven toward Atlantic
  Quays (`sind-d`).
- **Burnout Legends**: racing, lap 2 of 3, takedowns (`burnout-b`). **Midnight Club 3**: racing through the city at
  night, the timer running (`mc3-a`). **SOCOM**: a campaign begun, saved, its first mission's briefing, deployed: the
  soldier in the Andes, help tips showing (`socom-c`, `socom-d`). **Snoopy vs. the Red Baron**: Marcie's flying
  lessons over the town, the radar (`snoopy-b`). **Gunhound EX**: Mission 01 under way, the mech firing, its
  dialogue (`gun-b`). **Brave Story**: the prologue's scenes and talk at the fountain (`brave-b`). **Lumines**:
  Challenge Mode played (`lumines-c`). **Space Invaders Extreme**: Arcade Mode's first stage played, invaders shot
  (score 400), lives lost, its background black (`invaders-e`); then the pause menu's END GAME back to its title,
  the movie stopped on the way (its thread told to end, and ended), which the old loop couldn't have (`invaders-h`).

**Speed in play** (the host Mac, the scratch runner's `-O2` build, 600 frames from each game's state with a button
held, the GE on 7 threads / on 1): Gunhound EX 213 / 138 frames a second, Burnout Dominator racing 193 / 104, Brave
Story 168 / 94, Lumines 117 / 44, Burnout Legends racing 97 / 54, GTA Vice City Stories 86 / 40, Liberty City
Stories driving 78 / 31, Sindacco Chronicles 73 / 35, Snoopy flying 61 / 38, Peace Walker's training 60 / 33,
Midnight Club 3 racing at night 7.3 / 2.1. So all but Midnight Club 3 keep up with the PSP's 60 on several threads,
Peace Walker and Snoopy only just. Midnight Club 3 is bound by filling pixels: a profile (macOS `sample`, 10
seconds) puts nearly all the drawing threads' time in the triangle and sprite loops for its 32-bit framebuffer
(`triangleRows<3>`, `spriteRows<3>`), spread evenly over the seven; this race at night is heavier than the scene
part 24 timed (27.9). Part 24's next steps, four pixels at a time with SIMD and cheaper setup, are where its time
goes.

Seen on the way, not changed:
- **Lines** (PRIM 1 and 2) aren't drawn: Liberty City Stories (line strips, perhaps its radar's route), Space
  Invaders Extreme, SOCOM and Dominator draw some. There's no measurement of how the PSP rasterizes them, so none is
  drawn rather than a guess: one for the next measuring round.
- **Bounding boxes** (BBOX, BJUMP) all count as in sight: Dominator draws what a PSP would skip; only speed.
- **Dominator's race start** shows dark bands torn into strips for a moment: a bloom pass draws a framebuffer back
  onto itself, read as it's written; a PSP's order of reads and writes there isn't measured.
- sceKernelDevkitVersion (SysMemUserForUser 0x3fc9ae6a) isn't there: Peace Walker and the measuring program ask,
  and carry on. Peace Walker's YES to the optional install (320 or 880 MB) wasn't followed through.
- Movies: sceMpegQueryStreamOffset still refuses every header (games that check skip their movies); the sound's
  access units never come; sceMpegAvcDecodeStop doesn't give back the picture the decoder held.

Tests (`tests/psp/run-tests.sh`: 236 groups, both sanitizers; `tests/psp/ares` 278 checks):
- `media.cpp`: "mpeg movie fed and taken apart" (both engines: a made-up PSMF movie of twelve access units, three
  with time stamps, one past 32 bits, through a ring of 16 packets fed 3 at a time, against the rules basic recorded:
  the callback's calls, the free packets, each access unit's size and time stamps, the pictures, the last access
  unit once the file has ended, the ring empty at the end; the state round trip); "mpeg movie thread waits for its
  picture" (both engines: Space Invaders Extreme's movie thread and its main thread waiting for the first picture,
  then stopping it; with nothing put in, main never ran); "mpeg ringbuffer callback states" (both engines: a state
  saved at three moments while the callback waits, as a read would, carries on to the same frames and calls).
- `power.cpp`: "power volatile memory locked at once" (both engines).
- `states.cpp`: "state fields" changes each field of a ringbuffer's callback part way through, and refuses one of a
  thread that isn't there, one asking for nothing or for more than it has left, one past a ring's packets, and one
  feeding a ringbuffer nowhere.
- `tests/psp/ares`: a state of version 8 refused, as the seven before it.

Review: a general-purpose reviewer of the branch; the clean-room check found the mpeg code independent; four low
findings, all fixed, each with a test that failed before its fix. sceMpegRingbufferPut trusted the ring's fields,
the game's to write: 0x7fffffff packets with 0x80000000 holding data overflowed its signed subtraction, and 8192
packets, or -200 holding data, had the callback asked for 5000 or 4296 packets, more than a state holds, so a state
saved as it waited was refused by a fresh machine (and a run's size in bytes could wrap). Such a ring is given
nothing now (above), what's free counted unsigned; "mpeg ringbuffer that isn't one given nothing" saves each such
ring's machine where its callback would have waited and loads it in a fresh one. The callback's global pointer came
from the ring's offset 44, past pspsdk's 44-byte SceMpegRingbuffer: it's the caller's now ("mpeg ringbuffer callback
with the caller's global pointer": someone else's word after a 44-byte ring). Groups were missing for a callback
that returns more than it was asked for ("mpeg ringbuffer callback giving more than asked": what it was asked for
counts), one that returns an error ("mpeg ringbuffer callback returning an error": Put returns what the calls before
it gave, as after a callback that gives none; what a PSP's Put returns then isn't recorded), a feeder thread
terminated as it sleeps in its callback ("mpeg ringbuffer feeder terminated in its callback", a state saved as it
sleeps carried on in a fresh machine), and random packs ("mpeg random packs taken apart": from a seed, the next
packet to read stays among the ring's, those holding data no more than it has, and Put gives what its callback's
calls gave). Taking out the clamp on what a callback returns, the check for none or an error, or endThread's or
deleteThread's dropping of a thread's feeding fails one of them (deleteThread's only through a state the loader
takes in which a dormant thread still has a feeding: every other thread is ended before it's deleted). And the
counts given here and in the handoff (227 groups, 268 checks) were out of date. The tree passes 236 groups and 278
checks, the state layout unchanged (version 9).

## Part 26: music and movies

On branch `cursor/psp-codecs-2b67`, on top of #152 (`cursor/psp-test-data-2b67`, part 25's games with the test
programs and GE measurements in the repository, which carries master's desktop build and the GPL-3.0-or-later
license), PR #153. The owner's choice
(2026-10-06, night): the games' music (ATRAC3, ATRAC3plus, MP3) and movies (the PSP's H.264) through FFmpeg's LGPL
decoders, built in an LGPL-compliant way. Sources: pspsdk's headers (`pspatrac3.h`, `pspmp3.h`, `pspmpeg.h`),
pspautotests' audio/atrac, audio/mp3 and video/mpeg programs and the results they recorded on a PSP (the programs
themselves run through the HLE by the scratch runner, which answers their emulator devctl and compares their output
with the recordings), the PSP Developer Wiki, the RIFF WAVE and MPEG-2 program stream formats as publicly described,
FFmpeg's API documentation, and the games' own behavior; no other emulator's code was read.

**FFmpeg, built and licensed.** `thirdparty/ffmpeg/build.sh` builds FFmpeg 9.0.2, unmodified, from the official
release tarball (`https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz`, its SHA-256 checked:
`8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`). The script holds the whole configure line:
`--disable-gpl --disable-nonfree --disable-version3 --disable-programs --disable-doc --disable-everything
--disable-autodetect --disable-network`, no libavformat, libavfilter, libavdevice, libswscale or libswresample, and
nothing enabled but the decoders the libraries open, `atrac3`, `atrac3p`, `mp3float` and `h264`; no parsers or
demuxers (the libraries take the PSP's containers apart themselves, as part 25 does, and convert samples and pixels
themselves). The result is two shared libraries, libavcodec (63) and libavutil (61).
- Targets: `host` (the tests and runners: macOS here) and `android NDK API` (arm64-v8a with that NDK's clang, the
  app's minSdk 26, 16 KiB pages); `--name` before them prints only the build's name. Each build is kept under
  `.cache/ffmpeg` (or `$PHOBOS_FFMPEG_CACHE`), named by a hash of the script and the compiler, so it's made once. A
  lock lets the two flavors configure at once; it records its build's process and host, and a lock whose process is
  gone is taken over. The tarball is downloaded into a file of its own and kept only if its hash is right; a wrong
  one is thrown away and downloaded once more, and if that fails too the script says where to put the tarball by
  hand or how to build without FFmpeg. Offline, the tarball in the cache (or `$PHOBOS_FFMPEG_TARBALL`) is used. It
  needs bash, make, curl and xz, and the network the first time. Nothing of FFmpeg is committed.
- The app: the root `CMakeLists.txt` (option `PHOBOS_FFMPEG`, on) runs the script as it configures for Android,
  imports the two libraries, defines `ARES_ENABLE_FFMPEG` for the core (`phobos_core`, the static library both the
  app's and the desktop program's builds link) and links them into `phobos_android`; Gradle packs them into the APK
  beside Phobos's library (`lib/arm64-v8a/libavcodec.so`, `libavutil.so`), so `./gradlew :app:assembleModernRelease`
  builds FFmpeg the first time and reuses it after. `-Pphobos.ffmpeg=OFF` passes `-DPHOBOS_FFMPEG=OFF` through
  Gradle (the README's build section says so). Off, in a build without the define, or in the desktop program (built
  without FFmpeg for now), the core has no decoders and the libraries refuse streams as before this part.
- The tests: `tests/psp/run-tests.sh` and `tests/psp/ares/run-tests.sh` build the host FFmpeg and define
  `ARES_ENABLE_FFMPEG` unless `PSP_FFMPEG=0`. CI caches both builds, keyed on the build's own name (`--name`: the
  script and the NDK's revision, or the system and the compiler), so a runner's new compiler makes a new build that's
  kept in turn, and stages the tarball into `android/dist`, published beside the APKs.
- The LGPL: FFmpeg's libraries are linked dynamically and ship as separate files that can be replaced by a build of
  the user's own (same interface); their complete source (the tarball) goes out with every release, and the script
  says how they were built. `LICENSE` (the app's Settings, About, Open-source licenses) carries FFmpeg's notice ("This
  software uses libraries from the FFmpeg project under the LGPLv2.1"), who owns it, where its source comes from, the
  hash, the configure script, how to swap the libraries, the Independent JPEG Group's credit (libavcodec holds
  FFmpeg's versions of its DCTs, which H.264's error concealment pulls in; FFmpeg's `LICENSE.md` asks for it), and
  the LGPL 2.1's full text; the About screen says it too. `LicenseNoticesTest` checks the notice, the license text,
  the version and hash against the script, the IJG's credit, the decoders built, and that nothing turns GPL on.
- The APK: 23,604,722 bytes, against 22,574,705 for the same branch built with `-Pphobos.ffmpeg=OFF`: 1.03 MB (4.6%)
  more for FFmpeg's two libraries (2,507,776 bytes installed). (The first build, with the three decoders the review
  dropped, was 23,741,990, against 22,520,961 for part 25's.)

**The decoders** (`codec.cpp`): the libraries hand an `AudioDecoder` one frame (16-bit samples out, the channels
interleaved) and a `VideoDecoder` one access unit (a 4:2:0 picture out); the kernel's `audioDecoders` and
`videoDecoders` make them (FFmpeg's in builds with the define; tests put stand-ins in). One thread each, FFmpeg's log
silenced; float output rounded and clamped to 16 bits. A decoding call waits 300 microseconds of the PSP's time
(`Wait::Codec`, the call's result kept), as a PSP's thread waits for the Media Engine: other threads run meanwhile.
The time is chosen, not measured.

**sceAtrac3plus** (`atrac.cpp`, replacing part 20's refusals): ATRAC3 and ATRAC3plus in RIFF WAVE files (fmt with
the codec's parameters, fact's sample counts, smpl's loop), as pspsdk documents and audio/atrac recorded.
- IDs: six, ATRAC3plus 0-1 and ATRAC3 2-3 at first, shared again by sceAtracReinit (an ATRAC3plus ID taking room for
  two). A file is set whole (SetData), in a buffer it's loading into (SetHalfwayBuffer, filled by AddStreamData), or
  streamed through a smaller buffer as a ring of frames that the game refills as GetStreamDataInfo asks (the first
  time round from where the header leaves the frames, after that from the buffer's start, a frame cut by the end
  carried to the start); each with the mono-output (SetMOut) variants.
- Positions are on the decoder's count: the first sample worth hearing is the fact chunk's offset plus the codec's
  delay (368 samples for ATRAC3plus, 69 for ATRAC3), the loop's points smpl's plus the delay. Starting at a sample
  decodes first, unheard, the frame or two before it (priming); a loop's end plays its frame through, then jumps.
- DecodeData gives the samples (2048 or 1024 a frame, fewer for the first and last), whether it ended and the frames
  left (GetRemainFrame's numbers: -1 all in the buffer, -2 the rest of the stream in it, -3 the same with no loops
  left). Loops, the second buffer (what follows a loop that ends before the file does: IsSecondBufferNeeded,
  GetSecondBufferInfo, SetSecondBuffer), GetBufferInfoForResetting (both spellings, Reseting and Resetting) and
  ResetPlayPosition, GetSoundSample, GetNextSample, GetMaxSample, GetNextDecodePosition, GetChannel and
  GetOutputChannel, GetBitrate, GetLoopStatus, GetInternalErrorInfo, `_sceAtracGetContextAddress` (the contexts'
  fields where pspautotests' atrac.h reads them), and the errors pspautotests recorded.
- Run against the recordings: ids, setdata, decode, headerfields, c0mono, addstreamdata, replay and atractest match
  line for line. stream differs in 85 of 10,172 lines (a PSP zeroes the header after a halfway stream is set; a second
  buffer's size lingers in its context from one ID to the next); getremainframe, getsoundsample and second's getinfo,
  needed and setbuffer only where the test forces states 8 and 16 (the low-level and sas modes, not done); resetpos
  and resetting for negative samples; second/resetting for a second buffer partly filled; reset2 in an error path.
  Not done: sceAtracLowLevel, the sas mode and sceSas's ATRAC3 voices (no game of the owner's imports them), AA3
  files.
- sample.at3 decodes as true stereo. Of the owner's games, all but Gunhound EX and Lumines play music with it (twelve
  of fourteen import it); Peace Walker uses the second buffer and sceAtracReinit.

**sceMp3** (`mp3.cpp`): two handles, each a stream's place in its file, a stream buffer (its first 1472 bytes the
library's own, the rest filled a half at a time as GetInfoToAddStreamData asks) and a sample buffer whose halves
each decode fills in turn (1152 stereo samples, 4608 bytes); sceMp3Init reads the first frame's header (MPEG-1 layer
III at 44.1 kHz only, as audio/mp3/init recorded), the queries, loops (0 at each loop's end, the stream starting
again), ResetPlayPosition and ResetPlayPositionByFrame. Run against the recordings: reserve, release, initresource,
mp3test, init, checkneeded, infotoadd, getloopnum and setloopnum match; stream but for the buffers' addresses;
notifyadd with a negative size, getsumdecoded's loop, getframenum's last high word and resetposbyframe at 48 kHz
still differ; the low-level functions aren't there. None of the owner's games import sceMp3 (nor sceAac, so it isn't
done).

**Movies** (`mpeg.cpp`, part 25's feeding and splitting kept):
- sceMpegQueryStreamOffset and QueryStreamSize read the PSMF header (its "PSMF" mark; the stream's start and size,
  big-endian words 8 and 12 bytes in: video/mpeg/basic's 0x800 and 0x40800). The GTAs, Midnight Club 3 and Snoopy,
  which gave their movies up at part 20's refused header, play them now.
- Each picture's access unit goes to the H.264 decoder; a picture comes one decode late (basic's first decode gave
  none) and is converted by ITU-R BT.601's equations for video's range, rounded, into the game's buffer in its pixel
  format (sceMpegAvcDecodeMode: 5650, 5551, 4444, or 8888, the default; alpha opaque): by sceMpegAvcDecode (the GTAs,
  Midnight Club 3), or kept by sceMpegAvcDecodeYCbCr for sceMpegAvcCsc, which converts the part asked for, in pixels
  (Burnout Legends asks for 0, 0, 480, 272; Space Invaders Extreme for zeros, the whole picture). The YCbCr buffer is
  left alone: its layout is the Media Engine's. sceMpegAvcDecodeStop gives back the picture the decoder held.
- The sound: sceMpegGetAtracAu takes ATRAC3plus frames out of private stream 1 (each PES packet's data a 4-byte
  header, then frames, each behind an 8-byte header: 0x0f 0xd0 and the codec's parameters, as the games' movies hold
  them), with the time stamp of the PES packet a frame starts in (else the last one's plus 4180 ticks); sound in
  packets freed for the pictures before it's asked for is kept (1 MiB at most). sceMpegAtracDecode decodes a frame
  into 2048 stereo samples. With no sound, the "no data" basic recorded, as before.
- scePsmf and scePsmfPlayer aren't done: of the owner's games only Peace Walker imports any (scePsmfSetPsmf,
  GetNumberOfSpecificStreams, GetPresentationStartTime and EndTime), not seen called in its first minutes.

**States**: version 10 (9 refused). Saved: the ATRAC IDs' files, positions, rings and the frame decoded last; the MP3
handles' streams and rings; what each movie library's Media Engine holds (the next access unit, kept sound with its
time stamps, the held and shown pictures, the sound frame decoded last). The decoders aren't saved but made afresh as
they're next needed: ATRAC's and the movie sound's primed with the frame decoded last (as a reset primes it), MP3's
from the next frame (whose borrowed bits from the frame before are lost: a moment's noise at most), a movie's showing
no new picture until one it can start from: an IDR picture, or one whose slices are all I slices (a movie may start
later stretches from I pictures that aren't IDR ones). Loading checks each as setting, decoding and feeding leave it,
and a decode's wait (due within a frame, no callbacks). The limits are the runtime's own, in one place each: a
picture at most `VideoDecoder::MaxSide` (1024) either way, which the H.264 decoder is held to (`max_pixels`) and
larger pictures are passed over by; a file's fmt extras at most `AudioDecoder::Format::MaxExtra` (64) bytes, the rest
not kept. In Liberty City Stories, a state saved in its "Rockstar Games Presents" movie (frame 1100) and loaded gave
the same frames as the run that went on from frame 1260 (black until the movie's next picture to start from, where
the run that went on shows the title fading in) and the same sound, second by second; Burnout Legends' menu movie
the same from frame 1440.

What the games do now (the host, the scratch runner from boot, frames and WAVs under `/tmp/codecs-runner/out`; RMS
and peak per channel over the capture; pitch checked by comparing the strongest frequencies of the game's output
with those of the decoded stream, which match in every game below):
- **Burnout Legends**: the EA logo movie, the opening FMV, the title and profile menus over its FEMain movie (frames
  1200-3000: -11.8 dBFS, 0.34% of samples clipped, no second silent; the game's own mix is mono, the ATRAC3plus it
  decodes stereo; strongest 105.0, 83.4, 123.8, 166.9, 220.7 Hz in both). **Dominator**: the EA and Criterion logo
  movies, its title and profile screen over its movie (-18.4 dBFS, 15 samples clipped).
- **GTA Liberty City Stories**: the Rockstar Games, Leeds and North logos, "Rockstar Games Presents", the title, with
  their sound (-15.2 / -15.6 dBFS, 66 samples clipped in 40 seconds). **Vice City Stories**: the Rockstar Leeds logo,
  "Rockstar Games Presents", the title and the opening credits (-16.3 / -17.0 dBFS, none clipped).
- **Midnight Club 3**: the Rockstar Games and San Diego logos, "in association with DUB", the title (-18.2 / -18.0
  dBFS, peak 26069, none clipped).
- **Space Invaders Extreme**: its title music (-15.9 dBFS) and, in Arcade Mode's first stage, the background movie
  behind the invaders (black before) and the stage's music (-10.6 / -11.5 dBFS, 0.007% clipped).
- **SOCOM**: its menu music, quiet (-29.4 dBFS, peak 12255). **Brave Story**: its music (-25.8 / -24.7 dBFS, peak
  14892).
- Decoding keeps pace: in each game the stream decoded in a stretch of play lasts as long as the stretch (Burnout 42.1
  of 42.8 seconds, Space Invaders Extreme 31.6 of 31.5, SOCOM 51.6 of 51.5, Brave Story 44.1 of 45).

**CPU cost** (the host Mac, the scratch runner's `-O2` build, FFmpeg's own `-O3` with NEON): an ATRAC3plus frame
(2048 samples, 46 ms of sound) 90 microseconds to decode, 0.2% of a core a stream; a movie's sound frame 80; a
480x272 picture decoded and converted 600 (Liberty City Stories), 1.8% of a core at 30 pictures a second;
sceMpegAvcCsc's conversion alone 400. Altogether 1.9% of the game's time in Liberty City Stories' movies, 2.8% in
Burnout Legends' (music and movie at once). On the RP6, Burnout Legends' opening movie and its profile screen over
the menus' movie ran at 60 frames a second (the HUD's CPU 72% and 54%), with no decoding errors in the app's log.

Tests (`tests/psp/run-tests.sh`: 259 groups with both sanitizers, and without FFmpeg, #152's six media groups among
them; `tests/psp/ares`: 282 checks, with and without; the app's unit tests, 264):
- `atrac.cpp` (new, a stand-in decoder that numbers frames, on files made in sample.at3's shape so the recorded
  numbers apply): "atrac ids", "setting", "whole file", "halfway", "streamed", "looped", "second buffer", "reset",
  "bad frame", "states" (a state part way through a looping stream carries on alike, the decoder primed; a reset
  straight after a load), "header sizes" (the review's: below); "atrac ffmpeg" decodes sample.at3 with FFmpeg when
  `PSP_AUTOTESTS` names a pspautotests checkout (never committed: it isn't ours).
- `mp3.cpp` (new, made-up 128 kbps frames, padded as an encoder pads them): "mp3 handles", "mp3 stream" (as
  audio/mp3/stream recorded), "mp3 loops" (and a reset by frame, decoding after a load), "mp3 ffmpeg" (sample.mp3).
- `movies.cpp` (new): "mpeg header"; "mpeg pictures decoded" (an H.264 stream made here of I_PCM macroblocks, whose
  colours are exact, in a made-up PSMF movie: each picture one decode late, its pixels BT.601's, both engines, the
  held picture in a state); "mpeg pictures converted" (sceMpegAvcCsc's part, and all four pixel formats); "mpeg
  sound access units" (frames out of private stream 1 with their time stamps, through a stand-in decoder); and the
  review's (below, through a stand-in picture decoder too, so they run without FFmpeg): "mpeg csc part past the
  picture", "mpeg picture sizes", "mpeg create afresh", "mpeg after a state".
- `states.cpp`: "state fields" changes each field of an ATRAC ID streaming, an MP3 handle and a movie library (the
  last sound frame among them), and refuses IDs outside their codec's, a state there isn't, rings past their
  buffers, MP3 streams at 48 kHz, fmt extras past 64 bytes, pictures of the wrong size or wider than 1024, sound
  frames of no frame's size, sound stamps out of order, and decodes' waits due in a second or with callbacks.
- `loader.cpp`: GPREL16 relocations load (the global pointer moves with the module: pspautotests' newer programs have
  them); `tests/psp/ares`: a state of version 9 refused.

**The review** (an independent one of the branch, 2026-10-06: no PPSSPP code found re-expressed, the LGPL handling
sound but for one credit) found two high-severity bugs and five low ones, each fixed with a test that fails without
its fix (checked by building the tests with each fix taken out again):
- **High: sceMpegAvcCsc's part overflowed.** The clamp added the part's left and width (top and height) in 32 bits,
  so a part of (0, 1, 0, 0xffffffff) wrapped to a small sum, kept its height and read on past the host's picture (a
  heap over-read, the host's memory written into the game's as pixels); (1, 0, 0xffffffff, 0) made a row of 4 GiB.
  It now compares a width with what's left of the picture (`width > pictureWidth - x`), as the height. "mpeg csc part
  past the picture" converts both parts (31 rows, 31 columns); without the fix, AddressSanitizer stops it with the
  heap-buffer-overflow.
- **High: atracParse could loop for ever.** A chunk's length moved the walk in 32 bits, so 0xfffffff8 took it back
  where it was. The walk is in 64 bits now, so it always moves on and leaves at the file's end; the data's end and
  the sample counts (fact's, or the data's own, and the loop's) are worked out in 64 bits, and what a 32-bit count
  can't hold is refused with the samples check's 0x80630008. "atrac header sizes" sets the review's 256-byte file
  (refused as unreadable; without the fix the test never ends) and two files whose data or samples pass 4 GiB.
- **Low, licensing: the IJG's credit.** Added to FFmpeg's notice in `LICENSE`, and checked by `LicenseNoticesTest`;
  FFmpeg's error concealment is kept.
- **Low: sceMpegCreate kept the last movie's Media Engine.** A library made again where one was, without
  sceMpegDelete, gave the old movie's queued sound, skipped as much of the new one's as the old had taken, and timed
  its frames from the old movie's. Create now starts afresh; "mpeg create afresh" plays a movie's first sound frame
  twice, through two decoders.
- **Low: the runtime made what a state load refuses.** Pictures had no limit (FFmpeg's own is 268 megapixels, packed
  twice), and loading took 4096 by 4096; fmt extras were kept to 64 KiB, and loading took 64 bytes. Both now have one
  limit, used by both (above): 1024 each way (more than UMD Video's 720 by 480), and 64 bytes. "mpeg picture sizes"
  passes over pictures 1040 wide or tall (and FFmpeg's), keeps 1024 by 1024, and saves; "atrac header sizes" keeps
  64 of 100 extra bytes and saves.
- **Low: after a load, the movie's sound restarted unprimed, and pictures waited for an IDR one.** The last sound
  frame is in the state now, and primes the new decoder; pictures also start again from an I picture that isn't an
  IDR one (keyframe() reads each slice's type). "mpeg after a state" checks both (a P picture, and one with a P
  slice among its I slices, still waiting). In the games, Liberty City Stories and Burnout Legends carry on after a
  load mid-movie (above), the same without the I-picture rule: their movies start again from IDR pictures.
- **Low: building.** The download is checked before it's kept, and tried once more (above); the lock is taken over
  from a build that's gone; `-Pphobos.ffmpeg=OFF` turns FFmpeg off from Gradle; the README lists the requirements,
  the switch and `PHOBOS_FFMPEG_TARBALL`; CI's cache keys are the build's own names. Checked with a copy of the
  script pointed at a file that isn't the tarball (thrown away, tried again, then the guidance), a lock held by a
  process that's gone (taken over) and by a running one (waited for), a bad tarball named in `PHOBOS_FFMPEG_TARBALL`
  (refused), and an APK built with `-Pphobos.ffmpeg=OFF`.
- **Minor: the decoders nothing opens** (aac, atrac3al, atrac3pal) are no longer built; `LICENSE`'s notice lists the
  four that are, and `LicenseNoticesTest` checks the list against the script's.

With the fixes, on the RP6 (the modern release APK installed over the last, data kept): Liberty City Stories' logo
movies and Space Invaders Extreme's title screen (its music, on the host) at 60 frames a second, the sound's stream
steady in both (no underruns after the start; not listened to), nothing about decoding in the app's log.

Seen on the way, not changed:
- Burnout Legends' title clips 0.34% of its samples: the game's mix of music and effects, summed by the game; a PSP's
  mixer may saturate the same, not measured.
- The Media Engine's rounding of the colour conversion, and how long its decodes take, aren't measured.
- Midnight Club 3's San Diego logo shows in stripes for a moment (frame 600): taken for the movie's own wipe.
- Snoopy vs. the Red Baron's movies weren't seen: the run's presses stayed at its autosave notice.

## Part 27: the PSP in the desktop program

On branch `cursor/psp-desktop-2b67` (#155), on top of part 26's `cursor/psp-codecs-2b67` (#153). The owner's request
(2026-10-06): PSP games playable in the desktop program (`desktop/`, SDL3) on Linux, macOS and Windows, as in the
Android app, with FFmpeg's decoders built and shipped for each, LGPL-compliant as on Android. The desktop program
already linked the PSP core (`phobos_core` carries every system); what it lacked was the PSP in its library, the
PSP's settings, its stick on the keyboard, its picture drawn sharply, and FFmpeg.

**The library** (`desktop/Library.cpp`): the PSP's extensions, as the app lists them (`iso`, `cso`, `zso`, `dax`,
`jso`, `chd`, `pbp`, `elf`), and folders named `psp` or `playstationportable`. The PlayStation's games share `.iso`,
`.chd` and `.pbp` with the PSP's, so a file with one of those is the PSP's when mia's PSP medium takes it, wherever
it is: the runner's new `ares::isPspGame(path)` asks the medium, which reads only the file's head (an ISO whose
primary volume descriptor's system is "PSP GAME", a CHD of a DVD, 2048-byte units, an EBOOT.PBP whose PARAM.SFO's
category isn't "ME", the PlayStation's games sold for the PSP; an ELF for MIPS). A file it doesn't take goes by the
folder rules as before (a `psp` folder still names the PSP, whose medium then says what's wrong as the game starts:
"It's a CD's CHD..."); else `.chd`, `.iso` and `.pbp` are the PlayStation's. As in the app, an EBOOT.PBP goes by its
folder's name (`Cube/EBOOT.PBP` is "Cube", `pspProgramName`, a copy of LaunchSystems'), which is also the name the
runner keys its saves and states by, and the PSP's discs are each listed alone (no disc swapping yet). Checked by a
scratch program that prints the scan (not kept): a PSP CHD, a made-up PSP ISO, a homebrew EBOOT.PBP and an ELF were
the PSP's; a made-up PlayStation ISO and a PlayStation EBOOT (category "ME") the PlayStation's.

**Starting a game** (`desktop/main.cpp`): the runner gets the file's own path (`setRomPath`), so the medium reads it
in place (the app's `/proc/self/fd` path isn't involved). Before each start, three settings (`settings.ini`, the
pause menu's PSP items while a PSP game runs, each "(next start)", since the core reads them as it powers on):
- `psp.memoryStick`: a folder picked in the system's dialog; empty (the default) is the runner's shared one,
  `<saves>/PlayStation Portable/Memory Stick` (the desktop's data folder's `saves`), as on Android. Left or right goes
  back to it.
- `psp.fonts`: the user's own system fonts, none by default. The folder picked may be the fonts' own, one holding
  `font`, or a flash0 dump's root holding `flash0/font` (any case): `pspFontFolder` finds the first holding one of
  the 18 fonts (`jpn0`, `ltn0`-`ltn15`, `kr0.pgf`); the item shows how many of them it holds ("12 of 18"), counted
  as the setting is applied or changed (`setPspFonts`), not as the menu is drawn, and the folders are stepped through
  without exceptions, so a slow or vanished share can't stall or stop the menu. A folder with none is refused with a
  message.
- `psp.drawingThreads`: Auto (0: all cores but one), 1, 2, 4, 6 or 8, the app's choices.
A game that doesn't start shows the medium's sentence (`lastLoadProblem`) where there is one.

**Controls** (`desktop/Input.cpp`): the desktop's pad bits are the runner's, which maps the PSP's buttons by
position: Cross the south button (A on an Xbox pad, Cross on a DualShock), Circle east, Square west, Triangle north,
L and R the shoulders, Start and Select (Back), the D-pad, and the left stick the PSP's analog stick. On the
keyboard: the arrows, X Cross, Z Circle, S Square, A Triangle, Q and W the L and R buttons, Enter Start, right Shift
Select, and now I, J, K and L the left stick, pushed all the way (the PSP's, and the N64's and a DualShock's), held
unless a pad's stick is pushed further. F5 and F9 save and load a state, F12 or Escape opens the menu, Tab
fast-forwards, F11 is fullscreen, and F8 (new, and the menu's "Screenshot") saves the core's frame as a PNG,
`screenshots/<system>/<title> (n).png` in the data folder, as the app's screenshot does. A real pad wasn't at hand:
the gamepad's buttons are SDL's positional ones, as for every other system.

**The picture**: the PSP's 480x272 is drawn by the runner at a whole multiple of its size (`setPictureMultiple`), the
one nearest the window's height (1 to 4, as the app picks it), each pixel repeated, then scaled the rest of the way
by the GPU's bilinear filter: the one-pixel lines of the PSP's text and HUD stay even, without nearest-pixel
scaling's uneven rows. The other systems are scaled by nearest pixels as before.

**FFmpeg on the desktop** (`thirdparty/ffmpeg/build.sh`, `CMakeLists.txt`): the build is the same, unmodified 9.0.2
with the same four decoders, for three more targets, which the desktop branch of CMakeLists.txt picks by itself:
- `host` on Linux (the system's cc, with nasm for x86's assembly, else none: `--disable-x86asm`) and on Windows
  under MSYS2 UCRT64 (FFmpeg's configure doesn't know MSYS2's system name, so it's told `--target-os=mingw32`; its
  own Windows threads, and the compiler's libgcc and winpthreads linked in statically, as into Phobos.exe: the two
  DLLs need nothing but each other and Windows's own).
- `macos ARCH...`: each architecture cross-built by Apple's clang (`-arch`, for macOS 11 or the deployment target
  CMake is given), its libraries named `@rpath/libavcodec.63.dylib`; for more than one, each is built, then the
  libraries are put together by `lipo` (the universal app's arm64 and x86_64).
- `windows TOOL-PREFIX`: from Linux with MinGW-w64 (`desktop/windows/mingw-w64-x86_64.cmake`), which also needs a C
  compiler for Linux itself, for configure's own checks.
CMake runs it as it configures (as for Android), links `phobos` with the two libraries (Windows: their import
libraries), defines `ARES_ENABLE_FFMPEG` for the core, and copies the two files the program loads beside it in the
build folder (`libavcodec.so.63`, `libavcodec.63.dylib`, `avcodec-63.dll`, and libavutil's), where it finds them:
its run path is `$ORIGIN` on Linux and `@executable_path` on macOS, and Windows looks beside the program.
`-DPHOBOS_FFMPEG=OFF` builds without them, the PSP then silent in its music and movies as before part 26.
Any other cross build stops at configure, saying so.

**The packages** (`scripts/package-*.sh`), each with the two libraries as files of their own that can be replaced:
- Linux: the AppImage's `usr/lib`, passed to linuxdeploy with the build folder on `LD_LIBRARY_PATH` (linuxdeploy
  looks for libavcodec's own dependency, libavutil, on the system's search path, and stopped at "Could not find
  dependency: libavutil.so.61" without it); linuxdeploy gives the libraries the run path `$ORIGIN` and the program
  `$ORIGIN/../lib`. The script checks that each FFmpeg library the program needs (its `readelf -d`) is a file of its
  own in `usr/lib` by that name, and that the program and each of those libraries would find every FFmpeg library
  they need through their run paths.
- macOS: Phobos.app's `Contents/Frameworks`, the program's run path changed to `@executable_path/../Frameworks`; the
  script checks each library holds every architecture the program does, signs them (with the app's Developer ID,
  or ad hoc) and prints their architectures.
- Windows: the zip, beside Phobos.exe; the script checks Phobos.exe's imports are Windows's own or the two DLLs,
  and the DLLs' imports Windows's own or each other.
- CI (`.github/workflows/desktop.yml`): each job installs what FFmpeg's build needs (nasm, curl, xz; make and diffutils
  in MSYS2), caches `.cache/ffmpeg` under the build's own name (`build.sh --name`), and uploads FFmpeg's tarball
  with its package; a tag's release gets it with the rest (the same file the APKs' job uploads, replaced by itself).
- `LICENSE`'s FFmpeg notice names the four packages' files and how to swap each (the AppImage extracted and run or
  packed again, the app signed again, the DLLs replaced), and that the releases carry the tarball beside the desktop
  builds too. `.gitattributes` keeps shell scripts' line ends LF, since bash runs them on Windows too.
- The Windows build links the MinGW-w64 runtime and winpthreads into Phobos.exe and FFmpeg's DLLs (Phobos.exe did
  before this part; avutil-61.dll does since its `-static`): `LICENSE`'s "MinGW-w64 runtime and winpthreads" notice
  carries the runtime's `COPYING.MinGW-w64-runtime.txt` (its ZPL 2.1 overall notice, getopt's, gdtoa's, the math
  library's, musl's MIT parts) and winpthreads' `COPYING` (MIT, and Lockless Inc.'s BSD part), as the mingw-w64
  project ships them; GCC's runtime libraries need none (the GCC Runtime Library Exception). `LicenseNoticesTest`
  expects it and checks both texts are there.

**Checked on the host Mac** (Apple silicon, macOS; the desktop program built Release; games run with the user's data
folder redirected to a scratch one by `CFFIXED_USER_HOME`; keys sent through System Events; pictures by F8, under
`/tmp/psp-desktop-out/shots`):
- **Burnout Legends**: the title over its menu movie (FFmpeg's), the soundtrack's song shown ("Emanuel, The Hey
  Man!"), a profile made with the D-pad, Cross, Triangle and Square (the name screen's pages), saved to the shared
  memory stick (`PSP/SAVEDATA/ULUS10025SAVE000`), the main menu, a single event: racing, Cross held to 67 mph, at 60
  frames a second. A state saved (F5) on the name screen and loaded (F9) after moving on brought it back; the pause
  menu paused the core and took its screenshot.
- **GTA Liberty City Stories**: Start and Cross through its menus to a new game: the opening cutscene, then Toni in
  the street by his car, at 60.
- **Midnight Club 3**: its title movie and "Load Profile / Create Profile / Delete Save Data" over the city; 60 in
  its movies, 25 in that menu (the core's own speed there, part 25: filling its pixels).
- **Space Invaders Extreme**: to its name entry, at 60. **Lumines**: its title, at 60.
- The universal app (`scripts/package-macos-app.sh` on a `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` build): Phobos
  and both libraries x86_64 and arm64, signed ad hoc; Burnout Legends' title over its movie at 60 from the app, both
  natively and under Rosetta (x86_64), the libraries loaded from `Contents/Frameworks` in both. A copy of the app with
  libavcodec replaced by another build (arm64 only) and signed again ran the same.
- Windows and Linux, in Docker on the host (2 GB, too little for the whole core at once; the full builds and their
  packages are CI's): MinGW-w64 13 (posix) from Ubuntu 24.04, FFmpeg by `build.sh windows x86_64-w64-mingw32-` as
  CMake runs it; the desktop sources and the runner compiled; a program linked with the two import libraries, as
  Phobos.exe is, went through `package-windows.sh`'s checks into its zip. The first build's avutil-61.dll needed
  libwinpthread-1.dll (the posix toolchain's libgcc uses it), which the script refused; linked `-static` now, the
  DLLs need only KERNEL32, msvcrt (UCRT under MSYS2) and bcrypt, and each other. Linux: Ubuntu 24.04 on arm64,
  FFmpeg by `host`, `libavcodec.so.63` and `libavutil.so.61` needing only libc, libm and each other; the same
  sources compiled; a program beside the two, run path `$ORIGIN`, found them and FFmpeg's ATRAC3plus decoder.
- Tests: `tests/psp/run-tests.sh` 259 groups, none failed; `tests/psp/ares` 282 checks, none failed; the app's 264
  unit tests (`LicenseNoticesTest` on the new notice among them) and its modern release APK (23,604,918 bytes).

Not checked, or left:
- A real gamepad (none at hand), real PSP fonts (scratch files of their names only), a PlayStation CHD beside the
  PSP's, and a PlayStation game in the library (made-up files only).
- The desktop program built whole and run on Linux and Windows (CI builds and packages it; on 8501415c7 its macOS
  and Windows jobs passed, MSYS2's native FFmpeg build among them, and Linux's stopped in linuxdeploy, fixed below).
  After the fixes, CI's whole build packaged all three, the AppImage included (run 37544580853); neither the
  Linux nor the Windows program has been run.
- Discs can't be changed on the PSP (its games are listed one by one); the desktop's library shows no PSP icons or
  PARAM.SFO titles (the file's name is the title, as for every system).

**After review and CI** (the branch pushed with #153 merged in as 8501415c7; CI's Android, PSP Core
Tests, macOS and Windows jobs passed; LGPL compliance checked on the macOS and Windows artifacts):
- High: the AppImage step failed in linuxdeploy (libavutil not found as libavcodec's dependency). Fixed by
  `LD_LIBRARY_PATH` and checked further (above). In an x86-64 Ubuntu 24.04 container (emulated on the host Mac; the
  tools and the AppImage unpacked from their squashfs, since an emulated AppImage can't read itself), the real script
  packaged a stand-in `phobos` linked to FFmpeg's `host` build as CMake links it: both libraries in `usr/lib` as
  files, run paths `$ORIGIN` and `$ORIGIN/../lib`, and the AppImage's program, run from elsewhere, loaded them from
  its own `usr/lib` and found the ATRAC3plus decoder.
- Low: the MinGW-w64 runtime and winpthreads notice (above) was missing.
- Low: the PSP settings menu listed the fonts folder twice a frame for its label; it's counted once, by
  `setPspFonts`, as settings are applied and as the folder is picked or cleared.
- Info: `build.sh`'s stale-lock takeover re-checked the owner and then removed the lock, so two waiting builds
  could both remove it (the second removing the first's new lock). A takeover now first makes `takeover` inside the
  lock (`mkdir`, which only one can), re-checks the same dead owner, then removes it; the losers wait. Eight builds
  at once against a planted dead lock, three rounds: one takeover each, never more than one inside.
- Checked again: Burnout Legends on the host Mac at 60, the fonts folder set (its count in the menu) and cleared
  from the menu; the app's 265 unit tests, none failed.

## Part 28: the functions 266 games ask for

On branch `cursor/psp-hle-games5-2b67`, on top of part 27's `cursor/psp-desktop-2b67` (#155). The owner's agent ran
266 of the owner's games through a headless runner (`origin/local/psp-runner`'s `tools/psp-runner` and
`docs/psp-compatibility.md`) and listed the functions they call that the HLE kernel didn't have; this part gives
them the most-needed ones. Sources: pspsdk's headers (`pspthreadman.h`, `pspctrl.h`, `pspge.h`, `pspsysmem.h`,
`pspimpose.h`, `pspkerror.h`, the import stubs' names), pspautotests' programs and the results they recorded on a
PSP (threads/mutex, threads/alarm, threads/vtimers, threads/threads/release, threads/semaphores/cancel,
threads/events/cancel, threads/scheduling/mutexhandoff, waittimeouts, preemptuser and alarmcosts, ctrl/idle,
gpu/ge/edram, sysmem/memblock and its `sysmem-imports.S`), and the programs run through the HLE kernel (the scratch
runner of part 26, which answers their emulator devctl and compares their output with the recordings). No other
emulator's code was read. Where a recording doesn't show a behavior, the code and this part say what was chosen.

**Kernel mutexes** (`mutexes.cpp`): sceKernelCreateMutex, DeleteMutex, LockMutex, LockMutexCB, TryLockMutex,
UnlockMutex, CancelMutex and ReferMutexStatus, the kernel's own mutex (the lightweight one keeps its state in the
program's memory).
- The thread that locks a free mutex holds it. With the recursive attribute (0x200) its holder may lock it again, the
  locks counted (to 2^31 - 1: LOCK_OVERFLOW past it), and it's free once unlocked as often; else a count of more than
  1 is ILLEGAL_COUNT and the holder locking again RECURSIVE_NOT_ALLOWED. Threads finding it held wait first come first
  served, or by priority with attribute 0x100 (the longest waiting among equals); as it's freed it goes straight to
  the first, with the count that thread asked for, which runs at once if it's better than the one that freed it. A
  holder that ends (or is terminated, or deleted) frees what it holds, each mutex going on to its next waiter.
  Deleting wakes the waiters (WAIT_DELETE), cancelling too (WAIT_CANCEL) with a new count (the caller holding it if
  it's more than 0). Nobody's priority changes for a mutex (mutexhandoff: no inheritance).
- The checks' order and errors are the recordings': the mutex before the count, the count before who holds it, a
  try on another's mutex MUTEX_LOCKED (0x800201c4), unlocking another's or a free one MUTEX_UNLOCKED (c5), more than
  held UNLOCK_UNDERFLOW (c7), an unknown mutex c3; a NULL name ERROR, attribute 0x400 or any above 0xbff
  ILLEGAL_ATTR; a lock where no thread may wait refused before anything is looked at (intr/waits). A status is copied
  as far as the size its first word gives (0 copies nothing; 1 copies the size's first byte, so 56 reads back), the
  holder -1 when free.
- Only a thread may hold one: trying, unlocking, cancelling or making one held (and the lightweight mutex's create
  held, lock, try and unlock) are refused (ILLEGAL_CONTEXT, before anything else) in an interrupt handler or with no
  thread running (`Kernel::fromThread()`). Chosen: intr/waits recorded only the locks in a handler; the others would
  leave a mutex held by nobody, which nothing could unlock and no state could keep, or by the thread interrupted.
- Chosen: a negative count to CancelMutex (and CancelSema) is taken as the count it was made with.
  threads/mutex/cancel and threads/semaphores/cancel recorded -1 and -3 accepted on a mutex made free and a
  semaphore made with 0, which read 0 afterwards: "free" (0) fits as well. A count too big is refused
  (ILLEGAL_COUNT), a negative one isn't, so it isn't taken as a count; "as it was made" is the meaning that leaves,
  and the recordings' cases agree with it.
  ReferThreadStatus gives a thread waiting on a mutex no wait type (the PSP's number isn't known here).
- The lightweight mutex now honours its priority attribute too: mutexhandoff recorded both kinds alike ("UBAC",
  "BACU", "BUCA"), and it had been first come first served whatever the attribute.

**Alarms** (`timers.cpp`): sceKernelSetAlarm, SetSysClockAlarm, CancelAlarm, ReferAlarmStatus. An alarm goes off at
its time (the system's time plus the microseconds asked, 64 bits wide with the clock: 2^64 - 2 is 2 microseconds
ago), calling handler(common) as an interrupt handler (a call into the program, interrupts.cpp: on top of whatever
runs, no waiting, sceKernelGetThreadId telling it it's no thread, the global pointer it was set with). It returns 0 to
end the alarm (CancelAlarm then finds none, UNKNOWN_ALMID), or the microseconds until it goes off again, counted from
the moment it was due (set: ten 1000-microsecond calls in 10200 microseconds, the schedule then 11000 on). A NULL
handler is ILLEGAL_ADDR. A status is copied as far as its size (20). Two things the recordings pin down:
- A timer a system call sets going doesn't go off sooner than about 215 microseconds after the call (alarmcosts:
  "it never goes off sooner than about 215us after being set"; set's zero-time alarm hadn't gone off by the next
  call). What the call itself costs on a PSP (42 to 45 microseconds) isn't counted, as no call's is here, so
  alarmcosts' handler comes about 215 microseconds after the call began where a PSP's comes about 256 after, and
  the 50 microseconds a PSP takes to get a woken thread going, and the 72 a running one loses to a handler, aren't
  there either (alarmcosts and preemptuser still differ for that).
- An alarm due again by the time its handler returns (its next moment passed already) goes off its microseconds from
  then, not at once: alarm's handler, due again 100 microseconds on after interrupts had been held off for
  milliseconds, was called once.

**Virtual timers** (`timers.cpp`): sceKernelCreateVTimer, DeleteVTimer, Start/StopVTimer, Get/SetVTimerTime(Wide),
GetVTimerBase(Wide), SetVTimerHandler(Wide), CancelVTimerHandler, ReferVTimerStatus. A clock of the program's own,
running only while it's started (Start answers whether it ran already, Stop whether it was running), its base the
system's time it was started at (0 while stopped), which may be set to any time (the old one handed back by address,
or as the wide form's result, -1 for a timer there isn't). Its handler is called when its time reaches the schedule,
as handler(timer, &schedule, &its time now, common): the two times in kernel memory (`TimerClocks`), and the wide form
the same way (sethandler: "pspsdk is wrong, they are the same handler"). What it returns moves the schedule on in the
timer's time; one that has fallen behind is called again at once until it catches up (sethandler's three calls in a
millisecond); 0 drops the handler and keeps the schedule. A NULL handler drops the handler alone (the schedule and
common kept, the time not read). The recorded oddities: starting, stopping, setting or cancelling a handler on timer
0 is ILLEGAL_VTID outside any handler (start, stop, sethandler, cancelhandler), and cancelling its own timer's
handler is ILLEGAL_VTID in a handler, where timer 0 is merely UNKNOWN_VTID (cancelhandler): taken as one check, a
timer whose handler is running being the illegal one, which starting, stopping and setting a handler make too
(chosen: only cancelling is recorded in a handler). Creating and deleting are refused in an interrupt handler
(ILLEGAL_CONTEXT, before the timer is looked for: vtimers/interrupt), SetVTimerTimeWide answers -1 there; a status is
copied as far as its size (72). Chosen: setting a running timer's time moves its base to now.

**Threads released and waits cancelled**: sceKernelReleaseWaitThread lets a thread go of whatever it waits in,
told RELEASE_WAIT, the wait tidied as a timeout's is (a lightweight mutex counting one waiter fewer, those in line
behind served), the thread running at once if it's better than the caller; its errors as recorded (NOT_WAIT for one
ended or never started, ILLEGAL_THID for 0 and the caller, UNKNOWN_THID). threads/threads/release recorded a delay,
a sleep and a semaphore's wait let go; every other wait is taken to go the same way, and (chosen) a thread made
ready to run its callbacks counts as waiting still, one running them doesn't. ThreadManForUser 0xfccfad26 is
sceKernelCancelWakeupThread (its name's hash, as pspsdk names it): the wakeups that came for a thread while it was
awake are forgotten and counted. sceKernelCancelSema and sceKernelCancelEventFlag (which waittypes and the alarm test
use) wake their waiters with WAIT_CANCEL and set the count or bits, a flag's waiters told the new bits.

**The small ones**: sceCtrlSetIdleCancelThreshold and its getter (-1 to 128 each, kept to be read back: nothing
dims or sleeps; a kernel address refused, PRIVILEGE_REQUIRED); sceGeEdramSetAddrTranslation (0, 0x200, 0x400, 0x800
or 0x1000, the width there was returned, 0x400 first; VRAM's swizzled copies keep their starting layout whatever is
set); later SDKs' sceKernelAllocMemoryBlock, FreeMemoryBlock and GetMemoryBlockPtr, listed by the NIDs games import
(0xfe707fdf, 0x50f61d8a, 0xdb83a952, as pspautotests' `sysmem-imports.S` names them: not their names' hashes), blocks
of the user partition like any other (types 0 and 1, options 4 bytes long, a pointer read back as 0 for a block there
isn't, as recorded); sceImposeGetLanguageMode (what SetLanguageMode set, now kept, else English and the cross
button, numbered as the system's settings number them: `pspimpose.h` doesn't give the values);
sceImposeGetBatteryIconStatus (pspsdk names it only, no recording shows it: chosen, the natural answer with no
battery to show, not charging (0) and a full battery's icon (3, the icon showing up to three bars), as the power
functions say the battery's full); sceKernelDevkitVersion (0x06060110, firmware 6.61: threads/tls/partition
recorded 6.6-something, printing the major and minor numbers alone, "firmware 6.06"; the point release is chosen);
sceKernelUSec2SysClock and its wide form.

**Time as the PSP keeps it in system calls.** Running pspautotests' timer programs showed three things the kernel's
timekeeping did that a PSP doesn't, each of which the new functions exposed:
- **The clock stood still between waits.** A system function saw the time the CPU's go began (a go runs to the next
  thing due, up to a frame), however many instructions the thread had run since: an alarm set was due sooner than
  asked by however long the thread had been running, and a virtual timer read after the program had spun a while
  since starting it had run 0. Now the clock catches up at each syscall to the instructions run before it in the go
  (`Allegrex::instructionsBefore()`; run() adds the rest), the same count on both engines: inside a compiled block
  the recompiler counts the block's instructions before the syscall, which it knows from where the syscall is in it.
- **What a system function made due waited for the go to end.** A better thread's delay or timeout ending, or a
  timer's handler, while a worse thread ran on without waiting, waited for the end of its go, a frame at most. Now
  each syscall brings the go's end forward to the next thing due (`Allegrex::runLimit`), so it comes on time
  (preemptuser: a better thread's 1000-microsecond delays took 1030 while main spun, where the PSP took 1020 to
  1030).
- **Timeouts lasted exactly what they were asked.** pspautotests' waittimeouts recorded every kind of wait alike: a
  timeout lasts max(timeout, 205) plus about 35 microseconds (what's left written back from that), and a very short
  one (1) times out "at once", its time not written back. The scheduling logs of the BASIC_SCHED_TEST programs show
  "at once" is still a moment's wait in which a worse thread gets to print a line and make a call, where a
  1-microsecond timeout of 333 cycles left it a few instructions. Now the thread manager's waits with timeouts
  (semaphores, event flags, kernel and lightweight mutexes, message pipes, mailboxes, memory pools, a thread's end)
  last that long; at once is taken as 30 or less, a 35-microsecond wait (the edge, which the test says moves with
  the call and from run to run, lies between its 1 and 100). The drive's waits keep their own (part 14).
- Running the programs through both kernels, before and after this part: of threads/ and intr/'s 167 programs with
  recordings, 36 matched before and 72 after; of the 192 elsewhere (io, rtc, power, sysmem, ctrl, display, umd,
  audio, misc, utility, modules, hle, video), 48 before and 55 after. Matching now: all ten threads/mutex programs,
  scheduling/mutexhandoff and waittimeouts, alarm's alarm, set, cancel and refer, all twelve vtimers programs,
  threads/release, semaphores' cancel and wait, mbx/receive, callbacks' afterwait, callbacks, cbtimeout and
  waittypes, ctrl/idle, sysmem/memblock, misc/timeconv (the clock conversions), and gpu/ge/edram (run apart); and
  with the clock alone ctrl/sampling2, audio/blocking/depth, audio/output2/frequency and rest (which had measured a
  frame's 16.6 ms where the PSP's 1.3 pass). One line went the other way: msgpipe/trysend's receiver reads 9 ms left
  where the PSP read 8 (likely the 35 microseconds now on its timeout, the calls in between costing nothing here).
  display/hcount and vblanklen, which had spun for good on the stopped clock, now run to their end, their figures
  still off (sceDisplayAdjustAccumulatedHcount missing; the blank 770 microseconds long where the PSP's is 730 to
  740), so more of their lines differ; video/pmf stalls in both, polling more often now.
- The test group "interrupts held off, delivered once" had stored its blank count in the word its handler counts
  in, which went unnoticed while the clock stood still and fails now; it keeps it in a word of its own (the check
  the same: six blanks or more held off in its 110 ms).

**States**: version 11 (10 refused): the mutexes, alarms and virtual timers; each call into the program's fourth
argument and whether it's a timer's handler (and which), as the running call's are; the controller's idle
thresholds, the GE's translation width, the HOME menu's language and button. Loading checks a mutex held exactly
while counted, by a thread there is that hasn't ended, 1 at most unless recursive, and a thread waiting for one
waiting for one another thread holds, with a count it takes; an alarm with a handler; a stopped timer with no base, a
started one's base come; no timer going off later than a call could set it; each timer marked as being called having
exactly one call waiting or running (the running one may have lost its alarm to its own handler), and every timer's
call a timer there is; the thresholds and width ones their functions take.

Tests (`tests/psp/run-tests.sh`: 272 groups (274 after review, below), with and without the sanitizers;
`tests/psp/ares`: 286 checks; `tests/allegrex`, the CPU's run loop having changed: 56 groups; none failed):
- `mutexes.cpp` (new): "mutexes called directly" (create's refusals, counts and holders, overflow, a status's
  sizes, cancelling, deleting), "mutexes handed on in order" (both engines, both attributes: "UABCDU" and
  "UBCADU"), "mutexes waits ending" (a recursive mutex half unlocked, a timeout, a waiter terminated, a holder ending,
  cancel and delete), "mutexes callbacks in a wait", "mutexes state with a waiter" (saved mid-wait, carried on in a
  fresh machine, what's left of the timeout as the PSP's), "threads released from waits" (six kinds of wait, the
  refusals, wakeups cancelled), "semaphores and event flags cancelled".
- `timers.cpp` (new): "alarms called directly" (a status's sizes, 64-bit times, refusals), "alarms going off" (the
  handler's argument, global pointer and context, 1000 after each moment, 215 for at once, held off and once on
  coming back, 100 from then), "vtimers called directly", "vtimers' handlers" (the passed handler's three calls, the
  clocks it's told, the refusals inside it, a stopped timer's handler not called), "timers in states" (saved before
  they're due, and with a handler's call waiting with interrupts held off, in a fresh machine: the same calls at the
  same moments).
- `media.cpp`: "part 28's odds and ends" (the thresholds, widths, memory blocks by their NIDs, the impose
  functions, the firmware, the clock conversions); `states.cpp`: "state fields" changes each new field, refuses 29
  more states and loads one more (a thread waiting for a mutex another holds); `tests/psp/ares`: a state of version
  10 refused.
- Broken versions each failed: mutexes always first come first served ("mutexes handed on in order"), a thread's
  ending keeping its mutexes ("mutexes waits ending", "callbacks in a wait", "state with a waiter"), timers without
  their 215 microseconds ("alarms going off", "vtimers' handlers"), a virtual timer behind its schedule not caught up
  at once ("vtimers' handlers"), a mutex's holder left out of states ("mutexes called directly", "state with a
  waiter", "threads released from waits"). Two of those runs then crashed in "state fields", whose refusals take a
  failed load for granted; the failures came first.

The games (the host Mac; the runner of `origin/local/psp-runner`, built from a scratch copy, with this branch's tree
and with part 27's, FFmpeg on in both, 3600 frames, Start pressed at frame 120 and cross at 1800): Burnout Legends
and Dominator, Liberty City and Vice City Stories, Peace Walker, Lumines, Midnight Club 3, SOCOM, Snoopy vs. the Red
Baron and Gunhound EX reach the same screens in both at frames 1200, 2400 and 3000 (titles, the opening movies and
credits, SOCOM's and Snoopy's notices that there's no saved game, Lumines' demo), movies and title backgrounds a
moment apart, and none calls a function the kernel lacks, in either. Five import functions this part adds (Burnout
Legends and Lumines the alarms, Peace Walker sceKernelReleaseWaitThread and sceKernelCancelWakeupThread, SOCOM the
battery icon and the EDRAM translation, Snoopy the translation) but don't call them in that minute; the 266 games'
report counts those that do. Burnout Legends' calls to signal and wait for semaphores in that minute fell from 36
million each to 11 million, the time between them now counted.

Left, and why:
- scePsmf and scePsmfPlayer (19 games by the report, the top fifteen's scePsmfPlayer 0x235d8787 in 8 and scePsmf
  0xc22c8327 in 7): no source to write them from that this part may use. pspsdk has no PSMF header, pspautotests no
  scePsmf program, and the movie container's own fields (streams, entry points, times) are documented nowhere
  allowed here; scePsmfPlayer's programs exist, but the player drives part 26's decoders through a playback state
  machine of its own. A later part.
- Every other function the report names is here. It counts 34 ThreadManForUser functions games call but names only
  those eight; the owner's games here import two more the kernel lacks, sceKernelReferThreadRunStatus (Peace Walker)
  and sceKernelGetThreadmanIdList (SOCOM), and seven import sceGeBreak, none calling them in their first minute. Its
  GE work (compressed textures, bounding boxes, lines: 29 games) isn't the kernel's.
- Seen on the way, not changed: event flags' and semaphores' create (a NULL name, attributes) and status (the size
  word, the same copying rule as the mutex's) differ from threads/events and threads/semaphores' recordings (events'
  cancel still differs for its status's size word alone); the lightweight mutex's other differences in
  threads/lwmutex (its status function, its counts' refusals); sceDisplayAdjustAccumulatedHcount; what calls into
  the kernel and into handlers cost.

Uncertain: the "at once" edge (30) and its 35 microseconds' wait, the timeouts' 35 written back as part of what's
left; an alarm due again in the past going off from now (from one recording); a negative count to the cancels as
the initial count; a running timer's base after its time is set; a virtual timer's handler starting, stopping or
re-setting its own timer; the 215 microseconds taken for virtual timers too (alarmcosts measured alarms);
sceKernelGetThreadId's error in a handler (threads/alarm shows only that it's no thread's ID); the battery icon's
outputs and the impose functions' numbers; the mutex's wait type in a thread's status; and the mutex calls refused
outside a thread besides the locks.

**After review.** Three things changed. A mutex could be left held by nobody: an alarm's (or a virtual timer's, a
blank's, the GE's) handler trying a free mutex while no thread ran gave it count 1 and holder 0, which no thread
could lock or unlock and a state saved then couldn't load (its check: held exactly while it has a holder); with a
thread interrupted, that thread became the holder unknowing. Trying, unlocking and cancelling are now refused in a
handler or with no thread running, as locking was in a handler, and so is everything else that makes a thread a
mutex's holder or changes its count (`Kernel::fromThread()`, above). And the engines read the clock up to a block
apart at a syscall: the recompiler had published its count only as a block ended, so a syscall in a block saw the
clock as the block began, the interpreter as it came; it now counts the block's instructions before the syscall
(`instructionsBefore()`: the count each block's call into the interpreter already stores, less the syscall itself).
Tests: "mutexes outside
threads" (`mutexes.cpp`, both engines: an alarm's handler calling them with no thread running and interrupting main,
neither mutex left held, main taking and freeing both after, the state at the end loaded; and called directly with
no thread) and "the clock inside a block" (`timers.cpp`: the time read after 900 instructions of a block alike on
both engines); each fails with its fix undone. The comments on the battery icon and on the cancels' negative count
now say why. tests/psp 274 groups, with and without the sanitizers; tests/psp/ares 286 checks; tests/allegrex 56
groups; none failed. pspautotests' threads/ and intr/ programs each print what they did before (72 of 167 matching).

## Part 29: lines, bounding boxes and compressed textures

On branch `cursor/psp-ge-features-2b67`, on top of part 28's `cursor/psp-hle-games5-2b67` (#156). The owner's run of
266 games (`origin/local/psp-runner`'s `docs/psp-compatibility.md`) names the GE features its games stopped at: lines
aren't drawn (15 games), bounding boxes aren't tested (8), compressed DXT textures (6), curved surfaces (1); and seven
of the owner's games import sceGeBreak. This part draws lines, tests bounding boxes, decodes DXT textures and adds
sceGeBreak; curved surfaces are left. Sources: pspsdk's headers and GU library (BSD: `sceGuDrawArray` with `GU_LINES`
and `GU_LINE_STRIP`, `sceGuBeginObject` and `sceGuEndObject`, `pspgu.h`'s DXT formats, `pspge.h`'s sceGeBreak, its
`doc/commands.txt`), pspautotests' programs and the results they recorded on a PSP, public descriptions of S3TC, and
the owner's games. No other emulator's code was read. pspautotests' screenshots (`.expected.bmp`) are a PSP's own
frame buffer (its `common.c` writes it to `host0:` on hardware); part 28's scratch runner of their programs, given
their screenshot call (the frame buffer kept, compared with the `.expected.bmp` pixel for pixel), ran the programs
named below through this core, before and after.

**Lines** (`draw.cpp`'s `line()`, `raster.cpp`'s `lineRows()`, `transform.cpp`'s `clipLine()`):
- A pixel wide, by the "diamond exit" rule of OpenGL and Direct3D, which pspautotests' `gpu/exact/lines` names as the
  PSP's. Each pixel has a diamond around its middle, the points less than half a pixel from it counting the distance
  across plus the distance down; a line lights the pixels whose diamonds it leaves, those it meets and doesn't end
  in. Of a diamond's edge, its top and left corners count as inside it, with all its edges but the bottom-right
  one. So a level line along the boundary between two rows lights the row below it, and an upright one the column to
  its right, as the PSP drew them; a line a whole pixel long lights one pixel, the first along it and not the last,
  whichever way it runs; and the pixel holding the point where two lines of a strip meet is lit once, by the second.
  Exact: positions are whole sixteenths, the tests near a line's ends compare fractions multiplied out, and between
  them the line lights a pixel in each column (row, for a steep line) where it crosses the column's middle.
- That reproduces every picture of lines pspautotests recorded on a PSP, pixel for pixel: `gpu/primitives/lines` and
  `linestrip` (now identical, with the 8-bit fix below), and `indices`' lines (what still differs there is PRIM's kind
  7, not lines). It doesn't reproduce `gpu/exact/lines`, which checks 16 pictures (random lines, ends on pixel
  middles, edges and corners in every direction, lines shorter than a pixel, colors along lines, anti-aliasing) by
  their CRCs only: none of them, nor did any of a few hundred variants a scratch model tried against the same CRCs
  (the diamond's edges counted every way; sampling at the middles along the longer axis, either end counted or not,
  direction-aware or not; the other coordinate rounded down, to the nearest or up; the slope cut to 6-16 bits; ends
  snapped to whole pixels). The model's CRCs were checked against a blank picture's (`gpu/exact/coverage`'s
  zero-area jobs), and the core's way through such programs against that sibling program, whose jobs it matches but
  for three kinds of triangles (below). So the PSP's rule differs from these somewhere its pictures don't show; round
  4 of the measuring program records the pixels themselves (below).
- Colors, depth, fog and texture coordinates are blended along a line as at the point where it crosses the pixel's
  middle column (row, for a steep line), held to its ends, with the arithmetic triangles use; flat shading takes the
  second vertex's color; in 2D, texture coordinates are stepped from the left (top) end as for sprites and triangles,
  in 3D blended for the perspective. Lines go through the same pixel pipeline, as jobs in the GE's bands of rows on
  its threads, their textures kept decoded. In 3D a line is dropped by the rules for a triangle's corners (an end off
  the screen, depths outside the range), and cut where it reaches past the near plane (the new end blended from the
  kept one, as a triangle's; with flat shading, a cut-away second end keeps its color). None of that is measured but
  2D's pictures. Anti-aliasing (ANTI_ALIAS_ENABLE, `GU_LINE_SMOOTH`) isn't emulated, and noted once: `gpu/exact/lines`
  shows it changes nothing without blending; what alpha it gives with blending isn't known.

**Bounding boxes** (`draw.cpp`'s `boundingBox()`, `list.cpp`'s BOUNDING_BOX and BJUMP). BOUNDING_BOX tests the count
vertices at the vertex address (pspsdk's `sceGuBeginObject` gives the corners of a box around what follows), and BJUMP
then jumps where JUMP would, if the box was out of sight (`sceGuEndObject` aims it past the object). As pspautotests'
`gpu/bounding` programs recorded on a PSP, every case of `count`, `planes`, `viewport` and `vertexaddr`:
- Each vertex goes through the matrices and onto the screen as one drawn would (its distance from 2048 cut to the
  sixteenth), its position held to the GE's 4096 pixels. Across, it's in sight from a pixel left of the left edge of
  the scissor rectangle and drawing region to their right edge (a pixel's width past their last pixel's left edge);
  likewise down (`viewport`'s "cull box" cases: a vertex at the screen's edge, its offset 3615 or 3616, settles both
  the holding and the edges). With DEPTH_CLIP_ENABLE its z must be between -w and w as well; without it, depth
  doesn't count, nor do MIN_Z and MAX_Z. So a vertex past -w in clip space can be in sight (`viewport`: x -2 lands on
  pixel 0 with a viewport scale of 120).
- The vertices are read as PRIM reads them, and the vertex (or index) address moves on past them the same way.
- No vertices (the count is 16 bits: 0x10000 is none) are out of sight; and of more than 256, only those from 512
  before the end to 256 before it count. Every case of `count` fits that; it doesn't say why.
- Unmeasured, and chosen: a box whose corners are out of sight past different edges is in sight (a box around the
  camera is, so out of sight means every vertex past the same edge, which never drops a box that could be seen);
  vertices behind the camera are where dividing by w puts them; through mode takes positions as drawn (a vertex in
  sight was recorded in sight). Round 4 measures these.
- `count`, `planes` and `viewport` now print what the PSP printed, line for line, and `vertexaddr` too but for the
  addresses, which it reads out of sceGeSaveContext's buffer (Phobos's own layout; the PSP keeps the vertex and index
  addresses and the offset in its words 5-7). The result is kept in states, as a list may stop at its stall address
  between the two commands: version 12 (11 refused).

**DXT textures** (`texture.cpp`'s `dxtTexel()`), TEXTURE_FORMAT 8-10. pspautotests' `gpu/texcolors/dxt1`, `dxt3` and
`dxt5` recorded a block's first texel on a PSP, color and alpha, for 2216 cases; this core gives every one of them (it
gave none before). From their recordings and their programs' structures (the layout came from there; none of the six
DXT games of the report is on this Mac, and none of the fourteen here met a DXT texture in the runs below):
- A block of 4x4 texels starts with its colors, unlike S3TC's own layout: 4 bytes of 2-bit indices (a row a byte,
  the leftmost in the low bits), then two 16-bit colors, RGB 565 with red in the top bits (unlike the GE's 5650),
  widened by shifting alone (31 becomes 248). With the first above the second (as numbers), indices 2 and 3 are a
  third and two thirds of the way, each 8-bit channel (2a + b) / 3 and (a + 2b) / 3 rounded down; otherwise 2 is half
  way, rounded down, and 3 black, transparent in DXT1.
- DXT3 then has each texel's alpha in 4 bits (two bytes a row), widened by shifting alone (15 becomes 240); DXT5 48
  bits of 3-bit indices, then its two alphas: with the first above the second, six steps between them,
  ((7 - k) a + k b) / 7 rounded down; otherwise four, ((5 - k) a + k b) / 5, then 0 and 255. Index 3 of a DXT3 or
  DXT5 block whose colors aren't in order is black, its alpha its own.
- Taken, not recorded: the blocks go a row after another, TEXTURE_BUFFER_WIDTH0 / 4 to a row (S3TC's order; the
  recordings repeat one block four times), the buffer width's low two bits ignored; DXT5's indices past the first
  texel's in order; TEXTURE_MODE's swizzling doesn't apply. Round 4 records the order, odd widths and swizzling.
- DXT textures are kept decoded like the others (decoded once into 8888, watched, dropped when their memory changes),
  and read from memory otherwise; a palette never takes part.

**sceGeBreak** (`kernel/ge.cpp`), as pspautotests' `gpu/ge/break` and `breakwait` recorded: a mode but 0 or 1 is
INVALID_MODE, then parameters whose 16 bytes reach the kernel's half of memory PRIV_REQUIRED (none is fine), then an
empty queue ALREADY. Mode 0 breaks off the list the GE has and returns its ID: the list is left paused at the
queue's front (sceGeListSync says 4: chosen), the GE runs nothing (sceGeSaveContext works again), and sceGeContinue
takes it up where it was; a list paused by a PAUSE signal is BUSY. Mode 1 throws every list away and returns 0; the
next list takes the first ID again; threads waiting for a list or for all drawing aren't woken, and wake as a list
ends (one with their list's ID, or the queue left empty). (`pspge.h` has the two modes' results the other way round.)
`break` now prints what the PSP did but for its addresses (the context's layout, as above), and `breakwait` but for
the order its two waiters wake in as the new list ends (the PSP woke the one waiting for all drawing first; the core
wakes the list's own first, as it always has). What the lists thrown away saved of the GE's state isn't put back.
Their finish and signal callbacks still waiting their turn are dropped, and one running returns to no GE to go on
(after review, below; unmeasured).

**Found on the way, from pspautotests' pictures**, and fixed: 8-bit positions in through mode read as 0 (`points`
recorded a PSP drawing two such points at (0, 0), `triangles`, `lines`, `linestrip` and `rectangles` nothing of
theirs): those five pictures are now identical. Index format 3, which `pspgu.h` doesn't name, holds 32-bit indices of
which the GE takes the low 16 bits (`indices` drew with them, `indices32` drew 0x10000 and 0xffff0000 as 0,
`vertexaddr` moved 4 bytes an index): `indices32` now prints what the PSP did. Seen and left: three kinds of
`gpu/exact/coverage`'s triangles (huge ones, a long edge, tall edges: 7 of its 15 jobs, where the program's comment
names the PSP's snapping of very tall edges to 4-pixel spans); PRIM's kind 7 and vertices sent in the list
(`continue`, `immediate`, `indices`' last part); sceGeSaveContext's layout; `vertices/texcoords` and `morph`.

**The measuring program's round 4** (`tools/psp-measure`: the menu's new first line, "Round 4: the GE's lines,
boxes and DXT", about 4 MB, `manifest4.txt`; `ge.c` says what each draws): lines with both ends at every sixteenth of
a pixel (shallow, steep, diagonal, rising, level, upright; the first two drawn backwards too; and anti-aliased over
black), lines shorter than two pixels in 16 directions, strips added up so a pixel lit twice shows 2, colors, depths
(through VRAM's fourth copy) and texels along lines, lines in 3D with sub-pixel ends and cut at the near plane; 256
bounding boxes, each filling its cell only if the GE took it to be in sight (around the scissor rectangle's edges by
sixteenths, past each edge, past different edges at once, past the near and far planes with DEPTH_CLIP_ENABLE on and
off, behind the camera, in through mode, and random); DXT1, DXT3 and DXT5 textures of 256 random blocks each, colors
and alphas; and the blocks' order, with buffer widths of 32, 64 and 36 and swizzling on. Running it in this core found
a mistake in it before any PSP did: memory from `sceGuGetMemory` inside an object lets the GE run up to a BJUMP not
yet aimed, which then jumped to address 0; the object's memory is now taken before it begins. It was built in
pspdev's Docker image, and `tests/psp/programs/pspmeasure.elf` replaced (that folder's convention: its README has
the new hash); `tests/psp/measure.cpp` runs round 4 through the menu too and checks its 20 files, to be compared with
a PSP's once it has run (a `manifest4.txt` in the results tells).

**Tests** (`tests/psp/run-tests.sh`: 280 groups (281 after review, below), with the address and undefined-behavior
sanitizers; `tests/psp/ares`: 290 checks; none failed):
- `draw.cpp`: "draw lines" (pspautotests' three pictures, moved to the canvas; ends on pixel middles each way, steep
  ones, the boundary rows and columns; a strip's joints drawn once; lines too short to leave a diamond, and one just
  long enough; scissoring; colors and depth along a line; flat shading; texels along it), "draw DXT textures" (37 of
  pspautotests' recorded cases, the blocks' order with a wider buffer, each format drawn kept decoded and read from
  memory, also after its memory changes), "draw vertex formats" (the 8-bit points at (0, 0), 32-bit indices);
  "draw textures kept decoded against memory" now draws DXT textures too.
- `draw3d.cpp`: "draw3d lines" (on the screen, depth along it, the near plane's cut and its colors, DEPTH_CLIP_ENABLE
  off, flat shading, fog, an end off the screen) and "draw3d bounding boxes" (pspautotests' recorded cases of planes,
  viewport, count and vertexaddr, by `boundingBox()`, then BJUMP in a list, relative to an ORIGIN, both ways, and a
  list stopped between the two commands, its state carried on in another machine).
- `ge.cpp`: "ge break" (break's recorded results, and breakwait's waiters), and "ge drawn on several threads", whose
  frame now has lines, a strip, a DXT texture and two boxes (one skipping a sprite), alike at 1, 2, 4 and 8 threads;
  `states.cpp`: "state fields" changes the box's result.
- The comparison with the owner's PSP (rounds 2 and 3, "psp measure") is the same, line for line, before and after.
- Sixteen broken versions each failed them: a diamond's top corner left out, a line's last pixel lit, flat shading
  from the first vertex, no cut at the near plane ("draw lines", "draw3d lines"); a box out of sight with any
  vertex out of sight, without its pixel of slack, with every vertex counted, BJUMP never jumping, the result left
  out of states ("draw3d bounding boxes", "state fields"); DXT's colors widened by repeating their top bits, its
  layout S3TC's own, DXT5's sevenths rounded to the nearest ("draw DXT textures"); 32-bit indices kept whole, 8-bit
  positions read in through mode ("draw vertex formats"); a break of everything freeing its lists to the back of
  the free list, a paused list broken off ("ge break").

**The games** (the host Mac; `origin/local/psp-runner`'s runner built from scratch copies of this tree and of part
28's, FFmpeg on in both, the same presses, PNGs at the same frames; pictures in `/tmp/gef-pictures`, outside the
repository). In the first minute (the compatibility run's presses, Start at frame 120 and cross at 1800), and longer
for the GTAs (to 9000 frames, cross every 300): Burnout Dominator meets its first line at frame 98, Sindacco
Chronicles at 781, Space Invaders Extreme at 1113, Liberty City Stories at 7616 (in the city); SOCOM's first bounding
box comes at frame 8, Snoopy's at 1. None of the fourteen met a DXT texture, curved surface or sceGeBreak call.
- Space Invaders Extreme's title now has its "PRESS START BUTTON" framed in a box of lines
  (`sie-title-before-after.png`). In Sindacco Chronicles' city a thin, faint line now crosses the sky
  (`sindacco-sky-crop-before-after.png`). Burnout Dominator's and Liberty City Stories' frames captured are the same
  before and after: what they draw with lines isn't in those pictures.
- SOCOM's and Snoopy's frames are the same (boxes now tested drop nothing that was seen: their menus, and the
  frames up to 9600 of Snoopy, which these presses kept at its warning about saves); Vice City Stories' and Peace
  Walker's too (no lines, boxes or DXT met).

**Speed** (the GE's existing work isn't slower): both runners built with -O3 and the Android build's other speed
flags (no LTO), each from a state of its own of the same scene, 600 frames, best of three, host frames a second, one
GE thread and seven: Liberty City Stories in the city 22.0 / 22.1 and 70.8 / 69.9; Lumines 57.8 / 58.0 and 197.8 /
198.3; Peace Walker at frame 3600 336.7 / 337.2 and 1256.2 / 1256.1; Snoopy (boxes tested now) 94.3 / 95.3 and 360.2
/ 360.9; Sindacco (lines drawn now) 39.8 / 39.7 and 89.4 / 88.0; Space Invaders Extreme's title (its box drawn now)
263.1 / 260.5 and 942.2 / 953.9. Within a couple of percent, either way; a line job is set up only where there are
lines.

**Left, and why**: curved surfaces (BEZIER and SPLINE: one game of the report, Macross Ace Frontier; round 3's five
pictures of the PSP's patches are there to fit them to, a part of their own); the exact line rule and anti-aliased
lines (round 4); the DXT blocks' order, odd buffer widths and swizzling, and what a box past different edges or behind
the camera is (round 4); the wake order in breakwait; sceGeSaveContext's layout; `gpu/exact/coverage`'s tall and huge
triangles; PRIM's kind 7 and vertices sent in the list. The six DXT games and the other line and box games of the
report aren't on this Mac.

**After review.** The review found the clean room kept (every fact shared with PPSSPP is in pspautotests'
recordings), hostile lines, boxes and DXT textures clean under the address sanitizer and fast, and round 4 running
right in the core (143 BJUMPs taken, 113 not, every file written). One thing changed: sceGeBreak(1) left the GE's
callbacks that were waiting their turn (interrupts held off, or another call running) in the queue, so one ran
later for a list that was gone or whose ID a new list had, and as it returned ended the new list ahead of that
list's own finish callback. The GE's callbacks are now a kind of call of their own (`Call::Ge`, in states as the
kind already was: version 12 still), and a break of everything drops them, and has one that's running return to no
GE; unmeasured (pspautotests doesn't record a PSP at it), and noted so in `kernel/ge.cpp`. Test: "ge break and
callbacks" (`ge.cpp`, both engines: the break with the old list's finish callback waiting, then the next list taking
its ID, its callback alone running, once and first, the list still queued, the state round trip while it waits; and
the break from a finish callback that enqueues the next list itself); each half fails with its fix undone; "state
fields" changes the call's kind and refuses a GE callback that's a vertical blank's too. tests/psp 281 groups with
the sanitizers, tests/psp/ares 290 checks, none failed. On the RP6, Space Invaders Extreme's title shows its box of
lines at 60 fps, and Liberty City Stories' intro is unchanged at 60 fps (the owner's check of the build before this).

## Part 30: the GE faster again, four pixels at a time

On branch `cursor/psp-ge-speed2-2b67`, on top of part 29's `cursor/psp-ge-features-2b67` (#157). The owner's priority
(Decisions, 2026-10-06): the software renderer as fast as humanly possible, every picture exactly as it was; the
Vulkan and OpenGL renderers come later, apart. After part 24, Midnight Club 3 was still the worst case: its night race
about 7 frames a second on the host in part 25's -O2 runner, its profile menu 25 in the desktop program, "spent in the
GE's pixel loops"; on the RP6 the menu drew at 22 and the race at 18. Original code: no PPSSPP or JPCSP source was
read.

**The scenes.** Six, each from a state of its own (a scratch runner, states and scripts in `/tmp/ge2-bench`, never
committed; the games are the owner's): Midnight Club 3's night race (a Quick Race downtown, timed with Cross held) and
its profile menu ("Load Profile / Create Profile / Delete Save Data" over the city); GTA Liberty City Stories' opening
in the city (part 29's state) and Toni by the car in the woods (play, the HUD up); Peace Walker's title; Lumines'
demo. Burnout Dominator's race wasn't reached: past its profile and World Tour it loops tutorial movies that neither
held nor tapped buttons left (part 25 measured its races light on the GE).

**Where the time went.** Instruments' Time Profiler (`xctrace`), each sample's address taken back to its inlined
functions and source line (`atos -i` on the LTO build's kept object and dSYM), the core built with the Android build's
flags (`-O3 -flto=thin -ftree-vectorize -funroll-loops -fno-math-errno -fno-trapping-math`), one GE thread:
- GTA's city: the pixel pipeline (`drawPixelAs`) 24%, a triangle's interpolation 16% (up to eleven float divisions a
  pixel), looking texels up and filtering 20%, the texture function and its clamps 12%, setting primitives up 6%. Half
  its pixels are one state: 3D, a filtered 4-bit palette texture modulated and doubled, Gouraud colors, the alpha
  test, the depth test and write, alpha blending and fog.
- Peace Walker's title (1.9 million pixels a frame, 3D sprites into a 5551 frame buffer): the pipeline 41% (blending,
  dithering), the filter 31%.
- Midnight Club 3's menu: 2.8 million pixels a frame, 31% of them faint additive glows, two thirds of which fail the
  depth test after their texels were looked up and blended; 17% of the time in primitives drawing over their own
  texture (post effects), drawn at once on the GE's own thread with every texel through `Memory::read()`. Its race
  sets up 3,500 primitives, 117,000 vertices and 110,000 triangles a frame, decodes 47 textures (760,000 texels) again
  a frame (render targets, and textures whose palette changed) and changes the palette about 1,000 times.

**What was done** (`ares/psp/ge`), each step measured, and checked identical before the next:
1. **Four pixels at a time** (`four.cpp`, new). A triangle's or sprite's row is drawn in fours, four pixels side by
   side from a column that's a multiple of four, each in a lane of GCC's and Clang's own vector types (NEON on ARM64,
   SSE on x86-64): the interpolation (colors, depth, fog, perspective-correct texture coordinates, 2D steps in
   doubles), the texel axes, the four texels (read two at a time where the right one follows the left) and the filter
   (two channels in each lane's 32 bits, as filtered() has four in 64), the texture function, the depth range, alpha,
   color and depth tests, fog, blending, dithering, and the writes through MASK_COLOR and MASK_ALPHA. Exact by
   construction:
   - Whole numbers: the same operations lane by lane, in 32 bits, none of which overflows.
   - Floats: the same expressions, written the same way with a vector in every product. Where the host fuses
     multiply-adds (ARM64), the compiler fuses a product into its sum by the expression's shape, for vectors as for
     single numbers (checked in the code it makes: fmul, then two fmla, then fdiv, as the scalar code's fmul, fmadd,
     fmadd, fdiv; on x86-64 neither fuses); a product of two single numbers, made a vector afterwards, wouldn't be
     fused, so none is written that way. Conversions are the scalar ones lane by lane; where x86-64's four-lane
     conversion of a number that isn't one differs from its single one (a depth), that lane is said outright.
   - The order: four pixels read and written at once see what one after another did, as no pixel of a primitive
     depends on another but through the frame and depth buffers; fourFriendly() takes only jobs whose frame and depth
     buffers don't overlap, and whose texture is kept decoded.
   - The depth test comes first, before the texture: without the stencil test, failing any test only drops the pixel.
   - A four wholly inside its row reads and writes its pixels at once; one reaching past it, only its lanes inside it.

   Points, lines, the stencil test, logic operations and the jobs fourFriendly() turns down are drawn a pixel at a
   time, as before. `GE::fourPixels` turns the path off, for the test below.
2. **Textures decoded a row at a time; the palette's hash eight bytes at a time** (texture.cpp): decodeRows() gives
   each texel exactly as texelFrom() reads it, with a row's start (and a swizzled texture's blocks) worked out once a
   row and a palette's entries once a texture (once an index for 4- and 8-bit indices). DXT, and textures read through
   VRAM's rearranging copies, decode as before. The palette's hash only finds decoded textures, each checked byte for
   byte against the palette, so no picture depends on it.
3. **Jobs keep only their own kind's numbers; directional lights once a primitive.** A job carried a sprite's, a
   triangle's, a point's and a line's numbers side by side (584 bytes), all zeroed and copied into the batch; they're
   a union now (256 bytes). A directional light's way to it, made one long, and the half way between it and the
   viewer's, are the same at every vertex: lightingState() works them out with the very operations light() used at
   each.
4. **A texture read as it's drawn reads its bytes straight from the host's memory** (`direct()`): where all of a
   primitive's texture's bytes are side by side in the host's memory (VRAM's first and third copies, or RAM: nothing
   rearranged, nothing unmapped to report), texel() reads them from there, the very bytes `Memory::read()` gave; such
   a primitive is drawn at once, after what waits.
5. **A vertex's place past the depths once a vertex** (transform.cpp): outOfSight() divided z by w at every corner of
   every primitive, a strip's vertex three times; project() now does it once, with the same division.

Tried and dropped: handing a batch to the workers once it holds 2^17 or 2^18 pixels of work, so that they draw while
the GE's thread sets the rest of the list up (no gain: at seven threads Midnight Club 3's GE thread is busy all the
time and the drawing threads idle five sixths of it); workers spinning up to 1 ms before sleeping (fewer bands left to
the GE's thread, 61 to 40 a frame, no faster).

**How it's known to draw the same pixels:**
- The six scenes, 300 frames from each state: every frame's picture and all of VRAM, and RAM at the end, identical to
  part 29's at 1 and 7 threads, after each step.
- The differential fuzzer of part 24 (scratch), brought up to part 29's GE as the reference: thousands of random lists
  at each step (every pipeline and texture setting, palettes, swizzling, render to texture and drawing over its own
  texture, 3D with lighting and clipping, lines, transfers), at 1, 3, 4, 6 and 8 threads, batches shared however small
  and drawn past the list's end; under the address and undefined-behavior sanitizers, and under ThreadSanitizer, which
  caught the one race the four-pixel path had (step 1's last point, a commit of its own): a row's first or last four
  wrote back the bytes of its lanes outside the row, which where a frame buffer's rows are barely longer than the area
  drawn (a stride within three pixels of its width, the area not on a multiple of four) are another row's, maybe
  another thread's at that moment. No scene drew that way, and the fuzzer's pictures had stayed identical.
- A new group, "draw3d four pixels at a time against one" (`tests/psp/draw3d.cpp`): 20,000 random primitives (2D and
  3D in perspective, triangles, strips, fans and sprites, lit now and then with the shine kept apart, often
  one-colored; random pipeline and texture settings, textures up to 256 a side) drawn by two machines alike but for
  `fourPixels`, over the same random VRAM; frame and depth buffers compared after each, all of VRAM at the end. Broken
  versions it fails: the dither's columns turned round, a blend term shifted 9, fog rounded with 254, GEQUAL taken as
  GREATER, the filter's fraction in eighths, modulation without its + 1, a blended channel held to 254, the products
  of a color's, the depth's or a texture coordinate's sum taken in another order, a 3D sprite's fog halves split a
  sixteenth off, the stencil bits not kept, decal's + 1 dropped, 5551's alpha as 254, the clear's depth not written,
  all four lanes written where some are dropped, the texels read in pairs where they aren't side by side, the fog's
  sign not tested, a 3D sprite's coordinate down y taken from the wrong lane (19 in all). (A product taken out of a 2D
  texture coordinate's sum of doubles, so not fused, got through: it shows only where the double's last bit decides
  the float.) The fuzzer catches those too, and broken palettes and swizzling in decodeRows().
- tests/psp: 282 groups with the address and undefined-behavior sanitizers, none failing; "psp measure", the
  comparison with the owner's PSP's round-3 GE measurements, the same as part 29's line for line. tests/psp/ares: 290
  checks.

**Measured** on the host (Apple M1, 4 fast and 4 slow cores; a security agent kept about two cores busy throughout),
the core built with the Android build's flags, 300 frames from each scene's state, host frames a second, best of
three, part 29's and this part's runs taking turns, the picture hashed each frame (drawing may go on across frames, as
in the app):

| scene | part 29, 1 thread | part 30, 1 thread | part 29, 7 threads | part 30, 7 threads |
| --- | --- | --- | --- | --- |
| Midnight Club 3, night race | 11.6 | 27.8 (2.4x) | 26.6 | 40.9 (1.5x) |
| Midnight Club 3, profile menu | 11.6 | 32.0 (2.8x) | 29.0 | 51.7 (1.8x) |
| GTA Liberty City Stories, the city | 29.0 | 77.5 (2.7x) | 75.8 | 131.5 (1.7x) |
| GTA Liberty City Stories, the woods | 20.6 | 47.4 (2.3x) | 60.7 | 108.2 (1.8x) |
| Peace Walker's title | 29.2 | 106.2 (3.6x) | 135.2 | 487.9 (3.6x) |
| Lumines' demo | 59.0 | 111.9 (1.9x) | 197.7 | 341.8 (1.7x) |

The pictures of those timed runs are the same frame for frame too, at both thread counts. By step, one thread (other
work sharing the Mac, so within about 10%): four pixels at a time took the race from 11.6 to 23.8, the menu 11.3 to
27.7, the city 29.1 to 73.8, the woods 20.6 to 47.7, Peace Walker 29.4 to 96.1, Lumines 51.4 to 63.6; the paired texel
reads and the two-channel filter, then decodeRows() and the palette's hash, to 27.7, 30.7, 79.1, 50.2, 107.2; reading
textures drawn over straight from memory, Lumines 63.6 to 114.9 and the menu to 33.4. At seven threads the GE's own
thread is the limit (below).

**The profile after.** One GE thread, Midnight Club 3's menu: the four-pixel path about 57% (the pipeline 15, the
interpolation 14, texels and filter 9, the depth first 5, the texture function 2), primitives drawing over their own
texture 17% before step 4, the CPU's code and the rest of the machine the remainder. Seven threads, its race: the GE's
own thread is busy all the time and the seven drawing threads together about one sixth of it; on that thread the CPU's
recompiled code and interpreter take about 40%, setting primitives up 19% (lighting 4, triangles 3, projection 2,
vertices 2, matrices 2), its share of the drawing 13% (small batches, primitives drawn at once, and helping as it
waits for a batch whose picture it needs: six such waits a frame, render targets read back as textures), decoding 3%.

**On the RP6** (the app built from this branch installed over part 29's, data kept, and part 29's put back the same
way for "before"; each game launched from its CHD, "PSP Drawing Threads" on Auto, the performance overlay
screenshotted and the app's own "Emulation Stats" lines, each second's frames and the emulation's work a frame, taken
from the log; then force-stopped):
- Midnight Club 3's profile menu (Start, then Cross past the logos): 21-23 frames a second before, 36-37 after (its
  work a frame 44-48 ms before, 26-28 after).
- Its night race (Create Profile, Quick Race, the car at the start line): 17-19 before; 30-34 after over three runs.
  The device picks the race at random, so they're three courses (downtown, the pier, a circuit) against downtown
  before.
- Peace Walker's title, Lumines' demo, Liberty City Stories' intro: 60 before and after. Their work a frame (1-8 ms)
  is within what the core the emulation's thread lands on and its clock change from run to run (Peace Walker's ran on
  the fast core at 2.48 GHz before and on a middle one at 1.65-2.32 GHz after: 1.1 ms then, 1.7 now; Lumines 8.4 then,
  7.1).

In Midnight Club 3 the emulation's thread keeps the fast core 92-99% busy in both builds: as on the host, the GE's own
thread is the limit there now. Screenshots and the stats in `/tmp/ge2-device`, outside the repository.

**What's left for more speed:**
- The GE's own thread, now the longest path in Midnight Club 3 and GTA at several threads: the CPU's emulation (its
  recompiled code and the instructions it hands to the interpreter, FPU and VFPU) is the most of it, outside the GE.
  In the GE, vertices (117,000 a frame, 72,000 lit) and triangles (110,000) are set up one at a time: transforming and
  lighting them four at a time, or on the drawing threads, is next; and a GE running beside the CPU rather than inside
  its calls (the PSP's own is) would take setup off the CPU's thread altogether.
- Primitives drawing over their own texture are still drawn a pixel at a time on the GE's thread: four at a time is
  exact where no texel a pixel reads can be one an earlier pixel of the same primitive wrote (a 1:1 copy in place, for
  one), which can be worked out from the texture's address and coordinates.
- Waits for render targets read back as textures (six a frame in Midnight Club 3) keep the GE's thread drawing;
  drawing only the bands the texture comes from would shorten them.
- In the four-pixel path: 16-bit lanes (eight pixels a vector) for the color arithmetic that fits, triangles' rows
  stepped instead of divided, and the remaining divisions (a pixel's perspective takes five, its colors four).

## Part 31: movies through scePsmf and scePsmfPlayer

On branch `cursor/psp-psmf-2b67`, on top of part 30's `cursor/psp-ge-speed2-2b67` (#159). Games play PSMF movies
two ways: with sceMpeg, feeding it the program stream themselves (part 25 and 26), or with Sony's movie player
library, scePsmfPlayer, which reads a movie's file, decodes it and hands the game pictures and sound; and games of
the first kind read the movie's header with scePsmf. The two libraries aren't the firmware's: games ship them as
modules of their own (`psmf.prx`, `libpsmfplayer.prx`). 19 of the owner's 266 games stopped at them in the
compatibility run (scePsmfPlayerCreate in 8, scePsmfSetPsmf in 7). This part gives both libraries, their 53
functions (25 and 28), written in a clean room.

**How it was made.** Part 28 had left these libraries for want of a source this project may use: pspsdk has no PSMF
header, pspautotests no scePsmf program, and the container's fields are documented nowhere allowed here. With the
owner's approval (2026-10-07), two agents worked apart. A separate agent read other emulators' code and pspautotests'
recordings and wrote a facts-only specification of the PSP's behaviour, in its own words, with each fact's source
(a PSP recording, the movie files, both emulators, one of them): the container, both libraries' functions and
errors, the player's statuses and timing, and the uncertainties it found. This part was written from that
specification, from pspautotests' `video/psmfplayer` programs and the results they recorded on a PSP (read directly:
the strongest evidence), from the movies' own headers and streams (the test movies, and the owner's games' read in
place), from this repository's code and docs, and from the games' behaviour. The specification's author's sources
weren't seen here, and no other emulator's code was read, copied, translated or reworded. Where the specification
lists an uncertainty, the code chooses, says so where it chooses, and follows pspautotests' recordings wherever they
say anything (the choices are listed below).

**The container** (`kernel/psmf.cpp`'s comment): a header, then an MPEG-2 program stream of 2048-byte packs carrying
H.264 pictures (stream 0xe0 plus a channel) and ATRAC3plus sound (private stream 1, its packets' data a 4-byte header
then frames behind 8-byte headers, as part 26 takes them for sceMpeg). The header: "PSMF", a version ("0012" to
"0015"), where the program stream begins and how long it is (big-endian words at 8 and 12), the presentation's start
and end in 90 kHz ticks (six bytes each, at 0x54 and 0x5a: 90000, and the last picture's time plus 3003, in every
movie seen), a stream table (a count at 0x80, then 16-byte entries: the PES stream ID and private ID, where the
stream's EP map is and how many entries it has, a video stream's size in 16-pixel units, a sound stream's channels
and rate code), and the EP maps (10-byte entries: a picture's time and the pack its decoding can start from). The
movies looked at, with a scratch script (not kept): pspautotests' three (144 by 80, 190 pictures, no sound, no EP
map; `test_streams.pmf`, whose header lists two video and two sound streams and whose packs start 16 bytes late),
Burnout Legends' `englis30.pmf` (with sound and three EP entries), SOCOM's `c0919.pmf`, and Peace Walker's
`pw_op_ENG.pmf` and its 16-minute `AD0420_m_c00_c149.pmf` (version 0015, 114 MB, no EP map). All are I and P pictures
only, and FFmpeg's decoder gives a picture for each access unit, none held back; most access units carry no time
stamp, and the first sound frames are stamped a little before the presentation's start.

**scePsmf** (`psmf.cpp`): a game loads the header into its memory and sets up a structure of its own over it
(scePsmfSetPsmf), which every other function takes. The structure is 32 bytes (the size both of the specification's
accounts write, so a game's structure of either size isn't written past): the version word, 0x800 (the header's
size, as both accounts report it whatever the stream's offset), the stream's size, two zeros (the grouping period's
and group's numbers), the current stream's number and the header's address, as the more detailed account lays them
out, and a word of Phobos's own (the current stream's kind and channel, and the video stream whose EP map is read).
Everything is in the structure and the header, so a copy of a structure works on its own, with its own current
stream, as games rely on. The functions: SetPsmf and VerifyPsmf; QueryStreamOffset and QueryStreamSize (from a
header, unchecked); GetPsmfVersion, GetHeaderSize, GetStreamSize, GetPresentationStartTime and EndTime (absolute,
as words); GetNumberOfStreams and GetNumberOfSpecificStreams (kinds 0 H.264, 1 ATRAC3plus, 2 PCM, 3 user data, 15
either kind of sound); SpecifyStream, SpecifyStreamWithStreamType (kind and channel),
SpecifyStreamWithStreamTypeNumber (the n-th of a kind); GetCurrentStreamNumber and GetCurrentStreamType;
GetVideoInfo and GetAudioInfo; the EP map's
GetNumberOfEPentries, CheckEPmap (NID 0x971a3a90 is that spelling's hash, with a small m), GetEPWithId,
GetEPWithTimestamp and GetEPidWithTimestamp; and the marks' GetNumberOfPsmfMarks and GetPsmfMark.

**scePsmfPlayer** (`psmfplayer.cpp`): one player at a time (a second Create: 0x80618005), its statuses 1 (made), 2
(a movie set), 4 (playing) and 0x200 (played to its end), each function refusing a handle word of 0, a value no player
has, and every status its table in the recordings refuses (0x80616001). The handle is the address of a word that
points at a word holding 0, the shape basic printed of the PSP's (two words of kernel memory beside the trampoline
here); any copy of it works alike, and Delete zeroes the word it's given. The movie is read through a file
descriptor of the game's (as a PSP's library opens it through sceIo), from the disc, the memory stick or any device,
at an offset in its file (SetPsmfOffset), 64 KiB at a time, as far as the header's stream size; the packs are taken
apart by the PES walker sceMpeg now shares (`pesPackets()`), the pictures decoded by codec.cpp's decoder and the sound
frames by its ATRAC3plus decoder through the same helper sceMpegAtracDecode now uses (`atracUnitDecode()`), and the
pictures converted by sceMpeg's BT.601 conversion (`pictureConvert()`). How playing goes, as the recordings show it:
- Start-up: three pictures decoded ahead (and three sound frames, with sound) before the first comes; one is decoded
  each GetVideoData call, so the first picture comes on the third (basic: two refused with 0x8061600c, the third gave
  time 0). Meanwhile GetVideoData gives the picture shown last if there is one (a restart keeps it), else
  0x8061600c, and GetAudioData nothing.
- Pacing: Update, meant to come once a vertical blank, counts down toward the next picture: in play mode one is due
  two Updates after the last was handed out (basic, playmode: 49 pictures in 99 Updates), the count starting again
  at each (getvideodata and update, three Updates a picture: the end still on the second Update after the last).
  GetVideoData hands out at most one new picture a call, and writes the current picture into the game's buffer at
  every call that succeeds (pause, and after the end, the same picture again).
- Sound: a due picture is held while it's more than two pictures ahead of the next sound frame waiting to be taken,
  and two pictures are passed over while the sound is more than four ahead (the game's taking of the sound is the
  clock); the first frame after a start comes out silent; frames more than a frame before the start, or two pictures
  before the first picture, are dropped. The recordings' movies have no sound: these are the specification's more
  detailed account. Only ATRAC3plus is played (no movie seen has PCM).
- The end: everything decoded, the last picture handed out and two Updates since, and the sound all taken; then, not
  looping, status 0x200, at once when the player's priority (Create's) is better than the calling thread's, else as
  that thread next waits (getvideodata and update recorded both: during the second Update with 0x17 against the
  test's 0x20, at the next GetVideoData with 0x28). Here the player has no threads of its own: the end waits for the
  caller's next call that blocks, or for the next vertical blank, by which the PSP's threads have surely run.
  Looping, playing starts again from the start in play mode, the status staying 4.
- Play modes: slow motion a picture every six Updates (playmode: 17 in 99), pause and step frame none, but the
  picture already due as the mode changed still comes in all three (playmode: one more after switching to pause or
  step frame). Fast forward and rewind need a movie whose every video stream has an EP map (else 0x80616003, as
  recorded) and show its entries' pictures, the speed's number of entries on, one each 15, 20 or 25 Updates; rewound
  to the first entry it plays on from the start, forward past the last ends the movie (the specification's account;
  no recording, since no test movie has an EP map). Start at a time, in such a movie, begins at the entry at or
  before it, the pictures before it decoded but not handed out.
- Calls that block (the recordings' marks): Create, Delete, ReleasePsmf, the SetPsmf functions (the header's reading
  time on its device, even for a file that isn't there; not when refused for the status or a NULL path; the CB ones
  with callbacks), and GetVideoData when it succeeds; how long isn't measured (`Wait::Psmf`, a new kind of wait whose
  call returns its result as it ends).
- Pictures: as many rows as the picture has, each as many pixels as its width, the buffer width apart (0 for 512, an
  odd one less its lowest bit, a narrower one 0x800001fe, a negative one 0x80000023, a NULL buffer 0x80000103, as
  getvideodata recorded), the rest of each row left alone; in the pixel format ConfigPlayer set (8888 at first), with
  alpha zero in every format: configplayer told the PSP's formats apart by their zero alpha bits.

**sceMpeg's alpha** stays opaque: none of pspautotests' `video/mpeg` programs records a pixel (their results are
return codes, sizes and addresses), so nothing shows a PSP's sceMpeg giving zero alpha.

**Which modules the HLE stands in for.** As the module manager decides for Sony's modules (part 19): by the module's
name, "sce" or "Sce" first, or its being a kernel module. Peace Walker's `psmf.prx` and `libpsmfplayer.prx` are
`scePsmf_library` and `scePsmfP_library` (their ~PSP headers' names), so they're stood in for, as is the
specification's `scePsmfPlayer`. The names other games give the player's library (`libpsmfplayer`, `psmf_jk`,
`jkPsmfP_library`) aren't Sony's by that rule: such a module is loaded and run as a game's own code, its module_start
too; but the kernel's functions go before a module's exports as imports are linked (part 19), so a game's calls to
scePsmfPlayer reach the kernel's player whatever its library's module is called, and the module's own code sits in
memory unused. No rule special to these libraries was needed.

**States**: version 13 (12 refused). The player: its status and settings, its movie (the descriptor, the PSMF's place
in its file, the header), where reading is, the streams' data read and not yet taken with their time stamps, the
pictures decoded ahead and the one shown, the start-up, pacing and end, and the last sound frame decoded; and threads
waiting for it. The decoders are made afresh: the sound's primed with that frame, the pictures' passing over pictures
until one it can start from (an IDR picture, or one of I slices alone: `keyframe()`, as sceMpeg's after a load),
the pictures decoded ahead shown first. Loading checks a status there is, a priority Create takes, a pixel format and
play mode as they're set, the movie exactly while its status is 2 or more (a header that is one, as long as its
stream's offset, and a file number handed out), reading within the stream, stamps inside their data and in order,
three pictures ahead at most, each of its size and no larger than a movie's, a sound frame of a frame's size, and a
player's wait due no later than a header's reading could take.

**The choices made where the specification is uncertain** (each also noted in the code):
- scePsmf: a structure not set up gives 0x80615001 from the functions about streams and 0x80615025 from the rest;
  SetPsmf refuses a header without "PSMF" (0x80615501), a version word of 0 (0x80615002) and a stream offset of 0
  (0x806151fe); VerifyPsmf takes the four known versions only (0x80615501) and leaves the 0x100 bytes below the
  caller's stack pointer zeroed (one game reads a word there a PSP leaves zero); GetPsmfVersion gives the digits as a
  number (14); SpecifyStream with a number no stream has gives 0x80615100 and leaves no usable selection
  (0x80615001 then), a search by kind and channel that finds none gives 0 and no current stream (its number
  0x80615100, its kind and channel those selected before), kind 15 matching either kind of sound; GetVideoInfo and
  GetAudioInfo answer from the current stream's entry; the EP map read is the video stream selected last's, its
  positions the pack counts as stored (not bytes), an index past it 0x80615100, the entry for a time the last not
  after it, a time before the start 0x80615500; PCM is private IDs 0x10-0x1f and user data 0x20-0x2f (and anything
  else); no marks (none seen, their layout unknown); an EP map not lying whole inside the header is none, and an end
  before the start is the start (both after review, below).
- scePsmfPlayer: Create checks the priority before the size; SetTempBuf checks the status before the size, and the
  size whatever the address; a negative sound stream at Start plays without sound (-1 and -1 reported); Start's
  modes past 5 refused in a full movie too, as are negative times; switching streams (Select*) needs two streams of
  the kind whose data the program stream's first 64 packs carry (`test_streams.pmf`, two of each listed and none
  there, was refused on a PSP); step frame asked for again while in it steps a picture; a restart keeps the picture
  shown until the next; the movie is played to the header's stream size whatever the library's version; the sound
  thresholds are the specification's detailed account's; the unknown NID 0x340c12cb returns 0; calls from an
  interrupt handler aren't refused (one account refused most of them), and don't wait.

**pspautotests' video/psmfplayer** (its 22 programs through the HLE kernel, part 26's scratch runner, host0: mounted
at the checkout as PSPLink would have it; both engines): 19 print exactly what the PSP printed (create, setpsmf and
its `[r]`/`[x]` marks of which calls blocked, setpsmfoffset, settempbuf, start, getaudiodata, getaudiooutsize,
getvideodata, update, break, delete, releasepsmf, stop, selectspecific, selectstream, getcurrentpts,
getcurrentstatus, getcurrentstream, getpsmfinfo). The other three differ only where the PSP's own circumstances show:
basic prints the handle's address and the word it points at (the PSP's library block in its user memory);
configplayer's looping player had restarted far more slowly on the PSP (24024 after 500 calls, re-reading its file
over PSPLink's USB, where here the second pass is at 171171); and playmode counts the calls that wrote the buffer as
its CPU's data cache let it see them (51 of 100 on the PSP, every one here), and its four later players took five
calls longer to start on the PSP than its first (their start time 126126 against 141141), which the same model
can't give one and not the other. Before this part, every one of them stopped at its first missing function.

**Tests** (`tests/psp/run-tests.sh`: 289 groups with the address and undefined-behavior sanitizers, with FFmpeg
and with `PSP_FFMPEG=0`; `tests/psp/ares`: 294 checks, both ways; none failed). `movies.cpp`'s made-up H.264 (I_PCM
pictures, bare slice headers) moved to `movie-maker.hpp`, with a PSMF builder (the whole header, stream table and EP
maps, packs in time order, a second video or sound stream). `psmf.cpp` (new), its pictures from a stand-in decoder
whose picture is its access unit's bytes, so it runs without FFmpeg too: "psmf headers" (scePsmf on a header of two
video streams, one with an EP map, two ATRAC3plus, one PCM and one of user data: the structure, counts, times,
version, every way of specifying a stream and what's reported, the EP map by index and by time, a copy working on its
own, VerifyPsmf's zeroed stack, the refusals, a structure not set up); "psmf player statuses" (every function refused
in every status its table refuses, with no player and a handle of 0; Create's refusals; copies of the handle; the
transitions; the end on the second Update); "psmf player playing" (a program on both engines, both priorities: two
refusals, then a picture a round from time 0 with alpha zero, the end during the second Update or at the next
GetVideoData, the file open through a descriptor, the state round-tripped); "psmf player pictures" (FFmpeg: BT.601's
colours with alpha zero in all four formats, the buffer widths); "psmf player sound" (the frames in order, the first
silent, a picture held, two passed over, the end waiting for the sound, a negative sound stream); "psmf player modes"
(slow motion's 17 in 99, pause, step frame, fast forward and rewind on an EP map, a start part way, looping); "psmf
player files" (SetPsmfOffset, the disc, every refusal, no decoders, Start's refusals, two streams of each switched
to, the switch refused where the packs carry none); "psmf player states" (a program saved mid-movie, in GetVideoData's
wait: loaded, the same state saved, the same pictures to the end; with P pictures, the loaded machine's jumping to the
next IDR picture). `states.cpp`'s "state fields" changes each of the player's 38 fields, refuses 20 more states and
loads a thread in SetPsmfCB's wait; `tests/psp/ares` refuses a state of version 12. Eleven broken versions each
failed them: a picture every Update in play mode, the end without its two Updates, the start-up on the first call,
opaque alpha, the priority ignored, no wait for a picture to start from after a load, the pacing left out of states,
no hold for the sound, the first sound frame heard, the EP entry by its exact time only, and pause at once (without
the picture already due).

**The games** (the host Mac; part 26's scratch runner built from this tree and from part 29's, FFmpeg in both; the
pictures and sound outside the repository, in `/tmp/psmf-scratch`). Of the fourteen games here, only Peace Walker
touches these libraries: it ships `psmf.prx` and `libpsmfplayer.prx` and imports four scePsmf functions (SetPsmf,
GetNumberOfSpecificStreams, GetPresentationStartTime and EndTime), playing its movies through sceMpeg; none of the
fourteen imports scePsmfPlayer (the eight games of the report that call scePsmfPlayerCreate aren't on this Mac).
- **Peace Walker**: pressing Cross on its title (frames 3300 and 3900 after the compatibility run's presses), at
  frame 4341 it loads `psmf.prx` (stood in for), calls SetPsmf, GetNumberOfSpecificStreams twice and the start and
  end times on `pw_op_ENG.pmf`, and plays its opening through sceMpeg: all 2368 pictures, the cast introduced one by
  one, to the title card, with its sound (-11.5 to -16 dBFS over its 80 seconds; 0.027% of its samples at full
  scale, the game's own mix). Before this part, scePsmfSetPsmf wasn't there (0x8002013a): the game gave the movie up
  after four pictures, the screen black (`pw-movie-after.png` and `pw-movie-before.png`, frame 5400).
- **All fourteen**, the ten priority games among them, with the compatibility run's presses for 3600 frames, before
  and after: the same pictures at frames 1200, 2400 and 3000, byte for byte, the same sound, and no function missing
  in either.
- **On the RP6** (build 105200 of this tree, installed over the app, its data kept): Peace Walker, no button
  pressed, plays its opening movie after the Kant quote, a minute in, through `psmf.prx` stood in for (the app's
  log says so), at 60 frames a second, its sound track running throughout. Of the device's eight games not on this
  Mac, four were tried for under a minute each: Chili Con Carnage loads `psmf.prx` too and now plays its opening
  movie and the Eidos logo at 60 frames a second, to a Memory Stick warning (black in the compatibility run, at a
  function then missing); Ace Combat X and WipEout's collection stop at ThreadManForUser functions not written yet
  (0xffc36a14, 0x94416130), and Ace Combat Joint Assault is black at 50 seconds, as in the report, nothing noted.
  None of the four loads the player's library in that time.

**After review** (commit 617d34e48). An independent audit of this part found the clean room kept (the design
independent, the account of how it was made accurate) and three faults, each needing a malformed movie, none in the
owner's games. Its author had read the emulators' sources, so its report wasn't read here: the owner passed on its
findings in their own words, and the fixes are this part's own.
- An EP map's size, the header's entry count times 10, was worked out in 32 bits: a count of 0x1999999a came to
  4 bytes and passed the check that the map is in memory, so scePsmfGetEPWithId copied host memory past it into the
  game's, and scePsmfGetEPidWithTimestamp walked 429 million entries in one call. The size is now worked out in 64
  bits, and a map must lie whole inside the header (before its stream's offset), all of it in the game's memory,
  else there's none; a walk over it reads it once. (The player's own walks already bounded it so.)
- An access unit whose next delimiter never came grew without limit, the program stream read into it to its end,
  its end searched for from its start again after every pack (seconds at 8 MiB). A unit is now cut at 2 MiB (H.264's
  level 3, the most the PSP's decoder takes, keeps a coded picture within 1.5 MB), and the search goes on from where
  it had got to. The sound had the same fault, found while fixing this one: frames whose headers had gone were
  searched for from the start after every pack read, and all kept; the bytes passed over are now dropped as they're
  searched, their last time stamp kept for what follows.
- A presentation's end before its start made its length negative (GetPsmfInfo's time; Start's check of a start
  time). The end is now taken as the start when a header is read, scePsmfGetPresentationEndTime's answer too, and
  GetPsmfInfo gives 0 for a presentation shorter than a picture.

"psmf malformed movies" (new) has each: a program on both engines reading the wrapped map (no map, nothing written),
a map one entry past the header (none) and one filling it to its last entry (read); a unit of 5 MiB with no
delimiter after it (cut at 2 MiB, the movie playing on to its next picture and its end, the video held never much
past the cut); 3000 sound frames without headers (the pictures playing on, none of it held); ends before the start
and under a picture after it. "psmf headers" checks an end before the start. Each fix undone fails them, but for the
search going on from where it had got to, which the 2 MiB cut bounds and only time shows (that unit in 537 ms
searched from its start each time, 5 ms now; the sound's frames in 651 ms, 4 ms now). tests/psp 290 groups with the
sanitizers, with FFmpeg and without, tests/psp/ares 294 checks both ways, none failed; pspautotests' video/psmfplayer
gives the same output, byte for byte, as before (19 of 22 as the PSP), on both engines, and video/mpeg's programs
too; Peace Walker's opening movie, run to frame 9000, gives the same pictures and sound as commit 16f6d257f's.

**Left, and why**:
- The player meeting a game: the report's eight scePsmfPlayerCreate games are neither on this Mac nor among the
  device's games tried, so the player rests on pspautotests' recordings and the tests alone.
- LocoRoco 2's library version (SDK 0x03090510), whose GetPsmfInfo takes two more pointers, for the video's width
  and height: the stood-in module's version isn't kept, and no game on this Mac has it. Nor the older versions that
  play to the file's end.
- PCM sound, marks, and the unknown NID's real behaviour: no movie or game here has them.
- The start-up's and a loop's real lengths, which on a PSP follow its threads' reading and decoding in time; the
  calls' waits, unmeasured. A round of the measuring program with a movie that has sound and an EP map (a test movie
  could be made by adding EP entries to `test.pmf`'s header) would settle the sound's thresholds, fast forward and
  rewind, and scePsmf's uncertain answers.

## Part 32: the next functions games stop at

On branch `cursor/psp-hle-games6-2b67`, on top of part 31's `cursor/psp-psmf-2b67`. On the owner's RP6, Ace Combat X
and WipEout Portable Collection stopped at ThreadManForUser 0xffc36a14 and 0x94416130 (part 31), and Chili Con
Carnage at a Memory Stick warning after its intro; part 28 had left a list of differences pspautotests' recordings
settle. Sources: pspsdk's headers (`pspthreadman.h`'s SceKernelThreadRunStatus and SceKernelIdListType,
`pspiofilemgr_devctl.h`'s SceDevInf and libcglue's statvfs reading it, `psputility_savedata.h`), pspautotests'
programs and the results they recorded on a PSP (threads/threads' refer, threadend, exitstatus, threadmanidlist and
threadmanidtype; threads/events/refer and create; threads/semaphores/refer and create; threads/lwmutex's refer,
create and unlock; display/hcount, hcountwrap and vblankphase; utility/savedata's sizes, getsize and noupdate;
modules/loadexec/loader), and the games' own code and calls, traced with part 26's scratch runner. The PSP
Developer Wiki's archived copy has no page on these functions; no other emulator's code was read. Where nothing
recorded shows a behaviour, the code and this part say what was chosen. The owner's local agent's re-run of the
compatibility report hadn't landed (no `local/psp-rerun`, no newer `docs/psp-compatibility.md` on part 29's
branch); every function the existing report names was already here. (It landed later: the re-run's list, below.)

**sceKernelReferThreadRunStatus** (0xffc36a14, `threads.cpp`): a thread's SceKernelThreadRunStatus, 44 bytes: its
status, current priority, wait type and what it waits for, and wakeup count, as its full status gives them, then its
run figures: the time it has had the CPU (a SceKernelSysClock, 64 bits of microseconds), how many times a call into
the program interrupted it, how many times a better thread took the CPU from it while it could still run, and how
many times sceKernelReleaseWaitThread let it go of a wait. pspsdk names the figures only; no pspautotests program
calls the function (threads/threads/refer leaves the full status's figures unprinted, "it'll probably be slower"),
and its recordings show them 0 for threads never started. So threads now keep them (`Thread::runCycles`,
`interruptPreempts`, `threadPreempts`, `releases`, and the kernel's `ranSince`, when the running thread got the CPU):
a thread leaving the CPU adds its time (`switchTo()`), a call into the program stops the clock of the thread it
interrupts and counts one (`startCall()`, `callReturned()`), the scheduler counts a preemption as it makes a running
thread ready for a better one (its own start of that thread included), and sceKernelReleaseWaitThread a release. A
start counts afresh (chosen). The full status (sceKernelReferThreadStatus) gives the same figures.
- Ace Combat X's vertical blank handler asks after its main thread every other blank and wakes it unless it's
  running (status 1); the size word it passes is whatever its stack held, 0 here, so nothing is written and it wakes
  the thread each time, as it did while the function was missing. The game hadn't stopped at the function: it waited
  for good at "Checking Memory Stick", after the savedata utility's answer below.

**sceKernelGetThreadmanIdList** (0x94416130) and **sceKernelGetThreadmanIdType** (0x57cf62dd): the IDs of the thread
manager's objects of a kind, in the order they were made, and an object's kind. As threadmanidlist recorded: kinds 1
to 14 and 64 to 67 are taken, every other refused (ILLEGAL_TYPE), then a negative size (ILLEGAL_ADDR), neither writing
the count; a size of 0 gives the count alone (no buffer needed), the count's pointer may be 0, and threads made
dormant, sleeping, delaying and suspended are listed as such. pspsdk names 1 threads, 2 semaphores, 3 event flags, 4
mailboxes, 5 VPLs, 6 FPLs, 7 message pipes, 8 callbacks, 9 thread event handlers, 10 alarms, 11 virtual timers, 64
sleeping, 65 delaying, 66 suspended and 67 dormant threads; the recording lists a thread-local storage pool under 14.
Chosen: 12 and 13 as kernel and lightweight mutexes (which came with the same firmware as those pools, in that order);
9 and 14 list nothing (the kernel has neither); a suspended thread is listed under 66 whatever else it's doing; the
function returns how many IDs it wrote (pspsdk: "either 0 or the same as idcount", which a buffer of none and a large
one give that way), the count being all there are (pspsdk's own thread utilities ask for it with a buffer of none);
a buffer the IDs can't go in is ILLEGAL_ADDR (the test's comment says a PSP crashes on a null one). An ID that is no
thread manager object (deleted, -1, 0, 1, a memory block, a module) is ILLEGAL_ARGUMENT to IdType, as recorded.
WipEout lists its threads (a buffer of 20) after its loading screen's thread ends, then reads each one's status.

**sceKernelLoadExec** (LoadExecForUser 0xbd2f1094, `system.cpp`): with its thread list answered, WipEout went on to
its launcher, and on the RP6 choosing WipEout Pure there called this, missing, and fell back to the launcher's menu:
the collection starts each of its games as a program of its own (`disc0:/PSP_GAME/USRDIR/ELF/FX300.BIN`, no
parameters). The program ends and the one named starts in its place; its first thread gets the argument given in a
SceKernelLoadExecParam (psploadexec.h: its size, the argument's length, where it is, a key), none for none, or with no
parameters its path, as a program the system starts gets. As pspautotests' modules/loadexec/loader recorded, the call
never returns ("[1]", then the new program's "Hello world!", never "[2]"): the calling thread stops, and the kernel's
loop puts the new program in as it next goes round (`loadExec()`, never left pending for a state to find), ending
that go. Everything of the old program goes, as at power on (threads, memory, modules, open files, calls into it,
sound, the GE's lists, the clock, which starts afresh), the user partition is cleared, and the new program is loaded
as the system loads a game's (`start()`, which now takes the argument); the devices, the disc and the system fonts
stay the system's. Checked first, the old program left running: a file there isn't (its error), one that isn't a
program or can't be decrypted (ILLEGAL_OBJECT, UNSUPPORTED_PRX_TYPE), an argument over 4 KiB (ILLEGAL_SIZE), a call
from an interrupt handler (ILLEGAL_CONTEXT); those choices are the code's, no recording shows them. KDebugForKernel's
Kprintf, which the recorded test's second program prints with, is sceKernelPrintf's.

**The thread status, recorded** (sceKernelReferThreadStatus, sceKernelGetThreadExitStatus): a thread's exit status
reads DORMANT until it first starts and NOT_DORMANT once started, then what it ended with (threads/threads/threadend,
refer and exitstatus: waiting for a thread never started returns DORMANT), and sceKernelGetThreadExitStatus takes 0
as no thread (UNKNOWN_THID), not the caller. The full status is copied as its size word says, by the SDK the program
was built with: up to 2.60 (SDK 0x02060010) a structure of 104 bytes, any size taken; after it, 108 bytes (a last
word, 0), and a larger size refused (ILLEGAL_SIZE, nothing written, 0xffffffff among them).

**Status structures** (`report()`): every status function pspautotests recorded copies the structure's first bytes as
far as the size the program put in its first word, no further than the whole, the size reading back as the
structure's (a size of 1 copies the size's first byte; 0 copies nothing). The event flag's (52 bytes) and the
semaphore's (56) now do so (threads/events/refer, threads/semaphores/refer); they had written everything, the size
word left as it was. The thread's status, the run status and the lightweight mutex's go through the same helper.

**Creating event flags and semaphores**, as threads/events/create and threads/semaphores/create recorded: a NULL name
is ERROR for both; an event flag refuses attribute 0x100 and any past 0x2ff (0x100, 0x122, 0x300, 0x900, 0x1200
were), a semaphore any past 0x1ff (0x100, waiters by priority, is taken); and a semaphore takes any counts at all, a
negative first count, one above the largest, a negative largest (each had been refused, ILLEGAL_COUNT: a game making
one so on a PSP would have had no semaphore here). Snoopy vs. the Red Baron makes two semaphores with a NULL name,
which it had got and now doesn't, as on a PSP: it deletes the IDs it was refused and goes on without them, to the same
screens. The recordings were made with no SDK version set, which the thread status's rule shows a PSP treating as an
older program (above); a NULL name refused even there is most likely refused to Snoopy's (SDK 2.70) too.

**The lightweight mutex's status**: sceKernelReferLwMutexStatus (Kernel_Library 0xc1734599, by its work area) and
sceKernelReferLwMutexStatusByID (ThreadManForUser 0x4c145944): a SceKernelLwMutexInfo of 64 bytes (the structure the
test declares): name, attributes, ID, work area, the count it was made with and its count now, its holder or -1, its
waiters; one there isn't is LWMUTEX_NOTFOUND. The kernel keeps each one's name, attributes and first count
(`Kernel::LwMutex`); its state stays in the work area. As threads/lwmutex/create and unlock recorded, creating one
with a NULL name is ERROR and attributes past 0x3ff ILLEGAL_ATTR (both before the count), the work area's holder is 0
while it's free (it had been -1), and its last three words are zeroed.

**The display's lines**: sceDisplayAdjustAccumulatedHcount (0xa83ef139) sets the accumulated count of lines, which
counts on from it at each line; a negative count is INVALID_VALUE; from 0x7fffffff it goes on to 0 as the next line
starts (display/hcount; hcountwrap's reads straight after setting it sometimes saw 0 already, so 31 bits wide, not
wrapping to a negative number). The line the display is on stays counted from 0 as the blank starts, as the newer
display/vblankphase recorded (the blank's interrupt at the end of line 285, a handler reading line 0); hcount's
lowest line just after a wait for a blank (1) and highest inside it (14) are a waiting thread getting the CPU some 81
microseconds after the interrupt and the blank lasting some 818 from it, as vblankphase measured, where here a waiter
runs at once and the blank lasts 0.77 ms (0 and 13: left, below).

**The memory stick's free space.** Chili Con Carnage and Ace Combat X ask the savedata utility's sizes mode (8) for
the free space alone (msFree; no msData, no utilityData): the kernel answered SIZES_NO_DATA, as it looked for a save
by the request's own save name, empty, and found none. Chili Con Carnage then said "Not enough free space on your
Memory Stick... At least 512KB is needed", and Ace Combat X waited for good at "Checking Memory Stick". As
utility/savedata/sizes recorded (the request's save name "ASDF", none there, msData naming "ABC": its size, and 0),
the save measured is msData's, by its own game and save names; without msData there's nothing to look for, and the
answer is 0. Chosen: msData naming a save not there is SIZES_NO_DATA, the rest still answered. A save takes its files
in whole clusters and its folder's own cluster (sizes: three small files, 4 clusters); saving would take the data
file, the icons' and sound's files the request carries, its PARAM.SFO and the folder (sizes: 16 bytes of data and no
other file, 3). The size mode (22), which wrote nothing at all, now writes the free space in its 32 KiB "sectors"
(getsize), and with files listed what they'd still need past the free space, in whole clusters, the same newly or
over a save (chosen, after review: below; getsize listed none, and the rest was left as it was); its answer is 0
whether the save is there or not (chosen: a game asks before its first save). Sizes as text are whole units cut
down, as recorded ("96 KB", "128 KB", "15 GB" for 16,777,211 KiB), MB between (chosen).
- One stick everywhere (`io.cpp`): the capacity devctl (SceDevInf: 61,440 clusters, 57,344 free, a sector's 512 bytes,
  a cluster's 64 sectors), the sizes mode's msFree and the size mode's: a PSP with a large and mostly empty stick, a
  2 GB one with 1,792 MiB free. The devctl had said up to 1 GiB, all of it free, less where the host's disk had less;
  the sizes mode 1 GiB; the size mode nothing. Large beside any save, and small enough that its bytes fit in 31 bits:
  games add sizes up in 32-bit words, in bytes, where 2 GiB or more free wraps round to a negative number, or at a
  multiple of 4 GiB to almost none, and the game says there's no room. The PSP utility/savedata recorded on had a far
  larger stick (16 GiB less 10 sectors free: 0x7ffff clusters, 0xfffffb KiB, "15 GB"), which is why those figures
  aren't copied. The free space stays as it is whatever is written (chosen: a game asks before it saves).

**States**: version 14 (13 refused): each thread's run figures, when the running thread got the CPU, the lightweight
mutexes' names, attributes and first counts, the display's base for its count of lines. Loading checks no thread has
had the CPU longer than the clock has run, nor the running one got it after now, and a lightweight mutex made as
creating one allows (attributes to 0x3ff, a count of 1 at most unless recursive).

**Tests** (`tests/psp/run-tests.sh`: 296 groups with the address and undefined-behavior sanitizers, and without;
`tests/psp/ares`: 298 checks; none failed). `threadman.cpp` (new): "thread status sizes and exit"
(both statuses' sizes, the SDK's rule, exit statuses through a start and a termination, the refusals); "thread run
figures" (a program on both engines: main spinning 20 ms while a better thread delays three times, a vertical blank
handler: main's 20 ms, one interruption, four preemptions; the better thread's few microseconds; a release; figures
kept as it ends and counted afresh as it starts again; the state saved mid-spin carried on in a fresh machine to the
same figures); "threadman ID lists" (an object of every kind, threads by state, the refusals, sizes and pointers,
every kind's type and the IDs that are none); "status size words and creates" (the three statuses at sizes 0 to
0xffffffff, the lightweight mutex's by both functions, the creates' refusals and odd counts, the work area); and
"accumulated hcount adjusted" (the refusals, 0x7fffffff to 0 at the next line, kept in a state). `utility.cpp`'s
"memory stick free space" (the capacity devctl, the sizes mode with the free space alone, with msData of a save there
and not, what saving takes with icons, sizes as text, the size mode with and without files and in the smaller
structure); the savedata test now names msData's save, and counts the folder's cluster. `modules.cpp`'s "programs
started in another's place" (a program on both engines starting TESTEXEC, plain and encrypted, in its place with no
parameters, an argument and none: never returning, the new program alone on a cleared machine with its clock afresh,
its argument as given, a state of it loading; and the refusals, the old program running on). `states.cpp`'s "state
fields" changes each new field and refuses five more states; `tests/psp/ares` refuses a state of version 13.
Existing tests changed where the recordings disagreed with them: a thread never started reads DORMANT as its exit
status (it had read 0), the work area of a lightweight mutex freed reads holder 0 (-1), and the capacity devctl the
stick above. Broken versions each failed them: no preemptions counted, no interruptions counted, the run figures
left out of states ("thread run figures", and "state fields"); a status copied whole whatever its size ("status size
words and creates", "thread status sizes and exit"); the ID list returning the count ("threadman ID lists"); a
started thread's exit status not set ("thread status sizes and exit"); a free lightweight mutex's holder -1 ("status
size words and creates", "mutexes outside threads"); the accumulated count of lines wrapping to a negative number
("accumulated hcount adjusted"); the sizes mode measuring the request's save, or saying there's none without msData
("memory stick free space"); sceKernelLoadExec returning to its caller, or leaving the old program's memory ("programs
started in another's place").

**pspautotests** (part 26's scratch runner, host0: at the checkout; both builds of this Mac, before and after): of
threads/ and intr/'s 167 programs with recordings, 72 printed exactly what the PSP printed before and 83 after
(events' cancel, create, poll and refer; lwmutex's create and refer; semaphores' create, refer and semaphores;
threads' exitstatus and threadmanidtype), and none of the others differs in more lines than before (threadmanidlist
150 lines to 2, the thread-local storage pool it makes; refer 86 to 26; lwmutex's lock, try and unlock halved); of
the 192 elsewhere, 75 and 76 (utility/savedata/noupdate, whose sizes mode without msData answers 0, as recorded),
display/hcount down to its lowest and highest lines (20 to 4), hcountwrap from 227 lines to 17, savedata's sizes and
getsize to their free space alone (and the PARAM.SFO saves here don't have), and none worse. modules/loadexec/loader
prints "[1]" and then the new program's "Hello world!", as recorded, the scratch runner logging Kprintf's line apart
from the test's own output.

**The games** (the host Mac; the scratch runner built from part 31's tree and from this one, FFmpeg in both, 3600
frames with the compatibility run's presses, Start at frame 120 and Cross at 1800):
- **Ace Combat X**: from "Checking Memory Stick" for good to its title ("PRESS START BUTTON", frame 1800) and its
  attract movie (3000). Its vertical blank handler's run status calls answer; it calls no function missing.
- **Chili Con Carnage**: from "Not enough free space on your Memory Stick" to its profile menu ("Load profile", "New
  profile"), after its intro movie and the Eidos logo; its own logo is drawn in broken stripes (left, below).
- **WipEout Portable Collection**: listed its threads after its loading screen (sceKernelGetThreadmanIdList, then
  each one's status), and went on to its language select and main menu, as it had (the list's error hadn't stopped it
  on this Mac); no function missing in its minute. With Cross pressed through its menus (frames 1800 to 3000), it
  starts WipEout Pure (sceKernelLoadExec), which shows its own language selection and, further on, its title ("PRESS
  START BUTTON", frame 7600 of a longer run).
- **The fourteen others on this Mac** (the ten priority games among them): the same pictures at frames 600, 1200,
  1800, 2400 and 3000, byte for byte, and the same sound, but Snoopy vs. the Red Baron's: the same screens (its logo
  movie, the "no save file" warning) at moments apart, its two unnamed semaphores refused now (above), its sound the
  same. None calls a function the kernel lacks in either build.
- Imported but not called in a game's first minute (`--imports-end`), now that these are here: the network
  libraries' kernel halves (the stood-in modules'), sceRtc's formatting and arithmetic (WipEout, Peace Walker, the
  Burnouts), the utility's NP sign-in and net parameters, sceIoAssign/Unassign (Lumines), sceIoCancel (the GTAs),
  sceKernelCheckThreadStack and the MD5 utilities (WipEout), sceAudioInput (SOCOM); and sceKernelLoadExec, now here,
  which Burnout Dominator and SOCOM import too.

**On the RP6** (build 105300 of this tree, installed over the app, its data kept), each game for a minute or two,
pressing on with the buttons: Chili Con Carnage past its intro to the profile menu, a new profile named on its own
screen, the autosave notice, its title and main menu, "Profile: RAM", at 60 frames a second (its logo and the menu's
art drawn in stripes, a GE matter for another part); Ace Combat X past "Checking Memory Stick" to its opening movie,
title and main menu, a new campaign, and the first operation's briefing, at 60; WipEout Portable Collection to its
launcher at 60, and choosing WipEout Pure, which had fallen back to the menu at sceKernelLoadExec, starts it: its
language selection, the memory stick notice and its menu's opening scene, at 60 (its title, on this Mac, above).

**The re-run's list.** The owner's local agent's re-run of the compatibility report landed afterwards, as an unmerged
PR (#158, `local/psp-rerun`, run at part 29's `b27f084a5`): games stopping at a missing function fell from 149 to
77 of 266. Its state column isn't to be trusted (its "regressions" weren't real), but its count of missing functions
is. Apart from scePsmf and scePsmfPlayer (part 31), its top list is seven functions, named here from candidate
names' NIDs (pspsdk's import stubs and uOFW's export lists agree on each): scePspNpDrm_user 0xa1336091 (13 games),
sceRtc 0x011f03c1 (12), sceDisplay 0x77ed8b3a (8) and 0x40f1469c (4), ThreadManForUser 0xd13bde95 (6), sceHprm
0x7e69eda4 (5) and scePower 0xa85880d0 (4). Its per-game table gives only each game's last line ("not implemented
yet.." for 35 games), not which function, so no other function could be counted across three games; the five here
of the owner's device not on this Mac (Ace Combat: Joint Assault, Ape Escape: On the Loose, Killzone: Liberation,
MotorStorm: Arctic Edge, Ridge Racer 2) import none of the seven.
- **sceDisplayWaitVblankStartMulti** (0x40f1469c) and **sceDisplayWaitVblankStartMultiCB** (0x77ed8b3a; the report
  had them the other way round): the count-th vertical blank's start from now, as display/vblankmulti recorded (3
  from just after a blank: the display's count 0, 3, 6, 9, in the blank each time); 0, -1 and 0x80000000 are
  INVALID_VALUE, before intr/waits' refusals of waiting in an interrupt handler or with interrupts held off. A blank's
  wait now keeps the count of blanks it ends at (`Thread::waitCount`, now plus one for the others), which the blank
  and a callback's end both compare with. A state keeps it as before, the layout unchanged (version 14 still): a
  state of 17ea2d8a0's saved inside a callback run during a CB blank wait ends that wait a blank early, no more.
- **sceKernelCheckThreadStack** (0xd13bde95): the room from the calling thread's stack pointer down to its stack's
  bottom. threads/threads/stackfree recorded 0xeb0 in a 4 KiB stack from a function with a frame of 16 bytes and
  0xab0 with 1 KiB more: a new thread's stack pointer 0x140 below its top here, as on a PSP, gives the same. Chosen:
  0 with no thread, in an interrupt handler, or a stack pointer outside the stack.
- **sceRtcGetAccumulativeTime** (0x011f03c1, and Sony's spelling sceRtcGetAccumlativeTime, 0x029ca3b3, the same
  function in uOFW's rtc): microseconds since the PSP started, 64 bits in v0 and v1. uOFW hasn't worked out its body
  beyond reading the system time, and no pspautotests program calls it: the unit and what it counts from are chosen.
- **sceHprmIsHeadphoneExist** (0x7e69eda4), with the rest of sceHprm but its callbacks: nothing in the headphone
  socket (pspsdk's psphprm.h: 1 for plugged in, else 0), no remote or microphone, no key held, an empty latch; a
  buffer the answer can't go in is ILLEGAL_ADDR (chosen, as the controller's latch).
- **sceNpDrmSetLicenseeKey** (0xa1336091), with the user library's other four (uOFW's npdrm exports): the DRM of
  games sold as downloads, which give their licensee key before opening their own protected files (EDATA: a
  ".PSPEDAT" header with a PGD inside, the PSP Developer Wiki's PSP_EDAT and PGD pages say). There's no DRM here,
  and nothing decrypted: setting and clearing the key, and checking a file's name, answer 0; readying an open
  file's key answers 0 and its data's size is the file's own; a file not open, or a folder, is BAD_FILE. A game's
  plain files work so; an encrypted one reads as its encrypted bytes.
- **scePower 0xa85880d0**: left out. No candidate name hashes to it (pspsdk's lists don't have it, uOFW has no power
  module, and some 51,000 names built from scePower's words missed), pspautotests imports it unnamed and never calls
  it, and no game here imports it, so nothing says what it does; answering 0 would be a guess.
- Checked: tests/psp's new groups (299, three more: "vertical blanks waited for by count", a program on both
  engines from vblankmulti's pattern with a CB wait and a state saved mid-wait carried on in a fresh machine, and the
  refusals; "remote and running time"; "download DRM on a game's own files"), the stack check added to "kernel
  thread stack free" (both engines); none failed with the sanitizers or without; tests/psp/ares 298 checks, none
  failed. pspautotests:
  display/vblankmulti and threads/threads/stackfree now print exactly what the PSP printed (64 and 14 lines had
  differed), intr/waits 98 lines to 74 (none of them the new waits), display/display, vblanklen, power/power and
  rtc/rtc as before. Broken versions each failed them: the Multi wait ending at the next blank, either one taking
  any count, and a blank's wait ending at any blank ("vertical blanks waited for by count"); the stack's room less
  the ID's 16 bytes ("kernel thread stack free"); the running time in 32 bits ("remote and running time"); a file on
  the stick measured as nothing ("download DRM on a game's own files"). Games: the 14 others on this Mac and the
  three above the same pictures and sound as at 17ea2d8a0, but Snoopy vs. the Red Baron's picture at frame 600 and
  Chili Con Carnage's sound, which differ between two runs of that build too. WipEout imports the stack check and
  Grand Theft Auto: Vice City Stories the running time, and neither calls it in its first minute.
- Seen in the five games from the owner's device, not changed: Ace Combat: Joint Assault quits on its first frame
  when sceUtilityLoadModule refuses module 0x308 (past pspsdk's codecs, which end at 0x307; the firmware has a
  libmp4.prx, sceMp4, but nothing here gives its number); Killzone: Liberation's boot program starts the game's
  module (KZL.PRX), then unloads itself with sceKernelStopUnloadSelfModuleWithStatus, which ends the whole program
  here, the program itself being no module the kernel unloads (unloadSelf()), so the game's thread goes with it at
  frame 571; MotorStorm: Arctic Edge, after setting its callbacks up, waits for a blank every frame and nothing else.
  Ape Escape: On the Loose and Ridge Racer 2 run on.

**Left, and why**:
- Threads' attributes as reported: threads/threads/create and refer recorded every thread a program makes with
  0x800000ff added (user mode, and a low byte of ones), and attributes 0x100-0x1000 and 0x8000 refused; not changed
  (the refusals could cost a game its threads where the recordings are of one firmware, and nothing reads the rest).
- Where a thread preempted by a better one goes back in line: refer recorded the PSP running main again once the
  better thread slept, before a thread of main's priority that had been ready all along ("* delayFunc" never
  printed); here main goes behind it. A change of scheduling for every game, for a separate part.
- display/hcount's lowest and highest lines, display/vblanklen's length and vblankphase's figures: a woken thread
  getting the CPU some 81 microseconds after the blank's interrupt (91 with a handler; the next waiters 12 apart), the
  blank's 818 microseconds from the interrupt, and the CPU the interrupt takes (66 microseconds, 78 with a handler);
  here a waiter runs at once and the blank lasts 0.77 ms. A timing part's (with "what calls into the kernel cost",
  which hcountwrap's statistics also show).
- Thread-local storage pools (sceKernelCreateTlspl and its kin, kind 14 in the lists): threads/tls has recordings; no
  game here imports them.
- The lightweight mutex's other recorded differences (threads/lwmutex's lock, try, try600, unlock): a work area
  made by hand ("fake") unlocks on a PSP, the user-mode library touching the kernel only to wait, and a count past the
  lock is ILLEGAL_COUNT there; sceKernelTryLockLwMutex_600 (0x37431849).
- Saves still have no PARAM.SFO (sizes' msData counts it: 4 clusters there, 3 here), and the list mode lists every
  save of the game whatever pattern it's given (Ace Combat X's "USERID_*", Chili Con Carnage's "DATA*").
- Chili Con Carnage's logo and its main menu's art drawn in broken stripes and triangles, on this Mac and the RP6:
  a drawing matter for the GE's part (another branch is changing `ge/`), not looked into here.
- scePower 0xa85880d0 (above), and the three games just above: Killzone's the likeliest to go further (a program
  unloading itself while a module it started runs on should leave that module running), a change to how every
  program ends, for a part of its own.
- The functions above imported but not called in a minute; WipEout Pure's and Pulse's races, and Chili Con
  Carnage's and Ace Combat X's play, not tried here; the compatibility report's re-run, when it lands.

**Uncertain**: the run figures' meanings beyond their names (a call into the program counted as an interruption each,
a thread's own start of a better one as a preemption, a release as sceKernelReleaseWaitThread's), their reset at a
start, and the run status taking any size; kinds 12 and 13 as the two mutexes; how many IDs the list returns when the
buffer is smaller than the list (written, chosen); a buffer the IDs can't go in refused; GETSIZE's answers with files
listed and its 0 for a save not there; msData naming a save not there being SIZES_NO_DATA; a save's folder cluster
and saving's PARAM.SFO cluster beyond the one recording each; sizes as text in MB; the stick's size (a choice within
what recordings and 32-bit games allow); sceKernelLoadExec's refusals and their codes, the clock starting afresh
(whether a PSP's goes on across it isn't recorded), the user partition cleared, and an argument of none for
parameters with none; the creates' NULL names refused for a program built with any SDK; the running time's unit
and start, the stack check's 0 outside a thread, the remote's refusals, and every answer of the DRM functions.

**After review.** An audit of the branch (with part 31's PSMF fixes and part 30's faster GE merged in) found the
clean room held and sceKernelLoadExec sound, and three things to fix:
- The size mode (22) with files listed wrote their whole size as `neededKB` and `overwriteKB` (the size info's words
  at 36 and 48, and their text). pspsdk's names read as what saving needs beyond the free space, so a game that took
  a nonzero figure for "no room" would refuse its first save, the symptom Chili Con Carnage had in the sizes mode.
  They're now the shortfall, what the files take in whole clusters less the 1,835,008 KiB free (0 for any save that
  fits), in KiB, newly or over a save alike. Its text is written only when something is needed and is left as it
  was otherwise, as getsize recorded what there was nothing to say about left alone (chosen; no recording lists
  files). Tested: 128 KiB of files need 0 and leave the text; a 2 GiB file needs 262,144 KiB, "256 MB".
- sceKernelSignalSema added the count to the semaphore's in 32 bits (from before this part). With any count now
  taken at creation, a semaphore at 1 of 2 signalled with 0x7fffffff wrapped to a negative sum, passed the limit
  check and took a count near -2^31. The sum is now 64-bit; that signal is SEMA_OVF with the count kept, and a
  semaphore at 0x7ffffffe of 0x7fffffff takes 1 but not 2.
- A thread's time on the CPU was counted twice when an interrupt's handler ended the game. The call's start adds
  the interrupted thread's time without moving when it got the CPU, and the exit's switch away added it again, so
  a thread could show more time than the clock and a state saved after failed its load check. The call's start
  now moves it (`startCall()`). Tested on both engines: a blank's handler calling sceKernelExitGame while main
  spins leaves main with about 16.7 ms, no more than has passed, one interruption, and a state that loads.
Save states unchanged (version 14). tests/psp 302 groups (one new, two extended) with ASan and UBSan and without,
tests/psp/ares 298 checks, none failed; each fix undone (three broken versions), its own group failed.

## Part 33: curved surfaces

On branch `cursor/psp-ge-curves-2b67`, on top of part 32's `cursor/psp-hle-games6-2b67` (#161),
with parts 31 and 30 (#160, #159) underneath. The GE's Bézier and spline patches weren't drawn: BEZIER and
SPLINE were noted and skipped, and the owner's 266-game report names a game whose GE stopped there (Macross Ace
Frontier). This part draws them. Sources: pspsdk's GU library and headers (BSD: `sceGuDrawBezier`, `sceGuDrawSpline`,
`sceGuPatchDivide`, `sceGuPatchPrim`, `sceGuPatchFrontFace`, `pspgu.h`'s spline modes, `doc/commands.txt`'s SPLINE
edge bits, PATCH_PRIMITIVE and PATCH_FACING), pspautotests' `gpu/primitives/bezier` and `spline` (programs and the
pictures they recorded on a PSP) and `gpu/exact/curves` (programs and checksums), round 3 of tools/psp-measure (five
pictures of patches from the owner's PSP), and public descriptions of Bézier and B-spline math (the Bernstein
polynomials, de Boor's algorithm, the Cox-de Boor recursion). No other emulator's code was read.

**What the GE does** (`ares/psp/ge/curves.cpp`; BEZIER and SPLINE in `list.cpp`):
- The control points are read as PRIM reads vertices, by the vertex type, its indices, and the vertex or index
  address, which moves on past them (`gpu/primitives/bezier` drew its second patch from the next 16 points, and from
  the next 16 indices): ucount to a row (u), vcount rows (v). Every part of a vertex is worked out from the 4x4 points
  around it, each weighted: position, color, texture coordinates, normal and skinning weights.
- BEZIER: every 4x4 points make a cubic Bézier patch, neighbours sharing a row or column, so ucount is 3N + 1 for N
  patches; points past the last whole patch are left out, and fewer than 4 draw nothing (the PSP drew a 4x5 grid's
  first four rows, a 4x8's first seven, a 4x2 nothing). At t across a patch, the four points along a row weigh
  (1 - t)³, 3t(1 - t)², 3t²(1 - t) and t³, and likewise down v.
- SPLINE: a cubic B-spline through 4 or more points each way, every four along a row making a piece of curve, the
  knots evenly spaced. Bits 16-17 (u) and 18-19 (v) cut each end: bit 0 the first, bit 1 the last; open (the first
  three knots stacked on the curve's start, so it starts on the end point) or closed (the knots going on evenly, so
  it starts short of it). Fitted to the PSP's picture, points (a, a, b, b) drawing from a to b with both ends open,
  from (5a + b) / 6 to (a + 5b) / 6 both closed, from a to (a + 3b) / 4 with the first alone open, from (3a + b) / 4
  to b with the last; and round 3's 4x4 spline with both ends open was a PSP's Bézier patch, byte for byte.
- PATCH_DIVISION (bits 0-7 along u, 8-15 along v) cuts each patch, or each piece of a spline, into that many, evenly
  in t, neighbours sharing their edge's vertices; 0 cuts as 1 does (the PSP drew two triangles). The colors down a 4x5
  Bézier and a 4x5 spline step where 4 cuts of each patch, and of each piece, put their vertices.
- PATCH_PRIMITIVE (bits 0-1): 0 triangles, the vertices' rows two at a time as strips along u, (u0, v0), (u0, v1),
  (u1, v0), (u1, v1)... (with flat shading a quad's top left triangle takes its top right vertex's color, as the PSP
  drew); 1 lines, those strips' vertices as line strips (a line down each column and up to the next column's top:
  the PSP's picture, pixel for pixel); 2 and 3 points, a vertex each (value 3 drew points on the PSP too).
- Colors are worked out per channel and rounded up. The PSP's points and flat-shaded patches show vertex colors
  themselves: green 147.42, 223.13 and 251.02 came out 148, 224 and 252, where rounding to the nearest gives 147, 223
  and 251. Round 3's smooth patches agree: with colors rounded up, `bezier-curved` and `bezier-divide-8` are 543,
  481, 561 and 319, 345, 347 pixels a level apart (red, green, blue), against 11000 and 16300 rounded to the nearest,
  32300 and 37600 rounded down, 16200 and 18900 with the fractions kept. What's left is the core's blending across
  triangles, which rounds a little differently from the PSP's (as round 3's color ramps found), not the vertices: in
  `bezier-flat`, where the colors land exactly on whole levels, the PSP is a level lower in 9408 pixels of red and
  9216 of blue, a matter for the triangles, not for curves. A value within 1/4096 above a whole level counts as it.
- Without texture coordinates in the vertex type, they're made up: u from 0 to 1 across the surface, v down it
  (chosen). Without a normal, where one is wanted (lighting, environment mapping, the texture matrix from the
  normal), it's made from the slopes, the cross product of the slope along u with the slope along v
  (`gpu/exact/curves` names normals from the tangents), pointing out of the front face PATCH_FACING says: 0 (GU_CW)
  the side from which a strip's first triangle runs clockwise, 1 the other (chosen: that program's checksums don't
  change either way, the rest of its arithmetic being apart).
- The triangles are culled as any are (CULL_FACE_ENABLE and CULL), every other one of a strip the other way round.
  PATCH_CULL_ENABLE is noted, not emulated: what it does isn't known.
- From there the vertices are a PRIM's: `draw.cpp`'s `primitive()` now reads the vertices and hands them to
  `drawVertices()`, which draws them (transform, lighting, clipping, jobs, batches on the drawing threads, four pixels
  at a time); a surface's strips are drawn one after another as if each were a PRIM of its own. PRIM's own drawing is
  unchanged, the same operations in the same order.
- Malformed surfaces are bounded: one cut into more than 131072 vertices isn't drawn (noted), a guard of Phobos's own
  well past what the GU library allows (64 cuts each way); the points are still read and the address moves on. Points
  that aren't numbers make vertices that aren't, which aren't drawn. The state is all in the commands' words: version
  13 still.

**Against the PSP.** pspautotests' two pictures, their programs drawn again in `tests/psp/curves.cpp`: every pixel
each patch covers is the PSP's, in both (patches, lines, points, indices of 8, 16 and 32 bits, the addresses moving
on, PATCH_DIVISION 0, every spline end type); the points, flat shading's colors and the texture's texels exactly; 283
and 613 pixels of the smooth patches a level apart (the triangles' blending, as above). Round 3 ("psp measure"):
`bezier-flat`, `bezier-curved`, `bezier-divide-8` and `spline-edges-3` cover exactly the PSP's pixels, every channel
within a level (15368, 1494, 938 and 1494 pixels apart, from 36864, 42624, 42920 and 42624 when nothing was drawn);
`spline-edges-0`, both ends closed, covers all but 4 edge pixels the PSP covers, whose edges the core puts a
hundredth of a pixel from the PSP's (787 apart, from 4517), its sixths rounding in the GE's own arithmetic.

`gpu/exact/curves` checks the arithmetic itself, by checksums of whole pictures: points at every cut from 1 to 16 and
their depths, depths at 2^17 a unit, lit points with normals from the slopes, splines, patches as lines. 2 of its 16
checksums are met (the points' places at 1 to 8 cuts; their places at 5 cuts with depths at 2^17), and a third with
the rule below. The others are knife edges: its points land within a hundred-thousandth of a sixteenth of a pixel of
the GE's rounding, and its depths count 2^17 a unit, so they depend on the last bits of the GE's own fixed-point
arithmetic. Tried in scratch builds, and none met another checksum: t in fixed point (8 to 24 bits, cut, rounded or
stepped), the weights and de Casteljau's steps in floats (two forms of a step, either way first), and fixed-point de
Casteljau (12 to 23 bits of fraction in the points, 8 to 16 in t, rounded down or to the nearest, either way first).
Round 4 of the measuring program records the vertices themselves (below).

**Found on the way, and left:** the PSP draws no point whose z / w is past ±1, DEPTH_CLIP_ENABLE on or off. Round
2's `3d-rules` has two such points, at z 1.5 and -1.5 with it on (pixels (136, 8) and (152, 8)): the PSP drew
neither, the core both (two of that file's six pixels apart); and `gpu/exact/curves`' third checksum is met with such
points dropped (past 1 by the core's 2^-15, as for triangles). The core keeps the rule it had (points not judged by
it, as PPSSPP has it), since changing it changes round 2's comparison and every game's points, which this part was to
leave as they were; it's one line in `drawVertices()`'s points, for a part of its own.

**The measuring program's round 4** (the menu's first line, now "Round 4: the GE's lines, boxes, DXT and curves",
about 8 MB) gains fourteen curve cases, most drawn as points, a vertex each, in through mode, where nothing but the GE's
own tessellation stands between the control points and the pixels (`tools/psp-measure/ge.c`, part 33's section):
- `curves-bezier`: a bowed, twisted 4x4 grid with colors and depths of its own, cut 1 to 16 times in 16 cells: each
  vertex's pixel and color; `curves-bezier-depths` the depths written there (VRAM's fourth copy): 16 bits of each
  vertex's z.
- `curves-places`: the same grid cut 5 by 3 in 256 cells, its points moved by sixteenths of a pixel each way: where
  each vertex falls, to the sixteenth.
- `curves-spline` and `-spline-depths`: 5x5 points with every pair of end types, cut 3 by 2.
- `curves-texels`: texture coordinates at the vertices (a texture whose texel says where it is).
- `curves-made-up`: in 3D, the texture coordinates made up for a vertex type without them, over one patch, 2x2 and
  3x1 patches, splines with each kind of end, with a texture scale and offset, and in through mode; as points and as
  triangles.
- `curves-lit`: normals made from the slopes, lit from +z, +x, +y and -z with either patch front face, beside the same
  patches given normals of their own.
- `curves-culling`: the 16 combinations of CULL_FACE_ENABLE, CULL, PATCH_CULL_ENABLE and PATCH_FACING.
- `curves-joins`: which vertices strips join across patches, as lines and flat-shaded triangles.
- `curves-count`, `-count-128`, `-count-200` and `-count-255`, last: how many vertices a row has (points added up) at
  16, 63, 64 and 65 cuts, at 100 and 128, at 200, and at 255, each in a display list of its own. (Since split further,
  one count past 64 to a test, with the GE's waits timed: part 48, "Round 4 again".)
Built in pspdev's Docker image (`ghcr.io/pspdev/pspdev@sha256:54895e6f...`, part 29's), and `tests/psp/programs/
pspmeasure.elf` replaced as that folder's README asks (its SHA-256 there). The EBOOT.PBP for the PSP is outside the
repository: `/tmp/psp-measure-round4-curves/PSP/GAME/PSPMEASURE/EBOOT.PBP` (SHA-256
`bc6e90b4a7b566b734c828eff845069b81b84daaf03beda133f55146e69343f2`), to be copied to the memory stick's
`PSP/GAME/PSPMEASURE`. `tests/psp/measure.cpp` runs round 4 to its end and checks its 34 files; the comparison lists
them once a PSP's `manifest4.txt` is in the results.

**Tests** (`tests/psp/curves.cpp`, 6 groups):
- "curves pspautotests' primitives": `gpu/primitives/bezier` and `spline` drawn again, command for command, and
  checked against the PSP's pictures: each patch's box, the 50 points' places and colors, flat shading's colors row by
  row, the lines' pixels, where the texture's green texel starts.
- "curves knots": each kind of spline end with 5 points along u, against de Boor's algorithm worked out in the test;
  a 4x4 spline with open ends the Bézier patch, byte for byte, and with closed ends not; colors rounded up.
- "curves round 3": round 3's five patches drawn as `ge.c` draws them, against the owner's PSP's pictures
  (`PSP_GE_RESULTS`): the same pixels covered (but the 4 edge pixels above), every channel within a level.
- "curves lighting and texture": a flat patch without normals lit from +z, dark with PATCH_FACING 0 and fully lit with
  1; made-up texture coordinates at the corners; culling either way and off.
- "curves on several threads": a list of patches of every kind (Bézier and spline, triangles, lines and points,
  textured and filtered, lit, in 3D under a perspective and in through mode, 16-bit parts with indices) through the
  driver, at 1, 2, 4 and 8 threads, batches shared however small, four pixels at a time and one: every byte of VRAM
  alike; stopped at its stall address between PATCH_DIVISION and a SPLINE, a state carried on in another machine.
- "curves malformed": 255x255 points cut 255 times each way, refused quickly, the address moving on; a spline as
  finely cut as the budget allows along one way, drawn; points that aren't numbers; counts below 4.
Seven broken versions each failed them: colors rounded to the nearest, strips running the other way, the end bits
swapped, divisions spread over a whole surface, the normal turned round, PATCH_DIVISION 0 not taken as 1, value 3
not taken as points.

**Checks:** `tests/psp/run-tests.sh`, 297 groups (291 and these 6) with the address and undefined-behavior sanitizers,
none failing; "psp measure", the comparison with the owner's PSP, the same as part 31's line for line in rounds 2
and 3 but for round 3's five curve files (now drawn, as above). `tests/psp/ares/run-tests.sh`, 294 checks, none
failed. pspautotests' `gpu/primitives/bezier` and `spline`, run through the HLE kernel by part 29's scratch runner,
print what the PSP printed (nothing) and draw its pictures as above.

**The games.** Macross Ace Frontier, the report's one curved-surface game, and its two sequels aren't on this Mac or
the handheld. A probe build noting every BEZIER and SPLINE ran the 22 games here, which are the handheld's 22 (part
29's 14, and Chili Con Carnage, Ace Combat X and Joint Assault, Ape Escape, Killzone, MotorStorm, Ridge Racer 2 and
WipEout, copied from it), for 7200 frames each, Start and then Cross every 600 frames: none drew a curved surface, so
none changes, and no before and after pictures are of a game. (The probe noted pspautotests' spline program at
once.)

**Speed** (scenes without curves). A PRIM's way gains one function call; no per-pixel code changed. Part 30's
benchmark runner, built with the Android build's flags from this branch and from part 31's, each scene from a state
the latter made (part 29's scenes: Peace Walker at frame 3600, Lumines 3600, Sindacco 3000, Liberty City Stories
8000), 300 frames, the two builds taking turns, best of three, host frames a second:

| scene | before, 1 thread | after, 1 thread | before, 7 threads | after, 7 threads |
| --- | --- | --- | --- | --- |
| Peace Walker | 659.8 | 670.5 | 2127.1 | 2218.0 |
| Lumines | 41.4 | 37.5 | 235.0 | 235.8 |
| Sindacco Chronicles | 84.4 | 87.1 | 133.1 | 132.1 |
| Liberty City Stories | 44.6 | 46.5 | 105.7 | 100.3 |

Every frame's picture is the same in both builds (compared in the last round of that pass, and in all 24 runs of a
second pass). Other agents kept
the Mac busy throughout (load averages of 4 to 45), and the same build's speed moved by up to twice between rounds,
so differences of a few percent mean nothing here; Lumines on one thread, when the Mac was quiet, ran at 36-42 with
the old build and 68-77 with the new, thirteen runs each, in both orders, with the same pictures and a profile of the
same functions: a matter of how the two builds were laid out, not of what they do.

**Left, and why:**
- The GE's own arithmetic for a vertex's place, depth and color, to the last bit (`gpu/exact/curves`' checksums, the
  4 edge pixels of round 3's closed spline): round 4's curve files record the vertices themselves.
- What PATCH_CULL_ENABLE does, which way PATCH_FACING turns a made-up normal, how texture coordinates are made up, and
  whether more than 64 cuts are drawn: chosen above, and in round 4.
- Points past z / w ±1, which the PSP drops (above): a part of its own, as it changes more than curves.
- Morphing and skinning on control points (the points morphed as read, the weights blended, each vertex skinned as
  it's transformed: unmeasured), and NORMAL_REVERSE on made-up normals.

**After review.** An independent review found the clean room kept, PRIM's way unchanged, and the curves bounded and
clean under the sanitizers with hostile surfaces; the ELF and EBOOT built again byte for byte. Two things changed:
- The measuring program's `curves-count` had the strip-join rows and the cuts past pspsdk's 64 in one display list
  and one file: had the PSP's GE stalled at 255 cuts, the owner's restart would have given up on the whole test
  ("stopped twice"), the joins with it. The joins are now `curves-joins`, run before; the counts are split by how
  far past 64 they go (`curves-count` 16-65, `-count-128`, `-count-200`, `-count-255`), run last, so a stall loses
  only its own test. Round 4 has 34 files, about 8 MB (the menu says so; the README's list had still said 4 MB).
  Rebuilt in the same pinned image: `tests/psp/programs/pspmeasure.elf` (its README's SHA-256) and the EBOOT above.
- `GE::patch()` checks the budget from the counts and divisions before working out a step, so a surface past it
  allocates nothing (it had made up to about 5 MB of steps each way first); and `patchVertices`, kept between
  surfaces, gives back its room once drawn when it holds more than 4096 vertices (PatchKept), where one at the
  budget would have kept about 26 MB (13 MB as points) for the rest of a session. Nothing drawn changes: the same
  checks as above, none failing.

## Part 36: a hardware renderer on Vulkan

On branch `cursor/psp-gpu-hw-2b67`, on top of part 33's `cursor/psp-ge-curves-2b67` (#162). Parts 34 and 35 (the exact
compute renderer, on `cursor/psp-gpu-vulkan-2b67` and `cursor/psp-gpu-speed-2b67`) aren't underneath: the owner
stopped that direction (Decisions, 2026-10-07) and this part starts again from the GE, reusing what of it was Phobos's
own design (the seam's idea, VRAM pages owned by the renderer, the lost device, the device setup, the differential
harness). docs/psp-gpu-renderers.md, rewritten, is the design; this is what was built and measured. Sources: the
Vulkan specification, pspsdk's headers for the GE's state, the software renderer, and PPSSPP's GPU backends
(`GPU/Common`, `GPU/Vulkan`, `GPU/GLES`) read for their architecture only, as the owner allowed: none of their code is
used, copied or translated.

**What it is** (`ares/psp/ge/gpu`): the GE does everything up to the pixels as before (lists, vertices, transform,
lighting, clipping, culling, texture decoding) and hands the renderer each PRIM's settings and its primitives on the
screen (`GE::Renderer` in `ge.hpp`: `begin`, `triangle`, `sprite`, `point`, `line`, `submit`, `finish`, `written`,
`forget`, `holds`). `gpu.cpp` turns them into draws on the GPU's rasterizer: frame buffers kept on the GPU as targets
(8888 with depth and stencil), filled from memory and read back only when someone needs their pages (the pages busy
meanwhile, as the drawing threads' are); render to texture by copies on the GPU, taken again only when the target
changed; the GE's decoded textures put on the GPU once; one pipeline for each mix of settings, its fragment shader
specialized by 15 constants (the GE's pixel pipeline up to blending, in its own whole numbers: texel lookup and
filtering, texture functions, alpha and color tests, fog), the GPU's own depth, stencil, blending and logic operations
after. `vulkan.cpp` runs it: three slots round, so the GPU draws while the CPU emulates, work handed over every 128
commands, waits only in `finish()`. Vulkan comes from the host's `vkGetInstanceProcAddr` into the backend's own
tables (a custom driver included; volk untouched), the system loader only when none is handed over.

**Changes to the GE**: `draw.cpp` gives the primitives to a ready renderer instead of making jobs (a sprite's job is
still worked out, so the renderer gets exactly its pixels and texture steps), asks it whether it holds a texture
before decoding, and works out the columns a 2D PRIM's texture reaches beside its rows; `linePixels()` gives a line's
pixels; `ge.cpp` tells the renderer when VRAM is replaced whole; `list.cpp` hands it the list's work at the end;
`threads.cpp`'s `settleAll()` and the busy-page path finish it. Without a renderer nothing changes (the software
renderer's tests and pictures are as they were).

**Accuracy** (the design's "Accuracy"): 2D sprites flat and textured, in every frame buffer format and in clear mode,
byte for byte the software renderer's (the shader steps a sprite's texture coordinates as the GE does; interpolated by
the GPU they were a texel off in a grid across Liberty City Stories' text); render to texture the same. Blending can't
be: the PSP truncates each term, the GPU rounds the sum, and it doesn't dither or keep 16-bit formats between blended
draws. Pixels identical in the six benchmark scenes: Lumines 52.6%, Peace Walker's title 41.4%, Midnight Club 3's
menu 65.0% and race 28.4%, Liberty City Stories' park edge 46.0% and woods 43.0%; nearly every other channel one or
two levels apart, Peace Walker's one step of its 16-bit format (dithering after blending). pspsdk's samples: four of
eight identical, the rest 68.7-99.7%. The pictures look the same.

**Speed**, the RP6 (Adreno 740), host frames a second, software on 1 and 7 threads, then Vulkan: Lumines 87.7, 127.8,
295.2; Peace Walker 65.7, 224.0, 393.4; Midnight Club 3 menu 23.6, 26.5, 52.7 and race 21.2, 24.2, 37.5; Liberty City
Stories park edge 37.0, 49.3, 69.4 and woods 38.5, 54.5, 94.4. So 1.5 to 2.3 times the seven-thread software renderer,
on one thread; on the M1 level with it in the 3D games (the CPU's emulation is the limit there) and above it in the
2D ones.

**Tests** (`tests/psp/gpu.cpp`, in `run-tests.sh`, which now also checks `shaders.hpp` against its GLSL): the sprites
and render to texture byte for byte; pspsdk's samples measured for closeness (at most 3 channels in 1000 more than 8
levels apart); a lost GPU (a pretend backend) leaving the software renderer to draw, said once. They skip where
there's no Vulkan GPU (or with `PSP_GPU=0`).

**Found on the way**: a stale object file in the scratch harness's build gave a renderer of another layout and 25% of
pixels; and the harness's first comparison restored the software renderer's state every frame, so a game showing its
previous frame showed software's, and scored 100%. Both fixed before any number here; a deliberately broken renderer
now has to show as broken.

**Left, and why** (the design's plan): upscaling and presenting from the GPU (next), then OpenGL, then the app wiring
(Settings' "PSP Renderer: Software / Vulkan / OpenGL", Software the default; the host's `vkGetInstanceProcAddr` handed
to the renderer and `loadVulkan` run for the PSP; a start-up sanity check; measured in the app with the system and a
custom driver). Accuracy: programmable blending where the GPU has it, depth read back, transfers and texture decoding
on the GPU.

**After review** (an independent review of the branch: no PPSSPP code copied or translated, the design credited; six
findings, all fixed):
- GCC's `-Wextra -Werror` rejected two conditionals mixing an enumeration and a `u8` (clang doesn't warn): both arms
  are `u8` now, and the PSP system tests build and pass with Ubuntu 24.04's g++ 13, as CI's job has it.
- A PRIM the renderer couldn't take was lost: `targetFor()` refused a frame buffer whose drawing ran past VRAM's end,
  after the GE had handed the PRIM over. `begin()` now says whether it took the PRIM, and the GE has the software
  renderer draw a refused one, once the renderer has put back what it drew: no target (the GPU out of room), rows
  past the target's (past VRAM's end the software renderer's rows run round to VRAM's start, as the PSP's
  addresses do, which a target can't), a lost GPU, or a texture read from memory as it's drawn (none decoded).
- Render-to-texture copies were never let go while their target lived (one for each place sampled from, 300 after
  300 frames in the reviewer's probe), until the GPU's memory or descriptor sets ran out. Now one for each target and
  size, copied again for another place; at most 32, the one unused longest going for another, and none unused for
  16,384 PRIMs. Decoded textures the GE has let go are dropped at submits too, not only at finishes.
- The GPU's depth went stale: the depth buffer's pages weren't watched, so a depth buffer cleared by the CPU or a
  block transfer was missed, and a color change mid-frame refilled the GPU's depth from memory's older one. The depth
  buffer is watched now through VRAM's fourth copy, the rows a change may be in (each 16 KiB it touches) are filled
  again on their own, a color refill leaves the GPU's depth alone, and another depth buffer is filled afresh.
- A PRIM's recording had no bound (a long line costs six vertices a pixel): it's handed to the GPU within the PRIM
  once there are 262,144 vertices.
- The tests shared one renderer between machines without having it forget the one before, so a target kept the last
  machine's pixels. `GE::setRenderer()` now settles the renderer it replaces and has the new one forget (VRAM's
  colors and depth), and the tests attach through it; `forget()` drops the depth as well as the colors.
New tests, one for each of the medium findings: PRIMs refused (past VRAM's end, and a pretend backend that makes no
targets) drawn by the software renderer, byte for byte; render to texture from 300 places and sizes, its copies kept
to 32; and the depth buffer following memory (cleared by the CPU between two depth-tested sprites; a color pixel
written between them not bringing memory's depth back). The design's notes on depth say what's still approximated.

## Part 37: the stuck games, and the runner's presses

On branch `cursor/psp-hle-games7-2b67`, on top of part 36's `cursor/psp-gpu-hw-2b67` (#164). (Parts 34 and 35, the
exact compute renderer, are on branches of their own.) The owner's RP6 had four games stuck:
Killzone: Liberation, Ace Combat: Joint Assault, MotorStorm: Arctic Edge and Chili Con Carnage. Sources: pspsdk's
headers (`pspkerror.h`, `psputility.h`, `pspumd.h`), pspautotests' programs and recordings (umd/callbacks,
umd/register, umd/wait, umd/api), uOFW for how the module manager answers (read for behaviour, nothing copied), the
games' own code and calls, traced with the runner, and round 3's measurements (`psp measure`). No other emulator's
code was read. Each fix is a commit of its own, with its tests.

**Killzone: Liberation** (`modules.cpp`'s `unloadSelf()` and `programAsModule()`): the game's EBOOT is a small boot
program, a static program at 0x09f00000 that exports module_start and module_stop of its own. It loads and starts
KZL.PRX (the game, "Guerrilla", at 0x08800000), at frame 571, then calls sceKernelStopUnloadSelfModuleWithStatus(1)
to get out of the way. The kernel took a program unloading itself as the program leaving (part 19's choice, made
before any game did it), and ended everything: the game's threads with it. Now the program goes as any module does,
and only it: it becomes one of the modules (`programAsModule()`: its module, its ID and the block start() gave it,
`programUID` 0 from then on), has its module_stop run on a thread of its own, and goes once that ends; its exports go
back to the kernel with it (a module importing from it is linked to the kernel again), and the modules it started,
and their threads, run on. Code no module holds is refused with CAN_NOT_STOP (0x80020136), as uOFW's module manager
answers when it finds no module for the caller; it had been taken as the program leaving too. `sceKernelExitGame`
is now the only way the program ends (`exited`). Tests: a boot program built here starts a module whose thread
counts once a millisecond, then unloads itself; the module, its thread and its import of the boot program's
function are checked on both engines, and a state saved while the boot program's module_stop runs carries on in a
fresh machine as the machine does.
- The game then reaches its language menu at 60 fps (host and RP6), and with presses that reach it (below) its
  autosave notice. After that notice it loads `mpeg.prx` (Sony's, stood in for), and at frame 1547 jumps into VRAM
  (0x040cc000) after reading through a null pointer (0x28 to 0x30): not looked into yet.

**The 22 games that went from a menu to black** in the re-run's report: the Killzone finding doesn't explain them.
The program-leaving choice dates from part 19's review (fb01557e1, 5 October), and the WithStatus function from part
32's SDK work (436add499); both were in the old run's commit (de6e6dcb7) as in the new. Built at de6e6dcb7 (with the
new report's runner, FFmpeg off), Killzone shows its boot logo at frames 60 and 300 and is black from 600 on, the
boot program having ended it there too: it was black in both runs, so its change of category isn't a change in the
code. The report's runs had no input at all (the runner's fault, below), so a frame 3600 shows wherever each game's
own timing took it unaided; a game that unloads its boot program would have ended in both runs alike. A re-run with
the fixed runner is the way to sort them.

**The runner's presses** (`tools/psp-runner/runner.cpp`): `--press "F:C,F:C,..."` was never split at its commas.
The whole list was one press, its name all but the first frame; ending in "!", as the report's
`"120:Start,123:Start!,1800:Cross,1803:Cross!"` does, it was a release of a control called
"Start,123:Start!,1800:Cross,1803:Cross", which nothing checked. No button was ever pressed, in any run made so.
The list is now split, and a release must name a control as a press must. That's why Killzone's menu, Joint
Assault's notice and MotorStorm's notice had seemed to ignore Cross on the host. The runner also said "still
running" for games that had ended (`exited` read after unloading the core, which powers the kernel off): fixed.

**Ace Combat: Joint Assault** (`utility.cpp`): at boot (0x08800a4c) it loads utility module 0x308, and on BAD_ID
sets the GPO lights to 0xff and exits. pspsdk's list of sound and video modules stops at 0x307; later firmwares
have one more, which the kernel now takes as the MP4 library (chosen: nothing here says which it is; its functions
would be stood in for as the rest are), 0x309 still refused. The game then asks
sceUtilityGamedataInstallGetStatus, which the kernel didn't have, and exits on the missing function's error. The
gamedata install dialog is now one of the dialogs (InitStart, GetStatus, Update, ShutdownStart, and Abort;
NIDs 0x24ac31eb, 0xb57e95d9, 0x4aecd179, 0x32e32dcb and 0x180f7b62, their names' hashes, as pspautotests' imports
name them): asked about before one is started it's the wrong type (0x80110005), as every dialog answers, and the
game goes on; started, its parameters must be 0x590 or 0x598 bytes (else 0x80110004, chosen from the structure's
two sizes in later SDKs), and it runs and is cancelled, by Update or Abort (Abort while it isn't running: the wrong
status). It reaches the autosave notice, its text drawn with the owner's fonts, and with presses the legal notice
after it; on the RP6, the title screen, "PRESS START BUTTON".

**MotorStorm: Arctic Edge** (`umd.cpp`): it registers a UMD callback (its handler at 0x0893ef6c sets flags from the
state's bits 0x02 and 0x10/0x20), activates the drive, and waits in sceDisplayWaitVblankStartCB, a frame at a time
(0x0893ebb0), for those flags; the kernel never told the callback anything, so the game waited for good. As
umd/callbacks recorded: activating the drive notifies the callback with its state, deactivating it notifies it again
(the handler runs at the next sceKernelCheckCallback, or a wait that runs callbacks, even one that returns at once).
The state: deactivated, the drive is ready but not readable (0x12), readable again (0x32) once activated; and a
program that gave no SDK version is told 0x22 as it's activated, without "ready" (umd/wait's SDK version cases).
Either change wakes threads waiting for a bit of the state it brings (umd/wait). Unregistering follows umd/register:
any ID but the one registered is refused, 0 too while one is registered (0 is taken while none is), and the result is
the ID, or 0 for programs of SDKs after 3.00. The drive keeps whether it's deactivated (`umdDeactivated`), so the
state's version goes to 15. The game shows its epilepsy warning and memory stick notice; with presses, its studio
logos, a stretch of black, and its intro movie (frame 4800 of 6000).

**Chili Con Carnage** (`raster.cpp`'s `triangleRows()` and `lineRows()`, `four.cpp`'s `triangleFours()`): its logo,
a swizzled 8-bit texture in VRAM (0x041ed680), is a fan drawn at depth 65535 with the depth test "at least as near"
(GEQUAL) over a background at 65535. The rasterizer blends each pixel's depth from the corners in floats, the
weights' sum divided out again, and at some pixels that came to a hair under 65535, which the whole number cut to
65534: those pixels failed the test, in stripes. The game's two display lists, used in turn, draw the logo at
slightly different sizes, so frames alternated striped and whole. Corners at one depth now give every pixel that
depth, on both rasterizers and on lines (the smallest change that fixes it: the GPU renderers' agents work on the
GE too). The test draws the logo's three sizes and a line at 65535, four pixels at a time and one; without the fix
8607, 21 and 410 pixels of the fans were missing and the line drew 417 of 470. "psp measure" is unchanged.
Left: a smooth triangle whose corners share a color can lose a level the same way (255 to 254).

**Points past z / w ±1** (`draw.cpp`): part 33 found the PSP drops them even with DEPTH_CLIP_ENABLE on. A point is
now a primitive of one corner: past either end, it isn't drawn, the clamp on or off. "psp measure": 3d-rules from 6
to 4 of 8192 pixels apart, every other measurement the same.

**The re-run report** (#158, corrected): the same run, its list of missing functions unchanged. All of its top 15
are here now (scePsmf and scePsmfPlayer since part 31, the rest since part 32) but scePower 0xa85880d0, which
pspautotests imports only as an unnamed `scePower_A85880D0`: nothing says what it does, so it stays missing.

**Checks.** `tests/psp`: 312 groups, none failing, with the sanitizers and without; `tests/psp/ares`: 302 checks,
none failing (a version 14 state refused). The ten priority games, 3000 frames each against the base (8b0262b25):
the same pictures and the same sound, but for Burnout Legends' frame 3000 and Lumines' frame 600, the same screens
a moment apart.

**Left, and why:**
- Killzone's jump into VRAM after loading `mpeg.prx` (above).
- The other dialogs' parameter sizes (utility/dialog's statuses and sizes still differ), and the Screenshot and
  NpSignin dialogs, which nothing here has asked for.
- Colors that blend from equal corners losing a level (above): no game is known to show it.

## Part 38: the emulation thread faster, the CPU's recompiler first

On branch `cursor/psp-cpu-speed-2b67`, on top of part 37's `cursor/psp-hle-games7-2b67`; measured from part 33's
`cursor/psp-ge-curves-2b67` (70b239ac6). The task: the
emulation thread faster without changing any result, bit for bit. That thread runs the Allegrex (its recompiler,
with the interpreter as the reference and fallback) and the GE's setup: decoding, transforming and lighting
vertices, clipping, setting primitives up and handing them to the drawing threads. Original code: no PPSSPP or
JPCSP source was read, their JITs included.

**The scenes.** Six, each 300 frames from a state of its own. The scratch runner is `tools/psp-runner` with a patch
of its own that is never committed: per-frame hashes, a disc check on states, timing. It's built with the Android
build's flags (`-O3 -flto=thin -ftree-vectorize -funroll-loops -fno-math-errno -fno-trapping-math`) on the host, and
with the NDK (r28) for the RP6. The games are the owner's. The states were made again from boot after the Mac's
restart lost the first ones; each game was checked by its disc ID and its picture:
- Midnight Club 3 (ULUS10021), its menu: Quick Race, Career, Arcade over the city, after making a profile through
  the menus (Create Profile takes its default name at once).
- Midnight Club 3, its night race: a Quick Race in San Diego (PCH Sprint), timed with Cross held.
- GTA Liberty City Stories (ULUS10041), its city: Toni's car pressed against a wall in Portland, buildings and
  people in view, Cross held.
- GTA Liberty City Stories, Toni by the car in the woods.
- Peace Walker's title (ULUS10509).
- Lumines' demo (ULUS10002).

A state's memory stick is copied with its files' times kept. Midnight Club 3 reads its profile's times, so a copy
with new times ends in another state; the first comparisons tripped over that.

**Where the time went.** Instruments' Time Profiler on the emulation thread, Midnight Club 3's race at 7 GE threads.
These profiles were taken before the restart, on earlier states of the same scenes:
- Before: compiled code about 20%, the interpreter 12% (blocks handing it instructions), `System::run` and the run
  loop 4%, the GE 57% (its setup about 28%, its share of the drawing about 22%). A frame ran 284,000 blocks of 7.5
  instructions on average, 2.13 million instructions, and 324,000 of them went to the interpreter, half of those the
  FPU's and most of the rest the VFPU's.
- After (c777b1d0e): the GE 63%, compiled code 25% (the chains at blocks' ends 4%, then lw, addiu, lwc1 and sw), the
  interpreter 4%. In GTA's city: the GE 64% (setup about 29%: a triangle's setup 6%, the 4x3 matrix product 3%,
  lighting 4%, the render-target check 3%, transform 3%, projection 3%), compiled code 18%, the interpreter 9%.

**What was done**, all in the recompiler (`ares/psp/cpu/`), one commit each:
1. **The FPU natively** (`recompiler-fpu.cpp`, new; ec732c07e): add.s, sub.s, mul.s, div.s, abs.s, mov.s, neg.s,
   trunc.w.s, cvt.s.w, c.cond.s, mfc1, mtc1, mfv and mtv, and both coprocessors' branches. Exact: the host's one IEEE
   operation, as the interpreter's, run only while FCSR asks for rounding to the nearest without flushing (checked
   each time); a result that isn't a number goes to the interpreter from the start, nothing written, which chooses
   it from the inputs' bits; trunc.w.s natively only below 2^31 in size; compares by the host's ordered and
   unordered comparisons.
2. **Multiply, divide, trapping sums and bit fields** (7d1f1c33e): mult, multu, madd, maddu, msub, msubu, div,
   divu, clz, clo, add, addi, sub, ext, ins, wsbh and wsbw. Division by zero or by -1, and a sum that overflows, go
   to the interpreter from the start, as before.
3. **Blocks go on to the next by themselves** (0189913cb). Every block had returned to the run loop, which counted
   it, checked the run's limit, looked the next block up and called it. Now a block ending as blocks usually do
   does all that itself and jumps into the next block's body. A block still leaves for the run loop at a syscall,
   break, halt or eret, and wherever anything else is to be done, except a syscall in a delay slot: every import
   stub is a `jr ra` with its syscall there, so after nearly every system call the block chains on. That's exact
   because the kernel stops a run only by halting the CPU or bringing the run's limit forward, and the chain checks
   both; anything else the run loop came to do between blocks would need checking in the chain too.
4. **Chains to a successor known at compile time** (78cd2d4b4): j and jal, a section's last word, either side of a
   branch, a likely branch not taken. The next block's table entry is worked out once, and only the page's check is
   made as the block runs. The halted check is made only after a block that called the interpreter.
5. **Shorter loads and stores** (6c2862804): the page tables in sljit's saved registers (S1 for loads, S2 for
   stores), and the address worked out in 64 bits from the base register widened without its sign.
6. **The VFPU's common instructions with the prefixes at rest** (`recompiler-vfpu.cpp`, new; c777b1d0e): vadd,
   vsub, vdiv, vmul, vdot, vscl, vcmp, vmov, vabs, vneg and vi2f through helpers made for prefixes at rest; vzero,
   vone and vidt written directly; vmmul, vtfm and vhtfm called directly; vnop, vsync, vflush, mfvc and cache done
   natively. A block tracks whether the prefixes are at rest and checks only when it doesn't know.

The one rule behind all of them: **a block starts and ends where it always did.** The run loop checks its limit and
whether the CPU halted only between blocks, and lets a run go past its limit by up to a block, so where blocks end
decides when interrupts and the kernel's events land. A first try at native coprocessor branches took the delay slot
into the block, as the integer branches do. It moved threads' wake-ups by a few cycles in Midnight Club 3: every
frame was still identical, but the machine's whole state was not. So each step was checked against the full
serialized state, not only the pictures. The chains count instructions and check the limit exactly as the run loop
did, so every run stops after the same instructions as before.

**Tried and dropped: vertices four at a time.** For the GE's setup, the world, view and projection matrices (and the
world's turn of the normal) were worked out for four vertices at once, a vertex in each lane of a vector. The rest of
each vertex stayed one at a time: projection, texture coordinates, fog, lighting (b75298198). It's exact by
construction for vertices without skinning, and a 20,000-case test agreed bit for bit. For skinned vertices it isn't:
at -O3 the compiler reshapes the scalar loop over the bones (a count known only as it runs), fusing its multiply-adds
otherwise than the vector form, and GTA's characters differed in a last bit. Every frame of all six scenes was
identical with it, but it gained nothing: on the RP6 at one GE thread, the emulation thread's time a frame in the race
was 46.05 ms before and 46.04 after, and the menu, the city and the woods were the same within 0.3%. The matrices
are a small part of a vertex's setup, and gathering four vertices into lanes and back costs about what the lanes
save. It was reverted (d4b896d4f).

**How it's known to be exact:**
- `tests/allegrex/run-tests.sh` (both engines, 58 groups): the generated programs, now 2,000, cover every new native
  instruction with values at the edges (zeros, infinities, NaNs, subnormals, 2^31, fields past bit 31, ins upside
  down), ctc1 changing rounding and flushing mid-program, and the VFPU's matrix instructions, vi2f, vnop, vsync,
  cache and every control register. A new group, "chained runs", runs a loop of blocks (an interpreted instruction,
  a syscall, a fall into the next section, a call, a likely branch both ways, a store dropping its own section's
  code, a jump through a register, an FPU branch taken every other time) in 2,000 short runs on a chaining
  recompiler and on one without: every run must end at the same count, pc, pd and registers. Mutations (broken
  compares, a dropped FCSR check, msub adding, a mask a bit short, no limit check, no prefix check, vmul's product
  in floats) each fail them.
- `tests/psp/run-tests.sh`: 308 groups pass, under the address and undefined-behavior sanitizers.
- The six scenes, 300 frames each, at 1 and 7 GE threads: every frame's picture and all of VRAM, then RAM and the
  machine's whole serialized state at the end, identical to the base's, after each commit. The RP6's end states
  match too, both builds alike (the host's own in five scenes; in the menu, the RP6's copy of the stick has other
  file times).

**The numbers.** Best of three rounds, the two builds taking turns; base is 70b239ac6 and new is c777b1d0e (the
branch's code after the revert). "CPU" is the emulation thread's time in `Allegrex::run()` less the syscalls it
makes, in ms a frame. "Thread" is the emulation thread's whole CPU time a frame (the CPU, the kernel, the GE's setup
and its share of the drawing). On the host, other agents kept the Mac busy (load averages 9 to 34), so its frames a
second move by several percent from round to round; the CPU times move far less.

Host (Apple Silicon), 7 GE threads:

| scene | CPU ms | thread ms | fps |
| --- | --- | --- | --- |
| MC3 race | 10.17 → 7.08 | 26.28 → 22.80 | 34.7 → 41.2 |
| MC3 menu | 5.01 → 3.06 | 21.46 → 19.39 | 44.7 → 49.1 |
| GTA city | 5.03 → 3.27 | 13.14 → 11.34 | 69.2 → 82.1 |
| GTA woods | 3.02 → 2.12 | 9.87 → 9.08 | 88.2 → 94.1 |
| Peace Walker title | 0.48 → 0.42 | 2.98 → 2.86 | 274 → 273 |
| Lumines demo | 0.76 → 0.45 | 3.51 → 3.15 | 251 → 274 |

Host, 1 GE thread:

| scene | CPU ms | thread ms | fps |
| --- | --- | --- | --- |
| MC3 race | 10.26 → 7.00 | 39.42 → 36.30 | 25.2 → 26.8 |
| MC3 menu | 5.03 → 3.00 | 32.22 → 29.88 | 30.8 → 33.2 |
| GTA city | 4.95 → 3.11 | 21.82 → 19.98 | 45.4 → 49.7 |
| GTA woods | 2.71 → 1.92 | 21.10 → 20.34 | 47.0 → 47.6 |
| Peace Walker title | 0.54 → 0.45 | 10.87 → 10.12 | 89.5 → 98.0 |
| Lumines demo | 0.75 → 0.45 | 9.12 → 8.82 | 109 → 112 |

RP6 (the runner started from adb's shell, unpinned), 7 GE threads:

| scene | CPU ms | thread ms | fps |
| --- | --- | --- | --- |
| MC3 race | 12.80 → 8.36 | 39.63 → 33.73 | 21.6 → 25.2 |
| MC3 menu | 6.40 → 3.54 | 30.98 → 27.11 | 28.4 → 31.8 |
| GTA city | 6.59 → 3.80 | 20.09 → 15.71 | 43.6 → 57.2 |
| GTA woods | 4.55 → 3.17 | 16.83 → 17.14 | 47.4 → 46.0 |
| Peace Walker title | 0.40 → 0.28 | 3.88 → 3.78 | 222 → 223 |
| Lumines demo | 0.92 → 0.59 | 4.53 → 4.72 | 187 → 170 |

At 7 threads the RP6's frames a second depend on which cores the threads land on. Across six rounds of each build,
the woods ran at 47-51 with the base and 46-59 with the new code, and Lumines at 127-187 and 139-170. The CPU times
moved by a few percent at most.

RP6, 1 GE thread (steady to within 0.5% between rounds):

| scene | CPU ms | thread ms | fps |
| --- | --- | --- | --- |
| MC3 race | 11.79 → 7.25 | 50.60 → 46.05 | 19.66 → 21.61 |
| MC3 menu | 5.81 → 3.17 | 41.04 → 38.49 | 24.27 → 25.87 |
| GTA city | 5.79 → 3.35 | 27.33 → 25.09 | 36.42 → 39.69 |
| GTA woods | 3.06 → 2.02 | 26.04 → 25.24 | 38.24 → 39.45 |
| Peace Walker title | 0.38 → 0.28 | 15.20 → 15.09 | 65.56 → 66.07 |
| Lumines demo | 0.83 → 0.43 | 10.77 → 10.38 | 87.24 → 90.16 |

Step by step, measured before the restart on the earlier states (the host's CPU ms a frame at 7 threads; in order:
base, FPU, integer, chains, known chains, loads and stores, VFPU):
- MC3 race: 9.25, 7.64, 7.41, 7.68, 7.64, 7.27, 6.06.
- MC3 menu: 5.06, 4.11, 4.32, 4.00, 4.08, 3.85, 3.00.
- GTA city: 4.75, 4.27, 4.21, 4.26, 4.13, 4.05, 3.20.
- GTA woods: 2.94, 2.56, 2.56, 2.63, 2.56, 2.41, 2.12.
- Lumines: 0.73, 0.56, 0.54, 0.50, 0.47, 0.42, 0.42.

The chains alone barely moved these figures: they moved work from the run loop into the blocks. In the race, the
run loop's calls into compiled code went from 284,000 a frame to 14,000.

**Left, and why:**
- The GE is now most of the emulation thread (63% in the race: its setup about 29%, its share of the drawing
  about a quarter) and compiled code a quarter. Vertices four at a time gained nothing (above). What may: lighting's
  per-light numbers worked out once a primitive, where they don't depend on the vertex (as part 30 did for
  directional lights' directions); a triangle's setup; and the render-target check.
- The emulation thread's share of the drawing: Midnight Club 3 settles about 8 times a frame (only one at GE::run's
  start), each time drawing what's left on the emulation thread. Settling only the region needed, or letting the
  workers finish first, would take that off the thread; it touches `ares/psp/ge/threads.cpp`'s ordering and needs
  its own exactness argument.
- The recompiler keeps no MIPS registers in host registers across instructions: every instruction loads and stores
  its operands from the register file. Caching them within a block is the next large step for compiled code, kept
  exact by writing them back before any call to the interpreter, any syscall and every block end.
- Still interpreted: lwl, lwr, swl, swr, the VFPU with prefixes set, its rarer instructions (vrot, vcrs, the
  trigonometric ones, vfim, the conversions with scale) and the remaining FPU conversions (round, ceil, floor,
  cvt.w.s under other rounding modes).
- The chain's cost at a jr or jalr (a return): a small cache of the last target would skip the table walk.

## Part 41: the Vulkan renderer in Phobos

On branch `cursor/psp-gpu-hw2-2b67`, on top of part 38 (parts 37 and 36 under it). docs/psp-gpu-renderers.md's "In
Phobos (part 41)" is the
detail; this is what was built and measured. PPSSPP's GPU backends stayed a guide to the architecture only, as in
part 36: none of their code is used, copied or translated.

**The setting**: "PSP Renderer" in the app's Settings, Emulation, PlayStation Portable, "Software (exact)" by default
or "Vulkan (GPU)"; on the desktop "PSP renderer: Software / Vulkan (next start)" (`psp.renderer`). Both give the core
its "Renderer" option as a game loads (`psp.hpp`, read by `System::load()`), so it holds from the next game on.

**The loader**: the hosts' `vulkanLoader()` gives the core the `vkGetInstanceProcAddr` their `loadVulkan` got from
Granite's loader: on Android the system's or a custom driver's (libadrenotools), on the macOS desktop the bundle's
MoltenVK. The renderer builds its own tables from it; volk and the other cores' Vulkan are untouched.

**Starting, and falling back** (`System::startRenderer()`, `ge/gpu/check.cpp`): the renderer is made on that loader
and checked in a machine of its own (random sprites in two formats byte for byte, clear mode, blended triangles
within a bound) before the game's GE gets it. A renderer that doesn't start or fails is dropped for the software
renderer, said once in the log and once to the front end (`notice()`: the app's toast, the desktop's message); not
tried again until the next game. A device lost later (a wait past five seconds, `VK_ERROR_DEVICE_LOST`) does the
same from the next PRIM on.

**The frame shown**: both hosts take CPU pixels, so the frame is read back from the GPU either way; the choice was
what to read. `GPU::picture()` reads only the shown rectangle straight from its target, with no finish, so the GPU
keeps the pages and the next frame doesn't fill them again from memory; where the target can't give it (stale, too
few rows, pages another's, memory's newer bytes in it) `Kernel::picture()` shows memory's, finishing first. Showing a
GPU image without the read-back is part of upscaling (next), where it's needed anyway.

**Bytes beside the pixels**: Brave Story keeps its display list in VRAM, beside its 480-pixel picture in the same
pages, so every command the GE read waited for the GPU and every list the CPU wrote sent the frame back up: 18.6
frames a second on the RP6 (64 ms a frame; software 9.5). Busy pages are now asked about to the byte
(`Memory::vramDrawnOver`, `GE::drawnOver()`, `Renderer::drawnOver()`): memory waits only for bytes a target drew over
since its last finish. A write beside them (`Memory::vramChangedBusy`, `Renderer::besideChanged()`) is kept by the
target as a range, and the target takes memory's bytes again before it draws over them, shows or lends them, and
after its next finish. The drawing threads' busy pages stay whole pages. Brave Story: 59.5 frames a second on the RP6
(13.5 ms), 90-96 on the M1 (from 53; waits 26,000 to 5,700 a run). A test: a CPU writing and reading beside a
target's pixels without a finish, then a sprite over those bytes drawn over memory's.

**The runner**: `tools/psp-runner` builds with Vulkan's headers (the core needs them since part 41), takes
`--renderer NAME`, and prints the renderer's draws and waiting a frame. It and the tests use the system loader (on
macOS `DYLD_LIBRARY_PATH=/opt/homebrew/lib`).

**Measured in the app** (the RP6, Adreno 740, the app's stats, frames a second and milliseconds a frame): Lumines'
demo 60 (4.6 ms custom driver, 4.7 system); Peace Walker's title 60 (2.2, 2.7); Midnight Club 3's night city title
54.3 custom, 49.6 system, 35.6 software, and its quick race 39.0, 36.0, 29.0. The custom driver is StevenMXZ's Turnip
v26.3.0-R5, the one already installed (none installed or deleted); both pass the start-up check. Nineteen more games
pressed to their menus on Vulkan with the custom driver: none crash, 18 at 59.3-60 (0.9-13.3 ms) and Brave Story as
above. Ace Combat: Joint Assault, MotorStorm: Arctic Edge and Killzone are black with either renderer (the core's).
The M1's desktop draws Lumines at 60 on MoltenVK, and with no Vulkan driver says why once and draws in software.

**Not seen**: the app's fallback toast (neither driver failed on the RP6; the desktop's fallback and the lost-GPU
test cover the path), and a device lost in a game. The harness's saved scenes were lost after the frame-shown
measurements (docs/psp-gpu-renderers.md, "Speed"), so the six scenes' pixel scores weren't taken again after "bytes
beside the pixels"; the RP6's in-app numbers above were.

**Next** (the design's plan): upscaling, with the GPU's image presented without reading it back; then accuracy
(blending in the shader where the GPU allows); then OpenGL.

## Part 42: Killzone's movie, the dialogs' sizes and statuses

On branch `cursor/psp-hle-games8-2b67`, on top of part 37's `cursor/psp-hle-games7-2b67` with #164 merged into it
(7cafa81a4). Sources: pspsdk's headers (`pspmpeg.h`, `psputility.h`), pspautotests' programs and recordings
(video/mpeg/basic; utility/dialog's sizes, status, abort, priority and htmlviewer), the games' own code, traced and
disassembled with the runner, and round 3's measurements (`psp measure`). No other emulator's code was read. (The
older comments at the top of `utility.cpp` still name PPSSPP as where the dialogs' timings came from; nothing here
was added from it.) Each fix is a commit of its own, with its tests.

**Killzone: Liberation's jump into VRAM** (`mpeg.cpp`'s `mpegDecoded()`, `mpegOwnLibrary()`): after its autosave
notice the game loads its disc's `mpeg.prx` (sceMpeg_library 1.5, stood in for) and plays its intro movie. Its
movie object's Update is called every frame with no target of its own (`Update(0, 0)` in effect), so it decodes into
no buffer; after sceMpegAvcDecode, when told no picture came, it clears its target through that object, a null
pointer, reads a function's address from 0x28 to 0x30, and jumps to the garbage there (0x040cc000, in VRAM) at frame
1547. The kernel held the first picture back, as pspautotests' video/mpeg/basic recorded and pspsdk's header says
("will be set to 0 on first call, then 1"), but basic used the firmware's library; the game's code can't have run
that way on a PSP, so with the game's own library each access unit gives its own picture at once, the first too.
Programs on the firmware's library (sceUtilityLoadAvModule, sceUtilityLoadModule) still get the first held back.
The game's library is the same version Grand Theft Auto: Vice City Stories brings (Liberty City Stories, Midnight
Club 3 and Burnout Dominator bring 1.4, Snoopy vs. the Red Baron 1.3); the two movies' sequence headers are alike;
Burnout Legends and Dominator set the same decode mode (-1, 3). Those six show the same screens either way. Killzone
now plays its intro, reaches its title screen (frame 3300) and its main menu (frame 4800). The test plays
basic's movie both ways: with a stood-in sceMpeg_library every frame's picture comes with its access unit.

**The dialogs' sizes and statuses** (`utility.cpp`), as utility/dialog recorded them:
- Each kind's InitStart takes only its own parameter sizes (the first word of the common part), every other size up
  to 0x800 refused with INVALID_PARAM_SIZE (0x80110004), and the refusal leaves the current kind as it was:
  savedata 0x5c8, 0x5dc and 0x600; message 0x23c, 0x244 and 0x2c4; keyboard 0x40 and 0x44; network settings 0x38,
  0x40 and 0x44; game sharing 0x50, 0x54 and 0x64; the web browser 0x70, 0x78, 0x80, 0x98, 0xa4 and 0xa8; the
  gamedata install 0x590 and 0x598 (part 37's, now from the same table).
- A dialog starts only while none is current: starting, running, finished or shutting down, it's INVALID_STATUS
  (0x80110001), asked before the size (priority: an InitStart right after ShutdownStart is refused).
- Update does its work only while the dialog runs: starting, or after the dialog's gone, INVALID_STATUS. Finished,
  it answers 0 and does nothing (not recorded).
- sceUtilityMsgDialogAbort (new): another kind's, the wrong type; not running, INVALID_STATUS; running, 0, and the
  message stays running until 8 Updates have come since it started running, at least one after the abort (abort's
  cases, 0 to 10 Updates first, took `max(8 - before, 1)` more; a message here is answered at its first Update, so
  one still running has had none, and takes 8), then finishes with result 0, its button untouched. The count is in the state (`dialog
  .abortUpdates`), so the state's version goes to 16.
- Not done: the web browser's own state and its 0x380000 bytes of memory (htmlviewer: 0x800200d9 when they can't be
  had, even during a message); the Screenshot and NpSignin dialogs (their GetStatus the wrong type at boot), which
  no game here asks for.
The test runs every kind through every size, starts each at each of its sizes and takes it through its statuses,
and aborts a message, a state saved halfway through its count.

**Colors from equal corners losing a level** (part 37's last item): a smooth triangle whose corners share a channel
can come out one level under it (255 to 254), as depths did. Kept exact in a trial build, every line of `psp
measure` stayed the same (its 90 lines), so neither the PSP's pictures nor its measurements say the level is kept:
left as it is.

**scePower 0xa85880d0** (#158's one top missing function): none of the 22 games here calls it, pspautotests imports
it only unnamed, the PSP Developer Wiki has no scePower list naming it, and none of about 2.7 million candidate
names hashes to it. Left until a game calls it, to be modelled from how it uses it.

**Checks.** `tests/psp`: 321 groups, none failing, with the sanitizers and without; `tests/psp/ares`: 306 checks,
none failing (a version 15 state refused). The ten priority games, 3000 frames each, against the Killzone commit
(461219822): the same sound, and the same pictures but for four the same screens a few frames apart (Vice City
Stories' and Snoopy's logos, Gunhound's loading screen, Peace Walker's title).

## Part 43: the emulation thread faster again: drawing off it, registers held

On branch `cursor/psp-cpu-speed2-2b67`, on top of part 42's `cursor/psp-hle-games8-2b67` (#169); measured from part
38's `cursor/psp-cpu-speed-2b67` at 4ebc59d97. The task as in part 38: the emulation thread faster on the RP6, nothing
changed in any result. Original code: no PPSSPP or JPCSP source was read.

**The scenes** are part 38's six, made again from boot (the merge changed the save state's format, so the old states
no longer load) and checked by disc ID and picture. Each step was checked against 4ebc59d97 in all six, 300 frames
at 1 and 7 GE threads: every frame's picture and all of VRAM (drawing settled each frame), then RAM and the machine's
whole serialized state at the end. On the RP6 the end state was also compared without settling each frame, as the
app runs.

**Where the time went.** simpleperf on the RP6, Midnight Club 3's race at 7 GE threads, 4ebc59d97: the emulation
thread was busy 89% of the time and the bottleneck (the six workers about 30% each). Of its samples, drawing about
38% (`triangleRows` 27%, `spriteRows` 8%, `drawBands` 1%), compiled code 19%, `drawVertices` 6.6%, projection 5.4%,
a triangle's setup 3%, decoding 2.3%, reading vertices 2.2%, clipping 1.3%; lighting under 0.4%. The emulation
thread draws only while it waits for drawing (threads.cpp's settles, which draw bands rather than sleep), so what
matters is why it waits. A scratch count of the time it spends drawing, by the wait that made it (race, a frame):
the next list's start 5.7 ms, the GE reading VRAM a batch draws over 4.6 ms, primitives that can't wait in a batch
2.0 ms, primitives drawn at once 1.1 ms: 13.4 of the thread's 36 ms.

**What was done**, one commit each:
1. **A list waits only for the batch it fills** (`threads.cpp`, `list.cpp`; b0af46594). `GE::run` began by settling
   everything the last list left being drawn. All it needs is the batch it's about to fill back: at a list's end
   the GE's thread moves on to the other of its two batches, which may still be drawing. So it now waits for that
   one alone (`reclaim()`, the wait `launch()` already made), and what the last list launched goes on being drawn
   while the new list is set up. Nothing drawn changes: batches are still drawn one after another in the order they
   were filled; anything the GE's thread reads from VRAM a launched batch draws over (textures, palettes, vertices,
   the list) settles first (`drawnFirst()`), as does a block transfer; and VRAM's pages stay busy for the CPU until
   settled (some may be drawn already, which only makes whoever touches one settle sooner; with nothing left being
   drawn they're freed at once, `freeVRAM()`). A hardware renderer shares those busy pages, so with one, run()
   settles everything as before. In the race the list's-start wait went from 5.7 ms a frame to none.
2. **A block keeps the game's registers in host registers** (`recompiler.cpp`, `recompiler-ipu.cpp`,
   `recompiler-memory.cpp`; 5a5fc5d0c). Within a block a register's value once loaded, or worked out, stays in one
   of sljit's saved registers (7 on ARM64, 3 on x86-64), so the next instructions read it from there. It's written
   through: every result is still stored to `ipu.r[]` as its instruction ends. So `ipu.r[]` is always as the
   interpreter has it, wherever the block leaves, calls the interpreter or goes on to another block, and nothing is
   written back anywhere. What the copies could miss is a register written another way, so those are forgotten: by
   any call into the interpreter, by MFC1, MFV and MFVC (which write rt themselves), by a branch's link, and at a
   block's start. An instruction never takes a host register another of its operands is in. An operand read on one
   side of a jump inside an instruction is read before the jump. A native instruction's slow path (a load from a page
   without an entry, an overflow) runs the same instruction in the interpreter, which writes only that
   instruction's result, so only that is read back from memory afterwards.
3. **lwl, lwr, swl and swr natively** (`recompiler-memory.cpp`; e06caa086), as the interpreter defines them: the
   aligned word round the address, a shift from the address's byte in it, and a mask of the bytes kept. Stores read
   and write through the store table, so a page holding code, a watched page or a busy VRAM page goes to the
   interpreter, as sw's does. GTA's city handed the interpreter 33,500 instructions a frame before and 22,400 after.
4. **Lighting's light-by-material products once a primitive** (`lighting.cpp`; 3292c775f). Each light's ambient,
   diffuse and shine colors times the material's, each channel as 2c + 1, are worked out in `lightingState()`, as
   is the emissive plus the ambient light's share, unless a vertex's color stands for the material's (then
   `light()` makes them as before). The same integers multiply in the same order (511 × 511 × 256 is far inside 32
   bits), so every sum is the same.

**How it's known to be exact:** `tests/allegrex/run-tests.sh` (58 groups) on ARM64, and again built for x86-64 under
Rosetta (whose 3 host registers make the recompiler evict all the time): the 2,000 generated programs use 30
registers in every pattern, with lwl, lwr, swl and swr on both the page table's pages and the page left out of it,
and the chained runs. `tests/psp/run-tests.sh`: 319 groups, the hardware renderer's (`tests/psp/gpu.cpp`) among them.
The six scenes as above, after each commit.

**The numbers.** Best of rounds, the builds taking turns: ref is 4ebc59d97, then each commit in turn. CPU is the
emulation thread's time in `Allegrex::run()` less its syscalls, ms a frame; thread is its whole CPU time a frame.
After commit 1 the CPU's own figure rises a little in some scenes while the thread's falls: drawing now goes on
while the CPU runs, and a CPU touching a busy VRAM page waits for it inside `Allegrex::run()`.

RP6 (the runner from adb's shell, unpinned, two rounds, the app not running), 7 GE threads, ref → 3292c775f:

| scene | CPU ms | thread ms | fps |
| --- | --- | --- | --- |
| MC3 race | 8.72 → 7.90 | 34.37 → 26.79 | 24.2 → 32.3 |
| MC3 menu | 3.59 → 3.42 | 28.79 → 24.36 | 30.4 → 36.2 |
| GTA city | 3.81 → 3.62 | 16.27 → 16.21 | 55.3 → 55.5 |
| GTA woods | 3.17 → 3.11 | 15.60 → 15.98 | 49.0 → 47.9 |
| Peace Walker title | 0.28 → 0.42 | 3.75 → 4.17 | 226 → 216 |
| Lumines demo | 0.71 → 0.59 | 5.29 → 4.19 | 137 → 177 |

Step by step, MC3's race at 7 threads (ref, then after each commit): 24.2, 34.9, 31.9, 36.8 and 32.3 fps; the
thread 34.4, 25.5, 26.7, 24.8 and 26.8 ms. Commit 1 is the step; after it the differences are within how much a
7-thread run moves with the cores its threads land on (part 38). Peace Walker's title loses 4% at 216 fps: its CPU
now meets VRAM still being drawn (0.14 ms a frame more in `Allegrex::run()`).

RP6, 1 GE thread (nothing waits at 1 thread, so commit 1 changes nothing there; steady to 0.5%):

| scene | CPU ms | thread ms | fps |
| --- | --- | --- | --- |
| MC3 race | 7.20 → 7.03 | 45.65 → 45.56 | 21.8 → 21.9 |
| MC3 menu | 3.15 → 3.05 | 37.96 → 37.93 | 26.2 → 26.3 |
| GTA city | 3.34 → 3.19 | 24.85 → 24.77 | 40.1 → 40.2 |
| GTA woods | 2.01 → 1.94 | 24.96 → 24.97 | 39.9 → 39.9 |
| Peace Walker title | 0.27 → 0.27 | 15.10 → 15.10 | 66.0 → 66.0 |
| Lumines demo | 0.43 → 0.39 | 10.28 → 10.23 | 91.7 → 91.9 |

The CPU's time a frame at 1 thread, step by step: the race 7.20, 7.21, 7.03, 7.04, 7.03; GTA's city 3.34, 3.34,
3.26, 3.19, 3.19. Holding registers takes 2-3% off the compiled code's time, and lwl and friends 2% more in GTA. The
compiled code is already almost all native (the race hands the interpreter 12,700 of its 2.43 million instructions a
frame), spread thin over the game (its 400 busiest addresses run 18% of its instructions), and the host's loads from
`ipu.r[]` cost little. Lighting's change doesn't show at this precision.

The host (Apple Silicon) was kept busy by other agents throughout (load averages 10 to 127), so only the large
change shows there: MC3's race at 7 GE threads, the thread 21.1 ms a frame before and 18.7 after commit 1, 44.6 fps
to 48.1 (best of three).

**Left, and why:**
- What's left of the emulation thread's drawing in the race is mostly one wait: a texture decoded from what the
  batch being filled draws (3.4 ms a frame, three times a frame: the game draws into its target and then samples
  from it). The decode happens on the GE's thread before the primitive can be set up, so the batch must be drawn
  first. Taking that off the thread means decoding later, in the batch, with every band waiting for that point,
  and the texture cache shared with the workers.
- Primitives that can't wait in a batch (1.4 ms) and those drawn at once (1.1 ms, mostly textures read from memory
  as they're drawn, which is slow per pixel).
- Registers are held only within a block; across a chain into the next block they start again. Holding them across
  chains would need every block entered with the same registers held, or a store-back at each chain.
- Still interpreted: the VFPU with prefixes set and its rarer instructions (most of what's left in GTA's city:
  20,000 a frame), and the FPU's remaining conversions.
## Part 44: upscaling, and presenting without reading back

On branch `cursor/psp-gpu-hw3-2b67`, on top of part 46's `cursor/psp-cpu-speed3-2b67` (#176 under it).
docs/psp-gpu-renderers.md's "Upscaling" and "Presenting" are the detail. PPSSPP's render resolution multiplier and its
presentation stayed a guide
to the design only, as in parts 36 and 41: none of its code is used, copied or translated.

**The setting**: "PSP Resolution" in the app's Settings (Emulation, PlayStation Portable, under the renderer):
Native, the default, then 2x to 10x; on the desktop "PSP resolution (Vulkan): Native/Nx (next start)"
(`psp.resolution`). Both give the core its "Resolution" option as a game loads; the software renderer ignores it.
Native is exact as before: a scale of 1 runs part 41's code with nothing added.

**Drawing at N times** (`ge/gpu`): every target, its depth and stencil are N times the PSP's size each way, grown
the first time they're drawn into (`fit()`); draws get a viewport and scissor N times larger; memory's newer bytes go
up through a shader drawing them N times larger (colours, depth, and the stencil a bit at a time). What the CPU or the
GE reads (a frame buffer, depth, the shown picture in VRAM) comes back to the PSP's size first, nearest, so memory
always holds the PSP's own pixels and the game sees what the software renderer would. A target copied for a texture
(render to texture) keeps its scale, so effects drawn from frame buffers keep their detail. A new test
(`gpuScaled`, at 2 and 3 times): flat sprites over VRAM full of random bytes, in each format, read back byte for byte
the software renderer's, with memory's untouched bytes and stencils back as they went in; the screen's picture at the
target's resolution; render to texture from a scaled copy; and the depth and bytes-beside tests run again. Its first
failure was the test's own (each machine had drawn different random sprites).

**Presenting** (Android): the shown frame is drawn by the GPU straight onto a swapchain made on the app's window
(`VK_KHR_android_surface`; MAILBOX if there, else FIFO), "sharp bilinear" over the whole view, with no read-back and
the host's `video()` leaving the window alone. Memory's picture is presented the same way where the target can't
give it. Each presented frame is also shrunk to the PSP's size on the GPU for screenshots. A surface or swapchain that
can't be made gives presenting up, and the host locks the window and shows read-back frames as before. The desktop
reads back (SDL owns its window): its screen is up to 4 times the PSP's size (`MostShown`), the picture read back at
the renderer's scale.

**A debug switch** for the fallback toast part 41 never saw: `adb shell setprop debug.phobos.psp.failcheck 1` makes
the app give the core "Renderer Check: Fail", so the start-up check fails as if the GPU had drawn wrongly (reset with
`setprop debug.phobos.psp.failcheck 0`).

**Measured on the M1** (`tools/psp-runner --renderer Vulkan --resolution N`, MoltenVK, read back): Lumines and Ridge
Racer 2 are right at 1x, 2x and 4x (480x272, 960x544, 1920x1088 PNGs). The read-back's CPU cost grows with the
picture: Lumines spends 3 s of the CPU on 1,500 frames at 1x and 21 s at 4x, and waits 0.9 ms a frame against 3-4.

**Measured in the app** (the RP6, Adreno 740, the installed Turnip driver, the app's stats over 10 s): Lumines'
demo presents at 59.4 frames a second at Native (6.5 ms a frame), 59.3 at 4x (9.2 ms), 50.6 at 8x (16.3 ms) and
39.2 at 9x (24.1 ms, the GPU about 74% busy); Ridge Racer 2's title menu 60 at Native (5.2 ms) and 46.5 at 8x
(21.5 ms, the GPU 80% busy). The window's buffer is the window's own size (1906x1080 in SurfaceFlinger), so the
frame is the swapchain's, not read back. The app's screenshot (the pause menu's Shot) saves the presented frame at
480x272. With the debug switch on, the start-up check fails, the toast says "The Vulkan renderer couldn't start
(its start-up check made to fail, as the debug switch asks)" and the software renderer draws Lumines at 60.
Sending the app home and bringing it back while Lumines ran at 9x kept presenting (about 30 fps after the return).

**Not checked yet**: the swapchain against the read-back on the RP6 at the same scale (presenting can't be turned
off from the UI); 10x in play (the setting is in the menu; its row sits under the app's floating nav bar, so it
wasn't chosen from adb); 3D games in play past their menus; rotation and the window resized while presenting. One
8x screenshot of Ridge Racer 2's menu, mid-transition, had a one-pixel vertical line down the middle that Native
didn't show: not looked into yet. The swapchain uses the identity transform, so the compositor rotates it (the
RP6's panel is portrait-native): presenting in the panel's own orientation would save that.

**Next**: shader blending (programmable blending where the GPU has it, for accuracy), then OpenGL.

## Part 45: blending in the shader

On branch `cursor/psp-gpu-hw4-2b67`, on top of part 44's `cursor/psp-gpu-hw3-2b67` (with its review's fixes).
docs/psp-gpu-renderers.md's "Shader blending" is the detail. PPSSPP's shader blending stayed a guide to the approach
only, as in parts 36, 41 and 44: none of its code is used, copied or translated.

**What changed**: the Vulkan renderer's draws that blend, use a logic operation other than copy, or keep part of a
channel with a write mask now read the frame buffer's pixel in the fragment shader (an input attachment) and do
pixel.cpp's arithmetic there: the PSP's whole-number blend terms, all eight operations (the absolute difference no
longer approximated), dithering after blending, the clamp, all 16 logic operations and the write mask bit by bit.
Each write is narrowed to the frame buffer's format, so a 16-bit frame buffer keeps 5, 6 or 4 bits between draws as
memory does; its clears are narrowed too. Native stays exact (now blended draws too); Software stays the default.

**Order**: where the driver has rasterization-order attachment access (Turnip has it), the GPU keeps each pixel's
reads and writes in order within a draw. Without it (MoltenVK on the M1, the Adreno's own driver) a primitive
overlapping one already in the draw starts a new draw. Either way each reading draw waits for the ones before (a
barrier). Logcat's "PSP" start-up line now says which: "blending in the shader, in order" or "overlaps apart".

**Found on the RP6**: the first build asked for the order only in the pipelines that read, with no barrier in order.
Turnip failed the start-up check with it (the blended triangles over unblended ones 80 pixels off), so the software
renderer drew, and the first measurements were the software renderer's. Now every pipeline asks and the barrier is
always there, and Turnip passes in order. Should the check still fail in order, it's run again with the draws split,
and a check that fails names the GPU and whether it blended in the shader.

**Tests**: `gpuBlending` draws random blended sprites (random factors, operations, fixed colors, dithering, logic
operations and partial write masks) over random colors and stencils in all four formats at 1x and 2x: byte for byte
the software renderer's. pspsdk's blend sample is now 100% the same (68.7% before). The sanitized PSP suite passes
on the M1 (325 groups, the GPU groups run).

**Measured in the app** (the RP6, Adreno 740, Turnip, in order; build 106205): Lumines' menus 60 frames a second at
Native (5.2 ms a frame); Burnout Legends' menu with its 3D attract scene 60 at Native (14.4 ms) and 51.4 at 4x (18.3
ms, a heavier scene, the GPU 20% busy); the pictures right to the eye. The GPU tests built for Android pass on the
Adreno's own driver (overlaps apart). The Renderer and Resolution settings put back to Software and Native.

**Also** (part 44's review): a frame whose present didn't happen (no window, or the acquire timed out) is read back
and shown by the host, and a run whose GPU is lost while recording stops before submitting. (This part's review):
the shader's write mask keeps stencil and alpha bits as well as RGB.

**Not checked yet**: the owner's scenes' accuracy and speed tables measured again (their states were lost); the
3D games in play past their menus; the retry with the draws split on a driver whose check fails in order (none
seen yet).

**Next**: Software/7-thread remaining waits and Vulkan accuracy (OpenGL parked; Software covers devices without Vulkan).
## Part 39 — PSP disc info: title, disc ID, region and icon

**Branch:** `local/psp-disc-info`, on top of `cursor/psp-ge-curves-2b67` (#162).

**What it is:** A shared reader (`ares/psp/kernel/disc-info.hpp` / `.cpp`) that reads a PSP disc's title, disc ID
(NPD ID), region and icon (PSP_GAME/ICON0.PNG) from its image — an ISO, one of the scene's compressed forms (CSO,
ZSO, DAX, JSO), or a CHD — reading a few sectors at a time, never the whole image, and bounding every size it
finds: a PSP_GAME/PARAM.SFO over 64 KiB, or an ICON0.PNG over 1 MiB, is taken as none.

**The reader** (`ares::PlayStationPortable::readDiscInfo`):
- Takes a `Disc::Reader` (the same callback the kernel's disc reader uses) and the image's size.
- Detects a CHD by its 8-byte head ("MComprHD"); when `ARES_ENABLE_CHD` is defined, it reads the CHD's sectors
  through `nall::Decode::CHD` (libchdr), as `system.cpp`'s `startDisc()` does. Without that define, a CHD image
  is taken as none (graceful).
- Opens a `Disc` on the reader, finds PSP_GAME/PARAM.SFO (cap 64 KiB) and PSP_GAME/ICON0.PNG (cap 1 MiB).
- Reads the SFO's TITLE and DISC_ID with `sfoValue()` (moved here from `tools/psp-runner/runner.cpp` and
  `mia/medium/playstation-portable.cpp`, which now share it).
- Derives the region from the DISC_ID's third letter: J is Japan, U is the US, E is Europe (verified against
  real NPD IDs: NPJH50634, ULUS10025, NLES00552); every other prefix is "Unknown".
- Returns a `DiscInfo{title, discId, region, icon}`; its fields are empty when there's no PSP game.

**The SFO readers** (`sfoValue`, `paramSFO`):
- `sfoValue(sfo, name)` reads a text value by its key's name from a PARAM.SFO's bytes (a "\0PSF" head, a key
  table, a value table). Empty if the key isn't there or the value runs past the SFO's end.
- `paramSFO(file)` reads a PARAM.SFO out of an EBOOT.PBP (its "\0PBP" head, then the first part, bounded at
  64 KiB). Empty if there's none.
- Both moved to the shared reader; `psp-runner` and `mia` removed their local copies and call the shared ones.

**Consumers:**
- `tools/psp-runner/runner.cpp`: `discInfo()` now calls `readDiscInfo()`; `kind()` and the PBP title line call
  the shared `sfoValue` / `paramSFO`.
- `mia/medium/playstation-portable.cpp`: `load()` reads a disc's title with `readDiscInfo()` (a disc image's
  title, not just the file's name); `kind()` calls the shared `sfoValue` / `paramSFO`.
- Android app: `PhobosCore.pspDiscInfo(fd)` (JNI: `PhobosJNI.cpp`) opens the file by its descriptor, calls
  `readDiscInfo()`, and returns a `PspDiscInfo{titleBytes, discId, region, icon}` data class (the title as
  bytes, decoded to UTF-8 in Kotlin). The Library's `MainViewModel` shows the plain list first, then fills
  titles and icons in a separate job: each disc's title, disc ID and icon are cached under a SHA-256 of the
  URI+size+mtime key, in `cacheDir/psp-icons/`, so a second visit doesn't re-open the disc. The icon is
  downsampled to 160 px at cache time (refused if its PNG declares a size past 512 px), and `SystemDetailScreen`
  decodes the small cached PNG.
- Desktop app (`desktop/PspDiscInfo.cpp`): the Library's line shows the disc's title (its PARAM.SFO's TITLE)
  when there's one, the file's name when there isn't; the states, the saves and the memory card stay keyed by
  the file's name. The list shows at once; the titles fill in after, on a thread of their own, each from the
  cache or the shared reader, and a rescan supersedes the unfinished scan. The title, disc ID and icon (the
  ICON0.PNG's bytes) are cached beside the settings (`dataFolder`'s `psp-icons/`), under a SHA-256 of the
  file's path+size+mtime, as the Android app's `psp-icons/` cache: a second visit doesn't re-open the disc. The
  icon is cached, not drawn (the list's 8x8 font has no place for it), and a title the font can't draw (no
  printable ASCII) is shown as the file's name.
- The PARAM.SFO's CATEGORY isn't read: the Library shows the title and icon, not the category.

**Tests:**
- `tests/psp/disc-info.cpp`: synthetic ISO and CSO (title, disc ID, region, icon all read); a CHD (read through
  libchdr when enabled, taken as none when not); damaged images (no PSP_GAME, a PARAM.SFO whose value's offset
  runs past the SFO's end, an ICON0 over 1 MiB, an empty image); `sfoValue` alone (a value by its key, empty if
  the key isn't there, a SFO with no keys, a SFO too short, not a SFO, a value capped at 128 bytes with
  controls dropped); `regionFromDiscId` (J, U, E, short IDs, empty, unknown prefixes).
- `tests/psp/ares/system.cpp`: a CHD's title, disc ID, region and icon read through libchdr (this build has
  `ARES_ENABLE_CHD`).
- `android/app/src/test/java/com/phobos/emulator/util/PspDiscInfoTest.kt`: the title choice (disc's title when
  it has one, file's name when it doesn't), the cache key (URI+size+mtime, changes when the file changes),
  and the SHA-256 of the key.

**Cost:** the reader touches only a few sectors (the PVD, the directory it traverses, PARAM.SFO, ICON0.PNG), so its
cost is independent of the image's total size. Measured on 20 synthetic images from 100 MB to 3.5 GB through a
sparse in-memory reader: sub-millisecond for every size. On a Mac (M2), a 1 GB CHD opens and reads its disc info
in about 0.3 s; a 4 GB CHD in about 0.8 s. A cached disc (title, disc ID and icon already in `psp-icons/`) takes
no measurable time: the list shows instantly, and the titles and icons fill in from the cache.

## Part 46: the emulation thread faster again — render-to-texture decode later

On branch `cursor/psp-cpu-speed3-2b67`, on top of part 43's `cursor/psp-cpu-speed2-2b67` (with #165 disc-info merged
in). Exactness unchanged: nothing drawn differs.

**What was left (part 43):** on MC3's race at 7 GE threads, most of the remaining emulation-thread drawing wait was
a texture decoded from the batch being filled (render to texture, ~3.4 ms a frame). The decode ran on the GE's
thread before the sampling primitive could be set up, so that batch had to be drawn first (`drawnFirst` → `flush`).

**What this does** (`texture.cpp`, `threads.cpp`, `draw.cpp`, `ge.hpp`):
1. When workers are drawing and a texture's bytes still sit under a batch being filled or already launched, `decode`
   does not settle. If the overlap is the batch being filled, that batch is `launch`ed (drawn in the background);
   the Look waits in the next batch with `deferRows` (and a CLUT snapshot when indexed).
2. As that next batch starts being drawn (`drawBands`, or `flush` when drawn at once), `ensureDecoded` fills every
   deferred Look. Batches are drawn in order, so every batch before it is done and the pixels are in VRAM. One
   thread decodes (`decodeState`); the others wait. `readingDeferred` stops `pointer()` settling this batch while
   those prior pages are still marked busy.
3. The texture cache is shared with the workers (`textures.mutex`).

So the GE's thread sets up the sampling primitive and goes on; the decode runs when the workers (or the GE's thread
helping) are about to draw that batch, not while the list is being read.

**How it's known to be exact:** `tests/allegrex/run-tests.sh` (58 groups) and `tests/psp/run-tests.sh` (328 groups,
including `geThreads`'s render-to-texture and the hardware renderer's). The six cpu-speed scenes remade at
StateVersion 17 under `~/phobos-work/scratch/cpu-speed/scenes` (from the parent tip): 300 frames with every
frame's picture and VRAM hashed (`HASHES=1`), plus the end state, at 1 and 7 GE threads — identical to the
parent (`origin/cursor/psp-cpu-speed2-2b67` / `eeba5028e`) for all six. On the RP6 the race and menu end states
match the parent without settling drawing each frame.

**On the RP6** (7 GE threads, four rounds, remade StateVersion 17 scenes; parent → this tip):

| Scene | Before (median / avg) | After (median / avg) |
| --- | --- | --- |
| Midnight Club 3, race | 37.3 / 36.8 | 38.1 / 38.8 |
| Midnight Club 3, menu | 36.0 / 35.6 | 35.5 / 35.9 |

The race's emulation thread stays about 24 ms a frame either way (within round noise). The menu does not move.

**Left, and why:**
- Primitives that can't wait in a batch, and those drawn at once (textures read from memory as they're drawn).
- Holding registers across block chains; VFPU with prefixes and rarer instructions still interpreted.

## Part 47: the emulation thread faster again — no drawing for its own textures, batches in a ring

On branch `cursor/psp-cpu-speed4-2b67`, on top of #178's `cursor/psp-ge-fits-2b67` (72443bc4d). The task as in parts 38,
43 and 46: the Software renderer faster at 1 and 7 GE threads, nothing changed in any result. Original code: no PPSSPP
or JPCSP source was read.

**The scenes** are part 46's six at StateVersion 17 (`~/phobos-work/scratch/cpu-speed/scenes`). Each commit was checked
against 72443bc4d in all six, 300 frames at 1 and 7 GE threads: every frame's picture and all of VRAM (drawing settled
each frame), then RAM and the machine's whole serialized state at the end. On the RP6 the end states were also
compared without settling each frame.

**Where the time went.** simpleperf on the RP6 at 72443bc4d, with line tables (`-gline-tables-only`, which leaves the
code as it was):
- 1 GE thread: the emulation thread is mostly drawing. Midnight Club 3's race: triangles into its 8888 frame buffer 44%
  of the thread (nearly all of it four pixels at a time: the texel gather 29% of that, the pixel pipeline 21%, the depth
  test 15%), sprites 13%, compiled code 11%, the GE's setup about 14%. GTA's city: triangles 55%. Peace Walker's
  title: 3D sprites into 5551, 86%. Lumines: triangles 75%.
- 7 GE threads, the race (the emulation thread about 26 ms a frame, the bottleneck; the workers about a third busy):
  compiled code 5.7 ms, the GE's setup about 7.5 ms (`drawVertices` 2.3, `project` 1.8, `triangle` 1.1, reading
  vertices 1.5, clipping 0.5), and its own drawing about 6 ms.

Why the emulation thread draws at 7 threads, by a scratch count of where it draws and waits (ms a frame, drawing +
waiting for the workers):
- Midnight Club 3's race: 4 primitives a frame that draw over their own texture (texture.cpp's `drawsOver()`): each is
  drawn at once, reading its texels from memory as it draws, so the whole batch before it is drawn first (4.7 + 1.2),
  then the primitive itself, slowly (1.1). Its menu: 5 a frame, 7.0 + 3.4, then 7.0.
- GTA's city: filling a batch again (two of them) that's still being drawn: 4.3 + 1.6. Its woods: 2.1 + 4.2. Lumines:
  3.1 + 0.5.
- Peace Walker's title: the screen's picture reading pixels still being drawn: 3.1.

Midnight Club 3's primitives are its bloom: the frame buffer shrunk to 64 by 64 (a sprite of v = 0 at its top edge,
filtered, so its rows couldn't be bounded: all 512, which reach the 64 by 64 target), that brightened in place twice,
1:1, filtered, each texel its own pixel's; another target halved in place; and in the menu, a pass whose blending
keeps every pixel as it is (a fixed 0 for the source, a fixed 255 for the destination) over 512 by 320 pixels, and
one shrinking its own frame buffer a little from its top left.

**What was done**, one commit each:
1. **Deferred decodes stay with their batch, their pages busy for the CPU** (`texture.cpp`, `threads.cpp`, `draw.cpp`,
   `memory.*`). Part 46's deferred decode (render to texture) ran on whichever thread started its batch and went into
   the shared texture cache, calling `memory.watch()` off the emulation thread (the recompiler's `protect()`); its copy,
   of VRAM as the batch started, could go stale under primitives set up before it existed (their changes heard of before
   it was kept); and the CPU could write the texture before the batch read it, its pages not busy. Now each batch
   decodes its own (`ensureDecoded()`, a copy shared by its Looks with the same texture and palette), the cache is the
   GE thread's alone again (no mutex), and the texture's pages (`Batch::reads`) are busy for the CPU until the decode
   has run (`readsDone`). `Memory::knownDrawn` lets a thread read VRAM it knows is drawn without waiting or looking at
   the busy pages: the deferred decodes, and the GE thread's own reads after `drawnFirst()` (a texture's decode, a
   palette's copy). It's what the rest builds on; alone it changes the scenes' speed by little (below).
2. **A 2D sprite's texture rows as its pixels reach them** (`draw.cpp`'s `spriteReach()`). A sprite's texture
   coordinate runs one way along its job, so the rows (and columns) its pixels take, the filter's second texel too, are
   those at the job's two ends, worked out with the drawing code's own arithmetic (`spriteAxis()`, shared with
   `raster.cpp` and `four.cpp`; `texelSpot()`, `texelAxis()` before it wraps). The software renderer decodes that many
   rows (in eights), not the vertices' estimate. Midnight Club 3's and GTA's shrunk frame buffers are then no longer
   taken as drawing over their own textures, and GTA's deferred decode of its frame buffer keeps 280 rows busy, not
   512 (whose last rows were the displayed buffer, which the screen's picture then waited for).
3. **Batches in a ring of eight; the CPU waits only for the drawing it touches** (`threads.cpp`). With two batches the
   GE's thread waited for the older one to be drawn whenever it launched the next. Now batches are filled in turn, eight
   of them, the GE's thread waiting only to fill one still launched (the oldest); a batch all drawn is marked
   (`finished`), and those are emptied at each list's start, their pages given back to the CPU (`reap()`). Whoever
   touches busy pages waits only for the batches up to the last one reaching those bytes (`settleOver()`, through
   `Memory::finishDrawingOver`), drawing bands too, then gives back what's drawn; the rest go on being drawn. The GE's
   own reads of VRAM skip batches all drawn.
4. **A primitive that can't change any pixel isn't drawn** (`draw.cpp`'s `keepsPixels()`). Blending with a fixed 0 for
   the source and a fixed 255 for the destination gives (2d + 1) × 511 >> 10 = d for every d up to 255, and each
   format's channels widened and narrowed again are the bits they were, so with no dither, logic operation, stencil
   test or depth written every pixel is written back as it was. Such a primitive is set up and what it may draw over
   reported as before, but not drawn nor its texture decoded, unless reading its texels would have been reported (no
   memory behind them, or a format the PSP doesn't have: `memory.unmapped` counts those in the saved state).
5. **Sprites drawing over their own texture from a copy, where none reads what it drew** (`draw.cpp`'s
   `readsAhead()`). The old way, reading memory as it draws, sees the pixels the primitive has drawn by then. A copy
   taken first draws the same when no pixel takes a texel, with a weight that isn't 0, that the primitive has drawn by
   then, drawing its jobs in order, each row by row, left to right. For unturned 2D sprites into a frame buffer laid out
   as the texture is (the same bytes from row to row, a texel a pixel, VRAM's plain copies), a texel is a pixel a whole
   number of rows and columns away, so it's checked exactly from each job's columns and rows: none in an earlier job's
   area, none in a row above the pixel's in its own, none left of it in its row. Such a texture is decoded (at once,
   or deferred to its batch's start) and the primitive batched like any other.

**How it's known to be exact:**
- The six scenes, 300 frames at 1 and 7 GE threads, after each commit: every frame's picture and VRAM, RAM and the whole
  serialized state identical to 72443bc4d's. On the RP6, the end states of all six matched unsettled, every build.
- `tests/psp/run-tests.sh` (sanitized): 335 groups, none failing (340 after merging #179's desktop tests in). New:
  `ge deferred texture busy` (a deferred texture's pages busy for the CPU, its store landing after),
  `ge batches round the ring` (more than eight batches, against one thread), `ge drawn over its own texture` (the
  bloom's passes in two lists at 1, 2, 4 and 8 threads, shared however small, against a machine that keeps nothing
  decoded), `draw over its own texture from a copy` (each frame buffer format: passes `readsAhead()` takes and turns
  down, from texture layouts at an offset, through VRAM's third copy, of palette indices, each against reading memory),
  `draw keeping every pixel`. `ge drawn on several threads` checks that what the CPU touched is drawn (not everything
  settled); `draw textures kept decoded, their rows` keeps 16 rows, not 24 (those the sprite takes).
  `tests/allegrex/run-tests.sh`: 58 groups, none failing. `tests/psp/ares/run-tests.sh`: 307 checks, none failing (the
  parent's count on this machine too).
- Three independent read-only reviews. The first (commits 1-3) found no change to any result; its points were taken:
  a test's expectation, an uninitialized read after `spriteReach()` failed, `reap()` returning before giving back stale
  busy pages and `settleOver()` not giving any back, `reads` counted after their decode ran, the workers' formally
  racy look at the busy pages (now `knownDrawn`), comments, and the commits' order (deferred decodes first). The second
  (commits 4-5) found the one change to a result: a skipped primitive whose texels have no memory behind them no
  longer counted those reads in `System::unmappedReports`, which save states keep, now guarded; and asked for more
  test cases and a smaller capture for the `readsAhead()` callback. The third checked those fixes and found one more:
  `Memory::knownDrawn` around a palette's whole load also covered its byte-by-byte read past a VRAM copy's end, which
  `drawnFirst()` hadn't seen (now only the copy within one area skips the wait); and asked for tests of the skip's
  guard, of `decode()` taking the copy, and of the CPU's loads while batches draw. Those were added, and it confirmed
  the result.

**The numbers.** The RP6, the runner from adb's shell (unpinned), the Phobos app not running (checked before and after
each round; a first set of rounds, during which the app was started, was thrown away). Medians, the builds taking
turns. fps is the frame loop's; thread is the emulation thread's CPU time, ms a frame. The end states of all six
scenes were the same in every build, every round.

7 GE threads, 72443bc4d → the last commit, four rounds:

| scene | fps | thread ms |
| --- | --- | --- |
| MC3 race | 34.0 → 53.5 | 26.0 → 18.5 |
| MC3 menu | 33.3 → 71.9 | 26.1 → 13.1 |
| GTA city | 55.1 → 78.8 | 16.3 → 11.5 |
| GTA woods | 53.3 → 71.8 | 14.9 → 11.3 |
| Peace Walker title | 213.7 → 224.5 | 4.23 → 4.09 |
| Lumines demo | 171.3 → 182.7 | 4.95 → 4.67 |

At 7 threads the RP6's frames a second move with the cores the threads land on (part 38): the base's race ran at
32.8-35.3 here, 36-40 in part 46's rounds; the last commit's at 52.7-56.1.

1 GE thread, three rounds (steady to under 1%):

| scene | fps | thread ms |
| --- | --- | --- |
| MC3 race | 23.02 → 23.37 | 43.2 → 42.6 |
| MC3 menu | 25.60 → 30.49 | 38.9 → 32.7 |
| GTA city | 38.93 → 39.68 | 25.6 → 25.1 |
| GTA woods | 38.97 → 39.79 | 25.6 → 25.0 |
| Peace Walker title | 66.00 → 66.16 | 15.11 → 15.07 |
| Lumines demo | 91.04 → 90.95 | 10.30 → 10.25 |

At 1 thread only the menu moves much: its pass that keeps every pixel (164,000 pixels a frame, read from memory as it
drew) isn't drawn, and the in-place passes go four pixels at a time from their copies. The rest is the rasterizer.

Step by step at 7 threads (fps, medians of two rounds; base, then after each commit):
- MC3 race: 34.1, 37.5, 37.2, 39.0, 38.8, 55.1.
- MC3 menu: 33.6, 37.2, 39.2, 34.8, 39.3, 74.9.
- GTA city: 54.9, 55.4, 59.6, 84.5, 80.6, 80.4.
- GTA woods: 56.6, 51.0, 75.0, 67.6, 79.6, 75.2.
- Peace Walker title: 214.2, 215.1, 214.4, 225.9, 225.9, 225.2.
- Lumines demo: 174.2, 175.4, 171.7, 187.6, 188.2, 187.2.

The ring (commit 3) is GTA's, Peace Walker's and Lumines' step; the copies (commit 5) and the skipped pass (commit 4)
Midnight Club 3's. The exact rows (commit 2) help GTA by themselves (its shrunk frame buffer no longer read from
memory every other frame) and are what lets Midnight Club 3's downsample, and every deferred decode, go to the
workers. Commits 1 and 2 alone move the race and menu within a 7-thread run's spread. With the last commit, the race's
workers are each about 60% busy and the emulation thread is the bottleneck still, at about 18 ms a frame:
compiled code 5.4, the GE's setup about 7, the rest the kernel, the VFPU's interpreted instructions and the like;
it no longer draws for its own textures.

**Left, and why:**
- At 7 threads the emulation thread is now its own work: the CPU (the race's compiled code about 5.4 ms a frame; the
  VFPU with prefixes set, and the FPU's remaining conversions, still interpreted; registers held only within a block)
  and the GE's setup (about 7 ms in the race: `project()`'s four divisions a vertex, lighting, each vertex built and
  copied, each job copied into its batch). Each is spread thin; none is a large step alone.
- At 1 thread the frame is drawing, four pixels at a time, bound by divisions that must stay as they are: a lit,
  fogged, perspective-textured triangle makes about eleven four-wide divisions a four. Nothing exact is left there of
  the size of what's above.
- A deferred own-texture sprite is drawn a pixel at a time (`fourFriendly()` decides as the job is recorded, before the
  copy exists); one decoded at once goes four at a time.
- `readsAhead()` covers unturned 2D sprites in a frame buffer laid out as their texture; others drawing over their own
  texture are still drawn at once from memory. It re-derives the jobs and relies on raster.cpp's order for such a
  primitive: a change to either must keep them in step.
- Deferred decodes aren't kept between batches (a texture deferred in two batches is decoded twice: about 1.25 ms a
  frame in the race, over all threads).
- The screen's picture reading pixels still being drawn (Peace Walker's title) still waits for them. A CPU touching a
  busy page no batch still draws over gives nothing back till a wait or the next list (it goes the slow way meanwhile).
- Older, seen by the reviews, left as they were: `drawnFirst()`'s span stops at a VRAM copy's end, so a palette or
  vertices running past it into the next copy aren't waited for there (the palette's byte-by-byte read still waits in
  `pointer()`); and `vramGuard` giving a page back calls the recompiler's `writable()`, which lets compiled stores go
  straight to it even if code was compiled from it (only code run from VRAM the GE draws or reads would notice).

## Part 48: colors, fog and depth stepped as the PSP steps them; round 5 for what's left

On branch `cursor/psp-ge-fits2-2b67`, on top of #180's `cursor/psp-cpu-speed4-2b67` (a4a63b13f). A last fitting pass,
with the measurements we already have (round 2 and round 3, `tests/psp/measurements/ge-round3` and the raw files the
owner keeps), at the three things #178 left open: colors stepped across triangles, lighting's last six products, and
perspective-correct texels. A rule was taken only where it reproduces every measured case it covers. Then round 5 of
`tools/psp-measure`, for what the data couldn't settle. Original code: no PPSSPP or JPCSP source was read.

### Colors, fog and depth: fitted

The fitting was done by a scratch copy of the core that wrote down every triangle it set up (its corners in sixteenths
of a pixel, their colors, fog, depths and texture coordinates), for each test, and a small program that tried rules on
those triangles against the PSP's pictures (thousands of rules a minute, exact integer arithmetic).

**The rule** (`draw.cpp`'s `stepped()`, `raster.cpp`, `four.cpp`). The GE doesn't blend a triangle's colors at each
pixel; it steps them from one corner, with steps of limited precision:
- **The corner**: the one texture coordinates already step from in 2D (part 10's "Fixed since"): the leftmost, the
  topmost of two. Each value starts as exactly that corner's.
- **The steps**: one a sixteenth of a pixel across and one down, in 16384ths of a level (14 bits below a whole level),
  rounded down. They're worked out through the reciprocal of twice the triangle's area (in sixteenths squared) kept to
  16 significant bits, rounded down: `2^(bits + 16) / area` for an area of at least `2^bits` and below `2^(bits + 1)`.
  So an area that's a power of two gives exact steps, but a step that should be a whole number of 16384ths, such as a
  ramp of 255 levels over 240 pixels (17/16 of a level a pixel), falls one 16384th short (and a step downward, one
  further down).
- **A pixel**: the corner's value, plus each step times the sixteenths from the corner to the pixel's middle, rounded
  down to a whole level, held to 0-255.

That explains round 3's level shortfalls: a value can come out a level below the true blend, more likely the further
from the corner; and the vertical ramps' values a level above it where their triangles start from the bottom. The same
stepper does **fog** (its 0-255 amount at each corner, #178's fit) and **depth** (0-65535, each corner's a whole
number), with the same 14 bits. The cut at the near plane needed one more change:

**The cut at the near plane** (`transform.cpp`'s `clipTriangle()`). A triangle with one corner past the plane leaves
four corners, which the core walks round the edges as the two kept corners and the two cuts. It drew them as a fan
from the first, corners 0, 1, 2 and 0, 2, 3; now it draws corners 0, 1, 3 and 1, 2, 3. In `3d-clip`, the one case
measured (its third corner past the plane), that's the PSP's split, from the second kept corner to the cut after the
corner past the plane: every pixel of `3d-clip` is then the PSP's, where the fan's split leaves 1113 color values a
level apart. With the first or second corner past the plane, which split the PSP makes isn't measured: round 5's
`clip-split` cuts with each corner past it, each way round. The hardware renderer gets the same triangles (it's drawn
through `clipTriangle()` too).

**What it does to the measured pictures** (pixels a level apart, before and after):

| File | Before | After |
| --- | --- | --- |
| `ramp-colors` | 1712 | 0 |
| `ramp-colors-vertical` | 1359 | 0 |
| `ramp-colors-3d` | 6992 | 0 |
| `ramp-fog` | 240 | 0 |
| `3d-floor-fog` (round 2) | 497 | 0 |
| `3d-floor-depth` (round 2, depths) | 1388 | 0 |
| `3d-clip` (round 2) | 1512 | 0 |
| `bezier-flat` | 15368 | 0 |
| `bezier-curved`, `spline-edges-3` (the same picture) | 1494 | 1056 |
| `bezier-divide-8` | 938 | 721 |
| `spline-edges-0` | 787 | 782 |

`gouraud` stays identical, and every other file is as it was: no picture got worse.

**How tightly it's pinned.** Against the three color ramps, `gouraud`, `3d-clip` and the curved surfaces together, the
nearest alternatives all lose: the reciprocal kept to 17 bits leaves `ramp-colors-3d` 144 values apart, to 15 bits
320, an exact reciprocal (or one of a fixed 2^36 or more) 144; steps in 512ths, 2048ths or 4096ths of a level a pixel
(rather than 1024ths: 16384ths a sixteenth), thousands; steps rounded to the nearest or toward zero, thousands more;
starting from the first corner given, or the topmost, or the leftmost but the bottom one of two, thousands (the last
1810 in `ramp-colors-vertical`). Depths in 4096ths or 65536ths of a depth a sixteenth leave 692 and 209 of
`3d-floor-depth`'s 7728 measurable values apart. Bands of a power-of-two width (256, 128, 64, 16 pixels) come out
exact under any of these, which is why `gouraud` (a 256-pixel square) always matched.

**The curved surfaces** are better but not exact, and the rest isn't the stepping: in `bezier-curved` every triangle
fits the rule exactly once its start (its leftmost corner's value) is moved by less than about 0.05 of a level, the
steps as the rule has them; so what differs is the vertices the GE makes for a curved patch (their values, or where
they fall finer than a sixteenth). The flat patch's vertices fall on sixteenths and its colors on whole levels, and
it's exact. Moving the curved patch's vertices a sixteenth each way barely helps (1144 color values apart to 1139),
nor does splitting its quads the other way (both splits tried quad by quad; the one pspautotests recorded wins in
every quad). Round 4's `curves-*` record those vertices directly. `spline-edges-0`'s vertices are further off (4 edge
pixels already differ).

**Not changed, as not measured**: lines (colors, fog and depth along them are still blended as before; round 4's
`lines-colors`, `lines-depth` and `lines-3d` will show whether the same stepper runs along them), sprites and points
(nothing to step). The shine kept apart (LIGHT_MODE 1) and alpha are stepped like the colors: they're colors to the
same interpolator, though neither was measured across a triangle (nor has the shine a hand-worked test: it goes
through the colors' code, and the random four-at-a-time test lights triangles with it now and then).

**The hardware renderer** (Vulkan) can't mirror this: its colors, fog and depth are the GPU's own blend between
vertices, as before (part 36). Only the cut's split is shared.

**Tests** (`tests/psp/draw.cpp`'s "draw steps", and new checks in `draw3d.cpp`'s "draw3d clipping" and "draw3d fog"),
worked out by hand from the rule: a ramp of 128 levels over 15 pixels whose eighth pixel the true blend puts at 64 and
the steps at 63, the same in depths, the same blend stepped from the bottom left corner coming out 64, the topmost of
two leftmost corners, a triangle where 15 or 17 bits of reciprocal give another level, one where rounding the steps to
the nearest or toward zero does; the same ramp in fog; and the cut's split, two pixels of `draw3d clipping`'s cut that
the fan's split would give another level. Broken versions each failed them: the reciprocal to 15 or 17 bits, an exact
reciprocal, steps rounded to the nearest, the start from the first corner, the fan's split, and the code before this
part. "draw3d four pixels at a time against one" draws 20000 random primitives both ways, as before: the
four-at-a-time path steps in 32 bits that may run round (every lane inside the triangle keeps its value;
`fourFriendly()` sends triangles with steps of 2^24 16384ths a sixteenth or more, slivers, a pixel at a time), the
other in 64 bits, each row's start in 128.

### Lighting's last products: still open, and why

The core takes a light's share of a color as `ceil(256 × cosine)` 256ths (round 3's fit), which gives 1620 of 1632
values with exact cosines. The last 12 aren't a rounding detail: **the share isn't in 256ths at all.** For each
normal, all the products measured with it (about a hundred light-and-material pairs) pin the share the GE used to an
interval, and two of them hold no whole 256th:
- (33, 0, 56), cosine 56/65 (220.55 256ths): the share lies between 220.86 and 220.985;
- (65, 0, 72), cosine 72/97 (190.02): between 190.74 and 190.88.

So the share is finer than a 256th, and not simply above the true cosine: round 2's `light-diffuse` (normals (k, 0,
127 - k), its 255 channel pinning the share) has it below the cosine in places, such as (101, 0, 26), cosine 63.82
256ths, whose share lies between 63.25 and 63.63 (0.3% to 0.9% short). It isn't a function of the normal's length
alone: (16, 0, 63) and (33, 0, 56) are both 65 long, and their shares are over by 0.34% to 0.42% and 0.14% to 0.20%. A
cosine worked out in floats of 6 to 24 bits (rounded each way, through a square root or its reciprocal) misses at
least 114 of the 304 intervals. Nothing here pins the GE's arithmetic further, so lighting stays as it was. Round 5's
`light-share-*` measure the share of 16 normals with 192 products each, finer than a hundredth of a 256th, among them
those normals three times as long and turned.

### Perspective texels: still open, and why

`3d-wall-texels` (307 pixels a texel apart), `3d-floor-texels` (291) and `3d-sprite-texels` (72) stay as they were.
Stepping u / w, v / w and 1 / w with this part's stepper (u / w and v / w in units of 1 to 2^-16 texels, 1 / w in 2^-8
to 2^-24, steps of 2^-10 to 2^-18 of a unit a sixteenth) fits neither: the best leaves the floor 188 apart but the
wall 389. What the pictures show instead: on the floor, where 1 / w is the same along a row, the PSP's u comes out
short by a fixed fraction of u in each row (about 0.2% on row 164), as if 1 / w were a little high there; on the wall,
where 1 / w is the same down a column, it's short in most columns and over in some (column 156's whole u is 235 where
the true one is 234.98), and no one factor fits some columns' u and v together. That points at the divide at each
pixel, of limited precision, more than at the steps; 256 texels across 256 pixels show it only where a coordinate is
within a few hundredths of a whole texel. Round 5's `persp-*` read the coordinates to a texel in four thousand.

### Round 5

`tools/psp-measure`'s new first menu line, "Round 5: the GE's steps, lighting's share, texels (about 3 MB)", runs 12
tests into `results/ge` (`manifest5.txt`), each a 256x256 picture of 32-bit words (`ge.c`'s `round5()`):
- `steps-check`: 64 triangles at random (one to a 32x32 cell, corners to the sixteenth, colors from a fixed sequence)
  in through mode: the rule on slanting edges and every kind of corner; `steps-depth-check`: the same with random
  depths (written always), the depth buffer read through VRAM's fourth copy.
- `clip-split`: six bands, each round 2's 3d-clip triangle squashed into it with its blue corner past the near plane
  (cut two thirds of the way to it): bands 0-2 with the corners turning as 3d-clip's, the blue one first, second and
  third (band 2 is 3d-clip's order), bands 3-5 turning the other way, blue first, second and third. The two ways to
  split the four corners left differ in 181 pixels of each band (in the core's arithmetic), so each band will show the
  PSP's.
- `light-share-0` to `-3`: four normals each (rows 4j to 4j + 3), each row with its own light color and each cell its
  own material color (levels 96-255): 192 products a normal, which pin its share finely. The normals: round 3's two
  odd ones and (16, 0, 63), as long as one of them, and round 2's (101, 0, 26); the same four three times as long;
  four more of round 2's `light-diffuse` normals; and the first four turned (cosines a / c).
- `persp-wall`, `-divide`, `-w3`, `-floor`, `-sprite`: a repeating texture and coordinates up to 4096 texels, so each
  pixel shows its coordinates to the texel (its texel is their whole part less a multiple of 256, which a prediction
  within 128 texels tells; from one pixel to the next they change by less than that, the most at the far end): round
  3's wall and 3D sprite and round 2's floor (the floor's v and the sprite's coordinates to 2048 texels, so they
  change by under 128 a pixel); a wall whose u / w is the same at every corner, so u shows 1 / w alone; and one at w 3
  everywhere, no perspective, which shows the steps and divide by a 1 / w that isn't a whole number of any power of
  two.

#178's round 5 probes (source only, never run) are gone, each measured more finely by one of these: its color ramps by
`steps-check`, its two lit cells by `light-share-0`, its shorter wall and floor by `persp-wall` and `persp-floor`.

`tests/psp/measure.cpp` drives the new line too (the menu's lines below it moved down one) and checks every file is
written; once a PSP's `results/ge` with `manifest5.txt` is given in `PSP_GE_RESULTS`, it lists what differs, as for
the other rounds. `pspmeasure.elf` in `tests/psp/programs` is rebuilt (its README has the new SHA-256), in the same
pspdev image as before (`ghcr.io/pspdev/pspdev@sha256:54895e6f...`), which rebuilds the previous `pspmeasure.elf` byte
for byte from its own source; the other programs are as they were. Round 4 hasn't been run on the PSP yet either: its
line is the second.

The owner's copy: `.local/psp-round5/` beside the repository (not committed) holds the EBOOT.PBP, its SHA-256 and a
HOW-TO.md (where it goes on the memory stick, which line to run, how long, and which folder to bring back).

### Review

An independent read-only review found no bug in the core: no overflow or undefined behavior in `stepped()` and the row
starts (checked against exact 128-bit arithmetic on about 3 million triangles, slivers and far corners among them),
the scalar and four-at-a-time paths byte-identical over 550,000 extreme triangles, and nothing else changed (flat
shading, the shine, fog off, clear mode, the hardware renderer, lines, points, sprites; equal corner depths still give
exactly that depth). What it found, fixed before committing: the reciprocal's formula off by one in this part and in a
comment; probes credited to round 3 that are round 2's; the cut's split stated as if measured for every corner;
`clip-split`'s middle band turning the other way round as well as moving the corner past the plane (now six bands,
each corner past it each way round); light and material levels taken in one expression, in an order C leaves open (now
one statement each); the floor's v changing by more than 128 texels a row at the far end, too much to tell which
multiple of 256 it is (now a span of 2048, and the 3D sprite's too, which came close); stale comments, an unused
function and field; no hand-worked test of fog's steps (added). Seen and left as it was, older than this part: lines
and points in through mode turn a depth that isn't a number into a whole number (undefined behavior), where triangles'
corners now guard against it.

### Checks

- `tests/psp/run-tests.sh` (sanitizers on): 341 groups, 0 failures (one new: "draw steps");
  `tests/allegrex/run-tests.sh`: 58 groups, 0 failures; `tests/psp/ares/run-tests.sh`: 307 checks, 0 failed.
- The measure test against the owner's round 2 and 3 files: as in the table above, nothing else changed; it drives
  round 5 through the menu and every file is written.

### Round 4 again: the GE's waits timed (2026-10-09)

Running round 4, the owner's PSP froze at `curves-count` (16, 63, 64 and 65 divisions in one list) and
`curves-count-128` (100 and 128), and had to be switched off by hand, twice a test before results.c set it aside.
Most likely the GE never finished the list and `finishList` waited for it in `sceGeDrawSync(0)` for good (part 33
had expected a stall past pspsdk's 64); nothing showed whether the CPU was still running. Now:
- `waitForGe` peeks (`sceGeDrawSync(1)`) every millisecond for 5 seconds at most. A list still not done then is a
  stall: `<test>.stalled` records it (how long, the driver's state, what the list held) before anything is done
  about it, so if the reset stops the PSP the next start gives up on that test at once rather than after a second
  freeze (results.c). Then `sceGuBreak(GU_BREAK_CANCEL)` (`sceGeBreak(1)`: every list thrown away and the GE reset),
  its result recorded, and `sceGuTerm`/`sceGuInit` (its own list gives the GE back its first state; it waits for
  that list, for good if the GE is still stuck, and the record then lacks its result); the test saves what the GE
  had drawn, as `.bin`, and the round goes on. A freeze with no "the GE stalled" on the screen and no `.stalled` is
  the other kind (the CPU stopped too), handled as before.
- Each count past 64 is a test of its own: `curves-count` (16, 63, 64; rows 0-2), `-65`, `-100`, `-128`, `-200` and
  `-255` (rows 3-7, each in its own row as before). What the earlier program left of the counts (`.part`, `.again`,
  `.stopped`) is cleared once in a results folder (`curves-count.timed` marks it), so they run again; finished
  `.bin` files are kept.
- `stall-check`, before them, gets the GE stuck on purpose (a list left waiting at its stall address), so the reset
  runs whenever round 4 runs in a results folder for the first time, in this core too (the measure test checks its
  `.stalled` and that 16 divisions' count drawn before the reset and after it match; then that results.c gives up on
  this program's own leftovers, which ge.c's one-time clearing leaves alone). On the PSP, a break on a list stopped at
  its stall address is what pspautotests' gpu/ge/break recorded; a GE stuck inside a patch is unmeasured, and the next
  run will show whether the reset frees it.

`pspmeasure.elf` is rebuilt in the same image; the owner's copy is `.local/psp-round5b/` (EBOOT.PBP, its SHA-256,
HOW-TO.md).

## Part 49: the Vulkan renderer in play on the RP6, and its blending without rasterization order

On branch `cursor/psp-vk-play-2b67`, on top of #186's `cursor/psp-hle-games10-2b67` (#183, #182 and #180 under it).
docs/psp-gpu-renderers.md has the detail ("Without rasterization order", "Upscaling", and the accuracy and speed
tables). Original code: nothing of PPSSPP's or JPCSP's used, copied or translated. Software stays the default and
untouched; Native stays exact where it was (every blend still read in rasterization order, so on Turnip the pictures
are as before).

**In play, in the app** (the RP6, Adreno 740, Turnip from the Driver Manager: blending in the shader, in order;
Vulkan at Native unless said; the app's per-second stats over 15 s, 60 being full speed; each game entered by scripted
presses on the app's on-screen buttons and left by force-stopping the app, nothing saved):

| Game | Reached | Frames a second |
|---|---|---|
| Midnight Club 3: DUB Edition | a Quick Race, racing | 29.9 |
| GTA: Liberty City Stories | a new game's first mission, driving | 50.6; 4x 22.8, 10x 10.8; Software 60.0 |
| GTA: Vice City Stories | a saved game, in the city | 57.7 |
| Burnout Dominator | a race | 52.4 |
| Burnout Legends | a Single Event race, at 123 mph | 43.2 |
| WipEout Pure | a Single Race | 19.4 |
| Metal Gear Solid: Peace Walker | a new game's beach tutorial | 45.0 |
| MotorStorm: Arctic Edge | a Free-Play race (and its crash replay) | 50.7 |
| Ace Combat X | a campaign sortie, in flight | 60.0 |

The pictures were right to the eye in all of them, at each resolution: nothing missing, misplaced or flickering, no
crash. Killzone wasn't started in the app: it saves by itself, to the owner's memory stick (the runner, with a
scratch one, reached its Start Game menu). Ridge Racer 2 asks for a new driver profile again and again (the kernel's,
not the renderer's; part 50 fixes it), so only its menus were seen: the 8x line, below. At 10x the GPU is the bound (81% busy at its
top clock, 680 MHz); at 4x it wasn't (about 56% busy at 401 MHz, the CPU 61%). Going home and back while LCS
ran at 4x kept presenting; so did making the window smaller and back while it presented (`wm size 900x1600`, then
reset: about 30 fps each way). Rotation couldn't be tried: the RP6 puts its rotation lock back to landscape at once
(`cmd window user-rotation lock 0` or `3` snaps back to `1`).

**Faster at Native in software.** Part 47 made the software renderer's seven threads much faster, and in the 3D games
they're now ahead of Vulkan at Native: LCS above, driving, 60.0 frames a second against 50.6; from adb's shell
Midnight Club 3's race 53.5 against 33.8, GTA's city 77.2 against 63.7. With Vulkan the emulation thread is the
bound: the transform and setup as before, the recording, and a wait whenever the CPU or the GE reads what the GPU
drew (the race reads back 4 times a frame). WipEout Pure's race is the extreme: 209 submits, 9 read-backs and 53 new
textures a frame, Vulkan 8.6 frames a second on the M1 against 16.7 in software. On the RP6 Vulkan's lead is in the
2D games, in drawing sharper above 1x, and in leaving six cores idle (on the M1 seven threads lead everywhere).

**Without rasterization order, the GPU blends again.** Part 45 had every blend read the frame buffer in the shader;
without the order each such draw takes a barrier and holds no overlapping primitives, so a 3D game's blending became
thousands of draws a frame: Midnight Club 3's race 4 million in 300 frames, 1.3 frames a second on the M1 (MoltenVK)
and 13 on the RP6's own driver. Now, without the order, blending in an 8888 frame buffer by the factors and operations
the GPU has is the GPU's again (38.2 and 32.2 frames a second), and only what it can't come close to reads in the
shader, split as before: a write mask keeping part of a channel, a logic operation, the absolute difference, doubled
alphas, and any blend in a 16-bit frame buffer, where the PSP narrows each result to the format, dithered, before the
next blend reads it (Peace Walker's title, 5551, was 39.7% identical without that, 99.7% with). In order every blend
reads as before, now without the barrier between draws, which the order makes needless once every pipeline of the
subpass asks for it (in a run before and after, on an earlier build, GTA's woods 59 to 71 frames a second on Turnip
from adb's shell).

**The GPU's blending closer** (`draw.frag`'s `TERM`): where the GPU takes output 0 as it is, the shader makes that the
PSP's whole-number source term and puts it a quarter of a step below itself where the GPU adds the destination's
term, above where it subtracts, so the GPU's one rounding lands where the PSP's dropped fractions do. Alpha blending
over every value, in a model: channels the same 50.6% to 93.7% (92.7% in half floats). The tests' random 8888 blends:
8893 channels of 9216 the same on the M1 and on both of the RP6's drivers, none more than 8 apart.

**Qualcomm's own driver reads nothing.** The RP6's system driver (build of 12/27/23) passed the start-up check's small
pictures, but in a game's frame buffer a draw that read sometimes saw the pixels as they were before the draws just
before it, with a barrier, an image barrier or shader-read access as well: from adb's shell Midnight Club 3's menu was
10% identical, noise in all but Peace Walker. So `vulkan.cpp` turns reading off for that driver (its ID, from
`VkPhysicalDeviceDriverProperties`) where it has no rasterization order, and the GPU blends everything there; with
the order, should its check fail, the retry reads nothing either (`Backend::readsApart`). The start-up check gained a
game-sized picture (480x272 in rows of 512: 128 opaque sprites, each under a blended one moved a few pixels, through
a write mask keeping part of each channel), which caught that driver some of the time; a driver not known that fails
it in order is checked again with the GPU blending, then with nothing read (`System::startRenderer()`).
The log's start-up line says "blending in the shader, in order", "blending in 8888 by the GPU, the rest in the shader,
overlaps apart", or "blending by the GPU".

**The 8x line**, found with the runner on the M1 at 8x from a state just before Ridge Racer 2's title, a frame every
10, each column compared with its neighbours: a line down the middle (the PSP's x = 240) in each frame that draws the
menu's picture. The picture is two 2D quads meeting there, the right one's texture mirrored (u from 240 back to 0),
filtered. At 1x every pixel's middle samples at least half a texel inside the picture; at 8x the GPU's pixels a
sixteenth from the seam sampled at u = 239.94, which the filter blended with texel 240, past the picture. Above 1x a
2D triangle's texture coordinates are now held inside its corners' by what half a pixel moves them, half a texel at
most (`triangle()`, `draw.frag`), as far as its pixels' middles reach at 1x: the line is gone from every frame, and 1x
is untouched.

**Accuracy on the RP6's GPU** (10 frames of each of the six StateVersion 17 scenes, Software against Vulkan, pixels
identical; docs/psp-gpu-renderers.md has the channels apart): from adb's shell on the system driver, which reads
nothing, Lumines 73.0%, Peace Walker 40.4%, Midnight Club 3's menu 88.7% and race 67.1%, GTA's city 52.2% and woods
49.3% (45.6%, 39.8%, 78.7%, 33.9%, 41.4% and 39.2% before `TERM`); with Turnip, the app's driver (the runner opening
its `.so` itself, as the app does, a scratch patch), 97.8%, 100.0%, 97.5%, 87.8%, 73.4% and 80.6%; on the M1, 77.9%,
99.7%, 92.6%, 80.3%, 52.9% and 61.9%. adb's shell can't use the app's Turnip: the app loads it through libadrenotools.

**Tests**: `gpuBlendingApart` (new): without the order, the GPU's 8888 blending draws nothing in the shader and splits
nothing, at least 85% of channels the same and no more than 3 in 1000 more than 8 off (run on a GPU that reads nothing
too); masks, logic operations, the absolute difference, doubled alphas and every 16-bit blend, dithered or not, read
and are the software renderer's bytes in each format; a 16-bit blend by the GPU's own factors is a draw that reads.
`gpuSeams` (new): two 2D quads meeting at a mirrored seam, filtered, at 1x, 2x and 3x: every one of the GPU's pixels
the picture's, memory the software renderer's (without the held texels it fails at 2x and 3x). `gpuCheck`: the
game-sized picture's 128 blends read, and the check passes with nothing read. `gpuBlending` asks for every blend to
read, as in order. The GPU tests built for Android pass on the RP6 on Turnip and on Qualcomm's driver.

**Checks**: `tests/psp/run-tests.sh` (sanitized) 342 groups, none failing; `tests/allegrex/run-tests.sh` 58 groups;
`tests/psp/ares/run-tests.sh` 307 checks, none failing. An independent read-only review passed it with fixes, all
taken: the check's blended sprites over their opaque ones, the held texels scaled by the coordinates' slope, the
driver's ID through the extension alone, `readsApart` so a retry on that driver reads nothing, doubled alphas read
only for the operations that use factors, `TERM` not where output 0 is also the destination's factor, tests that
can't pass without the GPU drawing, and the docs brought in line. The final build installed (106205, `adb install
-r`): Ridge Racer 2's title at 8x without the line (39 fps, the GPU 80% busy), LCS in play at Native; then the
Renderer and Resolution put back to Software and Native.

**Left**: Vulkan at Native behind seven software threads in the 3D games (the emulation thread's recording and its
waits at read-backs; WipEout's submits); Qualcomm's own driver's 16-bit blending (Peace Walker's title a step or
three apart) and its reads, which a render pass of their own might make right (not tried); blended pixels not
dithered where the GPU blends; a 2D sprite's stepped coordinates not held above 1x (none seen bleeding); rotation on
a device that allows it; Killzone in play, and Ridge Racer 2's races; the swapchain in the panel's own orientation.

## Part 50: Ridge Racer 2's driver profile — the keyboard's answer

On branch `cursor/psp-hle-games9-2b67`, on top of #182's `cursor/psp-ge-fits2-2b67` (#180 under it). Sources: pspsdk's
`psputility_osk.h` and its keyboard sample (`samples/utility/osk`), pspautotests (it has no keyboard program or
recording; utility/dialog's sizes and statuses are part 42's), the PSP Developer Wiki through web.archive.org (no page
on the keyboard or the utility dialogs in its list of pages, December 2023), and the games' own code, traced and
disassembled with a scratch runner. No PPSSPP or JPCSP source was read.

**The report.** On the owner's RP6, Ridge Racer 2's new game showed "Welcome to the world of Ridge Racer! Please
create your driver profile." with an OK button, then went back to the menu instead of making a profile.

**Reproduced** in the runner, Software, from boot with presses (`~/phobos-work/scratch/rr2/race.script`): Select past
the Rally-X loading game (frame 650), Start past the intro (900), Start at the title (1200), Cross on NEW GAME (1300),
the notice, Cross on its OK (1450). A scratch build logged every sceUtility call: its parameters' size, the savedata
mode and names, the keyboard's fields, the answer and every status asked for (`scratch/rr2/trace.patch`):
- At boot: savedata's sizes mode (8, 0x600 bytes) answered 0; its autoload (mode 0, UCES00422 / 000 / DATA.BIN, into
  0x6900 bytes) no data (0x80110307), as there's no profile yet.
- At OK (frame 1486): sceUtilityOskInitStart, 0x40 bytes, one field: "Driver Name (7 CHAR MAX.)", holding "DAMACY"
  (the game's default driver name), room for 8 characters with the NUL, at most 7, input type 4, one line. Two frames
  on the game starts it again and is refused INVALID_STATUS, one being current (it doesn't look at that answer; a PSP
  refuses it too, part 42). The statuses: 1 for 18 frames, 2 (Update: the text as it was, the field UNCHANGED), 3
  (ShutdownStart), 4, 4, 0.
- Then no utility or I/O call and no missing function before the title showed again (frame 1520).

**Where the game gives up.** Its keyboard poller (0x08878164; its menu script's commands 0xcc and 0x136 open the
keyboard, 0x08872280) acts at status 0 on the field's result (read from 0x08a7402c): CHANGED (2) copies the name (7
characters at most) and tells the script the player answered (1); CANCELLED (1) and UNCHANGED (0) alike tell it the
player backed out (2), and the script goes back to the title. The kernel answered UNCHANGED for every field that
wasn't empty (part 22's choice, listed there among the uncertain things): here, the default name.

**What the PSP answers.** Nothing recorded says: pspsdk's header only names the three results, its sample prints them,
and pspautotests has no keyboard program. The games do. Of the 23 games here, ten import the keyboard's functions
(007: From Russia with Love, both Ace Combats, Brave Story, Killzone, Midnight Club 3, Peace Walker, Ridge Racer 2,
Space Invaders Extreme, WipEout; Peace Walker from a module it loads later, Brave Story only GetStatus, its keyboard
its own). Where each followed reads a field's result once its keyboard is gone:
- Ridge Racer 2: only CHANGED is an answer, though the field holds a name of its own.
- Killzone (0x089641b8): the name copied only when the common result is 0 and the field CHANGED; its field holds the
  name it has.
- Space Invaders Extreme (0x088825d8): the name taken only when the common result is 0 and the field CHANGED, else
  -1; its field starts empty.
- Midnight Club 3 (0x08b3f00c): anything but CANCELLED taken as the answer.
- WipEout takes the text whatever the result; Peace Walker checks the text itself (part 22); the others weren't
  followed that far.
Killzone, Space Invaders Extreme and Midnight Club 3 can't tell the two answers apart (Killzone's field holds the
name it keeps either way, Space Invaders Extreme's starts empty); Ridge Racer 2 can. It fills the field with a name
of its own and takes only CHANGED, so it would send every player who kept that name back to its title if a PSP said
UNCHANGED of it: a PSP is taken to say CHANGED of every field the player confirms, edited or not.

**The fix** (`utility.cpp`'s `keyboard()`): every field answered is CHANGED (2), its text as before (what it started
with, an empty field the console's nickname "PSP", as far as its room and limit allow). "utility keyboard" checks each
field CHANGED, Ridge Racer 2's field among them (DAMACY, room 8, limit 7); the old answer fails 6 of its
checks. No kernel state is added: the save states' version stays 17.

**After the fix**, from boot (Software: `scratch/rr2/sw-race`; the PNGs in `scratch/rr2/evidence`): the name taken,
"DRIVER NAME DAMACY. Would you like to proceed?", YES; the profile saved (savedata mode 3, 0x600 bytes:
`PSP/SAVEDATA/UCES00422000/DATA.BIN`, 26,628 bytes, DAMACY among them); "Would you like to turn Autosave ON?", YES;
the main menu; Arcade, Seaside Route 765, class 1, the Shootaway Fiera, automatic, OK, NOW LOADING, and the race: GO!
at frame 4200, 158 km/h in the tunnel at 4800, Cross held. Vulkan (MoltenVK, the M1, `scratch/rr2/vk-race`): the same
screens at the same frames. Booted again with that memory stick, the autoload finds the profile (0), the title offers
CONTINUE, which leads to the main menu, and Options' NAME CHANGE opens the keyboard holding DAMACY.

**Checks.** `tests/psp/run-tests.sh` (sanitized): 340 groups, none failing. `tests/allegrex/run-tests.sh`: 58
groups, none failing. `tests/psp/ares/run-tests.sh`: 307 checks, none failing.

**Left, and why:**
- The keyboard isn't drawn (the screen stays black the 18 frames it starts and the frame it runs), as with every
  dialog here.
- A save is its data file alone (no PARAM.SFO, icons or encryption), as before: the game reads it back.
- The kernel's player never cancels a keyboard, nor answers a field UNCHANGED: whether a PSP ever says UNCHANGED (of
  a second field, say) isn't known.

## Part 51: the library run again on the whole stack — three regressions, and what stopped games most

On branch `cursor/psp-hle-games10-2b67`, on top of #183's `cursor/psp-hle-games9-2b67` (the Vulkan renderer, the
speedups, the VFPU carry fix and Ridge Racer 2's keyboard under it). Sources: pspsdk's headers, pspautotests'
programs and recordings (each named below), and the games' own behaviour, traced and disassembled with a scratch
runner. No PPSSPP or JPCSP source was read.

**The run.** The owner's 266 games, as `docs/psp-compatibility.md`'s run went (tools/psp-runner/README.md, "The
report"): Software, 7 GE threads, 3600 frames, Start pressed at 120 and Cross at 1800, PNGs at 60, 300, 1200 and
3600, a scratch memory stick each. First on the stack's tip (`a2b48f8f8`, #183) as the library was copied in (a game
taken once its size held still across looks a minute apart: run 1), then part way through this branch (run 2) and at
its end (run 3). Each game's frames were judged by eye (menu, gameplay, movie, loading, black, hang) and compared row
for row with the report (`b27f084a5`); in run 3 a game whose frames matched run 2's kept run 2's judgement.
That report's runs pressed nothing (the runner's presses were broken then), so every game that looked worse was run
again without presses on both commits before calling it a regression.

**Regressions: two, both since scePsmf (#160, `18c58f968`).** Mega Man Maverick Hunter X and Juiced: Eliminator
skipped their movies while scePsmf was missing; now they play them through sceMpeg, and stalled (runners built at
`b27f084a5` and `18c58f968` showed where it began). Three things, each its own commit:
- *The ring past the movie's end* (`mpegReturned()`). Mega Man's ring callback reads 32 packets at a time from
  `rockx_pack.dat`, a file that goes on past the movie ("LINK..."), adds Put's answer times 2048 to a count, and
  starts the movie only when that count equals sceMpegQueryStreamSize's (0xB7800, 367 packets) or the ring is full
  (1600 packets: never). The count overshot, and the screen stayed black. Every 2048-byte packet of a PSMF movie
  starts with a pack's start code (00 00 01 ba: all 129 of pspautotests' `test.pmf`, all 74,871 of Ridge Racer 2's
  15 movies), so once a ring has had a pack, a packet without one is past the movie's end: Put stops before it, its
  callback isn't asked again, and the movie has ended. What comes before the first pack (a header, from a game that
  reads its file from the start) is taken, and a header coming round again after packs is the movie starting over
  when the feeding began with one (Space Invaders Extreme's callback goes back to its file's start). A ring with no
  library keeps no such state and takes everything. The state is two bits in the library's own memory (0x714), which
  sceMpegCreate zeroes and a flush clears: no save state change. Chosen, as no recording shows it.
- *Frame width 0* (`mpegFrameWidth()`). Juiced decodes with a frame width of 0, and its pictures went nowhere: 0 is
  now the width sceMpegCreate was given (pspmpeg.h: the display buffer's), kept at 0x710; sceMpegAvcDecodeStop's last
  picture too.
- *Sound past its end* (`sceMpegAtracDecode`). As each movie ends, Juiced's movie loop, its data fed and its ring
  empty, calls sceMpegAtracDecode with no new access unit to drain its sound buffers, and abandons the movie (then
  waits for good) at any error. Once a movie's sound has been decoded, a unit with nothing in it decodes to silence;
  before that, or with no buffer, it's 0x807f00fd, as video/mpeg/basic recorded for a movie with no sound.
Tests: "mpeg ring at the movie's end", "mpeg ring fed round and round", "mpeg decode at the library's width", "mpeg
sound past its end"; the random packs test keeps its packets that aren't packs and its checker now models the cut.

A third regression came from this branch itself: fix 1 below (a memory stick callback told the stick is in as
it's registered) left Patapon 2 deadlocked at boot in the second run; fix 17 put it right.

The other rows that rank lower than in the report weren't the code (docs/psp-compatibility.md goes through all 26):
games that now get the runner's presses go on from a menu or notice to a movie, a demo or a screen the old runs
never reached (each shows its menu again run without presses); some get further than before (past a "no Memory
Stick" notice, or into an intro movie scePsmfPlayer now plays); and some show the same at the report's commit, a
runner built there run without presses (black, loading, or the same intro frame the report judged a menu).

**What stopped games.** In the first run 58 ended black, 3 hung and 3 timed out; 24 games stopped at a missing
function (mostly one game each). The fixes, each its own commit with its tests, in the order they went in:
1. *A memory stick callback is told the stick is in as it's registered* (`sceIoDevctl` 0x02015804/0x02415821).
   pspautotests' `mstick` recorded the callback running at the next CheckCallback with count 1 and event 1 (inserted).
   50 Cent, Dirt 2, Crisis Core, Hexyz Force, Patapon 1 and 2, Parodius, Sonic Rivals 1 and 2 and Ys: The Ark of
   Napishtim learn of the stick only so, and said none was inserted, or that it was full.
2. *A file without PGD's header is read as it is once its key is given* (ioctl 0x04100001): 7th Dragon 2020's
   translations keep their data decrypted, yet set a key and retried for good on the refusal.
3. *A fixed pool's alignment of 0 is the default* (threads/fpl/create): Brothers in Arms: D-Day.
4. *Sony's static EBOOTs without sections find their module info* in the first program header (Ghostbusters,
   Dissidia 012, Dragon Ball Z: Tenkaichi Tag Team, Disaster Report 3, Kurohyou 2, Tekken 6, Monster Hunter Portable
   3rd HD).
5. *The program's first thread is made, and goes, as a module's start thread*: it starts at the module_start the
   program exports, with module_start_thread_parameter's priority, stack and attributes, and is deleted when it
   returns. Ghostbusters asks for a 1 KiB stack; Death Jr. and Ys Seven need the 256 KiB back ("mem tarine").
6. *sceUtilityLoadUsbModule/UnloadUsbModule*: ATV Offroad Fury Pro (black from boot at its assert).
7. *Writing save data needs the save to be there* (utility/savedata/saveemptyfilename: NO_DATA): Hot Shots Golf 2.
8. The three movie fixes above.
9. *sceRtc's day of the week, 64-bit time_t and when the clock was set*: sceRtcGetDayOfWeek by Zeller's congruence,
   which gives all twelve answers rtc/lookup recorded, impossible dates included (only January and February are
   counted as the year before's; 0 is Sunday, though psprtc.h says Monday); sceRtcGetTime64_t as rtc/convert
   recorded; sceRtcGetLastAdjustedTime and GetLastReincarnatedTime, both when the PSP started here. Juiced 2 (each
   frame), LittleBigPlanet, Kurohyou.
10. *sceKernelStopUnloadSelfModule* (MediEvil Resurrection's program, once it has started the game's module).
11. *sceIoAssign* (Metal Gear Acid, Ridge Racer: disc0: from umd0: through isofs0:, which it already is).
12. *sceImposeSetUMDPopup* (every Patapon), *sceUsbStart/Stop/Activate/Deactivate* (OutRun 2006),
    *sceDisplayIsForeground* (display/isstate: 1 with a frame buffer; Power Stone Collection),
    *sceKernelReferSystemStatus* (Dante's Inferno), *sceUtilitySetSystemParamInt* (Colin McRae Rally 2005), each
    its own commit.
13. *The savedata sizes mode reads no names of its own*: the save it measures is msData's. Valhalla Knights 2 asks
    with empty names, and took the refusal for a memory stick too full to save on.
14. *A program that's a PRX goes 16 KiB into user memory*, at 0x08804000: pspautotests' recordings of PRXs have
    their code there (cpu/cpu_branch's 0x420 bytes in at 0x08804420, video/pmf's ring callback at 0x088042b4) and
    their data (audio/mp3/stream's buffer at 0x0882ba40). The program's block still starts at the partition's
    start, the 16 KiB below held with it: sysmem/partition found the lowest free place with room for 1 MiB. Fan
    translations call code they added by its address: Persona 2: Eternal Punishment's start-up calls 0x08c47c84
    with no relocation, which held its data when the program sat at 0x08800000. With it, Persona 2, both Tales of
    Phantasia translations, Fate/Extra's perfect patch and Growlanser IV no longer crash at boot, Armored Core:
    Silent Line Portable's True Analogs patch no longer stops in a game thread, and Formula Front's no longer loads
    for ever at 18 frames a second (a runner built just before the change shows both).
15. *A ring may have more than 4096 packets*, when its memory fits (`sceMpegRingbufferConstruct`). Sega Rally Revo
    makes a ring of 4800 packets with their memory and asserts when that's refused. video/mpeg/ringbuffer/construct
    gave every count it tried 4096 packets' memory (0x868000): 4097 was refused, but -1, 0x7fffffff and 0x80000000
    were taken. Every line it recorded fits one rule: refused when the packets' memory, as
    sceMpegRingbufferQueryMemSize reckons it in 32 bits, is more than the size, both taken as signed. Put,
    sceMpegGetAvcAu and sceMpegGetAtracAu now take a ring of up to `RingMostPackets` (0x8000, 64 MiB of data, more
    than any ring can be) where they took 4096, and so do the save state's checks. Sega Rally Revo now shows its
    autosave notice and its title.
16. *sceMpegAvcDecodeDetail gives the decoder's newest picture's width and height* at 8 and 12 of the details.
    Spectral Souls (which fix 1 let past its "no Memory Stick" prompt, to its movies) checks those two words after
    each decode and asks again while they're 0; its movie code (0x08840c58) draws the picture from (1, 1) to
    (width - 1, height - 1) by them. Left at 0, a few texels of the picture's corner were stretched over the screen,
    the same on both renderers, and its intro played as coloured gradients. Now the intro plays and the main menu
    follows. The rest of the details are left as they were (nothing of them is recorded), and with no picture held
    (before the first, after a flush) nothing is written. Space Invaders Extreme, which also asks after each picture,
    is unchanged.
17. *A semaphore's CB wait runs its notified callbacks first, when it has no timeout and nobody else is in line*
    (`waitSemaphore()`). Fix 1 broke Patapon 2: its main thread registers a memory stick callback, then CB-waits on
    a semaphore that is free, with no callback point between, and the callback, notified as it was registered,
    first waits on that same semaphore. Taking the count and then running the callback (as every wait that ends at
    once does) left the callback waiting on its own thread for good. Now such a wait runs its callbacks at once, in
    its wait (no other thread runs first: threads/callbacks/nested has a CB wait that ends at once not yield to one
    of the same priority), and takes the count after them if it's still there, ahead of any thread that queued
    meanwhile; a callback deleting the semaphore ends the wait as deleted. With a timeout, or with another thread in
    line (waiting, woken for its callbacks, or its wait set aside while they run: served in order, one wanting more
    could hold this one up), the count is taken first as before. Chosen: no recording shows the order
    (threads/callbacks/afterwait's callback never looks at the semaphore), and the game shows this one. Patapon 2
    reaches its save prompt.
18. *A ring made for a game's own copy of the library is pspsdk's 44 bytes* (`sceMpegRingbufferConstruct`). With the
    firmware's library, construct records a ring of 48, the caller's global pointer last. Miami Vice loads its own
    MPEG.PRX (stood in for) and keeps its movie's file descriptor in the word after a 44-byte ring: written over with
    the global pointer (0x08b98530), every read of the movie failed (0x80020323, a bad descriptor) and the game
    retried for good on a black screen ("READ FAILED. RETRY..."). Nothing but the library would read that word, and
    the ring's callback already runs with Put's caller's global pointer, so the word is left alone with a game's own
    library. Miami Vice now plays its intro movies and reaches its title; Crash Tag Team Racing, which brings its own
    library too, no longer stops after its legal screen (a runner without this fix still crashes there).
19. *sceUtilityLoadModule takes the network modules 0x107 and 0x108.* pspsdk's list stops at 0x106; Macross:
    Triangle Frontier loads 0x108 at boot and, refused, waits six frames and tries again, for good, on a black
    screen. 0x107 is taken with it (a group's modules are numbered without gaps; 0x308 was taken on the same kind of
    evidence in part 37, for Ace Combat: Joint Assault), 0x109 is refused. Macross now shows its logo, its autosave
    notice and its next prompt.
20. *A movie has ended once a Put has given its whole stream* (`sceMpegQueryStreamOffset`, `mpegFinish()`). The
    last access unit comes only once the library knows the movie has ended; it learnt that only from a feeding that
    came up short. Games black after their logos ended in their movie loops, sceMpegGetAvcAu and GetAtracAu giving
    "no data" for good, the ring never quite free: Disney-Pixar Cars: Race-O-Rama gives its whole 561-packet movie
    in one Put (its callback reads it out of a container file), Pursuit Force: Extreme Justice asks for just what's
    left and then for nothing, MX vs. ATV Untamed the same. Sony's movies end with no program end code (Ridge Racer
    2's twelve and pspautotests' test.pmf: padding after the last PES packet), and each game reads its movie's
    header with sceMpegQueryStreamOffset, its handle given, right after making the library. So the library keeps the
    stream's size in packets from that header, counts the packs Put gives (a flush, or the header coming round
    again, starts the count over; a flush keeps the size), and the movie has ended when a Put stops having given
    exactly that many, as when a feeding comes up short (a feeding that goes on past them is the movie going round
    again, its access units whole). Both words are in the library's own memory, cleared by sceMpegCreate: no new
    save state. Cars now reaches its racing attract scene, Pursuit Force and MX vs. ATV Untamed their titles, and
    Blitz: Overtime, Death Jr. and its sequel, Disaster Report 3 and Dead Head Fred get past their movies to their
    titles or menus (a runner without this fix shows them as before).

**After.** Run 3, against the report and run 1 (gameplay counted as menu and a hang as black, as the report did):

| | Report (`b27f084a5`) | Run 1 (#183) | Run 3 |
|---|---|---|---|
| Menu or gameplay | 141 | 148 | 175 |
| Movie | 35 | 43 | 63 |
| Stuck loading | 13 | 8 | 5 |
| Black or hang | 73 | 64 | 21 |
| Timed out | 4 | 3 | 2 |

81 games are further along than in the report; 14 stop at a function not implemented yet, where 77 did. Of the
report's 22 games that had gone from a menu to black, 18 show something now; Black Wolves Saga (PGD-encrypted install
data, which the kernel can't decrypt yet), The King of Fighters - Orochi Saga (the sceCcc library), Melodie (a 40
MiB block of memory) and Ridge Racer (black frames after its opening movie, cause not found) are still black.

**Checks.** `tests/psp/run-tests.sh` (sanitized): 362 groups, none failing. `tests/allegrex/run-tests.sh`: 58 groups,
none failing. `tests/psp/ares/run-tests.sh`: 307 checks, none failing. Each batch reviewed independently, and what
the reviews found was fixed: among it a looping movie's header, the fuzz checker and the silence path's buffer (the
movies); the ring-size rule's signed comparison and the big rings' tests; a thread with its semaphore wait set aside
in its callbacks counted as in line, the callbacks run in place and the wait kept first in line (fix 17); the end of
a stream counted exactly, so a movie fed round again keeps whole access units, and its size kept across a flush
(fix 20). The save states' version stays 17.

**Left, and why.** The libraries the kernel doesn't have yet: sceCcc (The King of Fighters - Orochi Saga, Genso
Suikoden; pspautotests' ccc/convertstring records its conversions), thread-local storage pools (God Eater 2),
scePauth and sceJpeg (Monster Hunter Portable 3rd), scesupPreAcc (Persona 3 Portable's loading screen). PGD
decryption (Black Wolves Saga). sceKernelExtendThreadStack (Dragon Ball Z: Tenkaichi Tag Team) is written and
reviewed on a side branch, every line of threads/threads/extend matched, but not here: the game stays black with it,
and the review found two things to fix first (a program unloading itself from a lent call loses module_stop's
argument; a state of an older layout loads with no trampoline for its return). Black screens with the game running
and no missing function: Ridge Racer, Crush, Dead or Alive - Paradise, Def Jam - Fight for NY, Jak and Daxter - The
Lost Frontier, Naruto Shippuden Ultimate Ninja Impact, Need for Speed - Most Wanted 5-1-0, Shining Blade, Tekken -
Dark Resurrection, Valhalla Knights, Valkyria Chronicles III, PaRappa the Rapper, Monster Hunter Portable 3rd HD.
Need for Speed - ProStreet crashes after its notice. Both God of War games are too slow to reach frame 3600 in 1200
s. Vulkan draws what the software renderer draws in all 11 gameplay games, but 3D gameplay is much slower on it
(docs/psp-compatibility.md, "Vulkan spot checks").

## Part 52: the Vulkan renderer faster — its own waste, found with a profile

On branch `cursor/psp-gpu-fast-2b67`, on top of #184's `cursor/psp-vk-play-2b67` (80bfd9807). docs/psp-gpu-renderers.md
has the detail ("Submitting", "Render to texture", "The texture cache", "Speed"). Original code: nothing of PPSSPP's
or JPCSP's used, copied or translated. Software stays the default; its pictures and state are unchanged, and Vulkan
(accurate)'s pictures are byte for byte what they were.

**Where the time went.** simpleperf on the RP6 (call graphs, Turnip opened by the runner as the app opens it), the
race in Midnight Club 3: the emulation thread 26.4 ms a frame with Vulkan, 17.8 with seven software threads, which
spend about 7 ms of theirs on the GE's setup as Vulkan does. Vulkan's extra:
- `GPU::own()` told memory of every PRIM's rectangle (`Memory::changed()`, the recompiler's invalidation over all
  four of VRAM's copies); a 3D PRIM's rectangle is the whole scissor: 2.7 ms a frame.
- The bloom sampled the 480x272 frame buffer as a 512-row texture whose last rows are two other frame buffers'.
  `holds()` was given the vertices' rough reach (all 512 rows) and refused it, so the GPU finished every frame: a
  2.7 ms wait, the read-back narrowed into memory, the targets filled again, the 1 MB texture decoded and uploaded.
- 25 small swizzled textures in RAM were decoded again every frame (8,249 in 300 frames), the GPU making and
  destroying an image for each: about 2.6 ms. Their pages hold other data the game writes each frame.
- Pipelines made in the run's first frames (45 in the race): 2.5 ms a frame over 300 frames, once a session.
- Recording, about 1.3 ms. WipEout Pure ends 209 short lists a frame, each a `vkQueueSubmit` and a wait for the
  slot three runs back.

**What was done:**
1. `own()` tells memory only of a rectangle not inside one told since the target's last finish, its pages still
   busy (`Target::told`): no one can copy bytes the GPU drew over without a finish, which clears it.
2. A 2D sprite PRIM's texture rows and columns for the GPU are `spriteReach()`'s, exact, a filter's second texel at
   weight 0 left out; `draw.frag` holds every texel fetch inside the picture on the GPU. The bloom's copies are taken
   on the GPU: finishes 300 → 5, read-backs 1,200 → 20, uploads 1,249 → 51 in 300 frames.
3. Decoded textures keep the bytes they came from (where side by side and no larger than their texels); a write to
   their pages marks them, and before the next draw their bytes are compared: kept if the same, else decoded afresh.
   Both renderers: the race's textures 8,249 → 520.
4. A list's end hands the recording to the GPU only with 32 commands or more; frames shown and finishes always do.
5. The draw state isn't compared with the last recorded one at every primitive when nothing changed.
6. "Late Frames" (System's option, the runner's `--late-frames`): the shown frame copied on the GPU into the slot's
   shot buffer and taken when its run is done, a few frames late, so nothing waits each frame, as when the app
   presents. The runner needs it to time the GPU as the app runs: read back synchronously each frame from adb's shell,
   whose threads can't be pinned to the prime core as the app's are, the waits had the scheduler move the emulation
   thread to slower cores at lower clocks (seen: cpu 3-6 at 1.65-2.8 GHz instead of cpu 7 at 3.19).

**Measured** on the RP6, medians of three runs (the table in docs/psp-gpu-renderers.md, "Speed"), late frames, frames
a second: Turnip's race 34.5 → 52.6, menu 49.7 → 68.0, LCS city 63.3 → 76.3, woods 75.1 → 79.0, Peace Walker's
tutorial 73.9 → 105.8, WipEout 18.9 → 36.2; Qualcomm's driver 33.1 → 46.9, 46.3 → 64.0, 59.9 → 75.7, 73.9 → 94.7,
68.0 → 93.3, 21.0 → 60.9. Seven software threads: 53.2, 69.2, 71.8, 70.9, 98.1, 16.2. So Vulkan (accurate) is level
with seven threads in the 3D scenes and well ahead in WipEout and the 2D ones. Read back each frame, Turnip's LCS
scenes are slower than before (the waits above); that's the desktop's way, not the app's.

**Exact:** Software's every frame (VRAM and picture) and its end state (RAM and the whole machine) identical before
and after, all seven scenes at 1 and 7 threads, 300 frames each; Vulkan (accurate)'s pictures byte for byte the same,
all seven scenes, 10 frames each (M1).

**Tests:** "draw textures kept decoded, their pages written" (new: kept where the bytes are the same, decoded afresh
where they aren't, watched again); "gpu render to texture from a frame buffer taller than its picture, nothing
finished" (new: the bloom's case, its last row at weight 0; late frames' shot; a short list not submitted at its
end); "gpu lost" (its pretend backend marks itself lost, as Vulkan's does). `tests/psp/run-tests.sh` (sanitized) 345
groups, none failing; `tests/allegrex/run-tests.sh` 58; `tests/psp/ares/run-tests.sh` 307 checks; the GPU tests on
the RP6, Turnip and Qualcomm's driver, none failing.

**Review** (an independent read-only one): no change to a picture; fixed as it found: `release()` at every frame
handed over (short lists no longer submit), a lost GPU reported whichever path finds it first, source copies no
larger than their texels, a written texture with more rows than the PRIM needs decoded afresh, `own()` telling again
where the drawing threads' settle freed its pages, `GPU::drop()` dropping the last shot, `shoot()` false when the GPU
stops, comments. Not taken: copies' sizes rounded to eights (it changed the render-to-texture test's pixels, which
isn't understood yet); a test of the fetch's hold above 1x and of `System::late()`.

**Left:** block transfers on the GPU (WipEout's two finishes a frame), a pipeline cache kept on disk, the CPU's
transform (part 53: Vulkan (fast)).

## Part 53: Vulkan (fast) — the GPU transforms 3D, and blends with its own units

On branch `cursor/psp-gpu-fast2-2b67`, on top of part 52's `cursor/psp-gpu-fast-2b67`. docs/psp-gpu-renderers.md's
"Vulkan (fast) (part 53)" is the design. PPSSPP's hardware transform informed it; none of its code is used, copied or
translated (JPCSP's not read). Software stays the default and exact; Vulkan (accurate) is today's path, its pictures
as they were.

**The choice** (the owner's, 2026-10-09): "PSP Renderer" is now "Software (exact)", "Vulkan (accurate)" or "Vulkan
(fast)", each described in Settings as it's chosen; the desktop's menu likewise; the core's "Renderer" option takes
"Vulkan (fast)"; `psp-runner --renderer Vulkan-fast`. The saved value 1 is Vulkan (accurate), as Vulkan was; 2 is
fast. A start-up check that fails, or a device lost, falls back to Software and says so, in fast mode as in accurate.

**What fast mode does** (the design has the detail): 3D PRIMs of 16 vertices or more are handed to the GPU as the
vertex type laid them out; `transform.vert` skins, transforms, lights, fogs and maps them as `transform.cpp` and
`lighting.cpp` would (the lighting in the GE's own whole numbers) and cuts their positions to the GE's sixteenths.
The GE still decides which triangles are drawn (`GE::meshTriangles()`): from each corner's clip position it drops
what it wouldn't draw (out of sight, behind, no area, facing away) and cuts and draws the triangles reaching past the
near plane itself, in their place among the others; the GPU gets the rest. Each PRIM's settings are a block of the
run's storage buffer that its vertices name, so PRIMs drawn alike are one draw. Nothing is read in the shader: the
GPU blends. Everything else is as Vulkan (accurate).

**How it got fast** (the RP6, Turnip, the runner from adb's shell, late frames, frames a second after a 150-frame
warm-up unless said):
1. The first version (a uniform block per PRIM, skinning, lighting and mapping as pipeline constants) was slower
   than the accurate mode: GTA's city 52 against 81. Pipelines: Peace Walker's combinations of bones and lights made
   dozens more (5 ms a frame of compiling over 300 frames). Draws: each PRIM's block split them, 1,162 a frame against
   604. And the waits at a finish grew from 0.67 to 3.96 ms a frame: the GPU had far more queued.
2. Skinning, lighting and mapping read from the block, not constants: the pipelines as few as accurate's, about.
3. Eight slots instead of three: fast mode makes more commands, and with three its emulation thread kept waiting a
   moment for the run three back; sleeping often, the scheduler moved it to slower cores (seen: cpu 3-6 at 1.65-2.8
   GHz, not cpu 7 at 3.19). MC3's race 59.8 → 79.6. (A 512-command submit bound tried with it cost MC3's menu, which
   finishes every frame: back to 128.)
4. The blocks in a storage buffer, each vertex naming its own: draws as few as accurate's. GTA's city 68 → 102.
5. PRIMs under 16 vertices left to the CPU: Peace Walker's 5,000 small skinned PRIMs a frame cost more on the GPU's
   way (64 against accurate's 107); left to the CPU, 128.
6. The review's near plane (below): the GE deciding which triangles are drawn cost the race 1.3 ms a frame of the
   CPU's (74.0 → 65.4); the world, view and projection matrices made one for it, 67.1. The GPU now gets the triangles
   the accurate mode draws and no more (the menu's 23.7 million in 450 frames → 6.7), which shortened Qualcomm's
   driver's waits (the menu 3.8 → 2.8 ms a frame).

**Found on the way**: MoltenVK refused every transform pipeline (a helper named `signbit` clashed with Metal's own),
and the 3D drew nothing: fast mode now asks for a pipeline before it records a PRIM for it (`Backend::drawable()`),
leaving the PRIM to the GE where the driver won't make it, and the start-up check draws 3D through the GPU's
transform. GTA's cars lost their gloss (a second pass with a depth test of equal: the two pipelines put the vertices
a hair apart): `gl_Position` is invariant now.

**Measured** on the RP6 (docs/psp-gpu-renderers.md, "Speed", has the table): medians of three runs of 450 frames,
late frames, frames a second after a 150-frame warm-up. Turnip, accurate / fast: the race 62.3 / 67.1, the menu
75.1 / 77.4, LCS's city 92.3 / 105.1, the woods 93.8 / 126.5, Peace Walker's play 121.5 / 125.0, WipEout 39.6 / 79.7;
seven software threads 53.8, 67.0, 70.1, 71.3, 103.1, 17.1. Qualcomm's own driver, accurate / fast: 59.1 / 63.2,
73.4 / 70.9, 87.4 / 96.5, 110.8 / 99.5, 114.8 / 113.7, 75.5 / 76.5. So on Turnip, the app's driver, fast mode is
16-77% ahead of seven threads in the 3D scenes (WipEout 4.7 times) once its pipelines are made; over a whole run
from a state the first frames' pipelines take most of that back (the race 49.2 against 52.5), until they're kept
between sessions (part 54). On Qualcomm's driver its GPU is slower with fast mode's vertex shader and it's within
10% of the accurate mode either way, 6-40% ahead of seven threads.

**Accuracy** (docs/psp-gpu-renderers.md, "Accuracy"): pixels identical to the software renderer's, 10 frames of each
scene, accurate / fast: Lumines 96.6 / 72.9%, Peace Walker's title 100.0 / 40.4%, MC3's menu 97.0 / 88.0%, the race
95.0 / 71.5%, LCS's city 95.4 / 57.8%, the woods 91.5 / 52.4%, Peace Walker's play 99.2 / 55.0%; fast mode's
differences a level or two (colors and fog interpolated by the GPU) but for Peace Walker's 16-bit blends, a step of a
5-bit channel spread evenly as a different dither. Nothing missing or misplaced to the eye. Vulkan (accurate)'s
pictures are as they were: the GPU tests' every count the same from the base and from this branch (the review's), and
the RP6's scenes the same to the channel.

**In the app** (the RP6, Turnip, the release build, Vulkan (fast), the app's stats: frames a second, capped at 60,
and the emulation's ms a frame): Midnight Club 3's title 60.0 (16 ms) and a quick race 51-53 (19 ms; part 41 had
the accurate path at 39.0 and 25.6 ms in a quick race); Liberty City Stories' 3D city 60.0 (11-13 ms); Vice City
Stories' opening 60.0 (13-16 ms); Burnout Legends' car select and a race 60.0 (11-13 ms); WipEout's menus 60.0 (4-5
ms; at its profile screen it stopped taking adb's presses, so its race wasn't reached in the app); Peace Walker's
title 60.0 (5 ms); MotorStorm: Arctic Edge, black after its first screen in part 41, through its notice to its
title scene, 60.0 (9 ms); Ridge Racer 2's loading game and title 60.0 (5 ms). Each said "the Vulkan renderer draws
(fast, 3D transformed by the GPU), on Vulkan: Turnip Adreno (TM) 740"; none fell back, and nothing looked wrong.

**Review** (an independent read-only one, then a second over the fixes). Found and fixed:
- The start-up check's 3D never reached the GPU's transform (PRIMs of three corners, under `MeshLeast`), so a
  driver drawing `transform.vert` wrong would have passed it, and `gpuFast` failed: four PRIMs of six now, and the
  check fails where none reached it. Where its 3D alone fails, the check runs again with the GE transforming.
- Triangles cut by the near plane had their colors and fog blended across the screen by the GPU's clipper, not in
  clip space as the GE does (a ground mesh under the camera 67.6% the same, its fog 76.0%): the GE cuts and draws
  them now, in their place (`GE::meshTriangles()`). And the GPU drew triangles the GE drops for their depths (wholly
  past the far plane with DEPTH_CLIP_ENABLE on, any corner outside with it off): the GE's rules decide every
  triangle now, its culling with them. The reviewer's scenes, strips, fans, culling, flat shading, lights, texture
  modes, fog, skinning, the near and far planes, give the accurate mode's numbers exactly.
- `transform.vert` declared clip distances, which needs a device feature, and its module was made on every device,
  in both modes: no clip distance now, the module made in fast mode alone.
- 3D clear mode went through the GPU's transform (each triangle's stencil is its own last corner's): left to the GE.
- The filter was chosen at each pixel from perspective-correct coordinates: once a triangle now, from coordinates
  straight across the screen, the GE's ratio (the reviewer's filter scenes: 54 pixels far off → none).
- Eight slots let a GPU-bound game show frames up to eight runs late: two frames in flight at most.
- A GPU of the other mode was kept across a power cycle; the descriptor sets weren't bound again after a copy's
  pipeline layout (both modes; drivers kept them anyway); stale text ("three slots"); layout asserts.
The second review, over those fixes, found: the frames-in-flight wait named slots, not runs, so with five runs a
frame or more it waited for one of the frame's own (by serial now); with DEPTH_CLIP_ENABLE on the GPU clamped the
depth at each pixel where the GE holds each corner's (a triangle across the far plane, then a quad drawn with a test
of less: 210 pixels far off, none now; held at each corner in `transform.vert`); a corner behind the camera that
isn't past the near plane, which the GE projects through its w, went to the GPU (the GE's now); a mode switched
without unloading kept Android's window from the new GPU; `statistics.meshes` counted runs. Its scenes (overlapping
triangles cut and whole in turn, a strip cut mid-way, flat shading, a narrow depth range, a flush between a PRIM's
runs, random triangles from behind the camera to past the far plane, off the 4096-pixel screen, culled each way)
give the accurate mode's numbers exactly. Not taken: a second pass with a depth test of equal over a model drawn as
PRIMs of other sizes meets the GE's depths and the GPU's a float's last bit apart (181 of 3,448 pixels missed in a
scene made to show it; `MeshLeast` splits the paths; games' passes redraw the same PRIMs, which meet exactly).

**Tests:** "gpu fast mode: 3D transformed by the GPU, near the software renderer" (the start-up check with its 3D
through the GPU's transform, at least four PRIMs taken; pspsdk's samples, cube, celshading and envmap through it,
held to the same 3 channels in 1000 as the accurate mode); "gpu fast mode: the GE's rules for which triangles are
drawn" (random triangles from behind the camera to past the far plane, off the screen, culled each way, lists and
strips, and a fogged ground of random colors under the camera, DEPTH_CLIP_ENABLE on and off: no more pixels far off
than the accurate mode, none on the M1 and the RP6); "gpu start-up check" and the rest as before. The app's
`PspVideoTest.rendererChoicesKeepTheSavedValues` (0, 1 and 2 kept, anything else Software, the labels).
`tests/psp/run-tests.sh` (sanitized), `tests/allegrex/run-tests.sh`, `tests/psp/ares/run-tests.sh`: none failing;
the GPU tests on the RP6, Turnip and Qualcomm's own driver: none failing.

**Left:** a pipeline cache kept between sessions (part 54); the bones in a block of their own, so that small skinned
PRIMs (Peace Walker's) could go to the GPU too and every 3D PRIM take one path; Qualcomm's driver's GPU time in fast
mode; textures decoded on the GPU (palettes, swizzling, DXT: 0.4 ms a frame of the race's CPU since part 52, so
later); block transfers on the GPU (WipEout's two finishes a frame).

## Part 54: the Vulkan renderer's pipelines kept between sessions

On branch `cursor/psp-gpu-fast3-2b67`, on top of part 53's `cursor/psp-gpu-fast2-2b67`. docs/psp-gpu-renderers.md
has the design (the pipelines' paragraph) and the table ("Speed", part 54). Software is untouched; neither Vulkan
mode draws differently.

**Why.** Part 53's numbers after a warm-up and over a whole run from a state differ by the pipelines a game makes in
its first frames: fast mode's 79 in Midnight Club 3's race took Turnip 2.6 seconds to make (its transform pipelines
cost 33 ms each, the accurate mode's 14), so over the 450 frames the race ran at 49.2 frames a second, behind seven
software threads' 52.5, and in the app the race's start stalled for 725 ms. Pipelines were made afresh every session.

**What was done:**
- System's option "Pipeline Cache", a host file: read at the renderer's start and handed to `GPU::vulkan()`, which
  gives it to the driver as its pipeline cache's first data; written by `System::keepPipelines()` where pipelines
  were made since it was last written (as a game ends, every 600 frames, as the renderer is let go of), beside the
  file and renamed over it. The app's file is `psp_vulkan_pipeline_cache.bin` in its Vulkan cache folder, beside the
  N64's (or the saves folder); the runner's `--pipeline-cache FILE` the same; its summary says how many pipelines were
  made and the ms the driver took.
- The driver's data goes in the file after a header of our own (`Kept`: the device, the driver's version and cache
  UUID, a 64-bit or 32-bit build, the data's size and CRC-32), and reaches the next session's driver only where all
  of it matches and the driver's own header is sound: the review found MoltenVK taking a cache with an intact header
  and a zeroed tail (a power cut after the rename could leave one) and then failing the start-up check every session,
  and a cut-short one leaving the session's cache unreadable for good. A renderer made with a kept cache that fails
  its check is made again without it, the file let go of; a driver refusing the data gets an empty cache; the same
  size again isn't written again; past 32 MB the file is let go of; a folder that isn't there is made, and a file that
  can't be written is said once, not tried every 600 frames.

**Measured** on the RP6: each scene twice from its state with `--pipeline-cache`, the first from an empty cache, the
second from what the first kept; the driver's ms making the pipelines, and frames a second over the 450 frames.
Turnip, fast: the race 2,589 → 1.2 ms, 49.2 → 67.1; LCS's city 1,074 → 0.6 ms, 85.5 → 103.6; Peace Walker's play
790 → 0.6 ms, 105.2 → 123.8; WipEout 2,559 → 1.2 ms, 55.4 → 78.6. Qualcomm's driver, fast: 2,587 → 23 ms, 47.8 →
63.9; 1,258 → 13 ms, 78.8 → 95.7; 1,106 → 12 ms, 93.1 → 113.0; 2,423 → 24 ms, 55.1 → 75.6. The accurate mode gains
likewise (Turnip's race 872 → 0.8 ms, 54.3 → 60.2). From a game's second session on, fast mode is ahead of seven
software threads over a whole run too: the race 67.1 against 52.5, the city 103.6 against 70.4. The files: 0.15-1.3
MB a scene. In the app (Turnip, fast): the file written at the 600-frame mark, 198 KB, then 1.4 MB once
Midnight Club 3's race had been run; that race's start, its longest frames 619, 335 and 226 ms in a first session,
30 ms at most in the second (the file a build before `Kept` wrote was left out at the first, as another format's,
and written again).

**Review** (an independent read-only one): found the damaged-cache cases above (fixed: `Kept`, the retry without
the cache), the app's cache folder possibly not made on an install whose setting was never changed (made now), a
refused cache leaving the session without one (an empty one now), the driver's version not checked (in `Kept`),
the write's close not checked and a failed write's `.new` left (both fixed), every warm session rewriting the same
file (not now), an unbounded file (32 MB), the read byte by byte without a bound (sized now), and the test not
showing bad data left out. Not taken: writing on another thread (the write happens only where pipelines were made,
which stalls anyway: 0.4-1.5 ms on the M1 for 44 KB).

**Tests:** "gpu pipelines kept between sessions" (a renderer made with the data from one whose check ran takes it,
its cache as big from the start, and passes its check; another driver version's, another cache UUID's, a file cut
short, one with a zeroed tail, and junk are left out, each renderer's cache starting without them, and draw right).
`tests/psp/run-tests.sh` (sanitized), `tests/allegrex/run-tests.sh`, `tests/psp/ares/run-tests.sh`: none failing;
the GPU tests on the RP6, Turnip and Qualcomm's own driver: none failing.

**Left:** a file per game (one file holds every game's pipelines until 32 MB); fast mode's first session still pays
for its pipelines (made on another thread while the GE draws those PRIMs, as PPSSPP does, would hide it); the app's
Vulkan cache folder moved in Settings carries the N64's file over, not the PSP's (one session made afresh).

## Part 55: character conversion, thread-local storage, lent stacks, and what keeps the black games black

On branch `cursor/psp-hle-games11-2b67`, on top of #188's `cursor/psp-gpu-fast3-2b67` (#187, #185, #184 and #186's re-run
under it). Sources:
pspautotests' programs and their recordings (each named below; the programs themselves were also run through the kernel
by a scratch runner answering their emulator devctl, their output compared with the recordings line for line), pspsdk's
headers, the PSP Developer Wiki (through the Wayback Machine) and the games' own behaviour, traced with a scratch
runner. No PPSSPP or JPCSP source was read.

**1. sceCcc, character code conversion** (`ccc.cpp`). The King of Fighters - Orochi Saga's first call is
sceCccDecodeUTF8 on "Loading...", in a loop that ends only as the pointer reaches the string's end: refused, the pointer
never moved, and the game spun there for good (4.6 million calls in its first 30 frames, 9 frames a second, black). The
whole library is here now, as ccc/convertstring recorded it for all six conversions (UTF-8, UTF-16 and Shift-JIS, each
way): a conversion returns how many characters it wrote, not counting the terminator; the destination's size is in
bytes, and a character is written only while it fits with room for the terminator after it (a UTF-16 destination of 1
byte gets nothing, of 2 just the terminator, of 4 one character); no destination converts nothing; input that isn't
valid converts to nothing. Shift-JIS goes through the JIS tables the program gives (sceCccSetTable: the library has none
of its own, "crashes without table set"), indexed by JIS code, a Shift-JIS pair turned into its JIS code the standard
way. Decoding and encoding one character move the program's pointer past it; the error characters (sceCccSetErrorChar*)
stand for what can't be decoded or encoded, and are 0 until set, which ends the string (chosen: it's what makes the
recording's invalid input come out empty; no recording shows their first values). The tables' addresses and the error
characters are kept in states (layout 18). The convertstring program itself prints exactly what the PSP printed, with
the test's own tables.

**2. Thread-local storage pools** (`tls.cpp`): sceKernelCreateTlspl, DeleteTlspl, FreeTlspl, ReferTlsplStatus, the
system call _sceKernelAllocateTlspl and Kernel_Library's sceKernelGetTlsAddr. God Eater 2 makes its pools at boot,
asserted on the refusal, and stopped. As threads/tls's ten programs recorded: a pool of blocks of one size, each thread
holding one at most and getting the same one when it asks again; blocks handed out in turn from the one after the last
handed out (0, 1, 2 and 0 again), not the lowest; zeroed as they're handed out and as they're freed; a thread's blocks
back as it ends; waiters served in order or by priority (0x100), a timeout as every wait's; deleting refused while
another thread holds a block (0x800201d2); 16 pools at most (0x800201d1); creation's checks in create's order, the
memory a pool takes (blocks rounded up to their alignment, 4 bytes at least, and nothing more), partitions 2, 5 and 6
taken. Given an ID that isn't a pool's, sceKernelGetTlsAddr answers by its bits 3 to 6, as the index of a pool in which
the thread holds a block (0 and 1 gave the first pool's block, 0xf the second's, a deleted pool's ID nothing): the
library looks in the thread's own list before it calls the kernel. So a pool's ID carries its index there: the next ID
up that does, those passed over never handed out. From an interrupt handler sceKernelGetTlsAddr gives NULL (intr/waits).
Chosen: partition 5's memory comes from the user partition (the kernel hands out no other), and a handler has no thread
to hold, free or delete a block for. Pools, holders and threads waiting for a block are kept in states (layout 19),
checked as they load. Nine of the ten programs print what the PSP printed; the tenth (delete) interleaves two threads'
lines differently, the PSP's slower output through PSPLink letting a thread's delay run out first. threadmanidlist's
pool line matches now too.

**3. sceKernelTryLockLwMutex_600** (Kernel_Library 0x37431849), God Eater 2's other call. As threads/lwmutex/try600
recorded, it locks in the work area alone, as the user-mode library does (a work area made by hand locks too): a count
under 1 is ILLEGAL_COUNT, and so is a count but 1 for a free mutex that isn't recursive; its holder trying again,
LWMUTEX_RECURSIVE for one that isn't, LWMUTEX_LOCK_OVERFLOW past 2^31 - 1 for one that is; held by another,
LWMUTEX_LOCKED; a deleted one, LWMUTEX_NOTFOUND, which sceKernelDeleteLwMutex now marks by writing -1 to the work area's
ID (it wrote 0, which a hand-made one has). Every line that turns on the new function matches; the two that differ come
from the older sceKernelLockLwMutex refusing a hand-made work area (part 32's known difference).

**4. sceKernelExtendThreadStack**, part 51's (written and reviewed on a side branch), with that review's two findings
fixed. A function runs on the calling thread with a stack lent for the call, its result returned, as
threads/threads/extend recorded line for line. A program unloading itself from inside such a call
(sceKernelSelfStopUnloadModule) gives the lent stack back as its thread ends, and module_stop's thread could be given
that very memory for its own stack, its argument overwritten before it was copied: the argument is kept first ("modules
unload themselves from a lent stack"; without the fix module_stop saw 0). The trampoline, the kernel's own code the lent
call returns to, is now written as every state loads as well as at power on, so a state saved before that return existed
has it (the extend test wipes it before loading). The lent stacks are kept in states (layout 20).

**Save states.** Version 20; layouts 15 to 19 load, what's new left empty. A committed test can't hold an older-layout
state (megabytes of memory), so it was checked end to end: states the base commit's runner saved (version 17) of Lumines
at frame 600 and Patapon 2 at 1200 load into this branch's runner, and 60 to 600 frames on its pictures are byte for
byte those of the base runner carrying on from the same state.

**The games** (the report's runs: Software, 7 GE threads, 3600 frames, Start at 120 and Cross at 1800, PNGs at 60, 300,
1200 and 3600; the base commit's runner and this branch's on the same 53 games, and a frame every 300 where a judgement
needed it):
- *The King of Fighters - Orochi Saga*: black to its autosave notice (menu): "Loading...", the SNK Playmore logo, its
  intro, then the notice; 9 frames a second to 390-530.
- *God Eater 2* (English v2.0, RedArtz): black to a notice: the fan translation's "your DLC file is corrupted or
  missing" (it opens `ms0:/PSP/GAME/NPJH50832/SYSTEM_UPDATE.EDAT`, which the translation's patch puts on the memory
  stick and the owner's copy doesn't have).
- *Dragon Ball Z: Tenkaichi Tag Team*: black from boot to its autosave notice at frames 300 and 1200; past it (Cross at
  1800) black again, opening a PGD-encrypted file over and over (below). It now calls scePower 0xa85880d0, missing (as
  Final Fantasy Type-0 and Kingdom Hearts Birth by Sleep do), and goes on.
- *Genso Suikoden*: sceCcc is no longer missing; its intro plays with its subtitles as before (sceMt19937Init still is).
- *Persona 3 Portable* isn't stuck: with a frame every 300 it shows its logo, intro and title ("PRESS ANY BUTTON", 2100
  to 3000), and then its attract loop starts over, frame 3600 landing on that loop's loading screen. scesupPreAcc
  0x86debd66, called once with the disc's ID, is refused and the game goes on. Its row is judged again: menu.
- The other 48 (all the black and hanging games but these, and a sample of menus, movies and gameplay across the
  library): every frame the same picture on both runners, or the same scene an animation step apart (under 2.5 of 255),
  but Tekken: Dark Resurrection's frame 60, where the runner's capture caught its "Loading game data" box a frame early
  under load (run again alone, both runners give the same pictures at frames 55 to 65).

**What keeps the black games black.** Traced in the scratch runner, thread states at frames 300, 1200 and 2400:
- *PGD-encrypted data*, five games: Black Wolves Saga (`PSP_GAME/INSDIR/INSTALL.DNS`), Dragon Ball Z: Tenkaichi Tag Team
  (`disc0:/sce_lbn0xe98b_size0x4A0`), Shining Blade and Valkyria Chronicles III (`PSP_GAME/INSDIR/DATA.BIN`, through
  CRI's file system: its busy server thread is retrying, not stuck) and Naruto Shippuden: Ultimate Ninja Impact. Each
  opens the file, gives its key (ioctl 0x04100001; the headers say key index 1, DRM type 1) and, refused, tries again
  for good. Decrypting PGD needs AMCTRL's cipher, which the wiki's PGD page lays out only as a structure (its link for
  the algorithm goes to JPCSP's forum, not a source this project uses), and its MAC checks need the wiki's AMCTRL and
  DNAS keys, which aren't among the keys committed (an owner's decision, as part 18's were).
- *Need for Speed: Most Wanted 5-1-0*: its main thread waits for good in sceGeDrawSync; the GE met a RET with no CALL to
  return from in one of its lists (0x0914dc40 and 0x090edc00 take turns, calling shared lists at 0x08afd6c0) and stopped
  it, and the driver never hears its end. A matter for the GE's part (`ares/psp/ge/list.cpp`).
- *Monster Hunter Portable 3rd HD*: its program, 26.5 MiB in one segment from 0x08804000, doesn't fit the 24 MiB user
  partition (its PARAM.SFO asks for no more: the HD versions were made for the PS3), so it never starts. Given the 64
  MiB partition (a scratch build), it shows a Dolby logo and stops at the same missing functions as the PSP version
  (sceJpegCsc and sceMpegAvcConvertToYuv420): not worth a rule of its own until sceJpeg is here.
- *Def Jam: Fight for NY*: its file thread reads the disc's folders and then waits for requests that never come; why,
  not found.

**Checks.** `tests/psp/run-tests.sh` (sanitized): 373 groups, none failing. `tests/allegrex/run-tests.sh`: 58, none
failing. `tests/psp/ares/run-tests.sh`: 307 checks, none failing. New groups: "ccc convertstring", "ccc characters",
"ccc one at a time", "tls pools created", "tls pools handed out", "tls pools waited for", "lightweight mutexes tried the
newer way", "modules unload themselves from a lent stack"; "a stack lent for a call" (part 51's) with the older state's
trampoline; and the states' fields and refusals for each. Two independent read-only reviews; what they found was fixed:
the TLS status copied as far as its size word says (it had been written whole), a priority test that couldn't tell
priority from first come, an interrupt handler's TLS calls acting for the interrupted thread, state refusals not
isolated, the recordings' inputs followed exactly, and comments.

**Left, and why.** PGD decryption (five games, above). scePauth, sceJpeg and sceMpegAvcConvertToYuv420 (Monster Hunter
Portable 3rd, and its HD version once it has the memory). scesupPreAcc (both Dissidias and Persona 3 Portable call it
and go on). scePower 0xa85880d0 (Final Fantasy Type-0, Kingdom Hearts Birth by Sleep, Dragon Ball Z), sceMt19937 (Genso
Suikoden). The older sceKernelTryLockLwMutex, sceKernelLockLwMutex and Unlock disagree with threads/lwmutex's try, lock
and unlock recordings on hand-made work areas and counts (part 32 noted it). The GE's RET at depth 0 (Need for Speed:
Most Wanted). Black still, cause not found: Ridge Racer, Crush (its CPU stops in a call into the program at frame 730),
Dead or Alive - Paradise, Def Jam, Jak and Daxter - The Lost Frontier, Tekken - Dark Resurrection, Valhalla Knights,
PaRappa the Rapper, MACH; Need for Speed - ProStreet's crash after its notice.

## Part 57: the GE's lists — a RET with no CALL, and what the driver's callers see

On branch `cursor/psp-ge-lists-2b67`, on top of #189's `cursor/psp-hle-games11-2b67`. Sources: the games' own lists,
traced with a scratch runner; pspautotests' gpu/ge, gpu/signals, gpu/callbacks and gpu/displaylist programs and their
recordings (each named below; the programs were run through the kernel by a scratch runner answering their emulator
devctl, their output compared with the recordings line for line); the PSP Developer Wiki's Hardware Registers and
Graphics Engine pages (through the Wayback Machine); pspsdk's headers. No PPSSPP or JPCSP source was read.

**1. A RET with no CALL to return from** (`ge/list.cpp`). Need for Speed: Most Wanted 5-1-0 waited for good in
sceGeDrawSync from its ninth list on (frame 108): the GE stopped it at a RET at depth 0 and the driver never heard its
end. The list, 0x0914dc40 (it takes turns with 0x090edc00), is the game's frame: its settings come in small lists made
to be CALLed, a frame buffer's (FBP, FBW, PSM, then RET with FBP's low bits as its argument) and a depth buffer width's
(ZBW, RET). Most of them it jumps over and CALLs (0x0914e448: BASE, JUMP 0x0914e468, six such words, BASE, CALL
0x0914e450), but before drawing into its 256-wide target at 0x040cc000 it runs six such words, that target's, in place
(0x0914e374-0x0914e388), RETs and all, and the frame's commands go on after them to its FINISH. Nothing public says what
the GE does there: the wiki's Hardware Registers page gives it two return addresses (GE_RADR1, GE_RADR2, with
GE_OADR1/2 for the offsets) and two depth bits in GE_LIST (0x100 at depth 1 or 2, 0x200 at 2), and an ERROR bit among
its interrupts. The game draws on the PSP, so the GE goes on: back to the stale first return address (0x0914dcd0) it
would loop through the frame for good, and stopped, the game would hang as it did here. So a RET at depth 0 is passed
over, the next command next, the offset and depth as they were (and nothing noted: it isn't a fault). A third CALL
still stops the GE (no game was seen doing it; no source says). With it the game goes on past its autosave prompt:
its title art, its safe driving notice and its intro movie (frames 300, 600 and 1500-1800 with a frame every 300, on
Software), then its title, "Press START button", from frame 2100, on all three renderers (below).

**2. What the driver's callers see**, as pspautotests recorded (`kernel/ge.cpp`):
- *A finish callback's list is done.* The PSP's driver runs a list's finish callback before taking it off the queue
  and before starting the next (gpu/ge/queue2: "a finish callback runs before the next list is started, with its own
  list still linked"), and there sceGeDrawSync(1) no longer counts it: 0 when it's the last, 2 with another queued
  behind (queue2's "Finish callback ordering"; gpu/signals' simple, sync, pause, suspend, jumps, gpu/displaylist/state).
  Phobos said 2 for the last one too. A game whose finish callback asks whether drawing is over would have waited.
- *A list's own stall address.* "Stopped at its stall address" (3) is the GE waiting where the list's stall address
  is: between a PAUSE signal and the FINISH that delivers it, a list's stall address moved on goes no further than the
  list (only a running list's reaches the GE), so the GE waits on and the list reads as drawing (queue2's "Pause
  window": 3, then 2; Phobos kept saying 3).
- *sceGeSaveContext from a callback that stopped the GE.* Waiting for a suspending SIGNAL's or a FINISH's callback,
  the GE has stopped, and sceGeSaveContext saves (gpu/ge/callbackstate: "a GE that has stopped — which is all
  sceGeSaveContext cares about"); with the GE running on (a SIGNAL that lets it go on), or stalled, it's -1 still.
  sceGeRestoreContext is refused as before while a list is the GE's (unmeasured there; it would move the list).
- *A list queued twice.* Built with SDK 2.00 or later (gpu/ge/queue: 0x02000000 refuses, 0x01000010 doesn't), a
  program can't queue a list at an address the queue holds: BUSY. The driver compares the address it keeps for each
  queued list (queue2: "against the list's start address, not where it is now"), which is where a list starts, or where
  it was broken off or paused, the address it would go on from (queue2: enqueueing where a broken-off list stopped is
  refused, its start then isn't); both of memory's views (the uncached one too), and the list whose finish callback
  runs, still queued (queue2: from its own finish callback). Phobos's list registers hold exactly that address while a
  list isn't the GE's, so they're compared.
- *Programs built with older SDKs*, up to 2.00.10 or saying none. A suspending SIGNAL's list reads as paused (4)
  while its callback runs, the GE still on it, so breaking it off from the callback is BUSY, and a stall address moved
  there is the list's alone: the GE goes on to where it was to stop, the list reading as drawing, until it's moved on
  again (handlercalls with 0x01000010, suspend saying none; handlercalls: "above this SDK version [0x02000010] the
  list isn't PAUSED during the handler"). Callbacks aren't told where the list had got to (0 for the third argument:
  simple, pause2, jumps, gpu/callbacks/ge_callbacks saying none; the list's address at 0x02080000 and 0x03080000 in
  pause2, and at 6.60), and sceGeContinue on a list drawing says -1, not ALREADY (simple and continue saying none,
  ALREADY at 6.60): taken to change at handlercalls' cut, which those recordings allow. Unmeasured, and chosen so the
  list the GE still has isn't taken from under it: sceGeContinue on it (paused for that callback, or on its way to a
  PAUSE's FINISH, whose BUSY queue2 recorded) is BUSY, and sceGeListEnQueueHead ahead of it is INVALID_VALUE, as for
  a front list that isn't paused (before, it put the list back in the queue under the GE, which started it over).

Of the 22 driver programs (gpu/ge 11, gpu/signals 8, gpu/callbacks 1, gpu/displaylist 2), 3 printed what the PSP
printed before (alignment, edram, enqueueparam) and 7 do now (ge_callbacks, callbackstate, queue, jumps besides). Of
gpu/signals' 622 recorded lines 147 differed, now 23; of the other 14 programs' 938, 236, now 215 (edramswizzle's
swizzled patterns, unchanged, most of them).
What's left in the others is mostly the GE's time: on a PSP a list takes time, so a callback for a SIGNAL that lets the
GE go on finds the list still drawing (Phobos's GE, taking no time, is past it: continue, simple's kind 2), a
thread waiting in sceGeDrawSync is rescheduled (the [r] marks), intrsuspend's list is still drawing. The rest:
the GE state buffer's layout (context and break read its words: the PSP keeps the vertex, index and offset addresses
at words 5 to 7, command 0x17's word at 26, and writes 386 words at most; Phobos's own layout is 419 words, inside
the 512 of pspsdk's PspGeContext), sceGeGetStack (get: not here; no game in the library calls it), a SIGNAL call's
stack in use by a started list refused (queue2's shared stack), break's DrawSync(1) after a list is queued at the head
of a broken-off one (state: 3 on the PSP), and the transfers' and VRAM mirrors' edges with sceGeEdramSetAddrTranslation
(0) (gpu/transfer/invalid and mirrors: a transfer past VRAM's end wraps to its start).

**The report's black games** (traced in the scratch runner: thread states and the GE's queue at frames 300, 1200, 2400
and 3600, a frame every 300, then traces of each one's calls and lists). Only Need for Speed: Most Wanted was stuck in
the GE. For the others the GE draws what it's given; what each showed from the GE's side went to the notes shared with
the kernel side's parallel branch, which found and fixed the causes there:
- *Def Jam: Fight for NY*: its GE init lists (state only, no PRIM) finish at once; DVDUMD_LOAD_MODULE (priority 0x32)
  runs before 'file' (0x6f) has made UMD_SAMPLE_IO_SEMA, waits on semaphore -1 and ends; main polls its end for good.
  The kernel side: sceUmdActivate takes no time.
- *Ridge Racer*: after its opening movie each frame's list is one clear (18 commands); the disc image's movies are
  139264-byte stubs (3 pictures, no sound) and the movie tears down cleanly; what the game waits for after it isn't
  the GE, and isn't found.
- *Crush*: its buffered file reader's memcpy runs with a size near 0x7e000000 over all of RAM (the program's code too,
  hence the crash in a VBlank handler at frame 730), from a corrupted read. The kernel side: file descriptors handed
  out without reuse land the game's per-descriptor counter in its read buffer.
- *Tekken: Dark Resurrection*: after its logo the GE gets nothing; its intro movie never decodes a picture. The kernel
  side: its frame limiter waits for good on sceKernelSysClock2USecWide given no seconds pointer.
- *Dead or Alive Paradise*: a list a frame, all finished; its async read is re-issued for good. The kernel side: the
  read's bytes landed as it was made, before the game's marker.
- *Jak and Daxter: The Lost Frontier* and *PaRappa the Rapper*: the GE never gets a list; the first opens and closes an
  LBN file 87,861 times in 200 frames (the kernel side: descriptors), the second's sound thread spins at the top
  priority on sceAudioOutputPannedBlocking refusing an unreserved channel 0 (the kernel side: __sceSasCore taking no
  time, so the thread never lets the game reserve its channels).
- *Valhalla Knights*: a clear and a fade a frame; it seeks its first file over and over, never reading it. The kernel
  side: the asynchronous seek still under way when it looks.
- *Need for Speed: ProStreet*'s crash: sceIoDread fills 1040 bytes from the entry's d_private (0x09fff380) over its
  caller's saved registers. The kernel side: the game's buffer is the older 272-byte layout, written as the newer one.
- *MACH* isn't stuck: with a frame every 50 and 300 to 5400 it shows its title from 2100, a black transition at 3600
  (the report's frame), its attract movie and the title again. Judged again: menu.

**Save states.** Nothing new is kept: the RET needs no state, and the driver's new answers come from what was kept
already (each list's state and registers, the SDK version). Version 20 still; no layout change.

**Checks.** `tests/psp/run-tests.sh` (sanitized): 382 groups, none failing. `tests/allegrex/run-tests.sh`: 58, none
failing. `tests/psp/ares/run-tests.sh`: 307 checks, none failing. New and changed groups: "ge moving" (a frame buffer's
settings made to be CALLed, run in place: passed over, the offset kept), "ge callbacks see the ge stopped" (both SDK
ages), "ge pause window", "ge lists queued twice", "ge suspending signals' callbacks" (also sceGeContinue and a list
queued at the head from the callback), "ge callbacks" (both SDK ages), "ge break and callbacks" (its last list's
finish callback now reads 0). Each fails without its fix, each fix taken away alone (the RET: 4 checks; the finish
callback's sceGeDrawSync(1): 6; the list's own stall address: 3; sceGeSaveContext: 6; the twice-queued list: 6; the
older SDKs: 14; sceGeContinue's BUSY for a list the GE still has: 10; the head enqueue: 6). Three independent read-only
reviews: the first found the report's Need for Speed row left unchanged, the head enqueue (above), and claims to word
as unmeasured or as the kernel side's findings; the second, fail counts out of date and wording to tighten; the third
passed, with comments' wording to tighten; all fixed.

**The games** (the report's runs: 3600 frames, Start at 120 and Cross at 1800, 2 GE threads, frames at 300, 1200, 2400
and 3600; the base commit's runner and this branch's):
- *Need for Speed: Most Wanted 5-1-0*: black throughout before; now, on all three renderers, its title art at 300 and
  its title, "Press START button", at 2100 and 3600 (black at 1200, between its notice and its movie), at 19 frames a
  second on Software and on Vulkan, 14 on Vulkan (fast). Software and Vulkan (accurate) give the same pictures at the
  five frames taken (60, 300, 1200, 2100, 3600); Vulkan (fast) is within 0.44 of 255 of them (its own transform:
  part 53).
- *MACH*: judged again (above). *Ridge Racer*: as before, black after its movie.
- Thirty working games on Software: 14 built with SDKs up to 2.00.10 or saying none (Lumines, Twisted Metal: Head-On,
  Ape Escape: On the Loose, Darkstalkers Chronicle, Hot Shots Golf: Open Tee, Metal Gear Acid, Need for Speed:
  Underground Rivals, Tony Hawk's Underground 2 Remix, Burnout Legends, Death Jr., ATV Offroad Fury: Blazin' Trails,
  Archer Maclean's Mercury, MediEvil Resurrection; Tokobot at 2.00.10) and 16 newer (Gradius Collection, Liberty City
  Stories, Pursuit Force, Ridge Racer 2, Crisis Core, Patapon 2, LocoRoco, Midnight Club 3, Peace Walker, Dissidia,
  Ys Seven, Ratchet & Clank: Size Matters, Virtua Tennis 3, Daxter, Lumines II, Ultimate Ghosts 'n Goblins). 25 give
  the same pictures at all four frames. The other 5 differ at a frame or two by an animation step (Dissidia's logo
  fading in at 300, Lumines's title background at 1200, Peace Walker at 300, Pursuit Force at 2400, LocoRoco's title
  at 2400 and 3600); run again, each runner differs from itself as much (the base runner from itself in LocoRoco,
  this branch's in the other four), and the two runners' second runs match but for LocoRoco, which varies on its own.
- On Vulkan (accurate), Lumines, Tokobot, Crisis Core and Hot Shots Golf: the same pictures on both runners at all
  four frames.
- The final build (after the review's fixes) against the base runner: Lumines, Tokobot, Twisted Metal, Metal Gear
  Acid, Crisis Core, Patapon 2 and Ridge Racer 2 the same at all four frames; Need for Speed: Most Wanted's frames as
  the branch's earlier run.

**Left, and why.** The GE's time (a list taking time to draw: the statuses a callback or a poll would see meanwhile;
not modelled, as part 9 decided). The state buffer's layout as the PSP's (programs that look inside it: two
pspautotests). sceGeGetStack. A SIGNAL call's stack in use refused. A third CALL (still stops the GE). Transfers past
VRAM's end and sceGeEdramSetAddrTranslation(0)'s mirrors. God of War: Chains of Olympus's speed (7.7 frames a second,
the kernel side's profile finding most of its time in the drawing a TRANSFER_START waits for: every transfer flushes
everything pending, overlapping or not). The kernel-side causes above (the kernel side's branch).

## Part 60: perspective texels divided by the GE's reciprocal; patch divisions

On branch `cursor/psp-ge-fits3-2b67`, on top of #192's `cursor/psp-ge-lists-2b67` (#191 under it). Round 5 of
`tools/psp-measure` (the owner's `psp-round5` pictures) and round 4 run again with #191's timed waits (`psp-round5b`)
against the software renderer. A rule was taken only where every picture it touches got closer and none got worse.
Original arithmetic: none of PPSSPP's code was copied, and JPCSP was not read. The screen projection is unchanged
(part 10's trunc-toward-2048). Lighting is unchanged (below).

### The perspective divide: fitted

`persp-w3` holds w = 3 at every corner, so there is no perspective gradient, and u runs from 0 to 1024 texels. The
core's per-pixel u was an IEEE divide and came out exactly one texel high on the left half of every row. The geometry
and the edge weights already matched, so the divide is what differs: the GE divides by a reduced-precision reciprocal,
not a full divide. The same path is what `persp-wall`, `persp-divide` and `persp-floor` exercise, and the older
`3d-wall-texels` and `3d-floor-texels`.

**The reciprocal** (`draw.cpp`'s `geReciprocal()`). It's a table of 128 straight chords across the fraction: the top
7 bits of the magnitude's fraction pick the chord and the next 8 the position along it. Chord s starts at
`round(131072 / (1 + s/128))` (131072 down to 65793; never an exact half, so which way a half rounds doesn't arise)
and its slope is that run to the next start, over the 256 positions in the chord, rounded half to even (slopes from
-254 to -64). Rounding the slopes' halves away from zero is a trade: `persp-divide` 209, `persp-floor` 305, round 3's
`3d-wall-texels` 21 and `3d-floor-texels` 20 pixels apart, but `persp-wall` 246 and `persp-w3` 28928 (29,729 in all
against 19,616), so halves go to the even. The value is `(64 * start + 63 + slope * position) >> 7`, in units of 2^-16
of the fraction's reciprocal, placed back at the exponent. The sign is kept. A zero, a denormal, an infinity or not a
number gives 0. Two chords sit one unit high at the positions
`persp-divide` and `persp-floor` actually sample, so their starts are two counts low (the slopes stay the unadjusted
ones): chords 5 and 58. Without chord 5's correction `persp-divide` has 343 pixels apart and `persp-floor` 355;
without chord 58's, 305 and 353 (211 and 351 with both). Putting 49 or 73 one unit low as well would clear the rest of
`persp-divide`'s exact-integer columns and make `persp-floor` worse, so they stay. The other 126 chords aren't pinned
one by one: they're the formula's.

**At a vertex** (`GE::triangle()`). s is u * R(w) and t is v * R(w), each with its low 8 fraction bits cut off (the
GE's 24-bit float, toward zero), and q is R(w). s, t and q then become 15-bit integers on the unit of the largest
exponent among the three of that one, cut toward zero. The 15 bits are sharply pinned: with 13, 14, 16 and 17 bits
`persp-wall` has 5647, 2416, 1738 and 2396 pixels apart (23 with 15), and with 20 or 23 about 2550, as the plain
divide had. With the texture matrix's own q (TEXTURE_MAP_MODE 1, projected textures), q is float24(q * R(w)): the
divide by q the blend had before, carried over, not measured. (Every other way that's R(w) itself: it has 16
significant bits at most, which the cut keeps, but where it's a denormal, for w of 2^126 or more.)

**Across the triangle.** Those integers are stepped with part 48's stepper: the reciprocal of twice the area kept to
16 significant bits and rounded down, the steps in 16384ths and rounded down, anchored at the leftmost corner (the
topmost of two). A pixel takes the stepped value rounded down, times its unit. Its texture coordinate is that s (and
t) times the reciprocal of that q, the product kept to 24 significant bits and cut toward zero (a rounded float
multiply sits the wrong side of several of these texels; a product below 2^-126 then rounds as a float holds it). A q
of 0 has the reciprocal 0, so the coordinate 0, and a q below 0 a reciprocal below 0 (no picture measures either:
every corner's w is above 0 in all of them). Nearest filtering still takes `floor` of the coordinate (`texture.cpp`).
The four-at-a-time path (`four.cpp`) and the one-at-a-time path (`raster.cpp`) give the same numbers: the same
operations, but for the reciprocal and the 24-bit products, which the four-at-a-time path works out in its own way
with every step exact (`reciprocalLanes()`, `productLanes()`). It's integer arithmetic and masks on the floats' bits
(the chords a table made at compile time), with no division at a pixel. s, t and q are stepped there in 32-bit lanes
that may run round, as colors and depth are: every value inside a triangle stays within 32 bits, whatever its steps
(`fourFriendly()` says why), so no triangle is turned away for its size or steps any more.

**What it does to the measured pictures** (pixels apart, before and after; every other file is as it was):

| File | Before | After |
| --- | --- | --- |
| `persp-wall` | 2662 | 23 |
| `persp-divide` | 3172 | 211 |
| `persp-floor` | 6026 | 351 |
| `persp-w3` | 33040 | 18944 |
| `3d-wall-texels` (round 3) | 307 | 23 |
| `3d-floor-texels` (round 2) | 291 | 64 |
| `curves-made-up` | 13264 | 12974 |

`persp-wall`'s u is exact; its 23 are the same v pixels `3d-wall-texels` still misses, one texel off. `persp-w3` is
closer on every row and not exact: with w constant the stepper's gradient is a hair short of the true slope, so the
coordinate saws across the texel boundary, 74 columns of row 128 still a few thousandths to a few hundredths of a texel
the wrong side. Moving the chord w = 3 uses (chord 64) a unit either way doesn't beat 18944: raised it leaves
`persp-w3` 18944 and `persp-floor` 353, lowered `persp-w3` 22016; the wall pictures stay at 23 either way.
`persp-divide` and `persp-floor` keep a boundary pixel here and there (211 and 351); the two corrected chords are what
those pictures pin, and the rest of the table is what they all share.

**Not this path.** Sprites still blend 1 / w in IEEE doubles between the two corners (`spriteRows()`, `spriteFours()`):
`persp-sprite` stays at 1204 and `3d-sprite-texels` at 72. Lines still divide from their two ends: `lines-texels` stays
exact, and the other line pictures are unchanged. Curved surfaces' texels (`curves-texels`, 958) are unchanged; the
290 `curves-made-up` picked up are patches that go through a triangle. The hardware renderer returns before this setup
and draws with the GPU's own divide, as before.

**Tests.** "draw3d perspective texels as a PSP draws them" (`tests/psp/draw3d.cpp`) draws round 5's `persp-w3` and
`persp-divide` as `tools/psp-measure` drew them and checks six pixels as the owner's PSP drew them: five a float divide
gets wrong, among them one chord 5's correction pins (without it the test fails), and one chord 58's pins, four pixels
at a time and one at a time. "draw3d four pixels at a time against one", 20000 random primitives, both paths, now also gives some of them
texture coordinates over dozens of repeats, corners from just past the near plane to nearly the far one, or the
texture matrix's own q: they still match. Save states are unchanged (the new fields are on a job, which isn't
serialized). The arithmetic was first written with library calls (frexp, ldexp) and then as it is now; the two drew
every measured picture byte for byte the same. An independent review checked the reciprocal over all 2^32 floats, the
products and the quantizing over hundreds of millions of random cases, and 1.2 million extreme triangles both ways,
with no difference; the GE's source builds with GCC 11, as the Linux build's does.

**Speed.** The GE's divide costs more than a float division: a table read per lane, the products in doubles, and the
setup of s, t and q for every textured 3D triangle on the GE's thread. On the RP6 (the benchmark scenes, 300 frames,
rounds taking turns with the build before this part):

| Scene | 7 drawing threads, before → after | 1 drawing thread, before → after |
| --- | --- | --- |
| Midnight Club 3, race | 54.8 → 53.2 fps (−3%) | 23.7 → 21.7 fps (−8%) |
| Midnight Club 3, menu | 74.6 → 70.2 fps (−6%) | |
| Liberty City Stories, city | 75.1 → 70.0 fps (−7%) | 38.4 → 35.0 fps (−9%) |
| Liberty City Stories, woods | 71.6 → 69.3 fps (−3%) | 38.9 → 35.1 fps (−10%) |
| Lumines, demo (2D) | 186.9 → 198.1 fps (+6%) | |

The 2D scene gains as a triangle no longer sets up the old blend's edge values, and as every triangle now goes four
pixels at a time, whatever its size or steps (`fourFriendly()`: large triangles reaching past the screen used to go a
pixel at a time). The profile (simpleperf on the RP6) puts the 3D scenes' loss about half on the new arithmetic itself
and half on the pixel loop waiting for it (its chain from q to the texel is longer than the old divides'); with 7
threads the GE's thread also draws more bands itself, as the workers fall behind. Tried and kept: a 64-bit division a
triangle shared by all its steps, only the steps its pixels use, the corners quantized with integer shifts, the
exponent applied as two exact powers of two (no branch). Tried and dropped: reading a chord once for a four on one
chord (the test cost more, 5% in LCS's scenes). Left to try: the reciprocal worked out a four ahead, to hide its
latency.

### Patch divisions: round 4 again

#191's program gives each GE list 5 seconds and resets the GE when it doesn't finish (part 48, "Round 4 again"). The
owner's run (`psp-round5b`) finished round 4: `stall-check` drew the same rows before and after its reset as the core
does, so the reset works on the PSP, and the curve counts came back as:

| Test | Divisions along u | On the PSP |
| --- | --- | --- |
| `curves-count` | 16, 63, 64 | drawn (292 pixels lit) |
| `curves-count-65` | 65 | the GE never finished, not a point drawn |
| `curves-count-100` | 100 | the same |
| `curves-count-128` | 128 | drawn as 1 division: 4 points, and the list finished |
| `curves-count-200` | 200 | never finished, nothing drawn |
| `curves-count-255` | 255 | the same |

So PATCH_DIVISION's counts are 7 bits (bits 0-6 and 8-14): 128 is 0, which divides as 1 does (the rule already
known), and 200 is 72, 255 127, both past 64 like 65 and 100. More than 64 divisions leave the GE drawing forever
(`sceGeDrawSync` said "drawing", not at a stall address), and nothing of that surface is drawn. The core
(`curves.cpp`'s `patch()`) now takes the counts as 7 bits and draws nothing of a surface with more than 64 along
either way, its points still read, and goes on with the list: a game can't have relied on more, since the PSP would
stop there. (Only u was measured past 64; v is taken to be the same.) `curves-count-128` is now identical, and the
four that stalled are too (nothing drawn). `curves-count`'s 72 pixels apart at 16, 63 and 64 divisions are the
vertices' placement part 48 found (a point a pixel over here and there), not refitted here.

"curves malformed" (`tests/psp/curves.cpp`) checks the counts: 64 divisions light 65 points, 128 two, and 65, 127 and
200 none, the points read each time; its budget checks now cut 255 points each way 64 times.

### Lighting: left as it was

`share()` still returns `ceil(256 * amount)`, held to -256..256. Round 5's `light-share-0` through `light-share-3` (16
normals, 192 products each: 3072 channel values) have the PSP match the core on 2209, one level lower on 816 and one
higher on 47, so the core is mostly right or one high. Rounding the share down instead wrecks the round-3 pictures
(`light-diffuse`, `light-cosines`, `light-shine`, `light-materials`, `light-colors`, tens of thousands of pixels), which
is what settled the rounding up (part 48). The leftover isn't that multiply: a level taken as the product times the
true cosine still misses hundreds, well away from a level boundary, and a normal and the same normal turned don't share
one scale. A fixed-point cosine from 8 to 20 bits still leaves about 631 of the 3072 off, so no change to the
share was taken. `light-share-*` are as they were (43520, 39424, 42240, 36096), and so are `light-diffuse` (12288),
`light-spot` (768), `light-point` (9728), `light-environment`
(512), `light-materials` and `light-colors` (1536 each). `light-cosines`, `light-powered`, `light-shine` and
`light-ambient` stay exact.

### Left, and why

The near-plane cut when the first or second corner is past the plane (`clip-split`, 14086; part 48 measured only the
third) was not refitted. `steps-check` (27) and `steps-depth-check` (346) are the stepper's known leftovers and were
left. `persp-sprite` (1204) is the sprite path above. `persp-w3`'s 18944, `persp-divide`'s 211 and `persp-floor`'s 351
are the reciprocal and the stepper sitting a fraction of a texel the wrong side of a boundary; no further one-unit
change of a chord improved one of them without making another worse.
