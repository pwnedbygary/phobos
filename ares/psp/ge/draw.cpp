//Drawing: PRIM's vertices become primitives, and the pixels each covers go through the pixel pipeline (pixel.cpp),
//textured (texture.cpp) if TEXTURE_MAPPING_ENABLE says so. In through mode (2D) positions are already pixels (with
//four fraction bits: the GE works in sixteenths of a pixel) and texture coordinates are texels; in 3D, transform.cpp
//first puts the vertices on the screen, and cuts triangles at the near plane.
//
//  - Sprites: a rectangle between each pair of vertices, in the second's color and depth. It covers the pixels whose
//    middles are inside, the left and top edges included and the right and bottom ones not, its left edge reaching a
//    sixteenth further left. Texture coordinates run from one vertex's to the other's, across with x and down with
//    y; but if the corners are bottom-left and top-right (in either order), the texture is turned a quarter: its
//    coordinates run down with x and across with y. In 3D the texture coordinates follow the perspective, and the
//    fog is split at the middle column (see rectangle()).
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
//tools/psp-measure), where PPSSPP's software renderer, which the rest follows, has triangles sampled 7/16 in. Not
//yet: lines, and PRIM's kind 7, which goes on with the last primitive's vertices.)
//
//How it's done: each primitive is first set up (here), and then drawn (raster.cpp). Setting up works out once
//everything about the primitive that doesn't change from pixel to pixel (which rows and columns it may cover, its
//edges, what its colors, depth and texture coordinates are blended from, its filter) into a Job; drawing then works
//out each pixel from that with exactly the arithmetic a pixel always had, so a job's rows can be drawn in any order,
//a few at a time, by any thread, and come out the same. The settings a primitive is drawn with (the pixel pipeline's,
//the texture's) are its Look, shared by all the jobs it makes.

//A screen position in pixels as the GE holds it, in sixteenths. Wild values (from garbage vertices) are held to the
//GE's range.
static auto fixed(float position) -> s32 {
  if(!(position >= -4096 && position <= 4096)) position = position > 0 ? 4096 : -4096;
  return s32(position * 16);
}

static auto floorDivide(s32 value, s32 by) -> s32 { return value >= 0 ? value / by : -((-value + by - 1) / by); }

//The settings a primitive is drawn with: these pipeline and texture settings, with the commands' texture function.
auto GE::lookFor(const PixelState& pixel, const Sampler* texture) const -> Look {
  Look look;
  look.pixel = pixel;
  look.textured = texture != nullptr;
  if(texture) look.texture = *texture;
  look.function = commands[TextureFunction] & 7;
  look.withAlpha = commands[TextureFunction] >> 8 & 1;
  look.doubled = commands[TextureFunction] >> 16 & 1;
  look.environment = commands[TextureEnvironmentColor];
  return look;
}

auto GE::primitive(u32 kind, u32 count) -> void {
  if(!drawing.deferring) settle();  //(called by itself, outside run(): what the last list left being drawn first)
  auto format = vertexFormat();
  u32 ambient = (commands[AmbientColor] & 0xff'ffff) | (commands[AmbientAlpha] & 0xff) << 24;
  auto& vertices = primitiveVertices;  //(kept from one primitive to the next, with room for them)
  vertices.clear();
  for(u32 n = 0; n < count; n++) {
    //(indices and vertices in VRAM that primitives waiting to be drawn draw over: they're drawn first, threads.cpp)
    if(format.indexFormat) drawnFirst(indexAddress + n * format.indexFormat, format.indexFormat);
    u32 index = format.indexFormat ? readIndex(n, format) : n;
    u32 address = vertexAddress + index * format.size;
    drawnFirst(address, format.size);
    vertices.push_back(readVertex(address, format));
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
  bool textured = (commands[TextureMappingEnable] & 1) && !pixel.clear;
  Look look = lookFor(pixel, textured ? &texture : nullptr);
  //Where it may draw: inside the scissor rectangle, and in 2D, where its vertices' sprites, triangles or points can
  //reach (the pixels rectangle(), triangle() and point() would cover between its outermost vertices); and in 2D,
  //the rows of its texture it can take texels from: those its vertices' v reach, two more for the filter and
  //stepping (in eights), when nothing can repeat round to the far end (below), or v is held at the top. A texture
  //of more rows is often a picture of fewer (a frame buffer of 272), the rest maybe where this one draws.
  Region region{pixel.left, pixel.top, pixel.right, pixel.bottom};
  u32 rows = ~0u;
  if(format.through && !vertices.empty()) {
    s32 minX = 65536, maxX = -65536, minY = 65536, maxY = -65536;
    f64 minV = 65536, maxV = -65536;
    for(auto& vertex : vertices) {
      minX = std::min(minX, fixed(vertex.x)), maxX = std::max(maxX, fixed(vertex.x));
      minY = std::min(minY, fixed(vertex.y)), maxY = std::max(maxY, fixed(vertex.y));
      minV = std::min<f64>(minV, vertex.v), maxV = std::max<f64>(maxV, vertex.v);
    }
    //A sprite turned a quarter has v running across x, and its first column's middle may lie a sixteenth of a pixel
    //left of its left corner (its left edge reaches that much further: rectangle()), where v is a sixteenth of a
    //pixel's step (|dv| over the corners' distance in sixteenths) past the corner's. Its v's reach widens by that,
    //and by a 65536th of a texel more, which the rounding of v there and here can't come near.
    if(kind == Sprites) {
      for(u32 n = 0; n + 1 < count; n += 2) {
        auto &from = vertices[n], &to = vertices[n + 1];
        s32 x0 = fixed(from.x), y0 = fixed(from.y), x1 = fixed(to.x), y1 = fixed(to.y);
        if(x0 == x1 || y0 == y1 || (x0 < x1) == (y0 < y1)) continue;
        f64 beyond = std::abs(f64(to.v) - f64(from.v)) / std::abs(x1 - x0) + 1.0 / 65536;
        minV = std::min(minV, std::min<f64>(from.v, to.v) - beyond);
        maxV = std::max(maxV, std::max<f64>(from.v, to.v) + beyond);
      }
    }
    if(kind == Points) {
      region.left = std::max(region.left, minX >> 4), region.right = std::min(region.right, maxX >> 4);
      region.top = std::max(region.top, minY >> 4), region.bottom = std::min(region.bottom, maxY >> 4);
    } else {
      region.left = std::max(region.left, floorDivide(minX - 9 + 15, 16));
      region.right = std::min(region.right, floorDivide(maxX - 8, 16));
      region.top = std::max(region.top, floorDivide(minY - 8 + 15, 16));
      region.bottom = std::min(region.bottom, floorDivide(maxY - 8, 16));
    }
    //(a sprite's or point's v stays inside that reach, so it repeats round only from below 0, or below a half when
    //it may be filtered; a triangle's steps may take it a hair past its vertices')
    u32 height = std::min<u32>(texture.height, 512);
    bool filters = commands[TextureFilter] & 0x101;
    float lowest = kind == Sprites || kind == Points ? (filters ? 0.5f : 0.0f) : 2.0f;
    if((minV >= lowest || texture.clampV) && maxV + 2 < height) {
      u32 reach = maxV + 2 > 0 ? u32(maxV + 2) : 0;  //(all of them below the top, held at it)
      rows = std::min<u32>((reach + 8) & ~7u, height);
    }
  }
  if(textured) look.decoded = decode(look.texture, pixel, region, rows);
  //Waiting in the batch, to be drawn in bands with the rest (threads.cpp); or drawn at once, after what waits. A
  //texture read from memory as it's drawn (texture.cpp) has it drawn at once.
  drawing.recording = !(look.textured && !look.texture.decoded) && defer(pixel, region);
  if(!drawing.recording) flush();
  const Look& drawn = drawing.recording ? drawing.batch->looks.emplace_back(std::move(look)) : look;
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
    if(format.through) triangle(drawn, a, b, c, facing, false);
    else clipTriangle(drawn, t, a, b, c, facing);
  };
  touched = {};
  switch(kind) {
  case Points:
    for(auto& vertex : vertices) {
      if(!vertex.outside) point(drawn, vertex);
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
    for(u32 n = 0; n + 2 < count; n++) {
      drawTriangle(vertices[n], vertices[n + 1], vertices[n + 2], n & 1 ? -facing : facing);
    }
    break;
  case TriangleFan:
    for(u32 n = 1; n + 1 < count; n++) drawTriangle(vertices[0], vertices[n], vertices[n + 1], facing);
    break;
  case Sprites:
    for(u32 n = 0; n + 1 < count; n += 2) {
      if(!format.through && outOfSight(t.depthClamp, {&vertices[n], &vertices[n + 1]})) continue;
      rectangle(drawn, vertices[n], vertices[n + 1], !format.through);
    }
    break;
  }
  //what it may have drawn over (or will, once the batch is drawn), for whoever keeps a copy of memory (the
  //recompiler, decoded textures), and for the batch
  for(auto& range : touched) {
    if(range.high < range.low) continue;
    memory.changed(Memory::VRAMBase + range.low, range.high - range.low + 1);
    if(drawing.recording) {
      for(u32 page = range.low >> 12; page <= range.high >> 12; page++) drawing.batch->pending.set(page);
    }
  }
  drawing.recording = false;
}

//A job set up: drawn, and the bytes of VRAM it may write noted (touched: its frame buffer's rows, and its depth
//buffer's, by the 16 KiB the GE rearranges each in, where it tests depth), for primitive() to report.
auto GE::submit(const Job& job) -> void {
  if(job.firstX > job.lastX || job.firstY > job.lastY) return;
  auto& p = job.look->pixel;
  auto note = [&](u32 which, u32 from, u32 to, bool depth) {  //VRAM offsets, before wrapping at its end
    if(to - from >= Memory::VRAMSize - 1 || (from & ~(Memory::VRAMSize - 1)) != (to & ~(Memory::VRAMSize - 1))) {
      from = 0, to = Memory::VRAMSize - 1;  //round VRAM's end: all of it
    }
    from &= Memory::VRAMSize - 1, to &= Memory::VRAMSize - 1;
    if(depth) from &= ~0x3fffu, to |= 0x3fff;
    touched[which].low = std::min(touched[which].low, from);
    touched[which].high = std::max(touched[which].high, to);
  };
  u32 bytes = p.format == 3 ? 4 : 2;
  note(0, p.frameBuffer + (job.firstY * p.stride + job.firstX) * bytes,
       p.frameBuffer + (job.lastY * p.stride + job.lastX) * bytes + bytes - 1, false);
  if(p.clear ? p.clearDepth : p.depthTest) {  //(reading it as well: the batch's own while it waits, threads.cpp)
    note(1, p.depthBuffer + (job.firstY * p.depthStride + job.firstX) * 2,
         p.depthBuffer + (job.lastY * p.depthStride + job.lastX) * 2 + 1, true);
  }
  if(drawing.recording) record(job);
  else rasterize(job, job.firstY, job.lastY);
}

//The fog at a pixel, 0-255, from its 0-1: rounded down, 1 or more giving 255 (as PPSSPP has it).
static auto fogAmount(float fog) -> u32 {
  if(std::signbit(fog)) return 0;
  if(!(fog < 1)) return 255;
  return u32(fog * 256);
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

//perspective: 3D. A 3D sprite is drawn as a PSP draws it (measured, docs/psp-core.md, round 3's 3d-sprite-fog and
//3d-sprite-texels, a sprite from a near corner at the top left to a far one at the bottom right):
//  - Its fog is split at the middle column between its corners, and each half takes the fog of the corner on the
//    other side: there the left half took the far corner's fog. Only that order was measured (the first corner at
//    the top left); PPSSPP's software renderer splits it this way whichever corner comes first, and so does this.
//    Where the middle falls to the sixteenth isn't measured either (the case allows anywhere in a column): here it's
//    halfway, rounded down, and the right half's left edge reaches a sixteenth further left, as any sprite's does,
//    so a column on the middle is the right half's.
//  - Its texture coordinates follow the perspective: across x, u / w and 1 / w go from the left corner's to the right
//    corner's; down y, v / w goes from the top corner's to the bottom one's; and each pixel takes (u / w) / (1 / w)
//    and (v / w) / (1 / w). That's within a texel of every pixel measured; how a PSP steps and rounds them isn't
//    pinned down. A turned sprite isn't measured: its coordinate running across x is taken as u is, the one running
//    down y as v is.
//In 2D, texture coordinates are taken at each pixel's middle, stepped from the left (or top) edge's towards the
//other's (raster.cpp).
auto GE::rectangle(const Look& look, const Vertex& from, const Vertex& to, bool perspective) -> void {
  auto& pixel = look.pixel;
  s32 x0 = fixed(from.x), y0 = fixed(from.y), x1 = fixed(to.x), y1 = fixed(to.y);
  if(x0 == x1 || y0 == y1) return;
  s32 left = std::min(x0, x1), right = std::max(x0, x1), top = std::min(y0, y1), bottom = std::max(y0, y1);
  Job job{};
  job.kind = Job::Kind::Sprite;
  job.look = &look;
  //The pixels whose middles (eight sixteenths in) are inside, the left and top edges included and the right and
  //bottom ones not, as for triangles; but the left edge reaches a sixteenth further left (measured on a PSP,
  //docs/psp-core.md: a column is drawn while the left edge is at most 9/16 into it).
  job.firstX = std::max(floorDivide(left - 9 + 15, 16), pixel.left);
  job.lastX = std::min(floorDivide(right - 8 - 1, 16), pixel.right);
  job.firstY = std::max(floorDivide(top - 8 + 15, 16), pixel.top);
  job.lastY = std::min(floorDivide(bottom - 8 - 1, 16), pixel.bottom);
  bool turned = (x0 < x1) != (y0 < y1);  //bottom-left and top-right corners: the texture turns a quarter
  if(look.textured) {
    float across = std::abs(turned ? to.v - from.v : to.u - from.u) / ((right - left) / 16.0f);
    job.linear = chooseFilter(commands[TextureFilter], across);
  }
  auto& s = job.sprite;
  s.z = u32(std::clamp(to.z, 0.0f, 65535.0f));
  s.color = to.color, s.specular = to.specular;
  const Vertex& leftCorner = x0 < x1 ? from : to;
  const Vertex& rightCorner = x0 < x1 ? to : from;
  s.leftFog = fogAmount(rightCorner.fog), s.rightFog = fogAmount(leftCorner.fog);  //the halves' fog (3D only)
  s.middle = left + (right - left) / 2;
  s.turned = turned;
  //In 2D, the coordinate across x runs from one corner's to the other's as x runs from one's sixteenth to the
  //other's (from the left one's, by a step a pixel: shortStep()), and the one down y likewise down y.
  auto axis = [](f64 first, f64 second, s32 start, s32 end, f64& from, f64& step, s32& at) {
    if(start > end) std::swap(first, second), std::swap(start, end);
    from = first, at = start, step = shortStep(16 * (second - first) / f64(end - start));
  };
  axis(turned ? from.v : from.u, turned ? to.v : to.u, x0, x1, s.columnFirst, s.columnStep, s.columnStart);
  axis(turned ? from.u : from.v, turned ? to.u : to.v, y0, y1, s.rowFirst, s.rowStep, s.rowStart);
  //In 3D, the coordinate running across x (u, or v when turned) and the one running down y, each over w at the
  //corners; and 1 / w, which runs across x. A corner with no w in front of the camera falls back on 2D's way.
  s.divided = perspective && from.clip[3] > 0 && to.clip[3] > 0;
  const Vertex& topCorner = y0 < y1 ? from : to;
  const Vertex& bottomCorner = y0 < y1 ? to : from;
  s.left = left, s.right = right, s.top = top, s.bottom = bottom;
  s.leftInverse = 1.0 / leftCorner.clip[3], s.rightInverse = 1.0 / rightCorner.clip[3];
  s.leftAcross = (turned ? leftCorner.v : leftCorner.u) * s.leftInverse;
  s.rightAcross = (turned ? rightCorner.v : rightCorner.u) * s.rightInverse;
  s.topDown = (turned ? topCorner.u : topCorner.v) / topCorner.clip[3];
  s.bottomDown = (turned ? bottomCorner.u : bottomCorner.v) / bottomCorner.clip[3];
  submit(job);
}

//A triangle. facing: 0 draws it either way round; 1 only if its corners run clockwise on the screen (y down), -1
//only counterclockwise. perspective: 3D, its texture coordinates blended as the perspective has them.
auto GE::triangle(const Look& look, const Vertex& a, const Vertex& b, const Vertex& c, s32 facing,
                  bool perspective) -> void {
  auto& pixel = look.pixel;
  struct Corner { s64 x, y; const Vertex* vertex; };
  Corner p[3] = {{fixed(a.x), fixed(a.y), &a}, {fixed(b.x), fixed(b.y), &b}, {fixed(c.x), fixed(c.y), &c}};
  s64 area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);  //above 0: clockwise
  if(area == 0) return;
  if(facing && (area > 0) != (facing > 0)) return;
  if(area < 0) std::swap(p[1], p[2]), area = -area;  //the same corners, turned the way the edges are worked out for
  //Whether the edge from-to is a right edge or a flat bottom one (the other corner left of or above it): a pixel
  //exactly on such an edge isn't drawn.
  auto rightOrBottom = [](const Corner& other, const Corner& from, const Corner& to) {
    if(from.y == to.y) return other.y < from.y;
    return other.x < from.x + (to.x - from.x) * (other.y - from.y) / (to.y - from.y);
  };
  Job job{};
  job.kind = Job::Kind::Triangle;
  job.look = &look;
  auto& r = job.triangle;
  r.bias[0] = rightOrBottom(p[0], p[1], p[2]) ? -1 : 0;
  r.bias[1] = rightOrBottom(p[1], p[2], p[0]) ? -1 : 0;
  r.bias[2] = rightOrBottom(p[2], p[0], p[1]) ? -1 : 0;
  s64 minX = std::min({p[0].x, p[1].x, p[2].x}), maxX = std::max({p[0].x, p[1].x, p[2].x});
  s64 minY = std::min({p[0].y, p[1].y, p[2].y}), maxY = std::max({p[0].y, p[1].y, p[2].y});
  job.firstX = std::max<s32>(floorDivide(s32(minX) - 8 + 15, 16), pixel.left);
  job.lastX = std::min<s32>(floorDivide(s32(maxX) - 8, 16), pixel.right);
  job.firstY = std::max<s32>(floorDivide(s32(minY) - 8 + 15, 16), pixel.top);
  job.lastY = std::min<s32>(floorDivide(s32(maxY) - 8, 16), pixel.bottom);

  r.flat = !(commands[ShadeMode] & 1);
  r.flatColor = c.color, r.flatSpecular = c.specular;  //flat shading: the last vertex's, however the corners turn
  r.shines = a.specular || b.specular || c.specular;
  r.perspective = perspective;
  const Vertex &va = *p[0].vertex, &vb = *p[1].vertex, &vc = *p[2].vertex;
  if(look.textured) {
    float texels = std::abs((vb.u - va.u) * (vc.v - va.v) - (vb.v - va.v) * (vc.u - va.u));
    job.linear = chooseFilter(commands[TextureFilter], std::sqrt(texels / (area / 256.0f)));
  }
  r.total = float(area);
  for(u32 k = 0; k < 3; k++) {
    auto& v = *p[k].vertex;
    r.x[k] = p[k].x, r.y[k] = p[k].y;
    r.color[k] = v.color, r.specular[k] = v.specular;
    r.z[k] = v.z, r.fog[k] = v.fog, r.u[k] = v.u, r.v[k] = v.v, r.q[k] = v.q, r.w[k] = v.clip[3];
  }
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
  r.startX = p[leftmost].x, r.startY = p[leftmost].y;
  if(look.textured && !perspective) {
    const Vertex& start = *p[leftmost].vertex;
    auto u = stepsFor(va.u, vb.u, vc.u, start.u), v = stepsFor(va.v, vb.v, vc.v, start.v);
    r.uStart = u.start, r.uAcross = u.across, r.uDown = u.down;
    r.vStart = v.start, r.vAcross = v.across, r.vDown = v.down;
  }
  submit(job);
}

auto GE::point(const Look& look, const Vertex& at) -> void {
  auto& pixel = look.pixel;
  s32 x = fixed(at.x) >> 4, y = fixed(at.y) >> 4;
  if(x < pixel.left || x > pixel.right || y < pixel.top || y > pixel.bottom) return;
  Job job{};
  job.kind = Job::Kind::Point;
  job.look = &look;
  job.firstX = job.lastX = x, job.firstY = job.lastY = y;
  if(look.textured) job.linear = chooseFilter(commands[TextureFilter], 1.0f);
  job.point = {x, y, u32(std::clamp(at.z, 0.0f, 65535.0f)), at.color, at.specular, fogAmount(at.fog), at.u, at.v};
  submit(job);
}

//One sprite or triangle drawn by itself, with these settings and the commands' texture function: how the tests
//draw a single one (primitive() sets up one Look for all of its own).
auto GE::rectangle(PixelState& pixel, Sampler* texture, const Vertex& from, const Vertex& to, bool perspective)
  -> void {
  Look look = lookFor(pixel, texture);
  rectangle(look, from, to, perspective);
}

auto GE::triangle(PixelState& pixel, Sampler* texture, const Vertex& a, const Vertex& b, const Vertex& c,
                  s32 facing, bool perspective) -> void {
  Look look = lookFor(pixel, texture);
  triangle(look, a, b, c, facing, perspective);
}
