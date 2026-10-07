//Drawing in 3D (ares/psp/ge transform.cpp, and draw.cpp's 3D paths): the matrices and the viewport, the GE's
//rounding onto the screen, what it won't draw (off the screen, past the depths), the cut at the near plane, culling,
//perspective-correct texture coordinates, fog, the depth range test, texture coordinate modes, morphing and
//skinning. Expected values are worked out by hand from the rules in transform.cpp; pspsdk's "cube" sample (draw.cpp)
//checks it all together.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

constexpr u32 VertexData3D = 0x0896'0000, Texture3D = 0x0898'0000;
constexpr u32 VRAM3D = Memory::VRAMBase;
constexpr u32 DepthSeen3D = Memory::VRAMBase + 3 * Memory::VRAMSize;  //VRAM's fourth copy: depth as the GE has it

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
  auto depth(u32 x, u32 y) -> u32 { return memory.read(2, DepthSeen3D + 0x1'0000 + (y * 16 + x) * 2); }
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
//at its depth; and the GE's rounding onto the screen, to the sixteenth: toward screen coordinate 2048.
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

  //rounding: the distance from 2048 cut to a sixteenth, toward 2048 (so right of it down, left of it up)
  auto t = c.ge.transformState();
  GE::Vertex v;
  v.clip[0] = 2 + 0.9f / 16;
  c.ge.project(v, t, false);
  CHECK(v.x == 2.0f, true);
  v.clip[0] = 2 + 1.1f / 16;
  c.ge.project(v, t, false);
  CHECK(v.x == 2.0625f, true);
  v.clip[0] = -2 - 0.9f / 16;
  c.ge.project(v, t, false);
  CHECK(v.x == -2.0f, true);
  v.clip[0] = -2 - 1.1f / 16;
  c.ge.project(v, t, false);
  CHECK(v.x == -2.0625f, true);
  //the screen's edges, once cut: a 32nd left of 0 and 4095 + 31/32 are on it, a 16th left of 0 and 4096 off it
  struct Edge { float screen; bool off; };
  const Edge edges[] = {{-1.0f / 32, false}, {4095 + 31.0f / 32, false}, {-1.0f / 16, true}, {4096, true}};
  for(Edge edge : edges) {
    v.clip[0] = edge.screen - 2048;
    c.ge.project(v, t, false);
    CHECK(v.outside == edge.off, true);
  }
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
//the way along too, so the colors are as the whole triangle's would be: at pixel (8, 4), sample point (8.5, 4.5),
//the corners' weights are 0.354, 0.4375 and 0.208: red 90, green 111, blue 53. With
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
  CHECK(c.pixel(3, 4) != 0, true);    //sample point x 3.5: inside the cut edge, which runs from x 2 to 3.5 (3.25 there)
  CHECK(c.pixel(8, 5), 0);
  CHECK(c.pixel(8, 8), 0);
  CHECK(c.depth(8, 4) < 2000, true);  //nearer than z 0, further than the plane
  u32 color = c.pixel(8, 4);
  auto near = [](u32 value, u32 expected) { return value + 1 >= expected && value <= expected + 1; };
  CHECK(near(color & 0xff, 90) && near(color >> 8 & 0xff, 111) && near(color >> 16 & 0xff, 53), true);
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

  //A 3D sprite from (0, 0) at w 1 to (16, 16) at w 4: u / w and 1 / w across x, v / w down y. At pixel (7, 0),
  //0.46875 across: u = (4 * 0.46875) / (1 - 0.75 * 0.46875) = 2.89 (in 2D, 7). At (0, 15), 0.03125 across and
  //0.96875 down: v = (4 * 0.96875) / (1 - 0.75 * 0.03125) = 3.97, so down the left edge it reaches a quarter.
  GE::Vertex e, f;
  e.x = 0, e.y = 0, e.u = 0, e.v = 0, e.color = 0xffff'ffff;
  f.x = 16, f.y = 16, f.u = 16, f.v = 16, f.clip[3] = 4, f.color = 0xffff'ffff;
  c.ge.rectangle(pixel, &texture, e, f, true);
  CHECK(c.pixel(7, 0) & 0xff, 2);
  CHECK(c.pixel(0, 15) >> 8 & 0xff, 3);
  c.ge.rectangle(pixel, &texture, e, f, false);
  CHECK(c.pixel(7, 0) & 0xff, 7);
  //Turned (corners bottom-left and top-right): v runs across x and u down y, the same way. At (7, 0) v is 2.89, as u
  //was above, and u = (4 + (0 - 4) * 0.03125) / (1 - 0.75 * 0.46875) = 5.98.
  GE::Vertex g, h;
  g.x = 0, g.y = 16, g.u = 0, g.v = 0, g.color = 0xffff'ffff;
  h.x = 16, h.y = 0, h.u = 16, h.v = 16, h.clip[3] = 4, h.color = 0xffff'ffff;
  c.ge.rectangle(pixel, &texture, g, h, true);
  CHECK(c.pixel(7, 0) & 0xffff, 0x0205);
  //A corner with no w in front of the camera: 2D's stepping.
  f.clip[3] = -1;
  c.ge.rectangle(pixel, &texture, e, f, true);
  CHECK(c.pixel(7, 0) & 0xff, 7);
}

//Fog: FOG1 1, FOG2 0.5: a vertex at view z 0 keeps half its color, (0 + 1) * 0.5 = 0.5, 128 of 255:
//red (0 * 128 + 255 * 127 + 255) / 256 = 127 from the fog color; green (255 * 128 + 255) / 256 = 128. At z 1, fog 1:
//none; at z -0.9, 0.05: 12 of 255. Not in through mode. Across a triangle the fog is blended from the corners: from
//1 (z 1) at two to 0 (z -1) at the third, at pixel (7, 0) (the third's weight 0.46484375) it's 0.53515625, 137. A
//sprite is split at its middle column, each half taking the fog of the corner on the other side, whichever corner
//comes first.
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
  CHECK(c.pixel(7, 0), u32((255 * 136 + 255) / 256) << 8 | u32((255 * 119 + 255) / 256));  //at (7.5, 0.5)
  for(bool first : {true, false}) {  //no fog (z 1) at the left corner, all fog (z -1) at the right, either order
    c.clear();
    V3 left{0, 0, 0xff00'ff00, 0, 0, 1}, right{0, 0, 0xff00'ff00, 8, 4, -1};
    if(first) c.draw(GE::Sprites, {left, right});
    else c.draw(GE::Sprites, {right, left});
    CHECK(c.pixel(3, 1), 0x0000'00ff);  //the left half: the right corner's fog, all of it
    CHECK(c.pixel(4, 1), 0x0000'ff00);  //the right half: the left corner's, none
  }
  //Where the middle falls: halfway, rounded down, and a column whose middle (8/16 in) is at most a sixteenth left of
  //it is the right half's. From x 0 to 7.125 the middle is 57 sixteenths and column 3's middle 56; so too from 0 to
  //7.1875 (57.5, rounded down).
  for(float right : {7.125f, 7.1875f}) {
    c.clear();
    c.draw(GE::Sprites, {{0, 0, 0xff00'ff00, 0, 0, 1}, {0, 0, 0xff00'ff00, right, 4, -1}});
    CHECK(c.pixel(3, 1), 0x0000'ff00);  //the right half: the left corner's fog, none
    CHECK(c.pixel(2, 1), 0x0000'00ff);  //the left half: the right corner's, all
  }
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



//Lighting: vertices with normals (type 0x1ff: float texture coordinates, 8888 color, float normal, float position),
//drawn as points, whose pixels show their lit colors. A color c counts as 2c + 1: two colors multiply and shift down
//10 bits; with a light's share (256ths, rounded up), 18.
struct VN { float u, v; u32 color; float nx, ny, nz, x, y, z; };
static auto drawLit(Scene& c, std::initializer_list<VN> vertices) -> void {
  c.ge.commands[GE::VertexType] = 0x1ff;
  u32 at = VertexData3D;
  for(auto& vertex : vertices) {
    for(float value : {vertex.u, vertex.v}) c.memory.write(4, at, Scene::bits(value)), at += 4;
    c.memory.write(4, at, vertex.color), at += 4;
    for(float value : {vertex.nx, vertex.ny, vertex.nz, vertex.x, vertex.y, vertex.z}) {
      c.memory.write(4, at, Scene::bits(value)), at += 4;
    }
  }
  c.ge.vertexAddress = VertexData3D;
  c.ge.primitive(GE::Points, vertices.size());
}
static auto litPoint(Scene& c, float nx, float ny, float nz, u32 color = 0xffff'ffff) -> u32 {
  c.clear();
  drawLit(c, {{0, 0, color, nx, ny, nz, 5, 5, 0}});
  return c.pixel(5, 5);
}

//The ambient part: no lights, material ambient (200, 100, 50, 255) times ambient light (255, 128, 0, 255):
//401 * 511 >> 10 = 200, 201 * 257 >> 10 = 50, 101 * 1 >> 10 = 0, alpha 255; plus emissive (10, 20, 30).
static auto draw3dLightingAmbient() -> void {
  Scene c;
  c.ge.commands[GE::LightingEnable] = 1;
  c.ge.commands[GE::AmbientColor] = 0x32'64c8, c.ge.commands[GE::AmbientAlpha] = 0xff;
  c.ge.commands[GE::AmbientLightColor] = 0x00'80ff, c.ge.commands[GE::AmbientLightAlpha] = 0xff;
  c.ge.commands[GE::MaterialEmissive] = 0x1e'140a;
  CHECK(litPoint(c, 0, 0, 1), 0x001e'46d2);  //(the alpha written is the stencil, 0)
  c.ge.commands[GE::MaterialColor] = 1;      //the vertex's red stands for the ambient: 511 * 511 >> 10 + 10, held to 255
  CHECK(litPoint(c, 0, 0, 1, 0xff00'00ff), 0x001e'14ff);
}

//A directional light (toward +z: its position (0, 0, 2) is only a direction), white diffuse, on a material diffuse
//(128, 64, 0): squarely, 511 * 257 * 256 >> 18 = 128 and 64; at a cosine of 0.8, a share of 205: 102 and 51 (the
//normal made one long first); from behind (NORMAL_REVERSE), nothing. With MATERIAL_COLOR bit 1 the vertex's color
//(red) is the diffuse. A "powered" diffuse (kind 2) raises the cosine to the coefficient, 2: 0.75 becomes 0.5, a
//share of 128: 64 and 32.
static auto draw3dLightingDiffuse() -> void {
  Scene c;
  c.ge.commands[GE::LightingEnable] = 1;
  c.ge.commands[GE::LightEnable0] = 1;
  c.ge.commands[GE::LightType0] = 0;  //directional; ambient and diffuse
  c.ge.commands[GE::Light0X + 2] = f24(2);
  c.ge.commands[GE::Light0Ambient + 1] = 0xff'ffff;  //diffuse white
  c.ge.commands[GE::MaterialDiffuse] = 0x00'4080;
  CHECK(litPoint(c, 0, 0, 1), 0x0000'4080);
  c.ge.commands[GE::LightEnable0] = 0;  //off: nothing
  CHECK(litPoint(c, 0, 0, 1), 0);
  c.ge.commands[GE::LightEnable0] = 1;
  CHECK(litPoint(c, 0.6f, 0, 0.8f), 0x0000'3366);
  CHECK(litPoint(c, 1.2f, 0, 1.6f), 0x0000'3366);
  c.ge.commands[GE::NormalReverse] = 1;
  CHECK(litPoint(c, 0, 0, 1), 0);
  c.ge.commands[GE::NormalReverse] = 0;
  c.ge.commands[GE::LightType0] = 2;  //powered diffuse
  c.ge.commands[GE::MaterialSpecularCoefficient] = f24(2);
  CHECK(litPoint(c, 0.6614378f, 0, 0.75f), 0x0000'2040);
  c.ge.commands[GE::LightType0] = 0;
  //a share rounded up: at a cosine of 0.501, 256 * 0.501 = 128.26 makes 129: on a white material,
  //261121 * 129 >> 18 = 128 (128 would give 127). In 256ths, where PPSSPP has 512ths and one more, as a PSP showed
  //(round 3): at a cosine of 0.28 a share of 72, 261121 * 72 >> 18 = 71 (PPSSPP's 145 would give 72); red 32 at
  //0.8, 511 * 65 * 205 >> 18 = 25
  c.ge.commands[GE::MaterialDiffuse] = 0xff'ffff;
  CHECK(litPoint(c, 0.8654485f, 0, 0.501f), 0x0080'8080);
  CHECK(litPoint(c, 0.96f, 0, 0.28f), 0x0047'4747);
  c.ge.commands[GE::MaterialDiffuse] = 0x00'0020;
  CHECK(litPoint(c, 0.6f, 0, 0.8f), 0x0000'0019);
  c.ge.commands[GE::MaterialDiffuse] = 0x00'4080;
  c.ge.commands[GE::MaterialColor] = 2;
  CHECK(litPoint(c, 0, 0, 1, 0xff00'00ff), 0x0000'00ff);
}

//A point light 2 above, fading as 1 / (linear d): half, a share of 128: 261121 * 128 >> 18 = 127. A spotlight there
//pointing along +z (the cosine with the direction to the light 1, past the cutoff 0.5): full; pointing along -z: none.
static auto draw3dLightingPointAndSpot() -> void {
  Scene c;
  c.ge.commands[GE::LightingEnable] = 1;
  c.ge.commands[GE::LightEnable0] = 1;
  c.ge.commands[GE::LightType0] = 1 << 8;  //a point
  c.ge.commands[GE::Light0X] = f24(5), c.ge.commands[GE::Light0X + 1] = f24(5), c.ge.commands[GE::Light0X + 2] = f24(2);
  c.ge.commands[GE::Light0ConstantAttenuation + 1] = f24(1);  //linear
  c.ge.commands[GE::Light0Ambient + 1] = 0xff'ffff;
  c.ge.commands[GE::MaterialDiffuse] = 0xff'ffff;
  CHECK(litPoint(c, 0, 0, 1), 0x007f'7f7f);
  c.ge.commands[GE::LightType0] = 2 << 8;  //a spotlight
  c.ge.commands[GE::Light0ConstantAttenuation] = f24(1), c.ge.commands[GE::Light0ConstantAttenuation + 1] = 0;
  c.ge.commands[GE::Light0CutoffAttenuation] = f24(0.5f);
  c.ge.commands[GE::Light0ExponentAttenuation] = f24(1);
  c.ge.commands[GE::Light0DirectionX + 2] = f24(1);
  CHECK(litPoint(c, 0, 0, 1), 0x00ff'ffff);
  c.ge.commands[GE::Light0DirectionX + 2] = f24(-1);
  CHECK(litPoint(c, 0, 0, 1), 0);
  //A spotlight darkens where the cosine with its direction is below 0 and its cutoff lets that in (PPSSPP's reading,
  //carried over to 256ths; not measured): pointing away, cutoff -1, the strength -1, a share of -256. From emissive
  //white plus white ambient (255 + (511 * 511 >> 10) = 510), the light's black ambient takes
  //1 * 511 * -256 >> 18 = -1 and its white diffuse 261121 * -256 >> 18 = -256: 253.
  c.ge.commands[GE::Light0CutoffAttenuation] = f24(-1);
  c.ge.commands[GE::MaterialEmissive] = 0xff'ffff;
  c.ge.commands[GE::AmbientColor] = 0xff'ffff;
  c.ge.commands[GE::AmbientLightColor] = 0xff'ffff;
  CHECK(litPoint(c, 0, 0, 1), 0x00fd'fdfd);
}

//The shine: a directional light toward +z that shines (white specular, no diffuse), the viewer along +z, coefficient
//2. With the normal at a cosine of 0.75 to the half-way direction, the GE's quick power makes 0.75 squared 0.5 (not
//0.5625): a share of 128, 127. A coefficient of 1.03125 keeps only the top four bits of its fraction: 1, so 0.75, a
//share of 192, 191 (not 189). With the view turned so the viewer is along +x, the half-way direction is between +z
//and +x: a normal there takes the whole shine. Kept apart (LIGHT_MODE 1), the shine is added after the texture
//(replace: texel (0, 0), blue 0x80): (127, 127, 255); added in, the texture replaces it.
static auto draw3dLightingSpecular() -> void {
  Scene c;
  c.ge.commands[GE::LightingEnable] = 1;
  c.ge.commands[GE::LightEnable0] = 1;
  c.ge.commands[GE::LightType0] = 1;  //directional; ambient, diffuse and specular
  c.ge.commands[GE::Light0X + 2] = f24(1);
  c.ge.commands[GE::Light0Ambient + 2] = 0xff'ffff;  //specular white
  c.ge.commands[GE::MaterialSpecular] = 0xff'ffff;
  c.ge.commands[GE::MaterialSpecularCoefficient] = f24(2);
  CHECK(litPoint(c, 0.6614378f, 0, 0.75f), 0x007f'7f7f);
  c.ge.commands[GE::MaterialSpecularCoefficient] = f24(1.03125f);
  CHECK(litPoint(c, 0.6614378f, 0, 0.75f), 0x00bf'bfbf);
  //the view: world x to view z, y to y, z to -x, moved so the vertex stays where it was
  u32 turned[12] = {0, 0, f24(1), 0, f24(1), 0, f24(-1), 0, 0, f24(5), 0, f24(-5)};
  for(u32 n = 0; n < 12; n++) c.ge.view[n] = turned[n];
  CHECK(litPoint(c, 0.70710677f, 0, 0.70710677f), 0x00ff'ffff);
  for(u32 n = 0; n < 12; n++) c.ge.view[n] = n == 0 || n == 4 || n == 8 ? f24(1) : 0;
  c.ge.commands[GE::MaterialSpecularCoefficient] = f24(2);
  c.texture();
  CHECK(litPoint(c, 0.6614378f, 0, 0.75f), 0x0080'0000);
  c.ge.commands[GE::LightMode] = 1;
  CHECK(litPoint(c, 0.6614378f, 0, 0.75f), 0x00ff'7f7f);
}

//Environment mapping (TEXTURE_MAP_MODE 2): u from light 0 (directional, toward +x), v from light 1 (toward +y), lit
//or not: (the cosine + 1) / 2. Normal (0.6, 0.8, 0): u 0.8, 12.8 texels; v 0.9, 14.4. With light 0 shining, its
//direction is half way to the viewer's: u (0.6 / sqrt 2 + 1) / 2, 11.4 texels.
static auto draw3dEnvironmentMap() -> void {
  Scene c;
  c.texture();
  c.ge.commands[GE::TextureMapMode] = 2;
  c.ge.commands[GE::TextureShadeMapping] = 0 | 1 << 8;
  c.ge.commands[GE::Light0X] = f24(1);
  c.ge.commands[GE::Light0X + 3 + 1] = f24(1);
  CHECK(litPoint(c, 0.6f, 0.8f, 0) & 0xffff, 14 << 8 | 12);
  c.ge.commands[GE::LightType0] = 1;
  CHECK(litPoint(c, 0.6f, 0.8f, 0) & 0xffff, 14 << 8 | 11);
}

//Lines in 3D: put on the screen as triangles' corners are, then drawn as in 2D (from (1.5, 2.5) to (9.5, 2.5):
//columns 1-8), their depth blended along them (2000 to 3000: at column 5, half way, 2500); cut at the near plane
//where they reach past it (to z -3: a third of the way, at x 5.5, so columns 1-4), the new end's color blended a
//third of the way in 256ths as a triangle's (255 * 171 / 256 = 170), the colors then along the rest (at column 4,
//(255 * 16 + 170 * 48) / 64 = 191); with DEPTH_CLIP_ENABLE off, dropped for that z / w; with flat shading, the
//cut-away end's color; fog blended along them (none at z 1, all at z -1: at column 4's middle, 72 of 128
//sixteenths along, 0.4375 left); dropped with an end off the screen.
static auto draw3dLines() -> void {
  Scene c;
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 1.5f, 2.5f, 0}, {0, 0, 0xff00'00ff, 9.5f, 2.5f, 1}});
  for(u32 x = 0; x < 16; x++) CHECK(c.pixel(x, 2) != 0, x >= 1 && x <= 8);
  CHECK(c.depth(1, 2), 2000u);
  CHECK(c.depth(5, 2), 2500u);
  c.clear();
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 1.5f, 2.5f, 0}, {0, 0, 0xff00'0000, 13.5f, 2.5f, -3}});
  for(u32 x = 0; x < 16; x++) CHECK(c.pixel(x, 2) != 0, x >= 1 && x <= 4);
  CHECK(c.pixel(1, 2), 0x0000'00ffu);
  CHECK(c.pixel(4, 2), 0x0000'00bfu);
  c.ge.commands[GE::DepthClipEnable] = 0;
  c.clear();
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 1.5f, 2.5f, 0}, {0, 0, 0xff00'0000, 13.5f, 2.5f, -3}});
  CHECK(c.pixel(2, 2), 0u);
  c.ge.commands[GE::DepthClipEnable] = 1;
  c.ge.commands[GE::ShadeMode] = 0;
  c.clear();
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 1.5f, 2.5f, 0}, {0, 0, 0xff00'ff00, 13.5f, 2.5f, -3}});
  CHECK(c.pixel(1, 2), 0x0000'ff00u);
  c.ge.commands[GE::ShadeMode] = 1;
  c.ge.commands[GE::FogEnable] = 1;
  c.ge.commands[GE::FogEnd] = f24(1);
  c.ge.commands[GE::FogSlope] = f24(0.5f);
  c.ge.commands[GE::FogColor] = 0x00'00ff;
  c.clear();
  c.draw(GE::Lines, {{0, 0, 0xff00'ff00, 0, 0.5f, 1}, {0, 0, 0xff00'ff00, 8, 0.5f, -1}});
  u32 f = u32(0.4375f * 256);
  CHECK(c.pixel(4, 0), u32((255 * f + 255) / 256) << 8 | u32((255 * (255 - f) + 255) / 256));
  c.ge.commands[GE::FogEnable] = 0;
  c.clear();
  c.draw(GE::Lines, {{0, 0, 0xff00'00ff, 1.5f, 2.5f, 0}, {0, 0, 0xff00'00ff, 4096 - 2048, 2.5f, 0}});
  CHECK(c.pixel(2, 2), 0u);
}

//Bounding boxes (draw.cpp's boundingBox()), as pspautotests' gpu/bounding programs recorded on a PSP: one vertex at
//(1, 1, 1) whose clip space position the projection sets, against the 480x272 screen they draw to (viewport 240 and
//-136 about 2048, offset 2048 - 240 and 2048 - 136, depth -32767 about 32767, every matrix but the projection the
//identity): planes' x, y, z and w; viewport's scissor, region, viewport and "cull box" cases; count's counts and the
//vertices that count among many; vertexaddr's addresses moved on. Then through a list: BJUMP jumping (as JUMP,
//BASE and the offset added) only after a box out of sight, and a list stopped between BOUNDING_BOX and BJUMP, saved
//and carried on in another machine.
static auto draw3dBoundingBoxes() -> void {
  System s;
  GE& ge = s.ge;
  auto& c = ge.commands;
  auto reset = [&] {
    for(auto& word : c) word = 0;
    for(auto* m : {ge.world, ge.view, ge.projection}) for(u32 n = 0; n < 16; n++) m[n] = 0;
    for(u32 n : {0, 4, 8}) ge.world[n] = ge.view[n] = f24(1);
    for(u32 n : {0, 5, 10, 15}) ge.projection[n] = f24(1);
    c[GE::ViewportXScale] = f24(240), c[GE::ViewportYScale] = f24(-136), c[GE::ViewportZScale] = f24(-32767);
    c[GE::ViewportXCenter] = f24(2048), c[GE::ViewportYCenter] = f24(2048), c[GE::ViewportZCenter] = f24(32767);
    c[GE::OffsetX] = (2048 - 240) << 4, c[GE::OffsetY] = (2048 - 136) << 4;
    c[GE::Scissor2] = 479 | 271 << 10, c[GE::Region2] = 479 | 271 << 10;
    c[GE::VertexType] = 0x180;  //float position, 3D
  };
  auto vertex = [&](u32 n, float x, float y, float z) {
    for(u32 k : {0u, 1u, 2u}) s.memory.write(4, VertexData3D + n * 12 + k * 4, Scene::bits(k ? k == 1 ? y : z : x));
  };
  auto outside = [&](u32 side, float value, bool clamp = true) {  //the projection's x, y, z or w at value
    ge.projection[side * 5] = f24(value);
    c[GE::DepthClipEnable] = clamp;
    vertex(0, 1, 1, 1);
    ge.vertexAddress = VertexData3D;
    bool out = ge.boundingBox(1);
    ge.projection[side * 5] = f24(1);
    return out;
  };
  reset();
  //planes
  for(u32 side : {0u, 1u, 2u}) {
    CHECK(outside(side, -1.01f), true);
    CHECK(outside(side, -1.0f), false);
    CHECK(outside(side, 1.0f), false);
    CHECK(outside(side, 1.01f), true);
  }
  CHECK(outside(2, -65535.999f, false), false);
  CHECK(outside(2, 65535.999f, false), false);
  CHECK(outside(3, -0.01f), true);
  CHECK(outside(3, -0.0f), true);
  CHECK(outside(3, 65535.999f), false);
  //MIN_Z and MAX_Z don't count; viewport z: past -w or w out of sight with DEPTH_CLIP_ENABLE, in sight without
  c[GE::MinZ] = c[GE::MaxZ] = 0xffff;
  CHECK(outside(2, 1.0f), false);
  c[GE::MinZ] = c[GE::MaxZ] = 0;
  c[GE::ViewportZScale] = f24(16383);
  for(float z : {-2.0f, -3.0f, 2.0f, 3.0f}) CHECK(outside(2, z, true) && !outside(2, z, false), true);
  c[GE::ViewportZScale] = f24(-32767);
  //the scissor rectangle and the drawing region, each way: x 0 (at -1) and 480 (at 1); y 272 (at -1), 0 (at 1)
  auto edges = [&](u32 command1, u32 command2) {
    auto box = [&](u32 left, u32 top, u32 right, u32 bottom) {
      c[command1] = left | top << 10, c[command2] = right | bottom << 10;
    };
    box(240, 0, 479, 271); CHECK(outside(0, -1.0f), true); CHECK(outside(0, 1.0f), false);
    box(0, 0, 0, 271); CHECK(outside(0, -1.0f), false); CHECK(outside(0, 0.1f), true);
    box(0, 0, 479, 135); CHECK(outside(1, -1.0f), true); CHECK(outside(1, 1.0f), false);
    box(0, 136, 479, 271); CHECK(outside(1, -1.0f), false);
    box(0, 271, 479, 271); CHECK(outside(1, 0.1f), true);
    box(0, 0, 479, 271);
  };
  edges(GE::Scissor1, GE::Scissor2);
  edges(GE::Region1, GE::Region2);
  //viewport x (scale 120): at -3, x -120, out of sight; at -2, x 0, in sight though past -w; likewise right
  c[GE::ViewportXScale] = f24(120);
  CHECK(outside(0, -3.0f), true); CHECK(outside(0, -2.0f), false);
  CHECK(outside(0, 3.0f), true); CHECK(outside(0, 2.0f), false);
  //the cull box: held to the 4096-pixel screen, and a pixel's slack left of the left edge
  auto cull = [&](float value, float scale, float center, u32 offset) {
    c[GE::ViewportXScale] = f24(scale), c[GE::ViewportXCenter] = f24(center), c[GE::OffsetX] = offset << 4;
    bool out = outside(0, value);
    c[GE::ViewportXScale] = f24(240), c[GE::ViewportXCenter] = f24(2048), c[GE::OffsetX] = (2048 - 240) << 4;
    return out;
  };
  CHECK(cull(-999, 120, 0, 0), false);
  CHECK(cull(-0.0f, 120, 0, 0), false);
  CHECK(cull(-2, 1, 2, 2), true);
  CHECK(cull(-1, 1, 2, 2), false);
  CHECK(cull(999, 1, 4095, 4095), false);
  CHECK(cull(1, 1, 4095, 4095), false);
  CHECK(cull(999, 1, 4095, 3615), true);
  CHECK(cull(999, 1, 4095, 3616), false);

  //count: 0 (or 0x10000) vertices out of sight; one in sight among many; of 0x101 only the first counts, of
  //0x1000 only 0xe00-0xeff
  auto many = [&](u32 count, u32 insideFrom, u32 insideCount) {
    for(u32 n = 0; n < (count & 0xffff) + 1; n++) {
      bool in = n >= insideFrom && n < insideFrom + insideCount;
      vertex(n, in ? 0 : -999, in ? 0 : -999, in ? 0 : -999);
    }
    ge.vertexAddress = VertexData3D;
    return ge.boundingBox(count & 0xffff);
  };
  CHECK(many(0, 0, 0), true);
  CHECK(many(0x10000, 0, 1), true);
  CHECK(many(0x100, 0xff, 1), false);
  CHECK(many(0x101, 0x100, 1), true);
  CHECK(many(0x101, 0, 1), false);
  CHECK(many(0x101, 1, 0x100), true);
  CHECK(many(0x1000, 0xe00, 1), false);
  CHECK(many(0x1000, 0xeff, 1), false);
  CHECK(many(0x1000, 0xf00, 1), true);
  CHECK(many(0x1000, 0xdff, 1), true);
  //vertexaddr: the vertex address moves on as PRIM's would; with indices, the index address (32-bit indices too)
  vertex(0, 0, 0, 0);
  for(u32 type : {0x180u, 0x100u, 0x80u, 0x80'0180u, 0x0a1u}) {
    c[GE::VertexType] = type;
    ge.vertexAddress = VertexData3D;
    ge.boundingBox(1);
    CHECK(ge.vertexAddress - VertexData3D, ge.vertexFormat().size);
  }
  for(u32 index : {1u, 2u, 3u}) {
    c[GE::VertexType] = 0x180 | index << 11;
    for(u32 n = 0; n < 256; n++) s.memory.write(1, VertexData3D + 0x1000 + n, 0);
    ge.vertexAddress = VertexData3D, ge.indexAddress = VertexData3D + 0x1000;
    CHECK(ge.boundingBox(64), false);
    CHECK(ge.vertexAddress, VertexData3D);
    CHECK(ge.indexAddress - (VertexData3D + 0x1000), 64 * (index == 3 ? 4 : index));
  }

  //in a list: AMBIENT_ALPHA 1, BOUNDING_BOX, BJUMP over AMBIENT_ALPHA 2, as pspautotests' programs; the box in and
  //out of sight; BJUMP's address relative as JUMP's (here to an ORIGIN), and a list stalled before its BJUMP, saved
  //and loaded into another machine, carrying on as it would have
  constexpr u32 List = 0x0894'0000;
  for(bool out : {false, true}) {
    for(bool stalled : {false, true}) {
      reset();
      vertex(0, out ? -999 : 0, 0, 0);
      u32 at = List;
      auto put = [&](u32 command, u32 argument = 0) { s.memory.write(4, at, command << 24 | argument), at += 4; };
      put(GE::Base, VertexData3D >> 8 & 0xf'0000), put(GE::VertexAddress, VertexData3D & 0xff'ffff);
      put(GE::Base, 0);
      put(GE::AmbientAlpha, 1);
      put(GE::Origin);
      put(GE::BoundingBox, 1);
      u32 jump = at;
      put(GE::ConditionalJump, 4 * 4);  //from the ORIGIN, past what follows
      put(GE::AmbientAlpha, 2);
      put(GE::End);
      ge.list = {};
      ge.list.address = List;
      if(stalled) ge.list.stall = jump;
      ge.run(100);
      if(stalled) {
        serializer saved;
        s.memory.serialize(saved);
        ge.serialize(saved);
        System other;
        serializer loading{saved.data(), saved.size()};
        other.memory.serialize(loading);
        CHECK(other.ge.serialize(loading), true);
        other.ge.list.stall = 0;
        other.ge.run(100);
        CHECK(other.ge.commands[GE::AmbientAlpha] & 0xff, out ? 1u : 2u);
      } else {
        CHECK(ge.commands[GE::AmbientAlpha] & 0xff, out ? 1u : 2u);
      }
    }
  }
}

//Four pixels at a time (ares/psp/ge/four.cpp) against one at a time: random primitives, 2D and 3D (in perspective,
//lit now and then with the shine kept apart), triangles, strips, fans and sprites, with random settings of all the
//pixel pipeline and textures read, drawn by two machines alike but for the four-pixel path, over the same random
//VRAM (colors, stencils and depths); the frame and depth buffers must come out the same, byte for byte, and in the
//end all of VRAM. Now and then the settings send a primitive a pixel at a time (the stencil test, a logic operation,
//the depth buffer on the frame buffer, a texture where it draws), which must be the same too.
static auto draw3dFours() -> void {
  constexpr u32 Width = 64, Height = 40, Depth = 0x10'0000, Area = 0x0900'0000, AreaSize = 1 << 20;
  constexpr u32 Bits[8] = {16, 16, 16, 32, 4, 8, 16, 32};
  constexpr u32 Kinds[6] = {GE::Triangles, GE::TriangleStrip, GE::TriangleFan, GE::Sprites, GE::Sprites,
                            GE::Triangles};
  std::mt19937 random{20261007};
  //(each value taken from it in a statement of its own, so every compiler takes them in the same order)
  auto below = [&](u32 n) { return u32(random() % n); };
  auto chance = [&](u32 percent) -> u32 { return below(100) < percent; };
  auto real = [&](float low, float high) { return low + (high - low) * float(below(1 << 20)) / float(1 << 20); };
  //a value of fields taken one after another: each {where it goes, how many values it has}
  auto fields = [&](std::initializer_list<std::pair<u32, u32>> parts) {
    u32 value = 0;
    for(auto [shift, count] : parts) value |= below(count) << shift;
    return value;
  };
  auto mask = [&](u32 percent) { return chance(percent) ? 0xffu : below(256); };  //(all ones, mostly)
  Scene fours, single;
  single.ge.fourPixels = false;
  std::vector<u8> bytes(AreaSize), vram(Memory::VRAMSize);
  for(auto& byte : bytes) byte = random();
  for(auto& byte : vram) byte = random();
  for(Scene* c : {&fours, &single}) {
    c->memory.copyIn(Area, bytes.data(), AreaSize);
    c->memory.copyIn(VRAM3D, vram.data(), Memory::VRAMSize);
    //in perspective, w growing with distance (near 0.5, far 10), onto the 64x40 picture
    auto& g = c->ge;
    for(u32 n = 0; n < 16; n++) g.projection[n] = 0;
    g.projection[0] = g.projection[5] = f24(1), g.projection[10] = f24(-10.5f / 9.5f), g.projection[11] = f24(-1);
    g.projection[14] = f24(-10.0f / 9.5f);
    g.commands[GE::ViewportXScale] = f24(28), g.commands[GE::ViewportYScale] = f24(-18);
    g.commands[GE::ViewportXCenter] = f24(2048 + 32), g.commands[GE::ViewportYCenter] = f24(2048 + 20);
    g.commands[GE::ViewportZScale] = f24(30000), g.commands[GE::ViewportZCenter] = f24(32000);
  }
  u32 differing = 0;
  for(u32 n = 0; n < 2500 && !differing; n++) {
    std::vector<std::pair<u32, u32>> commands;  //for both machines
    auto set = [&](u32 command, u32 value) { commands.push_back({command, value}); };
    set(GE::FrameBufferWidth, Width);
    set(GE::FrameBufferPixelFormat, below(4));
    set(GE::DepthBufferPointer, chance(5) ? 0 : Depth);
    set(GE::DepthBufferWidth, Width);
    set(GE::ClearMode, chance(8) ? 1 | below(8) << 8 : 0);
    set(GE::AlphaTestEnable, chance(40));
    u32 alphaTest = fields({{0, 8}, {8, 256}});
    set(GE::AlphaTest, alphaTest | mask(70) << 16);
    set(GE::DepthTestEnable, chance(60));
    set(GE::DepthTest, below(8));
    set(GE::DepthMask, chance(30));
    set(GE::StencilTestEnable, chance(8));
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
    set(GE::LogicOpEnable, chance(4));
    set(GE::LogicOp, below(16));
    set(GE::MaskColor, chance(80) ? 0 : random() & 0xff'ffff);
    set(GE::MaskAlpha, chance(80) ? 0 : below(256));
    set(GE::FogEnable, chance(40));
    set(GE::FogColor, random() & 0xff'ffff);
    set(GE::FogEnd, f24(real(-3, 6)));
    set(GE::FogSlope, f24(real(-2, 4)));
    set(GE::MinZ, chance(70) ? 0 : below(40000));
    set(GE::MaxZ, chance(70) ? 0xffff : 20000 + below(45536));
    u32 left = chance(30) ? below(16) : 0;
    u32 top = chance(30) ? below(16) : 0;
    u32 right = chance(30) ? left + below(Width - left) : Width - 1;
    u32 bottom = chance(30) ? top + below(Height - top) : Height - 1;
    set(GE::Scissor1, left | top << 10);
    set(GE::Scissor2, right | bottom << 10);
    set(GE::ShadeMode, chance(75));
    u32 textured = chance(70), format = below(8), widthBits = below(8), heightBits = below(8);
    set(GE::TextureMappingEnable, textured);
    if(textured) {
      u32 least = 128 / Bits[format];  //(a row of at least 16 bytes)
      u32 bufferWidth = std::max(1u << widthBits, least) + (chance(20) ? least * below(4) : 0);
      u32 address = chance(10) ? VRAM3D : Area + below((AreaSize - 0x10'0000) / 16) * 16;  //(where it draws)
      set(GE::TextureAddress0, address & 0xff'fff0);
      set(GE::TextureBufferWidth0, (address >> 24 & 0xf) << 16 | bufferWidth);
      set(GE::TextureSize0, heightBits << 8 | widthBits);
      set(GE::TextureFormat, format);
      set(GE::TextureMode, chance(30));
      set(GE::TextureWrap, fields({{0, 2}, {8, 2}}));
      set(GE::TextureFilter, fields({{0, 2}, {8, 2}}));
      u32 function = fields({{0, 8}, {8, 2}});
      set(GE::TextureFunction, function | chance(20) << 16);
      set(GE::TextureEnvironmentColor, random() & 0xff'ffff);
      if(format >= 4) {
        u32 palette = Area + below((AreaSize - 1024) / 16) * 16;
        set(GE::ClutAddress, palette & 0xff'fff0);
        set(GE::ClutAddressUpper, palette >> 8 & 0xf'0000);
        u32 shift = below(chance(70) ? 1 : 32);
        u32 clutFormat = below(4) | shift << 2;
        clutFormat |= mask(70) << 8;
        set(GE::ClutFormat, clutFormat | below(32) << 16);
        set(GE::ClutLoad, 32);
      }
    }
    bool flat = chance(35);  //2D: through mode
    u32 lit = !flat && chance(15);
    set(GE::LightingEnable, lit);
    if(lit) {
      set(GE::LightMode, chance(70));
      set(GE::LightEnable0, 1);
      set(GE::LightType0, below(3));  //directional: ambient and diffuse, those and the shine, or powered
      for(u32 k = 0; k < 3; k++) set(GE::Light0DirectionX + k, f24(real(-1, 1)));
      for(u32 k = 0; k < 3; k++) set(GE::Light0Ambient + k, random() & 0xff'ffff);
      set(GE::MaterialSpecular, random() & 0xff'ffff);
      set(GE::MaterialDiffuse, random() & 0xff'ffff);
      set(GE::MaterialColor, below(8));
      set(GE::MaterialSpecularCoefficient, f24(real(0, 12)));
    }
    for(Scene* c : {&fours, &single}) {
      for(auto [command, value] : commands) c->ge.commands[command] = value;
      if(textured && format >= 4) c->ge.loadClut();
    }

    u32 kind = Kinds[below(6)];
    u32 count = kind == GE::Sprites ? 2 * (1 + below(3)) : kind == GE::Triangles ? 3 * (1 + below(3)) : 4 + below(3);
    //vertex type 0x1ff (float texture coordinates, 8888 color, float normal, float position), through mode in 2D
    u32 at = VertexData3D;
    //(often every vertex of the primitive the same color, white or not: blended, it lands on whole numbers)
    u32 one = chance(30) ? (chance(50) ? 0xffff'ffff : u32(random())) : 0;
    for(u32 k = 0; k < count; k++) {
      float u = flat ? real(-4, float(4 << widthBits)) : real(-0.2f, 1.3f);
      float v = flat ? real(-4, float(4 << heightBits)) : real(-0.2f, 1.3f);
      u32 color = one ? one : chance(20) ? 0xffff'ffff : u32(random());
      float normal[3], position[3];
      for(auto& value : normal) value = real(-1, 1);
      if(flat) {
        float steps = chance(50) ? 16 : 1;  //(on sixteenths, or whole pixels)
        position[0] = std::round(real(-8, Width + 8) * steps) / steps;
        position[1] = std::round(real(-8, Height + 8) * 16) / 16;
        position[2] = real(0, 65535);
      } else {
        position[2] = real(-4, -0.6f);
        position[0] = real(-1.3f, 1.3f) * -position[2];
        position[1] = real(-1.3f, 1.3f) * -position[2];
      }
      for(Scene* c : {&fours, &single}) {
        u32 to = at;
        for(float value : {u, v}) c->memory.write(4, to, Scene::bits(value)), to += 4;
        c->memory.write(4, to, color), to += 4;
        for(float value : normal) c->memory.write(4, to, Scene::bits(value)), to += 4;
        for(float value : position) c->memory.write(4, to, Scene::bits(value)), to += 4;
      }
      at += 36;
    }
    for(Scene* c : {&fours, &single}) {
      c->ge.commands[GE::VertexType] = 0x1ff | (flat ? 1 << 23 : 0);
      c->ge.vertexAddress = VertexData3D;
      c->ge.primitive(kind, count);
    }
    auto same = [&](u32 address, u32 size) {
      return !std::memcmp(fours.memory.vram.data() + address, single.memory.vram.data() + address, size);
    };
    if(!same(0, Width * Height * 4) || !same(Depth, 0x4000)) {
      differing++;
      std::printf("  case %u: primitive %u of %u vertices (%s), differs\n", n, kind, count, flat ? "2D" : "3D");
    }
  }
  CHECK(differing, 0u);
  CHECK(fours.memory.vram == single.memory.vram, true);
}

auto draw3dTests() -> Tests {
  return {
    {"draw3d transform", draw3dTransform}, {"draw3d outside", draw3dOutside}, {"draw3d clipping", draw3dClipping},
    {"draw3d culling", draw3dCulling}, {"draw3d perspective", draw3dPerspective}, {"draw3d fog", draw3dFog},
    {"draw3d depth range", draw3dDepthRange}, {"draw3d texture coordinates", draw3dTextureCoordinates},
    {"draw3d morph and skin", draw3dMorphAndSkin}, {"draw3d lighting ambient", draw3dLightingAmbient},
    {"draw3d lighting diffuse", draw3dLightingDiffuse}, {"draw3d lighting point and spot", draw3dLightingPointAndSpot},
    {"draw3d lighting specular", draw3dLightingSpecular}, {"draw3d environment map", draw3dEnvironmentMap},
    {"draw3d lines", draw3dLines}, {"draw3d bounding boxes", draw3dBoundingBoxes},
    {"draw3d four pixels at a time against one", draw3dFours},
  };
}

}
