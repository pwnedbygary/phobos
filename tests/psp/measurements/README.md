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

Without a round's files (`PSP_GE_RESULTS`, `PSP_GE_ROUND6`, `PSP_GE_ROUND8`, `PSP_GE_ROUND9` or `PSP_GE_ROUND10` set
empty), the groups comparing with them say so as a skip of `ge-round3`, `ge-round6`, `ge-round8`, `ge-round9` or
`ge-round10`; where
`PHOBOS_TESTS_REQUIRE` names it, as CI's does, that's a failure instead.

## `ge-round8/`

Round 8 of the same program (its menu's first line since, "Round 8: the GE's time again, and the CPU's"), run on the
same PSP on 2026-10-18 (the night's `results/ge`, beside rounds 6 and 7 again): 19 files, the GE's timed lists
(`time-*`, at 222/111 and 333/166 MHz, `time-lists` and `time-scenes` at 333/222/166 and 266/133 too), the CPU
beside the GE (`cpu-contention`), the CPU alone (`cpu-*`) and the display as the program starts (`display-start`), laid
out as `manifest8.txt` describes; `time-lists.stalled` is the program's note of what it took for stalls in its case 3
(its own timeout, not the GE's: docs/psp-core.md, part 107). Part 107 says what each test is for and what the PSP
showed. Each `.bin` is packed with xz (`xz -9e`: 140 KB of records as 28 KB, 169 KB as 56 KB with the manifest, the
note and the sums); `SHA256SUMS` holds the unpacked files' sums. `tests/psp/run-tests.sh` unpacks them
(`PSP_GE_ROUND8`, as round 6's), `tools/psp-measure/ge-fit.py` fits the GE timing's model to them (part 106), and
`tests/psp/measured8.cpp` compares the core's run of round 8's timed lists with them.

## `ge-round9/`

Round 9 of the same program (its menu's first line since, "Round 9: the GE's triangles, textures, vertices"), run on
the same PSP on 2026-10-18 (the morning after round 8): 6 files, the GE's timed lists laid out as `manifest9.txt`
describes (round 8's records): `time-anchors` (15 of round 8's cases again), `time-tristrips` (triangles reading
narrow textures, and sprites), `time-loads` (a texture's loads and the cache's reuse, palettes, mipmaps),
`time-formats` (vertex formats) and `time-lists2` (round 8's `time-lists` with its stall addresses cached), at
222/111 and 333/166 MHz, `time-lists2` at 333/222/166 and 266/133 too; nothing stalled. Part 110 says what each test
is for and what the PSP showed. He ran `7445a290a`'s `pspmeasure.elf` (its EBOOT `f554782c…`); the one in
`tests/psp/programs` is the later fix round's, which draws round 9's lists the same way. Each `.bin` is packed with xz
(`xz -9e`, 97 KB as 18 KB); `SHA256SUMS` holds the unpacked files' sums (and the manifest's). As round 8's, they're
unpacked (`PSP_GE_ROUND9`), fitted (`ge-fit.py`) and compared with the core's run (`tests/psp/measured8.cpp`).

## `ge-captures/`

The owner's captures of six games on the same PSP with `tools/psp-ge-profile`'s plugin (docs/psp-core.md, parts 103
and 106): Friday's 12, Killzone: Liberation (`UCUS98646-1` and `-2`, two moments of a level), Chains of Olympus
(`UCUS98653`, the difficulty menu), Ghost of Sparta (`UCUS98737`, the main menu and two later scenes), Super Monkey Ball
Adventure (`ULES00364`, Story Mode), Activision Hits Remixed (`ULUS10186`, its first boot cut short, its second boot's
copyright screen, the 3D menu) and Army of Two (`ULUS10472`, two moments of play); and Sunday's 6, taken with round 9
(part 110), named apart: `UCUS98646-r9-1` to `-5` (Killzone in one session, 60, 78, 159, 322 and 443 s in, the first
the courtyard Friday's -1 is of) and `ULUS10186-r9-1` (Activision Hits' 3D menu idle, at the moment of Phobos's
`ahr-menu` state). Each is the plugin's file as it wrote it (`profile.h`'s layout: the games' GE and display calls,
interrupts and times, the disc ID, title and thread names; no game code or data), packed with xz (`xz -9e`);
`SHA256SUMS` holds the unpacked files' sums. `tools/psp-ge-profile/decoder/gate.py` sets them beside Phobos's profiles
of the same scenes with the GE timing "PSP" (the owner's bar since 2026-10-18: each judged scene's whole frame within
15% of the PSP's, none more than 15% too slow, each list reported; Super Monkey Ball's frame, too fast, his accepted
exception for now, in `gate.py`'s `EXCEPTED` with his words; part 106 has the table and how the profiles are made).

## `ge-round10/`

Round 10 of the same program (its menu's first line since, "Round 10: the GE's triangles, lights, the CPU beside it"),
run on the same PSP on the evening of 2026-10-18 (every earlier round run again after it: `ge-reruns-round10`): 10
files, the GE's timed lists laid out as `manifest10.txt` describes (round 8's records; `time-contention`'s and
`time-contention2`'s runs two words longer, a thread's turns and their time): `time-anchors2`, `time-lights`,
`time-clut32`, `time-widetri`, `time-perspective`, `time-mipmaps`, `time-vram8`, `time-contention` and
`time-contention2`, at 222/111 and 333/166 MHz, `time-contention2` at 333/222/166; nothing stalled. He ran `d435eb2bd`'s
`pspmeasure.elf` (its EBOOT `31364afe…`). The one in `tests/psp/programs` is round 10c's (`9bcaff348`'s, `b38077b4…`;
round 10b's build before it), which draws round 10's lists the same way but for where its arrays sit: on 1 KiB
boundaries (since round 10b's build), where `d435eb2bd`'s put the RAM textures and palette 64 bytes off a 128-byte
boundary, which made swizzled RAM textures about 7% dearer (by time-placement the committed build's would come out 6 to
7% cheaper). Part 112 says so with what each test is for and what the PSP showed. Each `.bin` is packed with xz
(`xz -9e`, 160 KB as 36 KB); `SHA256SUMS` holds the unpacked files' sums (and the manifest's). As round 9's, they're
unpacked (`PSP_GE_ROUND10`), fitted (`ge-fit.py`, #269's part 106) and compared with the core's run
(`tests/psp/measured8.cpp`: the committed program's, whose arrays' places don't move the core's times).

## `ge-round10b/`

Round 10b, the same program's top-up (its menu's first line since, "Round 10b: the CPU polling, before and beside;
placement"), run on the same PSP on the night of 2026-10-18 (pulled at 20:23): 3 files, `time-contention3` and
`time-placement` laid out as `manifest10b.txt` describes (round 10's records; `time-contention3`'s runs two words
longer, as `time-contention`'s), at 222/111 and 333/166 MHz; nothing stalled. He ran `2578e331a`'s `pspmeasure.elf` (its
EBOOT `6fa4ca80…`), the one in `tests/psp/programs`, its arrays on 1 KiB boundaries. Part 112 says what each test is for
and what the PSP showed. Each `.bin` is packed with xz (`xz -9e`, 19 KB as 4.5 KB); `SHA256SUMS` holds the unpacked
files' sums (and the manifest's). No test reads them yet: they're for the GE timing's fit (#269).

## `ge-reruns-round10/`

Rounds 2 to 9 run again on the same PSP in round 10's session (the evening of 2026-10-18), with round 10's program
(`d435eb2bd`): 217 files as each round's manifest describes, the five `.stalled` files rounds 4 and 5's known stalls.
Every picture is the same bit for bit as the earlier runs' (rounds 2 and 3's 87 against `ge-round3`, round 6's 28
against `ge-round6`, rounds 6 and 7's 34 against round 8's session); the timing records repeat within 0.0 to 2.1% at the
median, their tails where the program's arrays moved (part 112). Each `.bin` is packed with xz (`xz -9e`, 44 MB as 3.6
MB); `SHA256SUMS` holds the unpacked files' sums (and the manifests', notes' and markers'). `ge-fit.py --again` holds
out of the GE timing's fit the lists that moved more than 5% between these and the first runs (part 106).

## Not here

The VFPU's measurements (`tools/psp-measure`'s `vfpu.c`: rounds 1 to 3, about 670 MB each, mostly exhaustive sweeps
of functions like `vlog2` and `vsqrt`) are too big for the repository. The owner keeps them and copies them where
they're needed; `docs/psp-vfpu-measurements.md` describes what they showed.

Kept here at the owner's request (round 3 on 2026-10-06, round 6 on 2026-10-11, rounds 8 and 9 and the captures on
2026-10-18), as `docs/development-process.md` records.
