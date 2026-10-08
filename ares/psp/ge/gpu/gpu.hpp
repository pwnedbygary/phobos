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
//What's drawn is handed to the GPU when a list ends (submit(), not waited for) and waited for only when VRAM's
//pages are needed (finish()).
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
    u32 flags;          //bit 0: filtered; 1: stepped (a 2D sprite's coordinates, below); 2: turned
    //A 2D sprite's texture coordinates as the GE steps them (draw.cpp's rectangle()): across x from columnFirst at
    //sixteenth columnStart, by columnStep a pixel; down y likewise. The coordinate across x is v when turned.
    float columnFirst, columnStep, rowFirst, rowStep;
    s32 columnStart, rowStart;
  };

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
    auto operator==(const Pipeline&) const -> bool = default;
  };
  static_assert(sizeof(Pipeline) == 32);
  //draw.frag's push constants
  struct Push {
    float scale[2];
    u32 alphaTest, colorReference, colorMask, fogColor, environment, depthRange, fixedA, stencil, dither[2];
    u32 textureSize;
    u32 resolution = 1;    //the target's: each of the PSP's pixels resolution x resolution of the GPU's
    u32 textureScale = 1;  //the texture's: a copy of a target's (render to texture) is at its resolution
    u32 fixedB = 0;        //BLEND_FIXED_B (shader blending's)
    u32 writeMask = 0;     //the frame buffer's bits a write leaves alone, in its format (shader blending's)
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
  //finish(), copied into a texture's top left for draws after to sample), and the screen's picture presented on the
  //window the backend shows on (Present: a target's top left, or with no target memory's picture, uploaded). Every
  //position and size is in the PSP's pixels: the backend has each of them Backend::scale times over.
  struct Command {
    enum class Kind : u8 { Draw, Upload, Readback, Copy, Present } kind;
    u32 target;
    u32 state = 0, first = 0, count = 0;  //Draw
    s32 x = 0, y = 0;                     //Upload, Readback, Copy
    u32 width = 0, height = 0;
    u64 colors = 0, stencil = 0, depth = 0;  //Upload: where in uploads (8888s, bytes, 16-bit depths); Present's
    u8 parts = 3;                            //Upload: bit 0 the colors and stencils, bit 1 the depths
    u32 texture = 0;                         //Copy
    //Readback: how many times the PSP's size its pixels come back: 1, the PSP's own (for memory: one of each pixel's
    //scale x scale, the same one each time, so that memory's bytes the GPU didn't draw over come back as they went
    //in), or more, up to the backend's scale, for the screen (shrunk smoothly, the colors alone, no stencil)
    u8 at = 1;
    u8 format = 3;  //Present: the frame buffer's GE format, its colors shown as memory keeps them (3: as they are)
  };
  struct Recorded {
    std::vector<Command> commands;
    std::vector<State> states;
    std::vector<Vertex> vertices;
    std::vector<u8> uploads;
    u32 readbacks = 0;
    auto empty() const -> bool { return commands.empty(); }
    auto clear() -> void { commands.clear(), states.clear(), vertices.clear(), uploads.clear(), readbacks = 0; }
  };

  struct Backend {
    virtual ~Backend() = default;
    bool lost = false;        //the GPU stopped answering: nothing more is drawn by it
    bool dualSource = false;  //dual-source blending (Source1Color)
    bool logicOps = false;    //its own logic operations
    //Shader blending: the target's pixel read in the fragment shader (an input attachment), so blending, dithering,
    //logic operations and write masks are pixel.cpp's own; the backend puts a barrier before each draw that reads.
    //inOrder where a draw's overlapping primitives see each other's pixels in order (rasterization order access),
    //else the renderer draws those apart (emit()); System::startRenderer() turns it off where the check fails so
    bool reads = false, readsInOrder = false;
    //Whether the last run put a picture on the window: a Present's swapchain image acquired and presented (not
    //where there's no window, or the acquiring timed out: the host shows the frame itself then)
    bool presented = false;
    //The internal resolution (GPU::resolution()): each of the PSP's pixels is scale x scale of the GPU's in every
    //target, its drawing, its copies and its depth. 1, the PSP's own, draws its pixels alone (the exact native
    //mode); mostScale is the most its pictures' size allows (a target 512 of the PSP's pixels across).
    u32 scale = 1, mostScale = 1;
    u64 pipelines = 0;        //made so far
    u64 recording = 0, submitting = 0, waiting = 0;  //nanoseconds in run()'s recording, vkQueueSubmit, the waits
    u64 passes = 0;                                  //render passes begun
    virtual auto name() const -> std::string = 0;
    virtual auto makeTarget(u32 width, u32 height) -> u32 = 0;  //0: it couldn't
    virtual auto dropTarget(u32 target) -> void = 0;
    //(texels: none for one a Copy fills) 0: it couldn't
    virtual auto makeTexture(u32 width, u32 height, const u32* texels) -> u32 = 0;
    virtual auto dropTexture(u32 texture) -> void = 0;  //(once the GPU is done with it)
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
    u64 used = 0;
    u64 version = 0;              //goes up whenever its pixels may change (filled, drawn into)
    //VRAM's bytes (offsets, inclusive) someone reached in its pages while the GPU drew in them, not over what it
    //drew (drawnOver()): memory's, maybe changed, so it's filled afresh before it draws, shows or lends a texture
    //over any of them. (At most 16 ranges; more are taken together.)
    std::vector<std::pair<u32, u32>> beside;
    auto bytes() const -> u32 { return format == 3 ? 4 : 2; }
    auto end() const -> u32 { return address + rows * stride * bytes(); }  //(the VRAM bytes it covers)
    auto drawn() const -> bool { return left <= right; }
    //gpu.cpp: whether VRAM's bytes first to last reach its pixels left-right, top-bottom (or those of beside's)
    auto reaches(u32 first, u32 last, s32 left, s32 top, s32 right, s32 bottom) const -> bool;
    auto besideReaches(s32 left, s32 top, s32 right, s32 bottom) const -> bool;
    auto touch(u32 first, u32 last) -> void;  //(beside's: first to last added)
  };
  //A decoded texture on the GPU, while the GE keeps it
  struct Texture { std::weak_ptr<GE::Decoded> decoded; u32 id = 0, rows = 0; };

  struct Statistics {
    u64 draws = 0, primitives = 0, submits = 0, finishes = 0, uploads = 0, readbacks = 0, textures = 0, copies = 0;
    u64 pictures = 0;  //frames shown straight from a target (picture())
    u64 readingDraws = 0, splits = 0;  //draws blended in the shader; those begun as their primitives overlapped
    u64 presents = 0, presentsFromMemory = 0;  //frames presented on the window (show()): from a target, memory's
    u64 waiting = 0;   //nanoseconds the CPU waited in finish() and picture()
  } statistics;

  std::unique_ptr<Backend> backend;
  std::function<void(const std::string&)> report;  //what went wrong (once); stderr's "PSP GPU:" if none
  bool reported = false;

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
  static auto vulkan(void* getInstanceProcAddr, std::string& error) -> std::unique_ptr<GPU>;

private:
  static constexpr u32 SubmitEvery = 128;  //commands recorded, handed to the GPU without waiting for the list's end
  GE* ge = nullptr;
  Recorded recorded;
  std::vector<std::unique_ptr<Target>> targets;
  Target* target = nullptr;  //the PRIM's
  bool drawing = false;      //the PRIM draws something (begin())
  State state;               //its settings
  u32 filter = 0, shade = 0; //TEXTURE_FILTER, SHADE_MODE
  bool through = false, textured = false;
  u32 textureWidth = 0, textureHeight = 0;
  std::unordered_map<const GE::Decoded*, Texture> textures;
  Target* owners[GE::VRAMPages] = {};  //VRAM's pages whose newest pixels are the GPU's: the target drawn there
  u64 uses = 0;
  //A texture in a target the GPU has drawn (holds()), for the next PRIM: where, and the texture it's copied into
  //(one for each place and size, kept with the target)
  struct Held { Target* target; s32 x, y; u32 width, rows, format; };
  std::optional<Held> held;
  //(one for each target and size, the place it was last copied from, and the target's version then; at most
  //MostCopies, the one unused longest let go for another, and none unused for CopyAge PRIMs: release())
  struct Copied { u32 texture; u64 version; s32 x, y; u64 used; };
  std::map<std::tuple<u32, u32, u32>, Copied> copies;  //(target, width, rows)
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

  auto targetFor(const GE::PixelState& p) -> Target*;
  auto fill(Target& t, u32 from, u32 to, const GE::PixelState& p, u8 parts = 1) -> void;
  auto release() -> void;
  auto own(Target& t, s32 left, s32 top, s32 right, s32 bottom) -> void;
  auto textureFor(const GE::Look& look, u8& texels, u32& scale) -> u32;
  auto settings(const GE::Look& look) -> void;
  auto emit(const Vertex* vertices, u32 count) -> void;
  auto vertex(const GE::Vertex& v, bool perspective) const -> Vertex;
  auto lose() -> void;
  //the target picture() and show() take the frame buffer's pixels from: none where the GPU hasn't the newest
  auto shownTarget(u32 address, u32 stride, u32 format, u32 width, u32 height) -> Target*;
  auto tell(const std::string& what) -> void;
};

}
