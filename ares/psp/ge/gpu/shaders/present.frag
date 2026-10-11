//The picture on the screen (docs/psp-gpu-renderers.md, "Presenting"): the frame buffer the PSP shows, straight from
//its target on the GPU, or memory's picture of it, drawn over the whole window. Its colors are first as memory would
//keep them (FORMAT: narrowed to a 16-bit frame buffer's bits and widened again, as GPU::picture() and the screen
//have them), then scaled "sharp bilinear": each of the picture's pixels a block of the window's, blended with the
//next only over the window pixel or so between them, so its edges are sharp but even at any size (a whole multiple
//blends nothing; shrunk, it's plain bilinear).

layout(constant_id = 0) const uint FORMAT = 3u;  //the frame buffer's (0-2: 16-bit), 3 as they are

layout(push_constant) uniform Push {
  vec2 source;  //the picture's size, in its own pixels
  vec2 shown;   //the window's
} push;

layout(set = 0, binding = 0) uniform sampler2D picture;  //8888, red in the low byte

layout(location = 0) out vec4 outColor;

uint narrowed(uint value, uint bits) {
  uint kept = value >> (8u - bits);
  return kept << (8u - bits) | kept >> (2u * bits - 8u);
}

vec3 pixel(ivec2 at) {
  at = clamp(at, ivec2(0), ivec2(push.source) - 1);
  uvec3 c = uvec3(texelFetch(picture, at, 0).rgb * 255.0 + 0.5);
  if(FORMAT == 0u) c = uvec3(narrowed(c.r, 5u), narrowed(c.g, 6u), narrowed(c.b, 5u));
  if(FORMAT == 1u) c = uvec3(narrowed(c.r, 5u), narrowed(c.g, 5u), narrowed(c.b, 5u));
  if(FORMAT == 2u) c = uvec3(narrowed(c.r, 4u), narrowed(c.g, 4u), narrowed(c.b, 4u));
  return vec3(c) / 255.0;
}

void main() {
  vec2 at = gl_FragCoord.xy * push.source / push.shown;  //(in the picture's pixels)
  vec2 enlarged = max(floor(push.shown / push.source), vec2(1.0));
  vec2 whole = floor(at), apart = at - whole - 0.5;
  vec2 alone = 0.5 - 0.5 / enlarged;  //(how far from a pixel's middle it's that pixel alone)
  vec2 taken = whole + (apart - clamp(apart, -alone, alone)) * enlarged;  //(a pixel's middle: whole, here)
  ivec2 first = ivec2(floor(taken));
  vec2 weight = taken - floor(taken);
  vec3 top = mix(pixel(first), pixel(first + ivec2(1, 0)), weight.x);
  vec3 bottom = mix(pixel(first + ivec2(0, 1)), pixel(first + ivec2(1, 1)), weight.x);
  outColor = vec4(mix(top, bottom, weight.y), 1.0);
}
