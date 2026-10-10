//Fast mode's vertex shader (docs/psp-gpu-renderers.md, "Vulkan (fast)"): a 3D vertex as the vertex type laid it out
//(the GE blends morph targets as it reads it), skinned, transformed, lit, fogged and given its texture coordinates
//here, as ge/transform.cpp and ge/lighting.cpp do on the CPU for the accurate path and the software renderer: in
//their whole numbers where they have them (lighting's products, shares and quick power), in floats where they're
//floats. Then put where Vulkan's clip space has the GE's screen, for draw.frag, as draw.vert puts the CPU's: x and y
//cut to the GE's sixteenths of a pixel toward 2048, the depth held to 0-65535. Which triangles are drawn is the GE's
//to say (GE::meshTriangles(): it drops and culls them, and cuts and draws those reaching past the near plane
//itself), so the GPU clips nothing more.

layout(push_constant) uniform Push {
  vec2 scale;  //2 / the target's width and height: pixels to Vulkan's -1 to 1
} push;

//The PRIMs' settings (gpu.hpp's Transformed, as the GE's Transform has them), a block each in the run's storage
//buffer, the vertex naming its PRIM's (t)
struct Light {
  vec4 position;     //(w unused)
  vec4 direction;    //one long; w its cutoff
  vec4 attenuation;  //constant, linear, quadratic; w its exponent
  uvec4 colors;      //ambient, diffuse, shine (24-bit); w: bit 0 on, 1 directional, 2 spot, 3 shines, 4 powered
};
struct Transformed {
  mat4x3 world;
  mat4x3 view;
  mat4 projection;
  mat4x3 textureMatrix;
  vec4 viewport;       //VIEWPORT_X/Y/Z_SCALE
  vec4 center;         //VIEWPORT_X/Y/Z_CENTER
  vec4 offset;         //OFFSET_X and OFFSET_Y in pixels; the texture's width and height
  vec4 textureScale;   //TEX_SCALE_U, _V, TEX_OFFSET_U, _V
  vec4 fog;            //FOG1, FOG2, the value forced; 1 where it's forced
  uvec4 modes;         //the texture matrix's source; the lights environment mapping takes u and v from; bits: 0
                       //NORMAL_REVERSE, 1 the shine kept apart, 2 the vertex has a color, 3 FOG_ENABLE, 4
                       //LIGHTING_ENABLE, 5-6 TEXTURE_MAP_MODE (0 scaled, 1 the texture matrix, 2 environment),
                       //8-11 the bone matrices each vertex mixes (0-8: skinning)
  uvec4 material;      //MATERIAL_COLOR; emissive; ambient (8888); diffuse
  uvec4 material2;     //specular; the ambient light (8888)
  vec4 viewDirection;  //where the view looks, in the world; w the specular power
  Light lights[4];
  mat4x3 bones[8];
};
layout(set = 2, binding = 0, std430) readonly buffer Transforms { Transformed blocks[]; };

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 coordinates;
layout(location = 3) in vec4 color;  //8888 (UNORM)
layout(location = 4) in vec4 weightsLow;
layout(location = 5) in vec4 weightsHigh;
layout(location = 6) in uint transform;
#define t blocks[transform]

layout(location = 0) noperspective out vec4 vertexColor;
layout(location = 1) noperspective out vec4 vertexSpecular;
layout(location = 2) smooth out vec3 vertexCoordinates;
layout(location = 3) noperspective out float vertexFog;
layout(location = 4) noperspective out float vertexDepth;
layout(location = 5) flat out uint vertexFlags;
layout(location = 6) flat out vec4 vertexStepping;
layout(location = 7) flat out ivec2 vertexStart;
layout(location = 8) flat out vec4 vertexFlatColor;
layout(location = 9) flat out vec4 vertexFlatSpecular;
layout(location = 10) noperspective out vec2 vertexTexels;  //(the texture coordinates straight across the screen)

//(invariant: the same vertex lands on the same place and depth whatever else the pipeline does, lighting or not,
//mapping or not: games draw a model twice, its gloss over it with a depth test of equal, as GTA's cars)
out gl_PerVertex {
  invariant vec4 gl_Position;
};

uint channel(uint color, uint n) { return color >> (n * 8u) & 255u; }

//lighting.cpp's factor(): a channel as the GE multiplies it, 2c + 1
int factor(uint color, uint n) { return int(channel(color, n)) * 2 + 1; }

//lighting.cpp's share(): a light's share in 256ths, rounded up, -256 to 256
int share(float amount) {
  float value = ceil(256.0 * amount);
  if(isnan(value)) return 0;
  return value < 256.0 ? int(max(value, -256.0)) : 256;
}

//lighting.cpp's quickPower() and lightPower(): the GE's x to the power e, from a float's bits
float quickPower(float x, float e) {
  uint bits = floatBitsToUint(x);
  float result = max(e, 0.0) * float(int(bits - 0x3f800000u)) + float(0x3f800000u);
  result = result >= 0.0 ? min(result, float(0x7f7fffffu)) : 0.0;
  return uintBitsToFloat(uint(result));
}
float lightPower(float x, float e) { return e <= 0.0 ? 1.0 : x > 0.0 ? quickPower(x, e) : x; }

//lighting.cpp's normalize3(): one long, or (0, 0, 1) (zeroIfNone: 0) where it has no length
vec3 normalized(vec3 v, bool zeroIfNone) {
  float length = sqrt(dot(v, v));
  if(length > 0.0) return v / length;
  return zeroIfNone ? vec3(0.0) : vec3(0.0, 0.0, 1.0);
}

bool negative(float value) { return floatBitsToUint(value) >> 31 != 0u; }

uint pack(ivec4 c) {
  uvec4 held = uvec4(clamp(c, ivec4(0), ivec4(255)));
  return held.r | held.g << 8 | held.b << 16 | held.a << 24;
}

//lighting.cpp's light(): the vertex's color and shine from the lights, at world with its normal there (one long)
void light(uint own, vec3 world, vec3 normal, inout uint colored, out uint shone) {
  uint stands = (t.modes.w & 4u) != 0u ? t.material.x : 0u;  //(the vertex's color for the material's)
  uint emissive = t.material.y, ambient = t.material.z, diffuse = t.material.w;
  uint specular = t.material2.x, ambientLight = t.material2.y;
  ivec4 sum, shine = ivec4(0);
  for(uint n = 0u; n < 4u; n++) {
    uint from = (stands & 1u) != 0u ? own : ambient;
    sum[n] = int(channel(emissive, n)) + (factor(from, n) * factor(ambientLight, n) >> 10);
  }
  for(uint index = 0u; index < 4u; index++) {
    Light l = t.lights[index];
    uint kinds = l.colors.w;
    if((kinds & 1u) == 0u) continue;
    ivec4 products[3];
    uint materials[3] = uint[3](ambient, diffuse, specular);
    for(uint kind = 0u; kind < 3u; kind++) {
      uint by = (stands >> kind & 1u) != 0u ? own : materials[kind];
      for(uint n = 0u; n < 4u; n++) products[kind][n] = factor(l.colors[kind], n) * factor(by, n);
    }
    vec3 toLight = l.position.xyz;
    float strength = 1.0;
    if((kinds & 2u) == 0u) {
      toLight -= world;
      float distance = sqrt(dot(toLight, toLight));
      toLight = normalized(toLight, false);
      strength = 1.0 / (l.attenuation.x + l.attenuation.y * distance + l.attenuation.z * distance * distance);
      strength = strength > 0.0 ? min(strength, 1.0) : 0.0;
    } else {
      toLight = normalized(toLight, false);
    }
    if((kinds & 4u) != 0u) {
      float along = dot(l.direction.xyz, toLight);
      if(isnan(along)) along = negative(along) ? 0.0 : 1.0;
      float spot = along >= l.direction.w ? lightPower(along, l.attenuation.w) : 0.0;
      strength *= isnan(spot) ? 0.0 : spot;
    }
    sum += products[0] * share(strength) >> 18;
    float facing = dot(toLight, normal);
    if((kinds & 16u) != 0u) facing = lightPower(facing, t.viewDirection.w);
    if(facing > 0.0) sum += products[1] * share(strength * facing) >> 18;
    if((kinds & 8u) != 0u && facing >= 0.0) {
      vec3 halfway = normalized(toLight + t.viewDirection.xyz, false);
      float gleam = lightPower(dot(halfway, normal), t.viewDirection.w);
      if(gleam > 0.0) shine += products[2] * share(strength * gleam) >> 18;
    }
  }
  if((t.modes.w & 2u) != 0u) {
    colored = pack(sum), shone = pack(ivec4(shine.rgb, 0));
  } else {
    colored = pack(sum + shine), shone = 0u;
  }
}

//lighting.cpp's shadeCoordinate(): environment mapping's coordinate (0-1) from light index
float shadeCoordinate(uint index, vec3 world, vec3 normal) {
  Light l = t.lights[index & 3u];
  vec3 toLight = l.position.xyz;
  if((l.colors.w & 2u) == 0u) toLight -= world;
  toLight = normalized(toLight, true);
  if((l.colors.w & 8u) != 0u) toLight = normalized(toLight + t.viewDirection.xyz, true);
  return (dot(toLight, normal) + 1.0) * 0.5;
}

void main() {
  uint bones = t.modes.w >> 8 & 15u, lighting = t.modes.w >> 4 & 1u, mapping = t.modes.w >> 5 & 3u;
  //(precise: the position's arithmetic done as written, nothing fused or reordered by the driver's compiler, which
  //put corners a sixteenth from where project() puts them more often)
  precise vec3 model = position;
  vec3 turned = normal;
  if(bones > 0u) {
    float weights[8] = float[8](weightsLow.x, weightsLow.y, weightsLow.z, weightsLow.w,
                                weightsHigh.x, weightsHigh.y, weightsHigh.z, weightsHigh.w);
    vec3 moved = vec3(0.0), turnedBy = vec3(0.0);
    for(uint n = 0u; n < min(bones, 8u); n++) {
      moved += t.bones[n] * vec4(position, 1.0) * weights[n];
      turnedBy += mat3(t.bones[n]) * normal * weights[n];
    }
    model = moved, turned = turnedBy;
  }
  precise vec3 inWorld = t.world * vec4(model, 1.0);
  precise vec3 inView = t.view * vec4(inWorld, 1.0);
  precise vec4 clip = t.projection * vec4(inView, 1.0);
  bool reversed = (t.modes.w & 1u) != 0u;

  //the normal in the world, one long, for lighting and environment mapping
  vec3 worldNormal = vec3(0.0, 0.0, 1.0);
  if(lighting != 0u || mapping == 2u) {
    vec3 inWorldNormal = mat3(t.world) * (turned * (reversed ? -1.0 : 1.0));
    float length = sqrt(dot(inWorldNormal, inWorldNormal));
    if(length > 0.0) worldNormal = inWorldNormal / length;
  }

  //the texture coordinates, in texels
  vec2 size = t.offset.zw;
  vec3 uvq;
  if(mapping == 2u) {
    uvq = vec3(shadeCoordinate(t.modes.y, inWorld, worldNormal) * size.x,
               shadeCoordinate(t.modes.z, inWorld, worldNormal) * size.y, 1.0);
  } else if(mapping == 1u) {
    vec3 source = model;
    if(t.modes.x == 1u) source = vec3(coordinates, 0.0);
    if(t.modes.x >= 2u) {
      float length = t.modes.x == 2u ? sqrt(dot(turned, turned)) : 1.0;
      source = turned / (reversed ? -length : length);
    }
    vec3 stq = t.textureMatrix * vec4(source, 1.0);
    uvq = vec3(stq.xy * size, stq.z);
  } else {
    uvq = vec3((coordinates * t.textureScale.xy + t.textureScale.zw) * size, 1.0);
  }

  //fog: how much of the color stays, at the vertex, as the 0-255 draw.frag blends (gpu.cpp's fogAsAttribute())
  float fogged = 1.0;
  if((t.modes.w & 8u) != 0u) fogged = t.fog.w != 0.0 ? t.fog.z : (inView.z + t.fog.x) * t.fog.y;
  vertexFog = negative(fogged) ? 0.0 : !(fogged < 1.0) ? 255.0 / 256.0 : floor(fogged * 256.0) / 256.0;

  //the colors: the vertex's (or lit)
  uvec4 own = uvec4(color * 255.0 + 0.5);
  uint colored = own.r | own.g << 8 | own.b << 16 | own.a << 24, shone = 0u;
  if(lighting != 0u) light(colored, inWorld, worldNormal, colored, shone);
  vec4 lit = vec4(uvec4(colored, colored >> 8, colored >> 16, colored >> 24) & 255u);
  vec4 shines = vec4(uvec4(shone, shone >> 8, shone >> 16, shone >> 24) & 255u);
  vertexColor = lit, vertexFlatColor = lit;
  vertexSpecular = shines, vertexFlatSpecular = shines;

  //onto the screen: transform.cpp's project(), x and y cut to the GE's sixteenths toward 2048, and the depth held to
  //0-65535 at each corner, as the GE steps it between them (the GE hands over no triangle with a corner behind the
  //camera; one the GPU's rounding puts there goes at the GPU's own straight lines)
  float w = clip.w;
  if(w > 0.0) {
    precise vec2 screen = clip.xy * t.viewport.xy / w + t.center.xy;
    precise vec2 pixels = 2048.0 + trunc((screen - 2048.0) * 16.0) / 16.0 - t.offset.xy;
    precise float z = clip.z * t.viewport.z / w + t.center.z;
    float held = z > 0.0 ? min(z, 65535.0) : 0.0;
    gl_Position = vec4((pixels * push.scale - 1.0) * w, held / 65535.0 * w, w);
    vertexDepth = held;
  } else {
    vec2 pixels = clip.xy * t.viewport.xy + (t.center.xy - t.offset.xy) * w;
    gl_Position = vec4(pixels * push.scale - w, (clip.z * t.viewport.z + t.center.z * w) / 65535.0, w);
    vertexDepth = 0.0;
  }
  vertexCoordinates = uvq;
  vertexTexels = uvq.xy;
  vertexFlags = 16u;  //(the filter chosen at each pixel: draw.frag)
  vertexStepping = vec4(0.0);
  vertexStart = ivec2(0);
}
