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

- `bin.comp`: for each tile of 16x16 pixels, which of the batch's jobs reach it: a bit a job, 32 to a word. A thread
  a job sets its bit (`atomicOr`) in every tile its box touches, a triangle only where none of its edges excludes
  the whole tile (the edge function at the tile's corner where it's largest, in the same 64-bit sums `raster.comp`
  uses, so exact). (Stage 3; a coarser level for batches of thousands of jobs is still to come.)
- `raster.comp`: a workgroup a tile. It gathers the tile's jobs from its bin 32 at a time, in order, and stages
  what deciding coverage needs in shared memory; each pixel finds which of the 32 reach it; then the pixels are
  drawn in rounds, each round sharing the pixels' next jobs (a pixel and its job) out among the 256 threads, so a
  pixel's jobs are drawn in the list's order while threads aren't idle on a small triangle's tile. Each pixel is
  worked out as `raster.cpp` does (sprites, triangles, points), textured (`texture.glsl`) and put through the
  pipeline (`pixel.glsl`), its values held in shared memory meanwhile and written to VRAM once at the end. (The
  prototype had a thread two pixels, each taking every job of the tile in turn.) 16-bit pixels and depths come two
  to a 32-bit word: a pair in the same tile is written whole by its even pixel's thread; a pixel whose neighbour
  isn't drawn (the area's edge, or a row's last pixel sharing a word with the next row's first in a frame buffer
  exactly as wide as the area) changes only its half, with atomic operations.
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
  batches' 6.4 million pixels are apart on the M1. Where the GPU's `fma()` isn't fused (the Adreno), the correction
  is done in whole numbers instead (stage 3): with X, Y and Q the 24-bit significands of the dividend, divisor and
  quotient, the remainder X·2^(23-e) - Q·Y is an exact 64-bit whole number, and each step of Q by one moves it by
  Y, so the nearest Q (the even one on a tie) is a few comparisons away. Near a power of two, where the quotient's
  unit changes, or if four steps don't settle it, the `fma()` way decides.
- **Bounds before exactness** (stage 3). Most blended floats only matter as the whole number they become: a color's
  level (truncated, held to 0-255), a depth (`depthOf()`), fog's amount (`fogAmount()`), the texel or the filter's
  sixteenth a texture coordinate falls in (the floor of it, or of 256 times it). Each of those never goes down as
  the float goes up. So the GPU first works the float out its own way (its products and sums rounded to the nearest,
  as Vulkan requires; its division within Vulkan's 2.5 units), with a bound on how far that can be from the host's
  float; where the whole number is the same at both ends of the bound, it's the host's too, and the exact way
  (dozens of steps for each built `fma()` and corrected division) runs only where the ends fall apart. The bounds
  are counted in roundings (u = 2^-24 of a value): a blend of three products over a total, by the host and by the
  GPU, differ by at most u·(10·p/total + 6·|quotient|), with p the products' magnitudes; the shaders use 2^-20 of
  p/total + |quotient|. A perspective coordinate (three quotients, three products over a divisor that is itself
  such a sum) gets 2^-18 of its products' magnitudes over the divisor plus |coordinate|·(the divisor's own
  magnitudes over it, plus 1), and only where the divisor is at least 2^-10 of its magnitudes and between 2^-120 and
  2^120 (so that its own error is small beside it and the division keeps its 2.5 units). 2^-100 more on every
  bound covers results below the normal floats, which a GPU may flush. A value or bound that isn't a finite number
  goes the exact way. Taking the uncertain pixels out to a second pass was tried and was slower every way: on the
  Adreno an extra pass a round costs more than the exact path's divergence saves.
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
  column's 1/w). Since stage 3 the CPU works that axis out per pixel with the software renderer's own expressions,
  a table the shader reads (`SpriteDivided`); the GPU does the rest of the sprite. Exact by construction, and cheap
  (a sprite's few hundred values). A float-float division with an exact whole-number check at boundaries would
  move it to the GPU, if it ever shows in a profile.
- Lines are drawn by the GPU as points: `GE::linePixels()` is `lineRows()`'s own computation, shared, so each point
  carries the very values the software renderer gives that pixel.
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

**Where stage 3 has got to.** The GPU no longer waits after each run: runs go into a ring of three slots (records,
bins and parameters each its own, a fence each) and the renderer goes on; batches are drawn on a thread of the GE's
own (`GE::launch()`), so the GE's thread sets up the next meanwhile. A page the GPU draws over is the GPU's
(`owned`) and the GE's owed pages make whatever reads it (transfers, `Memory::pointer()`, the display) wait, as for
the drawing threads. But the waiting point is coarse: `Renderer::finish()` waits for everything submitted and
copies every owned page back, and the GE calls it at each settle, including where the CPU's decoded textures need a
render target's pages. Midnight Club 3 finishes 10 times a frame, Liberty City Stories 2.5, Lumines 3.3. Not done
yet: textures, palettes and block transfers on the GPU (textures are still the software renderer's decoded copies,
uploaded), and finishing only the pages someone reads. Those are what makes VRAM truly resident.

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

The prototype wasn't fast: it waited for the GPU after every run of jobs. On the M1 (host frames a second, the same
300 frames from the same state; the software renderer on 1 and 7 drawing threads, then the GPU renderer): Lumines'
demo 92.6, 165.2, 112.9; Peace Walker's title 87.9, 260.2, 77.5; Midnight Club 3 at night 17.4, 25.1, 14.4;
Liberty City Stories in the woods 29.6, 62.7, 26.4.

**Stage 3 so far** (part 35): no waits per run, the GPU drawing beside the GE, VRAM kept on the GPU between runs,
lines and 3D sprites on the GPU, exact binning, drawing in rounds, a tile's jobs gathered 32 at a time, bounds
before exact arithmetic. Every scene below is 0 pixels and 0 VRAM bytes apart from the software renderer. Host
frames a second, 300 frames, software on 1 and 7 threads, then the GPU:

| Scene (game) | M1 | RP6 |
|---|---|---|
| Midnight Club 3, night race | 23.8, 45.1, 26.1 | 21.1, 23.9, 6.0 |
| Midnight Club 3, profile menu | 24.2, 50.6, 26.3 | 23.5, 26.1, 7.5 |
| Liberty City Stories, park edge | 38.0, 80.9, 38.9 | 36.9, 49.0, 10.8 |
| Liberty City Stories, woods | 38.1, 111.6, 39.1 | 38.4, 50.7, 11.5 |
| Peace Walker, title | 103.9, 414.3, 150.8 | 66.0, 224.9, 49.9 |
| Lumines, demo | 112.6, 262.5, 113.0 | 90.9, 122.6, 33.1 |

(On the RP6 before stage 3 the GPU drew 1.3 to 2.2 frames a second in the 3D games.) The M1 is now at or above the
one-thread software renderer everywhere, and the RP6 far below it: the target (full speed with headroom on the
RP6) is a long way off.

**Where the GPU's time goes** (GPU timestamps around each run; ms of GPU time a frame; "fixed" is with every job
skipped at the pixel and no coverage computed, so binning, staging, the rounds' barriers and VRAM's reads and
writes; "coverage" adds which staged jobs reach each pixel, the 64-bit edge functions included):

| | Adreno 740, LCS park edge | Adreno 740, MC3 race | M1, LCS park edge | M1, MC3 race |
|---|---|---|---|---|
| all of it (before part 35's shaders) | 67 | 87-92 | | |
| all of it, now | 49.3 | 85.4 | 22.4 | 24.8 |
| fixed | 12.5 | 15.7 | 6.0 | 8.7 |
| coverage | 11 | 11.6 | 1.9 | 5.3 |
| what fma() built from adds costs (before) | 22 | 10 | 0 (fused) | 0 |
| what the corrected division costs | 19.5 (before) | 8 (before) | 0.4 | 0.9 |

- **Synchronous waits and VRAM round trips.** The CPU's wait at each finish shows as "waiting": on the RP6, 77.5 ms
  a frame in Midnight Club 3 and 44.3 in Liberty City Stories (on the M1, 17 and 13.6). That's mostly the GPU's own
  time, waited for 10 and 2.5 times a frame, so the CPU and the GPU barely overlap. Copying pages and records costs
  2.6 and 1.4 ms a frame on the RP6 (0.4 and 0.2 on the M1). Finishing less often needs textures on the GPU and
  finishing only the pages that are read.
- **Every thread checking every job.** Exact binning sends a tile only the jobs whose box and edges reach it, and
  the gathering stages them 32 at a time, but each pixel still tests all 32 (coverage, 11 ms on the Adreno), and the
  fixed part (12-16 ms) is paid by every tile of every run whether it has jobs or not: a run dispatches the whole
  area's tiles (510 at 480x272), and Midnight Club 3 has 10 runs a frame. A coarse bin level, compact per-tile job
  lists, and dispatching only tiles with jobs are the next steps.
- **The cost of exactness.** On the M1 (fused `fma()`, fast division) it's under a millisecond. On the Adreno it was
  most of the drawing: the built `fma()` (Boldo and Melquiond's, a dozen operations for each) and the division's
  correction. The bounds and the whole-number division took Liberty City Stories from 67 to 49 ms. Midnight Club 3
  gained little: its perspective coordinates are often uncertain under the bound (a 256th of a texel is a few
  roundings at coordinates of hundreds of texels), so the exact way still runs. The 64-bit edges weren't measured
  apart; they're inside coverage's 11 ms and the drawing.
- **The Adreno's shader** (`VK_KHR_pipeline_executable_properties`): `raster.comp` compiles to 27,266 instructions,
  27 registers and some scratch memory, and the driver reports 18% of the shader processors busy (with a fused
  `fma()`: 15,198 instructions, 25%). It's bound by latency and occupancy, not arithmetic. Making it small (loops
  not unrolled, 11k instructions) made it slower, as did a second pass for uncertain pixels, a single call site for
  the exact arithmetic, and staging all of a tile's words at once.

**Bit-exact formulations that could run natively** (for the next part):
- 32-bit edge functions for jobs whose box is small enough that every edge value and step fits 32 bits (decided per
  job on the CPU from `draw.cpp`'s own numbers; most of a game's triangles are a few dozen pixels across).
- Hoisting per-primitive work out of the pixel: each corner's 1/w and the weights' total are per triangle, not per
  pixel; but the software renderer divides at every pixel, so hoisting would change its rounding (below).
- Fewer, larger runs and only tiles with jobs dispatched; a smaller workgroup or two pixels a thread for occupancy.

**Changes to the software renderer worth proposing** (not made: each changes its pixels and would have to be checked
against the owner's PSP with `psp measure` first):
- **No fused multiply-add on the host** (raster.cpp compiled with `-ffp-contract=off`, or the blend written so the
  compiler can't fuse it). The GPUs would then need no `fma()` at all (each product rounded, as every GPU does
  natively), so the built `fma()` goes, and ARM64 and x86-64 would draw the same pixels. On the Adreno that was worth
  about 22 ms of 67 in Liberty City Stories. Whether the PSP's pixels are nearer one or the other is for the
  measurements to say.
- **Interpolation in fixed point, if that's what the PSP does.** A hardware rasterizer of its day would likely step
  colors, depth and texture coordinates in fixed point rather than divide floats per pixel. If measurements show
  that, emulating it would bring the software renderer nearer the PSP and make the GPU's arithmetic whole numbers,
  exact natively. The largest possible win and the most work: it needs the PSP's interpolation measured first.
- **Reciprocals per primitive**: 1/w per corner and 1/total per triangle, then multiplications per pixel. Fewer
  divisions on both sides; it changes pixels slightly, so it also needs `psp measure`.

**Stopped** (part 35's end): timed by GPU timestamps at the bottom of the pipe, the fixed costs are small and the
pixels' own work is most of the Adreno's 75-113 ms a frame; specialized shaders, tiles with jobs only and shared
bin words were no faster; the software renderer without fused multiply-adds matches the PSP identically and speeds
no GPU up. The owner moved the GPU renderers to the GPU's own rasterizer (2026-10-07).

## Upscaling (designed, not built)

The owner's decision: an internal resolution factor N (2x to 8x and more) inside the same compute renderer, as
paraLLEl-RDP has one for the N64 (its approach only). At 1x nothing changes and everything stays exact. How it
fits this renderer:
- **Two VRAMs.** The PSP's VRAM (2 MiB, what the CPU, transfers, states and the tests see) stays as it is, drawn at
  1x. Beside it, a scaled VRAM on the GPU: each page that's been a render target has an N×N-times-larger image
  (color and depth). Drawing at Nx writes the scaled image; the 1x VRAM stays the PSP's view.
- **Keeping the PSP's view right.** Either the 1x picture is drawn as well (exact, the renderer as it is now; the
  scaled picture costs N² more on top), or it's made from the scaled one where nobody reads it exactly (cheaper,
  not exact). The first is the safe default: what the CPU reads, block transfers and save states are always the
  PSP's own bytes. A page the CPU writes drops its scaled image (it's 1x again, scaled up as a texture when read).
- **Rasterizing at Nx.** A pixel at Nx is a sub-pixel position in the PSP's 12.4 fixed point: the edge functions
  and blends are the same expressions, evaluated at (x + i/N, y + j/N). The tile is still 16×16 scaled pixels; the
  bins cover N² times as many tiles. The bounds and the exact way are unchanged (they don't depend on where the
  pixel is), so at Nx the arithmetic is still the software renderer's at that point.
- **Textures from render targets** read the scaled image where one exists (with coordinates scaled by N), else
  the 1x texels. 2D sprites (menus, text) draw at Nx with their texels' own resolution, so they look as sharp as
  their textures allow.
- **Transfers** within VRAM copy the scaled images too (a copy shader at Nx); from RAM they drop them; into RAM
  they read the 1x bytes.
- **The display** shows the scaled image of the shown frame buffer where it has one, else the 1x picture scaled.
- **Cost.** N² times the pixels: 4x is 16 times the work, so it needs the native renderer at a sixteenth of a frame
  or better. That's why the native speed comes first.

## The plan

1. **Prototype** (tonight, part 34): the seam, the shaders, the Vulkan backend, the tests; 2D and 3D sprites and
   triangles and points, flat and Gouraud, textured (every format, through the software renderer's decoded copies),
   the whole pixel pipeline; exactness verified on the M1 and on the RP6's Adreno 740.
2. **Everything on the GPU**: lines (done, as points); 3D sprites' texels (done, the down axis a table from the
   CPU); textures and palettes decoded on the GPU from its VRAM and RAM's pages; block transfers; clears as fills.
3. **Speed**: VRAM resident with pages owned by the CPU or the GPU, read back on demand through the drawing threads'
   protocol (owned and owed pages done; finishing only what's read, not yet); the GPU drawing while the CPU runs on
   (done: a ring of three slots, fences); several render targets a submission; binning refined (edges against
   tiles done, a coarse level not yet); 32-bit edge functions where a job's fit (not yet); measured against the
   software renderer on the six scenes of part 30, and on the RP6 (part 35). The target is full speed on the RP6
   with headroom (the owner's decision below), not only beating the software renderer.
3a. **Upscaling** (below), once the native speed is there.
4. **OpenGL**: the GL device (ES 3.2 and 4.3) over the same shaders and tests.
5. **In Phobos**: the setting, the fallback, presenting, a start-up self-test against the software renderer; the
   games compared on the device too (the tests' samples and random batches already are).

Every stage keeps the tests' rule: the software renderer's pixels, byte for byte, in the random batches, the samples
and the games' frames.

## The owner's decisions (2026-10-07)

- **Whose rounding on x86-64:** the host's own. The GPU renderer matches the software renderer it runs beside (as
  now), so on x86-64 desktops it follows x86-64's few different pixels, and the tests check it against that.
- **Textures decoded on the GPU** (stage 2), so render targets used as textures never come back to the CPU.
- **The default renderer stays Software** until the user picks a GPU renderer in Settings, even once the GPU
  renderers draw everything.
- **Exact before shipping:** a feature whose exact emulation isn't done yet stays on the software renderer until it
  is (textured 3D sprites, with their doubles, among them); nothing ships on the GPU a hair from exact.
- **A self-test at start-up:** before a GPU renderer draws a game, a fraction of a second of random jobs drawn both
  ways; any pixel apart, and the software renderer draws instead (and the app says so once).
- **Order:** speed first (stage 3 above: VRAM kept on the GPU, the GPU's work overlapping the CPU's, finer binning,
  with stage 2's textures and transfers on the GPU that it needs), then the OpenGL backend (stage 4) over the same
  shaders.
- **The target** (later the same day): full speed in every game on the RP6 at the PSP's own resolution, with large
  headroom, enough for upscaling. Faster than the seven-thread software renderer is a waypoint, not the goal (the
  owner: the Adreno 740 runs other PSP emulators at many times the PSP's resolution).
- **Upscaling:** the GPU renderers stay exact at the PSP's resolution and gain an optional internal resolution
  factor (2x to 8x and more) inside the same compute renderer, as paraLLEl-RDP has for the N64: exact at 1x, the
  same rules at higher factors; render targets, textures read from render targets, block transfers and the display
  handled at the scaled size, the PSP's own view of VRAM kept correct. The speed work is to be shaped so that this
  follows; it's built once the native speed is there.
