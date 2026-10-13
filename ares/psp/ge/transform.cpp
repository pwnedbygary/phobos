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
  settingsState(t);
  lightingState(t);
  return t;
}

//The commands settingsState() and lightingState() read, as ranges of their indices, first to last (transformKept()
//compares them)
static constexpr std::pair<u32, u32> SettingsCommands[] = {
  {GE::DepthClipEnable, GE::DepthClipEnable}, {GE::FogEnable, GE::FogEnable}, {GE::ViewportXScale, GE::OffsetY},
  {GE::TextureMapMode, GE::TextureMapMode}, {GE::FogEnd, GE::FogSlope}};
static constexpr std::pair<u32, u32> LightingCommands[] = {
  {GE::LightingEnable, GE::LightEnable0 + 3}, {GE::NormalReverse, GE::NormalReverse},
  {GE::MaterialColor, GE::AmbientAlpha}, {GE::MaterialSpecularCoefficient, GE::Light0Ambient + 11},
  {GE::TextureShadeMapping, GE::TextureShadeMapping}};
static constexpr auto commandsIn(const auto& ranges) -> u32 {
  u32 count = 0;
  for(auto [first, last] : ranges) count += last - first + 1;
  return count;
}
static_assert(commandsIn(SettingsCommands) == GE::KeptTransform::Settings);
static_assert(commandsIn(LightingCommands) == GE::KeptTransform::Lighting);

//transformState() as it would be now, from the last one kept: each part of a Transform is worked out from its own
//words alone (a matrix's floats from its words, the viewport's, texture mapping's and fog's settings from their
//commands, lighting's from its commands and the view matrix), so only the parts whose words have changed are worked
//out again. Most PRIMs share their 3D settings with the one before, but for the world matrix; and they're compared,
//not told of, as a list, a context or a state loaded, and the tests all set them. The bone matrices are looked at
//only for a PRIM that skins (skinned): one that doesn't takes none of them, and keeps those of the last that did.
auto GE::transformKept(bool skinned) -> Transform& {
  auto& k = keptTransform;
  auto& t = k.transform;
  auto renew = [&](u32* kept, const u32* now, u32 count, float* floats, bool valid) {
    if(valid && !std::memcmp(kept, now, count * 4)) return false;
    std::memcpy(kept, now, count * 4);
    toFloats(now, floats, count);
    return true;
  };
  auto gathered = [&](const auto& ranges, u32* kept) {
    bool same = k.valid;
    for(auto [first, last] : ranges) {
      for(u32 command = first; command <= last; command++, kept++) {
        same &= *kept == commands[command], *kept = commands[command];
      }
    }
    return !same;
  };
  renew(k.world, world, 12, t.world, k.valid);
  bool viewed = renew(k.view, view, 12, t.view, k.valid);
  renew(k.projection, projection, 16, t.projection, k.valid);
  renew(k.textureMatrix, textureMatrix, 12, t.textureMatrix, k.valid);
  if(skinned) renew(k.bones, bones, 96, t.bones, k.bonesValid), k.bonesValid = true;
  if(gathered(SettingsCommands, k.settings)) settingsState(t);
  if(gathered(LightingCommands, k.lighting) || viewed) lightingState(t);
  k.valid = true;
  return t;
}

//The viewport's, texture mapping's and fog's settings, from their commands (SettingsCommands)
auto GE::settingsState(Transform& t) const -> void {
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
  t.fogForced = false, t.fogValue = 0;
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
}

static auto projected(GE::Vertex& v, const GE::Transform& t, bool clipped) -> void;
static auto projectedFour(GE::Vertex* v, u32 lanes, const GE::Transform& t) -> void;
static auto placedFour(GE::Vertex* v, u32 lanes, const GE::Transform& t, GE::Placing* placing) -> void;

//A vertex through the matrices onto the screen, with its texture coordinates (in texels) and fog: its place, then
//its place on the screen, then the rest (transformAll()'s stages, for one vertex).
auto GE::transform(Vertex& vertex, const Transform& t) -> void {
  Placing placing;
  transformPlace(vertex, t, placing);
  projected(vertex, t, false);
  transformRest(vertex, t, placing);
}

//A PRIM's vertices transformed, each as transform() has it, but a stage at a time over all of them (64 at a time):
//each vertex's arithmetic waits on itself all along (project()'s divisions and conversions, lighting's), and a
//vertex's stage done after the last's leaves the CPU the next's to do meanwhile. (God of War's battle draws 255,000
//vertices a frame, nearly all lit.) Unskinned, their places and their places on the screen four at a time
//(placedFour()); skinned, their places a vertex at a time, as ever: the compiler may sum a vertex's bones four at a
//time there (as it does at -O3), each product rounded before it's added (the multiply and add written together may be
//fused or not), which placedFour()'s lanes, each a vertex, don't do; then their places on the screen four at a time
//(projectedFour()). The rest four at a time where texture coordinates are mapped by UV (transformRestFour()). (The
//same numbers as transform()'s, but for which of two operands that aren't numbers a sum gives, and so the sign of a
//result that isn't one: the compiler may swap them either way, in the scalar code as in the lanes.)
auto GE::transformAll(std::vector<Vertex>& vertices, const Transform& t) -> void {
  Placing placings[64];
  for(u32 first = 0, count = vertices.size(); first < count; first += 64) {
    Vertex* v = vertices.data() + first;
    u32 run = std::min(count - first, 64u);
    if(t.weights) {
      for(u32 n = 0; n < run; n++) transformPlace(v[n], t, placings[n]);
      for(u32 n = 0; n < run; n += 4) projectedFour(v + n, std::min(run - n, 4u), t);
    } else {
      for(u32 n = 0; n < run; n += 4) placedFour(v + n, std::min(run - n, 4u), t, placings + n);
    }
    if(t.mapMode == 0) {
      for(u32 n = 0; n < run; n += 4) transformRestFour(v + n, std::min(run - n, 4u), t, placings + n);
    } else {
      for(u32 n = 0; n < run; n++) transformRest(v[n], t, placings[n]);
    }
  }
}

//transformRest() for up to four vertices (lanes of them) whose texture coordinates are mapped by UV
//(TEXTURE_MAP_MODE 0): a lane each, the same operations as one vertex's alone, lit four at a time (lightFour()).
auto GE::transformRestFour(Vertex* v, u32 lanes, const Transform& t, const Placing* placing) -> void {
  auto lane = [&](u32 k) -> Vertex& { return v[std::min(k, lanes - 1)]; };
  auto place = [&](u32 k) -> const Placing& { return placing[std::min(k, lanes - 1)]; };
  f32x4 normal[3] = {f32x4{}, f32x4{}, f32x4{} + 1.0f};  //(in the world, one long)
  if(t.lighting) {
    float sign = t.normalReverse ? -1.0f : 1.0f;
    f32x4 own[3], turned[3];
    for(u32 n = 0; n < 3; n++) {
      own[n] = f32x4{lane(0).normal[n], lane(1).normal[n], lane(2).normal[n], lane(3).normal[n]} * sign;
    }
    for(u32 j = 0; j < 3; j++) turned[j] = own[0] * t.world[j] + own[1] * t.world[3 + j] + own[2] * t.world[6 + j];
    f32x4 squared = turned[0] * turned[0] + turned[1] * turned[1] + turned[2] * turned[2], length;
    for(u32 k = 0; k < 4; k++) length[k] = std::sqrt(squared[k]);
    s32x4 some = length > 0;
    for(u32 n = 0; n < 3; n++) normal[n] = pickFloats(some, turned[n] / length, normal[n]);
  }
  f32x4 u = {lane(0).u, lane(1).u, lane(2).u, lane(3).u}, w = {lane(0).v, lane(1).v, lane(2).v, lane(3).v};
  u = (u * t.textureScale[0] + t.textureOffset[0]) * t.textureWidth;
  w = (w * t.textureScale[1] + t.textureOffset[1]) * t.textureHeight;
  f32x4 viewZ = {place(0).viewZ, place(1).viewZ, place(2).viewZ, place(3).viewZ};
  f32x4 fog = t.fogForced ? f32x4{} + t.fogValue : (viewZ + t.fogEnd) * t.fogSlope;
  for(u32 k = 0; k < lanes; k++) {
    v[k].u = u[k], v[k].v = w[k];
    if(t.fog) v[k].fog = fog[k];
  }
  if(!t.lighting) return;
  f32x4 world[3];
  for(u32 n = 0; n < 3; n++) {
    world[n] = f32x4{place(0).world[n], place(1).world[n], place(2).world[n], place(3).world[n]};
  }
  lightFour(v, lanes, world, normal, t);
}

//A vertex's place: in its model (through the bone matrices its weights pick, its normal too), in the world, its depth
//in the view, and in clip space.
alwaysinline auto GE::transformPlace(Vertex& vertex, const Transform& t, Placing& placing) -> void {
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
  float inView[3];
  times43(t.world, model, placing.world);
  times43(t.view, placing.world, inView);
  times44(t.projection, inView, vertex.clip);
  for(u32 k = 0; k < 3; k++) placing.model[k] = model[k];
  placing.viewZ = inView[2];
}

//The rest of a vertex, once it's placed: its normal in the world, its texture coordinates, its fog, its lighting.
alwaysinline auto GE::transformRest(Vertex& vertex, const Transform& t, const Placing& placing) -> void {
  const float* model = placing.model;
  const float* inWorld = placing.world;
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
  if(t.fog) vertex.fog = t.fogForced ? t.fogValue : (placing.viewZ + t.fogEnd) * t.fogSlope;
  if(t.lighting) light(vertex, inWorld, normal, t);
}

//A position in clip space onto the screen: x and y in pixels (whole sixteenths), z the depth, and whether the GE
//can draw it there. A vertex made by clipping (on the near plane) is always checked against the screen's edges.
auto GE::project(Vertex& v, const Transform& t, bool clipped) const -> void {
  projected(v, t, clipped);
}

//A vertex on the screen (projected()): x and y in pixels from the frame buffer's top left (whole sixteenths), z its
//depth, whether the GE can draw it there, and its z / w past either end of the depths (outOfSight())
struct OnScreen { float x, y, z; bool outside, far, near; };

//The rest of projected() once the viewport has its place: x, y and z, and depth (z / w), from the clip position's z
//and w.
static alwaysinline auto onScreen(const GE::Transform& t, bool clipped, float x, float y, float z, float depth,
                                  float clipZ, float w) -> OnScreen {
  OnScreen s;
  //The position the GE holds, in sixteenths of its 4096-pixel screen: the distance from 2048 cut toward 2048 (in
  //doubles, where a float's distance is exact). Off the screen is outside 0-65535 once cut, so a sixteenth or more
  //left of (or above) 0, or 4096 or more: that follows from the rounding, as no case measured the edges.
  auto cut = [](float position) -> f64 { return 32768 + std::trunc((f64(position) - 2048) * 16); };
  f64 cutX = cut(x), cutY = cut(y);
  bool offScreen = !(cutX >= 0 && cutX < 65536 && cutY >= 0 && cutY < 65536);  //not a number counts as off it
  if(t.depthClamp) s.outside = offScreen && (clipped || clipZ > -w);
  else s.outside = offScreen || !(z >= 0 && z < 65536);
  //Then from the frame buffer's top left, in pixels; wild positions (from a vertex that won't be drawn) stay at 0.
  auto pixels = [](f64 position, float offset) -> float {
    return std::abs(position) < 1e9 ? float((position - offset) / 16) : 0.0f;
  };
  s.x = pixels(cutX, t.offsetX);
  s.y = pixels(cutY, t.offsetY);
  s.z = z > 0 ? std::min(z, 65535.0f) : 0.0f;  //with DEPTH_CLIP_ENABLE off, a depth outside isn't drawn anyway
  //z / w past either end of the depths (outOfSight()): worked out once a vertex, not at each primitive's corner
  s.far = depth >= OutsideDepth, s.near = -depth >= OutsideDepth;
  return s;
}

//(project()'s, inlined where it runs over a PRIM's vertices: transformAll())
static alwaysinline auto projected(GE::Vertex& v, const GE::Transform& t, bool clipped) -> void {
  float w = v.clip[3];
  float x = v.clip[0] * t.scale[0] / w + t.center[0];
  float y = v.clip[1] * t.scale[1] / w + t.center[1];
  float z = v.clip[2] * t.scale[2] / w + t.center[2];
  auto s = onScreen(t, clipped, x, y, z, v.clip[2] / v.clip[3], v.clip[2], w);
  v.x = s.x, v.y = s.y, v.z = s.z, v.outside = s.outside, v.far = s.far, v.near = s.near;
}

//Four vertices on the screen (projectedFour()), a lane each: OnScreen's, its yes as all bits set.
struct OnScreenFour { GE::f32x4 x, y, z; GE::s32x4 outside, far, near; };

//Four clip positions (a lane each: x, y, z and w) onto the screen, as projected() puts each, not cut by clipping:
//each lane the same operations as onScreen()'s on one, four at a time (its doubles two at a time).
static alwaysinline auto projectedFour(const GE::Transform& t, GE::f32x4 cx, GE::f32x4 cy, GE::f32x4 cz,
                                       GE::f32x4 cw) -> OnScreenFour {
  using f32x4 = GE::f32x4;
  using f64x2 = GE::f64x2;
  using f32x2 = float __attribute__((vector_size(8)));
  using s32x2 = s32 __attribute__((vector_size(8)));
  f32x4 x = cx * t.scale[0] / cw + t.center[0];
  f32x4 y = cy * t.scale[1] / cw + t.center[1];
  f32x4 z = cz * t.scale[2] / cw + t.center[2];
  f32x4 depth = cz / cw;
  //(onScreen()'s cut, its test for the screen and pixels() for half the lanes: 0 and 1, or 2 and 3)
  auto cut = [](f32x4 position, u32 half) -> f64x2 {
    f32x2 two{position[half * 2], position[half * 2 + 1]};
    f64x2 sixteenths = (__builtin_convertvector(two, f64x2) - 2048) * 16;
#if __has_builtin(__builtin_elementwise_trunc)
    return 32768 + __builtin_elementwise_trunc(sixteenths);
#else
    return 32768 + f64x2{std::trunc(sixteenths[0]), std::trunc(sixteenths[1])};
#endif
  };
  auto pixels = [](f64x2 position, float offset) -> f32x2 {
    s32x2 tame = __builtin_convertvector((position < 1e9) & (position > -1e9), s32x2);
    return (f32x2)((s32x2)__builtin_convertvector((position - offset) / 16, f32x2) & tame);
  };
  s32x2 on[2];
  f32x2 across[2], down[2];
  for(u32 half = 0; half < 2; half++) {
    f64x2 cutX = cut(x, half), cutY = cut(y, half);
    on[half] = __builtin_convertvector((cutX >= 0) & (cutX < 65536) & (cutY >= 0) & (cutY < 65536), s32x2);
    across[half] = pixels(cutX, t.offsetX), down[half] = pixels(cutY, t.offsetY);
  }
  OnScreenFour s;
  GE::s32x4 offScreen = ~GE::s32x4{on[0][0], on[0][1], on[1][0], on[1][1]};
  s.outside = t.depthClamp ? offScreen & (cz > -cw) : offScreen | ~((z >= 0) & (z < 65536));
  s.x = f32x4{across[0][0], across[0][1], across[1][0], across[1][1]};
  s.y = f32x4{down[0][0], down[0][1], down[1][0], down[1][1]};
  s.z = pickFloats(z > 0, pickFloats(65535.0f < z, f32x4{} + 65535.0f, z), f32x4{});
  s.far = depth >= OutsideDepth, s.near = -depth >= OutsideDepth;
  return s;
}

//Up to four placed vertices (lanes of them, their clip positions worked out) put on the screen, as projected() puts
//each, the last vertex standing in for lanes past lanes.
static auto projectedFour(GE::Vertex* v, u32 lanes, const GE::Transform& t) -> void {
  auto lane = [&](u32 k) -> const GE::Vertex& { return v[std::min(k, lanes - 1)]; };
  auto clip = [&](u32 n) { return GE::f32x4{lane(0).clip[n], lane(1).clip[n], lane(2).clip[n], lane(3).clip[n]}; };
  auto s = projectedFour(t, clip(0), clip(1), clip(2), clip(3));
  for(u32 k = 0; k < lanes; k++) {
    auto& out = v[k];
    out.x = s.x[k], out.y = s.y[k], out.z = s.z[k];
    out.outside = s.outside[k], out.far = s.far[k], out.near = s.near[k];
  }
}

//Up to four unskinned vertices (lanes of them) placed and put on the screen, as transformPlace() and projected() have
//each: a lane each, the same operations in each lane as one vertex's alone (each matrix's products summed in the same
//order), the last vertex standing in for lanes past lanes.
static auto placedFour(GE::Vertex* v, u32 lanes, const GE::Transform& t, GE::Placing* placing) -> void {
  using f32x4 = GE::f32x4;
  auto lane = [&](u32 k) -> GE::Vertex& { return v[std::min(k, lanes - 1)]; };
  auto gather = [&](auto part) { return f32x4{part(lane(0)), part(lane(1)), part(lane(2)), part(lane(3))}; };
  f32x4 x = gather([](auto& p) { return p.x; });
  f32x4 y = gather([](auto& p) { return p.y; });
  f32x4 z = gather([](auto& p) { return p.z; });
  //(times43() through the world and the view, times44() through the projection)
  const float *world = t.world, *view = t.view, *projection = t.projection;
  f32x4 wx = x * world[0] + y * world[3] + z * world[6] + world[9];
  f32x4 wy = x * world[1] + y * world[4] + z * world[7] + world[10];
  f32x4 wz = x * world[2] + y * world[5] + z * world[8] + world[11];
  f32x4 vx = wx * view[0] + wy * view[3] + wz * view[6] + view[9];
  f32x4 vy = wx * view[1] + wy * view[4] + wz * view[7] + view[10];
  f32x4 vz = wx * view[2] + wy * view[5] + wz * view[8] + view[11];
  f32x4 cx = vx * projection[0] + vy * projection[4] + vz * projection[8] + projection[12];
  f32x4 cy = vx * projection[1] + vy * projection[5] + vz * projection[9] + projection[13];
  f32x4 cz = vx * projection[2] + vy * projection[6] + vz * projection[10] + projection[14];
  f32x4 cw = vx * projection[3] + vy * projection[7] + vz * projection[11] + projection[15];
  auto s = projectedFour(t, cx, cy, cz, cw);
  for(u32 k = 0; k < lanes; k++) {
    auto& p = placing[k];
    p.model[0] = x[k], p.model[1] = y[k], p.model[2] = z[k];
    p.world[0] = wx[k], p.world[1] = wy[k], p.world[2] = wz[k];
    p.viewZ = vz[k];
    auto& out = v[k];
    out.clip[0] = cx[k], out.clip[1] = cy[k], out.clip[2] = cz[k], out.clip[3] = cw[k];
    out.x = s.x[k], out.y = s.y[k], out.z = s.z[k];
    out.outside = s.outside[k], out.far = s.far[k], out.near = s.near[k];
  }
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
