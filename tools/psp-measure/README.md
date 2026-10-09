# psp-measure

One homebrew program that records what a real PSP computes and draws: what its VFPU (the vector maths unit) and FPU
compute, what its GE (the graphics chip) draws, and how its controller driver times its reads. Beside it are the
host tools that check Phobos's PSP core against those recordings, bit for bit.

Until 2026-10-04 these were two programs, `tools/psp-vfpu-measure` and `tools/psp-ge-measure`; rounds 1 and 2 were
recorded with them. Their tests are unchanged here and write the same results, byte for byte (but for the
manifests' wording, and `controller-timing.bin`, whose times are measured), now into `results/vfpu` and `results/ge`.

## Why it was needed

**The VFPU and the FPU.** The VFPU's maths functions (sine, cosine, reciprocal, square root, log, exponent and the
rest) aren't exact maths: each is the hardware's own approximation, and its answers differ from the true ones in the
last few bits. Games depend on getting the same bits as the hardware, because small differences add up: physics
drifts, replays and ghost runs fall out of step, comparisons go the other way. No public document lists those exact
answers. PPSSPP has tables for them, but they're GPL code, and Phobos doesn't copy other emulators' code. So the user
chose to measure their own PSP instead (see "Decisions" in [docs/psp-core.md](../../docs/psp-core.md)): this program
runs every input that matters through the real hardware and saves the answers; the host fits a model to them, and
checks the core reproduces every recorded value.

**The GE and the controller.** Parts 8 to 12 of the core (the controls, the GE's display lists, drawing in 2D and
3D, lighting) are written from public sources: pspsdk's headers, uOFW's study of the PSP's firmware, and PPSSPP's
software renderer, read for behavior only. They're good references, but they aren't the hardware. PPSSPP's authors
mark some of their own rules as unverified, and pspsdk's documentation and uOFW disagree about whether a second
`sceCtrlReadLatch` waits. Getting rules like these wrong shows up as colors a level off, pixels drawn twice or not at
all at the edges of shapes, textures shifted by a texel, or lighting a shade too bright. This program draws each case
on a real PSP, reads back the exact pixels and saves them, so the core can be compared with the hardware itself.

## Building and running

1. Build with pspdev's toolchain (<https://github.com/pspdev/pspdev>; the `phobos-linux` container has it in
   `/opt/pspdev`, with `psp-config` on the PATH): `make` gives `EBOOT.PBP`. `make SMOKE=1` gives a quick version for
   trying in an emulator first, PPSSPP's PPSSPPHeadless, with `-i` (its ARM64 JIT trips an assertion on one of
   round 3's recorder entries) and `--graphics=software`. It runs round 3's VFPU and FPU tests (the big ones cut
   short), the FPU probes and the GE's tests (rounds 2-5) at once, then leaves; its results say nothing about a
   PSP. Run `make clean` when switching between the two.
2. On a PSP with custom firmware that runs homebrew, copy `EBOOT.PBP` to a folder under `PSP/GAME` on the memory
   stick (say `PSP/GAME/PSPMEASURE`) and start it. Keep the charger in. Up and down pick a line of the menu, and X
   runs it:
   - Round 5: the GE's steps, lighting's share, texels (about 3 MB)
   - Round 4: the GE's lines, boxes, DXT and curves (about 8 MB)
   - Round 3: the VFPU and the FPU (about 6 MB)
   - Round 3: the GE (about 7 MB)
   - The FPU probes
   - Round 2 again: the VFPU and the FPU (about 230 MB)
   - Round 2 again: the GE and the controller (about 15 MB)
   - Round 1 again: the VFPU (about 450 MB)
   - Start afresh
   - Leave

   After a round, X goes back to the menu.
3. Results go to `results/` beside `EBOOT.PBP`: the VFPU's and the FPU's in `results/vfpu`, the GE's in
   `results/ge`. A round skips the tests it has finished, so it can be stopped and started again. Start afresh (it
   asks first) renames `results` to `results-1`, or the next number that's free, and starts an empty one, so every
   test runs again; nothing is deleted. The program keeps the PSP awake, and each test's line says "running" as it
   starts. A test that didn't finish runs once more; if it stops the PSP again, the next start gives up on it
   (`<name>.stopped`) and finishes the rest. A probe is given up on after one stop, so expect to start the program
   again, and choose the probes again, after each probe that switches the PSP off.
4. Copy `results/` back. `tools/psp-measure/compare.sh <results folder>` checks the core against the VFPU's and the
   FPU's results (in its `vfpu/`); for the GE's, see "How the GE's comparison works" below.

The menu, and starting afresh (which renames a folder on the memory stick), have only run in Phobos's own core so
far: the first run on a PSP is their first real test. Round 1's random number test (`vrnd`) records the generator
as the program found it, so it runs once per start of the program: to run it again (after starting afresh, say),
start the program again first (otherwise it says so, and skips that one test).

## Files

| File | What it is |
|---|---|
| `main.c` | The menu, starting afresh, and leaving. |
| `results.c`, `measure.h` | The results folder, and each test's file in it: written as `<name>.part` and renamed to `.bin` once complete, run once more after a stop, then given up on as `.stopped`. `measure.h` is what the parts share. |
| `vfpu.c` | The VFPU's and the FPU's tests. |
| `ge.c` | The GE's and the controller's tests. |
| `Makefile` | Builds them into the one program. |
| `ops.py`, `ops.h`, `ops3.h` | `ops.py` writes `ops.h` and `ops3.h`, the instruction recorder's lists for rounds 2 and 3. Every entry is checked by pspdev's own assembler, because a real PSP stops a program at an instruction it doesn't have. |
| `compare.sh`, `compare.cpp` | Checks the core against a folder of VFPU and FPU results: each input run through the core's own instruction and compared bit for bit, with how far off any mismatch is (in units in the last place); for the instruction recorder, each differing entry's words counted by kind (a NaN, a denormal, rounding, a lane left unwritten, other); for the probes, each result beside the core's. Works on any round's files. |
| `fit.py` | Fits the model behind the maths functions (128 segments of integer coefficients per function) from the results alone, and checks it reproduces every recorded value. It gave the core's `vfpu-segments.hpp`. Needs numpy. |

## The VFPU and the FPU

Each round writes its own files into `results/vfpu`, and a `manifest.txt` (round 1), `manifest2.txt` (round 2) or
`manifest3.txt` (round 3 and the probes) saying how each file's inputs are made.

- **Round 1 (about 450 MB):** every input of the range each maths function reduces its argument to, and a million
  spread-out inputs for each; the random number generator from a range of seeds; `vadd`, `vsub`, `vmul`, `vdiv` and
  `vdot` on inputs of every kind (signs, sizes, denormals, infinities, NaNs).
- **Round 2 (about 230 MB):** what round 1 couldn't settle: `vlog2` over whole binades above 4; dot products, sums
  and averages built to show how the VFPU adds several numbers; every half float through `vh2f` and a million
  floats through `vf2h`; the integer divide (by zero too) and the FPU's conversions and arithmetic in each rounding
  mode; and the instruction recorder, which runs every VFPU instruction pspdev's assembler knows (1216 entries) on
  random register states.
- **Round 3 (about 6 MB):** what round 2 left open.
  - The FPU's conversions and arithmetic again, on inputs it's safe with. Round 2's FPU tests had switched the PSP
    off, and the guess was the NaNs, infinities, denormals and too-big numbers they began with. (Round 3 showed
    otherwise: no probe stopped the PSP, and round 2's tests finished in the same session.)
  - FCSR, the FPU's control register, as a program finds it. Its flush-to-zero bit decides what denormals do.
  - Products a sliver below the smallest normal number, built to tell three answers apart: all flushed to 0 (too
    small seen before rounding), up to j = 1448 rounded up to it (rounded to 24 bits first), or up to j = 2048
    (rounded as IEEE's denormals would be, then flushed: what the core does now).
  - A second recorder list (`ops3.h`, 284 entries): the maths functions with prefixes (`vrcp` takes them on its
    last lane alone), swizzles past an operand's size in instructions that don't work lane by lane, and `vavg` and
    `vfad` with t prefixes.
- **The FPU probes:** the FPU on one value at a time of the kinds that may switch the PSP off (17 probes:
  infinities, quiet and signaling NaNs, -2^31 and 2^31 as integers, denormals, too-big and too-small results, with
  flush to zero off, and the denormal ones again with it on). The probes' labels give NaNs in MIPS's older
  encoding, where a quiet NaN has the top fraction bit clear; the PSP turned out to use IEEE 754-2008's, the other
  way round. They're a menu line of their own so that round 3 can always finish.

The files hold only what the hardware gave (and, for the smaller tests, the inputs). The host makes every input
again exactly as the program did, so nothing has to be computed on the PSP.

### What it has given so far

The user ran round 1 on 2026-10-03 (firmware 6.61). From it:

- the random number generator matches every word;
- `vadd`, `vsub`, `vmul` and `vdiv` match every result;
- `vsin` and `vcos` are exact (`fit.py`), and so is `vlog2` below 4.

The user ran round 2 on 2026-10-04: from it the core's matrices, division by zero, NaNs, denormals, comparisons and
prefixes now match the PSP, and the recorder matches in 1157 of its 1216 entries (the rest is the adders' rounding).

The user ran round 3 later that day, with the probes and rounds 1 and 2 again (every result file identical, byte for
byte; the manifests differ only in their wording). No probe stopped the PSP, and round 2's FPU tests finished this
time. From it: FCSR starts with the overflow, divide-by-zero and invalid exceptions enabled; NaNs follow IEEE
754-2008's encoding; the FPU's arithmetic follows the rounding mode (the core's doesn't yet) and its conversions
saturate by sign; the VFPU rounds a product to 24 bits before flushing it to 0 below 2^-126; and the second recorder
list gives the data for the prefix rules still open. Results and findings are in
[docs/psp-vfpu-measurements.md](../../docs/psp-vfpu-measurements.md). The small result files are kept in
`tests/allegrex/measured/`, which the tests check the core against; the big tables stay outside the repository.

## The GE and the controller

Each case is drawn into VRAM, read back as it is, and written to `results/ge`: 64 files of little-endian 32-bit
words and a `manifest.txt`, about 15 MB, in a few seconds. These are round 2's, the first the GE had:

- **2D:** every blend operation and factor; the texture functions, with alpha and color doubling; the texture
  filter; which pixels sprites and triangles cover with their corners at each sixteenth of a pixel; two triangles
  sharing an edge; the sprite corners' quarter turn; dithering; the stencil's steps; every texel of the 16-bit texture
  formats, and colors narrowed into 16-bit frame buffers; colors across triangles; the texel each pixel takes when a
  texture is shrunk or stretched.
- **The controller:** whether a second `sceCtrlReadLatch`, a `sceCtrlReadBufferPositive` just after a vertical blank,
  and a second `sceCtrlReadBufferPositive` wait.
- **3D:** a floor receding in perspective (the texels, the depths written, the fog); a 3D sprite whose corners lie at
  different depths; the GE's rounding onto the screen; a triangle cut at the near plane; which depths stop a
  primitive; culling.
- **Lighting:** diffuse and the shine across the angles; a spotlight's pool with its direction either way; a point
  light's fading; environment mapping's texture coordinates.

Round 3 (about 7 MB, `manifest3.txt`) takes what round 2 left open (docs/psp-core.md, "Results from the user's
PSP"), one 256x256 picture per case but the depth buffer's:

- **Lighting with cosines known exactly:** normals built from Pythagorean triples and quadruples (such as (3, 0, 4),
  five long), so each cosine is an exact fraction, scaled and turned in ways the GE must undo; for plain diffuse
  (`light-cosines`, also with the light's direction 3 long), powered diffuse (`light-powered`, the GE's light kind
  2, which round 2 didn't use) and the shine (`light-shine`), at powers 1 and 2; and material, light and ambient
  colors at levels where rounding shows (`light-materials`, `light-colors`, `light-ambient`).
- **Colors and fog stepped across a primitive:** 16 ramps at slopes from shallow to steep, in through mode along x
  and along y, in 3D with corners between pixels (as a triangle cut at the near plane has), and fog ramps in 3D
  with no perspective (`ramp-*`).
- **3D:** edges just past the pixel middle, where the GE's rounding onto the screen decides a pixel
  (`3d-rounding-middle`); a wall receding along x (`3d-wall-texels`); round 2's 3D sprite taken apart into its fog
  alone, its texels alone, and both at one depth (`3d-sprite-*`).
- **Curved surfaces:** a 4x4 grid of control points as Bézier patches, flat, curved and cut finer, and as splines
  with either edge type (`bezier-*`, `spline-*`). The core draws them since part 33.
- **The depth buffer's layout:** every pixel of a 256x64 area given a depth of its own, read back through each of
  VRAM's four copies (`depth-layout-0` to `-3`, the whole depth buffer's 512x256 values each). Last, as no program
  here has read VRAM's other copies on a PSP before.

Round 4 (about 8 MB, `manifest4.txt`) records what the core's parts 29 and 33 drew by rules of their own, where
pspautotests' recordings settle only part (docs/psp-core.md, parts 29 and 33):

- **Lines:** in 16x16 cells, lines with both ends at every sixteenth of a pixel, shallow, steep, diagonal, rising,
  level and upright, the first two drawn backwards too, and anti-aliased over black (`lines-*`); lines shorter than
  two pixels in 16 directions (`lines-short`); strips added up, so a pixel lit twice shows 2 (`lines-strips`);
  colors, depths (through VRAM's fourth copy) and texels along lines (`lines-colors`, `-depth`, `-texels`); lines
  in 3D with sub-pixel ends, and cut at the near plane (`lines-3d`). pspautotests' `gpu/exact/lines` checked the
  PSP's lines by CRCs the core's rule doesn't meet; these are the pixels.
- **Bounding boxes:** 256 boxes, each filling its cell only if the GE took it to be in sight (`bbox`): single
  vertices by sixteenths around the scissor rectangle's edges, boxes past each edge, past different edges at once
  (around the view among them), past the near and far planes with `GU_CLIP_PLANES` on and off, behind the camera,
  in through mode, and random ones.
- **DXT textures:** 256 random blocks each of DXT1, DXT3 and DXT5, their colors and alphas (`dxt*-colors`), and the
  blocks' order with buffer widths of 32, 64 and 36 and swizzling on (`dxt-layout`).
- **Curved surfaces' vertices** (BEZIER and SPLINE), most drawn as points, a vertex each, in through mode, where
  nothing but the GE's own tessellation stands between the control points and the pixels: Bézier patches cut 1 to 16
  times, their pixels and colors (`curves-bezier`) and depths, read through VRAM's fourth copy
  (`curves-bezier-depths`); where vertices fall to the sixteenth of a pixel, from control points moved by sixteenths
  (`curves-places`); splines of 5x5 points with every pair of end types (`curves-spline`, `-spline-depths`); texture
  coordinates at the vertices (`curves-texels`); in 3D, the texture coordinates the GE makes up for a vertex type
  without them, over one patch, several, splines, with a texture scale and offset, and in through mode
  (`curves-made-up`); normals made from the slopes, lit from four ways with either patch front face, beside the same
  patches given normals (`curves-lit`); which of culling and its front face, and the patch culling and patch front
  face, cull a patch's triangles (`curves-culling`); which vertices strips join across patches, as lines and flat
  triangles (`curves-joins`); and how many vertices a row has, points added up, at 16, 63, 64 and 65 divisions
  (`curves-count`), then past pspsdk's 64 at 100 and 128 (`curves-count-128`), 200 (`curves-count-200`) and 255
  (`curves-count-255`), last and a few to a display list, so that if the GE stalls there only that test is given up
  on. The core draws these by a rule fitted to round 3's pictures and pspautotests' (docs/psp-core.md, part 33).

Round 5 (about 3 MB, `manifest5.txt`) takes what docs/psp-core.md's part 48 left open, once it fitted how the GE
steps colors, fog and depth across triangles:

- **The steps on shapes they weren't fitted to:** 64 random triangles in through mode, colors (`steps-check`) and
  depths (`steps-depth-check`, read through VRAM's fourth copy); the near plane's cut with each corner past it in
  turn, each way round (`clip-split`), which shows how the four corners left are split into two triangles.
- **Lighting's share:** four normals to a picture, each with 192 different light-times-material products, which pin
  the share the GE takes of a light far finer than a 256th (`light-share-0` to `-3`): round 3's two odd cells'
  normals, others as long, the same three times as long, round 2's sweep normals, and turned.
- **Perspective texels to the texel in thousands:** a repeating texture with coordinates up to 4096 texels, so each
  pixel shows its coordinates to the texel: round 3's wall and 3D sprite and round 2's floor (`persp-wall`,
  `-sprite`, `-floor`), a wall with u / w the same at every corner, so u shows 1 / w alone (`persp-divide`), and one
  at w 3 everywhere, no perspective (`persp-w3`).

### How the GE's comparison works

The program computes nothing itself. `tests/psp/measure.cpp` runs the same program in Phobos's core, through its
menu as a person would (the GE's tests, starting afresh, the tests again, round 3, the probes, rounds 4 and 5,
leaving), and checks every file is written; with `PSP_GE_RESULTS` set to a `results/ge` folder, it lists what differs
from that folder (`PSP_GE_OURS` keeps the core's own files for a closer look):

```
tools/psp-test-programs/build.sh /tmp/programs   # builds pspmeasure.elf among the test programs
PSP_TEST_PROGRAMS=/tmp/programs PSP_GE_RESULTS=<results folder>/ge tests/psp/run-tests.sh
```

Against PPSSPP's software renderer (its headless build running the `SMOKE` version), 61 of round 2's 64 files
matched when they were written. The ones that differed were the questions for the PSP:

- a sprite whose right or bottom edge runs exactly through pixel middles;
- which texel a shrunk sprite takes (the core samples a sprite at each pixel's middle, PPSSPP 7/16 in);
- the fog across a 3D sprite whose corners lie at different depths;
- and, in a file that does match, which way a spotlight's direction points (PPSSPP's reading has it point toward the
  light, which is unusual).

The user's PSP has answered these and more (docs/psp-core.md, "Results from the user's PSP"). Sprites and triangles
are sampled at each pixel's middle, a sprite's left edge reaching a sixteenth further left. Texture coordinates are
stepped from a primitive's leftmost corner. The spotlight points toward the light, as PPSSPP reads it. With the
core fixed to match (the filter's rounding and the near-plane cut too), 52 of the 63 pictures are identical to the
PSP's.

For round 3, PPSSPP and the core agree on all six lighting cases (the core's lighting follows PPSSPP's reading), the
3D sprite at one depth, and the four depth-layout reads (both read VRAM's copies alike). They differ on the ramps,
the edges near the pixel middle, the wall's texels, and the 3D sprite's fog and texels, where the core now samples
and steps as round 2 showed the PSP does, or neither is measured yet; and on the curved surfaces, which the core
doesn't draw.

The user's PSP has answered (docs/psp-core.md, "Round 3's results"). The GE truncates screen positions to the
sixteenth toward 2048 (here both the viewport's center and the middle of its space; which one counts is still open);
it splits a 3D sprite's fog across the middle as PPSSPP does, and steps its texels a way of its own; ambient light
and, with exact cosines, plain diffuse are the core's arithmetic in all but two cells (one cosine), while the other
lighting cases and the color and fog ramps are a level apart in places (lower on the PSP, but both ways in the
vertical ramps); only VRAM's fourth copy reads the depth buffer in order. The findings go into
[docs/psp-core.md](../../docs/psp-core.md), under "Measuring the GE and the controller on a PSP".
