# The PSP's VFPU, measured

What a real PSP's vector unit (the VFPU) computes, recorded so Phobos's PSP core can match it exactly, and shared
so anyone can check it or build on it. Recorded on 2026-10-03 on the user's PSP (firmware 6.61; the program reported
devkit version `06060110`) with [`tools/psp-vfpu-measure`](../tools/psp-vfpu-measure/main.c), and compared with the
core by [`tools/psp-vfpu-measure/compare.sh`](../tools/psp-vfpu-measure/compare.sh).

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
all 27 files (`SHA256SUMS`), the random number and arithmetic files, and the first 16384 results of each exact math
function's spread-out file (`*-spread-16k`), xz-compressed (5.5 MB). The test suite checks the core against them on
every run (`measured.cpp`). The other 21 files (the math functions, 449 MB, about
30 MB compressed) are kept outside the repository by the user's choice; `SHA256SUMS` identifies them, and they can
be shared on request.

## Results

How often the core's result matches the PSP's bit for bit, now that the findings below are applied, and the worst
difference otherwise, in units in the last place (ulps: how many representable floats apart). `vlog2` and `vdot`
are still the core's approximations:

| test | results | exact | worst (ulps) |
| --- | --- | --- | --- |
| vrcp-1-2 | 8388608 | 8388608 (100.00%) | 0 |
| vrsq-1-4 | 16777216 | 16777216 (100.00%) | 0 |
| vsqrt-1-4 | 16777216 | 16777216 (100.00%) | 0 |
| vexp2-1-2 | 8388608 | 8388608 (100.00%) | 0 |
| vrexp2-1-2 | 8388608 | 8388608 (100.00%) | 0 |
| vlog2-half-2 | 16777216 | 80568 (0.48%) | 935274650 |
| vsin-fixed | 8388608 | 8388608 (100.00%) | 0 |
| vcos-fixed | 8388608 | 8388608 (100.00%) | 0 |
| vasin-fixed | 8388612 | 8388612 (100.00%) | 0 |
| vrcp-spread | 1048576 | 1048576 (100.00%) | 0 |
| vnrcp-spread | 1048576 | 1048576 (100.00%) | 0 |
| vrsq-spread | 1048576 | 1048576 (100.00%) | 0 |
| vsqrt-spread | 1048576 | 1048576 (100.00%) | 0 |
| vexp2-spread | 1048576 | 1048576 (100.00%) | 0 |
| vrexp2-spread | 1048576 | 1048576 (100.00%) | 0 |
| vlog2-spread | 1048576 | 572521 (54.60%) | 932532706 |
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

- **The math functions keep 22 bits.** Every finite result of `vrcp`, `vrsq`, `vsqrt`, `vexp2`, `vrexp2`, `vlog2`,
  `vsin` and `vcos` over their full ranges has its two lowest bits zero: 22 of a float's 24 significant bits (98.75%
  of `vasin`'s).
- **They're one quadratic interpolator, and our own data pins it down.** The structure published about the
  hardware (PPSSPP's write-up of fp64's work: 128 segments per function, a linear term over the index's low 16
  bits and a squared term over their top 10, results truncated to 22 bits) fits our measurements exactly, and
  [`tools/psp-vfpu-measure/fit.py`](../tools/psp-vfpu-measure/fit.py) finds every segment's integers from them:
  `vrcp`, `vrsq`, `vsqrt`, `vexp2`, `vcos` and `vasin`, each of their 2^23 results reproduced, with one scale
  (Q = 9) for all. The core uses those tables (`ares/psp/cpu/vfpu-segments.hpp`), not PPSSPP's.
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
- **`vlog2` doesn't fit the model as it is**; it needs more work.
- **`vsin`, `vcos` and `vasin` work on fixed-point arguments,** so tiny inputs give 0 and results step in 2^-23 of
  a quarter turn; and `vsin`/`vcos` of very large inputs give values unrelated to the true sine.
- **`vlog2` near 1** is accurate in absolute terms, not relative ones, hence the large ulp counts for results near
  zero.
- **`vdot` sums its products its own way**, not as a rounded exact sum: about one in five results differ, by up to
  757 ulps when terms cancel.

## Next

`vlog2`, and `vdot`'s summation.
`compare.sh` shows the progress against the full data; `fit.py <results> ares/psp/cpu/vfpu-segments.hpp`
regenerates the tables.

## Reproducing

1. Build the program with pspdev's toolchain (`make` in `tools/psp-vfpu-measure`), copy `EBOOT.PBP` to
   `PSP/GAME/VFPUMEASURE/` on a PSP with custom firmware, and run it; it writes `results/` beside itself (about 450
   MB; resumable).
2. `tools/psp-vfpu-measure/compare.sh <results folder>` prints the table above for that data.
