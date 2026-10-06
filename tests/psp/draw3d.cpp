//Drawing in 3D (ares/psp/ge transform.cpp, and draw.cpp's 3D paths): the matrices and the viewport, the GE's
//rounding onto the screen, what it won't draw (off the screen, past the depths), the cut at the near plane, culling,
//perspective-correct texture coordinates, fog, the depth range test, texture coordinate modes, morphing and
//skinning. Expected values are worked out by hand from the rules in transform.cpp; pspsdk's "cube" sample (draw.cpp)
//checks it all together.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

constexpr u32 VertexData3D = 0x0896'0000, Texture3D = 0x0898'0000;
constexpr u32 VRAM3D = Memory::VRAMBase;

//A float as a command's argument: its top 24 bits.
static auto f24(float value) -> u32 { u32 bits; std::memcpy(&bits, &value, 4); return bits >> 8; }

//A 3D vertex as vertex type 0x19f lays it out: float texture coordinates, 8888 color, float position.
struct V3 { float u, v; u32 color; float x, y, z; };

//The GE ready to draw in 3D into a 16-pixel-wide 8888 frame buffer at VRAM's start (depth at 0x10000), every matrix
//the identity, and the viewport and offset set so that a model's x and y are the screen's pixels and its z gives the
//depth 2000 + 1000z (w stays 1); DEPTH_CLIP_ENABLE on; the depth range every depth; the depth test passing
//everything, so depths are written.
struct Scene {
  System s;
  GE& ge = s.ge;
  Memory& memory = s.memory;
  Scene() {
    auto& c = ge.commands;
    c[GE::FrameBufferPointer] = 0, c[GE::FrameBufferWidth] = 16, c[GE::FrameBufferPixelFormat] = 3;
    c[GE::DepthBufferPointer] = 0x1'0000, c[GE::DepthBufferWidth] = 16;
    c[GE::Scissor2] = 1023 << 10 | 1023, c[GE::Region2] = 1023 << 10 | 1023;
    c[GE::VertexType] = 0x19f, c[GE::ShadeMode] = 1;
    c[GE::DepthTestEnable] = 1, c[GE::DepthTest] = 1;
    c[GE::DepthClipEnable] = 1;
    c[GE::MaxZ] = 0xffff;  //the depth range test: every depth (sceGuDepthRange sets it; at power-on it's 0-0)
    for(u32 n : {0, 4, 8}) ge.world[n] = ge.view[n] = f24(1);
    for(u32 n : {0, 5, 10, 15}) ge.projection[n] = f24(1);
    c[GE::ViewportXScale] = f24(1), c[GE::ViewportYScale] = f24(1), c[GE::ViewportZScale] = f24(1000);
    c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(2000);
    c[GE::OffsetX] = 2048 << 4, c[GE::OffsetY] = 2048 << 4;
    c[GE::TextureScaleU] = c[GE::TextureScaleV] = f24(1);
  }
  auto draw(u32 kind, std::initializer_list<V3> vertices) -> void {
    u32 at = VertexData3D;
    for(auto& vertex : vertices) {
      for(float value : {vertex.u, vertex.v}) memory.write(4, at, bits(value)), at += 4;
      memory.write(4, at, vertex.color), at += 4;
      for(float value : {vertex.x, vertex.y, vertex.z}) memory.write(4, at, bits(value)), at += 4;
    }
    ge.vertexAddress = VertexData3D;
    ge.primitive(kind, vertices.size());
  }
  static auto bits(float value) -> u32 { u32 word; std::memcpy(&word, &value, 4); return word; }
  auto pixel(u32 x, u32 y) -> u32 { return memory.read(4, VRAM3D + (y * 16 + x) * 4); }
  auto depth(u32 x, u32 y) -> u32 { return memory.read(2, VRAM3D + 0x1'0000 + (y * 16 + x) * 2); }
  auto clear() -> void { memory.fill(VRAM3D, 0, 0x2'0000); }
  //a 16x16 8888 texture at Texture3D whose texel (x, y) is x | y << 8 | 0x80 << 16, nearest, replace
  auto texture() -> void {
    for(u32 y = 0; y < 16; y++) {
      for(u32 x = 0; x < 16; x++) memory.write(4, Texture3D + (y * 16 + x) * 4, x | y << 8 | 0x80 << 16);
    }
    ge.commands[GE::TextureMappingEnable] = 1;
    ge.commands[GE::TextureAddress0] = Texture3D & 0xff'ffff;
    ge.commands[GE::TextureBufferWidth0] = (Texture3D >> 24 & 0xf) << 16 | 16;
    ge.commands[GE::TextureSize0] = 4 << 8 | 4;
    ge.commands[GE::TextureFormat] = 3;
    ge.commands[GE::TextureFunction] = 3;
  }
};

//The matrices, in order (world, then view, then projection), and the viewport: a 3D sprite lands where they put it,
//at its depth; and the GE's rounding onto the screen, to the sixteenth: up from 0.625 of one.
static auto draw3dTransform() -> void {
  Scene c;
  c.draw(GE::Sprites, {{0, 0, 0xff00'00ff, 2, 1, 0.1f}, {0, 0, 0xff00'00ff, 6, 5, 0.1f}});
  CHECK(c.pixel(2, 1), 0x0000'00ff);  //(the alpha written is the stencil, 0)
  CHECK(c.pixel(5, 4), 0x0000'00ff);
  CHECK(c.pixel(6, 4), 0);
  CHECK(c.depth(3, 3), 2100);  //2000 + 1000 * 0.1

  c.clear();
  c.ge.world[9] = f24(3);           //the world moves it 3 right
  c.ge.view[4] = f24(2);            //the view doubles y
  c.ge.projection[13] = f24(-1);    //the projection moves it 1 up
  c.draw(GE::Sprites, {{0, 0, 0xff00'ff00, 0, 1, 0}, {0, 0, 0xff00'ff00, 2, 3, 0}});
  CHECK(c.pixel(3, 1), 0x0000'ff00);  //x 0-2 becomes 3-5; y 1-3 becomes 2-6, then 1-5
  CHECK(c.pixel(4, 4), 0x0000'ff00);
  CHECK(c.pixel(2, 1), 0);
  CHECK(c.pixel(3, 5), 0);

  //settings are read from whole command words, as a display list leaves them: the command byte doesn't count
  CHECK(GE::float24(GE::ViewportXScale << 24 | f24(1)) == 1.0f, true);
  c.ge.commands[GE::ViewportXScale] = GE::ViewportXScale << 24 | f24(1);
  c.ge.commands[GE::ViewportXCenter] = GE::ViewportXCenter << 24 | f24(2048);

  //rounding: screen x * 16 + 0.375, cut toward zero
  auto t = c.ge.transformState();
  GE::Vertex v;
  v.clip[0] = 2 + 0.6f / 16;
  c.ge.project(v, t, false);
  CHECK(v.x == 2.0f, true);
  v.clip[0] = 2 + 0.7f / 16;
  c.ge.project(v, t, false);
  CHECK(v.x == 2.0625f, true);
}

//What isn't drawn: a primitive with a vertex off the screen's 4096 pixels; with DEPTH_CLIP_ENABLE off, one with a
//depth outside 0-65535 or a z / w past 1; with it on, depths are held to the range, and only a primitive with every
//z / w past the same end is dropped.
static auto draw3dOutside() -> void {
  Scene c;
  auto triangle = [&](float z0, float z1, float z2, float x2 = 2) {
    c.clear();
    c.draw(GE::Triangles, {{0, 0, 0xffff'ffff, 2, 2, z0}, {0, 0, 0xffff'ffff, 14, 2, z1}, {0, 0, 0xffff'ffff, x2, 14, z2}});
    return c.pixel(4, 4) != 0;
  };
  CHECK(triangle(0, 0, 0), true);
  CHECK(triangle(0, 0, 0, 4096 - 2048), false);  //screen x 4096: off the screen
  CHECK(triangle(-0.5f, 0, 0), true);             //depth 1500, z / w -0.5: fine
  CHECK(triangle(0, 0, -3), true);                //past the near plane: cut (above y 6), the depth held to 0
  CHECK(triangle(1.5f, 1.5f, 0), true);           //two past the far end: drawn, with the depth clamp
  CHECK(triangle(1.5f, 1.5f, 1.5f), false);       //all three past it: not
  c.ge.commands[GE::ViewportZCenter] = f24(500);  //depth 500 + 1000z: below 0 from z -0.5
  CHECK(triangle(-0.9f, 0, 0), true);             //depth -400, held to 0
  CHECK(c.depth(2, 2) <= 100, true);
  c.ge.commands[GE::DepthClipEnable] = 0;
  CHECK(triangle(-0.9f, 0, 0), false);            //z / w is fine, but depth -400 is off the depths: not drawn
  c.ge.commands[GE::ViewportZCenter] = f24(2000);
  CHECK(triangle(0, 0, 0), true);
  CHECK(triangle(1.5f, 0, 0), false);             //depth 3500 is fine, but z / w is past 1
  CHECK(triangle(0.99999f, 0, 0), true);          //z / w just short of 1 + 2^-15

  //sprites, by the same rules for their two corners
  auto sprite = [&](float x1, float z0, float z1) {
    c.clear();
    c.draw(GE::Sprites, {{0, 0, 0xffff'ffff, 2, 2, z0}, {0, 0, 0xffff'ffff, x1, 6, z1}});
    return c.pixel(3, 3) != 0;
  };
  CHECK(sprite(6, 0, 0), true);
  CHECK(sprite(6, 1.5f, 0), false);               //DEPTH_CLIP_ENABLE still off
  c.ge.commands[GE::DepthClipEnable] = 1;
  CHECK(sprite(6, 1.5f, 0), true);
  CHECK(sprite(6, 1.5f, 1.5f), false);
  CHECK(sprite(4096 - 2048, 0, 0), false);        //a corner off the screen

  //points: one with a depth below 0 is held to it with DEPTH_CLIP_ENABLE on, and not drawn with it off; z / w past 1
  //doesn't count for points
  auto point = [&](float z) {
    c.clear();
    c.draw(GE::Points, {{0, 0, 0xffff'ffff, 3, 3, z}});
    return c.pixel(3, 3) != 0;
  };
  c.ge.commands[GE::ViewportZCenter] = f24(500);
  CHECK(point(-0.9f), true);
  c.ge.commands[GE::DepthClipEnable] = 0;
  CHECK(point(-0.9f), false);
  CHECK(point(0), true);
  CHECK(point(1.5f), true);  //depth 2000
}

//The near plane (z < -w, here z < -1): a triangle reaching past it is cut along it, and only the rest drawn. Corners
//(2, 2) red and (14, 2) green at z 0, (8, 14) blue at z -4: the edges to the third cross the plane a quarter of the
//way along, at y = 5, so rows 2-4 are drawn and none below. The corners the cut makes take the colors a quarter of
//the way along too, so the colors are as the whole triangle's would be: at pixel (8, 4), sample point
//(8.4375, 4.4375), the corners' weights are 0.362, 0.435 and 0.203: red 92, green 110, blue 51. With
//DEPTH_CLIP_ENABLE off, the third corner's z / w (-4) drops the whole triangle instead. A triangle with every w below
//zero isn't drawn.
static auto draw3dClipping() -> void {
  Scene c;
  auto draw = [&] {
    c.clear();
    c.draw(GE::Triangles, {{0, 0, 0xff00'00ff, 2, 2, 0}, {0, 0, 0xff00'ff00, 14, 2, 0}, {0, 0, 0xffff'0000, 8, 14, -4}});
  };
  draw();
  CHECK(c.pixel(8, 2) != 0, true);
  CHECK(c.pixel(8, 4) != 0, true);
  CHECK(c.pixel(3, 4) != 0, true);    //sample point x 3.4375: inside the cut edge, which runs from x 2 to 3.5
  CHECK(c.pixel(8, 5), 0);
  CHECK(c.pixel(8, 8), 0);
  CHECK(c.depth(8, 4) < 2000, true);  //nearer than z 0, further than the plane
  u32 color = c.pixel(8, 4);
  auto near = [](u32 value, u32 expected) { return value + 1 >= expected && value <= expected + 1; };
  CHECK(near(color & 0xff, 92) && near(color >> 8 & 0xff, 110) && near(color >> 16 & 0xff, 51), true);
  c.ge.commands[GE::ShadeMode] = 0;   //flat: every piece takes the last vertex's color, blue
  draw();
  CHECK(c.pixel(8, 4), 0x00ff'0000);
  CHECK(c.pixel(3, 4), 0x00ff'0000);
  c.ge.commands[GE::ShadeMode] = 1;
  c.ge.commands[GE::DepthClipEnable] = 0;
  draw();
  CHECK(c.pixel(8, 2), 0);
  c.ge.commands[GE::DepthClipEnable] = 1;
  //w -1 everywhere, with the viewport's x and y turned round to match, so the triangle would land where it did; z 1,
  //so z / w is -1 and z + w is 0: in reach of both rules above, and dropped only because every w is below zero
  c.ge.projection[15] = f24(-1);
  c.ge.commands[GE::ViewportXScale] = c.ge.commands[GE::ViewportYScale] = f24(-1);
  c.clear();
  c.draw(GE::Triangles, {{0, 0, 0xffff'ffff, 2, 2, 1}, {0, 0, 0xffff'ffff, 14, 2, 1}, {0, 0, 0xffff'ffff, 8, 14, 1}});
  CHECK(c.pixel(8, 4), 0);
}

//Culling: with CULL 1 only triangles running clockwise on the screen are drawn, with 0 only counterclockwise ones;
//in 3D and in through mode; a strip's every other triangle is judged the other way round, so a strip whose
//triangles all face the same way is drawn whole; clear mode isn't culled.
static auto draw3dCulling() -> void {
  Scene c;
  V3 a{0, 0, 0xffff'ffff, 2, 2, 0}, b{0, 0, 0xffff'ffff, 14, 2, 0}, d{0, 0, 0xffff'ffff, 2, 14, 0};  //a b d: clockwise
  auto drawn = [&](std::initializer_list<V3> corners, u32 kind = GE::Triangles) {
    c.clear();
    c.draw(kind, corners);
    return c.pixel(4, 4) != 0;
  };
  CHECK(drawn({a, b, d}), true);
  CHECK(drawn({a, d, b}), true);
  c.ge.commands[GE::CullFaceEnable] = 1;
  c.ge.commands[GE::Cull] = 1;
  CHECK(drawn({a, b, d}), true);
  CHECK(drawn({a, d, b}), false);
  c.ge.commands[GE::Cull] = 0;
  CHECK(drawn({a, b, d}), false);
  CHECK(drawn({a, d, b}), true);
  //a strip a b d e: triangles a b d (clockwise) and b d e (counterclockwise as listed: the strip's way round)
  V3 e{0, 0, 0xffff'ffff, 14, 14, 0};
  c.ge.commands[GE::Cull] = 1;
  c.clear();
  c.draw(GE::TriangleStrip, {a, b, d, e});
  CHECK(c.pixel(4, 4) != 0 && c.pixel(12, 12) != 0, true);
  c.ge.commands[GE::Cull] = 0;
  c.clear();
  c.draw(GE::TriangleStrip, {a, b, d, e});
  CHECK(c.pixel(4, 4) == 0 && c.pixel(12, 12) == 0, true);
  c.ge.commands[GE::ClearMode] = 1 | 1 << 8;
  CHECK(drawn({a, b, d}), true);
  c.ge.commands[GE::ClearMode] = 0;
  c.ge.commands[GE::VertexType] = 0x80'019f;  //through mode: positions are pixels
  CHECK(drawn({a, b, d}), false);
  CHECK(drawn({a, d, b}), true);
}

//Perspective: texture coordinates blended as u/w and 1/w. Corners (0, 0) u 0 w 1, (16, 0) u 16 w 4, (0, 16) u 0 w 1:
//at pixel (7, 0), sample point (7.4375, 0.4375), the weights are 0.5078125, 0.46484375 and 0.02734375, so
//u = (0.46484375 * 16 / 4) / (0.5078125 + 0.46484375 / 4 + 0.02734375) = 2.85: texel 2. Blended straight (2D), 7.4375.
//With the second corner's q 0.5 (texture projection), q is blended the same way and divides:
//u = 1.859375 / (0.5078125 + 0.46484375 * 0.5 / 4 + 0.02734375) = 3.13: texel 3.
static auto draw3dPerspective() -> void {
  Scene c;
  c.texture();
  auto pixel = c.ge.pixelState();
  auto texture = c.ge.sampler();
  GE::Vertex a, b, d;
  a.x = 0, a.y = 0, a.u = 0, a.color = 0xffff'ffff;
  b.x = 16, b.y = 0, b.u = 16, b.clip[3] = 4, b.color = 0xffff'ffff;
  d.x = 0, d.y = 16, d.u = 0, d.color = 0xffff'ffff;
  c.ge.triangle(pixel, &texture, a, b, d, 0, true);
  CHECK(c.pixel(7, 0) & 0xff, 2);
  c.ge.triangle(pixel, &texture, a, b, d, 0, false);
  CHECK(c.pixel(7, 0) & 0xff, 7);
  b.q = 0.5f;
  c.ge.triangle(pixel, &texture, a, b, d, 0, true);
  CHECK(c.pixel(7, 0) & 0xff, 3);
}

//Fog: FOG1 1, FOG2 0.5: a vertex at view z 0 keeps half its color, (0 + 1) * 0.5 = 0.5, 128 of 255:
//red (0 * 128 + 255 * 127 + 255) / 256 = 127 from the fog color; green (255 * 128 + 255) / 256 = 128. At z 1, fog 1:
//none; at z -0.9, 0.05: 12 of 255. Not in through mode. Across a triangle the fog is blended from the corners: from
//1 (z 1) at two to 0 (z -1) at the third, at pixel (7, 0) (the third's weight 0.46484375) it's 0.53515625, 137.
static auto draw3dFog() -> void {
  Scene c;
  c.ge.commands[GE::FogEnable] = 1;
  c.ge.commands[GE::FogEnd] = f24(1);
  c.ge.commands[GE::FogSlope] = f24(0.5f);
  c.ge.commands[GE::FogColor] = 0x00'00ff;
  c.draw(GE::Sprites, {{0, 0, 0xff00'ff00, 0, 0, 0}, {0, 0, 0xff00'ff00, 4, 4, 0}});
  CHECK(c.pixel(1, 1), 0x0000'807f);
  c.clear();
  c.draw(GE::Sprites, {{0, 0, 0xff00'ff00, 0, 0, 1}, {0, 0, 0xff00'ff00, 4, 4, 1}});
  CHECK(c.pixel(1, 1), 0x0000'ff00);
  c.clear();
  c.draw(GE::Sprites, {{0, 0, 0xff00'ff00, 0, 0, -0.9f}, {0, 0, 0xff00'ff00, 4, 4, -0.9f}});
  CHECK(c.pixel(1, 1), (u32((255 * 12 + 255) / 256) << 8) | u32((255 * 243 + 255) / 256));
  c.clear();
  c.ge.commands[GE::VertexType] = 0x80'019f;
  c.draw(GE::Sprites, {{0, 0, 0xff00'ff00, 0, 0, 0}, {0, 0, 0xff00'ff00, 4, 4, 0}});
  CHECK(c.pixel(1, 1), 0x0000'ff00);
  c.ge.commands[GE::VertexType] = 0x19f;
  c.clear();
  c.draw(GE::Triangles, {{0, 0, 0xff00'ff00, 0, 0, 1}, {0, 0, 0xff00'ff00, 16, 0, -1}, {0, 0, 0xff00'ff00, 0, 16, 1}});
  CHECK(c.pixel(7, 0), u32((255 * 137 + 255) / 256) << 8 | u32((255 * 118 + 255) / 256));
  c.clear();  //the distance is the view's: the view moved 0.5 back makes z 0's fog (0 - 0.5 + 1) * 0.5, 64 of 255
  c.ge.view[11] = f24(-0.5f);
  c.draw(GE::Sprites, {{0, 0, 0xff00'ff00, 0, 0, 0}, {0, 0, 0xff00'ff00, 4, 4, 0}});
  CHECK(c.pixel(1, 1), u32((255 * 64 + 255) / 256) << 8 | u32((255 * 191 + 255) / 256));
}

//The depth range test (MIN_Z, MAX_Z): in 3D a pixel whose depth is outside is dropped; through mode has none.
static auto draw3dDepthRange() -> void {
  Scene c;
  c.ge.commands[GE::MinZ] = 1000;
  c.ge.commands[GE::MaxZ] = 2050;
  c.draw(GE::Sprites, {{0, 0, 0xff00'00ff, 0, 0, 0}, {0, 0, 0xff00'00ff, 4, 4, 0}});     //depth 2000
  CHECK(c.pixel(1, 1), 0x0000'00ff);
  c.clear();
  c.draw(GE::Sprites, {{0, 0, 0xff00'00ff, 0, 0, 0.1f}, {0, 0, 0xff00'00ff, 4, 4, 0.1f}});  //depth 2100
  CHECK(c.pixel(1, 1), 0);
  c.ge.commands[GE::VertexType] = 0x80'019f;
  c.draw(GE::Sprites, {{0, 0, 0xff00'00ff, 0, 0, 2100}, {0, 0, 0xff00'00ff, 4, 4, 2100}});
  CHECK(c.pixel(1, 1), 0x0000'00ff);
}

//Texture coordinates in 3D are fractions of the texture: (u * TEX_SCALE + TEX_OFFSET) * size. Or, with
//TEXTURE_MAP_MODE 1, from the texture matrix: here s = u / 2 + 0.25 (t = v / 2, q = 1), so u 0.25 lands on
//(0.125 + 0.25) * 16 = 6. Each sprite is drawn texel for pixel, so its first pixel shows its first texel.
static auto draw3dTextureCoordinates() -> void {
  Scene c;
  c.texture();
  c.draw(GE::Sprites, {{0.25f, 0.5f, 0xffff'ffff, 0, 0, 0}, {0.5f, 0.75f, 0xffff'ffff, 4, 4, 0}});
  CHECK(c.pixel(0, 0) & 0xffff, 8 << 8 | 4);  //u 0.25 of 16 texels: 4; v 0.5: 8
  c.ge.commands[GE::TextureScaleU] = f24(2);
  c.ge.commands[GE::TextureOffsetU] = f24(-0.25f);
  c.draw(GE::Sprites, {{0.25f, 0, 0xffff'ffff, 0, 0, 0}, {0.375f, 0.25f, 0xffff'ffff, 4, 4, 0}});
  CHECK(c.pixel(0, 0) & 0xff, 4);             //u (0.25 * 2 - 0.25) * 16 = 4
  c.ge.commands[GE::TextureScaleU] = f24(1);       //(mode 0 would now put u 0.25-0.75 on 4-12: texel 5)
  c.ge.commands[GE::TextureOffsetU] = 0;
  c.ge.commands[GE::TextureMapMode] = 1 | 1 << 8;  //from the texture matrix, applied to (u, v, 0)
  c.ge.textureMatrix[0] = f24(0.5f);               //s from u
  c.ge.textureMatrix[4] = f24(0.5f);               //t from v
  c.ge.textureMatrix[9] = f24(0.25f);              //s moved
  c.ge.textureMatrix[11] = f24(1);                 //q: 1
  c.draw(GE::Sprites, {{0.25f, 0, 0xffff'ffff, 0, 0, 0}, {0.75f, 0.5f, 0xffff'ffff, 4, 4, 0}});
  CHECK(c.pixel(0, 0) & 0xff, 6);
}

//Morphing: two targets, MORPH_WEIGHT 0.25 and 0.75, blended: position and color. Skinning: one weight of 1 through
//bone matrix 0, which moves 4 right. Points show where a vertex lands.
static auto draw3dMorphAndSkin() -> void {
  Scene c;
  c.ge.commands[GE::VertexType] = 0x19c | 1 << 18;  //color 8888, float position, two morph targets
  c.ge.commands[GE::MorphWeight0] = f24(0.25f);
  c.ge.commands[GE::MorphWeight0 + 1] = f24(0.75f);
  u32 at = VertexData3D;
  auto put = [&](u32 color, float x, float y) {
    c.memory.write(4, at, color), at += 4;
    for(float value : {x, y, 0.0f}) c.memory.write(4, at, Scene::bits(value)), at += 4;
  };
  put(0xff00'0000, 2, 2);  //target 0
  put(0xff00'0080, 6, 10); //target 1: x 2 * 0.25 + 6 * 0.75 = 5, y 0.5 + 7.5 = 8, red 0x60
  c.ge.vertexAddress = VertexData3D;
  c.ge.primitive(GE::Points, 1);
  CHECK(c.pixel(5, 8), 0x0000'0060);

  c.clear();
  c.ge.commands[GE::VertexType] = 0x19c | 3 << 9;  //one float weight
  c.ge.bones[0] = c.ge.bones[4] = c.ge.bones[8] = f24(1);
  c.ge.bones[9] = f24(4);
  at = VertexData3D;
  c.memory.write(4, at, Scene::bits(1.0f)), at += 4;
  put(0xffff'ffff, 3, 3);
  c.ge.vertexAddress = VertexData3D;
  c.ge.primitive(GE::Points, 1);
  CHECK(c.pixel(7, 3), 0x00ff'ffff);
  CHECK(c.pixel(3, 3), 0);
}



auto draw3dTests() -> Tests {
  return {
    {"draw3d transform", draw3dTransform}, {"draw3d outside", draw3dOutside}, {"draw3d clipping", draw3dClipping},
    {"draw3d culling", draw3dCulling}, {"draw3d perspective", draw3dPerspective}, {"draw3d fog", draw3dFog},
    {"draw3d depth range", draw3dDepthRange}, {"draw3d texture coordinates", draw3dTextureCoordinates},
    {"draw3d morph and skin", draw3dMorphAndSkin},
  };
}

}
