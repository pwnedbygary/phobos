# psp-ge-measure

A homebrew program that records what a real PSP's GE (its graphics chip) draws, and how its controller driver times
its reads, in the cases where Phobos's PSP core follows someone else's reading rather than measurements of its own.

## Why it was needed

Parts 8 to 12 of the core (the controls, the GE's display lists, drawing in 2D and 3D, lighting) are written from
public sources: pspsdk's headers, uOFW's study of the PSP's firmware, and PPSSPP's software renderer, read for
behavior only. They're good references, but they aren't the hardware. PPSSPP's authors mark some of their own rules
as unverified, and pspsdk's documentation and uOFW disagree about whether a second `sceCtrlReadLatch` waits.
Getting rules like these wrong shows up as colors a level off, pixels drawn twice or not at all at the edges of
shapes, textures shifted by a texel, or lighting a shade too bright. This program draws each case on a real PSP,
reads back the exact pixels and saves them, so the core can be compared with the hardware itself.

## What it records

Each case is drawn into VRAM, read back as it is, and written to `results/` beside the program: 64 files of
little-endian 32-bit words and a `manifest.txt`, about 15 MB, in a few seconds. The cases:

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

## How the comparison works

The program computes nothing itself. `tests/psp/measure.cpp` runs the same program in Phobos's core and checks every
file is written; with `PSP_GE_RESULTS` set to a results folder, it lists what differs from that folder
(`PSP_GE_OURS` keeps the core's own files for a closer look):

```
tools/psp-test-programs/build.sh /tmp/programs   # builds gemeasure.elf among the test programs
PSP_TEST_PROGRAMS=/tmp/programs PSP_GE_RESULTS=<results folder> tests/psp/run-tests.sh
```

Against PPSSPP's software renderer (its headless build running the `SMOKE` version), 61 of the 64 files match. The
ones that differ are the questions for the PSP:

- a sprite whose right or bottom edge runs exactly through pixel middles;
- which texel a shrunk sprite takes (the core samples a sprite at each pixel's middle, PPSSPP 7/16 in);
- the fog across a 3D sprite whose corners lie at different depths;
- and, in a file that does match, which way a spotlight's direction points (PPSSPP's reading has it point toward the
  light, which is unusual).

The user's PSP has answered these and more (docs/psp-core.md, "Results from the user's PSP"). Sprites and triangles
are sampled at each pixel's middle, a sprite's left edge reaching a sixteenth further left. Texture coordinates are
stepped from a primitive's leftmost corner. The spotlight points toward the light, as PPSSPP reads it. With the
core fixed to match, 49 of the 63 pictures are identical to the PSP's.

## Building and running

1. Build with pspdev's toolchain (the `phobos-linux` container has it in `/opt/pspdev`): `make` gives `EBOOT.PBP`;
   `make SMOKE=1` gives a version for an emulator, which starts at once and leaves when done.
2. On a PSP with custom firmware that runs homebrew, copy `EBOOT.PBP` to `PSP/GAME/GEMEASURE` on the memory stick,
   start it, and press X.
3. Copy `results/` back, and run the comparison above.

The findings go into [docs/psp-core.md](../../docs/psp-core.md), under "Measuring the GE and the controller on a
PSP".
