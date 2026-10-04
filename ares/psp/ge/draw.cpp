//Drawing: PRIM's vertices become primitives, and the pixels each covers go through the pixel pipeline (pixel.cpp),
//textured (texture.cpp) if TEXTURE_MAPPING_ENABLE says so. In through mode (2D) positions are already pixels (with
//four fraction bits: the GE works in sixteenths of a pixel) and texture coordinates are texels; in 3D, transform.cpp
//first puts the vertices on the screen, and cuts triangles at the near plane.
//
//  - Sprites: a rectangle between each pair of vertices, in the second's color and depth. It covers the pixels whose
//    middles are inside, the left and top edges included and the right and bottom ones not, its left edge reaching a
//    sixteenth further left. Texture coordinates run from one vertex's to the other's, across with x and down with
//    y; but if the corners are bottom-left and top-right (in either order), the texture is turned a quarter: its
//    coordinates run down with x and across with y.
//  - Triangles (each three vertices, or a strip, or a fan): the pixels whose middles are inside, those on an edge
//    counting only on left and top edges, so triangles sharing an edge don't both draw it. Color, depth and texture
//    coordinates are blended across from the corners at each pixel's middle, or with flat
//    shading (SHADE_MODE 0) the color is the last vertex's. In 3D the texture coordinates are blended as the
//    perspective has them (as u/w and 1/w, then divided, so a texture on a floor shrinks into the distance); colors
//    and depth aren't, nor is the fog.
//  - Culling (CULL_FACE_ENABLE, not in clear mode): with CULL 1 only triangles whose corners run clockwise on the
//    screen are drawn, with 0 only those running counterclockwise (pspsdk's sceGuFrontFace(GU_CW) sets 1). Every
//    other triangle of a strip runs the other way round, so for those it's the other way.
//  - Points: the pixel each vertex is in.
//A vertex without a color takes the material's ambient color (AMBIENT_COLOR, AMBIENT_ALPHA).
//(Coverage, the sample points and how texture coordinates are stepped were measured on a PSP (docs/psp-core.md,
//tools/psp-ge-measure), where PPSSPP's software renderer, which the rest follows, has triangles sampled 7/16 in. Not
//yet: lines, and PRIM's kind 7, which goes on with the last primitive's vertices.)

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
  if(kind == 7) return note("PRIM's kind 7 (going on with the last primitive's vertices) isn't emulated yet");
  if(!format.positionFormat) return;  //vertices without positions draw nothing (as PPSSPP has it)

  auto pixel = pixelState();
  pixel.depthRange = !format.through;
  pixel.fog = !format.through && !pixel.clear && (commands[FogEnable] & 1);
  Sampler texture = sampler();
  Sampler* textured = (commands[TextureMappingEnable] & 1) && !pixel.clear ? &texture : nullptr;
  Transform t{};
  if(!format.through) {
    t = transformState();
    t.weights = format.weightFormat ? format.weights : 0;
    t.textureWidth = texture.width, t.textureHeight = texture.height;
    t.vertexColor = format.colorFormat != 0;
    for(auto& vertex : vertices) transform(vertex, t);
  }
  s32 facing = (commands[CullFaceEnable] & 1) && !pixel.clear ? (commands[Cull] & 1 ? 1 : -1) : 0;
  auto drawTriangle = [&](const Vertex& a, const Vertex& b, const Vertex& c, s32 facing) {
    if(format.through) triangle(pixel, textured, a, b, c, facing, false);
    else clipTriangle(pixel, textured, t, a, b, c, facing);
  };
  switch(kind) {
  case Points:
    for(auto& vertex : vertices) {
      if(!vertex.outside) point(pixel, textured, vertex);
    }
    break;
  case Lines:
  case LineStrip:
    note("lines aren't drawn yet");
    break;
  case Triangles:
    for(u32 n = 0; n + 2 < count; n += 3) drawTriangle(vertices[n], vertices[n + 1], vertices[n + 2], facing);
    break;
  case TriangleStrip:
    for(u32 n = 0; n + 2 < count; n++) drawTriangle(vertices[n], vertices[n + 1], vertices[n + 2], n & 1 ? -facing : facing);
    break;
  case TriangleFan:
    for(u32 n = 1; n + 1 < count; n++) drawTriangle(vertices[0], vertices[n], vertices[n + 1], facing);
    break;
  case Sprites:
    for(u32 n = 0; n + 1 < count; n += 2) {
      if(!format.through && outOfSight(t.depthClamp, {&vertices[n], &vertices[n + 1]})) continue;
      rectangle(pixel, textured, vertices[n], vertices[n + 1]);
    }
    break;
  }
  if(pixel.high >= pixel.low) memory.changed(Memory::VRAMBase + pixel.low, pixel.high - pixel.low + 4);
}

//The fog at a pixel, 0-255, from its 0-1: rounded down, 1 or more giving 255 (as PPSSPP has it).
static auto fogAmount(float fog) -> u32 {
  if(std::signbit(fog)) return 0;
  if(!(fog < 1)) return 255;
  return u32(fog * 256);
}

//A screen position in pixels as the GE holds it, in sixteenths. Wild values (from garbage vertices) are held to the
//GE's range.
static auto fixed(float position) -> s32 {
  if(!(position >= -4096 && position <= 4096)) position = position > 0 ? 4096 : -4096;
  return s32(position * 16);
}

static auto floorDivide(s32 value, s32 by) -> s32 { return value >= 0 ? value / by : -((-value + by - 1) / by); }

//A pixel's color: the vertex color, through the texture if there is one, plus lighting's shine when it's kept apart
//(each channel held to 255), then into the pixel pipeline.
auto GE::shade(PixelState& pixel, Sampler* texture, s32 x, s32 y, u32 z, u32 color, u32 specular, float u, float v,
               u32 fog) -> void {
  if(texture) color = textureFunction(color, sample(*texture, u, v));
  if(specular) {
    color = pack(channel(color, 0) + channel(specular, 0), channel(color, 1) + channel(specular, 1),
                 channel(color, 2) + channel(specular, 2), channel(color, 3));
  }
  drawPixel(pixel, x, y, z, color, fog);
}

//The filter for a primitive: TEXTURE_FILTER's for enlarging if a texel covers a pixel or more, else its for shrinking
//(of which the mipmap kinds, 4-7, filter as their bit 0 says: mipmaps aren't emulated yet).
static auto chooseFilter(u32 filter, float texelsPerPixel) -> bool {
  return texelsPerPixel <= 1.0f ? filter >> 8 & 1 : filter & 1;
}

//How a PSP steps texture coordinates across a primitive (measured, docs/psp-core.md): from its leftmost corner (the
//topmost of two), by a step per pixel that's cut short, toward zero, to a 65536th of a texel when it isn't exact. So
//a coordinate that should land exactly on a texel boundary falls a hair short of it, on that corner's side (256
//texels over 240 pixels), while an exact step lands on it (4 texels over 2 pixels). How many bits the step keeps,
//and which way a step going down (to the left or up) is cut, aren't pinned down; this fits every measurement.
static auto shortStep(f64 step) -> f64 { return std::trunc(step * 65536) / 65536; }

auto GE::rectangle(PixelState& pixel, Sampler* texture, const Vertex& from, const Vertex& to) -> void {
  s32 x0 = fixed(from.x), y0 = fixed(from.y), x1 = fixed(to.x), y1 = fixed(to.y);
  if(x0 == x1 || y0 == y1) return;
  s32 left = std::min(x0, x1), right = std::max(x0, x1), top = std::min(y0, y1), bottom = std::max(y0, y1);
  //The pixels whose middles (eight sixteenths in) are inside, the left and top edges included and the right and
  //bottom ones not, as for triangles; but the left edge reaches a sixteenth further left (measured on a PSP,
  //docs/psp-core.md: a column is drawn while the left edge is at most 9/16 into it).
  s32 firstX = std::max(floorDivide(left - 9 + 15, 16), pixel.left);
  s32 lastX = std::min(floorDivide(right - 8 - 1, 16), pixel.right);
  s32 firstY = std::max(floorDivide(top - 8 + 15, 16), pixel.top);
  s32 lastY = std::min(floorDivide(bottom - 8 - 1, 16), pixel.bottom);
  bool turned = (x0 < x1) != (y0 < y1);  //bottom-left and top-right corners: the texture turns a quarter
  if(texture) {
    float across = std::abs(turned ? to.v - from.v : to.u - from.u) / ((right - left) / 16.0f);
    texture->linear = chooseFilter(commands[TextureFilter], across);
  }
  u32 z = u32(std::clamp(to.z, 0.0f, 65535.0f));
  u32 fog = fogAmount(to.fog);  //in 3D, the second vertex's (PPSSPP splits it across the middle; not done here)
  //texture coordinates at each pixel's middle, stepped from the left (or top) edge's towards the other's
  auto across = [](f64 first, f64 second, s32 at, s32 start, s32 end) -> float {
    if(start > end) std::swap(first, second), std::swap(start, end);
    return first + f64(at - start) / 16 * shortStep(16 * (second - first) / f64(end - start));
  };
  for(s32 y = firstY; y <= lastY; y++) {
    for(s32 x = firstX; x <= lastX; x++) {
      s32 sampleX = x * 16 + 8, sampleY = y * 16 + 8;
      float u = turned ? across(from.u, to.u, sampleY, y0, y1) : across(from.u, to.u, sampleX, x0, x1);
      float v = turned ? across(from.v, to.v, sampleX, x0, x1) : across(from.v, to.v, sampleY, y0, y1);
      shade(pixel, texture, x, y, z, to.color, to.specular, u, v, fog);
    }
  }
}

//A triangle. facing: 0 draws it either way round; 1 only if its corners run clockwise on the screen (y down), -1
//only counterclockwise. perspective: 3D, its texture coordinates blended as the perspective has them.
auto GE::triangle(PixelState& pixel, Sampler* texture, const Vertex& a, const Vertex& b, const Vertex& c, s32 facing,
                  bool perspective) -> void {
  struct Corner { s64 x, y; const Vertex* vertex; };
  Corner p[3] = {{fixed(a.x), fixed(a.y), &a}, {fixed(b.x), fixed(b.y), &b}, {fixed(c.x), fixed(c.y), &c}};
  s64 area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);  //above 0: clockwise
  if(area == 0) return;
  if(facing && (area > 0) != (facing > 0)) return;
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
  s32 firstX = std::max<s32>(floorDivide(s32(minX) - 8 + 15, 16), pixel.left);
  s32 lastX = std::min<s32>(floorDivide(s32(maxX) - 8, 16), pixel.right);
  s32 firstY = std::max<s32>(floorDivide(s32(minY) - 8 + 15, 16), pixel.top);
  s32 lastY = std::min<s32>(floorDivide(s32(maxY) - 8, 16), pixel.bottom);

  bool flat = !(commands[ShadeMode] & 1);
  u32 flatColor = c.color, flatSpecular = c.specular;  //flat shading: the last vertex's, whichever way the corners turned
  bool shines = a.specular || b.specular || c.specular;
  if(texture) {
    const Vertex &va = *p[0].vertex, &vb = *p[1].vertex, &vc = *p[2].vertex;
    float texels = std::abs((vb.u - va.u) * (vc.v - va.v) - (vb.v - va.v) * (vc.u - va.u));
    texture->linear = chooseFilter(commands[TextureFilter], std::sqrt(texels / (area / 256.0f)));
  }
  float total = float(area);
  //Without perspective, texture coordinates are stepped from the leftmost corner (see shortStep): its value, then a
  //step per pixel across and down, from the plane through the three corners.
  struct Steps { f64 start, across, down; };
  auto stepsFor = [&](f64 first, f64 second, f64 third, f64 last) -> Steps {
    f64 x1 = f64(p[1].x - p[0].x), y1 = f64(p[1].y - p[0].y), x2 = f64(p[2].x - p[0].x), y2 = f64(p[2].y - p[0].y);
    f64 across = ((second - first) * y2 - (third - first) * y1) / f64(area);
    f64 down = ((third - first) * x1 - (second - first) * x2) / f64(area);
    return {last, shortStep(16 * across), shortStep(16 * down)};
  };
  u32 leftmost = 0;
  for(u32 k : range(1, 3)) {
    if(std::tie(p[k].x, p[k].y) < std::tie(p[leftmost].x, p[leftmost].y)) leftmost = k;
  }
  s64 startX = p[leftmost].x, startY = p[leftmost].y;
  auto stepped = [&](const Steps& steps, s64 sampleX, s64 sampleY) -> float {
    return steps.start + f64(sampleX - startX) / 16 * steps.across + f64(sampleY - startY) / 16 * steps.down;
  };
  Steps uStep{}, vStep{};
  if(texture && !perspective) {
    const Vertex &va = *p[0].vertex, &vb = *p[1].vertex, &vc = *p[2].vertex, &start = *p[leftmost].vertex;
    uStep = stepsFor(va.u, vb.u, vc.u, start.u);
    vStep = stepsFor(va.v, vb.v, vc.v, start.v);
  }
  for(s32 y = firstY; y <= lastY; y++) {
    s64 sampleY = s64(y) * 16 + 8;
    for(s32 x = firstX; x <= lastX; x++) {
      s64 sampleX = s64(x) * 16 + 8;
      s64 w0 = edge(p[1], p[2], sampleX, sampleY), w1 = edge(p[2], p[0], sampleX, sampleY);
      s64 w2 = edge(p[0], p[1], sampleX, sampleY);
      if(w0 + bias0 < 0 || w1 + bias1 < 0 || w2 + bias2 < 0) continue;
      const Vertex &va = *p[0].vertex, &vb = *p[1].vertex, &vc = *p[2].vertex;
      auto blend = [&](float first, float second, float third) {
        return (first * float(w0) + second * float(w1) + third * float(w2)) / total;
      };
      auto blendColor = [&](u32 first, u32 second, u32 third) {
        u32 blended = 0;
        for(u32 n = 0; n < 4; n++) {
          s32 value = s32(blend(channel(first, n), channel(second, n), channel(third, n)));
          blended |= u32(std::clamp(value, 0, 255)) << n * 8;
        }
        return blended;
      };
      u32 color = flat ? flatColor : blendColor(va.color, vb.color, vc.color);
      u32 specular = flat || !shines ? flatSpecular : blendColor(va.specular, vb.specular, vc.specular);
      u32 z = u32(std::clamp(blend(va.z, vb.z, vc.z), 0.0f, 65535.0f));
      float u = 0, v = 0;
      if(texture && perspective) {
        float ka = float(w0) / va.clip[3], kb = float(w1) / vb.clip[3], kc = float(w2) / vc.clip[3];
        float divisor = ka * va.q + kb * vb.q + kc * vc.q;
        u = (ka * va.u + kb * vb.u + kc * vc.u) / divisor;
        v = (ka * va.v + kb * vb.v + kc * vc.v) / divisor;
      } else if(texture) {
        u = stepped(uStep, sampleX, sampleY), v = stepped(vStep, sampleX, sampleY);
      }
      shade(pixel, texture, x, y, z, color, specular, u, v, fogAmount(blend(va.fog, vb.fog, vc.fog)));
    }
  }
}

auto GE::point(PixelState& pixel, Sampler* texture, const Vertex& at) -> void {
  s32 x = fixed(at.x) >> 4, y = fixed(at.y) >> 4;
  if(x < pixel.left || x > pixel.right || y < pixel.top || y > pixel.bottom) return;
  if(texture) texture->linear = chooseFilter(commands[TextureFilter], 1.0f);
  shade(pixel, texture, x, y, u32(std::clamp(at.z, 0.0f, 65535.0f)), at.color, at.specular, at.u, at.v,
        fogAmount(at.fog));
}
