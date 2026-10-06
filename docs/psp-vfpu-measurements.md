# The PSP's VFPU, measured

What a real PSP's vector unit (the VFPU) computes, recorded so Phobos's PSP core can match it exactly, and shared
so anyone can check it or build on it. Recorded on the user's PSP (firmware 6.61; the program reported devkit version
`06060110`) with [`tools/psp-vfpu-measure`](../tools/psp-vfpu-measure/main.c): round 1 on 2026-10-03, round 2 on
2026-10-04 (below, with round 1 run again), and compared with the core by
[`tools/psp-vfpu-measure/compare.sh`](../tools/psp-vfpu-measure/compare.sh).

## What was recorded

`manifest.txt` describes every file's inputs; the files hold only the PSP's results, as raw little-endian 32-bit
words, in input order. 27 files, 455 MB:

- **Math functions over every input of the range each reduces its argument to:** `vrcp` over [1, 2), `vrsq` and
  `vsqrt` over [1, 4), `vexp2` and `vrexp2` over [1, 2), `vlog2` over [1/2, 2), and `vsin`, `vcos` and `vasin` over
  k / 2^23 (a quarter turn in 2^23 steps). These are the `*-1-2`, `*-1-4`, `*-half-2` and `*-fixed` files.
- **A million spread-out inputs for each math function** and the negated forms (`vnrcp`, `vnsin`): every sign,
  size, zero, denormal, infinity and NaN (`*-spread`).
- **Arithmetic on spread-out inputs:** `vadd`, `vsub`, `vmul`, `vdiv` (2^18 lanes each) and `vdot` (2^18 dot
  products of quads).
- **The random number generator** (`vrnd.bin`): its state and 256 numbers at start, then for 64 seeds the state
  after `vrnds` and 4096 numbers, then `vrndi.q`'s lane order.

Spread-out inputs come from `state = state * 1664525 + 1013904223`, taking each new state, from each test's seed.

**In the repository:** [`tests/allegrex/measured/`](../tests/allegrex/measured/) has `manifest.txt`, the SHA-256 of
all 27 files (`SHA256SUMS`), the random number and arithmetic files, the first 16384 results of each math
function's spread-out file but `vdot`'s (`*-spread-16k`), and every 1024th result of `vlog2` from 1/2 up to 2
(`vlog2-half-2-1k`), xz-compressed (5.5 MB). The test suite checks the core against them on
every run (`measured.cpp`). The other 21 files (the math functions, 449 MB, about
30 MB compressed) are kept outside the repository by the user's choice; `SHA256SUMS` identifies them, and they can
be shared on request.

## Results

How often the core's result matches the PSP's bit for bit, now that the findings below are applied, and the worst
difference otherwise, in units in the last place (ulps: how many representable floats apart). `vlog2` from 4 up
and `vdot` are still the core's approximations:

| test | results | exact | worst (ulps) |
| --- | --- | --- | --- |
| vrcp-1-2 | 8388608 | 8388608 (100.00%) | 0 |
| vrsq-1-4 | 16777216 | 16777216 (100.00%) | 0 |
| vsqrt-1-4 | 16777216 | 16777216 (100.00%) | 0 |
| vexp2-1-2 | 8388608 | 8388608 (100.00%) | 0 |
| vrexp2-1-2 | 8388608 | 8388608 (100.00%) | 0 |
| vlog2-half-2 | 16777216 | 16777216 (100.00%) | 0 |
| vsin-fixed | 8388608 | 8388608 (100.00%) | 0 |
| vcos-fixed | 8388608 | 8388608 (100.00%) | 0 |
| vasin-fixed | 8388612 | 8388612 (100.00%) | 0 |
| vrcp-spread | 1048576 | 1048576 (100.00%) | 0 |
| vnrcp-spread | 1048576 | 1048576 (100.00%) | 0 |
| vrsq-spread | 1048576 | 1048576 (100.00%) | 0 |
| vsqrt-spread | 1048576 | 1048576 (100.00%) | 0 |
| vexp2-spread | 1048576 | 1048576 (100.00%) | 0 |
| vrexp2-spread | 1048576 | 1048576 (100.00%) | 0 |
| vlog2-spread | 1048576 | 926394 (88.35%) | 4 |
| vsin-spread | 1048576 | 1048576 (100.00%) | 0 |
| vnsin-spread | 1048576 | 1048576 (100.00%) | 0 |
| vcos-spread | 1048576 | 1048576 (100.00%) | 0 |
| vasin-spread | 1048576 | 1048576 (100.00%) | 0 |
| vadd-spread | 262144 | 262144 (100.00%) | 0 |
| vsub-spread | 262144 | 262144 (100.00%) | 0 |
| vmul-spread | 262144 | 262144 (100.00%) | 0 |
| vdiv-spread | 262144 | 262144 (100.00%) | 0 |
| vdot-spread | 262144 | 212363 (81.01%) | 757 |

The random number generator: all 263,944 words match (the start, 64 seeds of 4096 numbers, and `vrndi.q`).

## Findings

- **The random number generator is exactly the model fp64 published** (PPSSPP issue 16946), which the core
  implements: all 64 seeds, including those whose numbers exercise its carry, which had only been fitted to his
  data before.
- **`vadd`, `vsub`, `vmul`, `vdiv` are IEEE single precision rounded to the nearest, with denormals as zero**, as
  the core already computed: every non-NaN result matched.
- **The VFPU never passes a NaN through.** Every NaN result is `0x7f800001`; only its sign varies, by instruction.
  The core does this now (`NaNSign` in `ares/psp/cpu/allegrex.hpp`):

  | instruction | sign of a NaN result |
  | --- | --- |
  | `vadd`, `vsub`, `vexp2`, `vrexp2`, `vcos`, `vdot` | always positive |
  | `vsqrt`, `vlog2` | always positive, including for negative inputs |
  | `vmul`, `vdiv` | negative when exactly one input is |
  | `vrcp`, `vsin`, `vasin`, `vrsq` | the input's (so `vrsq` and `vasin` of a negative number give a negative NaN) |
  | `vnrcp`, `vnsin` | the opposite of the input's |

- **The math functions keep 22 bits.** Every finite result of `vrcp`, `vrsq`, `vsqrt`, `vexp2`, `vrexp2`, `vsin`
  and `vcos` over their full ranges has its two lowest bits zero: 22 of a float's 24 significant bits (98.75% of
  `vasin`'s). `vlog2` is fixed point instead (below).
- **They're one quadratic interpolator, and our own data pins it down.** The structure published about the
  hardware (PPSSPP's write-up of fp64's work: 128 segments per function, a linear term over the index's low 16
  bits and a squared term over their top 10, results truncated to 22 bits) fits our measurements exactly, and
  [`tools/psp-vfpu-measure/fit.py`](../tools/psp-vfpu-measure/fit.py) finds every segment's integers from them:
  `vrcp`, `vrsq`, `vsqrt`, `vexp2`, `vcos`, `vasin` and `vlog2`, each of their 2^23 results reproduced, with one
  scale (Q = 9) for all. The core uses those tables (`ares/psp/cpu/vfpu-segments.hpp`), not PPSSPP's.
- **Some functions are others read differently.** `vsqrt` and `vrsq` ignore their input's lowest bit (pairs of
  inputs give the same result over [1, 4)); `vrexp2` reads `vexp2`'s table backwards, its fraction's bits
  inverted, and so does `vexp2` for negative inputs: both are one 2^y; the table behind sine and cosine is
  cosine's, sine being it read backwards (`vsin` of k is `vcos` of 2^23 - k); `vnrcp` and `vnsin` are the
  negations.
- **Edges:** the arguments become 23-bit fixed point by dropping the rest; `vsqrt(-0)` is +0; `vrexp2(0)` and
  `vexp2(0)` are exactly 1; `vasin(1)` is exactly 1 and past 1 is NaN.
- **Sine and cosine of very large arguments** (2^32 quarter turns and up) come out as if the argument were much
  smaller: the shift that turns the argument into fixed point has only 5 bits, so it wraps around past 2^31, and a
  shift of exactly 32, 64 or 96 shifts everything out, leaving 0 (sine 0, cosine 1). That reproduces every one of
  the million spread-out results.
- **`vlog2` works in fixed point.** Its table holds log2 over [1, 2) in units of 2^-24 in every segment (that fits
  the interpolator too), and the input's exponent p is added to it as a whole number:
  - From 1 up, the result is truncated to 22 bits after the point, and to 23 significant bits once p needs some
    of them. That reproduces every result from 1 up to 2.
  - Below 1 the PSP takes a cheaper path: a straight line through each segment (the table's first value cut to 17
    bits after the point, its slope missing its low 9 bits, no squared term), and the magnitude |log2 x| truncated
    to 15 bits after the point whatever its size. So log2 of numbers just below 1 is -0, and of 1/2 plus a little
    is -1 + 2^-15. That reproduces every result from 1/2 up to 1, and every spread-out one below 1.
  - From 4 up, about half the results are one unit (of those 23 bits) less than that reading gives: the PSP drops
    more precision there, in a way the spread-out results don't pin down.
  - Zero and denormals give -infinity, and negative numbers NaN.
- **`vsin`, `vcos` and `vasin` work on fixed-point arguments,** so tiny inputs give 0 and results step in 2^-23 of
  a quarter turn; and `vsin`/`vcos` of very large inputs give values unrelated to the true sine.
- **`vdot` sums its products its own way**, not as a rounded exact sum: about one in five results differ, by up to
  757 ulps when terms cancel.

## Round 2 (2026-10-04)

### What was recorded

`manifest2.txt` describes every file's inputs. 18 files, 238 MB:

- **`vlog2` over a whole binade for each size its results take above 4:** from 4, 16, 2^8, 2^16, 2^32 and 2^64,
  every input of the binade (`vlog2-4-8` and on).
- **Dot products, sums and averages built to show how the VFPU adds several numbers:** one product (the other lanes
  zero), two, four of similar size, exact products of short significands (`vdot-*`), and the same for `vhdp`,
  `vfad` (sums) and `vavg` (averages).
- **Every half float through `vh2f`** (`vh2f-all`) and a million floats through `vf2h` (`vf2h-spread`).
- **`div` and `divu`** of 32 chosen numbers each with each (division by zero included), then spread-out pairs
  (`ipu-divide`).
- **The instruction recorder** (`ops.bin`, `ops.txt`): every VFPU instruction pspdev's assembler knows, at each size,
  and 240 random prefix combinations among them: 1216 entries, each run on 64 random register states.
- **The FPU's conversions and arithmetic in each rounding mode** (`fpu-convert`, `fpu-arith`) were not recorded: each
  switched the PSP off twice, and the program gave up on them (below).

Round 1 was run again in the same session: all 26 of its result files came out identical to round 1's, byte for byte
(only `manifest.txt`'s first line differs, its wording changed since): the PSP gives the same answers every time.

**In the repository:** [`tests/allegrex/measured/`](../tests/allegrex/measured/) has `manifest2.txt`, `ops.txt` (the
recorder's entries) and the SHA-256 of all 18 files (`SHA256SUMS2`). The files themselves are kept outside the
repository, like round 1's big files, but for `ipu-divide.bin` (packed with xz, 100 KB), which the host tests replay.

### Results

| test | results | exact | worst (ulps) |
| --- | --- | --- | --- |
| vlog2-4-8 | 8388608 | 6075144 (72.42%) | 2 |
| vlog2-16-32 | 8388608 | 5070999 (60.45%) | 4 |
| vlog2-256-512 | 8388608 | 4446697 (53.01%) | 2 |
| vlog2-2p16 | 8388608 | 3920844 (46.74%) | 4 |
| vlog2-2p32 | 8388608 | 4029242 (48.03%) | 4 |
| vlog2-2p64 | 8388608 | 4654070 (55.48%) | 2 |
| vdot-one | 262144 | 171530 (65.43%) | 1 |
| vdot-two | 1048576 | 717271 (68.40%) | 192534 |
| vdot-close | 1048576 | 663010 (63.23%) | 61598 |
| vdot-short | 262144 | 207603 (79.19%) | 11776 |
| vhdp-close | 262144 | 159966 (61.02%) | 85515 |
| vfad-close | 1048576 | 608721 (58.05%) | 196608 |
| vfad-two | 262144 | 169417 (64.63%) | 32 |
| vavg-close | 262144 | 152382 (58.13%) | 16384 |
| vh2f-all | 65536 | 65536 (100.00%) | 0 |
| vf2h-spread | 524288 | 524288 (100.00%) | 0 |
| div (lo and hi) | 8192 | 8192 (100.00%) | |
| divu hi | 8192 | 8192 (100.00%) | |
| divu lo | 8192 | 8181 (99.87%) | |

The instruction recorder: 963 of the 1216 entries match in every one of their 64 runs; the other 253 differ in at
least one.

### Findings

- **`vh2f` and `vf2h` are exactly what the core does:** every half float, and every one of the million floats.
- **Division by zero doesn't trap, and gives fixed results:** `div` gives LO -1 for a dividend of 0 or more and +1
  for a negative one, `divu` gives LO 0x0000ffff for a dividend below 0x10000 and 0xffffffff otherwise; HI is the
  dividend for both. The core had `divu`'s LO always 0xffffffff, which is the 11 differences above; it follows the
  PSP now, and the host tests replay all 8192 pairs.
- **The FPU stops the PSP on some inputs.** `fpu-convert` (`cvt.w.s`, `round.w.s`, `trunc.w.s`, `ceil.w.s` and
  `floor.w.s` in each rounding mode) and `fpu-arith` (`add.s`, `sub.s`, `mul.s`, `div.s`, `sqrt.s`) each switched
  the PSP off twice, at 333 MHz and at the "auto" clock alike, and neither wrote a result. Both start with special
  values: the conversions with the 32 specials (zeros, denormals, infinities, NaNs, numbers past the integer
  range), the arithmetic with a mix in which they come up early. The exception enables were cleared for each
  instruction, so it isn't an enabled trap. The likeliest reading is the MIPS "unimplemented operation" exception,
  which can't be masked and expects the operating system to finish the operation in software; the PSP's doesn't,
  for a program. If so, no game can feed those values to those instructions, and what an emulator returns for them
  matters little. Which values exactly needs a probe that tries them one kind at a time.
- **`vlog2` above 4 loses precision in a way these binades can now pin down:** from 47% to 72% exact, at most 4 ulps
  off. The data is there for the fit (as `fit.py` did for 1 up to 2).
- **The adders aren't IEEE, even with one term.** A single product (`vdot-one`) is exact only 65% of the time, off by
  one ulp otherwise, so even one product isn't rounded to the nearest; two terms (`vfad-two`) are 65% exact. These
  files are built to show the products' precision and how the sum is rounded; that's the next fit.
- **What the recorder found**, by kind (from each differing entry's first difference, checked against its inputs in
  `ops.bin`):
  - **Matrices the other way round:** `vmmul.q M200, M000, M100` gives M000 × M100, reading C registers as columns
    (result column c, row r is the sum over k of M000's column k, row r times M100's column c, row k); the core
    gives M100 × M000. `vtfm` and `vhtfm` dot each column of the matrix with the vector, where the core dots each
    row. All of the PSP's finite results fit these (up to rounding). Both are what the core would give if it read
    matrix operands the other way round (bit 5, transposed, taken the opposite way), which changes nothing for moves
    and element-wise instructions. These entries differed in 59 to 64 of their 64 runs, and as games transform
    vertices with `vtfm`, they mattered most. **Fixed:** the core computes all three as the PSP does now, and every
    run without a NaN or infinity among its inputs matches, up to the adders' rounding.
  - **NaN results:** in 100 entries the PSP gives its own NaN (`0x7f800001`, or `0xff800001`) where the core doesn't.
    In 43 the core passes another NaN through: `vocp`, `vscl`, `vmscl`, `vhdp`, `vdet`, `vcrs`, `vqmul`, `vbfy1`,
    `vbfy2`, `vfad`, `vavg`, `vmmul`, `vhtfm4` and 20 prefixed entries. In 57 it gives a number: `vrot` (46
    entries), `vsin`, `vcos` and `vnsin` of an infinity, `vsocp` of a NaN, `vtfm3` and `vtfm4` with a NaN in the
    matrix (some of the matrix ones may be the orientation above), and one prefixed entry. Round 1's rule ("the VFPU
    never passes a NaN through") holds for all of them, and an infinity has no sine or cosine. The sign, checked on
    every NaN recorded: the product of the two inputs' signs for `vscl`, `vmscl` and `vcrs` (as for `vmul`), the
    input's for `vsin`, its opposite for `vnsin`, positive for all the others.
  - **Denormals kept:** in 59 entries the PSP keeps a denormal's bits where the core flushes it to zero: moves
    (`vmov`), `vmin`, `vmax`, `vsat0`, `vsat1`, the sorts, and prefixed instructions. Arithmetic flushes denormals
    (round 1); moving, comparing and clamping evidently don't. `vsbz` leaves one as it is too (the core gave
    1.mantissa).
  - **NaNs in comparisons, and how the comparisons order:** `vmin`, `vmax`, the sorts (`vsrt1` to `vsrt4`), `vsgn`
    and `vscmp` all order values the same way, every recorded run agreeing: as numbers, but with denormals counting
    as zero (and -0 as +0) and a NaN past the infinity of its sign. That's the order of the bits as a sign and a
    magnitude, the denormals squashed to zero. They give back the lanes themselves, so a denormal that wins comes out
    as it went in. On a tie (two zeros or denormals), `vmin` and `vmax` give t's lane, and the sorts, working out
    each lane of a pair on its own, give both lanes the same one: the pair's first for `vsrt1` and `vsrt2`, its
    second for `vsrt3` and `vsrt4` (so two different denormals can come out as one, twice). `vsgn` and `vscmp`
    therefore give a NaN its sign (±1, where the core gave 0), and a denormal 0, as the core did: an earlier version
    of this page had them seeing denormals, which the inputs in `ops.bin` don't bear out. `vsat0` and `vsat1` pass a
    NaN through unchanged and keep what's already in range; `vsat0` makes anything with its sign bit set 0, -0 and
    negative denormals included. `vlgb` of a NaN gives a NaN of the same sign with the input's low byte moved up 16
    bits (`0xffd20000` for `0xfffffa52`; all 7 recorded).
  - **Fixed:** the core follows all of the above now (`vfpuOrder` and the instructions in
    `ares/psp/cpu/interpreter-vfpu.cpp`), with a test of each rule (`vfpu edges` in `tests/allegrex/vfpu.cpp`). The
    recorder then matches in 1129 of its 1216 entries, and 38 more differ only by the adders' rounding; what's left
    is the prefixed entries and the adders.
  - **Prefixes:** 123 of the differing entries are the random prefix combinations; in 6 of them the core leaves a lane
    unwritten that the PSP writes (on `vrcp.q` with source and destination prefixes), so the core applies some
    prefixes differently from the hardware.
  - **Rounding:** the rest are the adders (`vdot`, `vhdp`, `vfad`, `vavg`, `vcrsp`, by an ulp) and `vlog2` above 4
    (by 2), as above.

## Next

- From round 2 (the matrix operands' orientation, `divu`'s LO by zero, the NaN results, kept denormals and the
  comparisons' order are done): the prefix differences (one entry at a time, against `ops.bin`); then `vlog2` above
  4, and the adders' model.
- The FPU: a probe that tries one kind of value at a time, to find what the PSP refuses; then the FPU tests without
  them.

`compare.sh` shows the progress against the full data; `fit.py <results> ares/psp/cpu/vfpu-segments.hpp` regenerates
the tables.

## Reproducing

1. Build the program with pspdev's toolchain (`make` in `tools/psp-vfpu-measure`), copy `EBOOT.PBP` to
   `PSP/GAME/VFPUMEASURE/` on a PSP with custom firmware, and run it, pressing O for round 1 (about 450 MB) or X for
   round 2 (about 240 MB); it writes `results/` beside itself, and can be started again: finished tests are skipped,
   and a test that stops the PSP twice is given up on (see its [README](../tools/psp-vfpu-measure/README.md)).
2. `tools/psp-vfpu-measure/compare.sh <results folder>` prints the tables above for that data.
