//The hardware renderer's fragment shader (docs/psp-gpu-renderers.md, "Shaders"): the GE's pixel pipeline up to
//blending, as the software renderer has it (ge/texture.cpp, ge/pixel.cpp), and in the same whole numbers where it
//can: the texel looked up and filtered as the GE does (by texelFetch, the four texels weighed in sixteenths, not by
//the GPU's own filtering), the texture function, lighting's shine, the alpha test, fog and the color test. What
//follows (stencil, depth, blending, the write) is the GPU's own, set up from the GE's settings by the renderer
//(gpu.cpp): blending's factors that depend on the pixel alone are applied here, those on the frame buffer's by the
//GPU, as the design's "Blending" says.
//
//Each choice the GE's state makes is a specialization constant, so each mix of settings games use is a shader of
//its own, compiled by the driver without what it doesn't use: the "generated shaders" of the design, made by the
//driver from one source. A pipeline's constants are its key's (gpu.cpp's Pipeline).

layout(constant_id = 0) const uint TEXTURED = 0u;
layout(constant_id = 1) const uint FUNCTION = 0u;    //TEXTURE_FUNCTION: bits 0-2 which, 3 with alpha, 4 doubled
layout(constant_id = 2) const uint ALPHA_TEST = 8u;  //its comparison (0-7), 8 none
layout(constant_id = 3) const uint COLOR_TEST = 4u;  //its comparison (0-3), 4 none
layout(constant_id = 4) const uint FOG = 0u;
layout(constant_id = 5) const uint DEPTH_RANGE = 0u;
layout(constant_id = 6) const uint CLEAR = 0u;
//How output 0's color is weighed for blending (the source factor's part that depends on the pixel alone; the
//frame buffer's is the GPU's): 0 not at all, 1 its alpha, 2 one less it, 3 twice it, 4 one less twice it (held at
//0), 5 BLEND_FIXED_A
layout(constant_id = 7) const uint SOURCE = 0u;
//What output 1 carries, for the destination's factor (dual-source blending): 0 the pixel's color, 1 one less it,
//2 its alpha, 3 one less it, 4 twice it (held at 1), 5 one less twice it (held at 0)
layout(constant_id = 8) const uint DESTINATION = 0u;
layout(constant_id = 9) const uint ALPHA_OUT = 0u;   //the alpha written: 0 the pixel's own, 1 push.stencil
layout(constant_id = 10) const uint DITHER = 0u;
layout(constant_id = 11) const uint QUANTIZE = 4u;   //narrowed to the frame buffer's format (0-2) and widened, 4 not
//A logic operation done here, where the GPU has none (LOGIC_OP: 0 clear, 12 inverted, 15 set; 3, copy, for the
//rest, which the renderer approximates otherwise)
layout(constant_id = 12) const uint LOGIC = 3u;
layout(constant_id = 13) const uint CLAMP = 0u;      //TEXTURE_WRAP: bit 0 across held at the edge, bit 1 down
//A texture taken from a frame buffer the GPU drew (render to texture): its format, 16-bit (0-2), or 4 for any other
layout(constant_id = 14) const uint TEXELS = 4u;

layout(push_constant) uniform Push {
  vec2 scale;
  uint alphaTest;       //the reference, and the mask in bits 8-15
  uint colorReference;  //24 bits each
  uint colorMask;
  uint fogColor;
  uint environment;     //TEXTURE_ENVIRONMENT_COLOR
  uint depthRange;      //MIN_Z, and MAX_Z in bits 16-31
  uint fixedA;          //BLEND_FIXED_A
  uint stencil;         //the stencil written where the test passes (ALPHA_OUT 1)
  uint dither[2];       //DITHER0-3: 16 nibbles, the row's four in each 16 bits
  uint textureSize;     //the texture's width and, in bits 16-31, its height (each at most 512)
} push;

layout(set = 0, binding = 0) uniform sampler2D texels;  //8888, red in the low byte (as the GE's decoded copies)

layout(location = 0) noperspective in vec4 vertexColor;
layout(location = 1) noperspective in vec4 vertexSpecular;
layout(location = 2) smooth in vec3 vertexCoordinates;
layout(location = 3) noperspective in float vertexFog;
layout(location = 4) noperspective in float vertexDepth;
layout(location = 5) flat in uint vertexFlags;
layout(location = 6) flat in vec4 vertexStepping;
layout(location = 7) flat in ivec2 vertexStart;

//Output 1 is the destination's factor for dual-source blending; a GPU without it gets the shader built without
//(SINGLE: compile.sh), and the renderer approximates those factors (gpu.cpp).
#ifdef SINGLE
layout(location = 0) out vec4 outColor;
vec4 outFactor;
#else
layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outFactor;
#endif

bool passes(uint comparison, uint a, uint b) {
  switch(comparison) {
  case 0u: return false;
  case 1u: return true;
  case 2u: return a == b;
  case 3u: return a != b;
  case 4u: return a < b;
  case 5u: return a <= b;
  case 6u: return a > b;
  }
  return a >= b;
}

//texture.cpp's texelAxis(): a coordinate's texel (or the two whose middles it lies between, and the fraction from
//the first to the second in sixteenths), inside the texture: repeated, or held at its edge.
uint inside(int c, uint size, bool held) {
  int last = int(size) - 1;
  return held ? uint(clamp(c, 0, last)) : uint(c & last);
}

//An 8888 channel narrowed to bits of it and widened again by repeating its top bits, as a 16-bit frame buffer
//keeps it (pixel.cpp's narrowPixel() and widenPixel())
uint narrowed(uint value, uint bits) {
  uint kept = value >> (8u - bits);
  return kept << (8u - bits) | kept >> (2u * bits - 8u);
}

//A texel; one of a texture taken from a 16-bit frame buffer (TEXELS) as its format keeps it, as the GE reads it
uvec4 fetch(uint u, uint v) {
  uvec4 t = uvec4(texelFetch(texels, ivec2(u, v), 0) * 255.0 + 0.5);
  if(TEXELS == 0u) t = uvec4(narrowed(t.r, 5u), narrowed(t.g, 6u), narrowed(t.b, 5u), 255u);
  if(TEXELS == 1u) t = uvec4(narrowed(t.r, 5u), narrowed(t.g, 5u), narrowed(t.b, 5u), t.a >= 128u ? 255u : 0u);
  if(TEXELS == 2u) t = uvec4(narrowed(t.r, 4u), narrowed(t.g, 4u), narrowed(t.b, 4u), narrowed(t.a, 4u));
  return t;
}

uvec4 sampleTexture(vec2 at, bool linear) {
  uint width = push.textureSize & 0xffffu, height = push.textureSize >> 16;
  bool heldU = (CLAMP & 1u) != 0u, heldV = (CLAMP & 2u) != 0u;
  vec2 held = clamp(at, vec2(-65536.0), vec2(65536.0));
  if(isnan(at.x)) held.x = 0.0;
  if(isnan(at.y)) held.y = 0.0;
  if(!linear) return fetch(inside(int(floor(held.x)), width, heldU), inside(int(floor(held.y)), height, heldV));
  ivec2 base = ivec2(floor(held * 256.0)) - 128;
  uint u0 = inside(base.x >> 8, width, heldU), u1 = inside((base.x >> 8) + 1, width, heldU);
  uint v0 = inside(base.y >> 8, height, heldV), v1 = inside((base.y >> 8) + 1, height, heldV);
  uint across = uint(base.x >> 4) & 15u, down = uint(base.y >> 4) & 15u;
  uvec4 upper = (fetch(u0, v0) * (16u - across) + fetch(u1, v0) * across) >> 4;
  uvec4 lower = (fetch(u0, v1) * (16u - across) + fetch(u1, v1) * across) >> 4;
  return (upper * (16u - down) + lower * down) >> 4;
}

uint channel(uint color, uint n) { return color >> (n * 8u) & 255u; }

//texture.cpp's textureFunctionWith()
uvec4 textureFunction(uvec4 f, uvec4 t) {
  uint function = FUNCTION & 7u;
  bool withAlpha = (FUNCTION & 8u) != 0u, doubled = (FUNCTION & 16u) != 0u;
  uint twice = doubled ? 2u : 1u, down = doubled ? 7u : 8u;
  uint alpha = withAlpha ? (f.a + 1u) * t.a >> 8 : f.a;
  uvec3 o;
  if(function == 0u) {
    o = (f.rgb + 1u) * t.rgb * twice >> 8;
  } else if(function == 1u) {
    o = withAlpha ? ((f.rgb + 1u) * (255u - t.a) + (t.rgb + 1u) * t.a) >> down : t.rgb * twice;
    alpha = f.a;
  } else if(function == 2u) {
    uvec3 e = uvec3(channel(push.environment, 0u), channel(push.environment, 1u), channel(push.environment, 2u));
    o = ((255u - t.rgb) * f.rgb + t.rgb * e + 255u) >> down;
  } else if(function == 3u) {
    o = t.rgb * twice;
    alpha = withAlpha ? t.a : f.a;
  } else {
    o = (f.rgb + t.rgb) * twice;
  }
  return uvec4(min(o, uvec3(255u)), min(alpha, 255u));
}

void main() {
  float depth = clamp(vertexDepth, 0.0, 65535.0);
  if(DEPTH_RANGE != 0u) {
    uint z = uint(depth);
    if(z < (push.depthRange & 0xffffu) || z > push.depthRange >> 16) discard;
  }
  //(the GE blends a color's channels as floats and drops their fractions; the interpolation's own error is kept
  //from tipping a whole number below itself)
  uvec4 color = uvec4(clamp(floor(vertexColor + 1.0 / 512.0), 0.0, 255.0));
  if(CLEAR != 0u) {
    outColor = vec4(color) / 255.0;
    outFactor = vec4(0.0);
    return;
  }
  if(TEXTURED != 0u) {
    vec2 at = vertexCoordinates.xy / vertexCoordinates.z;
    if((vertexFlags & 2u) != 0u) {
      //a 2D sprite's, stepped from its edge as the GE steps them (raster.cpp's spriteRows()): the pixel's middle
      //in sixteenths from the start (exact), times the step, plus the first, rounded once as the GE's doubles are
      vec2 middle = gl_FragCoord.xy * 16.0;
      precise float column = fma((middle.x - float(vertexStart.x)) / 16.0, vertexStepping.y, vertexStepping.x);
      precise float row = fma((middle.y - float(vertexStart.y)) / 16.0, vertexStepping.w, vertexStepping.z);
      at = (vertexFlags & 4u) != 0u ? vec2(row, column) : vec2(column, row);
    }
    color = textureFunction(color, sampleTexture(at, (vertexFlags & 1u) != 0u));
  }
  uvec4 shine = uvec4(clamp(floor(vertexSpecular + 1.0 / 512.0), 0.0, 255.0));
  color.rgb = min(color.rgb + shine.rgb, uvec3(255u));
  if(ALPHA_TEST != 8u) {
    uint mask = push.alphaTest >> 8 & 255u;
    if(!passes(ALPHA_TEST, color.a & mask, push.alphaTest & mask)) discard;
  }
  if(FOG != 0u) {
    uint f = vertexFog <= 0.0 ? 0u : vertexFog >= 1.0 ? 255u : uint(vertexFog * 256.0);
    uvec3 c = uvec3(channel(push.fogColor, 0u), channel(push.fogColor, 1u), channel(push.fogColor, 2u));
    color.rgb = (color.rgb * f + c * (255u - f) + 255u) >> 8;
  }
  if(COLOR_TEST != 4u && COLOR_TEST != 1u) {
    uint packed = color.r | color.g << 8 | color.b << 16;
    bool equal = (packed & push.colorMask) == (push.colorReference & push.colorMask);
    if(COLOR_TEST == 0u || (COLOR_TEST == 2u) != equal) discard;
  }
  //blending's factors that depend on the pixel alone (see SOURCE and DESTINATION)
  vec3 rgb = vec3(color.rgb) / 255.0, weight = vec3(1.0);
  float alpha = float(color.a) / 255.0;
  if(SOURCE == 1u) weight = vec3(alpha);
  if(SOURCE == 2u) weight = vec3(1.0 - alpha);
  if(SOURCE == 3u) weight = vec3(2.0 * alpha);
  if(SOURCE == 4u) weight = vec3(max(1.0 - 2.0 * alpha, 0.0));
  if(SOURCE == 5u) {
    weight = vec3(channel(push.fixedA, 0u), channel(push.fixedA, 1u), channel(push.fixedA, 2u)) / 255.0;
  }
  vec3 factor = rgb;
  if(DESTINATION == 1u) factor = 1.0 - rgb;
  if(DESTINATION == 2u) factor = vec3(alpha);
  if(DESTINATION == 3u) factor = vec3(1.0 - alpha);
  if(DESTINATION == 4u) factor = vec3(min(2.0 * alpha, 1.0));
  if(DESTINATION == 5u) factor = vec3(max(1.0 - 2.0 * alpha, 0.0));
  outFactor = vec4(factor, 0.0);
  //dithering and the frame buffer's format, where the GPU's blending won't change the color after (QUANTIZE 4 and
  //DITHER 0 otherwise: blended, they're approximated by the 8888 the GPU keeps)
  if(DITHER != 0u) {
    uvec2 at = uvec2(gl_FragCoord.xy) & 3u;
    uint nibble = push.dither[at.y >> 1] >> ((at.y & 1u) * 16u + at.x * 4u) & 15u;
    int offset = nibble < 8u ? int(nibble) : int(nibble) - 16;
    color.rgb = uvec3(clamp(ivec3(color.rgb) + offset, 0, 255));
    rgb = vec3(color.rgb) / 255.0;
  }
  if(QUANTIZE == 0u) rgb = vec3(narrowed(color.r, 5u), narrowed(color.g, 6u), narrowed(color.b, 5u)) / 255.0;
  if(QUANTIZE == 1u) rgb = vec3(narrowed(color.r, 5u), narrowed(color.g, 5u), narrowed(color.b, 5u)) / 255.0;
  if(QUANTIZE == 2u) rgb = vec3(narrowed(color.r, 4u), narrowed(color.g, 4u), narrowed(color.b, 4u)) / 255.0;
  if(LOGIC == 0u) rgb = vec3(0.0);
  if(LOGIC == 12u) rgb = 1.0 - rgb;
  if(LOGIC == 15u) rgb = vec3(1.0);
  float stencil = ALPHA_OUT == 1u ? float(push.stencil) / 255.0 : alpha;
  outColor = vec4(rgb * weight, stencil);
}
