# The headless PSP game runner

A command-line front end for the PSP core: it boots a game through the ares system
(`ares/psp/psp.cpp`, the whole core as Phobos builds it, with ares's node tree), runs it a
frame at a time, and reports what happens. It is the headless standing-in for a front end, and
it is what `docs/psp-compatibility.md` was run with.

## Building

```sh
tools/psp-runner/build.sh
```

The binary ends up in this checkout's build folder, named `psp-runner-` and a hash of its
configuration, which the build's last line names
(`${TMPDIR:-/tmp}/phobos-psp-runner/<a number for the checkout>/psp-runner-<hash>`); a copy also
goes in `${TMPDIR:-/tmp}/phobos-psp-runner/psp-runner`, the last one built there. The build
reuses the recipe of `tests/psp/ares/run-tests.sh`: the same objects (the core, ares's globals,
the node tree, nall, sljit, libchdr, zlib, libco), the same defines, its objects kept between
builds by `tests/objects.sh`, each compiled again only when a file it was built from (any header
it includes) or its compile command changes. `PHOBOS_JOBS=N` compiles N at once. The checkout's
folder (its `.phobos-checkout` names the checkout) can be deleted between builds; each
configuration built keeps about 45 MB there until then.

It also builds in FFmpeg's decoders for the games' music and movies, as the tests and Phobos's
builds do: `thirdparty/ffmpeg/build.sh host` downloads FFmpeg's release once (it needs curl, xz,
make and, on x86-64, nasm for speed) and keeps its build in the repository's `.cache/ffmpeg`.
`PSP_FFMPEG=0 tools/psp-runner/build.sh` builds without them: the games' movies then stay black
and their ATRAC music silent.

## Running a game

```sh
psp-runner GAME [options]
```

The game is a disc image (ISO, CSO, ZSO, DAX, JSO, CHD) or a homebrew program (PBP, ELF,
PRX). It is named in its pak the way mia's PSP medium names it (`kind()`, reproduced in
`runner.cpp` from `mia/medium/playstation-portable.cpp`), and for a disc image the runner
reads its `PSP_GAME/PARAM.SFO` and prints the title and the disc's NPID, which the
compatibility report uses for its region and disc ID column.

The options, and what each does:

- `--frames N` — how many frames to run (default 3600, a minute of the PSP's time)
- `--press "F:C,..."` — `F:C` presses and holds `C` at frame `F` (a button, or a stick's
  value: `L-StickX:12345`, or the right stick's: `R-StickX:-20000`); `F:C!` releases `C` at frame `F`
- `--script FILE` — the presses in a file, one `F:C` per line (`#` begins a comment)
- `--png-every K` — write a PNG every K presented frames, into `--out`'s folder
- `--png-at F1,F2,...` — write a PNG at each of these presented frames, into `--out`'s folder
- `--out DIR` — the folder the PNGs go in (needed with `--png-every` and `--png-at`)
- `--wav FILE` — the sound, as a 16-bit stereo WAV at the PSP's 44.1 kHz
- `--save-state-at F FILE` — the machine's state at frame `F`, as a save-state file
- `--load-state FILE` — start from a save-state file (the game booted, then the state
  loaded at once)
- `--interpreter` — run the CPU's interpreter (the recompiler is the default)
- `--ge-threads N` — how many threads draw the GE's pictures (0, the default, for one
  fewer than the host's cores)
- `--renderer NAME` — who draws them: `Software` (the default), `Vulkan` (or `Vulkan-accurate`),
  the hardware renderer (docs/psp-gpu-renderers.md), or `Vulkan-fast`, its fast mode, which
  transforms 3D on the GPU; on the system's Vulkan loader (on macOS, Homebrew's:
  `DYLD_LIBRARY_PATH=/opt/homebrew/lib`). The summary then counts the GPU's work: draws, 3D PRIMs
  transformed by the GPU (fast mode), hand-overs, waits, uploads, read-backs, copies, and the
  milliseconds a frame the CPU waited for it. If Vulkan doesn't start, the run says why and the
  software renderer draws.
- `--resolution N` — the Vulkan renderer's internal resolution: 1, the PSP's own (the default),
  to 10 times it each way. The frames, and so the PNGs, are read back from the GPU at up to 4 times
  the PSP's size (1920x1088), shrunk smoothly where it draws larger.
- `--late-frames` — the Vulkan renderer's frames taken a frame or two late (the core's option
  "Late Frames", at `--resolution 1` only): each frame's picture is copied on the GPU and read when
  its run is done, so nothing waits for the GPU at a frame's end, as when the app presents on its
  window. For timing the GPU as the app runs it; the PNGs then show a frame or two before the one
  they're named by.
- `--pipeline-cache FILE` — the Vulkan renderer's pipelines kept in `FILE` between runs (the core's
  option "Pipeline Cache"), as the app keeps them: a second run starts with the first's, made at once.
- `--memory-stick DIR` — the host folder standing for `ms0:` (a scratch folder, made and
  removed with the run, by default)
- `--fonts DIR` — the PSP's system fonts (the `.pgf` files of a PSP's `flash0:`), for the
  game's text
- `--trace-savedata` — each savedata request the system answers, with its answer, as a line
  `[frame] savedata: ...` on standard output (below)
- `--report FILE` — the run as JSON (below): for comparing runs by what ran, not only by their
  speed
- `--start-time SECONDS` — the PSP's clock starts there (seconds since 1970) rather than at the
  host's time, so two runs see the same dates and can be compared byte for byte (the time zone
  is the host's `TZ`: `TZ=UTC` pins it too). A loaded state keeps the clock it was saved with.
- `--screen-stops-at F` — from frame F the runner takes no more pictures from the screen, as if it
  had stopped presenting: for testing the runner's own capture timeout (`runner-test.sh`)

The buttons by name are `Select`, `Start`, `Up`, `Down`, `Left`, `Right`, `Triangle`,
`Circle`, `Cross`, `Square`, `L` and `R`; the stick's axes are `L-StickX` and `L-StickY`,
their values -32768 to 32767 (left or up to right or down), as the front end takes them. A press
holds its control from its frame on, until a later `!` releases it; a stick stays where it was
put until another value for its axis (`0` centres it).

The right stick, `R-StickX` and `R-StickY` (the same values), is one the PSP itself hasn't got:
the core reports it in bytes 10 and 11 of every SceCtrlData a game reads, 128 each way when it's
left alone, as PPSSPP does, for fan patches made for a second stick (the Armored Core "True
Analogs" mods turn and look with it). pspsdk's pspctrl.h names those bytes Rx and Ry, valid on a
PSP Go with a DualShock 3, a PS Vita, or in emulation (docs/psp-core.md, part 81). For example,
to turn right for a second from frame 600, then look up for half a second:

```sh
psp-runner GAME --press "600:R-StickX:32767,660:R-StickX:0,700:R-StickY:-32768,730:R-StickY:0"
```

## The report

On standard output, the runner prints, each with the frame it happened on: the game's own
output, and the kernel's notes (a function it doesn't have, a thread that will never wake).
At the end it prints a summary: the frames run, whether the program ended, the frames per
second, the unique missing functions with their counts (the notes that begin
`not implemented yet:`), and the last note. The frames per second are the frame loop's, which
includes its captures (the PNGs, waiting for each picture and writing it, and the save state);
where there were any, a line after gives the speed without them and their time, and a capture
whose wait ran out (`stale captures: N`, each said on standard error) is counted. The system's own messages (a disc that can't
start, a memory stick folder that isn't there) go to standard error, as on the host.

With `--trace-savedata`, each savedata request (sceUtilitySavedataInitStart's parameters, done
at its Update) adds a line: the mode (pspsdk's name), the structure's size, the game's and the
save's names as the game gave them, a list mode's names (the first 8, and how many in all), a
list dialog's `focus` (in the sizes that have it) and the save chosen from the list
(`chosen`: the player's, where the app shows the dialog; otherwise, for a load, the save the
dialog would focus among the saves there, or if that one is broken the one the same focus
takes among those that aren't, and for a save naming none (or a pattern, `*` or `?`, which
names none there) the list's first name, unless that's `"<>"`, the game's folder alone), the
data file's name, the buffer's address and size and the data's size, and whether the save is
on the memory stick; the sizes mode's msData, the list and files modes' most, the size mode's
files, a save mode's title; then after `->` the result (the system's savedata errors named)
and what came back: a load's bytes read, a list load's or save's save name (the chosen one,
written back into the request), the list mode's saves, the files mode's counts, the sizes
modes' clusters and KB. Where the app shows the dialogs, what they answer without doing a
request isn't traced: a dialog backed out of, the notices that there's nothing to load or
delete, and the deletes from a list. Names are quoted, their bytes outside printable ASCII
written `\xNN`. For example (After Burner: Black Falcon's autoload, then Brandish: The Dark
Revenant's save to a list on an empty stick, and in another run its load from the list):

```
[002138] savedata: mode 0 (autoload), size 1536, game "ULUS10244", save "0000", file "AFTRBURN.BIN", buffer 0x09c5b570 of 16368, data 16368, "ULUS102440000" there -> 0x00000000, read 16368
[000474] savedata: mode 5 (listsave), size 1536, game "NPUH10195", save "", list "0001" "0002" "0003" "0004" "0005" "0006" "0007" "0008" (99 in all), focus 3, chosen "0001", file "SAVEDATA.DAT", buffer 0x09dd8cd0 of 105260, data 105260, "NPUH101950001" not there, title "Brandish The Dark Revenant" -> 0x00000000, save "0001"
[000451] savedata: mode 4 (listload), size 1536, game "NPUH10195", save "", list "0001" "0002" "0003" "0004" "0005" "0006" "0007" "0008" (99 in all), focus 3, chosen "0001", file "SAVEDATA.DAT", buffer 0x09dbc7b0 of 105260, data 0, "NPUH101950001" there -> 0x00000000, read 105260, save "0001"
```

The frames the PNGs name are the frames the screen presented, one after another from the
first: the picture a frame's run shows reaches the screen's own thread a step behind, so a
PNG named `frame-000030.png` is the picture run 29 or 30 made. If no picture has come since the
last PNG and none comes within 2 s (a screen that stopped presenting, a host too loaded to keep
up), the PNG shows the last one presented, and the runner says so on standard error (`frame N:
the screen presented no new picture in 2 s`) and counts it in the summary's `stale captures`.

With `--report FILE`, the run is also written as JSON, `"report": "phobos-psp-runner 1"` (a
later layout gets another number):

- `runner`: the source the runner was built from (`build.sh` passes `git describe`'s name for
  the commit, `-dirty` with changes; `unknown` built otherwise);
- `game`: its path, the kind the runner took it for (`disc.iso`, `program.elf`, ...), its title
  and disc ID where it has them, its size in bytes;
- `asked`: the renderer, resolution, engine (`recompiler` or `interpreter`), GE threads (0: the
  core's choice), late frames, frames, start time and state to load, as the options gave them;
- `ran`: what the run had: the renderer the core used (`Software` if Vulkan didn't start, as it
  falls back), the GPU's device as its driver names it, the resolution, the GE's threads, the
  clock it started at (seconds since 1970: the host's, the one asked for, or a loaded state's)
  and `TZ`;
- `result`: the frames run, whether the program ended, the frame loop's seconds, its captures'
  seconds, stale captures, the missing functions (library, NID, count) and the last note;
- `gpu`: the GPU renderer's counts (as the summary's `gpu:` line, with its pipelines), or null
  where the software renderer drew.

Values the run didn't have are null, not zero.

## An example

```sh
psp-runner tests/psp/programs/cube.elf --frames 60 --out /tmp/out --png-every 30 \
  --wav /tmp/out/sound.wav
```

```
game: tests/psp/programs/cube.elf
--
frames run: 60
program: still running
frames per second: 1272.91
unique missing functions: 0
```

`tests/psp/ares/runner-test.sh` runs this, and checks the summary, the PNGs and the WAV.
