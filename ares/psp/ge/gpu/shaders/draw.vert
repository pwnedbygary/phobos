//The hardware renderer's vertex shader (docs/psp-gpu-renderers.md, "Shaders"). The GE has done the transform,
//lighting and clipping on the CPU already (as for the software renderer), so a vertex arrives on the screen: its
//position in the frame buffer's pixels (whole sixteenths of one, as the GE holds positions), its depth (0-65535)
//and its clip w (1 in 2D), with its colors, texture coordinates and fog. All that's left is to put the position
//where Vulkan's clip space has it, so the GPU's rasterizer covers the pixels the GE would: the pixel's middle is
//inside (Vulkan samples there, as the GE does at eight sixteenths), and the edges' ties go to the top and left
//(Vulkan's usual rule, the GE's too).
//
//Multiplying x, y and the depth by w gives the rasterizer w back, so that what's interpolated "smooth" (the
//texture coordinates and their divisor) follows the perspective as the GE's u/w and 1/w do, while the colors, fog
//and depth, which the GE blends straight across the screen, are interpolated "noperspective".

layout(push_constant) uniform Push {
  vec2 scale;  //2 / the target's width and height: pixels to Vulkan's -1 to 1
} push;

layout(location = 0) in vec4 position;  //x, y in pixels; the depth; w
layout(location = 1) in vec3 coordinates;  //u, v in texels, and q (texture projection's divisor; 1 otherwise)
layout(location = 2) in vec4 color;     //8888, 0-1 (UNORM)
layout(location = 3) in vec4 specular;  //lighting's shine kept apart, added after texturing
layout(location = 4) in float fog;      //how much of the color stays: 0-1
layout(location = 5) in uint flags;     //bit 0: filtered (the primitive's TEXTURE_FILTER choice); 1 stepped; 2 turned;
                                        //3 held
layout(location = 6) in vec4 stepping;  //a 2D sprite's coordinates: across x's first and step, down y's (gpu.hpp);
                                        //held, a 2D triangle's u from x to y, v from z to w
layout(location = 7) in ivec2 start;    //and the sixteenths they start at

layout(location = 0) noperspective out vec4 vertexColor;
layout(location = 1) noperspective out vec4 vertexSpecular;
layout(location = 2) smooth out vec3 vertexCoordinates;
layout(location = 3) noperspective out float vertexFog;
layout(location = 4) noperspective out float vertexDepth;
layout(location = 5) flat out uint vertexFlags;
layout(location = 6) flat out vec4 vertexStepping;
layout(location = 7) flat out ivec2 vertexStart;

void main() {
  float w = position.w;
  gl_Position = vec4((position.xy * push.scale - 1.0) * w, position.z / 65535.0 * w, w);
  vertexColor = color * 255.0;
  vertexSpecular = specular * 255.0;
  vertexCoordinates = coordinates;
  vertexFog = fog;
  vertexDepth = position.z;
  vertexFlags = flags;
  vertexStepping = stepping;
  vertexStart = start;
}
