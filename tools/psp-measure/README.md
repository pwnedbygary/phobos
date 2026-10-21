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
   short), the FPU probes and the GE's tests (rounds 2-10c) at once, then leaves; its results say nothing about a
   PSP. Run `make clean` when switching between the two.
2. On a PSP with custom firmware that runs homebrew, copy `EBOOT.PBP` to a folder under `PSP/GAME` on the memory
   stick (say `PSP/GAME/PSPMEASURE`) and start it. Keep the charger in. Up and down pick a line of the menu, and X
   runs it:
   - Round 10c: Monkey Ball's strips, Activision Hits' lists (30 KB)
   - Round 10b: the CPU polling, before and beside; placement (30 KB)
   - Round 10: the GE's triangles, lights, the CPU beside it (0.2 MB)
   - Round 9: the GE's triangles, textures, vertices (about 0.1 MB)
   - Round 8: the GE's time again, and the CPU's (about 1 MB)
   - Round 7: the GE's anti-aliased lines (about 1.5 MB)
   - Round 6: the GE's time, lighting, reciprocal, curves (about 7 MB)
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
   again, and choose the probes again, after each probe that switches the PSP off. The GE gets 5 seconds for each
   list: if it hasn't finished by then, it has stalled, and the program resets it and goes on. The test's line says
   "the GE stalled: resetting", then ends "the GE stalled: reset"; its `.bin` holds what the GE had drawn, and
   `<name>.stalled` says how long it waited, what the list held and what the reset gave. If the reset itself stops
   the PSP (the line stays at "resetting"), the next start gives up on that test at once.
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
| `results.c`, `measure.h` | The results folder, and each test's file in it: written as `<name>.part` and renamed to `.bin` once complete, run once more after a stop, then given up on as `.stopped` (at once if it left a `.stalled`). `measure.h` is what the parts share. |
| `vfpu.c` | The VFPU's and the FPU's tests. |
| `ge.c` | The GE's and the controller's tests, and the timed lists of rounds 6, 8, 9, 10, 10b and 10c (round 10's, 10b's and 10c's with a thread reading and writing memory beside the GE). Its waits for the GE are timed (`waitForGe`): a stall is recorded, the GE reset, and the round goes on. |
| `cpu.c` | Round 8's tests of the CPU alone: its loops in assembly (memory's in C), memory, system and driver calls, the clocks granted. Its waits for the GE are timed as `ge.c`'s are, sharing its reset (`geReset`). |
| `Makefile` | Builds them into the one program. |
| `ops.py`, `ops.h`, `ops3.h` | `ops.py` writes `ops.h` and `ops3.h`, the instruction recorder's lists for rounds 2 and 3. Every entry is checked by pspdev's own assembler, because a real PSP stops a program at an instruction it doesn't have. |
| `compare.sh`, `compare.cpp` | Checks the core against a folder of VFPU and FPU results: each input run through the core's own instruction and compared bit for bit, with how far off any mismatch is (in units in the last place); for the instruction recorder, each differing entry's words counted by kind (a NaN, a denormal, rounding, a lane left unwritten, other); for the probes, each result beside the core's. Works on any round's files. |
| `fit.py` | Fits the model behind the maths functions (128 segments of integer coefficients per function) from the results alone, and checks it reproduces every recorded value. It gave the core's `vfpu-segments.hpp`. Needs numpy. |
| `ge-terms.py` | The GE timing's fit (`GE::Cost`, docs/psp-core.md parts 106 and 113), its terms: patches a copy of the source so its runner writes each run of the GE's terms (the counts `GE::Cost`'s clocks multiply). Python alone. |
| `ge-fit.py` | Fits the GE's terms to rounds 6, 8, 9 and 10's timed lists. Python alone. |
| `contention10.py` | Fits the CPU beside the GE to round 10's time-contention. Python alone. |
| `contention11.py` | Fits the CPU beside the GE again, with the traffic as the CPU's caches count it (and with `--core`, the core's own run of rounds 10 and 10b beside the PSP's). Python alone. |
| `contention12.py` | Fits rounds 10, 10b and 10c in the Super Monkey Ball refit's two forms and chooses between them (with `--core`, the core's own run of the three rounds against measured8's bounds). Python alone. |

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
  triangles (`curves-joins`); and how many vertices a row has, points added up, at 16, 63 and 64 divisions
  (`curves-count`), then past pspsdk's 64 one to a test, last: `curves-count-65`, `-100`, `-128`, `-200` and `-255`.
  The core draws these by a rule fitted to round 3's pictures and pspautotests' (docs/psp-core.md, part 33).
- **The stall check** (`stall-check`), just before the counts: a list left waiting at its stall address, so the GE
  doesn't finish it and the program resets it as after a stall (its `.stalled` says so), with 16 divisions' count
  drawn before the reset (row 0) and after it (row 1), which should match.

The owner's PSP froze in round 4 at `curves-count` (then 16 to 65 divisions in one list) and `curves-count-128` (100
and 128): most likely the GE didn't finish, and the program waited for it for good. Since 2026-10-09 the program's
waits are timed and a stall is reset (above), and each count past 64 has its own test. What the earlier program left
of the counts (`.part`, `.again`, `.stopped`) is cleared the first time round 4 runs in a results folder
(`curves-count.timed` marks that), so they run again; finished `.bin` files are kept.

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

Round 6 (about 7 MB, `manifest6.txt`, which also lists every timing case) takes what parts 60 and 64 left open
(docs/psp-core.md, part 66, says what each result would settle):

- **The GE's drawing time** (part 64 models it from Sony's peak rates): lists of known work, each queued alone and
  timed from just before `sceGeListEnQueue` to its finish callback, every run's time kept, each case at a few sizes
  for straight-line fits, at the clocks the program found and again at 333/166 MHz: commands of several kinds
  (`time-commands`), small primitives of every kind in through mode and 3D, lit, skinned, morphed, culled, cut and as
  curved surfaces (`time-primitives`), fills in every state and texture format (`time-fill`), the same pixels in
  primitives of every size (`time-sizes`), block transfers (`time-transfers`), the texture cache (`time-texcache`)
  and a SIGNAL's time (`time-signals`).
- **Lighting's cosine**: the products in full at four normals (`light-products-*`, as round 5's `light-share-*` fit
  no one share a normal), 1024 normals swept a picture with 64 products each (`light-sweep-*`), and lights added up
  (`light-sum`).
- **The reciprocal's chords**: every chord's value read off texels at 13-bit and 14-bit qs (`recip-pixels`,
  `recip-projected`, the latter through the texture matrix, also measuring that path and a q of 0 or below), at a
  vertex in constant-w cells at every chord (`recip-vertex-*`, and `recip-sprites` as sprites), and how far the GE
  keeps a texture coordinate's low bits (`texel-range`).
- **Curved surfaces**: what PATCH_CULL_ENABLE does (`patch-cull`, `patch-cull-edges`, and two timed cases), and
  divisions past 64 along v (`curves-count-v`, then `-v-65`, `-v-100`, `-v-128`, `-v-200` and `-v-255` one to a test,
  last, where the GE may stall on purpose).
- **The driver's statuses** while a list draws, and with its FINISH's interrupt held off (`ge-statuses`).

Round 7 (about 1.5 MB, `manifest7.txt`) takes anti-aliased lines (`GU_LINE_SMOOTH`) past round 4's
`lines-smooth`, which pinned their coverage only for white, opaque lines falling to the right over black
(docs/psp-core.md, part 78):

- **Every octant** (`aa-slopes`): shallow and steep, falling and rising, at two slopes, each starting at every
  sixteenth along x and drawn both ways over the same segments, white over black: the coverage of every direction,
  and which end it's stepped from.
- **Level, upright and diagonal lines** (`aa-axes`): each both ways, at every sixteenth across (a level line from
  along its row's edge, where the core gives it no coverage, to through its pixels' middles), long and short.
- **The vertices' alpha** (`aa-alpha`): `lines-smooth`'s lines with each cell's vertex alpha its number, 0 to 255:
  whether the coverage replaces the alpha or scales it.
- **Blending over a known background** (`aa-blend`): a colored line over 0x80c06020 in eight modes (none, the usual
  source alpha over, additive, fixed and reverse-subtract factors, the destination's alpha, twice the source alpha,
  clear mode), each with and without the alpha test above 0.
- **Textures** (`aa-textured`): every vertex at one texel, or a texel ramp, in replace, modulate and bilinear: which
  texels a line's pixels take (Dead or Alive Paradise's edge pass samples the frame buffer it draws into), and the
  texel's alpha against the coverage (read through blending: the frame buffer's alpha is the stencil's).
- **In 3D** (`aa-3d`): colored and textured lines under identity matrices, against through mode's, some with the
  texture scaled and offset.

Round 8 (about 170 KB, `manifest8.txt`, which lists every case) takes the GE's time again (docs/psp-core.md, part
107). Part 76 fitted a model of the GE's time to round 6's lists, and the owner's captures of games (part 103) show
where it misses: Killzone's scene lists come out 1.8 to 2 times as long as on his PSP (+77 to +98%, as #266's
reviewed decoder reads his captures), most of their time a 512x512 VRAM buffer read as a CLUT32 texture in strips 32
texels wide, which round 6 never drew; Super Monkey Ball
Adventure's lists 2.5 times as short. Its timed lists are round 6's kind, each queued alone and timed to its finish
callback, at 222/111 and 333/166 MHz, and `time-lists` and `time-scenes` at 333/222/166 (the CPU's clock apart
from the bus's, if the PSP grants it) and 266/133 too:

- **Big textures in strips** (`time-strips`): 512-texel-wide textures in VRAM (8888, CLUT32, 5650, CLUT8) and RAM
  (8888 swizzled or not, CLUT8 512x256 swizzled or not, 5650) drawn texel for pixel in strips 8 to 512 wide.
- **Other textures** (`time-textures`): shrunk 2:1 and 4:1, stretched, mipmaps, perspective, a texture walked by
  columns, a texture of its own for each sprite.
- **Pixels** (`time-pixels`): the depth test passing and failing with textures (does a failing pixel fetch its
  texel?), the alpha, stencil and color tests, every blend equation, the texture functions, a game's pixel.
- **Setup** (`time-setup`): squares 8x8 to 64x64 in 3D as games draw them, textured and depth tested; perspective,
  strips, fans, the near plane's cut, culling, slivers.
- **Vertices** (`time-vertices`): the front end alone (culled strips): each part of a vertex in each format,
  indices (and whether reused ones cost less), every kind of light, skinning, morphing, texture coordinates made,
  fog.
- **State between draws** (`time-state`): a texture, a palette, a matrix, a blend or depth function set before each
  sprite, and CALLed lists, as Super Monkey Ball Adventure draws.
- **Lists** (`time-lists`): a list's own time, lists back to back, a list drawn while its stall address is moved on
  (with and without waiting for the GE to reach it), CALLs.
- **A SIGNAL against its list's end** (`time-signals2`): 8 to 1024 sprites or triangles, then a SIGNAL, then the
  FINISH: when the signal callback comes beside the finish, how far ahead of the drawing the GE's front end gets.
- **The display as a program starts** (`display-start`): what `sceDisplayGetFrameBuf` reads before anything is set,
  and which frame buffers set at once the firmware refuses from there.
- **Transfers, clears, overlap** (`time-transfers2`, `time-clears`, `time-overlap`): narrow transfers and frame
  buffer copies; clears over 512x256; whether the GE's front and back ends go on at once.
- **Lists like the captured scenes'** (`time-scenes`): Killzone's, Activision Hits Remixed's, Chains of Olympus's
  and Super Monkey Ball Adventure's, built to what Phobos's profiles of them show.
- **The CPU beside the GE** (`cpu-contention`): the CPU's reads of RAM while the GE draws from RAM and from VRAM,
  and the GE's drawing while a thread reads RAM.
- **The CPU alone** (`cpu.c`: `cpu-alu`, `cpu-latency`, `cpu-memory`, `cpu-syscalls`, `cpu-clocks`): in every
  captured scene that syncs, a PSP's CPU took 7 to 9 times Phobos's time between a loop's waits; these time ALU work,
  branches, loads and stores, multiplies, divides, the FPU's and the VFPU's latencies, memory at working sets from
  1 KiB to 8 MiB (cached, uncached, the scratchpad, VRAM), system calls, a thread switch and the GE's and the
  display's calls, at all four clock settings, and record what the power service grants for each clock call.

Round 8 takes a few minutes. It changes the PSP's clocks part of the time and puts them back after each test.

Round 9 (about 0.1 MB, `manifest9.txt`, which lists every case) takes what the fit of rounds 6 and 8 still guesses
where the 15% gate of #269 misses (docs/psp-core.md, part 110): Ghost of Sparta's menu, Super Monkey Ball
Adventure's story and Killzone's second capture come out too fast, Activision Hits Remixed's 3D menu too slow. Its
timed lists are round 8's kind, at 222/111 and 333/166 MHz, `time-lists2` at all four settings:

- **Anchors** (`time-anchors`), first: 15 of round 8's cases built by round 8's own code at its sizes (strips from
  VRAM and RAM, a texture of its own for each sprite, TEX_FLUSH between sprites, culled strips with float and 8-bit
  positions, the textured fill, 16x16 squares, Killzone's scene list), to show the PSP drawing as it did then.
- **Triangles reading narrow textures** (`time-tristrips`): round 8's strips 8 to 512 texels wide over 512x256, texel
  for pixel, as triangles (two a strip) from seven textures, RAM and VRAM, swizzled and not (8888, CLUT8, 5650);
  sprites over the swizzled VRAM textures round 8 didn't have; squares 8x8 to 64x64 of two triangles tiling the
  target as a mesh does; strips in 3D beside through mode's; strips 32 wide bilinear, and shrunk 2:1, triangles and
  sprites. #269's model takes triangles reading narrow textures as sprites, unmeasured.
- **A texture's loads and the cache's reuse** (`time-loads`): sprites each with a new texture of 64 bytes to 64 KiB
  (drawn whole or 32x32 texels of it), the same texture set again, TEX_FLUSH alone, the address alone without
  TEX_FLUSH (the same or a new one), two textures in turn; 8888, 5650, CLUT8 and CLUT4, swizzled or not, RAM or VRAM;
  textures set, flushes and palette loads with no sprite between; palettes of 16 to 256 colors (8888, 5650, from
  VRAM) before each sprite; mipmaps in 3D (a level set, the level chosen, against a texture without its mipmap).
  Round 8 had a new texture cost about 7.4 microseconds a sprite whether it was 1, 4 or 16 KiB, where the model
  charges by size.
- **Vertex formats** (`time-formats`): the same triangles in every format (positions every format holds exactly):
  8-bit, 16-bit and float positions alone and with a color, texture coordinates or a normal of each width, whole
  vertices of each width; through 16- and 8-bit indices; culled, drawn in 3D and drawn in through mode; strips.
  Round 8's culled strips had 8-bit positions 4.5 times the model's time and 16-bit 1.5 times.
- **Lists again** (`time-lists2`): round 8's `time-lists` with its stall addresses cached (round 8's run left 67 of
  its 80 records empty: the driver says "stall reached" only when the stall address as given is the GE's own, kept with
  its top three bits cut), every case at 222/111, 333/166, 333/222/166 and 266/133.

Round 9 takes about a minute, two at most (its lists about 35 seconds of the GE's time by round 8's rates, the runs'
own overhead and building the lists the rest); it changes the clocks as round 8 does. The owner ran it on 2026-10-18
(`tests/psp/measurements/ge-round9`); part 110 says what it showed.

Round 10 (about 0.2 MB, `manifest10.txt`) takes the cases #269's refit with round 9 still misses, as its agent
specified them from the games' logged state (docs/psp-core.md, part 112; synthetic lists, textures and vertices, none
a game's). Its timed lists are round 8's kind, at 222/111 and 333/166 MHz:

- **Anchors** (`time-anchors2`), first: six of round 9's anchors, and round 8's time-strips 37 with its texture 0,
  256, 512 and 768 bytes past a 1 KiB boundary (round 9 drew it 2% faster than round 8; its texture's place the likely
  cause).
- **Lights** (`time-lights`): round 8's culled strips lit by 1 to 4 directional lights, point lights and spotlights,
  diffuse and with their shine; the shine's power; which of four lights shine; a point light's fall-off; Activision
  Hits Remixed's menu's mixes of spotlights and a directional light, skinned too; strips drawn as that game draws
  them, lit and unlit (its first list is 19% short in the refit).
- **CLUT32** (`time-clut32`): that menu's second list's tiles, 32x34 pixels reading a VRAM CLUT32 texture 1:1 in three
  passes with palette loads, into 5650, and one thing at a time changed (one pass, as 8888, as CLUT8, no loads, no
  texture, sprites, into 8888, swizzled, from RAM, other tile sizes, a buffer drawn just before, bilinear); round 9's
  strips as triangles from the CLUT32 texture. No round had timed CLUT32 in triangles.
- **Wide triangles into 16-bit** (`time-widetri`): its copyright screen's pass, two triangles over 480x272 reading a
  CLUT8 512x256 texture, bilinear, blended, into 5650, and one thing at a time changed; round 9's two wide triangles
  and one sprite into 8888, 5650, 5551 and 4444.
- **Perspective and turned** (`time-perspective`): two triangles over 256x256 under the lens, w 1 at the left to 1,
  1.25, 1.5, 2 and 4 at the right and 1 to 2 down, from three textures; squares 64 and 128 wide; w 2, and 2 to 1;
  squares 32 to 256 wide in through mode, as they are and turned a quarter (u down the screen), from three textures.
- **Mipmaps** (`time-mipmaps`): round 8's 256x256 texture shrunk 2:1 with and without its levels, level 1 alone,
  bilinear within a level, levels blended; the same in CLUT8; Super Monkey Ball's small CLUT4 textures (64x64, 7
  levels; 64x32, 6) in small 3D strips, trilinear with the game's level bias, against nearest and no levels.
- **Narrow strips** (`time-vram8`): strips 4 to 24 texels wide, sprites and triangles, CLUT8 and CLUT4, from VRAM
  not swizzled and swizzled and from RAM swizzled.
- **The CPU beside the GE** (`time-contention`, `time-contention2`): ten GE lists (RAM and VRAM textures read wide,
  fills, vertices from RAM, NOPs, Super Monkey Ball's CALLed lists, RAM-to-VRAM copies, a new texture each sprite,
  that game's small trilinear strips, Ghost of Sparta's skinned lit strips), each alone and while a thread at a
  higher priority runs one of 17 patterns till the list's finish callback (arithmetic; reads within the data cache;
  streaming reads flat out and slowed to a half, a quarter and an eighth; streaming writes; copies; a chain of
  pointers; uncached reads; VRAM reads and writes; a game's mix over 16 KiB to 1 MiB), and each pattern alone for 10
  ms: the GE's time, the thread's turns and their time. The streaming reads again at 333/222/166, the bus apart from
  the CPU. The thread stops after 5 seconds whatever happens, so a stalled GE is reset as ever.

Round 10 takes about two minutes, three at most (its lists about 70 seconds of the GE's time by rounds 8 and 9's
rates, the runs' own overhead and building the lists the rest); it changes the clocks as round 8 does. The owner
ran it on the evening of 2026-10-18; part 112 says what it showed.

Round 10b (about 30 KB, `manifest10b.txt`) tops round 10 up the same night, as #269's agent specified it:
`time-contention3`, six of round 10's GE lists alone and beside a CPU polling through system calls (`sceKernelPollSema`,
`sceGeListSync`, `sceGeDrawSync`, each with `sceKernelGetSystemTimeLow`) or writing 1 MiB before the list is queued
(with and without the cache written back) or once beside it; and `time-placement`, one swizzled RAM texture at offsets
from 0 to 512 bytes past a 1 KiB boundary, controls not swizzled and CLUT8, a palette at two offsets, and CLUT32
swizzled in RAM. From round 10b on the arrays the GE reads are on 1 KiB boundaries (round 10's build had them 64 bytes
off a 128-byte one: part 112). About half a minute. The owner ran it the same night; part 112 says what it showed.

Round 10c (about 30 KB, `manifest10c.txt`; part 114) is for what #269's refit and #279's model of the CPU's traffic
still miss: `time-contention4`, Super Monkey Ball's small trilinear strips (round 10's list H) taken apart (nearest
within the level, bilinear and nearest without levels, untextured, the vertices in VRAM), each alone and beside a
thread streaming reads (flat out and at a half) or running the game's mix over 256 KiB; `time-shrink` and
`time-magnify`, Activision Hits' menu's second and third lists as #269's agent specified them (60 sprites shrinking a
VRAM picture, and the whole list as logged; triangles magnifying a RAM texture into 5650, bilinear and nearest at
each magnification); `time-gamestrips`, round 10's game-style strips taken apart; and `time-anchors3`, seven of round
10's cases again. About half a minute.

### How the GE's comparison works

The program computes nothing itself. `tests/psp/measure.cpp` runs the same program in Phobos's core, through its menu as
a person would (the GE's tests, starting afresh, the tests again, round 3, the probes, round 4, round 4 again over
counts left as the earlier program left them, round 5, round 6, round 7, round 8, round 9, round 10, round 10b, round
10c, leaving), and checks every file is written (that the stall check got past its stall, and the timing records of
rounds 6, 8, 9, 10, 10b and 10c are whole); with `PSP_GE_RESULTS` set to a `results/ge` folder, it lists what differs
from that folder, and for round 6's timing files each case's middle times here and there (`PSP_GE_OURS` keeps the
core's own files for a closer look):

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

### The GE timing's fit

`GE::Cost` (`ares/psp/ge/ge.hpp`), the clocks of the GE timing "PSP", is fitted to rounds 6, 8, 9 and 10's timed lists
(docs/psp-core.md, part 106): `ge-terms.py` counts, for each run of the GE, what each of its terms multiplies, in a
runner built from a patched copy of the source (the core itself carries no counting code), and `ge-fit.py` fits the
clocks to the PSP's times. To fit them again:

```
mkdir -p /tmp/terms
git archive HEAD | tar -x -C /tmp/terms          # a copy of the checkout, which ge-terms.py patches
python3 tools/psp-measure/ge-terms.py /tmp/terms
(cd /tmp/terms && tools/psp-runner/build.sh)     # its last line names the runner
```

Then the program's rounds 10, 9, 8 and 6 in that runner with the GE timing "PSP", each with its terms written to a
file: `EBOOT.PBP` (`make` here, with round 10c) in a memory stick folder's `PSP/GAME/PSPMEASURE`, the round's menu line
chosen with presses (round 10c the first line, round 10b the second, round 10 the third, round 9 the fourth, round 8
the fifth, round 6 the seventh), each run's `results/ge` kept beside its terms, the stick emptied between runs:

```
PHOBOS_GE_TERMS=r10.txt RUNNER STICK/PSP/GAME/PSPMEASURE/EBOOT.PBP --memory-stick STICK --ge-timing psp \
  --frames 30000 --press 60:Down,66:Down!,80:Down,86:Down!,100:Cross,106:Cross! --start-time 1791290096   # as ours10
PHOBOS_GE_TERMS=r9.txt ... --press 60:Down,66:Down!,80:Down,86:Down!,100:Down,106:Down!,120:Cross,126:Cross! ... # ours9
PHOBOS_GE_TERMS=r8.txt ... --press 60:Down,66:Down!,80:Down,86:Down!,100:Down,106:Down!,120:Down,126:Down!,\
  140:Cross,146:Cross! ...                                                                         # as ours8
PHOBOS_GE_TERMS=r6.txt ... --press 60:Down,66:Down!,80:Down,86:Down!,100:Down,106:Down!,120:Down,126:Down!,\
  140:Down,146:Down!,160:Down,166:Down!,180:Cross,186:Cross! ...                                   # as ours6
python3 tools/psp-measure/ge-fit.py PSP_RESULTS r10.txt ours10 r9.txt ours9 r8.txt ours8 r6.txt ours6 --again PSP_AGAIN
```

`PSP_RESULTS` holds the PSP's files of the four rounds (`tests/psp/measurements/ge-round6`, `ge-round8`, `ge-round9` and
`ge-round10` unpacked into one folder), `PSP_AGAIN` the owner's runs of the earlier rounds again on round 10's evening
(`ge-reruns-round10`): a list that moved more than 5% between the two (and more than 4 microseconds) is held out as a
check, as where the program's arrays moved and a texture's place with them. The owner ran round 10's own build (round 10
on its menu's first line); the committed one is round 10c's, whose lines are two further down. `ge-fit.py` prints each
term's clocks under `GE::Cost`'s names, then how far the fitted model misses each test's lists (and the checks:
`time-anchors` and `time-anchors2`, round 8's cases again, the cases outside the model, the lists held out); part 106
has the command that gave the clocks in `ge.hpp`. `contention10.py PSP_RESULTS r10.txt ours10` reads round 10's
time-contention: each list's slowdown beside each CPU pattern, each pattern's RAM traffic, and the GE's contention by
kind of read fitted to them. `contention11.py PSP_RESULTS r10.txt ours10` fits the same with each pattern's traffic as
the CPU's caches count it, and the PSP CPU's pace from the patterns' turns: the lines `GE::Cost::Contention`,
`StreamLines`, `WritebackLine`, `UncachedLine`, `InstructionCycles` and the stalls' cycles in `ge.hpp` (part 113).
`contention11.py --core CORE_R10 PSP_R10 CORE_R10B PSP_R10B` sets the core's own run of rounds 10 and 10b (the runner's
`results/ge` after each round's line) beside the PSP's files: each list's slowdown beside each pattern, both ways.

`contention12.py` fits the CPU beside the GE to rounds 10, 10b and 10c together in the two forms the Super Monkey Ball
refit chooses between, and makes the choice (part 113; the refit's design, step 2). A list's reads of RAM are its shares
as the refit's core counts them (a PRIM's own clocks with its vertices, CALL/RET/JUMP's refetches with the words), u is
the CPU's lines while the list drew, and the forms are:

- the rule, s = 1 + A + B (one `Contention` for every kind, as the core has it now);
- the hybrid, s = 1 / (1/(1 + A) - B): the back end's reads (B: texels, transfers) cost a fixed time a line, and its
  push is at most 0.75 of the window.

```
python3 tools/psp-measure/contention12.py --terms r10.txt --ours ours10 --turns CORE_R10 CORE_R10B --scenes SCENES
python3 tools/psp-measure/contention12.py --core CORE_R10 CORE_R10B CORE_R10C [--counters LOG ...]
```

`r10.txt` and `ours10` are round 10's terms and results as above. Step 2's own came from round 10's own build instead
(its menu's first line, `--ge-threads 2`; the scratch's `refit/terms10.sh`), and the committed EBOOT's recipe above
isn't yet shown to give the same files: that comparison joins step 4a's runner build. `CORE_R10` and `CORE_R10B` are a
runner's own run of rounds 10 and 10b (`results/ge`, any build): the CPU patterns' instructions a turn, from which the
core's slowdowns are worked out as the kernel works them. The PSP's files are read from `tests/psp/measurements` as they
are packed (`--psp` for others). The output's first line names every input as given (in the repository relative to it,
else absolute, without `..`) with its sha256, a folder's that of its files' `sha256sum` lines in the names' byte order
(`cd FOLDER && LC_ALL=C sh -c 'sha256sum -- * | sha256sum'`), and `GE::Cost`'s pace as parsed from `ge.hpp` with
`ge.hpp`'s sha256: a saved output names its inputs. For each form it prints its misses in s and s - 1 by round, list
and rate; the held-out tests; the core simulated against measured8's bounds; the windows at or past the cap; with
`--scenes`, the gate's judged frames predicted from #279's push (part 113's table); the choice; and `GE::Cost`'s lines.
`--weights WB UC` puts both forms at one pair of line weights and `--stalls a` takes the stalls without the time-check
term: the design's prototypes' figures. `--core` sets a runner's run of the three rounds (the refit's, at step 4)
beside the PSP's files, with measured8's bounds; `--counters` reads the build's counter line in runner outputs.

The counter line: step 3's runner prints `ge contention: N windows capped; M spin skips refused, I instructions run in
them, of R` once, at the end of a run with the PSP's timing (a log with none, or with more than one, ends the tool's
run). N counts each window whose due reached the cap, once; M the spin skips refused because the cap binds at either end
of the turns they would skip; I the emulated instructions run in the turns of those refused skips; R all the emulated
instructions of the run. I / R is a proxy for H6's cost: the emulated CPU's instructions, where H6 (the design's §2.3)
takes the host's, the emulation thread's instructions a frame in part 113's eight polling games. If M > 0 in any run,
step 4g's host measurement decides H6.

The choice, as the tool makes it:

- **The line weights (L2).** Each form at its own: WritebackLine 0-1 and UncachedLine 0-1.5 in steps of 0.05, the pair
  with the least squared relative error in s over the 908 pairs. Every check of a form uses that pair; the held-out
  tests fit `Contention` again on their part at it.
- **H1, no worse out of sample.** In each held-out test (round 10c; round 10's game-like rates; lists G, A and E2; round
  10 alone against 10b and against 10c; each bus clock against the other), the hybrid's median |miss in s| is at most
  the rule's plus 0.5 points.
- **H2, better where the forms differ.** With the game-like rates held out, the hybrid's median |miss in s - 1| is at
  most 0.75 of the rule's, and its worst list (A, D, E1, E2, F, G, H) no worse than the rule's worst.
- **H3 (L3).** All of `tests/psp/measured8.cpp`'s bounds for rounds 10 and 10b (round 10's 680 pairs: median 7%, 90%
  within 25%; round 10b's 144: median 1.5%, polling three ways, writing before the list and writing and settling each
  within 8%, beside the writer once C within 1%, A within 10% and, twice as long, 30%, and D, E2, H and I within 100%)
  and the round 10c group's (the RAM-vertex lists' median within 7% and each pair within 20%, the VRAM-vertex lists
  within 7%): on the simulated core here, on the core's own run with `--core` at step 4. Each median is measured8's: the
  misses' sizes sorted, the one at half their count (`errors[size / 2]`, the upper of the two middle ones); step 3's
  round 10c group takes its median the same way.
- **H4.** No window at or past the cap among the PSP's pairs, the simulated rounds and step 1's windows; in the code,
  none in step 4's counters.
- **H5.** Each judged frame within 15% (Super Monkey Ball's exception kept, with a tighter limit, where it isn't, down
  to -21%), and Activision Hits' 3D menu within LIMIT (`--limit`; the owner's, 10%). All six scenes are judged: a
  `--scenes` folder without a `.probe` or a `.log` for one of them, or a scene without a terms file and none of the
  others' to bracket it from, ends the run. A scene judged on provisional inputs (a log without a probe, or a probe
  without its terms file) is named PROVISIONAL in the header, and H5 then holds so far at most, waiting for step 1.
  Without `--scenes`, the fit and H1-H4 print as ever, H5 and the choice as not judged. From a probe, each scene's
  windows bound by T (the CPU's time for its instructions and stalls past the time passed), with T/passed's median and
  largest. A "work" scene with half or more of its windows bound by T may be a loop spinning for the GE instead, which
  the tool's table can't tell: H5 waits for its kind ("holds so far (waits: the kind of ...)", or FAILS if a row
  fails), and the choice says it waits, never a bare "holds" or a final choice beside the warning. The person who has
  checked the scene gives its kind with `--kind ID=work` or `--kind ID=spin` (printed in the header), which settles it.
  `--kind` is for a probed scene only: one judged on its log keeps the table's kind, the one its provisional CPU inputs
  are counted in, and `--kind` for it stops the run as a usage error (argparse's, exit 2, as its other misuses).
- **The exits (L1).** The hybrid if H1-H5 hold; else the rule, if it meets H3 and H5; else stop and report. After the
  code, if H6 fails (the spin test, or the refused skips' turns past 0.5% of a polling game's instructions on the host:
  the counter line above): the rule, if it met H3 and H5 here; else stop and report. A window at or past the cap in step
  1's log or step 4's counters fails H4.

What step 1 gives (`--scenes SCENES`): a folder with each judged scene's files, named by its id (`smba-story`,
`gos-menu`, `ahr-menu`, `ahr-legal`, `coo-diff`, `kz-gp`), each scene with a `.probe` or, until the probe, a `.log`:

- `<id>.terms`: `ge-terms.py`'s file of the probe's own run of the scene, with `PHOBOS_GE_TERMS=<id>.terms` set on it
  (600 frames from its state, the profile from frame 301; the copyright screen's 361, from frame 2). It holds every
  list of its run, the frames before the profile included (nothing in it marks the profile's first frame), and the
  tool averages them all (`scene_list()`). Of a scene drawing more than one list a frame it judges the list with the
  most clocks (place None in the tool's scene table): a fixed place counts from the file's first `S` line, so it can
  land on the wrong list: a list started before the save (running, or done with its completion still to come) has no
  `S` line in the probe's run, so the count starts at the first list to start after the load (Killzone's file starts
  at the frame's small blit list, which place 0 took for its 3D list). It comes from the probe's run because #279
  changed which lists a scene's
  600 frames hold (Super Monkey Ball's 647 became 578), and the files made before came from #269's worktree; #277
  changes none, and the probe still only observes with the terms on. A scene without one is bracketed from the other
  scenes' parts. A terms file with fewer lists (an `S` line each) than the scene draws a frame, or whose judged list
  has no clocks, or a line not UTF-8 or naming a term twice, ends the run.
- `<id>.probe`: the probe's, a line for the run and one for each of the GE's busy windows in the profiled frames (each W
  is one line, broken here to fit):

  ```
  P scene=<id> cpu=<MHz> frames=<n>
  W frame=<n> passed=<cycles> instructions=<n> fills=<n> ifills=<n> writebacks=<n> uncached=<n> vertex_ram=<clocks>
    vertex_vram=<clocks> prim_ram=<clocks> prim_vram=<clocks> word_ram=<clocks> word_vram=<clocks>
    refetch_ram=<clocks> refetch_vram=<clocks> texel_ram=<clocks> transfer_ram=<clocks> all=<clocks>
  ```

  - **A window** is the kernel's own contention window, the one `geContendDue()` and `geContendBound()` charge
    (`Kernel::GeWindow`, to which `geContendNow()` adds each stretch of the CPU's; with the GE on its thread, as
    `geFold()` takes the stretches, the same sums): from its start, the first stretch begun while the GE is busy after
    it was idle (`geContend()` starts the window afresh whenever a stretch begins with the GE done), to its due, the GE
    done (`geDoneAt`) with the window's push in it. A W line for each window, its fields the window's totals: each W
    line is a whole window. `frame` (a number; the tool doesn't use it) is the frame the run is in when the probe
    records the window, at the stretch whose `geContend()` finds the GE done and resets the window; the window written
    after the run's last frame (below) counts under that frame. The GE may have finished a frame earlier, as the reset
    waits for the CPU's next stretch. That matters only at the profile's first frame (301; 2 for Activision Hits'
    copyright screen, whose run goes 361 frames from its state and profiles frames 2 to 361, the gate run's 360 a frame
    later, after the window its state restores: below): a window recorded in it counts there, whole, though its GE
    finished in the frame before. A window still open as the run ends, its GE not yet done at the run's last cycle
    (`geDoneAt` past it), is left out, since a W line is a whole window. One whose GE is done by then is complete,
    though no later stretch has reset it, and the probe writes it. The probe counts the open window it leaves out (0 or
    1). The runner's summary line reports that count, with the open window's cycles and lines, so that a cross-check
    against the kernel's busy totals (which include it) can take it out; it also reports how many lists the probe left
    out of `<id>.lists` (below).
  - **`passed`**: the window's cycles in the PSP CPU's cycles at `cpu=` MHz, the time `geContendDue()` and
    `geContendBound()` set against its instructions and stalls (the kernel counts `GeWindow::passed` at
    `Kernel::CPUFrequency`, 333 MHz: times `cpu` / 333). `cpu`: the CPU's clock through the profiled frames
    (`powerState.cpu`), the same through all of them: a probe whose clock changes mid-profile stops and says so. One
    whose program starts again in the profiled frames, the first profiled frame of a run from boot included
    (`Kernel::loadExec()` powers the kernel on again, its cycles back to 0), stops too, with no probe file, naming the
    frame the program started again in. `frames`: the 1/60 s frames profiled (300 for frames 301 to 600). The probe
    runs with the GE thread Off (option "GE Thread"; since #277 On gives the same sums).
  - **The CPU's counts**, the window's: `instructions` the CPU's own run in it (`GeWindow::instructions`, none waiting);
    `fills` the data cache's lines read in, `writebacks` its lines written back, `uncached` its uncached accesses and
    `ifills` the instruction cache's lines read in, from `geCacheCounts()`'s four counts (`cpu.dataCache`'s `fills`,
    `writebacks` and `uncached`, `cpu.instructionCache.fills`) as `geContendNow()` adds them to the window (`fills` and
    `ifills` together are `GeWindow::fills`; with the GE done part way through a stretch, the share before it).
  - **The GE's clocks** are the running list's, as `geContendDue()` reads them at the window's last stretch: its
    `ramClocks[kind]` and `allClocks`, counted since `pipeStart()` zeroed them as the list started (`Kernel::geOwe()`),
    not the clocks counted while the window was open. The two differ because a list's runs count most of its clocks
    before its window opens: `Kernel::syscall()` charges the CPU's stretch through `geContend()` before it dispatches
    the call, `Kernel::geEnqueue()` then starts the list (`geOwe()`) and runs it as far as it goes (`geRun()`, as a
    stall address's update does further on), counting its clocks, and the window opens only at the next stretch. The
    push `geContendNow()` applies after that read sets only the window's own `pushed` and the kernel's `gePipeAt`,
    `geDoneAt` and `geReachAt`, never the totals, so a window's last stretch reads the same before and after its push.
    Taken then: `texel_ram` is `ramClocks[2]`, `transfer_ram` `ramClocks[3]`, `all` `allClocks` (PRIMs and refetches
    left out), and each split is a running total since `pipeStart()` in the same way, kept where its clocks are charged:
    - `vertex_ram`, `vertex_vram`: the vertices' work, where `GE::oweVertices()` charges it with `ramRead(0, ...)`
      (with the GE thread On, `GeThread::replayWords()`), by the vertex address (through indices, the vertices' base);
    - `prim_ram`, `prim_vram`: `Cost::Prim`, the own start of a PRIM, BEZIER or SPLINE, where `GE::pipePrimitive()`
      adds it to the front end's work (a patch's in `GE::patch()`), by the same vertex address; it reaches neither
      `ramClocks` nor `allClocks` now. A patch's `Cost::Patch`, which `GE::patch()` adds beside it, stays out, as
      `Cost::PatchVertex` does, and step 3 charges the same: the refit's design counts the vertices with their PRIM's
      own start, which `Cost::Prim` is for all three commands, while `Cost::Patch` is the patch's work making its
      vertices, which the fit's shares leave out (`clocks()`), so counting it would give the core shares the constants
      weren't fitted on;
    - `word_ram`, `word_vram`: the list words, where `GE::pipeItem()` and `GE::pipeStop()` charge `Cost::Command`, by
      each word's address;
    - `refetch_ram`, `refetch_vram`: CALL, RET, JUMP and a conditional jump taken, where `GE::runList()`, `GE::run()`'s
      command loop, charges `Cost::Call` or `Cost::Jump` to the front end, by the address jumped to.

    Today every vertex and every word is charged as a RAM read (with the thread Off, `ramRead(0)` only in
    `GE::oweVertices()`, `ramRead(1)` only in `GE::pipeItem()` and `GE::pipeStop()`), so in every window `vertex_ram +
    vertex_vram` equals `ramClocks[0]` and `word_ram + word_vram` equals `ramClocks[1]`. The probe checks both on every
    W line it writes, on its f64 values, to rounding (a relative 1e-9: the splits are summed apart), and stops on a
    mismatch with no probe file, naming the window by its `frame` and its number among the run's windows (counted from
    1 in the order the probe records them) and giving both pairs of values (`vertex_ram + vertex_vram` and
    `ramClocks[0]`, `word_ram + word_vram` and `ramClocks[1]`).

    A state's load restores the kernel's window as the state holds it (`GeWindow`'s `passed`, `instructions`, `fills`,
    `writebacks`, `uncached` and `pushed`) and the held list's `ramClocks` and `allClocks` (`Kernel::serialize()`),
    but not the probe's splits. So the window the load restored carries CPU counts from before the state was saved,
    and one open at the load whose last list began after it passes the identity check with those counts in it. The
    list whose totals the GE holds at the save can also outlive that window, those totals keeping the pre-save clocks
    its splits lack: a list waiting at its stall address stays the running one while the GE is idle (`geRun()` returns
    on `Stop::Stalled`), `geContend()` resets the window once the GE is done, and `sceGeListUpdateStallAddr()` then
    runs the same list on with no `geOwe()`, so no `pipeStart()`, as `sceGeContinue()` does a paused list already
    started.

    So straight after `--load-state` the probe marks both as begun before the load: the window then held (open at the
    load, or done but not yet reset by a stretch: any of its six fields nonzero; the mark cleared with the window) and
    the list whose totals the GE holds (running, waiting at its stall address, or paused, as `sceGeContinue()` runs a
    paused list on with the saved totals; on `GE::probe`, the mark cleared by `pipeStart()`). A profiled window that is
    the marked one, or whose last stretch falls in a marked list, stops the run with no probe file, as the clock check
    does, saying which mark fired (the window's, if both): for the window, "the window ending in frame N began before
    the state was saved:
    profile from a later frame"; for the list, "the window ending in frame N ends in a display list that began before
    the state was saved: profile from after that list ends, or from another state". The identity check would also stop
    a W line whose last stretch falls in a marked list, but naming a mismatch rather than the load, which is why the
    probe has a test of its own; a marked list gets no L line (below). Only a profile from a state's first frames can
    meet the marked window, so the copyright screen's starts at frame 2: in every stand-in state the probe's review
    ran, the window the state restores ended in frame 1 (Chains of Olympus's in frame 2). If the real state's lasts
    longer, the probe stops with the window's message, and the first frame moves later. A list the GE holds at the save
    that lasts into the profiled frames (one kept open with stall-address updates) stops the run at its first profiled
    window, with the list's message.

    A window can span lists: `pipeStart()` zeroes the totals as each list starts, and a queued list starts at the one
    before's finish while the GE stays busy (`Kernel::geStartNext()`, or `sceGeContinue()`), so a W line spanning lists
    carries the last list's totals at its last stretch. That is the kernel's own view, right for H4. But lists mix
    memories: in the probe's review's runs a list holds both memories in 9,490 of Chains of Olympus's 9,492 windows and
    in 5,265 of Ghost of Sparta's menu's 5,267, and Killzone's vertices do in 1,197 of its 2,601. So the RAM shares the
    tool sums over a scene's windows (its `share`) assume that each list of a scene has the scene's share (Activision
    Hits' menu draws 3 lists a frame, its copyright screen 2, Killzone 2). The per-list log (`<id>.lists`, below) lets
    the probe's review check that.

    At master `9f2d0633d`: `geContendDue()`'s read, `ares/psp/kernel/ge.cpp:246`; `pipeStart()`'s zeroing,
    `ares/psp/ge/ge.hpp:897`; `Kernel::syscall()`, `ares/psp/kernel/kernel.cpp:1210` and `:1214`; the kernel's cycles
    set to 0 as `Kernel::loadExec()` powers it on again, `:916` (its `power()` call, `ares/psp/kernel/system.cpp:80`);
    `geRun()`, `ares/psp/kernel/ge.cpp:58-84` (a list given up on, `:63-64`; one at its stall address, `:86`), and
    `Kernel::geEnqueue()`'s address, `:746`, and calls, `:768-769`; a stall address's update, `:793-797`;
    `geContend()`'s reset, `:175-178`; the push, after the read at `:224`, at `:226-229` (`w.pushed`, `:227`; the
    kernel's values, `:228-229`); a queued list's start, `:576` and `:907`, through `geOwe()`'s `pipeStart()`, with
    `Cost::List`'s clocks to the front end, `:297`; a list enqueued at the head, `:752-755`; a list's end,
    `Kernel::geFinished()`'s Completed path, `:550-553` (its SYNC and PAUSE paths, `:536-549`); `sceGeBreak()`'s
    lists thrown away, `:942-957`, and its list paused, `:969-972`; the vertices, `ares/psp/ge/draw.cpp:534` and
    `ares/psp/ge/thread.cpp:757`; `Cost::Prim`,
    `ares/psp/ge/draw.cpp:379` and `ares/psp/ge/curves.cpp:250`, and `Cost::Prim`'s and `Cost::Patch`'s own words,
    `ares/psp/ge/ge.hpp:777` and `:791`; the words, `ares/psp/ge/draw.cpp:398` and `ares/psp/ge/ge.hpp:907`; the
    jumps, in `GE::runList()` (`ares/psp/ge/list.cpp:23`), `:102`, `:104`, `:114` and `:123`; the texels' clocks in
    both `ramClocks[2]` and `otherWork()`, `ares/psp/ge/draw.cpp:282` and `:295`; a state's window and totals,
    `ares/psp/kernel/serialization.cpp:1187`, `:1197` and `:1203-1204`. No other `ramRead(0)` or `ramRead(1)` is under
    `ares/psp/ge`; the totals are also set where the thread is switched, its runs folded or a list started on it
    (`System::threadGE()`, `Kernel::geFold()`, `Kernel::geOwe()`), not with the thread Off.
  - **RAM and VRAM** are told apart by the core's VRAM-window test: VRAM's if the address's physical part
    (`& 0x1fff'ffff`) is in `[Memory::VRAMBase, Memory::VRAMBase + Memory::VRAMWindow)` (VRAM's 2 MiB four times over),
    as `GE::owePrimitive()` and `GE::texelClocks()` test a texture's address and `GE::oweTransfer()` a transfer's ends;
    else RAM's.
  - **As the tool reads it**: each line UTF-8; blank lines skipped; every other line a P or a W line of `name=value`
    fields, none given twice, numbers 0 or more but `scene`'s; one P line, its `scene` the file's id, its `cpu` and
    `frames` above 0; each W line with all the fields above, `passed` and `all` above 0, and `all` at least
    `vertex_ram`, `vertex_vram`, `word_ram`, `word_vram`, `texel_ram` and `transfer_ram` together (to a part in a
    million): `allClocks` holds every `ramRead()` and `otherWork()`, the texels' with the rest of a primitive's
    back-end work. Anything else ends the run, naming the file and the line. So does a `cpu` other than the scene's
    clock in the tool's scene table, naming the file and both clocks, since `regimes()` cuts a push by the table's.
    `all` above 0 holds in practice (in all 92,098 windows of the probe's review's runs), but master doesn't guarantee
    it: a list enqueued with its stall address at its start keeps the GE busy for `Cost::List`'s clocks, which go to
    the front end, not to `allClocks` (`Kernel::geOwe()`), so a window ending then would read `all` 0. The tool still
    stops on such a line. The probe prints every value exactly, as the shortest text that reads back to the same f64
    (`std::to_chars`), since that part in a million is tighter than `%.6g`'s rounding, up to about 5e-6.
- `<id>.lists`: the probe's per-list log, beside `<id>.probe` and written with it, which the tool doesn't read. A line
  for each list that ends in a profiled frame, in the order they end, taken as the list completes, on
  `Kernel::geFinished()`'s Completed path (not on a FINISH after a SYNC or PAUSE signal, which only lets the list go on
  or pauses it), before `Kernel::geStartNext()` starts the next and `pipeStart()` zeroes the totals (each L is one
  line, broken here to fit):

  ```
  L frame=<n> place=<n> list=<address> vertex_ram=<clocks> vertex_vram=<clocks> prim_ram=<clocks> prim_vram=<clocks>
    word_ram=<clocks> word_vram=<clocks> refetch_ram=<clocks> refetch_vram=<clocks> texel_ram=<clocks>
    transfer_ram=<clocks> all=<clocks>
  ```

  `frame` the frame it ends in; `place` its index among that frame's L lines, from 0 (a list the log leaves out takes
  no index), which is not the tool's place (the order lists start in, from the terms file: a list may end in the next
  1/60 s frame); `list` the address the list was enqueued with (`sceGeListEnQueue()`'s or `sceGeListEnQueueHead()`'s)
  as the kernel keeps it, its low 28 bits (`address & 0x0fff'ffff`, `Kernel::geEnqueue()`), in hex, so an address and
  its mirrors print alike (Super Monkey Ball enqueues at `0x48c9fcc0`, which prints `0x08c9fcc0`): it is what tells
  lists apart; then the W line's GE fields in its order and units (GE clocks, as `Cost` counts them), the list's own
  totals since its `pipeStart()`, each kept as a W line's is. A list that doesn't complete gets none: one
  `sceGeBreak()` throws away before it completes (one that already completed keeps its line), one the GE gives up on
  (a bare END, a fault or a SIGNAL the driver can't do: it stays in the queue as running), or one still running as the
  profile ends. A paused list gets its line if it is continued and completes, but not one resumed after another
  list's start: that start zeroes the running totals (`sceGeContinue()` starts a list enqueued at the head through
  `geOwe()`'s `pipeStart()`), and `Kernel::geStartNext()` then resumes the paused list without zeroing them, so its
  totals would also hold the other list's clocks. The probe counts such lists (the summary line, above). W lines are
  unaffected: a W line carries what `geContendDue()` reads, whichever list ran. A list begun before a state's load
  (marked at the load, above) gets none either, and the probe counts it with the others the log leaves out. From the
  log the probe's review compares each list's own share with the scene's, the judged list's especially (Killzone draws
  two lists a frame).
- Until the probe, `<id>.log`: a runner's output with its one `caches:` line (each line UTF-8, as the counters' logs
  too; two `caches:` lines end the run, as two counter lines do). Super Monkey Ball's and the menu's CPU work are then
  the design's ranges, and the figures are marked provisional.
