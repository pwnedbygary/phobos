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
//sits on it. The GE keeps positions in sixteenths: drawing x = screen x * 16 + 0.375 - OFFSET_X, cut to a whole
//sixteenth (toward zero).
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
//  2: environment mapping, which comes from lighting (not emulated yet: the vertex's are used, as in 0).
//
//Fog: how much of a pixel's color shows rather than FOG_COLOR, from how far in front of the camera it is:
//(view z + FOG1) * FOG2, held to 0-1 at each pixel (pspsdk's sceGuFog sets FOG1 to the far end and FOG2 to
//1 / (far - near), so it's 1 at the near end and 0 at the far one).
//
//(The rounding, the range checks, the near plane and the other rules here are as PPSSPP's software renderer has
//them, which its authors checked against tests on the PSP.)

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

  if(t.mapMode == 1) {
    float source[3] = {model[0], model[1], model[2]};
    if(t.mapSource == 1) source[0] = vertex.u, source[1] = vertex.v, source[2] = 0;
    if(t.mapSource >= 2) {
      float length = t.mapSource == 2 ? std::sqrt(vertex.normal[0] * vertex.normal[0] +
                     vertex.normal[1] * vertex.normal[1] + vertex.normal[2] * vertex.normal[2]) : 1.0f;
      for(u32 k = 0; k < 3; k++) source[k] = vertex.normal[k] / length;
    }
    float stq[3];
    times43(t.textureMatrix, source, stq);
    vertex.u = stq[0] * t.textureWidth, vertex.v = stq[1] * t.textureHeight, vertex.q = stq[2];
  } else {
    if(t.mapMode == 2) note("environment mapping (TEXTURE_MAP_MODE 2) isn't emulated yet: the vertex's texture coordinates are used");
    vertex.u = (vertex.u * t.textureScale[0] + t.textureOffset[0]) * t.textureWidth;
    vertex.v = (vertex.v * t.textureScale[1] + t.textureOffset[1]) * t.textureHeight;
  }
  if(t.fog) vertex.fog = t.fogForced ? t.fogValue : (inView[2] + t.fogEnd) * t.fogSlope;
}

//A position in clip space onto the screen: x and y in pixels (whole sixteenths), z the depth, and whether the GE
//can draw it there. A vertex made by clipping (on the near plane) is always checked against the screen's edges.
auto GE::project(Vertex& v, const Transform& t, bool clipped) const -> void {
  float w = v.clip[3];
  float x = v.clip[0] * t.scale[0] / w + t.center[0];
  float y = v.clip[1] * t.scale[1] / w + t.center[1];
  float z = v.clip[2] * t.scale[2] / w + t.center[2];
  constexpr float Edge = 4095 + 15.5f / 16;  //the screen's last sixteenth, rounding as the GE rounds
  bool offScreen = !(x >= 0 && y >= 0 && x < Edge && y < Edge);  //not a number counts as off it
  if(t.depthClamp) v.outside = offScreen && (clipped || v.clip[2] > -w);
  else v.outside = offScreen || !(z >= 0 && z < 65536);
  //A whole number of sixteenths, toward zero; wild positions (from a vertex that won't be drawn) are left at 0.
  auto sixteenths = [](float position, float offset) -> float {
    float value = position * 16 + 0.375f - offset;
    return std::abs(value) < 1e9f ? float(s32(value)) / 16 : 0.0f;
  };
  v.x = sixteenths(x, t.offsetX);
  v.y = sixteenths(y, t.offsetY);
  v.z = z > 0 ? std::min(z, 65535.0f) : 0.0f;  //with DEPTH_CLIP_ENABLE off, a depth outside isn't drawn anyway
}

//Whether a primitive with these corners isn't drawn because of where they are: one off the screen, or depths
//outside the range (any with DEPTH_CLIP_ENABLE off, all past the same end with it on).
static auto outOfSight(bool depthClamp, std::initializer_list<const GE::Vertex*> corners) -> bool {
  u32 far = 0, near = 0;
  for(auto* corner : corners) {
    if(corner->outside) return true;
    float depth = corner->clip[2] / corner->clip[3];
    if(depth >= OutsideDepth) far++;
    if(-depth >= OutsideDepth) near++;
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
  v.color = 0;
  for(u32 n = 0; n < 4; n++) v.color |= u32((channel(a.color, n) * (256 - step) + channel(b.color, n) * step) / 256) << n * 8;
  return v;
}

//A triangle in 3D: dropped if out of sight, else cut along the near plane where it reaches past it, and drawn.
//facing: as for triangle(). With flat shading every piece takes the last vertex's color.
auto GE::clipTriangle(PixelState& pixel, Sampler* texture, const Transform& t, const Vertex& a, const Vertex& b,
                      const Vertex& c, s32 facing) -> void {
  if(outOfSight(t.depthClamp, {&a, &b, &c})) return;
  if(a.clip[3] < 0 && b.clip[3] < 0 && c.clip[3] < 0) return;
  auto nearer = [](const Vertex& v) { return v.clip[2] + v.clip[3]; };  //below zero: nearer than the near plane
  if(nearer(a) >= 0 && nearer(b) >= 0 && nearer(c) >= 0) return triangle(pixel, texture, a, b, c, facing, true);

  //Walk round the edges, keeping each corner on the far side of the plane and making one wherever an edge crosses
  //it: a triangle cut by a plane leaves three or four corners, drawn as a fan of triangles from the first.
  const Vertex* corners[3] = {&a, &b, &c};
  Vertex kept[4];
  u32 count = 0;
  for(u32 n = 0; n < 3; n++) {
    const Vertex& from = *corners[n];
    const Vertex& to = *corners[(n + 1) % 3];
    float fromSide = nearer(from), toSide = nearer(to);
    if(fromSide >= 0) kept[count++] = from;
    if((fromSide >= 0) != (toSide >= 0)) {
      kept[count] = between(from, to, fromSide / (fromSide - toSide));
      project(kept[count++], t, true);
    }
  }
  bool flat = !(commands[ShadeMode] & 1);
  for(u32 n = 2; n < count; n++) {
    Vertex last = kept[n];
    if(kept[0].outside || kept[n - 1].outside || last.outside) continue;
    if(flat) last.color = c.color;
    triangle(pixel, texture, kept[0], kept[n - 1], last, facing, true);
  }
}
