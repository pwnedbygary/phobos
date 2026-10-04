//Drawing (ares/psp/ge draw.cpp, texture.cpp, pixel.cpp): sprites, triangles and points in 2D, textures in each
//format with the palette, swizzling and filtering, the texture functions, the pixel pipeline's tests, blending,
//dithering, logic operations and write masks; and, with PSP_TEST_PROGRAMS, pspsdk's GU samples "blit" (three ways)
//and "doublelist", whose pictures are known pixel for pixel. Expected values are worked out by hand from pspgu.h's
//descriptions and the PSP's rounding (see pixel.cpp).
#include "kernel-machine.hpp"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <set>

namespace allegrex_test::psp {

constexpr u32 VertexData = 0x0896'0000, Texture = 0x0898'0000, Palette = 0x089a'0000;
constexpr u32 VRAM = Memory::VRAMBase;

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
  auto draw(u32 kind, std::initializer_list<V> vertices) -> void {
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
  auto depth(u32 x, u32 y) -> u32 { return memory.read(2, VRAM + 0x1'0000 + (y * 16 + x) * 2); }
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

  c.memory.write(2, VRAM + 0x1'0000, 0x8000);
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

//Triangles: the pixels whose sample points are inside, not those on right or bottom edges, so two triangles sharing
//an edge draw each pixel once; colors blended across, or with flat shading the last vertex's.
static auto drawTriangles() -> void {
  Canvas c;
  c.draw(GE::Triangles, {{0, 0, 0xff00'00ff, 0, 0, 0}, {0, 0, 0xff00'ff00, 8, 0, 0}, {0, 0, 0xffff'0000, 0, 8, 0}});
  u32 covered = 0;
  for(u32 y = 0; y < 9; y++) for(u32 x = 0; x < 9; x++) covered += c.pixel(x, y) != 0;
  CHECK(covered, 36);  //x + y up to 7
  CHECK(c.pixel(7, 0) != 0 && c.pixel(8, 0) == 0 && c.pixel(4, 4) == 0, true);
  CHECK(c.pixel(0, 0), 227 | 13 << 8 | 13 << 16);  //mostly the first vertex's red
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

  //two rectangles of two triangles each, sharing an edge at x = 4 7/16, right through column 4's sample points: the
  //column belongs to the rectangle it's a left edge of, so it's drawn once
  Canvas shared;
  shared.ge.commands[GE::AlphaBlendEnable] = 1;
  shared.ge.commands[GE::BlendMode] = 10 | 10 << 4;
  shared.ge.commands[GE::BlendFixedA] = 0xff'ffff;
  shared.ge.commands[GE::BlendFixedB] = 0xff'ffff;
  for(float left : {0.0f, 4.4375f}) {
    float right = left ? 8.0f : 4.4375f;
    shared.draw(GE::TriangleStrip, {{0, 0, 0xff01'0101, left, 0, 0}, {0, 0, 0xff01'0101, right, 0, 0},
                                    {0, 0, 0xff01'0101, left, 4, 0}, {0, 0, 0xff01'0101, right, 4, 0}});
  }
  bool column = true;
  for(u32 y = 0; y < 4; y++) for(u32 x = 0; x < 8; x++) column &= (shared.pixel(x, y) & 0xff) == 1;
  CHECK(column, true);

  Canvas dots;
  dots.draw(GE::Points, {{0, 0, 0xff12'3456, 3.5f, 2.9f, 0}});
  CHECK(dots.pixel(3, 2), 0x12'3456);
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

auto drawTests() -> Tests {
  return {
    {"draw sprites", drawSprites}, {"draw texture formats", drawTextureFormats}, {"draw filter", drawFilter},
    {"draw texture functions", drawTextureFunctions}, {"draw pixel tests", drawPixelTests}, {"draw blending", drawBlending},
    {"draw dither and masks", drawDitherAndMasks}, {"draw triangles", drawTriangles},
    {"draw ambient and filters", drawAmbientAndFilters},
    {"blit sample", blitSample}, {"doublelist sample", doublelistSample}, {"clut sample", clutSample},
    {"blend sample", blendSample}, {"cube sample", cubeSample}, {"celshading sample", celshadingSample},
    {"envmap sample", envmapSample},
  };
}

}
