# psp-vfpu-measure

A homebrew program that records what a real PSP's VFPU (its vector maths unit) computes, and the host tools that
turn those recordings into Phobos's PSP core and check the core against them, bit for bit.

## Why it was needed

The VFPU's maths functions (sine, cosine, reciprocal, square root, log, exponent and the rest) aren't exact maths:
each is the hardware's own approximation, and its answers differ from the true ones in the last few bits. Games
depend on getting the same bits as the hardware, because small differences add up: physics drifts, replays and
ghost runs fall out of step, comparisons go the other way. No public document lists those exact answers. PPSSPP has
tables for them, but they're GPL code, and Phobos doesn't copy other emulators' code. So the user chose to measure
their own PSP instead (see "Decisions" in [docs/psp-core.md](../../docs/psp-core.md)): this program runs every
input that matters through the real hardware and saves the answers; the host fits a model to them, and checks the
core reproduces every recorded value.

## What it measures

It asks which round to run when it starts. Each round writes its own files, and a `manifest.txt` (round 1) or
`manifest2.txt` (round 2) saying how each file's inputs are made.

- **Round 1 (O, about 450 MB):** every input of the range each maths function reduces its argument to, and a million
  spread-out inputs for each; the random number generator from a range of seeds; `vadd`, `vsub`, `vmul`, `vdiv` and
  `vdot` on inputs of every kind (signs, sizes, denormals, infinities, NaNs).
- **Round 2 (X, about 230 MB):** what round 1 couldn't settle: `vlog2` over whole binades above 4; dot products,
  sums and averages built to show how the VFPU adds several numbers; every half float through `vh2f` and a million
  floats through `vf2h`; the integer divide (by zero too) and the FPU's conversions and arithmetic in each rounding
  mode; and the instruction recorder, which runs every VFPU instruction pspdev's assembler knows (1216 entries) on
  random register states.

The files hold only what the hardware gave (and, for the smaller tests, the inputs). The host makes every input
again exactly as the program did, so nothing has to be computed on the PSP.

## Files

| File | What it is |
|---|---|
| `main.c`, `Makefile` | The PSP program. |
| `ops.py`, `ops.h` | `ops.py` writes `ops.h`, the instruction recorder's list. Every entry is checked by pspdev's own assembler, because a real PSP stops a program at an instruction it doesn't have. |
| `compare.sh`, `compare.cpp` | Checks the core against a folder of results: each input run through the core's own instruction and compared bit for bit, with how far off any mismatch is (in units in the last place); for the instruction recorder, each differing entry's words counted by kind (a NaN, a denormal, rounding, a lane left unwritten, other). Works on either round's files. |
| `fit.py` | Fits the model behind the maths functions (128 segments of integer coefficients per function) from the results alone, and checks it reproduces every recorded value. It gave the core's `vfpu-segments.hpp`. Needs numpy. |

## Building and running

1. Build with pspdev's toolchain (<https://github.com/pspdev/pspdev>; the `phobos-linux` container has it in
   `/opt/pspdev`, with `psp-config` on the PATH): `make` gives `EBOOT.PBP`. `make SMOKE=1` gives a version for
   trying in an emulator first (PPSSPP's PPSSPPHeadless): it starts round 2 at once, with the big tests cut short,
   and its results say nothing about a PSP.
2. On a PSP with custom firmware that runs homebrew, copy `EBOOT.PBP` to a folder under `PSP/GAME` on the memory
   stick, start it, and press X or O. Keep the charger in.
3. Results go to `results/` beside `EBOOT.PBP`. A round can be stopped and started again: finished tests are
   skipped. The program keeps the PSP awake, and each test's line says "running" as it starts. A test that didn't
   finish runs once more; if it stops the PSP again, the next start gives up on it (`<name>.stopped`) and finishes
   the rest.
4. Copy `results/` back, and run `tools/psp-vfpu-measure/compare.sh <results folder>`.

## What it has given so far

The user ran round 1 on 2026-10-03 (firmware 6.61). From it:

- the random number generator matches every word;
- `vadd`, `vsub`, `vmul` and `vdiv` match every result;
- `vsin` and `vcos` are exact (`fit.py`), and so is `vlog2` below 4.

Round 2, for the rest, is on the user's PSP. Results and findings are in
[docs/psp-vfpu-measurements.md](../../docs/psp-vfpu-measurements.md). The small result files are kept in
`tests/allegrex/measured/`, which the tests check the core against; the big tables stay outside the repository.
