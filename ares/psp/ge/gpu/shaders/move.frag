//A frame buffer's pixels moved from one target into another whose rows are the same bytes (docs/psp-gpu-renderers.md,
//"Owned until needed"): what putting them back in memory and filling the other from memory would leave there, done
//on the GPU. The source's pixels, one of each of the PSP's (the one a read back takes: at the middle of its scale x
//scale), come as two pictures of the rectangle at the PSP's size: the colors, and the stencils, a row of the target's
//at a time (scale x the width, of which the pixel's is at its middle). Each is narrowed to its frame buffer's format
//as memory keeps it (the stencil its alpha), the bytes taken as the other's format, and widened as a fill widens them
//(gpu.cpp's narrowTarget() and widenTarget()).
//
//The colors are written as they are; the stencil, which a shader can't write, a bit at a time as copy.frag does.

layout(constant_id = 0) const uint MODE = 0u;  //0 the colors, 1 a bit of the stencil

layout(push_constant) uniform Push {
  ivec2 origin;  //where the rectangle starts in the target, in its own pixels
  uint scale;    //the targets' resolution
  uint bit;      //MODE 1: the stencil's bit
  uint formats;  //the source's GE format, and in bits 8-15 the target's
} push;

layout(set = 0, binding = 0) uniform sampler2D colors;
layout(set = 0, binding = 1) uniform usampler2D stencils;

layout(location = 0) out vec4 outColor;

//The source's pixel (the rectangle's column) as memory keeps it
uint narrowed(int column, int row) {
  uvec4 c = uvec4(texelFetch(colors, ivec2(column, row), 0) * 255.0 + 0.5);
  uint s = texelFetch(stencils, ivec2(column * int(push.scale) + int(push.scale) / 2, row), 0).r;
  uint format = push.formats & 255u;
  if(format == 0u) return c.r >> 3 | c.g >> 2 << 5 | c.b >> 3 << 11;
  if(format == 1u) return c.r >> 3 | c.g >> 3 << 5 | c.b >> 3 << 10 | s >> 7 << 15;
  if(format == 2u) return c.r >> 4 | c.g >> 4 << 4 | c.b >> 4 << 8 | s >> 4 << 12;
  return c.r | c.g << 8 | c.b << 16 | s << 24;
}

uint widened(uint value, uint bits) { return value << (8u - bits) | value >> (2u * bits - 8u); }

void main() {
  ivec2 at = ivec2(gl_FragCoord.xy - vec2(push.origin)) / int(push.scale);
  uint from = push.formats & 255u, to = push.formats >> 8;
  uint p;
  if((from == 3u) == (to == 3u)) p = narrowed(at.x, at.y);                          //(a pixel for a pixel)
  else if(to == 3u) p = narrowed(at.x * 2, at.y) | narrowed(at.x * 2 + 1, at.y) << 16;  //(two for one)
  else p = narrowed(at.x / 2, at.y) >> (uint(at.x) & 1u) * 16u & 0xffffu;           //(half of one)
  uvec4 c;
  if(to == 0u) c = uvec4(widened(p & 31u, 5u), widened(p >> 5 & 63u, 6u), widened(p >> 11 & 31u, 5u), 0u);
  else if(to == 1u) c = uvec4(widened(p & 31u, 5u), widened(p >> 5 & 31u, 5u), widened(p >> 10 & 31u, 5u),
                              p >> 15 != 0u ? 255u : 0u);
  else if(to == 2u) c = uvec4(widened(p & 15u, 4u), widened(p >> 4 & 15u, 4u), widened(p >> 8 & 15u, 4u),
                              widened(p >> 12, 4u));
  else c = uvec4(p & 255u, p >> 8 & 255u, p >> 16 & 255u, p >> 24);
  outColor = vec4(c) / 255.0;
  if(MODE == 1u && (c.a & push.bit) == 0u) discard;
}
