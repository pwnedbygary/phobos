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
//machines the tests run on needn't have one.
#include "kernel-machine.hpp"
#include "../../ares/psp/ge/gpu/gpu.hpp"

#include <random>

namespace allegrex_test::psp {

using ares::PlayStationPortable::GPU;

constexpr u32 GPUVertices = 0x0896'0000, GPUTexture = 0x0898'0000;

//The renderer, made once (none where there's no Vulkan GPU, said once); each test's machine takes it with
//setRenderer(), which has it forget the machine before's VRAM, and gives it back the same way.
static auto renderer() -> GPU* {
  static std::unique_ptr<GPU> gpu;
  static bool tried = false;
  if(!tried) {
    tried = true;
    const char* wanted = std::getenv("PSP_GPU");
    std::string error = "PSP_GPU=0";
    if(!wanted || std::string(wanted) != "0") gpu = GPU::vulkan(nullptr, error);
    if(gpu) std::printf("  on %s\n", gpu->backend->name().c_str());
    else std::printf("  skipped: %s\n", error.c_str());
  }
  return gpu.get();
}

static auto bitsOf(float value) -> u32 { u32 bits; std::memcpy(&bits, &value, 4); return bits; }

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
  auto before = gpu->statistics;
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
  CHECK(hardware.memory.vramBusy, true);  //(nothing read back yet)
  u32 bytes = apart(software, hardware);
  CHECK(bytes, 0u);
  CHECK(gpu->statistics.copies > before.copies, true);
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
  return gpu.get();
}

static auto gpuFast() -> void {
  if(!renderer()) return;
  GPU* gpu = fastRenderer();
  CHECK((bool)gpu, true);
  if(!gpu) return;
  CHECK(gpu->fast && !gpu->backend->reads, true);
  if(!gpu->backend->transforms) return void(std::printf("  the driver didn't take transform.vert: 3D on the CPU\n"));
  auto before = gpu->statistics;
  std::string error;
  bool passed = gpu->check(error);
  if(!passed) std::printf("  %s\n", error.c_str());
  CHECK(passed, true);
  CHECK(gpu->statistics.meshes - before.meshes >= 4, true);
  if(!testPrograms()) return;
  before = gpu->statistics;
  samples(gpu, 3);
  CHECK(gpu->statistics.meshes > before.meshes, true);
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
  if(!fast || !fast->backend->transforms) return;
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
      auto before = gpu->statistics;
      scene(software, clip), scene(hardware, clip);
      if(mode) CHECK(gpu->statistics.meshes > before.meshes, true);
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

//Fast mode's 3D (mesh()) changes a target as any draw does, for render to texture taken again: a frame buffer
//sampled into another, a wall of one color drawn over part of it with the GPU transforming it, then sampled again from
//the same place. The second picture has the wall in it as the software renderer has it, all but a few pixels of its
//edges (which fast mode may cover where the GE doesn't).
static auto gpuFastMeshChanges() -> void {
  GPU* gpu = renderer() ? fastRenderer() : nullptr;
  if(!gpu || !gpu->backend->transforms) return;
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
  auto before = gpu->statistics;
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0, 3);
    for(auto& one : drawn) sprite(*s, one);
    sample(*s);
    wall(*s);
    sample(*s);
    s->ge.settleAll();
  }
  CHECK(gpu->statistics.meshes > before.meshes, true);
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
  CHECK(hardware.memory.vramBusy, true);
  hardware.ge.settleAll();
  CHECK(gpu.ready(), false);
  CHECK(hardware.memory.vramBusy, false);
  CHECK(apart(software, hardware), 0u);  //(the first sprites, never put in memory, gone)
  u32 runs = pretend.runs;
  for(System* s : {&software, &hardware}) for(auto& one : after) sprite(*s, one);
  CHECK(apart(software, hardware), 0u);
  CHECK(pretend.runs, runs);
  CHECK(reports, 1u);
  hardware.ge.setRenderer(nullptr);
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
    if(scale > gpu->backend->mostScale) break;
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
    auto before = gpu->statistics;
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
    u64 copied = gpu->statistics.copied - before.copied;
    if(copied * 2 >= whole) std::printf("  %ux: %llu pixels copied of %llu\n", scale, (unsigned long long)copied,
                                        (unsigned long long)whole);
    CHECK(copied * 2 < whole, true);
    hardware.ge.setRenderer(nullptr);
  }
  for(u32 scale : {1u, 2u}) {
    if(scale > gpu->backend->mostScale) break;
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
//PRIM drawing into it while it samples rows it doesn't draw over, as MC3's does. And one whose pixels the first frame
//buffer drew over the second's first rows since the second drew there (the newest of those pixels the first's):
//the second's pixels in the texture's pages moved into the first's target on the GPU (move()), nothing finished;
//where the GPU doesn't move them, decoded from memory, as before.
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
    auto before = gpu->statistics;
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
    CHECK(gpu->statistics.finishes, before.finishes);
    CHECK(gpu->statistics.copies - before.copies >= 2, true);
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
    auto before = gpu->statistics;
    for(System* s : {&software, &hardware}) {
      prepare(*s, 0x2'0000, 3);
      texture(*s, Memory::VRAMBase, 64, 64, 64);
      sprite(*s, {{{0, 0, 0, 0}, {64, 64, 64, 48}}, 0xffff'ffff});
      prepare(*s, 48 * 64 * 4, 3);
      sprite(*s, {{{0, 0, 0, 20}, {64, 64, 64, 44}}, 0xffff'ffff});  //(rows 20-43 of the second, from both)
    }
    CHECK(gpu->statistics.finishes, before.finishes);
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
    auto before = gpu->statistics;
    for(System* s : {&software, &hardware}) sprite(*s, {{{0, 0, 0, 0}, {64, 80, 64, 48}}, 0xffff'ffff});
    if(gpu->backend->moves) {
      CHECK(gpu->statistics.finishes, before.finishes);
      CHECK(gpu->statistics.moves > before.moves, true);
    } else {
      CHECK(gpu->statistics.finishes > before.finishes, true);
    }
    CHECK(apart(software, hardware), 0u);
    hardware.ge.setRenderer(nullptr);
  }
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
//rows are the first's rows 16 on, and a 5551 one 128 wide (its alpha the stencil's top bit). Each takes the pixels the
//one before drew in its pages on the GPU, as memory would have them (move()), with nothing put back; at 1x, 2x and
//3x, and everything comes out as the software renderer has it. (Sprites with random colors and alphas, some in clear
//mode setting the stencil, some textured from another frame buffer the GPU drew.)
static auto gpuMoves() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(!gpu->backend->moves) return void(std::printf("  skipped: the GPU doesn't move pixels between targets\n"));
  std::mt19937 random{20261016};
  constexpr u32 At = 0x3'0000;  //(where no other test draws: VRAM's offset)
  for(u32 scale : {1u, 2u, 3u}) {
    if(scale > gpu->backend->mostScale) break;
    gpu->resolution(scale);
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    for(System* s : {&software, &hardware}) {
      for(u32 n = 0; n < 64 * 48; n++) s->memory.write(4, Memory::VRAMBase + 0x2'0000 + n * 4, u32(n * 0x9e37'79b9));
    }
    auto before = gpu->statistics;
    struct Pass { u32 address, width, format; std::vector<Corners> sprites; bool clear, textured; };
    std::vector<Pass> passes;
    for(u32 round = 0; round < 3; round++) {
      passes.push_back({At, 64, 3, randomSprites(random, 12, false), round == 1, false});
      passes.push_back({At, 128, 0, randomSprites(random, 12, false), false, round == 2});
      passes.push_back({At + 0x1000, 64, 3, randomSprites(random, 8, false), false, round == 0});
      passes.push_back({At, 128, 1, randomSprites(random, 8, false), round == 2, false});
    }
    for(System* s : {&software, &hardware}) {
      auto& c = s->ge.commands;
      for(auto& pass : passes) {
        prepare(*s, pass.address, pass.format);
        c[GE::FrameBufferWidth] = pass.width;
        if(pass.textured) texture(*s, Memory::VRAMBase + 0x2'0000, 64, 64, 64);
        if(pass.clear) c[GE::ClearMode] = 1 | 3 << 8;  //(colors and stencil)
        for(auto& one : pass.sprites) sprite(*s, one);
        c[GE::ClearMode] = 0, c[GE::TextureMappingEnable] = 0;
      }
    }
    CHECK(gpu->statistics.finishes, before.finishes);
    CHECK(gpu->statistics.moves - before.moves >= 9, true);
    u32 bytes = vramApart(software, hardware);
    if(bytes) std::printf("  %ux: %u bytes apart\n", scale, bytes);
    CHECK(bytes, 0u);
    hardware.ge.setRenderer(nullptr);
  }
  gpu->resolution(1);
}

//Frame buffers side by side in the rows of a wide one, and one a row down, as God of War draws its effects (four 5650
//ones 64 pixels wide in rows of 256 here): parts of one target, drawn into in turn with nothing finished, dithered
//(each one's matrix from its own first row and column: the fifth starts a row down), a sprite's texture stepped
//across each, and a texture read across the four, held on the GPU. Then one whose PRIM reaches past its target's
//rows' end, and one drawn with its depth buffer, each a target of its own. Everything as the software renderer has it.
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
  auto before = gpu->statistics;
  for(System* s : {&software, &hardware}) {
    for(u32 n = 0; n < 4; n++) draw(*s, At + n * 128, n * 6);
    draw(*s, At + 512 + 128, 24);
  }
  auto across = gpu->statistics;
  for(System* s : {&software, &hardware}) {
    prepare(*s, 0x2'0000, 3);
    texture(*s, Memory::VRAMBase + At, 256, 32, 256, 0);
    sprite(*s, {{{0, 0, 0, 0}, {256, 32, 64, 16}}, 0xffff'ffff});
  }
  CHECK(gpu->statistics.finishes, before.finishes);
  CHECK(gpu->statistics.textures, across.textures);  //(none decoded: held)
  CHECK(gpu->statistics.copies > across.copies, true);
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
//its middle) finishes first. All as the software renderer has it.
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
  auto before = gpu->statistics;
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
  CHECK(gpu->statistics.finishes, before.finishes + 1);  //(the CPU's read of the sprite's pixel)
  before = gpu->statistics;
  for(System* s : {&software, &hardware}) {
    prepare(*s, At, 3);
    for(u32 n = 0; n < 8; n++) sprite(*s, drawn[n]);
    transfer(*s, Memory::VRAMBase + At, 1 | 20 << 10, 16, 2, 2);  //(2-byte pixels from the middle of an 8888 one's)
  }
  CHECK(gpu->statistics.finishes > before.finishes, true);
  u32 bytes = vramApart(software, hardware);
  if(bytes) std::printf("  %u bytes apart\n", bytes);
  CHECK(bytes, 0u);
  hardware.ge.setRenderer(nullptr);
}

//The CPU's stores over pixels the GPU drew (as Liberty City Stories writes a list over a picture it drew, which the
//GE then reads): words over two rows, and halfwords over a 5650 frame buffer's pixels, memory's at once
//(overwritten()), nothing finished, and read again (as the GE reads the list) without a finish either; a texture over
//them decoded from memory, and a sprite drawn across them after. A byte into an 8888 pixel is only part of it: that
//one finishes first. As the software renderer has it each time.
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
    auto before = gpu->statistics;
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
    CHECK(gpu->statistics.finishes, before.finishes);
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
    CHECK(gpu->statistics.finishes, before.finishes + 1);  //(the CPU's read of the sprite's pixels)
    if(format == 3) {
      for(System* s : {&software, &hardware}) {
        sprite(*s, {{{0, 0, 10, 28}, {0, 0, 40, 34}}, 0xff12'3456});
        before = gpu->statistics;
        s->memory.write(1, Memory::VRAMBase + At + 30 * rowBytes + 20 * 4 + 1, 0x5a);
        if(s == &hardware) CHECK(gpu->statistics.finishes, before.finishes + 1);
      }
    }
    u32 bytes = vramApart(software, hardware);
    if(bytes) std::printf("  format %u: %u bytes apart\n", format, bytes);
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
    auto before = gpu->statistics;
    u32 seen[2] = {};
    for(System* s : {&software, &hardware}) {
      sprite(*s, {{{0, 0, 8, 34}, {0, 0, 50, 46}}, 0xff60'a0e0});
      if(s == &hardware && ranges == 1) CHECK(gpu->statistics.finishes, before.finishes);
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
//3x. And at 2x and 3x a tile filtered, its last texels at weight 0 left out of what it takes (whose pixels between the
//GE's sample a little past them), drawn twice: from a copy of only what it takes, and after a larger tile, from that
//larger copy, held to what it takes (Push::held): the GPU's pixels the same both times.
static auto gpuTiles() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261020};
  for(u32 scale : {1u, 2u, 3u}) {
    if(scale > gpu->backend->mostScale) break;
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
    auto before = gpu->statistics;
    for(u32 y = 0; y < 4; y++) {
      for(u32 x = 0; x < 6; x++) {  //(each tile 10 by 12, its own texels 1:1: the copy of the corner to its own)
        float left = x * 10, top = y * 12, right = left + 10, bottom = top + 12;
        for(System* s : {&software, &hardware}) {
          sprite(*s, {{{left, top, left, top}, {right, bottom, right, bottom}}, 0xffff'ffff});
        }
      }
    }
    u64 copies = gpu->statistics.copies - before.copies;
    if(copies > 8) std::printf("  %ux: %llu copies for 24 tiles\n", scale, (unsigned long long)copies);
    CHECK(copies <= 8, true);
    u32 bytes = apart(software, hardware);
    if(bytes) std::printf("  %ux: %u bytes apart\n", scale, bytes);
    CHECK(bytes, 0u);
    if(scale > 1) {
      auto& c = hardware.ge.commands;
      auto tile = [&](u32 at, float reach) {  //(into the frame buffer at VRAM's offset at, shrunk a little)
        prepare(hardware, at, 3);
        texture(hardware, Memory::VRAMBase + 0x2000, 64, 64, 64);
        c[GE::TextureFilter] = 1 | 1 << 8;
        sprite(hardware, {{{0.5f, 0.5f, 0, 0}, {reach - 0.5f, reach - 0.5f, 24, 24}}, 0xffff'ffff});
      };
      tile(0x2'8000, 20), tile(0x2'c000, 40), tile(0x3'0000, 20);
      std::vector<u32> first, again;
      CHECK(gpu->picture(0x2'8000, 64, 3, 24, 24, first, scale), true);
      CHECK(gpu->picture(0x3'0000, 64, 3, 24, 24, again, scale), true);
      u32 differ = 0;
      for(u32 n = 0; n < first.size() && n < again.size(); n++) differ += first[n] != again[n];
      if(differ) std::printf("  %ux: %u of the GPU's pixels differ from the larger copy\n", scale, differ);
      CHECK(differ, 0u);
    }
    hardware.ge.setRenderer(nullptr);
  }
  gpu->resolution(1);
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
    finishes = gpu->statistics.finishes;
    for(u32 n = 0; n < 16; n++) s->memory.write(4, list + n * 4, 0x1234'5600 + n);
    for(u32 n = 0; n < 16; n++) read += s->memory.read(4, list + n * 4) == 0x1234'5600 + n;
    sprite(*s, {{{0, 0, 4, 4}, {0, 0, 20, 20}}, 0xff00'ff00});
  }
  CHECK(read, 32u);
  CHECK(gpu->statistics.finishes, finishes);  //(the hardware machine's writes, reads and sprite: no wait)
  for(System* s : {&software, &hardware}) {
    s->ge.commands[GE::Scissor2] = 63 | 47 << 10;
    sprite(*s, {{{0, 0, 40, 8}, {0, 0, 56, 12}}, 0xffff'0000});
  }
  CHECK(apart(software, hardware), 0u);
  CHECK(software.memory.read(4, list + 12 * 4), 0x1234'560cu);  //(column 60: the list's, not drawn over)
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
  auto before = gpu->statistics;
  bool passed = gpu->check(error);
  if(!passed) std::printf("  %s\n", error.c_str());
  CHECK(passed, true);
  CHECK(gpu->ready(), true);
  if(gpu->backend->reads) CHECK(gpu->statistics.readingDraws - before.readingDraws >= 128, true);
  bool reads = gpu->backend->reads, readsBlending = gpu->backend->readsBlending;
  gpu->backend->reads = gpu->backend->readsBlending = false;
  before = gpu->statistics, error.clear();
  passed = gpu->check(error);
  if(!passed) std::printf("  nothing read: %s\n", error.c_str());
  CHECK(passed, true);
  CHECK(gpu->statistics.readingDraws, before.readingDraws);
  gpu->backend->reads = reads, gpu->backend->readsBlending = readsBlending;
}

//At 2 and 3 times the PSP's resolution (GPU::resolution()): flat sprites over VRAM full of random bytes, in each
//format, are the software renderer's byte for byte once read back (each pixel's scale x scale the same; memory's
//bytes the GPU didn't draw over, the stencils too, back as they went in, through copy.frag's enlarging and the
//blit's shrinking); the screen's picture at the target's own resolution is them, scale x scale each; render to
//texture from a copy at the target's resolution, sampled 1:1, is exact too; and the depth buffer and the bytes beside
//the pixels behave as at the PSP's own (gpuDepth, gpuBeside, run again).
static auto gpuScaled() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(gpu->backend->mostScale < 3) return void(std::printf("  skipped: the GPU takes at most %ux\n",
                                                         gpu->backend->mostScale));
  std::mt19937 random{20261012};
  for(u32 scale : {2u, 3u}) {
    gpu->resolution(scale);
    CHECK(gpu->resolution(), scale);
    for(u32 format : {3u, 0u, 1u, 2u}) {
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      auto flat = randomSprites(random, 40, false);
      std::vector<u8> noise(0x4'0000);
      for(auto& byte : noise) byte = random();
      for(System* s : {&software, &hardware}) {
        std::memcpy(s->memory.vram.data(), noise.data(), noise.size());
        prepare(*s, 0, format);
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
    if(scale > gpu->backend->mostScale) {
      std::printf("  %ux skipped: the GPU takes at most %ux\n", scale, gpu->backend->mostScale);
      continue;
    }
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
  if(!gpu->backend->reads) return void(std::printf("  skipped: the GPU doesn't read the frame buffer\n"));
  std::printf("  %s\n", gpu->backend->readsInOrder ? "in rasterization order" : "draws split where they overlap");
  bool readsBlending = gpu->backend->readsBlending;
  gpu->backend->readsBlending = true;
  std::mt19937 random{20261013};
  for(u32 scale : {1u, 2u}) {
    if(scale > gpu->backend->mostScale) continue;
    gpu->resolution(scale);
    for(u32 format : {3u, 0u, 1u, 2u}) {
      System software, hardware;
      hardware.ge.setRenderer(gpu);
      auto before = gpu->statistics;
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
      CHECK(gpu->statistics.readingDraws > before.readingDraws, true);
      if(!gpu->backend->readsInOrder) CHECK(gpu->statistics.splits > before.splits, true);
      hardware.ge.setRenderer(nullptr);
    }
  }
  gpu->resolution(1);
  gpu->backend->readsBlending = readsBlending;
}

//Without rasterization order (Backend::readsInOrder off, as here on any GPU that reads), each draw that reads waits
//for the ones before and holds no overlapping primitives, so games' blending, thousands of draws a frame, would be
//thousands of draws and barriers: the GPU blends with its own units then, and only what they can't come close to
//reads. Blending in 8888 with the factors and operations the GPU has draws nothing in the shader and splits nothing,
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
    auto before = gpu->statistics;
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
    CHECK(gpu->statistics.primitives > before.primitives, true);
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
    CHECK(gpu->statistics.readingDraws, before.readingDraws);
    CHECK(gpu->statistics.splits, before.splits);
    hardware.ge.setRenderer(nullptr);
  }
  if(!gpu->backend->reads) {
    std::printf("  what reads skipped: the GPU doesn't read the frame buffer\n");
    gpu->backend->readsInOrder = readsInOrder, gpu->backend->readsBlending = readsBlending;
    return;
  }
  //what reads: each round a mask keeping part of a channel, a logic operation, the absolute difference or a doubled
  //alpha (as source or destination factor), with random factors and operations besides; in a 16-bit frame buffer
  //also the GPU's own factors and operations alone, and then one sprite blended so, a draw that reads
  for(u32 format : {3u, 0u, 1u, 2u}) {
    System software, hardware;
    hardware.ge.setRenderer(gpu);
    auto before = gpu->statistics;
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
    CHECK(gpu->statistics.readingDraws > before.readingDraws, true);
    CHECK(gpu->statistics.splits > before.splits, true);
    if(format != 3) {
      auto between = gpu->statistics;
      for(System* s : {&software, &hardware}) {
        s->ge.commands[GE::AlphaBlendEnable] = 1, s->ge.commands[GE::BlendMode] = 2 | 3 << 4;
        sprite(*s, {{{0, 0, 4, 4}, {0, 0, 36, 28}}, 0x80'40c0ff});
        s->ge.commands[GE::AlphaBlendEnable] = 0;
      }
      CHECK(gpu->statistics.readingDraws - between.readingDraws, u64(1));
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
    if(s == &hardware) before = gpu->statistics;
    sprite(*s, {{{0, 0, 0, 0}, {64, 48, 64, 48}}, 0});  //(1:1: row 47's v 47.5, rows 47 and 48, 48 at weight 0)
    sprite(*s, {{{2, 1, 0, 0}, {34, 47, 20, 30}}, 0});
  }
  CHECK(gpu->statistics.finishes, before.finishes);
  CHECK(gpu->statistics.copies > before.copies, true);
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
  auto submits = gpu->statistics.submits;
  sprite(hardware, {{{0, 0, 0, 0}, {0, 0, 4, 4}}, 0xff12'3456});
  gpu->submit(hardware.ge);
  CHECK(gpu->statistics.submits, submits);
  for(u32 n = 0; n < 40; n++) {  //(each scissor its own draw)
    hardware.ge.commands[GE::Scissor2] = (24 + n) | 47 << 10;
    sprite(hardware, {{{0, 0, 8, 8}, {0, 0, f32(12 + n % 7), 12}}, 0xff00'0000 | n << 8});
  }
  gpu->submit(hardware.ge);
  CHECK(gpu->statistics.submits, submits + 1);
  hardware.ge.settleAll();
  CHECK(hardware.memory.read(4, Memory::VRAMBase + 0x2'0000 + (3 * 64 + 3) * 4) & 0xff'ffff, 0x12'3456u);
  hardware.ge.setRenderer(nullptr);
}

//A pipeline cache kept between sessions (System's "Pipeline Cache"): the renderer's pipelines, after the start-up
//check made some, given to a renderer made afresh, which takes them (its cache as big from the start) and draws as
//right (its check passes); and data it must leave out, each made into a renderer whose cache starts without it and
//which draws right: another driver version's, another driver cache UUID's, a file cut short, one whose second half
//is zeros (a power cut after it was written), and junk.
static auto gpuPipelineCache() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::string error;
  CHECK(gpu->check(error), true);
  auto data = gpu->backend->pipelineData();
  CHECK(data.size() > 64, true);
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
    CHECK(other->backend->pipelineData().size() < data.size(), true);
    CHECK(other->check(error), true);
  }
}

auto gpuTests() -> Tests {
  return {
    {"gpu sprites against the software renderer", gpuSprites},
    {"gpu render to texture against the software renderer", gpuRenderToTexture},
    {"gpu render to texture from a frame buffer taller than its picture, nothing finished", gpuTallTexture},
    {"gpu samples near the software renderer", gpuSamples},
    {"gpu fast mode: 3D transformed by the GPU, near the software renderer", gpuFast},
    {"gpu fast mode: the GE's rules for which triangles are drawn", gpuFastRules},
    {"gpu fast mode: 3D the GPU transformed is taken by render to texture after", gpuFastMeshChanges},
    {"gpu blending in the shader against the software renderer", gpuBlending},
    {"gpu blending without rasterization order: the GPU's own, the rest read", gpuBlendingApart},
    {"gpu lost: the software renderer draws instead", gpuLost},
    {"gpu refused primitives drawn by the software renderer", gpuRefused},
    {"gpu render-to-texture copies kept to a bound", gpuCopies},
    {"gpu render to texture taken again in part: the strip an effect draws into", gpuPartialCopies},
    {"gpu render to texture from two frame buffers, one below the other, nothing finished", gpuStackedTexture},
    {"gpu a texture decoded again before the GPU has the first: its texels kept till then", gpuTexelsKept},
    {"gpu a finish puts back only the pages drawn in, not memory's newer bytes between", gpuBetweenPages},
    {"gpu a PRIM it refuses among the drawing threads: drawn in order, the GPU's pages kept", gpuRefusedAmongThreads},
    {"gpu the same memory as frame buffers of other formats and places: moved on the GPU", gpuMoves},
    {"gpu frame buffers side by side in one target's rows", gpuSideBySide},
    {"gpu a block transfer over pixels it drew: memory's at once, nothing finished", gpuTransferOver},
    {"gpu the CPU's stores over pixels it drew: memory's at once, nothing finished", gpuStoresOver},
    {"gpu memory's bytes in its pages taken again, not the target afresh", gpuRevived},
    {"gpu a texture's place taken by tiles reaching further: few copies, held to what each takes", gpuTiles},
    {"gpu depth buffer follows memory's changes", gpuDepth},
    {"gpu bytes beside its pixels are memory's, without waiting", gpuBeside},
    {"gpu start-up check passes on a GPU that draws right", gpuCheck},
    {"gpu at 2 and 3 times the resolution: memory's bytes exact", gpuScaled},
    {"gpu above 1x: 2D quads meeting at a seam keep to their texels", gpuSeams},
    {"gpu pipelines kept between sessions", gpuPipelineCache},
  };
}

}
