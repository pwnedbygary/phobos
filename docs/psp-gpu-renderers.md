# The PSP's hardware renderers: Vulkan, then OpenGL

**Status (2026-10-09):** a Vulkan renderer drawing the owner's games with the GPU's own rasterizer, texture units
and blending, at the PSP's resolution, on Apple's M1 (MoltenVK) and the RP6's Adreno 740 (docs/psp-core.md, part
36). Since part 47 made the software renderer's seven threads faster, those are ahead of it at Native in the 3D
scenes on the RP6; it's ahead in the 2D ones there, and draws above 1x (below: "Speed"). On Turnip, the app's
driver on the RP6, the owner's scenes match the software renderer on 73-100% of pixels ("Accuracy"); the rest are
mostly a level or two apart from texel edges and rasterization. In the app and the desktop program since part 41
(Settings' "PSP Renderer", Software the default: "In Phobos"). Since part 44 it draws at 1 (exact) to 10 times the
PSP's resolution and presents on Android's window without reading back ("Upscaling", "Presenting"); since part 45
it blends in the shader where the GPU keeps rasterization order (Turnip), reading the frame buffer, so blending,
dithering, logic operations and write masks are the software renderer's to the bit ("Shader blending"); without
the order the GPU blends 8888 itself, close (part 49: "Without rasterization order"). No OpenGL yet ("The plan").

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
format when it's a frame buffer's; 18 since shader blending's reading and its operation (part 45) and the source
term (part 49)). So each mix of settings a game uses is a shader of its own, compiled by the driver
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
pixels put back) makes it stale, and its colors and stencil are filled afresh before it's next drawn into. The depth
buffer's bytes are watched too, through VRAM's fourth copy as the GE sees them, but on their own: a change there (the
CPU clearing depth, a block transfer, the software renderer's drawing) has the rows it may be in (all of each 16 KiB
it touches, which the fourth copy rearranges) filled again from memory, and the rest of the GPU's depth kept; a change
to the colors leaves the GPU's depth alone; and a PRIM with another depth buffer than the target's last fills it
afresh.

**What the GPU doesn't draw.** A PRIM the renderer can't take is drawn by the software renderer instead, once the
renderer has put back what it drew (`begin()` says so): no target for its frame buffer (the GPU out of room), rows
past the target's (a frame buffer near VRAM's end whose drawing runs past it, where the PSP's addresses run round to
VRAM's start, or past 512 rows), a lost GPU, or a texture the GE reads from memory as it draws (none decoded, which
the GPU can't sample). Columns past a row's end are cut at the row, as before.

**Owned until needed.** What's drawn stays on the GPU. VRAM's pages under the pixels drawn are the renderer's: busy
(`Memory::busyPages`), exactly as the drawing threads' batches make them, so anyone touching one (the CPU, the
display, a texture decode, a block transfer, a save state) first has the renderer `finish()`: everything drawn is
waited for, each target's drawn rectangle read back, narrowed to its format (the stencil as alpha) and put into
memory's VRAM, and the pages are memory's again. Decoded textures and the recompiler hear of the change when the pages
are first owned, as they do of the software renderer's drawing. The depth buffer isn't read back (the GPU's depth
stays the GPU's; a game that reads depth with the CPU, or samples it as a texture, sees what memory had; and a PRIM
the software renderer draws, above, tests against memory's depth): PPSSPP makes the same choice by default, and it's
on the list below.

**Bytes beside the pixels.** A page is 4 KiB, two rows of a 512-wide 8888 frame buffer, and games keep their own
bytes in the same pages: Brave Story keeps its display list in the 32 unused columns right of its 480-pixel picture,
and writes and runs it every frame. So busy pages are asked about to the byte (`Memory::vramDrawnOver`, the GE's
`drawnOver()`): memory finishes the renderer only for bytes the GPU drew over since its last finish (each target's
drawn rectangle, row by row), and reaches the rest at once. A change to such bytes (`Memory::changed()` while pages
are busy) is kept by the target over them (`beside`, at most 16 ranges), which takes them from memory again (filled
afresh, after a finish) before it draws over them, shows them or lends them as a texture, and after its next finish.
(The drawing threads' busy pages stay whole pages: their batches draw anywhere in them.) Brave Story's title, on the
M1: 53 frames a second before, 90-96 after (the software renderer: 178), its waits for the GPU from 26,000 to 5,700
in 2,400 frames. What's left are textures sampled across two targets (its bloom passes), which the renderer can't
lend (below), and the uploads that follow.

**Two targets over the same pages** (a frame buffer drawn as 5650 then 8888, or a smaller buffer inside a larger one):
before a PRIM draws into, or fills, rows whose pages another target owns, the renderer finishes first, so memory has
the other's pixels, and the target is filled from them.

**Submitting.** What's recorded is handed to the GPU every 128 commands and at a list's end (`submit()`), not waited
for, so the GPU draws while the CPU emulates; and within a PRIM once it has 262,144 vertices (a long line costs six
for each pixel it lights), so no PRIM's recording grows without bound. The backend has three slots, each with its own
command buffer, fence and staging memory (vertices, uploads), used round. Only `finish()` waits.

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
on the GPU, in order with the draws, for the PRIM to sample. One copy is kept for each target and size, and taken
again when the place is another or the target has changed since (each target counts its fills and PRIMs drawn); the
draws before sample it as it was, as the GPU runs the commands in order. At most 32 are kept, the one unused longest
let go for another, and one unused for 16,384 PRIMs goes too, so a game that samples its frame buffer from a place
that moves each frame doesn't fill the GPU's memory or its descriptor sets. The shader reads a
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
GPU once and keeps it while the GE keeps the copy (a weak pointer: once the GE lets it go, the renderer drops its copy
at the next submit or finish), and again when the GE's copy grows more rows. So texture decoding stays one piece of
CPU code for both renderers, and its caching and invalidation are the GE's, measured since part 24. Textures from
render targets are the copies above.

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
  then adds, while the GPU rounds the sum, so blended pixels were often a level apart; since part 49 the shader
  gives the GPU the source term as the PSP makes it, nudged a quarter of a step (`TERM`), and most of them are the
  PSP's ("Without rasterization order").
- **Dithering**: done in the shader where the GPU doesn't blend after (and with the color narrowed to the frame
  buffer's 16-bit format there, as the PSP narrows it); not when blending, since the PSP dithers the blended color.
- **16-bit frame buffers** are 8888 on the GPU: unblended draws are narrowed in the shader, so they're exact; blended
  ones keep 8 bits until they're read back, where the PSP keeps 5 or 6 between draws.
- **Logic operations**: the GPU's own where it has them (Vulkan's `logicOp`, which the Adreno has and MoltenVK
  doesn't); without, clear, set and invert are done in the shader, "keep the destination" by the write mask, and
  "inverted destination" by a blend; the rest are drawn as copy.
- **Color test, alpha test, fog, texture functions**: in the shader, exact.

That's the fixed-function way, still what a backend that can't read the frame buffer does. Vulkan's now reads it
(part 45, "Shader blending" below): blending, dithering after it, logic operations, partial write masks and the
16-bit formats are pixel.cpp's arithmetic there, and the differences above are gone for those draws. Every blend
reads where the GPU keeps rasterization order; without it (part 49) only what the GPU can't come close to does, and
on Qualcomm's own driver nothing.

## Upscaling (part 44)

The option is "Resolution" (`psp.hpp`'s `option()`, read as a game loads): 1, the PSP's own and the default, to 10
times it. The app's Settings has "PSP Resolution" (Native, 2x to 10x) under the renderer and the desktop's menu
"PSP resolution (Vulkan)" (`psp.resolution`). The software renderer ignores it. Native is the exact mode: a scale of
1 runs exactly the code part 41 ran, with no extra copies or filters.

At a scale N (`Backend::scale`), each of the PSP's pixels is N x N of the GPU's everywhere: in every target, in its
depth and stencil, its draws, its copies and its read-backs. `Backend::mostScale` caps it at the size the device's
images allow (a target 512 of the PSP's pixels across, so 10 on both test GPUs).
- **Targets** are made at the PSP's size and grow to N times it the first time they're drawn into at a scale above
  1 (`fit()`), so a target only ever read back, or never drawn, costs nothing extra.
- **Draws**: the viewport and scissor are multiplied by N; the vertices' positions are already in the PSP's
  sixteenths, and the shader works from the resolution it's given. The 2D sprites' stepping samples each pixel at
  its middle (the GE's texel centres), so `gl_FragCoord` times 16 over the scale lands on the same texels at any N,
  with no half-pixel shift (one was tried; it moved the texels).
- **Uploads** (memory's newer bytes into a target) go through `copy.frag`, drawn N times larger: mode 0 the colours,
  mode 1 the depth, mode 2 the stencil one bit at a time (a stencil can only be written by its test).
- **Read-backs** for the CPU or the GE (a frame buffer it reads, depth, the picture in VRAM) are first blitted to
  the PSP's size with NEAREST into a scratch image (`shrunk`), the stencil copied row by row, and squeezed on the
  CPU, so memory's VRAM always holds the PSP's own pictures and everything that reads it sees what the software
  renderer would.
- **Render to texture**: a copy of a target for a texture keeps the target's scale (`Texture::textureScale`), and
  the shader samples it with its coordinates multiplied by that, so a frame buffer used as a texture keeps its
  detail.
- **2D triangles' edges** (part 49): above 1x a 2D triangle's texture coordinates are held inside its corners' by
  what half a pixel moves them, half a texel at most (`triangle()`, from the coordinates' slope across the screen;
  the vertex's "held" flag; `draw.frag` clamps them), which is as far as its pixels' middles reach at 1x. The GPU's
  pixels within half a PSP pixel of an edge sampled up to half a texel beyond it before: Ridge
  Racer 2's menu draws its picture as two quads meeting at x = 240, the right one mirroring the left, and the filter
  blended the texel past the picture into a one-pixel line down the middle (`gpuSeams`). At 1x nothing changes. A
  2D sprite's stepped coordinates aren't held (none seen bleeding).

The shown picture, read back (where it isn't presented): the screen is made `MostShown` (4) times the PSP's size
at most, and the picture is read back at the renderer's scale and copied (nearest) into it. That's the desktop's
path (SDL owns its window), and Android's when the swapchain can't be had. Above 4x the picture is drawn at the
scale but shown at 4x; the read-back's CPU cost grows with the square of the scale (Lumines on the M1: 3 s of the
CPU for 1,500 frames at 1x, 21 s at 4x).

## Presenting (part 44)

On Android the renderer presents on the window itself, with no read-back: a `Present` command at the end of a
frame's recording (`GPU::show()`) draws the shown target, or memory's picture where the target can't give it, onto
an image of a swapchain on the host's `ANativeWindow`, over the whole of it (the app's view already has the
picture's shape). `present.frag` scales it "sharp bilinear", as the host's whole multiple and the compositor did
before: each pixel a block, blended with the next only over the window pixel between them. It also narrows a 16-bit
frame buffer's colours as memory would keep them. The host's `video()` then neither locks nor draws on the window.
- **The window** is handed over by the host (`PlayStationPortable::window()`, holding a reference with
  `ANativeWindow_acquire`) and given to the GPU on the emulation thread as the next frame is shown (`handWindow()`).
  The surface is made from it (`VK_KHR_android_surface`) lazily, the swapchain when a frame is first presented:
  MAILBOX when the driver has it (no waiting on the compositor), else FIFO; the identity transform, opaque alpha, an
  8888 UNORM format. A resized window or `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` rebuilds it next frame.
- **Synchronisation**: each slot has its own `acquired` semaphore, each swapchain image its own `rendered` one, so
  nothing waits on the CPU except the acquire (100 ms at most, then the frame isn't presented).
- **Screenshots**: a presented frame is also blitted (nearest) to the PSP's size into the slot's own buffer and kept
  when the slot is next waited for (`GPU::shot()`), so the host's screenshot still has the picture.
- **Falling back**: a surface or swapchain that can't be made gives presenting up (`failed`): the surface is let go
  so the host can lock the window again, and frames are read back as before. `presents()` doesn't depend on the
  window being there, so the host never locks a window the GPU is about to present on.
- **The desktop** reads back (SDL owns its window, and its renderer isn't Vulkan's); MoltenVK on the M1 has no
  surface for it.

PPSSPP's presentation (drawing its output framebuffer to the backbuffer with a post-processing pass, and its
render resolution multiplier) informed the design here as before; none of its code is used, copied or translated.

Presenting reports itself: `Backend::presented` is set only when this frame's `vkQueuePresentKHR` succeeded (or was
suboptimal), and `System::present` returns false otherwise (no window, as in the background, or an acquire that
timed out), so that frame is read back and the host draws it. A screenshot taken just after a presented frame may be
the frame before's: the shrunk copy is kept when its slot is next waited for, a frame or two later.

## Shader blending (part 45)

A draw that blends, uses a logic operation other than copy, or has a write mask keeping part of a channel reads the
target's pixel in `draw.frag` and does from there on what pixel.cpp does, so its result is the software renderer's
to the bit (`settings()`: `reading`, the pipeline's `reads` and `blending`). That's every such draw where the GPU
keeps rasterization order; without it, since part 49, only those the GPU's own blending can't come close to (below):
- **The read**: the target's color is the render pass's input attachment as well as its color attachment, in the
  GENERAL layout, read with `subpassLoad` (`READS`), which every Vulkan GPU has. Each write is narrowed to the frame
  buffer's format (`QUANTIZE` its format), so what's read back is what memory would hold: 5, 6 or 4 bits kept
  between draws as on the PSP, where fixed-function blending kept 8.
- **Blending**: the GE's factors (the colors, alphas, doubled alphas, fixed A and B) and all eight
  operations, the absolute difference included, each term `(2s+1)(2f+1) >> 10` and then added, as pixel.cpp
  multiplies them; then the dither (after blending, as the PSP does it), the clamp, all 16 logic operations, and the
  write mask bit by bit in the format's packing (`packed()`, `unpacked()`). The GPU's own blending and logic
  operations are off for those draws.
- **Order**: a pixel's read has to see the primitive before's write. Where the driver offers rasterization-order
  attachment access (`VK_EXT_rasterization_order_attachment_access`, or ARM's before it) the subpass and pipelines
  ask for it, every pipeline too, and a draw's overlapping primitives stay in one draw (`readsInOrder`). Part 45
  also had every reading draw wait for the ones before (a `BY_REGION` barrier, a self-dependency of the subpass):
  with only the reading pipelines asking for the order, Turnip on the RP6 had failed the start-up check without it.
  Since every pipeline of the subpass asks, the order covers the draws before a reading one too, and part 49 drops
  the barrier in order: Turnip passes the check and the tests without it, and in a run before and after (from adb's
  shell, an earlier build) GTA's woods ran 59 frames a second with it, 71 without. Without that access each reading
  draw has the barrier, and `GPU::emit()` keeps no two primitives that overlap in one draw: each primitive's box in
  the target's pixels is checked against the draw's (at most `MostBoxes`, 64), and one that overlaps begins another
  draw. The M1's MoltenVK has no such access, nor the Adreno's own driver; there only what the GPU's own blending
  can't come close to reads ("Without rasterization order", below), and on the Adreno's driver nothing. Where the
  start-up check fails in order, it's run again with the GPU blending and the rest split (nothing read where reading
  apart can't be trusted), then with nothing read (`System::startRenderer()`), and the log says how it draws.
- **Clears** of a 16-bit frame buffer are narrowed to its format too, so a reading draw over a cleared pixel reads
  memory's color.
- **Native stays exact**, the software renderer the default; at a scale above 1 the same arithmetic runs on each of
  the GPU's pixels.

The test (`gpuBlending`, "gpu blending in the shader against the software renderer"): random sprites over a random
background of random colors and stencils, blended with random factors, operations and fixed colors, dithered or
not, with random logic operations and write masks keeping part of a channel, then a clear through such a mask, in
each of the four formats at 1x and 2x: read back byte for byte the software renderer's, with reading draws counted
and, without rasterization order, draws split. pspsdk's blend
sample went from 68.7% of pixels the same to 100%.

On the RP6 (Adreno 740, Turnip from the Driver Manager, in order; logcat's "PSP" start-up line says which way:
"blending in the shader, in order" or "overlaps apart"): Lumines' menus 60 frames a second at Native (5.2 ms a
frame); Burnout Legends' menu, its 3D attract scene behind, 60 at Native (14.4 ms) and 51.4 at 4x (18.3 ms, a
heavier attract scene, the GPU 20% busy); the pictures right to the eye. The GPU tests, built for Android and run
on the Adreno's own driver (no rasterization-order access: `cmd gpu vkjson`), pass with the draws split. (Part 49:
in games that driver's reads go wrong, and nothing reads there now.)

### Without rasterization order (part 49)

A 3D game blends thousands of primitives a frame, and split apart each became a draw and a barrier: Midnight Club
3's race made 4 million draws in 300 frames, 1.3 frames a second on the M1 and 13 on the RP6's own driver, against
38.2 and 32.2 with the GPU blending. So without the order (`Backend::readsBlending` off), blending in an 8888 frame
buffer by the factors and operations the GPU has is the GPU's again, and only what it can't come close to reads,
split as above (`settings()`: `unlike`): a write mask keeping part of a channel, a logic operation, the absolute
difference, doubled alphas, and any blend in a 16-bit frame buffer. There the PSP narrows each blend's result to the
format, dithered, before the next blend reads it, and the GPU's 8-bit target drifted by whole steps over layered
blends: Peace Walker's title (5551, dithered, additive noise over alpha blends) was 39.7% identical on the M1 that
way, 99.7% reading. Those are few in the scenes measured (Midnight Club 3 and GTA blend in 8888, Lumines a few 5551
sprites a frame), and in order every blend still reads.

**The GPU's blending, closer** (`draw.frag`'s `TERM`): where it takes output 0 as it is (its source factor one),
the shader makes that the PSP's whole-number source term, `(2s+1)(2f+1) >> 10`, and puts it a quarter of a step below
itself where the GPU adds the destination's term, above where it subtracts. The PSP drops each term's fraction and
then adds; the GPU adds and rounds once, and the quarter puts its rounding where the PSP's dropped fractions land
nearly always. In a model of alpha blending over every value that took channels the same from 50.6% to 93.7% (92.7%
blending in half floats); `gpuBlendingApart`'s random 8888 blends are 8893 channels of 9216 the same on the M1 and on
both of the RP6's drivers, none more than 8 apart; pspsdk's blend sample is 100% the same through the GPU's blending.
Dithering still isn't done for blended pixels there, nor the 16-bit narrowing.

**Qualcomm's own driver** (the RP6's, build of 12/27/23) passed the start-up check's small pictures, but in a game's
frame buffer a draw that read sometimes saw the pixels as they were before the draws just before it, barrier or not
(a memory or image barrier, shader-read access as well): Midnight Club 3's menu was 10% identical reading. So on that
driver, without the order, nothing reads (`vulkan.cpp`, by `VkPhysicalDeviceDriverProperties`' driver ID), and its
16-bit blends are the GPU's too. The start-up check gained a game-sized picture (480x272 in rows of 512: 128 opaque
sprites, each under a blended one moved a few pixels, through a write mask keeping part of each channel), which
caught that driver some of the time. A driver that fails the check in order is checked again without the order, the
GPU blending (with nothing read where reading apart can't be trusted: `Backend::readsApart`), and if reading fails
then too, checked again with nothing read (`System::startRenderer()`). The
log's start-up line says "blending in the shader, in order", "blending in 8888 by the GPU, the rest in the shader,
overlaps apart", or "blending by the GPU".

PPSSPP's shader blending (its framebuffer fetch and its copies of the destination) was a guide to the approach only;
none of its code is used, copied or translated.

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
  3 channels in 1000 more than 8 levels off; a GPU that stops answering, with a pretend backend; PRIMs the renderer
  refuses (a frame buffer whose drawing runs past VRAM's end, and a pretend backend that makes no targets) drawn by
  the software renderer, the same; render to texture from 300 places and sizes, its copies kept to the bound; and the
  depth buffer following memory's changes (cleared by the CPU between two depth-tested sprites, and a color pixel
  written between them not bringing memory's older depth back). Each machine takes the renderer with
  `GE::setRenderer()`, which has it forget the machine before's VRAM.
- **A harness for the owner's games** (scratch, never committed; its numbers here): from a scene's state, the same
  frames drawn by the software renderer and then by the GPU renderer, each run unbroken, every frame's picture
  compared: the share of pixels identical, and each differing channel counted as 1-2, 3-8 or more levels apart.

pspsdk's samples (M1): clut, blit, doublelist and gu 100% the same; celshading 100% (176 channels a level apart);
cube 99.7%, envmap 99.4%; blend 68.7% (its blended pixels all a level or two apart), 100% since part 45.

The owner's games, 10 frames each from the verified scenes. Before part 45, on the RP6 (part 41's code):

| Scene (game) | Pixels identical | Channels apart by 1-2 | 3-8 | more |
|---|---|---|---|---|
| Lumines, demo | 52.6% | 1,159,746 | 99,215 | 209 |
| Peace Walker, title | 41.4% | 0 | 738,861 | 842,011 |
| Midnight Club 3, profile menu | 65.0% | 863,984 | 60,947 | 3,387 |
| Midnight Club 3, night race | 28.4% | 1,855,697 | 300,805 | 3,549 |
| Liberty City Stories, park edge | 46.0% | 1,188,948 | 34,244 | 6,428 |
| Liberty City Stories, woods | 43.0% | 1,348,394 | 20,498 | 1,986 |

(Part 36 said the M1's were the same; they aren't quite without shader blending: 52.7%, 41.3%, 60.0%, 27.6%, 43.9%
and 42.6%, in that order.)

After part 45, the same scenes on the M1 (MoltenVK; blending in the shader, overlaps apart), states migrated from
layout 15 to 17. Peace Walker's title wouldn't load (its program hash no longer matches the disc's EBOOT):

| Scene (game) | Pixels identical | Channels apart by 1-2 | 3-8 | more |
|---|---|---|---|---|
| Lumines, demo | 97.8% | 62,942 | 13,338 | 272 |
| Midnight Club 3, profile menu | 97.6% | 41,814 | 10,031 | 3,333 |
| Midnight Club 3, night race | 89.2% | 202,401 | 7,456 | 774 |
| Liberty City Stories, park edge | 77.4% | 397,544 | 3,145 | 1,588 |
| Liberty City Stories, woods | 82.2% | 333,806 | 5,163 | 2,207 |

After part 49: the six scenes remade at StateVersion 17 (`~/phobos-work/scratch/cpu-speed/scenes`; the race and the
city with the accelerator held, the city in place of the park's edge; Peace Walker's title loads again), 10 frames
each. On the RP6 from adb's shell, the runner gets the system's driver (Qualcomm's), not the app's: the app hands the
core the Driver Manager's driver through libadrenotools. Qualcomm's driver has no rasterization order and reads
nothing (above), so the GPU blends everything:

| Scene (game) | Pixels identical | Channels apart by 1-2 | 3-8 | more |
|---|---|---|---|---|
| Lumines, demo | 73.0% | 646,394 | 128,032 | 502 |
| Peace Walker, title | 40.4% | 0 | 740,466 | 824,039 |
| Midnight Club 3, profile menu | 88.7% | 236,941 | 11,273 | 3,324 |
| Midnight Club 3, night race | 67.1% | 681,781 | 31,554 | 5,806 |
| Liberty City Stories, city | 52.2% | 909,324 | 4,537 | 1,666 |
| Liberty City Stories, woods | 49.3% | 1,054,982 | 8,662 | 2,199 |

The same driver before part 49, reading in the shader with its draws split: 59.3%, 100.0%, 10.0%, 34.5%, 47.3% and
32.2% (its reads wrong in all but Peace Walker's title: noise, up to 3.3 million channels more than 8 apart); with
the GPU's blending as it was (no `TERM`): 45.6%, 39.8%, 78.7%, 33.9%, 41.4% and 39.2%. Peace Walker's title, all
5551 with dithering, is the one left whole steps apart (8, 16, 25 levels: its layered noise a little brighter); the
rest are mostly a level apart.

On the RP6 with Turnip, the app's driver (the runner opening Turnip's `.so` itself as the app does, a scratch
patch), in order, every blend read:

| Scene (game) | Pixels identical | Channels apart by 1-2 | 3-8 | more |
|---|---|---|---|---|
| Lumines, demo | 97.8% | 63,552 | 13,774 | 242 |
| Peace Walker, title | 100.0% | 0 | 326 | 115 |
| Midnight Club 3, profile menu | 97.5% | 42,768 | 10,364 | 3,315 |
| Midnight Club 3, night race | 87.8% | 248,585 | 8,060 | 1,093 |
| Liberty City Stories, city | 73.4% | 509,055 | 4,191 | 1,673 |
| Liberty City Stories, woods | 80.6% | 378,827 | 5,624 | 2,197 |

On the M1 (MoltenVK, no rasterization order: blending in 8888 by the GPU, the rest read; before part 49 every blend
read, and the 3D scenes ran at 1-3 frames a second):

| Scene (game) | Pixels identical | Channels apart by 1-2 | 3-8 | more |
|---|---|---|---|---|
| Lumines, demo | 77.9% | 550,666 | 14,190 | 348 |
| Peace Walker, title | 99.7% | 0 | 3,638 | 1,258 |
| Midnight Club 3, profile menu | 92.6% | 138,645 | 9,986 | 3,324 |
| Midnight Club 3, night race | 80.3% | 375,181 | 8,460 | 1,093 |
| Liberty City Stories, city | 52.9% | 968,787 | 3,849 | 1,683 |
| Liberty City Stories, woods | 61.9% | 799,829 | 6,095 | 2,207 |

What differs after shader blending:
- **Texel edges and rasterization** are most of what's left: where the GPU's interpolated texture coordinates land
  on the other side of a texel's edge from the GE's per-pixel division, and edges where the GPU's fill rule and the
  GE's disagree by a pixel. Those show as channels a level or two apart (and a few farther) over the 3D games'
  textured surfaces.
- **Blending's rounding**, the dithering of blended pixels, and the 8 bits kept between draws of a 16-bit frame
  buffer are gone where the GPU reads the frame buffer (part 45); pspsdk's blend sample is 100% the same. Where the
  GPU blends (in 8888 without rasterization order; everything on Qualcomm's own driver), its rounding is mostly the
  PSP's (`TERM`) and the rest a level apart, but blended pixels aren't dithered, and on Qualcomm's driver a 16-bit
  frame buffer keeps 8 bits between blends.
- **Known approximations** still: the depth buffer not read back (so the CPU, a texture or a PRIM the software
  renderer draws sees memory's depth, not the GPU's); a change to the depth buffer's bytes refills all of each 16
  KiB it touches; a PRIM's pixels past its frame buffer's row end cut at the row; textures past a frame buffer's
  row; and, for a backend that can't read the frame buffer, the older blending and logic approximations.

The pictures look the same to the eye in the scenes measured; nothing is missing or misplaced.

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

Part 49, the scenes remade at StateVersion 17 (the race and the city with the accelerator held), the runner from
adb's shell with the Phobos app not running, means of two steady rounds (a first, slower round after the device had
run 10x in the app was dropped); the RP6's Vulkan on the system's driver (Qualcomm's) and on Turnip (the app's,
opened by a scratch patch); the M1 one round:

| Scene (game) | RP6: 1, 7 threads, Vulkan (Qualcomm's), Vulkan (Turnip) | M1 (MoltenVK): 1, 7 threads, Vulkan |
|---|---|---|
| Lumines, demo | 91.2, 188.0, **199.4**, **209.1** | 113.8, 245.3, **114.6** |
| Peace Walker, title | 66.5, 227.8, **368.6**, **344.8** | 104.2, 416.2, **307.6** |
| Midnight Club 3, profile menu | 30.3, 71.1, **45.9**, **49.7** | 39.9, 109.9, **55.0** |
| Midnight Club 3, night race | 22.5, 53.5, **32.2**, **33.8** | 30.3, 50.7, **38.2** |
| Liberty City Stories, city | 39.2, 77.2, **59.8**, **63.7** | 47.5, 111.9, **72.2** |
| Liberty City Stories, woods | 39.3, 76.6, **73.4**, **74.7** | 50.8, 134.8, **89.6** |

Part 47 made the software renderer's seven threads much faster, and in the 3D scenes they're now ahead of the
Vulkan renderer at Native: there the emulation thread is the bound (Turnip's race 26.9 ms a frame, the software
renderer's 18.3), doing the transform and setup as before plus the recording, and waiting whenever the CPU or the GE
reads what the GPU drew (the race reads back 4 times a frame). The app shows the same: Liberty City Stories, driving
off from its new game's first mission, 50.6 frames a second at Native on Turnip against 60.0 in software. On the RP6
Vulkan's lead is in the 2D scenes, in drawing above 1x, and in leaving the drawing threads' cores idle. On the M1,
whose CPU draws fast, seven threads lead in every scene; there Lumines reads its 5551 sprites' blending, split
(30,284 draws in 300 frames against 4,891), at 114.6 against 311 with the GPU's blending, and Peace Walker's title
reads too.

What made it fast, in order: drawing on the GPU's rasterizer at all; keeping frame buffers on the GPU and reading back
only on demand; render to texture on the GPU (the copies above); copies taken only when their target changed; and
handing work to the GPU every 128 commands, so it draws while the CPU goes on (the race's wait from 6.8 to 1.6 ms a
frame on the M1). Next for speed: block transfers on the GPU, textures sampled across two targets (Brave Story's
bloom: "Bytes beside the pixels"), hoisting copies and uploads out of render passes (a tile-based GPU loads and
stores the whole picture at every pass break), and the CPU side (transform and setup), which bounds the 3D games now.

**Showing the frame from the GPU** (part 41: `GPU::picture()`, no finish a frame), the same harness on the RP6, frames
a second for the software renderer on 7 threads, the Vulkan renderer finishing every frame, and showing it from its
target: Lumines 117.5, 304.5, 263.5; Peace Walker 223.7, 410.0, 595.0; Midnight Club 3's menu 26.4, 53.5, 55.3 and
race 24.3, 38.1, 38.7; Liberty City Stories' park edge 49.4, 69.9, 68.2 and woods 51.4, 95.1, 92.7. A 2D game that
draws little (Lumines) loses a little: the frame's read-back waits for its drawing, 0.86 ms a frame against 0.37. The
others gain or stay level, and the pages stay the GPU's. (These runs' scene states were lost with a scratch folder
afterwards, and weren't made again; new ones come with upscaling.)

## Vulkan, and the custom driver

The Vulkan backend (`ares/psp/ge/gpu/vulkan.cpp`) gets Vulkan the way the host chose to load it: `GPU::vulkan(gipa,
error)` takes the host's `vkGetInstanceProcAddr` and loads every function it uses into its own tables (`PSP_VULKAN_
INSTANCE` and `PSP_VULKAN_DEVICE`), through that and the device's `vkGetDeviceProcAddr`. It never touches volk, whose
globals paraLLEl-RDP keeps for the N64, and with a `vkGetInstanceProcAddr` handed over it never opens the system's
`libvulkan` itself. So a custom driver the host loaded with libadrenotools (the app's Driver Manager: Turnip, or a
newer Qualcomm driver) is what the PSP renderer runs on, as it is for paraLLEl-RDP. Only when nothing is handed over
(the tests, `tools/psp-runner`) does the backend open the system's loader (`libvulkan.so` on Android,
`libvulkan.so.1` elsewhere, `libvulkan.1.dylib` on macOS: Homebrew's, `DYLD_LIBRARY_PATH=/opt/homebrew/lib`).

The hosts (part 41): both front ends' `loadVulkan` go through Granite's `Vulkan::Context::init_loader`, which runs
before every system starts, the PSP among them, and the host's `vulkanLoader()` hands what Granite resolved
(`Vulkan::Context::get_instance_proc_addr()`) to the PSP's system (`vulkanLoader`, beside its options), which gives
it to `GPU::vulkan`. On Android that's the custom driver's `vkGetInstanceProcAddr` when the Driver Manager has one
chosen (opened through libadrenotools), else the system loader's. The desktop hands one over too (part 36 said it
didn't): Phobos.app's MoltenVK on macOS (`GRANITE_VULKAN_LIBRARY` set to the bundle's `libMoltenVK.dylib`; a build
outside a bundle can be pointed at Homebrew's the same way), `libvulkan.so.1` on Linux, the system loader on
Windows.

The device: the first GPU with a graphics queue that isn't the CPU (lavapipe and SwiftShader only when asked for,
with `PSP_GPU_ON_CPU`), with dual-source blending and logic operations where it has them. A wait longer than five
seconds marks the device lost, as `VK_ERROR_DEVICE_LOST` does.

**A lost GPU**: the renderer says so once (`report`), and the software renderer draws from then on. What the GPU drew
since the last finish is lost (memory's VRAM keeps what was there before); the game goes on. Since part 41 the shown
frame doesn't finish, so that can be more than a frame: a render-to-texture result only ever sampled on the GPU stays
lost, and the software renderer samples what memory had, until the game draws it again. A lost device is rare, so no
finish is forced to bound it.

## In Phobos (part 41)

**The setting.** "PSP Renderer" in the app's Settings, Emulation, PlayStation Portable (beside Drawing Threads):
"Software (exact)", the default, or "Vulkan (GPU)", applied when a game starts. The desktop program's menu has "PSP
renderer: Software / Vulkan (next start)" (`psp.renderer` in its settings). Either hands the core its "Renderer"
option as the game loads; OpenGL's choice comes with OpenGL.

**Starting.** As the PSP powers on with Vulkan chosen, the system (`System::startRenderer()`) makes the GPU renderer
on the host's loader and puts it through a start-up check (`GPU::check()`, `ge/gpu/check.cpp`), in a machine of its
own (4 MiB of memory, a GE, nothing of the game's): 48 flat and 48 textured random sprites into an 8888 and a 5650
frame buffer, clear-mode sprites, and 24 triangles, half of them blended, drawn by the GPU and by the software
renderer. The sprites' bytes must be the same, as `tests/psp/gpu.cpp` has them; of the triangles' pixels, at most one
in 50 may have a channel more than 8 levels apart. A GPU that doesn't start, draws wrong or stops answering fails it.
Then the software renderer draws instead, and the system says so once: in the log ("The Vulkan renderer couldn't
start (why): the software renderer draws instead"), and to the front end (`notice()`), which the app shows as a
toast and the desktop as a message. A renderer that passed says on which device ("the Vulkan renderer draws, on
Vulkan: Turnip Adreno (TM) 740"); one that failed isn't tried again until the next game.

**A lost device** later (a wait past five seconds, `VK_ERROR_DEVICE_LOST`): the renderer stops, the software
renderer draws from the next PRIM on, and the system says so once the same way ("The Vulkan renderer stopped: ...").

**The frame shown.** Both hosts take a frame as pixels in memory, so the choice was between reading the shown frame
buffer back from the GPU and finishing everything (memory's VRAM, then `Kernel::picture()` as for the software
renderer). The shown frame is read straight from its target (`GPU::picture()`: the shown rectangle alone, after
what's recorded, narrowed and widened as the PSP's display would see it): no finish, so the pages stay the GPU's
and the next frame doesn't fill them again. Where the target can't give it (stale, too few rows, pages another's,
memory's newer bytes in it), `Kernel::picture()` shows memory's, which finishes first. Measured: "Speed".

**Save states** stay exact: saving finishes the renderer (memory's VRAM has everything), loading has it forget its
copies.

**Measured in the app**, the RP6 (Adreno 740), the app's own stats (frames a second, capped at 60, and the
milliseconds a frame of emulation), each a short session:

| Scene | Vulkan, custom driver (Turnip, StevenMXZ v26.3.0-R5) | Vulkan, system driver (Qualcomm's) | Software |
|---|---|---|---|
| Lumines, demo | 60.0, 4.6 ms | 60.0, 4.7 ms | - |
| Peace Walker, title | 60.0, 2.2 ms | 60.0, 2.7 ms | - |
| Midnight Club 3, night city title | 54.3, 18.4 ms | 49.6, 20.2 ms | 35.6, 28.0 ms |
| Midnight Club 3, quick race | 39.0, 25.6 ms | 36.0, 27.7 ms | 29.0, 34.5 ms |

(The custom driver's column is the build before "Bytes beside the pixels", the others after it. The quick race
isn't the same race each time, so its numbers differ by a few frames a second for that too. The software renderer
wasn't measured in the app in the 2D scenes: the harness has it at 117.5 and 223.7, past the cap.) Both drivers
pass the start-up check and draw every scene right; Turnip is a little faster in the 3D ones. Neither failed, so
the app's toast for a fallback wasn't seen on the RP6; the desktop's fallback was (below), and
`tests/psp/gpu.cpp`'s lost GPU covers the renderer's side.

The other 19 games, booted and pressed through to their menus on Vulkan with the custom driver: no crash in any;
18 at 59.3-60 frames a second, from 0.9 to 13.3 ms a frame. Brave Story's title drew right but at 18.6 frames a second
(64 ms a frame, the software renderer 9.5): its display list beside the frame buffer ("Bytes beside the pixels"),
fixed in part 41, after which it runs at 59.5 (13.5 ms a frame). Ace Combat: Joint Assault, MotorStorm: Arctic
Edge and Killzone stay black as they do with the software renderer (the core's, not the renderer's). The desktop
program (the M1, MoltenVK) draws Lumines on the GPU at 60 frames a second, and with Vulkan made to fail (a loader
with no driver) says why once ("The Vulkan renderer couldn't start (no Vulkan instance)") and draws with the
software renderer.

## The plan

1. **Vulkan at native resolution** (part 36, done): the milestone above, measured on the M1 and the RP6.
2. **In Phobos** (part 41, done): the setting, the host's loader, the start-up check, the fallbacks, the frame shown
   from the GPU, measured in the app with the system and a custom driver.
3. **Upscaling** (part 44, done): an internal resolution from 1 (exact) to 10 times the PSP's, and presenting the
   target's image on Android's window without reading it back.
4. **Accuracy**: shader blending (part 45, done: blending, dithering, logic operations, write masks and 16-bit
   formats as the PSP's, on every Vulkan GPU); still to do, depth read back where games need it, block transfers
   between targets on the GPU, textures decoded on the GPU.
5. **OpenGL** (parked 2026-10-08): not worth a second HW backend yet; Software is the non-Vulkan path. Revisit only if telemetry shows need.
