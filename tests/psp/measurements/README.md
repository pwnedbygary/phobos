# Measurements from a real PSP

What the owner's own PSP drew, read back and saved, for the PSP core's tests to compare against. These are the
measured truth the GE is fitted to. Never edit them: a new measurement round goes in a folder of its own.

## `ge-round3/`

The GE tests of `tools/psp-measure` (`ge.c`), round 3, run on the owner's PSP (a PSP-1000 with 32 MiB, running ARK
custom firmware on Sony's 6.60/6.61, the console of every round) on 2026-10-04. Each `.bin` is one test's target
pixels after it drew. `manifest3.txt` (and `manifest.txt`, from the same program) describes the layout of each file
and what each test draws.

`tests/psp/measure.cpp` runs `pspmeasure.elf` (`tests/psp/programs`) in the core and compares each result with these.
`tests/psp/run-tests.sh` uses this folder unless `PSP_GE_RESULTS` names another. `docs/psp-core.md` (part 10,
"Measuring the GE and the controller on a PSP", and the parts after it) says what each round found.

## `ge-round6/`

Round 6 of the same program's GE tests (its menu's first line), run on the same PSP and brought in on 2026-10-11: 36
files, 28 pictures (256x256, a 32-bit word a pixel), seven timing files (`time-*`: lists' times in microseconds, at
222/111 and then 333/166 MHz) and the driver's statuses (`ge-statuses`), laid out as `manifest6.txt` describes.
`docs/psp-core.md` says what each test is for (part 66) and what the PSP showed (parts 75 and 76). Each `.bin` is
packed with xz (`xz -9e`, 7.3 MB as 1.5 MB), as `tests/allegrex/measured`'s are; `SHA256SUMS` holds the unpacked
files' sums.
`tests/psp/run-tests.sh` unpacks them into its build folder (`PSP_GE_ROUND6`, unless that names another folder), and
`tests/psp/measured6.cpp` compares the core's run of round 6 with them.

## Not here

The VFPU's measurements (`tools/psp-measure`'s `vfpu.c`: rounds 1 to 3, about 670 MB each, mostly exhaustive sweeps
of functions like `vlog2` and `vsqrt`) are too big for the repository. The owner keeps them and copies them where
they're needed; `docs/psp-vfpu-measurements.md` describes what they showed.

Kept here at the owner's request (round 3 on 2026-10-06, round 6 on 2026-10-11), as `docs/development-process.md`
records.
