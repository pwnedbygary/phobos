//Memory's pixels put into a target drawn at a higher resolution (docs/psp-gpu-renderers.md, "Upscaling"): each of
//the PSP's pixels, uploaded as it is into a picture of the PSP's size, becomes scale x scale of the target's, its
//color, its depth or its stencil, exactly as it was (each read back from one of them: the same bytes again). At the
//PSP's own size the backend copies them in instead, as Vulkan's copies can't enlarge.
//
//The color is written as it is; the depth (two bytes of the picture, the low one first) through gl_FragDepth; the
//stencil, which a shader can't write, a bit at a time: the target's stencil cleared, then for each bit a draw that
//replaces it with the bit (the stencil's write mask that bit alone) where the pixel's stencil (its alpha) has it.

layout(constant_id = 0) const uint MODE = 0u;  //0 the colors, 1 the depth, 2 a bit of the stencil

layout(push_constant) uniform Push {
  ivec2 origin;  //where the rectangle starts in the target, in its own pixels
  uint scale;    //the target's resolution
  uint bit;      //MODE 2: the stencil's bit
} push;

layout(set = 0, binding = 0) uniform sampler2D picture;  //8888: the colors (the stencil the alpha), or the depths

layout(location = 0) out vec4 outColor;

void main() {
  ivec2 at = ivec2(gl_FragCoord.xy - vec2(push.origin)) / int(push.scale);
  vec4 texel = texelFetch(picture, at, 0);
  outColor = texel;
  if(MODE == 0u) return;
  uvec4 bytes = uvec4(texel * 255.0 + 0.5);
  if(MODE == 1u) {
    gl_FragDepth = float(bytes.r | bytes.g << 8) / 65535.0;
  } else {
    gl_FragDepth = gl_FragCoord.z;
    if((bytes.a & push.bit) == 0u) discard;
  }
}
