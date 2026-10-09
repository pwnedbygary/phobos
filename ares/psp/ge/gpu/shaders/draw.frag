//The hardware renderer's fragment shader (docs/psp-gpu-renderers.md, "Shaders"): the GE's pixel pipeline up to
//blending, as the software renderer has it (ge/texture.cpp, ge/pixel.cpp), and in the same whole numbers where it
//can: the texel looked up and filtered as the GE does (by texelFetch, the four texels weighed in sixteenths, not by
//the GPU's own filtering), the texture function, lighting's shine, the alpha test, fog and the color test. What
//follows (stencil, depth, blending, the write) is the GPU's own, set up from the GE's settings by the renderer
//(gpu.cpp): blending's factors that depend on the pixel alone are applied here, those on the frame buffer's by the
//GPU, as the design's "Blending" says. Where the GPU lets the shader read the frame buffer's pixel (READS: the
//design's "Shader blending"), blending, dithering, the logic operation and the write mask are pixel.cpp's instead,
//in its whole numbers, and the GPU only writes the result.
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
//Shader blending: 1 where the frame buffer's pixel is read (frameBuffer), and what follows the tests is pixel.cpp's:
//blending by SOURCE's and DESTINATION's factors (the GE's, 0-15) with BLENDING's operation (BLEND_MODE's, 0-7;
//8 none), dithering (DITHER), the logic operation (LOGIC, any of the 16), the write mask (push.writeMask), each
//write in the frame buffer's format (QUANTIZE)
layout(constant_id = 15) const uint READS = 0u;
layout(constant_id = 16) const uint BLENDING = 8u;
//Where the GPU blends with output 0's color as it is (its source factor one), that color is the source term made as
//pixel.cpp makes it, in whole numbers (TERM). The PSP drops each term's fraction and then adds, where the GPU adds
//and rounds once: so the term goes a quarter of a step below itself where the GPU adds the destination's term (1),
//above where it subtracts (2), which puts the GPU's rounding of the sum on the PSP's nearly always. 0 the color
//weighed as floats.
layout(constant_id = 17) const uint TERM = 0u;
//Fast mode's 3D (transform.vert): flat shading's colors are the triangle's first corner's, which mesh() makes the
//GE's last
layout(constant_id = 18) const uint FLAT = 0u;

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
  uint resolution;      //the target's: each of the PSP's pixels resolution x resolution of the GPU's (gpu.hpp)
  uint textureScale;    //the texture's: a copy of a target is at the target's resolution, a decoded texture at 1
  uint fixedB;          //BLEND_FIXED_B (READS)
  uint writeMask;       //the frame buffer's bits left alone, in its format (READS)
  uint filtering;       //TEXTURE_FILTER, where the filter is chosen at each pixel (vertexFlags' bit 4)
} push;

layout(set = 0, binding = 0) uniform sampler2D texels;  //8888, red in the low byte (as the GE's decoded copies)
layout(input_attachment_index = 0, set = 1, binding = 0) uniform subpassInput frameBuffer;  //(READS) the target's

layout(location = 0) noperspective in vec4 vertexColor;
layout(location = 1) noperspective in vec4 vertexSpecular;
layout(location = 2) smooth in vec3 vertexCoordinates;
layout(location = 3) noperspective in float vertexFog;
layout(location = 4) noperspective in float vertexDepth;
layout(location = 5) flat in uint vertexFlags;
layout(location = 6) flat in vec4 vertexStepping;
layout(location = 7) flat in ivec2 vertexStart;
layout(location = 8) flat in vec4 vertexFlatColor;
layout(location = 9) flat in vec4 vertexFlatSpecular;
layout(location = 10) noperspective in vec2 vertexTexels;

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
//the first to the second in sixteenths), inside the texture: repeated, or held at its edge. (A copy of a target at
//3, 5, 6, 7, 9 or 10 times the PSP's size isn't a power of two across: repeated by the remainder.)
uint inside(int c, uint size, bool held) {
  int last = int(size) - 1;
  if(held) return uint(clamp(c, 0, last));
  if((size & (size - 1u)) == 0u) return uint(c & last);
  return uint(c - int(size) * int(floor(float(c) / float(size))));
}

//An 8888 channel narrowed to bits of it and widened again by repeating its top bits, as a 16-bit frame buffer
//keeps it (pixel.cpp's narrowPixel() and widenPixel())
uint narrowed(uint value, uint bits) {
  uint kept = value >> (8u - bits);
  return kept << (8u - bits) | kept >> (2u * bits - 8u);
}

//A texel; one of a texture taken from a 16-bit frame buffer (TEXELS) as its format keeps it, as the GE reads it.
//(Held inside the picture on the GPU, which may have only the rows and columns the GE's pixels reach: above 1x a
//pixel between two of the GE's may sample a little past them.)
uvec4 fetch(uint u, uint v) {
  ivec2 at = min(ivec2(u, v), textureSize(texels, 0) - 1);
  uvec4 t = uvec4(texelFetch(texels, at, 0) * 255.0 + 0.5);
  if(TEXELS == 0u) t = uvec4(narrowed(t.r, 5u), narrowed(t.g, 6u), narrowed(t.b, 5u), 255u);
  if(TEXELS == 1u) t = uvec4(narrowed(t.r, 5u), narrowed(t.g, 5u), narrowed(t.b, 5u), t.a >= 128u ? 255u : 0u);
  if(TEXELS == 2u) t = uvec4(narrowed(t.r, 4u), narrowed(t.g, 4u), narrowed(t.b, 4u), narrowed(t.a, 4u));
  return t;
}

//(at a copy's scale, the coordinates and the texture's size are the copy's texels: each of the PSP's scale x scale)
uvec4 sampleTexture(vec2 at, bool linear) {
  uint width = push.textureSize & 0xffffu, height = push.textureSize >> 16;
  if(push.textureScale > 1u) {
    at *= float(push.textureScale), width *= push.textureScale, height *= push.textureScale;
  }
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

//pixel.cpp's narrowPixel(): an 8888 color in the frame buffer's format (QUANTIZE: 0-2, 16-bit; 4, 8888, as it is)
uint packed(uvec4 c) {
  if(QUANTIZE == 0u) return c.r >> 3 | c.g >> 2 << 5 | c.b >> 3 << 11;
  if(QUANTIZE == 1u) return c.r >> 3 | c.g >> 3 << 5 | c.b >> 3 << 10 | c.a >> 7 << 15;
  if(QUANTIZE == 2u) return c.r >> 4 | c.g >> 4 << 4 | c.b >> 4 << 8 | c.a >> 4 << 12;
  return c.r | c.g << 8 | c.b << 16 | c.a << 24;
}

//and back, pixel.cpp's widenPixel(): each field widened by repeating its top bits (a 5650's alpha 0)
uint widened(uint value, uint bits) { return value << (8u - bits) | value >> (2u * bits - 8u); }
uvec4 unpacked(uint p) {
  if(QUANTIZE == 0u) return uvec4(widened(p & 31u, 5u), widened(p >> 5 & 63u, 6u), widened(p >> 11 & 31u, 5u), 0u);
  if(QUANTIZE == 1u) {
    uint alpha = p >> 15 != 0u ? 255u : 0u;
    return uvec4(widened(p & 31u, 5u), widened(p >> 5 & 31u, 5u), widened(p >> 10 & 31u, 5u), alpha);
  }
  if(QUANTIZE == 2u) {
    return uvec4(widened(p & 15u, 4u), widened(p >> 4 & 15u, 4u), widened(p >> 8 & 15u, 4u), widened(p >> 12, 4u));
  }
  return uvec4(p & 255u, p >> 8 & 255u, p >> 16 & 255u, p >> 24);
}

//The colors a write leaves in the frame buffer, as memory would keep them: value in its format, and where the
//write mask says (READS), the bits of old (the frame buffer's pixel) kept — RGB and the stencil/alpha bits too
uvec4 written(uvec4 value, uvec4 old) {
  uint keep = READS != 0u ? push.writeMask : 0u;
  return unpacked((packed(value) & ~keep) | (packed(old) & keep));
}

//DITHER0-3's value for the pixel (the matrix over the PSP's pixels, each resolution x resolution of the GPU's)
int dithered() {
  uvec2 at = uvec2(gl_FragCoord.xy / float(push.resolution)) & 3u;
  uint nibble = push.dither[at.y >> 1] >> ((at.y & 1u) * 16u + at.x * 4u) & 15u;
  return nibble < 8u ? int(nibble) : int(nibble) - 16;
}

//pixel.cpp's blending factors (BLEND_MODE's 0-15): by other's color (the frame buffer's, for the source; the
//pixel's, for the destination), the source's or the destination's alpha, or fixed
ivec3 factor(uint which, ivec3 other, int sourceAlpha, int destinationAlpha, uint fixedFactor) {
  if(which == 0u) return other;
  if(which == 1u) return 255 - other;
  if(which == 2u) return ivec3(sourceAlpha);
  if(which == 3u) return ivec3(255 - sourceAlpha);
  if(which == 4u) return ivec3(destinationAlpha);
  if(which == 5u) return ivec3(255 - destinationAlpha);
  if(which == 6u) return ivec3(2 * sourceAlpha);
  if(which == 7u) return ivec3(255 - min(2 * sourceAlpha, 255));
  if(which == 8u) return ivec3(2 * destinationAlpha);
  if(which == 9u) return ivec3(255 - min(2 * destinationAlpha, 255));
  return ivec3(channel(fixedFactor, 0u), channel(fixedFactor, 1u), channel(fixedFactor, 2u));
}

//pixel.cpp's logic operations (LOGIC_OP), on the colors' 24 bits
uvec3 logic(uvec3 s, uvec3 d) {
  uvec3 v = s;
  if(LOGIC == 0u) v = uvec3(0u);
  if(LOGIC == 1u) v = s & d;
  if(LOGIC == 2u) v = s & ~d;
  if(LOGIC == 4u) v = ~s & d;
  if(LOGIC == 5u) v = d;
  if(LOGIC == 6u) v = s ^ d;
  if(LOGIC == 7u) v = s | d;
  if(LOGIC == 8u) v = ~(s | d);
  if(LOGIC == 9u) v = ~(s ^ d);
  if(LOGIC == 10u) v = ~d;
  if(LOGIC == 11u) v = s | ~d;
  if(LOGIC == 12u) v = ~s;
  if(LOGIC == 13u) v = ~s | d;
  if(LOGIC == 14u) v = ~(s & d);
  if(LOGIC == 15u) v = uvec3(255u);
  return v & 255u;
}

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
  //(fast mode's 3D, vertexFlags' bit 4: the filter for enlarging or shrinking as the texels a pixel covers say,
  //as the GE chooses it once a triangle (draw.cpp's chooseFilter()): from the texture coordinates straight across
  //the screen, whose steps are the triangle's own texels to its pixels; before anything's discarded)
  bool chosen = false;
  if(TEXTURED != 0u && CLEAR == 0u && (vertexFlags & 16u) != 0u) {
    vec2 across = dFdx(vertexTexels), down = dFdy(vertexTexels);
    float texels = abs(across.x * down.y - across.y * down.x) * float(push.resolution * push.resolution);
    chosen = texels <= 1.0 ? (push.filtering >> 8 & 1u) != 0u : (push.filtering & 1u) != 0u;
  }
  float depth = clamp(vertexDepth, 0.0, 65535.0);
  if(DEPTH_RANGE != 0u) {
    uint z = uint(depth);
    if(z < (push.depthRange & 0xffffu) || z > push.depthRange >> 16) discard;
  }
  //(the GE blends a color's channels as floats and drops their fractions; the interpolation's own error is kept
  //from tipping a whole number below itself)
  uvec4 color = uvec4(clamp(floor((FLAT != 0u ? vertexFlatColor : vertexColor) + 1.0 / 512.0), 0.0, 255.0));
  //(the frame buffer's pixel as memory keeps it, where it's read)
  uvec4 old = READS != 0u ? unpacked(packed(uvec4(subpassLoad(frameBuffer) * 255.0 + 0.5))) : uvec4(0u);
  if(CLEAR != 0u) {
    uvec4 cleared = QUANTIZE != 4u || READS != 0u ? written(color, old) : color;
    outColor = vec4(cleared) / 255.0;
    outFactor = vec4(0.0);
    return;
  }
  if(TEXTURED != 0u) {
    vec2 at = vertexCoordinates.xy / vertexCoordinates.z;
    if((vertexFlags & 2u) != 0u) {
      //a 2D sprite's, stepped from its edge as the GE steps them (raster.cpp's spriteRows()): the pixel's middle
      //in sixteenths from the start (exact), times the step, plus the first, rounded once as the GE's doubles are.
      //(At a higher resolution, the middle of the GPU's pixel, in the PSP's sixteenths: between the GE's steps.)
      vec2 middle = gl_FragCoord.xy * 16.0 / float(push.resolution);
      precise float column = fma((middle.x - float(vertexStart.x)) / 16.0, vertexStepping.y, vertexStepping.x);
      precise float row = fma((middle.y - float(vertexStart.y)) / 16.0, vertexStepping.w, vertexStepping.z);
      at = (vertexFlags & 4u) != 0u ? vec2(row, column) : vec2(column, row);
    }
    //(above 1x, a 2D triangle's texels where its pixels' middles reach at 1x: gpu.cpp's triangle())
    if((vertexFlags & 8u) != 0u) at = clamp(at, vertexStepping.xz, vertexStepping.yw);
    color = textureFunction(color, sampleTexture(at, (vertexFlags & 1u) != 0u || chosen));
  }
  uvec4 shine = uvec4(clamp(floor((FLAT != 0u ? vertexFlatSpecular : vertexSpecular) + 1.0 / 512.0), 0.0, 255.0));
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
  if(READS != 0u) {  //(pixel.cpp's, from blending to the write)
    ivec3 result = ivec3(color.rgb);
    if(BLENDING < 8u) {
      ivec3 s = ivec3(color.rgb), d = ivec3(old.rgb);
      ivec3 f = factor(SOURCE, d, int(color.a), int(old.a), push.fixedA);
      ivec3 g = factor(DESTINATION, s, int(color.a), int(old.a), push.fixedB);
      ivec3 sourceTerm = (s * 2 + 1) * (f * 2 + 1) >> 10, destinationTerm = (d * 2 + 1) * (g * 2 + 1) >> 10;
      if(BLENDING == 0u) result = sourceTerm + destinationTerm;
      if(BLENDING == 1u) result = sourceTerm - destinationTerm;
      if(BLENDING == 2u) result = destinationTerm - sourceTerm;
      if(BLENDING == 3u) result = min(s, d);
      if(BLENDING == 4u) result = max(s, d);
      if(BLENDING == 5u) result = abs(s - d);
    }
    if(DITHER != 0u) result += dithered();
    uvec3 kept = uvec3(clamp(result, 0, 255));
    if(LOGIC != 3u) kept = logic(kept, old.rgb);
    //(stencil/alpha: Replace/Clear known here, else the color's alpha is left — write mask applied in packing)
    uint stencil = ALPHA_OUT == 1u ? push.stencil : old.a;
    outColor = vec4(written(uvec4(kept, stencil), old)) / 255.0;
    outFactor = vec4(0.0);
    return;
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
    color.rgb = uvec3(clamp(ivec3(color.rgb) + dithered(), 0, 255));
    rgb = vec3(color.rgb) / 255.0;
  }
  if(QUANTIZE == 0u) rgb = vec3(narrowed(color.r, 5u), narrowed(color.g, 6u), narrowed(color.b, 5u)) / 255.0;
  if(QUANTIZE == 1u) rgb = vec3(narrowed(color.r, 5u), narrowed(color.g, 5u), narrowed(color.b, 5u)) / 255.0;
  if(QUANTIZE == 2u) rgb = vec3(narrowed(color.r, 4u), narrowed(color.g, 4u), narrowed(color.b, 4u)) / 255.0;
  if(LOGIC == 0u) rgb = vec3(0.0);
  if(LOGIC == 12u) rgb = 1.0 - rgb;
  if(LOGIC == 15u) rgb = vec3(1.0);
  float stencil = ALPHA_OUT == 1u ? float(push.stencil) / 255.0 : alpha;
  if(TERM != 0u) {
    ivec3 s = ivec3(rgb * 255.0 + 0.5), f = ivec3(255);
    int a = int(color.a);
    if(SOURCE == 1u) f = ivec3(a);
    if(SOURCE == 2u) f = ivec3(255 - a);
    if(SOURCE == 3u) f = ivec3(2 * a);
    if(SOURCE == 4u) f = ivec3(255 - min(2 * a, 255));
    if(SOURCE == 5u) f = ivec3(uvec3(channel(push.fixedA, 0u), channel(push.fixedA, 1u), channel(push.fixedA, 2u)));
    vec3 term = vec3((s * 2 + 1) * (f * 2 + 1) >> 10);
    outColor = vec4((term + (TERM == 1u ? -0.25 : 0.25)) / 255.0, stencil);
    return;
  }
  outColor = vec4(rgb * weight, stencil);
}
