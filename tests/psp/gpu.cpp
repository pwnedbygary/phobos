//The hardware renderer (ares/psp/ge/gpu, docs/psp-gpu-renderers.md) against the software renderer, the exact one:
//random 2D sprites, flat and textured, in each frame buffer format, which the GPU must draw byte for byte the same;
//a frame buffer drawn and then sampled as a texture (render to texture: the copy taken on the GPU), the same again;
//pspsdk's samples (PSP_TEST_PROGRAMS) run by two machines alike but for the renderer, measured for how close their
//pictures are (blending rounds differently on the GPU, so they needn't be the same); and a GPU that stops
//answering. Each group but the last is skipped, saying why, where there's no Vulkan GPU (or with PSP_GPU=0): the
//machines the tests run on needn't have one.
#include "kernel-machine.hpp"
#include "../../ares/psp/ge/gpu/gpu.hpp"

#include <random>

namespace allegrex_test::psp {

using ares::PlayStationPortable::GPU;

constexpr u32 GPUVertices = 0x0896'0000, GPUTexture = 0x0898'0000;

//The renderer, made once (none where there's no Vulkan GPU, said once).
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

//A sprite: its corners (u, v, x, y each), its color.
struct Corners { float at[2][4]; u32 color; };
static auto sprite(System& s, const Corners& sprite) -> void {
  u32 to = GPUVertices;
  for(auto& corner : sprite.at) {
    for(float value : {corner[0], corner[1]}) s.memory.write(4, to, bitsOf(value)), to += 4;
    s.memory.write(4, to, sprite.color), to += 4;
    for(float value : {corner[2], corner[3], 0.0f}) s.memory.write(4, to, bitsOf(value)), to += 4;
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
    hardware.ge.renderer = gpu;
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
    hardware.ge.renderer = nullptr;
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
  hardware.ge.renderer = gpu;
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
  hardware.ge.renderer = nullptr;
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
    hardware.system.ge.renderer = gpu;
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
    hardware.system.ge.settleAll();
    hardware.system.ge.renderer = nullptr;
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
  hardware.ge.renderer = &gpu;
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
  hardware.ge.renderer = nullptr;
}

auto gpuTests() -> Tests {
  return {
    {"gpu sprites against the software renderer", gpuSprites},
    {"gpu render to texture against the software renderer", gpuRenderToTexture},
    {"gpu samples near the software renderer", gpuSamples},
    {"gpu lost: the software renderer draws instead", gpuLost},
  };
}

}
