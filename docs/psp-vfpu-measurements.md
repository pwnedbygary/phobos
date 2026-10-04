# The PSP's VFPU, measured

What a real PSP's vector unit (the VFPU), and its FPU, compute, recorded so Phobos's PSP core can match them exactly,
and shared so anyone can check them or build on them. Recorded on the user's PSP (firmware 6.61; the program reported
devkit version `06060110`) with `tools/psp-vfpu-measure` (since 2026-10-04 the VFPU and FPU half of
[`tools/psp-measure`](../tools/psp-measure/vfpu.c)): round 1 on 2026-10-03, round 2 on 2026-10-04 (below, with
round 1 run again), round 3 later that day (with rounds 1 and 2 again), and compared with the core by
[`tools/psp-measure/compare.sh`](../tools/psp-measure/compare.sh).

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
  [`tools/psp-measure/fit.py`](../tools/psp-measure/fit.py) finds every segment's integers from them:
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
  matters little. Which values exactly needs a probe that tries them one kind at a time. (Round 3 disproved this
  reading: in the third session both tests finished, no probe stopped the PSP or raised the unimplemented-operation
  cause, and the results are IEEE's. See "Round 3".)
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
    `ares/psp/cpu/interpreter-vfpu.cpp`), with a test of each rule (`vfpu edges` in `tests/allegrex/vfpu.cpp`). With
    them the recorder matched 1129 of its 1216 entries, and 38 more only by the adders' rounding; the prefixed
    entries were left, below.
  - **Prefixes:** 123 of the differing entries are the random prefix combinations. With the rules above in, 46 still
    differed beyond rounding, and they come down to a few things the hardware does, each holding in every entry that
    shows it:
    - `vrcp.q` takes the prefixes on its last lane alone, with lane 0's settings, as `vrndi` does with its
      destination prefix: lanes 0 to 2 are the plain reciprocals of their inputs, unclamped and unmasked, and lane
      3 is masked or clamped by lane 0's destination settings. A constant in lane 0's source setting gives lane 3
      that constant's reciprocal (5 entries), and a setting naming another lane gives 0 (3 entries), as though the
      lanes were worked out one at a time from the last, only the first step seeing the prefixes, and that step's
      input were its own lane alone. How the other math functions (`vrsq`, `vsin` and the rest) take prefixes
      wasn't recorded.
    - `vfad` is a dot product with t forced to constants: each lane weighed by 1, or 1/3 where the t prefix sets the
      absolute bit, negated where it sets negate (the constant bit and a swizzle of 1 forced, the rest kept). So a t
      prefix changes `vfad`, which has no t operand.
    - `vhdp` forces s's last lane to such a constant in the same way (1, or 1/3 with the absolute bit, and its sign).
    - `vscl`'s t prefix takes rt in every lane (its swizzle ignored), its other settings applying lane by lane.
    - A swizzle that reaches past the operand's size (lane 2 of a pair, lane 3 of a triple) gives 0 as that lane's
      result, whatever the instruction (`vmov.p`, `vmov.t`, `vadd.s`; and `vrcp`'s last step above); the
      instruction I first read as `vmov.s` is `vmov.t`, whose lanes 1 and 2 are in range.
  - **Fixed:** the core takes prefixes this way now (`vrcp`, `vfad`, `vhdp`, `vscl`, and the out-of-range rule for
    the instructions that work lane by lane), with a test of each from a recorded run. The recorder then matches in
    1157 of its 1216 entries; 49 more differ only by the adders' rounding, and the last 10 by the adders' rounding
    beyond 4 ulps (where terms cancel), and one `vscl` lane whose product lies just below the smallest normal number,
    which the PSP gives as 0 and the core, rounding it as an IEEE denormal, as the smallest normal (round 3 explains
    it: see "Round 3").
  - **Rounding:** the rest are the adders (`vdot`, `vhdp`, `vfad`, `vavg`, `vcrsp`, by an ulp) and `vlog2` above 4
    (by 2), as above.

## Round 3 (2026-10-04)

### What was recorded

A third session ran [`tools/psp-measure`](../tools/psp-measure/README.md), the two programs made one, on the same PSP:
round 3, the FPU probes, and rounds 1 and 2 again. `manifest3.txt` describes round 3's files and the probes:

- **The FPU as a program finds it** (`fpu-state`): FCSR, its control and status register, and FIR, which says what
  FPU it is.
- **The FPU on safe inputs** (`fpu-convert-safe`, `fpu-arith-safe`): the conversions and the arithmetic in each
  rounding mode, on zeros and normal numbers only.
- **Products a sliver below the smallest normal number** (`vmul-tiny`): 2^-126 (1 - j² 2^-46) for j from 1 to 4096.
- **A second recorder list** (`ops3.bin`, `ops3.txt`): 284 entries, the math functions with prefixes, swizzles past an
  operand's size in instructions that don't work lane by lane, and `vavg` and `vfad` with t prefixes.
- **Seventeen FPU probes** (`probe-*`), one value each.

And from round 2, **the FPU's conversions and arithmetic in each rounding mode on every kind of input**
(`fpu-convert`, `fpu-arith`), which had switched the PSP off twice each in the second session, finished this time.
Why isn't known: the program's FPU tests have cleared FCSR's exception enables around each instruction they measure
since round 2's first version. Nothing was given up on, and no probe stopped the PSP. Every repeated result file of
rounds 1 and 2 came out identical to the earlier sessions', byte for byte, again (the manifests differ only in their
wording).

**In the repository:** [`tests/allegrex/measured/`](../tests/allegrex/measured/) has `manifest3.txt`, `ops3.txt` and
the SHA-256 of every file new in this session (`SHA256SUMS3`: round 3's files, the probes, and round 2's two FPU
files), and, packed with xz (360 KB), `fpu-state`, the four FPU files and `vmul-tiny`, for the host tests to replay.
`ops3.bin` (3.7 MB) stays outside the repository, like `ops.bin`.

### Results

| test | results | exact | worst (ulps) |
| --- | --- | --- | --- |
| vmul-tiny | 262144 | 223744 (85.35%) | (0 where the core gives 2^-126) |
| fpu-convert-safe: each conversion, each rounding mode | 4096 | 4096 (100.00%) | 0 |
| fpu-arith-safe: each operation, rounding to nearest | 4096 | 4096 (100.00%) | 0 |
| fpu-arith-safe: each operation, the other three modes | 4096 | 49.05% to 64.65% | 1 |
| fpu-convert: each conversion, each rounding mode | 4096 | 3930 (95.95%) | |
| fpu-arith: each operation, rounding to nearest | 4096 | 4096 (100.00%) | 0 |
| fpu-arith: each operation, the other three modes | 4096 | 52.42% to 80.08% | 1 |

The second recorder list: 36 of its 284 entries match in every run, and 7 more differ only by rounding (198 and
54 since: see the findings).

The probes (each run once in rounding mode 0, FCSR's flags, enables and causes cleared, flush to zero off but in
the -fs ones). The core's results already match:

| probe | a, b | result | FCSR after |
| --- | --- | --- | --- |
| div.s 1 / 0 | 3f800000, 00000000 | 7f800000 | 00008020 (divide by zero) |
| mul.s 2^126 × 2^126 | 7e800000, 7e800000 | 7f800000 | 00004010 (overflow) |
| sqrt.s -1 | bf800000 | 7fc00000 | 00010040 (invalid) |
| add.s infinity + 1 | 7f800000, 3f800000 | 7f800000 | 00000000 |
| add.s 0x7fbfffff + 1 | 7fbfffff, 3f800000 | 7fffffff | 00010040 (invalid) |
| add.s 0x7fc00000 + 1 | 7fc00000, 3f800000 | 7fc00000 | 00000000 |
| cvt.w.s -2^31 | cf000000 | 80000000 | 00000000 |
| cvt.w.s infinity | 7f800000 | 7fffffff | 00010040 (invalid) |
| cvt.w.s 0x7fbfffff | 7fbfffff | 7fffffff | 00010040 (invalid) |
| cvt.w.s 0x7fc00000 | 7fc00000 | 7fffffff | 00010040 (invalid) |
| cvt.w.s 2^31 | 4f000000 | 7fffffff | 00010040 (invalid) |
| mul.s 2^-100 × 2^-30 | 0d800000, 30800000 | 00080000 | 00002008 (underflow) |
| add.s the smallest denormal + 1 | 00000001, 3f800000 | 3f800000 | 00001004 (inexact) |
| cvt.w.s the smallest denormal | 00000001 | 00000000 | 00001004 (inexact) |
| the three above with flush to zero | | 00000000, 3f800000, 00000000 | 01002008, 01001004, 01001004 |

### Findings

- **FCSR starts as 0x00000e00:** rounding to nearest, flush to zero off, and the overflow, divide-by-zero and
  invalid exceptions enabled (bits 9 to 11), so an FPU instruction that overflows, divides by zero or is invalid
  traps unless the program turns them off. FIR is 0x00003351.
- **NaNs follow IEEE 754-2008's encoding, not MIPS's older one:** 0x7fc00000, whose top fraction bit is set, is
  quiet and passes through `add.s` silently; 0x7fbfffff is signaling, raises invalid, and comes back quieted
  (0x7fffffff: its payload with the quiet bit set); an invalid operation with no NaN going in (`sqrt.s -1`) gives
  0x7fc00000. (The probes' own labels, written before, have it the other way round.)
- **The exception bits:** invalid sets cause bit 16 and flag bit 6, divide by zero 15 and 5, overflow 14 and 4
  (without inexact), underflow 13 and 3 (even for an exact result: 2^-130 came back exactly, as a denormal), inexact
  12 and 2.
- **Denormals:** with flush to zero off the FPU computes with them and gives them; with it on (bit 24), 2^-130
  becomes 0. A denormal going in is used as it is, with no exception of its own.
- **The rounding modes:** `add.s`, `sub.s`, `mul.s`, `div.s` and `sqrt.s` follow FCSR's rounding mode, as IEEE says
  (1 - 1 rounding down is -0). The core rounds to nearest whatever the mode, so in the other three it's an ulp off
  in 35% to 51% of the safe results and 20% to 48% of `fpu-arith`'s. The conversions already follow the mode in the
  core.
- **Conversions out of range:** `cvt`, `round`, `trunc`, `ceil` and `floor.w.s` give 0x7fffffff for large positive
  numbers, infinity and NaNs (raising invalid, the probes show), and 0x80000000 for large negative numbers and
  -infinity, where the core gives 0x7fffffff.
- **Fixed since, in the core (the FPU findings above):** its arithmetic rounds as FCSR's mode says (the host's
  rounding switched for the one operation when it isn't the nearest; `cvt.s.w` too, as MIPS documents, unmeasured);
  the NaN an operation gives is picked as the PSP's is (the first signaling NaN made quiet, else the first quiet one,
  else 0x7fc00000), since x86 hosts give others; its conversions saturate by sign; FIR reads 0x00003351; and a thread
  starts with FCSR 0x00000e00. All four FPU files now match in every result, every mode, on ARM and x86 hosts alike
  (the host tests replay them). FCSR's exception bits are still kept but not set or acted on.
- **Tiny products:** the VFPU rounds a product to 24 bits first, as if exponents had no lower limit, then flushes
  it to 0 if it's below 2^-126. Of the products 2^-126 (1 - j² 2^-46), those up to j = 1448 round to 2^-126 and are
  kept, and the rest are 0 (35.35% of the inputs, every one as measured). The core rounds as IEEE's denormals would
  before flushing, so it also keeps 2^-126 from j = 1449 to 2048. Round 2's one `vscl` lane fits too: its product,
  2^-126 - 2^-150, is exact at 24 bits and below 2^-126, so the PSP flushes it to 0, where IEEE's denormal rounding (a
  tie, to even) takes the core to 2^-126. **Fixed since:** the core rounds every VFPU result this way: `vmul`,
  `vscl`, `vmscl` and `vcrs` now hand it their exact products, and `vdiv` its quotient to a double's 53 bits (taken to
  round the same way, not measured). So `vmul-tiny` matches in every result (the host tests replay it), and round
  2's recorder list gains its `vscl` entry (1158 of 1216 exact).
- **The second recorder list** matched in 36 of its 284 entries at first. Fitting it gave these rules, now in the
  core, which bring it to 198 exact and 54 more differing only by rounding:
  - **The math functions** (`vrcp`, `vrsq`, `vsqrt`, `vsin`, `vcos`, `vasin`, `vexp2`, `vlog2`, `vnrcp`, `vnsin`,
    `vrexp2`) all take prefixes as round 2 found `vrcp` does: worked out a lane at a time from the last lane back,
    only the last lane's step sees the prefixes, with lane 0's settings (a constant if it names one; the lane's own
    input, its absolute value too, with a swizzle of 0; 0 if it names another lane), and lane 0's clamp and write mask
    go to the last lane alone. `vnrcp`, `vnsin` and `vrexp2` ignore the negate setting: `vnrcp` of a negated 1/6
    gives -5.999998 (minus its reciprocal), and `vnsin` of a negated 3 gives -sin(3 quarter turns), which is 1.
  - **The adders add up four lanes, whatever the size.** `vdot`, `vhdp`, `vfad` and `vavg` read all four lanes, each
    through its own prefix settings, and leave out the product of a lane whose swizzle names a lane past the size
    (one within the size too). With the prefixes at rest, the lanes past the size name lanes past the size, so
    nothing changes; but a constant there, or a swizzle back into the operand, adds to the sum. A `vfad.p` whose s
    prefix puts its own two lanes past the size and the constant 1 in lane 3 gives 1 in every run.
  - **`vavg`'s t prefix only negates:** a lane whose t prefix sets negate is subtracted, and its absolute and constant
    settings change nothing (fitting a weight per lane, the constants of either sign, against every run showed it).
    The sum is divided by the size, which fits the PSP better than weighing each lane by the constant 1/3 does (round
    2's unprefixed `vavg.t` loses 5 runs that way). `vfad`'s t prefix keeps round 2's rule (1, or 1/3 with the
    absolute bit, negated with the negate bit).
  - **A swizzle past the operand's size** also gives 0 in `vf2i`'s, `vi2f`'s and `vbfy1`'s lane, as in the
    instructions that work lane by lane (for `vbfy1`, seen with both lanes of a pair past it), a half float of 0 in
    `vf2h` whatever the negate setting, and in `vh2f` a 0 that the negate setting still makes -0 before it's split
    (so its second float is -0).

  The 54 that differ only by rounding are the adders' (39) and `vlog2`'s above 4 (15): see Next, below. Still open, 32
  entries, all from the part of the list built to put swizzles past an operand's size into instructions that combine
  lanes, which compilers don't produce: `vavg` (8: where large terms cancel, the PSP loses the small terms' low bits
  or the small terms entirely, so it can be far from the core: halving 1.25e35 + 1/2 - 1.25e35 - 2, it gives 0 where
  the core gives -1; the adders' fit should explain it), `vcmp` (8: given a destination prefix too, it changes
  condition bits of lanes its write mask leaves out, with no rule seen yet), and `vcrs`, `vcrsp`, `vdet` and `vsocp`
  (4 each), which reading such a lane as 0 doesn't fit.

## Next

- From round 2: `vlog2` above 4, and the adders' model (`vdot`, `vhdp`, `vfad`, `vavg`, `vcrsp`, `vdet`, `vqmul` and
  the matrix products).
- From round 3: the second recorder list's 32 open entries (swizzles past the size, above), if a game ever needs them.
  The GE's round 3 is in [psp-core.md](psp-core.md) ("Round 3's results").
- For a next round: flush to zero in the directed rounding modes (MIPS documents the smallest normal number when
  rounding toward it; the core gives 0 in every mode), and `cvt.s.w` in each mode.

`compare.sh` shows the progress against the full data; `fit.py <results> ares/psp/cpu/vfpu-segments.hpp` regenerates
the tables.

## Reproducing

1. Build the program with pspdev's toolchain (`make` in `tools/psp-measure`), copy `EBOOT.PBP` to
   `PSP/GAME/PSPMEASURE/` on a PSP with custom firmware, and run it, choosing round 1 (about 450 MB), round 2 (about
   230 MB), round 3 (about 6 MB) or the FPU probes from its menu. It writes `results/vfpu` beside itself, and a round
   can be started again: finished tests are skipped, a test that stops the PSP twice is given up on, and so is a
   probe that stops it once. Starting afresh keeps the old results under another name (see its
   [README](../tools/psp-measure/README.md)).
2. `tools/psp-measure/compare.sh <results folder>/vfpu` prints the tables above for that data.
