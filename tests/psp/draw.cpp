//Drawing (ares/psp/ge draw.cpp, texture.cpp, pixel.cpp): sprites, triangles and points in 2D, textures in each
//format with the palette, swizzling and filtering, the texture functions, the pixel pipeline's tests, blending,
//dithering, logic operations and write masks; and, with PSP_TEST_PROGRAMS, pspsdk's GU samples "blit" (three ways)
//and "doublelist", whose pictures are known pixel for pixel. Expected values are worked out by hand from pspgu.h's
//descriptions and the PSP's rounding (see pixel.cpp).
#include "kernel-machine.hpp"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <random>
#include <set>

namespace allegrex_test::psp {

constexpr u32 VertexData = 0x0896'0000, Texture = 0x0898'0000, Palette = 0x089a'0000;
constexpr u32 VRAM = Memory::VRAMBase;
constexpr u32 DepthSeen = Memory::VRAMBase + 3 * Memory::VRAMSize;  //VRAM's fourth copy: depth as the GE has it

//A through-mode vertex: float texture coordinates, 8888 color, float position (vertex type 0x80019f).
struct V { float u, v; u32 color; float x, y, z; };

//The GE ready to draw into a frame buffer at VRAM's start, 16 pixels wide, in format; the depth buffer at 0x10000.
struct Canvas {
  System s;
  GE& ge = s.ge;
  Memory& memory = s.memory;
  u32 format;
  Canvas(u32 format = 3) : format(format) {
    ge.commands[GE::FrameBufferPointer] = 0;
    ge.commands[GE::FrameBufferWidth] = 16;
    ge.commands[GE::FrameBufferPixelFormat] = format;
    ge.commands[GE::DepthBufferPointer] = 0x1'0000;
    ge.commands[GE::DepthBufferWidth] = 16;
    ge.commands[GE::Scissor2] = 1023 << 10 | 1023;
    ge.commands[GE::Region2] = 1023 << 10 | 1023;
    ge.commands[GE::VertexType] = 0x80'019f;
    ge.commands[GE::ShadeMode] = 1;
  }
  auto draw(u32 kind, std::initializer_list<V> vertices) -> void { draw(kind, std::vector<V>(vertices)); }
  auto draw(u32 kind, const std::vector<V>& vertices) -> void {
    u32 at = VertexData;
    for(auto& vertex : vertices) {
      for(float value : {vertex.u, vertex.v}) memory.write(4, at, bits(value)), at += 4;
      memory.write(4, at, vertex.color), at += 4;
      for(float value : {vertex.x, vertex.y, vertex.z}) memory.write(4, at, bits(value)), at += 4;
    }
    ge.vertexAddress = VertexData;
    ge.primitive(kind, vertices.size());
  }
  static auto bits(float value) -> u32 { u32 word; std::memcpy(&word, &value, 4); return word; }
  auto pixel(u32 x, u32 y) -> u32 {
    return format == 3 ? memory.read(4, VRAM + (y * 16 + x) * 4) : memory.read(2, VRAM + (y * 16 + x) * 2);
  }
  auto setPixel(u32 x, u32 y, u32 value) -> void {
    if(format == 3) memory.write(4, VRAM + (y * 16 + x) * 4, value);
    else memory.write(2, VRAM + (y * 16 + x) * 2, value);
  }
  auto depth(u32 x, u32 y) -> u32 { return memory.read(2, DepthSeen + 0x1'0000 + (y * 16 + x) * 2); }
  //a texture at Texture: width x height texels of format, rows of bufferWidth texels, nearest, replace with alpha
  auto texture(u32 format, u32 width, u32 height, u32 bufferWidth) -> void {
    ge.commands[GE::TextureMappingEnable] = 1;
    ge.commands[GE::TextureAddress0] = Texture & 0xff'ffff;
    ge.commands[GE::TextureBufferWidth0] = (Texture >> 24 & 0xf) << 16 | bufferWidth;
    ge.commands[GE::TextureSize0] = std::countr_zero(height) << 8 | std::countr_zero(width);
    ge.commands[GE::TextureFormat] = format;
    ge.commands[GE::TextureFunction] = 3 | 1 << 8;
  }
};

//Sprites: 1:1 texels; corners bottom-left and top-right turn the texture a quarter; corners the other way round
//mirror it both ways.
static auto drawSprites() -> void {
  Canvas c;
  c.texture(3, 4, 4, 4);
  auto texel = [](u32 x, u32 y) { return (x + 1) | (y + 1) << 8 | 0x80 << 16; };
  for(u32 y = 0; y < 4; y++) for(u32 x = 0; x < 4; x++) c.memory.write(4, Texture + (y * 4 + x) * 4, texel(x, y) | 0xff00'0000);
  c.draw(GE::Sprites, {{0, 0, 0, 2, 1, 0}, {4, 4, 0, 6, 5, 0}});
  bool same = true;
  for(u32 y = 0; y < 4; y++) for(u32 x = 0; x < 4; x++) same &= c.pixel(2 + x, 1 + y) == texel(x, y);  //alpha: the stencil, 0
  CHECK(same, true);
  CHECK(c.pixel(6, 1), 0); CHECK(c.pixel(2, 5), 0); CHECK(c.pixel(1, 1), 0);
  c.memory.fill(VRAM, 0, 0x1000);
  c.draw(GE::Sprites, {{0, 0, 0, 2, 5, 0}, {4, 4, 0, 6, 1, 0}});  //bottom-left to top-right
  bool turned = true;
  for(u32 j = 0; j < 4; j++) for(u32 i = 0; i < 4; i++) turned &= c.pixel(2 + i, 1 + j) == texel(3 - j, i);
  CHECK(turned, true);
  c.memory.fill(VRAM, 0, 0x1000);
  c.draw(GE::Sprites, {{0, 0, 0, 6, 5, 0}, {4, 4, 0, 2, 1, 0}});  //bottom-right to top-left
  bool mirrored = true;
  for(u32 j = 0; j < 4; j++) for(u32 i = 0; i < 4; i++) mirrored &= c.pixel(2 + i, 1 + j) == texel(3 - i, 3 - j);
  CHECK(mirrored, true);
}

//Texture formats: 5650, 5551, 4444 and 8888 widened; palette indices of 4, 8, 16 and 32 bits, with the palette's
//shift, mask and offset; a swizzled texture reads as the plain one.
static auto drawTextureFormats() -> void {
  Canvas c;
  auto row = [&](u32 format, u32 bufferWidth, u32 count) {  //draws the texture's first count texels to row 0
    c.texture(format, 8, 1, bufferWidth);
    c.memory.fill(VRAM, 0, 0x100);
    c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {float(count), 1, 0, float(count), 1, 0}});
  };
  for(u32 n : {0u, 1u, 2u, 3u}) c.memory.write(2, Texture + n * 2, std::array<u32, 4>{0x001f, 0x07e0, 0xf800, 0x0421}[n]);
  row(0, 8, 4);
  CHECK(c.pixel(0, 0), 0x0000'00ff); CHECK(c.pixel(1, 0), 0x0000'ff00); CHECK(c.pixel(2, 0), 0x00ff'0000);
  CHECK(c.pixel(3, 0), 0x0000'8608);  //red 1 of 31, green 33 of 63
  for(u32 n : {0u, 1u, 2u}) c.memory.write(2, Texture + n * 2, std::array<u32, 3>{0x7fff, 0x0010, 0x8000}[n]);
  row(1, 8, 3);
  CHECK(c.pixel(0, 0), 0x00ff'ffff); CHECK(c.pixel(1, 0), 0x0000'0084); CHECK(c.pixel(2, 0), 0);
  c.memory.write(2, Texture, 0x0123);
  row(2, 8, 1);
  CHECK(c.pixel(0, 0), 0x0011'2233);
  c.memory.write(4, Texture, 0x8812'3456);
  row(3, 4, 1);
  CHECK(c.pixel(0, 0), 0x0012'3456);

  for(u32 n = 0; n < 256; n++) c.memory.write(4, Palette + n * 4, n * 0x0001'0203);
  c.ge.commands[GE::ClutAddress] = Palette & 0xff'ffff;
  c.ge.commands[GE::ClutAddressUpper] = (Palette >> 8) & 0xf'0000;
  c.ge.commands[GE::ClutLoad] = 32;  //256 entries of 4 bytes
  c.ge.loadClut();
  c.ge.commands[GE::ClutFormat] = 3 | 0xff << 8;
  for(u32 n : {0u, 1u, 2u, 3u}) c.memory.write(1, Texture + n, std::array<u32, 4>{0, 1, 7, 255}[n]);
  row(5, 16, 4);
  CHECK(c.pixel(0, 0), 0); CHECK(c.pixel(1, 0), 0x0001'0203); CHECK(c.pixel(2, 0), 7 * 0x0001'0203);
  CHECK(c.pixel(3, 0), (255 * 0x0001'0203) & 0x00ff'ffff);
  c.ge.commands[GE::ClutFormat] = 3 | 4 << 2 | 0x0f << 8 | 1 << 16;  //(index >> 4) & 15, plus 16
  c.memory.write(1, Texture, 0x37);
  row(5, 16, 1);
  CHECK(c.pixel(0, 0), 19 * 0x0001'0203);
  c.ge.commands[GE::ClutFormat] = 3 | 0xff << 8;
  c.memory.write(1, Texture, 0x21);  //4 bits: the low nibble first
  row(4, 32, 2);
  CHECK(c.pixel(0, 0), 1 * 0x0001'0203); CHECK(c.pixel(1, 0), 2 * 0x0001'0203);
  for(u32 n = 0; n < 4; n++) c.memory.write(2, Palette + n * 2, std::array<u32, 4>{0, 0x001f, 0x07e0, 0xf800}[n]);
  c.ge.commands[GE::ClutLoad] = 1;
  c.ge.loadClut();
  c.ge.commands[GE::ClutFormat] = 0 | 0xff << 8;  //5650 entries
  c.memory.write(2, Texture, 3); c.memory.write(2, Texture + 2, 1);
  row(6, 8, 2);
  CHECK(c.pixel(0, 0), 0x00ff'0000); CHECK(c.pixel(1, 0), 0x0000'00ff);

  //swizzled: pspsdk's layout, 16 bytes by 8 rows a block, rows of each block in turn, blocks left to right
  Canvas plain, swizzled;
  constexpr u32 Width = 16, Height = 16, RowBytes = Width * 4;
  std::vector<u32> image(Width * Height);
  for(u32 n = 0; n < image.size(); n++) image[n] = 0x0100'0000 * (n & 0x7f) | n * 0x10203;
  for(auto* canvas : {&plain, &swizzled}) canvas->texture(3, Width, Height, Width);
  for(u32 n = 0; n < image.size(); n++) plain.memory.write(4, Texture + n * 4, image[n]);
  u32 out = 0;
  for(u32 blockY = 0; blockY < Height / 8; blockY++) {
    for(u32 blockX = 0; blockX < RowBytes / 16; blockX++) {
      for(u32 line = 0; line < 8; line++) {
        for(u32 word = 0; word < 4; word++) {
          u32 y = blockY * 8 + line, byte = blockX * 16 + word * 4;
          swizzled.memory.write(4, Texture + out, image[y * Width + byte / 4]);
          out += 4;
        }
      }
    }
  }
  swizzled.ge.commands[GE::TextureMode] = 1;
  for(auto* canvas : {&plain, &swizzled}) canvas->draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 16, 0, 16, 16, 0}});
  bool same = true;
  for(u32 y = 0; y < 16; y++) for(u32 x = 0; x < 16; x++) same &= plain.pixel(x, y) == swizzled.pixel(x, y);
  CHECK(same, true);
  CHECK(plain.pixel(5, 9), (image[9 * 16 + 5]) & 0x00ff'ffff);
}

//Filtering: four texels blended by sixteenths around the point less half a texel; repeating and clamping past the
//edge.
static auto drawFilter() -> void {
  Canvas c;
  c.texture(3, 2, 2, 4);
  for(u32 n : {0u, 1u, 2u, 3u}) c.memory.write(4, Texture + (n / 2 * 4 + n % 2) * 4, std::array<u32, 4>{0, 160, 32, 192}[n]);
  auto t = c.ge.sampler();
  t.linear = true;
  CHECK(c.ge.sample(t, 1.0f, 1.0f) & 0xff, 96);   //all four, evenly
  CHECK(c.ge.sample(t, 0.75f, 0.5f) & 0xff, 40);  //a quarter of the way across, on the top row's middle
  t.linear = false;
  CHECK(c.ge.sample(t, 1.99f, 0.0f) & 0xff, 160);
  CHECK(c.ge.sample(t, 2.5f, 0.0f) & 0xff, 0);     //past the edge: around again
  t.clampU = true;
  CHECK(c.ge.sample(t, 2.5f, 0.0f) & 0xff, 160);   //held at the edge
  CHECK(c.ge.sample(t, -3.0f, 1.5f) & 0xff, 32);
  //Each step of the blend drops its fraction, as a PSP's does: across the top two (0 and 15: 7.5, so 7), across the
  //bottom two (0 and 17: 8.5, so 8), then down between those (7.5, so 7), where blending all four at once gives 8.
  for(u32 n : {0u, 1u, 2u, 3u}) c.memory.write(4, Texture + (n / 2 * 4 + n % 2) * 4, std::array<u32, 4>{0, 15, 0, 17}[n]);
  auto stepwise = c.ge.sampler();
  stepwise.linear = true;
  CHECK(c.ge.sample(stepwise, 1.0f, 1.0f) & 0xff, 7);
}

//The texture functions, with the PSP's rounding.
static auto drawTextureFunctions() -> void {
  Canvas c;
  auto apply = [&](u32 function, u32 color, u32 texel) {
    c.ge.commands[GE::TextureFunction] = function;
    return c.ge.textureFunction(color, texel);
  };
  CHECK(apply(0, 0x0000'0080, 0x0000'00ff) & 0xff, 128);                  //modulate: (128+1)*255/256
  CHECK(apply(0 | 1 << 8, 0xff00'0000, 0x8000'0000) >> 24, 128);          //and alpha: (255+1)*128/256
  CHECK(apply(0, 0xff00'0000, 0x8000'0000) >> 24, 255);                   //without: the fragment's alpha
  CHECK(apply(0 | 1 << 16, 0x0000'0080, 0x0000'00ff) & 0xff, 255);       //doubled, and held to 255
  CHECK(apply(1 | 1 << 8, 0x7700'0064, 0x4000'00c8) & 0xff, 125);         //decal: (101*191 + 201*64)/256
  CHECK(apply(1 | 1 << 8, 0x7700'0064, 0x4000'00c8) >> 24, 0x77);
  CHECK(apply(1, 0x0000'0064, 0x4000'00c8) & 0xff, 200);                  //decal without alpha: the texel
  c.ge.commands[GE::TextureEnvironmentColor] = 200;
  CHECK(apply(2, 0x0000'0064, 0x0000'0032) & 0xff, 120);                  //blend: (205*100 + 50*200 + 255)/256
  CHECK(apply(3 | 1 << 8, 0x1100'0011, 0x2200'0022), 0x2200'0022);       //replace
  CHECK(apply(3, 0x1100'0011, 0x2200'0022), 0x1100'0022);                 //replace, the fragment's alpha
  CHECK(apply(4, 0x0000'00c8, 0x0000'0064) & 0xff, 255);                  //add, held
  CHECK(apply(4, 0x0000'0032, 0x0000'003c) & 0xff, 110);
}

//The tests: alpha and color against references; depth, writing it unless masked; the stencil and its operations,
//which stop at each format's ends; drawing without the stencil test keeps the stencil.
static auto drawPixelTests() -> void {
  Canvas c;
  auto dot = [&](u32 color, float z = 0) { c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, color, 1, 1, z}}); };
  c.ge.commands[GE::AlphaTestEnable] = 1;
  c.ge.commands[GE::AlphaTest] = 6 | 0x80 << 8 | 0xff << 16;  //greater than 0x80
  dot(0x8000'0011);
  CHECK(c.pixel(0, 0), 0);
  dot(0x8100'0022);
  CHECK(c.pixel(0, 0), 0x22);
  c.ge.commands[GE::AlphaTestEnable] = 0;
  c.ge.commands[GE::ColorTestEnable] = 1;
  c.ge.commands[GE::ColorTest] = 2;  //equal
  c.ge.commands[GE::ColorReference] = 0x12'3456;
  c.ge.commands[GE::ColorTestMask] = 0xff'ffff;
  dot(0xff12'3457);
  CHECK(c.pixel(0, 0), 0x22);
  dot(0xff12'3456);
  CHECK(c.pixel(0, 0), 0x12'3456);
  c.ge.commands[GE::ColorTestMask] = 0xff'fff0;
  dot(0xff12'3459);
  CHECK(c.pixel(0, 0), 0x12'3459);
  c.ge.commands[GE::ColorTestEnable] = 0;

  c.memory.write(2, DepthSeen + 0x1'0000, 0x8000);
  c.ge.commands[GE::DepthTestEnable] = 1;
  c.ge.commands[GE::DepthTest] = 4;  //less
  dot(0xff00'0001, 0x8000);
  CHECK(c.pixel(0, 0), 0x12'3459);
  dot(0xff00'0002, 0x7fff);
  CHECK(c.pixel(0, 0), 2); CHECK(c.depth(0, 0), 0x7fff);
  c.ge.commands[GE::DepthMask] = 1;
  dot(0xff00'0003, 0x1000);
  CHECK(c.pixel(0, 0), 3); CHECK(c.depth(0, 0), 0x7fff);  //drawn, depth kept
  c.ge.commands[GE::DepthTestEnable] = 0;

  c.setPixel(0, 0, 0x7700'0000);
  dot(0xff10'2030);
  CHECK(c.pixel(0, 0), 0x7710'2030);  //no stencil test: the stencil stays
  c.ge.commands[GE::StencilTestEnable] = 1;
  c.ge.commands[GE::StencilTest] = 1 | 0x05 << 8 | 0xff << 16;  //always
  c.ge.commands[GE::StencilOperation] = 3 | 0 << 8 | 4 << 16;   //fail: invert; pass: increment
  c.setPixel(0, 0, 0x0500'0000);
  dot(0xff00'0044);
  CHECK(c.pixel(0, 0), 0x0600'0044);
  c.setPixel(0, 0, 0xff00'0000);
  dot(0xff00'0044);
  CHECK(c.pixel(0, 0), 0xff00'0044);  //stops at 255
  c.ge.commands[GE::StencilTest] = 0 | 0x05 << 8 | 0xff << 16;  //never: fail, invert, and nothing drawn
  c.setPixel(0, 0, 0x0511'1111);
  dot(0xff00'0044);
  CHECK(c.pixel(0, 0), 0xfa11'1111);
  c.ge.commands[GE::StencilTest] = 2 | 0x15 << 8 | 0x0f << 16;  //equal, masked: 0x15 & 0x0f against 5
  c.ge.commands[GE::StencilOperation] = 0 | 0 << 8 | 2 << 16;   //pass: replace, with the whole reference
  c.setPixel(0, 0, 0x0500'0000);
  dot(0xff00'0044);
  CHECK(c.pixel(0, 0), 0x1500'0044);
  Canvas four(2);  //4444: the stencil in four bits, counting in sixteenths
  four.ge.commands[GE::StencilTestEnable] = 1;
  four.ge.commands[GE::StencilTest] = 1 | 0xff << 16;
  four.ge.commands[GE::StencilOperation] = 4 << 16;
  four.setPixel(0, 0, 0xe000);
  four.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, 0xff00'0000, 1, 1, 0}});
  CHECK(four.pixel(0, 0) >> 12, 0xf);
  four.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, 0xff00'0000, 1, 1, 0}});
  CHECK(four.pixel(0, 0) >> 12, 0xf);
}

//Blending: each operation, the factors (the other's color, alpha, doubled alpha, fixed), with the PSP's rounding:
//each term is (color*2+1)*(factor*2+1)/1024.
static auto drawBlending() -> void {
  Canvas c;
  auto blend = [&](u32 operation, u32 source, u32 destination, u32 color) {
    c.ge.commands[GE::AlphaBlendEnable] = 1;
    c.ge.commands[GE::BlendMode] = source | destination << 4 | operation << 8;
    c.setPixel(0, 0, 0x0064'6464);
    c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, color, 1, 1, 0}});
    return c.pixel(0, 0) & 0xff;
  };
  CHECK(blend(0, 2, 3, 0x80c8'c8c8), 150);  //200 at alpha 128 over 100: 100 + 50
  c.ge.commands[GE::BlendFixedA] = 0x80'8080;
  c.ge.commands[GE::BlendFixedB] = 0x40'4040;
  CHECK(blend(1, 10, 10, 0x00c8'c8c8), 75);  //(401*257)/1024 - (201*129)/1024
  CHECK(blend(2, 10, 10, 0x00c8'c8c8), 0);   //reversed: below zero, held
  CHECK(blend(3, 10, 10, 0x00c8'c8c8), 100);  //the smaller
  CHECK(blend(4, 10, 10, 0x00c8'c8c8), 200);  //the larger
  CHECK(blend(5, 10, 10, 0x00c8'c8c8), 100);  //the difference
  c.ge.commands[GE::BlendFixedA] = 0x01'0101;
  c.ge.commands[GE::BlendFixedB] = 0;
  CHECK(blend(0, 10, 10, 0x00fe'fefe), 1);   //254 by a factor of 1: (509*3)/1024 rounds to 1, where 254*1/255 wouldn't
  CHECK(blend(0, 6, 10, 0x80c8'c8c8), 200);  //doubled alpha: (401*513)/1024
  CHECK(blend(0, 0, 10, 0x00c8'c8c8), 78);   //the source times the destination's color: (401*201)/1024
}

//Dithering (pspsdk's matrix: -4 at (0,0), +1 at (3,0)), logic operations, and the write masks.
static auto drawDitherAndMasks() -> void {
  Canvas c;
  c.ge.commands[GE::DitherEnable] = 1;
  c.ge.commands[GE::Dither0] = 0xc | 0 << 4 | 0xd << 8 | 1 << 12;  //-4, 0, -3, 1
  c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, 0xff64'6464, 4, 1, 0}});
  CHECK(c.pixel(0, 0), 0x0060'6060); CHECK(c.pixel(1, 0), 0x0064'6464); CHECK(c.pixel(3, 0), 0x0065'6565);
  c.ge.commands[GE::DitherEnable] = 0;
  c.ge.commands[GE::LogicOpEnable] = 1;
  c.ge.commands[GE::LogicOp] = 6;  //xor
  c.setPixel(0, 0, 0x330f'0f0f);
  c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, 0xffff'00ff, 1, 1, 0}});
  CHECK(c.pixel(0, 0), 0x33f0'0ff0);  //the stencil untouched
  c.ge.commands[GE::LogicOpEnable] = 0;
  c.ge.commands[GE::MaskColor] = 0x00'00ff;  //red's bits stay
  c.setPixel(0, 0, 0x0000'0011);
  c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {0, 0, 0xffaa'bbcc, 1, 1, 0}});
  CHECK(c.pixel(0, 0), 0x00aa'bb11);
}

//Texture coordinates are stepped from a primitive's leftmost corner, by a step per pixel cut a hair short when it
//isn't exact (as the user's PSP drew 256 texels over 240 pixels). Here 16 texels over 15 pixels: pixel 7's middle
//falls exactly on texel 8's left edge, so it takes texel 7, in a sprite and in two triangles. The second triangle's
//leftmost corner is its bottom left, so going up from it, v at row 7's middle comes out just past 8 instead.
static auto drawTexelSteps() -> void {
  auto texel = [](u32 x, u32 y) { return x | y << 8 | 0x80 << 16; };
  auto ready = [&](Canvas& c) {
    c.texture(3, 16, 16, 16);
    for(u32 y = 0; y < 16; y++) {
      for(u32 x = 0; x < 16; x++) c.memory.write(4, Texture + (y * 16 + x) * 4, texel(x, y) | 0xff00'0000);
    }
  };
  Canvas sprite;
  ready(sprite);
  sprite.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 16, 0, 15, 15, 0}});
  CHECK(sprite.pixel(7, 0), texel(7, 0));
  CHECK(sprite.pixel(8, 0), texel(9, 0));
  CHECK(sprite.pixel(0, 7), texel(0, 7));
  Canvas triangles;
  ready(triangles);
  triangles.draw(GE::Triangles, {{0, 0, 0, 0, 0, 0}, {16, 0, 0, 15, 0, 0}, {0, 16, 0, 0, 15, 0},
                                 {16, 0, 0, 15, 0, 0}, {16, 16, 0, 15, 15, 0}, {0, 16, 0, 0, 15, 0}});
  CHECK(triangles.pixel(0, 7), texel(0, 7));  //the first triangle, stepped from its top left
  CHECK(triangles.pixel(7, 0), texel(7, 0));
  CHECK(triangles.pixel(7, 7), texel(7, 8));  //on the diagonal, the second's: stepped from its bottom left
}

//Triangles: the pixels whose middles are inside (as a PSP samples them), not those on right or bottom edges, so two
//triangles sharing an edge draw each pixel once; colors blended across, or with flat shading the last vertex's.
static auto drawTriangles() -> void {
  Canvas c;
  c.draw(GE::Triangles, {{0, 0, 0xff00'00ff, 0, 0, 0}, {0, 0, 0xff00'ff00, 8, 0, 0}, {0, 0, 0xffff'0000, 0, 8, 0}});
  u32 covered = 0;
  for(u32 y = 0; y < 9; y++) for(u32 x = 0; x < 9; x++) covered += c.pixel(x, y) != 0;
  CHECK(covered, 28);  //x + y up to 6: pixel (7, 0)'s middle is on the right edge
  CHECK(c.pixel(6, 0) != 0 && c.pixel(7, 0) == 0 && c.pixel(4, 4) == 0, true);
  CHECK(c.pixel(0, 0), 223 | 15 << 8 | 15 << 16);  //mostly the first vertex's red: at (0.5, 0.5), 7/8 of it
  c.memory.fill(VRAM, 0, 0x1000);
  c.ge.commands[GE::ShadeMode] = 0;
  c.draw(GE::Triangles, {{0, 0, 0xff00'00ff, 0, 0, 0}, {0, 0, 0xff00'ff00, 8, 0, 0}, {0, 0, 0xffff'0000, 0, 8, 0}});
  CHECK(c.pixel(0, 0), 0x00ff'0000); CHECK(c.pixel(3, 3), 0x00ff'0000);

  Canvas square;  //two triangles adding 1 to each pixel they draw
  square.ge.commands[GE::AlphaBlendEnable] = 1;
  square.ge.commands[GE::BlendMode] = 10 | 10 << 4;
  square.ge.commands[GE::BlendFixedA] = 0xff'ffff;
  square.ge.commands[GE::BlendFixedB] = 0xff'ffff;
  square.draw(GE::TriangleStrip, {{0, 0, 0xff01'0101, 0, 0, 0}, {0, 0, 0xff01'0101, 8, 0, 0},
                                  {0, 0, 0xff01'0101, 0, 8, 0}, {0, 0, 0xff01'0101, 8, 8, 0}});
  bool once = true;
  for(u32 y = 0; y < 8; y++) for(u32 x = 0; x < 8; x++) once &= (square.pixel(x, y) & 0xff) == 1;
  CHECK(once, true);
  CHECK(square.pixel(8, 4), 0); CHECK(square.pixel(4, 8), 0);

  //two rectangles of two triangles each, sharing an edge at x = 4 1/2, right through column 4's sample points (their
  //middles): the column belongs to the rectangle it's a left edge of, so it's drawn once
  Canvas shared;
  shared.ge.commands[GE::AlphaBlendEnable] = 1;
  shared.ge.commands[GE::BlendMode] = 10 | 10 << 4;
  shared.ge.commands[GE::BlendFixedA] = 0xff'ffff;
  shared.ge.commands[GE::BlendFixedB] = 0xff'ffff;
  for(float left : {0.0f, 4.5f}) {
    float right = left ? 8.0f : 4.5f;
    shared.draw(GE::TriangleStrip, {{0, 0, 0xff01'0101, left, 0, 0}, {0, 0, 0xff01'0101, right, 0, 0},
                                    {0, 0, 0xff01'0101, left, 4, 0}, {0, 0, 0xff01'0101, right, 4, 0}});
  }
  bool column = true;
  for(u32 y = 0; y < 4; y++) for(u32 x = 0; x < 8; x++) column &= (shared.pixel(x, y) & 0xff) == 1;
  CHECK(column, true);

  //a sliver sharing an edge with a wider triangle on its right: the edge, from (0.5, 0.5) to (2.5, 20.5), runs
  //through pixel (1, 10)'s middle, and the sliver's third corner is half a sixteenth of a pixel left of it (at
  //(1.5, 10.8125), where the edge is at x = 1.53125), so it's the sliver's right edge and the wider one's left: the
  //pixel is the wider one's, drawn once (working out where the edge is with a division cut short took the corner for
  //being on it, and drew the pixel twice)
  Canvas sliver;
  sliver.ge.commands[GE::AlphaBlendEnable] = 1;
  sliver.ge.commands[GE::BlendMode] = 10 | 10 << 4;
  sliver.ge.commands[GE::BlendFixedA] = 0xff'ffff;
  sliver.ge.commands[GE::BlendFixedB] = 0xff'ffff;
  sliver.draw(GE::Triangles, {{0, 0, 0xff01'0101, 0.5f, 0.5f, 0}, {0, 0, 0xff01'0101, 2.5f, 20.5f, 0},
                              {0, 0, 0xff01'0101, 1.5f, 10.8125f, 0}, {0, 0, 0xff01'0101, 0.5f, 0.5f, 0},
                              {0, 0, 0xff01'0101, 3.5f, 10.5f, 0}, {0, 0, 0xff01'0101, 2.5f, 20.5f, 0}});
  CHECK(sliver.pixel(1, 10) & 0xff, 1u);
  bool atMostOnce = true;
  for(u32 y = 0; y < 21; y++) for(u32 x = 0; x < 4; x++) atMostOnce &= (sliver.pixel(x, y) & 0xff) <= 1;
  CHECK(atMostOnce, true);

  Canvas dots;
  dots.draw(GE::Points, {{0, 0, 0xff12'3456, 3.5f, 2.9f, 0}});
  CHECK(dots.pixel(3, 2), 0x12'3456);
}

//Lines (draw.cpp's line()): the pictures pspautotests recorded of them on a PSP (gpu/primitives/lines, linestrip and
//indices, moved to fit the canvas), and the diamond exit rule's own cases: from a pixel's middle to another's, the
//first pixel lit and not the last; steep lines; a strip's joint lit once; lines too short to leave a diamond, and one
//just long enough; scissoring; colors, depth and texture coordinates blended along a line, and flat shading.
static auto drawLines() -> void {
  auto lit = [](Canvas& c) {
    std::set<std::pair<u32, u32>> pixels;
    for(u32 y = 0; y < 16; y++) for(u32 x = 0; x < 16; x++) if(c.pixel(x, y)) pixels.insert({x, y});
    return pixels;
  };
  using Pixels = std::set<std::pair<u32, u32>>;
  Canvas c;
  //lines: (14, 10) to (16, 10) and (24, 10) to (22, 10) lit (14, 10), (15, 10), (22, 10) and (23, 10); a line from
  //(9.375, 19.125) to (11.25, 19.125) (lines' 8-bit one in 3D, its position worked out) lit (9, 19) and (10, 19)
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 2, 3, 0}, {0, 0, 0xff00'00ff, 4, 3, 0},
                     {0, 0, 0xff00'00ff, 8, 3, 0}, {0, 0, 0xff00'00ff, 6, 3, 0},
                     {0, 0, 0xff00'00ff, 1.375f, 9.125f, 0}, {0, 0, 0xff00'00ff, 3.25f, 9.125f, 0}});
  CHECK(lit(c) == Pixels({{2, 3}, {3, 3}, {6, 3}, {7, 3}, {1, 9}, {2, 9}}), true);
  //linestrip: (14, 10), (16, 10), (16, 12) lit (14-16, 10) and (16, 11); (24, 12), (24, 10), (22, 10) lit (22-24, 10)
  //and (24, 11)
  c.memory.fill(VRAM, 0, 0x1000);
  c.draw(GE::LineStrip, {{0, 0, 0xff00'00ff, 2, 5, 0}, {0, 0, 0xff00'00ff, 4, 5, 0}, {0, 0, 0xff00'00ff, 4, 7, 0}});
  c.draw(GE::LineStrip, {{0, 0, 0xff00'00ff, 12, 7, 0}, {0, 0, 0xff00'00ff, 12, 5, 0},
                         {0, 0, 0xff00'00ff, 10, 5, 0}});
  CHECK(lit(c) == Pixels({{2, 5}, {3, 5}, {4, 5}, {4, 6}, {10, 5}, {11, 5}, {12, 5}, {12, 6}}), true);
  //indices: a square's four sides, each drawn from either corner, leave its bottom right corner unlit
  c.memory.fill(VRAM, 0, 0x1000);
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 1, 1, 0}, {0, 0, 0xff00'00ff, 5, 1, 0},
                     {0, 0, 0xff00'00ff, 5, 5, 0}, {0, 0, 0xff00'00ff, 1, 5, 0},
                     {0, 0, 0xff00'00ff, 5, 1, 0}, {0, 0, 0xff00'00ff, 5, 5, 0},
                     {0, 0, 0xff00'00ff, 1, 1, 0}, {0, 0, 0xff00'00ff, 1, 5, 0}});
  Pixels square;
  for(u32 n = 1; n <= 4; n++) {
    square.insert({n, 1}), square.insert({n, 5}), square.insert({1, n}), square.insert({5, n});
  }
  square.insert({5, 1});
  CHECK(lit(c) == square, true);

  //from a pixel's middle to another's: the first lit, the last not, whichever way round; steep likewise. Crossing a
  //column's middle on the boundary between two rows (at y 1, from (0.5, 0.5) to (4.5, 2.5)), it lights the lower
  //row; crossing a row's middle between two columns, the right one
  c.memory.fill(VRAM, 0, 0x1000);
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 0.5f, 0.5f, 0}, {0, 0, 0xff00'00ff, 4.5f, 2.5f, 0},
                     {0, 0, 0xff00'00ff, 14.5f, 2.5f, 0}, {0, 0, 0xff00'00ff, 10.5f, 0.5f, 0},
                     {0, 0, 0xff00'00ff, 0.5f, 4.5f, 0}, {0, 0, 0xff00'00ff, 2.5f, 8.5f, 0}});
  CHECK(lit(c) == Pixels({{0, 0}, {1, 1}, {2, 1}, {3, 2}, {14, 2}, {13, 2}, {12, 1}, {11, 1},
                          {0, 4}, {1, 5}, {1, 6}, {2, 7}}), true);
  //a strip's joint at a pixel's middle is drawn once (each draw adds 1)
  Canvas sum;
  sum.ge.commands[GE::AlphaBlendEnable] = 1;
  sum.ge.commands[GE::BlendMode] = 10 | 10 << 4;
  sum.ge.commands[GE::BlendFixedA] = 0xff'ffff;
  sum.ge.commands[GE::BlendFixedB] = 0xff'ffff;
  sum.draw(GE::LineStrip, {{0, 0, 0xff01'0101, 0.5f, 3.5f, 0}, {0, 0, 0xff01'0101, 6.5f, 3.5f, 0},
                           {0, 0, 0xff01'0101, 6.5f, 9.5f, 0}, {0, 0, 0xff01'0101, 1.5f, 4.5f, 0}});
  bool once = true;
  for(u32 y = 0; y < 16; y++) for(u32 x = 0; x < 16; x++) once &= (sum.pixel(x, y) & 0xff) <= 1;
  CHECK(once, true);
  CHECK(sum.pixel(6, 3) & 0xff, 1u);
  CHECK(sum.pixel(6, 9) & 0xff, 1u);
  //too short to leave a diamond (both ends in pixel (2, 2)'s, or neither in any), and just long enough to leave one
  c.memory.fill(VRAM, 0, 0x1000);
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 2.25f, 2.5f, 0}, {0, 0, 0xff00'00ff, 2.75f, 2.5f, 0},
                     {0, 0, 0xff00'00ff, 6.0f, 6.1875f, 0}, {0, 0, 0xff00'00ff, 6.0625f, 6.1875f, 0},
                     {0, 0, 0xff00'00ff, 9.5f, 9.5f, 0}, {0, 0, 0xff00'00ff, 10.0625f, 9.5f, 0}});
  CHECK(lit(c) == Pixels({{9, 9}}), true);
  //scissored: only the pixels inside
  c.memory.fill(VRAM, 0, 0x1000);
  c.ge.commands[GE::Scissor1] = 3 | 1 << 10;
  c.ge.commands[GE::Scissor2] = 5 | 1 << 10;
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 0, 1, 0}, {0, 0, 0xff00'00ff, 9, 1, 0}});
  CHECK(lit(c) == Pixels({{3, 1}, {4, 1}, {5, 1}}), true);
  c.ge.commands[GE::Scissor1] = 0;
  c.ge.commands[GE::Scissor2] = 1023 << 10 | 1023;

  //blended along the line as at each pixel's middle column: red 255 to 0 over 8 pixels (128 sixteenths), pixel c's
  //middle 16c + 8 along: (255 * (120 - 16c)) / 128, rounded down; depth likewise, from 1000 to 2000
  c.memory.fill(VRAM, 0, 0x20000);
  c.ge.commands[GE::DepthTestEnable] = 1;
  c.ge.commands[GE::DepthTest] = 1;
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 0, 0.5f, 1000}, {0, 0, 0xffff'0000, 8, 0.5f, 2000}});
  for(u32 x = 0; x < 8; x++) {
    u32 red = 255 * (120 - 16 * x) / 128, blue = 255 * (16 * x + 8) / 128;
    CHECK(c.pixel(x, 0), red | blue << 16);
    CHECK(c.depth(x, 0), (1000 * (120 - 16 * x) + 2000 * (16 * x + 8)) / 128);
  }
  c.ge.commands[GE::DepthTestEnable] = 0;
  c.ge.commands[GE::ShadeMode] = 0;  //flat: the second vertex's color
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 0, 1.5f, 0}, {0, 0, 0xffff'0000, 8, 1.5f, 0}});
  CHECK(c.pixel(0, 1), 0x00ff'0000u);
  CHECK(c.pixel(7, 1), 0x00ff'0000u);
  c.ge.commands[GE::ShadeMode] = 1;
  //textured: u from 0 to 8 over 8 pixels takes texel c at pixel c (u at its middle, c + 0.5), v 3 the row
  c.texture(3, 16, 16, 16);
  for(u32 n = 0; n < 256; n++) c.memory.write(4, Texture + n * 4, n % 16 | n / 16 << 8 | 0xff00'0000);
  c.draw(GE::Lines, {{0, 3.5f, 0, 0, 2.5f, 0}, {8, 3.5f, 0, 8, 2.5f, 0}});
  for(u32 x = 0; x < 8; x++) CHECK(c.pixel(x, 2), x | 3 << 8);
}

//Shapes whose corners are all at one depth draw every pixel at that depth: Chili Con Carnage's logo, a fan at depth
//65535 drawn with the depth test "at least as near" (7) over a background at 65535, at each of the sizes its frames
//draw it, lost stripes of pixels to depths blended a hair under 65535 and cut to 65534. Each fan, and a line at that
//depth, draws every pixel it draws with the test off, each pixel's depth 65535; with four pixels at a time and one.
static auto drawOneDepth() -> void {
  struct Fan { float left, top, right, bottom; };
  for(bool fours : {true, false}) {
    //what a shape draws (with the depth test at test), and whether each pixel it drew has depth 65535
    auto drawn = [&](u32 test, const std::function<void (Canvas&)>& draw, bool& deepest) {
      Canvas c;
      c.ge.fourPixels = fours;
      c.ge.commands[GE::FrameBufferWidth] = 512;
      c.ge.commands[GE::DepthBufferPointer] = 0x10'0000;
      c.ge.commands[GE::DepthBufferWidth] = 512;
      c.ge.commands[GE::DepthTestEnable] = 1;
      c.ge.commands[GE::DepthTest] = test;
      c.memory.fill(VRAM, 0, 512 * 272 * 4);
      c.memory.fill(VRAM + 0x10'0000, 0xff, 512 * 272 * 2);  //the background, at 65535
      draw(c);
      u32 count = 0;
      deepest = true;
      for(u32 y = 0; y < 272; y++) {
        for(u32 x = 0; x < 512; x++) {
          if(!c.memory.read(4, VRAM + (y * 512 + x) * 4)) continue;
          count++;
          deepest &= c.memory.read(2, DepthSeen + 0x10'0000 + (y * 512 + x) * 2) == 0xffff;
        }
      }
      return count;
    };
    for(auto fan : {Fan{-28, -2, 162, 92}, Fan{-29, -3, 163, 93}, Fan{-31, -4, 165, 94}}) {
      auto draw = [&](Canvas& c) {
        c.draw(GE::TriangleFan, {{0, 0, 0xff00'00ff, fan.left, fan.top, 65535},
                                 {0, 0, 0xff00'00ff, fan.right, fan.top, 65535},
                                 {0, 0, 0xff00'00ff, fan.right, fan.bottom, 65535},
                                 {0, 0, 0xff00'00ff, fan.left, fan.bottom, 65535}});
      };
      bool deepest = false, any = false;
      u32 all = drawn(1, draw, any);
      CHECK(all > 0, true);
      CHECK(drawn(7, draw, deepest), all);
      CHECK(deepest, true);
    }
    auto line = [](Canvas& c) {
      c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 0.5f, 100.5f, 65535}, {0, 0, 0xff00'00ff, 470.5f, 140.5f, 65535}});
    };
    bool deepest = false;
    CHECK(drawn(7, line, deepest), 470);
    CHECK(deepest, true);
  }
}

//DXT textures (texture.cpp's dxtTexel()) as pspautotests' gpu/texcolors/dxt1, dxt3 and dxt5 recorded them on a PSP:
//each case a block's first texel, its color as the program read it back (red in the low byte) and its alpha, from
//its colors, its first index and its alphas; then the blocks' order in a texture, and a DXT texture kept decoded
//against one read from memory, and seen again after its memory changes.
static auto drawDXT() -> void {
  struct Case { u32 format, first, second, index, alpha, rgb, a; };
  //alpha: DXT3's first texel's 4 bits; DXT5's alphas (bits 0-15) and the first texel's index (bits 16-18)
  const Case cases[] = {
    {8, 0xffff, 0xffff, 0, 0, 0xf8fcf8, 255}, {8, 0xffff, 0xffff, 1, 0, 0xf8fcf8, 255},
    {8, 0xffff, 0xffff, 2, 0, 0xf8fcf8, 255}, {8, 0xffff, 0xffff, 3, 0, 0x000000, 0},
    {8, 0xffe3, 0x8410, 0, 0, 0x18fcf8, 255}, {8, 0xffe3, 0x8410, 1, 0, 0x808080, 255},
    {8, 0xffe3, 0x8410, 2, 0, 0x3ad2d0, 255}, {8, 0xffe3, 0x8410, 3, 0, 0x5da9a8, 255},
    {8, 0x8410, 0xf85f, 0, 0, 0x808080, 255}, {8, 0x8410, 0xf85f, 1, 0, 0xf808f8, 255},
    {8, 0x8410, 0xf85f, 2, 0, 0xbc44bc, 255}, {8, 0x8410, 0xf85f, 3, 0, 0x000000, 0},
    {8, 0x7890, 0x1234, 2, 0, 0x8a2155, 255}, {8, 0x7890, 0x1234, 3, 0, 0x953232, 255},
    {8, 0x7777, 0x1356, 2, 0, 0xb5c050, 255},
    {9, 0xffff, 0xffff, 3, 5, 0x000000, 0x50}, {9, 0x7890, 0x1234, 2, 0, 0x8a2155, 0},
    {9, 0x7890, 0x1234, 3, 15, 0x953232, 0xf0}, {9, 0x8410, 0xf85f, 1, 5, 0xf808f8, 0x50},
    {10, 0x7777, 0x1356, 2, 0x59e1 | 0 << 16, 0xb5c050, 0xe1},
    {10, 0x7777, 0x1356, 2, 0x59e1 | 1 << 16, 0xb5c050, 0x59},
    {10, 0x7777, 0x1356, 2, 0x59e1 | 2 << 16, 0xb5c050, 0xcd},
    {10, 0x7777, 0x1356, 2, 0x59e1 | 3 << 16, 0xb5c050, 0xba},
    {10, 0x7777, 0x1356, 2, 0x59e1 | 4 << 16, 0xb5c050, 0xa6},
    {10, 0x7777, 0x1356, 2, 0x59e1 | 5 << 16, 0xb5c050, 0x93},
    {10, 0x7777, 0x1356, 2, 0x59e1 | 6 << 16, 0xb5c050, 0x7f},
    {10, 0x7777, 0x1356, 2, 0x59e1 | 7 << 16, 0xb5c050, 0x6c},
    {10, 0xffff, 0xffff, 0, 0xff55 | 2 << 16, 0xf8fcf8, 0x77}, {10, 0xffff, 0xffff, 0, 0xff55 | 6 << 16, 0xf8fcf8, 0},
    {10, 0xffff, 0xffff, 0, 0xff55 | 7 << 16, 0xf8fcf8, 0xff},
    {10, 0x7890, 0x1234, 2, 0x00ff | 2 << 16, 0x8a2155, 0xda},
    {10, 0x7890, 0x1234, 2, 0x00ff | 7 << 16, 0x8a2155, 0x24},
    {10, 0x7890, 0x1234, 2, 0xff00 | 5 << 16, 0x8a2155, 0xcc},
    {10, 0x7890, 0x1234, 2, 0xfbff | 7 << 16, 0x8a2155, 0xfb},
    {10, 0x7890, 0x1234, 2, 0xfcff | 2 << 16, 0x8a2155, 0xfe},
    {10, 0x7890, 0x1234, 2, 0xfcff | 4 << 16, 0x8a2155, 0xfd}, {10, 0x7890, 0x1234, 2, 0xffff | 6 << 16, 0x8a2155, 0},
  };
  Canvas c;
  for(auto& k : cases) {
    c.texture(k.format, 8, 8, 8);
    u32 block = k.format == 8 ? 8 : 16;
    for(u32 n = 0; n < 4; n++) {  //four blocks alike, as the programs have them
      u32 at = Texture + n * block;
      c.memory.write(4, at, k.index);  //the first texel's index, every other's 0
      c.memory.write(2, at + 4, k.first), c.memory.write(2, at + 6, k.second);
      if(k.format == 9) for(u32 row = 0; row < 4; row++) c.memory.write(2, at + 8 + row * 2, row ? 0 : k.alpha);
      if(k.format == 10) {
        c.memory.write(4, at + 8, k.alpha >> 16), c.memory.write(2, at + 12, 0);
        c.memory.write(1, at + 14, k.alpha & 0xff), c.memory.write(1, at + 15, k.alpha >> 8 & 0xff);
      }
    }
    c.ge.dropTextures();
    u32 texel = c.ge.texel(c.ge.sampler(), 0, 0);
    CHECK(texel, k.rgb | k.a << 24);
  }

  //the blocks' order: a row of blocks after another, TEXTURE_BUFFER_WIDTH0 / 4 blocks to a row (here 8 texels of a
  //16-wide buffer); block n's first color is n, everything else index 0
  c.texture(8, 8, 8, 16);
  for(u32 n = 0; n < 8; n++) {
    c.memory.write(4, Texture + n * 8, 0);
    c.memory.write(2, Texture + n * 8 + 4, n << 11), c.memory.write(2, Texture + n * 8 + 6, 0);
  }
  for(u32 v : {0u, 5u}) {
    for(u32 u : {1u, 6u}) CHECK(c.ge.texel(c.ge.sampler(), u, v) & 0xff, (v / 4 * 4 + u / 4) << 3);
  }

  //drawn kept decoded and drawn read from memory: the same; and after a block changes
  std::vector<u32> drawn[2];
  for(bool decoding : {false, true}) {
    Canvas d;
    if(!decoding) d.memory.watching = nullptr;
    for(u32 format : {8u, 9u, 10u}) {
      d.texture(format, 16, 16, 16);
      d.ge.commands[GE::TextureFilter] = 1 | 1 << 8;
      for(u32 n = 0; n < 16 * 16 * 2; n++) d.memory.write(2, Texture + n * 2, n * 0x9e37 ^ n >> 3);
      d.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 16, 0, 16, 16, 0}});
      for(u32 n = 0; n < 256; n++) drawn[decoding].push_back(d.pixel(n % 16, n / 16));
      d.memory.write(4, Texture + 40, 0x1234'5678);
      d.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 16, 0, 16, 16, 0}});
      for(u32 n = 0; n < 256; n++) drawn[decoding].push_back(d.pixel(n % 16, n / 16));
    }
    if(decoding) CHECK(d.ge.textures.entries.empty(), false);
  }
  CHECK(drawn[1] == drawn[0], true);
}

//Vertices: 8-bit positions in through mode read as 0 (pspautotests' gpu/primitives/points recorded a PSP drawing
//such points at (0, 0), and its triangles, lines and sprites nothing); 32-bit indices (format 3), of which the GE
//takes the low 16 bits, the index address moving on 4 bytes for each (gpu/primitives/indices and indices32).
static auto drawVertexFormats() -> void {
  Canvas c;
  c.ge.commands[GE::VertexType] = 0x80'009c;  //8888 color, 8-bit position, through mode
  u32 at = VertexData;
  for(u32 n = 0; n < 2; n++) {
    c.memory.write(4, at, 0xff00'ff00), c.memory.write(1, at + 4, 10 + 2 * n), c.memory.write(1, at + 5, 10);
    c.memory.write(1, at + 6, 0), at += 8;
  }
  c.ge.vertexAddress = VertexData;
  c.ge.primitive(GE::Points, 2);
  CHECK(c.pixel(0, 0), 0x00'ff00u);
  CHECK(c.pixel(10, 10), 0u);
  CHECK(c.ge.vertexAddress, VertexData + 16);

  Canvas d;
  d.ge.commands[GE::VertexType] = 0x80'019c | 3 << 11;  //8888 color, float position, 32-bit indices, through mode
  constexpr u32 Indices = VertexData + 0x1000;
  for(u32 n = 0; n < 3; n++) {
    float corner[3][2] = {{1, 1}, {9, 1}, {1, 9}};
    d.memory.write(4, VertexData + n * 16, 0xff00'00ff);
    d.memory.write(4, VertexData + n * 16 + 4, Canvas::bits(corner[n][0]));
    d.memory.write(4, VertexData + n * 16 + 8, Canvas::bits(corner[n][1]));
    d.memory.write(4, VertexData + n * 16 + 12, 0);
    d.memory.write(4, Indices + n * 4, 0xffff'0000 + n);  //0, 1, 2 once their top halves go
  }
  d.ge.vertexAddress = VertexData;
  d.ge.indexAddress = Indices;
  d.ge.primitive(GE::Triangles, 3);
  CHECK(d.pixel(2, 2), 0x00'00ffu);
  CHECK(d.ge.indexAddress, Indices + 12);
}

//A vertex without a color takes the material's ambient color; a texture shrunk onto the screen is filtered as
//TEXTURE_FILTER's shrinking half says, one enlarged as its other half says.
static auto drawAmbientAndFilters() -> void {
  Canvas c;
  c.ge.commands[GE::VertexType] = 0x80'0183;  //float texture coordinates and position, no color
  c.ge.commands[GE::AmbientColor] = 0x12'3456;
  c.ge.commands[GE::AmbientAlpha] = 0xff;
  auto corner = [&](u32 n, float u, float v, float x, float y) {
    u32 at = VertexData + n * 20;
    for(float value : {u, v, x, y, 0.0f}) c.memory.write(4, at, Canvas::bits(value)), at += 4;
  };
  corner(0, 0, 0, 0, 0); corner(1, 0, 0, 2, 2);
  c.ge.vertexAddress = VertexData;
  c.ge.primitive(GE::Sprites, 2);
  CHECK(c.pixel(1, 1), 0x12'3456);

  c.texture(3, 2, 2, 4);
  c.memory.write(4, Texture, 0); c.memory.write(4, Texture + 4, 160);
  c.ge.commands[GE::TextureFilter] = 0 | 1 << 8;  //shrinking: nearest; enlarging: filtered
  corner(0, 0, 0, 0, 0); corner(1, 2, 1, 1, 1);    //two texels across one pixel: shrunk
  c.ge.vertexAddress = VertexData;
  c.ge.primitive(GE::Sprites, 2);
  CHECK(c.pixel(0, 0) & 0xff, 160);  //the nearest to u = 1, unblended
  corner(0, 0, 0, 0, 0); corner(1, 2, 1, 4, 1);    //two texels across four pixels: enlarged
  c.ge.vertexAddress = VertexData;
  c.ge.primitive(GE::Sprites, 2);
  CHECK(c.pixel(1, 0) & 0xff, 40);   //u = 0.75: a quarter of the way to the next texel
}

//The picture pspsdk's samples "blit" and "doublelist" draw: a 4444 texture whose texel (x, y) is x*y, copied 1:1.
static auto blitPicture(Memory& memory, u32 buffer, u32 firstRow) -> bool {
  bool same = true;
  for(u32 y = firstRow; y < 272; y += 3) {
    for(u32 x = 0; x < 480; x += 3) {
      u32 t = x * y & 0xffff;
      u32 expected = (t & 15) * 17 | (t >> 4 & 15) * 17 << 8 | (t >> 8 & 15) * 17 << 16;
      same &= memory.read(4, VRAM + buffer + (y * 512 + x) * 4) == expected;
    }
  }
  return same;
}

//Runs a sample that never waits until the screen has swapped twice more (both buffers drawn again).
static auto swapTwice(KernelMachine& m) -> bool {
  u32 swaps = 0, shown = m.kernel.display.frameBuffer;
  for(u32 slice = 0; slice < 2000 && swaps < 2; slice++) {
    m.kernel.run(100'000);
    if(m.kernel.display.frameBuffer != shown) swaps++, shown = m.kernel.display.frameBuffer;
  }
  return swaps == 2;
}

//pspsdk's "blit": a texture drawn as one sprite; with circle pressed, from its swizzled copy; with cross, as strips
//32 pixels wide.
static auto blitSample() -> void {
  auto program = testProgram("blit.elf");
  if(program.empty()) return;
  KernelMachine m;
  m.system.recompiler.enabled = true;
  std::string error;
  CHECK(m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/BLIT/EBOOT.PBP", error), true);
  for(u32 buttons : {0u, 0x2000u, 0x6000u}) {  //nothing, circle (swizzled), circle and cross (strips)
    m.kernel.controller.buttons = buttons;
    CHECK(swapTwice(m), true);
    CHECK(swapTwice(m), true);
    for(u32 buffer : {0x0u, 0x8'8000u}) CHECK(blitPicture(m.system.memory, buffer, 8), true);
  }
  for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
}

//pspsdk's "doublelist": the same picture, from display lists sent while the other buffer shows.
static auto doublelistSample() -> void {
  auto program = testProgram("doublelist.elf");
  if(program.empty()) return;
  KernelMachine m;
  m.system.recompiler.enabled = true;
  std::string error;
  CHECK(m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/DOUBLE/EBOOT.PBP", error), true);
  CHECK(swapTwice(m), true);
  CHECK(swapTwice(m), true);
  for(u32 buffer : {0x0u, 0x8'8000u}) CHECK(blitPicture(m.system.memory, buffer, 32), true);  //below its four lines of text
  for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
}

//A pspsdk sample run for about a second of the PSP's time: it must still be running, and have drawn more than its
//clear color (many shades of it); looksRight(pixels) looks closer. With PSP_PICTURES set to a folder, the pictures a frame
//before, at and a frame after the second are saved there (name-0.ppm and on) for
//tools/psp-test-programs/compare-ppsspp.sh to compare with PPSSPP's software renderer after its second (whose
//start-up takes a different time, so the frames may be one apart).
static auto sampleAfterASecond(const std::string& name, std::function<bool (const std::vector<u32>&)> looksRight = {})
  -> void {
  auto program = testProgram((name + ".elf").c_str());
  if(program.empty()) return;
  KernelMachine m;
  m.system.recompiler.enabled = true;
  std::string error;
  CHECK(m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/SAMPLE/EBOOT.PBP", error), true);
  m.kernel.run(Kernel::CPUFrequency - Kernel::VblankCycles);
  for(u32 frame = 0; frame < 3; frame++) {
    if(frame) m.kernel.run(Kernel::VblankCycles);
    CHECK(m.kernel.exited, false);
    std::vector<u32> pixels;
    m.kernel.picture(pixels);
    std::set<u32> colors(pixels.begin(), pixels.end());
    CHECK(colors.size() > 64, true);
    if(looksRight) CHECK(looksRight(pixels), true);
    if(const char* folder = std::getenv("PSP_PICTURES")) {
      std::ofstream file(std::string(folder) + "/" + name + "-" + std::to_string(frame) + ".ppm", std::ios::binary);
      file << "P6\n480 272\n255\n";
      for(u32 pixel : pixels) file.put(char(pixel & 0xff)).put(char(pixel >> 8 & 0xff)).put(char(pixel >> 16 & 0xff));
    }
  }
  for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
}

//pspsdk's "clut" and "blend": a palette texture (the palette turned a step each frame) drawn over the screen,
//filtered; "blend" blends it as its first mode says.
static auto clutSample() -> void { sampleAfterASecond("clut"); }
static auto blendSample() -> void { sampleAfterASecond("blend"); }

//pspsdk's "cube": a textured cube turning in 3D, in perspective, its back faces culled and the rest depth-tested,
//over a clear color (0x554433). The cube covers the screen's middle, and not its corners.
static auto cubeSample() -> void {
  sampleAfterASecond("cube", [](const std::vector<u32>& pixels) {
    u32 background = 0xff55'4433;
    return pixels[136 * 480 + 240] != background && pixels[0] == background && pixels[271 * 480 + 479] == background;
  });
}

//pspsdk's "celshading" and "envmap": a turning torus whose texture comes from environment mapping (texture
//coordinates from lights' directions and its normals); "envmap" lights it too. The torus covers part of the screen,
//not its bottom corners. ("envmap" prints help over the top left of the first frame buffer, so every other frame
//shows it.)
static auto torusSample(const std::string& name, u32 background) -> void {
  sampleAfterASecond(name, [background](const std::vector<u32>& pixels) {
    u32 covered = 0;
    for(u32 pixel : pixels) covered += pixel != background;
    return pixels[271 * 480] == background && pixels[271 * 480 + 479] == background && covered > 10'000;
  });
}
static auto celshadingSample() -> void { torusSample("celshading", 0xffff'ffff); }
static auto envmapSample() -> void { torusSample("envmap", 0xff55'4433); }

//Textures kept decoded (texture.cpp): drawn again after its memory changed, a texture shows the change, whoever made
//it (the CPU's stores, compiled and interpreted, after the recompiler started afresh; a write, copy or fill from
//outside the CPU; the GE drawing into it; a block transfer), and the palette it's drawn with; loading a state drops
//what was kept; and a primitive drawing over its own texture reads it as it draws.
static auto drawDecodedTextures() -> void {
  constexpr u32 Code = 0x0890'0000;
  for(bool recompile : {false, true}) {
    Canvas c;
    c.texture(3, 4, 4, 4);
    for(u32 n = 0; n < 16; n++) c.memory.write(4, Texture + n * 4, 0x0010'2030 + n);
    auto drawn = [&](u32 n) {  //the texture drawn 1:1 at the top left: its texel n as it lands (alpha: the stencil)
      c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {4, 4, 0, 4, 4, 0}});
      return c.pixel(n % 4, n / 4);
    };
    CHECK(drawn(5), 0x0010'2035u);
    CHECK(c.ge.textures.entries.size(), 1u);  //kept decoded, as the rest of this test needs
    c.s.runProgram(Code, {lui(t0, Texture >> 16), ori(t0, t0, 20), lui(t1, 0x55), ori(t1, t1, 0x6677), sw(t1, 0, t0)},
                   recompile);
    CHECK(drawn(5), 0x0055'6677u);
    //compiled code that ran before the texture was decoded runs again (no fresh start between): its store is seen
    std::vector<u32> increment = {lui(t0, Texture >> 16), ori(t0, t0, 44), lw(t1, 0, t0), addiu(t1, t1, 1),
                                  sw(t1, 0, t0)};
    c.s.runProgram(Code + 0x100, increment, recompile);
    CHECK(drawn(11), 0x0010'203cu);
    c.s.ipu.pc = Code + 0x100, c.s.ipu.pd = Code + 0x104, c.s.scc.halted = 0;
    c.s.run(1000);
    CHECK(drawn(11), 0x0010'203du);
    c.memory.write(4, Texture + 24, 0x00aa'bbcc);
    CHECK(drawn(6), 0x00aa'bbccu);
    u32 word = 0x0012'3456;
    c.memory.copyIn(Texture + 28, &word, 4);
    CHECK(drawn(7), 0x0012'3456u);
    c.memory.fill(Texture + 32, 0x11, 3);
    CHECK(drawn(8), 0x0011'1111u);
    CHECK(drawn(9), 0x0010'2039u);

    //the GE drawing into a texture in VRAM, and copying into it
    c.ge.commands[GE::TextureAddress0] = 0x4000;
    c.ge.commands[GE::TextureBufferWidth0] = 0x04 << 16 | 4;
    for(u32 n = 0; n < 16; n++) c.memory.write(4, VRAM + 0x4000 + n * 4, 0x0020'0000 + n);
    CHECK(drawn(10), 0x0020'000au);
    c.ge.commands[GE::FrameBufferPointer] = 0x4000;
    c.ge.commands[GE::FrameBufferWidth] = 4;
    c.ge.commands[GE::TextureMappingEnable] = 0;
    c.draw(GE::Sprites, {{0, 0, 0, 2, 2, 0}, {0, 0, 0x0040'5060, 4, 3, 0}});  //texels 10 and 11
    c.ge.commands[GE::FrameBufferPointer] = 0;
    c.ge.commands[GE::FrameBufferWidth] = 16;
    c.ge.commands[GE::TextureMappingEnable] = 1;
    CHECK(drawn(10), 0x0040'5060u);
    CHECK(drawn(9), 0x0020'0009u);
    c.memory.write(4, Texture + 64, 0x0077'0011);
    c.ge.commands[GE::TransferSource] = (Texture + 64) & 0xff'ffff;
    c.ge.commands[GE::TransferSourceWidth] = (Texture >> 24) << 16 | 8;
    c.ge.commands[GE::TransferDestination] = 0x4000 + 12 * 4;
    c.ge.commands[GE::TransferDestinationWidth] = 0x04 << 16 | 8;
    c.ge.commands[GE::TransferSourcePosition] = 0;
    c.ge.commands[GE::TransferDestinationPosition] = 0;
    c.ge.commands[GE::TransferSize] = 0;  //one pixel
    c.ge.commands[GE::TransferStart] = 1;
    c.ge.transfer();
    CHECK(drawn(12), 0x0077'0011u);
    CHECK(drawn(13), 0x0020'000du);

    //the palette: another, then the first again (found again by its hash, checked byte for byte)
    c.texture(5, 4, 4, 16);
    for(u32 n = 0; n < 16; n++) c.memory.write(1, Texture + n / 4 * 16 + n % 4, n);
    c.ge.commands[GE::ClutAddress] = Palette & 0xff'ffff;
    c.ge.commands[GE::ClutAddressUpper] = (Palette >> 8) & 0xf'0000;
    c.ge.commands[GE::ClutFormat] = 3 | 0xff << 8;
    c.ge.commands[GE::ClutLoad] = 2;  //16 entries of 4 bytes
    auto palette = [&](u32 base) {
      for(u32 n = 0; n < 16; n++) c.memory.write(4, Palette + n * 4, base + n);
      c.ge.loadClut();
    };
    palette(0x0030'0000);
    CHECK(drawn(3), 0x0030'0003u);
    palette(0x0031'0000);
    CHECK(drawn(3), 0x0031'0003u);
    palette(0x0030'0000);
    CHECK(drawn(4), 0x0030'0004u);
    CHECK(c.ge.textures.entries.size(), 3u);  //the 8888 one in VRAM, and the indices with each palette

    //a state loaded: the memory as it was then, and nothing kept from after
    serializer saved;
    c.memory.serialize(saved);
    c.ge.serialize(saved);
    c.memory.write(1, Texture + 1, 9);
    CHECK(drawn(1), 0x0030'0009u);
    serializer loading{saved.data(), saved.size()};
    c.memory.serialize(loading);
    c.ge.serialize(loading);
    CHECK(drawn(1), 0x0030'0001u);
  }

  //A texture of 512 rows whose picture is 16 (as a frame buffer of 272 rows is drawn with as one of 512): drawn into
  //its rows 400 on, a sprite taking texels from its first 16 keeps it decoded (only the rows it reaches count);
  //drawn into its row 8 on, it's read from memory as the sprite draws, rows it has drawn among them.
  {
    Canvas tall;
    tall.texture(3, 16, 512, 16);
    tall.ge.commands[GE::TextureAddress0] = 0x4'0000;
    tall.ge.commands[GE::TextureBufferWidth0] = 0x04 << 16 | 16;
    for(u32 n = 0; n < 16 * 16; n++) tall.memory.write(4, VRAM + 0x4'0000 + n * 4, 0x0050'0000 + n);
    tall.ge.commands[GE::FrameBufferPointer] = 0x4'0000 + 400 * 64;
    tall.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 16, 0, 16, 16, 0}});
    CHECK(tall.ge.textures.entries.size(), 1u);
    CHECK(tall.memory.read(4, VRAM + 0x4'0000 + (400 + 5) * 64 + 9 * 4), 0x0050'0000u + 5 * 16 + 9);
    tall.ge.dropTextures();
    tall.ge.commands[GE::FrameBufferPointer] = 0x4'0000 + 8 * 64;
    tall.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 16, 0, 16, 8, 0}});
    CHECK(tall.ge.textures.entries.size(), 0u);
    //(its row 4 takes texels from row 9, its own row 1, which took them from row 3)
    CHECK(tall.memory.read(4, VRAM + 0x4'0000 + (8 + 4) * 64 + 3 * 4), 0x0050'0000u + 3 * 16 + 3);
  }

  //drawing over its own texture (the frame buffer): row 2 takes row 1 as this sprite has just drawn it
  Canvas c;
  for(u32 x = 0; x < 4; x++) c.setPixel(x, 0, 0x0001'0101 * (x + 1)), c.setPixel(x, 1, 0x0012'3456);
  c.texture(3, 16, 16, 16);
  c.ge.commands[GE::TextureAddress0] = 0;
  c.ge.commands[GE::TextureBufferWidth0] = 0x04 << 16 | 16;
  c.draw(GE::Sprites, {{0, 0, 0, 0, 1, 0}, {4, 2, 0, 4, 3, 0}});
  for(u32 x = 0; x < 4; x++) CHECK(c.pixel(x, 1), 0x0001'0101u * (x + 1));
  for(u32 x = 0; x < 4; x++) CHECK(c.pixel(x, 2), 0x0001'0101u * (x + 1));
}

//Sprites drawing over their own texture (the frame buffer, 64 pixels wide, as a texture of 256 by 256) that never
//take a texel they have drawn by then, where its weight isn't 0, draw from a copy taken first (draw.cpp's
//readsAhead()), and come out as reading the texture from memory as they draw does (a machine without watching(), which
//keeps nothing decoded), in each frame buffer format: the picture brightened in place, filtered (some second texels,
//whose weights are 0, drawn already: the first sprite's first column, a row down, past the frame buffer's width);
//halved in place; shrunk a little from its own top left; from a texture starting two rows and 16 pixels before the
//frame buffer, a row down and 16 pixels right; through VRAM's third copy; and of palette indices. Those taking a texel
//they have drawn aren't copied, and come out the same too: a pixel up, or left; from the texture before the frame
//buffer, a row up; a filtered second texel, a quarter of the weight, repeated round to its row's first; a second
//sprite taking the first's pixels.
static auto drawCopiedTexture() -> void {
  for(u32 format : {0u, 1u, 2u, 3u}) {
    u32 bytes = format == 3 ? 4 : 2;
    Canvas copied{format}, read{format};
    read.memory.watching = nullptr;
    std::mt19937 random{format + 1};
    std::vector<u8> picture(64 * 1024), palette(1024);
    for(auto& byte : picture) byte = random();
    for(auto& byte : palette) byte = random();
    for(Canvas* c : {&copied, &read}) {
      c->memory.copyIn(Palette, palette.data(), palette.size());
      c->ge.commands[GE::ClutAddress] = Palette & 0xff'ffff;
      c->ge.commands[GE::ClutAddressUpper] = (Palette >> 8) & 0xf'0000;
      c->ge.commands[GE::ClutFormat] = 3 | 0xff << 8;
      c->ge.commands[GE::ClutLoad] = 32;  //(all 1 KiB)
      c->ge.loadClut();
    }
    auto strips = [](u32 count, float width, float height, float across, float down) {
      std::vector<V> sprites;
      for(u32 n = 0; n < count; n++) {
        sprites.push_back({n * across, 0, 0xff8e'8e8e, n * width, 0, 0});
        sprites.push_back({(n + 1) * across, down, 0xff8e'8e8e, (n + 1) * width, height, 0});
      }
      return sprites;
    };
    auto one = [](float x, float y, float u, float v, float width = 32) {  //a sprite 32 high, from (x, y) at (u, v)
      return std::vector<V>{{u, v, 0xffff'ffff, x, y, 0}, {u + width, v + 32, 0xffff'ffff, x + width, y + 32, 0}};
    };
    auto both = [](std::vector<V> first, const std::vector<V>& second) {
      first.insert(first.end(), second.begin(), second.end());
      return first;
    };
    u32 before = 0x8000 - (2 * 64 + 16) * bytes;  //(two rows and 16 pixels before a frame buffer at 0x8000)
    struct Case {
      std::vector<V> sprites;
      u32 filter;
      bool ahead;
      u32 frameBuffer = 0, texture = 0, size = 8, kind = 0;  //(the texture's address, sides' bits and format)
    };
    std::vector<Case> cases = {
      {strips(2, 32, 64, 32, 64), 0x101, true},   //brightened in place, 1:1
      {strips(4, 8, 32, 16, 64), 0x101, true},    //halved
      {strips(2, 30, 60, 32, 64), 0x101, true},   //shrunk a little
      {one(0, 0, 16, 3), 0, true, 0x8000, before},
      {strips(2, 32, 64, 32, 64), 0x101, true, 0, 0x40'0000},  //(VRAM's third copy)
      {strips(2, 32, 64, 32, 64), 0x101, true, 0, 0, 8, format == 3 ? 7u : 6u},
      {one(0, 1, 0, 0), 0, false},
      {one(1, 0, 0, 0), 0, false},
      {one(0, 0, 16, 1), 0, false, 0x8000, before},
      {one(0, 0, 0.25f, 0, 64), 0x101, false, 0, 0, 6},  //(64 wide: column 64 repeats round to 0)
      {both(one(0, 0, 0, 0), one(32, 0, 0, 0)), 0, false},
    };
    for(auto& test : cases) {
      for(Canvas* c : {&copied, &read}) {
        c->memory.copyIn(VRAM, picture.data(), picture.size());
        c->ge.commands[GE::FrameBufferPointer] = test.frameBuffer;
        c->ge.commands[GE::FrameBufferWidth] = 64;
        c->texture(test.kind ? test.kind : format, 1 << test.size, 1 << test.size, 64);
        c->ge.commands[GE::TextureAddress0] = test.texture;
        c->ge.commands[GE::TextureBufferWidth0] = 0x04 << 16 | 64;
        c->ge.commands[GE::TextureFilter] = test.filter;
        c->ge.commands[GE::TextureFunction] = 0 | 1 << 16;  //modulated, doubled
        c->ge.commands[GE::AlphaBlendEnable] = 1;
        c->ge.commands[GE::BlendMode] = 10 | 10 << 4;
        c->ge.commands[GE::BlendFixedA] = 0xff'ffff, c->ge.commands[GE::BlendFixedB] = 0xff'ffff;
      }
      auto pixel = copied.ge.pixelState();
      auto texture = copied.ge.sampler();
      auto look = copied.ge.lookFor(pixel, &texture);
      std::vector<GE::Vertex> corners;
      for(auto& v : test.sprites) {
        GE::Vertex corner;
        corner.u = v.u, corner.v = v.v, corner.x = v.x, corner.y = v.y, corner.color = v.color;
        corners.push_back(corner);
      }
      CHECK(copied.ge.readsAhead(look, corners, corners.size()), test.ahead);
      //(decode() takes the copy where readsAhead() says so, and only there)
      GE::Look decoding = look;
      copied.ge.decode(decoding, GE::Region{pixel.left, pixel.top, pixel.right, pixel.bottom}, 512, false,
                       [&] { return copied.ge.readsAhead(decoding, corners, corners.size()); });
      CHECK(decoding.texture.decoded != nullptr, test.ahead);
      copied.ge.dropTextures();  //(the draw decodes afresh)
      copied.draw(GE::Sprites, test.sprites);
      read.draw(GE::Sprites, test.sprites);
      CHECK(copied.memory.vram == read.memory.vram, true);
    }
  }
}

//A sprite whose blending keeps every pixel as it was (draw.cpp's keepsPixels(): the source times a fixed 0, plus the
//destination times a fixed 255) isn't drawn, in each frame buffer format, which leaves the picture as drawing it does:
//as dithering by a matrix of zeros, which is drawn, leaves it. Unless reading its texture would be reported: with
//nothing behind it, it's drawn, each read reported as when it's dithered; of format 11, the format noted.
static auto drawKeptPixels() -> void {
  for(u32 format : {0u, 1u, 2u, 3u}) {
    Canvas c{format};
    std::mt19937 random{format + 5};
    std::vector<u8> picture(64 * 64 * 4);
    for(auto& byte : picture) byte = random();
    c.ge.commands[GE::FrameBufferWidth] = 64;
    c.texture(3, 4, 4, 4);
    c.ge.commands[GE::TextureFunction] = 0;
    c.ge.commands[GE::AlphaBlendEnable] = 1;
    c.ge.commands[GE::BlendMode] = 12 | 15 << 4;  //(fixed factors, both)
    c.ge.commands[GE::BlendFixedA] = 0, c.ge.commands[GE::BlendFixedB] = 0xff'ffff;
    for(bool dithered : {false, true}) {
      c.memory.copyIn(VRAM, picture.data(), picture.size());
      c.ge.commands[GE::DitherEnable] = dithered;
      c.draw(GE::Sprites, {{0, 0, 0xffff'ffff, 0, 0, 0}, {4, 4, 0x80ff'ffff, 64, 64, 0}});
      CHECK(std::equal(picture.begin(), picture.end(), c.memory.vram.begin()), true);
    }
    //its texels with nothing behind them (address 0): each read reported, drawn or dithered
    c.ge.commands[GE::TextureAddress0] = 0, c.ge.commands[GE::TextureBufferWidth0] = 4;
    size_t reported[2];
    for(bool dithered : {false, true}) {
      c.s.unmapped.clear();
      c.ge.commands[GE::DitherEnable] = dithered;
      c.draw(GE::Sprites, {{0, 0, 0xffff'ffff, 0, 0, 0}, {4, 4, 0x80ff'ffff, 64, 64, 0}});
      reported[dithered] = c.s.unmapped.size();
      CHECK(std::equal(picture.begin(), picture.end(), c.memory.vram.begin()), true);
    }
    CHECK(reported[0] > 0 && reported[0] == reported[1], true);
    //a format the PSP doesn't have
    c.ge.noted.clear();
    c.ge.commands[GE::DitherEnable] = 0;
    c.texture(11, 4, 4, 4);
    c.ge.commands[GE::TextureFunction] = 0;
    c.draw(GE::Sprites, {{0, 0, 0xffff'ffff, 0, 0, 0}, {4, 4, 0x80ff'ffff, 64, 64, 0}});
    CHECK(c.ge.noted.size(), 1u);
  }
}

//What's kept of a texture: one copy, of as many rows as any primitive has taken from it. One taking fewer draws from
//it as it is; one taking more makes it longer (the rows already decoded kept as they are, the rest decoded). Past
//the cache's budget, the copies used longest ago go first.
static auto drawDecodedRows() -> void {
  Canvas c;
  c.texture(3, 16, 512, 16);
  for(u32 n = 0; n < 16 * 512; n++) c.memory.write(4, Texture + n * 4, n);
  auto& entries = c.ge.textures.entries;
  c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 16, 0, 16, 16, 0}});  //rows 0-15, so 16 kept (in eights)
  CHECK(entries.size(), 1u);
  auto* first = entries.begin()->second.get();
  CHECK(first->rows, 16u);
  c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 8, 0, 16, 8, 0}});  //fewer: the same copy
  CHECK(entries.size(), 1u);
  CHECK(entries.begin()->second.get() == first, true);
  c.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 100, 0, 16, 16, 0}});  //more: a longer copy, still one
  CHECK(entries.size(), 1u);
  CHECK(entries.begin()->second->rows, 104u);
  CHECK(c.pixel(5, 15), 96u * 16 + 5);  //row 15's v: 15.5 x 6.25 = 96.875
  CHECK(c.pixel(5, 0), 3u * 16 + 5);     //a row the shorter copy had: 0.5 x 6.25 = 3.125
  CHECK(c.ge.textures.bytes, 16u * 104 * 4);

  Canvas lru;
  lru.ge.textures.budget = 3 * 16 * 16 * 4;  //three 16x16 textures
  auto use = [&](u32 n) {  //the nth of four 16x16 textures, one after another
    lru.texture(3, 16, 16, 16);
    lru.ge.commands[GE::TextureAddress0] = (Texture + n * 1024) & 0xff'ffff;
    lru.draw(GE::Sprites, {{0, 0, 0, 0, 0, 0}, {16, 16, 0, 16, 16, 0}});
  };
  auto kept = [&](u32 n) {
    return std::any_of(lru.ge.textures.entries.begin(), lru.ge.textures.entries.end(),
                       [&](auto& entry) { return entry.first.address == Texture + n * 1024; });
  };
  use(0), use(1), use(2), use(0), use(3);  //the second, unused longest, goes
  CHECK(kept(0) && !kept(1) && kept(2) && kept(3), true);
  CHECK(lru.ge.textures.bytes, 3u * 16 * 16 * 4);
  use(1);  //and is decoded again; the third goes
  CHECK(kept(0) && kept(1) && !kept(2) && kept(3), true);
}

//A sprite turned a quarter (corners bottom-left and top-right, so v runs across x) whose left edge is 9/16 into its
//first column draws that column, sampled at its middle: a sixteenth of a pixel left of the left corner, where v is a
//sixteenth of a pixel's step past the corner's. From v = 0 that's below 0, which repeats round to the texture's last
//row; falling 100 texels a pixel to the right, it's 6 rows past the highest v. Kept decoded, the texture must give
//those rows as memory does (draw.cpp's rows a primitive reaches).
static auto drawTurnedDecoded() -> void {
  struct Case { u32 width, height, filter; float leftV, rightV, rightX; };
  for(auto [width, height, filter, leftV, rightV, rightX] :
      {Case{16, 256, 0, 0, 8, 8.5625f}, Case{1, 16, 0, 0, 4, 8.5625f}, Case{16, 256, 0x101, 0.5f, 8.5f, 8.5625f},
       Case{16, 512, 0, 300, 200, 1.5625f}}) {
    std::vector<u32> drawn[2];
    for(bool decoding : {false, true}) {
      Canvas c;
      if(!decoding) c.memory.watching = nullptr;  //nothing kept decoded (Memory::canWatch())
      u32 bufferWidth = std::max(width, 4u);
      c.texture(3, width, height, bufferWidth);
      c.ge.commands[GE::TextureFilter] = filter;
      for(u32 v = 0; v < height; v++) {
        for(u32 u = 0; u < bufferWidth; u++) c.memory.write(4, Texture + (v * bufferWidth + u) * 4, v << 8 | u);
      }
      c.draw(GE::Sprites, {{0, leftV, 0, 0.5625f, 8, 0}, {float(width), rightV, 0, rightX, 0, 0}});
      for(u32 y = 0; y < 8; y++) for(u32 x = 0; x < 9; x++) drawn[decoding].push_back(c.pixel(x, y));
      if(decoding) CHECK(c.ge.textures.entries.size(), 1u);
    }
    CHECK(drawn[1] == drawn[0], true);
    if(height == 256 && !filter) CHECK(drawn[1][0], 255u << 8 | 15);  //column 0, row 0: texel (15, 255)
  }
}

//Textures kept decoded against textures read from memory texel by texel, as the GE read them before it kept any (a
//machine without watching() decodes nothing: Memory::canWatch()): the same random 2D primitives drawn by both must
//come out the same, pixel for pixel. (Comparing thread counts can't catch a mistake in what's kept: every count
//draws from the same copy.) The primitives: sprites (upright, turned and mirrored), triangles, strips, fans and
//points, their corners on any sixteenth, left ones often 9/16 into a pixel; their textures of every format, DXT's
//too, with palettes, swizzled or not, up to 512 rows, repeating or held at the edges, filtered or not, their
//coordinates
//inside, on and around texel boundaries, outside and steep; some textures in VRAM where the primitives draw, some
//drawn with again after their memory changed.
static auto drawDecodedAgainstMemory() -> void {
  constexpr u32 Area = 0x0900'0000, AreaSize = 1 << 20;  //random bytes: the textures and palettes in RAM
  constexpr u32 Width = 64, Height = 48;                  //the frame buffer, at VRAM's start
  constexpr u32 Bits[11] = {16, 16, 16, 32, 4, 8, 16, 32, 4, 8, 8}, Filters[4] = {0, 1, 0x100, 0x101};
  constexpr u32 Kinds[9] = {GE::Sprites, GE::Sprites, GE::Sprites, GE::Sprites, GE::Triangles, GE::Triangles,
                            GE::TriangleStrip, GE::TriangleFan, GE::Points};
  std::mt19937 random{20261006};
  auto below = [&](u32 n) { return u32(random() % n); };
  auto chance = [&](u32 percent) -> u32 { return below(100) < percent; };
  //(each value taken from it in a statement of its own, so every compiler takes them in the same order)
  auto position = [&](u32 extent) {  //pixels, on a sixteenth, a few past either end, often 9/16 into one
    s32 whole = s32(below(extent + 8)) - 4;
    u32 sixteenths = chance(25) ? 9 : below(16);
    return whole + sixteenths / 16.0f;
  };
  auto coordinate = [&](u32 size) -> float {  //in texels: inside, around the edges, outside, far, anywhere
    constexpr float Low[4] = {0, 0.5f, 1.0f / 16, -1.0f / 16}, High[3] = {0, 0.5f, 1.0f / 16};
    switch(below(6)) {
    case 0: return below(size * 16 + 1) / 16.0f;
    case 1: return Low[below(4)];
    case 2: return size - High[below(3)];
    case 3: return (s32(below(size * 64)) - s32(size * 32)) / 16.0f;
    case 4: return s32(below(1200)) - 400;
    default: return below(1 << 20) / float(1 << 20) * size;
    }
  };
  Canvas kept, read;
  read.memory.watching = nullptr;
  std::vector<u8> bytes(AreaSize);
  for(auto& byte : bytes) byte = random();
  for(Canvas* c : {&kept, &read}) {
    c->memory.copyIn(Area, bytes.data(), AreaSize);
    c->ge.commands[GE::FrameBufferWidth] = Width;
  }
  u32 format = 0, width = 1, height = 1, bufferWidth = 4, address = Area, span = 16, differing = 0;
  for(u32 n = 0; n < 4000; n++) {
    std::vector<std::pair<u32, u32>> commands;  //for both machines
    auto set = [&](u32 command, u32 value) { commands.push_back({command, value}); };
    if(n && chance(30)) {  //the last texture again, maybe changed
      for(u32 count = chance(50) ? 1 + below(4) : 0; count; count--) {
        u32 at = address + below(span) / 4 * 4;
        u32 value = random();
        for(Canvas* c : {&kept, &read}) c->memory.write(4, at, value);
      }
    } else {
      format = below(11);
      u32 widthBits = below(8);
      u32 heightBits = below(10);
      while(widthBits + heightBits > 14) (widthBits > heightBits ? widthBits : heightBits)--;
      width = 1 << widthBits, height = 1 << heightBits;
      u32 least = 128 / Bits[format];  //(a row of at least 16 bytes)
      u32 more = chance(20) ? below(4) : 0;
      bufferWidth = (std::max(width, least) + least - 1) / least * least + more * least;
      span = bufferWidth * Bits[format] / 8 * ((height + 7) / 8 * 8);
      if(chance(20)) address = VRAM + below(Width * Height * 4 / 16) * 16;  //where the primitives draw
      else address = Area + below((AreaSize - span) / 16) * 16;
      set(GE::TextureAddress0, address & 0xff'fff0);
      set(GE::TextureBufferWidth0, (address >> 24 & 0xf) << 16 | bufferWidth);
      set(GE::TextureSize0, heightBits << 8 | widthBits);
      set(GE::TextureFormat, format);
      set(GE::TextureMode, chance(30));
    }
    set(GE::TextureMappingEnable, 1);
    u32 clampU = chance(50);
    set(GE::TextureWrap, clampU | chance(50) << 8);
    set(GE::TextureFilter, Filters[below(4)]);
    u32 function = below(5);
    u32 withAlpha = chance(50);
    set(GE::TextureFunction, function | withAlpha << 8 | chance(20) << 16);
    set(GE::TextureEnvironmentColor, random() & 0xff'ffff);
    set(GE::FrameBufferPixelFormat, chance(80) ? 3 : below(3));
    u32 left = chance(30) ? below(16) : 0;
    u32 top = chance(30) ? below(16) : 0;
    set(GE::Scissor1, left | top << 10);
    u32 right = Width - 1, bottom = Height - 1;
    if(chance(30)) right = left + below(Width - left), bottom = top + below(Height - top);
    set(GE::Scissor2, right | bottom << 10);
    if(format >= 4 && format < 8) {
      u32 palette = Area + below((AreaSize - 1024) / 16) * 16;
      set(GE::ClutAddress, palette & 0xff'fff0);
      set(GE::ClutAddressUpper, palette >> 8 & 0xf'0000);
      u32 clutFormat = below(4);
      u32 shift = below(32);
      u32 mask = chance(70) ? 0xff : below(256);
      set(GE::ClutFormat, clutFormat | shift << 2 | mask << 8 | below(32) << 16);
      set(GE::ClutLoad, 1 + below(32));
    }
    for(Canvas* c : {&kept, &read}) {
      for(auto [command, value] : commands) c->ge.commands[command] = value;
      if(format >= 4 && format < 8) c->ge.loadClut();
    }

    u32 kind = Kinds[below(9)];
    u32 count = kind == GE::Sprites ? 2 * (1 + below(3)) : kind == GE::Triangles ? 3 * (1 + below(2))
              : kind == GE::Points ? 1 + below(4) : 4 + below(2);
    std::vector<V> vertices;
    for(u32 k = 0; k < count; k++) {  //(a braced list's values are taken in order)
      vertices.push_back({coordinate(width), coordinate(height), u32(random()), position(Width), position(Height),
                          0});
    }
    if(kind == GE::Sprites && chance(40)) {  //turned, its left corner 9/16 into a pixel, its v whole, steep or not
      V &from = vertices[0], &to = vertices[1];
      from.x = s32(below(Width)) - 4 + 9 / 16.0f;
      u32 across = 1 + below(12);
      to.x = from.x + across + below(16) / 16.0f;
      to.y = position(Height);
      from.y = to.y + 1 + below(12);
      from.v = s32(below(height + 2)) - 1;
      s32 reach = chance(30) ? 400 : 20;
      to.v = from.v + s32(below(2 * reach)) - reach;
    }
    for(Canvas* c : {&kept, &read}) c->draw(kind, vertices);
    if(std::memcmp(kept.memory.vram.data(), read.memory.vram.data(), 0x1'0000) && !differing++) {
      std::printf("  case %u: format %u, %ux%u (rows of %u) at %08x, primitive %u of %u vertices, differs\n", n,
                  format, width, height, bufferWidth, address, kind, count);
    }
  }
  CHECK(differing, 0u);
}

auto drawTests() -> Tests {
  return {
    {"draw textures kept decoded", drawDecodedTextures},
    {"draw textures kept decoded, their rows", drawDecodedRows},
    {"draw over its own texture from a copy", drawCopiedTexture},
    {"draw keeping every pixel", drawKeptPixels},
    {"draw turned sprites kept decoded", drawTurnedDecoded},
    {"draw textures kept decoded against memory", drawDecodedAgainstMemory},
    {"draw sprites", drawSprites}, {"draw texture formats", drawTextureFormats}, {"draw filter", drawFilter},
    {"draw texture functions", drawTextureFunctions}, {"draw pixel tests", drawPixelTests}, {"draw blending", drawBlending},
    {"draw dither and masks", drawDitherAndMasks}, {"draw triangles", drawTriangles},
    {"draw texel steps", drawTexelSteps}, {"draw ambient and filters", drawAmbientAndFilters},
    {"draw lines", drawLines}, {"draw corners at one depth", drawOneDepth}, {"draw DXT textures", drawDXT},
    {"draw vertex formats", drawVertexFormats},
    {"blit sample", blitSample}, {"doublelist sample", doublelistSample}, {"clut sample", clutSample},
    {"blend sample", blendSample}, {"cube sample", cubeSample}, {"celshading sample", celshadingSample},
    {"envmap sample", envmapSample},
  };
}

}
