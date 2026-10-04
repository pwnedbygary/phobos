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
Nothing is in the app yet.

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
- **A recompiler from the start, the interpreter as its fallback.** Speed and accuracy are the project's aims, so
  the CPU gets a dynamic recompiler (dynarec) early rather than as a late optimization. The interpreter stays: it is
  the reference the recompiler is tested against, and runs whatever the recompiler doesn't handle. The recompiler's
  MIPS-generic parts are kept apart so other MIPS systems can reuse them (ares's PS1 CPU is interpreter-only).
- **Code in the style of ares and near**, so it reads as part of the same codebase: plain structs with public
  members, `auto f() -> T`, one function per instruction named after its mnemonic, decoder tables built with
  macros as in ares's PS1 and N64 cores. Everything important is explained in plain language in the comments, for
  someone who has never seen a MIPS CPU.

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
nearest is used), and conversions of NaN or out-of-range values (0x7fffffff, MIPS's default).

Tests: `tests/allegrex/run-tests.sh` (14 groups, with the undefined-behavior sanitizer; on Linux the address
sanitizer too, which the PSP Core Tests workflow runs for changes to `ares/psp/`, nall or ares's types).
`harness.hpp` holds the test machine (a CPU over 64 KiB of RAM) and the instruction encoders.

## Part 3: the VFPU

`ares/psp/cpu/interpreter-vfpu.cpp`, with its decoder tables in `interpreter.cpp`: all of the VFPU's instructions,
one function each, named after their mnemonics. The register file is eight 4x4 matrices of floats; a 7-bit
register number names a single, a row or column vector (pair, triple, quad) or a matrix, by the operand size
(`vfpuLine()`, `vfpuSquare()`). Source prefixes swizzle, take absolute values, negate or substitute constants;
the destination prefix saturates or masks; every VFPU instruction but the prefix ones uses them up. Denormals
count as zero both ways, and only round-to-nearest exists. The recompiler runs the VFPU through the interpreter
for now (its branches included), as it does the FPU.

The random number generator is the hardware's, as fp64 worked it out from a PSP's output (PPSSPP issue 16946):
a linear congruential generator, a xorshift and a Pell-like sequence with a carry, added together, their state
packed into the eight RCX registers (`vfpuRandom()`); `vrnds` spreads its seed over them; `vrndi` and `vrndf`
fill lanes from the last back, and their destination prefix only reaches the last lane. Implemented from that
description (no PPSSPP code), and checked against the user's PSP: its state at power on and the numbers of 64
seeds all match, including those that exercise the carry (psp-vfpu-measurements.md).

The math functions `vrcp`, `vnrcp`, `vrsq`, `vsqrt`, `vexp2`, `vrexp2`, `vsin`, `vcos`, `vnsin` and `vasin` (and
`vrot`'s sine and cosine) are the PSP's own: its quadratic interpolator with coefficients fitted from our
measurements (`vfpu-segments.hpp`, from `tools/psp-vfpu-measure/fit.py`), exact on every measured input; see
psp-vfpu-measurements.md. So is `vlog2` below 4, from a fixed-point table and a cheaper straight-line path below 1;
from 4 up about half its results are one unit above the PSP's, which drops more precision there.

Not checked against hardware: `vrot` on its own (it uses vsin's and vcos's), `vwbn` (implemented from its
description; no test), and what reserved size combinations do (they raise ReservedInstruction, and leave the
prefixes). Which instructions use up the prefixes: every VFPU
arithmetic instruction, and `vnop`, as pspdev's documentation says (PPSSPP keeps them through `vnop`, but the
documentation rests on tests on hardware); not `vsync`, `vflush`, `vmfvc` and `vmtvc`, which the documentation
doesn't cover, as in PPSSPP; nor the loads, stores and moves to integer registers.

Tests: `tests/allegrex/vfpu.cpp`, ten groups (addressing, arithmetic, prefixes, products, comparisons,
conversions, functions, matrices, moves, and more instructions worked out by hand from the descriptions), run on
both engines; the generated programs that compare the engines include VFPU instructions and its branches, and
compare its registers, prefixes and condition codes.

### Measuring the VFPU on a PSP

To make the math functions exact from our own data (the user chose this over adopting PPSSPP's GPL tables, and
has a PSP to run it on), `tools/psp-vfpu-measure` records what a real PSP computes. It's a homebrew program, built
with pspdev's toolchain (`make` in that folder, with `psp-config` on the PATH; the `phobos-linux` container has the
toolchain in `/opt/pspdev`). On a PSP with custom firmware it writes, beside its EBOOT.PBP in `results/` (about
450 MB, resumable):

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

The program asks which round to run when it starts: O for that first round, X for the second (about 230 MB,
`manifest2.txt`), for what the first couldn't settle:

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

`make SMOKE=1` builds a quick version (round 2 straight away, the big tests cut short) for trying the program in
PPSSPP's PPSSPPHeadless first, which `phobos-linux` has in `/opt/tools/ppsspp`; it says nothing about a PSP.
`compare.sh` checks whatever files a folder has, from either round.

## Part 5: the memory map

`ares/psp/memory/`: what each address reaches. The Allegrex has no TLB, so the four windows (user, uncached,
kernel, kernel uncached: an address's top three bits) all reach the same physical memory, the low 29 bits; under
HLE nothing keeps the game out of the kernel's. Physical memory is the scratchpad (16 KiB at `0x00010000`), VRAM
(2 MiB at `0x04000000`, seen four times in a row up to `0x047fffff`) and main RAM (32 MiB at `0x08000000`, or 64
MiB as on later models). Anything else (the hardware registers, the boot ROM) is empty for now: reading gives 0,
writing goes nowhere, and `unmapped()` is told, since under HLE that means a bug or something not emulated yet.
The bytes are kept little-endian, as the PSP sees them.

Every change, the CPU's stores and the loader's or HLE functions' copies alike, is reported to `written()`, which
the CPU's owner points at the recompiler so that code compiled from there is dropped. A change to VRAM is reported
for all four copies, as they're four physical addresses for the same bytes. `buildPages()` fills the CPU's page
table for compiled loads and stores, listing VRAM at its first copy only: compiled stores only steer clear of the
pages holding compiled code, so a second address for the same bytes would let a store change compiled code
unseen. The other copies go through `read()` and `write()`, and the recompiler interprets code at any address the
table leaves out. `power()` keeps the buffers while their sizes stay the same, so the table stays valid. `copyIn()`, `copyOut()`, `fill()` and `readString()` serve
the loader and the HLE functions; a range that crosses an area's end fails rather than running over.

Not yet: VRAM's swizzled copies (the GE's depth buffer seen rearranged), and the hardware registers HLE may still
need (the GE's, for one).

Tests: `tests/psp/run-tests.sh`, the PSP system's own suite, built like the CPU's with the same sanitizers and run
by the PSP Core Tests workflow: the windows, each area's edges and what's past them, the hooks, the copies, the page
table, and the CPU on the memory map on both engines, including code that rewrites a function it already ran, in
RAM and in VRAM through another copy, both ways. Four deliberately broken versions (stores not reported; VRAM's
copies not shared; all four copies in the page table; code at unlisted addresses compiled) each failed them.

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
  outside memory, and encrypted programs (`~PSP`), named by the module and encryption type their header gives in the
  clear.

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
  at the start of the user partition, reserves the program's memory exactly where it is (or refuses a program whose
  segments overlap), and starts its first thread at the entry
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

- **Primitives**: sprites (rectangles between pairs of vertices, covering the pixels whose middles are inside, both
  edges included; the second vertex's color and depth; corners bottom-left and top-right turn the texture a quarter),
  triangles, strips and fans (sample points 7/16 into each pixel; pixels exactly on right or bottom edges left to the
  neighbour; colors, depth and texture coordinates blended across, or the last vertex's color with flat shading), and
  points. A vertex without a color takes the material's ambient color. Not yet: lines, and 3D.
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
rounds at a boundary, which the two step to differently). Fifteen broken versions each failed the tests (among them
the texture functions' and blending's rounding, the filter's half texel, swizzled rows, the palette's shift, sprites
never turning, edges all inclusive, 4444's stencil counting by ones, the alpha test the wrong way round, the depth
mask ignored, the dither matrix unsigned, no ambient color, the stencil overwritten by alpha, flat shading taking
the first vertex). A review's claim that a pixel at VRAM's end could run past it was disproved (the offsets stay
multiples of the pixel's size; the code now says so). Found on the way: the address sanitizer's check for stack use
after return made the per-pixel functions hundreds of times slower (18 minutes for the samples), so the test script
turns that one check off (`ASAN_OPTIONS`); the run takes 95 seconds.

### Measuring the GE and the controller on a PSP

The rules above that came from PPSSPP or uOFW rather than from measurements of our own are what
`tools/psp-ge-measure` records on a real PSP. Like the VFPU's program it's homebrew built with pspdev's toolchain
(`make SMOKE=1` builds a version for an emulator, which starts at once and leaves when done). It draws each case into
VRAM, reads the pixels back as they are and writes them to `results/` beside its EBOOT.PBP: 64 result files and a
manifest, about 15 MB, in a few seconds. The cases:

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
`PSP_TEST_PROGRAMS`), checks that every file is written, and with `PSP_GE_RESULTS` set to a results folder lists what
differs from it (`PSP_GE_OURS` keeps this core's files for a closer look). Against PPSSPP's software renderer (its
headless build running the smoke version), 61 of the 64 files match: 60 pictures identical, and the controller's
timing agreeing on what waits (a second latch read doesn't, a buffer read after a vertical blank doesn't either, and a
second buffer read waits a frame). The three that differ are the PSP's to settle:

- a sprite whose right or bottom edge runs exactly through pixel middles (127 pixels): this core draws them, while
  PPSSPP draws them or not depending on where the other corners are, in code its authors mark as unverified;
- a shrunk sprite's texels (4240 pixels, a texel apart): this core takes a sprite's texture coordinates at each
  pixel's middle, PPSSPP at 7/16 in, as for triangles;
- the 3D sprite's fog (10152 pixels): this core takes the second corner's fog for the whole sprite, while PPSSPP
  splits it across the sprite's middle (which, its comments say, seems to be the way).

The 3D cases found one difference that was this core's to fix: a triangle cut at the near plane had 7292 pixels a
level apart from PPSSPP's, because the cut's corners were blended from the other end of the edge, and their colors'
256ths rounded the other way (part 11 now blends them from the corner past the plane). An earlier count here, "49 of
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
- **3D positions aren't rounded up the way PPSSPP has it** (+0.375 of a sixteenth, which the core copies): the PSP
  draws the pixel in all 256 cells, with its left and top edges up to 15/256 of a pixel past the sample point the
  program assumed (7/16 in), where that rounding drops it from 10/256. Either the GE truncates positions to the
  sixteenth, or its sample point is further into the pixel than 7/16 (as the sprites' are); a case with edges
  past 8/16 will tell which.
- **The spotlight's direction is toward the light**, as PPSSPP reads it: the pool has the same shape on the PSP, the
  differences only a level of rounding.
- **The depth buffer doesn't read back in the order the program assumed:** none of the floor's 15264 depths match,
  9616 of them read 0, and 2080 pixels off the floor read something. The PSP's depth buffer is evidently arranged
  differently in VRAM when the CPU reads it at its normal address (PPSSPP reads depth through a separate mirror),
  so this file needs that layout worked out before it says anything about depths.
- **With DEPTH_CLIP_ENABLE off, a triangle reaching past the near plane isn't drawn at all**, as the core has it
  (`3d-clip-unclamped` is empty on both).

The open questions above, settled: a sprite edge through pixel middles follows neither the core nor PPSSPP (the rule
above); a shrunk sprite's texels follow neither, PPSSPP's nearer; a 3D sprite's fog follows neither; the spotlight's
direction is PPSSPP's reading; a second latch read doesn't wait. Next: fit each rule from these files (the coverage
and sample points, the rounding onto the screen, interpolation, the filter, lighting's rounding, the depth layout),
one at a time, each fix checked against them.

## Part 11: drawing in 3D

`ares/psp/ge/transform.cpp`, and 3D paths in `vertex.cpp`, `draw.cpp` and `pixel.cpp`. Outside through mode:

- **Vertices**: 8- and 16-bit numbers are fractions (128ths and 32768ths). With morph targets a vertex is their sum,
  each weighted by its MORPH_WEIGHT. With skinning (a vertex type with weights), its position and normal go through
  the bone matrices its weights pick, and the results are added up, weighted.
- **The transform**: the world, view and projection matrices in turn (each element the float its 24-bit DATA word
  holds), then the viewport and the screen offset, rounded to the sixteenth as the GE rounds (up from 0.625 of one).
- **What isn't drawn**: a primitive with a vertex off the 4096x4096 screen. Depths outside 0-65535 are held to that
  range with DEPTH_CLIP_ENABLE on, and count as off the screen with it off. A z / w past 1 (by 2^-15) drops a
  triangle or sprite with any such vertex (DEPTH_CLIP_ENABLE off) or with all of them past the same end (on); points
  aren't judged by it (as PPSSPP has it). A triangle with every w below zero isn't drawn either.
- **Clipping**: a triangle is cut at the near plane (z < -w) only, never at the screen's edges (the scissor does
  those). The new corners are blended in clip space from the corner past the plane (colors in 256ths, which that way
  round decides) and put on the screen again; with flat shading every piece keeps the last vertex's color.
- **Culling** (CULL_FACE_ENABLE, not in clear mode, through mode too): CULL 1 draws the triangles running clockwise on
  the screen, 0 those running counterclockwise; every other triangle of a strip counts the other way round.
- **Texture coordinates**: perspective-correct across triangles (blended as u/w and 1/w, then divided); colors, depth
  and fog are blended straight. Mode 0 takes the vertex's, times TEX_SCALE plus TEX_OFFSET; mode 1 the texture
  matrix's result from the position, the texture coordinates or the normal, its q dividing at each pixel.
- **Fog**: each vertex's (view z + FOG1) × FOG2, blended across, 0-255, mixed in after the alpha test as
  (color × f + fog color × (255 − f) + 255) / 256. A FOG1 or FOG2 that isn't a number to a float is a huge number to
  the GE.
- **The depth range test** (MIN_Z to MAX_Z), in 3D only, clear mode included.
- **Sprites in 3D**: both corners transformed and checked by the same rules, then drawn as in 2D, with the second
  corner's fog.

Not yet: lighting (noted; vertices keep their colors), environment mapping (which comes from lighting), PRIM's kind 7
(going on with the last primitive's vertices), lines, mipmaps, curved surfaces. Nor these smaller details:
- a 3D sprite's texture projection (its q is ignored);
- the fog PPSSPP splits across a 3D sprite;
- the texture coordinates and normal PPSSPP lets a vertex without them keep from the last one read.

The rules are PPSSPP's software renderer's, for behavior only. The commands' layouts come from pspsdk's GU library:
`sceGuSetMatrix`'s element order, `sceGuViewport`, `sceGuDepthRange`, `sceGuFog` and `sceGuFrontFace`.

Tests (`tests/psp/draw3d.cpp`) are worked out by hand on a scene where every matrix is the identity and the viewport
puts a model's x and y on the screen's pixels. They cover:
- the matrices in order, and the rounding to the sixteenth;
- each "isn't drawn" rule, for triangles, sprites and points, with DEPTH_CLIP_ENABLE on and off, and every w below
  zero on its own;
- a triangle cut at the near plane, the rows and colors it leaves, and flat shading's color on every piece;
- culling either way, strips, clear mode and through mode;
- perspective-correct texture coordinates, with q;
- fog's rounding, its blending across a triangle, and its distance being the view's;
- the depth range test;
- both texture coordinate modes, morphing and skinning.

pspsdk's "cube" sample (a textured cube turning in perspective, its back faces culled, depth-tested) runs, and its
picture is within 1 level of PPSSPP's software renderer on every pixel (`compare-ppsspp.sh`). Twenty-six broken
versions each failed the tests, among them:
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
  multiply and shift down 10 bits; a light's share counts 512ths, rounded up, and three numbers shift down 19. "To the
  power of" is the GE's quick approximation (exact at powers of two, a little low between them), and the coefficient
  keeps only the top four bits of its fraction. Each channel ends held to 0-255.
- **The shine kept apart** (LIGHT_MODE 1): a second color, blended across triangles like the first and added after
  texturing, so a dark texture doesn't dull it.
- **Environment mapping** (TEXTURE_MAP_MODE 2): texture coordinates from two lights (TEXTURE_SHADE_MAPPING), lit or
  not: (the cosine between the normal and the direction to the light + 1) / 2, a shining light's direction taken half
  way to the viewer's.

The rules are PPSSPP's software renderer's (its lighting, and its notes on the PSP's power function and shade mapping
from tests on the hardware), for behavior only. The layouts come from pspsdk's `sceGuLight`, `sceGuLightAtt`,
`sceGuLightColor`, `sceGuLightSpot`, `sceGuLightMode`, `sceGuMaterial`, `sceGuModelColor`, `sceGuSpecular`,
`sceGuAmbient`, `sceGuColorMaterial` and `sceGuTexMapMode`.

Tests (five more groups in `tests/psp/draw3d.cpp`) work each color out by hand from those rules:
- the ambient part with emissive, and the vertex's color standing for the ambient (red held to 255);
- a directional light's diffuse: squarely, at a cosine of 0.8, with a normal not one long, a direction not one long,
  the light off, from behind, powered, at a cosine where the share's rounding up and its one more show (0.501), and
  the vertex's color standing for the diffuse;
- a point light's fading, and a spotlight's cone either way;
- the shine: the quick power (0.75 squared is 0.5), the coefficient's cut fraction (1.03125 counts as 1), a turned view,
  and kept apart, added after the texture;
- environment mapping from two lights, one of them shining.

pspsdk's "celshading" (shaded through environment mapping) matches PPSSPP's software renderer pixel for pixel, and
"envmap" (lit and environment-mapped) is within 1 level on every pixel (`compare-ppsspp.sh`). Twenty-one broken
versions each failed the tests, among them:
- colors without their + 1, the ambient shifted a bit too far;
- shares rounded down or without their one more;
- the true power in place of the GE's quick one, the coefficient's whole fraction kept;
- NORMAL_REVERSE, MATERIAL_COLOR, fading, cones or the powered diffuse ignored;
- the viewer left out of the half-way direction, or taken from the wrong column;
- the shine never kept apart, or never added after the texture;
- environment mapping's u and v swapped, or blind to shining lights;
- every light on, or lighting without LIGHTING_ENABLE;
- normals or directions not made one long.

Two of them (the shares) first got through: no case crossed a level by one share, and the cosine of 0.501 now does.

Not yet: lines, mipmaps, curved surfaces (BEZIER, SPLINE), bounding boxes, PRIM's kind 7.
