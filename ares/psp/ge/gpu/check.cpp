//The start-up check (gpu.hpp's check()): before a game draws with it, the GPU draws what the software renderer draws,
//in a machine of its own (its memory and its GE, nothing of the game's), and the two pictures are compared. A
//driver that compiles the shaders wrong, blends or narrows wrong, or steps a texture wrong shows it here, and the
//software renderer draws the game instead. What's drawn, into 64x48 frame buffers:
//  - sprites, flat and textured (enlarged, shrunk, mirrored), some in clear mode, in 8888 and 5650: the GPU's bytes
//    are the software renderer's exactly, as tests/psp/gpu.cpp's gpuSprites has them;
//  - shaded triangles, some blended over them: close (each channel within 8 of the software renderer's but at the
//    edges, where the two may cover a pixel differently: at most 1 pixel in 50 further off);
//  - where the GPU reads the frame buffer in the shader (Backend::reads), a frame buffer of a game's size (480x272
//    in rows of 512) drawn as games draw it: 128 opaque sprites, each followed by a blended one over it (moved a
//    few pixels) through a write mask keeping part of each channel (which reads wherever the GPU can), exactly. A
//    driver whose reads there see the pixels as they were before the draw just before (Qualcomm's own, some of the
//    time, which passes the rest) fails here: vulkan.cpp has nothing read on that one, and where another fails
//    System::startRenderer() has the GPU blend with its own units instead.
//  - in fast mode, where the GPU transforms 3D vertices itself (mesh()), 3D triangles in perspective, smooth, half of
//    them lit by a light, some textured: close, as the 2D triangles are, so a driver that takes transform.vert wrong
//    fails here rather than in a game.
//It takes some milliseconds, most of them the GPU making its first pipelines (kept for the game).

auto GPU::check(std::string& error) -> bool {
  if(!ready()) return error = "it isn't ready", false;
  struct Machine {
    Memory memory;
    GE ge{memory};
    //(watched pages, so that the GE keeps textures decoded, which the GPU draws from: no one else to tell)
    Machine() { memory.power(4 << 20), memory.watching = [](u32) {}; }
  };
  auto software = std::make_unique<Machine>(), hardware = std::make_unique<Machine>();
  hardware->ge.setRenderer(this);
  constexpr u32 Vertices = 0x0820'0000, Texels = 0x0830'0000;
  constexpr u32 Width = 64, Height = 48;
  u32 seed = 20261007;
  auto random = [&] { return seed = seed * 1664525 + 1013904223, seed >> 8; };
  auto bitsOf = [](float value) { u32 bits; std::memcpy(&bits, &value, 4); return bits; };

  //(through mode: float u, v; 8888 color; float x, y, z; the vertices written at Vertices, then drawn)
  struct Corner { float u, v; u32 color; float x, y; };
  auto draw = [&](Machine& m, u32 kind, const std::vector<Corner>& corners) {
    u32 to = Vertices;
    for(auto& c : corners) {
      for(u32 word : {bitsOf(c.u), bitsOf(c.v), c.color, bitsOf(c.x), bitsOf(c.y), 0u}) {
        m.memory.write(4, to, word), to += 4;
      }
    }
    m.ge.vertexAddress = Vertices;
    m.ge.primitive(kind, corners.size());
  };
  auto into = [&](Machine& m, u32 buffer, u32 format) {
    auto& c = m.ge.commands;
    c[GE::FrameBufferPointer] = buffer, c[GE::FrameBufferWidth] = Width, c[GE::FrameBufferPixelFormat] = format;
    c[GE::DepthBufferPointer] = 0x1f'0000, c[GE::DepthBufferWidth] = Width;
    c[GE::Region2] = 1023 << 10 | 1023;
    c[GE::Scissor2] = (Width - 1) | (Height - 1) << 10;
    c[GE::VertexType] = 0x80'019f;
    c[GE::ShadeMode] = 1;
    c[GE::AlphaBlendEnable] = 0, c[GE::TextureMappingEnable] = 0, c[GE::ClearMode] = 0;
  };
  auto texture = [&](Machine& m) {
    auto& c = m.ge.commands;
    c[GE::TextureMappingEnable] = 1;
    c[GE::TextureAddress0] = Texels & 0xff'ffff;
    c[GE::TextureBufferWidth0] = (Texels >> 24 & 0xf) << 16 | 16;
    c[GE::TextureSize0] = 4 << 8 | 4;  //(16x16)
    c[GE::TextureFormat] = 3;
    c[GE::TextureFunction] = 3 | 1 << 8;  //(replace, with the texture's alpha)
    c[GE::TextureFilter] = 0;
  };

  //(3D: float u, v; 8888 color; float normal; float position)
  struct Corner3 { float u, v; u32 color; float normal[3], at[3]; };
  auto draw3d = [&](Machine& m, const std::vector<Corner3>& corners) {
    u32 to = Vertices;
    for(auto& c : corners) {
      for(u32 word : {bitsOf(c.u), bitsOf(c.v), c.color, bitsOf(c.normal[0]), bitsOf(c.normal[1]),
                      bitsOf(c.normal[2]), bitsOf(c.at[0]), bitsOf(c.at[1]), bitsOf(c.at[2])}) {
        m.memory.write(4, to, word), to += 4;
      }
    }
    m.ge.vertexAddress = Vertices;
    m.ge.primitive(GE::Triangles, corners.size());
  };
  auto f24 = [&](float value) { return bitsOf(value) >> 8; };
  std::vector<std::vector<Corner>> sprites, textured, triangles;
  for(u32 n = 0; n < 48; n++) {
    float x = random() % 56, y = random() % 40, w = 1 + random() % 40, h = 1 + random() % 30;
    float u = random() % 16, v = random() % 16;
    const float scale[] = {1, 2, 0.5f, 0.75f, 1.5f};
    float us = w * scale[random() % 5], vs = h * scale[random() % 5];
    if(random() % 4 == 0) us = -us;
    u32 color = random() | random() << 24;
    sprites.push_back({{0, 0, color, x + (random() % 16) / 16.0f, y}, {0, 0, color, x + w, y + h}});
    textured.push_back({{u, v, color, x, y}, {u + us, v + vs, color, x + w, y + h}});
  }
  for(u32 n = 0; n < 24; n++) {
    std::vector<Corner> corners;
    for(u32 k = 0; k < 3; k++) {
      corners.push_back({0, 0, random() | random() << 24, float(random() % (Width * 16)) / 16.0f,
                         float(random() % (Height * 16)) / 16.0f});
    }
    triangles.push_back(corners);
  }
  bool transforms = fast && backend->transforms;
  u64 meshes = statistics.meshes;
  transformsFailed = false;
  std::vector<std::vector<Corner3>> solids;
  for(u32 n = 0; n < 24 && transforms; n++) {
    std::vector<Corner3> corners;
    for(u32 k = 0; k < 3; k++) {
      float depth = -1.5f - (random() % 1000) / 666.0f;  //(in front of the camera, from 1.5 to 3 away)
      float x = (s32(random() % 2000) - 1000) / 1000.0f * -depth, y = (s32(random() % 2000) - 1000) / 1000.0f * -depth;
      float facing[3] = {(s32(random() % 200) - 100) / 400.0f, (s32(random() % 200) - 100) / 400.0f, 1};
      corners.push_back({float(random() % 16), float(random() % 16), random() | random() << 24,
                         {facing[0], facing[1], facing[2]}, {x, y, depth}});
    }
    solids.push_back(corners);
  }
  //(the game-sized frame buffer's: each opaque sprite, then the blended one over it, moved up to 4 pixels each way)
  constexpr u32 Large = 0x1'0000, LargeStride = 512, LargeWidth = 480, LargeHeight = 272;
  bool reads = backend->reads;
  std::vector<std::vector<Corner>> layered;
  for(u32 n = 0; n < 128 && reads; n++) {
    float x = 4 + random() % (LargeWidth - 88), y = 4 + random() % (LargeHeight - 68);
    float w = 8 + random() % 72, h = 8 + random() % 52;
    float dx = s32(random() % 9) - 4, dy = s32(random() % 9) - 4;
    u32 color = random() | random() << 24, over = random() | random() << 24;
    layered.push_back({{0, 0, color, x, y}, {0, 0, color, x + w, y + h}});
    layered.push_back({{0, 0, over, x + dx, y + dy}, {0, 0, over, x + dx + w, y + dy + h}});
  }

  for(Machine* m : {software.get(), hardware.get()}) {
    for(u32 n = 0; n < 16 * 16; n++) m->memory.write(4, Texels + n * 4, n * 0x9e37'79b9u);
    //sprites, in 8888 and in 5650
    for(u32 format : {3u, 0u}) {
      into(*m, format == 3 ? 0 : 0x4000, format);
      for(auto& one : sprites) draw(*m, GE::Sprites, one);
      texture(*m);
      for(auto& one : textured) draw(*m, GE::Sprites, one);
      m->ge.commands[GE::TextureMappingEnable] = 0;
      m->ge.commands[GE::ClearMode] = 1 | 7 << 8;  //(clear mode: color, stencil and depth written as they come)
      for(u32 n = 0; n < 4; n++) draw(*m, GE::Sprites, sprites[n]);
      m->ge.commands[GE::ClearMode] = 0;
    }
    //shaded triangles, the second half blended over the first (source alpha, one minus it)
    into(*m, 0x8000, 3);
    for(u32 n = 0; n < triangles.size(); n++) {
      if(n == triangles.size() / 2) m->ge.commands[GE::AlphaBlendEnable] = 1, m->ge.commands[GE::BlendMode] = 0x32;
      draw(*m, GE::Triangles, triangles[n]);
    }
    //3D (fast mode): the camera looking down -z, w the distance, the 64x48 picture the screen's middle
    auto& c = m->ge.commands;
    if(transforms) {
      into(*m, 0xc000, 3);
      c[GE::VertexType] = 0x1ff, c[GE::DepthClipEnable] = 1, c[GE::MaxZ] = 0xffff;
      for(u32 n = 0; n < 12; n++) m->ge.world[n] = m->ge.view[n] = f24(n % 4 == 0 ? 1 : 0);
      for(u32 n = 0; n < 16; n++) m->ge.projection[n] = 0;
      m->ge.projection[0] = m->ge.projection[5] = f24(1);
      m->ge.projection[10] = m->ge.projection[11] = f24(-1), m->ge.projection[14] = f24(-2);
      c[GE::ViewportXScale] = f24(32), c[GE::ViewportYScale] = f24(-24), c[GE::ViewportZScale] = f24(30000);
      c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32768);
      c[GE::OffsetX] = (2048 - 32) << 4, c[GE::OffsetY] = (2048 - 24) << 4;
      c[GE::TextureScaleU] = c[GE::TextureScaleV] = f24(1.0f / 16);
      c[GE::MaterialColor] = 2, c[GE::AmbientLightColor] = 0x30'3030, c[GE::AmbientLightAlpha] = 0xff;
      c[GE::LightEnable0] = 1, c[GE::LightType0] = 0;  //(directional: ambient and diffuse)
      c[GE::Light0X] = f24(0.3f), c[GE::Light0X + 1] = f24(0.2f), c[GE::Light0X + 2] = f24(1);
      c[GE::Light0Ambient] = 0x10'1010, c[GE::Light0Ambient + 1] = 0xff'ffff;
      //(six triangles a PRIM, enough vertices for mesh() to take them: lit and unlit in turn, the last textured)
      static_assert(6 * 3 >= MeshLeast);
      for(u32 group = 0; group < solids.size() / 6; group++) {
        std::vector<Corner3> corners;
        for(u32 n = group * 6; n < group * 6 + 6; n++) {
          corners.insert(corners.end(), solids[n].begin(), solids[n].end());
        }
        c[GE::LightingEnable] = group & 1;
        if(group == 3) texture(*m);
        draw3d(*m, corners);
        c[GE::TextureMappingEnable] = 0;
      }
      c[GE::LightingEnable] = 0;
    }
    //the game-sized frame buffer, opaque and blended sprites in turn, the blended through the write mask
    into(*m, Large, 3);
    c[GE::FrameBufferWidth] = LargeStride, c[GE::Scissor2] = (LargeWidth - 1) | (LargeHeight - 1) << 10;
    for(u32 n = 0; n < layered.size(); n++) {
      bool blended = n & 1;
      c[GE::AlphaBlendEnable] = blended, c[GE::BlendMode] = 0x32, c[GE::MaskColor] = blended ? 0x0f'0f0f : 0;
      draw(*m, GE::Sprites, layered[n]);
    }
    c[GE::AlphaBlendEnable] = 0, c[GE::MaskColor] = 0;
    m->ge.settleAll();
  }
  hardware->ge.setRenderer(nullptr);

  auto& a = software->memory.vram;
  auto& b = hardware->memory.vram;
  bool passed = true;
  for(u32 buffer : {0u, 0x4000u}) {
    u32 bytes = buffer ? 2 : 4, apart = 0;
    for(u32 n = buffer; n < buffer + Width * Height * bytes; n++) apart += a[n] != b[n];
    if(apart) {
      error = "its sprites are " + std::to_string(apart) + " bytes off the software renderer's (" +
              (buffer ? "5650" : "8888") + ")";
      passed = false;
      break;
    }
  }
  if(passed) {
    u32 far = 0;
    for(u32 n = 0; n < Width * Height; n++) {
      for(u32 channel = 0; channel < 3; channel++) {
        s32 d = s32(a[0x8000 + n * 4 + channel]) - s32(b[0x8000 + n * 4 + channel]);
        if(d > 8 || d < -8) { far++; break; }
      }
    }
    if(far > Width * Height / 50) {
      error = "its triangles are " + std::to_string(far) + " pixels off the software renderer's";
      passed = false;
    }
  }
  if(passed && transforms && statistics.meshes == meshes) {
    error = "its 3D triangles weren't transformed by the GPU";
    passed = false, transformsFailed = true;
  }
  if(passed && transforms) {
    u32 far = 0;
    for(u32 n = 0; n < Width * Height; n++) {
      for(u32 channel = 0; channel < 3; channel++) {
        s32 d = s32(a[0xc000 + n * 4 + channel]) - s32(b[0xc000 + n * 4 + channel]);
        if(d > 8 || d < -8) { far++; break; }
      }
    }
    if(far > Width * Height / 50) {
      error = "its 3D triangles, transformed by the GPU, are " + std::to_string(far) +
              " pixels off the software renderer's";
      passed = false, transformsFailed = true;
    }
  }
  if(passed && reads) {
    u32 apart = 0;
    for(u32 n = Large; n < Large + LargeStride * LargeHeight * 4; n++) apart += a[n] != b[n];
    if(apart) {
      error = "its draws reading the frame buffer in a game-sized picture are " + std::to_string(apart) +
              " bytes off the software renderer's";
      passed = false;
    }
  }
  drop();
  return passed;
}
