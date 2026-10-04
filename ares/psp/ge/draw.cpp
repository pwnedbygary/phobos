//Drawing: PRIM's vertices become primitives, and the pixels each covers go through the pixel pipeline (pixel.cpp),
//textured (texture.cpp) if TEXTURE_MAPPING_ENABLE says so. So far in 2D only: through mode, where positions are
//already pixels (with four fraction bits: the GE works in sixteenths of a pixel) and texture coordinates are texels.
//
//  - Sprites: a rectangle between each pair of vertices, in the second's color and depth. It covers the pixels whose
//    middles are inside, both edges included. Texture coordinates run from one vertex's to the other's, across with
//    x and down with y; but if the corners are bottom-left and top-right (in either order), the texture is turned a
//    quarter: its coordinates run down with x and across with y.
//  - Triangles (each three vertices, or a strip, or a fan): the pixels whose sample points (7/16 of a pixel in from
//    their top left) are inside, those on an edge counting only on left and top edges, so triangles sharing an edge
//    don't both draw it. Color, depth and texture coordinates are blended across from the corners, or with flat
//    shading (SHADE_MODE 0) the color is the last vertex's.
//  - Points: the pixel each vertex is in.
//A vertex without a color takes the material's ambient color (AMBIENT_COLOR, AMBIENT_ALPHA).
//(These rules, the sample points and the corner order's quarter turn among them, are as PPSSPP's software renderer
//has them, which its authors checked against tests on the PSP. Not yet: lines, and 3D.)

auto GE::primitive(u32 kind, u32 count) -> void {
  auto format = vertexFormat();
  u32 ambient = (commands[AmbientColor] & 0xff'ffff) | (commands[AmbientAlpha] & 0xff) << 24;
  std::vector<Vertex> vertices;
  vertices.reserve(count);
  for(u32 n = 0; n < count; n++) {
    u32 index = format.indexFormat ? readIndex(n, format) : n;
    vertices.push_back(readVertex(vertexAddress + index * format.size, format));
    if(!format.colorFormat) vertices.back().color = ambient;
  }
  //the next PRIM carries on where this one stopped: after its indices if it had them, else after its vertices
  if(format.indexFormat) indexAddress += count * (format.indexFormat == 2 ? 2 : 1);
  else vertexAddress += count * format.size;
  if(!format.through) return note("drawing in 3D (not through mode) isn't emulated yet");

  auto pixel = pixelState();
  Sampler texture = sampler();
  Sampler* textured = (commands[TextureMappingEnable] & 1) && !pixel.clear ? &texture : nullptr;
  switch(kind) {
  case Points:
    for(auto& vertex : vertices) point(pixel, textured, vertex);
    break;
  case Lines:
  case LineStrip:
    note("lines aren't drawn yet");
    break;
  case Triangles:
    for(u32 n = 0; n + 2 < count; n += 3) triangle(pixel, textured, vertices[n], vertices[n + 1], vertices[n + 2]);
    break;
  case TriangleStrip:
    for(u32 n = 0; n + 2 < count; n++) triangle(pixel, textured, vertices[n], vertices[n + 1], vertices[n + 2]);
    break;
  case TriangleFan:
    for(u32 n = 1; n + 1 < count; n++) triangle(pixel, textured, vertices[0], vertices[n], vertices[n + 1]);
    break;
  case Sprites:
    for(u32 n = 0; n + 1 < count; n += 2) rectangle(pixel, textured, vertices[n], vertices[n + 1]);
    break;
  }
  if(pixel.high >= pixel.low) memory.changed(Memory::VRAMBase + pixel.low, pixel.high - pixel.low + 4);
}

//A screen position in pixels as the GE holds it, in sixteenths. Wild values (from garbage vertices) are held to the
//GE's range.
static auto fixed(float position) -> s32 {
  if(!(position >= -4096 && position <= 4096)) position = position > 0 ? 4096 : -4096;
  return s32(position * 16);
}

static auto floorDivide(s32 value, s32 by) -> s32 { return value >= 0 ? value / by : -((-value + by - 1) / by); }

//A pixel's color: the vertex color, through the texture if there is one, then into the pixel pipeline.
auto GE::shade(PixelState& pixel, Sampler* texture, s32 x, s32 y, u32 z, u32 color, float u, float v) -> void {
  if(texture) color = textureFunction(color, sample(*texture, u, v));
  drawPixel(pixel, x, y, z, color);
}

//The filter for a primitive: TEXTURE_FILTER's for enlarging if a texel covers a pixel or more, else its for shrinking
//(of which the mipmap kinds, 4-7, filter as their bit 0 says: mipmaps aren't emulated yet).
static auto chooseFilter(u32 filter, float texelsPerPixel) -> bool {
  return texelsPerPixel <= 1.0f ? filter >> 8 & 1 : filter & 1;
}

auto GE::rectangle(PixelState& pixel, Sampler* texture, const Vertex& from, const Vertex& to) -> void {
  s32 x0 = fixed(from.x), y0 = fixed(from.y), x1 = fixed(to.x), y1 = fixed(to.y);
  if(x0 == x1 || y0 == y1) return;
  s32 left = std::min(x0, x1), right = std::max(x0, x1), top = std::min(y0, y1), bottom = std::max(y0, y1);
  //the pixels whose middles (eight sixteenths in) are inside, both edges included
  s32 firstX = std::max(floorDivide(left - 8 + 15, 16), pixel.left), lastX = std::min(floorDivide(right - 8, 16), pixel.right);
  s32 firstY = std::max(floorDivide(top - 8 + 15, 16), pixel.top), lastY = std::min(floorDivide(bottom - 8, 16), pixel.bottom);
  bool turned = (x0 < x1) != (y0 < y1);  //bottom-left and top-right corners: the texture turns a quarter
  if(texture) {
    float across = std::abs(turned ? to.v - from.v : to.u - from.u) / ((right - left) / 16.0f);
    texture->linear = chooseFilter(commands[TextureFilter], across);
  }
  u32 z = u32(std::clamp(to.z, 0.0f, 65535.0f));
  for(s32 y = firstY; y <= lastY; y++) {
    float down = float(y * 16 + 8 - y0) / float(y1 - y0);  //from the first vertex's y to the second's
    for(s32 x = firstX; x <= lastX; x++) {
      float along = float(x * 16 + 8 - x0) / float(x1 - x0);
      float u = turned ? from.u + down * (to.u - from.u) : from.u + along * (to.u - from.u);
      float v = turned ? from.v + along * (to.v - from.v) : from.v + down * (to.v - from.v);
      shade(pixel, texture, x, y, z, to.color, u, v);
    }
  }
}

auto GE::triangle(PixelState& pixel, Sampler* texture, const Vertex& a, const Vertex& b, const Vertex& c) -> void {
  struct Corner { s64 x, y; const Vertex* vertex; };
  Corner p[3] = {{fixed(a.x), fixed(a.y), &a}, {fixed(b.x), fixed(b.y), &b}, {fixed(c.x), fixed(c.y), &c}};
  s64 area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);
  if(area == 0) return;
  if(area < 0) std::swap(p[1], p[2]), area = -area;  //the same corners, turned the way the edges are worked out for
  //An edge's function: positive on the triangle's side of it, zero on it.
  auto edge = [](const Corner& from, const Corner& to, s64 x, s64 y) {
    return (from.y - to.y) * x + (to.x - from.x) * y + (to.y * from.x - to.x * from.y);
  };
  //Whether the edge from-to is a right edge or a flat bottom one (the other corner left of or above it): a pixel
  //exactly on such an edge isn't drawn.
  auto rightOrBottom = [](const Corner& other, const Corner& from, const Corner& to) {
    if(from.y == to.y) return other.y < from.y;
    return other.x < from.x + (to.x - from.x) * (other.y - from.y) / (to.y - from.y);
  };
  s64 bias0 = rightOrBottom(p[0], p[1], p[2]) ? -1 : 0;
  s64 bias1 = rightOrBottom(p[1], p[2], p[0]) ? -1 : 0;
  s64 bias2 = rightOrBottom(p[2], p[0], p[1]) ? -1 : 0;
  s64 minX = std::min({p[0].x, p[1].x, p[2].x}), maxX = std::max({p[0].x, p[1].x, p[2].x});
  s64 minY = std::min({p[0].y, p[1].y, p[2].y}), maxY = std::max({p[0].y, p[1].y, p[2].y});
  s32 firstX = std::max<s32>(floorDivide(s32(minX) - 7 + 15, 16), pixel.left);
  s32 lastX = std::min<s32>(floorDivide(s32(maxX) - 7, 16), pixel.right);
  s32 firstY = std::max<s32>(floorDivide(s32(minY) - 7 + 15, 16), pixel.top);
  s32 lastY = std::min<s32>(floorDivide(s32(maxY) - 7, 16), pixel.bottom);

  bool flat = !(commands[ShadeMode] & 1);
  u32 flatColor = c.color;  //flat shading: the last vertex's color, whichever way the corners were turned
  if(texture) {
    const Vertex &va = *p[0].vertex, &vb = *p[1].vertex, &vc = *p[2].vertex;
    float texels = std::abs((vb.u - va.u) * (vc.v - va.v) - (vb.v - va.v) * (vc.u - va.u));
    texture->linear = chooseFilter(commands[TextureFilter], std::sqrt(texels / (area / 256.0f)));
  }
  float total = float(area);
  for(s32 y = firstY; y <= lastY; y++) {
    s64 sampleY = s64(y) * 16 + 7;
    for(s32 x = firstX; x <= lastX; x++) {
      s64 sampleX = s64(x) * 16 + 7;
      s64 w0 = edge(p[1], p[2], sampleX, sampleY), w1 = edge(p[2], p[0], sampleX, sampleY);
      s64 w2 = edge(p[0], p[1], sampleX, sampleY);
      if(w0 + bias0 < 0 || w1 + bias1 < 0 || w2 + bias2 < 0) continue;
      const Vertex &va = *p[0].vertex, &vb = *p[1].vertex, &vc = *p[2].vertex;
      auto blend = [&](float first, float second, float third) {
        return (first * float(w0) + second * float(w1) + third * float(w2)) / total;
      };
      u32 color = flatColor;
      if(!flat) {
        color = 0;
        for(u32 n = 0; n < 4; n++) {
          s32 value = s32(blend(channel(va.color, n), channel(vb.color, n), channel(vc.color, n)));
          color |= u32(std::clamp(value, 0, 255)) << n * 8;
        }
      }
      u32 z = u32(std::clamp(blend(va.z, vb.z, vc.z), 0.0f, 65535.0f));
      shade(pixel, texture, x, y, z, color, blend(va.u, vb.u, vc.u), blend(va.v, vb.v, vc.v));
    }
  }
}

auto GE::point(PixelState& pixel, Sampler* texture, const Vertex& at) -> void {
  s32 x = fixed(at.x) >> 4, y = fixed(at.y) >> 4;
  if(x < pixel.left || x > pixel.right || y < pixel.top || y > pixel.bottom) return;
  if(texture) texture->linear = chooseFilter(commands[TextureFilter], 1.0f);
  shade(pixel, texture, x, y, u32(std::clamp(at.z, 0.0f, 65535.0f)), at.color, at.u, at.v);
}
