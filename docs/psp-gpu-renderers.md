# The PSP's hardware renderers: Vulkan, then OpenGL

**Status (2026-10-07):** a Vulkan renderer drawing the owner's games with the GPU's own rasterizer, texture units
and blending, at the PSP's resolution, on Apple's M1 (MoltenVK) and the RP6's Adreno 740 (docs/psp-core.md, part
36). On the RP6 it draws the six benchmark scenes 1.5 to 2.3 times as fast as the software renderer on seven threads
(below: "Speed"). It isn't exact: blending rounds differently on the GPU, so 28-65% of a scene's pixels come out the
same as the software renderer's, nearly all of the rest a level or two apart ("Accuracy"). Not in the app or the
desktop program yet, and no upscaling or OpenGL yet ("The plan").

The owner's direction (2026-10-07): a hardware renderer as PPSSPP has one (the GPU's own rasterizer, texture units
and blending; shaders generated from the GE's state; upscaling), Vulkan first and OpenGL after. The software renderer
(`ares/psp/ge`) stays the exact one and the default. **PPSSPP's GPU backends (`GPU/Common`, `GPU/Vulkan`, `GPU/GLES`)
informed this design: how it splits drawing into a draw engine, a framebuffer manager and a texture cache, what it
keeps on the GPU, and which of the GE's features it approximates and how. None of PPSSPP's code is used, copied or
translated**; what's here is Phobos's own, written in the style of the rest of the core. (The HLE kernel's rule is
unchanged: clean room.)

## Before this: the compute prototype, and why the direction changed

Parts 34 and 35 (their branches) built a GPU renderer of another kind, after paraLLEl-RDP's idea for the N64: compute
shaders doing, for every pixel, exactly what the software renderer does (the same edge functions in 64 bits, every
float rounded as the host rounds it), so that the GPU drew the very same pixels. It did: 0 pixels apart from the
software renderer in random batches, pspsdk's samples and fifteen scenes of the owner's games, on the M1 and the
Adreno. But it was slow where it mattered. On the RP6 it drew the 3D scenes at 6 to 11.5 frames a second against the
seven-thread software renderer's 24 to 51: each pixel of a perspective triangle took hundreds of instructions of
exact arithmetic (the Adreno has no fused multiply-add to build it from cheaply, and the corrected divisions cost as
much again), and the shader was bound by latency and occupancy, which neither specializing it nor binning more finely
helped. Exactness on a GPU costs more than a phone's GPU has to give, and the owner wants full speed with room to
upscale. So the GPU renderers now do what PPSSPP's do: let the GPU draw as GPUs draw, and approximate what it can't do
as the PSP does. The compute work is committed on `cursor/psp-gpu-speed-2b67`; pieces of it are reused here (the
seam's idea, VRAM pages owned by the renderer and read back on demand, the lost device's fallback, the device setup,
the differential harness, now measuring closeness instead of identity).

## Where the GE hands over (`GE::Renderer`, `ge.hpp`)

The GE does everything up to the pixels just as it does for the software renderer, and hands the renderer each
primitive on the screen:
- `begin(ge, look, through, region)` once a PRIM (or a curved surface's run, which goes through PRIM's path): the
  primitive's settings, worked out by the GE (`Look`: the pixel pipeline's state, the texture and its decoded copy,
  the texture function), whether it's 2D, and the region it may draw in (the scissor, narrowed in 2D to where its
  vertices reach).
- `triangle(a, b, c)`: a triangle's corners on the screen, culled and clipped already, with their colors (lit),
  depth, fog, texture coordinates and their perspective divisor.
- `sprite(job)`: a sprite as the GE sets it up for its own drawing (`rectangle()`'s job): exactly the pixels it
  covers, and how its texture coordinates step across them, or, in 3D, how they follow the perspective.
- `point(at)`, and `line(job, pixels)`: a line as the pixels the GE's rule lights (`linePixels()`), each its own
  values.
- `submit()` (a list's end: hand what's recorded to the GPU, don't wait), `finish()` (every page the renderer owns
  put back in memory's VRAM), `written(page)` (someone changed a watched page), `forget()` (VRAM replaced whole: a
  state loaded, the power), and `holds(texture, rows, columns)` (below: "Render to texture").

With a renderer that's `ready()`, `draw.cpp` gives it the primitives instead of making jobs for the drawing threads;
when it isn't (the GPU lost), the software renderer draws. Nothing changes without a renderer.

## The vertex path: transform on the CPU

The vertices are read, skinned, morphed, transformed, lit, clipped at the near plane and culled by the GE's own code
(`transform.cpp`, `lighting.cpp`, `draw.cpp`), as for the software renderer: "software transform", in PPSSPP's terms.
That keeps one implementation of everything that decides where a vertex lands and what color it is (full of floating
point whose rounding the pictures depend on), keeps the two renderers from disagreeing about a vertex, and costs
little: setting primitives up was 6-19% of the GE thread in the heaviest scenes (part 30). The renderer then only
converts (`gpu.cpp`'s `vertex()`):
- **Position**: the screen position in the GE's own sixteenths of a pixel (`fixed()`), so the GPU rasterizes the very
  positions the GE would; depth 0-65535 passed through to the fragment shader (written as the GPU's depth, with the
  GE's depth range test done in the shader); in 3D, w (the clip w), so the GPU's interpolation follows the
  perspective. The GE samples at a pixel's middle, as GPUs do, and the GPU's sub-pixel precision (8 bits on both
  GPUs tested) is finer than the GE's 4.
- **Colors** (primary and specular), fog and depth blend without perspective (`noperspective`), as the GE's do;
  texture coordinates with it (u, v and q, so q-projected coordinates come out right), but straight in 2D.
- **Flat shading**: each corner takes the provoking (last) corner's colors.
- **The filter**: the GE chooses nearest or linear per primitive from how many texels a pixel covers; the renderer
  makes the same choice per triangle (`chooseFilter()`) and passes it as a flag.
- **2D sprites** cover exactly the pixels the GE's `rectangle()` covers (two triangles over the job's pixel
  rectangle), and their texture coordinates are not interpolated by the GPU: the fragment shader steps them as the GE
  steps them, from the job's first coordinate, step and starting sixteenth (`fma()` over the pixel's middle,
  `precise`), so a sprite's texels are the GE's, pixel for pixel. (Interpolated by the GPU they came out a texel apart
  in a grid across Liberty City Stories' text and HUD.) A sprite turned a quarter swaps the axes; a sprite whose fog
  differs from half to half is drawn as two.
- **3D sprites** have their corners at the pixel edges, with w and the texture coordinates times w set so that the
  GPU's perspective interpolation is the GE's straight lines in 1/w.
- **Points and lines** are pixel-sized squares, each with its own values.

Consecutive primitives with the same settings go into one draw.

## Shaders generated from the GE's state, and their caches

One vertex shader and one fragment shader (`ares/psp/ge/gpu/shaders`, GLSL compiled to SPIR-V by `compile.sh`;
`shaders.hpp` keeps the SPIR-V and the sources' hash, and `tests/psp/run-tests.sh` checks it). The fragment shader
does the GE's pixel pipeline up to blending, in the GE's own whole numbers where it can (as `texture.cpp` and
`pixel.cpp` do it): the texel looked up with `texelFetch` and filtered as the GE filters (four texels weighed in
sixteenths, wrapped or clamped at the GE's sizes, not by the GPU's filtering), the texture function (doubled, with or
without alpha), specular shine, the alpha test, fog and the color test; then the parts of blending that depend on the
pixel alone, dithering and the frame buffer's format where the GPU won't blend after, and the logic operations the GPU
can't do.

Every choice the GE's state makes there is a **specialization constant** (15: texturing, the texture function, the
alpha and color tests' comparisons, fog, the depth range test, clear mode, the source and destination factors'
parts, the alpha written, dithering, the format to narrow to, the logic operation, clamping, and a texture's 16-bit
format when it's a frame buffer's). So each mix of settings a game uses is a shader of its own, compiled by the driver
without what it doesn't use: generated shaders, made by the driver from one source rather than by writing GLSL text
at run time as PPSSPP does. The per-draw values (references, masks, colors, the dither matrix, the texture's size)
are push constants.

A **pipeline** is keyed by the 32-byte `Pipeline` struct: the constants, and what the GPU's fixed stages are set to
(blend factors and operation, color write mask, depth test and write, stencil test and operations, the GPU's own
logic operation). The backend makes each once, through a Vulkan pipeline cache, and keeps it (a game uses a few dozen:
7 in Peace Walker's title, 45 in Midnight Club 3's race). The stencil masks and reference, the blend constant, the
viewport and scissor are dynamic state, so they don't multiply pipelines. Pipelines made are kept for the session; a
pipeline cache kept on disk between sessions is for the app wiring.

A GPU without dual-source blending gets the fragment shader built without its second output (`SINGLE`), and the
factors it needs approximated (below).

## Frame buffers on the GPU ("targets")

Each frame buffer the GE draws into (its address, row width and format) is a **target** on the GPU: an 8888 color
picture as wide as the row and as tall as fits in VRAM (at most 512 rows), and a depth and stencil picture (32-bit
float depth with 8-bit stencil, or 24-bit depth where that's all there is), drawn in one render pass. The PSP's
stencil is its frame buffer's alpha (none in 5650), so the GPU's stencil holds it, and the color's alpha follows it
where the renderer knows what's written.

**Filled from memory.** Before a target is first drawn into, the rows a PRIM reaches are filled from memory's VRAM:
the pixels widened to 8888, the stencil from their alpha, the depth from the depth buffer's bytes. Its bytes are then
watched (`Memory::watch()`): whoever changes them (the CPU, a block transfer, a texture upload, another target's
pixels put back) makes it stale, and it's filled afresh before it's next drawn into.

**Owned until needed.** What's drawn stays on the GPU. VRAM's pages under the pixels drawn are the renderer's: busy
(`Memory::busyPages`), exactly as the drawing threads' batches make them, so anyone touching one (the CPU, the
display, a texture decode, a block transfer, a save state) first has the renderer `finish()`: everything drawn is
waited for, each target's drawn rectangle read back, narrowed to its format (the stencil as alpha) and put into
memory's VRAM, and the pages are memory's again. Decoded textures and the recompiler hear of the change when the pages
are first owned, as they do of the software renderer's drawing. The depth buffer isn't read back (the GPU's depth
stays the GPU's; a game that reads depth with the CPU, or samples it as a texture, sees what memory had): PPSSPP
makes the same choice by default, and it's on the list below.

**Two targets over the same pages** (a frame buffer drawn as 5650 then 8888, or a smaller buffer inside a larger one):
before a PRIM draws into, or fills, rows whose pages another target owns, the renderer finishes first, so memory has
the other's pixels, and the target is filled from them.

**Submitting.** What's recorded is handed to the GPU every 128 commands and at a list's end (`submit()`), not waited
for, so the GPU draws while the CPU emulates. The backend has three slots, each with its own command buffer, fence and
staging memory (vertices, uploads), used round. Only `finish()` waits.

**Block transfers** go through memory for now: a transfer reading or writing pages the renderer owns finishes it
first, then copies in memory, and the targets it wrote over are filled afresh. Transfers between targets on the GPU
are on the list below.

### Render to texture

A game that draws into a frame buffer and then samples it as a texture (reflections, shadows, blurs, the screen
shrunk into a menu) would otherwise make the renderer finish every time: the texture's pages are busy, so decoding
them waits for the read back. Instead, before decoding, the GE asks the renderer whether it `holds()` the texture:
its pages are all one target's (the newest pixels on the GPU), its format is the frame buffer's own (5650, 5551, 4444
or 8888, not swizzled, not a palette's indices), its row width is the target's, and it starts inside the target. Then
the GE doesn't decode it, and the renderer copies the part of the target the texture covers into a texture of its own
on the GPU, in order with the draws, for the PRIM to sample. The copy is kept per target, place and size, and taken
again only when the target has changed since (each target counts its fills and PRIMs drawn). The shader reads a
16-bit target's texels as its format keeps them (the GPU's 8888 narrowed and widened again), as the GE would read
them from memory.

The GE tells the renderer how many of the texture's rows and columns a 2D PRIM can reach (from its vertices'
coordinates, as it already does for decoding), so a texture declared larger than the picture in it is copied only as
far as it's used. Where that isn't known (3D), a texture wider than the target's row has only the columns inside the
row copied; on the PSP the columns past it are the next rows' first pixels, so such a texture is approximate past the
row (Midnight Club 3 declares 256-wide textures over 64-pixel frame buffers, and samples only the 64). Rows of the
target the GPU hasn't drawn are filled from memory before the copy.

In Midnight Club 3's race this took the render passes from 82 a frame to 12-16 and the GPU's wait from 15.9 ms to
1.6-2 ms a frame (M1).

## The texture cache

The GE already keeps decoded copies of the textures it samples (`texture.cpp`: 8888, the palette applied, unswizzled,
DXT decoded, watched for changes, and decoded again when their bytes or palette change). The renderer puts each on the
GPU once and keeps it while the GE keeps the copy (a weak pointer: once the GE lets it go, the renderer drops its
copy at the next finish), and again when the GE's copy grows more rows. So texture decoding stays one piece of CPU
code for both renderers, and its caching and invalidation are the GE's, measured since part 24. Textures from render
targets are the copies above.

Not yet: decoding on the GPU (palettes and swizzling in a shader), which would also cover textures in a target in a
format the target isn't (a palette's indices drawn by the GE, rare).

## Depth, stencil, blending and logic operations

What the GPU does with the GPU's own units, and how near the PSP it comes:
- **Depth test and write**: the GPU's, on the GE's 16-bit depth (0-65535) kept as a float; the comparisons are the
  GE's, and the depth range test (MIN_Z, MAX_Z) is done in the shader. Clear mode writes depth where the GE does.
- **Stencil**: the GPU's 8-bit stencil, with the GE's comparison, reference, masks and operations (keep, zero,
  replace, invert, increment and decrement clamped). The alpha written to the color follows the stencil where the pass
  operation's result is known in advance (replace, zero), so blending by the frame buffer's alpha reads it; the
  stencil itself is what's read back as the alpha.
- **Write masks**: per channel, where a channel's mask is all or nothing (as games use them); the GPU has no bitwise
  color mask, so a partial mask writes the whole channel.
- **Blending**: the GE's factors map onto the GPU's: the frame buffer's color or alpha as the GPU's destination
  factors; factors from the pixel's own color or alpha, doubled alphas and BLEND_FIXED_A applied in the shader to the
  source; the destination's factor from the pixel (its color, alpha, doubled) carried in the shader's second output
  (dual-source blending), and without that, the GPU's own source factors where they're the same; BLEND_FIXED_B as the
  blend constant. Minimum and maximum are the GPU's; the absolute difference is approximated by the maximum. The
  difference from the PSP: the PSP multiplies each term in whole numbers and truncates it (`(2s+1)(2f+1) >> 10`),
  then adds, while the GPU rounds the sum, so blended pixels are often a level apart. Fixed-function blending can't
  do better.
- **Dithering**: done in the shader where the GPU doesn't blend after (and with the color narrowed to the frame
  buffer's 16-bit format there, as the PSP narrows it); not when blending, since the PSP dithers the blended color.
- **16-bit frame buffers** are 8888 on the GPU: unblended draws are narrowed in the shader, so they're exact; blended
  ones keep 8 bits until they're read back, where the PSP keeps 5 or 6 between draws.
- **Logic operations**: the GPU's own where it has them (Vulkan's `logicOp`, which the Adreno has and MoltenVK
  doesn't); without, clear, set and invert are done in the shader, "keep the destination" by the write mask, and
  "inverted destination" by a blend; the rest are drawn as copy.
- **Color test, alpha test, fog, texture functions**: in the shader, exact.

Exactness here would need programmable blending: the frame buffer's pixel read in the shader (Vulkan's input
attachments with rasterization-order access, `GL_EXT_shader_framebuffer_fetch` on OpenGL ES), so blending, dithering
and the 16-bit formats could be done as the PSP does them. The Adreno has it; MoltenVK on the M1 doesn't. It's the
first item on the accuracy list.

## Upscaling and presenting from the GPU (next)

- **An internal resolution factor** (2x to 8x): targets made N times larger, the viewport and scissor scaled, the
  vertices' positions scaled (the 2D sprites' stepping and the GE's sixteenths scaled with them), points and lines
  drawn N pixels wide. Textures from render targets are copied at the scaled size and sampled with their coordinates
  scaled. Read-backs scale down (nearest, or a box filter) so memory's VRAM keeps the PSP's own pictures; uploads
  from memory scale up.
- **Presenting from the GPU**: the displayed frame buffer shown from its target, without reading it back or
  finishing, so the CPU and GPU stop meeting once a frame; with upscaling, the only way to show the scaled picture.
  The host's presentation (`ares::Video`, and paraLLEl-RDP's path for the N64 on Android) gets the target's image.
  Today every displayed frame finishes, which serializes the CPU and the GPU there (the remaining "waiting" below).

## OpenGL (after Vulkan)

The renderer (`gpu.cpp`: targets, ownership, render to texture, the texture cache, the settings' mapping, the
vertices) knows nothing of Vulkan: it records commands (`GPU::Recorded`: draws with their states, uploads, read-backs,
copies) for a backend (`GPU::Backend`) to run. The OpenGL backend (OpenGL ES 3.2 on Android, 3.3 or 4.x on desktops)
runs the same commands: targets as framebuffer objects with depth-stencil renderbuffers, the same GLSL compiled by the
driver (the specialization constants become `#define`s, one program per key), dual-source blending through
`GL_EXT_blend_func_extended`, and pixel buffer objects for asynchronous read-backs. The same tests and harness measure
it against the software renderer.

## Accuracy: how it's measured, and what differs

The software renderer is the reference. Two tools compare against it:
- **`tests/psp/gpu.cpp`** (in `tests/psp/run-tests.sh`, skipped where there's no Vulkan GPU): random 2D sprites, flat
  and textured (1:1, enlarged, shrunk, mirrored, in clear mode too), in each of the four frame buffer formats, must
  come out **byte for byte the same** as the software renderer's; a frame buffer drawn and then sampled as a texture
  (the copy taken on the GPU) the same again; pspsdk's samples run by two machines alike but for the renderer, every
  frame's picture compared, with the share of pixels the same and how far apart the rest are printed, and no more than
  3 channels in 1000 more than 8 levels off; and a GPU that stops answering, with a pretend backend.
- **A harness for the owner's games** (scratch, never committed; its numbers here): from a scene's state, the same
  frames drawn by the software renderer and then by the GPU renderer, each run unbroken, every frame's picture
  compared: the share of pixels identical, and each differing channel counted as 1-2, 3-8 or more levels apart.

pspsdk's samples (M1): clut, blit, doublelist and gu 100% the same; celshading 100% (176 channels a level apart);
cube 99.7%, envmap 99.4%; blend 68.7% (its blended pixels all a level or two apart).

The owner's games, 10 frames each from the verified scenes (identical on the M1 and the RP6):

| Scene (game) | Pixels identical | Channels apart by 1-2 | 3-8 | more |
|---|---|---|---|---|
| Lumines, demo | 52.6% | 1,159,746 | 99,215 | 209 |
| Peace Walker, title | 41.4% | 0 | 738,861 | 842,011 |
| Midnight Club 3, profile menu | 65.0% | 863,984 | 60,947 | 3,387 |
| Midnight Club 3, night race | 28.4% | 1,855,697 | 300,805 | 3,549 |
| Liberty City Stories, park edge | 46.0% | 1,188,948 | 34,244 | 6,428 |
| Liberty City Stories, woods | 43.0% | 1,348,394 | 20,498 | 1,986 |

What differs:
- **Blending's rounding** is most of it: the channels a level or two apart, over every blended surface (Lumines'
  blocks and backgrounds, the 3D games' transparent textures, smoke, lights and HUD).
- **Peace Walker's title** draws into a 16-bit frame buffer, blended and dithered: the PSP dithers the blended color
  and keeps 5 or 6 bits, the GPU doesn't dither when blending and keeps 8. Every differing channel is one or more
  steps of the 16-bit format, which shows as 3 or more levels apart in 8 bits.
- **The 3D games' 3-8 and more**: 16-bit intermediate frame buffers blended over several draws (the 8 bits kept
  between them), texels chosen differently where the GPU's interpolated texture coordinates land on the other side of
  a texel's edge from the GE's per-pixel division, and edges where the GPU's rasterization rule and the GE's disagree
  by a pixel. They weren't separated further.
- **Known approximations**: the depth buffer not read back; textures past a frame buffer's row; partial write masks;
  absolute-difference blending; logic operations other than clear, set, invert and keep without the GPU's own.

The pictures look the same to the eye in all six scenes; nothing is missing or misplaced.

## Speed

Host frames a second, the same 300 frames from each scene's state (the M1's GPU runs 120): the software renderer on
1 and 7 drawing threads, then the Vulkan renderer (one thread: the GE's).

| Scene (game) | RP6 (Adreno 740): 1, 7 threads, Vulkan | M1 (MoltenVK): 1, 7 threads, Vulkan |
|---|---|---|
| Lumines, demo | 87.7, 127.8, **295.2** | 117, 347, **408** |
| Peace Walker, title | 65.7, 224.0, **393.4** | 108, 427, **531** |
| Midnight Club 3, profile menu | 23.6, 26.5, **52.7** | 27, 53, **58** |
| Midnight Club 3, night race | 21.2, 24.2, **37.5** | 28, 46, **36-41** |
| Liberty City Stories, park edge | 37.0, 49.3, **69.4** | 45, 82, **74** |
| Liberty City Stories, woods | 38.5, 54.5, **94.4** | 34, 105, **107** |

On the RP6 the Vulkan renderer is 1.5-2.3 times the seven-thread software renderer, and frees the six drawing
threads' cores. On the M1, whose CPU draws fast, it's level with seven threads in the 3D games: there what's left is
the CPU's emulation (the GE's thread does the transform and setup in both renderers). The GPU itself is far from busy:
on the RP6 the CPU waits 0.3-1.5 ms a frame for it, and spends 0.25-2.8 ms recording and submitting.

What made it fast, in order: drawing on the GPU's rasterizer at all; keeping frame buffers on the GPU and reading back
only on demand; render to texture on the GPU (the copies above); copies taken only when their target changed; and
handing work to the GPU every 128 commands, so it draws while the CPU goes on (the race's wait from 6.8 to 1.6 ms a
frame on the M1). Next for speed: presenting from the GPU (no finish a frame), block transfers on the GPU, hoisting
copies and uploads out of render passes (a tile-based GPU loads and stores the whole picture at every pass break), and
the CPU side (transform and setup), which bounds the 3D games now.

## Vulkan, and the custom driver

The Vulkan backend (`ares/psp/ge/gpu/vulkan.cpp`) gets Vulkan the way the host chose to load it: `GPU::vulkan(gipa,
error)` takes the host's `vkGetInstanceProcAddr` and loads every function it uses into its own tables (`PSP_VULKAN_
INSTANCE` and `PSP_VULKAN_DEVICE`), through that and the device's `vkGetDeviceProcAddr`. It never touches volk, whose
globals paraLLEl-RDP keeps for the N64, and with a `vkGetInstanceProcAddr` handed over it never opens the system's
`libvulkan` itself. So a custom driver the host loaded with libadrenotools (the app's Driver Manager: Turnip, or a
newer Qualcomm driver) is what the PSP renderer runs on, as it is for paraLLEl-RDP. Only when nothing is handed over
(tools and tests) does the backend open the system's loader (`libvulkan.so` on Android, `libvulkan.so.1` elsewhere;
on macOS Homebrew's loader or MoltenVK).

The host side (`android/app/src/main/cpp/PhobosHostAndroid.cpp`'s `loadVulkan`) already runs for every system, the PSP
among them: with a custom driver set it opens it through libadrenotools and resolves its `vkGetInstanceProcAddr`,
else it initializes the system loader. What the app wiring adds: the host keeps that `vkGetInstanceProcAddr` (the
custom driver's, or the system loader's) and hands it to `GPU::vulkan` when the PSP renderer is Vulkan, and
`loadVulkan` runs before the PSP core starts with a Vulkan renderer chosen. Measuring in the app on the RP6 is to be
done with the system driver and with a custom driver the Driver Manager already has (none installed or deleted
without the owner's word).

The device: the first GPU with a graphics queue that isn't the CPU (lavapipe and SwiftShader only when asked for,
with `PSP_GPU_ON_CPU`), with dual-source blending and logic operations where it has them. A wait longer than five
seconds marks the device lost, as `VK_ERROR_DEVICE_LOST` does.

**A lost GPU**: the renderer says so once (`report`), and the software renderer draws from then on. What the GPU drew
since the last finish is lost (memory's VRAM keeps what was there before), a frame or less; the game goes on.

## The plan

1. **Vulkan at native resolution** (part 36, done): the milestone above, measured on the M1 and the RP6.
2. **Upscaling** (next), then **presenting from the GPU**.
3. **OpenGL**: the GL backend over the same renderer and shaders; the same measurements.
4. **In Phobos**: Settings' "PSP Renderer: Software / Vulkan / OpenGL", Software the default; the host's
   `vkGetInstanceProcAddr` handed over and `loadVulkan` run for the PSP; a start-up sanity check (a few primitives
   drawn both ways, the GPU's picture near the software renderer's, else the software renderer draws and the app says
   so once); the fallback on a lost device; measured in the app on the RP6 with the system and a custom driver.
5. **Accuracy**, alongside: programmable blending where the GPU has it (blending, dithering and 16-bit formats as the
   PSP's), depth read back where games need it, block transfers between targets on the GPU, textures decoded on the
   GPU.
