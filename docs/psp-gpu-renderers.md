# The PSP's GPU renderers: Vulkan and OpenGL

**Status (2026-10-07):** designed, and a Vulkan prototype drawing on the GPU at the software renderer's exactness
(docs/psp-core.md, part 34): every pixel the same as the software renderer's, on Apple's M1 (MoltenVK) and on the
RP6's Adreno 740, in random batches of every kind and setting, pspsdk's samples, and fifteen scenes of the owner's
games. Not in the app or the desktop program yet.

The owner's request (2026-10-06, docs/psp-core.md's Decisions): "Add both a Vulkan AND OpenGL option to the renderer
later for speed. I would still like to be as accurate as possible in those GPU rendering modes though." The software
renderer (`ares/psp/ge`) is the reference: matched pixel for pixel against the owner's PSP where it was measured,
and fast since parts 24 and 30. This document is how the GPU renderers are built so that they draw the software
renderer's pictures, the very same pixels, only on the GPU; what the prototype does; and what comes after.

## The idea: the software renderer's arithmetic, in compute shaders

The usual way for an emulator to draw on a GPU is to translate the console's primitives into the GPU's own: its
triangles, rasterizer, texture units, depth buffer and blending. That can't be exact for the PSP. The GPU decides
which pixels a triangle covers by its own rules (Vulkan's eight bits of sub-pixel precision, its own sample points
and tie-breaking), interpolates in its own way, filters textures with its own weights, blends in fixed-function
units with their own rounding, and has no 16-bit frame buffers laid out as the PSP's (5650, 5551 and 4444 with the
stencil in the alpha bits), no depth buffer arranged as the PSP's VRAM arranges it, no color test, and no dithering
as the GE dithers. Each of those can be approximated; none can be made to give the same bits on every GPU.

paraLLEl-RDP, already in the repository for the N64 (`ares/n64/vulkan/parallel-rdp`, MIT), showed another way:
don't use the GPU's rasterizer at all. It runs the N64's RDP as compute shaders, with the RDP's own integer
arithmetic, and so draws what the hardware draws, bit for bit, on any GPU. The PSP's renderers take that idea (not
its code): **compute shaders that do, for every pixel, exactly what the software renderer does**, the same
operations on the same numbers in the same order. Binning primitives into tiles, deciding which pixels each covers,
blending colors, depth, fog and texture coordinates across them, texturing, and the whole pixel pipeline run on the
GPU, written in GLSL from the software renderer's C++. Where the software renderer works in whole numbers, so do the
shaders. Where it works in floats, the shaders round every step as the host's CPU rounds it (below: "Exactness").

The PSP draws 480x272 pixels, about 130,000, a few times over a frame: tens of thousands of threads' work, each a
few hundred to a few thousand instructions, which a phone's GPU does in a millisecond or two. Giving up the GPU's
fixed-function rasterizer costs little at that size, and buys exactness.

**One set of shaders for both backends.** The shaders are GLSL that both Vulkan (compiled to SPIR-V) and OpenGL
(OpenGL ES 3.2, or 3.1 with `GL_OES_gpu_shader5`, and desktop OpenGL 4.3, compiled by the driver) take, using only
what both have: compute shaders, at most four storage buffers and a uniform buffer, 32-bit numbers only (no 64-bit
or 16-bit types, no subgroup operations), and `precise`. Each backend is a thin layer that makes the buffers and runs
the stages (`GPU::Device`); everything about the PSP is in the shared code.

## Where the GE hands over

`ares/psp/ge` already splits drawing in two (part 24): setting each primitive up into jobs (`draw.cpp`: which rows and
columns it may cover, its edges, what its values are blended from, its filter, all worked out once), and drawing a
job's pixels from that (`raster.cpp`, `four.cpp`, `pixel.cpp`). While a display list runs, jobs wait in a batch (one
frame buffer and depth buffer, no two of its pixels in different rows sharing a byte: `threads.cpp`'s `defer()`), and
a batch is drawn whenever anything could see it: the list ends, the render target changes, a texture or palette or
vertices are read from VRAM it draws over, a block transfer, the CPU touching its pages.

The GPU renderers come in at exactly that point (`GE::Renderer`, `ge.hpp`): with a renderer set, batches always form
(even with one drawing thread), and wherever a batch would be drawn it's handed to the renderer, which draws its jobs
in order into VRAM before anyone could look. That is the whole seam in the GE: a pointer, and five lines in
`threads.cpp` and `list.cpp`. Nothing changes without a renderer.

**What stays on the CPU**, as it is: running the display lists, reading vertices, the transform, lighting, clipping
at the near plane, culling, and setting primitives up into jobs. Those are a small part of the time (part 30's
profiles: setup is 6-19% of the GE thread in the heaviest scenes), they're full of floating point whose rounding the
pictures depend on, and keeping them as one piece of code means the two renderers can't disagree about a vertex.
**What the GPU does:** binning the jobs into tiles, and every pixel: which pixels a job covers, its colors, depth,
fog and texture coordinates there, texturing (texels, filtering, the texture function), and the pixel pipeline
(the depth range, alpha, color, stencil and depth tests, fog, blending, dithering, logic operations, the masks).

## The shaders (`ares/psp/ge/gpu/shaders`)

- `bin.comp`: for each tile of 16x16 pixels, which of the batch's jobs reach it: a bit a job, 32 to a word, from the
  box each job may cover. (A later stage refines this, e.g. a triangle's edges against the tile's corners, and a
  coarser level for batches of thousands of jobs.)
- `raster.comp`: a workgroup a tile, a thread two pixels side by side. Each thread reads its pixels' frame buffer
  and depth buffer words once, takes the tile's jobs in the list's order (the set bits, lowest first), works each
  pixel out as `raster.cpp` does (sprites, triangles, points), textures it (`texture.glsl`) and puts it through the
  pipeline (`pixel.glsl`), keeping the words in registers, and writes them once at the end. Two pixels to a thread
  because 16-bit pixels and depths come two to a 32-bit word, and a word must be one thread's: the batch's rule that
  no two pixels in different rows share a byte (`defer()`) makes each word one pair's, except where a row's last
  pixel and the next row's first share one (a frame buffer exactly as wide as the area drawn), whose halves are
  written with atomic operations.
- `exact.glsl`: the arithmetic that makes the floats come out as the CPU's (below).
- `common.glsl`: the buffers and the records' layout. A batch goes to the GPU as records: its looks (the pixel
  pipeline's and texture's settings: 32 words), its jobs (64 words: the job's numbers as `draw.cpp` set them up) and
  tables (a 2D sprite's texel axes, a word a column and a row).
- `probe.comp`: the arithmetic case by case, for the tests and to find out what a GPU's own `fma()` does.

The buffers: VRAM (2 MiB, the frame and depth buffers' words), the records, textures (decoded texels), and the bins.

**How shaders are built into the program.** `compile.sh` makes `shaders.hpp`: each stage's SPIR-V (glslang) for
Vulkan, and the same GLSL as text for OpenGL, which the driver compiles when the renderer starts. `shaders.hpp` is
kept in the repository, as paraLLEl-RDP keeps its `slangmosh.hpp`, and records the SHA-256 of the GLSL it came from;
`compile.sh --check` (which `tests/psp/run-tests.sh` runs, so CI runs it) compares that with the sources as they are,
needing no compiler, so a change to the GLSL without running `compile.sh` fails the tests. With glslang around, the
check also compiles the GLSL as Vulkan's, OpenGL ES 3.20's and desktop OpenGL 4.30's compute shaders, makes
`shaders.hpp` again and compares it with the kept one byte for byte (glslang's output is the same from run to run),
so a hand edit to the SPIR-V fails too. Why not compile
at build time: the desktop builds and CI (Ubuntu, MSYS2, macOS) have no shader compiler, and requiring one there for
one renderer isn't worth it; the Android NDK has `glslc`, but two ways of making the SPIR-V would make two programs.
Kept SPIR-V is the same on every platform, and reviewable as a diff of the GLSL beside it. OpenGL can't take SPIR-V
on GLES at all (`GL_ARB_gl_spirv` is desktop 4.6 only), so it gets the text; each backend's prelude (`#version`,
how bindings and constants are named) is the only difference.

## Exactness

The software renderer's whole numbers (coverage, the filter, the texture function, blending, dithering, the stencil,
the formats) are ported as they are: 32-bit integer arithmetic is the same on every GPU. The edge functions of
triangles are 64-bit in the software renderer, and the shaders have no 64-bit numbers: they're two 32-bit halves
(`imulExtended`, `uaddCarry`), exact.

Floats are harder. The software renderer blends a triangle's colors, depth and fog as `(c0 * w0 + c1 * w1 + c2 *
w2) / total` in floats, then truncates to a whole level; perspective-correct texture coordinates divide sums of
such products. Most results are then whole numbers (a level, a depth, a texel), so a result one unit in its last
place below a whole number (254.99998 for 255) is a level off. A Gouraud triangle with three equal colors lands on
whole numbers at every pixel, so these near-misses are common, not rare. Every float step therefore has to round as
the CPU rounds it:

- **Additions and multiplications** round to the nearest float on GPUs as on CPUs (Vulkan requires correct
  rounding; the probes check it).
- **Fused multiply-adds.** On ARM64 (the RP6, Apple's Macs: part 24 made ARM64 the reference) clang fuses a product
  into the sum it's part of, rounding once: for the blend above, `c1 * w1` alone, then `c0 * w0` and `c2 * w2` fused
  into it in turn (seen in the compiler's output). On x86-64 without FMA every step rounds apart. The shaders do
  whichever the host does (the `fused` constant, from `GPU::hostFuses()`, which evaluates the very expression), with
  a fused multiply-add that is exact on every GPU: the GPU's own `fma()` where a probe finds it fused, else one built
  from additions and multiplications (Boldo and Melquiond's algorithm with rounding to odd). The tests check both.
  Apple's M1 has a fused one; the RP6's Adreno 740 doesn't (its `fma()` rounds the product first), so there the
  built one runs, and every probe is exact all the same. Getting this wrong shows: drawn with x86-64's rounding on an
  ARM64 host, 4,499 of 6.4 million pixels of the random batches come out a level apart.
- **Division.** Vulkan lets a GPU's division be 2.5 units in the last place off. Measured on 65,536 random quotients:
  the Adreno 740's is off in 28% of them, the M1's (MoltenVK, whose default is fast math) in 31%, and exact only with
  MoltenVK's fast math turned off. `divide()` divides the two significands (1 to 2, read from the floats' bits, so
  numbers below the normal floats too): the GPU's quotient, corrected once from its exact remainder (an `fma`), then,
  of it and its two neighbours, the one whose remainder is least, the even one on a tie. The exponents' difference
  then goes on the exponent, which leaves the bits the host's wherever its quotient is a normal float, for any
  operands (0 wrong on both GPUs, to the ends of the floats). Where the host's quotient is below the normal floats,
  what's drawn depends only on its sign and whether it's zero (a texel axis's floor, a depth, fog, a level), so the
  GPU gives 2^-100 of its sign, or zero where the host's rounds to zero. Without `divide()`, 13,194 of the random
  batches' 6.4 million pixels are apart on the M1.
- **Conversions**: 64-bit edge functions to floats rounded to the nearest (ties to even) by hand; floats to whole
  numbers held as ARM64 holds them; floor() of numbers below the normal floats (which GPUs may take for zero) as the
  CPU takes them; tests for numbers that aren't numbers done on their bits, since a GPU may assume floats always are.
- **Nothing reordered or contracted behind the code's back**: every float that takes part is `precise` (SPIR-V's
  `NoContraction` on every arithmetic instruction, checked with `spirv-dis`). glslang doesn't put `NoContraction` on
  `fma()` itself, which Vulkan would let a driver split; the probe at start-up decides whether the GPU's own is used.
- **Doubles.** The software renderer steps 2D texture coordinates in doubles, which GPUs (phones', Apple's, and GLES)
  don't have. The steps are whole 65536ths and the starts floats, so every sum is a whole number over a power of two:
  the CPU sends the GPU a base and two steps, the GPU adds them in 64 bits and rounds the result to a float exactly.
  That is the double's value wherever no double step rounded, which the CPU checks for each job (sums below 2^53);
  where one might (wild coordinates), the job is drawn by the software renderer. A 2D sprite's coordinates are
  worked out once a column and once a row on the CPU (as `spriteRows()` does: a few hundred values a sprite) and
  looked up.

**Where exactness can't be guaranteed, or isn't yet:**
- A 3D sprite's texture coordinates divide two doubles at every pixel (`raster.cpp`: the coordinate down y over the
  column's 1/w). The prototype leaves those sprites to the software renderer. Planned: the quotient's floor in 256ths
  is all that matters, which a float-float division settles except within a hair of a boundary, where an exact
  comparison of whole numbers (the doubles' mantissas multiplied out) decides.
- Lines aren't ported yet (the same arithmetic as triangles with two weights; next).
- A texture a primitive draws over while reading it: the software renderer reads texels as it draws (texture.cpp),
  an order only one thread can follow; such primitives stay the software renderer's.
- **Numbers below the normal floats.** GPUs may flush them to zero: the M1 (MoltenVK) and the Adreno 740 both do,
  and neither offers Vulkan's float controls to keep them (`shaderDenormPreserveFloat32`). The host keeps them. A
  texel axis's floor reads them from the bits; divisions are exact at any size (above). What's left is `sum3()`,
  the sums of three products: exact only while no product, nor what its rounding leaves out, falls below the normal
  floats (the built `fma()`'s error-free steps need that too, even on a GPU that keeps them). So `take()` leaves to
  the software renderer any triangle with a depth, fog, u, v or q other than 0 outside 2^-40 to 2^40, or a w outside
  2^-32 to 2^32 (`exactRange()`, which works the bounds out: every product is then 0 or at least 2^-72 and every sum
  0 or a normal float, however near its products cancel). On a GPU that keeps them, found so by a probe at start-up
  (`detect()`), and whose own `fma()` is fused, only the upper bounds and w's apply. Where a device offers float
  controls, the shaders run with `DenormPreserve 32` (`SPV_KHR_float_controls`), put into the SPIR-V as it's loaded.
  Games' floats are far inside the bounds; random batches with coordinates of every size and w scaled by up to
  2^48 either way draw the same, and without the bounds they don't (one pixel of 6.4 million on the M1: a negative
  coordinate below the normal floats, the texture's last column on the CPU and its first on the GPU).
- A host whose compiler fuses in yet another way (GCC's `-ffp-contract=fast` may fuse across statements):
  `hostFuses()` says so and the renderer refuses to start, as the GPU couldn't follow.
- Hosts differ already: the software renderer on x86-64 draws a few pixels differently from ARM64 (part 24). The GPU
  renderer follows its own host, so it matches the software renderer it runs beside (an owner's decision below).
- **GPUs' compilers.** On the RP6, Adreno's shader compiler got the green and blue of a blend factor wrong ("255
  minus the destination's color", made as a three-channel vector in a `switch`): 89 of the 2,500 random batches
  apart, though every arithmetic probe was exact. Written a channel at a time, with plain ifs, it's right. The
  shaders therefore keep to plain code (no vectors indexed by a variable, no clever constructs), and the only guard
  against the next such bug is running the differential tests on each family of GPU. A start-up self-test (a few
  hundred random jobs drawn both ways, the GPU renderer refused if any pixel differs) would protect the owner's
  devices from GPUs the tests never ran on.

**How it's verified** (`tests/psp/gpu.cpp`, skipped where there's no Vulkan GPU, as on CI; and scratch runs on the
owner's games): the arithmetic case by case against the host (65,536 random cases of each kind, past the GE's ranges
too); random batches of every kind of primitive and pipeline setting drawn by both renderers over the same random
VRAM, compared byte for byte; pspsdk's samples run by two machines alike but for the renderer, every frame's picture
and all of VRAM compared; the owner's games from boot to a scene, each frame drawn by both from the same state and
compared, reporting the percentage of identical pixels and where they differ. Broken versions (VRAM not copied back,
the other host's rounding, the GPU's own division) must fail them. The tests also build as an Android command-line
program and run on the RP6 (`adb`, from `/data/local/tmp`), which is how Adreno's `fma()` and its compiler's blend
factors were found.

The prototype's results (part 34): on the M1 and on the Adreno 740 alike, every arithmetic probe exact; the 2,500
random batches (37,000 jobs, 6.4 million pixels) without a byte apart; pspsdk's eight samples identical over 90 frames
each, the GPU drawing every job of them. On the M1, fifteen scenes of the owner's games (title screens, menus,
attract modes, Midnight Club 3's night city, Liberty City Stories in the woods), 20 to 30 frames each: 100% of
pixels, and all of VRAM, the same, the GPU drawing all but a few hundred jobs (lines, and Peace Walker's and Midnight
Club 3's textured 3D sprites).

**After review** (the same day). An independent review found the exactness held only for normal floats: the GPUs
flush numbers below them, and the probes' ranges (2^-40 to 2^40) never reached there. Now: `divide()` works on the
significands and is exact for any operands; `take()` leaves triangles whose floats could take `sum3()` below the
normal floats to the CPU (bounds worked out above, tighter than the review's suggested 2^-60 to 2^60, which a near
cancellation could still get under); a start-up probe finds whether the GPU keeps them, and Vulkan's float controls
are used where offered; the probes cover every exponent, numbers below the normal floats, zeros and infinities, and
the random batches have wild coordinates and w. Also: a run's wait is two seconds at most (a minute for a
pipeline's first), after which the device counts as lost (the software renderer draws on, said once; a test with a
pretend device checks it); `probe()` checks its buffers; `hostFuses()` says why `gpu.cpp` must be compiled with the
GE's floating-point flags; and `compile.sh --check` compares freshly compiled SPIR-V with `shaders.hpp`. On the M1
and the RP6, every probe is exact again and the batches and samples draw the same; so on Mesa's lavapipe (Ubuntu
24.04 in Docker, clang, `PSP_GPU_ON_CPU`), the one GPU here that keeps numbers below the normal floats.

## VRAM: the PSP's and the GPU's copies

On the PSP, VRAM is one memory that the GE draws into, the display shows, the CPU reads and writes (games draw
into frame buffers themselves, read pixels back, copy rectangles), and block transfers copy within. Phobos's VRAM is
`Memory::vram`; the GPU renderer works on a copy, and the two must never disagree where anyone looks.

**The prototype** keeps it simple and always right: each run of jobs the GPU draws gets its batch's VRAM pages (every
byte the batch may draw over, as the GE noted them) copied to the GPU before and back after, synchronously. Nobody
else runs meanwhile, so nobody sees anything stale. Textures come from the software renderer's own decoded copies
(exact by construction: every format, palettes, swizzling and DXT, decoded once on the CPU), kept on the GPU while
those copies live.

**The design for speed** keeps VRAM on the GPU and moves it only when someone looks:
- VRAM lives in a GPU buffer for good. Each 4 KiB page is the CPU's or the GPU's at a time. A batch drawn by the GPU
  makes its pages the GPU's (not copied back); a page the CPU writes is the CPU's again.
- Whoever touches a GPU page first waits for the GPU and has the page read back: exactly the protocol the drawing
  threads already have (part 24): `Memory::pointer()` (behind every read, write and copy, the screen's picture among
  them), save states and power wait for busy pages (`Memory::finishDrawing`), and the CPU's compiled loads and stores
  are kept off them (`Memory::vramGuard` takes them out of the page tables). The GPU becomes one more drawer whose
  pages are busy until settled; settling one reads it back.
- Before a GPU batch, the pages the CPU wrote since are copied up. The CPU's writes are seen as decoded textures see
  them (watched pages, `Memory::changed()`), extended to VRAM's pages.
- **Render targets read as textures** never come back: textures are decoded on the GPU from its own copy of VRAM
  (and from RAM's bytes, uploaded by page and watched as decoded textures are now). A frame buffer drawn and then
  sampled for a post effect stays on the GPU. So does a palette loaded from VRAM (copied into a palette buffer on
  the GPU).
- **Block transfers** inside VRAM are a copy shader; from RAM, an upload; into RAM, a read-back of the source.
- **The display**: the shown frame buffer is read back once a frame (480x272x4 bytes, a few hundred microseconds at
  worst on a phone, less where the GPU shares the CPU's memory), as the N64's Vulkan output is copied into the
  window today (`PhobosRunner`'s `mapScanoutRead()`); later, presented straight from the GPU's memory.
- **Read-back points and cost**: the display (every frame), states and screenshots (all of VRAM), the CPU reading a
  page the GPU drew (rare in games but some read pixels back), a transfer into RAM, a texture or palette decoded on
  the CPU (none, once decoding is on the GPU). On the RP6 and Apple's chips the GPU's memory is the CPU's: a
  read-back is a fence and a cache's worth of copying, no bus. Discrete GPUs (desktops) would copy through a staging
  buffer.

**Texture and render-target caches.** There is no render-target cache in the PC emulators' sense: the GPU keeps the
PSP's own VRAM with its own layouts and formats, so a frame buffer is a texture by being where it is. Textures are
decoded on use from the PSP's bytes in every format (5650, 5551, 4444, 8888; palette indices of 4, 8, 16 and 32 bits
through the palette's shift, mask and offset; swizzled; DXT1, 3 and 5), either at each texel (simple, no
invalidation beyond VRAM's own) or into a cache of decoded texels keyed as the software renderer's (`TextureKey`) and
dropped when their pages change. Which is faster on a phone's GPU is to be measured; the prototype uses the software
renderer's decoded copies.

## The backends

- **Vulkan** (`vulkan.cpp`, the prototype's): through volk, loaded at run time, so nothing links against Vulkan and a
  machine without it just has no GPU renderer. Its device functions come from its own table, not volk's globals,
  which the N64's paraLLEl-RDP uses. Buffers in memory both the host and the GPU see (the GPU's own where it has one
  memory), mapped for good. Specialization constants carry the host's rounding and whether the GPU's `fma()` is
  fused. On Apple, MoltenVK (Vulkan on Metal) is a "portability" implementation, which the instance asks for. Each
  run is waited for at most two seconds (a pipeline's first, a minute: a driver may compile the shader only then,
  as Mesa's lavapipe does, taking five and a half); a run that takes longer, or the driver's
  `VK_ERROR_DEVICE_LOST`, marks the device lost (`Device::lost`): the software renderer draws every job from then
  on, and the renderer says so once (`GPU::report`, which the program points at its log).
- **OpenGL** (next): OpenGL ES 3.2 on Android (the RP6's Adreno 740 has it), desktop OpenGL 4.3 elsewhere; the same
  four storage buffers and uniform block (`glBindBufferBase`), the GLSL from `shaders.hpp` after its `#version` and
  `#define`s of the constants, `glDispatchCompute`, `glMemoryBarrier`, and fences (`glFenceSync`) to wait. Buffers
  mapped persistently where `GL_EXT_buffer_storage` is there, else written with `glBufferSubData`. The context: its
  own, on the emulation thread (EGL on Android, SDL's on the desktop), shared with the front end's when frames are
  presented from it.
- Both implement `GPU::Device`: the four buffers, the constants, `draw()` (binning then rasterizing) and `probe()`.

## In Phobos

- **The setting**: "PSP Renderer: Software / Vulkan / OpenGL" in the Android app (Settings, Emulation, beside "PSP
  Drawing Threads") and the desktop program's settings; Software the default until the GPU renderers draw everything
  at least as fast. Handed to the core as an option, as "GE Threads" is ("GE Renderer"); a renderer that can't start
  (no Vulkan, or a host whose rounding it can't follow) falls back to the software renderer and the log says why.
- **Presenting**: as above, the shown frame buffer read back into the picture the front ends already take
  (`Kernel::picture()`), so nothing in the front ends changes at first.
- **Threads**: the GE's own thread records and submits the GPU's work; in the design for speed the GPU draws while
  the CPU runs on, as the drawing threads do now.

## Speed

What the GPU can save is the drawing: on the host, 57% of a frame's time in Midnight Club 3's menu at one thread
(part 30), most of Peace Walker's title. What it can't is the rest: the CPU's emulation and setting primitives up,
which bound GTA and Midnight Club 3 at seven drawing threads already (part 30: the GE's own thread busy all the
time, the drawing threads one sixth). So the GPU renderers should help most where drawing is the limit (Peace
Walker's title, Midnight Club 3's menus, Lumines), and everywhere free the CPU's cores (the RP6's battery and heat,
and the emulation thread's core, which the drawing threads share).

The exact arithmetic costs the GPU: a pixel of a textured triangle in perspective is a few hundred instructions (the
edge functions in 64 bits, a dozen corrected divisions, the pipeline), a few times the cost of a GPU's own
rasterizer and texture units. At the PSP's resolution that's still small: Midnight Club 3's menu draws 2.8 million
pixels a frame, 170 million a second at 60 frames; at even a thousand instructions a pixel that's under a fifth of an
Adreno 740's or an M1's arithmetic.

The prototype isn't fast: it waits for the GPU after every run of jobs. On the M1 (host frames a second, the same 300
frames from the same state; the software renderer on 1 and 7 drawing threads, then the GPU renderer): Lumines' demo
92.6, 165.2, 112.9; Peace Walker's title 87.9, 260.2, 77.5 (four fifths of its jobs, textured 3D sprites, still
drawn by the CPU); Midnight Club 3 at night 17.4, 25.1, 14.4; Liberty City Stories in the woods 29.6, 62.7, 26.4.
About the one-thread software renderer's speed. Copying VRAM's pages and the records costs 0.2-0.4 ms a frame; the
rest is the GPU's time and the waits (7-25 ms a frame): one submission and wait per run of jobs (up to nine a frame
in Midnight Club 3), and kernels in which every thread of a tile looks at every job of the tile. The stages below
take those away: the GPU working while the CPU runs on, binning that sends a tile only the jobs that cover it, and
edge functions in 32 bits where a job's fit.

## The plan

1. **Prototype** (tonight, part 34): the seam, the shaders, the Vulkan backend, the tests; 2D and 3D sprites and
   triangles and points, flat and Gouraud, textured (every format, through the software renderer's decoded copies),
   the whole pixel pipeline; exactness verified on the M1 and on the RP6's Adreno 740.
2. **Everything on the GPU**: lines; 3D sprites' texels (the exact division of doubles); textures and palettes
   decoded on the GPU from its VRAM and RAM's pages; block transfers; clears as fills.
3. **Speed**: VRAM resident with pages owned by the CPU or the GPU, read back on demand through the drawing threads'
   protocol; the GPU drawing while the CPU runs on (records double-buffered, fences instead of waits); several
   render targets a submission; binning refined (edges against tiles, a coarse level); 32-bit edge functions where a
   job's fit; measured against the software renderer on the six scenes of part 30, and on the RP6.
4. **OpenGL**: the GL device (ES 3.2 and 4.3) over the same shaders and tests.
5. **In Phobos**: the setting, the fallback, presenting, a start-up self-test against the software renderer; the
   games compared on the device too (the tests' samples and random batches already are).

Every stage keeps the tests' rule: the software renderer's pixels, byte for byte, in the random batches, the samples
and the games' frames.

## For the owner to decide

- **Whose rounding on x86-64.** The software renderer already draws a few pixels differently on x86-64 desktops
  than on ARM64 (part 24). The GPU renderer can follow the host it runs on (as now: it matches the software renderer
  beside it, and the tests can check it there), or always ARM64's (the same pictures on every computer, but then
  not the x86-64 software renderer's). Making the software renderer itself round as ARM64 everywhere (explicit fused
  multiply-adds) would settle it, at some cost on x86-64 machines without FMA.
- **Textures decoded on the GPU or the CPU.** The GPU (stage 2) avoids read-backs of render targets used as
  textures; the CPU's decoded copies (the prototype's) are exact by construction and cost the CPU's time. The plan
  is the GPU; this can be settled by measurement.
- **The default renderer**, once the GPU renderers draw everything: per device, or Software until asked.
- **"Very nearly exact"**: should a feature whose exact emulation is expensive (3D sprites' doubles) ship on the GPU
  a hair from exact where the software renderer would be slower, or stay on the software renderer until it's exact?
  The plan is exact; the question is whether to wait for it.
- **A self-test at start-up** (a fraction of a second: random jobs drawn both ways, the GPU renderer refused on any
  difference), as a guard against GPU compilers the tests never met, as Adreno's blend factors were.
