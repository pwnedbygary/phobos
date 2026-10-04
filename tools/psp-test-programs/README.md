# psp-test-programs

Real PSP programs for the PSP core's host tests, built from source whenever they're needed, and a script that
compares the pictures Phobos's PSP core draws with PPSSPP's.

## Why they were needed

Most of the core's tests feed it small pieces written by hand: a few instructions, one system call, one display
list. That checks each piece on its own, but not what happens when a real program built with the PSP's own toolchain
runs: the loader taking an ELF, PRX or EBOOT apart; pspsdk's start-up code and its C library calling dozens of
system functions; files, controls, the GE driver and drawing, all together. These programs supply that.

The repository holds no binaries, and no PSP games or firmware, so the programs are built from source each time,
with pspdev's free toolchain. pspsdk's own GU samples are well-known real programs whose pictures can be checked.
PPSSPP's software renderer gives an outside reference for the drawing: it's only run as a program to compare
against, and none of its code is in Phobos.

## What's here

| Program | What it does | Tests |
|---|---|---|
| `hello/` | Prints a line and leaves. Even that uses some 45 system functions from 11 libraries, as pspsdk's start-up code and the C library set themselves up: a first test of the loader and the HLE kernel. Built three ways: as an ELF, a PRX and an EBOOT.PBP. | `tests/psp/loader.cpp`, `kernel.cpp` |
| `system/` | Writes a file and reads it back, reads one the host put there, lists the folder, then waits for a button and reports the analog stick, all through the C library. | `tests/psp/files.cpp` |
| `gu/` | Drives the GE through pspsdk's GU library: clears the screen, has its signal and finish callbacks called, calls one display list from another, copies a picture into VRAM. | `tests/psp/ge.cpp` |
| pspsdk's samples | `copy`, `blit`, `clut`, `blend`, `doublelist`, `cube`, `celshading` and `envmap`, from the toolchain's own examples: copying pictures, textures, palettes, blending, 3D, lighting, environment mapping. | `tests/psp/ge.cpp`, `draw.cpp` |
| `gemeasure.elf` | [tools/psp-ge-measure](../psp-ge-measure/README.md)'s program, run in the core to compare with a real PSP's results. | `tests/psp/measure.cpp` |

- `build.sh` builds them all into a folder.
- `compare-ppsspp.sh` runs `clut`, `blend`, `cube`, `celshading` and `envmap` for one second of the PSP's time in
  both Phobos and PPSSPP's software renderer, and compares the pictures pixel by pixel (taking the closest of three
  frames, since the two start programs at slightly different times).

## Using them

In the `phobos-linux` container, which has pspdev in `/opt/pspdev`:

```
tools/psp-test-programs/build.sh /tmp/programs
PSP_TEST_PROGRAMS=/tmp/programs tests/psp/run-tests.sh
```

Without `PSP_TEST_PROGRAMS` (or with it empty), the tests that need these programs are skipped; CI has no pspdev, so
it skips them. With `PSP_PICTURES` set to a folder, the samples' pictures are saved there.

`compare-ppsspp.sh <programs folder>` needs PPSSPPHeadless, built from PPSSPP's source in the container at
`/opt/tools/ppsspp` (CMake with `HEADLESS=ON`, `HEADLESS_CROSS=ON`, `LIBRETRO=OFF`, a release build).

## What they show so far

- `blit` and `doublelist` draw their pictures pixel for pixel.
- `celshading` is identical to PPSSPP's software renderer.
- `cube` and `envmap` are within 1 level of it on every pixel, and `blend` within 2.
- `clut` is within 2 levels on 99.94% of pixels (the rest sit where filtering rounds at a boundary, which the two
  emulators handle differently).
