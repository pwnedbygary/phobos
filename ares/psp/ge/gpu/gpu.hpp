#pragma once

#include "../ge.hpp"

#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>

//The GE's hardware renderer (docs/psp-gpu-renderers.md): the GPU's own rasterizer, texture units and blending
//drawing the GE's primitives, fast and with room for upscaling, as close to the PSP as the hardware allows. The
//software renderer stays the exact one. PPSSPP's GPU backends (its draw engine, framebuffer manager and texture
//cache) informed the design; none of their code is used.
//
//What stays on the CPU, as for the software renderer: the display lists, the vertices, the transform, lighting and
//clipping (ge/transform.cpp, ge/lighting.cpp), the texture decoding (ge/texture.cpp's decoded copies). The GE hands
//the renderer each PRIM's settings and its primitives' vertices on the screen (GE::Renderer, ge.hpp); the renderer
//turns them into draws:
//  - Frame buffers ("targets"): each frame buffer the GE draws into (address, width, format) is a picture on the GPU,
//    8888 with an 8-bit stencil (the PSP's stencil is its frame buffer's alpha) and a depth buffer. It's filled from
//    memory's VRAM before it's first drawn into, and again whenever someone else has changed those bytes since
//    (written()); what's drawn stays on the GPU, and VRAM's pages under it are the renderer's (busy, Memory::
//    busyPages) until something needs them: the CPU, the screen, a texture, a block transfer (finish()). Then the
//    pixels drawn are read back and put into memory's VRAM in the frame buffer's format, the stencil as its alpha.
//  - Textures: the GE's decoded copies, each put on the GPU once and kept while the GE keeps the copy.
//  - Draws: a primitive's vertices go into a list for the GPU, consecutive primitives with the same settings into
//    one draw; the settings make a pipeline (Pipeline: what the GPU's fixed stages do, and the fragment shader's
//    specialization constants), which the backend makes once and keeps.
//What's drawn is handed to the GPU as it piles up and with each frame shown (submit(), not waited for), and waited for
//only when VRAM's pages are needed (finish()).
//
//The backend (Backend: Vulkan's in vulkan.cpp, OpenGL's to come) only runs what the renderer recorded: it knows
//nothing of the PSP.

namespace ares::PlayStationPortable {

struct GPU : GE::Renderer {
  //A vertex as the shaders take it (shaders/draw.vert)
  struct Vertex {
    float x, y, z, w;   //in the target's pixels; the depth, 0-65535; clip w (1: no perspective)
    float u, v, q;      //texels; q, the divisor
    u32 color, specular;
    float fog;
    u32 flags;          //bit 0: filtered; 1: stepped (a 2D sprite's coordinates, below); 2: turned; 3: held
    //A 2D sprite's texture coordinates as the GE steps them (draw.cpp's rectangle()): across x from columnFirst at
    //sixteenth columnStart, by columnStep a pixel; down y likewise. The coordinate across x is v when turned.
    //Held (a 2D triangle above 1x: triangle()): u kept from columnFirst to columnStep, v from rowFirst to rowStep.
    float columnFirst, columnStep, rowFirst, rowStep;
    s32 columnStart, rowStart;
  };

  //Fast mode's vertices (mesh(): docs/psp-gpu-renderers.md, "Vulkan (fast)"): a 3D PRIM's, as the vertex type laid
  //them out (morph targets blended), untransformed, for shaders/transform.vert to put on the screen
  struct Model {
    float x, y, z;
    float normal[3];
    float u, v;
    u32 color;
    float weights[8];
    u32 transform;  //its PRIM's settings, in the run's Transformed blocks
  };
  static_assert(sizeof(Model) == 72);
  //And the PRIM's 3D settings (the GE's Transform) for it: one of transform.vert's storage buffer's blocks, laid out
  //as std430 has it (every member 16 bytes or a multiple, so std140 would lay it out the same)
  struct Transformed {
    float world[4][4];       //4x3 matrices as four columns (where x, y and z go, then the move), each padded to 4
    float view[4][4];
    float projection[4][4];
    float texture[4][4];
    float viewport[4];       //VIEWPORT_X/Y/Z_SCALE; 0
    float center[4];         //VIEWPORT_X/Y/Z_CENTER; 0
    float offset[4];         //OFFSET_X and OFFSET_Y in pixels; the texture's width and height
    float textureScale[4];   //TEX_SCALE_U, _V, TEX_OFFSET_U, _V
    float fog[4];            //FOG1, FOG2, the value every vertex takes where FOG1 isn't a number; 1 for that
    u32 modes[4];            //TEXTURE_MAP_MODE's source; TEXTURE_SHADE_MAPPING's lights for u and v; bits: 0 the
                             //normal reversed, 1 the shine kept apart, 2 the vertex has a color, 3 fog, 4
                             //lighting, 5-6 how texture coordinates are made (TEXTURE_MAP_MODE's 0-2), 8-11 the
                             //bone matrices mixed (0-8)
    u32 material[4];         //MATERIAL_COLOR; MATERIAL_EMISSIVE; the ambient color (8888); MATERIAL_DIFFUSE
    u32 material2[4];        //MATERIAL_SPECULAR; the ambient light (8888); 0; 0
    float viewDirection[4];  //the way the view looks, in the world; MATERIAL_SPECULAR_COEF as lighting takes it
    struct Light {
      float position[4];     //and 0
      float direction[4];    //one long; its cutoff
      float attenuation[4];  //constant, linear, quadratic; its exponent
      u32 colors[4];         //ambient, diffuse, shine; bits: 0 on, 1 directional, 2 spot, 3 shines, 4 powered
    } lights[4];
    float bones[8][4][4];
  };
  static_assert(sizeof(Transformed) == 1168);
  static_assert(offsetof(Transformed, viewport) == 256 && offsetof(Transformed, lights) == 400 &&
                offsetof(Transformed, bones) == 656);

  //What the GPU's fixed stages are set to, and the fragment shader's constants: one pipeline each.
  enum : u8 { Zero, One, SourceColor, OneMinusSourceColor, DestinationColor, OneMinusDestinationColor,
              DestinationAlpha, OneMinusDestinationAlpha, Constant, Source1Color, OneMinusSource1Color,
              SourceAlpha, OneMinusSourceAlpha };                          //blend factors
  enum : u8 { Add, Subtract, ReverseSubtract, Minimum, Maximum };          //blend operations
  enum : u8 { Keep, Clear, Replace, Invert, Increment, Decrement };        //stencil operations
  //(comparisons are the GE's: 0 never, 1 always, 2 equal, 3 not equal, 4 less, 5 less or equal, 6 greater, 7
  //greater or equal; logic operations the GE's too, 0-15, which are Vulkan's)
  struct Pipeline {
    //draw.frag's specialization constants, in its order
    u8 textured, function, alphaTest, colorTest, fog, depthRange, clear, source, destination, alphaOut, dither,
       quantize, logic, clamp;
    u8 blend, sourceFactor, destinationFactor, operation;
    u8 colorMask;  //bits 0-3: red, green, blue, alpha written
    u8 depthTest, depthCompare, depthWrite;
    u8 stencilTest, stencilCompare, stencilFail, stencilDepthFail, stencilPass;
    u8 logicOp, logicOperation;  //the GPU's own logic operation
    u8 texels;                   //draw.frag's TEXELS: a texture taken from a 16-bit frame buffer's format (4 not)
    //Shader blending (docs/psp-gpu-renderers.md, "Shader blending"): draw.frag reads the frame buffer's pixel and
    //does what follows the tests as pixel.cpp does (READS 1), with source and destination the GE's factors (0-15)
    //and blending its BLEND_MODE operation (0-7; 8 not blending: a logic operation or a write mask alone)
    u8 reads, blending;
    //draw.frag's TERM: output 0's color as pixel.cpp's source term, which the GPU's blending adds the destination's
    //term to (1) or subtracts it from or the other way round (2); 0 the color weighed as floats
    u8 term;
    //Fast mode's 3D PRIMs (mesh()): transform.vert puts the vertices on the screen (transformed 1); draw.frag's FLAT
    //takes the corners' colors as the GE's flat shading does. (Skinning, lighting and texture mapping are the PRIM's
    //Transformed block's to say: each a constant would multiply the pipelines. The GE has culled and clipped:
    //meshTriangles().)
    u8 transformed, flat;
    auto operator==(const Pipeline&) const -> bool = default;
  };
  static_assert(sizeof(Pipeline) == 35);
  //draw.frag's push constants
  struct Push {
    float scale[2];
    u32 alphaTest, colorReference, colorMask, fogColor, environment, depthRange, fixedA, stencil, dither[2];
    u32 textureSize;
    u32 resolution = 1;    //the target's: each of the PSP's pixels resolution x resolution of the GPU's
    u32 textureScale = 1;  //the texture's: a copy of a target's (render to texture) is at its resolution
    u32 fixedB = 0;        //BLEND_FIXED_B (shader blending's)
    u32 writeMask = 0;     //the frame buffer's bits a write leaves alone, in its format (shader blending's)
    u32 filter = 0;        //TEXTURE_FILTER, where the filter is chosen at each pixel (fast mode's 3D: mesh())
    //A copy of a target with more texels than its PRIM takes (render to texture: textureFor()): the texels taken,
    //its width and, in bits 16-31, its rows, which fetches are held inside as they'd be in a copy of only those
    //(0: the copy's own)
    u32 held = 0;
    u32 origin = 0;  //the row of the target its frame buffer's first is (targetFor()), where the dither starts
    auto operator==(const Push&) const -> bool = default;
  };
  //A draw's settings, all of them
  struct State {
    Pipeline pipeline{};
    Push push{};
    u32 stencilReference = 0, stencilCompareMask = 0, stencilWriteMask = 0;
    u32 blendConstant = 0;  //8888 (Constant's)
    s32 scissor[4] = {};    //left, top, width, height
    u32 texture = 0;        //the backend's (0: none)
    u32 target = 0;
    auto operator==(const State&) const -> bool = default;
  };

  //What the backend runs, in order: draws (a state's, count vertices from first), and pictures put into, read from
  //or copied out of a target (a rectangle; uploaded from the renderer's upload bytes, read back to be read() after
  //finish(), copied into a texture for draws after to sample: its top left, or a part of it taken again), and the
  //screen's picture presented on the window the backend shows on (Present: a target's top left, or with no target
  //memory's picture, uploaded), or only a shot of a target's top left taken for the host, as a Present takes one
  //(Shot); fast mode's 3D draws (Mesh: count of the indices from first, into the models, each model's settings
  //its Transformed block); and a rectangle of another target's pixels put into a target as memory would have them
  //(Move: from source's rectangle at sourceX, sourceY, the same bytes in its format, sourceFormat, as the target's,
  //format). Every position and size is in the PSP's pixels: the backend has each of them Backend::scale times over.
  struct Command {
    enum class Kind : u8 { Draw, Upload, Readback, Copy, Present, Shot, Mesh, Move } kind;
    u32 target;
    u32 state = 0, first = 0, count = 0;  //Draw, Mesh
    s32 x = 0, y = 0;                     //Upload, Readback, Copy
    u32 width = 0, height = 0;
    u64 colors = 0, stencil = 0, depth = 0;  //Upload: where in uploads (8888s, bytes, 16-bit depths); Present's
    u8 parts = 3;                            //Upload: bit 0 the colors and stencils, bit 1 the depths
    u32 texture = 0;                         //Copy
    u16 intoX = 0, intoY = 0;                //Copy: where in the texture (its top left the target's x, y)
    //Readback: how many times the PSP's size its pixels come back: 1, the PSP's own (for memory: one of each pixel's
    //scale x scale, the same one each time, so that memory's bytes the GPU didn't draw over come back as they went
    //in), or more, up to the backend's scale, for the screen (shrunk smoothly, the colors alone, no stencil)
    u8 at = 1;
    u8 format = 3;  //Present: the frame buffer's GE format, its colors shown as memory keeps them (3: as they are)
    u8 sourceFormat = 3;            //Move
    u32 source = 0;
    u16 sourceX = 0, sourceY = 0;
  };
  struct Recorded {
    std::vector<Command> commands;
    std::vector<State> states;
    std::vector<Vertex> vertices;
    std::vector<u8> uploads;
    std::vector<Model> models;   //(Mesh's)
    std::vector<u32> indices;
    std::vector<Transformed> transforms;
    u32 readbacks = 0;
    auto empty() const -> bool { return commands.empty(); }
    auto clear() -> void {
      commands.clear(), states.clear(), vertices.clear(), uploads.clear(), readbacks = 0;
      models.clear(), indices.clear(), transforms.clear();
    }
  };

  struct Backend {
    virtual ~Backend() = default;
    bool lost = false;        //the GPU stopped answering: nothing more is drawn by it
    bool dualSource = false;  //dual-source blending (Source1Color)
    bool logicOps = false;    //its own logic operations
    //Shader blending: the target's pixel read in the fragment shader (an input attachment), so blending, dithering,
    //logic operations and write masks are pixel.cpp's own. readsInOrder where the GPU keeps every draw's pixels in
    //rasterization order (rasterization order access), which needs no barrier between draws; without it each draw
    //that reads has one, and the renderer draws its overlapping primitives apart (emit()), which a 3D game makes
    //thousands of a frame: there only what the GPU's own units can't come close to reads (a write mask keeping
    //part of a channel, a logic operation, the absolute difference, doubled alphas, blending in a 16-bit frame
    //buffer), unless readsBlending asks for every blend as well (as it is in order, and as the tests ask).
    //System::startRenderer() turns these off as the start-up check fails, so the log can say how it draws instead
    //of failing the renderer. readsApart where reading without the order can be trusted: not on Qualcomm's own
    //driver, whose reads in a game's frame buffer see pixels as they were before the draws just before.
    bool reads = false, readsInOrder = false, readsBlending = false, readsApart = true;
    //Fast mode's 3D (mesh()): whether the GPU transforms vertices itself (the driver took transform.vert)
    bool transforms = false;
    //Whether it moves pixels from one target into another (Command::Move: the driver took move.frag's pipelines)
    bool moves = false;
    //Whether the last run put a picture on the window: a Present's swapchain image acquired and presented (not
    //where there's no window, or the acquiring timed out: the host shows the frame itself then)
    bool presented = false;
    //The internal resolution (GPU::resolution()): each of the PSP's pixels is scale x scale of the GPU's in every
    //target, its drawing, its copies and its depth. 1, the PSP's own, draws its pixels alone (the exact native
    //mode); mostScale is the most its pictures' size allows (a target 512 of the PSP's pixels across).
    u32 scale = 1, mostScale = 1;
    u64 pipelines = 0;        //made so far
    u64 pipelineMaking = 0;   //nanoseconds the driver took making them
    u64 recording = 0, submitting = 0, waiting = 0;  //nanoseconds in run()'s recording, vkQueueSubmit, the waits
    u64 passes = 0;                                  //render passes begun
    virtual auto name() const -> std::string = 0;
    virtual auto makeTarget(u32 width, u32 height) -> u32 = 0;  //0: it couldn't
    virtual auto dropTarget(u32 target) -> void = 0;
    //(texels: none for one a Copy fills; kept, not copied, until they're on the GPU, by owner where one is given,
    //which they mustn't change under) 0: it couldn't
    virtual auto makeTexture(u32 width, u32 height, const u32* texels, std::shared_ptr<const void> owner = {})
      -> u32 = 0;
    virtual auto dropTexture(u32 texture) -> void = 0;  //(once the GPU is done with it)
    //Whether draws of this pipeline can be made (the driver took its shaders): fast mode's 3D asks before it records
    //one (mesh()), so that a driver refusing them leaves the GE to transform those PRIMs itself
    virtual auto drawable(const Pipeline&) -> bool { return true; }
    //The pipelines made so far, as the driver keeps them, for the next session to start with (System's "Pipeline
    //Cache": the GPU::vulkan() of a game's next start); none where it has nothing to say
    virtual auto pipelineData() -> std::vector<u8> { return {}; }
    virtual auto submit(const Recorded& recorded) -> bool = 0;  //run, not waited for
    virtual auto finish(const Recorded& recorded) -> bool = 0;  //run, and everything waited for
    //Readback n of the last finish(): its 8888s and stencils (none for one at more than 1), a row after another
    virtual auto read(u32 n, const u32*& colors, const u8*& stencil) -> bool = 0;
    //Presenting (docs/psp-gpu-renderers.md, "Presenting"): the window it shows Present's pictures on (the host's:
    //an ANativeWindow on Android), none to let go of it; whether it presents (it can, and hasn't given up: the host
    //shows the screen's frames otherwise); and the newest picture it presented from a target, at the PSP's size as
    //it was in the target (8888, red in the low byte) and its GE format, swapped into pixels (false: none newer).
    virtual auto window(void*) -> void {}
    virtual auto presents() const -> bool { return false; }
    virtual auto shot(std::vector<u32>&, u32&, u32&, u32&) -> bool { return false; }
  };

  //A frame buffer on the GPU
  struct Target {
    u32 address, stride, format;  //VRAM offset, pixels a row, GE format
    u32 id = 0;                   //the backend's
    u32 height = 0;               //rows it has room for
    u32 rows = 0;                 //rows filled from memory so far (stale: none)
    bool stale = true;            //memory's VRAM has changed under it: filled afresh before it's drawn into
    //Its depth buffer (the GE's, the last PRIM's: VRAM's offset through its fourth copy, and its row width), the
    //rows of it filled from memory so far, and those memory has changed since (filled again before the next draw).
    //The GPU's depth stays on the GPU: memory's isn't changed by what the GPU draws.
    u32 depthBuffer = 0, depthStride = 0, depthRows = 0;
    u32 depthChangedFrom = 0, depthChangedTo = 0;
    s32 left = 0, top = 0, right = -1, bottom = -1;  //drawn since it was last read back
    s32 changes[4] = {0, 0, -1, -1};  //changed since its copies last heard (textureFor()): left, top, right, bottom
    std::vector<std::array<s32, 4>> told;  //rectangles memory has heard were drawn over since then (own())
    u64 used = 0;
    //VRAM's bytes (offsets, inclusive) someone reached in its pages while the GPU drew in them, not over what it
    //drew (drawnOver()), or changed in pages it hasn't drawn in since the last finish (written()): memory's, maybe
    //changed, so they're filled again from memory before it draws, shows or lends a texture over any of them
    //(revive()). (At most 16 ranges; more are taken together, merged, which may take in its pixels between: then
    //it's filled afresh, what it drew put back first.)
    std::vector<std::pair<u32, u32>> beside;
    bool merged = false;
    //VRAM's bytes (offsets, inclusive) in its drawn rows a block transfer has written whole since it drew there
    //(overwritten()): memory's, not put back, and filled again before a PRIM draws over them or they're copied or
    //shown or moved (revive()). (At most 16; where there'd be more, the transfer waits for a finish.)
    std::vector<std::pair<u32, u32>> dead;
    auto bytes() const -> u32 { return format == 3 ? 4 : 2; }
    auto end() const -> u32 { return address + rows * stride * bytes(); }  //(the VRAM bytes it covers)
    auto drawn() const -> bool { return left <= right; }
    //gpu.cpp: whether VRAM's bytes first to last reach its pixels left-right, top-bottom (or those of beside's)
    auto reaches(u32 first, u32 last, s32 left, s32 top, s32 right, s32 bottom) const -> bool;
    auto besideReaches(s32 left, s32 top, s32 right, s32 bottom) const -> bool;
    auto touch(u32 first, u32 last) -> void;  //(beside's: first to last added)
    auto deadReaches(s32 left, s32 top, s32 right, s32 bottom) const -> bool;
    auto covered(u32 first, u32 last) const -> bool;  //(whether dead has every byte first to last)
    auto drawnBytes(u32 first, u32 last) const -> bool;  //(whether it drew any of them, not dead since)
  };
  //A decoded texture on the GPU, while the GE keeps it
  struct Texture { std::weak_ptr<GE::Decoded> decoded; u32 id = 0, rows = 0; };

  struct Statistics {
    u64 draws = 0, primitives = 0, submits = 0, finishes = 0, uploads = 0, readbacks = 0, textures = 0, copies = 0;
    u64 copied = 0;    //the targets' pixels those copies took (render to texture: textureFor())
    u64 pictures = 0;  //frames shown straight from a target (picture())
    u64 readingDraws = 0, splits = 0;  //draws blended in the shader; those begun as their primitives overlapped
    u64 presents = 0, presentsFromMemory = 0;  //frames presented on the window (show()): from a target, memory's
    u64 meshes = 0;    //3D PRIMs the GPU transformed (fast mode: mesh(), the first of each PRIM's runs)
    u64 moves = 0;     //pixels moved from one target into another on the GPU (move()), not put back and filled
    u64 waiting = 0;   //nanoseconds the CPU waited in finish() and picture()
  } statistics;

  std::unique_ptr<Backend> backend;
  std::function<void(const std::string&)> report;  //what went wrong (once); stderr's "PSP GPU:" if none
  bool reported = false;
  //Vulkan (fast) (docs/psp-gpu-renderers.md): 3D PRIMs' triangles transformed, lit and clipped by the GPU (mesh()),
  //the GPU's own blending throughout; else Vulkan (accurate)
  bool fast = false;
  //The start-up check's 3D through the GPU's transform failed, and the rest passed (check()): System checks again
  //with the GE transforming
  bool transformsFailed = false;

  //gpu.cpp
  GPU(std::unique_ptr<Backend> backend);
  ~GPU();
  auto ready() const -> bool override { return backend && !backend->lost; }
  auto begin(GE& ge, const GE::Look& look, bool through, const GE::Region& region) -> bool override;
  auto triangle(const GE::Vertex& a, const GE::Vertex& b, const GE::Vertex& c) -> void override;
  auto sprite(const GE::Job& job) -> void override;
  auto point(const GE::Vertex& at) -> void override;
  auto line(const GE::Job& job, const std::vector<GE::LinePixel>& pixels) -> void override;
  auto submit(GE& ge) -> void override;
  auto finish(GE& ge) -> void override;
  auto written(GE& ge, u32 page) -> void override;
  auto forget(GE& ge) -> void override;
  auto holds(GE& ge, const GE::Sampler& texture, u32 rows, u32 columns) -> bool override;
  auto drawnOver(GE& ge, u32 first, u32 last) -> bool override;
  auto besideChanged(GE& ge, u32 first, u32 last) -> void override;
  auto overwritten(GE& ge, u32 first, u32 last) -> void override;
  auto meshes(GE& ge, const GE::Transform& t, u32 count) -> bool override;
  auto mesh(GE& ge, const GE::Transform& t, const std::vector<GE::Vertex>& vertices, const std::vector<u32>& corners,
            bool again) -> void override;
  auto copiesKept() const -> u32 { return copies.size(); }  //(render-to-texture copies on the GPU: the tests')

  //What the screen shows (the frame buffer at VRAM's offset address, stride pixels a row, in GE format), width x
  //height of it as Kernel::picture() makes it (8888, red in the low byte, alpha 255), read straight from the target
  //the GPU drew it in: only those pixels come back, VRAM's pages stay the GPU's and nothing else is waited for but
  //the drawing before. False, with pixels untouched, where the GPU doesn't hold the newest of every pixel shown (no
  //such target, or rows it hasn't, or pages another target or the CPU has changed since), or hasn't drawn there
  //since memory last had it all: memory's VRAM is the picture then.
  //At at times the PSP's size (up to resolution()), the picture is width x height times at, shrunk from the GPU's.
  auto picture(u32 address, u32 stride, u32 format, u32 width, u32 height, std::vector<u32>& pixels, u32 at = 1)
    -> bool;
  //Presenting, where the backend shows on a window itself (presents(): Android's): show() the frame buffer as
  //picture() would read it, straight from its target with nothing read back or waited for (false, with nothing
  //presented, where picture() would be false: memory has the picture then), or memory's picture, width x height
  //of 8888s as Kernel::picture() makes them; shot(), the newest picture show() presented from a target, as it was
  //(its GE format's colors not yet narrowed; false: none newer since the last). presented(): whether the last
  //show() put the picture on the window (false where it couldn't, no window or a timeout: the host shows it then).
  auto presents() const -> bool { return ready() && backend->presents(); }
  auto presented() const -> bool { return ready() && backend->presented; }
  auto window(void* window) -> void { if(backend) backend->window(window); }
  auto show(u32 address, u32 stride, u32 format, u32 width, u32 height) -> bool;
  auto show(const std::vector<u32>& pixels, u32 width, u32 height) -> void;
  auto shot(std::vector<u32>& pixels, u32& width, u32& height, u32& format) -> bool {
    return backend && backend->shot(pixels, width, height, format);
  }
  //A late frame, for a host that shows frames read back but takes them late (System's "Late Frames": the runner
  //timing the GPU as the app presents, which reads nothing back): the frame buffer as picture() would read it, a
  //shot of it taken on the GPU at the PSP's size, as show() takes one, with nothing waited for; shot() has it once
  //its run is done, a frame or two later. False where picture() would be false, or the GPU stopped answering.
  auto shoot(u32 address, u32 stride, u32 format, u32 width, u32 height) -> bool;
  //Everything on the GPU let go (the targets, the textures and their copies) without a pixel read back: for a
  //machine powered on afresh, whose memory is new.
  auto drop() -> void;
  //The internal resolution: each of the PSP's pixels drawn scale x scale times over (1-10, held to what the GPU
  //takes: Backend::mostScale), from now on. Everything on the GPU is let go first, as drop() does: for a machine
  //powered on afresh. 1, the PSP's own, is the exact native mode.
  auto resolution(u32 scale) -> void;
  auto resolution() const -> u32 { return backend ? backend->scale : 1; }

  //check.cpp: the GPU's pixels against the software renderer's for a few dozen primitives drawn in a machine of its
  //own (sprites exactly, triangles closely), at start-up; why not, where they aren't. Everything's dropped after.
  auto check(std::string& error) -> bool;

  //vulkan.cpp: a renderer on the first Vulkan GPU, through the host's vkGetInstanceProcAddr (the loader or driver
  //the host chose, a custom one included: docs/psp-gpu-renderers.md, "Vulkan"), or, with none, the system's loader;
  //none, and why, where there's no Vulkan or no GPU fit for it.
  //cached: pipelines a session before made (Backend::pipelineData()), taken where they're this device's and driver's.
  static auto vulkan(void* getInstanceProcAddr, std::string& error, bool fast = false,
                     const std::vector<u8>& cached = {}) -> std::unique_ptr<GPU>;

private:
  static constexpr u32 SubmitEvery = 128;  //commands recorded, handed to the GPU without waiting for the list's end
  static constexpr u32 SubmitEnough = 32;  //and at a list's end, at least these (submit())
  GE* ge = nullptr;
  Recorded recorded;
  std::vector<std::unique_ptr<Target>> targets;
  Target* target = nullptr;  //the PRIM's
  u32 column = 0, row = 0;   //and where in it the PRIM's frame buffer starts (targetFor())
  bool drawing = false;      //the PRIM draws something (begin())
  State state;               //its settings
  bool stateKept = false;    //and they're the last recorded's (emit())
  u32 filter = 0, shade = 0; //TEXTURE_FILTER, SHADE_MODE
  bool through = false, textured = false;
  u32 textureWidth = 0, textureHeight = 0;
  std::unordered_map<const GE::Decoded*, Texture> textures;
  Target* owners[GE::VRAMPages] = {};  //VRAM's pages whose newest pixels are the GPU's: the target drawn there
  //(drawnOver()'s: a page whose owner has drawn none of its bytes, but dead ones, as of drawnSerial, which goes up as
  //anything is drawn or owned afresh: God of War's GE reads 700,000 vertices a frame from VRAM's busy pages)
  u64 clean[GE::VRAMPages] = {}, drawnSerial = 1;
  u64 uses = 0;
  //A texture in a target the GPU has drawn (holds()), for the next PRIM: where, and the texture it's copied into
  //(one for each place and size, kept with the target); its rows from split on in next, from that one's first, where
  //it runs on into the frame buffer below
  struct Held { Target* target; s32 x, y; u32 width, rows, format; Target* next = nullptr; u32 split = 0; };
  std::optional<Held> held;
  //(one for each target and place copied from, its width and rows, and the rectangle of the target changed since,
  //left, top, right, bottom in its pixels, none where left is past right: taken again, only that part is copied, so
  //an effect drawing again and again into one corner of the frame buffer it samples copies the corner, not the
  //picture, as Killzone's menu blurs a strip right of its picture 1,100 times a frame. A PRIM taking fewer rows or
  //columns from the place takes them from it too (Push::held). At most MostCopies, the one unused longest let go for
  //another, and none unused for CopyAge PRIMs: release().)
  struct Copied { u32 texture; u32 width, rows; u64 used; s32 dirty[4] = {0, 0, -1, -1}; };
  std::map<std::tuple<u32, s32, s32>, Copied> copies;  //(target, x, y)
  auto changed(Target& t, s32 left, s32 top, s32 right, s32 bottom) -> void;  //(Target::changes)
  //The PRIM's target, scissor and the extent of its corners drawn so far (none: left past right), given to the
  //target's changes as the next PRIM begins (reached())
  static constexpr float Far = 1e30f;
  struct Reach {
    Target* target = nullptr;
    s32 scissor[4] = {};
    float box[4] = {Far, Far, -Far, -Far};
  } reach;
  auto reached() -> void;
  static constexpr u32 MostCopies = 32;
  static constexpr u64 CopyAge = 1 << 14;
  //The current draw's primitives' boxes (left, top, right, bottom in the target's pixels, at most MostBoxes), where
  //it reads the frame buffer without the GPU keeping their order (Backend::readsInOrder): one overlapping them
  //begins another draw
  std::vector<std::array<float, 4>> boxes;
  static constexpr u32 MostBoxes = 64;
  //(a PRIM's vertices handed to the GPU, without waiting for its end, once there are this many: a long line's
  //pixels are six each)
  static constexpr u32 MostVertices = 1 << 18;
  static constexpr u32 MostTold = 8;  //(Target::told's)
  static constexpr u32 MeshLeast = 16;  //(the fewest vertices a PRIM meshes() takes has)

  auto targetFor(const GE::PixelState& p, const GE::Region& region, u32& column, u32& row) -> Target*;
  auto fill(Target& t, u32 from, u32 to, const GE::PixelState& p, u8 parts = 1, u32 left = 0, u32 right = ~0u)
    -> void;
  auto revive(Target& t, std::vector<std::pair<u32, u32>>& ranges) -> void;  //(ranges of its bytes filled from memory)
  auto release() -> void;
  auto flush() -> void;  //(what's recorded handed to the GPU now: submit()'s)
  //(pages from first to last another target has drawn in, whose pixels move into t on the GPU; and the rectangle of
  //from's pixels moved, in its own pixels, where movable(): none for left past right)
  struct Moving { Target* from; u32 first, last; };
  std::vector<Moving> moving;
  auto movable(const Target& from, const Target& t, u32 first, u32 last, s32 (&box)[4]) const -> bool;
  auto move(Target& from, Target& t, const Moving& m) -> void;
  auto own(Target& t, s32 left, s32 top, s32 right, s32 bottom) -> void;
  auto textureFor(const GE::Look& look, u8& texels, u32& scale, u32& taken) -> u32;
  auto settings(const GE::Look& look) -> void;
  auto emit(const Vertex* vertices, u32 count) -> void;
  auto vertex(const GE::Vertex& v, bool perspective) const -> Vertex;
  auto transformed(const GE::Transform& t) -> u32;  //(mesh()'s Transformed block for t: its index in transforms)
  GE::Transform lastTransform{};                    //(the last one's settings, and where its frame buffer was)
  u32 lastColumn = 0, lastRow = 0;
  std::optional<Pipeline> lastDrawable;             //(the last pipeline meshes() found drawable)
  u32 meshFirst = 0;                                //(the last PRIM's first model, in recorded's)
  auto lose() -> void;
  //the target picture() and show() take the frame buffer's pixels from: none where the GPU hasn't the newest
  auto shownTarget(u32 address, u32 stride, u32 format, u32 width, u32 height) -> Target*;
  auto tell(const std::string& what) -> void;
};

}
