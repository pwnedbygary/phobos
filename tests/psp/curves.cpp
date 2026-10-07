//Curved surfaces (ares/psp/ge curves.cpp): BEZIER and SPLINE. pspautotests' gpu/primitives/bezier and spline drawn
//again, command for command, and checked against what they recorded on a PSP (where each patch lands, the points'
//places and colors, flat shading's colors, the lines, the texels); the knots, a spline with open ends drawn as the
//same Bezier patch, and round 3 of tools/psp-measure's patches against the owner's PSP; lighting on normals made from
//the slopes, made-up texture coordinates and culling; the same pictures on 1 to 8 threads and four pixels at a time
//or one; a list stopped part way and carried on from a saved state; and malformed surfaces, bounded.
#include "kernel-machine.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>

namespace allegrex_test::psp {

namespace {

constexpr u32 PatchList = 0x0894'0000, PatchPoints = 0x0896'0000, PatchTexture = 0x0898'0000;
constexpr u32 PatchVRAM = Memory::VRAMBase, PatchDepth = 0x8'8000;  //pspautotests' frame and depth buffers
constexpr u32 PatchDepthSeen = Memory::VRAMBase + 3 * Memory::VRAMSize + PatchDepth;  //depth as the GE has it

auto f24(float value) -> u32 { return std::bit_cast<u32>(value) >> 8; }

//A control point as pspautotests' programs lay them out: Vertex_C8888_P16 (vertex type 0x80011c), Vertex_UV16_P16
//(0x800102, its u and v in color's place); a float one, u, v, color, x, y, z (0x80019f, 0x19f in 3D); or a position
//alone (0x180).
struct Point { u32 color; s16 x, y, z, pad; };
struct FloatPoint { float u, v; u32 color; float x, y, z; };
struct Position { float x, y, z; };

//The GE alone, drawing pspautotests' way: a 512-pixel-wide 8888 frame buffer at VRAM's start, its depth buffer
//after it, the scissor the screen's 480x272; display lists written into memory and run.
struct Surface {
  System s;
  GE& ge = s.ge;
  Memory& memory = s.memory;
  u32 at = PatchList, data = PatchPoints;
  Surface() {
    put(GE::FrameBufferPointer, 0), put(GE::FrameBufferWidth, 512), put(GE::FrameBufferPixelFormat, 3);
    put(GE::DepthBufferPointer, PatchDepth), put(GE::DepthBufferWidth, 512);
    put(GE::Scissor2, 479 | 271 << 10), put(GE::Region2, 479 | 271 << 10);
    put(GE::ShadeMode, 1), put(GE::MaxZ, 0xffff);
    put(GE::OffsetX, (2048 - 240) << 4), put(GE::OffsetY, (2048 - 136) << 4);
    put(GE::ViewportXScale, f24(240)), put(GE::ViewportYScale, f24(-136)), put(GE::ViewportZScale, f24(-32767.5f));
    put(GE::ViewportXCenter, f24(2048)), put(GE::ViewportYCenter, f24(2048)), put(GE::ViewportZCenter, f24(32767.5f));
    put(GE::TextureScaleU, f24(1)), put(GE::TextureScaleV, f24(1));
    for(u32 n : {0, 4, 8}) ge.world[n] = ge.view[n] = f24(1);
    for(u32 n : {0, 5, 10, 15}) ge.projection[n] = f24(1);
  }
  auto put(u32 command, u32 argument = 0) -> void {
    memory.write(4, at, command << 24 | (argument & 0xff'ffff));
    at += 4;
  }
  auto to(u32 command, u32 target) -> void { put(GE::Base, target >> 8 & 0xf'0000), put(command, target); }
  //bytes into memory (control points, indices), returning where they went
  auto store(const void* bytes, u32 size) -> u32 {
    u32 address = data;
    for(u32 n = 0; n < size; n++) memory.write(1, data++, static_cast<const u8*>(bytes)[n]);
    data = (data + 15) & ~15u;
    return address;
  }
  //Runs what's been written, from where the last run stopped.
  auto run() -> void {
    u32 from = at;
    put(GE::Finish), put(GE::End);
    if(ge.list.address < PatchList || ge.list.address >= from) ge.list.address = PatchList;
    ge.run(1 << 24);
    ge.flush();
  }
  //sceGuDrawBezier and sceGuDrawSpline: the vertex type, indices and control points (if given), then the command
  template<typename T> auto patch(u32 command, u32 type, u32 argument, const std::vector<T>& points,
                                  const void* indices = nullptr, u32 indexBytes = 0) -> void {
    put(GE::VertexType, type);
    if(indices) to(GE::IndexAddress, store(indices, indexBytes));
    if(!points.empty()) to(GE::VertexAddress, store(points.data(), points.size() * sizeof(T)));
    put(command, argument);
  }
  auto pixel(u32 x, u32 y) -> u32 { return memory.read(4, PatchVRAM + (y * 512 + x) * 4) & 0xff'ffff; }
  auto depth(u32 x, u32 y) -> u32 { return memory.read(2, PatchDepthSeen + (y * 512 + x) * 2); }
  auto clear() -> void { memory.fill(PatchVRAM, 0, 0x11'0000); }
  //the box around the pixels drawn in a 40x40 cell from (x, y): left, right, top, bottom, how many (none: 0, -1)
  struct Box { s32 left, right, top, bottom, count; auto operator==(const Box&) const -> bool = default; };
  auto box(u32 x, u32 y) -> Box {
    Box b{0, -1, 0, -1, 0};
    for(u32 j = y; j < y + 40; j++) {
      for(u32 i = x; i < x + 40; i++) {
        if(!pixel(i, j)) continue;
        if(!b.count) b = {s32(i), s32(i), s32(j), s32(j), 0};
        b.left = std::min(b.left, s32(i)), b.right = std::max(b.right, s32(i));
        b.top = std::min(b.top, s32(j)), b.bottom = std::max(b.bottom, s32(j)), b.count++;
      }
    }
    return b;
  }
};

//The four columns of pspautotests' grids, 10 pixels apart from x, at the rows ys, row 0 in first's color and the
//rest in rest's.
auto grid(s16 x, std::initializer_list<s16> ys, u32 first, u32 rest) -> std::vector<Point> {
  std::vector<Point> points;
  for(s16 y : ys) {
    for(s16 i = 0; i < 4; i++) points.push_back({points.size() < 4 ? first : rest, s16(x + i * 10), y, 0, 0});
  }
  return points;
}

//A square's four corners (top left, top right, bottom left, bottom right) and the indices pspautotests' programs
//draw them with: each corner twice along both ways.
auto corners(s16 x, s16 y, u32 top, u32 bottom) -> std::vector<Point> {
  return {{top, x, y, 0, 0}, {top, s16(x + 30), y, 0, 0}, {bottom, x, s16(y + 30), 0, 0},
          {bottom, s16(x + 30), s16(y + 30), 0, 0}};
}
template<typename T> auto cornerIndices(T first) -> std::vector<T> {
  std::vector<T> indices;
  for(u32 n : {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 3, 3, 2, 2, 3, 3}) indices.push_back(T(first + n));
  return indices;
}

//pspautotests' gpu/primitives/bezier (spline: false) or spline: the same commands, in the same order, with the same
//control points. Its own setup (common's initDisplay and startFrame) is Surface's.
auto drawPrimitivesProgram(Surface& c, bool spline) -> void {
  u32 command = spline ? GE::Spline : GE::Bezier;
  auto size = [&](u32 ucount, u32 vcount, u32 uedge = 3, u32 vedge = 3) {
    return ucount | vcount << 8 | (spline ? uedge << 16 | vedge << 18 : 0);
  };
  constexpr u32 Colored = 0x80'011c, Textured = 0x80'0102;
  //the texture: 2x2 8-bit indices (texel (1, 1) 1, the rest 0) into red, green, blue and yellow; nearest, decal
  const u8 image[4][16] = {{0, 0}, {0, 1}, {0, 1}, {0, 0}};
  const u32 palette[4] = {0xff00'00ff, 0xff00'ff00, 0xffff'0000, 0xff00'ffff};
  for(u32 n = 0; n < 64; n++) c.memory.write(1, PatchTexture + n, image[n / 16][n % 16]);
  for(u32 n = 0; n < 4; n++) c.memory.write(4, PatchTexture + 0x100 + n * 4, palette[n]);
  c.put(GE::TextureFormat, 5), c.put(GE::TextureFilter, 0), c.put(GE::TextureFunction, 1);
  c.put(GE::ClutFormat, 3 | 0xff << 8), c.to(GE::TextureAddress0, PatchTexture);
  c.put(GE::TextureBufferWidth0, (PatchTexture >> 24) << 16 | 16), c.put(GE::TextureSize0, 1 << 8 | 1);
  c.put(GE::ClutAddressUpper, (PatchTexture + 0x100) >> 8 & 0xf'0000), c.put(GE::ClutAddress, PatchTexture + 0x100);
  c.put(GE::ClutLoad, 1);
  c.put(GE::PatchPrimitive, 0), c.put(GE::PatchDivision, 16 | 16 << 8), c.put(GE::PatchFacing, 0);

  c.put(GE::TextureMappingEnable, 0);
  c.patch(command, Colored, size(4, 4), grid(10, {10, 20, 30, 40}, 0xff00'00ff, 0xff00'00ff));
  //texture coordinates: u and v 0, 1, 2, 2 along each way
  std::vector<std::array<s16, 5>> uvs;
  for(s16 j = 0; j < 4; j++) {
    for(s16 i = 0; i < 4; i++) {
      uvs.push_back({std::min<s16>(i, 2), std::min<s16>(j, 2), s16(50 + i * 10), s16(10 + j * 10), 0});
    }
  }
  c.put(GE::TextureMappingEnable, 1);
  c.patch(command, Textured, size(4, 4), uvs);
  c.put(GE::TextureMappingEnable, 0);
  c.patch(command, Colored, size(4, 4), grid(90, {10, 20, 30, 40}, 0xff00'00ff, 0xffff'0000));
  c.patch(command, Colored, size(4, 4), grid(130, {10, 30, 35, 40}, 0xff00'00ff, 0xff00'ffff));
  //the other primitives (PATCH_PRIMITIVE 1-4)
  for(u32 kind : {1, 2, 3, 4}) {
    c.put(GE::PatchPrimitive, kind), c.put(GE::PatchDivision, 4 | 4 << 8);
    c.patch(command, Colored, size(4, 4), grid(s16(130 + 40 * kind), {10, 30, 35, 40}, 0xff00'00ff, 0xff00'ffff));
  }
  c.put(GE::PatchPrimitive, 0), c.put(GE::ShadeMode, 0);
  c.put(GE::PatchDivision, 1 | 1 << 8);
  c.patch(command, Colored, size(4, 4), grid(330, {10, 30, 35, 40}, 0xff00'00ff, 0xffff'00ff));
  c.put(GE::PatchDivision, 1 | 2 << 8);
  c.patch(command, Colored, size(4, 4), grid(370, {10, 30, 35, 40}, 0xff00'00ff, 0xffff'00ff));
  c.put(GE::PatchDivision, 2 | 2 << 8);
  c.patch(command, Colored, size(4, 4), grid(410, {10, 30, 35, 40}, 0xff00'00ff, 0xffff'00ff));
  //counts that aren't 3N + 1 (a spline takes them all)
  c.put(GE::PatchDivision, 4 | 4 << 8), c.put(GE::ShadeMode, 1);
  c.patch(command, Colored, size(4, 2), grid(10, {50, 80}, 0xff00'00ff, 0xffff'ff00));
  c.patch(command, Colored, size(4, 5), grid(50, {50, 55, 60, 70, 80}, 0xff00'00ff, 0xffff'ff00));
  c.patch(command, Colored, size(4, 8), grid(90, {50, 54, 58, 62, 66, 70, 75, 80}, 0xff00'00ff, 0xffff'ff00));
  //indices of 8, 16 and 32 bits
  auto bytes = cornerIndices<u8>(0);
  auto halves = cornerIndices<u16>(0);
  auto words = cornerIndices<u32>(0);
  c.patch(command, Colored | 1 << 11, size(4, 4), corners(130, 50, 0xffff'0000, 0xffff'ffff), bytes.data(), 16);
  c.patch(command, Colored | 2 << 11, size(4, 4), corners(170, 50, 0xffff'0000, 0xff77'7777), halves.data(), 32);
  c.patch(command, Colored | 3 << 11, size(4, 4), corners(210, 50, 0xffff'0000, 0xff00'7fff), words.data(), 64);
  //the vertex address moving on past the points (none given the second time), and the index address
  auto both = grid(250, {50, 60, 70, 80}, 0xff00'00ff, 0xff00'0077);
  for(auto& point : grid(290, {50, 60, 70, 80}, 0xff00'00ff, 0xff00'7700)) both.push_back(point);
  c.patch(command, Colored, size(4, 4), both);
  c.patch(command, Colored, size(4, 4), std::vector<Point>{});
  auto squares = corners(330, 50, 0xffff'0000, 0xff77'0000);
  for(auto& point : corners(370, 50, 0xffff'0000, 0xff77'7700)) squares.push_back(point);
  auto twice = cornerIndices<u8>(0);
  for(u8 n : cornerIndices<u8>(4)) twice.push_back(n);
  c.patch(command, Colored | 1 << 11, size(4, 4), squares, twice.data(), 32);
  c.patch(command, Colored | 1 << 11, size(4, 4), std::vector<Point>{});
  //PATCH_DIVISION 0
  c.put(GE::ShadeMode, 0), c.put(GE::PatchDivision, 0);
  c.patch(command, Colored, size(4, 4), grid(410, {50, 60, 75, 80}, 0xffff'0000, 0xff00'7777));
  if(spline) {  //each end type along u, then along v (the other way open at both ends)
    c.put(GE::ShadeMode, 1), c.put(GE::PatchDivision, 0);
    const u32 bottoms[6] = {0xff99'99ff, 0xff99'3366, 0xffff'ffcc, 0xff66'0066, 0xffff'8080, 0xff00'66cc};
    for(u32 n = 0; n < 6; n++) {
      c.patch(command, Colored | 1 << 11, n < 3 ? size(4, 4, n, 3) : size(4, 4, 3, n - 3),
              corners(s16(10 + 40 * n), 90, 0xffff'ffff, bottoms[n]), bytes.data(), 16);
    }
  }
  c.run();
}

}

//pspautotests' gpu/primitives/bezier and spline as they recorded on a PSP (their pictures, .expected.bmp, the PSP's
//own frame buffer): where each patch lands, every point, flat shading's colors, the lines, and where the texture's
//green texel starts. (Colors blended across the smooth patches are within a level of the PSP's, as round 3's color
//ramps are: the core blends colors across triangles a little differently. Those aren't checked here.)
static auto curvesPrimitivesPrograms() -> void {
  for(bool spline : {false, true}) {
    Surface c;
    drawPrimitivesProgram(c, spline);
    //each 40x40 cell's box: row 0 (y 10-40), row 1 (y 50-80), and the spline's end types (y 90-120)
    using Box = Surface::Box;
    const Box row0[11] = {{10, 39, 10, 39, 900}, {50, 79, 10, 39, 900}, {90, 119, 10, 39, 900},
                          {130, 159, 10, 39, 900}, {170, 200, 10, 40, 278}, {210, 240, 10, 40, 25},
                          {250, 280, 10, 40, 25}, {290, 319, 10, 39, 900}, {330, 359, 10, 39, 900},
                          {370, 399, 10, 39, 900}, {410, 439, 10, 39, 900}};
    Box row1[11] = {{0, -1, 0, -1, 0}, {50, 79, 50, 69, 600}, {90, 119, 50, 74, 750}};
    if(spline) row1[1] = {50, 79, 50, 79, 900}, row1[2] = {90, 119, 50, 79, 900};
    for(u32 n = 3; n < 11; n++) row1[n] = {s32(10 + 40 * n), s32(39 + 40 * n), 50, 79, 900};
    const Box row2[6] = {{15, 34, 90, 119, 600}, {50, 71, 90, 119, 660}, {97, 119, 90, 119, 690},
                         {130, 159, 95, 114, 600}, {170, 199, 90, 111, 660}, {210, 239, 97, 119, 690}};
    bool boxes = true;
    for(u32 n = 0; n < 11; n++) boxes &= c.box(5 + 40 * n, 5) == row0[n] && c.box(5 + 40 * n, 45) == row1[n];
    for(u32 n = 0; n < 6; n++) boxes &= c.box(5 + 40 * n, 85) == (spline ? row2[n] : Box{0, -1, 0, -1, 0});
    CHECK(boxes, true);
    //points (PATCH_PRIMITIVE 2 and 3): a vertex each, its color worked out and rounded up (green 147.42 is 148)
    const u32 columns[5] = {0, 7, 15, 22, 30}, rows[5] = {10, 22, 30, 36, 40};
    const u32 colors[5] = {0x0000ff, 0x0094ff, 0x00e0ff, 0x00fcff, 0x00ffff};
    bool points = true;
    for(u32 x0 : {210, 250}) {
      for(u32 j = 0; j < 5; j++) for(u32 i = 0; i < 5; i++) points &= c.pixel(x0 + columns[i], rows[j]) == colors[j];
    }
    CHECK(points, true);
    //flat shading: each triangle the color of its last vertex, a strip's (u1, v0) for the top left one. The red
    //pixels (row 0's color) and those of the vertex half way down (magenta's blue 223.125, rounded up), each row
    const u8 redDivided[30] = {29, 28, 26, 25, 23, 22, 21, 19, 18, 16, 15, 13, 12, 10, 9, 7, 6, 5, 3, 2};
    const u8 halfDivided[30] = {1, 2, 4, 5, 7, 8, 9, 11, 12, 14, 15, 17, 18, 20, 21, 23, 24, 25, 27, 28, 30, 27, 24,
                                21, 18, 14, 11, 8, 5, 2};
    const u8 redTwice[30] = {30, 28, 26, 24, 24, 22, 20, 20, 18, 16, 14, 14, 12, 10, 8, 8, 6, 4, 4, 2};
    const u8 halfTwice[30] = {0, 2, 4, 6, 6, 8, 10, 10, 12, 14, 16, 16, 18, 20, 22, 22, 24, 26, 26, 28, 30, 28, 24,
                              20, 18, 14, 12, 8, 4, 2};
    bool flat = true;
    for(u32 y = 10; y < 40; y++) {
      u32 red[3] = {}, half[3] = {}, other[3] = {};
      for(u32 k = 0; k < 3; k++) {
        for(u32 x = 330 + 40 * k; x < 360 + 40 * k; x++) {
          u32 color = c.pixel(x, y);
          if(color == 0x0000ff) red[k]++;
          else if(color == 0xe000ff) half[k]++;
          else if(color != 0xff00ff) other[k]++;
        }
      }
      flat &= red[0] == 39 - y && half[0] == 0 && red[1] == redDivided[y - 10] && half[1] == halfDivided[y - 10];
      flat &= red[2] == redTwice[y - 10] && half[2] == halfTwice[y - 10] && !other[0] && !other[1] && !other[2];
    }
    //and PATCH_DIVISION 0, as 1: blue above the diagonal, the olive of rows 1-3 below
    for(u32 y = 50; y < 80; y++) {
      for(u32 x = 410; x < 440; x++) flat &= c.pixel(x, y) == (x - 410 < 79 - y && y < 79 ? 0xff0000 : 0x007777);
    }
    CHECK(flat, true);
    //lines (PATCH_PRIMITIVE 1): each row band's strip as a line strip, a bit for each pixel lit from x 170
    const u32 lines[31] = {
      0x6040c081, 0x6060c0c1, 0x5050a0a1, 0x485090a1, 0x48489091, 0x44488891, 0x44448889, 0x42428485, 0x41428285,
      0x41418283, 0x40c18183, 0x40c08181, 0x6040c081, 0x5060a0c1, 0x5050a0a1, 0x48489091, 0x44448889, 0x42428485,
      0x41418283, 0x40c18183, 0x6040c081, 0x5060a0c1, 0x485890b1, 0x46448c89, 0x41438287, 0x40c08181, 0x7060e0c1,
      0x4c5898b1, 0x4346868d, 0x40c18183, 0x00400080,
    };
    bool lit = true;
    for(u32 y = 10; y <= 40; y++) {
      u32 mask = 0;
      for(u32 x = 170; x <= 200; x++) mask |= u32(c.pixel(x, y) != 0) << (x - 170);
      lit &= mask == lines[y - 10];
    }
    CHECK(lit, true);
    //the texture: green (texel (1, 1)) from x 60 in rows 20-39, red elsewhere
    bool texels = true;
    for(u32 y = 10; y < 40; y++) {
      for(u32 x = 50; x < 80; x++) texels &= c.pixel(x, y) == (x >= 60 && y >= 20 ? 0x00ff00 : 0x0000ff);
    }
    CHECK(texels, true);
  }
}

//A spline's knots: every way its ends may be cut, with more points than four (pieces of curve meeting), against the
//curve worked out another way (de Boor's algorithm on the points, where the core weighs them by the Cox-de Boor
//recursion). Points at x 16.1 + (0, 0, 120, 120, 120) along u, each piece cut in 4, drawn as points in through mode
//(16.1: no vertex lands on a pixel's edge); a 4x4 spline with both ends open draws exactly the same Bezier patch (as
//round 3 found on a PSP); and a curve's colors round up.
static auto curvesKnots() -> void {
  for(u32 type : {0, 1, 2, 3}) {
    Surface c;
    std::vector<FloatPoint> points;
    const float xs[5] = {16.1f, 16.1f, 136.1f, 136.1f, 136.1f};
    for(u32 j = 0; j < 4; j++) {
      for(u32 i = 0; i < 5; i++) points.push_back({0, 0, 0xffff'ffff, xs[i], 10, 0});
    }
    c.put(GE::PatchPrimitive, 2), c.put(GE::PatchDivision, 4 | 1 << 8);
    c.patch(GE::Spline, 0x80'019f, 5 | 4 << 8 | type << 16 | 3 << 18, points);
    c.run();
    //the curve's x at each vertex, from the knots: a B-spline's weights worked out exactly
    std::vector<u32> lit;
    for(u32 x = 0; x < 200; x++) if(c.pixel(x, 10)) lit.push_back(x);
    f64 knots[9];
    for(u32 n = 0; n < 9; n++) knots[n] = f64(n) - 3;
    if(type & 1) knots[0] = knots[1] = knots[2] = 0;
    if(type & 2) knots[6] = knots[7] = knots[8] = 2;
    std::vector<u32> want;
    for(u32 k = 0; k <= 8; k++) {
      f64 t = k / 4.0;
      u32 span = std::min<u32>(u32(t), 1) + 3;
      //de Boor's algorithm on the points' x
      f64 d[4];
      for(u32 n = 0; n < 4; n++) d[n] = xs[span - 3 + n];
      for(u32 r = 1; r <= 3; r++) {
        for(u32 j = 3; j >= r; j--) {
          u32 i = span - 3 + j;
          f64 alpha = (t - knots[i]) / (knots[i + 4 - r] - knots[i]);
          d[j] = (1 - alpha) * d[j - 1] + alpha * d[j];
        }
      }
      want.push_back(u32(d[3]));
    }
    std::sort(want.begin(), want.end());
    want.erase(std::unique(want.begin(), want.end()), want.end());
    CHECK(lit == want, true);
  }

  //a 4x4 spline with both ends open is the Bezier patch, pixel for pixel; and both ends closed isn't
  auto picture = [](u32 command, u32 argument) {
    Surface c;
    std::vector<FloatPoint> points;
    for(u32 j = 0; j < 4; j++) {
      for(u32 i = 0; i < 4; i++) {
        float x = 100 + i * 60 + (j == 1 || j == 2 ? (i == 0 ? -20.0f : i == 3 ? 20.0f : 0) : 0);
        float y = 20 + j * 60 + (i == 1 || i == 2 ? (j == 0 ? -20.0f : j == 3 ? 20.0f : 0) : 0);
        points.push_back({0, 0, 0xff00'0000 | i * 85 | j * 85 << 8 | (3 - i) * 85 << 16, x, y, 0});
      }
    }
    c.put(GE::PatchPrimitive, 0), c.put(GE::PatchDivision, 6 | 6 << 8);
    c.patch(command, 0x80'019f, argument, points);
    c.run();
    return std::vector<u8>(c.memory.vram.begin(), c.memory.vram.begin() + 512 * 272 * 4);
  };
  auto bezier = picture(GE::Bezier, 4 | 4 << 8);
  CHECK(picture(GE::Spline, 4 | 4 << 8 | 3 << 16 | 3 << 18) == bezier, true);
  CHECK(picture(GE::Spline, 4 | 4 << 8) != bezier, true);

  //colors round up: from red 0 to 255 along u, cut in 8, each vertex a point: 255k / 8 rounded up
  Surface c;
  std::vector<FloatPoint> points;
  for(u32 j = 0; j < 4; j++) {
    for(u32 i = 0; i < 4; i++) points.push_back({0, 0, 0xff00'0000 | i * 85, 20 + i * 80.0f, 20, 0});
  }
  c.put(GE::PatchPrimitive, 2), c.put(GE::PatchDivision, 8 | 1 << 8);
  c.patch(GE::Bezier, 0x80'019f, 4 | 4 << 8, points);
  c.run();
  bool rounded = true;
  for(u32 k = 0; k <= 8; k++) rounded &= c.pixel(20 + k * 30, 20) == (255 * k + 7) / 8;
  CHECK(rounded, true);
}

//Round 3 of tools/psp-measure (ge.c's patch()): its five patches, drawn here as that program draws them, against the
//owner's PSP's pictures (PSP_GE_RESULTS): the same pixels covered, every channel within a level (the blending across
//triangles, as round 3's ramps), but four pixels on the edges of the spline with both ends closed, which the PSP
//covers and the core, a hair from them, doesn't.
static auto curvesRound3() -> void {
  const char* results = std::getenv("PSP_GE_RESULTS");
  if(!results || !*results) return;
  auto load = [&](const char* name) {
    std::ifstream file(std::filesystem::path(results) / (std::string(name) + ".bin"), std::ios::binary);
    std::vector<u8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<u32> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), words.size() * 4);
    return words;
  };
  const float flat[4][4][2] = {
    {{-0.75f, 0.75f}, {-0.25f, 0.75f}, {0.25f, 0.75f}, {0.75f, 0.75f}},
    {{-0.75f, 0.25f}, {-0.25f, 0.25f}, {0.25f, 0.25f}, {0.75f, 0.25f}},
    {{-0.75f, -0.25f}, {-0.25f, -0.25f}, {0.25f, -0.25f}, {0.75f, -0.25f}},
    {{-0.75f, -0.75f}, {-0.25f, -0.75f}, {0.25f, -0.75f}, {0.75f, -0.75f}},
  };
  const float curved[4][4][2] = {
    {{-0.75f, 0.75f}, {-0.25f, 0.875f}, {0.25f, 0.875f}, {0.75f, 0.75f}},
    {{-0.875f, 0.25f}, {-0.125f, 0.125f}, {0.375f, 0.375f}, {0.875f, 0.25f}},
    {{-0.875f, -0.25f}, {-0.375f, -0.375f}, {0.125f, -0.125f}, {0.875f, -0.25f}},
    {{-0.75f, -0.75f}, {-0.25f, -0.875f}, {0.25f, -0.875f}, {0.75f, -0.75f}},
  };
  struct Case { const char* name; const float (*grid)[4][2]; u32 divisions; s32 edges; u32 apart; };
  const Case cases[] = {{"bezier-flat", flat, 4, -1, 0}, {"bezier-curved", curved, 4, -1, 0},
                        {"bezier-divide-8", curved, 8, -1, 0}, {"spline-edges-0", curved, 4, 0, 4},
                        {"spline-edges-3", curved, 4, 3, 0}};
  for(auto& test : cases) {
    auto theirs = load(test.name);
    if(theirs.size() != 256 * 256) continue;
    //start3D(&identity, 1) and patch(): the viewport onto a 256x256 target 512 pixels wide, z / w -1 to 1 the depths
    //65535 to 0, DEPTH_CLIP_ENABLE on, the depth test passing all, each point a color of its own
    Surface c;
    c.put(GE::Scissor2, 511 | 255 << 10), c.put(GE::Region2, 511 | 255 << 10);
    c.put(GE::OffsetX, (2048 - 128) << 4), c.put(GE::OffsetY, (2048 - 128) << 4);
    c.put(GE::ViewportXScale, f24(128)), c.put(GE::ViewportYScale, f24(-128));
    c.put(GE::DepthClipEnable, 1), c.put(GE::DepthTestEnable, 1), c.put(GE::DepthTest, 1);
    std::vector<FloatPoint> points;
    for(u32 j = 0; j < 4; j++) {
      for(u32 i = 0; i < 4; i++) {
        u32 color = 0xff00'0000 | i * 85 | j * 85 << 8 | (3 - i) * 85 << 16;
        points.push_back({float(i), float(j), color, test.grid[j][i][0], test.grid[j][i][1], 0});
      }
    }
    c.put(GE::PatchDivision, test.divisions | test.divisions << 8), c.put(GE::PatchPrimitive, 0);
    if(test.edges < 0) c.patch(GE::Bezier, 0x19f, 4 | 4 << 8, points);
    else c.patch(GE::Spline, 0x19f, 4 | 4 << 8 | test.edges << 16 | test.edges << 18, points);
    c.run();
    u32 covered = 0, apart = 0;
    for(u32 y = 0; y < 256; y++) {
      for(u32 x = 0; x < 256; x++) {
        u32 ours = c.pixel(x, y), psp = theirs[y * 256 + x] & 0xff'ffff;
        if((ours == 0) != (psp == 0)) {
          covered++;
          continue;
        }
        for(u32 shift : {0, 8, 16}) apart += std::abs(s32(ours >> shift & 0xff) - s32(psp >> shift & 0xff)) > 1;
      }
    }
    CHECK(covered, test.apart);
    CHECK(apart, 0u);
  }
}

//Lighting, texture coordinates and culling on a surface. A flat patch in the plane z = 0 (x along u, y down along v
//as the screen has it; identity matrices), its vertex type without a normal: lit by a directional light along +z, a
//vertex is lit fully where its normal (the slopes' cross product, out of the front PATCH_FACING says) faces the
//light, and not at all where it faces away. Without texture coordinates, u runs 0 to 1 across the surface and v down
//it (TEX_SCALE 1: the texture's width and height). Its triangles are culled as CULL says.
static auto curvesLightingAndTexture() -> void {
  auto draw = [](Surface& c) {
    std::vector<Position> points;
    for(u32 j = 0; j < 4; j++) {
      for(u32 i = 0; i < 4; i++) points.push_back({-0.5f + i / 3.0f, 0.5f - j / 3.0f, 0});
    }
    c.patch(GE::Bezier, 0x180, 4 | 4 << 8, points);
    c.run();
  };
  //lit: material white, the light's diffuse white (no ambient); the vertex type has no normal, nor color
  for(u32 facing : {0, 1}) {
    Surface c;
    c.put(GE::LightingEnable, 1), c.put(GE::LightEnable0, 1), c.put(GE::LightType0, 0);
    c.put(GE::Light0X + 2, f24(1));  //(the direction toward it: +z)
    c.put(GE::Light0Ambient + 1, 0xff'ffff), c.put(GE::MaterialDiffuse, 0xff'ffff);
    c.put(GE::PatchFacing, facing), c.put(GE::PatchDivision, 2 | 2 << 8);
    draw(c);
    //du is +x and dv -y: du x dv is -z, out of the clockwise front (facing 0), away from the light
    CHECK(c.pixel(240, 136), facing ? 0xff'ffffu : 0u);
  }
  //made-up texture coordinates: a 4x4 texture, each texel its own; u and v 0 to 1 across the surface, so the
  //texture once across it: the top left pixel takes texel (0, 0), the bottom right texel (3, 3)
  Surface c;
  for(u32 n = 0; n < 16; n++) {
    c.memory.write(4, PatchTexture + n * 4, 0xff00'0000 | (n % 4) * 0x40 | (n / 4) * 0x40 << 8);
  }
  c.put(GE::TextureMappingEnable, 1), c.put(GE::TextureFormat, 3), c.put(GE::TextureFunction, 3);
  c.to(GE::TextureAddress0, PatchTexture), c.put(GE::TextureBufferWidth0, (PatchTexture >> 24) << 16 | 4);
  c.put(GE::TextureSize0, 2 << 8 | 2), c.put(GE::TextureWrap, 1 | 1 << 8);
  c.put(GE::PatchDivision, 4 | 4 << 8);
  draw(c);
  //(x -0.5 to 0.5 is pixels 120-360, y 0.5 to -0.5 rows 68-204)
  CHECK(c.pixel(121, 69), 0x000000u);
  CHECK(c.pixel(358, 202), 0x00c0c0u);
  CHECK(c.pixel(358, 69), 0x0000c0u);
  CHECK(c.pixel(121, 202), 0x00c000u);
  //culled: the strip's first triangle runs counterclockwise on the screen (y down), so CULL 1 (clockwise drawn)
  //drops every one, and 0 draws them all; with culling off, all of them
  for(u32 cull : {0, 1, 2}) {
    Surface c;
    c.put(GE::CullFaceEnable, cull < 2), c.put(GE::Cull, cull & 1), c.put(GE::AmbientColor, 0xff'ffff);
    c.put(GE::PatchDivision, 3 | 3 << 8);
    draw(c);
    u32 drawn = 0;
    for(u32 y = 60; y < 210; y++) for(u32 x = 110; x < 370; x++) drawn += c.pixel(x, y) != 0;
    CHECK(drawn, cull == 1 ? 0u : 240u * 136u);
  }
}

//The same surfaces drawn on 1 to 8 threads, in batches shared out however small, four pixels at a time and one at a
//time: every byte of VRAM alike. A list of patches of every kind (Bezier and spline, triangles, lines and points,
//textured and filtered, lit, in 3D under a perspective and in through mode, one with a vertex type of 16-bit parts
//and indices), run by the driver; stopped at its stall address between PATCH_DIVISION and the SPLINE after it, a
//state saved there carries on in another machine to the same picture.
static auto curvesThreads() -> void {
  u32 stall = 0;
  auto build = [&](Memory& memory) {
    for(u32 n = 0; n < 64 * 64; n++) {
      memory.write(4, PatchTexture + n * 4, 0xff00'0000 | (n * 0x0103'0507 & 0xff'ffff));
    }
    u32 at = PatchList, data = PatchPoints;
    auto put = [&](u32 command, u32 argument = 0) {
      memory.write(4, at, command << 24 | (argument & 0xff'ffff));
      at += 4;
    };
    auto points = [&](u32 count, auto&& point) {
      put(GE::Base, data >> 8 & 0xf'0000), put(GE::VertexAddress, data);
      for(u32 n = 0; n < count; n++) {
        FloatPoint p = point(n);
        for(u32 word : {std::bit_cast<u32>(p.u), std::bit_cast<u32>(p.v), p.color, std::bit_cast<u32>(p.x),
                        std::bit_cast<u32>(p.y), std::bit_cast<u32>(p.z)}) memory.write(4, data, word), data += 4;
      }
    };
    put(GE::FrameBufferPointer, 0), put(GE::FrameBufferWidth, 512), put(GE::FrameBufferPixelFormat, 3);
    put(GE::DepthBufferPointer, PatchDepth), put(GE::DepthBufferWidth, 512);
    put(GE::Scissor2, 479 | 271 << 10), put(GE::Region2, 479 | 271 << 10), put(GE::ShadeMode, 1);
    put(GE::MaxZ, 0xffff), put(GE::DepthTestEnable, 1), put(GE::DepthTest, 7);
    put(GE::OffsetX, (2048 - 240) << 4), put(GE::OffsetY, (2048 - 136) << 4);
    put(GE::ViewportXScale, f24(240)), put(GE::ViewportYScale, f24(-136)), put(GE::ViewportZScale, f24(-32767.5f));
    put(GE::ViewportXCenter, f24(2048)), put(GE::ViewportYCenter, f24(2048)), put(GE::ViewportZCenter, f24(32767.5f));
    put(GE::DepthClipEnable, 1);
    for(u32 n = 0; n < 12; n++) {
      put(GE::WorldMatrixData, f24(n % 4 == 0 ? 1.0f : 0));
      put(GE::ViewMatrixData, f24(n % 4 == 0 ? 1.0f : 0));
    }
    //a perspective lens (near 0.5, far 10)
    const float lens[16] = {1, 0, 0, 0, 0, 1.7f, 0, 0, 0, 0, -1.10526316f, -1, 0, 0, -1.05263158f, 0};
    for(float element : lens) put(GE::ProjectionMatrixData, f24(element));
    put(GE::TextureScaleU, f24(1)), put(GE::TextureScaleV, f24(1));
    //textured, filtered, smooth patches in 3D (a sheet bowing toward the camera), triangles; one lit
    put(GE::TextureMappingEnable, 1), put(GE::TextureFormat, 3), put(GE::TextureFunction, 0 | 1 << 8);
    put(GE::TextureFilter, 1 | 1 << 8), put(GE::Base, PatchTexture >> 8 & 0xf'0000);
    put(GE::TextureAddress0, PatchTexture), put(GE::TextureBufferWidth0, (PatchTexture >> 24) << 16 | 64);
    put(GE::TextureSize0, 6 << 8 | 6), put(GE::VertexType, 0x19f);
    for(u32 k = 0; k < 6; k++) {
      put(GE::PatchDivision, (3 + k * 3) | (2 + k * 2) << 8), put(GE::PatchPrimitive, 0);
      points(7 * 4, [&](u32 n) {
        u32 i = n % 7, j = n / 7;
        float x = -1.6f + k % 3 * 1.1f + i * 0.13f, y = 0.9f - k / 3 * 1.0f - j * 0.25f;
        float z = -2.5f + ((i * 7 + j * 3 + k) % 5) * 0.2f;
        return FloatPoint{i / 6.0f, j / 3.0f, 0xff00'0000 | (i * 40) | (j * 80) << 8 | (k * 40) << 16, x, y, z};
      });
      put(GE::Bezier, 7 | 4 << 8);
      if(k == 2) {
        put(GE::LightingEnable, 1), put(GE::LightEnable0, 1), put(GE::Light0X + 2, f24(1));
        put(GE::Light0Ambient + 1, 0xc0'a080), put(GE::MaterialDiffuse, 0xff'ffff), put(GE::PatchFacing, 1);
      }
    }
    put(GE::LightingEnable, 0);
    //splines: each end type, lines and points
    put(GE::TextureMappingEnable, 0), put(GE::DepthTestEnable, 0);
    for(u32 k = 0; k < 4; k++) {
      put(GE::PatchDivision, 5 | 3 << 8), put(GE::PatchPrimitive, k == 3 ? 2 : k == 2 ? 1 : 0);
      if(k == 1) stall = at + 4;  //(after PATCH_DIVISION and PATCH_PRIMITIVE, before the SPLINE)
      points(5 * 5, [&](u32 n) {
        u32 i = n % 5, j = n / 5;
        return FloatPoint{0, 0, 0xff20'4060 + n * 0x0905'0301, -0.9f + k * 0.45f + i * 0.08f, -0.2f - j * 0.15f, -1};
      });
      put(GE::Spline, 5 | 5 << 8 | k << 16 | (3 - k) << 18);
    }
    //through mode: 16-bit parts with indices, flat shading
    put(GE::ShadeMode, 0);
    put(GE::Base, data >> 8 & 0xf'0000), put(GE::IndexAddress, data);
    for(u32 n = 0; n < 16; n++) memory.write(2, data + n * 2, 15 - n);
    data += 32;
    put(GE::Base, data >> 8 & 0xf'0000), put(GE::VertexAddress, data);
    for(u32 n = 0; n < 16; n++) {
      memory.write(4, data, 0xff00'0000 | n * 0x10'0f0e), data += 4;
      for(s32 value : {s32(300 + n % 4 * 50), s32(150 + n / 4 * 30 + (n % 4 == 1 ? -40 : 0)), 0}) {
        memory.write(2, data, u32(value)), data += 2;
      }
      data += 2;
    }
    put(GE::VertexType, 0x80'111c), put(GE::PatchDivision, 9 | 7 << 8), put(GE::PatchPrimitive, 0);
    put(GE::Bezier, 4 | 4 << 8);
    put(GE::Finish), put(GE::End);
  };
  auto drawn = [&](u32 threads, u64 shared, bool fours, bool stalled) {
    KernelMachine m;
    m.system.ge.setThreads(threads);
    m.system.ge.drawing.shared = shared;
    m.system.ge.fourPixels = fours;
    build(m.system.memory);
    m.call("sceGeListEnQueue", {PatchList, stalled ? stall : 0, 0xffff'ffff, 0});
    m.system.ge.settle();
    return m.system.memory.vram;
  };
  auto whole = drawn(1, 8192, true, false), half = drawn(1, 8192, true, true);
  CHECK(whole != half, true);
  u32 drawnPixels = 0;
  for(u32 n = 0; n < 512 * 272; n++) drawnPixels += (whole[n * 4] | whole[n * 4 + 1] | whole[n * 4 + 2]) != 0;
  CHECK(drawnPixels > 20000, true);
  for(u32 threads : {1u, 2u, 4u, 8u}) {
    for(u64 shared : {u64(0), u64(8192)}) {
      for(bool fours : {false, true}) {
        CHECK(drawn(threads, shared, fours, false) == whole, true);
        CHECK(drawn(threads, shared, fours, true) == half, true);
      }
    }
  }
  //a state saved at the stall address carries on in another machine
  KernelMachine m;
  m.system.ge.setThreads(4);
  m.system.ge.drawing.shared = 0;
  build(m.system.memory);
  u32 id = m.call("sceGeListEnQueue", {PatchList, stall, 0xffff'ffff, 0});
  auto state = saveState(m);
  KernelMachine n;
  n.system.ge.setThreads(2);
  CHECK(loadState(n, state), true);
  CHECK(n.system.memory.vram == half, true);
  CHECK(n.call("sceGeListUpdateStallAddr", {id, 0}), 0u);
  n.system.ge.settle();
  CHECK(n.system.memory.vram == whole, true);
}

//Malformed surfaces: bounded, and quick. Counts of 255 each way cut 255 times (a spline of 252 pieces each way
//would be 4 billion vertices, a Bezier 458 million) aren't drawn, but the points are read and the address moves on
//past them; a spline cut as finely as the budget allows along one way is drawn; control points that aren't
//numbers draw nothing, nor do counts below 4; and none of it takes long.
static auto curvesMalformed() -> void {
  auto start = std::chrono::steady_clock::now();
  Surface c;
  std::vector<FloatPoint> many(255 * 255, FloatPoint{0, 0, 0xffff'ffff, 0.1f, 0.1f, 0});
  c.put(GE::PatchDivision, 255 | 255 << 8);
  c.patch(GE::Bezier, 0x19f, 255 | 255 << 8, many);
  c.patch(GE::Spline, 0x19f, 255 | 255 << 8, std::vector<FloatPoint>{});
  c.run();
  CHECK(c.ge.vertexAddress, PatchPoints + 2 * 255 * 255 * 24);
  bool blank = true;
  for(u32 y = 0; y < 272; y++) for(u32 x = 0; x < 480; x++) blank &= c.pixel(x, y) == 0;
  CHECK(blank, true);
  CHECK(c.ge.noted.count("a curved surface cut into more vertices than the core draws"), 1u);
  //the most along one way: 252 pieces cut 255 times, by one along the other (64,261 x 2 vertices), drawn
  Surface wide;
  std::vector<FloatPoint> row;
  for(u32 j = 0; j < 4; j++) {
    for(u32 i = 0; i < 255; i++) row.push_back({0, 0, 0xff00'ff00, -0.9f + i * 0.007f, 0.5f - j * 0.3f, 0});
  }
  wide.put(GE::PatchDivision, 255 | 1 << 8);
  wide.patch(GE::Spline, 0x19f, 255 | 4 << 8 | 3 << 16 | 3 << 18, row);
  wide.run();
  CHECK(wide.pixel(240, 120), 0x00ff00u);
  //not numbers: nothing drawn (3D: not on the screen; through mode: held to the GE's range, off this one), the
  //colors still worked out
  for(u32 type : {0x19fu, 0x80'019fu}) {
    Surface n;
    std::vector<FloatPoint> points(16, FloatPoint{NAN, NAN, 0xffff'ffff, NAN, NAN, NAN});
    points[5].x = INFINITY, points[6].y = -INFINITY;
    for(u32 kind : {0, 1, 2}) {
      n.put(GE::PatchPrimitive, kind), n.put(GE::PatchDivision, 8 | 8 << 8);
      n.patch(GE::Bezier, type, 4 | 4 << 8, points);
      n.patch(GE::Spline, type, 4 | 4 << 8 | 1 << 16 | 2 << 18, points);
    }
    n.run();
    bool none = true;
    for(u32 y = 0; y < 272; y++) for(u32 x = 0; x < 480; x++) none &= n.pixel(x, y) == 0;
    CHECK(none, true);
  }
  //counts below 4 draw nothing (a Bezier of 1, 2 or 3 points along a way, a spline of 3), but are read
  Surface few;
  std::vector<FloatPoint> points(16, FloatPoint{0, 0, 0xffff'ffff, 0, 0, 0});
  for(u32 count : {0, 1, 2, 3}) {
    few.patch(GE::Bezier, 0x80'019f, count | 4 << 8, points);
    few.patch(GE::Spline, 0x80'019f, 4 | count << 8, points);
  }
  few.run();
  CHECK(few.pixel(0, 0), 0u);
  CHECK(few.ge.vertexAddress > PatchPoints, true);
  auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  CHECK(seconds < 20, true);
}

auto curvesTests() -> Tests {
  return {
    {"curves pspautotests' primitives", curvesPrimitivesPrograms}, {"curves knots", curvesKnots},
    {"curves round 3", curvesRound3}, {"curves lighting and texture", curvesLightingAndTexture},
    {"curves on several threads", curvesThreads}, {"curves malformed", curvesMalformed},
  };
}

}
