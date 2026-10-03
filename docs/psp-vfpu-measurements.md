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
all 27 files (`SHA256SUMS`), and the random number and arithmetic files, xz-compressed (5.2 MB). The test suite
checks the core against them on every run (`measured.cpp`). The other 21 files (the math functions, 449 MB, about
30 MB compressed) are kept outside the repository by the user's choice; `SHA256SUMS` identifies them, and they can
be shared on request.

## Results

How often the core's result matched the PSP's bit for bit, after the findings below were applied (the NaN rules),
and the worst difference otherwise, in units in the last place (ulps: how many representable floats apart):

| test | results | exact | worst (ulps) |
| --- | --- | --- | --- |
| vrcp-1-2 | 8388608 | 1294598 (15.43%) | 6 |
| vrsq-1-4 | 16777216 | 2526952 (15.06%) | 7 |
| vsqrt-1-4 | 16777216 | 1352136 (8.06%) | 6 |
| vexp2-1-2 | 8388608 | 1329680 (15.85%) | 5 |
| vrexp2-1-2 | 8388608 | 443655 (5.29%) | 7 |
| vlog2-half-2 | 16777216 | 80568 (0.48%) | 935274650 |
| vsin-fixed | 8388608 | 813071 (9.69%) | 69595 |
| vcos-fixed | 8388608 | 813071 (9.69%) | 69595 |
| vasin-fixed | 8388612 | 1371213 (16.35%) | 333740 |
| vrcp-spread | 1048576 | 175174 (16.71%) | 6 |
| vnrcp-spread | 1048576 | 176474 (16.83%) | 5 |
| vrsq-spread | 1048576 | 606725 (57.86%) | 7 |
| vsqrt-spread | 1048576 | 567868 (54.16%) | 6 |
| vexp2-spread | 1048576 | 723762 (69.02%) | 6 |
| vrexp2-spread | 1048576 | 723376 (68.99%) | 7 |
| vlog2-spread | 1048576 | 572521 (54.60%) | 932532706 |
| vsin-spread | 1048576 | 168297 (16.05%) | 1065353216 |
| vnsin-spread | 1048576 | 167205 (15.95%) | 1065353216 |
| vcos-spread | 1048576 | 593300 (56.58%) | 2130706432 |
| vasin-spread | 1048576 | 535707 (51.09%) | 866317536 |
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
  of `vasin`'s). The core's double-precision results carry every bit; the usual difference is a few ulps.
- **`vsin`, `vcos` and `vasin` work on fixed-point arguments,** so tiny inputs give 0 and results step in 2^-23 of
  a quarter turn; and `vsin`/`vcos` of very large inputs give values unrelated to the true sine.
- **`vlog2` near 1** is accurate in absolute terms, not relative ones, hence the large ulp counts for results near
  zero.
- **`vdot` sums its products its own way**, not as a rounded exact sum: about one in five results differ, by up to
  757 ulps when terms cancel.

## Next

Exact math functions: fitting the PSP's way of computing them (published as one quadratic interpolator per
function over 128 segments, its results truncated to 22 bits) to these results, from our own data rather than
PPSSPP's GPL tables, as the user chose; then `vdot`'s summation, and `vsin`/`vcos` for large inputs. `compare.sh`
shows the progress against the full data.

## Reproducing

1. Build the program with pspdev's toolchain (`make` in `tools/psp-vfpu-measure`), copy `EBOOT.PBP` to
   `PSP/GAME/VFPUMEASURE/` on a PSP with custom firmware, and run it; it writes `results/` beside itself (about 450
   MB; resumable).
2. `tools/psp-vfpu-measure/compare.sh <results folder>` prints the table above for that data.
