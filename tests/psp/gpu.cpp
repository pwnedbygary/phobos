//The GPU renderer (ares/psp/ge/gpu, docs/psp-gpu-renderers.md): its arithmetic against the host's, case by case;
//random batches of every kind of primitive and pipeline setting drawn by it and by the software renderer over the
//same VRAM, byte for byte; and pspsdk's samples (PSP_TEST_PROGRAMS) run by two machines alike but for the renderer,
//frame for frame. Each group is skipped, saying why, where there's no Vulkan GPU (or with PSP_GPU=0): the software
//renderer is the reference, and the machines the tests run on needn't have a GPU.
#include "kernel-machine.hpp"
#include "../../ares/psp/ge/gpu/gpu.hpp"

#include <random>

namespace allegrex_test::psp {

using ares::PlayStationPortable::GPU;

//The renderer, made once (none where there's no Vulkan GPU, said once).
static auto renderer() -> GPU* {
  static std::unique_ptr<GPU> gpu;
  static bool tried = false;
  if(!tried) {
    tried = true;
    const char* wanted = std::getenv("PSP_GPU");
    std::string error = "PSP_GPU=0";
    if(!wanted || std::string(wanted) != "0") gpu = GPU::vulkan(error);
    if(gpu) {
      std::printf("  on %s; the host's sums %s, the GPU's own fma() %s\n", gpu->device->name().c_str(),
                  gpu->fused ? "fused (ARM64)" : "rounded step by step",
                  gpu->nativeFma ? "fused" : "not fused (built)");
    } else {
      std::printf("  skipped: %s\n", error.c_str());
    }
  }
  return gpu.get();
}

static auto bitsOf(float value) -> u32 { u32 bits; std::memcpy(&bits, &value, 4); return bits; }
static auto floatOf(u32 bits) -> float { float value; std::memcpy(&value, &bits, 4); return value; }

//texture.cpp's texelAxis() and draw.cpp's fogAmount(), as the software renderer has them (static there).
static auto hostAxis(float coordinate, u32 size, bool clamp, bool linear) -> GE::TexelAxis {
  auto inside = [&](s32 c) -> s32 {
    s32 last = std::min<s32>(size, 512) - 1;
    return clamp ? std::clamp(c, 0, last) : c & last;
  };
  float held = std::isnan(coordinate) ? 0.0f : std::clamp(coordinate, -65536.0f, 65536.0f);
  if(!linear) return {inside(s32(std::floor(held))), 0, 0};
  s32 base = s32(std::floor(held * 256)) - 128;
  return {inside(base >> 8), inside((base >> 8) + 1), base >> 4 & 15};
}
static auto hostFog(float fog) -> u32 {
  if(std::signbit(fog)) return 0;
  if(!(fog < 1)) return 255;
  return u32(fog * 256);
}

//The GPU's exact arithmetic (shaders/exact.glsl) against the host's, on random cases in the ranges the GE uses and
//past them: products, sums, fused multiply-adds (the GPU's own and built), divisions, sums of three products as the
//host's compiler rounds them, 64-bit whole numbers to floats, the blend of a triangle's corners, texel axes, fog and
//depths. Every case must come out the very same bits.
static auto gpuArithmetic() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  std::mt19937 random{20261007};
  auto real = [&](float low, float high) { return low + (high - low) * float(random() >> 8) / 16777216.0f; };
  auto anyFloat = [&] {  //finite, any exponent from 2^-40 to 2^40, either sign
    u32 bits = (random() & 0x807f'ffff) | (87 + random() % 80) << 23;
    return floatOf(bits);
  };
  constexpr u32 Count = 1 << 16;
  //0: a, b, c
  std::vector<u32> cases;
  for(u32 n = 0; n < Count; n++) {
    float a, b, c;
    switch(n % 4) {
    case 0: a = float(random() % 256), b = float(s32(random() % (1u << 30))), c = float(s64(random()) * 997); break;
    case 1: a = anyFloat(), b = anyFloat(), c = anyFloat(); break;
    case 2: a = real(1, 2), b = real(1, 2), c = -(a * b) * (1 + real(0, 1.0f / 4096)); break;  //cancelling
    default: a = real(-65536, 65536), b = real(0.25f, 70000), c = real(-1e6f, 1e6f); break;
    }
    cases.insert(cases.end(), {bitsOf(a), bitsOf(b), bitsOf(c), 0, 0, 0, 0, 0});
  }
  for(bool native : {gpu->nativeFma, false}) {
    bool wasNative = gpu->nativeFma;
    CHECK(gpu->configure(gpu->fused, native), true);
    auto results = gpu->probe(0, cases);
    CHECK(results.size(), cases.size());
    u32 wrong[7] = {};
    for(u32 n = 0; n < Count && results.size() == cases.size(); n++) {
      float a = floatOf(cases[n * 8]), b = floatOf(cases[n * 8 + 1]), c = floatOf(cases[n * 8 + 2]);
      volatile float product = a * b, sum = a + c, quotient = a / b;
      float sum3 = a * c + b * a + c * b;  //(as the host's compiler rounds such a sum: raster.cpp's)
      float expected[7] = {product, sum, std::fma(a, b, c), quotient, std::fma(a, b, c), quotient, sum3};
      for(u32 k : {0u, 1u, 2u, 3u, 4u, 5u, 6u}) wrong[k] += results[n * 8 + k] != bitsOf(expected[k]);
    }
    std::printf("  %u cases (fma %s): wrong products %u, sums %u, fmaExact %u, divide %u, sum3 %u (the GPU's own fma "
                "%u, division %u)\n", Count, native ? "the GPU's" : "built", wrong[0], wrong[1], wrong[4], wrong[5],
                wrong[6], wrong[2], wrong[3]);
    for(u32 k : {0u, 1u, 4u, 5u, 6u}) CHECK(wrong[k], 0u);
    CHECK(gpu->configure(gpu->fused, wasNative), true);
  }

  //1: 64-bit whole numbers, of every length
  cases.clear();
  std::vector<s64> wholes;
  for(u32 n = 0; n < Count; n++) {
    s64 value = s64(u64(random()) << 32 | random()) >> (random() % 64);
    if(n % 7 == 0) value = (value >> 24 << 24) + (s64(1) << 23) * (n & 8 ? 1 : -1);  //(halfway: ties to even)
    wholes.push_back(value);
    cases.insert(cases.end(), {u32(value), u32(u64(value) >> 32), 0, 0, 0, 0, 0, 0});
  }
  auto results = gpu->probe(1, cases);
  CHECK(results.size(), cases.size());
  u32 wrongWholes = 0;
  for(u32 n = 0; n < Count && results.size() == cases.size(); n++) {
    wrongWholes += results[n * 8] != bitsOf(float(wholes[n]));
  }
  CHECK(wrongWholes, 0u);

  //2: a triangle's three channels blended by edge functions over twice its area (raster.cpp's blendColor()); a
  //third with all three the same, which lands on whole numbers
  cases.clear();
  for(u32 n = 0; n < Count; n++) {
    float v0 = float(random() % 256);
    float v1 = n % 3 ? float(random() % 256) : v0;
    float v2 = n % 3 ? float(random() % 256) : v0;
    s64 area = 1 + s64(random() % (1u << (n % 30 + 1)));
    s64 e0 = s64(random() % u64(area + 1)), e1 = s64(random() % u64(area - e0 + 1)), e2 = area - e0 - e1;
    cases.insert(cases.end(), {bitsOf(v0), bitsOf(v1), bitsOf(v2), bitsOf(float(e0)), bitsOf(float(e1)),
                               bitsOf(float(e2)), bitsOf(float(area)), 0});
  }
  results = gpu->probe(2, cases);
  CHECK(results.size(), cases.size());
  u32 wrongBlends = 0, wrongLevels = 0;
  for(u32 n = 0; n < Count && results.size() == cases.size(); n++) {
    float v[3], w[3];
    for(u32 k = 0; k < 3; k++) v[k] = floatOf(cases[n * 8 + k]), w[k] = floatOf(cases[n * 8 + 3 + k]);
    float total = floatOf(cases[n * 8 + 6]);
    float mixed = (v[0] * w[0] + v[1] * w[1] + v[2] * w[2]) / total;
    wrongBlends += results[n * 8] != bitsOf(mixed);
    wrongLevels += results[n * 8 + 1] != u32(s32(mixed));
  }
  CHECK(wrongBlends, 0u);
  CHECK(wrongLevels, 0u);

  //3: texel axes (around texel and sixteenth boundaries, wild, not numbers), fog and depths
  cases.clear();
  std::vector<float> coordinates;
  for(u32 n = 0; n < Count; n++) {
    float c;
    switch(n % 5) {
    case 0: c = std::nextafter(float(s32(random() % 2048) - 1024) / 256, n & 8 ? 1e9f : -1e9f); break;
    case 1: c = real(-600, 600); break;
    case 2: c = anyFloat(); break;
    case 3: c = n & 8 ? floatOf(0x7fc0'0000 | random() % 4096) : (n & 16 ? 1.0f / 0.0f : -1.0f / 0.0f); break;
    default: c = real(-0.01f, 1.01f); break;
    }
    coordinates.push_back(c);
    u32 size = 1u << (random() % 10), clamped = random() % 2, linear = random() % 2;
    cases.insert(cases.end(), {bitsOf(c), size, clamped, linear, 0, 0, 0, 0});
  }
  results = gpu->probe(3, cases);
  CHECK(results.size(), cases.size());
  u32 wrongAxes = 0, wrongFog = 0, wrongDepths = 0;
  for(u32 n = 0; n < Count && results.size() == cases.size(); n++) {
    float c = coordinates[n];
    auto axis = hostAxis(c, cases[n * 8 + 1], cases[n * 8 + 2], cases[n * 8 + 3]);
    bool wrongAxis = results[n * 8] != u32(axis.first) || results[n * 8 + 1] != u32(axis.second) ||
                     results[n * 8 + 2] != u32(axis.fraction);
    if(wrongAxis && wrongAxes < 4) {
      std::printf("  axis of %08x (size %u, clamp %u, linear %u): %d %d %d, not %d %d %d\n", bitsOf(c),
                  cases[n * 8 + 1], cases[n * 8 + 2], cases[n * 8 + 3], s32(results[n * 8]), s32(results[n * 8 + 1]),
                  s32(results[n * 8 + 2]), axis.first, axis.second, axis.fraction);
    }
    wrongAxes += wrongAxis;
    wrongFog += results[n * 8 + 3] != hostFog(c);
    wrongDepths += results[n * 8 + 4] != (std::isnan(c) ? 0 : u32(std::clamp(c, 0.0f, 65535.0f)));  //(ARM64's)
  }
  CHECK(wrongAxes, 0u);
  CHECK(wrongFog, 0u);
  CHECK(wrongDepths, 0u);

  //4: edge functions, a * x + b * y in 64 bits, as triangles' (each part up to 2^18 or so either way) and past
  cases.clear();
  for(u32 n = 0; n < Count; n++) {
    u32 bits = n % 2 ? 19 : 32;
    auto any = [&] { return u32(s32(random()) >> (32 - bits)); };
    cases.insert(cases.end(), {any(), any(), any(), any(), 0, 0, 0, 0});
  }
  results = gpu->probe(4, cases);
  CHECK(results.size(), cases.size());
  u32 wrongEdges = 0;
  for(u32 n = 0; n < Count && results.size() == cases.size(); n++) {
    s64 edge = s64(s32(cases[n * 8])) * s32(cases[n * 8 + 1]) + s64(s32(cases[n * 8 + 2])) * s32(cases[n * 8 + 3]);
    wrongEdges += results[n * 8] != u32(edge) || results[n * 8 + 1] != u32(u64(edge) >> 32) ||
                  results[n * 8 + 2] != u32(edge < 0) || results[n * 8 + 3] != u32(edge == 0);
  }
  CHECK(wrongEdges, 0u);
}

//Random batches drawn by the GPU renderer and by the software renderer, as draw3d.cpp's four-pixels-against-one
//draws its primitives: a 64x40 frame buffer in each format (a format a batch, as a batch has one frame buffer) over
//random VRAM; every pipeline setting at random; textures in every format, with palettes, swizzled, filtered,
//wrapped, in RAM and now and then where the batch draws (read as drawn: the software renderer's then); 2D and 3D
//(in perspective, lit now and then); sprites, triangles, strips, fans, points and lines; 1 to 16 primitives a
//batch. The frame and depth buffers must come out the same, byte for byte, after every batch, and all of VRAM in the
//end; and the GPU must have drawn most of the jobs.
static auto gpuDrawsAsSoftware() -> void {
  auto gpu = renderer();
  if(!gpu) return;
  constexpr u32 Width = 64, Height = 40, Depth = 0x10'0000, Area = 0x0900'0000, AreaSize = 1 << 20;
  constexpr u32 Vertices = 0x0896'0000, VRAM = Memory::VRAMBase;
  constexpr u32 Bits[8] = {16, 16, 16, 32, 4, 8, 16, 32};
  constexpr u32 Kinds[9] = {GE::Triangles, GE::TriangleStrip, GE::TriangleFan, GE::Sprites, GE::Sprites,
                            GE::Triangles, GE::Points, GE::Lines, GE::Sprites};
  std::mt19937 random{20261008};
  auto below = [&](u32 n) { return u32(random() % n); };
  auto chance = [&](u32 percent) -> u32 { return below(100) < percent; };
  auto real = [&](float low, float high) { return low + (high - low) * float(below(1 << 20)) / float(1 << 20); };
  auto fields = [&](std::initializer_list<std::pair<u32, u32>> parts) {
    u32 value = 0;
    for(auto [shift, count] : parts) value |= below(count) << shift;
    return value;
  };
  auto mask = [&](u32 percent) { return chance(percent) ? 0xffu : below(256); };
  auto f24 = [](float value) { return bitsOf(value) >> 8; };
  System software, hardware;
  hardware.ge.renderer = gpu;
  auto before = gpu->statistics;
  std::vector<u8> bytes(AreaSize), vram(Memory::VRAMSize);
  for(auto& byte : bytes) byte = random();
  for(auto& byte : vram) byte = random();
  for(System* s : {&software, &hardware}) {
    s->memory.copyIn(Area, bytes.data(), AreaSize);
    s->memory.copyIn(VRAM, vram.data(), Memory::VRAMSize);
    auto& g = s->ge;
    auto& c = g.commands;
    c[GE::FrameBufferPointer] = 0, c[GE::FrameBufferWidth] = Width;
    c[GE::DepthBufferPointer] = Depth, c[GE::DepthBufferWidth] = Width;
    c[GE::Region2] = 1023 << 10 | 1023;
    c[GE::DepthClipEnable] = 1;
    c[GE::TextureScaleU] = c[GE::TextureScaleV] = f24(1);
    for(u32 n : {0, 4, 8}) g.world[n] = g.view[n] = f24(1);
    for(u32 n = 0; n < 16; n++) g.projection[n] = 0;
    g.projection[0] = g.projection[5] = f24(1), g.projection[10] = f24(-10.5f / 9.5f), g.projection[11] = f24(-1);
    g.projection[14] = f24(-10.0f / 9.5f);
    c[GE::ViewportXScale] = f24(28), c[GE::ViewportYScale] = f24(-18);
    c[GE::ViewportXCenter] = f24(2048 + 32), c[GE::ViewportYCenter] = f24(2048 + 20);
    c[GE::ViewportZScale] = f24(30000), c[GE::ViewportZCenter] = f24(32000);
    c[GE::OffsetX] = 2048 << 4, c[GE::OffsetY] = 2048 << 4;
  }
  u32 batches = 0, differing = 0, pixels = 0, pixelsApart = 0;
  for(u32 batch = 0; batch < 2500; batch++) {
    u32 format = below(4), count = 1 + below(16);
    std::string described;  //(each primitive's kind and settings, said for a batch that differs)
    hardware.ge.drawing.deferring = true;  //(as run() has it: the primitives wait in a batch)
    for(u32 primitive = 0; primitive < count; primitive++) {
      std::vector<std::pair<u32, u32>> commands;
      auto set = [&](u32 command, u32 value) { commands.push_back({command, value}); };
      set(GE::FrameBufferPixelFormat, format);
      set(GE::ClearMode, chance(6) ? 1 | below(8) << 8 : 0);
      set(GE::AlphaTestEnable, chance(40));
      set(GE::AlphaTest, fields({{0, 8}, {8, 256}}) | mask(70) << 16);
      set(GE::DepthTestEnable, chance(60));
      set(GE::DepthTest, below(8));
      set(GE::DepthMask, chance(30));
      set(GE::StencilTestEnable, chance(10));
      set(GE::StencilTest, fields({{0, 8}, {8, 256}, {16, 256}}));
      set(GE::StencilOperation, fields({{0, 6}, {8, 6}, {16, 6}}));
      set(GE::AlphaBlendEnable, chance(50));
      set(GE::BlendMode, fields({{0, 16}, {4, 16}, {8, 8}}));
      set(GE::BlendFixedA, random() & 0xff'ffff);
      set(GE::BlendFixedB, random() & 0xff'ffff);
      set(GE::DitherEnable, chance(30));
      for(u32 row = 0; row < 4; row++) set(GE::Dither0 + row, random() & 0xffff);
      set(GE::ColorTestEnable, chance(15));
      set(GE::ColorTest, below(4));
      set(GE::ColorReference, random() & 0xff'ffff);
      set(GE::ColorTestMask, chance(50) ? 0xff'ffff : random() & 0xff'ffff);
      set(GE::LogicOpEnable, chance(6));
      set(GE::LogicOp, below(16));
      set(GE::MaskColor, chance(80) ? 0 : random() & 0xff'ffff);
      set(GE::MaskAlpha, chance(80) ? 0 : below(256));
      set(GE::FogEnable, chance(40));
      set(GE::FogColor, random() & 0xff'ffff);
      set(GE::FogEnd, f24(real(-3, 6)));
      set(GE::FogSlope, f24(real(-2, 4)));
      set(GE::MinZ, chance(70) ? 0 : below(40000));
      set(GE::MaxZ, chance(70) ? 0xffff : 20000 + below(45536));
      u32 left = chance(30) ? below(16) : 0, top = chance(30) ? below(16) : 0;
      u32 right = chance(30) ? left + below(Width - left) : Width - 1;
      u32 bottom = chance(30) ? top + below(Height - top) : Height - 1;
      set(GE::Scissor1, left | top << 10);
      set(GE::Scissor2, right | bottom << 10);
      set(GE::ShadeMode, chance(75));
      u32 textured = chance(70), textureFormat = below(8), widthBits = below(9), heightBits = below(9);
      set(GE::TextureMappingEnable, textured);
      if(textured) {
        u32 least = 128 / Bits[textureFormat];
        u32 bufferWidth = std::max(1u << widthBits, least) + (chance(20) ? least * below(4) : 0);
        u32 address = chance(5) ? VRAM : Area + below((AreaSize - 0x6'0000) / 16) * 16;
        set(GE::TextureAddress0, address & 0xff'fff0);
        set(GE::TextureBufferWidth0, (address >> 24 & 0xf) << 16 | bufferWidth);
        set(GE::TextureSize0, heightBits << 8 | widthBits);
        set(GE::TextureFormat, textureFormat);
        set(GE::TextureMode, chance(30));
        set(GE::TextureWrap, fields({{0, 2}, {8, 2}}));
        set(GE::TextureFilter, fields({{0, 2}, {8, 2}}));
        set(GE::TextureFunction, fields({{0, 8}, {8, 2}}) | chance(20) << 16);
        set(GE::TextureEnvironmentColor, random() & 0xff'ffff);
        if(textureFormat >= 4) {
          u32 palette = Area + below((AreaSize - 1024) / 16) * 16;
          set(GE::ClutAddress, palette & 0xff'fff0);
          set(GE::ClutAddressUpper, palette >> 8 & 0xf'0000);
          u32 shift = below(chance(70) ? 1 : 32);
          set(GE::ClutFormat, below(4) | shift << 2 | mask(70) << 8 | below(32) << 16);
          set(GE::ClutLoad, 32);
        }
      }
      bool flat = chance(45);  //2D: through mode
      u32 lit = !flat && chance(15);
      set(GE::LightingEnable, lit);
      if(lit) {
        set(GE::LightMode, chance(70));
        set(GE::LightEnable0, 1);
        set(GE::LightType0, below(3));
        for(u32 k = 0; k < 3; k++) set(GE::Light0DirectionX + k, f24(real(-1, 1)));
        for(u32 k = 0; k < 3; k++) set(GE::Light0Ambient + k, random() & 0xff'ffff);
        set(GE::MaterialSpecular, random() & 0xff'ffff);
        set(GE::MaterialDiffuse, random() & 0xff'ffff);
        set(GE::MaterialColor, below(8));
        set(GE::MaterialSpecularCoefficient, f24(real(0, 12)));
      }
      for(System* s : {&software, &hardware}) {
        for(auto [command, value] : commands) s->ge.commands[command] = value;
        if(textured && textureFormat >= 4) s->ge.loadClut();
      }
      u32 kind = Kinds[below(9)];
      u32 vertices = kind == GE::Sprites || kind == GE::Lines ? 2 * (1 + below(3))
                   : kind == GE::Triangles ? 3 * (1 + below(3)) : kind == GE::Points ? 1 + below(6) : 4 + below(3);
      u32 at = Vertices;
      u32 one = chance(30) ? (chance(50) ? 0xffff'ffff : u32(random())) : 0;
      for(u32 k = 0; k < vertices; k++) {
        float u = flat ? real(-4, float(4 << widthBits)) : real(-0.2f, 1.3f);
        float v = flat ? real(-4, float(4 << heightBits)) : real(-0.2f, 1.3f);
        u32 color = one ? one : chance(20) ? 0xffff'ffff : u32(random());
        float normal[3], position[3];
        for(auto& value : normal) value = real(-1, 1);
        if(flat) {
          float steps = chance(50) ? 16 : 1;
          position[0] = std::round(real(-8, Width + 8) * steps) / steps;
          position[1] = std::round(real(-8, Height + 8) * 16) / 16;
          position[2] = real(0, 65535);
        } else {
          position[2] = real(-4, -0.6f);
          position[0] = real(-1.3f, 1.3f) * -position[2];
          position[1] = real(-1.3f, 1.3f) * -position[2];
        }
        for(System* s : {&software, &hardware}) {
          u32 to = at;
          for(float value : {u, v}) s->memory.write(4, to, bitsOf(value)), to += 4;
          s->memory.write(4, to, color), to += 4;
          for(float value : normal) s->memory.write(4, to, bitsOf(value)), to += 4;
          for(float value : position) s->memory.write(4, to, bitsOf(value)), to += 4;
        }
        at += 36;
      }
      for(System* s : {&software, &hardware}) {
        s->ge.commands[GE::VertexType] = 0x1ff | (flat ? 1 << 23 : 0);
        s->ge.vertexAddress = Vertices;
        s->ge.primitive(kind, vertices);
      }
      auto& c = software.ge.commands;
      char text[160];
      std::snprintf(text, sizeof(text), " [%u %s%s%s clear %x blend %x/%x stencil %x/%x logic %x dither %x "
                    "test %x/%x fog %x mask %x/%x]", kind, flat ? "2D" : "3D", textured ? " textured" : "",
                    lit ? " lit" : "",
                    c[GE::ClearMode] & 0xffff, c[GE::AlphaBlendEnable] & 1, c[GE::BlendMode] & 0xfff,
                    c[GE::StencilTestEnable] & 1, c[GE::StencilOperation] & 0x70707, c[GE::LogicOpEnable] & 1,
                    c[GE::DitherEnable] & 1, c[GE::AlphaTestEnable] & 1, c[GE::ColorTestEnable] & 1,
                    c[GE::FogEnable] & 1, c[GE::MaskColor] & 0xff'ffff, c[GE::MaskAlpha] & 0xff);
      described += text;
    }
    hardware.ge.drawing.deferring = false;
    hardware.ge.launch(true);  //(as run() ends: the batch drawn, by the renderer)
    batches++;
    u32 bytesPerPixel = format == 3 ? 4 : 2;
    bool apart = false;
    std::string where;
    auto hexOf = [](u32 value) {
      char text[12];
      std::snprintf(text, sizeof(text), "%x", value);
      return std::string(text);
    };
    for(u32 n = 0; n < Width * Height; n++) {
      u32 wanted = 0, drawn = 0;
      std::memcpy(&wanted, software.memory.vram.data() + n * bytesPerPixel, bytesPerPixel);
      std::memcpy(&drawn, hardware.memory.vram.data() + n * bytesPerPixel, bytesPerPixel);
      if(wanted != drawn && where.size() < 120) {
        where += " (" + std::to_string(n % Width) + ", " + std::to_string(n / Width) + "): " + hexOf(wanted) +
                 " not " + hexOf(drawn);
      }
      pixels++, pixelsApart += wanted != drawn, apart |= wanted != drawn;
    }
    bool depthApart = std::memcmp(software.memory.vram.data() + Depth, hardware.memory.vram.data() + Depth, 0x4000);
    apart |= depthApart;
    if(apart && differing++ < 6) {
      std::printf("  batch %u (format %u) differs%s:%s%s\n", batch, format, depthApart ? " (depth too)" : "",
                  where.c_str(), described.c_str());
    }
    //(each batch over the same VRAM: one that differs doesn't leave the next batches differing too)
    if(apart) hardware.memory.copyIn(VRAM, software.memory.vram.data(), Memory::VRAMSize);
  }
  auto& after = gpu->statistics;
  u64 gpuJobs = after.gpuJobs - before.gpuJobs, cpuJobs = after.cpuJobs - before.cpuJobs;
  std::printf("  %u batches: %u apart, %u of %u pixels apart; the GPU drew %llu jobs, the CPU %llu (lines %llu, 3D "
              "sprites' texels %llu, 2D coordinates %llu) in %llu runs\n", batches, differing, pixelsApart, pixels,
              (unsigned long long)gpuJobs, (unsigned long long)cpuJobs,
              (unsigned long long)(after.lines - before.lines),
              (unsigned long long)(after.spriteTexels3D - before.spriteTexels3D),
              (unsigned long long)(after.coordinates2D - before.coordinates2D),
              (unsigned long long)(after.runs - before.runs));
  CHECK(differing, 0u);
  CHECK(software.memory.vram == hardware.memory.vram, true);
  CHECK(gpuJobs > 4 * cpuJobs, true);
  hardware.ge.renderer = nullptr;
}

//pspsdk's samples (PSP_TEST_PROGRAMS) run by two machines alike but for the renderer, frame by frame for a second
//and a half of the PSP's time: every frame's picture and all of VRAM the same.
static auto gpuSamples() -> void {
  auto gpu = renderer();
  if(!gpu || !testPrograms()) return;
  for(const char* name : {"cube", "blend", "clut", "blit", "celshading", "envmap", "doublelist", "gu"}) {
    auto program = testProgram((std::string(name) + ".elf").c_str());
    if(program.empty()) continue;
    KernelMachine software, hardware;
    hardware.system.ge.renderer = gpu;
    auto before = gpu->statistics;
    std::string error;
    for(auto* m : {&software, &hardware}) {
      m->system.recompiler.enabled = true;
      CHECK(m->kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/SAMPLE/EBOOT.PBP", error), true);
    }
    u32 frames = 0, framesApart = 0;
    u64 pixels = 0, pixelsApart = 0;
    std::vector<u32> picture[2];
    for(u32 frame = 0; frame < 90; frame++) {
      software.kernel.run(Kernel::VblankCycles), hardware.kernel.run(Kernel::VblankCycles);
      software.kernel.picture(picture[0]), hardware.kernel.picture(picture[1]);
      u32 apart = 0;
      for(u32 n = 0; n < picture[0].size() && n < picture[1].size(); n++) apart += picture[0][n] != picture[1][n];
      frames++, framesApart += apart || software.system.memory.vram != hardware.system.memory.vram;
      pixels += picture[0].size(), pixelsApart += apart;
    }
    auto& after = gpu->statistics;
    std::printf("  %s: %u frames, %u apart (%llu of %llu pixels); the GPU drew %llu jobs (%llu pixels' boxes), the "
                "CPU %llu (%llu)\n", name, frames, framesApart, (unsigned long long)pixelsApart,
                (unsigned long long)pixels, (unsigned long long)(after.gpuJobs - before.gpuJobs),
                (unsigned long long)(after.gpuPixels - before.gpuPixels),
                (unsigned long long)(after.cpuJobs - before.cpuJobs),
                (unsigned long long)(after.cpuPixels - before.cpuPixels));
    CHECK(framesApart, 0u);
    hardware.system.ge.renderer = nullptr;
  }
}

auto gpuTests() -> Tests {
  return {
    {"gpu arithmetic against the host", gpuArithmetic},
    {"gpu batches against the software renderer", gpuDrawsAsSoftware},
    {"gpu samples against the software renderer", gpuSamples},
  };
}

}
