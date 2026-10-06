# Measurements from a real PSP

What the owner's own PSP drew, read back and saved, for the PSP core's tests to compare against. These are the
measured truth the GE is fitted to. Never edit them: a new measurement round goes in a folder of its own.

## `ge-round3/`

The GE tests of `tools/psp-measure` (`ge.c`), round 3, run on the owner's PSP (a PSP-2000/3000, firmware 6.61) on
2026-10-04. Each `.bin` is one test's target pixels after it drew. `manifest3.txt` (and `manifest.txt`, from the
same program) describes the layout of each file and what each test draws.

`tests/psp/measure.cpp` runs `pspmeasure.elf` (`tests/psp/programs`) in the core and compares each result with these.
`tests/psp/run-tests.sh` uses this folder unless `PSP_GE_RESULTS` names another. `docs/psp-core.md` (part 10,
"Measuring the GE and the controller on a PSP", and the parts after it) says what each round found.

## Not here

The VFPU's measurements (`tools/psp-measure`'s `vfpu.c`: rounds 1 to 3, about 670 MB each, mostly exhaustive sweeps
of functions like `vlog2` and `vsqrt`) are too big for the repository. The owner keeps them and copies them where
they're needed; `docs/psp-vfpu-measurements.md` describes what they showed.

Kept here at the owner's request (2026-10-06), as `docs/development-process.md` records.
