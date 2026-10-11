//Lighting (3D, LIGHTING_ENABLE): each vertex's color worked out from the lights shining on it, the way lamps light a
//model, once per vertex after the transform; the colors are then stepped across each triangle as usual. What goes in:
//  - the material, the model's own colors: emissive (MATERIAL_EMISSIVE: light it gives off itself), ambient
//    (AMBIENT_COLOR, AMBIENT_ALPHA), diffuse (MATERIAL_DIFFUSE: what direct light shows), specular
//    (MATERIAL_SPECULAR: its shine), and how tight the shine is (MATERIAL_SPECULAR_COEF). MATERIAL_COLOR's bits 0-2
//    let the vertex's own color, if it has one, stand for the ambient, diffuse and specular colors.
//  - the ambient light, lighting everything evenly (AMBIENT_LIGHT_COLOR, AMBIENT_LIGHT_ALPHA);
//  - up to four lights (LIGHT_ENABLE0-3). LIGHT_TYPEn bits 8-9 say what kind: 0 directional, like the sun (its
//    position is the direction toward it); 1 a point, like a bulb; 2 a spotlight, a point lighting only the cone
//    around LIGHTn_DIRECTION (where the cosine reaches LIGHTn_CUTOFF), brighter toward the middle by
//    LIGHTn_EXPONENT. Bits 0-1 say what it does: 0 ambient and diffuse; 1 those and specular; 2 ambient and a
//    "powered" diffuse, tightened like a shine. Each has an ambient, a diffuse and a specular color; and all but
//    directional ones fade with the distance d as 1 / (constant + linear d + quadratic d²) (LIGHTn_*_ATTEN), held to
//    0-1.
//
//A vertex's color is emissive + ambient light × material ambient, plus for each light, times its fading and spot:
//  - its ambient × the material ambient;
//  - its diffuse × the material diffuse × how squarely it falls (the cosine between the vertex's normal, in the
//    world, and the direction to the light), if it falls on the front;
//  - its specular × the material specular × the shine: the cosine between the normal and the direction half way
//    between the light's and the viewer's (the viewer far off along the view's z), to the power of the coefficient.
//With LIGHT_MODE 1 the shine is kept apart, as a second color added after texturing (so a dark texture doesn't dull
//it); otherwise it's added in. Each channel ends held to 0-255.
//
//The arithmetic is the GE's: an 8-bit color c counts as 2c + 1, so that white times white is white; two colors
//multiply and shift down 10 bits; a light's share counts 256ths, rounded up (times the two colors, then down 18
//bits). "To the power of" is the GE's own quick approximation, exact at powers of two and a little low between them,
//and the coefficient keeps only the top four bits of its fraction. (As PPSSPP reads it from tests on the PSP, but for
//the share, which PPSSPP counts in 512ths, rounded up and one more: the PSP's own pictures settled 256ths, round 3's
//lighting files in docs/psp-core.md. Rounding up at a whole 256th is assumed, no measured share landing on one; and
//a few products come out a level lower on the PSP than this: part 48 found the PSP's share finer than a 256th, its
//own cosine, off the true one either way, which round 5 of tools/psp-measure measures.)
//
//Environment mapping (TEXTURE_MAP_MODE 2) takes texture coordinates from two lights (TEXTURE_SHADE_MAPPING bits 0-1
//for u, 8-9 for v), lit or not: (the cosine between the normal and the direction to that light, or with a shining
//light the half-way direction, + 1) / 2.

//A color's channel n (0 red, 1 green, 2 blue, 3 alpha) as the GE multiplies it: 2c + 1.
static auto factor(u32 color, u32 n) -> s32 { return s32(color >> n * 8 & 0xff) * 2 + 1; }

//A light's share in 256ths, rounded up, at most 256. A spotlight's can be below 0 (its power of a cosine below 0
//stays below 0), which darkens; it can't be below -1 (here -256). (The darkening is PPSSPP's reading, carried over to
//256ths: not measured.)
static auto share(float amount) -> s32 {
  float value = std::ceil(256 * amount);
  if(std::isnan(value)) return 0;
  return value < 256 ? s32(std::max(value, -256.0f)) : 256;
}

//The GE's x to the power e for x above 0: 2 to the e × log2(x), with log2 and its inverse each taken as straight
//lines between powers of two (which is what a float's bits are, read as a whole number: its exponent, then its
//fraction). So 0.5 squared is 0.25, but 0.75 squared is 0.5 rather than 0.5625.
static auto quickPower(float x, float e) -> float {
  u32 bits;
  std::memcpy(&bits, &x, 4);
  float result = std::max(e, 0.0f) * float(s32(bits - 0x3f80'0000)) + float(0x3f80'0000);
  result = result >= 0 ? std::min(result, float(0x7f7f'ffff)) : 0;  //(not a number: 0; at most infinity's bits)
  bits = u32(result);
  std::memcpy(&x, &bits, 4);
  return x;
}

//To the power e as lighting uses it: 1 when e is 0 or less; the quick power above 0; a cosine below 0 left as it is.
static auto lightPower(float x, float e) -> float {
  if(e <= 0) return 1;
  return x > 0 ? quickPower(x, e) : x;
}

static auto dot3(const float* a, const float* b) -> float { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

//Made one long, returning the length it had; one of no length becomes (0, 0, 1), or with zeroIfNone stays 0.
static auto normalize3(float* v, bool zeroIfNone = false) -> float {
  float length = std::sqrt(dot3(v, v));
  if(length > 0) {
    for(u32 n = 0; n < 3; n++) v[n] /= length;
  } else if(!zeroIfNone) {
    v[0] = 0, v[1] = 0, v[2] = 1;
  }
  return length;
}

auto GE::lightingState(Transform& t) const -> void {
  t.lighting = commands[LightingEnable] & 1;
  t.separateSpecular = commands[LightMode] & 1;
  t.normalReverse = commands[NormalReverse] & 1;
  t.materialColor = commands[MaterialColor] & 7;
  t.materialEmissive = commands[MaterialEmissive] & 0xff'ffff;
  t.materialAmbient = (commands[AmbientColor] & 0xff'ffff) | (commands[AmbientAlpha] & 0xff) << 24;
  t.materialDiffuse = commands[MaterialDiffuse] & 0xff'ffff;
  t.materialSpecular = commands[MaterialSpecular] & 0xff'ffff;
  t.ambientLight = (commands[AmbientLightColor] & 0xff'ffff) | (commands[AmbientLightAlpha] & 0xff) << 24;
  //the coefficient: only the top four bits of its fraction count
  float power = float24(commands[MaterialSpecularCoefficient] & 0xff'f800);
  t.specularPower = std::isnan(power) ? (std::signbit(power) ? 0.0f : INFINITY) : std::max(power, 0.0f);
  float toViewer[3] = {t.view[2], t.view[5], t.view[8]};  //where the view's z points, in the world
  normalize3(toViewer);
  for(u32 n = 0; n < 3; n++) t.viewDirection[n] = toViewer[n];
  for(u32 index = 0; index < 4; index++) {
    auto& light = t.lights[index];
    u32 type = commands[LightType0 + index];
    light.enabled = commands[LightEnable0 + index] & 1;
    light.directional = (type >> 8 & 3) == 0;
    light.spot = (type >> 8 & 3) == 2;
    light.specular = (type & 3) == 1;
    light.powered = (type & 3) == 2;
    for(u32 n = 0; n < 3; n++) {
      light.position[n] = float24(commands[Light0X + index * 3 + n]);
      light.direction[n] = float24(commands[Light0DirectionX + index * 3 + n]);
      light.attenuation[n] = float24(commands[Light0ConstantAttenuation + index * 3 + n]);
    }
    normalize3(light.direction);
    //A directional light's way to it, and the half way between that and the viewer's that its shine takes, are the
    //same at every vertex: worked out here once, as light() would at each (the same operations on the same numbers).
    for(u32 n = 0; n < 3; n++) light.toLight[n] = light.position[n];
    normalize3(light.toLight);
    for(u32 n = 0; n < 3; n++) light.halfway[n] = light.toLight[n] + t.viewDirection[n];
    normalize3(light.halfway);
    light.cutoff = float24(commands[Light0CutoffAttenuation + index]);
    if(std::isnan(light.cutoff) && std::signbit(light.cutoff)) light.cutoff = 0;
    light.exponent = float24(commands[Light0ExponentAttenuation + index]);
    if(std::isnan(light.exponent)) light.exponent = std::signbit(light.exponent) ? 0.0f : INFINITY;
    light.exponent = std::max(light.exponent, 0.0f);
    light.ambient = commands[Light0Ambient + index * 3] & 0xff'ffff;
    light.diffuse = commands[Light0Ambient + index * 3 + 1] & 0xff'ffff;
    light.shine = commands[Light0Ambient + index * 3 + 2] & 0xff'ffff;
    //Its colors times the material's, the same at every vertex unless a vertex's own color stands for the
    //material's (light() then works them out there): the products light() would make, 511 × 511 at most, which a
    //share of 256 at most leaves far inside 32 bits, so multiplying by the share after is the same sum.
    for(u32 n = 0; n < 4; n++) {
      light.products[0][n] = factor(light.ambient, n) * factor(t.materialAmbient, n);
      light.products[1][n] = factor(light.diffuse, n) * factor(t.materialDiffuse, n);
      light.products[2][n] = factor(light.shine, n) * factor(t.materialSpecular, n);
    }
  }
  for(u32 n = 0; n < 4; n++) {
    t.unlit[n] = channel(t.materialEmissive, n) + (factor(t.materialAmbient, n) * factor(t.ambientLight, n) >> 10);
  }
  t.shadeU = commands[TextureShadeMapping] & 3;
  t.shadeV = commands[TextureShadeMapping] >> 8 & 3;
}

//A vertex lit: world is where it is in the world, normal its normal there (one long).
auto GE::light(Vertex& vertex, const float world[3], const float normal[3], const Transform& t) const -> void {
  //which of the material's colors the vertex's own stands for (bit 0 ambient, 1 diffuse, 2 shine)
  u32 own = t.vertexColor ? t.materialColor : 0;
  s32 sum[4], shine[4] = {};
  for(u32 n = 0; n < 4; n++) {
    sum[n] = own & 1 ? channel(t.materialEmissive, n) + (factor(vertex.color, n) * factor(t.ambientLight, n) >> 10)
                     : t.unlit[n];
  }
  auto add = [](s32* total, const s32* product, s32 share) {
    for(u32 n = 0; n < 4; n++) total[n] += product[n] * share >> 18;
  };
  for(auto& light : t.lights) {
    if(!light.enabled) continue;
    //the light's colors times the material's (lightingState()), or times the vertex's where it stands for them
    s32 made[3][4];
    const s32* products[3];
    const u32 colors[3] = {light.ambient, light.diffuse, light.shine};
    for(u32 kind = 0; kind < 3; kind++) {
      products[kind] = light.products[kind];
      if(!(own >> kind & 1)) continue;
      for(u32 n = 0; n < 4; n++) made[kind][n] = factor(colors[kind], n) * factor(vertex.color, n);
      products[kind] = made[kind];
    }
    float toLight[3] = {light.position[0], light.position[1], light.position[2]};
    float strength = 1;
    if(!light.directional) {
      for(u32 n = 0; n < 3; n++) toLight[n] -= world[n];
      float distance = normalize3(toLight);
      strength = 1 / (light.attenuation[0] + light.attenuation[1] * distance + light.attenuation[2] * distance * distance);
      strength = strength > 0 ? std::min(strength, 1.0f) : 0.0f;  //(not a number: 0)
    } else {
      for(u32 n = 0; n < 3; n++) toLight[n] = light.toLight[n];  //(normalize3(toLight), worked out once)
    }
    if(light.spot) {
      float along = dot3(light.direction, toLight);  //the cosine between the spot's direction and the light's
      if(std::isnan(along)) along = std::signbit(along) ? 0.0f : 1.0f;
      float spot = along >= light.cutoff ? lightPower(along, light.exponent) : 0.0f;
      strength *= std::isnan(spot) ? 0.0f : spot;
    }
    add(sum, products[0], share(strength));
    float facing = dot3(toLight, normal);
    if(light.powered) facing = lightPower(facing, t.specularPower);
    if(facing > 0) add(sum, products[1], share(strength * facing));
    if(light.specular && facing >= 0) {
      float halfway[3] = {light.halfway[0], light.halfway[1], light.halfway[2]};
      if(!light.directional) {
        for(u32 n = 0; n < 3; n++) halfway[n] = toLight[n] + t.viewDirection[n];
        normalize3(halfway);
      }
      float gleam = lightPower(dot3(halfway, normal), t.specularPower);
      if(gleam > 0) add(shine, products[2], share(strength * gleam));
    }
  }
  if(t.separateSpecular) {
    vertex.color = pack(sum[0], sum[1], sum[2], sum[3]);
    vertex.specular = pack(shine[0], shine[1], shine[2], 0);
  } else {
    vertex.color = pack(sum[0] + shine[0], sum[1] + shine[1], sum[2] + shine[2], sum[3] + shine[3]);
    vertex.specular = 0;
  }
}

//Environment mapping's texture coordinate (0-1) from light index, for a vertex at world with normal.
static auto shadeCoordinate(const GE::Transform& t, u32 index, const float world[3], const float normal[3]) -> float {
  auto& light = t.lights[index];
  float toLight[3] = {light.position[0], light.position[1], light.position[2]};
  if(!light.directional) {
    for(u32 n = 0; n < 3; n++) toLight[n] -= world[n];
  }
  normalize3(toLight, true);
  if(light.specular) {
    for(u32 n = 0; n < 3; n++) toLight[n] += t.viewDirection[n];
    normalize3(toLight, true);
  }
  return (dot3(toLight, normal) + 1) * 0.5f;
}
