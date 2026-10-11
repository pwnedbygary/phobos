//3D: vertices from a model's own space to the screen, and triangles cut where they reach past the camera's near
//plane; draw.cpp then draws them as it draws 2D ones.
//
//Outside through mode a vertex's position is where it sits in its own model (its numbers fractions: see
//vertex.cpp), and the GE takes it through three matrices, each element a float as its DATA command gave it:
//  - world (4x3): where the model stands in the world;
//  - view (4x3): the world as the camera sees it (the camera at the middle, looking down -z);
//  - projection (4x4): the camera's lens. The result, "clip space", has a fourth number, w, which grows with distance.
//With skinning (a vertex type with weights), the position first goes through each bone matrix its weights pick
//(BONE_MATRIX: up to eight, 4x3), and the results are added up, weighted.
//
//Dividing by w gives perspective (the further away, the smaller), and the viewport turns the result into the
//screen's pixels and a depth: screen x = x / w * VIEWPORT_X_SCALE + VIEWPORT_X_CENTER, likewise y and z. The screen
//is 4096 pixels square, and OFFSET_X and OFFSET_Y (in sixteenths of a pixel) say where the frame buffer's top left
//sits on it. The GE keeps positions in sixteenths: it cuts a position's distance from screen coordinate 2048 (the
//middle of the 4096) to a whole sixteenth, toward 2048, so a position left of the middle moves right and one right of
//it moves left; drawing x = that - OFFSET_X.
//
//What the GE won't draw:
//  - a primitive with a vertex off the screen's 4096x4096 pixels, at all;
//  - depths outside 0-65535: with DEPTH_CLIP_ENABLE on (pspsdk's GU_CLIP_PLANES) they're held to that range (and a
//    vertex nearer than the near plane may be off the screen, since clipping cuts it away); off, a vertex whose depth
//    is outside the range counts as off the screen too;
//  - z / w past 1 either way (by more than 2^-15) is outside the depth range: with DEPTH_CLIP_ENABLE off, a triangle
//    or sprite with any such vertex isn't drawn; on, only one with all of them past the same end. (Points aren't
//    judged by it, only by the screen and its depths: PPSSPP's reading, which the PSP could settle.)
//  - a triangle with every w below zero (all of it behind the camera);
//  - the part of a triangle nearer than the near plane (z < -w): it's cut along that plane, and the rest drawn. The
//    PSP cuts against no other plane: the scissor rectangle does the screen's edges.
//
//Texture coordinates (TEXTURE_MAP_MODE bits 0-1), in fractions of the texture, become texels here:
//  0: the vertex's, times TEX_SCALE, plus TEX_OFFSET;
//  1: from the texture matrix (4x3), applied to what bits 8-9 pick: the model position, the texture coordinates (and
//     0), the normal made one long, or the normal as it is. That gives s, t and q, and each pixel's texture
//     coordinates are s/q and t/q there: "projected", like a slide projector's picture;
//  2: environment mapping: from two lights' directions and the vertex's normal (lighting.cpp).
//
//Lighting (LIGHTING_ENABLE) then works out each vertex's color (lighting.cpp), from its normal turned into the world
//by the world matrix (NORMAL_REVERSE turns it round first).
//
//Fog: how much of a pixel's color shows rather than FOG_COLOR, from how far in front of the camera it is:
//(view z + FOG1) * FOG2, held to 0-1 at each pixel (pspsdk's sceGuFog sets FOG1 to the far end and FOG2 to
//1 / (far - near), so it's 1 at the near end and 0 at the far one).
//
//(The rounding onto the screen was measured on a PSP (docs/psp-core.md, round 3's 3d-rounding-middle; 2048 was also
//the viewport's center there, so which of the two the GE cuts toward isn't settled); the screen's edges follow from
//it (unmeasured, and a hair from PPSSPP's). The depth checks, the near plane and the other rules here are as
//PPSSPP's software renderer has them, which its authors checked against tests on the PSP.)

static constexpr float OutsideDepth = 1.000030517578125f;  //z / w this far or further from 0: outside the depths

static auto toFloats(const u32* words, float* floats, u32 count) -> void {
  for(u32 n = 0; n < count; n++) floats[n] = GE::float24(words[n]);
}

//A point times a 4x3 matrix, as pspsdk's GU library sends one: a row for where each of x, y and z goes, then a row
//for the move.
static auto times43(const float* m, const float* in, float* out) -> void {
  for(u32 j = 0; j < 3; j++) out[j] = in[0] * m[j] + in[1] * m[3 + j] + in[2] * m[6 + j] + m[9 + j];
}
//Likewise a 4x4 matrix, giving four numbers.
static auto times44(const float* m, const float* in, float* out) -> void {
  for(u32 j = 0; j < 4; j++) out[j] = in[0] * m[j] + in[1] * m[4 + j] + in[2] * m[8 + j] + m[12 + j];
}
//A direction (a normal) times a 4x3 matrix: turned, not moved.
static auto turn43(const float* m, const float* in, float* out) -> void {
  for(u32 j = 0; j < 3; j++) out[j] = in[0] * m[j] + in[1] * m[3 + j] + in[2] * m[6 + j];
}

auto GE::transformState() const -> Transform {
  Transform t{};
  toFloats(world, t.world, 12);
  toFloats(view, t.view, 12);
  toFloats(projection, t.projection, 16);
  toFloats(textureMatrix, t.textureMatrix, 12);
  toFloats(bones, t.bones, 96);
  for(u32 n = 0; n < 3; n++) {
    t.scale[n] = float24(commands[ViewportXScale + n]);
    t.center[n] = float24(commands[ViewportXCenter + n]);
  }
  t.offsetX = commands[OffsetX] & 0xffff;
  t.offsetY = commands[OffsetY] & 0xffff;
  t.depthClamp = commands[DepthClipEnable] & 1;
  t.mapMode = commands[TextureMapMode] & 3;
  t.mapSource = commands[TextureMapMode] >> 8 & 3;
  t.textureScale[0] = float24(commands[TextureScaleU]), t.textureScale[1] = float24(commands[TextureScaleV]);
  t.textureOffset[0] = float24(commands[TextureOffsetU]), t.textureOffset[1] = float24(commands[TextureOffsetV]);
  t.textureWidth = t.textureHeight = 1;
  t.fog = commands[FogEnable] & 1;
  t.fogEnd = float24(commands[FogEnd]), t.fogSlope = float24(commands[FogSlope]);
  //A FOG1 or FOG2 whose exponent is all ones (infinity, or not a number, to a float) is still a number to the GE:
  //a huge one. So an end like that makes every vertex all fog or none, by the signs (a slope of 0 counts as
  //negative), and a slope like that is just steep. (As PPSSPP has it: OutRun 2006's sky needs it.)
  if(!std::isfinite(t.fogEnd)) {
    bool negative = std::signbit(t.fogEnd) != std::signbit(t.fogSlope) || t.fogSlope == 0;
    t.fogForced = true;
    t.fogValue = negative ? 0.0f : 1.0f;
  } else if(!std::isfinite(t.fogSlope)) {
    t.fogSlope = std::signbit(t.fogSlope) ? -262144.0f : 262144.0f;
  }
  lightingState(t);
  return t;
}

//A vertex through the matrices onto the screen, with its texture coordinates (in texels) and fog.
auto GE::transform(Vertex& vertex, const Transform& t) -> void {
  float model[3] = {vertex.x, vertex.y, vertex.z};
  if(t.weights) {
    float position[3] = {}, normal[3] = {};
    for(u32 n = 0; n < t.weights; n++) {
      float moved[3], turned[3];
      times43(t.bones + n * 12, model, moved);
      turn43(t.bones + n * 12, vertex.normal, turned);
      for(u32 k = 0; k < 3; k++) position[k] += moved[k] * vertex.weights[n], normal[k] += turned[k] * vertex.weights[n];
    }
    for(u32 k = 0; k < 3; k++) model[k] = position[k], vertex.normal[k] = normal[k];
  }
  float inWorld[3], inView[3];
  times43(t.world, model, inWorld);
  times43(t.view, inWorld, inView);
  times44(t.projection, inView, vertex.clip);
  project(vertex, t, false);
  float normal[3] = {0, 0, 1};  //in the world, one long: for lighting and environment mapping
  if(t.lighting || t.mapMode == 2) {
    float turned[3], sign = t.normalReverse ? -1.0f : 1.0f;
    float own[3] = {vertex.normal[0] * sign, vertex.normal[1] * sign, vertex.normal[2] * sign};
    turn43(t.world, own, turned);
    float length = std::sqrt(turned[0] * turned[0] + turned[1] * turned[1] + turned[2] * turned[2]);
    if(length > 0) {
      for(u32 k = 0; k < 3; k++) normal[k] = turned[k] / length;
    }
  }

  if(t.mapMode == 2) {
    vertex.u = shadeCoordinate(t, t.shadeU, inWorld, normal) * t.textureWidth;
    vertex.v = shadeCoordinate(t, t.shadeV, inWorld, normal) * t.textureHeight;
  } else if(t.mapMode == 1) {
    float source[3] = {model[0], model[1], model[2]};
    if(t.mapSource == 1) source[0] = vertex.u, source[1] = vertex.v, source[2] = 0;
    if(t.mapSource >= 2) {  //the normal (turned round by NORMAL_REVERSE), made one long or as it is
      float length = t.mapSource == 2 ? std::sqrt(vertex.normal[0] * vertex.normal[0] +
                     vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2]) : 1.0f;
      if(t.normalReverse) length = -length;
      for(u32 k = 0; k < 3; k++) source[k] = vertex.normal[k] / length;
    }
    float stq[3];
    times43(t.textureMatrix, source, stq);
    vertex.u = stq[0] * t.textureWidth, vertex.v = stq[1] * t.textureHeight, vertex.q = stq[2];
  } else {
    vertex.u = (vertex.u * t.textureScale[0] + t.textureOffset[0]) * t.textureWidth;
    vertex.v = (vertex.v * t.textureScale[1] + t.textureOffset[1]) * t.textureHeight;
  }
  if(t.fog) vertex.fog = t.fogForced ? t.fogValue : (inView[2] + t.fogEnd) * t.fogSlope;
  if(t.lighting) light(vertex, inWorld, normal, t);
}

//A position in clip space onto the screen: x and y in pixels (whole sixteenths), z the depth, and whether the GE
//can draw it there. A vertex made by clipping (on the near plane) is always checked against the screen's edges.
auto GE::project(Vertex& v, const Transform& t, bool clipped) const -> void {
  float w = v.clip[3];
  float x = v.clip[0] * t.scale[0] / w + t.center[0];
  float y = v.clip[1] * t.scale[1] / w + t.center[1];
  float z = v.clip[2] * t.scale[2] / w + t.center[2];
  //The position the GE holds, in sixteenths of its 4096-pixel screen: the distance from 2048 cut toward 2048 (in
  //doubles, where a float's distance is exact). Off the screen is outside 0-65535 once cut, so a sixteenth or more
  //left of (or above) 0, or 4096 or more: that follows from the rounding, as no case measured the edges.
  auto cut = [](float position) -> f64 { return 32768 + std::trunc((f64(position) - 2048) * 16); };
  f64 cutX = cut(x), cutY = cut(y);
  bool offScreen = !(cutX >= 0 && cutX < 65536 && cutY >= 0 && cutY < 65536);  //not a number counts as off it
  if(t.depthClamp) v.outside = offScreen && (clipped || v.clip[2] > -w);
  else v.outside = offScreen || !(z >= 0 && z < 65536);
  //Then from the frame buffer's top left, in pixels; wild positions (from a vertex that won't be drawn) stay at 0.
  auto pixels = [](f64 position, float offset) -> float {
    return std::abs(position) < 1e9 ? float((position - offset) / 16) : 0.0f;
  };
  v.x = pixels(cutX, t.offsetX);
  v.y = pixels(cutY, t.offsetY);
  v.z = z > 0 ? std::min(z, 65535.0f) : 0.0f;  //with DEPTH_CLIP_ENABLE off, a depth outside isn't drawn anyway
  //z / w past either end of the depths (outOfSight()): worked out once a vertex, not at each primitive's corner
  float depth = v.clip[2] / v.clip[3];
  v.far = depth >= OutsideDepth, v.near = -depth >= OutsideDepth;
}

//Whether a primitive with these corners isn't drawn because of where they are: one off the screen, or depths
//outside the range (any with DEPTH_CLIP_ENABLE off, all past the same end with it on).
static auto outOfSight(bool depthClamp, std::initializer_list<const GE::Vertex*> corners) -> bool {
  u32 far = 0, near = 0;
  for(auto* corner : corners) {
    if(corner->outside) return true;
    far += corner->far, near += corner->near;
  }
  if(!depthClamp) return far + near > 0;
  return far == corners.size() || near == corners.size();
}

//A vertex part way from one to another (at 0, the first; at 1, the second): everything about it blended, the colors
//in 256ths (as PPSSPP has it), to be put on the screen again.
static auto between(const GE::Vertex& a, const GE::Vertex& b, float t) -> GE::Vertex {
  GE::Vertex v = a;
  for(u32 n = 0; n < 4; n++) v.clip[n] = a.clip[n] + (b.clip[n] - a.clip[n]) * t;
  v.u = a.u + (b.u - a.u) * t;
  v.v = a.v + (b.v - a.v) * t;
  v.q = a.q + (b.q - a.q) * t;
  v.fog = a.fog + (b.fog - a.fog) * t;
  s32 step = t > 0 ? s32(std::min(t, 1.0f) * 256) : 0;  //(a corner that isn't a number makes t none either)
  auto mix = [&](u32 from, u32 to) {
    u32 mixed = 0;
    for(u32 n = 0; n < 4; n++) mixed |= u32((channel(from, n) * (256 - step) + channel(to, n) * step) / 256) << n * 8;
    return mixed;
  };
  v.color = mix(a.color, b.color);
  v.specular = mix(a.specular, b.specular);
  return v;
}

//A triangle in 3D: dropped if out of sight, else cut along the near plane where it reaches past it, and drawn.
//facing: as for triangle(). With flat shading every piece takes the last vertex's color.
auto GE::clipTriangle(const Look& look, const Transform& t, const Vertex& a, const Vertex& b, const Vertex& c,
                      s32 facing) -> void {
  if(outOfSight(t.depthClamp, {&a, &b, &c})) return;
  if(a.clip[3] < 0 && b.clip[3] < 0 && c.clip[3] < 0) return;
  auto nearer = [](const Vertex& v) { return v.clip[2] + v.clip[3]; };  //below zero: nearer than the near plane
  if(nearer(a) >= 0 && nearer(b) >= 0 && nearer(c) >= 0) return triangle(look, a, b, c, facing, true);

  //Walk round the edges, keeping each corner on the far side of the plane and making one wherever an edge crosses
  //it: a triangle cut by a plane leaves three or four corners. Four are drawn as two triangles split from the second
  //to the fourth, corners 0, 1, 3 and 1, 2, 3, each turning as the whole did: with the third corner past the plane
  //(so 0 and 1 kept, 2 and 3 cut on the edges after them), that splits it from the second kept corner to the cut
  //after the one past the plane, as the PSP does (measured, docs/psp-core.md, part 48: 3d-clip, every pixel; split
  //from the first to the third, as a fan, 1113 color values a level apart). Cut with another corner past the plane
  //isn't measured.
  const Vertex* corners[3] = {&a, &b, &c};
  Vertex kept[4];
  u32 count = 0;
  for(u32 n = 0; n < 3; n++) {
    const Vertex& from = *corners[n];
    const Vertex& to = *corners[(n + 1) % 3];
    float fromSide = nearer(from), toSide = nearer(to);
    if(fromSide >= 0) kept[count++] = from;
    if((fromSide >= 0) != (toSide >= 0)) {
      //blended from the kept corner toward the one past the plane: the way round decides how the colors' 256ths
      //round, and this is the PSP's (measured, docs/psp-core.md: 3d-clip; PPSSPP goes the other way)
      bool fromPast = fromSide < 0;
      const Vertex& past = fromPast ? from : to;
      const Vertex& kept1 = fromPast ? to : from;
      float pastSide = fromPast ? fromSide : toSide, keptSide = fromPast ? toSide : fromSide;
      kept[count] = between(kept1, past, keptSide / (keptSide - pastSide));
      project(kept[count++], t, true);
    }
  }
  bool flat = !(commands[ShadeMode] & 1);
  auto draw = [&](const Vertex& first, const Vertex& second, Vertex last) {
    if(first.outside || second.outside || last.outside) return;
    if(flat) last.color = c.color, last.specular = c.specular;
    triangle(look, first, second, last, facing, true);
  };
  if(count == 3) draw(kept[0], kept[1], kept[2]);
  if(count == 4) draw(kept[0], kept[1], kept[3]), draw(kept[1], kept[2], kept[3]);
}

//A line in 3D: dropped if out of sight, by the rules for a triangle's corners, else cut where it reaches past the
//near plane (its new end blended from the kept one toward the one past the plane, as a triangle's is), and drawn.
//With flat shading, a line whose second end is cut away keeps that end's color. (Unmeasured: a PSP's lines were
//measured in 2D alone, by pspautotests; these rules are its triangles'.)
auto GE::clipLine(const Look& look, const Transform& t, const Vertex& a, const Vertex& b) -> void {
  if(outOfSight(t.depthClamp, {&a, &b})) return;
  if(a.clip[3] < 0 && b.clip[3] < 0) return;
  float aSide = a.clip[2] + a.clip[3], bSide = b.clip[2] + b.clip[3];  //below zero: nearer than the near plane
  if(aSide >= 0 && bSide >= 0) return line(look, a, b, true);
  if(aSide < 0 && bSide < 0) return;
  bool keepsA = aSide >= 0;
  float keptSide = keepsA ? aSide : bSide, pastSide = keepsA ? bSide : aSide;
  Vertex cut = between(keepsA ? a : b, keepsA ? b : a, keptSide / (keptSide - pastSide));
  project(cut, t, true);
  if(cut.outside) return;
  if(!keepsA) return line(look, cut, b, true);
  if(!(commands[ShadeMode] & 1)) cut.color = b.color, cut.specular = b.specular;
  line(look, a, cut, true);
}

//A vertex's position in clip space (x, y, z, w), through the bone matrices its weights pick and the world, view and
//projection matrices, as transform() takes it (the same operations, so the same numbers).
auto GE::clipPosition(const Vertex& vertex, const Transform& t, float clip[4]) const -> void {
  float model[3] = {vertex.x, vertex.y, vertex.z};
  if(t.weights) {
    float position[3] = {};
    for(u32 n = 0; n < t.weights; n++) {
      float moved[3];
      times43(t.bones + n * 12, model, moved);
      for(u32 k = 0; k < 3; k++) position[k] += moved[k] * vertex.weights[n];
    }
    for(u32 k = 0; k < 3; k++) model[k] = position[k];
  }
  float inWorld[3], inView[3];
  times43(t.world, model, inWorld);
  times43(t.view, inWorld, inView);
  times44(t.projection, inView, clip);
}
