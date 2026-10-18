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
`docs/psp-core.md` says what each test is for (part 66) and what the PSP showed (part 75). Each `.bin` is packed with
xz (`xz -9e`, 7.3 MB as 1.5 MB), as `tests/allegrex/measured`'s are; `SHA256SUMS` holds the unpacked files' sums.
`tests/psp/run-tests.sh` unpacks them into its build folder (`PSP_GE_ROUND6`, unless that names another folder), and
`tests/psp/measured6.cpp` compares the core's run of round 6 with them.

Without a round's files (`PSP_GE_RESULTS` or `PSP_GE_ROUND6` set empty), the groups comparing with them say so as a
skip of `ge-round3` or `ge-round6`; where `PHOBOS_TESTS_REQUIRE` names it, as CI's does, that's a failure instead.

## `ge-round8/`

Round 8 of the same program (its menu's first line since, "Round 8: the GE's time again, and the CPU's"), run on the
same PSP on 2026-10-18 (the night's `results/ge`, beside rounds 6 and 7 again): 19 files, the GE's timed lists
(`time-*`, at 222/111 and 333/166 MHz, `time-lists` and `time-scenes` at 333/222/166 and 266/133 too), the CPU
beside the GE (`cpu-contention`), the CPU alone (`cpu-*`) and the display as the program starts (`display-start`), laid
out as `manifest8.txt` describes; `time-lists.stalled` is the program's note of what it took for stalls in its case 3
(its own timeout, not the GE's: docs/psp-core.md, part 107). Part 107 says what each test is for and what the PSP
showed. Each `.bin` is packed with xz (`xz -9e`: 140 KB of records as 28 KB, 169 KB as 56 KB with the manifest, the
note and the sums); `SHA256SUMS` holds the unpacked files' sums. No test reads them yet: #269's
`tools/psp-measure/ge-fit.py` fits the GE timing's model to them (with #269's model) and a
`measured8` test will compare the core's run of round 8 with them once that model and this program are both in.

## `ge-round9/`

Round 9 of the same program (its menu's first line since, "Round 9: the GE's triangles, textures, vertices"), run on
the same PSP on 2026-10-18 (the morning after round 8): 6 files, the GE's timed lists laid out as `manifest9.txt`
describes (round 8's records): `time-anchors` (15 of round 8's cases again), `time-tristrips` (triangles reading
narrow textures, and sprites), `time-loads` (a texture's loads and the cache's reuse, palettes, mipmaps),
`time-formats` (vertex formats) and `time-lists2` (round 8's `time-lists` with its stall addresses cached), at
222/111 and 333/166 MHz, `time-lists2` at 333/222/166 and 266/133 too; nothing stalled. Part 110 says what each test
is for and what the PSP showed. He ran `7445a290a`'s `pspmeasure.elf` (its EBOOT `f554782c…`); the one in
`tests/psp/programs` is the later fix round's, which draws round 9's lists the same way. Each `.bin` is packed with xz
(`xz -9e`, 97 KB as 18 KB); `SHA256SUMS` holds the unpacked files' sums (and the manifest's). No test reads them yet,
as round 8's: they're for the GE timing's fit (#269).

## `ge-round10/`

Round 10 of the same program (its menu's first line since, "Round 10: the GE's triangles, lights, the CPU beside it"),
run on the same PSP on the evening of 2026-10-18 (every earlier round run again after it: `ge-reruns-round10`): 10
files, the GE's timed lists laid out as `manifest10.txt` describes (round 8's records; `time-contention`'s and
`time-contention2`'s runs two words longer, a thread's turns and their time): `time-anchors2`, `time-lights`,
`time-clut32`, `time-widetri`, `time-perspective`, `time-mipmaps`, `time-vram8`, `time-contention` and
`time-contention2`, at 222/111 and 333/166 MHz, `time-contention2` at 333/222/166; nothing stalled. He ran `d435eb2bd`'s
`pspmeasure.elf` (its EBOOT `31364afe…`), the one in `tests/psp/programs`. That build put its RAM textures and palette
64 bytes off a 128-byte boundary, which makes swizzled RAM textures about 7% dearer than an aligned place: part 112 says
so with what each test is for and what the PSP showed. Each `.bin` is packed with xz (`xz -9e`, 160 KB as 36 KB);
`SHA256SUMS` holds the unpacked files' sums (and the manifest's). No test reads them yet: they're for the GE timing's
fit (#269).

## `ge-reruns-round10/`

Rounds 2 to 9 run again on the same PSP in round 10's session (the evening of 2026-10-18), with round 10's program
(`d435eb2bd`): 217 files as each round's manifest describes, the five `.stalled` files rounds 4 and 5's known stalls.
Every picture is the same bit for bit as the earlier runs' (rounds 2 and 3's 87 against `ge-round3`, round 6's 28
against `ge-round6`, rounds 6 and 7's 34 against round 8's session); the timing records repeat within 0.0 to 2.1% at the
median, their tails where the program's arrays moved (part 112). Each `.bin` is packed with xz (`xz -9e`, 44 MB as 3.6
MB); `SHA256SUMS` holds the unpacked files' sums (and the manifests', notes' and markers').

## Not here

The VFPU's measurements (`tools/psp-measure`'s `vfpu.c`: rounds 1 to 3, about 670 MB each, mostly exhaustive sweeps
of functions like `vlog2` and `vsqrt`) are too big for the repository. The owner keeps them and copies them where
they're needed; `docs/psp-vfpu-measurements.md` describes what they showed.

Kept here at the owner's request (round 3 on 2026-10-06, round 6 on 2026-10-11, rounds 8 and 9 on 2026-10-18), as
`docs/development-process.md` records.
