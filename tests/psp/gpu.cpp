//The hardware renderer (ares/psp/ge/gpu, docs/psp-gpu-renderers.md) against the software renderer, the exact one:
//random 2D sprites, flat and textured, in each frame buffer format, which the GPU must draw byte for byte the same;
//a frame buffer drawn and then sampled as a texture (render to texture: the copy taken on the GPU), the same again;
//pspsdk's samples (PSP_TEST_PROGRAMS) run by two machines alike but for the renderer, measured for how close their
//pictures are (they needn't be the same: the GPU interpolates texture coordinates its own way); blending, dithering,
//logic operations and write masks done in the shader (shader blending), byte for byte; a GPU that stops answering;
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
//difference: 1-2 is blending's rounding, the GPU's sum against the PSP's terms each truncated). Every frame must
//show the picture: no pixel's channel further off than 8 in more than 1 of 1000.
static auto gpuSamples() -> void {
  auto gpu = renderer();
  if(!gpu || !testPrograms()) return;
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
    CHECK(near[2] * 1000 <= pixels * 3, true);
    hardware.system.ge.setRenderer(nullptr);
  }
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
    auto makeTexture(u32, u32, const u32*) -> u32 override { return ++made; }
    auto dropTexture(u32) -> void override {}
    auto submit(const GPU::Recorded&) -> bool override { return runs++, true; }
    auto finish(const GPU::Recorded&) -> bool override { return runs++, false; }
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
    auto makeTexture(u32, u32, const u32*) -> u32 override { return 1; }
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
//renderer as it was for the next machine (everything dropped, nothing of its own machine's kept).
static auto gpuCheck() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::string error;
  bool passed = gpu->check(error);
  if(!passed) std::printf("  %s\n", error.c_str());
  CHECK(passed, true);
  CHECK(gpu->ready(), true);
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

//Shader blending (docs/psp-gpu-renderers.md, "Shader blending"), where the GPU reads the frame buffer: overlapping
//sprites blended with random factors (fixed ones too) and operations, dithered or not, with random logic operations
//and write masks that keep part of a channel, over a frame buffer cleared to random colors and stencils, in each
//format, at the PSP's resolution and at twice it: the software renderer's bytes exactly. Consecutive sprites with
//the same settings are one draw, so where the GPU doesn't keep their order the renderer must split them (emit()).
static auto gpuBlending() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  if(!gpu->backend->reads) return void(std::printf("  skipped: the GPU doesn't read the frame buffer\n"));
  std::printf("  %s\n", gpu->backend->readsInOrder ? "in rasterization order" : "draws split where they overlap");
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
}

auto gpuTests() -> Tests {
  return {
    {"gpu sprites against the software renderer", gpuSprites},
    {"gpu render to texture against the software renderer", gpuRenderToTexture},
    {"gpu samples near the software renderer", gpuSamples},
    {"gpu blending in the shader against the software renderer", gpuBlending},
    {"gpu lost: the software renderer draws instead", gpuLost},
    {"gpu refused primitives drawn by the software renderer", gpuRefused},
    {"gpu render-to-texture copies kept to a bound", gpuCopies},
    {"gpu depth buffer follows memory's changes", gpuDepth},
    {"gpu bytes beside its pixels are memory's, without waiting", gpuBeside},
    {"gpu start-up check passes on a GPU that draws right", gpuCheck},
    {"gpu at 2 and 3 times the resolution: memory's bytes exact", gpuScaled},
  };
}

}
