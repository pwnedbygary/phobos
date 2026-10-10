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
//    counting only on left and top edges, so triangles sharing an edge don't both draw it. Texture coordinates are
//    blended across from the corners at each pixel's middle (in 2D, stepped from a corner: shortStep()); color, fog
//    and depth are stepped from a corner, as a PSP steps them (stepped()), or with flat shading (SHADE_MODE 0) the
//    color is the last vertex's. In 3D each corner's texture coordinate is divided by w with the GE's reciprocal
//    first (geReciprocal()), then s, t and q are stepped as the colors are; a pixel divides s and t by q the
//    same way. Colors, fog and depth aren't divided by w. Fog is turned into 0-255 at each corner first (measured:
//    ramp-fog).
//  - Culling (CULL_FACE_ENABLE, not in clear mode): with CULL 1 only triangles whose corners run clockwise on the
//    screen are drawn, with 0 only those running counterclockwise (pspsdk's sceGuFrontFace(GU_CW) sets 1). Every
//    other triangle of a strip runs the other way round, so for those it's the other way.
//  - Points: the pixel each vertex is in.
//  - Lines (each two vertices, or a strip): a pixel wide, by the "diamond exit" rule (see line()), colors, depth, fog
//    and texture coordinates blended along them, straight (unmeasured: not stepped as triangles' are). In 3D they're
//    cut at the near plane too.
//A vertex without a color takes the material's ambient color (AMBIENT_COLOR, AMBIENT_ALPHA).
//(Coverage, the sample points and how texture coordinates are stepped were measured on a PSP (docs/psp-core.md,
//tools/psp-measure), where PPSSPP's software renderer, which the rest follows, has triangles sampled 7/16 in. Lines
//follow what pspautotests recorded of them on a PSP, the rest of their rule unmeasured (line()). Not yet: PRIM's
//kind 7, which goes on with the last primitive's vertices.)
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
static auto floorDivide64(s64 value, s64 by) -> s64 { return value >= 0 ? value / by : -((-value + by - 1) / by); }

//A 2D sprite's texture coordinate across x (or down y) at the middle of pixel column (or row) at: from the one at
//sixteenth start, by its step a pixel (spriteJob()). As drawing takes each (raster.cpp, four.cpp).
static alwaysinline auto spriteAxis(f64 first, f64 step, s32 start, s32 at) -> float {
  return first + f64(at * 16 + 8 - start) / 16 * step;
}

//The texture's rows and columns a 2D sprite's job takes texels from, every one drawing it may (spriteRows(),
//spriteFours(): their coordinates' texels, and the second of each where it filters, whatever its weight), first to
//last: false where some may repeat round, or aren't numbers, or for a 3D sprite (whose texels follow the perspective
//at each pixel). A coordinate runs one way along the job (spriteAxis(): from a number by a step, both numbers, and
//rounding keeps the order), so its texels' first and last are those at the job's ends. weighed: only the texels whose
//weight isn't 0 (a filter's second texel, a fraction 0 from the first, is multiplied by 0), which are all a hardware
//renderer's sprites take anything from.
static auto spriteReach(const GE::Job& job, const GE::Sampler& t, s32 (&rows)[2], s32 (&columns)[2],
                        bool weighed = false) -> bool {
  auto& s = job.sprite;
  if(s.divided) return false;
  auto ends = [&](f64 first, f64 step, s32 start, s32 from, s32 to, u32 size, bool clamp, s32 (&reach)[2]) {
    if(!std::isfinite(first) || !std::isfinite(step)) return false;
    s32 low = std::numeric_limits<s32>::max(), high = std::numeric_limits<s32>::min();
    s32 last = std::min<s32>(size, 512) - 1;
    for(s32 at : {from, to}) {
      float coordinate = spriteAxis(first, step, start, at);
      if(std::isnan(coordinate)) return false;
      auto spot = GE::texelSpot(coordinate, job.linear);
      low = std::min(low, spot.first), high = std::max(high, spot.first + (job.linear && (!weighed || spot.fraction)));
    }
    if(clamp) low = std::clamp(low, 0, last), high = std::clamp(high, 0, last);
    else if(low < 0 || high > last) return false;
    reach[0] = low, reach[1] = high;
    return true;
  };
  s32 across[2], down[2];
  if(!ends(s.columnFirst, s.columnStep, s.columnStart, job.firstX, job.lastX, s.turned ? t.height : t.width,
           s.turned ? t.clampV : t.clampU, across)) return false;
  if(!ends(s.rowFirst, s.rowStep, s.rowStart, job.firstY, job.lastY, s.turned ? t.width : t.height,
           s.turned ? t.clampU : t.clampV, down)) return false;
  for(u32 n : range(2)) rows[n] = s.turned ? across[n] : down[n], columns[n] = s.turned ? down[n] : across[n];
  return true;
}

//Whether every pixel drawn with these settings is left as it was, whatever its color, depth and fog: blending keeps
//the frame buffer's color (the source times a fixed 0, plus the destination times a fixed 255, which comes back
//exactly: (2d + 1) * 511 >> 10 is d for every d up to 255), with no dithering or logic operation after it, the
//stencil kept (no stencil test) and no depth written; not clear mode. The pixel written back is then the very bits it
//was: each format's channels, widened and narrowed again, are as they were (pixel.cpp), and the tests only drop
//pixels.
static auto keepsPixels(const GE::PixelState& p) -> bool {
  return !p.clear && p.blend && p.blendOperation == 0 && p.blendSource >= 10 && p.blendDestination >= 10 &&
         p.fixedA == 0 && p.fixedB == 0xff'ffff && !p.dither && !p.logicOp && !p.stencilTest && !p.depthWrite;
}

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

//PRIM's (and BOUNDING_BOX's) count vertices into primitiveVertices, as the vertex type lays them out, from the vertex
//address or by the indices at the index address; then the next carries on where this one stopped: after its
//indices if it had them, else after its vertices.
auto GE::readVertices(u32 count, const VertexFormat& format) -> void {
  u32 ambient = (commands[AmbientColor] & 0xff'ffff) | (commands[AmbientAlpha] & 0xff) << 24;
  u32 indexBytes = format.indexFormat == 3 ? 4 : format.indexFormat;
  auto& vertices = primitiveVertices;  //(kept from one primitive to the next, with room for them)
  vertices.clear();
  for(u32 n = 0; n < count; n++) {
    //(indices and vertices in VRAM that primitives waiting to be drawn draw over: they're drawn first, threads.cpp)
    if(format.indexFormat) drawnFirst(indexAddress + n * indexBytes, indexBytes);
    u32 index = format.indexFormat ? readIndex(n, format) : n;
    u32 address = vertexAddress + index * format.size;
    drawnFirst(address, format.size);
    vertices.push_back(readVertex(address, format));
    if(!format.colorFormat) vertices.back().color = ambient;
  }
  if(format.indexFormat) indexAddress += count * indexBytes;
  else vertexAddress += count * format.size;
}

auto GE::primitive(u32 kind, u32 count) -> void {
  if(!drawing.deferring) settle();  //(called by itself, outside run(): what the last list left being drawn first)
  auto format = vertexFormat();
  readVertices(count, format);
  if(kind == 7) return note("PRIM's kind 7 (going on with the last primitive's vertices) isn't emulated yet");
  if(!format.positionFormat) return;  //vertices without positions draw nothing (as PPSSPP has it)
  drawVertices(kind, format, primitiveVertices, count);
}

//Draws vertices (as the vertex type laid them out; 3D ones are transformed here) as primitives of kind: PRIM's, or a
//curved surface's (curves.cpp), whose rows are strips of strip vertices each, drawn one after another as if each were
//a PRIM of its own (a strip, or points, of all of them: strip as many as there are).
auto GE::drawVertices(u32 kind, const VertexFormat& format, std::vector<Vertex>& vertices, u32 strip) -> void {
  u32 count = vertices.size();
  strip = std::max(strip, 1u);
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
  //of more rows is often a picture of fewer (a frame buffer of 272), the rest maybe where this one draws. Its
  //columns likewise, by u (for a hardware renderer, which copies no more of a frame buffer than they: holds()).
  Region region{pixel.left, pixel.top, pixel.right, pixel.bottom};
  u32 rows = ~0u, columns = ~0u;
  if(format.through && !vertices.empty()) {
    s32 minX = 65536, maxX = -65536, minY = 65536, maxY = -65536;
    f64 minU = 65536, maxU = -65536, minV = 65536, maxV = -65536;
    for(auto& vertex : vertices) {
      minX = std::min(minX, fixed(vertex.x)), maxX = std::max(maxX, fixed(vertex.x));
      minY = std::min(minY, fixed(vertex.y)), maxY = std::max(maxY, fixed(vertex.y));
      minU = std::min<f64>(minU, vertex.u), maxU = std::max<f64>(maxU, vertex.u);
      minV = std::min<f64>(minV, vertex.v), maxV = std::max<f64>(maxV, vertex.v);
    }
    //A sprite turned a quarter has v running across x, and its first column's middle may lie a sixteenth of a pixel
    //left of its left corner (its left edge reaches that much further: rectangle()), where v is a sixteenth of a
    //pixel's step (|dv| over the corners' distance in sixteenths) past the corner's. Its v's reach widens by that,
    //and by a 65536th of a texel more, which the rounding of v there and here can't come near; its u, running down
    //y, likewise by a sixteenth of a pixel's step down.
    if(kind == Sprites) {
      for(u32 n = 0; n + 1 < count; n += 2) {
        auto &from = vertices[n], &to = vertices[n + 1];
        s32 x0 = fixed(from.x), y0 = fixed(from.y), x1 = fixed(to.x), y1 = fixed(to.y);
        if(x0 == x1 || y0 == y1 || (x0 < x1) == (y0 < y1)) continue;
        f64 beyond = std::abs(f64(to.v) - f64(from.v)) / std::abs(x1 - x0) + 1.0 / 65536;
        minV = std::min(minV, std::min<f64>(from.v, to.v) - beyond);
        maxV = std::max(maxV, std::max<f64>(from.v, to.v) + beyond);
        beyond = std::abs(f64(to.u) - f64(from.u)) / std::abs(y1 - y0) + 1.0 / 65536;
        minU = std::min(minU, std::min<f64>(from.u, to.u) - beyond);
        maxU = std::max(maxU, std::max<f64>(from.u, to.u) + beyond);
      }
    }
    if(kind == Points) {
      region.left = std::max(region.left, minX >> 4), region.right = std::min(region.right, maxX >> 4);
      region.top = std::max(region.top, minY >> 4), region.bottom = std::min(region.bottom, maxY >> 4);
    } else if(kind == Lines || kind == LineStrip) {  //the pixels whose diamonds the lines can reach (line())
      region.left = std::max(region.left, floorDivide(minX - 1, 16));
      region.right = std::min(region.right, floorDivide(maxX, 16));
      region.top = std::max(region.top, floorDivide(minY - 1, 16));
      region.bottom = std::min(region.bottom, floorDivide(maxY, 16));
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
    u32 width = std::min<u32>(texture.width, 512);
    if((minU >= lowest || texture.clampU) && maxU + 2 < width) {
      u32 reach = maxU + 2 > 0 ? u32(maxU + 2) : 0;
      columns = std::min<u32>((reach + 8) & ~7u, width);
    }
  }
  //With a hardware renderer (ge.hpp's Renderer) the primitives go to it, their settings first. Its texture is the
  //renderer's own where it's in a frame buffer the GPU has drawn (render to texture), else always decoded, even one
  //the primitive draws over itself (the GPU reads it as it was before the primitive, as the software renderer's
  //drawing a pixel at a time from memory doesn't: Region{0, 0, -1, -1} reaches nothing).
  //A PRIM the renderer refuses (begin()) is drawn here instead, as without one: what it drew put back in memory
  //first, and the texture decoded for the region it draws in.
  hardware = renderer && renderer->ready();
  //A primitive drawn with settings that leave every pixel as it was (keepsPixels()) is set up as ever, and what it
  //may draw over reported, but it isn't drawn, nor its texture decoded: nothing it would draw could change. Unless
  //reading its texels would be reported (a format the PSP doesn't have, bytes with no memory behind them: texel()).
  auto quiet = [&] {
    if(texture.format > 10) return false;
    u32 low, high;
    textureBytes(texture, std::min<u32>(texture.height, 512), low, high);
    return memory.reaches(low, high - low);
  };
  skipping = !hardware && keepsPixels(pixel) && (!textured || quiet());
  //A 2D sprite's texels are taken where its pixels' middles fall, so the rows it reaches are exactly those
  //spriteReach() finds, often fewer than its vertices' reach above can say: a sprite sampling a frame buffer (a
  //texture of 512 rows) from its top edge, filtered, may reach round to the far end there for all that can tell.
  //A hardware renderer takes those rows and columns exactly (holds(): the part of a frame buffer it copies), as its
  //sprites' texels are the GE's, those whose weight is 0 left out (its fetches are held inside the copy, and what
  //they find multiplied by 0). (Midnight Club 3's bloom samples its 480x272 picture as a texture 512 rows tall,
  //whose last rows are other frame buffers': the GPU takes it from its frame buffer, not from memory.)
  u32 drawnRows = rows, heldRows = rows, heldColumns = columns;
  if(textured && !skipping && format.through && kind == Sprites) {
    s32 reached = 1, across = 1;
    bool exact = true;
    for(u32 n = 0; n + 1 < count && exact; n += 2) {
      Job job;
      if(!spriteJob(look, vertices[n], vertices[n + 1], false, job)) continue;
      if(job.firstX > job.lastX || job.firstY > job.lastY) continue;
      s32 rowsReached[2], columnsReached[2];
      if(!(exact = spriteReach(job, look.texture, rowsReached, columnsReached, hardware))) break;
      reached = std::max(reached, rowsReached[1] + 1), across = std::max(across, columnsReached[1] + 1);
    }
    if(exact) {
      drawnRows = std::min<u32>(rows, (reached + 7) & ~7);  //(in eights, as above)
      heldRows = std::min<u32>(rows, reached), heldColumns = std::min<u32>(columns, across);
    }
  }
  //(a 2D sprite drawing over its own texture is still decoded where a copy taken first draws the same: readsAhead())
  struct Ahead { GE& ge; const Look& look; const std::vector<Vertex>& vertices; u32 count; bool sprites; };
  Ahead ahead{*this, look, vertices, count, format.through && kind == Sprites};
  auto copyDraws = [&ahead] { return ahead.sprites && ahead.ge.readsAhead(ahead.look, ahead.vertices, ahead.count); };
  if(textured && !skipping && !(hardware && renderer->holds(*this, look.texture, heldRows, heldColumns))) {
    //Hardware keeps its own copy of a frame buffer: don't defer a software-batch wait for it.
    decode(look, hardware ? Region{0, 0, -1, -1} : region, drawnRows, !hardware, copyDraws);
    if(!look.texture.decoded && !look.deferRows) look.texture.bytes = direct(look.texture);
  }
  if(hardware && !renderer->begin(*this, look, format.through, region)) {
    renderer->finish(*this);
    hardware = false;
    if(textured) {
      decode(look, region, rows, true, copyDraws);
      if(!look.texture.decoded && !look.deferRows) look.texture.bytes = direct(look.texture);
    }
  }
  //Waiting in the batch, to be drawn in bands with the rest (threads.cpp); or drawn at once, after what waits. A
  //texture read from memory as it's drawn (texture.cpp) has it drawn at once. One deferred until the batch starts
  //(render to texture) still waits in the batch: ensureDecoded fills it before any band draws.
  drawing.recording = !hardware && !skipping && !(look.textured && !look.texture.decoded && !look.deferRows) &&
                      defer(pixel, region);
  if(!drawing.recording && !hardware && !skipping) flush();
  const Look& drawn = drawing.recording ? drawing.batch->looks.emplace_back(std::move(look)) : look;
  if(drawing.recording && drawn.deferRows) {  //(the CPU waits for its texture's pages till it's decoded: threads.cpp)
    for(u32 page = drawn.deferFirst >> 12; page <= drawn.deferLast >> 12; page++) drawing.batch->reads.set(page);
  }
  Transform t{};
  s32 facing = (commands[CullFaceEnable] & 1) && !pixel.clear ? (commands[Cull] & 1 ? 1 : -1) : 0;
  if(!format.through) {
    t = transformState();
    t.weights = format.weightFormat ? format.weights : 0;
    t.textureWidth = texture.width, t.textureHeight = texture.height;
    t.vertexColor = format.colorFormat != 0;
    //(a renderer transforming its own: the triangles the GE would draw whole handed over as they are)
    bool triangles = kind == Triangles || kind == TriangleStrip || kind == TriangleFan;
    if(hardware && triangles && renderer->meshes(*this, t, count)) {
      meshTriangles(drawn, t, kind, strip, facing, vertices);
      return void(hardware = false);
    }
    for(auto& vertex : vertices) transform(vertex, t);
  }
  auto drawTriangle = [&](const Vertex& a, const Vertex& b, const Vertex& c, s32 facing) {
    if(format.through) triangle(drawn, a, b, c, facing, false);
    else clipTriangle(drawn, t, a, b, c, facing);
  };
  touched = {};
  switch(kind) {
  case Points:
    //a point is a primitive of one corner: in 3D, one whose z / w is past either end of the depths isn't drawn
    //either, as a sprite's or a line's corners all past one end aren't (the PSP's 3d-rules pictures, measured in
    //round 3, come closer so: two fewer pixels apart)
    for(auto& vertex : vertices) {
      if(format.through ? !vertex.outside : !outOfSight(t.depthClamp, {&vertex})) point(drawn, vertex);
    }
    break;
  case Lines:
  case LineStrip:
    if(commands[AntiAliasEnable] & 1) note("anti-aliased lines (ANTI_ALIAS_ENABLE) are drawn aliased");
    for(u32 first = 0; first < count; first += strip) {
      u32 end = std::min(first + strip, count);
      for(u32 n = first; n + 1 < end; n += kind == Lines ? 2 : 1) {
        if(format.through) line(drawn, vertices[n], vertices[n + 1], false);
        else clipLine(drawn, t, vertices[n], vertices[n + 1]);
      }
    }
    break;
  case Triangles:
    for(u32 n = 0; n + 2 < count; n += 3) drawTriangle(vertices[n], vertices[n + 1], vertices[n + 2], facing);
    break;
  case TriangleStrip:
    for(u32 first = 0; first < count; first += strip) {
      u32 end = std::min(first + strip, count);
      for(u32 n = first; n + 2 < end; n++) {
        drawTriangle(vertices[n], vertices[n + 1], vertices[n + 2], (n - first) & 1 ? -facing : facing);
      }
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
  //recompiler, decoded textures), and for the batch; a hardware renderer tells memory of its own pages (begin())
  if(hardware) return void(hardware = false);
  for(auto& range : touched) {
    if(range.high < range.low) continue;
    memory.changed(Memory::VRAMBase + range.low, range.high - range.low + 1);
    if(drawing.recording) {
      for(u32 page = range.low >> 12; page <= range.high >> 12; page++) drawing.batch->pending.set(page);
    }
  }
  drawing.recording = false;
  skipping = false;
}

//A 3D PRIM's triangles for a renderer that transforms and lights them itself (Renderer::meshes()), kept to the GE's
//rules: each corner's place on the screen worked out as project() has it, the triangles clipTriangle() and
//triangle() wouldn't draw dropped (out of sight, behind the camera, no area, facing away), and those reaching past
//the near plane (or with a corner behind the camera, which projects through its w as the GPU doesn't) transformed,
//cut and drawn by the GE as ever: the GPU's clipper would blend their new corners' colors and fog across the
//screen, not in clip space as the GE does. The rest are handed over whole, untransformed,
//in runs between those, so that they're drawn in order. (The corners' clip positions come through the world, view
//and projection matrices made one, once a PRIM: clipPosition()'s but for a float's rounding, which can only move a
//triangle lying on an edge of these rules across it.)
auto GE::meshTriangles(const Look& look, const Transform& t, u32 kind, u32 strip, s32 facing,
                       std::vector<Vertex>& vertices) -> void {
  u32 count = vertices.size();
  placed.resize(count), handed.clear();
  float viewWorld[12], clipping[16];
  for(u32 c = 0; c < 4; c++) turn43(t.view, t.world + c * 3, viewWorld + c * 3);
  for(u32 r = 0; r < 3; r++) viewWorld[9 + r] += t.view[9 + r];
  for(u32 c = 0; c < 4; c++) {
    for(u32 r = 0; r < 4; r++) {
      clipping[c * 4 + r] = viewWorld[c * 3] * t.projection[r] + viewWorld[c * 3 + 1] * t.projection[4 + r] +
                            viewWorld[c * 3 + 2] * t.projection[8 + r] + (c == 3 ? t.projection[12 + r] : 0.0f);
    }
  }
  for(u32 n = 0; n < count; n++) {
    auto& v = vertices[n];
    float model[3] = {v.x, v.y, v.z};
    if(t.weights) {
      float position[3] = {};
      for(u32 bone = 0; bone < t.weights; bone++) {
        float moved[3];
        times43(t.bones + bone * 12, model, moved);
        for(u32 k = 0; k < 3; k++) position[k] += moved[k] * v.weights[bone];
      }
      for(u32 k = 0; k < 3; k++) model[k] = position[k];
    }
    times44(clipping, model, placed[n].clip);
    project(placed[n], t, false);
  }
  bool again = false;
  auto hand = [&] {
    if(handed.empty()) return;
    renderer->mesh(*this, t, vertices, handed, again);
    handed.clear(), again = true;
  };
  auto nearer = [](const Vertex& v) { return v.clip[2] + v.clip[3]; };  //below zero: nearer than the near plane
  auto consider = [&](u32 a, u32 b, u32 c, s32 facing) {
    const Vertex &pa = placed[a], &pb = placed[b], &pc = placed[c];
    if(outOfSight(t.depthClamp, {&pa, &pb, &pc})) return;
    if(pa.clip[3] < 0 && pb.clip[3] < 0 && pc.clip[3] < 0) return;
    if(nearer(pa) < 0 || nearer(pb) < 0 || nearer(pc) < 0 || !(pa.clip[3] > 0 && pb.clip[3] > 0 && pc.clip[3] > 0)) {
      hand();
      Vertex corners[3] = {vertices[a], vertices[b], vertices[c]};  //(the vertices kept as they came, for the runs)
      for(auto& corner : corners) transform(corner, t);
      return clipTriangle(look, t, corners[0], corners[1], corners[2], facing);
    }
    s64 x0 = fixed(pa.x), y0 = fixed(pa.y);
    s64 area = (fixed(pb.x) - x0) * (fixed(pc.y) - y0) - (fixed(pb.y) - y0) * (fixed(pc.x) - x0);
    if(area == 0 || (facing && (area > 0) != (facing > 0))) return;
    handed.insert(handed.end(), {c, a, b});
  };
  if(kind == Triangles) {
    for(u32 n = 0; n + 2 < count; n += 3) consider(n, n + 1, n + 2, facing);
  } else if(kind == TriangleStrip) {
    for(u32 first = 0; first < count; first += strip) {
      u32 end = std::min(first + strip, count);
      for(u32 n = first; n + 2 < end; n++) consider(n, n + 1, n + 2, (n - first) & 1 ? -facing : facing);
    }
  } else {
    for(u32 n = 1; n + 1 < count; n++) consider(0, n, n + 1, facing);
  }
  hand();
}

//A job set up: drawn, and the bytes of VRAM it may write noted (touched: its frame buffer's rows, and its depth
//buffer's, by the 16 KiB the GE rearranges each in, where it tests depth), for primitive() to report.
auto GE::submit(Job& job) -> void {
  if(job.firstX > job.lastX || job.firstY > job.lastY) return;
  job.fours = fourFriendly(job);
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
  if(skipping) return;
  if(drawing.recording) record(job);
  else rasterize(job, job.firstY, job.lastY);
}

//The fog at a vertex, 0-255, from its 0-1: rounded down, 1 or more giving 255. Measured (docs/psp-core.md,
//ramp-fog): this amount is what is stepped across a triangle (stepped()) and blended along a line, not the 0-1;
//blending the 0-1 and converting at each pixel leaves the first pixel of a ramp unfogged (255) where the PSP has
//already stepped to 254.
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

//The GE's reciprocal's 128 straight chords (geReciprocal()): where chord s starts, round(2^17 / (1 + s/128)) (131072
//down to 65793, never a half), and its slope over its 256 positions, the run to the next start over 4, rounded half
//to even (half away from zero suits persp-divide, persp-floor and round 3's wall and floor a few pixels better but
//leaves persp-wall 246 pixels apart, not 23, and persp-w3 28928, not 18944). Two chords sit one unit high where
//round 5's persp-divide and persp-floor sample them, so their starts are two counts low; their slopes are the
//unadjusted ones (part 60).
struct ReciprocalChords {
  struct Chord { s32 start, slope; } chords[128];
  constexpr ReciprocalChords() : chords() {
    for(u32 s = 0; s < 128; s++) chords[s].start = s32((16'777'216 + (128 + s) / 2) / (128 + s));
    for(u32 s = 0; s < 128; s++) {
      s32 run = (s < 127 ? chords[s + 1].start : 65536) - chords[s].start;  //(below zero)
      s32 whole = run >> 2, quarters = run & 3;  //run / 4 is whole + quarters / 4, whole rounded down
      chords[s].slope = whole + (quarters == 3 || (quarters == 2 && (whole & 1)));
    }
    chords[5].start -= 2, chords[58].start -= 2;
  }
};
static constexpr ReciprocalChords reciprocalChords;

//The GE's reciprocal, for the perspective divide (measured, docs/psp-core.md, part 60). Not a division: the top 7
//bits of the magnitude's fraction pick a chord, the next 8 the position along it, and the result is that chord in
//units of 2^-16 of the fraction's reciprocal (32769 to 65536), placed back at the exponent, the sign kept. A zero, a
//denormal, an infinity or not a number gives 0.
static alwaysinline auto geReciprocal(float x) -> float {
  u32 bits;
  std::memcpy(&bits, &x, 4);
  u32 exponent = bits >> 23 & 255, chord = bits >> 16 & 127, at = bits >> 8 & 255;
  if(exponent == 0 || exponent == 255) return 0.0f;
  auto& line = reciprocalChords.chords[chord];
  s32 q = (64 * line.start + 63 + line.slope * s32(at)) >> 7;
  float value;
  if(exponent <= 237) {  //times 2^(111 - exponent), a power of two a float holds: exact
    u32 power = (238 - exponent) << 23;
    std::memcpy(&value, &power, 4);
    value *= float(q);
  } else {
    value = std::ldexp(float(q), 111 - s32(exponent));
  }
  return bits >> 31 ? -value : value;
}

//A float with its low 8 fraction bits cut off (the GE's float24): toward zero.
static auto cutLowBits(float x) -> float {
  u32 bits;
  std::memcpy(&bits, &x, 4);
  bits &= 0xffff'ff00;
  std::memcpy(&x, &bits, 4);
  return x;
}

//A product kept to 24 significant bits and cut toward zero (the pixel's s * R(q); a float multiply would round): a
//double's fraction past its top 23 bits cleared. Zeros, infinities and not a number stay as they are, and a double
//too small for its leading one to be normal is far below any float. (One below 2^-126 then rounds as a float holds
//it, with fewer bits: far below any texel.)
static alwaysinline auto trunc24(f64 x) -> float {
  u64 bits;
  std::memcpy(&bits, &x, 8);
  u32 exponent = bits >> 52 & 2047;
  if(exponent == 0 || exponent == 2047) return float(x);
  bits &= ~u64(0) << 29;
  std::memcpy(&x, &bits, 8);
  return float(x);
}

//Three values as 15-bit integers on the unit of the largest one's exponent (the largest's leading one is the
//integers' bit 14), cut toward zero; returns the unit (1 when all three are 0). A pixel rebuilds a value as the
//integer times the unit. Infinities and not a number count as 0. Mostly each integer is the value's 24 bits (the
//hidden one and the fraction) shifted down by how far its exponent is below the largest's, and 9 more; a trio with
//a denormal, or whose largest is below 2^-112, is worked out in doubles, where its unit always fits (as a float, a
//tiny trio's unit can be 0, which leaves its pixels' values 0).
static auto quantize15(const float (&value)[3], s32 (&out)[3]) -> float {
  u32 bits[3], top = 0;  //the largest exponent among the finite values
  bool tiny = false;
  for(u32 k = 0; k < 3; k++) {
    std::memcpy(&bits[k], &value[k], 4);
    u32 exponent = bits[k] >> 23 & 255;
    if(exponent == 255) continue;
    tiny |= exponent == 0 && bits[k] << 1;
    top = std::max(top, exponent);
  }
  if(!tiny && top >= 15) {
    for(u32 k = 0; k < 3; k++) {
      u32 exponent = bits[k] >> 23 & 255, shift = 9 + top - exponent;
      bool counts = exponent && exponent < 255 && shift < 24;  //(0, infinities and not a number: 0)
      s32 magnitude = counts ? s32(((bits[k] & 0x7f'ffff) | 0x80'0000) >> shift) : 0;
      out[k] = bits[k] >> 31 ? -magnitude : magnitude;
    }
    u32 unitBits = (top - 14) << 23;
    float unit;
    std::memcpy(&unit, &unitBits, 4);
    return unit;
  }
  s32 largest = -1000;  //the largest one's exponent: it's 1 to 2 times 2 to that
  for(u32 k = 0; k < 3; k++) {
    u32 exponent = bits[k] >> 23 & 255, fraction = bits[k] & 0x7f'ffff;
    if(exponent == 255 || (exponent == 0 && fraction == 0)) continue;
    largest = std::max(largest, exponent ? s32(exponent) - 127 : -118 - __builtin_clz(fraction));  //(denormal)
  }
  if(largest == -1000) return out[0] = out[1] = out[2] = 0, 1.0f;
  u64 unitBits = u64(1023 + largest - 14) << 52, inverseBits = u64(1023 - largest + 14) << 52;
  f64 unit, inverse;
  std::memcpy(&unit, &unitBits, 8);
  std::memcpy(&inverse, &inverseBits, 8);
  for(u32 k = 0; k < 3; k++) out[k] = std::isfinite(value[k]) ? s32(f64(value[k]) * inverse) : 0;
  return float(unit);
}

//How a PSP steps colors, fog and depth across a triangle (measured, docs/psp-core.md, part 48: every pixel of round
//3's color and fog ramps and bezier-flat, and round 2's gouraud, 3d-clip, 3d-floor-fog and 3d-floor-depth). Each
//starts at the corner texture coordinates are stepped from (the leftmost, the topmost of two) and is stepped from
//there, a step a sixteenth of a pixel across and one down, each in 16384ths of a level (or of a depth) and rounded
//down; so a value can come out a level under the true blend, the more likely the further from that corner. The steps
//come through the reciprocal of twice the triangle's area (in sixteenths squared) kept to 16 significant bits,
//rounded down: an area that's a power of two is exact, but a step that should be a whole number of 16384ths (17/16
//a pixel, say) then falls one short of it, or one further down. At a pixel the value is the corner's, plus each step
//times the sixteenths from that corner to the pixel's middle, rounded down (raster.cpp, four.cpp).
//  - The start is exactly the corner's value (its depth a whole number, cut down as a point's is). The reciprocal to
//    17 bits, or 15, leaves round 3's 3D ramps 144 and 320 values apart; steps in 512ths, 2048ths or 4096ths of a
//    level a pixel (not 1024ths), thousands. Depths in 2^-12 or 2^-16, 692 and 209 of 3d-floor-depth's 7728.
//  - An area of 2^bits or more, below 2^(bits + 1): the reciprocal 2^(bits + 16) / area, which keeps 16 bits.
//(stepScale() works the reciprocal out once a triangle, for each value stepped.)
struct StepScale { u32 shift; s64 reciprocal; };
static auto stepScale(s64 area) -> StepScale {
  u32 shift = 64 - __builtin_clzll(u64(area)) + 15;  //(area is above 0, and below 2^36: positions are held)
  return {shift, (s64(1) << shift) / area};
}
static auto stepped(const s64 (&x)[3], const s64 (&y)[3], StepScale scale, const s32 (&value)[3], s32 start)
  -> GE::Job::Stepped {
  u32 shift = scale.shift;
  s64 reciprocal = scale.reciprocal;
  s64 x1 = x[1] - x[0], y1 = y[1] - y[0], x2 = x[2] - x[0], y2 = y[2] - y[0];
  s64 across = s64(value[1] - value[0]) * y2 - s64(value[2] - value[0]) * y1;
  s64 down = s64(value[2] - value[0]) * x1 - s64(value[1] - value[0]) * x2;
  //(each below 2 * 65535 * 2^17, times the reciprocal's 2^16: below 2^51. Times 2^14 then down shift bits is down
  //shift - 14 bits, which rounds down as well, since shift is at least 16)
  return {across * reciprocal >> (shift - 14), down * reciprocal >> (shift - 14), start * 16384};
}

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
  Job job;
  if(!spriteJob(look, from, to, perspective, job)) return;
  if(hardware) return renderer->sprite(job);  //(its pixels, and their coordinates' steps, worked out as here)
  submit(job);
}

//Whether 2D sprites drawing over their own texture (texture.cpp's drawsOver()) come out the same drawn from a copy of
//it taken just before them (decoded) as reading it from memory as they draw (texture.cpp): so when no texel any of
//their pixels takes, where its weight isn't zero (a filter's second texel, a fraction 0 from the first, is multiplied
//by 0), is a pixel they have drawn by then, drawing their jobs one after another, each row by row and left to right
//(raster.cpp, as such a primitive is drawn). Pixels a test drops aren't drawn, but count as drawn here.
//Looked into only for unturned sprites without a depth buffer drawn, into a frame buffer laid out as the texture is in
//VRAM's plain copies (the same bytes from one row to the next, a texel a pixel): a texel is then the frame buffer's
//pixel a whole number of rows and columns from where it would be at the frame buffer's start (rows dy down, and
//within its row, past columns made up by the row after: a carry). Each job's texels are its columns' by its rows':
//one taken in a row above it, or to its left in its own row, may be one it has drawn; one in an earlier job's area,
//one that job drew.
auto GE::readsAhead(const Look& look, const std::vector<Vertex>& vertices, u32 count) const -> bool {
  auto& p = look.pixel;
  auto& t = look.texture;
  s64 bytes = p.format == 3 ? 4 : 2;
  if(p.clear || p.depthWrite || t.swizzled || t.format >= 8 || TexelBits[t.format] != bytes * 8) return false;
  u32 physical = t.address & 0x1fff'ffff;
  if(physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMWindow) return false;
  if((physical - Memory::VRAMBase) / Memory::VRAMSize & 1) return false;  //(a copy that rearranges VRAM)
  s64 pitch = s64(p.stride) * bytes, texture = (physical - Memory::VRAMBase) % Memory::VRAMSize;
  s64 width = std::min<u32>(t.width, 512), height = std::min<u32>(t.height, 512);
  if(!pitch || pitch != s64(t.bufferWidth) * bytes) return false;
  if(texture + (height - 1) * pitch + width * bytes > Memory::VRAMSize) return false;  //(all of it in this copy)
  //texel (c, r) is the frame buffer's pixel (column(c), r + down + carry(c)): apart = down rows and along bytes
  s64 apart = texture - s64(p.frameBuffer);  //(both on 16 bytes: a whole number of pixels)
  s64 down = apart >= 0 ? apart / pitch : -((-apart + pitch - 1) / pitch), along = apart - down * pitch;
  auto column = [&](s64 c) { return (along + c * bytes) % pitch / bytes; };
  auto carry = [&](s64 c) { return (along + c * bytes) / pitch; };
  std::vector<Job> jobs;
  for(u32 n = 0; n + 1 < count; n += 2) {
    Job job;
    if(!spriteJob(look, vertices[n], vertices[n + 1], false, job)) continue;
    if(job.firstX > job.lastX || job.firstY > job.lastY) continue;
    if(job.sprite.turned || job.lastX >= s32(p.stride) || jobs.size() == 256) return false;
    if(p.frameBuffer + (s64(job.lastY) * p.stride + job.lastX + 1) * bytes > Memory::VRAMSize) return false;
    jobs.push_back(job);
  }
  //each job's texels whose weights aren't 0: its columns' (each with its pixel column), its rows' (each with its row)
  struct Texel { s64 texel, at; };
  struct Taken { std::vector<Texel> columns, rows; };
  std::vector<Taken> taken(jobs.size());
  for(u32 j = 0; j < jobs.size(); j++) {
    auto& job = jobs[j];
    auto& s = job.sprite;
    for(s32 x = job.firstX; x <= job.lastX; x++) {
      auto a = texelAxis(spriteAxis(s.columnFirst, s.columnStep, s.columnStart, x), t.width, t.clampU, job.linear);
      taken[j].columns.push_back({a.first, x});
      if(job.linear && a.fraction) taken[j].columns.push_back({a.second, x});
    }
    for(s32 y = job.firstY; y <= job.lastY; y++) {
      auto a = texelAxis(spriteAxis(s.rowFirst, s.rowStep, s.rowStart, y), t.height, t.clampV, job.linear);
      taken[j].rows.push_back({a.first, y});
      if(job.linear && a.fraction) taken[j].rows.push_back({a.second, y});
    }
  }
  for(u32 j = 0; j < jobs.size(); j++) {
    auto& job = jobs[j];
    //by carry: the pixel columns its texel columns are, those inside its own area (any; and one left of the pixel
    //that took it)
    struct Carry { s64 carry; std::vector<s64> columns; bool inside = false, left = false; };
    std::vector<Carry> carries;
    for(auto& c : taken[j].columns) {
      s64 k = carry(c.texel), x = column(c.texel);
      auto at = std::find_if(carries.begin(), carries.end(), [&](auto& e) { return e.carry == k; });
      if(at == carries.end()) at = carries.insert(carries.end(), Carry{k, {}, false, false});
      at->columns.push_back(x);
      if(x >= job.firstX && x <= job.lastX) at->inside = true, at->left |= x < c.at;
    }
    std::vector<s64> rows;
    for(auto& r : taken[j].rows) rows.push_back(r.texel);
    std::sort(rows.begin(), rows.end());
    auto any = [](const std::vector<s64>& sorted, s64 low, s64 high) {
      auto at = std::lower_bound(sorted.begin(), sorted.end(), low);
      return at != sorted.end() && *at <= high;
    };
    for(auto& e : carries) std::sort(e.columns.begin(), e.columns.end());
    //an earlier job's area: drawn before any of this job's pixels
    for(u32 i = 0; i < j; i++) {
      auto& before = jobs[i];
      for(auto& e : carries) {
        if(any(e.columns, before.firstX, before.lastX) &&
           any(rows, before.firstY - down - e.carry, before.lastY - down - e.carry)) return false;
      }
    }
    //its own area: a row above the pixel's, or its own row left of it
    for(auto& r : taken[j].rows) {
      for(auto& e : carries) {
        s64 y = r.texel + down + e.carry;
        if(y < job.firstY || y > job.lastY || !e.inside) continue;
        if(y < r.at || (y == r.at && e.left)) return false;
      }
    }
  }
  return true;
}

//A sprite's job (rectangle()): false if it covers no area at all (its corners in one row or column of sixteenths).
auto GE::spriteJob(const Look& look, const Vertex& from, const Vertex& to, bool perspective, Job& job) const -> bool {
  auto& pixel = look.pixel;
  s32 x0 = fixed(from.x), y0 = fixed(from.y), x1 = fixed(to.x), y1 = fixed(to.y);
  if(x0 == x1 || y0 == y1) return false;
  s32 left = std::min(x0, x1), right = std::max(x0, x1), top = std::min(y0, y1), bottom = std::max(y0, y1);
  job = {};
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
  job.sprite = {};
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
  return true;
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
  if(hardware) return renderer->triangle(a, b, c);
  if(area < 0) std::swap(p[1], p[2]), area = -area;  //the same corners, turned the way the edges are worked out for
  //(which pixels on its edges it draws: triangleRows(), raster.cpp)
  Job job{};
  job.kind = Job::Kind::Triangle;
  job.look = &look;
  auto& r = job.triangle;
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
  for(u32 k = 0; k < 3; k++) r.x[k] = p[k].x, r.y[k] = p[k].y;
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
  //Colors, the shine, fog and depth stepped from there (stepped()); fog held to 0-255 at each corner first
  //(fogAmount()), and a depth to 0-65535.
  auto scale = stepScale(area);
  auto steps = [&](auto&& of) {
    s32 value[3] = {s32(of(*p[0].vertex)), s32(of(*p[1].vertex)), s32(of(*p[2].vertex))};
    return stepped(r.x, r.y, scale, value, value[leftmost]);
  };
  //Only those its pixels use (as triangleRows() and triangleFours() choose them); the rest are left 0.
  bool needsZ = pixel.depthRange || (pixel.clear ? pixel.clearDepth : pixel.depthTest);
  if(!r.flat) for(u32 n = 0; n < 4; n++) r.colors[n] = steps([n](const Vertex& v) { return channel(v.color, n); });
  if(!r.flat && r.shines) {
    for(u32 n = 0; n < 3; n++) r.shine[n] = steps([n](const Vertex& v) { return channel(v.specular, n); });
  }
  if(pixel.fog) r.fog = steps([](const Vertex& v) { return fogAmount(v.fog); });
  if(needsZ) {
    r.depth = steps([](const Vertex& v) { return v.z > 0 ? u32(std::min(v.z, 65535.0f)) : 0u; });  //(not a number: 0)
  }
  //In 3D, each corner takes s = float24(u * R(w)), t = float24(v * R(w)) and q = float24(q * R(w)), where its own
  //q is 1 but with the texture matrix's (TEXTURE_MAP_MODE 1: the divide by q the blend had before, carried over, not
  //measured). Every other way q is then R(w) itself: it has 16 significant bits at most (but where it's a denormal,
  //for w of 2^126 or more), which the cut keeps. s, t and q then become 15-bit integers on the largest exponent among
  //the three of each, and are stepped from the same corner as the colors (part 60). A pixel's coordinate is the
  //floored step times R of the floored q.
  if(look.textured && perspective) {
    float qs[3], ss[3], ts[3];
    for(u32 k = 0; k < 3; k++) {
      auto& v = *p[k].vertex;
      float reciprocal = geReciprocal(v.clip[3]);
      qs[k] = cutLowBits(v.q * reciprocal);
      ss[k] = cutLowBits(v.u * reciprocal);
      ts[k] = cutLowBits(v.v * reciprocal);
    }
    s32 si[3], ti[3], qi[3];
    r.sUnit = quantize15(ss, si), r.tUnit = quantize15(ts, ti), r.qUnit = quantize15(qs, qi);
    r.texS = stepped(r.x, r.y, scale, si, si[leftmost]);
    r.texT = stepped(r.x, r.y, scale, ti, ti[leftmost]);
    r.texQ = stepped(r.x, r.y, scale, qi, qi[leftmost]);
  }
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
  if(hardware) return renderer->point(at);
  Job job{};
  job.kind = Job::Kind::Point;
  job.look = &look;
  job.firstX = job.lastX = x, job.firstY = job.lastY = y;
  if(look.textured) job.linear = chooseFilter(commands[TextureFilter], 1.0f);
  job.point = {x, y, u32(std::clamp(at.z, 0.0f, 65535.0f)), at.color, at.specular, fogAmount(at.fog), at.u, at.v};
  submit(job);
}

//Whether the line from (ax, ay) to (bx, by) leaves the diamond around (x, y), all in sixteenths: it meets the diamond
//and doesn't end inside it. Turned 45 degrees the diamond is a square, u = dx + dy and v = dx - dy (dx and dy from
//its middle) each from -8 to 8, of whose edges all count as inside but u = 8 (the diamond's bottom-right edge, with
//its bottom and right corners). The line's points are a + t (b - a) for t from 0 to 1; each edge keeps t on one side
//of a fraction, and the line meets the diamond if some t is left. Exact: the numbers are whole, and fractions are
//compared by multiplying out (positions are held to the GE's range, so the products fit in 64 bits).
static auto leavesDiamond(s64 ax, s64 ay, s64 bx, s64 by, s64 x, s64 y) -> bool {
  s64 ua = ax - x + (ay - y), va = ax - x - (ay - y), ub = bx - x + (by - y), vb = bx - x - (by - y);
  if(ub >= -8 && ub < 8 && vb >= -8 && vb <= 8) return false;  //it ends inside
  struct Limit { s64 n, d; bool open; };  //t at n / d (d above 0), the limit itself left out if open
  Limit low{0, 1, false}, high{1, 1, false};
  auto below = [](const Limit& p, const Limit& q) { return p.n * q.d < q.n * p.d; };
  //from + t * step at least limit (or at most it; open: and not equal to it)
  auto keep = [&](s64 from, s64 step, s64 limit, bool atLeast, bool open) -> bool {
    if(step == 0) return atLeast ? (open ? from > limit : from >= limit) : (open ? from < limit : from <= limit);
    Limit at = step > 0 ? Limit{limit - from, step, open} : Limit{from - limit, -step, open};
    if((step > 0) == atLeast) {  //a limit from below
      if(below(low, at) || (!below(at, low) && open)) low = at;
    } else {
      if(below(at, high) || (!below(high, at) && open)) high = at;
    }
    return true;
  };
  if(!keep(ua, ub - ua, -8, true, false) || !keep(ua, ub - ua, 8, false, true)) return false;
  if(!keep(va, vb - va, -8, true, false) || !keep(va, vb - va, 8, false, false)) return false;
  if(below(low, high)) return true;
  return !below(high, low) && !low.open && !high.open;
}

//A line, a pixel wide. Which pixels it lights is the "diamond exit" rule of OpenGL and Direct3D (pspautotests'
//gpu/exact/lines names it the PSP's), fitted to every picture of lines pspautotests recorded on a PSP
//(gpu/primitives/lines, linestrip and indices), and otherwise unmeasured (tools/psp-measure's round 4 records it):
//  - Each pixel has a diamond around its middle: the points less than half a pixel from it, the distance across
//    and the distance down added. A line lights the pixels whose diamonds it leaves: those it meets and doesn't end
//    in. So a line a whole pixel long lights one pixel, the first along it and not the last, and in a strip the
//    pixel holding the point where two lines meet is lit once, by the second.
//  - Of a diamond's edge, its top and left corners count as inside it, with all of its edges but the bottom-right
//    one: a level line along the boundary between two rows lights the row below it, and an upright one between two
//    columns the column right of it, as the PSP drew them (a line from (14, 10) to (16, 10) lights (14, 10) and
//    (15, 10), one from (24, 10) to (22, 10) lights (22, 10) and (23, 10)).
//  - Along its longer axis (x for a line as wide as tall), it lights one pixel in each column whose diamond it
//    passes through: the one in the row it crosses the column's middle in. Near its ends, a column's pixel is lit
//    only if the line leaves its diamond (leavesDiamond()).
//Colors, depth, fog and texture coordinates are blended along it as at the point where it crosses the pixel's
//middle column (its middle row, along y), held to its ends; with flat shading it takes its second vertex's color;
//in 2D, texture coordinates are stepped from the left (top) end, as for sprites and triangles (shortStep()).
//pspautotests' gpu/exact/lines checks lines by CRCs of whole pictures, which this rule doesn't meet: the PSP's rule
//differs somewhere it doesn't say. Anti-aliasing (ANTI_ALIAS_ENABLE) isn't emulated: that program found it changing
//nothing without blending, and what alpha it gives isn't known.
auto GE::line(const Look& look, const Vertex& from, const Vertex& to, bool perspective) -> void {
  auto& pixel = look.pixel;
  s64 x0 = fixed(from.x), y0 = fixed(from.y), x1 = fixed(to.x), y1 = fixed(to.y);
  if(x0 == x1 && y0 == y1) return;  //(it leaves no diamond)
  //With x and y swapped, the diamond's rule reads the same (its top and left corners swap, and its bottom-left and
  //top-right edges, all of which count), so a steep line is worked out as a shallow one turned.
  bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
  if(steep) std::swap(x0, y0), std::swap(x1, y1);
  s64 along = x1 - x0, rise = y1 - y0;
  auto acrossAt = [&](s64 column) {  //the row in which the line crosses the column's middle
    s64 n = y0 * along + rise * (column * 16 + 8 - x0), d = along * 16;
    return d > 0 ? floorDivide64(n, d) : floorDivide64(-n, -d);
  };
  //The columns whose diamonds it may meet; it lights those whose diamonds lie wholly between its ends, corners
  //and all, and of the rest those whose diamonds it leaves. Those it lights follow one after another.
  s64 left = std::min(x0, x1), right = std::max(x0, x1);
  s64 first = floorDivide64(left - 1, 16), last = floorDivide64(right, 16);
  auto lights = [&](s64 column) {
    s64 middle = column * 16 + 8;
    if(left < middle - 8 && middle + 8 < right) return true;
    return leavesDiamond(x0, y0, x1, y1, middle, acrossAt(column) * 16 + 8);
  };
  while(first <= last && !lights(first)) first++;
  while(last >= first && !lights(last)) last--;
  if(first > last) return;
  Job job{};
  job.kind = Job::Kind::Line;
  job.look = &look;
  job.line = {};
  auto& l = job.line;
  const Vertex* ends[2] = {&from, &to};
  if(along < 0) std::swap(x0, x1), std::swap(y0, y1), std::swap(ends[0], ends[1]), along = -along, rise = -rise;
  l.x[0] = x0, l.x[1] = x1, l.y[0] = y0, l.y[1] = y1, l.along = along, l.rise = rise;
  l.steep = steep;
  l.first = s32(first), l.last = s32(last);
  s64 acrossFirst = acrossAt(first), acrossLast = acrossAt(last);
  s32 lowest = s32(std::min(acrossFirst, acrossLast)), highest = s32(std::max(acrossFirst, acrossLast));
  s32 firstX = steep ? lowest : l.first, lastX = steep ? highest : l.last;
  s32 firstY = steep ? l.first : lowest, lastY = steep ? l.last : highest;
  job.firstX = std::max(firstX, pixel.left), job.lastX = std::min(lastX, pixel.right);
  job.firstY = std::max(firstY, pixel.top), job.lastY = std::min(lastY, pixel.bottom);

  l.flat = !(commands[ShadeMode] & 1);
  l.flatColor = to.color, l.flatSpecular = to.specular;  //flat shading: the second vertex's
  l.shines = from.specular || to.specular;
  l.perspective = perspective;
  for(u32 k = 0; k < 2; k++) {
    auto& v = *ends[k];
    l.color[k] = v.color, l.specular[k] = v.specular;
    l.z[k] = v.z, l.u[k] = v.u, l.v[k] = v.v, l.q[k] = v.q, l.w[k] = v.clip[3];
    l.fog[k] = float(fogAmount(v.fog));  //0-255 at each end, then blended
  }
  if(look.textured) {
    float du = l.u[1] - l.u[0], dv = l.v[1] - l.v[0];
    job.linear = chooseFilter(commands[TextureFilter], std::sqrt(du * du + dv * dv) / (along / 16.0f));
    if(!perspective) {
      l.uStep = shortStep(16 * (f64(l.u[1]) - f64(l.u[0])) / f64(along));
      l.vStep = shortStep(16 * (f64(l.v[1]) - f64(l.v[0])) / f64(along));
    }
  }
  if(hardware) {  //(the pixels it lights, each the hardware renderer's to draw as a square)
    if(job.firstX > job.lastX || job.firstY > job.lastY) return;
    linePixels(job, job.firstY, job.lastY, hardwareLine);
    return renderer->line(job, hardwareLine);
  }
  submit(job);
}

//BOUNDING_BOX: whether the count vertices at the vertex address (or by the indices at the index address), the corners
//of a box around what follows in the list, are out of sight, for BJUMP to skip it (pspsdk's sceGuBeginObject and
//sceGuEndObject). As pspautotests' gpu/bounding programs recorded on a PSP (count, planes, viewport and vertexaddr,
//every case):
//  - Each vertex goes through the matrices and onto the screen as one drawn would (its distance from 2048 cut to the
//    sixteenth), its position held to the GE's 4096 pixels. Across, it's in sight from a pixel left of the left edge
//    of the scissor rectangle and drawing region to their right edge (one past their last column: the edges in
//    pixels, so a pixel's width past their last pixel's left edge); likewise down. With DEPTH_CLIP_ENABLE, its z
//    must also be between -w and w; without it, depth doesn't count, nor do MIN_Z and MAX_Z.
//  - The vertices are read as PRIM reads them, and the vertex (or index) address moves on past them the same way.
//  - No vertices (the count is 16 bits) are out of sight. Of more than 256, only those from 512 before the end to
//    256 before it count: every case of count's fits that, for a reason it doesn't show.
//Unmeasured: corners out of sight past different edges, which could be a box around the camera, so the box is taken
//to be out of sight only with every vertex past the same edge; vertices behind the camera (taken where dividing by
//w puts them); and through mode (positions taken as drawn: a vertex in sight was recorded in sight).
auto GE::boundingBox(u32 count) -> bool {
  auto format = vertexFormat();
  readVertices(count, format);
  if(!count) return true;
  enum : u32 { Left = 1, Right = 2, Top = 4, Bottom = 8, Near = 16, Far = 32 };
  auto pixel = pixelState();
  s64 left = (pixel.left - 1) * 16, right = (pixel.right + 1) * 16;
  s64 top = (pixel.top - 1) * 16, bottom = (pixel.bottom + 1) * 16;
  Transform t{};
  if(!format.through) {
    t = transformState();
    t.weights = format.weightFormat ? format.weights : 0;
  }
  //(on the screen in sixteenths, held to it: something not a number at its left or top edge)
  auto screen = [](float position) -> s64 {
    f64 cut = 32768 + std::trunc((f64(position) - 2048) * 16);
    return cut >= 0 ? s64(std::min(cut, 65535.0)) : 0;
  };
  u32 past = ~0u;  //the edges every vertex so far is past
  u32 from = count > 512 ? count - 512 : 0, to = count > 256 ? count - 256 : count;
  for(u32 n = from; n < to; n++) {
    auto& vertex = primitiveVertices[n];
    s64 x, y;
    u32 edges = 0;
    if(format.through) {
      x = fixed(vertex.x), y = fixed(vertex.y);
    } else {
      float clip[4];
      clipPosition(vertex, t, clip);
      float w = clip[3];
      x = screen(clip[0] * t.scale[0] / w + t.center[0]) - s64(t.offsetX);
      y = screen(clip[1] * t.scale[1] / w + t.center[1]) - s64(t.offsetY);
      if(t.depthClamp && clip[2] < -w) edges |= Near;
      if(t.depthClamp && clip[2] > w) edges |= Far;
    }
    if(x < left) edges |= Left;
    if(x > right) edges |= Right;
    if(y < top) edges |= Top;
    if(y > bottom) edges |= Bottom;
    past &= edges;
  }
  return past != 0;
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
