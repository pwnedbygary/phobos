//The hardware renderer (ares/psp/ge/gpu, docs/psp-gpu-renderers.md) against the software renderer, the exact one:
//random 2D sprites, flat and textured, in each frame buffer format, which the GPU must draw byte for byte the same;
//a frame buffer drawn and then sampled as a texture (render to texture: the copy taken on the GPU), the same again;
//pspsdk's samples (PSP_TEST_PROGRAMS) run by two machines alike but for the renderer, measured for how close their
//pictures are (they needn't be the same: the GPU interpolates texture coordinates its own way); blending, dithering,
//logic operations and write masks done in the shader (shader blending), byte for byte, and without rasterization
//order the GPU's own blending, close, and what it can't do in the shader, byte for byte; a GPU that stops answering;
//bytes beside the GPU's pixels, memory's without waiting; the start-up check a game's renderer goes through; and
//drawing at 2 and 3 times the PSP's resolution, where memory's bytes must still come back exactly.
//Each group but the lost GPU's is skipped, saying why, where there's no Vulkan GPU (or with PSP_GPU=0): the
//machines the tests run on needn't have one. The skips name what they lack (skip(): "vulkan", or a GPU's capability,
//"vulkan-palettes" and the like), and fail where PHOBOS_TESTS_REQUIRE names it, as CI's lane on lavapipe does.
#include "kernel-machine.hpp"
#include "../../ares/psp/ge/gpu/gpu.hpp"
#include "../../ares/psp/ge/gpu/shaders/shaders.hpp"

#include <random>

namespace allegrex_test::psp {

using ares::PlayStationPortable::GPU;

constexpr u32 GPUVertices = 0x0896'0000, GPUTexture = 0x0898'0000;

//The renderer, made once (none where there's no Vulkan GPU, said once); each test's machine takes it with
//setRenderer(), which has it forget the machine before's VRAM, and gives it back the same way.
static auto renderer() -> GPU* {
  static std::unique_ptr<GPU> gpu;
  static bool tried = false;
  static std::string error = "PSP_GPU=0";
  if(!tried) {
    tried = true;
    const char* wanted = std::getenv("PSP_GPU");
    if(!wanted || std::string(wanted) != "0") gpu = GPU::vulkan(nullptr, error);
    if(gpu) std::printf("  on %s\n", gpu->backend->name().c_str());
  }
  if(!gpu) skip("vulkan", error);
  return gpu.get();
}

static auto bitsOf(float value) -> u32 { u32 bits; std::memcpy(&bits, &value, 4); return bits; }

//A GPU's statistics, every threaded machine's GE thread done first (they're that thread's while it runs).
static auto stats(GPU* gpu) -> GPU::Statistics& {
  drainThreads();
  return gpu->statistics;
}

//A machine's GE ready to draw 2D sprites (through mode: float u, v; 8888 color; float position) into a frame buffer
//at VRAM's offset buffer, 64 pixels wide, in format; no depth test, so the depth buffer (VRAM's last 64 KiB) stays.
static auto prepare(System& s, u32 buffer, u32 format) -> void {
  auto& c = s.ge.commands;
  c[GE::FrameBufferPointer] = buffer, c[GE::FrameBufferWidth] = 64, c[GE::FrameBufferPixelFormat] = format;
  c[GE::DepthBufferPointer] = 0x1f'0000, c[GE::DepthBufferWidth] = 64;
  c[GE::Region2] = 1023 << 10 | 1023;
  c[GE::Scissor2] = 63 | 47 << 10;
  c[GE::VertexType] = 0x80'019f;
  c[GE::ShadeMode] = 1;
}

//A sprite: its corners (u, v, x, y each), its color; its depth.
struct Corners { float at[2][4]; u32 color; };
static auto sprite(System& s, const Corners& sprite, float z = 0) -> void {
  u32 to = GPUVertices;
  for(auto& corner : sprite.at) {
    for(float value : {corner[0], corner[1]}) s.memory.write(4, to, bitsOf(value)), to += 4;
    s.memory.write(4, to, sprite.color), to += 4;
    for(float value : {corner[2], corner[3], z}) s.memory.write(4, to, bitsOf(value)), to += 4;
  }
  s.ge.vertexAddress = GPUVertices;
  s.ge.primitive(GE::Sprites, 2);
}

//A texture for the sprites: 8888 at address (RAM's or VRAM's), width and height texels, rows of bufferWidth,
//nearest, replace with its alpha.
static auto texture(System& s, u32 address, u32 width, u32 height, u32 bufferWidth, u32 format = 3) -> void {
  auto& c = s.ge.commands;
  c[GE::TextureMappingEnable] = 1;
  c[GE::TextureAddress0] = address & 0xff'ffff;
  c[GE::TextureBufferWidth0] = (address >> 24 & 0xf) << 16 | bufferWidth;
  c[GE::TextureSize0] = std::countr_zero(height) << 8 | std::countr_zero(width);
  c[GE::TextureFormat] = format;
  c[GE::TextureFunction] = 3 | 1 << 8;
  c[GE::TextureFilter] = 0;
}

//Random sprites over the frame buffer: flat ones, and textured ones whose texels are 1:1, enlarged, shrunk and
//mirrored (the steps the GE takes across a sprite's pixels, which the GPU must take the same), some in clear mode.
static auto randomSprites(std::mt19937& random, u32 count, bool textured) -> std::vector<Corners> {
  std::vector<Corners> sprites;
  for(u32 n = 0; n < count; n++) {
    Corners s{};
    float x = random() % 56, y = random() % 40, w = 1 + random() % 40, h = 1 + random() % 30;
    float u = random() % 16, v = random() % 16;
    float scale[] = {1, 1, 2, 0.5f, 0.75f, 1.5f, 3};
    float us = w * scale[random() % 7], vs = h * scale[random() % 7];
    if(random() % 4 == 0) us = -us;
    s.at[0][0] = u, s.at[0][1] = v, s.at[0][2] = x + (random() % 16) / 16.0f, s.at[0][3] = y;
    s.at[1][0] = u + us, s.at[1][1] = v + vs, s.at[1][2] = x + w, s.at[1][3] = y + h;
    s.color = u32(random());
    if(!textured) s.at[0][0] = s.at[0][1] = s.at[1][0] = s.at[1][1] = 0;
    sprites.push_back(s);
  }
  return sprites;
}

//VRAM's bytes the two machines don't agree on, the frame buffers' first 0x40000 bytes.
static auto apart(System& a, System& b) -> u32 {
  a.ge.settleAll(), b.ge.settleAll();
  u32 bytes = 0;
  for(u32 n = 0; n < 0x4'0000; n++) {
    if(a.memory.vram[n] == b.memory.vram[n]) continue;
    if(bytes++ < 4) std::printf("  VRAM %05x: %02x, the GPU's %02x\n", n, a.memory.vram[n], b.memory.vram[n]);
  }
  return bytes;
}

//The ways the backend sets a target's stencil from its pixels' alphas, above 1x and in moves, to try: a draw for each
//of its bits, and, where a shader can give it (Backend::stencilExport), with the colors.
static auto stencilWays(GPU* gpu) -> std::vector<bool> {
  if(gpu->backend->stencilExport) return {false, true};
  skip("vulkan-stencil-export", "the GPU can't give the stencil from the shader: only the other way tested");
  return {false};
}

//Whether the GPU takes a resolution, saying so as a skip of it where it doesn't.
static auto scaleTaken(GPU* gpu, u32 scale) -> bool {
  if(scale <= gpu->backend->mostScale) return true;
  auto most = std::to_string(gpu->backend->mostScale);
  skip("vulkan-" + std::to_string(scale) + "x", "the GPU takes at most " + most + "x");
  return false;
}

//Sprites without blending or dithering, in each format: the GPU's pixels are the software renderer's, byte for
//byte (the colors narrowed as the PSP narrows them, a texture's coordinates stepped as the GE steps them, the
//stencil the alpha).
static auto gpuSprites() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261007};
  for(u32 format : {3u, 0u, 1u, 2u}) {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, format);
      for(u32 n = 0; n < 32 * 32; n++) s->memory.write(4, GPUTexture + n * 4, u32(n * 0x9e37'79b9));
    }
    auto flat = randomSprites(random, 60, false), textured = randomSprites(random, 60, true);
    for(System* s : {&software, &hardware}) {
      for(auto& one : flat) sprite(*s, one);
      texture(*s, GPUTexture, 32, 32, 32);
      for(auto& one : textured) sprite(*s, one);
      s->ge.commands[GE::ClearMode] = 1 | 7 << 8;  //(clear mode: color, stencil and depth written as they come)
      s->ge.commands[GE::TextureMappingEnable] = 0;
      for(u32 n = 0; n < 4; n++) sprite(*s, flat[n]);
      s->ge.commands[GE::ClearMode] = 0;
    }
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  format %u: %u bytes apart\n", format, bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
  }
}

//A frame buffer drawn, then sampled by sprites drawing into another: the texture taken from the GPU's own picture
//(a copy on the GPU, not a read back), and the result the software renderer's.
static auto gpuRenderToTexture() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261008};
  auto before = stats(gpu);
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  for(System* s : {&software, &hardware}) prepare(*s, 0, 3);
  auto drawn = randomSprites(random, 40, false), sampled = randomSprites(random, 40, true);
  for(System* s : {&software, &hardware}) {
    for(auto& one : drawn) sprite(*s, one);
    prepare(*s, 0x2'0000, 3);
    texture(*s, Memory::VRAMBase, 64, 64, 64);
    for(auto& one : sampled) sprite(*s, one);
  }
  CHECK(hardware.ge.drawer().memory.vramBusy, true);  //(nothing read back yet: the drawing GE's memory's pages)
  u32 bytes = apart(software, hardware);
  CHECK(bytes, 0u);
  CHECK(stats(gpu).copies > before.copies, true);
  hardware.ge.setRenderer(nullptr);
}

//Anti-aliased lines (raster.cpp's linePixels()) over random sprites, without blending, so the coverage shows only
//through the alpha test: each line's above 0, 31, 63 or 95 in turn, which keeps a pixel or drops it by its coverage.
//Flat, then textured from the frame buffer they draw into (Dead or Alive Paradise's edge pass), modulated by their
//colors so that each pixel shows it was drawn; one line a PRIM, so none samples a pixel drawn in its own. The GPU's
//pixels are the software renderer's, byte for byte.
static auto gpuSmoothLines() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261012};
  auto before = stats(gpu).primitives;
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  auto background = randomSprites(random, 30, false);
  struct End { float x, y; u32 color; };
  std::vector<std::pair<End, End>> lines;
  for(u32 n = 0; n < 80; n++) {
    auto end = [&] { return End{(random() % 1024) / 16.0f, (random() % 768) / 16.0f, u32(random()) | 0xff00'0000}; };
    lines.push_back({end(), end()});
  }
  auto line = [](System& s, const End& a, const End& b) {
    u32 to = GPUVertices;
    for(auto& end : {a, b}) {
      for(float value : {0.0f, 0.0f}) s.memory.write(4, to, bitsOf(value)), to += 4;
      s.memory.write(4, to, end.color), to += 4;
      for(float value : {end.x, end.y, 0.0f}) s.memory.write(4, to, bitsOf(value)), to += 4;
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Lines, 2);
  };
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);
    for(auto& one : background) sprite(*s, one);
    auto& c = s->ge.commands;
    c[GE::AntiAliasEnable] = 1;
    c[GE::AlphaTestEnable] = 1;
    for(u32 n = 0; n < 80; n++) {
      if(n == 40) {
        texture(*s, Memory::VRAMBase, 64, 64, 64);
        c[GE::TextureFunction] = 0;  //modulate, the coverage kept for the alpha
      }
      c[GE::AlphaTest] = 6 | (n % 4 * 32 - (n % 4 != 0)) << 8 | 0xff << 16;
      line(*s, lines[n].first, lines[n].second);
    }
  }
  u32 bytes = apart(software, hardware);
  CHECK(bytes, 0u);
  CHECK(stats(gpu).primitives > before + 80, true);  //(the GPU drew the lines, not the GE in its place)
  hardware.ge.setRenderer(nullptr);
}

//pspsdk's samples (PSP_TEST_PROGRAMS) run by two machines alike but for the renderer, frame by frame for a second
//and a half of the PSP's time: how many pixels are the same, and how far apart the rest are (each channel's
//difference: 1-2 is blending's rounding, the GPU's sum against the PSP's terms each truncated), printed. Every
//frame must show the picture: no more than farPerThousand channels in 1000 more than 8 levels apart.
static auto samples(GPU* gpu, u32 farPerThousand) -> void {
  for(const char* name : {"cube", "blend", "clut", "blit", "celshading", "envmap", "doublelist", "gu"}) {
    auto program = testProgram((std::string(name) + ".elf").c_str());
    if(program.empty()) continue;
    KernelMachine software, hardware;
    hardware.system.ge.setRenderer(gpu);
    std::string error;
    for(auto* m : {&software, &hardware}) {
      m->system.recompiler.enabled = true;
      CHECK(m->kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/SAMPLE/EBOOT.PBP", error), true);
    }
    u64 pixels = 0, same = 0, near[3] = {};
    std::vector<u32> picture[2];
    for(u32 frame = 0; frame < 90; frame++) {
      software.kernel.run(Kernel::VblankCycles), hardware.kernel.run(Kernel::VblankCycles);
      software.kernel.picture(picture[0]), hardware.kernel.picture(picture[1]);
      for(u32 n = 0; n < picture[0].size() && n < picture[1].size(); n++) {
        pixels++, same += picture[0][n] == picture[1][n];
        for(u32 c = 0; c < 3; c++) {
          s32 d = std::abs(s32(picture[0][n] >> c * 8 & 0xff) - s32(picture[1][n] >> c * 8 & 0xff));
          if(d) near[d <= 2 ? 0 : d <= 8 ? 1 : 2]++;
        }
      }
    }
    std::printf("  %s: %.2f%% of pixels the same; channels off by 1-2 %llu, 3-8 %llu, more %llu\n", name,
                pixels ? 100.0 * same / pixels : 0.0, (unsigned long long)near[0], (unsigned long long)near[1],
                (unsigned long long)near[2]);
    CHECK(pixels > 0, true);
    CHECK(near[2] * 1000 <= pixels * farPerThousand, true);
    hardware.system.ge.setRenderer(nullptr);
  }
}

static auto gpuSamples() -> void {
  auto gpu = renderer();
  if(!gpu || !testPrograms()) return;
  samples(gpu, 3);
}

//Vulkan (fast) (docs/psp-gpu-renderers.md): a renderer of its own, the GPU transforming 3D vertices itself and
//blending with its own units. It passes the start-up check, whose 3D triangles it transforms; and pspsdk's samples,
//3D among them (cube, celshading's lighting, envmap's environment mapping), come out close to the software
//renderer's (not as close as the accurate mode's: the GPU steps colors and texture coordinates its own way).
static auto fastRenderer() -> GPU* {
  static std::unique_ptr<GPU> gpu;
  std::string error;
  if(!gpu && renderer()) gpu = GPU::vulkan(nullptr, error, true);
  if(!gpu && renderer()) skip("vulkan-fast", error.empty() ? "the fast renderer didn't start" : error);
  return gpu.get();
}

static auto gpuFast() -> void {
  if(!renderer()) return;
  GPU* gpu = fastRenderer();
  CHECK((bool)gpu, true);
  if(!gpu) return;
  CHECK(gpu->fast && !gpu->backend->reads, true);
  if(!gpu->backend->transforms) {
    return skip("vulkan-transforms", "the driver didn't take transform.vert: 3D on the CPU");
  }
  auto before = stats(gpu);
  std::string error;
  bool passed = gpu->check(error);
  if(!passed) std::printf("  %s\n", error.c_str());
  CHECK(passed, true);
  CHECK(stats(gpu).meshes - before.meshes >= 4, true);
  if(!testPrograms()) return;
  before = stats(gpu);
  samples(gpu, 3);
  CHECK(stats(gpu).meshes > before.meshes, true);
}

//The stencil as each 16-bit format keeps it (part 68), beside 8888's, at 1x and 2x. A 5650 frame buffer has none,
//which a stencil test reads as 0: a test failing against it drops every pixel (its depth not written either: a
//sprite behind shows through), one passing draws them all. A 5551 one keeps a bit and a 4444 one four, so clear
//mode's alpha and a REPLACE's reference leave the stencil the GPU tests next as the GE's: 0 or 255, or four bits
//twice over, also through MASK_ALPHA keeping some of its bits, and a 5551 REPLACE whose comparison takes bits the
//format doesn't keep compares as the GE does. Clears and replaces over random rectangles, clear mode's points, two
//lines (one flat) and (at 1x) a triangle, then a full-frame pass testing for each stencil a frame buffer can hold
//and some it can't, each its own color: every byte the software renderer's. In either mode (fast mode's settings
//are the accurate mode's).
static auto stencilFormats(GPU* gpu) -> void {
  struct Pass { bool clear; u32 test, operation, maskAlpha, color; Corners at; };
  std::mt19937 random{20261068};
  auto rectangle = [&](u32 color) {
    float x = random() % 48, y = random() % 32;
    return Corners{{{0, 0, x, y}, {0, 0, x + 8 + random() % 24, y + 8 + random() % 20}}, color};
  };
  //(STENCIL_TEST: comparison, reference, mask; STENCIL_OPERATION: fail, depth fail, pass)
  auto test = [](u32 comparison, u32 reference, u32 mask) { return comparison | reference << 8 | mask << 16; };
  auto replaceOn = [](bool pass) { return pass ? 2u << 16 : 2u; };
  for(u32 scale : {1u, 2u}) {
    if(!scaleTaken(gpu, scale)) break;
    gpu->resolution(scale);
    for(u32 format : {0u, 1u, 2u, 3u}) {
      std::vector<Pass> passes;
      for(u32 alpha : {0x7fu, 0x80u, 0x5au, 0xa5u, 0x01u, 0xfeu, 0x0fu, 0xf0u, 0x33u, 0xc4u}) {
        passes.push_back({true, 0, 0, 0, 0, rectangle(u32(random() & 0xff'ffff) | alpha << 24)});
      }
      for(u32 maskAlpha : {0x7fu, 0x80u, 0x3cu, 0x0fu}) {
        passes.push_back({true, 0, 0, maskAlpha, 0, rectangle(u32(random()))});
      }
      struct Replace { u32 comparison, reference, mask; bool pass; u32 maskAlpha; };
      std::vector<Replace> replaces = {{1, 0x81, 0xff, true, 0}, {1, 0x5a, 0xff, true, 0}, {1, 0x7e, 0x0f, true, 0},
                                       {2, 0xa5, 0xf0, true, 0}, {3, 0x5a, 0xf0, false, 0},
                                       {1, 0xc3, 0xff, true, 0x3c}};
      if(format == 1) {  //(comparisons taking the bits a 5551 one doesn't keep)
        for(Replace r : {Replace{6, 0x81, 0xff, true, 0}, {4, 0x7f, 0xff, false, 0}, {2, 0x01, 0x7f, true, 0},
                         {7, 0x80, 0xc0, false, 0}, {3, 0x7f, 0xff, true, 0x7f}, {5, 0x40, 0x7f, false, 0}}) {
          replaces.push_back(r);
        }
      }
      for(auto& r : replaces) {
        passes.push_back({false, test(r.comparison, r.reference, r.mask), replaceOn(r.pass), r.maskAlpha, 0,
                          rectangle(u32(random()))});
      }
      std::vector<u32> stencils;  //(the full-frame passes: each tests for one stencil)
      for(u32 n = 0; n < 16; n++) stencils.push_back(n * 0x11);
      for(u32 stencil : {0x80u, 0x7fu, 0x01u, 0xfeu, 0x5au, 0xa5u, 0x81u, 0x7eu}) stencils.push_back(stencil);
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      for(System* s : {&software, &hardware}) {
        auto& c = s->ge.commands;
        prepare(*s, 0, format);
        c[GE::ClearMode] = 1 | 3 << 8;
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0x0012'3456});  //(the stencil 0 to start)
        for(auto& pass : passes) {
          c[GE::ClearMode] = pass.clear ? 1 | 3 << 8 : 0;
          c[GE::StencilTestEnable] = !pass.clear, c[GE::StencilTest] = pass.test;
          c[GE::StencilOperation] = pass.operation, c[GE::MaskAlpha] = pass.maskAlpha;
          sprite(*s, pass.at);
        }
        //clear mode's points, a line and a triangle, each pixel's stencil its color's alpha (along the line, blended;
        //the triangle's corners one alpha, as the GPU takes its last corner's for all of it)
        struct Corner { float x, y; u32 color; };
        auto draw = [&](u32 kind, std::initializer_list<Corner> corners) {
          u32 to = GPUVertices;
          for(auto& corner : corners) {
            for(float value : {0.0f, 0.0f}) s->memory.write(4, to, bitsOf(value)), to += 4;
            s->memory.write(4, to, corner.color), to += 4;
            for(float value : {corner.x, corner.y, 0.0f}) s->memory.write(4, to, bitsOf(value)), to += 4;
          }
          s->ge.vertexAddress = GPUVertices;
          s->ge.primitive(kind, corners.size());
        };
        c[GE::ClearMode] = 1 | 3 << 8, c[GE::StencilTestEnable] = 0, c[GE::MaskAlpha] = 0;
        draw(GE::Points, {{2, 44, 0x7f00'00ff}, {4, 44, 0x8000'ff00}, {6, 44, 0x5aff'0000}, {8, 44, 0xa500'ffff}});
        draw(GE::Lines, {{10, 46, 0x0112'3456}, {62, 47, 0xfe65'4321}});
        c[GE::ShadeMode] = 0;  //(flat: the second end's alpha all along)
        draw(GE::Lines, {{10, 40, 0x4012'3456}, {40, 42, 0x9b65'4321}});
        c[GE::ShadeMode] = 1;
        if(scale == 1) {  //(above it a triangle's edges are the GPU's coverage, not the PSP's pixels')
          draw(GE::Triangles, {{40, 2, 0xc4aa'bbcc}, {62, 2, 0xc4aa'bbcc}, {50, 20, 0xc4aa'bbcc}});
        }
        c[GE::ClearMode] = 0, c[GE::MaskAlpha] = 0, c[GE::StencilOperation] = 0, c[GE::StencilTestEnable] = 1;
        for(u32 n = 0; n < stencils.size(); n++) {
          c[GE::StencilTest] = test(2, stencils[n], 0xff);
          sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff00'0000 | (n + 1) * 0x0a'1b2c});
        }
        if(format == 0) {  //(5650: tests against a stencil of 0, each over a strip of its own)
          struct Five { u32 comparison, reference, mask; };
          Five fives[] = {{2, 5, 0xff}, {2, 5, 0xf0}, {3, 5, 0xff}, {4, 0, 0xff}, {7, 9, 0xff}, {6, 1, 0xff},
                          {5, 1, 0xff}, {0, 0, 0xff}, {1, 0, 0xff}, {5, 0, 0xff}};
          for(u32 n = 0; n < 10; n++) {
            c[GE::StencilTest] = test(fives[n].comparison, fives[n].reference, fives[n].mask);
            sprite(*s, {{{0, 0, n * 6.0f, 0}, {0, 0, n * 6.0f + 6, 48}}, 0xff80'4020 + n * 0x0c'0b0a});
          }
          c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written: but for the stencil's test)
          c[GE::StencilTest] = test(2, 5, 0xff);
          sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 24}}, 0xffff'ffff}, 60000);
          c[GE::StencilTestEnable] = 0, c[GE::DepthTest] = 7;  //(greater or equal: drawn where no depth was)
          sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 24}}, 0xff00'ff00}, 30000);
          c[GE::DepthTestEnable] = 0;
        }
        c[GE::StencilTestEnable] = 0;
      }
      u32 bytes = apart(software, hardware);
      const char* mode = gpu->fast ? "fast" : "accurate";
      if(bytes) std::printf("  %s %ux, format %u: %u bytes apart\n", mode, scale, format, bytes);
      CHECK(bytes, 0u);
      hardware.ge.setRenderer(nullptr);
    }
  }
  gpu->resolution(1);
}
static auto gpuStencilFormats() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) stencilFormats(gpu);
  }
}

//Fast mode's triangles kept to the GE's rules for which are drawn (GE::meshTriangles()), against the software
//renderer and beside the accurate mode: random triangles from behind the camera to past the far plane, some off the
//GE's 4096-pixel screen, culled either way or not, in lists and strips, no depth test (so that their order shows);
//and a ground of random colors, fogged, running under the camera, whose triangles the near plane cuts; with
//DEPTH_CLIP_ENABLE on and off. Fast mode must come as close as the accurate mode: no more pixels more than 8 levels
//off but one in a thousand.
static auto gpuFastRules() -> void {
  GPU* accurate = renderer();
  GPU* fast = accurate ? fastRenderer() : nullptr;
  if(!fast) return;
  if(!fast->backend->transforms) return skip("vulkan-transforms", "the driver didn't take transform.vert");
  constexpr u32 Width = 128, Height = 96, Background = 0xff20'2020;
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  struct Corner { float u, v; u32 color; float normal[3], at[3]; };  //(vertex type 0x1ff)
  auto draw = [&](System& s, u32 kind, const std::vector<Corner>& corners) {
    u32 to = GPUVertices;
    for(auto& c : corners) {
      for(u32 word : {bitsOf(c.u), bitsOf(c.v), c.color, bitsOf(c.normal[0]), bitsOf(c.normal[1]),
                      bitsOf(c.normal[2]), bitsOf(c.at[0]), bitsOf(c.at[1]), bitsOf(c.at[2])}) {
        s.memory.write(4, to, word), to += 4;
      }
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(kind, corners.size());
  };
  auto scene = [&](System& s, u32 clip) {
    auto& c = s.ge.commands;
    c[GE::FrameBufferPointer] = 0, c[GE::FrameBufferWidth] = Width, c[GE::FrameBufferPixelFormat] = 3;
    c[GE::DepthBufferPointer] = 0x10'0000, c[GE::DepthBufferWidth] = Width;
    c[GE::Region2] = 1023 << 10 | 1023, c[GE::Scissor2] = (Width - 1) | (Height - 1) << 10;
    c[GE::VertexType] = 0x1ff, c[GE::ShadeMode] = 1, c[GE::DepthClipEnable] = clip, c[GE::MaxZ] = 0xffff;
    for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
    for(u32 n = 0; n < 16; n++) s.ge.projection[n] = 0;
    s.ge.projection[0] = s.ge.projection[5] = f24(1);  //(the near plane 1 away, the far one 10)
    s.ge.projection[10] = f24(-11.0f / 9), s.ge.projection[11] = f24(-1), s.ge.projection[14] = f24(-20.0f / 9);
    c[GE::ViewportXScale] = f24(64), c[GE::ViewportYScale] = f24(-48), c[GE::ViewportZScale] = f24(30000);
    c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
    c[GE::OffsetX] = (2048 - 64) << 4, c[GE::OffsetY] = (2048 - 48) << 4;
    for(u32 n = 0; n < Width * Height; n++) std::memcpy(&s.memory.vram[n * 4], &Background, 4);
    std::mt19937 random{20261009};
    for(u32 prim = 0; prim < 6; prim++) {
      c[GE::CullFaceEnable] = prim % 3 != 0, c[GE::Cull] = prim % 3 == 2;
      std::vector<Corner> corners;
      for(u32 n = 0; n < 60; n++) {
        float z = 1.0f - (random() % 1000) / 1000.0f * 16.0f, distance = std::max(std::abs(z), 0.3f);
        float spread = random() % 10 == 0 ? 60.0f : 1.4f;
        float x = (s32(random() % 2000) - 1000) / 1000.0f * spread * distance;
        float y = (s32(random() % 2000) - 1000) / 1000.0f * distance;
        corners.push_back({0, 0, u32(random()) | 0xff00'0000, {0, 0, 1}, {x, y, z}});
      }
      draw(s, prim & 1 ? GE::TriangleStrip : GE::Triangles, corners);
    }
    c[GE::CullFaceEnable] = 0;
    c[GE::FogEnable] = 1, c[GE::FogEnd] = f24(30.0f), c[GE::FogSlope] = f24(1.0f / 28), c[GE::FogColor] = 0xff'0000;
    std::vector<std::vector<u32>> colors(7, std::vector<u32>(5));
    for(auto& row : colors) for(auto& color : row) color = u32(random()) | 0xff00'0000;
    auto at = [&](u32 i, u32 j) -> Corner {
      return {0, 0, colors[i][j], {0, 1, 0}, {-6 + 3.0f * j, -0.8f, 1.0f - 1.5f * i}};
    };
    std::vector<Corner> ground;
    for(u32 i = 0; i < 6; i++) {
      for(u32 j = 0; j < 4; j++) {
        for(auto& corner : {at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j), at(i + 1, j + 1), at(i, j + 1)}) {
          ground.push_back(corner);
        }
      }
    }
    draw(s, GE::Triangles, ground);
    c[GE::FogEnable] = 0;
    s.ge.settleAll();
  };
  for(u32 clip : {1u, 0u}) {
    u32 far[2] = {};
    for(u32 mode : {0u, 1u}) {
      GPU* gpu = mode ? fast : accurate;
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      auto before = stats(gpu);
      scene(software, clip), scene(hardware, clip);
      if(mode) CHECK(stats(gpu).meshes > before.meshes, true);
      hardware.ge.setRenderer(nullptr);
      for(u32 n = 0; n < Width * Height; n++) {
        for(u32 channel = 0; channel < 3; channel++) {
          s32 d = s32(software.memory.vram[n * 4 + channel]) - s32(hardware.memory.vram[n * 4 + channel]);
          if(d > 8 || d < -8) { far[mode]++; break; }
        }
      }
    }
    std::printf("  DEPTH_CLIP_ENABLE %u: pixels more than 8 levels off, accurate %u, fast %u\n", clip, far[0], far[1]);
    CHECK(far[1] <= far[0] + Width * Height / 1000, true);
  }
}

//Fast mode's PRIMs of fewer than MeshLeast vertices (the GE transforms them) between its meshes (the GPU transforms
//them), in one setting after another (smooth and flat shading, a texture filtered and not, fog, lighting with its
//shine kept apart), each setting's first mesh with a triangle cut at the near plane (which the GE cuts and draws among
//its runs), into a frame buffer 128 pixels wide and one 480 wide (a target whose scale to the GPU's -1 to 1 isn't a
//power of two), at 1x and 2x: the GE's triangles go among the meshes (GPU::screenTriangle()), so each setting's PRIMs
//are one draw, and every pixel is as when they're drawn apart (draw.vert's own draws).
static auto gpuScreenMeshes() -> void {
  GPU* gpu = renderer() ? fastRenderer() : nullptr;
  if(!gpu) return;
  if(!gpu->backend->transforms) return skip("vulkan-transforms", "the driver didn't take transform.vert");
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  struct Corner { float u, v; u32 color; float normal[3], at[3]; };  //(vertex type 0x1ff)
  auto draw = [&](System& s, u32 kind, const std::vector<Corner>& corners) {
    u32 to = GPUVertices;
    for(auto& c : corners) {
      for(u32 word : {bitsOf(c.u), bitsOf(c.v), c.color, bitsOf(c.normal[0]), bitsOf(c.normal[1]),
                      bitsOf(c.normal[2]), bitsOf(c.at[0]), bitsOf(c.at[1]), bitsOf(c.at[2])}) {
        s.memory.write(4, to, word), to += 4;
      }
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(kind, corners.size());
  };
  constexpr u32 Settings = 5;
  //(each setting: 4 PRIMs of 30 corners, a small one of 6 after each; the texture 16 by 16 random texels in RAM)
  auto scene = [&](System& s, u32 width, u32 height, std::vector<u64>& draws) {
    auto& c = s.ge.commands;
    c[GE::FrameBufferPointer] = 0, c[GE::FrameBufferWidth] = width, c[GE::FrameBufferPixelFormat] = 3;
    c[GE::DepthBufferPointer] = 0x10'0000, c[GE::DepthBufferWidth] = width;
    c[GE::Region2] = 1023 << 10 | 1023, c[GE::Scissor2] = (width - 1) | (height - 1) << 10;
    c[GE::VertexType] = 0x1ff, c[GE::MaxZ] = 0xffff, c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 5;
    for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
    for(u32 n = 0; n < 16; n++) s.ge.projection[n] = 0;
    s.ge.projection[0] = s.ge.projection[5] = f24(1);  //(the near plane 1 away, the far one 10)
    s.ge.projection[10] = f24(-11.0f / 9), s.ge.projection[11] = f24(-1), s.ge.projection[14] = f24(-20.0f / 9);
    c[GE::ViewportXScale] = f24(width / 2.0f), c[GE::ViewportYScale] = f24(height / -2.0f);
    c[GE::ViewportZScale] = f24(30000);
    c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
    c[GE::OffsetX] = (2048 - width / 2) << 4, c[GE::OffsetY] = (2048 - height / 2) << 4;
    c[GE::TextureScaleU] = c[GE::TextureScaleV] = f24(1);
    std::mt19937 random{20261014};
    for(u32 n = 0; n < 16 * 16; n++) s.memory.write(4, GPUTexture + n * 4, u32(random()) | 0xff00'0000);
    for(u32 n = 0; n < width * height * 4; n += 4) s.memory.write(4, 0x0400'0000 + n, 0xff20'2020);
    for(u32 n = 0; n < width * height * 2; n += 4) s.memory.write(4, 0x0410'0000 + n, 0xffff'ffff);
    for(u32 setting = 0; setting < Settings; setting++) {
      c[GE::ShadeMode] = setting != 1;
      c[GE::TextureMappingEnable] = 0, c[GE::FogEnable] = 0, c[GE::LightingEnable] = 0;
      if(setting == 2 || setting == 3) {
        texture(s, GPUTexture, 16, 16, 16);
        c[GE::TextureFilter] = setting == 3 ? 1 | 1 << 8 : 0;
        c[GE::TextureFunction] = 0;
      }
      if(setting == 3) {
        c[GE::FogEnable] = 1, c[GE::FogEnd] = f24(12.0f), c[GE::FogSlope] = f24(1.0f / 11);
        c[GE::FogColor] = 0x30'60ff;
      }
      if(setting == 4) {  //(a light whose shine is kept apart, the vertices' colors the diffuse)
        c[GE::LightingEnable] = 1, c[GE::LightMode] = 1, c[GE::LightEnable0] = 1, c[GE::LightType0] = 1;
        c[GE::Light0X] = f24(0.3f), c[GE::Light0X + 1] = f24(0.2f), c[GE::Light0X + 2] = f24(1);
        c[GE::Light0Ambient] = 0x10'1010, c[GE::Light0Ambient + 1] = 0xff'ffff, c[GE::Light0Ambient + 2] = 0xff'ffff;
        c[GE::MaterialColor] = 2, c[GE::MaterialSpecular] = 0xff'ffff, c[GE::MaterialSpecularCoefficient] = f24(2);
        c[GE::AmbientLightColor] = 0x30'3030, c[GE::AmbientLightAlpha] = 0xff;
      }
      u64 before = s.ge.drawer().renderer ? stats(gpu).draws : 0;  //(the drawing GE's: ge/thread.hpp)
      for(u32 prim = 0; prim < 8; prim++) {
        std::vector<Corner> corners;
        for(u32 n = 0; n < (prim & 1 ? 6u : 30u); n++) {
          float z = -1.5f - (random() % 1000) / 1000.0f * 7.0f;
          float x = (s32(random() % 2000) - 1000) / 1000.0f * -z, y = (s32(random() % 2000) - 1000) / 1000.0f * -z;
          float facing[3] = {(s32(random() % 200) - 100) / 400.0f, (s32(random() % 200) - 100) / 400.0f, 1};
          corners.push_back({(random() % 64) / 4.0f, (random() % 64) / 4.0f, u32(random()) | 0xff00'0000,
                             {facing[0], facing[1], facing[2]}, {x, y, z}});
        }
        if(prim == 0) corners[2].at[2] = -0.5f;  //(nearer than the near plane: its triangle cut)
        draw(s, GE::Triangles, corners);
      }
      if(s.ge.drawer().renderer) draws.push_back(stats(gpu).draws - before);
    }
    c[GE::LightingEnable] = 0, c[GE::LightMode] = 0, c[GE::LightType0] = 0;
  };
  for(u32 scale : {1u, 2u}) {
    if(!scaleTaken(gpu, scale)) break;
    for(auto [width, height] : {std::pair{128u, 96u}, std::pair{480u, 272u}}) {
      std::vector<u32> pictures[2];
      std::vector<u64> draws[2];
      u64 screened = 0;
      for(bool together : {true, false}) {
        gpu->resolution(scale);
        gpu->screenMeshes = together;
        System hardware;
        hardware.ge.setRenderer(gpu);
        u64 before = stats(gpu).screened;
        scene(hardware, width, height, draws[together]);
        if(together) screened = stats(gpu).screened - before;
        CHECK(gpu->picture(0, width, 3, width, height, pictures[together], scale), true);
        hardware.ge.setRenderer(nullptr);
      }
      gpu->screenMeshes = true;
      u32 apart = 0;
      for(u32 n = 0; n < pictures[0].size() && n < pictures[1].size(); n++) apart += pictures[0][n] != pictures[1][n];
      if(apart) std::printf("  %ux, %u wide: %u pixels differ, among the meshes and apart\n", scale, width, apart);
      CHECK(pictures[0].size() == pictures[1].size() && pictures[0].size() == u64(width) * height * scale * scale,
            true);
      CHECK(apart, 0u);
      CHECK(screened >= Settings * 4 * 2, true);  //(the small PRIMs' two triangles each, the cut ones' pieces too)
      CHECK(draws[1].size(), u64(Settings));
      for(u32 setting = 0; setting < draws[1].size(); setting++) {
        CHECK(draws[1][setting], 1u);
        CHECK(draws[0][setting] >= 8, true);
      }
    }
  }
  gpu->resolution(1);
}

//Fast mode's 3D (mesh()) changes a target as any draw does, for render to texture taken again: a frame buffer
//sampled into another, a wall of one color drawn over part of it with the GPU transforming it, then sampled again from
//the same place. The second picture has the wall in it as the software renderer has it, all but a few pixels of its
//edges (which fast mode may cover where the GE doesn't).
static auto gpuFastMeshChanges() -> void {
  GPU* gpu = renderer() ? fastRenderer() : nullptr;
  if(!gpu) return;
  if(!gpu->backend->transforms) return skip("vulkan-transforms", "the driver didn't take transform.vert");
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  std::mt19937 random{20261015};
  auto drawn = randomSprites(random, 30, false);
  auto sample = [](System& s) {
    prepare(s, 0x2'0000, 3);
    texture(s, Memory::VRAMBase, 64, 64, 64);
    sprite(s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
  };
  auto wall = [&](System& s) {  //(2 away, 2 by 2: x 16-48 and y 12-36 of the frame buffer at 0)
    prepare(s, 0, 3);
    auto& c = s.ge.commands;
    c[GE::TextureMappingEnable] = 0, c[GE::VertexType] = 0x1ff, c[GE::MaxZ] = 0xffff;
    for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
    for(u32 n = 0; n < 16; n++) s.ge.projection[n] = 0;
    s.ge.projection[0] = s.ge.projection[5] = f24(1);  //(the near plane 1 away, the far one 10)
    s.ge.projection[10] = f24(-11.0f / 9), s.ge.projection[11] = f24(-1), s.ge.projection[14] = f24(-20.0f / 9);
    c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-24), c[GE::ViewportZScale] = f24(30000);
    c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
    c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 24) << 4;
    u32 to = GPUVertices, count = 0;
    for(u32 j = 0; j < 2; j++) {
      for(u32 i = 0; i < 4; i++) {
        for(u32 corner : {0u, 1u, 3u, 0u, 3u, 2u}) {  //(4 by 2 quads, 2 triangles each)
          float x = -1 + 0.5f * (i + (corner & 1)), y = -1 + 1.0f * (j + (corner >> 1));
          for(u32 word : {0u, 0u, 0xff30'c060u, 0u, 0u, bitsOf(1), bitsOf(x), bitsOf(y), bitsOf(-2)}) {
            s.memory.write(4, to, word), to += 4;
          }
          count++;
        }
      }
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Triangles, count);
  };
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  auto before = stats(gpu);
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);
    for(auto& one : drawn) sprite(*s, one);
    sample(*s);
    wall(*s);
    sample(*s);
    s->ge.settleAll();
  }
  CHECK(stats(gpu).meshes > before.meshes, true);
  u32 far = 0;
  for(u32 n = 0; n < 64 * 48; n++) {
    for(u32 channel = 0; channel < 4; channel++) {
      u32 at = 0x2'0000 + n * 4 + channel;
      s32 d = s32(software.memory.vram[at]) - s32(hardware.memory.vram[at]);
      if(d > 8 || d < -8) { far++; break; }
    }
  }
  if(far >= 64) std::printf("  %u pixels of the second picture more than 8 levels off\n", far);
  CHECK(far < 64, true);
  hardware.ge.setRenderer(nullptr);
}

//A GPU that stops answering (as the driver's VK_ERROR_DEVICE_LOST, or a wait that never ends, has it) at its first
//finish: what it drew since is gone, memory's VRAM keeps what was there, the loss is said once, and the software
//renderer draws everything after. A pretend backend: no GPU needed.
static auto gpuLost() -> void {
  struct Lost : GPU::Backend {
    u32 runs = 0, made = 0;
    auto name() const -> std::string override { return "pretend"; }
    auto makeTarget(u32, u32) -> u32 override { return ++made; }
    auto dropTarget(u32) -> void override {}
    auto makeTexture(u32, u32, const u32*, std::shared_ptr<const void>) -> u32 override { return ++made; }
    auto dropTexture(u32) -> void override {}
    auto submit(const GPU::Recorded&) -> bool override { return runs++, true; }
    auto finish(const GPU::Recorded&) -> bool override { return runs++, lost = true, false; }  //(as Vulkan's)
    auto read(u32, const u32*&, const u8*&) -> bool override { return false; }
  };
  auto backend = std::make_unique<Lost>();
  auto& pretend = *backend;
  GPU gpu(std::move(backend));
  u32 reports = 0;
  gpu.report = [&](const std::string&) { reports++; };
  System software, hardware;
  hardware.ge.setRenderer(&gpu);
  std::mt19937 random{20261009};
  auto first = randomSprites(random, 8, false), after = randomSprites(random, 8, false);
  for(System* s : {&software, &hardware}) prepare(*s, 0, 3);
  for(auto& one : first) sprite(hardware, one);
  CHECK(hardware.ge.drawer().memory.vramBusy, true);
  hardware.ge.settleAll();
  CHECK(gpu.ready(), false);
  CHECK(hardware.ge.drawer().memory.vramBusy, false);
  CHECK(apart(software, hardware), 0u);  //(the first sprites, never put in memory, gone)
  u32 runs = pretend.runs;
  for(System* s : {&software, &hardware}) for(auto& one : after) sprite(*s, one);
  CHECK(apart(software, hardware), 0u);
  CHECK(pretend.runs, runs);
  CHECK(reports, 1u);
  hardware.ge.setRenderer(nullptr);
}

//A texture the backend couldn't make (its memory for pictures exhausted, as at a high resolution): its PRIM draws
//untextured, and the next PRIM, with the same settings and the texture the one before it had, isn't taken for that
//one (GPU::settings()' kept state): it draws with its texture. A pretend backend, no GPU needed, whose second
//texture fails; each draw's texture as it's run.
static auto gpuKeptAfterFailedTexture() -> void {
  struct Failing : GPU::Backend {
    u32 made = 0, textures = 0;
    std::vector<u32> drawn;
    auto name() const -> std::string override { return "pretend"; }
    auto makeTarget(u32, u32) -> u32 override { return ++made; }
    auto dropTarget(u32) -> void override {}
    auto makeTexture(u32, u32, const u32*, std::shared_ptr<const void>) -> u32 override {
      return ++textures == 2 ? 0 : ++made;
    }
    auto dropTexture(u32) -> void override {}
    auto run(const GPU::Recorded& r) -> bool {
      for(auto& c : r.commands) if(c.kind == GPU::Command::Kind::Draw) drawn.push_back(r.states[c.state].texture);
      return true;
    }
    auto submit(const GPU::Recorded& r) -> bool override { return run(r); }
    auto finish(const GPU::Recorded& r) -> bool override { return run(r); }
    auto read(u32, const u32*&, const u8*&) -> bool override { return false; }
  };
  auto backend = std::make_unique<Failing>();
  auto& pretend = *backend;
  GPU gpu(std::move(backend));
  System hardware;
  hardware.ge.setRenderer(&gpu);
  prepare(hardware, 0, 3);
  std::mt19937 random{20261014};
  for(u32 n = 0; n < 2 * 16 * 16; n++) hardware.memory.write(4, GPUTexture + n * 4, u32(random()) | 0xff00'0000);
  Corners corners{{{0, 0, 4, 4}, {16, 16, 20, 20}}, 0xffff'ffff};
  texture(hardware, GPUTexture, 16, 16, 16);
  sprite(hardware, corners);  //(its texture made)
  texture(hardware, GPUTexture + 0x400, 16, 16, 16);
  sprite(hardware, corners);  //(its texture not: untextured)
  texture(hardware, GPUTexture, 16, 16, 16);
  sprite(hardware, corners);  //(the first's texture again)
  hardware.ge.settleAll();
  CHECK(gpu.ready(), true);
  CHECK(pretend.textures, 2u);
  CHECK(pretend.drawn.size(), u64(3));
  if(pretend.drawn.size() == 3) {
    CHECK(pretend.drawn[0] != 0, true);
    CHECK(pretend.drawn[1], 0u);
    CHECK(pretend.drawn[2], pretend.drawn[0]);
  }
  hardware.ge.setRenderer(nullptr);
}

//A pretend backend that draws nothing and keeps each draw's kind and state as it's run (the two tests below).
struct RecordingBackend : GPU::Backend {
  u32 made = 0;
  std::vector<std::pair<GPU::Command::Kind, GPU::State>> drawn;
  auto name() const -> std::string override { return "pretend"; }
  auto makeTarget(u32, u32) -> u32 override { return ++made; }
  auto dropTarget(u32) -> void override {}
  auto makeTexture(u32, u32, const u32*, std::shared_ptr<const void>) -> u32 override { return ++made; }
  auto dropTexture(u32) -> void override {}
  auto run(const GPU::Recorded& r) -> bool {
    for(auto& c : r.commands) {
      if(c.kind == GPU::Command::Kind::Draw || c.kind == GPU::Command::Kind::Mesh) {
        drawn.push_back({c.kind, r.states[c.state]});
      }
    }
    return true;
  }
  auto submit(const GPU::Recorded& r) -> bool override { return run(r); }
  auto finish(const GPU::Recorded& r) -> bool override { return run(r); }
  auto read(u32, const u32*&, const u8*&) -> bool override { return false; }
};

//3D quads two units square side by side six units before the camera (vertex type 0x1ff, each quad's u and v 0 to 1),
//one PRIM of triangles into prepare()'s frame buffer: fast mode's mesh at 3 quads or more, else the GE's own.
static auto quads3D(System& s, u32 quads) -> void {
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  auto& c = s.ge.commands;
  c[GE::VertexType] = 0x1ff, c[GE::MaxZ] = 0xffff;
  for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
  for(u32 n = 0; n < 16; n++) s.ge.projection[n] = 0;
  s.ge.projection[0] = s.ge.projection[5] = f24(1);  //(the near plane 1 away, the far one 10)
  s.ge.projection[10] = f24(-11.0f / 9), s.ge.projection[11] = f24(-1), s.ge.projection[14] = f24(-20.0f / 9);
  c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-24), c[GE::ViewportZScale] = f24(30000);
  c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
  c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 24) << 4;
  c[GE::TextureScaleU] = c[GE::TextureScaleV] = f24(1);
  u32 to = GPUVertices;
  for(u32 quad = 0; quad < quads; quad++) {
    for(u32 corner : {0u, 1u, 3u, 0u, 3u, 2u}) {
      float u = corner & 1, v = corner >> 1, x = 2.0f * quad - quads + 2 * u, y = 2 * v - 1;
      for(u32 word : {bitsOf(u), bitsOf(v), 0xff40'8020u, 0u, 0u, bitsOf(1), bitsOf(x), bitsOf(y), bitsOf(-6)}) {
        s.memory.write(4, to, word), to += 4;
      }
    }
  }
  s.ge.vertexAddress = GPUVertices;
  s.ge.primitive(GE::Triangles, quads * 6);
}

//A PRIM's settings kept (GPU::kept()) only while every input of them is the last one's: two PRIMs the same but for
//one input, each input in turn, and the second must have settings of its own (a draw of its own with another state,
//where taken for the first's it joins the first's draw). Each field of KeptSettings that can change alone; the
//pixel pipeline's 16-byte blocks each (bytes 0-15, the clear flags, the frame buffer and its format, change the target
//or make the settings unkept as well); the texture as the decoded copy it is; and the backend's ways of drawing, which
//a pretend backend can change between PRIMs. (The target, its row width, rows and the frame buffer's place in it go
//with the frame buffer's address; MASK_ALPHA with the write mask in the pixel pipeline; the texture's width, below
//512, with the decoded copy, and above, the GPU takes 512.) No GPU needed.
static auto gpuKeptInputs() -> void {
  using Change = std::function<void(System&, RecordingBackend&)>;
  struct Case { const char* input; u32 format; bool fast; Change both, change; };
  Change none = [](System&, RecordingBackend&) {};
  Change textured = [](System& s, RecordingBackend&) { texture(s, GPUTexture, 16, 16, 16); };
  auto set = [](u32 command, u32 value) -> Change {
    return [=](System& s, RecordingBackend&) { s.ge.commands[command] = value; };
  };
  auto stencils = [](System& s, RecordingBackend&) {
    s.ge.commands[GE::StencilTestEnable] = 1, s.ge.commands[GE::StencilTest] = 1;  //(always)
  };
  auto logic = [](System& s, RecordingBackend&) {
    s.ge.commands[GE::LogicOpEnable] = 1, s.ge.commands[GE::LogicOp] = 6;  //(XOR)
  };
  auto blended = [](System& s, RecordingBackend&) {
    s.ge.commands[GE::AlphaBlendEnable] = 1, s.ge.commands[GE::BlendMode] = 2 | 3 << 4;  //(by the source's alpha)
  };
  std::vector<Case> cases = {
    {"the pixel pipeline's bytes 16-31: the alpha test", 3, false, none, set(GE::AlphaTestEnable, 1)},
    {"bytes 32-47: the alpha test's reference", 3, false, none, set(GE::AlphaTest, 0x80 << 8)},
    {"bytes 48-63: the color test's reference", 3, false, none, set(GE::ColorReference, 0x12'3456)},
    {"bytes 64-79: the stencil's pass operation", 3, false, stencils, set(GE::StencilOperation, 2 << 16)},
    {"bytes 80-95: the depth test's function", 3, false, none, set(GE::DepthTest, 4)},
    {"bytes 96-111: BLEND_FIXED_A", 3, false, none, set(GE::BlendFixedA, 0x10'2030)},
    {"bytes 112-127: the dither matrix's third", 3, false, none, set(GE::Dither0, 3 << 8)},
    {"bytes 128-143: its seventh", 3, false, none, set(GE::Dither0 + 1, 3 << 8)},
    {"bytes 144-159: its eleventh", 3, false, none, set(GE::Dither0 + 2, 3 << 8)},
    {"bytes 160-175: its fifteenth", 3, false, none, set(GE::Dither0 + 3, 3 << 8)},
    {"bytes 176-191: the scissor", 3, false, none, set(GE::Scissor1, 1)},
    {"bytes 192-207: the depth range", 3, false, none, set(GE::MinZ, 0x100)},
    //(red's low bits, which a 5650 frame buffer's write mask doesn't keep)
    {"MASK_COLOR", 0, false, set(GE::MaskColor, 0xf8), set(GE::MaskColor, 0xff)},
    {"the texture function", 3, false, textured, set(GE::TextureFunction, 0 | 1 << 8)},
    {"the texture's alpha", 3, false, textured, set(GE::TextureFunction, 3)},
    {"the colors doubled", 3, false, textured, set(GE::TextureFunction, 3 | 1 << 8 | 1 << 16)},
    {"the environment color", 3, false, textured, set(GE::TextureEnvironmentColor, 0x20'4060)},
    {"clamped across", 3, false, textured, set(GE::TextureWrap, 1)},
    {"clamped down", 3, false, textured, set(GE::TextureWrap, 1 << 8)},
    {"the texture's height, the decoded copy the same", 3, false, textured, set(GE::TextureSize0, 3 << 8 | 4)},
    {"the texture", 3, false, textured,
      [](System& s, RecordingBackend&) { texture(s, GPUTexture + 0x400, 16, 16, 16); }},
    {"textured", 3, false, textured, set(GE::TextureMappingEnable, 0)},
    {"the backend reading the frame buffer", 3, false, logic, [](System&, RecordingBackend& b) { b.reads = true; }},
    {"the backend reading every blend", 3, false,
      [=](System& s, RecordingBackend& b) { blended(s, b), b.reads = true; },
      [](System&, RecordingBackend& b) { b.readsBlending = true; }},
    {"the backend's dual-source blending", 3, false, blended,
      [](System&, RecordingBackend& b) { b.dualSource = true; }},
    {"the backend's logic operations", 3, false, logic, [](System&, RecordingBackend& b) { b.logicOps = true; }},
    {"the shading, fast mode's", 3, true, none, set(GE::ShadeMode, 0)},
    {"the filter, fast mode's", 3, true, textured, set(GE::TextureFilter, 1 | 1 << 8)},
  };
  for(auto& one : cases) {
    auto backend = std::make_unique<RecordingBackend>();
    auto& pretend = *backend;
    pretend.transforms = one.fast;
    GPU gpu(std::move(backend));
    gpu.fast = one.fast;
    System s;
    s.ge.setRenderer(&gpu);
    prepare(s, 0, one.format);
    for(u32 n = 0; n < 2 * 16 * 16; n++) s.memory.write(4, GPUTexture + n * 4, 0xff00'0000 | n * 0x01'0203);
    one.both(s, pretend);
    auto draw = [&] { one.fast ? quads3D(s, 1) : sprite(s, {{{0, 0, 4, 4}, {16, 16, 20, 20}}, 0x8040'2010}); };
    draw();
    one.change(s, pretend);
    draw();
    s.ge.settleAll();
    bool own = pretend.drawn.size() == 2 && !(pretend.drawn[0].second == pretend.drawn[1].second);
    if(!own) std::printf("  %s: %u draws\n", one.input, u32(pretend.drawn.size()));
    CHECK(own, true);
    s.ge.setRenderer(nullptr);
  }
}

//Fast mode's 3D whose pipeline the driver won't make (Backend::drawable(), its answer kept while a PRIM's settings
//are: GPU::drawableKnown): a PRIM of three quads drawn as a mesh, then the same blended, whose pipeline a pretend
//backend refuses. The GE transforms that one itself and its triangles are drawn apart, not as a mesh. No GPU needed.
static auto gpuMeshRefused() -> void {
  struct Refusing : RecordingBackend {
    auto drawable(const GPU::Pipeline& k) -> bool override { return !k.blend; }
  };
  auto backend = std::make_unique<Refusing>();
  auto& pretend = *backend;
  pretend.transforms = true;
  GPU gpu(std::move(backend));
  gpu.fast = true;
  System s;
  s.ge.setRenderer(&gpu);
  prepare(s, 0, 3);
  quads3D(s, 3);
  s.ge.commands[GE::AlphaBlendEnable] = 1, s.ge.commands[GE::BlendMode] = 2 | 3 << 4;
  quads3D(s, 3);
  s.ge.settleAll();
  u32 meshes = 0, apart = 0, blendedMeshes = 0;
  for(auto& [kind, state] : pretend.drawn) {
    if(kind == GPU::Command::Kind::Mesh) meshes++, blendedMeshes += state.pipeline.blend;
    if(kind == GPU::Command::Kind::Draw) apart += state.pipeline.blend;
  }
  CHECK(meshes, 1u);
  CHECK(blendedMeshes, 0u);
  CHECK(apart > 0, true);
  s.ge.setRenderer(nullptr);
}

//VRAM's bytes the two machines don't agree on, all of them.
static auto vramApart(System& a, System& b) -> u32 {
  a.ge.settleAll(), b.ge.settleAll();
  u32 bytes = 0;
  for(u32 n = 0; n < Memory::VRAMSize; n++) {
    if(a.memory.vram[n] != b.memory.vram[n] && bytes < 4) {
      std::printf("  VRAM %05x: %02x, the GPU's %02x\n", n, a.memory.vram[n], b.memory.vram[n]);
    }
    bytes += a.memory.vram[n] != b.memory.vram[n];
  }
  return bytes;
}

//A PRIM the renderer can't draw is the software renderer's: one in a frame buffer near VRAM's end that reaches past
//it (its rows there run round to VRAM's start, as the PSP's addresses do, which a target on the GPU can't), and
//those in frame buffers for which a GPU out of room has no target (a pretend backend, which makes none: the
//software renderer draws them all, and the GPU's still ready).
static auto gpuRefused() -> void {
  struct Full : GPU::Backend {
    auto name() const -> std::string override { return "pretend"; }
    auto makeTarget(u32, u32) -> u32 override { return 0; }
    auto dropTarget(u32) -> void override {}
    auto makeTexture(u32, u32, const u32*, std::shared_ptr<const void>) -> u32 override { return 1; }
    auto dropTexture(u32) -> void override {}
    auto submit(const GPU::Recorded&) -> bool override { return true; }
    auto finish(const GPU::Recorded&) -> bool override { return true; }
    auto read(u32, const u32*&, const u8*&) -> bool override { return false; }
  };
  std::mt19937 random{20261010};
  auto sprites = randomSprites(random, 16, false);
  {
    GPU full(std::make_unique<Full>());
    System software, hardware;
    hardware.ge.setRenderer(&full);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      for(auto& one : sprites) sprite(*s, one);
    }
    CHECK(vramApart(software, hardware), 0u);
    CHECK(full.ready(), true);
    hardware.ge.setRenderer(nullptr);
  }
  auto gpu = renderer();
  if(!gpu) return;
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0x1f'8000, 3);  //(128 rows of 64 pixels to VRAM's end)
    s->ge.commands[GE::Scissor2] = 63 | 271 << 10;
    sprite(*s, {{{0, 0, 4, 100}, {0, 0, 40, 140}}, 0xff33'66cc});
  }
  CHECK(vramApart(software, hardware), 0u);
  CHECK(software.memory.vram[0x1f'8000 + (110 * 64 + 10) * 4], 0xccu);
  CHECK(software.memory.vram[(2 * 64 + 10) * 4], 0xccu);  //(row 130: VRAM's row 2)
  hardware.ge.setRenderer(nullptr);
}

//With the GE on its thread (ge/thread.hpp) and held back before it takes a list that draws a textured sprite on the
//GPU: the CPU's store into the sprite's texture in RAM just after the list, and its store into VRAM beside the frame
//buffer, don't wait for the thread (the bytes' old contents kept as versions, from which the render GE decodes the
//texture as of the sprite's place); the picture and memory come out as the software renderer has them without the
//thread.
static auto gpuThreadVersions() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 List = 0x0894'0000, Texture = GPUTexture, Beside = Memory::VRAMBase + (40 * 64 + 40) * 4;
  auto build = [](Memory& memory) {
    u32 at = List, vertex = GPUVertices;
    auto put = [&](u32 command, u32 argument = 0) {
      memory.write(4, at, command << 24 | (argument & 0xff'ffff)), at += 4;
    };
    put(GE::FrameBufferPointer, 0), put(GE::FrameBufferWidth, 64), put(GE::FrameBufferPixelFormat, 3);
    put(GE::Scissor1, 0), put(GE::Scissor2, 63 | 47 << 10), put(GE::Region2, 1023 | 1023 << 10);
    put(GE::VertexType, 0x80'019f), put(GE::ShadeMode, 1);
    put(GE::TextureMappingEnable, 1), put(GE::TextureAddress0, Texture & 0xff'ffff);
    put(GE::TextureBufferWidth0, (Texture >> 24 & 0xf) << 16 | 16), put(GE::TextureSize0, 4 << 8 | 4);
    put(GE::TextureFormat, 3), put(GE::TextureFunction, 3 | 1 << 8), put(GE::TextureFilter, 0);
    put(GE::Base, vertex >> 8 & 0xf'0000), put(GE::VertexAddress, vertex);
    for(auto [u, x] : {std::pair{0.0f, 8.0f}, std::pair{16.0f, 24.0f}}) {
      for(float value : {u, u}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
      memory.write(4, vertex, 0xffff'ffff), vertex += 4;
      for(float value : {x, x, 0.0f}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
    }
    put(GE::Primitive, GE::Sprites << 16 | 2);
    put(GE::Finish), put(GE::End);
    for(u32 n = 0; n < 16 * 16; n++) memory.write(4, Texture + n * 4, 0xff00'0000 | n * 0x01'0307);
  };
  System software, hardware;
  software.threadGE(false), hardware.threadGE(true);
  hardware.ge.setRenderer(gpu);
  std::atomic<bool> held{true};
  hardware.geThreading->drain();  //(the thread reads its gate as it takes each record)
  hardware.geThreading->gate = [&](u64) { while(held.load()) std::this_thread::yield(); };
  u64 waits = hardware.geThreading->waits;
  for(System* s : {&software, &hardware}) {
    build(s->memory);
    s->ge.list = {}, s->ge.list.address = List;
    CHECK(u32(s->ge.run(1000)), u32(GE::Stop::Finished));
    s->memory.write(4, Texture + (5 * 16 + 5) * 4, 0xffab'cdef);  //(after the list: the sprite draws the old texel)
    s->memory.write(4, Texture + (9 * 16 + 9) * 4, 0xff12'3456);  //(the page unwatched by the first, as ever: still heard)
    s->memory.write(4, Beside, 0x1234'5678);
  }
  CHECK(held.load(), true);  //(neither store waited for the thread, held still)
  CHECK(hardware.geThreading->waits, waits);
  CHECK(hardware.geThreading->versions->versions() > 0, true);
  held = false;
  hardware.geThreading->drain();
  hardware.geThreading->gate = nullptr;
  software.ge.settleAll(), hardware.ge.settleAll();
  CHECK(vramApart(software, hardware), 0u);
  CHECK(software.memory.read(4, Beside), 0x1234'5678u);
  hardware.ge.setRenderer(nullptr);
}

//With the GE on its thread, caught up (nothing handed over left to do): a store through VRAM's fourth copy onto a pixel
//the GPU drew, whose check finishes first (a load through the depth views doesn't, a store does), and a block transfer
//writing half of 8888 pixels, which finishes first too (overwritten()): each write's own record's finish comes before
//the write in the stream's order, though the render GE makes it after, so the write's old bytes are kept as a version
//even with nothing else to wait for, and the finish's write-back goes there, under the write. Memory comes out as the
//software renderer has it without the thread.
static auto gpuThreadOwnFinish() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 At = 0x3'0000;
  System software, hardware;
  software.threadGE(false), hardware.threadGE(true);
  hardware.ge.setRenderer(gpu);
  for(System* s : {&software, &hardware}) {
    for(u32 n = 0; n < 64 * 48 * 4; n += 4) s->memory.write(4, Memory::VRAMBase + At + n, 0x0102'0304 * (n / 4 + 1));
    prepare(*s, At, 3);
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030});
  }
  u64 finishes = stats(gpu).finishes;
  u32 seen = Memory::vramSeen(3, At + 20 * 256 + 8 * 4);
  for(System* s : {&software, &hardware}) s->memory.write(2, 0x0460'0000 + seen, 0x7788);
  CHECK(stats(gpu).finishes, finishes + 1);
  for(System* s : {&software, &hardware}) {
    auto& c = s->ge.commands;
    for(u32 n = 0; n < 64 * 4; n++) s->memory.write(4, GPUTexture + n * 4, 0xff00'0000 | n * 0x0a'0b0c);
    c[GE::TransferSource] = GPUTexture & 0xff'fff0, c[GE::TransferSourceWidth] = (GPUTexture >> 24) << 16 | 64;
    c[GE::TransferSourcePosition] = 0;
    c[GE::TransferDestination] = At & 0xff'fff0, c[GE::TransferDestinationWidth] = (Memory::VRAMBase >> 24) << 16 | 128;
    c[GE::TransferDestinationPosition] = 1 | 30 << 10;
    c[GE::TransferSize] = 15 | 1 << 10, c[GE::TransferStart] = 0;  //(2-byte pixels from the middle of 8888 ones)
    //(below the stored pixel, over the transfer's rows; the transfer's commands handed over with it)
    sprite(*s, {{{0, 0, 0, 24}, {0, 0, 64, 48}}, 0xff40'5060});
  }
  CHECK(stats(gpu).finishes, finishes + 1);  //(the thread caught up: the transfer hands nothing over before its rows)
  for(System* s : {&software, &hardware}) s->ge.transfer();
  CHECK(stats(gpu).finishes, finishes + 2);
  software.ge.settleAll(), hardware.ge.settleAll();
  u32 bytes = vramApart(software, hardware);
  if(bytes) std::printf("  %u bytes apart\n", bytes);
  CHECK(bytes, 0u);
  for(System* s : {&software, &hardware}) CHECK(s->memory.read(2, 0x0460'0000 + seen), 0x7788u);
  hardware.ge.setRenderer(nullptr);
}

//Ghost of Sparta's way, with the GE on its thread held back: a list draws a sprite over its frame buffer on the GPU,
//copies part of a list from RAM into VRAM over those pixels by a block transfer, and CALLs it there (it draws a second
//sprite and RETurns). The kernel's GE runs it all without waiting for the thread (the transfer's rows kept as
//versions where the render GE may read them, the list's words read from VRAM clean: the transfer is newer than the
//pixels drawn under them); the picture comes out as the software renderer's without the thread (the transfer's bytes
//over the first sprite, the GPU's finish leaving them be).
static auto gpuThreadTransferredList() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 List = 0x0894'0000, Called = 0x0895'0000, Vertices = GPUVertices, Into = 32 * 64 * 4;
  auto build = [](Memory& memory) {
    u32 at = List, vertex = Vertices;
    auto put = [&](u32 command, u32 argument = 0) {
      memory.write(4, at, command << 24 | (argument & 0xff'ffff)), at += 4;
    };
    auto sprite = [&](float left, float top, float right, float bottom, u32 color) {
      u32 words[2] = {GE::Base << 24 | (vertex >> 8 & 0xf'0000), GE::VertexAddress << 24 | (vertex & 0xff'ffff)};
      for(auto [x, y] : {std::pair{left, top}, std::pair{right, bottom}}) {
        for(float value : {0.0f, 0.0f}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
        memory.write(4, vertex, color), vertex += 4;
        for(float value : {x, y, 0.0f}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
      }
      return std::array<u32, 3>{words[0], words[1], u32(GE::Primitive << 24 | GE::Sprites << 16 | 2)};
    };
    put(GE::FrameBufferPointer, 0), put(GE::FrameBufferWidth, 64), put(GE::FrameBufferPixelFormat, 3);
    put(GE::Scissor1, 0), put(GE::Scissor2, 63 | 47 << 10), put(GE::Region2, 1023 | 1023 << 10);
    put(GE::VertexType, 0x80'019f), put(GE::ShadeMode, 1);
    for(u32 word : sprite(0, 0, 64, 48, 0xff20'4060)) memory.write(4, at, word), at += 4;
    //(the called list in RAM: a sprite, RET; 16 words, one row of 16 four-byte pixels)
    u32 called = Called;
    for(u32 word : sprite(10, 10, 30, 20, 0xffc0'8040)) memory.write(4, called, word), called += 4;
    memory.write(4, called, GE::Return << 24), called += 4;
    while(called < Called + 64) memory.write(4, called, 0), called += 4;  //(NOPs)
    put(GE::TransferSource, Called), put(GE::TransferSourceWidth, (Called >> 24) << 16 | 16);
    put(GE::TransferSourcePosition, 0);
    put(GE::TransferDestination, Memory::VRAMBase + Into);
    put(GE::TransferDestinationWidth, (Memory::VRAMBase >> 24) << 16 | 64), put(GE::TransferDestinationPosition, 0);
    put(GE::TransferSize, 15), put(GE::TransferStart, 1);
    put(GE::Base, (Memory::VRAMBase >> 8) & 0xf'0000), put(GE::Call, (Memory::VRAMBase + Into) & 0xff'ffff);
    put(GE::Finish), put(GE::End);
  };
  System software, hardware;
  software.threadGE(false), hardware.threadGE(true);
  hardware.ge.setRenderer(gpu);
  std::atomic<bool> held{true};
  hardware.geThreading->drain();  //(the thread reads its gate as it takes each record)
  hardware.geThreading->gate = [&](u64) { while(held.load()) std::this_thread::yield(); };
  u64 waits = hardware.geThreading->waits;
  for(System* s : {&software, &hardware}) {
    build(s->memory);
    s->ge.list = {}, s->ge.list.address = List;
    CHECK(u32(s->ge.run(1000)), u32(GE::Stop::Finished));
  }
  CHECK(held.load(), true);  //(the kernel's GE ran it all, the thread held still)
  CHECK(hardware.geThreading->waits, waits);
  held = false;
  hardware.geThreading->drain();
  hardware.geThreading->gate = nullptr;
  software.ge.settleAll(), hardware.ge.settleAll();
  CHECK(vramApart(software, hardware), 0u);
  CHECK(software.memory.read(4, Memory::VRAMBase + Into), u32(GE::Base << 24 | (Vertices >> 8 & 0xf'0000)));
  hardware.ge.setRenderer(nullptr);
}

//3D for the tests: a 64x48 frame buffer seen through an orthographic camera, its pixels' middles at the GE's, depth
//32768 plus 30000 times z; a texture 64 texels wide at address (VRAM's start by default) and rows tall, held at its
//edges (clamp), filtered nearest; and a quad over it from x0 to x1 and y0 to y1 at z, its texels the same columns'
//from row v0 on, 1:1. Its rows aren't known before it's drawn, as no 3D PRIM's are.
static auto orthographic(System& s, u32 rows, u32 address = Memory::VRAMBase, u32 format = 3) -> void {
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  auto& c = s.ge.commands;
  texture(s, address, 64, rows, 64, format);
  c[GE::TextureWrap] = 0x101, c[GE::VertexType] = 0x1ff, c[GE::MaxZ] = 0xffff;
  c[GE::TextureScaleU] = c[GE::TextureScaleV] = f24(1);
  for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
  for(u32 n = 0; n < 16; n++) s.ge.projection[n] = f24(n % 5 == 0 ? 1 : 0);
  c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-24), c[GE::ViewportZScale] = f24(30000);
  c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
  c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 24) << 4;
}
static auto quad3D(System& s, u32 rows, f32 x0, f32 x1, f32 y0, f32 y1, f32 v0, f32 z = 0) -> void {
  u32 to = GPUVertices;
  for(u32 corner : {0u, 1u, 3u, 0u, 3u, 2u}) {
    f32 x = corner & 1 ? x1 : x0, y = corner >> 1 ? y1 : y0, v = v0 + y - y0;
    for(u32 word : {bitsOf(x / 64), bitsOf(v / rows), 0xffff'ffffu, 0u, 0u, bitsOf(1), bitsOf(x / 32 - 1),
                    bitsOf(1 - y / 24), bitsOf(z)}) {
      s.memory.write(4, to, word), to += 4;
    }
  }
  s.ge.vertexAddress = GPUVertices;
  s.ge.primitive(GE::Triangles, 6);
}

//A frame buffer near VRAM's end sampled as a texture declared past it (Castlevania's background blur samples the
//frame buffer at VRAM's 0x154000 as a texture 512 rows tall, from 3D strips with a depth test): a 3D quad, whose
//rows aren't known, its texels stopping inside VRAM, taking it from the GPU's copy of its rows inside VRAM in either
//mode, nothing finished, not refused. Refused, the software renderer drew it against memory's depth, which isn't the
//GPU's (memory's 65535 here, the GPU's 30000), and the test of greater than 40000 left it out.
static auto gpuTextureAtVRAMsEnd() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Near = 0x1f'8000;  //(256 rows of 64 16-bit pixels to VRAM's end)
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  std::mt19937 random{20261084};
  auto pictures = randomSprites(random, 12, false);
  u64 finishes = 0;
  for(System* s : {&software, &hardware}) {
    auto& c = s->ge.commands;
    for(u32 n = 0; n < 64 * 48 * 2; n += 4) s->memory.write(4, 0x0460'0000 + 0x1f'0000 + n, 0xffff'ffff);
    prepare(*s, Near, 0);
    for(auto& one : pictures) sprite(*s, one);
    prepare(*s, 0, 3);
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written)
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'1010}, 30000);
    if(s == &hardware) finishes = stats(gpu).finishes;
    orthographic(*s, 512, Memory::VRAMBase + Near, 0);
    c[GE::DepthMask] = 1, c[GE::DepthTest] = 6;  //(greater, not written)
    quad3D(*s, 512, 4, 60, 2, 44, 0, (40000 - 32768) / 30000.0f);  //(at depth 40000)
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0, c[GE::TextureMappingEnable] = 0;
  }
  CHECK(stats(gpu).finishes, finishes);
  CHECK(apart(software, hardware), 0u);
  CHECK(software.memory.read(4, 0x0400'0000 + (20 * 64 + 30) * 4) != 0xff10'1010u, true);  //(the triangles drawn)
  hardware.ge.setRenderer(nullptr);
}

//A frame buffer the GPU drew sampled as palette indices, looked up on the GPU (Death Jr. II takes each of its
//picture's colors through a palette so): CLUT32 of an 8888 one and CLUT16 of each 16-bit one, through 8888 and 16-bit
//palettes, each with a shift, a mask and an offset (the index's bits the colors', not the stencil's); by a sprite,
//whose rows are known, and by a 3D quad, whose rows aren't, from a texture running on past the picture into another
//frame buffer's pages (of another row width) just below, which isn't taken. The GPU's pixels
//are the software renderer's, byte for byte, nothing finished.
static auto gpuPalettes() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(!gpu->backend->palettes) return skip("vulkan-palettes", "no palettes on the GPU");
  constexpr u32 Palette = 0x0897'0000;
  struct Case { u32 format, clutFormat, shift, mask, offset; };
  Case cases[] = {{3, 3, 8, 0xff, 0}, {3, 1, 0, 0xff, 0x100}, {3, 2, 13, 0x7f, 0x80}, {0, 0, 5, 0x3f, 0x40},
                  {0, 3, 8, 0xff, 0}, {1, 3, 2, 0xff, 0}, {1, 1, 10, 0x1f, 0x1e0}, {2, 2, 4, 0xff, 0x10}};
  std::mt19937 random{20261085};
  for(auto& k : cases) {
    for(bool triangles : {false, true}) {
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      auto drawn = randomSprites(random, 40, false);
      u32 entries[256];
      for(auto& entry : entries) entry = u32(random());
      u32 rowBytes = k.format == 3 ? 256 : 128, picture = 0x3000 / rowBytes;  //(rows to the next page: 48 or 96)
      GPU::Statistics before;
      for(System* s : {&software, &hardware}) {
        auto& c = s->ge.commands;
        for(u32 n = 0; n < 256; n++) s->memory.write(4, Palette + n * 4, entries[n]);
        prepare(*s, 0, k.format);
        c[GE::Scissor2] = 63 | (picture - 1) << 10;
        for(auto& one : drawn) {
          sprite(*s, {{{0, 0, one.at[0][2], one.at[0][3] * 2}, {0, 0, one.at[1][2], one.at[1][3] * 2}}, one.color});
        }
        prepare(*s, 0x3000, 3);  //(below the picture, 32 pixels wide)
        c[GE::FrameBufferWidth] = 32, c[GE::Scissor2] = 31 | 15 << 10;
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 32, 16}}, 0x5a12'3456});
        prepare(*s, 0x2'0000, 3);
        c[GE::Scissor2] = 63 | 63 << 10;
        c[GE::ClutAddress] = Palette & 0xff'ffff, c[GE::ClutAddressUpper] = Palette >> 8 & 0xf'0000;
        c[GE::ClutLoad] = 32;
        s->ge.loadClut();
        c[GE::ClutFormat] = k.clutFormat | k.shift << 2 | k.mask << 8 | k.offset >> 4 << 16;
        texture(*s, Memory::VRAMBase, 64, 128, 64, k.format == 3 ? 7 : 6);
        if(s == &hardware) before = stats(gpu);
        u32 rows = std::min(picture, 64u);
        if(!triangles) {
          sprite(*s, {{{0, 0, 0, 0}, {64, f32(rows), 64, f32(rows)}}, 0});
          continue;
        }
        //(its texels stopping short of the frame buffer below, so that accurate mode may hold the rows past it too)
        orthographic(*s, 128, Memory::VRAMBase, k.format == 3 ? 7 : 6);
        quad3D(*s, 128, 0, 64, 0, 44, 0);
      }
      bool taken = stats(gpu).finishes == before.finishes && stats(gpu).copies > before.copies;
      u32 bytes = apart(software, hardware);
      if(!taken || bytes) {
        std::printf("  format %u, palette %u >> %u & %02x | %03x, %s: %u bytes apart, %s\n", k.format, k.clutFormat,
                    k.shift, k.mask, k.offset, triangles ? "triangles" : "a sprite", bytes,
                    taken ? "taken" : "not taken from the GPU");
      }
      CHECK(taken, true);
      CHECK(bytes, 0u);
      hardware.ge.setRenderer(nullptr);
    }
  }
}

//The palettes kept on the GPU found again: a frame buffer sampled through palette A, then B, then A again (the GE's
//palette version moving on each time, its bytes A's again the third: found, not made again), each picture the
//software renderer's.
static auto gpuPalettesAgain() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(!gpu->backend->palettes) return skip("vulkan-palettes", "no palettes on the GPU");
  constexpr u32 Palette = 0x0897'0000;
  std::mt19937 random{20261088};
  auto drawn = randomSprites(random, 40, false);
  u32 palettes[2][256];
  for(auto& palette : palettes) for(auto& entry : palette) entry = u32(random());
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  GPU::Statistics before;
  for(System* s : {&software, &hardware}) {
    auto& c = s->ge.commands;
    prepare(*s, 0, 3);
    for(auto& one : drawn) sprite(*s, one);
    prepare(*s, 0x2'0000, 3);
    texture(*s, Memory::VRAMBase, 64, 64, 64, 7);
    if(s == &hardware) before = stats(gpu);
    for(u32 n = 0; n < 3; n++) {
      for(u32 k = 0; k < 256; k++) s->memory.write(4, Palette + k * 4, palettes[n & 1][k]);
      c[GE::ClutAddress] = Palette & 0xff'ffff, c[GE::ClutAddressUpper] = Palette >> 8 & 0xf'0000;
      c[GE::ClutLoad] = 32;
      s->ge.loadClut();
      c[GE::ClutFormat] = 3 | 8 << 2 | 0xff << 8;
      f32 top = n * 16;
      sprite(*s, {{{0, top, 0, top}, {64, top + 16, 64, top + 16}}, 0});
    }
  }
  CHECK(stats(gpu).finishes, before.finishes);
  CHECK(stats(gpu).palettes - before.palettes <= 2, true);
  CHECK(apart(software, hardware), 0u);
  hardware.ge.setRenderer(nullptr);
}

//Without rasterization order (Backend::readsInOrder off), a primitive that reads the frame buffer (blending in a
//16-bit one does) waits for a barrier only where it overlaps what was drawn into it since the last: eight sprites
//side by side, each its own draw (its own fixed blend color), over the background drawn before them one, the first;
//then one over all of them one more. The pictures the software renderer's.
static auto gpuBarriers() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(!gpu->backend->reads || gpu->backend->readsInOrder) {
    return skip("vulkan-unordered-reads", "the GPU reads in order, or reads nothing");
  }
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  GPU::Statistics before, between;
  for(System* s : {&software, &hardware}) {
    auto& c = s->ge.commands;
    prepare(*s, 0, 1);
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff20'4060});
    c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 10 | 10 << 4;  //(fixed A and B, added)
    c[GE::BlendFixedB] = 0x80'8080;
    if(s == &hardware) before = stats(gpu);
    for(u32 n = 0; n < 8; n++) {
      c[GE::BlendFixedA] = 0x10'1010 * (n + 1);
      sprite(*s, {{{0, 0, f32(n * 8), 8}, {0, 0, f32(n * 8 + 8), 24}}, 0xff80'40c0});
    }
    if(s == &hardware) between = stats(gpu);
    sprite(*s, {{{0, 0, 4, 12}, {0, 0, 60, 30}}, 0xff10'2030});
    c[GE::AlphaBlendEnable] = 0;
  }
  CHECK(between.barriers - before.barriers, 1u);
  CHECK(stats(gpu).barriers - between.barriers, 1u);
  CHECK(between.draws - before.draws, 8u);
  CHECK(apart(software, hardware), 0u);
  hardware.ge.setRenderer(nullptr);
}

//Textures the GPU let go of taken again for the next of their size: a texture in RAM changed between sprites, each
//time a texture decoded and made afresh, its runs finished between; the pictures the software renderer's.
static auto gpuTexturesKept() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  u64 kept = 0;
  for(u32 n = 0; n < 4; n++) {
    if(n == 1) kept = gpu->backend->texturesKept;  //(after the first, which may take one an earlier test left)
    for(System* s : {&software, &hardware}) {
      if(!n) prepare(*s, 0, 3);
      for(u32 k = 0; k < 32 * 32; k++) s->memory.write(4, GPUTexture + k * 4, u32((k + n * 77) * 0x9e37'79b9));
      texture(*s, GPUTexture, 32, 32, 32);
      sprite(*s, {{{0, 0, f32(n * 16), 0}, {32, 32, f32(n * 16 + 16), 32}}, 0});
      s->ge.settleAll();
    }
  }
  CHECK(gpu->backend->texturesKept > kept, true);
  CHECK(apart(software, hardware), 0u);
  hardware.ge.setRenderer(nullptr);
}

//A texture whose first rows are memory's (the CPU's picture) and whose rows then run on into a frame buffer the GPU
//is drawing (another row width), by a 3D quad, whose rows aren't known: the GE decodes the rows
//before that frame buffer's pages alone, without waiting for the GPU, and the GPU's pixels are the software
//renderer's. (Death Jr. II's bloom samples its picture, read back for the CPU, as a texture 512 rows tall, into the
//smaller frame buffer below it: each of its tiles finished drawing.)
static auto gpuMemorysRows() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  std::mt19937 random{20261086};
  std::vector<u32> picture(64 * 48);
  for(auto& pixel : picture) pixel = u32(random());
  GPU::Statistics before;
  for(System* s : {&software, &hardware}) {
    auto& c = s->ge.commands;
    for(u32 n = 0; n < picture.size(); n++) s->memory.write(4, Memory::VRAMBase + n * 4, picture[n]);
    prepare(*s, 0x3000, 3);  //(right below it, 32 pixels wide)
    c[GE::FrameBufferWidth] = 32, c[GE::Scissor2] = 31 | 15 << 10;
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 32, 16}}, 0x5a65'4321});
    prepare(*s, 0x2'0000, 3);
    orthographic(*s, 128);
    if(s == &hardware) before = stats(gpu);
    quad3D(*s, 128, 0, 64, 0, 44, 0);  //(its texels short of the frame buffer below: see gpuPalettes)
  }
  CHECK(stats(gpu).finishes, before.finishes);
  CHECK(hardware.ge.drawer().memory.vramBusy, true);
  CHECK(apart(software, hardware), 0u);
  hardware.ge.setRenderer(nullptr);
}


//3D quads each reading the top of the frame buffer they draw into while writing its bottom (Killzone's menus blur a
//strip so, reading one half while writing the other): the texels they take (their corners' u and v, the texture
//held at its edges) miss what those before them drew, so the copy taken for the first serves them all, the target's
//changes since left uncopied; then one reading what they drew is copied again. In each renderer, the pictures the
//software renderer's.
static auto gpuCopiesKept() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(!gpu) continue;
    std::mt19937 random{20261087};
    auto drawn = randomSprites(random, 30, false);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    GPU::Statistics before, between;
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      for(auto& one : drawn) sprite(*s, one);
      orthographic(*s, 32);
      if(s == &hardware) before = stats(gpu);
      for(u32 n = 0; n < 8; n++) quad3D(*s, 32, n * 8, n * 8 + 8, 26, 48, 0);
      if(s == &hardware) between = stats(gpu);
      quad3D(*s, 32, 0, 32, 0, 22, 24);  //(reading what they drew)
    }
    CHECK(between.copies - before.copies, 1u);
    CHECK(stats(gpu).copies - between.copies, 1u);
    CHECK(stats(gpu).finishes, before.finishes);
    CHECK(apart(software, hardware), 0u);
    hardware.ge.setRenderer(nullptr);
  }
}

//A texture's rows past its frame buffer's, where another frame buffer (of another row width) begins, held at the
//last of its own only where that can't change the picture (holdable()): in fast mode, or where the PRIM's texels stop
//before them. A 3D quad taking rows past them: in accurate mode decoded from memory after a finish, the picture the
//software renderer's; in fast mode held, nothing finished.
static auto gpuHeldRows() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(!gpu) continue;
    std::mt19937 random{20261089};
    auto drawn = randomSprites(random, 30, false);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    GPU::Statistics before;
    for(System* s : {&software, &hardware}) {
      auto& c = s->ge.commands;
      prepare(*s, 0, 3);
      for(auto& one : drawn) sprite(*s, one);
      prepare(*s, 0x3000, 3);  //(below the picture's 48 rows, 48 pixels wide: rows that don't fill pages, not moved)
      c[GE::FrameBufferWidth] = 48, c[GE::Scissor2] = 47 | 15 << 10;
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 48, 16}}, 0x5a12'3456});
      prepare(*s, 0x2'0000, 3);
      orthographic(*s, 64);
      if(s == &hardware) before = stats(gpu);
      quad3D(*s, 64, 0, 64, 0, 40, 16);  //(rows 16 to 55)
    }
    if(!gpu->fast) {
      CHECK(stats(gpu).finishes > before.finishes, true);
      CHECK(apart(software, hardware), 0u);
    } else {
      CHECK(stats(gpu).finishes, before.finishes);
    }
    hardware.ge.setRenderer(nullptr);
  }
}

//Fast mode's 3D taking a frame buffer as a texture whose rows run on into another frame buffer's (of another row
//width, not taken: held at the last row of its own, take()'s cut), from a copy of more rows a sprite took before that
//frame buffer was drawn: the rows taken ride with the PRIM (GPU::Vertex::taken()), which fetches are held inside. A
//PRIM of six corners among the meshes (screenTriangle(): transform.vert's placed vertices) and drawn apart (draw.vert),
//and a mesh of eighteen (its Transformed block), filtered, their texels reaching past the rows taken, at 1x and 2x:
//every pixel as from a copy of only the rows taken.
static auto gpuTakenRowsHeld() -> void {
  GPU* gpu = renderer() ? fastRenderer() : nullptr;
  if(!gpu) return;
  if(!gpu->backend->transforms) return skip("vulkan-transforms", "the driver didn't take transform.vert");
  std::mt19937 random{20261089};
  auto drawn = randomSprites(random, 30, false);
  auto quads = [](System& s, std::vector<std::array<f32, 5>> list) {  //(each x0, x1, y0, y1, v0: two triangles)
    u32 to = GPUVertices, count = 0;
    for(auto [x0, x1, y0, y1, v0] : list) {
      for(u32 corner : {0u, 1u, 3u, 0u, 3u, 2u}) {
        f32 x = corner & 1 ? x1 : x0, y = corner >> 1 ? y1 : y0, v = v0 + y - y0;
        for(u32 word : {bitsOf(x / 64), bitsOf(v / 64), 0xffff'ffffu, 0u, 0u, bitsOf(1), bitsOf(x / 32 - 1),
                        bitsOf(1 - y / 24), 0u}) {
          s.memory.write(4, to, word), to += 4;
        }
        count++;
      }
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Triangles, count);
  };
  for(u32 scale : {1u, 2u}) {
    if(!scaleTaken(gpu, scale)) break;
    auto run = [&](bool larger, bool together) {
      gpu->resolution(scale);
      gpu->screenMeshes = together;
      System s;
      s.ge.setRenderer(gpu);
      auto& c = s.ge.commands;
      prepare(s, 0, 3);
      for(auto& one : drawn) sprite(s, one);
      if(larger) {  //(the copy of all 64 rows, before the frame buffer below is drawn)
        prepare(s, 0x2'8000, 3);
        texture(s, Memory::VRAMBase, 64, 64, 64);
        sprite(s, {{{0, 0, 0, 0}, {64, 64, 64, 48}}, 0xffff'ffff});
      }
      prepare(s, 0x3000, 3);  //(below the picture's 48 rows, 48 pixels wide)
      c[GE::FrameBufferWidth] = 48, c[GE::Scissor2] = 47 | 15 << 10;
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 48, 16}}, 0x5a12'3456});
      prepare(s, 0x2'0000, 3);
      orthographic(s, 64);
      c[GE::TextureFilter] = 1 | 1 << 8;
      quads(s, {{0, 30, 0, 40, 16}});  //(rows 16 to 56)
      quads(s, {{32, 64, 0, 20, 16}, {32, 64, 20, 40, 36}, {32, 48, 40, 48, 40}});
      std::vector<u32> picture;
      CHECK(gpu->picture(0x2'0000, 64, 3, 64, 48, picture, scale), true);
      s.ge.setRenderer(nullptr);
      return picture;
    };
    auto held = run(true, true), apart = run(true, false), alone = run(false, true);
    auto differ = [](const std::vector<u32>& a, const std::vector<u32>& b) {
      u32 count = a.size() != b.size();
      for(u32 n = 0; n < a.size() && n < b.size(); n++) count += a[n] != b[n];
      return count;
    };
    u32 off = differ(held, alone), offApart = differ(held, apart);
    if(off) std::printf("  %ux: %u pixels differ from a copy of the rows taken alone\n", scale, off);
    if(offApart) std::printf("  %ux: %u pixels differ, among the meshes and apart\n", scale, offApart);
    CHECK(held.size(), u64(64 * 48 * scale * scale));
    CHECK(off, 0u);
    CHECK(offApart, 0u);
  }
  gpu->screenMeshes = true;
  gpu->resolution(1);
}

//Render to texture from many places and sizes (a strip further down each time, wider or taller): the GPU's copies
//of them are kept to a bound, the oldest let go, and every picture is still the software renderer's.
static auto gpuCopies() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261011};
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  auto drawn = randomSprites(random, 40, false);
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);
    for(auto& one : drawn) sprite(*s, one);
    prepare(*s, 0x2'0000, 3);
  }
  u32 most = 0;
  for(u32 n = 0; n < 300; n++) {
    float w = 1 + n % 50, h = 1 + n * 7 % 60;
    Corners one{{{0, 0, 0, 0}, {w, h, w, h}}, 0xffff'ffff};
    for(System* s : {&software, &hardware}) {
      texture(*s, Memory::VRAMBase + (n % 20) * 64 * 4, 64, 64, 64);
      sprite(*s, one);
    }
    most = std::max(most, gpu->copiesKept());
  }
  CHECK(most <= 32, true);
  CHECK(apart(software, hardware), 0u);
  hardware.ge.setRenderer(nullptr);
}

//Render to texture taken again from the same place with the target changed in part since, at 1x and above: an effect
//blurring a strip right of the picture back and forth, each sprite sampling the frame buffer it draws into (as
//Killzone's menu does), the picture left of it sampled now and then. Every picture is the software renderer's (each
//pass modulates by its own color, so a part of a copy left stale would show), and the copies after the first take
//the strip, not the picture: fewer than half the pixels copies of the whole would. Then a texture starting inside the
//frame buffer (at 8, 8) sampled into another frame buffer, small patches of it drawn over between the samples (each
//part copied to its own place in the copy), and rows the CPU writes over between two of them (the target filled
//from memory again: the rows filled are changes too). And sprites reaching past a scissor away from the frame
//buffer's corner (16, 8 to 47, 39) between samples: what they change is inside the scissor, wherever it is.
static auto gpuPartialCopies() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261010};
  for(u32 scale : {1u, 2u, 3u}) {
    if(!scaleTaken(gpu, scale)) break;
    gpu->resolution(scale);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto drawn = randomSprites(random, 40, false);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      for(auto& one : drawn) sprite(*s, one);
      texture(*s, Memory::VRAMBase, 64, 64, 64);
      s->ge.commands[GE::TextureFunction] = 0 | 1 << 8;  //(modulated by the color, with its alpha)
    }
    auto before = stats(gpu);
    u64 whole = 0;  //(what copies of all that's sampled would take)
    for(u32 n = 0; n < 16; n++) {  //(columns 48-63: rows 0-15 drawn from rows 16-31, then back)
      float from = n & 1 ? 0 : 16, to = n & 1 ? 16 : 0;
      u32 color = 0xff80'8080 + n * 0x0007'0b05;
      for(System* s : {&software, &hardware}) {
        sprite(*s, {{{48, from, 48, to}, {64, from + 16, 64, to + 16}}, color});
        if(n % 4 == 3) sprite(*s, {{{0, 0, 48, 32}, {16, 16, 64, 48}}, 0xffff'ffff});
      }
      whole += 64 * (from + 16) + (n % 4 == 3 ? 16 * 16 : 0);
    }
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %ux: %u bytes apart\n", scale, bytes);
    CHECK(bytes, 0u);
    u64 copied = stats(gpu).copied - before.copied;
    if(copied * 2 >= whole) std::printf("  %ux: %llu pixels copied of %llu\n", scale, (unsigned long long)copied,
                                        (unsigned long long)whole);
    CHECK(copied * 2 < whole, true);
    hardware.ge.setRenderer(nullptr);
  }
  for(u32 scale : {1u, 2u}) {
    if(!scaleTaken(gpu, scale)) break;
    gpu->resolution(scale);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto drawn = randomSprites(random, 30, false);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      for(auto& one : drawn) sprite(*s, one);
    }
    for(u32 n = 0; n < 12; n++) {
      float x = 8 + n * 5 % 26, y = 8 + n * 7 % 26;
      for(System* s : {&software, &hardware}) {
        prepare(*s, 0, 3);
        s->ge.commands[GE::TextureMappingEnable] = 0;
        sprite(*s, {{{0, 0, x, y}, {0, 0, x + 6, y + 6}}, 0xff00'0000 | n * 0x13'2b47});
        if(n == 6) {  //(the CPU's bytes over the texture's rows 4-7: the target filled with them when next drawn)
          for(u32 m = 0; m < 256; m++) s->memory.write(4, Memory::VRAMBase + 12 * 256 + m * 4, 0xff00'00ffu + m * 77);
        }
        prepare(*s, 0x2'0000, 3);
        texture(*s, Memory::VRAMBase + (8 * 64 + 8) * 4, 32, 32, 64);
        sprite(*s, {{{0, 0, 4 * float(n % 3), 4}, {32, 32, 4 * float(n % 3) + 32, 36}}, 0xffff'ffff});
      }
    }
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %ux, inside the frame buffer: %u bytes apart\n", scale, bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
  }
  gpu->resolution(1);
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  auto drawn = randomSprites(random, 30, false);
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);
    for(auto& one : drawn) sprite(*s, one);
  }
  for(u32 n = 0; n < 4; n++) {
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0x2'0000, 3);
      texture(*s, Memory::VRAMBase, 64, 64, 64);
      sprite(*s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
      prepare(*s, 0, 3);
      s->ge.commands[GE::Scissor1] = 16 | 8 << 10, s->ge.commands[GE::Scissor2] = 47 | 39 << 10;
      s->ge.commands[GE::TextureMappingEnable] = 0;
      sprite(*s, {{{0, 0, 4 + 9.0f * n, 2}, {0, 0, 30 + 9.0f * n, 46}}, 0xff00'0000 | (n + 1) * 0x30'5070});
      s->ge.commands[GE::Scissor1] = 0;
    }
  }
  u32 bytes = apart(software, hardware);
  if(bytes) std::printf("  a scissor away from the corner: %u bytes apart\n", bytes);
  CHECK(bytes, 0u);
  hardware.ge.setRenderer(nullptr);
}

//A texture running from one frame buffer into the next below it in memory (Midnight Club 3's menu samples its frame
//buffer as a texture 512 rows tall, whose last rows are the next frame buffer's): taken from the two targets on the
//GPU, each part from its own, with nothing finished, and the picture the software renderer's. From the first's row 0
//and from its row 8 (fewer rows of it, more of the second), the whole texture sampled again after each of two sprites
//drawn into the second (the same copy, taken again: the second part too, as MC3's menu samples it every frame); with
//the second drawn only in its first 4 rows (the rest the texture takes filled from memory, which the CPU wrote) and a
//PRIM drawing into it while it samples rows it doesn't draw over, as MC3's does: that one alone takes the two parts
//with nothing moved (a move of the second's pixels into the first's target would go back as it draws there). And one
//whose pixels the first frame buffer drew over the second's first rows since the second drew there (the newest of
//those pixels the first's): the second's pixels in the texture's pages moved into the first's target on the GPU
//(move()), nothing finished, also for a PRIM drawing into the second, which can't take two parts there; where the
//GPU doesn't move them, decoded from memory, as before.
static auto gpuStackedTexture() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261013};
  for(u32 from : {0u, 8u}) {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto upper = randomSprites(random, 30, false), lower = randomSprites(random, 30, false);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);  //(rows 0-47)
      for(auto& one : upper) sprite(*s, one);
      prepare(*s, 48 * 64 * 4, 3);  //(the next frame buffer, from row 48 on: its rows 0-15 the texture's rows 48-63)
      for(auto& one : lower) sprite(*s, one);
      prepare(*s, 0x2'0000, 3);
      texture(*s, Memory::VRAMBase + from * 64 * 4, 64, 64, 64);
    }
    auto before = stats(gpu);
    for(System* s : {&software, &hardware}) {
      sprite(*s, {{{0, 0, 0, 0}, {64, 64, 64, 48}}, 0xffff'ffff});  //(the whole texture, shrunk to rows 0-47)
      sprite(*s, {{{8, 40, 4, 4}, {40, 64, 36, 28}}, 0xffff'ffff});  //(across the two)
    }
    for(u32 n = 0; n < 2; n++) {
      for(System* s : {&software, &hardware}) {
        prepare(*s, 48 * 64 * 4, 3);
        s->ge.commands[GE::TextureMappingEnable] = 0;
        sprite(*s, {{{0, 0, 10 + 20.0f * n, 2}, {0, 0, 30 + 20.0f * n, 12}}, 0xff20'40c0 + n * 0x30'0000});
        prepare(*s, 0x2'0000, 3);
        s->ge.commands[GE::TextureMappingEnable] = 1;
        sprite(*s, {{{0, 0, 0, 0}, {64, 64, 64, 48}}, 0xffff'ffff});
      }
    }
    CHECK(stats(gpu).finishes, before.finishes);
    CHECK(stats(gpu).copies - before.copies >= 2, true);
    CHECK(apart(software, hardware), 0u);
    hardware.ge.setRenderer(nullptr);
  }
  {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto upper = randomSprites(random, 30, false);
    for(System* s : {&software, &hardware}) {
      for(u32 n = 0; n < 64 * 16; n++) s->memory.write(4, Memory::VRAMBase + (48 * 64 + n) * 4, 0xff40'2010 + n * 131);
      prepare(*s, 0, 3);
      for(auto& one : upper) sprite(*s, one);
      prepare(*s, 48 * 64 * 4, 3);
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 4}}, 0xff00'ff00});
    }
    auto before = stats(gpu);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0x2'0000, 3);
      texture(*s, Memory::VRAMBase, 64, 64, 64);
      sprite(*s, {{{0, 0, 0, 0}, {64, 64, 64, 48}}, 0xffff'ffff});
      prepare(*s, 48 * 64 * 4, 3);
      sprite(*s, {{{0, 0, 0, 20}, {64, 64, 64, 44}}, 0xffff'ffff});  //(rows 20-43 of the second, from both)
    }
    CHECK(stats(gpu).finishes, before.finishes);
    CHECK(apart(software, hardware), 0u);
    hardware.ge.setRenderer(nullptr);
  }
  {  //(MC3's PRIM alone: drawing into the second, sampling both)
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto upper = randomSprites(random, 30, false), lower = randomSprites(random, 30, false);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      for(auto& one : upper) sprite(*s, one);
      prepare(*s, 48 * 64 * 4, 3);
      for(auto& one : lower) sprite(*s, one);
      texture(*s, Memory::VRAMBase, 64, 64, 64);
    }
    auto before = stats(gpu);
    for(System* s : {&software, &hardware}) sprite(*s, {{{0, 0, 0, 20}, {64, 64, 64, 44}}, 0x8080'8080});
    CHECK(stats(gpu).finishes, before.finishes);
    CHECK(stats(gpu).moves, before.moves);
    CHECK(stats(gpu).copies - before.copies >= 2, true);
    CHECK(apart(software, hardware), 0u);
    hardware.ge.setRenderer(nullptr);
  }
  {  //(the second draws rows 0-31, the first its rows 0-7 since, then the second its rows 20-30 again: the first owns
     //the second's first page, the second the page after)
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto upper = randomSprites(random, 30, false);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      for(auto& one : upper) sprite(*s, one);
      prepare(*s, 48 * 64 * 4, 3);
      for(u32 n = 0; n < 4; n++) sprite(*s, {{{0, 0, 16.0f * n, 0}, {0, 0, 16.0f * n + 16, 32}}, 0xff00'8000 + n * 17});
      prepare(*s, 0, 3);
      s->ge.commands[GE::Scissor2] = 63 | 63 << 10;
      for(u32 n = 0; n < 6; n++) sprite(*s, {{{0, 0, 6.0f * n, 48}, {0, 0, 6.0f * n + 9, 56}}, 0xff80'0040 + n * 34});
      prepare(*s, 48 * 64 * 4, 3);
      sprite(*s, {{{0, 0, 4, 20}, {0, 0, 50, 31}}, 0xff40'40c0});
      prepare(*s, 0x2'0000, 3);
      texture(*s, Memory::VRAMBase, 64, 128, 64);
    }
    auto before = stats(gpu);
    for(System* s : {&software, &hardware}) sprite(*s, {{{0, 0, 0, 0}, {64, 80, 64, 48}}, 0xffff'ffff});
    if(gpu->backend->moves) {
      CHECK(stats(gpu).finishes, before.finishes);
      CHECK(stats(gpu).moves > before.moves, true);
    } else {
      CHECK(stats(gpu).finishes > before.finishes, true);
    }
    CHECK(apart(software, hardware), 0u);
    hardware.ge.setRenderer(nullptr);
  }
  {  //(the same, the PRIM drawing into the second's rows 40-47 past those it samples: no two parts there, moved)
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto upper = randomSprites(random, 30, false);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      for(auto& one : upper) sprite(*s, one);
      prepare(*s, 48 * 64 * 4, 3);
      for(u32 n = 0; n < 4; n++) sprite(*s, {{{0, 0, 16.0f * n, 0}, {0, 0, 16.0f * n + 16, 32}}, 0xff00'8000 + n * 17});
      prepare(*s, 0, 3);
      s->ge.commands[GE::Scissor2] = 63 | 63 << 10;
      for(u32 n = 0; n < 6; n++) sprite(*s, {{{0, 0, 6.0f * n, 48}, {0, 0, 6.0f * n + 9, 56}}, 0xff80'0040 + n * 34});
      prepare(*s, 48 * 64 * 4, 3);
      sprite(*s, {{{0, 0, 4, 20}, {0, 0, 50, 31}}, 0xff40'40c0});
      prepare(*s, 0x2'0000, 3);
      texture(*s, Memory::VRAMBase, 64, 128, 64);
    }
    auto before = stats(gpu);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 48 * 64 * 4, 3);
      sprite(*s, {{{0, 0, 0, 40}, {64, 80, 64, 48}}, 0xffff'ffff});
    }
    if(gpu->backend->moves) {
      CHECK(stats(gpu).finishes, before.finishes);
      CHECK(stats(gpu).moves > before.moves, true);
    } else {
      CHECK(stats(gpu).finishes > before.finishes, true);
    }
    CHECK(apart(software, hardware), 0u);
    hardware.ge.setRenderer(nullptr);
  }
}

//A texture from two frame buffers, one below the other, taken once for the PRIMs after it while neither changes: the
//frame buffer below's changes are heard as the one above's are (Copied::nextDirty), so God of War's 272 tiles — each
//sampling the picture it shrank the screen into, which lies in two frame buffers — take one copy, not a copy each. A
//draw into either frame buffer between samples is seen, and one below the rows the texture takes leaves it as it is.
//The picture is the software renderer's, as the copies are the drawing's.
static auto gpuStackedCopiesKept() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261013};
  auto upper = randomSprites(random, 30, false), lower = randomSprites(random, 30, false);
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  GPU::Statistics before;
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);  //(rows 0-47)
    for(auto& one : upper) sprite(*s, one);
    prepare(*s, 48 * 64 * 4, 3);  //(the next frame buffer, from row 48 on: its rows 0-15 the texture's rows 48-63)
    for(auto& one : lower) sprite(*s, one);
    prepare(*s, 0x2'0000, 3);
    texture(*s, Memory::VRAMBase, 64, 64, 64);
    if(s == &hardware) before = stats(gpu);
    for(u32 n = 0; n < 8; n++) sprite(*s, {{{0, 0, 0, 0}, {64, 64, 64, 48}}, 0xffff'ffff});  //(the whole texture, 8 times)
  }
  CHECK(stats(gpu).copies - before.copies, 2u);  //(both its parts once, not a copy a PRIM)
  CHECK(stats(gpu).finishes, before.finishes);
  CHECK(apart(software, hardware), 0u);
  auto between = stats(gpu);
  for(System* s : {&software, &hardware}) {  //(a draw into the frame buffer above: both parts again)
    prepare(*s, 0, 3);
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 4}}, 0xff00'ff00});
    prepare(*s, 0x2'0000, 3);
    sprite(*s, {{{0, 0, 0, 0}, {64, 64, 64, 48}}, 0xffff'ffff});
  }
  CHECK(stats(gpu).copies - between.copies >= 1, true);  //(the picture's change seen, not served as it was)
  CHECK(apart(software, hardware), 0u);
  between = stats(gpu);
  for(System* s : {&software, &hardware}) {  //(one below the rows it takes there: it serves as it is)
    prepare(*s, 48 * 64 * 4, 3);
    sprite(*s, {{{0, 0, 0, 32}, {0, 0, 64, 36}}, 0xff00'ff00});  //(rows 32-35, past the 16 the texture takes there)
    prepare(*s, 0x2'0000, 3);
    sprite(*s, {{{0, 0, 0, 0}, {64, 64, 64, 48}}, 0xffff'ffff});
  }
  CHECK(stats(gpu).copies - between.copies, 0u);
  CHECK(apart(software, hardware), 0u);
  hardware.ge.setRenderer(nullptr);
}

//A texture decoded again before the GPU has the first copy's texels (its bytes rewritten between two sprites, which
//has the GE let the first go): each copy's texels are kept until the run that puts them on the GPU, without a copy of
//its own (the sanitizers would catch the first one's read after it was let go), and the picture is the software
//renderer's.
static auto gpuTexelsKept() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261014};
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  auto sampled = randomSprites(random, 8, true);
  for(System* s : {&software, &hardware}) prepare(*s, 0, 3);
  for(u32 frame = 0; frame < 4; frame++) {
    for(auto& one : sampled) {
      u32 seed = random();
      for(System* s : {&software, &hardware}) {
        for(u32 n = 0; n < 32 * 32; n++) s->memory.write(4, GPUTexture + n * 4, u32((n + seed) * 0x9e37'79b9));
        texture(*s, GPUTexture, 32, 32, 32);
        sprite(*s, one);
      }
    }
    CHECK(apart(software, hardware), 0u);
  }
  hardware.ge.setRenderer(nullptr);
}

//What the GPU drew put into memory at a finish only in the pages each target drew in: a frame buffer's drawn
//rectangle spans two PRIMs far apart (rows 0-1 and 40-41: its pages 0 and 2, not 1), and memory's bytes between them
//are newer than its pixels there: the CPU's, written between the PRIMs, or another frame buffer's, drawn into that
//page since (the other one read back first). Both kept, as the software renderer has them; and the CPU's in the
//second page of a row that runs over two (a frame buffer 1,024 pixels wide, from half a page in).
static auto gpuBetweenPages() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  for(bool cpu : {true, false}) {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    u32 at = cpu ? 0 : 0x3'0000;  //(where no other test draws: the other frame buffer's target made first, below)
    for(System* s : {&software, &hardware}) {
      if(!cpu) {  //(the other frame buffer, at row 16's page, drawn first: read back first)
        prepare(*s, at + 0x1000, 3);
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 4}}, 0xff80'4020});
        (void)s->memory.read(4, Memory::VRAMBase + at + 0x1000);  //(a finish)
      }
      prepare(*s, at, 3);
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 2}}, 0xff00'00ff});
      sprite(*s, {{{0, 0, 0, 40}, {0, 0, 64, 42}}, 0xff00'ff00});
      if(cpu) {
        for(u32 n = 0; n < 64; n++) s->memory.write(4, Memory::VRAMBase + at + (20 * 64 + n) * 4, 0xffab'cdef);
      } else {
        prepare(*s, at + 0x1000, 3);
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 4}}, 0xff10'20c0});
        prepare(*s, at, 3);
      }
      sprite(*s, {{{0, 0, 0, 44}, {0, 0, 8, 46}}, 0xffff'0000});
    }
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %s: %u bytes apart\n", cpu ? "the CPU's row" : "the other frame buffer's", bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
  }
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  u32 at = 0x3'8800;  //(row 0 in pages 0x38 and 0x39, row 1 in 0x39 and 0x3a, row 2 in 0x3a and 0x3b)
  for(System* s : {&software, &hardware}) {
    prepare(*s, at, 3);
    s->ge.commands[GE::FrameBufferWidth] = 1024, s->ge.commands[GE::Scissor2] = 1023 | 47 << 10;
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 16, 1}}, 0xff00'00ff});  //(page 0x38)
    sprite(*s, {{{0, 0, 600, 2}, {0, 0, 616, 3}}, 0xff00'ff00});  //(page 0x3b)
    for(u32 n = 0; n < 64; n++) s->memory.write(4, Memory::VRAMBase + at + (512 + n) * 4, 0xffab'cdef);  //(0x39)
    sprite(*s, {{{0, 0, 0, 1}, {0, 0, 8, 2}}, 0xffff'0000});
  }
  u32 bytes = apart(software, hardware);
  if(bytes) std::printf("  a row over two pages: %u bytes apart\n", bytes);
  CHECK(bytes, 0u);
  hardware.ge.setRenderer(nullptr);
}

//A PRIM the renderer refuses among PRIMs the GPU draws, in a list run with the drawing threads (four, batches shared
//out however small, the CPU's guard over VRAM): the refused one (a sprite reaching rows 505-520 of a frame buffer,
//past a target's 512) is the software renderer's, and later sprites the GPU draws reach rows 500-511 of the same
//frame buffer, either side of it, and the rows of another. Then the next list starts, and the CPU loads a pixel the
//GPU drew and stores into another of them. Everything comes out as the software renderer has it: the refused sprite
//under the GPU's (not the rows the GPU had from memory before it, put back over it), the load finding the GPU's
//pixel, and the store kept (not put back over by the GPU's older pixels at the finish).
static auto gpuRefusedAmongThreads() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 ListA = 0x0894'0000, ListB = 0x0895'0000, Other = 0x4'0000;  //(Other: a VRAM offset)
  auto build = [](Memory& memory) {
    u32 at = ListA, vertex = GPUVertices;
    auto put = [&](u32 command, u32 argument = 0) {
      memory.write(4, at, command << 24 | (argument & 0xff'ffff)), at += 4;
    };
    auto sprite = [&](float left, float top, float right, float bottom, u32 color) {
      put(GE::Base, vertex >> 8 & 0xf'0000), put(GE::VertexAddress, vertex);
      for(auto [x, y] : {std::pair{left, top}, std::pair{right, bottom}}) {
        for(float value : {0.0f, 0.0f}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
        memory.write(4, vertex, color), vertex += 4;
        for(float value : {x, y, 0.0f}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
      }
      put(GE::Primitive, GE::Sprites << 16 | 2);
    };
    put(GE::FrameBufferPointer, 0), put(GE::FrameBufferWidth, 64), put(GE::FrameBufferPixelFormat, 3);
    put(GE::Scissor1, 0), put(GE::Scissor2, 63 | 1023 << 10), put(GE::Region2, 1023 | 1023 << 10);
    put(GE::VertexType, 0x80'019f), put(GE::ShadeMode, 1);
    sprite(4, 505, 40, 521, 0xff33'66cc);  //(refused: rows 505-520)
    sprite(0, 500, 4, 512, 0xff00'ff00);   //(the GPU's: rows 500-511, either side of it)
    sprite(60, 500, 64, 512, 0xffff'0000);
    put(GE::FrameBufferPointer, Other);
    sprite(0, 0, 64, 16, 0xff12'3456);
    put(GE::Finish), put(GE::End);
    at = ListB;
    put(GE::ShadeMode, 1), put(GE::Finish), put(GE::End);
  };
  KernelMachine software, hardware;
  hardware.system.ge.setRenderer(gpu);
  u32 loaded[2] = {};
  for(auto* m : {&software, &hardware}) {
    m->system.ge.setThreads(4);
    m->system.ge.drawing.shared = 0;
    build(m->system.memory);
    m->call("sceGeListEnQueue", {ListA, 0, 0xffff'ffff, 0});
    m->call("sceGeListEnQueue", {ListB, 0, 0xffff'ffff, 0});
    loaded[m == &hardware] = m->system.memory.read(4, Memory::VRAMBase + Other + (8 * 64 + 8) * 4);
    m->system.memory.write(4, Memory::VRAMBase + Other + (10 * 64 + 8) * 4, 0xffab'cdef);
  }
  if(loaded[1] != loaded[0]) std::printf("  the CPU's load found %08x, not the GPU's pixel\n", loaded[1]);
  CHECK(loaded[0], 0x0012'3456u);  //(the stencil, the alpha, kept: 0)
  CHECK(loaded[1], loaded[0]);
  u32 bytes = vramApart(software.system, hardware.system);
  if(bytes) std::printf("  %u bytes apart\n", bytes);
  CHECK(bytes, 0u);
  CHECK(hardware.system.memory.read(4, Memory::VRAMBase + Other + (10 * 64 + 8) * 4), 0xffab'cdefu);
  CHECK(hardware.system.memory.read(4, Memory::VRAMBase + (508 * 64 + 20) * 4), 0x0033'66ccu);
  hardware.system.ge.setRenderer(nullptr);
}

//The same memory drawn as frame buffers of different formats and places in turn, as God of War draws its effects:
//an 8888 frame buffer 64 pixels wide, a 5650 one 128 wide over the same bytes, an 8888 one a page further down whose
//rows are the first's rows 16 on, a 5551 one 128 wide (its alpha the stencil's top bit), and a 4444 one (its top
//four). Each takes the pixels the one before drew in its pages on the GPU, as memory would have them (move()), with
//nothing put back; at 1x, 2x and 3x, and everything comes out as the software renderer has it. (Sprites with random
//colors and alphas, some in clear mode setting the stencil, some textured from a texture the CPU wrote in VRAM, some
//keeping the stencil's low bits, some drawn where the stencil is 255, which the moved stencil decides.) The stencil
//moved a bit at a time and, where a shader can give it, with the colors (stencilWays()).
static auto gpuMoves() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(!gpu->backend->moves) return skip("vulkan-moves", "the GPU doesn't move pixels between targets");
  std::mt19937 random{20261016};
  constexpr u32 At = 0x3'0000;  //(where no other test draws: VRAM's offset)
  bool exports = gpu->backend->stencilExport;
  std::printf("  the stencil %s\n", exports ? "a bit at a time, and with the colors" : "a bit at a time");
  for(bool exported : stencilWays(gpu)) {
    gpu->backend->stencilExport = exported;
    for(u32 scale : {1u, 2u, 3u}) {
      if(!scaleTaken(gpu, scale)) break;
      gpu->resolution(scale);
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      for(System* s : {&software, &hardware}) {
        for(u32 n = 0; n < 64 * 48; n++) {
          s->memory.write(4, Memory::VRAMBase + 0x2'0000 + n * 4, u32(n * 0x9e37'79b9));
        }
      }
      auto before = stats(gpu);
      struct Pass {
        u32 address, width, format;
        std::vector<Corners> sprites;
        bool clear, textured, masked = false, tested = false;
      };
      std::vector<Pass> passes;
      for(u32 round = 0; round < 3; round++) {
        passes.push_back({At, 64, 3, randomSprites(random, 12, false), round == 1, false, round == 2});
        passes.push_back({At, 128, 0, randomSprites(random, 12, false), false, round == 2});
        passes.push_back({At + 0x1000, 64, 3, randomSprites(random, 8, false), false, round == 0});
        passes.push_back({At, 128, 1, randomSprites(random, 8, false), round == 2, false, false, round == 1});
        passes.push_back({At, 128, 2, randomSprites(random, 8, false), round == 0, round == 1, false, round == 2});
      }
      for(System* s : {&software, &hardware}) {
        auto& c = s->ge.commands;
        for(auto& pass : passes) {
          prepare(*s, pass.address, pass.format);
          c[GE::FrameBufferWidth] = pass.width;
          if(pass.textured) texture(*s, Memory::VRAMBase + 0x2'0000, 64, 64, 64);
          if(pass.clear) c[GE::ClearMode] = 1 | 3 << 8;  //(colors and stencil)
          if(pass.masked) c[GE::MaskAlpha] = 0x0f;  //(the stencil's low bits kept: a write mask the move mustn't keep)
          if(pass.tested) c[GE::StencilTestEnable] = 1, c[GE::StencilTest] = 2 | 0xff << 8 | 0xff << 16;
          for(auto& one : pass.sprites) sprite(*s, one);
          c[GE::ClearMode] = 0, c[GE::TextureMappingEnable] = 0, c[GE::MaskAlpha] = 0, c[GE::StencilTestEnable] = 0;
        }
      }
      CHECK(stats(gpu).finishes, before.finishes);
      CHECK(stats(gpu).moves - before.moves >= 9, true);
      u32 bytes = vramApart(software, hardware);
      if(bytes) std::printf("  %ux: %u bytes apart\n", scale, bytes);
      CHECK(bytes, 0u);
      hardware.ge.setRenderer(nullptr);
    }
  }
  gpu->backend->stencilExport = exports;
  gpu->resolution(1);
}

//The same memory as frame buffers of other row widths (Colin McRae Rally draws its 480-pixel picture's memory as a
//256-pixel frame buffer too, Dirt 2 a 128-pixel 5551 one's as a 64-pixel 4444 one, Brave Story a 256-pixel 8888
//one's as a 128-pixel one, each frame): each pass's pixels moved on the GPU into the next one's target, each pixel
//there the one at its bytes, where a shader gives the stencil (else moves into a target with a stencil take whole
//rows of it they can't keep the stencil of: put back and filled from memory, a finish); every byte the software
//renderer's, at 1, 2 and 3 times the resolution, the passes' widths 32, 64, 128 and 256 pixels in each format.
static auto gpuStridedMoves() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(!gpu->backend->moves) return skip("vulkan-moves", "the GPU doesn't move pixels between targets");
  std::mt19937 random{20261083};
  constexpr u32 At = 0x3'0000;
  bool exports = gpu->backend->stencilExport;
  for(bool exported : stencilWays(gpu)) {
    gpu->backend->stencilExport = exported;
    for(u32 scale : {1u, 2u, 3u}) {
      if(!scaleTaken(gpu, scale)) break;
      gpu->resolution(scale);
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      auto before = stats(gpu);
      struct Pass { u32 width, format; std::vector<Corners> sprites; bool clear; };
      std::vector<Pass> passes;
      for(u32 round = 0; round < 2; round++) {
        passes.push_back({64, 3, randomSprites(random, 10, false), round == 0});
        passes.push_back({32, 3, randomSprites(random, 8, false), false});
        passes.push_back({64, 1, randomSprites(random, 8, false), round == 1});
        passes.push_back({128, 3, randomSprites(random, 8, false), false});
        passes.push_back({32, 2, randomSprites(random, 8, false), round == 0});
        passes.push_back({256, 0, randomSprites(random, 8, false), false});
        passes.push_back({128, 1, randomSprites(random, 8, false), false});
      }
      for(System* s : {&software, &hardware}) {
        auto& c = s->ge.commands;
        for(auto& pass : passes) {
          prepare(*s, At, pass.format);
          c[GE::FrameBufferWidth] = pass.width;
          c[GE::Scissor2] = (std::min(pass.width, 64u) - 1) | 47 << 10;
          if(pass.clear) c[GE::ClearMode] = 1 | 3 << 8;  //(colors and stencil)
          for(auto& one : pass.sprites) sprite(*s, one);
          c[GE::ClearMode] = 0;
        }
      }
      if(exported) CHECK(stats(gpu).finishes, before.finishes);
      CHECK(stats(gpu).moves - before.moves >= (exported ? 10 : 1), true);
      u32 bytes = vramApart(software, hardware);
      if(bytes) std::printf("  %ux%s: %u bytes apart\n", scale, exported ? ", the stencil with the colors" : "", bytes);
      CHECK(bytes, 0u);
      hardware.ge.setRenderer(nullptr);
    }
  }
  gpu->backend->stencilExport = exports;
  gpu->resolution(1);
}

//Vertices read from VRAM the GPU drew over (two points whose vertices, through mode, an 8888 color and 16-bit
//positions, are the pixels of a sprite drawn just before, read in a row and by indices, those also past the PRIM's
//two vertices from the first, which are then in the page before, which nothing draws): the GPU's pixels put back
//first, so they're where the software renderer has them; likewise a display list there (AMBIENT_COLOR words whose
//low 24 bits the sprite draws, jumped to from words in the page it doesn't draw), and a list whose own sprite draws
//over its words further on. And pages given back to the CPU without a finish (as the drawing
//threads' settle gives every busy page back: GE::freeVRAM()) while the GPU has drawn in them: a sprite drawn again
//inside what it drew makes them its own again, busy (own()), so the CPU's read there finds its pixels.
static auto gpuVerticesDrawn() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Drawn = 0x1'0000, Indices = 0x0899'0000;  //(Drawn: VRAM offset)
  for(auto [indexed, first] : {std::pair{false, 0u}, std::pair{true, 0u}, std::pair{true, 24u}}) {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      for(u32 n = 0; n < 64; n++) s->memory.write(4, Memory::VRAMBase + Drawn + n * 4, 0x0008'0008);
      s->memory.write(1, Indices, first / 12 + 1), s->memory.write(1, Indices + 1, first / 12);  //(the sprite's two)
      prepare(*s, Drawn, 3);
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 8, 1}}, 0x0020'0010});
      prepare(*s, 0, 3);
      s->ge.commands[GE::VertexType] = 0x80'011c | (indexed ? 1 << 11 : 0);
      s->ge.vertexAddress = Memory::VRAMBase + Drawn - first, s->ge.indexAddress = Indices;
      s->ge.primitive(GE::Points, 2);
    }
    CHECK(apart(software, hardware), 0u);
    CHECK(software.memory.read(4, Memory::VRAMBase + (32 * 64 + 16) * 4), 0x0020'0010u);
    hardware.ge.setRenderer(nullptr);
  }
  {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {  //(it starts in row 0, which nothing draws, and jumps into row 1)
      u32 list = Memory::VRAMBase + Drawn, words = list + 64 * 4;
      s->memory.write(4, list, GE::Base << 24 | (words >> 8 & 0xf'0000));
      s->memory.write(4, list + 4, GE::Jump << 24 | (words & 0xff'ffff));
      for(u32 n = 0; n < 4; n++) s->memory.write(4, words + n * 4, GE::AmbientColor << 24);
      s->memory.write(4, words + 16, GE::Finish << 24), s->memory.write(4, words + 20, GE::End << 24);
      prepare(*s, Drawn, 3);
      sprite(*s, {{{0, 0, 0, 1}, {0, 0, 4, 2}}, 0xff00'ff40});
      s->ge.list = {}, s->ge.list.address = list;
      CHECK(s->ge.run(16) == GE::Stop::Finished, true);
    }
    CHECK(software.ge.commands[GE::AmbientColor], u32(GE::AmbientColor << 24 | 0x00'ff40));
    CHECK(hardware.ge.commands[GE::AmbientColor], software.ge.commands[GE::AmbientColor]);
    hardware.ge.setRenderer(nullptr);
  }
  {  //(a list whose own sprite draws over its words further on in the page, reached by a JUMP)
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      u32 list = Memory::VRAMBase + Drawn, at = list, vertex = GPUVertices;
      auto put = [&](u32 command, u32 argument) { s->memory.write(4, at, command << 24 | argument), at += 4; };
      for(auto [x, y, color] : {std::tuple{0.0f, 1.0f, 0u}, std::tuple{4.0f, 2.0f, 0xff00'ff40u}}) {
        for(float value : {0.0f, 0.0f}) s->memory.write(4, vertex, bitsOf(value)), vertex += 4;
        s->memory.write(4, vertex, color), vertex += 4;
        for(float value : {x, y, 0.0f}) s->memory.write(4, vertex, bitsOf(value)), vertex += 4;
      }
      prepare(*s, Drawn, 3);
      put(GE::Base, GPUVertices >> 8 & 0xf'0000), put(GE::VertexAddress, GPUVertices & 0xff'ffff);
      put(GE::Primitive, GE::Sprites << 16 | 2);
      put(GE::Base, (list + 64 * 4) >> 8 & 0xf'0000), put(GE::Jump, (list + 64 * 4) & 0xff'ffff);
      at = list + 64 * 4;  //(row 1, which the sprite draws)
      for(u32 n = 0; n < 4; n++) put(GE::AmbientColor, 0);
      put(GE::Finish, 0), put(GE::End, 0);
      s->ge.list = {}, s->ge.list.address = list;
      CHECK(s->ge.run(32) == GE::Stop::Finished, true);
    }
    CHECK(software.ge.commands[GE::AmbientColor], u32(GE::AmbientColor << 24 | 0x00'ff40));
    CHECK(hardware.ge.commands[GE::AmbientColor], software.ge.commands[GE::AmbientColor]);
    hardware.ge.setRenderer(nullptr);
  }
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030});
  }
  hardware.ge.freeVRAM();
  for(System* s : {&software, &hardware}) sprite(*s, {{{0, 0, 8, 8}, {0, 0, 24, 24}}, 0xff40'5060});
  u32 at = Memory::VRAMBase + (10 * 64 + 10) * 4;
  CHECK(hardware.memory.read(4, at), software.memory.read(4, at));
  CHECK(apart(software, hardware), 0u);
  hardware.ge.setRenderer(nullptr);
}

//Frame buffers side by side in the rows of a wide one, and one a row down, as God of War draws its effects (four 5650
//ones 64 pixels wide in rows of 256 here): parts of one target, drawn into in turn with nothing finished, dithered
//(each one's matrix from its own first row, its columns starting on a multiple of four: the fifth starts a row down), a
//sprite's texture stepped across each, and a texture read across the four, held on the GPU. Then one whose PRIM
//reaches past its target's rows' end, and one drawn with its depth buffer, each a target of its own. Everything as the
//software renderer has it.
static auto gpuSideBySide() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261017};
  constexpr u32 At = 0x3'8000;  //(VRAM's offset, on a page: rows of 512 bytes, 8 a page)
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  for(System* s : {&software, &hardware}) {
    for(u32 n = 0; n < 32 * 32; n++) s->memory.write(4, GPUTexture + n * 4, u32(n * 0x9e37'79b9));
  }
  auto flat = randomSprites(random, 30, false), textured = randomSprites(random, 30, true);
  auto draw = [&](System& s, u32 address, u32 first) {
    auto& c = s.ge.commands;
    prepare(s, address, 0);
    c[GE::FrameBufferWidth] = 256, c[GE::Scissor2] = 63 | 31 << 10;
    c[GE::DitherEnable] = 1, c[GE::Dither0] = 0x7f80, c[GE::Dither0 + 1] = 0x3c4d;
    c[GE::Dither0 + 2] = 0xe1a5, c[GE::Dither0 + 3] = 0x96b2;
    for(u32 n = first; n < first + 6; n++) sprite(s, flat[n]);
    texture(s, GPUTexture, 32, 32, 32);
    for(u32 n = first; n < first + 6; n++) sprite(s, textured[n]);
    c[GE::TextureMappingEnable] = 0, c[GE::DitherEnable] = 0;
  };
  auto before = stats(gpu);
  for(System* s : {&software, &hardware}) {
    for(u32 n = 0; n < 4; n++) draw(*s, At + n * 128, n * 6);
    draw(*s, At + 512 + 128, 24);
  }
  auto across = stats(gpu);
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0x2'0000, 3);
    texture(*s, Memory::VRAMBase + At, 256, 32, 256, 0);
    sprite(*s, {{{0, 0, 0, 0}, {256, 32, 64, 16}}, 0xffff'ffff});
  }
  CHECK(stats(gpu).finishes, before.finishes);
  CHECK(stats(gpu).textures, across.textures);  //(none decoded: held)
  CHECK(stats(gpu).copies > across.copies, true);
  for(System* s : {&software, &hardware}) {
    prepare(*s, At + 192 * 2, 0);
    s->ge.commands[GE::FrameBufferWidth] = 256, s->ge.commands[GE::Scissor2] = 127 | 31 << 10;
    sprite(*s, {{{0, 0, 40, 2}, {0, 0, 100, 9}}, 0xff20'40c0});  //(past the rows' end, at 256: run on into the next)
    prepare(*s, At + 128, 0);
    s->ge.commands[GE::FrameBufferWidth] = 256;
    s->ge.commands[GE::DepthTestEnable] = 1, s->ge.commands[GE::DepthTest] = 7;
    sprite(*s, {{{0, 0, 4, 4}, {0, 0, 30, 20}}, 0xff80'2010}, 30000);
    s->ge.commands[GE::DepthTestEnable] = 0;
  }
  u32 bytes = apart(software, hardware);  //(the frame buffers': the GPU's depth isn't put back)
  if(bytes) std::printf("  %u bytes apart\n", bytes);
  CHECK(bytes, 0u);
  hardware.ge.setRenderer(nullptr);
}

//A block transfer over pixels the GPU drew, after they were sampled (God of War streams palettes over the rows of a
//picture it has used): the pixels it writes whole are memory's at once (overwritten()), nothing finished; the CPU
//reads them as written, and the GE decodes a texture from them; a sprite drawn over some of them after draws over
//memory's bytes, and the CPU then finds its pixels drawn. A copy writing half of an 8888 pixel (2 bytes a pixel, from
//its middle) finishes first. And rows written over the GPU's pixels, then a finish before anything takes them: not put
//back over; and every pixel it drew in a page written over (so drawnOver() keeps the answer that it drew none there),
//then more drawn in the page: the CPU finds those. All as the software renderer has it.
static auto gpuTransferOver() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261018};
  constexpr u32 At = 0x3'0000;
  auto transfer = [](System& s, u32 to, u32 at, u32 width, u32 height, u32 bytes) {
    auto& c = s.ge.commands;
    c[GE::TransferSource] = GPUTexture & 0xff'fff0, c[GE::TransferSourceWidth] = (GPUTexture >> 24) << 16 | 64;
    c[GE::TransferSourcePosition] = 0;
    c[GE::TransferDestination] = to & 0xff'fff0;
    c[GE::TransferDestinationWidth] = (to >> 24) << 16 | (bytes == 4 ? 64 : 128);
    c[GE::TransferDestinationPosition] = at;
    c[GE::TransferSize] = (width - 1) | (height - 1) << 10, c[GE::TransferStart] = bytes == 4;
    s.ge.transfer();
  };
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  auto drawn = randomSprites(random, 30, false);
  u32 read[2] = {}, after[2] = {};
  for(System* s : {&software, &hardware}) {
    for(u32 n = 0; n < 64 * 64; n++) s->memory.write(4, GPUTexture + n * 4, 0xff00'0000 | u32(n * 0x01'0203));
    prepare(*s, At, 3);
    for(auto& one : drawn) sprite(*s, one);
    prepare(*s, 0x2'0000, 3);
    texture(*s, Memory::VRAMBase + At, 64, 64, 64);
    sprite(*s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
  }
  auto before = stats(gpu);
  for(System* s : {&software, &hardware}) {
    transfer(*s, Memory::VRAMBase + At, 8 << 10, 64, 8, 4);    //(rows 8-15, whole)
    transfer(*s, Memory::VRAMBase + At, 4 | 30 << 10, 20, 3, 4);  //(rows 30-32, columns 4-23)
    read[s == &hardware] = s->memory.read(4, Memory::VRAMBase + At + (10 * 64 + 20) * 4);
    prepare(*s, 0x2'4000, 3);  //(a texture of rows of 32 over the transferred rows: decoded from memory)
    texture(*s, Memory::VRAMBase + At + 8 * 256, 32, 16, 32);
    sprite(*s, {{{0, 0, 0, 0}, {32, 16, 32, 16}}, 0xffff'ffff});
    prepare(*s, At, 3);
    s->ge.commands[GE::TextureMappingEnable] = 0;
    sprite(*s, {{{0, 0, 10, 9}, {0, 0, 40, 13}}, 0xff35'79bd});
    after[s == &hardware] = s->memory.read(4, Memory::VRAMBase + At + (11 * 64 + 20) * 4);
  }
  CHECK(read[1], read[0]);
  CHECK(after[1], after[0]);
  CHECK(after[0], 0xff35'79bdu);  //(the stencil, the alpha, kept: the transfer's)
  CHECK(stats(gpu).finishes, before.finishes + 1);  //(the CPU's read of the sprite's pixel)
  before = stats(gpu);
  for(System* s : {&software, &hardware}) {
    prepare(*s, At, 3);
    for(u32 n = 0; n < 8; n++) sprite(*s, drawn[n]);
    transfer(*s, Memory::VRAMBase + At, 1 | 20 << 10, 16, 2, 2);  //(2-byte pixels from the middle of an 8888 one's)
  }
  CHECK(stats(gpu).finishes > before.finishes, true);
  for(System* s : {&software, &hardware}) {  //(rows written over the GPU's, then a finish before anything takes them)
    sprite(*s, {{{0, 0, 0, 36}, {0, 0, 64, 48}}, 0xff42'8642});
    transfer(*s, Memory::VRAMBase + At, 40 << 10, 64, 2, 4);
    s->memory.read(4, Memory::VRAMBase + At + 38 * 256);
  }
  u32 found[2] = {};
  for(System* s : {&software, &hardware}) {  //(every pixel it drew in a page written over, then more drawn there)
    prepare(*s, 0x3'8000, 3);
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 2}}, 0xff13'5713});
    transfer(*s, Memory::VRAMBase + 0x3'8000, 0, 64, 2, 4);
    s->memory.read(4, Memory::VRAMBase + 0x3'8000 + 8 * 256);  //(nothing it drew there: drawnOver()'s answer kept)
    sprite(*s, {{{0, 0, 0, 4}, {0, 0, 64, 6}}, 0xff24'6824});
    found[s == &hardware] = s->memory.read(4, Memory::VRAMBase + 0x3'8000 + 4 * 256 + 16);
  }
  CHECK(found[1], found[0]);
  u32 bytes = vramApart(software, hardware);
  if(bytes) std::printf("  %u bytes apart\n", bytes);
  CHECK(bytes, 0u);
  hardware.ge.setRenderer(nullptr);
}

//The CPU's stores over pixels the GPU drew (as Liberty City Stories writes a list over a picture it drew, which the
//GE then reads): words over two rows, and halfwords over a 5650 frame buffer's pixels, memory's at once
//(overwritten()), nothing finished, and read again (as the GE reads the list) without a finish either; a texture over
//them decoded from memory, and a sprite drawn across them after. A byte into an 8888 pixel is only part of it: that
//one finishes first. And rows stored over the GPU's, then a finish before anything takes them: not put back over; a
//store through VRAM's third copy (the same bytes) as through the first, a 17th range apart finishing, and one through
//its second copy (which rearranges them) finishing. As the software renderer has it each time.
static auto gpuStoresOver() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261021};
  constexpr u32 At = 0x3'0000;
  for(u32 format : {3u, 0u}) {
    u32 rowBytes = 64 * (format == 3 ? 4 : 2);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto drawn = randomSprites(random, 30, false);
    for(System* s : {&software, &hardware}) {
      prepare(*s, At, format);
      for(auto& one : drawn) sprite(*s, one);
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 24}}, 0xff10'2030});  //(rows 0-23 drawn whole)
    }
    auto before = stats(gpu);
    u32 read[2] = {}, after[2] = {};
    for(System* s : {&software, &hardware}) {
      for(u32 n = 0; n < 2 * rowBytes / 4; n++) {  //(rows 8 and 9)
        s->memory.write(4, Memory::VRAMBase + At + 8 * rowBytes + n * 4, 0xff00'0000 | n * 0x03'0507);
      }
      for(u32 n = 0; n < 20 && format == 0; n++) {  //(row 20, columns 4-23)
        s->memory.write(2, Memory::VRAMBase + At + 20 * rowBytes + (4 + n) * 2, n * 0x0321);
      }
      read[s == &hardware] = s->memory.read(4, Memory::VRAMBase + At + 9 * rowBytes + 12);
    }
    CHECK(read[1], read[0]);
    CHECK(stats(gpu).finishes, before.finishes);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0x2'0000, 3);  //(a texture over the rows stored: decoded from memory)
      texture(*s, Memory::VRAMBase + At + 8 * rowBytes, 32, 2, 64, format);
      sprite(*s, {{{0, 0, 0, 0}, {32, 2, 32, 2}}, 0xffff'ffff});
      prepare(*s, At, format);
      s->ge.commands[GE::TextureMappingEnable] = 0;
      sprite(*s, {{{0, 0, 10, 7}, {0, 0, 40, 21}}, 0xff35'79bd});
      after[s == &hardware] = s->memory.read(4, Memory::VRAMBase + At + 9 * rowBytes + 48 * 2);
    }
    CHECK(after[1], after[0]);
    CHECK(stats(gpu).finishes, before.finishes + 1);  //(the CPU's read of the sprite's pixels)
    if(format == 3) {
      for(System* s : {&software, &hardware}) {
        sprite(*s, {{{0, 0, 10, 28}, {0, 0, 40, 34}}, 0xff12'3456});
        before = stats(gpu);
        s->memory.write(1, Memory::VRAMBase + At + 30 * rowBytes + 20 * 4 + 1, 0x5a);
        if(s == &hardware) CHECK(stats(gpu).finishes, before.finishes + 1);
      }
    }
    for(System* s : {&software, &hardware}) {  //(rows stored over the GPU's, then a finish before anything takes them)
      sprite(*s, {{{0, 0, 0, 36}, {0, 0, 64, 48}}, 0xff42'8642});
      for(u32 n = 0; n < rowBytes / 4; n++) s->memory.write(4, Memory::VRAMBase + At + 40 * rowBytes + n * 4, n);
      s->memory.read(4, Memory::VRAMBase + At + 38 * rowBytes);
    }
    if(format == 3) {
      for(System* s : {&software, &hardware}) {
        sprite(*s, {{{0, 0, 0, 24}, {0, 0, 64, 34}}, 0xff22'4466});
        before = stats(gpu);
        s->memory.write(4, 0x0440'0000 + At + 26 * rowBytes + 8, 0xfeed'beefu);  //(VRAM's third copy: the same bytes)
        for(u32 n = 0; n < 16; n++) {  //(each a range of its own: the 17th a finish)
          s->memory.write(4, Memory::VRAMBase + At + 30 * rowBytes + n * 8, n);
          if(s == &hardware) CHECK(stats(gpu).finishes, before.finishes + (n == 15));
        }
        sprite(*s, {{{0, 0, 0, 24}, {0, 0, 64, 34}}, 0xff66'4422});
        before = stats(gpu);
        s->memory.write(4, 0x0420'0000 + At + 28 * rowBytes + 8, 0xdead'beefu);  //(its second copy rearranges them)
        if(s == &hardware) CHECK(stats(gpu).finishes, before.finishes + 1);
      }
    }
    u32 bytes = vramApart(software, hardware);
    if(bytes) std::printf("  format %u: %u bytes apart\n", format, bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
  }
}

//Bytes memory has the newest of over a target's pixels, then another target's newer pixels moved over them, the same
//bytes as another format (God of War's 5650 and 8888 over the same memory, among its streamed writes): the moved
//pixels are the newest, not memory's. An 8888 frame buffer drawn, a 5650 one over the same bytes drawn (the 8888's
//pixels in its pages moved into it), the CPU's word stored over both (dead in each), the 5650 drawn over the word;
//then the 8888 drawn in that page again, or taken as a texture from its first row (the 5650's pixels moved back into
//it), and the word's place read. The word written after a finish instead, over pixels the 8888 drew before it (its
//page's bytes memory's: beside), the 5650 drawn over it, then the 8888 taken as a texture. And bytes beside the
//8888's pixels its drawn rectangle grows over as the 5650's pixels below move into it, not put back over at the
//finish; and more than 16 such words apart (taken together, over a row it drew), then the 8888 taken as a texture
//across the 5650's page: not moved, finished. As the software renderer has it each time.
static auto gpuMovedOverMemorys() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 At = 0x3'0000, Word = 20 * 256 + 40;
  auto as = [](System& s, u32 format, u32 address) {  //(8888 in rows of 64 pixels, or 5650 in rows of 128)
    prepare(s, address, format);
    s.ge.commands[GE::FrameBufferWidth] = format == 3 ? 64 : 128;
    s.ge.commands[GE::TextureMappingEnable] = 0;
  };
  auto taken = [](System& s) {  //(the 8888 as a texture from its first row, 32 rows)
    prepare(s, 0x2'0000, 3);
    texture(s, Memory::VRAMBase + At, 64, 32, 64);
    sprite(s, {{{0, 0, 0, 0}, {64, 32, 64, 32}}, 0xffff'ffff});
  };
  for(u32 variant : {0u, 1u, 2u, 3u, 4u}) {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    u32 seen[2] = {}, read = variant < 3 ? Word : variant == 3 ? 256 + 80 : 2 * 256 + 16;
    for(System* s : {&software, &hardware}) {
      as(*s, 3, At);
      if(variant < 3) {
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030});
        if(variant == 2) {  //(a finish, so no page is the GPU's: the word's page written)
          s->memory.read(4, Memory::VRAMBase + At);
          s->memory.write(4, Memory::VRAMBase + At + Word, 0x1234'5678);
        }
        as(*s, 0, At);
        sprite(*s, {{{0, 0, 0, 16}, {0, 0, 64, 32}}, 0xff40'5060});
        if(variant < 2) {  //(the word stored over both: dead in each; the 5650 drawn over it)
          s->memory.write(4, Memory::VRAMBase + At + Word, 0x1234'5678);
          sprite(*s, {{{0, 0, 0, 18}, {0, 0, 64, 22}}, 0xff70'8090});
        }
        if(variant == 0) {
          as(*s, 3, At);
          sprite(*s, {{{0, 0, 0, 28}, {0, 0, 64, 30}}, 0xffa0'b0c0});
        } else {
          taken(*s);
        }
      } else {
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 8, variant == 3 ? 2.0f : 4.0f}}, 0xff10'2030});
        if(variant == 3) s->memory.write(4, Memory::VRAMBase + At + 256 + 80, 0x1357'9bdf);  //(row 1, column 20)
        for(u32 n = 0; n < 17 && variant == 4; n++) {  //(rows 1 and 3 from column 10, a pixel apart)
          u32 at = (1 + 2 * (n & 1)) * 256 + (10 + 2 * (n >> 1)) * 4;
          s->memory.write(4, Memory::VRAMBase + At + at, n * 0x0101'0101);
        }
        as(*s, 0, At + 0x1000);
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 4}}, 0xff40'5060});
        if(variant == 3) {
          as(*s, 3, At);
          sprite(*s, {{{0, 0, 0, 16}, {0, 0, 4, 18}}, 0xffa0'b0c0});
        } else {
          taken(*s);
        }
      }
      seen[s == &hardware] = s->memory.read(4, Memory::VRAMBase + At + read);
    }
    CHECK(seen[1], seen[0]);
    u32 bytes = vramApart(software, hardware);
    if(bytes) std::printf("  variant %u: %u bytes apart\n", variant, bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
  }
}

//Memory's bytes changed in a target's pages it hasn't drawn in since the last finish (the CPU's word): taken from
//memory again before a sprite draws over them (written(), revive()), nothing finished, and the pixels the sprite drew
//found by the CPU after. And more than 16 ranges of words beside pixels it has drawn in a page it owns: taken
//together, which may take in pixels the GPU drew between, so the target is filled afresh, what it drew put back
//first. As the software renderer has it either way.
static auto gpuRevived() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261019};
  constexpr u32 At = 0x3'0000;
  for(u32 ranges : {1u, 20u}) {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto drawn = randomSprites(random, 30, false);
    for(System* s : {&software, &hardware}) {
      prepare(*s, At, 3);
      for(auto& one : drawn) sprite(*s, one);
      s->ge.settleAll();
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 32, 4}}, 0xff10'2030});  //(rows 0-3, columns 0-31: page 0x30)
      if(ranges == 1) s->memory.write(4, Memory::VRAMBase + At + (40 * 64 + 20) * 4, 0xff12'3456);  //(page 0x32)
      for(u32 n = 0; n < ranges && ranges > 1; n++) {  //(beside the sprite, a word apart)
        s->memory.write(4, Memory::VRAMBase + At + ((n & 3) * 64 + 34 + n / 4 * 2) * 4, 0xff00'0000 | n * 0x10'1010);
      }
    }
    auto before = stats(gpu);
    u32 seen[2] = {};
    for(System* s : {&software, &hardware}) {
      sprite(*s, {{{0, 0, 8, 34}, {0, 0, 50, 46}}, 0xff60'a0e0});
      if(s == &hardware && ranges == 1) CHECK(stats(gpu).finishes, before.finishes);
      seen[s == &hardware] = s->memory.read(4, Memory::VRAMBase + At + (40 * 64 + 20) * 4);
    }
    CHECK(seen[1], seen[0]);
    u32 bytes = vramApart(software, hardware);
    if(bytes) std::printf("  %u ranges: %u bytes apart\n", ranges, bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
  }
}

//A frame buffer drawn, then taken as a texture from its top left by sprites drawn in tiles, each reaching further
//across or down than the ones before (as God of War draws the picture it shrank the screen into back over the screen,
//272 tiles a time): a copy of the place serves those that take as much of it or less, made again larger (to a power
//of two) where one takes more, so there are few copies; every tile's pixels are the software renderer's at 1x, 2x and
//3x. And at 2x and 3x, in machines afresh (the frame buffer drawn and nothing finished: the GPU's copies of it, at its
//resolution), filtered tiles: one drawn from a copy of only what it takes, and again after a larger tile, from that
//larger copy, held to what it takes (Push::held); and the larger tile, from the copy made larger for it (to a power of
//two), and alone, from a copy of its own: the GPU's pixels the same each time. (Its texels 1:1 at the copy's
//resolution, a tile samples none past what it takes, so a copy not held isn't caught here.)
static auto gpuTiles() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261020};
  for(u32 scale : {1u, 2u, 3u}) {
    if(!scaleTaken(gpu, scale)) break;
    gpu->resolution(scale);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto drawn = randomSprites(random, 40, false);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      for(auto& one : drawn) sprite(*s, one);
      prepare(*s, 0x2'0000, 3);
      texture(*s, Memory::VRAMBase, 64, 64, 64);
    }
    auto before = stats(gpu);
    for(u32 y = 0; y < 4; y++) {
      for(u32 x = 0; x < 6; x++) {  //(each tile 10 by 12, its own texels 1:1: the copy of the corner to its own)
        float left = x * 10, top = y * 12, right = left + 10, bottom = top + 12;
        for(System* s : {&software, &hardware}) {
          sprite(*s, {{{left, top, left, top}, {right, bottom, right, bottom}}, 0xffff'ffff});
        }
      }
    }
    u64 copies = stats(gpu).copies - before.copies;
    if(copies > 8) std::printf("  %ux: %llu copies for 24 tiles\n", scale, (unsigned long long)copies);
    CHECK(copies <= 8, true);
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %ux: %u bytes apart\n", scale, bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
    if(scale > 1) {
      //(each list of tiles in a machine afresh, its frame buffer drawn and nothing finished since, so the tiles take
      //the GPU's copies of it at its resolution; into the frame buffers at VRAM's offset at, texels 1:1)
      auto tiles = [&](std::vector<std::pair<u32, f32>> list) {
        gpu->resolution(scale);
        System machine;
        machine.ge.setRenderer(gpu);
        prepare(machine, 0, 3);
        for(auto& one : drawn) sprite(machine, one);
        auto made = stats(gpu).copies;
        for(auto [at, reach] : list) {
          prepare(machine, at, 3);
          texture(machine, Memory::VRAMBase + 0x2000, 64, 64, 64);
          machine.ge.commands[GE::TextureFilter] = 1 | 1 << 8;
          sprite(machine, {{{0, 0, 0, 0}, {reach, reach, reach, reach}}, 0xffff'ffff});
        }
        CHECK(stats(gpu).copies > made, true);
        std::vector<std::vector<u32>> pictures(list.size());
        for(u32 n = 0; n < list.size(); n++) {
          u32 size = list[n].second;
          CHECK(gpu->picture(list[n].first, 64, 3, size, size, pictures[n], scale), true);
        }
        machine.ge.setRenderer(nullptr);
        return pictures;
      };
      auto differ = [](const std::vector<u32>& a, const std::vector<u32>& b) {
        u32 count = a.size() != b.size();
        for(u32 n = 0; n < a.size() && n < b.size(); n++) count += a[n] != b[n];
        return count;
      };
      auto three = tiles({{0x2'8000, 20}, {0x2'c000, 40}, {0x3'0000, 20}}), alone = tiles({{0x2'c000, 40}});
      u32 off = differ(three[0], three[2]);
      if(off) std::printf("  %ux: %u of the GPU's pixels differ from the larger copy\n", scale, off);
      CHECK(off, 0u);
      off = differ(three[1], alone[0]);  //(the larger tile: from the copy made larger for it, and from its own)
      if(off) std::printf("  %ux: %u of the GPU's pixels differ from a copy made larger\n", scale, off);
      CHECK(off, 0u);
    }
  }
  gpu->resolution(1);
}

//A frame buffer taken as a texture whole by one sprite, then by sprites taking less of it, each its own columns and
//rows (Ghost of Sparta's menu draws 1,200 such PRIMs a frame from one copy): what each takes rides in its vertices
//(GPU::Vertex::taken()), not the push constants, so after the first they're all one draw, in either mode, and every
//pixel is the software renderer's. Then, filtered, at 1x and 2x, tiles side by side each shrinking a part of its own
//size onto the screen, their texels reaching the edge of what each takes (above 1x the GPU's pixels between two of the
//GE's sample past it, where the copy has more): one draw again, and each tile's pixels as when it's drawn alone, from
//a copy of only what it takes.
static auto gpuTakenInOneDraw() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(!gpu) continue;
    std::mt19937 random{20261014};
    auto drawn = randomSprites(random, 30, false);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    GPU::Statistics before, between;
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      for(auto& one : drawn) sprite(*s, one);
      prepare(*s, 0x2'0000, 3);
      texture(*s, Memory::VRAMBase, 64, 64, 64);
      if(s == &hardware) before = stats(gpu);
      sprite(*s, {{{0, 0, 0, 0}, {48, 40, 48, 40}}, 0xffff'ffff});  //(the copy: 48 columns, 40 rows)
      if(s == &hardware) between = stats(gpu);
      for(u32 n = 0; n < 6; n++) {
        float u = 3 * n, v = 2 * n, width = 8 + 5 * n, height = 6 + 3 * n, x = 7 * n, y = 40 - height;
        sprite(*s, {{{u, v, x, y}, {u + width, v + height, x + width, y + height}}, 0xffff'ffff});
      }
    }
    CHECK(between.copies - before.copies, 1u);
    CHECK(stats(gpu).copies, between.copies);
    CHECK(stats(gpu).draws, between.draws);
    CHECK(apart(software, hardware), 0u);
    hardware.ge.setRenderer(nullptr);

    //(tile n: 10 by 16 pixels at x 2 + 10n, its texels 2 to 14 + 4n across and 2 to 12 + 3n down, filtered: from 2, so
    //that the filter's first texels are inside, not repeated round from the far edge, which would take the whole)
    auto tile = [](System& s, u32 n) {
      float x = 2 + 10 * n, across = 14 + 4 * n, down = 12 + 3 * n;
      sprite(s, {{{2, 2, x, 20}, {across, down, x + 10, 36}}, 0xffff'ffff});
    };
    for(u32 scale : {1u, 2u}) {
      if(!scaleTaken(gpu, scale)) break;
      auto machine = [&](bool larger, s32 only, u64& draws) {
        gpu->resolution(scale);
        System s;
        s.ge.setRenderer(gpu);
        //(the frame buffer drawn from random texels, each unlike the next, so a texel past what a tile takes shows)
        std::mt19937 noise{20261015};
        for(u32 n = 0; n < 64 * 64; n++) s.memory.write(4, GPUTexture + n * 4, u32(noise()) | 0xff00'0000);
        prepare(s, 0, 3);
        texture(s, GPUTexture, 64, 64, 64);
        sprite(s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
        s.ge.commands[GE::TextureMappingEnable] = 0;
        prepare(s, 0x2'0000, 3);
        sprite(s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030});  //(all the picture's rows the GPU's)
        texture(s, Memory::VRAMBase, 64, 64, 64);
        s.ge.commands[GE::TextureFilter] = 1 | 1 << 8;
        if(larger) sprite(s, {{{0, 0, 0, 0}, {48, 40, 48, 40}}, 0xffff'ffff});
        u64 first = stats(gpu).draws;
        for(u32 n = 0; n < 6; n++) if(only < 0 || u32(only) == n) tile(s, n);
        draws = stats(gpu).draws - first;
        std::vector<u32> picture;
        CHECK(gpu->picture(0x2'0000, 64, 3, 64, 48, picture, scale), true);
        s.ge.setRenderer(nullptr);
        return picture;
      };
      u64 draws = 0, ignored = 0;
      auto together = machine(true, -1, draws);
      CHECK(draws, 0u);  //(the tiles join the larger sprite's draw)
      u32 off = 0;
      for(u32 n = 0; n < 6; n++) {
        auto alone = machine(false, n, ignored);
        bool sized = alone.size() == together.size() && alone.size() == 64 * 48 * scale * scale;
        for(u32 y = 20 * scale; y < 36 * scale && sized; y++) {
          for(u32 x = (2 + 10 * n) * scale; x < (12 + 10 * n) * scale; x++) {
            off += alone[y * 64 * scale + x] != together[y * 64 * scale + x];
          }
        }
      }
      if(off) std::printf("  %s, %ux: %u of the tiles' pixels differ from tiles drawn alone\n",
                          gpu->fast ? "fast" : "accurate", scale, off);
      CHECK(together.size(), u64(64 * 48 * scale * scale));
      CHECK(off, 0u);
    }
    gpu->resolution(1);
  }
}

//The GPU's depth buffer against memory's: depth cleared by the CPU between two depth-tested sprites is taken (the
//second sprite drawn), and a color pixel the CPU writes between them doesn't bring back memory's older depth (the
//second sprite, behind the first, not drawn).
static auto gpuDepth() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  for(bool clearing : {true, false}) {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0, 3);
      s->ge.commands[GE::DepthTestEnable] = 1, s->ge.commands[GE::DepthTest] = 7;  //(greater or equal)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff00'00ff}, 60000);
      if(clearing) {  //(through VRAM's fourth copy, as the GE sees its depth buffer)
        for(u32 n = 0; n < 64 * 48 * 2; n += 4) s->memory.write(4, 0x0460'0000 + 0x1f'0000 + n, 0);
      } else {
        s->memory.write(4, 0x0400'0000 + 47 * 64 * 4, 0xff12'3456);
      }
      sprite(*s, {{{0, 0, 8, 8}, {0, 0, 40, 40}}, 0xff00'ff00}, 30000);
    }
    CHECK(apart(software, hardware), 0u);
    u32 green = software.memory.vram[(20 * 64 + 20) * 4 + 1];
    CHECK(green, clearing ? 0xffu : 0u);
    hardware.ge.setRenderer(nullptr);
  }
}

//Memory's changes to the depth buffer between PRIMs that neither test nor write depth (Darkstalkers' CPU writes its
//sprites beside its depth buffer between its 2D PRIMs, which filled its rows on the GPU again each time): kept for
//the next PRIM that tests it, filled once then, and that PRIM draws as the software renderer does. (The CPU zeroes
//all of the depth buffer, a piece between each two PRIMs: a change refills all of each 16 KiB it touches from memory,
//where the GPU's own depth isn't.)
static auto gpuDepthUnused() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  u64 uploads = 0;
  for(System* s : {&software, &hardware}) {
    auto& c = s->ge.commands;
    prepare(*s, 0, 3);
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written)
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff00'00ff}, 50000);
    c[GE::DepthTestEnable] = 0;
    if(s == &hardware) uploads = stats(gpu).uploads;
    for(u32 n = 0; n < 24; n++) {  //(through the fourth copy: rows 2n and 2n + 1 of depth zeroed, then a PRIM)
      for(u32 x = 0; x < 2 * 64 * 2; x += 4) s->memory.write(4, 0x0460'0000 + 0x1f'0000 + n * 2 * 64 * 2 + x, 0);
      sprite(*s, {{{0, 0, n * 2.0f, 2}, {0, 0, n * 2.0f + 2, 6}}, 0xff00'0000 | n * 0x0b'0d0f});
    }
    if(s == &hardware) CHECK(stats(gpu).uploads, uploads);
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 6, c[GE::DepthMask] = 1;  //(greater, not written)
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff00'ff00}, 30000);
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
  }
  CHECK(stats(gpu).uploads - uploads >= 1, true);
  CHECK(apart(software, hardware), 0u);
  CHECK(u32(software.memory.vram[(30 * 64 + 50) * 4 + 1]), 0xffu);  //(its depth zeroed: drawn over)
  hardware.ge.setRenderer(nullptr);
}

//The depth as the GE's whole numbers (draw.frag's DEPTHS): 2D triangles a fraction of a step above a sprite's depth,
//their corners 1000.2 to 1000.8 over 1000, have the GE's depth, those rounded down, 1000 at every pixel, so each of
//the eight comparisons, each over a strip of its own, comes out as the GE's: equal, at most and at least draw them,
//less and greater don't (the GPU's own depth, a fraction above, would pass greater and fail equal). And a triangle
//writing such depths, over which the comparison of equal takes a sprite at 1000. In either mode.
static auto depthSteps(GPU* gpu) -> void {
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  for(System* s : {&software, &hardware}) {
    auto& c = s->ge.commands;
    prepare(*s, 0, 3);
    struct Corner { float x, y, z; };
    auto triangles = [&](float left, float right, u32 color) {
      Corner corners[6] = {{left, 0, 1000.2f}, {right, 0, 1000.5f}, {left, 48, 1000.6f},
                           {right, 0, 1000.5f}, {right, 48, 1000.8f}, {left, 48, 1000.6f}};
      u32 to = GPUVertices;
      for(auto& corner : corners) {
        for(float value : {0.0f, 0.0f}) s->memory.write(4, to, bitsOf(value)), to += 4;
        s->memory.write(4, to, color), to += 4;
        for(float value : {corner.x, corner.y, corner.z}) s->memory.write(4, to, bitsOf(value)), to += 4;
      }
      s->ge.vertexAddress = GPUVertices;
      s->ge.primitive(GE::Triangles, 6);
    };
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written)
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 40}}, 0xff10'2030}, 1000);
    c[GE::DepthMask] = 1;  //(not written: each strip's comparison against the sprite's)
    for(u32 n = 0; n < 8; n++) {
      c[GE::DepthTest] = n;
      triangles(n * 8.0f, n * 8.0f + 8, 0xff00'0000 | (n + 1) * 0x1f'2b37);
    }
    c[GE::DepthMask] = 0, c[GE::DepthTest] = 1;
    triangles(0, 64, 0xff40'4040);
    c[GE::DepthMask] = 1, c[GE::DepthTest] = 2;  //(equal: over the triangles' depths, 1000 where they're the GE's)
    sprite(*s, {{{0, 0, 8, 40}, {0, 0, 56, 48}}, 0xffc0'e0f0}, 1000);
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
  }
  u32 bytes = apart(software, hardware);
  if(bytes) std::printf("  %s: %u bytes apart\n", gpu->fast ? "fast" : "accurate", bytes);
  CHECK(bytes, 0u);
  CHECK(u32(software.memory.vram[(44 * 64 + 20) * 4]), 0xf0u);  //(the last sprite drawn)
  hardware.ge.setRenderer(nullptr);
}
static auto gpuDepthSteps() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) depthSteps(gpu);
  }
}

//A SPIR-V module's execution modes for the depth (OpExecutionMode's: DepthReplacing 12, DepthGreater 14, DepthLess 15,
//DepthUnchanged 16), size its bytes.
static auto depthModes(const u32* words, u32 size) -> std::vector<u32> {
  std::vector<u32> modes;
  for(u32 n = 5; n < size / 4 && words[n] >> 16; n += words[n] >> 16) {
    if((words[n] & 0xffff) == 16 && words[n] >> 16 >= 3 && words[n + 2] >= 12 && words[n + 2] <= 16) {
      modes.push_back(words[n + 2]);
    }
  }
  return modes;
}

//The depth a GPU may test before the shader runs (draw.frag's conservative depth). The builds that write the depth
//(DEPTHS) declare it never above the depth rasterized, which draw.vert and transform.vert put a step and a half above
//the GE's where the depth is clamped (depth_less); the builds that write none declare nothing; and the DEPTHS builds
//undeclared (GPU::undeclared(): for the pipelines the declaration's proof doesn't reach, gpuDepthBuilds) have the
//depth's DepthReplacing alone, one instruction fewer. And what a GPU may reject first is only what the test fails
//after, nor do the corners' depths rasterized move what's interpolated beside them: the same triangles drawn twice,
//the first time writing the depth, the second testing it with each comparison over a strip of its own, their depths
//across all of 0-65535 (the depth the shader writes from, interpolated beside the GPU's own, crosses whole numbers
//where the GPU's rounding may put its depth the other side of them): always, equal, at most and at least draw over
//every pixel drawn the first time, the others over none. In either mode, at 1x and 2x: 2D strips, against the
//software renderer, and 2D depths past 65535 (held there, by the GE as by the shader), at most, equal and at least
//over 65535, where the GPU clamps the depth (without, it clips by it); in 3D (fast mode: transform.vert's), a floor
//from the near plane to the far one, its depth from 36 to 65534 along it, and the depth range turned around (its far
//corners' depth below 1.5: on MoltenVK a corner's depth rasterized below 0 moves the depth interpolated beside it, so
//none is).
static auto depthEarly(GPU* gpu, bool past) -> void {
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  constexpr u32 First = 0xff30'6090;  //(the first drawing's color; the second's, each comparison's own)
  auto second = [](u32 n) { return 0xff00'0000 | (n + 1) * 0x1f'2b37; };
  auto passes = [](u32 comparison) { return comparison == 1 || comparison == 2 || comparison == 5 || comparison == 7; };
  constexpr u32 Overs[4] = {5, 2, 7, 4};  //(at most, equal, at least and less, over 65535)
  //(2D: a rectangle, two triangles, its corners' depths top left, top right, bottom left, bottom right)
  auto rectangle = [&](System& s, float left, float right, float top, float bottom, const float (&z)[4], u32 color) {
    float corners[6][3] = {{left, top, z[0]}, {right, top, z[1]}, {left, bottom, z[2]},
                           {right, top, z[1]}, {right, bottom, z[3]}, {left, bottom, z[2]}};
    u32 to = GPUVertices;
    for(auto& corner : corners) {
      for(float value : {0.0f, 0.0f}) s.memory.write(4, to, bitsOf(value)), to += 4;
      s.memory.write(4, to, color), to += 4;
      for(float value : corner) s.memory.write(4, to, bitsOf(value)), to += 4;
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Triangles, 6);
  };
  auto flat = [&](System& s, bool past) {
    auto& c = s.ge.commands;
    prepare(s, 0, 3);
    c[GE::DepthTestEnable] = 1;
    for(u32 pass = 0; pass < 2; pass++) {
      c[GE::DepthMask] = pass;  //(the first written, always; the second not)
      for(u32 n = 0; n < 8; n++) {
        c[GE::DepthTest] = pass ? n : 1;
        float z[4] = {0.25f + n * 0.37f, 65534.75f - n * 1.3f, 30000.5f + n * 977.1f, 1.6f + n * 5.9f};
        rectangle(s, n * 8.0f, n * 8.0f + 8, 0, 40, z, pass ? second(n) : First);
      }
      for(u32 n = 0; past && n < 4; n++) {
        c[GE::DepthTest] = pass ? Overs[n] : 1;
        float z[4] = {65535, 65535, 65535, 65535}, over[4] = {66000.5f, 90000, 70000.25f, 65536.75f};
        rectangle(s, n * 16.0f, n * 16.0f + 16, 40, 48, pass ? over : z, pass ? second(8 + Overs[n]) : First);
      }
    }
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
  };
  //(3D: vertex type 0x19c, a color then the position; the near plane 1 away, the far one 10; the floor 0.8 below the
  //eye from 1.0005 away to 9.999, four columns of two triangles, a mesh of 24 corners)
  auto floor = [&](System& s, bool turned, u32 color) {
    auto& c = s.ge.commands;
    c[GE::VertexType] = 0x19c, c[GE::MaxZ] = 0xffff;
    for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
    for(u32 n = 0; n < 16; n++) s.ge.projection[n] = 0;
    s.ge.projection[0] = s.ge.projection[5] = f24(1);
    s.ge.projection[10] = f24(-11.0f / 9), s.ge.projection[11] = f24(-1), s.ge.projection[14] = f24(-20.0f / 9);
    c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-24);
    c[GE::ViewportZScale] = f24(turned ? -32767.5f : 32767.5f);
    c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32767.5f);
    c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 24) << 4;
    u32 to = GPUVertices;
    for(u32 column = 0; column < 4; column++) {
      float x0 = -10 + column * 5.0f, x1 = x0 + 5, near = -1.0005f, far = -9.999f;
      float corners[6][3] = {{x0, -0.8f, near}, {x1, -0.8f, near}, {x1, -0.8f, far},
                             {x0, -0.8f, near}, {x1, -0.8f, far}, {x0, -0.8f, far}};
      for(auto& corner : corners) {
        s.memory.write(4, to, color), to += 4;
        for(float value : corner) s.memory.write(4, to, bitsOf(value)), to += 4;
      }
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Triangles, 24);
  };
  const char* mode = gpu->fast ? "fast" : "accurate";
  for(u32 scale : {1u, 2u}) {
    if(!scaleTaken(gpu, scale)) break;
    gpu->resolution(scale);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    flat(software, past), flat(hardware, past);
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %s %ux, 2D: %u bytes apart\n", mode, scale, bytes);
    CHECK(bytes, 0u);
    auto pixel = [&](u32 x, u32 y) {  //(the software renderer's, as the GE's comparisons have it)
      u32 value;
      std::memcpy(&value, &software.memory.vram[(y * 64 + x) * 4], 4);
      return value & 0xff'ffff;
    };
    for(u32 n = 0; n < 8; n++) CHECK(pixel(n * 8 + 4, 20), (passes(n) ? second(n) : First) & 0xff'ffff);
    for(u32 n = 0; past && n < 4; n++) {
      CHECK(pixel(n * 16 + 8, 44), (passes(Overs[n]) ? second(8 + Overs[n]) : First) & 0xff'ffff);
    }
    hardware.ge.setRenderer(nullptr);
    for(bool turned : {false, true}) {
      System s;
      s.ge.setRenderer(gpu);
      auto& c = s.ge.commands;
      prepare(s, 0, 3);
      c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;
      floor(s, turned, First);
      c[GE::DepthMask] = 1;
      for(u32 n = 0; n < 8; n++) {
        c[GE::Scissor1] = n * 8, c[GE::Scissor2] = (n * 8 + 7) | 47 << 10, c[GE::DepthTest] = n;
        floor(s, turned, second(n));
      }
      std::vector<u32> picture;
      CHECK(gpu->picture(0, 64, 3, 64, 48, picture, scale), true);
      s.ge.setRenderer(nullptr);
      u32 wrong = 0, drawn[8] = {}, strips[8] = {};
      for(u32 y = 0; y < 48 * scale && picture.size() == 64 * 48 * scale * scale; y++) {
        for(u32 x = 0; x < 64 * scale; x++) {
          u32 n = x / scale / 8, color = picture[y * 64 * scale + x] & 0xff'ffff;
          if(color == 0) continue;
          drawn[n]++;
          strips[n] += color != ((passes(n) ? second(n) : First) & 0xff'ffff);
        }
      }
      for(u32 n = 0; n < 8; n++) {
        wrong += strips[n];
        const char* range = turned ? ", the depth range turned" : "";
        if(strips[n]) std::printf("  %s %ux, 3D%s, comparison %u: %u of %u pixels the wrong drawing's\n", mode, scale,
                                  range, n, strips[n], drawn[n]);
      }
      CHECK(picture.size(), u64(64 * 48 * scale * scale));
      CHECK(wrong, 0u);
      for(u32 n = 0; n < 8; n++) CHECK(drawn[n] > 8 * 10 * scale * scale, true);
    }
  }
  gpu->resolution(1);
}
static auto gpuDepthEarly() -> void {
  namespace Shaders = ares::PlayStationPortable::GPUShaders;
  struct Build { const char* name; const u32* words; u32 size; std::vector<u32> modes; };
  Build builds[] = {
    {"draw.frag", Shaders::fragmentSPIRV, sizeof(Shaders::fragmentSPIRV), {}},
    {"SINGLE", Shaders::fragmentSingleSPIRV, sizeof(Shaders::fragmentSingleSPIRV), {}},
    {"DEPTHS", Shaders::fragmentDepthSPIRV, sizeof(Shaders::fragmentDepthSPIRV), {12, 15}},
    {"SINGLE DEPTHS", Shaders::fragmentSingleDepthSPIRV, sizeof(Shaders::fragmentSingleDepthSPIRV), {12, 15}},
  };
  for(auto& build : builds) {
    auto modes = depthModes(build.words, build.size);
    std::sort(modes.begin(), modes.end());
    if(modes != build.modes) std::printf("  %s: the depth's execution modes aren't as declared\n", build.name);
    CHECK(modes == build.modes, true);
    if(build.modes.empty()) continue;
    auto undeclared = GPU::undeclared(build.words, build.size);
    CHECK(depthModes(undeclared.data(), undeclared.size() * 4) == std::vector<u32>{12}, true);
    CHECK(u32(undeclared.size() * 4 + 12), build.size);
  }
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(!gpu) continue;
    bool past = gpu->backend->depthClamp;
    if(!past) skip("vulkan-depth-clamp", "the GPU clips by the depth: 2D depths past 65535 not tried");
    depthEarly(gpu, past);
  }
}

//2D PRIMs whose corners' depths lie past the GE's 0-65535 (through mode's float positions), where the depth decides:
//there the GPU's two interpolations, of the depth rasterized and of the one written beside it, may part by more than
//the declaration's margin (a float's own step is a whole one at ten million), so their pipelines are made
//undeclared, the depth rasterized at the GE's (Pipeline::outside), and only theirs. A pretend backend sees the keys of
//the quad below, tested and written; one in range; a quad past the range below 0 alone (its second triangle's first
//corner in range); one in range again; a triangle whose first corner is in range and another past 65535; the quad
//neither tested nor written; then fast mode's 3D mesh, held in range: every corner weighs, on either side of the
//range. On the GPU, where it clamps the depth, as gpuDepthEarly's: the quad, its corners at -10 million, 10 million,
//5 million and -5 million, moved for each strip (the "at least" strip's at -9.9 million, 9.8 million, 5.3 million and
//-5.3 million, of which MoltenVK lost 1 of 384 pixels at 1x and 2 of 1,536 at 2x to the declaration), drawn twice,
//written always then tested with each comparison over a strip of its own, at 1x-5x, in either mode.
static auto gpuDepthOutside() -> void {
  constexpr u32 First = 0xff30'6090;
  auto second = [](u32 n) { return 0xff00'0000 | (n + 1) * 0x1f'2b37; };
  auto passes = [](u32 comparison) { return comparison == 1 || comparison == 2 || comparison == 5 || comparison == 7; };
  auto quad = [](System& s, float left, const float (&z)[4], u32 color) {  //(8 pixels across, 48 down)
    float corners[6][3] = {{left, 0, z[0]}, {left + 8, 0, z[1]}, {left, 48, z[2]},
                           {left + 8, 0, z[1]}, {left + 8, 48, z[3]}, {left, 48, z[2]}};
    u32 to = GPUVertices;
    for(auto& corner : corners) {
      for(float value : {0.0f, 0.0f}) s.memory.write(4, to, bitsOf(value)), to += 4;
      s.memory.write(4, to, color), to += 4;
      for(float value : corner) s.memory.write(4, to, bitsOf(value)), to += 4;
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Triangles, 6);
  };
  auto triangle = [](System& s, float left, const float (&z)[3], u32 color) {  //(top left, top right, bottom left)
    float corners[3][3] = {{left, 0, z[0]}, {left + 8, 0, z[1]}, {left, 48, z[2]}};
    u32 to = GPUVertices;
    for(auto& corner : corners) {
      for(float value : {0.0f, 0.0f}) s.memory.write(4, to, bitsOf(value)), to += 4;
      s.memory.write(4, to, color), to += 4;
      for(float value : corner) s.memory.write(4, to, bitsOf(value)), to += 4;
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Triangles, 3);
  };
  auto far = [](u32 n, float (&z)[4]) {  //(strip n's corners: top left, top right, bottom left, bottom right)
    constexpr float Corners[4] = {-1.0e7f, 1.0e7f, 5.0e6f, -5.0e6f};
    for(u32 i = 0; i < 4; i++) z[i] = Corners[i] + n * 12345.6f * (i + 1) * (i & 1 ? -1 : 1);
  };
  {
    auto backend = std::make_unique<RecordingBackend>();
    auto& pretend = *backend;
    pretend.transforms = true;
    GPU gpu(std::move(backend));
    gpu.fast = true;
    System s;
    s.ge.setRenderer(&gpu);
    prepare(s, 0, 3);
    auto& c = s.ge.commands;
    float z[4], near[4] = {0.25f, 65534.75f, 30000.5f, 1.6f}, below[4] = {-1.0e7f, 30000, 60000, -5.0e6f};
    float some[3] = {1000, 70000, 2000};
    far(7, z);
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 7;  //(at least, written)
    quad(s, 0, z, First), quad(s, 8, near, First);
    quad(s, 24, below, First), quad(s, 32, near, First);
    triangle(s, 40, some, First);
    c[GE::DepthTestEnable] = 0;
    quad(s, 16, z, First);
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;
    quads3D(s, 3);
    s.ge.settleAll();
    std::string keys;  //(each draw's kind, Pipeline::outside and whether its depth decides)
    for(auto& [kind, state] : pretend.drawn) {
      keys += kind == GPU::Command::Kind::Mesh ? 'M' : 'D';
      keys += char('0' + state.pipeline.outside), keys += state.pipeline.depths() ? '+' : '-';
      keys += ' ';
    }
    if(keys != "D1+ D0+ D1+ D0+ D1+ D0- M0+ ") std::printf("  the draws' keys: %s\n", keys.c_str());
    CHECK(keys == "D1+ D0+ D1+ D0+ D1+ D0- M0+ ", true);
    s.ge.setRenderer(nullptr);
  }
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(!gpu || !gpu->backend->depthClamp) continue;  //(without, the quad clips: gpuDepthEarly says so)
    const char* mode = gpu->fast ? "fast" : "accurate";
    for(u32 scale : {1u, 2u, 3u, 4u, 5u}) {
      if(!scaleTaken(gpu, scale)) break;
      gpu->resolution(scale);
      System s;
      s.ge.setRenderer(gpu);
      auto& c = s.ge.commands;
      prepare(s, 0, 3);
      c[GE::DepthTestEnable] = 1;
      for(u32 pass = 0; pass < 2; pass++) {
        c[GE::DepthMask] = pass;  //(the first written, always; the second not)
        for(u32 n = 0; n < 8; n++) {
          float z[4];
          far(n, z);
          c[GE::DepthTest] = pass ? n : 1;
          quad(s, n * 8.0f, z, pass ? second(n) : First);
        }
      }
      c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
      std::vector<u32> picture;
      CHECK(gpu->picture(0, 64, 3, 64, 48, picture, scale), true);
      s.ge.setRenderer(nullptr);
      u32 wrong = 0, drawn[8] = {}, strips[8] = {};
      for(u32 y = 0; y < 48 * scale && picture.size() == 64 * 48 * scale * scale; y++) {
        for(u32 x = 0; x < 64 * scale; x++) {
          u32 n = x / scale / 8, color = picture[y * 64 * scale + x] & 0xff'ffff;
          if(color == 0) continue;
          drawn[n]++;
          strips[n] += color != ((passes(n) ? second(n) : First) & 0xff'ffff);
        }
      }
      for(u32 n = 0; n < 8; n++) {
        wrong += strips[n];
        if(strips[n]) std::printf("  %s %ux, comparison %u: %u of %u pixels the wrong drawing's\n", mode, scale, n,
                                  strips[n], drawn[n]);
      }
      CHECK(picture.size(), u64(64 * 48 * scale * scale));
      CHECK(wrong, 0u);
      for(u32 n = 0; n < 8; n++) CHECK(drawn[n], 8 * 48 * scale * scale);
    }
    gpu->resolution(1);
  }
}

//How the pipelines whose depth decides are made (Backend::depthBuild()): with the declaration and the margin its proof
//needs (draw.vert's ABOVE; a pipeline with the one but not the other, for any comparison, is wrong), the two together
//for every comparison, written or not, in 2D and in fast mode's 3D, where the device clamps the depth and the corners'
//depths are the GE's; neither for a 2D PRIM's past them (Pipeline::outside), nor any where the device doesn't clamp:
//renderers of their own, accurate and fast, their depth clamping turned off before they make a pipeline (as a device
//without it makes them), which then draw gpuDepthEarly's 2D strips and 3D floor as the GE does.
static auto gpuDepthBuilds() -> void {
  if(!renderer()) return;
  auto builds = [](GPU* gpu) {
    GPU::Pipeline k{};
    k.alphaTest = 8, k.colorTest = 4, k.quantize = 4, k.logic = 3, k.texels = 4, k.colorMask = 15;
    u32 wrong = 0;
    for(u8 transformed : {0, 1}) for(u8 outside : {0, 1}) for(u8 written : {0, 1}) for(u8 n = 0; n < 8; n++) {
      if(transformed && !gpu->backend->transforms) continue;
      k.transformed = transformed, k.outside = outside, k.depthTest = 1, k.depthCompare = n, k.depthWrite = written;
      u32 expected = k.depths() && gpu->backend->depthClamp && !outside ? 3 : 0, made = gpu->backend->depthBuild(k);
      if(made != expected && wrong++ < 4) {
        std::printf("  %s%s%s, comparison %u%s: made %u, not %u\n", gpu->fast ? "fast" : "accurate",
                    transformed ? ", 3D" : "", outside ? ", outside" : "", n, written ? ", written" : "", made,
                    expected);
      }
    }
    return wrong;
  };
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) CHECK(builds(gpu), 0u);
  }
  for(bool fast : {false, true}) {
    std::string error;
    auto plain = GPU::vulkan(nullptr, error, fast);
    CHECK(bool(plain), true);
    if(!plain) continue;
    plain->backend->depthClamp = false;
    CHECK(builds(plain.get()), 0u);
    depthEarly(plain.get(), false);
  }
}

//Loads through VRAM's second and fourth copies, the views the GE keeps its depth buffer in, of pixels the GPU drew
//(WipEout reads its depth buffer where its bloom's buffers are): memory's bytes, without waiting, as the GPU's depth
//isn't memory's to finish for; through the first copy another of the sprite's pixels finishes and finds the GPU's
//pixels, as the software renderer has them; and the depth views find them too once they're memory's again. A store
//through the fourth copy still finishes first, as it did.
static auto gpuDepthViewLoads() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 At = 0x3'0000;
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  for(System* s : {&software, &hardware}) {
    for(u32 n = 0; n < 64 * 48 * 4; n += 4) s->memory.write(4, Memory::VRAMBase + At + n, 0x0102'0304 * (n / 4 + 1));
    prepare(*s, At, 3);
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030});
  }
  auto pixel = [](System& s, u32 offset) { return s.memory.read(4, Memory::VRAMBase + offset); };
  constexpr u32 Pixel = At + 20 * 256 + 8 * 4;  //(row 20, column 8)
  u32 memorys = 0x0102'0304 * ((Pixel - At) / 4 + 1), drawn = (memorys & 0xff00'0000) | 0x10'2030;  //(the stencil kept)
  for(u32 copy : {1u, 3u}) {
    u32 seen = Memory::VRAMBase + copy * Memory::VRAMSize + Memory::vramSeen(copy, Pixel);
    auto before = stats(gpu);
    CHECK(hardware.memory.read(4, seen), memorys);
    CHECK(stats(gpu).finishes, before.finishes);
    CHECK(software.memory.read(4, seen), drawn);
  }
  auto before = stats(gpu);
  CHECK(pixel(hardware, At + 30 * 256 + 40), pixel(software, At + 30 * 256 + 40));
  CHECK(stats(gpu).finishes, before.finishes + 1);
  u32 seen = Memory::vramSeen(3, Pixel);
  CHECK(hardware.memory.read(4, 0x0460'0000 + seen), drawn);
  for(System* s : {&software, &hardware}) {
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff40'5060});
    before = stats(gpu);
    s->memory.write(2, 0x0460'0000 + seen, 0x7788);
    if(s == &hardware) CHECK(stats(gpu).finishes, before.finishes + 1);
  }
  CHECK(apart(software, hardware), 0u);
  hardware.ge.setRenderer(nullptr);
}

//A depth buffer two frame buffers draw with (Hammerin' Hero and Jeanne d'Arc draw their scene into the screen's,
//then their characters into a frame buffer of their own, cleared without its depth, tested against the scene's): the
//depth either drew is the other's next, as memory's is on the PSP, where each frame buffer's depth is its own on the
//GPU (taken across on the GPU), and each keeps its own stencil. The second frame buffer's sprite, behind what the
//first drew, is drawn only where the first drew nothing; then the first's, behind what the second drew since,
//likewise; then a 3D mesh into the second (in fast mode the GPU's transform), behind what the first drew since. At 1x
//and 2x, in either mode. Where the device's copy of the depth alone takes the stencil too (vulkan.cpp's probe:
//Backend::depthAlone off, the depth through the staging buffer), that copy forced once more at 1x: the second frame
//buffer's stencil is then the first's, as the probe found. Wherever the copy is made, the depth's aspect alone named.
static auto sharedDepth(GPU* gpu) -> void {
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  bool alone = gpu->backend->depthAlone;
  for(u32 scale : {1u, 2u, 0u}) {
    bool forced = scale == 0;
    if(forced && alone) break;
    if(forced) scale = 1, gpu->backend->depthAlone = true;
    gpu->resolution(scale);
    gpu->backend->depthCopyAspects = 0;
    auto before = stats(gpu);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      auto& c = s->ge.commands;
      prepare(*s, 0, 3);
      c[GE::ClearMode] = 1 | 7 << 8;  //(its colors, stencil and depth)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xa520'2020});
      c[GE::ClearMode] = 0, c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0xff00'00ff}, 50000);
      c[GE::FrameBufferPointer] = 0x1'0000;
      c[GE::ClearMode] = 1 | 3 << 8;  //(its colors and stencil, not its depth)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0x5a40'4040});
      c[GE::ClearMode] = 0, c[GE::DepthTest] = 7;  //(greater or equal, written)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff00'ff00}, 30000);
      c[GE::DepthTest] = 1;
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 16}}, 0xffff'0000}, 60000);
      c[GE::FrameBufferPointer] = 0, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;  //(not written)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff00'ffff}, 55000);
      c[GE::DepthTest] = 1, c[GE::DepthMask] = 0;
      sprite(*s, {{{0, 0, 32, 24}, {0, 0, 64, 48}}, 0xff80'8080}, 58000);
      //(the mesh: 4 by 2 quads over the second's right half at depth 57000, seen orthographically, its pixels' middles
      //at the GE's)
      c[GE::FrameBufferPointer] = 0x1'0000, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;
      c[GE::VertexType] = 0x1ff, c[GE::MaxZ] = 0xffff;
      for(u32 n = 0; n < 12; n++) s->ge.world[n] = s->ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
      for(u32 n = 0; n < 16; n++) s->ge.projection[n] = f24(n % 5 == 0 ? 1 : 0);
      c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-24), c[GE::ViewportZScale] = f24(30000);
      c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
      c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 24) << 4;
      u32 to = GPUVertices, count = 0;
      for(u32 j = 0; j < 2; j++) {
        for(u32 i = 0; i < 4; i++) {
          for(u32 corner : {0u, 1u, 3u, 0u, 3u, 2u}) {
            float x = 0.25f * (i + (corner & 1)), y = -1 + 1.0f * (j + (corner >> 1)), z = (57000 - 32768) / 30000.0f;
            for(u32 word : {0u, 0u, 0xff60'30c0u, 0u, 0u, bitsOf(1), bitsOf(x), bitsOf(y), bitsOf(z)}) {
              s->memory.write(4, to, word), to += 4;
            }
            count++;
          }
        }
      }
      s->ge.vertexAddress = GPUVertices;
      s->ge.primitive(GE::Triangles, count);
      c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
    }
    if(gpu->backend->depthAlone) hardware.ge.settleAll(), CHECK(gpu->backend->depthCopyAspects, 2u);
    if(forced) {
      CHECK(u32(hardware.memory.vram[0x1'0000 + (30 * 64 + 8) * 4 + 3]), 0xa5u);
      gpu->backend->depthAlone = false;
      hardware.ge.setRenderer(nullptr);
      continue;
    }
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %s %ux: %u bytes apart\n", gpu->fast ? "fast" : "accurate", scale, bytes);
    CHECK(bytes, 0u);
    auto byte = [&](u32 buffer, u32 x, u32 y, u32 n) {
      return u32(software.memory.vram[buffer + (y * 64 + x) * 4 + n]);
    };
    CHECK(byte(0x1'0000, 8, 30, 1), 0x40u), CHECK(byte(0x1'0000, 40, 30, 1), 0xffu);  //(behind the first's, and not)
    CHECK(byte(0, 8, 8, 1), 0u), CHECK(byte(0, 8, 30, 1), 0xffu);  //(behind the second's, and not)
    CHECK(byte(0x1'0000, 40, 20, 1), 0x30u);  //(the mesh, in front of all but what the first drew since)
    CHECK(byte(0x1'0000, 8, 30, 3), 0x5au), CHECK(byte(0, 8, 8, 3), 0xa5u);  //(each its own stencil)
    CHECK(stats(gpu).depthsTaken - before.depthsTaken >= 3, true);
    if(gpu->fast && gpu->backend->transforms) CHECK(stats(gpu).meshes > before.meshes, true);
    hardware.ge.setRenderer(nullptr);
  }
  gpu->resolution(1);
}
static auto gpuSharedDepth() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) sharedDepth(gpu);
  }
}

//The ways other frame buffers' pixels in a depth buffer's bytes become the depth a PRIM is filled with (GPU::
//moveIntoDepth()), to try: moved into it on the GPU, where the backend can (Backend::depthMoves, which the tests turn
//off and on), and put in memory first, as a GPU that can't has them (a finish); and either's name.
static auto depthWays(GPU* gpu) -> std::vector<bool> {
  if(gpu->backend->depthMoves) return {true, false};
  skip("vulkan-moves", "the GPU doesn't move pixels into a depth: only memory's way tested");
  return {false};
}
static auto way(bool moves) -> const char* { return moves ? "moved on the GPU" : "put in memory first"; }
struct DepthMovesKept {  //(Backend::depthMoves as it was, back as it goes)
  GPU* gpu;
  bool was = gpu->backend->depthMoves;
  ~DepthMovesKept() { gpu->backend->depthMoves = was; }
};

//R22 (DESIGN.md 3.7; the review's round four): a frame buffer drawn on the GPU in its rows 0-15 and 32-47, the page of
//its rows between reached by no rectangle; a block transfer's row running from its row 15 onto that page (overwritten()
//marks the bytes dead, its pixels memory's from then on); then another frame buffer clearing its depth, whose depth
//buffer's 16 KiB holds the first one's top page but not the page between: the depth's fill moves the first one's
//pixels in on the GPU, and first takes its dead bytes from memory again (moveIntoDepth()'s revive()), reading the page
//between, outside the second's rows and its depth buffer's 16 KiBs. With the GE on its thread held back, the CPU
//stores over those bytes after the list: the render GE must read them as they were (the store's version), and its
//read must lie in what the model declares the PRIM may read (the G-interest check: only R22's rule, the rows of the
//targets with pages in the depth buffer's 16 KiBs, holds that page). Memory comes out as the same list's without the
//thread, on the same GPU; with the depth moves on, and off (nothing moved, nothing revived there).
static auto gpuThreadR22() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 List = 0x0894'0000, Row = 0x0897'0000;
  constexpr u32 Source = 0x10'3000, Between = 0x10'4000, Depth = 0x10'0000;  //(VRAM's offsets)
  auto build = [](Memory& memory) {
    u32 at = List, vertex = GPUVertices;
    auto put = [&](u32 command, u32 argument = 0) {
      memory.write(4, at, command << 24 | (argument & 0xff'ffff)), at += 4;
    };
    auto sprite = [&](float left, float top, float right, float bottom, u32 color) {
      put(GE::Base, vertex >> 8 & 0xf'0000), put(GE::VertexAddress, vertex);
      for(auto [x, y] : {std::pair{left, top}, std::pair{right, bottom}}) {
        for(float value : {0.0f, 0.0f}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
        memory.write(4, vertex, color), vertex += 4;
        for(float value : {x, y, 0.0f}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
      }
      put(GE::Primitive, GE::Sprites << 16 | 2);
    };
    put(GE::Scissor1, 0), put(GE::Scissor2, 63 | 47 << 10), put(GE::Region2, 1023 | 1023 << 10);
    put(GE::VertexType, 0x80'019f), put(GE::ShadeMode, 1);
    put(GE::FrameBufferPointer, Source), put(GE::FrameBufferWidth, 64), put(GE::FrameBufferPixelFormat, 3);
    sprite(0, 0, 64, 16, 0xff20'4060);
    sprite(0, 32, 64, 48, 0xff60'4020);
    put(GE::TransferSource, Row), put(GE::TransferSourceWidth, (Row >> 24) << 16 | 128);
    put(GE::TransferSourcePosition, 0);
    put(GE::TransferDestination, Memory::VRAMBase + Source + 15 * 256);
    put(GE::TransferDestinationWidth, (Memory::VRAMBase >> 24) << 16 | 128), put(GE::TransferDestinationPosition, 0);
    put(GE::TransferSize, 127), put(GE::TransferStart, 1);  //(one row of 128 four-byte pixels: rows 15 and 16)
    put(GE::FrameBufferPointer, 0), put(GE::FrameBufferWidth, 64);
    put(GE::DepthBufferPointer, Depth), put(GE::DepthBufferWidth, 64);
    put(GE::ClearMode, 0x701);
    sprite(0, 0, 64, 48, 0xff00'0000);
    put(GE::ClearMode, 0);
    put(GE::Finish), put(GE::End);
    for(u32 n = 0; n < 128; n++) memory.write(4, Row + n * 4, 0xff00'0000 | n * 0x01'0101);
    for(u32 n = 0; n < 0x1000; n += 4) memory.write(4, Memory::VRAMBase + Between + n, 0xff55'aa00 | n >> 2);
  };
  constexpr u32 Stored = Memory::VRAMBase + Between + 0x40;  //(row 16, column 16: the transfer's, dead)
  for(bool moves : depthWays(gpu)) {
    DepthMovesKept kept{gpu};
    gpu->backend->depthMoves = moves;
    System plain, threaded;
    plain.threadGE(false), threaded.threadGE(true);
    plain.ge.setRenderer(gpu);
    build(plain.memory);
    plain.ge.list = {}, plain.ge.list.address = List;
    CHECK(u32(plain.ge.run(1000)), u32(GE::Stop::Finished));
    plain.memory.write(4, Stored, 0x0a0b'0c0d);
    plain.ge.settleAll();
    plain.ge.setRenderer(nullptr);
    u64 moved = stats(gpu).depthsMoved;
    threaded.ge.setRenderer(gpu);
    build(threaded.memory);
    std::atomic<bool> held{true};
    threaded.geThreading->drain();  //(the thread reads its gate as it takes each record)
    threaded.geThreading->gate = [&](u64) { while(held.load()) std::this_thread::yield(); };
    u64 waits = threaded.geThreading->waits;
    threaded.ge.list = {}, threaded.ge.list.address = List;
    CHECK(u32(threaded.ge.run(1000)), u32(GE::Stop::Finished));
    threaded.memory.write(4, Stored, 0x0a0b'0c0d);
    CHECK(held.load(), true);  //(nothing waited for the thread)
    CHECK(threaded.geThreading->waits, waits);
    held = false;
    threaded.geThreading->drain();
    threaded.geThreading->gate = nullptr;
    threaded.ge.settleAll();
    threaded.ge.setRenderer(nullptr);
    if(moves) CHECK(stats(gpu).depthsMoved > moved, true);  //(the source's pixels moved into the depth)
    u32 apart = vramApart(plain, threaded);
    if(apart) std::printf("  %s: %u bytes apart\n", way(moves), apart);
    CHECK(apart, 0u);
    CHECK(plain.memory.read(4, Stored), 0x0a0b'0c0du);
  }
}

//A list for the tests with the GE on its thread (outside a list a PRIM is drawn at once, the thread drained: a list
//leaves it behind), from at on: commands one after another, sprites' vertices from GPUVertices on as sprite() writes
//them, and texture()'s and prepare()'s state as those set it.
struct ThreadedList {
  Memory& memory;
  u32 at = 0x0894'0000, vertex = GPUVertices;
  auto put(u32 command, u32 argument = 0) -> void {
    memory.write(4, at, command << 24 | (argument & 0xff'ffff)), at += 4;
  }
  auto sprite(const Corners& sprite, float z = 0) -> void {
    put(GE::Base, vertex >> 8 & 0xf'0000), put(GE::VertexAddress, vertex);
    for(auto& corner : sprite.at) {
      for(float value : {corner[0], corner[1]}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
      memory.write(4, vertex, sprite.color), vertex += 4;
      for(float value : {corner[2], corner[3], z}) memory.write(4, vertex, bitsOf(value)), vertex += 4;
    }
    put(GE::Primitive, GE::Sprites << 16 | 2);
  }
  auto texture(u32 address, u32 width, u32 height, u32 bufferWidth, u32 format = 3) -> void {
    put(GE::TextureMappingEnable, 1);
    put(GE::TextureAddress0, address), put(GE::TextureBufferWidth0, (address >> 24 & 0xf) << 16 | bufferWidth);
    put(GE::TextureSize0, std::countr_zero(height) << 8 | std::countr_zero(width));
    put(GE::TextureFormat, format), put(GE::TextureFunction, 3 | 1 << 8), put(GE::TextureFilter, 0);
  }
  auto prepare(u32 buffer, u32 format, u32 rows = 48) -> void {
    put(GE::FrameBufferPointer, buffer), put(GE::FrameBufferWidth, 64), put(GE::FrameBufferPixelFormat, format);
    put(GE::DepthBufferPointer, 0x1f'0000), put(GE::DepthBufferWidth, 64);
    put(GE::Region2, 1023 << 10 | 1023), put(GE::Scissor1, 0), put(GE::Scissor2, 63 | (rows - 1) << 10);
    put(GE::VertexType, 0x80'019f), put(GE::ShadeMode, 1);
  }
  auto end() -> void { put(GE::Finish), put(GE::End); }
};
static auto runList(System& s, u32 from) -> void {
  s.ge.list = {}, s.ge.list.address = from;
  CHECK(u32(s.ge.run(100'000)), u32(GE::Stop::Finished));
}

//The GE thread held back before the next record it takes, while this lives (then let go, the thread drained);
//waited(access): access made while it's held, let go of from another thread after a while, and whether the emulation
//thread waited for the thread meanwhile (it's held again after). The thread drained first: held over a ring part
//full, the emulation thread could wait for room it never makes.
struct HeldBack {
  GeThread& thread;
  std::atomic<bool> held{true};
  explicit HeldBack(GeThread& thread) : thread(thread) {
    thread.drain();
    thread.gate = [this](u64) { while(held.load()) std::this_thread::yield(); };
  }
  ~HeldBack() { held = false; thread.drain(); thread.gate = nullptr; }
  template<typename Access> auto waited(const Access& access) -> bool {
    u64 waits = thread.waits;
    std::thread release([&] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); held = false; });
    access();
    release.join();
    held = true;
    return thread.waits > waits;
  }
};

//R14 (DESIGN.md 3.5, the review's round two): with the GE on its thread, a load through VRAM's fourth copy of a
//pixel the GPU drew, which today's code reads from memory without finishing, waits for the thread and leaves the
//pixel the GPU's: the bytes may still change. A block transfer then writes half of a pixel further down the frame
//buffer (overwritten(): the GPU finished first), the thread held back before it, and a second load of the pixel waits
//again and reads what the finish put there; had the first load's wait made the bytes clean, it would read memory at
//once, the finish not made yet. Then the GPU's depth: a load through the depth views reads memory's at once (only a
//put point puts the GPU's depth there), and after a PRIM sampling that depth through the depth views, the thread
//held back before it, one waits and reads the depth put there. Every load as a machine without the thread reads.
static auto gpuThreadR14() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 At = 0x3'0000, Pixel = At + 20 * 256 + 8 * 4, Row = 0x0897'0000;  //(row 20, column 8)
  constexpr u32 Second = 0x0894'1000, Third = 0x0894'2000;
  const u32 seen = Memory::VRAMBase + 3 * Memory::VRAMSize + Memory::vramSeen(3, Pixel);
  const u32 depth = 0x0460'0000 + 0x1f'0000 + (20 * 64 + 8) * 2;  //(through the fourth copy, in order)
  auto build = [&](Memory& memory) {
    for(u32 n = 0; n < 0x8'0000; n += 4) memory.write(4, Memory::VRAMBase + n, 0x0102'0304 * (n / 4 + 1));
    for(u32 n = 0; n < 0x8000; n += 4) memory.write(4, Memory::VRAMBase + 0x1f'0000 + n, 0x1111 * (n / 4 + 1));
    for(u32 n = 0; n < 32; n++) memory.write(4, Row + n * 4, 0xff00'0000 | n * 0x05'0607);
    ThreadedList first{memory};
    first.prepare(At, 3), first.sprite({{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030}), first.end();
    ThreadedList second{memory, Second, GPUVertices + 0x100};
    second.prepare(0x5'0000, 3);
    second.put(GE::DepthTestEnable, 1), second.put(GE::DepthTest, 1);  //(always, written)
    second.sprite({{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff40'5060}, 0x4321), second.end();
    ThreadedList third{memory, Third, GPUVertices + 0x200};
    third.prepare(0x6'0000, 3);
    third.texture(0x0460'0000 + 0x1f'0000, 64, 64, 64, 0);  //(the depth through the fourth copy, as 5650's colors)
    third.sprite({{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff}), third.end();
  };
  auto transfer = [&](System& s) {  //(rows 40 and 41, from the middle of a pixel: 2-byte pixels from column 1)
    auto& c = s.ge.commands;
    c[GE::TransferSource] = Row & 0xff'fff0, c[GE::TransferSourceWidth] = (Row >> 24) << 16 | 64;
    c[GE::TransferSourcePosition] = 0;
    c[GE::TransferDestination] = At & 0xff'fff0;
    c[GE::TransferDestinationWidth] = (Memory::VRAMBase >> 24) << 16 | 128;
    c[GE::TransferDestinationPosition] = 1 | 40 << 10;
    c[GE::TransferSize] = 15 | 1 << 10, c[GE::TransferStart] = 0;
    s.ge.transfer();
  };
  System plain, threaded;
  plain.threadGE(false), threaded.threadGE(true);
  u32 read[4] = {}, expected[4] = {};
  plain.ge.setRenderer(gpu);
  build(plain.memory);
  runList(plain, 0x0894'0000);
  expected[0] = plain.memory.read(4, seen);
  transfer(plain);
  expected[1] = plain.memory.read(4, seen);
  runList(plain, Second);
  expected[2] = plain.memory.read(2, depth);
  runList(plain, Third);
  expected[3] = plain.memory.read(2, depth);
  plain.ge.settleAll();
  plain.ge.setRenderer(nullptr);
  threaded.ge.setRenderer(gpu);
  build(threaded.memory);
  {
    HeldBack back{*threaded.geThreading};
    runList(threaded, 0x0894'0000);
    CHECK(back.waited([&] { read[0] = threaded.memory.read(4, seen); }), true);
    transfer(threaded);
    CHECK(back.waited([&] { read[1] = threaded.memory.read(4, seen); }), true);
    runList(threaded, Second);
    CHECK(back.waited([&] { read[2] = threaded.memory.read(2, depth); }), false);
    runList(threaded, Third);
    CHECK(back.waited([&] { read[3] = threaded.memory.read(2, depth); }), true);
  }
  threaded.ge.settleAll();
  threaded.ge.setRenderer(nullptr);
  for(u32 n = 0; n < 4; n++) CHECK(read[n], expected[n]);
  CHECK(expected[0] != expected[1], true);  //(the finish put the GPU's pixel there)
  CHECK(expected[3], 0x4321u);              //(the depth put there)
  CHECK(vramApart(plain, threaded), 0u);
}

//With the GE on its thread held back after a list draws on the GPU, an HLE function's copy out of VRAM's fourth copy
//(an 8888 frame buffer's depth view) of bytes nothing drew, on the 16 KiB block the GPU drew in: clean, so it doesn't
//wait, but today's check for it (pointer()'s busy check, which through that copy covers the whole block) finishes the
//GPU, and the GE thread makes that check from the copy's Read record where the synchronous GE made it (a finish
//changes later pictures in Vulkan fast). Seen twice: the GPU finishes as often as without the thread, and a load of a
//pixel it drew then needn't wait (the finish known). Without the record neither holds.
static auto gpuThreadCleanCheck() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 At = 0x3'0000, Clean = At + 56 * 256;  //(the sprite draws rows 0-47; row 56 is in the same 16 KiB)
  const u32 copied = Memory::VRAMBase + 3 * Memory::VRAMSize + Memory::vramSeen(3, Clean);
  const u32 drawn = Memory::VRAMBase + At + (10 * 64 + 10) * 4;
  auto build = [&](Memory& memory) {
    for(u32 n = 0; n < 0x4000; n += 4) memory.write(4, Memory::VRAMBase + At + n, 0x0102'0304 * (n / 4 + 1));
    ThreadedList list{memory};
    list.prepare(At, 3), list.sprite({{{0, 0, 0, 0}, {0, 0, 40, 48}}, 0xff10'2030}), list.end();
  };
  u32 bytes[2] = {}, pixel[2] = {};
  u64 finished[2] = {};
  System plain, threaded;
  plain.threadGE(false), threaded.threadGE(true);
  plain.ge.setRenderer(gpu);
  build(plain.memory);
  u64 finishes = stats(gpu).finishes;
  runList(plain, 0x0894'0000);
  CHECK(plain.memory.copyOut(&bytes[0], copied, 4), true);
  finished[0] = stats(gpu).finishes - finishes;
  pixel[0] = plain.memory.read(4, drawn);
  plain.ge.settleAll();
  plain.ge.setRenderer(nullptr);
  threaded.ge.setRenderer(gpu);
  build(threaded.memory);
  finishes = stats(gpu).finishes;  //(before the thread's held back: stats() drains it)
  {
    HeldBack back{*threaded.geThreading};
    runList(threaded, 0x0894'0000);
    CHECK(back.waited([&] { CHECK(threaded.memory.copyOut(&bytes[1], copied, 4), true); }), false);
  }
  finished[1] = stats(gpu).finishes - finishes;
  u64 waits = threaded.geThreading->waits;
  pixel[1] = threaded.memory.read(4, drawn);
  CHECK(threaded.geThreading->waits, waits);
  threaded.ge.settleAll();
  threaded.ge.setRenderer(nullptr);
  CHECK(finished[0], 1u);  //(the synchronous GE's check finished the GPU)
  CHECK(finished[1], finished[0]);
  CHECK(bytes[1], bytes[0]);
  CHECK(pixel[1], pixel[0]);
  CHECK(pixel[0] != 0x0102'0304u * (10 * 64 + 11), true);  //(the GPU's pixel, finished)
  CHECK(vramApart(plain, threaded), 0u);
}

//R15 (DESIGN.md 3.6, the review's round two): with the GE on its thread held back after a list draws on the GPU, the
//CPU's halfword and byte stores into drawn 8888 and 16-bit pixels, one after another with nothing between (each
//replayed by itself: part of a pixel finishes the GPU first, as today's code does), and two aligned words across the
//right edge of what it drew (the first's pixel memory's from then on, the second's bytes beside it, kept): none
//waits, and memory comes out as the same list's and stores' without the thread.
static auto gpuThreadR15() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Wide = 0x3'0000, Narrow = 0x5'0000;  //(8888, 5650)
  auto build = [&](Memory& memory) {
    for(u32 n = 0; n < 0x3'0000; n += 4) memory.write(4, Memory::VRAMBase + Wide + n, 0x0102'0304 * (n / 4 + 1));
    ThreadedList list{memory};
    list.prepare(Wide, 3), list.put(GE::Scissor2, 39 | 47 << 10);  //(a picture 40 pixels wide in rows of 64)
    list.sprite({{{0, 0, 0, 0}, {0, 0, 40, 48}}, 0xff10'2030});
    list.prepare(Narrow, 0), list.sprite({{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff40'5060});
    list.end();
  };
  auto stores = [&](System& s) {
    auto wide = [&](u32 x, u32 y) { return Memory::VRAMBase + Wide + (y * 64 + x) * 4; };
    auto narrow = [&](u32 x, u32 y) { return Memory::VRAMBase + Narrow + (y * 64 + x) * 2; };
    s.memory.write(2, wide(5, 3), 0x1122), s.memory.write(2, wide(5, 3) + 2, 0x3344);  //(a pixel, in halves)
    s.memory.write(1, wide(9, 7) + 1, 0x55), s.memory.write(1, wide(9, 7) + 2, 0x66);  //(two of a pixel's bytes)
    s.memory.write(1, narrow(3, 4), 0x77), s.memory.write(1, narrow(3, 4) + 1, 0x88);   //(a 16-bit pixel, in bytes)
    s.memory.write(1, narrow(10, 20) + 1, 0x99);
    s.memory.write(4, wide(39, 30), 0xaabb'ccdd), s.memory.write(4, wide(40, 30), 0xeeff'0011);  //(across the edge)
  };
  System plain, threaded;
  plain.threadGE(false), threaded.threadGE(true);
  plain.ge.setRenderer(gpu);
  build(plain.memory);
  runList(plain, 0x0894'0000);
  stores(plain);
  plain.ge.settleAll();
  plain.ge.setRenderer(nullptr);
  threaded.ge.setRenderer(gpu);
  build(threaded.memory);
  {
    HeldBack back{*threaded.geThreading};
    runList(threaded, 0x0894'0000);
    u64 waits = threaded.geThreading->waits;
    stores(threaded);
    CHECK(threaded.geThreading->waits, waits);
    CHECK(back.held.load(), true);
  }
  threaded.ge.settleAll();
  threaded.ge.setRenderer(nullptr);
  u32 apart = vramApart(plain, threaded);
  if(apart) std::printf("  %u bytes apart\n", apart);
  CHECK(apart, 0u);
  CHECK(plain.memory.read(4, Memory::VRAMBase + Wide + (3 * 64 + 5) * 4), 0x3344'1122u);
}

//R16 (DESIGN.md 3.5, the review's round two): a frame buffer drawing with the depth one of the other order drew
//(Memory::depthCopy(): 5650 against 8888), where the depth buffer's pieces don't start on 32 bytes, takes it through
//memory (takeDepth()): the drawer's depth put there in its order first (putDepth()), a put point. With the GE on its
//thread held back, the CPU's store past the taker's rows, into the drawer's, stays over the put (whose write-back,
//before it in the stream, goes under it), and its load of the depth waits for the thread and reads the depth put
//there: memory as the same list's and accesses' without the thread, the depth moves on and off.
static auto gpuThreadR16() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Depth = 0x1f'0010;  //(16 bytes past 32: no runs placed)
  constexpr u32 Loaded = 0x0420'0000 + Depth + (8 * 64 + 4) * 2;   //(through the second copy, the 5650's order)
  constexpr u32 Stored = 0x0420'0000 + Depth + (40 * 64 + 4) * 2;  //(row 40: past the taker's 16)
  auto build = [&](Memory& memory) {
    ThreadedList list{memory};
    list.prepare(0x3'0000, 0), list.put(GE::DepthBufferPointer, Depth);
    list.put(GE::DepthTestEnable, 1), list.put(GE::DepthTest, 1);  //(always, written)
    list.sprite({{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030}, 0x2345);
    list.prepare(0x6'0000, 3, 16), list.put(GE::DepthBufferPointer, Depth);
    list.put(GE::DepthTestEnable, 1), list.put(GE::DepthTest, 7);  //(greater or equal, written)
    list.sprite({{{0, 0, 0, 0}, {0, 0, 64, 16}}, 0xff40'5060}, 0x3456);
    list.end();
  };
  for(bool moves : depthWays(gpu)) {
    DepthMovesKept kept{gpu};
    gpu->backend->depthMoves = moves;
    System plain, threaded;
    plain.threadGE(false), threaded.threadGE(true);
    plain.ge.setRenderer(gpu);
    build(plain.memory);
    u64 put = stats(gpu).depthsPut;
    runList(plain, 0x0894'0000);
    CHECK(stats(gpu).depthsPut, put + 1);  //(through memory)
    plain.memory.write(2, Stored, 0x7777);
    u32 expected = plain.memory.read(2, Loaded);
    plain.ge.settleAll();
    plain.ge.setRenderer(nullptr);
    threaded.ge.setRenderer(gpu);
    build(threaded.memory);
    u32 read = 0;
    {
      HeldBack back{*threaded.geThreading};
      runList(threaded, 0x0894'0000);
      u64 waits = threaded.geThreading->waits;
      threaded.memory.write(2, Stored, 0x7777);
      CHECK(threaded.geThreading->waits, waits);
      CHECK(back.waited([&] { read = threaded.memory.read(2, Loaded); }), true);
    }
    threaded.ge.settleAll();
    threaded.ge.setRenderer(nullptr);
    CHECK(read, expected);
    CHECK(expected, 0x2345u);  //(the drawer's depth, put)
    u32 apart = vramApart(plain, threaded);
    if(apart) std::printf("  %s: %u bytes apart\n", way(moves), apart);
    CHECK(apart, 0u);
    CHECK(threaded.memory.read(2, Stored), 0x7777u);
  }
}

//R24 (DESIGN.md 3.7, the review's round four): a frame buffer taking the depth of a newer drawer with more rows over
//the same depth buffer (takeDepth()) first fills the drawer's rows memory changed since it drew, which can lie past
//the taker's: here in the depth buffer's second 16 KiB, which the taker's 32 rows don't reach. With the GE on its
//thread held back, the CPU changes those rows between the two lists and again after the second: the render GE fills
//them as they were before the second change (its version), inside what the model declares the PRIM may read (the
//depth buffer's 16 KiBs to the most rows any frame buffer drew with it). Memory as the same lists' and stores'
//without the thread, the depth moves on and off.
static auto gpuThreadR24() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Second = 0x0894'1000;
  auto build = [&](Memory& memory) {
    ThreadedList tall{memory};
    tall.prepare(0x3'0000, 3, 192);  //(its depth's rows 128 on in the depth buffer's second 16 KiB)
    tall.put(GE::DepthTestEnable, 1), tall.put(GE::DepthTest, 1);  //(always, written)
    tall.sprite({{{0, 0, 0, 0}, {0, 0, 64, 192}}, 0xff10'2030}, 0x2000);
    tall.end();
    ThreadedList taker{memory, Second, GPUVertices + 0x100};
    taker.prepare(0x9'0000, 3, 32);
    taker.put(GE::DepthTestEnable, 1), taker.put(GE::DepthTest, 7);  //(greater or equal, written)
    taker.sprite({{{0, 0, 0, 0}, {0, 0, 64, 32}}, 0xff40'5060}, 0x3000);
    taker.end();
  };
  auto change = [&](System& s, u32 z) {  //(rows 140-179 of the depth, through the fourth copy, in order)
    for(u32 y = 140; y < 180; y++) {
      for(u32 x = 0; x < 64; x += 2) s.memory.write(4, 0x0460'0000 + 0x1f'0000 + (y * 64 + x) * 2, z | z << 16);
    }
  };
  for(bool moves : depthWays(gpu)) {
    DepthMovesKept kept{gpu};
    gpu->backend->depthMoves = moves;
    System plain, threaded;
    plain.threadGE(false), threaded.threadGE(true);
    plain.ge.setRenderer(gpu);
    build(plain.memory);
    runList(plain, 0x0894'0000), change(plain, 0x5000), runList(plain, Second), change(plain, 0x6000);
    plain.ge.settleAll();
    plain.ge.setRenderer(nullptr);
    u64 taken = stats(gpu).depthsTaken;
    threaded.ge.setRenderer(gpu);
    build(threaded.memory);
    {
      HeldBack back{*threaded.geThreading};
      u64 waits = threaded.geThreading->waits;
      runList(threaded, 0x0894'0000), change(threaded, 0x5000), runList(threaded, Second), change(threaded, 0x6000);
      CHECK(threaded.geThreading->waits, waits);
      CHECK(back.held.load(), true);
    }
    threaded.ge.settleAll();
    threaded.ge.setRenderer(nullptr);
    CHECK(stats(gpu).depthsTaken > taken, true);  //(the taker took the tall one's depth)
    u32 apart = vramApart(plain, threaded);
    if(apart) std::printf("  %s: %u bytes apart\n", way(moves), apart);
    CHECK(apart, 0u);
  }
}

//R19 (DESIGN.md 3.7, the review's round two): a frame buffer drawing over a page of another's (a move source:
//begin()) first fills the source's bytes beside its pixels from memory where it keeps more than 16 ranges of them
//(besideFull: all of them, revive()), wherever they lie in the source's rows: here in its rows 14-31, before the
//drawer's own rows start (it starts at the source's row 32). With the GE on its thread held back, the CPU writes those
//bytes (columns 48-63, beside a picture 48 wide) after the first list and again after the second: the render GE fills
//them as they were before the second writes (their versions), inside what the model declares the PRIM may read (the
//rows of every frame buffer with pages in its own). Memory as the same lists' and stores' without the thread.
static auto gpuThreadR19() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Source = 0x3'0000, Second = 0x0894'1000;
  auto build = [&](Memory& memory) {
    for(u32 n = 0; n < 0x3000; n += 4) memory.write(4, Memory::VRAMBase + Source + n, 0x0102'0304 * (n / 4 + 1));
    ThreadedList source{memory};
    source.prepare(Source, 3), source.put(GE::Scissor2, 47 | 47 << 10);  //(a picture 48 wide in rows of 64)
    source.sprite({{{0, 0, 0, 0}, {0, 0, 48, 48}}, 0xff10'2030});
    source.end();
    ThreadedList drawer{memory, Second, GPUVertices + 0x100};
    drawer.prepare(Source + 0x2000, 0, 32);  //(5650 over the source's rows 32-47: its third page)
    drawer.sprite({{{0, 0, 0, 0}, {0, 0, 64, 32}}, 0xff40'5060});
    drawer.end();
  };
  auto beside = [&](System& s, u32 value) {  //(columns 48-63 of rows 14-31: 18 ranges)
    for(u32 y = 14; y < 32; y++) {
      for(u32 x = 48; x < 64; x++) s.memory.write(4, Memory::VRAMBase + Source + (y * 64 + x) * 4, value + y * 64 + x);
    }
  };
  System plain, threaded;
  plain.threadGE(false), threaded.threadGE(true);
  plain.ge.setRenderer(gpu);
  build(plain.memory);
  runList(plain, 0x0894'0000), beside(plain, 0x5500'0000), runList(plain, Second), beside(plain, 0x6600'0000);
  plain.ge.settleAll();
  plain.ge.setRenderer(nullptr);
  threaded.ge.setRenderer(gpu);
  build(threaded.memory);
  {
    HeldBack back{*threaded.geThreading};
    u64 waits = threaded.geThreading->waits;
    runList(threaded, 0x0894'0000), beside(threaded, 0x5500'0000), runList(threaded, Second);
    beside(threaded, 0x6600'0000);
    CHECK(threaded.geThreading->waits, waits);
    CHECK(back.held.load(), true);
  }
  threaded.ge.settleAll();
  threaded.ge.setRenderer(nullptr);
  u32 apart = vramApart(plain, threaded);
  if(apart) std::printf("  %u bytes apart\n", apart);
  CHECK(apart, 0u);
}

//Above 1x (DESIGN.md 3.5, the hull rule): at 2x a sprite with edges half a pixel in (its edge pixels' samples
//differ), finished by the CPU's load of one of them (which waits for the GE on its thread); then two small sprites at
//its corners, the target's box grown over the first's pixels but nothing drawn there again. A load of the same pixel
//finishes again, the box read back at 2x, which may take another of its samples: with the thread held back it waits
//(the target's hull since its last finish reaches the pixel, drawn since memory's write of it), and reads what the
//machine without the thread reads.
static auto gpuThreadAbove1x() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Second = 0x0894'1000, Pixel = Memory::VRAMBase + 0x3'0000 + (4 * 64 + 8) * 4;  //(row 4, column 8)
  gpu->resolution(2);
  auto build = [&](Memory& memory) {
    for(u32 n = 0; n < 0x3000; n += 4) memory.write(4, Memory::VRAMBase + 0x3'0000 + n, 0x0102'0304 * (n / 4 + 1));
    ThreadedList first{memory};
    first.prepare(0x3'0000, 3), first.sprite({{{0, 0, 8.5f, 4.5f}, {0, 0, 23.5f, 11.5f}}, 0xff10'2030}), first.end();
    ThreadedList second{memory, Second, GPUVertices + 0x100};
    second.prepare(0x3'0000, 3);
    second.sprite({{{0, 0, 0, 0}, {0, 0, 4, 2}}, 0xff40'5060});
    second.sprite({{{0, 0, 28, 14}, {0, 0, 32, 16}}, 0xff70'8090});
    second.end();
  };
  System plain, threaded;
  plain.threadGE(false), threaded.threadGE(true);
  u32 expected[2] = {}, read[2] = {};
  plain.ge.setRenderer(gpu);
  build(plain.memory);
  runList(plain, 0x0894'0000), expected[0] = plain.memory.read(4, Pixel);
  runList(plain, Second), expected[1] = plain.memory.read(4, Pixel);
  plain.ge.settleAll();
  plain.ge.setRenderer(nullptr);
  threaded.ge.setRenderer(gpu);
  build(threaded.memory);
  {
    HeldBack back{*threaded.geThreading};
    runList(threaded, 0x0894'0000);
    CHECK(back.waited([&] { read[0] = threaded.memory.read(4, Pixel); }), true);
    runList(threaded, Second);
    CHECK(back.waited([&] { read[1] = threaded.memory.read(4, Pixel); }), true);
  }
  threaded.ge.settleAll();
  threaded.ge.setRenderer(nullptr);
  gpu->resolution(1);
  CHECK(read[0], expected[0]), CHECK(read[1], expected[1]);
  if(expected[0] != expected[1]) std::printf("  (the second finish took another sample: %08x, then %08x)\n",
                                             expected[0], expected[1]);
  u32 apart = vramApart(plain, threaded);
  if(apart) std::printf("  %u bytes apart\n", apart);
  CHECK(apart, 0u);
}

//R23 (DESIGN.md 3.5, the review's round four): a frame buffer's depth drawn over two 16 KiB and put into memory (a
//texture through the depth views); a source drawn over the second 16 KiB's bytes, edges half a pixel in; the frame
//buffer drawing again with its depth in the first 16 KiB, its rows in the second filled first (the source's pixels
//moved into them, or with the moves off memory's as it is); the source finished by the CPU's load of one of its
//pixels; and the depth put into memory again. The second put changes bytes the colour rule has clean again, in a
//16 KiB whose newest GPU depth write was put back already: above 1x a moved pixel is the move's sample and the finish
//may take another, and with the moves off the depth filled is the one before the source's, at 1x too (at 1x with the
//moves the second put changes nothing: a moved pixel is the bytes the finish put there). With the GE on its thread
//held back, a load of the pixel after the second put waits for the thread where it may change, and reads what the
//machine without the thread reads. At 1x and 2x, the depth moves on and off.
static auto gpuThreadR23() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Depth = 0x1f'0000, Source = Depth + 0x4000, Second = 0x0894'1000;
  constexpr u32 Pixel = Memory::VRAMBase + Source + (10 * 64 + 12) * 4;  //(the source's row 10, column 12)
  auto build = [&](Memory& memory) {
    for(u32 n = 0; n < 0x8000; n += 4) memory.write(4, Memory::VRAMBase + Depth + n, 0x0102'0304 * (n / 4 + 1));
    ThreadedList first{memory};
    first.prepare(0x3'0000, 3, 200);  //(its depth's rows 128-199 in the depth buffer's second 16 KiB)
    first.put(GE::DepthTestEnable, 1), first.put(GE::DepthTest, 1);  //(always, written)
    first.sprite({{{0, 0, 0, 0}, {0, 0, 64, 200}}, 0xff10'2030}, 0x1000);
    first.put(GE::DepthTestEnable, 0), first.prepare(0x7'0000, 3);
    first.texture(0x0460'0000 + Depth, 64, 256, 64, 0);  //(the depth through the fourth copy: put into memory first)
    first.sprite({{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
    first.put(GE::TextureMappingEnable, 0), first.prepare(Source, 3);
    first.sprite({{{0, 0, 4.5f, 2.5f}, {0, 0, 40.5f, 30.5f}}, 0xff40'5060});
    first.prepare(0x3'0000, 3, 24);
    first.put(GE::DepthTestEnable, 1), first.put(GE::DepthTest, 7);  //(greater or equal, written)
    first.sprite({{{0, 0, 0, 0}, {0, 0, 64, 24}}, 0xff70'8090}, 0x2000);
    first.end();
    ThreadedList second{memory, Second, GPUVertices + 0x200};
    second.prepare(0x8'0000, 3), second.texture(0x0460'0000 + Depth, 64, 256, 64, 0);
    second.sprite({{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
    second.end();
  };
  for(u32 scale : {1u, 2u}) {
    for(bool moves : depthWays(gpu)) {
      DepthMovesKept kept{gpu};
      gpu->backend->depthMoves = moves;
      gpu->resolution(scale);
      System plain, threaded;
      plain.threadGE(false), threaded.threadGE(true);
      u32 expected[2] = {}, read[2] = {};
      plain.ge.setRenderer(gpu);
      build(plain.memory);
      u64 put = stats(gpu).depthsPut;
      runList(plain, 0x0894'0000), expected[0] = plain.memory.read(4, Pixel);
      runList(plain, Second), expected[1] = plain.memory.read(4, Pixel);
      CHECK(stats(gpu).depthsPut - put, 2u);
      plain.ge.settleAll();
      plain.ge.setRenderer(nullptr);
      threaded.ge.setRenderer(gpu);
      build(threaded.memory);
      {
        HeldBack back{*threaded.geThreading};
        runList(threaded, 0x0894'0000);
        CHECK(back.waited([&] { read[0] = threaded.memory.read(4, Pixel); }), true);
        runList(threaded, Second);
        CHECK(back.waited([&] { read[1] = threaded.memory.read(4, Pixel); }), scale > 1 || !moves);
      }
      threaded.ge.settleAll();
      threaded.ge.setRenderer(nullptr);
      CHECK(read[0], expected[0]), CHECK(read[1], expected[1]);
      if(expected[0] != expected[1]) {
        std::printf("  %ux, %s: the second put changed the pixel (%08x, then %08x)\n", scale, way(moves), expected[0],
                    expected[1]);
      }
      u32 apart = vramApart(plain, threaded);
      if(apart) std::printf("  %ux, %s: %u bytes apart\n", scale, way(moves), apart);
      CHECK(apart, 0u);
    }
  }
  gpu->resolution(1);
}

//The depth a frame buffer of format has on the GPU (GPU::depthOf()), its depth buffer at depthBuffer, 512 wide, against
//the software renderer's memory there (in the order the format keeps it in: memory.hpp): the depths apart, the first
//few said.
static auto depthsApart(GPU* gpu, System& software, u32 address, u32 format, u32 depthBuffer) -> u32 {
  std::vector<u16> depths;
  u32 width = 0, height = 0;
  if(!gpu->depthOf(address, 512, format, depths, width, height) || width != 512) return ~0u;
  u32 apart = 0;
  for(u32 y = 0; y < height; y++) {
    for(u32 x = 0; x < width; x++) {
      u32 seen = (depthBuffer + (y * 512 + x) * 2) & (Memory::VRAMSize - 1);
      u32 at = Memory::vramOffset(Memory::depthCopy(format), seen);
      u32 expected = software.memory.vram[at] | software.memory.vram[at + 1] << 8, got = depths[y * width + x];
      if(got != expected && apart++ < 4) std::printf("  depth (%u, %u): %04x, the GPU's %04x\n", x, y, expected, got);
    }
  }
  return apart;
}

//VRAM's bytes the two machines don't agree on, all of them but those from skip to skipEnd (a depth buffer the
//software renderer writes, the GPU's depth staying on the GPU), the first few said.
static auto apartBut(System& a, System& b, u32 skip, u32 skipEnd) -> u32 {
  a.ge.settleAll(), b.ge.settleAll();
  auto &x = a.memory.vram, &y = b.memory.vram;
  u32 bytes = 0;
  for(auto [from, to] : {std::pair<u32, u32>{0, skip}, std::pair<u32, u32>{skipEnd, Memory::VRAMSize}}) {
    if(from >= to || !std::memcmp(&x[from], &y[from], to - from)) continue;
    for(u32 n = from; n < to; n++) {
      if(x[n] != y[n] && bytes++ < 4) std::printf("  VRAM %06x: %02x, the GPU's %02x\n", n, x[n], y[n]);
    }
  }
  return bytes;
}

//The depth buffer in the order a frame buffer's format has it (memory.hpp: a 16-bit frame buffer's as VRAM's second
//copy shows it, an 8888 one's as the fourth does), on the GPU as the software renderer has it, its frame buffers and
//depth buffer 512 pixels wide as games have them (16 rows; the frame buffers at 0, the depth buffer at 0x88000):
//  - the Kurohyou games' depth made as a picture: drawn into the depth buffer's memory as a 5650 frame buffer, taken
//    back through the second copy into a frame buffer of their own (0x116400) and from there into the depth buffer,
//    all three on the GPU, then a sprite into the 5551 screen tested against it ("greater or equal"), which needs the
//    other frame buffer's pixels, the GPU's, as its depth (a finish before the depth is filled from memory);
//  - the depth a 5551 frame buffer drew, taken through the second copy as a 5650 texture into another frame buffer
//    (Kurohyou's depth of field: the GPU's depth put into memory in that order first);
//  - a depth buffer an 8888 frame buffer and a 5551 one draw with in turn (Twisted Metal's small 5551 frame buffers
//    test against its 8888 scene's depth; on the PSP each reads the same bytes in its own order): the depth placed on
//    the GPU between them by runs of 16, each where the other's order puts the same bytes, put into memory once only,
//    for the texture;
//  - a second frame, the depth made again from another picture and the screen's sprite tested against it: the screen
//    then takes its depth from the 8888 frame buffer's, whose rows the picture changed (filled from memory, the
//    picture's pixels with them).
//Every byte of the screens the software renderer's, at 1x and 2x, in either mode, the picture's pixels moved into the
//depth on the GPU (no finish) and put in memory first (a finish for a depth fill each frame: depthWays()). Where the
//device's copy of the depth alone takes the stencil (vulkan.cpp's probe: Backend::depthAlone off, the depth through the
//staging buffer), that copy forced as well, which the runs then take image to image: their regions the staging
//buffer's, and the depth's aspect alone named (Backend::depthCopyAspects).
static auto depthOrders(GPU* gpu) -> void {
  constexpr u32 Depth = 0x8'8000, Scratch = 0x11'6400, Second = 0x1'0000, Rows = 16;
  DepthMovesKept kept{gpu};
  auto picture = [](u32 frame, u32 x, u32 y) -> u32 {  //(depths 0-999, each frame's own)
    return frame ? (x * 53 + y * 997 + 500) % 1000 : (x * 37 + y * 1031) % 1000;
  };
  auto into = [](System& s, u32 buffer, u32 format) {
    s.ge.commands[GE::FrameBufferPointer] = buffer, s.ge.commands[GE::FrameBufferPixelFormat] = format;
  };
  //(a 5650 texture 512 wide at from, 1:1 onto a 5650 frame buffer at to)
  auto copy = [&](System& s, u32 from, u32 to) {
    into(s, to, 0);
    texture(s, from, 512, 16, 512, 0);
    sprite(s, {{{0, 0, 0, 0}, {512, Rows, 512, Rows}}, 0xffff'ffff});
    s.ge.commands[GE::TextureMappingEnable] = 0;
  };
  auto frame = [&](System& s, u32 n) {  //(the depth made as a picture, the screen cleared and tested against it)
    auto& c = s.ge.commands;
    for(u32 y = 0; y < Rows; y++) {
      for(u32 x = 0; x < 512; x++) s.memory.write(2, GPUTexture + (y * 512 + x) * 2, picture(n, x, y));
    }
    copy(s, GPUTexture, Depth);
    copy(s, Memory::VRAMBase + Memory::VRAMSize + Depth, Scratch);
    copy(s, Memory::VRAMBase + Scratch, Depth);
    into(s, 0, 1);
    c[GE::ClearMode] = 1 | 1 << 8;  //(its colors, not its depth)
    sprite(s, {{{0, 0, 0, 0}, {0, 0, 480, Rows}}, 0});
    c[GE::ClearMode] = 0;
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;  //(greater or equal, not written)
    sprite(s, {{{0, 0, 0, 0}, {0, 0, 480, Rows}}, 0xffff'ffff}, 500);
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
  };
  bool alone = gpu->backend->depthAlone;
  auto ways = depthWays(gpu);
  for(bool forced : {false, true}) for(bool moves : ways) for(u32 scale : {1u, 2u}) {
    if((forced && alone) || !scaleTaken(gpu, scale)) continue;
    gpu->resolution(scale);
    gpu->backend->depthMoves = moves, gpu->backend->depthAlone = alone || forced;
    gpu->backend->depthCopyAspects = 0;
    auto before = stats(gpu);
    u64 copies = gpu->backend->depthCopies;
    const char* how = forced ? ", the image copy forced" : "";
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      auto& c = s->ge.commands;
      c[GE::FrameBufferWidth] = 512, c[GE::DepthBufferPointer] = Depth, c[GE::DepthBufferWidth] = 512;
      c[GE::Region2] = 1023 << 10 | 1023, c[GE::Scissor2] = 511 | (Rows - 1) << 10;
      c[GE::VertexType] = 0x80'019f, c[GE::ShadeMode] = 1;
      frame(*s, 0);
      c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written: a depth of the screen's own on the left)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 200, Rows}}, 0xff00'40ff}, 700);
      c[GE::DepthTestEnable] = 0;
      copy(*s, Memory::VRAMBase + Memory::VRAMSize + Depth, Second);  //(that depth through the second copy)
      into(*s, 0x2'0000, 3);  //(an 8888 frame buffer over the same depth buffer: its depth in the fourth copy's order)
      c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 512, Rows}}, 0xff20'2020}, 300);
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 96, 8}}, 0xff40'4040}, 900);
      into(*s, 0x3'0000, 1);  //(and a 5551 one tested against it: the same bytes, read in the second copy's order)
      c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 512, Rows}}, 0xffff'ffff}, 600);
      c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
    }
    auto at = [&](u32 buffer, u32 x, u32 y) {
      return u32(software.memory.read(2, Memory::VRAMBase + buffer + (y * 512 + x) * 2));
    };
    auto wrong = [&](u32 n, u32 left) {  //(the screen's pixels drawn but where the picture's nearer than 500)
      u32 count = 0;
      for(u32 y = 0; y < Rows; y++) {
        for(u32 x = left; x < 480; x++) count += ((at(0, x, y) & 0x7fff) == 0x7fff) != (500 >= picture(n, x, y));
      }
      return count;
    };
    u32 bytes = apart(software, hardware);
    if(bytes) {
      std::printf("  %s %ux, %s%s: %u bytes apart\n", gpu->fast ? "fast" : "accurate", scale, way(moves), how, bytes);
    }
    CHECK(bytes, 0u);
    CHECK(wrong(0, 200), 0u);
    //(the screen's depth, the picture's)
    CHECK(at(Second, 100, 3), 700u), CHECK(at(Second, 300, 3), picture(0, 300, 3));
    //(the 5551 frame buffer's pixel (20, 4) reads the 8888 one's (260, 4), 300; its (128, 4) the 8888's (64, 4), 900)
    CHECK(at(0x3'0000, 20, 4) & 0x7fff, 0x7fffu), CHECK(at(0x3'0000, 128, 4) & 0x7fff, 0u);
    CHECK(stats(gpu).depthFinishes - before.depthFinishes, moves ? 0u : 1u);
    CHECK(stats(gpu).depthsMoved > before.depthsMoved, moves);
    CHECK(stats(gpu).depthsPut - before.depthsPut, 1u);  //(the texture's, not the depth taken across the formats)
    for(System* s : {&software, &hardware}) frame(*s, 1);
    bytes = apart(software, hardware);
    if(bytes) {
      std::printf("  %s %ux, %s%s, second frame: %u bytes apart\n", gpu->fast ? "fast" : "accurate", scale,
                  way(moves), how, bytes);
    }
    CHECK(bytes, 0u);
    CHECK(wrong(1, 0), 0u);
    CHECK(stats(gpu).depthFinishes - before.depthFinishes, moves ? 0u : 2u);
    if(gpu->backend->depthAlone) {  //(the runs copied image to image, of the depth's aspect alone: Vulkan's 2)
      CHECK(gpu->backend->depthCopies > copies, true);
      CHECK(gpu->backend->depthCopyAspects, 2u);
    }
    hardware.ge.setRenderer(nullptr);
  }
  gpu->backend->depthAlone = alone;
  gpu->resolution(1);
}
static auto gpuDepthOrders() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) depthOrders(gpu);
  }
}

//Depth filled from memory as a finish then the fill would leave it (GPU::moveIntoDepth()), for every PRIM it's filled
//for, whether its test reads it or not, and so for every PRIM after that finds it filled: a picture of depths drawn
//into the depth buffer's first 16 rows as a 5650 frame buffer, on the GPU (the 5551 screen 512 pixels wide and 32
//rows, its depth buffer at 0x88000: the picture's rows one 16 KiB, the rest another), then on machines of their own
//each of these, and a sprite or quad tested against the depth they leave ("greater or equal", not written):
//  - a clear of all of the depth, or a sprite over all of it written with a test always true (games clear their depth
//    so over the bloom they drew into its memory, or write it so);
//  - a sprite at 65535, which passes whatever it finds, or at 65534, which fails where the picture's 65535 (Chili Con
//    Carnage and Dirt 2 test sprites at 65535 so); a 3D quad in rows 18-31, below the picture's 16 KiB, or in rows
//    2-13 (GTA Chinatown Wars tests its radar below the rows its bloom drew over);
//  - the review's five that leave the picture's rows filled for the sprite tested after, over them: a sprite at 65535,
//    a 3D quad below the picture, the left half written with a test always true or cleared, a sprite in rows 16-31;
//  - another picture drawn after a sprite tested against the first, and a sprite tested against it: the screen's depth
//    the only one over the depth buffer, its rows filled already, its own rows memory changed taken again (drawn where
//    the first was, which memory heard of then: own());
//  - the screen at 0x8'c000, its colors (cleared to a color of their own) the depth buffer's last 16 rows, tested in
//    its last 16 (the rows of its own it draws no depth but memory's): a finish either way, which puts both frame
//    buffers' pixels in memory (a target's own pixels don't go into its depth).
//Every byte of VRAM the software renderer's but the depth buffer's, and every depth the screen has (GPU::depthOf()),
//at 1x and 2x, in either mode, with the picture's pixels moved into the depth on the GPU (no finish). Where the GPU
//can't move them (Backend::depthMoves off), they're put in memory first only where the PRIM's test reads them (GPU::
//begin()): one finish for the sprite at 65534, one for the quad over the picture and two for the second picture, every
//byte and depth the software renderer's in those, the clear and the sprite always true; none in the rest, whose depth
//is memory's as it was, as master leaves it (the screen's own rows too: the rows it tests are its own pages alone).
static auto depthCurrent(GPU* gpu) -> void {
  constexpr u32 Depth = 0x8'8000, Rows = 32;
  DepthMovesKept kept{gpu};
  auto picture = [](u32 n, u32 x, u32 y) -> u32 {  //(about 32768, the quads' depth; 65535 every seventh diagonal)
    return (x + y) % 7 == 0 ? 65535 : 32268 + (x * (37 + n * 16) + y * 1031) % 1000;
  };
  auto into = [](System& s, u32 buffer, u32 format) {
    s.ge.commands[GE::FrameBufferPointer] = buffer, s.ge.commands[GE::FrameBufferPixelFormat] = format;
  };
  auto drawPicture = [&](System& s, u32 n) {
    for(u32 y = 0; y < 16; y++) {
      for(u32 x = 0; x < 512; x++) s.memory.write(2, GPUTexture + (y * 512 + x) * 2, picture(n, x, y));
    }
    into(s, Depth, 0);
    texture(s, GPUTexture, 512, 16, 512, 0);
    sprite(s, {{{0, 0, 0, 0}, {512, 16, 512, 16}}, 0xffff'ffff});
    s.ge.commands[GE::TextureMappingEnable] = 0;
    into(s, 0, 1);
  };
  auto tested = [](System& s, float z, float left, float top, float right, float bottom, u32 color) {
    auto& c = s.ge.commands;
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;
    sprite(s, {{{0, 0, left, top}, {0, 0, right, bottom}}, color}, z);
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
  };
  auto quad = [](System& s, float top, float bottom) {  //(3D, the screen's width, at 32768, tested as tested())
    auto f24 = [](float value) { return bitsOf(value) >> 8; };
    auto& c = s.ge.commands;
    c[GE::VertexType] = 0x1ff, c[GE::MaxZ] = 0xffff;
    for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
    for(u32 n = 0; n < 16; n++) s.ge.projection[n] = f24(n % 5 == 0 ? 1 : 0);
    c[GE::ViewportXScale] = f24(256), c[GE::ViewportYScale] = f24(-16), c[GE::ViewportZScale] = f24(30000);
    c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
    c[GE::OffsetX] = (2048 - 256) << 4, c[GE::OffsetY] = (2048 - 16) << 4;
    u32 to = GPUVertices;
    for(u32 corner : {0u, 1u, 3u, 0u, 3u, 2u}) {
      float x = corner & 1 ? 512 : 0, y = corner >> 1 ? bottom : top;
      for(u32 word : {0u, 0u, 0xff40'ffffu, 0u, 0u, bitsOf(1), bitsOf(x / 256 - 1), bitsOf(1 - y / 16), 0u}) {
        s.memory.write(4, to, word), to += 4;
      }
    }
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Triangles, 6);
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0, c[GE::VertexType] = 0x80'019f;
  };
  auto written = [](System& s, float right, bool clear) {  //(the depth of the screen's left, 30000, every row)
    auto& c = s.ge.commands;
    if(clear) c[GE::ClearMode] = 1 | 4 << 8;  //(the depth alone)
    else c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;
    sprite(s, {{{0, 0, 0, 0}, {0, 0, right, Rows}}, 0xff00'40ff}, 30000);
    c[GE::ClearMode] = 0, c[GE::DepthTestEnable] = 0;
  };
  u32 screen = 0;  //(the screen's address)
  //(off: the depth finishes with no moves, and whether that leaves every depth as a finish would)
  auto run = [&](u32 scale, bool moves, const char* what, std::pair<u32, bool> off,
                 const std::function<void (System&)>& steps) {
    gpu->backend->depthMoves = moves;
    auto before = stats(gpu);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      auto& c = s->ge.commands;
      c[GE::FrameBufferWidth] = 512, c[GE::DepthBufferPointer] = Depth, c[GE::DepthBufferWidth] = 512;
      c[GE::Region2] = 1023 << 10 | 1023, c[GE::Scissor2] = 511 | (Rows - 1) << 10;
      c[GE::VertexType] = 0x80'019f, c[GE::ShadeMode] = 1;
      into(*s, screen, 1);
      c[GE::ClearMode] = 1 | 1 << 8;  //(the screen's colors)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 512, Rows}}, screen ? 0x8000'7654 : 0});
      c[GE::ClearMode] = 0;
      drawPicture(*s, 0);
      into(*s, screen, 1);
      steps(*s);
    }
    bool exact = moves || off.second;
    u32 depths = 0, bytes = 0;  //(not looked at where the depth is memory's as it was)
    if(exact) depths = depthsApart(gpu, software, screen, 1, Depth);
    if(exact) bytes = apartBut(software, hardware, Depth, Depth + 512 * Rows * 2);
    else software.ge.settleAll(), hardware.ge.settleAll();
    u64 finished = stats(gpu).depthFinishes - before.depthFinishes;
    u64 moved = stats(gpu).depthsMoved - before.depthsMoved;
    bool none = moves && !screen;  //(no finish: a finish either way where the screen's own pixels are the depth's)
    bool counted = moves ? (finished == 0) == none : finished == off.first;
    if(bytes || depths || !counted || (moved > 0) != none) {
      std::printf("  %s %ux, %s, %s: %u bytes apart, %u depths, %llu depth finishes, %llu moved\n",
                  gpu->fast ? "fast" : "accurate", scale, way(moves), what, bytes, depths,
                  (unsigned long long)finished, (unsigned long long)moved);
    }
    CHECK(bytes, 0u), CHECK(depths, 0u);
    CHECK(counted, true), CHECK(moved > 0, none);
    hardware.ge.setRenderer(nullptr);
  };
  for(bool moves : depthWays(gpu)) for(u32 scale : {1u, 2u}) {
    if(!scaleTaken(gpu, scale)) continue;
    gpu->resolution(scale);
    run(scale, moves, "a clear of the depth", {0, true}, [&](System& s) {
      written(s, 512, true), tested(s, 32768, 0, 0, 512, Rows, 0xffff'ffff);
    });
    run(scale, moves, "a sprite written, its test always true", {0, true}, [&](System& s) {
      written(s, 512, false), tested(s, 32768, 0, 0, 512, Rows, 0xffff'ffff);
    });
    run(scale, moves, "a sprite at 65535", {0, false}, [&](System& s) {
      tested(s, 65535, 0, 0, 512, 16, 0xffff'ffff);
    });
    run(scale, moves, "a sprite at 65534", {1, true}, [&](System& s) { tested(s, 65534, 0, 0, 512, 16, 0xffff'ffff); });
    run(scale, moves, "a 3D quad below the picture", {0, false}, [&](System& s) { quad(s, 18, Rows); });
    run(scale, moves, "a 3D quad over it", {1, true}, [&](System& s) { quad(s, 2, 14); });
    run(scale, moves, "a sprite at 65535, then one at 32768", {0, false}, [&](System& s) {
      tested(s, 65535, 0, 0, 512, 16, 0xffff'ffff), tested(s, 32768, 0, 0, 512, 16, 0xff00'ffff);
    });
    run(scale, moves, "a 3D quad below the picture, then a sprite over it", {0, false}, [&](System& s) {
      quad(s, 18, Rows), tested(s, 32768, 0, 0, 512, 16, 0xff00'ffff);
    });
    run(scale, moves, "the left half written with a test always true, then a sprite over all", {0, false},
        [&](System& s) { written(s, 256, false), tested(s, 32768, 0, 0, 512, 16, 0xff00'ffff); });
    run(scale, moves, "the left half's depth cleared, then a sprite over all", {0, false}, [&](System& s) {
      written(s, 256, true), tested(s, 32768, 0, 0, 512, 16, 0xff00'ffff);
    });
    run(scale, moves, "a sprite in rows 16-31, then one in rows 0-15", {0, false}, [&](System& s) {
      tested(s, 32768, 0, 16, 512, Rows, 0xffff'ffff), tested(s, 32768, 0, 0, 512, 16, 0xff00'ffff);
    });
    run(scale, moves, "another picture", {2, true}, [&](System& s) {
      tested(s, 32768, 0, 0, 512, 16, 0xffff'ffff);
      drawPicture(s, 1);
      tested(s, 32768, 0, 0, 512, 16, 0xff00'ffff);
    });
    screen = 0x8'c000;
    run(scale, moves, "the screen over the depth's last rows", {0, false}, [&](System& s) {
      tested(s, 32768, 0, 16, 512, Rows, 0xff00'ffff);
    });
    screen = 0;
  }
  gpu->resolution(1);
}
static auto gpuDepthCurrent() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) depthCurrent(gpu);
  }
}

//Other frame buffers' pixels as the depth, in every pair of formats (GPU::moveIntoDepth()): a picture drawn on the GPU
//into a frame buffer over a depth buffer's bytes (16-bit, 5650, 5551 or 4444, or 8888: a texture's texels, their
//alphas the stencil), then a frame buffer of each format tested against that depth ("greater or equal" at 32768, not
//written), which keeps it in its own format's order (memory.hpp): the screen 512 pixels wide and 32 rows, its depth
//buffer at 0x88000 (two 16 KiB). The picture's frame buffer starts at the depth buffer's first byte, a page on, 64
//bytes on, or half a 16 KiB back, its rows 512 pixels or 480 (rows that aren't whole pages), and is drawn only in part,
//the rest of its bytes memory's (as the CPU left them: a pattern), or drawn whole (its 64 rows: where it starts on
//the depth's first byte or before, all of the depth's 16 KiB, whose depth isn't taken from memory first then:
//GPU::depthCovered()), in all its rows but only some columns, or in all its columns from its third row (its pages
//the first 16 KiB's, not all of their bytes), some of what it drew written over by the CPU since (memory's then:
//dead). And the review's two, an 8888 picture with 5551 and 8888 screens: in rows 0, 7, 8 and 15, the pages between
//its rows none's (its drawn rectangle full rows over all of both 16 KiB, but not their pages: not covered), and 480
//pixels wide in rows 0 and 5 (pages 0 and 2: two moves), the CPU's bytes in page 1 after, in row 2 and in row 4, which
//cross its ends (each move held to its own pages, below and above: move.frag). The depth the screen has
//(GPU::depthOf()) is the software renderer's, depth for depth, and so is every
//byte of VRAM after, with the pixels moved into the depth on the GPU (no finish) and put in memory first (a finish),
//at 1x, 2x and 3x, in either mode.
static auto depthMoves(GPU* gpu) -> void {
  constexpr u32 Depth = 0x8'8000, Rows = 32;
  DepthMovesKept kept{gpu};
  struct Place { s32 offset; u32 stride; };
  static constexpr Place places[] = {{0, 512}, {0x1000, 512}, {0x40, 512}, {-0x2000, 512}, {0, 480}, {0x40, 480}};
  auto machine = [&](System& s, u32 screen, u32 format, Place place, u32 shape) {
    for(u32 n = 0; n < 0x6'0000; n += 4) {  //(memory's bytes before: a pattern, from 0x6'0000)
      u32 word = (n * 0x9e37'79b9u) ^ (n >> 7) * 0x85eb'ca6bu;
      std::memcpy(&s.memory.vram[0x6'0000 + n], &word, 4);
    }
    for(u32 n = 0; n < 512 * 64; n++) s.memory.write(4, GPUTexture + n * 4, u32(n * 0x2545'f491u) ^ n >> 9);
    auto& c = s.ge.commands;
    c[GE::Region2] = 1023 << 10 | 1023, c[GE::VertexType] = 0x80'019f, c[GE::ShadeMode] = 1;
    c[GE::FrameBufferPointer] = Depth + place.offset, c[GE::FrameBufferWidth] = place.stride;
    c[GE::FrameBufferPixelFormat] = format, c[GE::Scissor2] = (place.stride - 1) | 63 << 10;
    texture(s, GPUTexture, 512, 64, 512);
    //(1:1: columns 37-436 of rows 2-59, all its columns and rows, those columns of all its rows, or all its columns
    //of rows 2-63)
    if(shape == 0) sprite(s, {{{37, 2, 37, 2}, {437, 60, 437, 60}}, 0xffff'ffff});
    if(shape == 1) sprite(s, {{{0, 0, 0, 0}, {512, 64, 512, 64}}, 0xffff'ffff});
    if(shape == 2) sprite(s, {{{37, 0, 37, 0}, {437, 64, 437, 64}}, 0xffff'ffff});
    if(shape == 3) sprite(s, {{{0, 2, 0, 2}, {512, 64, 512, 64}}, 0xffff'ffff});
    //(the review's: rows 0, 7, 8 and 15, the pages between them none's; and rows 0 and 5 of 480, its pages 0 and 2,
    //rows 2 and 4 across page 1's ends)
    std::vector<float> rows;
    if(shape == 4) rows = {0, 7, 8, 15};
    if(shape == 5) rows = {0, 5};
    for(float row : rows) {
      float across = place.stride;
      sprite(s, {{{0, row, 0, row}, {across, row + 1, across, row + 1}}, 0xffff'ffff});
    }
    c[GE::TextureMappingEnable] = 0;
    for(u32 n = 0; n < 8; n++) s.memory.write(4, Memory::VRAMBase + Depth + 0x5100 + n * 4, 0x1234'5678u * (n + 1));
    for(u32 n = 0; n < 32 && shape == 5; n++) {  //(and in page 1, none's: in row 2, and in row 4)
      s.memory.write(4, Memory::VRAMBase + Depth + 0x1100 + n * 4, 0xfedc'ba98u ^ n * 0x0101'0101u);
      s.memory.write(4, Memory::VRAMBase + Depth + 0x1f00 + n * 4, 0x0bad'f00du ^ n * 0x0101'0101u);
    }
    c[GE::FrameBufferPointer] = 0, c[GE::FrameBufferWidth] = 512, c[GE::FrameBufferPixelFormat] = screen;
    c[GE::DepthBufferPointer] = Depth, c[GE::DepthBufferWidth] = 512, c[GE::Scissor2] = 511 | (Rows - 1) << 10;
    c[GE::ClearMode] = 1 | 1 << 8;  //(the screen's colors)
    sprite(s, {{{0, 0, 0, 0}, {0, 0, 512, Rows}}, 0});
    c[GE::ClearMode] = 0;
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;
    sprite(s, {{{0, 0, 0, 0}, {0, 0, 512, Rows}}, 0xffc0'80ff}, 32768);
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
  };
  u32 cases = 0;
  auto ways = depthWays(gpu);
  auto test = [&](u32 screen, u32 format, Place place, u32 shape, bool covers) {
    System software;
    machine(software, screen, format, place, shape);
    software.ge.settleAll();
    for(bool moves : ways) for(u32 scale : {1u, 2u, 3u}) {
      if(!scaleTaken(gpu, scale)) continue;
      gpu->resolution(scale);
      gpu->backend->depthMoves = moves;
      auto before = stats(gpu);
      System hardware;
      hardware.ge.setRenderer(gpu);
      machine(hardware, screen, format, place, shape);
      u32 depths = depthsApart(gpu, software, 0, screen, Depth);
      u64 finished = stats(gpu).depthFinishes - before.depthFinishes;
      u64 moved = stats(gpu).depthsMoved - before.depthsMoved;
      u64 covered = stats(gpu).depthsCovered - before.depthsCovered;
      u32 bytes = vramApart(software, hardware);
      bool wrong = (finished == 0) != moves || (moved > 0) != moves || (covered > 0) != (moves && covers);
      if(bytes || depths || wrong) {
        std::printf("  %s %ux, %s: screen %u over picture %u at %+d, %u wide, shape %u: %u bytes apart, %u depths, "
                    "%llu depth finishes, %llu moved, %llu covered\n", gpu->fast ? "fast" : "accurate", scale,
                    way(moves), screen, format, place.offset, place.stride, shape, bytes, depths,
                    (unsigned long long)finished, (unsigned long long)moved, (unsigned long long)covered);
      }
      CHECK(bytes, 0u), CHECK(depths, 0u);
      CHECK(finished == 0, moves), CHECK(moved > 0, moves), CHECK(covered > 0, moves && covers);
      hardware.ge.setRenderer(nullptr);
    }
    cases++;
  };
  for(u32 screen : {0u, 1u, 2u, 3u}) {
    for(u32 format : {0u, 1u, 2u, 3u}) {
      for(u32 k : {0u, 1u, 2u, 3u, 4u}) {
        Place place = places[(screen * 4 + format + k) % std::size(places)];
        u32 shape = k == 3 ? 0 : k == 4 ? 3 : k;
        //(the depth's two 16 KiB the picture's drawn bytes: from the first's start, or from before it where it's
        //drawn from row 2)
        test(screen, format, place, shape, (shape == 1 && place.offset <= 0) || (shape == 3 && place.offset < 0));
      }
    }
  }
  test(1, 3, places[0], 4, false), test(3, 3, places[0], 4, false), test(1, 3, places[4], 5, false);
  CHECK(cases, 83u);
  gpu->resolution(1);
}
static auto gpuDepthMoves() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) depthMoves(gpu);
  }
}

//A 5551 frame buffer's stencil is a bit, which INCREMENT fills and DECREMENT empties (pixel.cpp), as the pass's
//operation (one over a test of "equal to 0" too, compared on the GPU against the 255 it writes), the fail's and the
//depth fail's: then tested (equal to 255: a sprite drawn only where it's filled), read back as the alpha, and taken as
//a texture's alpha into another frame buffer behind an alpha test (Hammerin' Hero and Jeanne d'Arc mark their
//characters' pixels so, and put them on the screen by it), every byte the software renderer's: the texture's rows
//those the fail's and depth fail's operations made too, where no color is written (the copy's alpha set from the
//stencil there first). At 1x and 2x, in either mode.
static auto stencilBit(GPU* gpu) -> void {
  for(u32 scale : {1u, 2u}) {
    gpu->resolution(scale);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      auto& c = s->ge.commands;
      prepare(*s, 0, 1);
      c[GE::ClearMode] = 1 | 3 << 8;  //(the colors, and the stencil 0)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0x0012'3456});
      c[GE::ClearMode] = 0, c[GE::StencilTestEnable] = 1, c[GE::StencilTest] = 1 | 0x10 << 8 | 0xff << 16;  //(always)
      c[GE::StencilOperation] = 4 << 16;  //(INCREMENT where it passes)
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 40, 48}}, 0x00ff'0000});
      c[GE::StencilOperation] = 5 << 16;  //(DECREMENT)
      sprite(*s, {{{0, 0, 20, 0}, {0, 0, 60, 20}}, 0x0000'ff00});
      c[GE::StencilTest] = 0 | 0xff << 16, c[GE::StencilOperation] = 4;  //(never: INCREMENT where it fails)
      sprite(*s, {{{0, 0, 48, 30}, {0, 0, 64, 48}}, 0x00ff'ffff});
      c[GE::StencilTest] = 2 | 0xff << 16, c[GE::StencilOperation] = 4 << 16;  //(equal to 0: INCREMENT where it is)
      sprite(*s, {{{0, 0, 36, 0}, {0, 0, 46, 24}}, 0x00ff'00ff});
      c[GE::StencilTest] = 0 | 0xff << 16, c[GE::StencilOperation] = 5;  //(never: DECREMENT where it fails)
      sprite(*s, {{{0, 0, 0, 40}, {0, 0, 16, 48}}, 0x00ff'ffff});
      c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 0;  //(the depth test never passing: the depth fail's)
      c[GE::StencilTest] = 1 | 0xff << 16, c[GE::StencilOperation] = 4 << 8;
      sprite(*s, {{{0, 0, 40, 26}, {0, 0, 48, 48}}, 0x00ff'ffff});
      c[GE::StencilOperation] = 5 << 8;
      sprite(*s, {{{0, 0, 56, 26}, {0, 0, 64, 48}}, 0x00ff'ffff});
      c[GE::DepthTestEnable] = 0;
      c[GE::StencilTest] = 2 | 0xff << 8 | 0xff << 16, c[GE::StencilOperation] = 0;  //(equal to 255)
      sprite(*s, {{{0, 0, 0, 40}, {0, 0, 64, 48}}, 0x0000'00ff});
      c[GE::StencilTestEnable] = 0;
      c[GE::FrameBufferPointer] = 0x1'0000, c[GE::FrameBufferPixelFormat] = 3;
      c[GE::ClearMode] = 1 | 3 << 8;
      sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff80'8080});
      c[GE::ClearMode] = 0, c[GE::AlphaTestEnable] = 1, c[GE::AlphaTest] = 6 | 0xff << 16;  //(its alpha above 0)
      texture(*s, Memory::VRAMBase, 64, 64, 64, 1);
      sprite(*s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
      c[GE::AlphaTestEnable] = 0, c[GE::TextureMappingEnable] = 0;
    }
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %s %ux: %u bytes apart\n", gpu->fast ? "fast" : "accurate", scale, bytes);
    CHECK(bytes, 0u);
    auto bit = [&](u32 x, u32 y) { return software.memory.vram[(y * 64 + x) * 2 + 1] >> 7; };
    CHECK(bit(10, 10), 1u), CHECK(bit(30, 10), 0u), CHECK(bit(52, 35), 1u), CHECK(bit(38, 10), 1u);
    CHECK(bit(10, 44), 0u), CHECK(bit(44, 35), 1u), CHECK(bit(60, 35), 0u), CHECK(bit(44, 22), 1u);
    auto taken = [&](u32 x, u32 y, u32 n) { return u32(software.memory.vram[0x1'0000 + (y * 64 + x) * 4 + n]); };
    CHECK(taken(10, 10, 2), 0xffu), CHECK(taken(30, 22, 2), 0xffu);  //(where it's filled)
    CHECK(taken(30, 10, 2), 0x80u), CHECK(taken(42, 10, 0), 0xffu);  //(where it's emptied; filled over "equal to 0")
    CHECK(taken(52, 35, 2) != 0x80, true), CHECK(taken(44, 35, 2) != 0x80, true);  //(filled where a test failed,
    CHECK(taken(60, 35, 2), 0x80u), CHECK(taken(10, 44, 2), 0x80u);                //the depth test; emptied so)
    hardware.ge.setRenderer(nullptr);
  }
  gpu->resolution(1);
}
static auto gpuStencilBit() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) stencilBit(gpu);
  }
}

//The frame buffer's alpha is its stencil, which stencil operations change where no color is written (MX vs. ATV
//Untamed's race sets its alpha from the depth everywhere, zeroes it where its hills are in front, then blends the sky
//in by it): in each format with a stencil, the left half drawn in front at 40000 and the stencil set everywhere;
//ZERO where the depth test (at least as near as 30000) fails, INVERT where the pass is, then a copy of the frame
//buffer taken as a texture's alpha into another; ZERO behind the hills once more by a 3D mesh (in fast mode the GPU's
//transform), then white blended in by the frame buffer's alpha. (Black under white and the reverse, so each blend
//comes out as the alpha exactly, however the GPU rounds.) Every byte the software renderer's, the colors' alpha set
//from the stencil twice (for the copy, then the blend), not for a blend by the pixel's own alpha after. At 1x and 2x,
//in either mode.
static auto stencilAlpha(GPU* gpu) -> void {
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  for(u32 format : {3u, 2u, 1u}) {
    for(u32 scale : {1u, 2u}) {
      gpu->resolution(scale);
      auto before = stats(gpu);
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      for(System* s : {&software, &hardware}) {
        auto& c = s->ge.commands;
        prepare(*s, 0, format);
        c[GE::ClearMode] = 1 | 7 << 8;  //(the colors black, the stencil 0, the depth 0)
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0x0000'0000});
        c[GE::ClearMode] = 0, c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written)
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0xff00'0000}, 40000);
        c[GE::DepthTestEnable] = 0, c[GE::MaskColor] = 0xff'ffff;  //(the colors kept)
        c[GE::StencilTestEnable] = 1, c[GE::StencilTest] = 1 | 0xc0 << 8 | 0xff << 16;  //(always)
        c[GE::StencilOperation] = 2 << 16;  //(REPLACE where it passes)
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xffff'ffff});
        c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;  //(at least as near, not written)
        c[GE::StencilOperation] = 1 << 8;  //(ZERO where the depth test fails)
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 40}}, 0xffff'ffff}, 30000);
        c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0, c[GE::StencilOperation] = 3 << 16;  //(INVERT)
        sprite(*s, {{{0, 0, 0, 40}, {0, 0, 64, 48}}, 0xffff'ffff});
        c[GE::StencilTestEnable] = 0, c[GE::MaskColor] = 0;
        c[GE::FrameBufferPointer] = 0x1'0000, c[GE::FrameBufferPixelFormat] = 3;
        c[GE::ClearMode] = 1 | 3 << 8;
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xffff'ffff});
        c[GE::ClearMode] = 0, c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 2 | 3 << 4;  //(by the texel's alpha)
        texture(*s, Memory::VRAMBase, 64, 64, 64, format);
        sprite(*s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
        c[GE::AlphaBlendEnable] = 0, c[GE::TextureMappingEnable] = 0;
        //(the mesh: 4 quads over rows 40-47 at depth 30000, seen orthographically, ZERO where it's behind the hills)
        c[GE::FrameBufferPointer] = 0, c[GE::FrameBufferPixelFormat] = format, c[GE::MaskColor] = 0xff'ffff;
        c[GE::StencilTestEnable] = 1, c[GE::StencilOperation] = 1 << 8;
        c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;
        c[GE::VertexType] = 0x1ff, c[GE::MaxZ] = 0xffff;
        for(u32 n = 0; n < 12; n++) s->ge.world[n] = s->ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
        for(u32 n = 0; n < 16; n++) s->ge.projection[n] = f24(n % 5 == 0 ? 1 : 0);
        c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-24), c[GE::ViewportZScale] = f24(30000);
        c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
        c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 24) << 4;
        u32 to = GPUVertices, count = 0;
        for(u32 i = 0; i < 4; i++) {
          for(u32 corner : {0u, 1u, 3u, 0u, 3u, 2u}) {
            float x = -1 + 0.5f * (i + (corner & 1)), y = -1 + (corner >> 1) / 3.0f, z = (30000 - 32768) / 30000.0f;
            for(u32 word : {0u, 0u, 0xffff'ffffu, 0u, 0u, bitsOf(1), bitsOf(x), bitsOf(y), bitsOf(z)}) {
              s->memory.write(4, to, word), to += 4;
            }
            count++;
          }
        }
        s->ge.vertexAddress = GPUVertices;
        s->ge.primitive(GE::Triangles, count);
        c[GE::VertexType] = 0x80'019f, c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
        c[GE::StencilTestEnable] = 0, c[GE::MaskColor] = 0;
        c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 4 | 5 << 4;  //(by the frame buffer's alpha)
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xffff'ffff});
        //(the stencil zeroed behind the hills again, then a blend by the pixel's own alpha: nothing to set)
        c[GE::AlphaBlendEnable] = 0, c[GE::StencilTestEnable] = 1, c[GE::MaskColor] = 0xff'ffff;
        c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 8}}, 0xffff'ffff}, 30000);
        c[GE::StencilTestEnable] = 0, c[GE::MaskColor] = 0, c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
        c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 2 | 3 << 4;
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0x00ff'0000});
        c[GE::AlphaBlendEnable] = 0;
      }
      u32 bytes = apart(software, hardware);
      if(bytes) {
        std::printf("  %s format %u %ux: %u bytes apart\n", gpu->fast ? "fast" : "accurate", format, scale, bytes);
      }
      CHECK(bytes, 0u);
      auto taken = [&](u32 x, u32 y) { return u32(software.memory.vram[0x1'0000 + (y * 64 + x) * 4 + 1]); };
      CHECK(taken(8, 8), 0xffu), CHECK(taken(40, 8) < 0x40, true);  //(behind the hills its alpha 0, else not)
      if(format == 3) {  //(white blended in by it: the alpha)
        auto blended = [&](u32 x, u32 y) { return u32(software.memory.vram[(y * 64 + x) * 4 + 1]); };
        CHECK(blended(8, 8), 0u), CHECK(blended(40, 8), 0xc0u), CHECK(blended(40, 44), 0x3fu);
        CHECK(blended(8, 44), 0u);
      }
      CHECK(stats(gpu).alphas - before.alphas, 2u);
      if(gpu->fast && gpu->backend->transforms) CHECK(stats(gpu).meshes > before.meshes, true);
      hardware.ge.setRenderer(nullptr);
    }
  }
  gpu->resolution(1);
}
static auto gpuStencilAlpha() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) stencilAlpha(gpu);
  }
}

//Each other way a PRIM leaves the frame buffer's alpha behind its stencil, the alpha then read by a blend (white over
//black by the frame buffer's alpha, so the colors come out as the alpha exactly): a stencil test failing; MASK_ALPHA
//keeping some of the stencil's bits, in a PRIM and in clear mode; INCREMENT and DECREMENT; a REPLACE blended by the
//pixel's own alpha without dual-source blending (output 0's alpha carrying it, not the stencil); the GPU's own logic
//operation, which takes the alpha too where the GE's leaves the stencil out (fast mode, where the GPU has them); and
//a 4444 frame buffer's stencil as a REPLACE under a comparison leaves it on the GPU (the GE's reference, 0x80, its
//alpha 0x88 as a read back takes it), set from the stencil with the rest of a rectangle noted for another PRIM. Every
//byte the software renderer's, the alpha set from the stencil in each. At 1x and 2x, in either mode.
static auto everywhere(System& s, u32 reference) -> void {  //(the stencil set to reference everywhere, colors kept)
  auto& c = s.ge.commands;
  c[GE::MaskColor] = 0xff'ffff, c[GE::StencilTestEnable] = 1;
  c[GE::StencilTest] = 1 | reference << 8 | 0xff << 16, c[GE::StencilOperation] = 2 << 16;  //(always: REPLACE)
  sprite(s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xffff'ffff});
}
static auto stencilAlphaWays(GPU* gpu) -> void {
  //(single: drawn without dual-source blending, left behind only by the GPU's own blending, not where every blend is
  //the shader's; logic: left behind only by the GPU's own logic operation, where it has them and doesn't blend in the
  //shader, the shader's leaving the alpha as the GE's does)
  struct Way { const char* name; u32 format; bool single, logic; std::function<void(System&)> draw; };
  std::vector<Way> ways = {
    {"a stencil test failing", 3, false, false, [](System& s) {
      auto& c = s.ge.commands;
      everywhere(s, 0xc0);
      c[GE::StencilTest] = 3 | 0xc0 << 8 | 0xff << 16, c[GE::StencilOperation] = 1;  //(not equal: ZERO where it fails)
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0xffff'ffff});
    }},
    {"MASK_ALPHA keeping some bits", 3, false, false, [](System& s) {
      auto& c = s.ge.commands;
      everywhere(s, 0xc0);
      c[GE::MaskAlpha] = 0x0f, c[GE::StencilTest] = 1 | 0x3f << 8 | 0xff << 16;
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0xffff'ffff});
    }},
    {"MASK_ALPHA keeping some bits in clear mode", 3, false, false, [](System& s) {
      auto& c = s.ge.commands;
      everywhere(s, 0xc0);
      c[GE::StencilTestEnable] = 0, c[GE::MaskAlpha] = 0x0f, c[GE::ClearMode] = 1 | 2 << 8;  //(the stencil alone)
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0x3f00'0000});
      c[GE::ClearMode] = 0;
    }},
    {"INCREMENT and DECREMENT", 3, false, false, [](System& s) {
      auto& c = s.ge.commands;
      everywhere(s, 0xc0);
      c[GE::StencilOperation] = 4 << 16;
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0xffff'ffff});
      c[GE::StencilOperation] = 5 << 16;
      sprite(s, {{{0, 0, 32, 0}, {0, 0, 64, 24}}, 0xffff'ffff});
    }},
    {"a REPLACE blended by its own alpha without dual-source blending", 3, true, false, [](System& s) {
      auto& c = s.ge.commands;
      everywhere(s, 0xc0);
      c[GE::MaskColor] = 0, c[GE::StencilTest] = 1 | 0x3f << 8 | 0xff << 16;
      c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 2 | 3 << 4;  //(black, whole: the colors as they were)
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0xff00'0000});
      c[GE::AlphaBlendEnable] = 0;
    }},
    {"the GPU's own logic operation", 3, false, true, [](System& s) {
      auto& c = s.ge.commands;
      everywhere(s, 0xc0);
      c[GE::MaskColor] = 0, c[GE::StencilTest] = 1 | 0xff << 8 | 0xff << 16, c[GE::LogicOpEnable] = 1;
      for(u32 logic : {6u, 0u, 5u}) {  //(XOR, CLEAR, NOOP: black either way)
        c[GE::LogicOp] = logic;
        sprite(s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0x0000'0000});
      }
      c[GE::LogicOpEnable] = 0;
    }},
    {"a 4444 REPLACE under a comparison", 2, false, false, [](System& s) {
      auto& c = s.ge.commands;
      c[GE::MaskColor] = 0xff'ffff, c[GE::StencilTestEnable] = 1;
      c[GE::StencilTest] = 3 | 0x80 << 8 | 0xff << 16, c[GE::StencilOperation] = 2 << 16;  //(not equal to 0x80)
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0xffff'ffff});
      c[GE::StencilTest] = 0 | 0xff << 16, c[GE::StencilOperation] = 3 << 16;  //(never: INVERT, noted everywhere)
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xffff'ffff});
    }},
  };
  for(auto& way : ways) {
    for(u32 scale : {1u, 2u}) {
      gpu->resolution(scale);
      auto before = stats(gpu);
      bool dualSource = gpu->backend->dualSource;
      if(way.single) gpu->backend->dualSource = false;
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      for(System* s : {&software, &hardware}) {
        auto& c = s->ge.commands;
        prepare(*s, 0, way.format);
        c[GE::ClearMode] = 1 | 7 << 8;  //(black, the stencil 0, the depth 0)
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0x0000'0000});
        c[GE::ClearMode] = 0;
        way.draw(*s);
        c[GE::MaskColor] = 0, c[GE::MaskAlpha] = 0, c[GE::StencilTestEnable] = 0;
        c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 4 | 5 << 4;  //(white, by the frame buffer's alpha)
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xffff'ffff});
        c[GE::AlphaBlendEnable] = 0;
      }
      u32 bytes = apart(software, hardware);
      gpu->backend->dualSource = dualSource;
      if(bytes) {
        std::printf("  %s, %s %ux%s: %u bytes apart\n", way.name, gpu->fast ? "fast" : "accurate", scale,
                    gpu->backend->logicOps ? " (the GPU's logic operations)" : "", bytes);
      }
      CHECK(bytes, 0u);
      bool shaded = gpu->backend->reads && gpu->backend->readsBlending;
      bool left = way.logic ? gpu->backend->logicOps && !gpu->backend->reads : !way.single || !shaded;
      CHECK(stats(gpu).alphas > before.alphas, left);
      hardware.ge.setRenderer(nullptr);
    }
  }
  gpu->resolution(1);
}
static auto gpuStencilAlphaWays() -> void {
  if(!renderer()) return;
  std::printf("  (the GPU's own logic operations: %s)\n", renderer()->backend->logicOps ? "yes" : "no");
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) stencilAlphaWays(gpu);
  }
}

//The frame buffer's alpha behind its stencil noted, and set from it, on part 92's paths too: a PRIM whose settings are
//the last one's keeps them (`keptSettings`), its own rectangle noted all the same (two sprites alike, leaving the
//alpha behind, then white blended in by it over the second's alone), and one blending by it sets it first all the
//same (part 100: two sprites alike blending by it, the first beside what lags, the second over it); and in fast mode
//a PRIM of fewer vertices than MeshLeast is drawn among the meshes (screenTriangle()), one leaving the alpha behind
//noted (a quad's depth fails over the nearer half), one blending by it after another leaving it behind set first.
//Every byte the software renderer's. At 1x and 2x, in either mode.
static auto stencilAlphaPaths(GPU* gpu) -> void {
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  auto quad = [&](System& s, float z) {  //(two triangles over the frame buffer at depth z, seen orthographically)
    auto& c = s.ge.commands;
    c[GE::VertexType] = 0x1ff, c[GE::MaxZ] = 0xffff;
    for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
    for(u32 n = 0; n < 16; n++) s.ge.projection[n] = f24(n % 5 == 0 ? 1 : 0);
    c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-24), c[GE::ViewportZScale] = f24(30000);
    c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
    c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 24) << 4;
    u32 to = GPUVertices;
    for(u32 corner : {0u, 1u, 3u, 0u, 3u, 2u}) {
      float x = -1 + 2.0f * (corner & 1), y = -1 + 2.0f * (corner >> 1), depth = (z - 32768) / 30000.0f;
      for(u32 word : {0u, 0u, 0xffff'ffffu, 0u, 0u, bitsOf(1), bitsOf(x), bitsOf(y), bitsOf(depth)}) {
        s.memory.write(4, to, word), to += 4;
      }
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Triangles, 6);
    c[GE::VertexType] = 0x80'019f;
  };
  struct Path { const char* name; bool screened; std::function<void(System&)> draw; };
  std::vector<Path> paths = {
    {"settings kept", false, [](System& s) {
      auto& c = s.ge.commands;
      everywhere(s, 0xc0);
      c[GE::StencilTest] = 3 | 0xc0 << 8 | 0xff << 16, c[GE::StencilOperation] = 1;  //(not equal: ZERO where it fails)
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 16, 48}}, 0xffff'ffff});
      sprite(s, {{{0, 0, 40, 0}, {0, 0, 56, 48}}, 0xffff'ffff});
      c[GE::MaskColor] = 0, c[GE::StencilTestEnable] = 0;
      c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 4 | 5 << 4;  //(white, by the frame buffer's alpha)
      sprite(s, {{{0, 0, 40, 0}, {0, 0, 56, 48}}, 0xffff'ffff});
      c[GE::AlphaBlendEnable] = 0;
    }},
    {"blending by it, settings kept", false, [](System& s) {
      auto& c = s.ge.commands;
      everywhere(s, 0xc0);
      c[GE::StencilTest] = 3 | 0xc0 << 8 | 0xff << 16, c[GE::StencilOperation] = 1;
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 16, 48}}, 0xffff'ffff});
      c[GE::MaskColor] = 0, c[GE::StencilTestEnable] = 0;
      c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 4 | 5 << 4;
      sprite(s, {{{0, 0, 40, 0}, {0, 0, 56, 48}}, 0xffff'ffff});
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 16, 48}}, 0xffff'ffff});
      c[GE::AlphaBlendEnable] = 0;
    }},
    {"a small PRIM leaving it behind among the meshes", true, [&](System& s) {
      auto& c = s.ge.commands;
      c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written: the nearer half)
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0xff00'0000}, 40000);
      c[GE::DepthTestEnable] = 0;
      everywhere(s, 0xc0);
      c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;  //(at least as near, not written)
      c[GE::StencilOperation] = 1 << 8;  //(ZERO where the depth test fails)
      quad(s, 30000);
      c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0, c[GE::MaskColor] = 0, c[GE::StencilTestEnable] = 0;
      c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 4 | 5 << 4;
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xffff'ffff});
      c[GE::AlphaBlendEnable] = 0;
    }},
    {"a small PRIM blending by it among the meshes", true, [&](System& s) {
      auto& c = s.ge.commands;
      everywhere(s, 0xc0);
      c[GE::StencilTest] = 3 | 0xc0 << 8 | 0xff << 16, c[GE::StencilOperation] = 1;
      sprite(s, {{{0, 0, 0, 0}, {0, 0, 32, 48}}, 0xffff'ffff});
      c[GE::MaskColor] = 0, c[GE::StencilTestEnable] = 0;
      c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 4 | 5 << 4;
      quad(s, 30000);
      c[GE::AlphaBlendEnable] = 0;
    }},
  };
  for(auto& path : paths) {
    for(u32 scale : {1u, 2u}) {
      gpu->resolution(scale);
      auto before = stats(gpu);
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      for(System* s : {&software, &hardware}) {
        auto& c = s->ge.commands;
        prepare(*s, 0, 3);
        c[GE::ClearMode] = 1 | 7 << 8;  //(black, the stencil 0, the depth 0)
        sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0x0000'0000});
        c[GE::ClearMode] = 0;
        path.draw(*s);
      }
      u32 bytes = apart(software, hardware);
      if(bytes) std::printf("  %s, %s %ux: %u bytes apart\n", path.name, gpu->fast ? "fast" : "accurate", scale, bytes);
      CHECK(bytes, 0u);
      CHECK(stats(gpu).alphas > before.alphas, true);
      bool screens = gpu->fast && gpu->screenMeshes && gpu->backend->transforms && !gpu->backend->reads;
      if(path.screened && screens) CHECK(stats(gpu).screened > before.screened, true);
      hardware.ge.setRenderer(nullptr);
    }
  }
  gpu->resolution(1);
}
static auto gpuStencilAlphaPaths() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) stencilAlphaPaths(gpu);
  }
}

//A depth buffer read as a texture through VRAM's depth views (Cars Race-O-Rama takes its race's depth through a
//palette into the alpha its sky is blended by): the depth the GPU drew, put into memory first, each texel the
//software renderer's, through the fourth copy (the depths in order) and the second (rearranged); put once, the second
//read finding memory has it, and again once more depth is drawn. At 1x and 2x, in either mode.
static auto depthAsTexture(GPU* gpu) -> void {
  for(u32 scale : {1u, 2u}) {
    gpu->resolution(scale);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    u64 put = stats(gpu).depthsPut;
    for(System* s : {&software, &hardware}) {
      auto& c = s->ge.commands;
      prepare(*s, 0, 3);
      c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written)
      for(u32 n = 0; n < 4; n++) {
        sprite(*s, {{{0, 0, n * 16.0f, 0}, {0, 0, n * 16.0f + 16, 48}}, 0xff10'2030}, 0x1234 + n * 0x3456);
      }
      sprite(*s, {{{0, 0, 8, 8}, {0, 0, 40, 30}}, 0xff10'2030}, 0xfedc);
      c[GE::DepthTestEnable] = 0, c[GE::FrameBufferPointer] = 0x1'0000;
      texture(*s, 0x0460'0000 + 0x1f'0000, 64, 64, 64, 0);  //(through the fourth copy, as 5650's colors)
      sprite(*s, {{{0, 0, 0, 0}, {64, 24, 64, 24}}, 0xffff'ffff});
      texture(*s, 0x0420'0000 + 0x1f'0000, 64, 64, 64, 0);  //(the second)
      sprite(*s, {{{0, 24, 0, 24}, {64, 48, 64, 48}}, 0xffff'ffff});
      if(s == &hardware) CHECK(stats(gpu).depthsPut - put, 1u);
      c[GE::TextureMappingEnable] = 0, c[GE::FrameBufferPointer] = 0, c[GE::DepthTestEnable] = 1;
      sprite(*s, {{{0, 0, 30, 20}, {0, 0, 60, 44}}, 0xff10'2030}, 0x0101);
      c[GE::DepthTestEnable] = 0, c[GE::FrameBufferPointer] = 0x2'0000;
      texture(*s, 0x0460'0000 + 0x1f'0000, 64, 64, 64, 0);
      sprite(*s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
      if(s == &hardware) CHECK(stats(gpu).depthsPut - put, 2u);
      c[GE::TextureMappingEnable] = 0;
    }
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %s %ux: %u bytes apart\n", gpu->fast ? "fast" : "accurate", scale, bytes);
    CHECK(bytes, 0u);
    auto red = [&](u32 buffer, u32 x, u32 y) { return u32(software.memory.vram[buffer + (y * 64 + x) * 4]); };
    CHECK(red(0x1'0000, 4, 4), u32(0x1234 & 31) << 3 | (0x1234 & 31) >> 2);  //(the first strip's depth, as 5650)
    CHECK(red(0x2'0000, 40, 30), u32(0x0101 & 31) << 3 | (0x0101 & 31) >> 2);  //(the depth drawn since)
    hardware.ge.setRenderer(nullptr);
  }
  gpu->resolution(1);
}
//Memory's changes to a depth buffer two frame buffers draw with, around a texture read from it through VRAM's depth
//views. The CPU writes all of it (so that filling it from memory is exact: a change fills all of each 16 KiB it
//touches), and the second frame buffer takes the first's depth with that change filled into it first; the CPU writes
//it again, the texture puts the first's depth into memory (that change filled in), and the second, holding the same
//depth, still fills the change before it draws again: every byte the software renderer's.
static auto depthChanges(GPU* gpu) -> void {
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  auto before = stats(gpu);
  for(System* s : {&software, &hardware}) {
    auto& c = s->ge.commands;
    auto depths = [&](u32 rows) {  //(through the fourth copy, in order: those rows 0, the rest 40000)
      for(u32 y = 0; y < 48; y++) {
        for(u32 x = 0; x < 64; x += 2) {
          u32 z = y < rows ? 0 : 40000;
          s->memory.write(4, 0x0460'0000 + 0x1f'0000 + (y * 64 + x) * 2, z | z << 16);
        }
      }
    };
    prepare(*s, 0, 3);
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;  //(always, written)
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030}, 40000);
    depths(8);
    c[GE::FrameBufferPointer] = 0x1'0000, c[GE::DepthTest] = 7, c[GE::DepthMask] = 1;  //(greater or equal, kept)
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff00'ff00}, 20000);
    depths(16);
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0, c[GE::FrameBufferPointer] = 0x2'0000;
    texture(*s, 0x0460'0000 + 0x1f'0000, 64, 64, 64, 0);
    sprite(*s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
    c[GE::TextureMappingEnable] = 0, c[GE::FrameBufferPointer] = 0x1'0000;
    c[GE::DepthTestEnable] = 1, c[GE::DepthMask] = 1;
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xffff'00ff}, 20000);
    c[GE::DepthTestEnable] = 0, c[GE::DepthMask] = 0;
  }
  u32 bytes = apart(software, hardware);
  if(bytes) std::printf("  %s: %u bytes apart\n", gpu->fast ? "fast" : "accurate", bytes);
  CHECK(bytes, 0u);
  auto red = [&](u32 x, u32 y) { return u32(software.memory.vram[0x1'0000 + (y * 64 + x) * 4]); };
  CHECK(red(8, 4), 0xffu), CHECK(red(8, 12), 0xffu), CHECK(red(8, 30), 0u);  //(in front of the second change only)
  CHECK(stats(gpu).depthsTaken - before.depthsTaken, 1u);
  CHECK(stats(gpu).depthsPut - before.depthsPut, 1u);
  hardware.ge.setRenderer(nullptr);
}
static auto gpuDepthAsTexture() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(gpu) depthAsTexture(gpu), depthChanges(gpu);
  }
}

//The GE's depths as a GPU's depth formats keep them, and back (GPU::depthToFloat() and the rest: a backend's uploads
//and read backs): all 65,536 the same again from D32, and from D24 whether the GPU rounded a fragment's 65,536ths to
//nearest or down, its top byte whatever it is. No GPU needed.
static auto gpuDepthFormats() -> void {
  u32 wrong = 0;
  for(u32 z = 0; z < 65536; z++) {
    f64 exact = z / 65536.0 * 0xff'ffff;
    wrong += GPU::depthFromFloat(GPU::depthToFloat(z)) != z;
    wrong += GPU::depthFrom24(GPU::depthTo24(z)) != z;
    wrong += GPU::depthFrom24(u32(std::floor(exact)) | 0xff00'0000) != z;
    wrong += GPU::depthFrom24(u32(std::floor(exact + 0.5))) != z;
  }
  CHECK(wrong, 0u);
}

//Bytes beside the GPU's pixels, in the unused columns right of a picture (where games keep their lists): the CPU
//writes and reads them without waiting for the GPU, a sprite beside them doesn't wait either, and a sprite over
//them, once the GPU has its pixels from memory again, draws as the software renderer does.
static auto gpuBeside() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  u32 list = 0x0400'0000 + (10 * 64 + 48) * 4;  //(row 10, columns 48-63)
  u64 finishes = 0;
  u32 read = 0;
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);
    s->ge.commands[GE::Scissor2] = 47 | 47 << 10;  //(a picture 48 pixels wide in rows of 64)
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 48, 48}}, 0xff00'00ff});
    finishes = stats(gpu).finishes;
    for(u32 n = 0; n < 16; n++) s->memory.write(4, list + n * 4, 0x1234'5600 + n);
    for(u32 n = 0; n < 16; n++) read += s->memory.read(4, list + n * 4) == 0x1234'5600 + n;
    sprite(*s, {{{0, 0, 4, 4}, {0, 0, 20, 20}}, 0xff00'ff00});
  }
  CHECK(read, 32u);
  CHECK(stats(gpu).finishes, finishes);  //(the hardware machine's writes, reads and sprite: no wait)
  for(System* s : {&software, &hardware}) {
    s->ge.commands[GE::Scissor2] = 63 | 47 << 10;
    sprite(*s, {{{0, 0, 40, 8}, {0, 0, 56, 12}}, 0xffff'0000});
  }
  CHECK(apart(software, hardware), 0u);
  CHECK(software.memory.read(4, list + 12 * 4), 0x1234'560cu);  //(column 60: the list's, not drawn over)
  //a list written a row at a time, in more pieces than the 16 ranges kept (Brave Story's): those filled from memory
  //first, as one rectangle (they reach none of its pixels), nothing finished; then a sprite over them
  finishes = stats(gpu).finishes;
  u64 uploads = stats(gpu).uploads;
  for(System* s : {&software, &hardware}) {
    sprite(*s, {{{0, 0, 0, 20}, {0, 0, 32, 44}}, 0xff20'4060});
    for(u32 n = 0; n < 24; n++) s->memory.write(4, 0x0400'0000 + ((20 + n) * 64 + 60) * 4, 0x5566'7700 + n);
  }
  CHECK(stats(gpu).uploads - uploads <= 2, true);
  for(System* s : {&software, &hardware}) sprite(*s, {{{0, 0, 58, 16}, {0, 0, 64, 40}}, 0xff80'a0c0});
  CHECK(stats(gpu).finishes, finishes);
  CHECK(apart(software, hardware), 0u);
  CHECK(software.memory.read(4, 0x0400'0000 + (43 * 64 + 60) * 4), 0x5566'7717u);
  hardware.ge.setRenderer(nullptr);
}

//The start-up check (check.cpp) a game's renderer goes through: on a GPU that draws right it passes, and leaves the
//renderer as it was for the next machine (everything dropped, nothing of its own machine's kept). Where the GPU
//reads the frame buffer, its game-sized picture's 128 blended sprites read it; with nothing read, as
//System::startRenderer() falls back to where reading fails, it passes too, reading nothing.
static auto gpuCheck() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::string error;
  auto before = stats(gpu);
  bool passed = gpu->check(error);
  if(!passed) std::printf("  %s\n", error.c_str());
  CHECK(passed, true);
  CHECK(gpu->ready(), true);
  if(gpu->backend->reads) CHECK(stats(gpu).readingDraws - before.readingDraws >= 128, true);
  bool reads = gpu->backend->reads, readsBlending = gpu->backend->readsBlending;
  gpu->backend->reads = gpu->backend->readsBlending = false;
  before = stats(gpu), error.clear();
  passed = gpu->check(error);
  if(!passed) std::printf("  nothing read: %s\n", error.c_str());
  CHECK(passed, true);
  CHECK(stats(gpu).readingDraws, before.readingDraws);
  gpu->backend->reads = reads, gpu->backend->readsBlending = readsBlending;
}

//At 2 and 3 times the PSP's resolution (GPU::resolution()): flat sprites over VRAM full of random bytes, in each
//format, are the software renderer's byte for byte once read back (each pixel's scale x scale the same; memory's
//bytes the GPU didn't draw over, the stencils too, back as they went in, through copy.frag's enlarging and the
//blit's shrinking: their stencils a bit at a time and, where a shader can give them, with the colors, stencilWays(),
//each exactly, as a sprite drawn first where the stencil is 255 shows); the screen's picture at the target's own
//resolution is them, scale x scale each; render to texture from a copy at
//the target's resolution, sampled 1:1, is exact too; and the depth buffer and the bytes beside the pixels behave as
//at the PSP's own (gpuDepth, gpuBeside, run again).
static auto gpuScaled() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(!scaleTaken(gpu, 3)) return;
  std::mt19937 random{20261012};
  for(u32 scale : {2u, 3u}) {
    gpu->resolution(scale);
    CHECK(gpu->resolution(), scale);
    bool exports = gpu->backend->stencilExport;
    for(bool exported : stencilWays(gpu)) {
      gpu->backend->stencilExport = exported;
      for(u32 format : {3u, 0u, 1u, 2u}) {
        System software, hardware;
        hardware.ge.setRenderer(gpu);
        auto flat = randomSprites(random, 40, false);
        std::vector<u8> noise(0x4'0000);
        for(auto& byte : noise) byte = random();
        for(System* s : {&software, &hardware}) {
          std::memcpy(s->memory.vram.data(), noise.data(), noise.size());
          prepare(*s, 0, format);
          if(format) {  //(first, over all of it, where the stencil filled in is 255: the GPU's stencil decides)
            s->ge.commands[GE::StencilTestEnable] = 1, s->ge.commands[GE::StencilTest] = 2 | 0xff << 8 | 0xff << 16;
            sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff20'4060});
            s->ge.commands[GE::StencilTestEnable] = 0;
          }
          for(auto& one : flat) sprite(*s, one);
          s->ge.commands[GE::ClearMode] = 1 | 7 << 8;
          for(u32 n = 0; n < 4; n++) sprite(*s, flat[n]);
          s->ge.commands[GE::ClearMode] = 0;
        }
        if(format == 3) {  //(the screen's picture, before anything's read back: one of each pixel's scale x scale)
          std::vector<u32> pixels;
          CHECK(gpu->picture(0, 64, 3, 64, 48, pixels, scale), true);
          CHECK(pixels.size(), 64u * 48 * scale * scale);
          u32 wrong = 0;
          for(u32 y = 0; y < 48 && pixels.size() == 64u * 48 * scale * scale; y++) {
            for(u32 x = 0; x < 64; x++) {
              u32 shown = pixels[(y * scale + scale / 2) * 64 * scale + x * scale + scale / 2] & 0xff'ffff;
              u32 drawn = software.memory.read(4, 0x0400'0000 + (y * 64 + x) * 4) & 0xff'ffff;
              wrong += shown != drawn;
            }
          }
          CHECK(wrong, 0u);
        }
        u32 bytes = apart(software, hardware);
        if(bytes) std::printf("  %ux, format %u: %u bytes apart\n", scale, format, bytes);
        CHECK(bytes, 0u);
        hardware.ge.setRenderer(nullptr);
      }
    }
    gpu->backend->stencilExport = exports;
    {
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      auto drawn = randomSprites(random, 40, false);
      //(1:1, whole pixels: each of the GPU's pixels in the PSP's it samples; the same ones for both machines)
      struct Sampled { float x, y, w, h, u, v; };
      std::vector<Sampled> sampled;
      for(u32 n = 0; n < 12; n++) {
        float x = random() % 40, y = random() % 30, w = 1 + random() % 24, h = 1 + random() % 18;
        float u = random() % 40, v = random() % 30;
        sampled.push_back({x, y, w, h, u, v});
      }
      for(System* s : {&software, &hardware}) {
        prepare(*s, 0, 3);
        for(auto& one : drawn) sprite(*s, one);
        prepare(*s, 0x2'0000, 3);
        texture(*s, Memory::VRAMBase, 64, 64, 64);
        for(auto& d : sampled) {
          sprite(*s, {{{d.u, d.v, d.x, d.y}, {d.u + d.w, d.v + d.h, d.x + d.w, d.y + d.h}}, 0xffff'ffff});
        }
      }
      CHECK(apart(software, hardware), 0u);
      hardware.ge.setRenderer(nullptr);
    }
    gpuDepth();
    gpuBeside();
  }
  gpu->resolution(1);
}

//Above 1x, two 2D quads of two triangles each meeting at a seam, the right one's texture mirrored (as Ridge Racer 2's
//menu draws its picture), filtered, from a texture whose texels past the picture, across and down, are black: each
//of the GPU's pixels at the quads' edges and at the seam keeps to the picture's texels, as the PSP's pixels' middles
//do (white, none of the black blended in: triangle()'s held texels), and memory's bytes are the software renderer's.
static auto gpuSeams() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  //(x from left to right and y 0-32, u from first to last and v 0-32)
  auto quad = [](System& s, float left, float right, float first, float last) {
    float corners[6][4] = {{first, 0, left, 0}, {last, 0, right, 0}, {first, 32, left, 32},
                           {last, 0, right, 0}, {last, 32, right, 32}, {first, 32, left, 32}};
    u32 to = GPUVertices;
    for(auto& corner : corners) {
      for(float value : {corner[0], corner[1]}) s.memory.write(4, to, bitsOf(value)), to += 4;
      s.memory.write(4, to, 0xffff'ffff), to += 4;
      for(float value : {corner[2], corner[3], 0.0f}) s.memory.write(4, to, bitsOf(value)), to += 4;
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Triangles, 6);
  };
  for(u32 scale : {1u, 2u, 3u}) {
    if(!scaleTaken(gpu, scale)) continue;
    gpu->resolution(scale);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      //(the picture white, 32x32; past it, across and down, black)
      for(u32 n = 0; n < 64 * 64; n++) {
        s->memory.write(4, GPUTexture + n * 4, n % 64 < 32 && n / 64 < 32 ? 0xffff'ffff : 0xff00'0000);
      }
      prepare(*s, 0, 3);
      texture(*s, GPUTexture, 64, 64, 64);
      s->ge.commands[GE::TextureFilter] = 1 | 1 << 8;
      quad(*s, 0, 32, 0, 32);
      quad(*s, 32, 64, 32, 0);
    }
    std::vector<u32> pixels;
    CHECK(gpu->picture(0, 64, 3, 64, 32, pixels, scale), true);
    CHECK(pixels.size(), 64u * 32 * scale * scale);
    u32 blended = 0;
    for(u32 y = 0; y < 32 * scale && pixels.size() == 64u * 32 * scale * scale; y++) {
      for(u32 x = 0; x < 64 * scale; x++) blended += (pixels[y * 64 * scale + x] & 0xff'ffff) != 0xff'ffff;
    }
    if(blended) std::printf("  %ux: %u of the GPU's pixels not the picture's white\n", scale, blended);
    CHECK(blended, 0u);
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %ux: %u bytes apart\n", scale, bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
  }
  gpu->resolution(1);
}

//Above 1x, 2D sprites texel for texel, filtered, across, mirrored and turned, from a texture whose texels past the
//picture, across and down, are black, repeating (the texel before its first is the black at its far end) or held at
//its edges, in each mode: each of the GPU's pixels at a sprite's edges keeps to the picture's texels, as the PSP's
//pixels' middles do (white, none of the black blended in: sprite()'s held pixels; Burnout Legends' banner, repeating,
//showed its dark top row along its bottom), and memory's bytes are the software renderer's. The texture in RAM, in
//VRAM where the CPU wrote it, and a frame buffer drawn first (a copy at its resolution, not held: white too).
static auto gpuSpriteSeams() -> void {
  if(!renderer()) return;
  //(across, mirrored, and turned: its bottom-left corner first)
  const Corners sprites[3] = {{{{0, 0, 4, 4}, {16, 16, 20, 20}}, 0xffff'ffff},
                              {{{16, 0, 28, 4}, {0, 16, 44, 20}}, 0xffff'ffff},
                              {{{0, 0, 4, 44}, {16, 16, 20, 28}}, 0xffff'ffff}};
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(!gpu) continue;
    for(u32 scale : {1u, 2u, 3u}) {
      if(!scaleTaken(gpu, scale)) continue;
      gpu->resolution(scale);
      for(u32 source : {0u, 1u, 2u}) {  //(RAM, VRAM the CPU wrote, a frame buffer)
        for(bool held : {false, true}) {
          System software, hardware;
          hardware.ge.setRenderer(gpu);
          u32 address = source == 0 ? GPUTexture : Memory::VRAMBase + (source == 1 ? 0x8'0000 : 0);
          for(System* s : {&software, &hardware}) {
            //(the picture white, 16x16, in a texture of 32x32; past it, across and down, black)
            if(source < 2) {
              for(u32 n = 0; n < 32 * 32; n++) {
                s->memory.write(4, address + n * 4, n % 32 < 16 && n / 32 < 16 ? 0xffff'ffff : 0xff00'0000);
              }
            } else {
              prepare(*s, 0, 3);
              sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff00'0000});
              sprite(*s, {{{0, 0, 0, 0}, {0, 0, 16, 16}}, 0xffff'ffff});
            }
            prepare(*s, 0x2'0000, 3);
            sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030});  //(all the picture's rows the GPU's)
            texture(*s, address, 32, 32, source < 2 ? 32 : 64);
            s->ge.commands[GE::TextureFilter] = 1 | 1 << 8;
            s->ge.commands[GE::TextureWrap] = held ? 0x101 : 0;
            for(auto& one : sprites) sprite(*s, one);
          }
          std::vector<u32> pixels;
          drainThreads();  //(before the picture: with the GE on its thread, the GPU is that thread's till it's done)
          CHECK(gpu->picture(0x2'0000, 64, 3, 64, 48, pixels, scale), true);
          CHECK(pixels.size(), 64u * 48 * scale * scale);
          u32 blended = 0;
          for(auto& one : sprites) {
            u32 left = u32(std::min(one.at[0][2], one.at[1][2])), right = u32(std::max(one.at[0][2], one.at[1][2]));
            u32 top = u32(std::min(one.at[0][3], one.at[1][3])), bottom = u32(std::max(one.at[0][3], one.at[1][3]));
            for(u32 y = top * scale; y < bottom * scale && pixels.size() == 64u * 48 * scale * scale; y++) {
              for(u32 x = left * scale; x < right * scale; x++) {
                blended += (pixels[y * 64 * scale + x] & 0xff'ffff) != 0xff'ffff;
              }
            }
          }
          if(blended) {
            std::printf("  %s, %ux, the texture from %s, %s: %u of the GPU's pixels not the picture's white\n",
                        gpu->fast ? "fast" : "accurate", scale, source == 0 ? "RAM" : source == 1 ? "VRAM" : "a copy",
                        held ? "held" : "repeating", blended);
          }
          CHECK(blended, 0u);
          CHECK(apart(software, hardware), 0u);
          hardware.ge.setRenderer(nullptr);
        }
      }
    }
    gpu->resolution(1);
  }
}

//Shader blending (docs/psp-gpu-renderers.md, "Shader blending"), where the GPU reads the frame buffer: overlapping
//sprites blended with random factors (fixed ones too) and operations, dithered or not, with random logic operations
//and write masks that keep part of a channel, over a frame buffer cleared to random colors and stencils, in each
//format, at the PSP's resolution and at twice it: the software renderer's bytes exactly. Consecutive sprites with
//the same settings are one draw, so where the GPU doesn't keep their order the renderer must split them (emit()).
//Every blend reads here (Backend::readsBlending), as it does in rasterization order: without the order games
//have the GPU's own blending (gpuBlendingApart).
static auto gpuBlending() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(!gpu->backend->reads) return skip("vulkan-reads", "the GPU doesn't read the frame buffer");
  std::printf("  %s\n", gpu->backend->readsInOrder ? "in rasterization order" : "draws split where they overlap");
  bool readsBlending = gpu->backend->readsBlending;
  gpu->backend->readsBlending = true;
  std::mt19937 random{20261013};
  for(u32 scale : {1u, 2u}) {
    if(!scaleTaken(gpu, scale)) continue;
    gpu->resolution(scale);
    for(u32 format : {3u, 0u, 1u, 2u}) {
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      auto before = stats(gpu);
      auto base = randomSprites(random, 12, false);
      struct Round { u32 mode, fixedA, fixedB, dither, logic, mask, alphaMask; std::vector<Corners> sprites; };
      std::vector<Round> rounds;
      for(u32 n = 0; n < 24; n++) {
        Round r;
        u32 source = random() % 11, destination = random() % 11, operation = random() % 8;
        r.mode = source | destination << 4 | operation << 8;
        r.fixedA = random() & 0xff'ffff, r.fixedB = random() & 0xff'ffff;
        r.dither = random() % 2, r.logic = random() % 3 ? 3 : random() % 16;
        r.mask = random() % 3 ? 0 : random() & 0xff'ffff;
        //(0 keep none, 0xff keep all, else some of the stencil/alpha bits — the shader's write mask)
        r.alphaMask = random() % 3 ? 0xff : random() % 2 ? 0 : random() & 0xff;
        r.sprites = randomSprites(random, 8, false);
        rounds.push_back(r);
      }
      for(System* s : {&software, &hardware}) {
        auto& c = s->ge.commands;
        prepare(*s, 0, format);
        c[GE::ClearMode] = 1 | 7 << 8;  //(colors and stencils, random: the destination's alpha is the stencil)
        for(auto& one : base) sprite(*s, one);
        c[GE::ClearMode] = 0;
        c[GE::Dither0] = 0x7f80, c[GE::Dither0 + 1] = 0x3c4d;
        c[GE::Dither0 + 2] = 0xe1a5, c[GE::Dither0 + 3] = 0x96b2;
        for(auto& r : rounds) {
          c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = r.mode;
          c[GE::BlendFixedA] = r.fixedA, c[GE::BlendFixedB] = r.fixedB;
          c[GE::DitherEnable] = r.dither;
          c[GE::LogicOpEnable] = r.logic != 3, c[GE::LogicOp] = r.logic;
          c[GE::MaskColor] = r.mask, c[GE::MaskAlpha] = r.alphaMask;
          for(auto& one : r.sprites) sprite(*s, one);
        }
        c[GE::AlphaBlendEnable] = 0, c[GE::DitherEnable] = 0, c[GE::LogicOpEnable] = 0;
        c[GE::MaskColor] = 0x0f'f00f, c[GE::MaskAlpha] = 0xf0;
        c[GE::ClearMode] = 1 | 3 << 8;  //(clear colors and stencil through write masks keeping some of each)
        for(u32 n = 0; n < 4; n++) sprite(*s, rounds[0].sprites[n]);
        c[GE::ClearMode] = 0, c[GE::MaskColor] = 0, c[GE::MaskAlpha] = 0;
      }
      u32 bytes = apart(software, hardware);
      if(bytes) std::printf("  %ux, format %u: %u bytes apart\n", scale, format, bytes);
      CHECK(bytes, 0u);
      CHECK(stats(gpu).readingDraws > before.readingDraws, true);
      if(!gpu->backend->readsInOrder) CHECK(stats(gpu).splits > before.splits, true);
      hardware.ge.setRenderer(nullptr);
    }
  }
  gpu->resolution(1);
  gpu->backend->readsBlending = readsBlending;
}

//Without rasterization order (Backend::readsInOrder off, as here on any GPU that reads), a primitive that reads waits
//for those before it that it overlaps, a draw and a barrier each, so games' blending, thousands of primitives a frame
//over each other, would be thousands of draws and barriers: the GPU blends with its own units then, and only what
//they can't come close to reads. Blending in 8888 with the factors and operations the GPU has draws nothing in the
//shader and splits nothing,
//close to the software renderer (the source term pixel.cpp's whole number, draw.frag's TERM: at least 85% of the
//channels the same, no more than 3 in 1000 more than 8 off), as on a GPU that reads nothing at all; a write mask
//keeping part of a channel, a logic operation, the absolute difference and doubled alphas read, dithered or not, and
//so does every blend in a 16-bit frame buffer (the PSP narrows each blend's result to the format before the next
//reads it, which the GPU's 8-bit target doesn't): the software renderer's bytes exactly, in each format.
static auto gpuBlendingApart() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  bool readsInOrder = gpu->backend->readsInOrder, readsBlending = gpu->backend->readsBlending;
  gpu->backend->readsInOrder = gpu->backend->readsBlending = false;
  std::mt19937 random{20261014};
  struct Round { u32 mode, fixedA, fixedB, dither, logic, mask; std::vector<Corners> sprites; };
  auto draw = [&](System& s, u32 format, std::vector<Corners>& base, std::vector<Round>& rounds) {
    auto& c = s.ge.commands;
    prepare(s, 0, format);
    c[GE::Dither0] = 0x7f80, c[GE::Dither0 + 1] = 0x3c4d, c[GE::Dither0 + 2] = 0xe1a5, c[GE::Dither0 + 3] = 0x96b2;
    c[GE::ClearMode] = 1 | 7 << 8;
    for(auto& one : base) sprite(s, one);
    c[GE::ClearMode] = 0;
    for(auto& r : rounds) {
      c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = r.mode;
      c[GE::BlendFixedA] = r.fixedA, c[GE::BlendFixedB] = r.fixedB;
      c[GE::DitherEnable] = r.dither;
      c[GE::LogicOpEnable] = r.logic != 3, c[GE::LogicOp] = r.logic;
      c[GE::MaskColor] = r.mask;
      for(auto& one : r.sprites) sprite(s, one);
    }
    c[GE::AlphaBlendEnable] = 0, c[GE::DitherEnable] = 0, c[GE::LogicOpEnable] = 0, c[GE::MaskColor] = 0;
  };
  static constexpr u32 own[] = {0, 1, 2, 3, 4, 5, 7, 10};
  //the GPU's own blending, in 8888: factors 0-5, 7 and fixed (10), operations 0-4, no logic operation or mask
  {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto before = stats(gpu);
    auto base = randomSprites(random, 12, false);
    std::vector<Round> rounds;
    for(u32 n = 0; n < 24; n++) {
      //(without dual-source blending, a destination factor by the pixel's color, or one less twice its alpha, is
      //approximated far off: settings())
      static constexpr u32 single[] = {2, 3, 4, 5, 10};
      u32 source = own[random() % 8];
      u32 destination = gpu->backend->dualSource ? own[random() % 8] : single[random() % 5];
      u32 mode = source | destination << 4 | (random() % 5) << 8;
      rounds.push_back({mode, u32(random()) & 0xff'ffff, u32(random()) & 0xff'ffff, 0, 3, 0,
                        randomSprites(random, 8, false)});
    }
    for(System* s : {&software, &hardware}) draw(*s, 3, base, rounds);
    CHECK(stats(gpu).primitives > before.primitives, true);
    software.ge.settleAll(), hardware.ge.settleAll();
    u64 channels = 0, same = 0, far = 0;
    for(u32 n = 0; n < 64 * 48 * 4; n++) {
      if(n % 4 == 3) continue;
      s32 off = std::abs(s32(software.memory.vram[n]) - s32(hardware.memory.vram[n]));
      channels++, same += off == 0, far += off > 8;
    }
    std::printf("  the GPU's own blending: of %llu channels %llu the same, %llu more than 8 off\n",
                (unsigned long long)channels, (unsigned long long)same, (unsigned long long)far);
    CHECK(far * 1000 <= channels * 3, true);
    CHECK(same * 20 >= channels * 17, true);
    CHECK(stats(gpu).readingDraws, before.readingDraws);
    CHECK(stats(gpu).splits, before.splits);
    hardware.ge.setRenderer(nullptr);
  }
  //and dithered, by the source's alpha or a fixed factor, adding or subtracting: the dither goes in the source term
  //(draw.frag's TERM), at least 75% of the channels the same (the M1's 87%, Mesa's lavapipe's 81%, as GPUs round their
  //own blending's sums a little differently; undithered, only the matrix's one 0 would be); those far
  //off, no more than 1 in 100, are dark blends layered over each other, where the term with a dither below 0 is held
  //at 0 by the GPU (Death Jr. II's glow is a dithered blend whose matrix is all ones and twos)
  {
    std::mt19937 dithering{20261083};  //(its own, so the rounds after are as they were)
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto before = stats(gpu);
    auto base = randomSprites(dithering, 12, false);
    std::vector<Round> rounds;
    for(u32 n = 0; n < 24; n++) {
      static constexpr u32 sources[] = {2, 3, 7, 10}, destinations[] = {2, 3, 4, 5, 10};
      u32 mode = sources[dithering() % 4] | destinations[dithering() % 5] << 4 | (dithering() % 3) << 8;
      rounds.push_back({mode, u32(dithering()) & 0xff'ffff, u32(dithering()) & 0xff'ffff, 1, 3, 0,
                        randomSprites(dithering, 8, false)});
    }
    for(System* s : {&software, &hardware}) draw(*s, 3, base, rounds);
    software.ge.settleAll(), hardware.ge.settleAll();
    u64 channels = 0, same = 0, far = 0;
    for(u32 n = 0; n < 64 * 48 * 4; n++) {
      if(n % 4 == 3) continue;
      s32 off = std::abs(s32(software.memory.vram[n]) - s32(hardware.memory.vram[n]));
      channels++, same += off == 0, far += off > 8;
    }
    std::printf("  the GPU's own blending, dithered: of %llu channels %llu the same, %llu more than 8 off\n",
                (unsigned long long)channels, (unsigned long long)same, (unsigned long long)far);
    CHECK(far * 100 <= channels, true);
    CHECK(same * 4 >= channels * 3, true);
    CHECK(stats(gpu).readingDraws, before.readingDraws);
    hardware.ge.setRenderer(nullptr);
  }
  if(!gpu->backend->reads) {
    skip("vulkan-reads", "the GPU doesn't read the frame buffer (what reads)");
    gpu->backend->readsInOrder = readsInOrder, gpu->backend->readsBlending = readsBlending;
    return;
  }
  //what reads: each round a mask keeping part of a channel, a logic operation, the absolute difference or a doubled
  //alpha (as source or destination factor), with random factors and operations besides; in a 16-bit frame buffer
  //also the GPU's own factors and operations alone, and then one sprite blended so, a draw that reads
  for(u32 format : {3u, 0u, 1u, 2u}) {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto before = stats(gpu);
    auto base = randomSprites(random, 12, false);
    std::vector<Round> rounds;
    for(u32 n = 0; n < 24; n++) {
      u32 source = random() % 11, destination = random() % 11, operation = random() % 6;
      u32 logic = 3, mask = 0, reason = n % (format == 3 ? 5 : 6);
      if(reason == 0) mask = (random() & 0x7f'7f7f) | 0x01'0101;
      if(reason == 1) logic = random() % 15, logic += logic >= 3;
      if(reason == 2) operation = 5;
      static constexpr u32 doubled[] = {6, 8, 9};
      if(reason == 3) source = doubled[random() % 3];
      if(reason == 4) destination = doubled[random() % 3];
      if(reason == 5) source = own[random() % 8], destination = own[random() % 8], operation = random() % 5;
      rounds.push_back({source | destination << 4 | operation << 8, u32(random()) & 0xff'ffff,
                        u32(random()) & 0xff'ffff, u32(random()) % 2, logic, mask, randomSprites(random, 8, false)});
    }
    for(System* s : {&software, &hardware}) draw(*s, format, base, rounds);
    CHECK(stats(gpu).readingDraws > before.readingDraws, true);
    CHECK(stats(gpu).splits > before.splits, true);
    if(format != 3) {
      auto between = stats(gpu);
      for(System* s : {&software, &hardware}) {
        s->ge.commands[GE::AlphaBlendEnable] = 1, s->ge.commands[GE::BlendMode] = 2 | 3 << 4;
        sprite(*s, {{{0, 0, 4, 4}, {0, 0, 36, 28}}, 0x80'40c0ff});
        s->ge.commands[GE::AlphaBlendEnable] = 0;
      }
      CHECK(stats(gpu).readingDraws - between.readingDraws, u64(1));
    }
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  format %u: %u bytes apart\n", format, bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
  }
  gpu->backend->readsInOrder = readsInOrder, gpu->backend->readsBlending = readsBlending;
}

//A frame buffer sampled as a texture declared taller than its picture (Midnight Club 3's bloom samples its 480x272
//picture as a texture of 512 rows, whose last rows are other frame buffers'): 2D sprites, filtered, taking texels
//from its first 48 rows, the last row's second texel at weight 0, while the rows past are another frame buffer's on
//the GPU. The copy is taken on the GPU of the rows the sprites reach (draw.cpp's spriteReach()), nothing finished,
//and the pixels are the software renderer's. Then the frame shown late (System's "Late Frames": GPU::shoot()), whose
//shot is the frame buffer as memory has it once its run is done; and a short list's draws not handed to the GPU at
//its end (GPU::submit()), but with the next frame or finish.
static auto gpuTallTexture() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261009};
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  auto drawn = randomSprites(random, 40, false);
  GPU::Statistics before;
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);
    for(auto& one : drawn) sprite(*s, one);
    prepare(*s, 48 * 256, 0);  //(another frame buffer, 5650, from the first's row 48)
    sprite(*s, {{{0, 0, 0, 0}, {0, 0, 64, 16}}, 0x1234'5678});
    prepare(*s, 0x2'0000, 3);
    texture(*s, Memory::VRAMBase, 64, 128, 64);
    s->ge.commands[GE::TextureFilter] = 0x101;
    if(s == &hardware) before = stats(gpu);
    sprite(*s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0});  //(1:1: row 47's v 47.5, rows 47 and 48, 48 at weight 0)
    sprite(*s, {{{2, 1, 0, 0}, {34, 47, 20, 30}}, 0});
  }
  CHECK(stats(gpu).finishes, before.finishes);
  CHECK(stats(gpu).copies > before.copies, true);
  CHECK(apart(software, hardware), 0u);

  //late: the frame buffer at 0x2'0000 shown, its shot taken; there once the run is done (finished here)
  prepare(hardware, 0x2'0000, 3);
  hardware.ge.commands[GE::TextureMappingEnable] = 0;
  sprite(hardware, {{{0, 0, 0, 0}, {0, 0, 8, 8}}, 0xff00'ff00});
  std::vector<u32> pixels;
  u32 width = 0, height = 0, format = 0;
  while(gpu->shot(pixels, width, height, format)) {}  //(none older left)
  CHECK(gpu->shoot(0x2'0000, 64, 3, 64, 48), true);
  hardware.ge.settleAll();
  CHECK(gpu->shot(pixels, width, height, format), true);
  CHECK(width == 64 && height == 48 && format == 3 && pixels.size() == 64u * 48, true);
  u32 same = 0;
  for(u32 y = 0; y < 48; y++) {
    for(u32 x = 0; x < 64; x++) {
      u32 pixel = hardware.memory.read(4, Memory::VRAMBase + 0x2'0000 + (y * 64 + x) * 4);
      same += (pixels[y * 64 + x] & 0xff'ffff) == (pixel & 0xff'ffff);
    }
  }
  CHECK(same, 64u * 48);
  CHECK(gpu->shoot(0x2'0000, 64, 3, 64, 48), false);  //(the pages memory's now: memory has the picture)

  //a short list's end: nothing handed over yet; a long one's, or a finish, hands it over
  auto submits = stats(gpu).submits;
  sprite(hardware, {{{0, 0, 0, 0}, {0, 0, 4, 4}}, 0xff12'3456});
  gpu->submit(hardware.ge.drawer());  //(the GE drawing: the render GE with the GE on its thread)
  CHECK(stats(gpu).submits, submits);
  for(u32 n = 0; n < 40; n++) {  //(each scissor its own draw)
    hardware.ge.commands[GE::Scissor2] = (24 + n) | 47 << 10;
    sprite(hardware, {{{0, 0, 8, 8}, {0, 0, f32(12 + n % 7), 12}}, 0xff00'0000 | n << 8});
  }
  gpu->submit(hardware.ge.drawer());
  CHECK(stats(gpu).submits, submits + 1);
  hardware.ge.settleAll();
  CHECK(hardware.memory.read(4, Memory::VRAMBase + 0x2'0000 + (3 * 64 + 3) * 4) & 0xff'ffff, 0x12'3456u);
  hardware.ge.setRenderer(nullptr);
}

//A pipeline cache kept between sessions (System's "Pipeline Cache"): the renderer's pipelines, after the start-up
//check made some, given to a renderer made afresh, which takes them (its cache as big from the start) and draws as
//right (its check passes); and data it must leave out, each made into a renderer whose cache starts without it and
//which draws right: another driver version's, another driver cache UUID's, a file cut short, one whose second half
//is zeros (a power cut after it was written), and junk. (Where a driver's cache data is no bigger for the pipelines
//it has than a renderer's at its start, as lavapipe's in CI, the sizes say nothing of what was taken.)
static auto gpuPipelineCache() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::string error;
  CHECK(gpu->check(error), true);
  auto data = gpu->backend->pipelineData();
  CHECK(data.size() > 64, true);
  auto start = GPU::vulkan(nullptr, error, false);
  CHECK((bool)start, true);
  if(!start) return;
  size_t empty = start->backend->pipelineData().size();
  std::printf("  pipeline data: %zu bytes, a renderer's at its start %zu\n", data.size(), empty);
  bool sized = data.size() > empty;
  auto again = GPU::vulkan(nullptr, error, false, data);
  CHECK((bool)again, true);
  if(!again) return;
  CHECK(again->backend->pipelineData().size() >= data.size(), true);
  CHECK(again->check(error), true);
  auto driver = data, uuid = data, cut = data, zeroed = data;
  driver[24] ^= 1;   //(vulkan.cpp's Kept: the driver's version)
  uuid[32] ^= 0xff;  //(its cache UUID)
  cut.resize(data.size() / 2);
  std::fill(zeroed.begin() + data.size() / 2, zeroed.end(), 0);
  for(auto& left : {driver, uuid, cut, zeroed, std::vector<u8>(64, 0x5a)}) {
    auto other = GPU::vulkan(nullptr, error, false, left);
    CHECK((bool)other, true);
    if(!other) continue;
    if(sized) CHECK(other->backend->pipelineData().size() < data.size(), true);
    CHECK(other->check(error), true);
  }
}

//Render to texture through palettes, as Backbone's engine draws its glow: a picture drawn into VRAM's start (rows of
//64 8888 pixels), then taken as CLUT32 indices, a channel at a time through a palette of each tile's own, by 3D quads
//(UV mapped) into a frame buffer inside the texture's 64 rows (from row 48), each quad's texels inside rows 3 to 43
//and columns 8 to 56, added up. Nearest and filtered, texels 1:1 and not. (drawn: the picture's sprites, flat; tiles:
//each quad's place and texels, x and u's first column, v's first row, and its filter; palettes: three a tile.)
struct GlowTile { u32 column, row; bool linear; float shift; };
static constexpr u32 GlowInto = 48 * 64 * 4;  //(the frame buffer the quads draw into, the texture's row 48)
static auto glowTiles(std::mt19937& random) -> std::vector<GlowTile> {
  std::vector<GlowTile> tiles;
  for(u32 k = 0; k < 6; k++) tiles.push_back({8 + k * 8, 3 + u32(random() % 28), k % 3 == 2, k % 2 ? 0.3f : 0.0f});
  return tiles;
}
static auto glow(System& s, const std::vector<Corners>& drawn, const std::vector<GlowTile>& tiles,
                 const std::vector<u32>& palettes) -> void {
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  auto& c = s.ge.commands;
  prepare(s, 0, 3);
  c[GE::TextureMappingEnable] = 0;
  for(auto& one : drawn) sprite(s, one);
  c[GE::FrameBufferPointer] = GlowInto, c[GE::Scissor2] = 63 | 15 << 10;
  texture(s, Memory::VRAMBase, 64, 64, 64, 7);
  c[GE::VertexType] = 0x19f;  //(float u and v, 8888 color, float position: 3D)
  for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
  for(u32 n = 0; n < 16; n++) s.ge.projection[n] = f24(n % 5 == 0 ? 1 : 0);
  c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-8), c[GE::ViewportZScale] = f24(30000);
  c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
  c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 8) << 4, c[GE::MaxZ] = 0xffff;
  c[GE::TextureScaleU] = c[GE::TextureScaleV] = f24(1), c[GE::TextureOffsetU] = c[GE::TextureOffsetV] = 0;
  c[GE::TextureMapMode] = 0;
  c[GE::AlphaBlendEnable] = 1, c[GE::BlendMode] = 10 | 10 << 4, c[GE::BlendFixedA] = c[GE::BlendFixedB] = 0xff'ffff;
  u32 palette = GPUTexture;
  for(u32 n = 0; n < palettes.size(); n++) s.memory.write(4, palette + n * 4, palettes[n]);
  for(u32 k = 0; k < tiles.size(); k++) {
    auto& tile = tiles[k];
    c[GE::TextureFilter] = tile.linear ? 1 | 1 << 8 : 0;
    float x0 = tile.column / 32.0f - 1, x1 = (tile.column + 8) / 32.0f - 1;
    float u0 = (tile.column + tile.shift) / 64, u1 = (tile.column + 8 + tile.shift) / 64;
    float v0 = (tile.row + tile.shift) / 64, v1 = (tile.row + 12 + tile.shift) / 64;
    for(u32 channel = 0; channel < 3; channel++) {
      u32 at = palette + (k * 3 + channel) * 1024;
      c[GE::ClutAddress] = at & 0xff'ffff, c[GE::ClutAddressUpper] = (at >> 24 & 0xf) << 16, c[GE::ClutLoad] = 32;
      s.ge.loadClut();
      c[GE::ClutFormat] = 3 | channel * 8 << 2 | 0xff << 8;
      u32 to = GPUVertices;
      const std::array<float, 4> corners[] = {{u0, v0, x0, 1.0f}, {u1, v0, x1, 1.0f}, {u0, v1, x0, -1.0f},
                                              {u1, v1, x1, -1.0f}};
      for(auto [u, v, x, y] : corners) {
        for(u32 word : {bitsOf(u), bitsOf(v), 0xffff'ffffu, bitsOf(x), bitsOf(y), bitsOf(0.0f)}) {
          s.memory.write(4, to, word), to += 4;
        }
      }
      s.ge.vertexAddress = GPUVertices;
      s.ge.primitive(GE::TriangleStrip, 4);
    }
  }
}

//The glow's tiles with a hardware renderer, at 1x and above, in the accurate renderer and the fast one. With its
//palettes on the GPU, each tile is taken from the GPU's copies of the picture and of the frame buffer below it, its
//indices looked up there: nothing decoded, nothing finished. Without them (turned off, as for a backend without them
//or indices the GPU can't take, CLUT8 say), each is decoded from memory: the first tile, whose rows hold the picture
//the GPU drew, only in its box, after the GPU's pixels there (a finish); those after it, the GPU drawing only in the
//frame buffer below, the 48 rows before that whole (memoryRows()), waiting for nothing. Every byte of VRAM the same
//as with the boxes off (the first tile's rows decoded whole too, after the finish), and at 1x as with the palettes on
//the GPU (above 1x its copy is of its own pixels, at its resolution: not memory's).
static auto gpuGlowParts() -> void {
  GPU* accurate = renderer();
  if(!accurate) return;
  if(!accurate->backend->palettes) return skip("vulkan-palettes", "no palettes on the GPU");
  std::mt19937 random{20261012};
  auto drawn = randomSprites(random, 30, false);
  auto tiles = glowTiles(random);
  std::vector<u32> palettes(tiles.size() * 3 * 256);
  for(auto& entry : palettes) entry = u32(random()) & 0x3f3f'3f3f;
  u32 decodes = tiles.size() * 3;  //(a palette for each tile's channel)
  for(GPU* gpu : {accurate, fastRenderer()}) {
    for(u32 scale : {1u, 2u}) {
      if(!gpu || !scaleTaken(gpu, scale)) continue;
      gpu->resolution(scale);
      System held, parts, whole;
      whole.ge.boxes = false;
      //(each machine's finishes, textures and palettes put on the GPU; and of the textures it decoded and kept, the
      //parts, those of the 48 rows before the frame buffer, and all their texels)
      u64 finishes[3], textures[3], palettesMade[3], texels[3];
      u32 partsKept[3], rowsKept[3];
      for(u32 n = 0; n < 3; n++) {
        System& s = n == 0 ? held : n == 1 ? parts : whole;
        gpu->backend->palettes = n == 0;
        s.ge.setRenderer(gpu);
        auto before = stats(gpu);
        glow(s, drawn, tiles, palettes);
        finishes[n] = stats(gpu).finishes - before.finishes;
        textures[n] = stats(gpu).textures - before.textures;
        palettesMade[n] = stats(gpu).palettes - before.palettes;
        texels[n] = 0, partsKept[n] = 0, rowsKept[n] = 0;
        for(auto& [key, entry] : s.ge.drawer().textures.entries) {  //(the drawing GE's: ge/thread.hpp)
          texels[n] += entry->texels.size();
          partsKept[n] += entry->part, rowsKept[n] += !entry->part && entry->rows == 48;
        }
        s.ge.settleAll();
        s.ge.setRenderer(nullptr);
      }
      gpu->backend->palettes = true;
      u32 bytes[2] = {vramApart(parts, whole), scale == 1 ? vramApart(held, parts) : 0};
      if(bytes[0] || bytes[1] || finishes[0] || finishes[1] != 1 || finishes[2] != 1) {
        std::printf("  %s, %ux: %u bytes apart with the boxes and without, %u held and not; finishes %llu held, "
                    "%llu with the boxes, %llu without\n", gpu == accurate ? "accurate" : "fast", scale, bytes[0],
                    bytes[1], (unsigned long long)finishes[0], (unsigned long long)finishes[1],
                    (unsigned long long)finishes[2]);
      }
      CHECK(bytes[0], 0u);
      CHECK(bytes[1], 0u);
      CHECK(finishes[0], 0u), CHECK(textures[0], 0u), CHECK(texels[0], 0u), CHECK(palettesMade[0], u64(decodes));
      CHECK(finishes[1], 1u), CHECK(textures[1], u64(decodes)), CHECK(palettesMade[1], 0u);
      CHECK(partsKept[1], 1u), CHECK(rowsKept[1], decodes - 1);
      CHECK(finishes[2], 1u), CHECK(textures[2], u64(decodes));
      CHECK(partsKept[2], 0u), CHECK(rowsKept[2], decodes - 1);
      CHECK(texels[1] < texels[2], true);
    }
    gpu->resolution(1);
  }
}

//The glow's tiles with a pretend renderer, which draws nothing and has no palettes on the GPU: what each decodes,
//and when it finishes. A tile whose rows hold pixels the GPU has drawn (the picture) waits for them and decodes its
//box alone (the texels its quad may take, and the margin); the GPU then drawing only in the frame buffer below, the
//tiles after it decode the rows before that frame buffer whole, waiting for nothing (memoryRows()); each texel
//through its palette from memory's bytes. A whole texture decoded first (nothing drawn over it) serves a tile after
//the GPU draws in its other rows, its texels there unchanged, without waiting or decoding again; a byte changed in
//that tile's box has it decoded again, and those rows then serve tiles elsewhere through the same palettes. With the
//GPU drawing in the picture's first rows too, so that decoding the rows before the frame buffer would wait: a box
//reaching past a repeating edge isn't known (the rows decoded whole, after a finish); past a held one it stops there,
//and a box missing the GPU's pixels is decoded without waiting; a part serves no tile it doesn't hold. And a tile
//taking the frame buffer's first row, which accurate mode doesn't hold at the last, decodes its box after a finish,
//as the whole would be decoded after one.
static auto gpuGlowPretend() -> void {
  struct Pretend : GPU::Backend {
    u32 made = 0, finishes = 0;
    struct Made { u32 left, top, width, height; std::vector<u32> texels; };
    std::vector<Made> parts, wholes;
    auto name() const -> std::string override { return "pretend"; }
    auto makeTarget(u32, u32) -> u32 override { return ++made; }
    auto dropTarget(u32) -> void override {}
    auto makeTexture(u32 width, u32 height, const u32* texels, std::shared_ptr<const void>) -> u32 override {
      if(texels) wholes.push_back({0, 0, width, height, std::vector<u32>(texels, texels + width * height)});
      return ++made;
    }
    auto makeTexturePart(u32, u32, const u32* texels, u32 left, u32 top, u32 width, u32 height,
                         std::shared_ptr<const void>) -> u32 override {
      parts.push_back({left, top, width, height, std::vector<u32>(texels, texels + width * height)});
      return ++made;
    }
    auto dropTexture(u32) -> void override {}
    auto submit(const GPU::Recorded&) -> bool override { return true; }
    auto finish(const GPU::Recorded&) -> bool override { return finishes++, true; }
    auto read(u32, const u32*&, const u8*&) -> bool override { return false; }
  };
  auto backend = std::make_unique<Pretend>();
  auto& pretend = *backend;
  GPU gpu(std::move(backend));
  //(a part's or whole's texels that aren't memory's bytes, those of channel, through palette)
  auto wrong = [](System& s, const Pretend::Made& made, const u32* palette, u32 channel) {
    u32 count = 0;
    for(u32 y = 0; y < made.height; y++) {
      for(u32 x = 0; x < made.width; x++) {
        u32 index = s.memory.vram[((made.top + y) * 64 + made.left + x) * 4 + channel];
        count += made.texels[y * made.width + x] != palette[index];
      }
    }
    return count;
  };
  std::mt19937 random{20261013};
  System s;
  for(u32 n = 0; n < 64 * 64; n++) s.memory.write(4, Memory::VRAMBase + n * 4, u32(random()));
  std::vector<u32> palettes(3 * 256 * 3);
  for(auto& entry : palettes) entry = u32(random());
  s.ge.setRenderer(&gpu);
  //(a picture drawn by the GPU over rows 0-15: the first tile's first channel, its box holding some, waits for it;
  //then, the GPU drawing in rows 48-63, the first tile's other channels and the second tile's take rows 0-47 whole)
  glow(s, {{{{0, 0, 0, 0}, {0, 0, 64, 16}}, 0xff10'2030}}, {{8, 3, false, 0}, {16, 20, true, 0.3f}}, palettes);
  CHECK(pretend.finishes, 1u);
  CHECK(pretend.parts.size(), 1u);
  CHECK(pretend.wholes.size(), 5u);
  if(pretend.parts.size() == 1 && pretend.wholes.size() == 5) {
    auto& first = pretend.parts[0];  //(s 8-16, t 3-15: columns 5-18, rows 0-17)
    CHECK(first.left, 5u), CHECK(first.top, 0u), CHECK(first.width, 14u), CHECK(first.height, 18u);
    u32 apart = wrong(s, first, &palettes[0], 0);
    for(u32 n = 1; n < 6; n++) {  //(each channel through its own palette)
      auto& rows = pretend.wholes[n - 1];
      CHECK(rows.width, 64u), CHECK(rows.height, 48u);
      if(rows.height == 48) apart += wrong(s, rows, &palettes[n * 256], n % 3);
    }
    CHECK(apart, 0u);
  }
  //(a whole texture decoded with nothing drawn over it; the GPU then draws in rows 48-63, its frame buffer; a tile
  //taking rows 30-44 of it after waits for nothing, and nothing's decoded again: its bytes there are as they were)
  System again;
  for(u32 n = 0; n < 64 * 64; n++) again.memory.write(4, Memory::VRAMBase + n * 4, u32(random()));
  again.ge.setRenderer(&gpu);
  again.ge.boxes = false;
  glow(again, {}, {{0, 30, false, 0}}, palettes);  //(the frame buffer drawn into: rows 48-63)
  u32 wholes = pretend.wholes.size(), finishes = pretend.finishes, parts = pretend.parts.size();
  again.ge.boxes = true;
  glow(again, {}, {{40, 30, false, 0}}, palettes);
  CHECK(pretend.finishes, finishes);
  CHECK(pretend.wholes.size(), wholes);
  CHECK(pretend.parts.size(), parts);
  //(a byte in that box written: the three channels' rows 0-47 decoded again, not waiting for the GPU's drawing below
  //them; those then serve a tile elsewhere, and one at the texture's left edge, where a box isn't known, repeating)
  u32 changed = Memory::VRAMBase + (36 * 64 + 45) * 4;
  again.memory.write(1, changed, again.memory.read(1, changed) ^ 1);
  glow(again, {}, {{40, 30, false, 0}}, palettes);
  CHECK(pretend.wholes.size(), wholes + 3);
  CHECK(pretend.parts.size(), parts);
  CHECK(pretend.finishes, finishes);
  if(pretend.wholes.size() == wholes + 3) {
    auto& rows = pretend.wholes[wholes];  //(the changed byte red's: the first channel's)
    CHECK(rows.height, 48u);
    u32 index = again.memory.vram[(36 * 64 + 45) * 4];
    if(rows.height == 48) CHECK(rows.texels[36 * 64 + 45], palettes[index]);
  }
  glow(again, {}, {{24, 10, false, 0}}, palettes);
  glow(again, {}, {{0, 10, false, 0}}, palettes);
  CHECK(pretend.wholes.size(), wholes + 3);
  CHECK(pretend.parts.size(), parts);
  CHECK(pretend.finishes, finishes);
  //(the GPU drawing in the picture's rows 0-3 too, before each glow's tiles, so that rows 0-63 wait; through palettes
  //not decoded yet. At the left edge, repeating, no box: rows 0-63 decoded whole after a finish, and the next
  //channels', the GPU drawing in the frame buffer alone again, rows 0-47 without one. Held there, the box, rows 7-24,
  //stops at column 0, and misses the GPU's pixels: decoded without waiting)
  Corners top{{{0, 0, 0, 0}, {0, 0, 64, 4}}, 0xff40'5060};
  auto fresh = [&] {
    std::vector<u32> others(3 * 256);
    for(auto& entry : others) entry = u32(random());
    return others;
  };
  glow(again, {top}, {{0, 10, false, 0}}, fresh());
  CHECK(pretend.finishes, finishes + 1);
  CHECK(pretend.wholes.size(), wholes + 6);
  CHECK(pretend.parts.size(), parts);
  if(pretend.wholes.size() == wholes + 6) {
    CHECK(pretend.wholes[wholes + 3].height, 64u), CHECK(pretend.wholes[wholes + 5].height, 48u);
  }
  again.ge.commands[GE::TextureWrap] = 1 | 1 << 8;
  glow(again, {top}, {{0, 10, false, 0}}, fresh());
  CHECK(pretend.parts.size(), parts + 3);
  CHECK(pretend.finishes, finishes + 1);
  if(pretend.parts.size() == parts + 3) {
    auto& part = pretend.parts.back();  //(s 0-8, t 10-22: columns 0-10, rows 7-24)
    CHECK(part.left, 0u), CHECK(part.width, 11u), CHECK(part.top, 7u), CHECK(part.height, 18u);
  }
  again.ge.commands[GE::TextureWrap] = 0;
  //(a part serves no tile it doesn't hold: two tiles through the same three palettes, each decoded as its box, the
  //second filtered, its texels 0.3 on)
  auto three = fresh();
  std::vector<u32> same = three;
  same.insert(same.end(), three.begin(), three.end());
  glow(again, {top}, {{8, 10, false, 0}, {16, 20, true, 0.3f}}, same);
  CHECK(pretend.parts.size(), parts + 9);
  CHECK(pretend.finishes, finishes + 1);
  if(pretend.parts.size() == parts + 9) {
    auto& second = pretend.parts[parts + 6];  //(s 16.3-24.3, t 20.3-32.3: columns 14-26, rows 18-34)
    CHECK(second.left, 14u), CHECK(second.top, 18u), CHECK(second.width, 13u), CHECK(second.height, 17u);
    CHECK(wrong(again, second, three.data(), 0), 0u);
  }
  //(a tile taking the frame buffer's first row, t 36-48, which the pretend renderer, accurate, doesn't hold at the
  //last: rows 0-63, and its box, rows 33-50, wait for the GPU's drawing there; each channel's box decoded after a
  //finish, as with the boxes off each channel's rows 0-63 are)
  glow(again, {}, {{32, 36, false, 0}}, fresh());
  CHECK(pretend.parts.size(), parts + 12);
  CHECK(pretend.finishes, finishes + 4);
  if(pretend.parts.size() == parts + 12) {
    auto& part = pretend.parts.back();  //(s 32-40: columns 29-42)
    CHECK(part.left, 29u), CHECK(part.top, 33u), CHECK(part.width, 14u), CHECK(part.height, 18u);
  }
  again.ge.boxes = false;
  glow(again, {}, {{32, 36, false, 0}}, fresh());
  CHECK(pretend.wholes.size(), wholes + 9);
  CHECK(pretend.finishes, finishes + 7);
  if(pretend.wholes.size() == wholes + 9) CHECK(pretend.wholes.back().height, 64u);
  s.ge.setRenderer(nullptr), again.ge.setRenderer(nullptr);
}

//A picture with detail between the PSP's pixels above 1x, in the frame buffer prepare() left: 3D, gpuDepthEarly's
//floor, four columns going off into the distance in two colors (2D sprites take the GE's steps, the same at every
//scale)
static auto farFloor(System& s) -> void {
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  auto& c = s.ge.commands;
  c[GE::VertexType] = 0x19c, c[GE::MaxZ] = 0xffff;
  for(u32 n = 0; n < 12; n++) s.ge.world[n] = s.ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
  for(u32 n = 0; n < 16; n++) s.ge.projection[n] = 0;
  s.ge.projection[0] = s.ge.projection[5] = f24(1);
  s.ge.projection[10] = f24(-11.0f / 9), s.ge.projection[11] = f24(-1), s.ge.projection[14] = f24(-20.0f / 9);
  c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-24), c[GE::ViewportZScale] = f24(32767.5f);
  c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32767.5f);
  c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 24) << 4;
  u32 to = GPUVertices;
  for(u32 column = 0; column < 4; column++) {
    float x0 = -10 + column * 5.0f, x1 = x0 + 5, near = -1.0005f, far = -9.999f;
    float corners[6][3] = {{x0, -0.8f, near}, {x1, -0.8f, near}, {x1, -0.8f, far},
                           {x0, -0.8f, near}, {x1, -0.8f, far}, {x0, -0.8f, far}};
    for(auto& corner : corners) {
      s.memory.write(4, to, column & 1 ? 0xffff'ffff : 0xff20'4060), to += 4;
      for(float value : corner) s.memory.write(4, to, bitsOf(value)), to += 4;
    }
  }
  s.ge.vertexAddress = GPUVertices;
  s.ge.primitive(GE::Triangles, 24);
}

//A frame drawn above 1x, then every target's pixels put in memory and their pages given back by a finish another
//frame buffer's PRIM asks for (one the GPU refuses: rows past the 512 a target has, nowhere near this frame):
//nothing has changed the frame since, so it's shown from its target still, the picture it showed before the finish,
//not memory's at 1x (Burnout Legends' menus at 4x: a full-screen sprite each frame whose texture the GPU can't have,
//and every frame shown 480x272); memory's change to it after the finish shown with it. At 1x, memory's picture, the
//same, as before.
static auto gpuShownAfterFinish() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Frame = 0x4'0000;
  for(u32 scale : {2u, 1u}) {
    if(!scaleTaken(gpu, scale)) continue;
    gpu->resolution(scale);
    System s;
    s.ge.setRenderer(gpu);
    prepare(s, Frame, 3);
    farFloor(s);
    std::vector<u32> before, picture;
    drainThreads();  //(before each picture: with the GE on its thread, the GPU is that thread's till it's done)
    CHECK(gpu->picture(Frame, 64, 3, 64, 48, before, scale), true);  //(its pages the target's: from it)
    u32 width = 64 * scale, detailed = 0;  //(the PSP's pixels whose samples differ)
    for(u32 y = 0; y < 48 && before.size() == u64(width) * 48 * scale; y++) {
      for(u32 x = 0; x < 64; x++) {
        u32 corner = before[y * scale * width + x * scale];
        bool one = true;
        for(u32 dy = 0; dy < scale; dy++) {
          for(u32 dx = 0; dx < scale; dx++) one = one && before[(y * scale + dy) * width + x * scale + dx] == corner;
        }
        detailed += !one;
      }
    }
    CHECK(detailed > 0, scale > 1);
    u64 finishes = stats(gpu).finishes;
    prepare(s, 0x8'0000, 3);
    s.ge.commands[GE::Scissor2] = 63 | 1023 << 10;
    sprite(s, {{{0, 0, 4, 520}, {0, 0, 40, 530}}, 0xff33'66cc});  //(rows 520-529: past the target's 512)
    CHECK(stats(gpu).finishes > finishes, true);
    drainThreads();
    bool shown = gpu->picture(Frame, 64, 3, 64, 48, picture, scale);
    CHECK(shown, scale > 1);
    if(shown) CHECK(picture == before, true);
    s.memory.write(4, Memory::VRAMBase + Frame + (40 * 64 + 50) * 4, 0xff00'ff00);
    drainThreads();  //(and it has heard the store)
    shown = gpu->picture(Frame, 64, 3, 64, 48, picture, scale);
    CHECK(shown, scale > 1);
    if(shown) CHECK(picture[40 * scale * width + 50 * scale] & 0xff'ffff, 0x00'ff00u);
    s.ge.setRenderer(nullptr);
  }
  gpu->resolution(1);
}

//A frame buffer sampled as a texture taller than its picture, whose rows run on into the frame buffer below it, the
//one shown, by a PRIM drawing elsewhere whose rows aren't known exactly (a 2D strip whose v starts at 0, repeating):
//taken as it is, each part from its own target, nothing moved or finished, so the frame buffer below keeps its pages
//and is shown from its target, the picture it showed before (Burnout Legends' car select at 4x: the pages moved into
//the texture's target, and the frame buffer shown as memory's picture at 1x). At each scale.
static auto gpuShownBelowTexture() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Lower = 48 * 64 * 4;  //(the upper frame buffer's rows 48 on)
  for(u32 scale : {2u, 1u}) {
    if(!scaleTaken(gpu, scale)) continue;
    gpu->resolution(scale);
    System s;
    s.ge.setRenderer(gpu);
    prepare(s, 0, 3);
    sprite(s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030});
    sprite(s, {{{0, 0, 8, 6}, {0, 0, 40, 30}}, 0xff80'c0f0});
    prepare(s, Lower, 3);
    farFloor(s);
    std::vector<u32> before, picture;
    drainThreads();  //(before each picture: with the GE on its thread, the GPU is that thread's till it's done)
    CHECK(gpu->picture(Lower, 64, 3, 64, 48, before, scale), true);
    auto was = stats(gpu);
    prepare(s, 0x2'0000, 3);
    texture(s, Memory::VRAMBase, 64, 128, 64);
    float corners[4][4] = {{0, 0, 0, 0}, {64, 0, 64, 0}, {0, 96, 0, 48}, {64, 96, 64, 48}};  //(u, v, x, y)
    u32 to = GPUVertices;
    for(auto& corner : corners) {
      for(float value : {corner[0], corner[1]}) s.memory.write(4, to, bitsOf(value)), to += 4;
      s.memory.write(4, to, 0xffff'ffff), to += 4;
      for(float value : {corner[2], corner[3], 0.0f}) s.memory.write(4, to, bitsOf(value)), to += 4;
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::TriangleStrip, 4);
    CHECK(stats(gpu).copies > was.copies, true);
    CHECK(stats(gpu).moves, was.moves);
    CHECK(stats(gpu).finishes, was.finishes);
    drainThreads();
    bool shown = gpu->picture(Lower, 64, 3, 64, 48, picture, scale);
    CHECK(shown, true);
    if(shown) CHECK(picture == before, true);
    s.ge.setRenderer(nullptr);
  }
  gpu->resolution(1);
}

//A frame buffer sampled as a texture taller than its picture (128 rows; another frame buffer, 48 pixels wide, begins
//below its 48) by a 3D quad over its picture, from its first row: two triangles, or a 3D sprite; nearest, the texture
//repeating (Sonic Rivals' palette passes), or filtered and held at its edges (MX vs. ATV Untamed's 3D sprites over its
//picture). Above 1x the GPU holds the rows the PSP's pixels take (texelsReached()), the picture's, not all 128 filled
//from memory past it (whose corners' coordinates alone reach a row past each end); so the frame buffer below, drawn
//after, and every target finished by another frame buffer's refused PRIM, doesn't make it stale: it's shown from its
//target, the picture it showed before. And what the 3D PRIM drew from it shows the picture's last row, which isn't the
//row before it (a row fewer held, the GPU's pixels there would take the last held): nearest, it's the picture texel
//for texel; filtered, its last row is nearer the picture's last than the one before, at every pixel (at 3x a 3D
//sprite's interpolated coordinates can fall a hair below a copy texel's middle, and the filter, placing them in
//256ths of a texel, then gives the texel before a sixteenth: up to 13 per channel at edges inside the PSP's pixels).
//At 1x, memory's picture after the finish, as ever, and in accurate mode, nearest, every byte the software
//renderer's. In each mode, at 1x-3x. (Filtered and repeating, the triangles' first row takes the texture's last
//too, a hair below its texels' middle as the GE steps it: all its rows held, as before.)
static auto gpuPictureRowsHeld() -> void {
  if(!renderer()) return;
  auto sprite3D = [](System& s, u32 rows, f32 x0, f32 x1, f32 y0, f32 y1) {  //(quad3D()'s, as one 3D sprite)
    u32 to = GPUVertices;
    for(u32 corner : {0u, 3u}) {
      f32 x = corner & 1 ? x1 : x0, y = corner >> 1 ? y1 : y0;
      for(u32 word : {bitsOf(x / 64), bitsOf(y / rows), 0xffff'ffffu, 0u, 0u, bitsOf(1), bitsOf(x / 32 - 1),
                      bitsOf(1 - y / 24), 0u}) {
        s.memory.write(4, to, word), to += 4;
      }
    }
    s.ge.vertexAddress = GPUVertices;
    s.ge.primitive(GE::Sprites, 2);
  };
  std::mt19937 random{20261024};
  auto drawn = randomSprites(random, 20, false);
  drawn.push_back({{{0, 0, 0, 46}, {0, 0, 64, 47}}, 0xff20'c040});  //(its last two rows, each a colour of its own)
  drawn.push_back({{{0, 0, 0, 47}, {0, 0, 64, 48}}, 0xffc0'2080});
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(!gpu) continue;
    for(u32 scale : {1u, 2u, 3u}) {
      if(!scaleTaken(gpu, scale)) continue;
      gpu->resolution(scale);
      for(bool sprites : {false, true}) {
        for(bool filtered : {false, true}) {
          System software, hardware;
          hardware.ge.setRenderer(gpu);
          std::vector<u32> before, picture;
          for(System* s : {&software, &hardware}) {
            auto& c = s->ge.commands;
            prepare(*s, 0, 3);
            for(auto& one : drawn) sprite(*s, one);
            if(s == &hardware) {
              drainThreads();  //(before each picture: with the GE on its thread, the GPU is that thread's till done)
              CHECK(gpu->picture(0, 64, 3, 64, 48, before, scale), true);
            }
            prepare(*s, 0x2'0000, 3);
            orthographic(*s, 128);
            c[GE::TextureWrap] = filtered ? 0x101 : 0, c[GE::TextureFilter] = filtered ? 1 | 1 << 8 : 0;
            if(sprites) sprite3D(*s, 128, 0, 64, 0, 48);
            else quad3D(*s, 128, 0, 64, 0, 48, 0);
            prepare(*s, 0x3000, 3);  //(below the picture's 48 rows, 48 pixels wide)
            c[GE::FrameBufferWidth] = 48, c[GE::Scissor2] = 47 | 15 << 10;
            sprite(*s, {{{0, 0, 0, 0}, {0, 0, 48, 16}}, 0x5a12'3456});
          }
          u64 finishes = stats(gpu).finishes;
          prepare(hardware, 0x8'0000, 3);
          hardware.ge.commands[GE::Scissor2] = 63 | 1023 << 10;
          sprite(hardware, {{{0, 0, 4, 520}, {0, 0, 40, 530}}, 0xff33'66cc});  //(rows 520-529: past the target's 512)
          CHECK(stats(gpu).finishes > finishes, true);
          drainThreads();
          bool shown = gpu->picture(0, 64, 3, 64, 48, picture, scale);
          if(shown != (scale > 1)) {
            std::printf("  %s, %ux, %s, %s: %s\n", gpu->fast ? "fast" : "accurate", scale,
                        sprites ? "a 3D sprite" : "two triangles", filtered ? "filtered, held" : "nearest, repeating",
                        shown ? "shown from its target at 1x" : "not shown from its target (stale)");
          }
          CHECK(shown, scale > 1);
          if(shown) CHECK(picture == before, true);
          if(scale > 1) {
            std::vector<u32> copy;
            bool copied = gpu->picture(0x2'0000, 64, 3, 64, 48, copy, scale) && copy.size() == before.size();
            if(copied && !filtered) copied = copy == before;
            auto distance = [](u32 a, u32 b) {
              u32 sum = 0;
              for(u32 shift : {0u, 8u, 16u}) sum += std::abs(s32(a >> shift & 255) - s32(b >> shift & 255));
              return sum;
            };
            u32 width = 64 * scale;
            for(u32 y = 47 * scale; copied && y < 48 * scale; y++) {
              for(u32 x = 0; copied && x < width; x++) {
                u32 pixel = copy[y * width + x], last = before[(48 * scale - 1) * width + x];
                copied = distance(pixel, last) < distance(pixel, before[(47 * scale - 1) * width + x]);
              }
            }
            if(!copied) {
              std::printf("  %s, %ux, %s, %s: the 3D PRIM's copy isn't the frame buffer's picture\n",
                          gpu->fast ? "fast" : "accurate", scale, sprites ? "a 3D sprite" : "two triangles",
                          filtered ? "filtered, held" : "nearest, repeating");
            }
            CHECK(copied, true);
          }
          if(scale == 1 && !gpu->fast && !filtered) CHECK(apart(software, hardware), 0u);
          hardware.ge.setRenderer(nullptr);
        }
      }
    }
    gpu->resolution(1);
  }
}

//A frame buffer drawn (gpuShownAfterFinish's floor, whose edges above 1x fall inside the PSP's pixels), every target
//then finished by another frame buffer's refused PRIM (its pages given back, none drawing in them since), and sampled
//after as a texture, texel for texel into another frame buffer: by a 2D sprite, or a 3D quad over its picture (Sonic
//Rivals draws its bloom from its smallest picture so; IL-2 samples the frame buffer it drew so, after a finish). Above
//1x it comes from its target, which has its pixels at the GPU's resolution (freshTarget()), not decoded from memory at
//the PSP's, one colour for each of the PSP's pixels: the copy is the frame buffer's picture as its target shows it. At
//1x, from memory as ever: that picture too. In each mode, at 1x-3x.
static auto gpuFreshTarget() -> void {
  if(!renderer()) return;
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(!gpu) continue;
    for(u32 scale : {1u, 2u, 3u}) {
      if(!scaleTaken(gpu, scale)) continue;
      gpu->resolution(scale);
      for(bool quad : {false, true}) {
        System s;
        s.ge.setRenderer(gpu);
        prepare(s, 0, 3);
        farFloor(s);
        std::vector<u32> picture, copy;
        drainThreads();  //(before each picture: with the GE on its thread, the GPU is that thread's till it's done)
        CHECK(gpu->picture(0, 64, 3, 64, 48, picture, scale), true);
        u32 width = 64 * scale, detailed = 0;  //(the PSP's pixels whose samples differ)
        for(u32 y = 0; y < 48 && picture.size() == u64(width) * 48 * scale; y++) {
          for(u32 x = 0; x < 64; x++) {
            u32 corner = picture[y * scale * width + x * scale];
            bool one = true;
            for(u32 dy = 0; dy < scale; dy++) {
              for(u32 dx = 0; dx < scale; dx++) {
                one = one && picture[(y * scale + dy) * width + x * scale + dx] == corner;
              }
            }
            detailed += !one;
          }
        }
        CHECK(detailed > 0, scale > 1);
        u64 finishes = stats(gpu).finishes;
        prepare(s, 0x8'0000, 3);
        s.ge.commands[GE::Scissor2] = 63 | 1023 << 10;
        sprite(s, {{{0, 0, 4, 520}, {0, 0, 40, 530}}, 0xff33'66cc});  //(rows 520-529: past the target's 512)
        CHECK(stats(gpu).finishes > finishes, true);
        prepare(s, 0x2'0000, 3);
        if(quad) {
          orthographic(s, 64);
          quad3D(s, 64, 0, 64, 0, 48, 0);
        } else {
          texture(s, Memory::VRAMBase, 64, 64, 64);
          sprite(s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
        }
        drainThreads();
        CHECK(gpu->picture(0x2'0000, 64, 3, 64, 48, copy, scale), true);
        bool same = copy == picture;
        if(!same) {
          std::printf("  %s, %ux, %s: the copy isn't the frame buffer's picture\n", gpu->fast ? "fast" : "accurate",
                      scale, quad ? "a 3D quad" : "a 2D sprite");
        }
        CHECK(same, true);
        s.ge.setRenderer(nullptr);
      }
    }
    gpu->resolution(1);
  }
}

//A texture two targets fit, none of them drawn in since the last finish: gpuFreshTarget's frame buffer, drawn and
//finished, and the one above it in memory, whose rows past its picture a texture taller than it, sampled after that
//finish, filled from memory, over the first. freshTarget() takes a target only where one alone fits, so neither is
//taken: the texture is decoded from memory, and copied texel for texel by a 2D sprite or a 3D quad, the copy is
//memory's picture, one colour for each of the PSP's pixels; above 1x the first's own picture isn't, and it's the first
//of the two among the targets. At 1x, from memory as ever. In each mode, at 1x-3x.
static auto gpuTwoFreshTargets() -> void {
  if(!renderer()) return;
  constexpr u32 Below = 48 * 64 * 4;  //(the frame buffer above's rows 48 on)
  for(GPU* gpu : {renderer(), fastRenderer()}) {
    if(!gpu) continue;
    for(u32 scale : {1u, 2u, 3u}) {
      if(!scaleTaken(gpu, scale)) continue;
      gpu->resolution(scale);
      for(bool quad : {false, true}) {
        System s;
        s.ge.setRenderer(gpu);
        auto finish = [&] {  //(another frame buffer's PRIM the GPU refuses: rows 520-529, past a target's 512)
          u64 finishes = stats(gpu).finishes;
          prepare(s, 0x8'0000, 3);
          s.ge.commands[GE::Scissor2] = 63 | 1023 << 10;
          sprite(s, {{{0, 0, 4, 520}, {0, 0, 40, 530}}, 0xff33'66cc});
          CHECK(stats(gpu).finishes > finishes, true);
        };
        prepare(s, Below, 3);
        farFloor(s);
        std::vector<u32> own, copy;
        drainThreads();  //(before each picture: with the GE on its thread, the GPU is that thread's till it's done)
        CHECK(gpu->picture(Below, 64, 3, 64, 48, own, scale), true);
        finish();
        prepare(s, 0, 3);
        sprite(s, {{{0, 0, 0, 0}, {0, 0, 64, 48}}, 0xff10'2030});
        prepare(s, 0x2'0000, 3);
        texture(s, Memory::VRAMBase, 64, 128, 64);
        sprite(s, {{{0, 0, 0, 0}, {64, 96, 64, 48}}, 0xffff'ffff});  //(rows 0-95: the frame buffer below's too)
        finish();
        prepare(s, 0x4'0000, 3);
        if(quad) {
          orthographic(s, 64, Memory::VRAMBase + Below);
          quad3D(s, 64, 0, 64, 0, 48, 0);
        } else {
          texture(s, Memory::VRAMBase + Below, 64, 64, 64);
          sprite(s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0xffff'ffff});
        }
        drainThreads();
        CHECK(gpu->picture(0x4'0000, 64, 3, 64, 48, copy, scale), true);
        s.ge.settleAll();
        u32 width = 64 * scale;
        std::vector<u32> fromMemory(u64(width) * 48 * scale);
        for(u32 y = 0; y < 48 * scale; y++) {
          for(u32 x = 0; x < width; x++) {
            const u8* pixel = &s.memory.vram[Below + (y / scale * 64 + x / scale) * 4];
            fromMemory[y * width + x] = 0xff00'0000 | pixel[0] | pixel[1] << 8 | pixel[2] << 16;
          }
        }
        bool same = copy == fromMemory;
        if(!same) {
          std::printf("  %s, %ux, %s: the copy isn't memory's picture\n", gpu->fast ? "fast" : "accurate", scale,
                      quad ? "a 3D quad" : "a 2D sprite");
        }
        CHECK(same, true);
        CHECK(own != fromMemory, scale > 1);
        s.ge.setRenderer(nullptr);
      }
    }
    gpu->resolution(1);
  }
}

//gpuShownBelowTexture's texture, taken in two parts with its rows not known, against the software renderer at 1x: the
//strip, its v from 0, repeating, samples the frame buffer texel for texel as a texture of 128 rows, its rows 48-95
//the frame buffer below, both drawn with sprites (exact in either renderer); taken so, from copies, nothing moved or
//finished (else this would pass by decoding memory's texels after a finish); every byte of VRAM the same.
static auto gpuBelowTextureTexels() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Lower = 48 * 64 * 4;
  std::mt19937 random{20261022};
  auto upper = randomSprites(random, 20, false), lower = randomSprites(random, 20, false);
  System software, hardware;
  hardware.ge.setRenderer(gpu);
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);
    for(auto& one : upper) sprite(*s, one);
    prepare(*s, Lower, 3);
    for(auto& one : lower) sprite(*s, one);
  }
  auto was = stats(gpu);
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0x2'0000, 3);
    s->ge.commands[GE::Scissor2] = 63 | 95 << 10;
    texture(*s, Memory::VRAMBase, 64, 128, 64);
    float corners[4][4] = {{0, 0, 0, 0}, {64, 0, 64, 0}, {0, 96, 0, 96}, {64, 96, 64, 96}};  //(u, v, x, y)
    u32 to = GPUVertices;
    for(auto& corner : corners) {
      for(float value : {corner[0], corner[1]}) s->memory.write(4, to, bitsOf(value)), to += 4;
      s->memory.write(4, to, 0xffff'ffff), to += 4;
      for(float value : {corner[2], corner[3], 0.0f}) s->memory.write(4, to, bitsOf(value)), to += 4;
    }
    s->ge.vertexAddress = GPUVertices;
    s->ge.primitive(GE::TriangleStrip, 4);
  }
  CHECK(stats(gpu).copies > was.copies, true);
  CHECK(stats(gpu).moves, was.moves);
  CHECK(stats(gpu).finishes, was.finishes);
  CHECK(apart(software, hardware), 0u);
  hardware.ge.setRenderer(nullptr);
}

auto gpuTests() -> Tests {
  return {
    {"gpu sprites against the software renderer", gpuSprites},
    {"gpu the stencil as each 16-bit format keeps it: 5650's tests against 0, clears and replaces", gpuStencilFormats},
    {"gpu render to texture against the software renderer", gpuRenderToTexture},
    {"gpu anti-aliased lines against the software renderer", gpuSmoothLines},
    {"gpu render to texture from a frame buffer taller than its picture, nothing finished", gpuTallTexture},
    {"gpu samples near the software renderer", gpuSamples},
    {"gpu fast mode: 3D transformed by the GPU, near the software renderer", gpuFast},
    {"gpu fast mode: the GE's rules for which triangles are drawn", gpuFastRules},
    {"gpu fast mode: 3D the GPU transformed is taken by render to texture after", gpuFastMeshChanges},
    {"gpu blending in the shader against the software renderer", gpuBlending},
    {"gpu blending without rasterization order: the GPU's own, the rest read", gpuBlendingApart},
    {"gpu lost: the software renderer draws instead", gpuLost},
    {"gpu refused primitives drawn by the software renderer", gpuRefused},
    {"gpu with the GE on its thread: the CPU's stores after a list don't wait, the texture drawn as it was", gpuThreadVersions},
    {"gpu with the GE on its thread: a list copied into VRAM over drawn pixels and called there, nothing waited for",
     gpuThreadTransferredList},
    {"gpu with the GE on its thread, caught up: a write whose own checks finish first, kept under them", gpuThreadOwnFinish},
    {"gpu with the GE on its thread: R22, a depth fill revives a source's dead bytes on a page no rectangle reaches",
     gpuThreadR22},
    {"gpu with the GE on its thread: R14, a waited load through the depth views leaves the GPU's pixels dirty",
     gpuThreadR14},
    {"gpu with the GE on its thread: a copy out of clean bytes through the depth views, its check made, the GPU "
     "finished", gpuThreadCleanCheck},
    {"gpu with the GE on its thread: R15, halfword and byte stores into drawn pixels, each replayed alone",
     gpuThreadR15},
    {"gpu with the GE on its thread: R16, a depth of the other order taken through memory: a put point",
     gpuThreadR16},
    {"gpu with the GE on its thread: R24, a taller drawer's changed depth rows filled as they were",
     gpuThreadR24},
    {"gpu with the GE on its thread: R19, a move source's bytes beside its pixels, before the drawer's rows",
     gpuThreadR19},
    {"gpu with the GE on its thread: above 1x, a finish's box grown over pixels not drawn again: the hull rule",
     gpuThreadAbove1x},
    {"gpu with the GE on its thread: R23, a depth put back over pixels a finish put there since", gpuThreadR23},
    {"gpu a frame buffer near VRAM's end sampled as a texture declared past it, nothing finished",
     gpuTextureAtVRAMsEnd},
    {"gpu a frame buffer it drew sampled as palette indices: looked up on the GPU, nothing finished", gpuPalettes},
    {"gpu a texture memory has the first rows of, then a frame buffer it draws: those rows decoded alone",
     gpuMemorysRows},
    {"gpu palettes on the GPU found again: A, then B, then A", gpuPalettesAgain},
    {"gpu without rasterization order, barriers only where reading primitives overlap", gpuBarriers},
    {"gpu textures let go of taken again for the next of their size", gpuTexturesKept},
    {"gpu render-to-texture copies kept to a bound", gpuCopies},
    {"gpu a copy serves PRIMs whose texels miss what those before them drew", gpuCopiesKept},
    {"gpu rows past a texture's frame buffer held at its last only where that can't change the picture", gpuHeldRows},
    {"gpu render to texture taken again in part: the strip an effect draws into", gpuPartialCopies},
    {"gpu render to texture from two frame buffers, one below the other, nothing finished", gpuStackedTexture},
    {"gpu render to texture from two frame buffers taken once for many PRIMs", gpuStackedCopiesKept},
    {"gpu a texture decoded again before the GPU has the first: its texels kept till then", gpuTexelsKept},
    {"gpu a finish puts back only the pages drawn in, not memory's newer bytes between", gpuBetweenPages},
    {"gpu a PRIM it refuses among the drawing threads: drawn in order, the GPU's pages kept", gpuRefusedAmongThreads},
    {"gpu the same memory as frame buffers of other formats and places: moved on the GPU", gpuMoves},
    {"gpu the same memory as frame buffers of other row widths: moved on the GPU", gpuStridedMoves},
    {"gpu frame buffers side by side in one target's rows", gpuSideBySide},
    {"gpu vertices and a list read from what it drew, and pages given back without a finish", gpuVerticesDrawn},
    {"gpu a block transfer over pixels it drew: memory's at once, nothing finished", gpuTransferOver},
    {"gpu the CPU's stores over pixels it drew: memory's at once, nothing finished", gpuStoresOver},
    {"gpu pixels moved over bytes memory has the newest of: the moved ones newest", gpuMovedOverMemorys},
    {"gpu memory's bytes in its pages taken again, not the target afresh", gpuRevived},
    {"gpu a texture's place taken by tiles reaching further: few copies, held to what each takes", gpuTiles},
    {"gpu sprites taking less of one copy: one draw, the software renderer's pixels, filtered as from copies of what "
     "each takes", gpuTakenInOneDraw},
    {"gpu fast mode's small 3D PRIMs among its meshes: one draw, the pixels as drawn apart", gpuScreenMeshes},
    {"gpu fast mode's 3D held to the rows it takes of a larger copy, placed by the GE and in a mesh", gpuTakenRowsHeld},
    {"gpu a texture the backend couldn't make: the next PRIM alike still draws with its own", gpuKeptAfterFailedTexture},
    {"gpu settings kept only while every input of them is the same: each changed alone, the next PRIM's its own",
      gpuKeptInputs},
    {"gpu fast mode: a PRIM changing to a pipeline the driver won't make isn't a mesh", gpuMeshRefused},
    {"gpu depth buffer follows memory's changes", gpuDepth},
    {"gpu depth as the GE's whole numbers: a fraction of a step above meets the GE's comparisons", gpuDepthSteps},
    {"gpu depth a GPU may test before the shader runs: declared, and nothing rejected first that passes after",
     gpuDepthEarly},
    {"gpu depth past 0-65535 in 2D: its pipelines undeclared, and nothing rejected first that passes after",
     gpuDepthOutside},
    {"gpu depth's pipelines: declared with the margin where the depth clamps and is in range, else undeclared",
     gpuDepthBuilds},
    {"gpu memory's changes to depth kept past PRIMs that don't use it, for the next that does", gpuDepthUnused},
    {"gpu loads through the depth buffer's views: memory's, without waiting", gpuDepthViewLoads},
    {"gpu a depth buffer two frame buffers draw with: the depth either drew taken by the other", gpuSharedDepth},
    {"gpu the depth buffer in each format's order: a depth made as a picture through VRAM's second copy, read as a "
     "texture, drawn with two formats", gpuDepthOrders},
    {"gpu depth filled from memory as a finish would leave it, for every PRIM after: other frame buffers' pixels moved "
     "into it on the GPU; with no moves, put in memory first where a PRIM's test reads them", gpuDepthCurrent},
    {"gpu other frame buffers' pixels as the depth in every pair of formats, at offsets, in part, at 1x-3x: moved on "
     "the GPU as a finish then a fill would leave them", gpuDepthMoves},
    {"gpu a 5551 frame buffer's stencil bit filled by INCREMENT, emptied by DECREMENT, taken as a texture's alpha",
     gpuStencilBit},
    {"gpu the frame buffer's alpha as its stencil has it, where operations changed it without a color: blended by, "
     "taken as a texture", gpuStencilAlpha},
    {"gpu the frame buffer's alpha as its stencil has it, each other way a PRIM leaves it behind", gpuStencilAlphaWays},
    {"gpu the frame buffer's alpha as its stencil has it, on settings kept and fast mode's small PRIMs among the meshes",
     gpuStencilAlphaPaths},
    {"gpu a depth buffer read as a texture through VRAM's depth views: the depth it drew put into memory first",
     gpuDepthAsTexture},
    {"gpu depths as the depth formats keep them, and back: all 65,536 the same again", gpuDepthFormats},
    {"gpu bytes beside its pixels are memory's, without waiting", gpuBeside},
    {"gpu start-up check passes on a GPU that draws right", gpuCheck},
    {"gpu at 2 and 3 times the resolution: memory's bytes exact", gpuScaled},
    {"gpu a frame drawn above 1x, then a finish another frame buffer's refused PRIM asks for: shown from its target "
     "still, memory's later change with it; at 1x, memory's picture", gpuShownAfterFinish},
    {"gpu a frame buffer sampled as a texture running on into the one shown below it, rows not known: taken in two "
     "parts, nothing moved, the one below shown from its target", gpuShownBelowTexture},
    {"gpu that texture taken in two parts, rows not known, at 1x: the software renderer's texels",
     gpuBelowTextureTexels},
    {"gpu above 1x: a frame buffer read as a taller texture by a 3D quad over its picture holds the picture's rows "
     "alone, so the frame buffer below doesn't make it stale", gpuPictureRowsHeld},
    {"gpu above 1x: a frame buffer none has drawn in since a finish, sampled as a texture: taken from its target, not "
     "decoded from memory at 1x", gpuFreshTarget},
    {"gpu above 1x: a texture two targets none has drawn in since a finish both fit, one filled from memory over the "
     "other: decoded from memory, neither taken", gpuTwoFreshTargets},
    {"gpu above 1x: 2D quads meeting at a seam keep to their texels", gpuSeams},
    {"gpu above 1x: 2D sprites keep to their texels at their edges, the texture repeating or held", gpuSpriteSeams},
    {"gpu pipelines kept between sessions", gpuPipelineCache},
    {"gpu render to texture through palettes: looked up on the GPU, or decoded, the box where the whole would wait",
     gpuGlowParts},
    {"gpu render to texture through palettes, pretend: the box where the whole would wait, else the rows memory has",
     gpuGlowPretend},
  };
}

}
