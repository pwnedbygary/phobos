//psp-measure's GE and controller tests: what a real PSP's GE draws, and how its controller driver times its reads, in
//the cases where Phobos's PSP core follows PPSSPP's software renderer or uOFW's reading of the firmware rather than
//measurements of its own (see docs/psp-core.md, parts 8 to 12): blending's and the texture functions' rounding, the
//texture filter's weights, which pixels sprites and triangles cover, the sprite corners' quarter turn, dithering, the
//stencil's steps, and whether the controller's reads wait; in 3D, perspective-correct texels, the depths and fog
//written, the GE's rounding onto the screen, the cut at the near plane, which depths stop a primitive, and culling;
//and lighting: diffuse and the shine across the angles, a spotlight's cone, a point light's fading, environment
//mapping. Those are round 2's (the first the GE had). Round 3 takes what they left open: lighting with normals
//whose cosines are exact (plain diffuse, powered diffuse and the shine, with material, light and ambient levels),
//color and fog stepped across primitives, 3D edges near the pixel middle, a receding wall's texels, round 2's 3D
//sprite taken apart, curved surfaces, and the depth buffer's layout through VRAM's four copies. Round 4 records what
//Phobos's core drew by rules of its own in part 29: which pixels lines light (ends at every sixteenth, short lines,
//strips' joints, anti-aliasing), colors, depth and texels along them, lines in 3D and cut at the near plane; which
//bounding boxes the GE takes to be in sight; compressed (DXT) textures' colors, alphas and block order; and, in part
//33, curved surfaces' vertices: where they fall, their colors, depths and texture coordinates, the texture
//coordinates and normals the GE makes up, culling, and how many. Round 5 takes what docs/psp-core.md's part 48 left
//open: the steps of colors and depths on random triangles, the near plane's cut each way, lighting's share, and
//perspective texels to the texel in thousands. Each round is picked from the menu (main.c).
//
//Each test draws into VRAM (away from the text on the screen), reads the pixels back as they are and writes them to
//results/ge/<test>.bin: little-endian 32-bit words, one per pixel, row by row (a 16-bit frame buffer's pixels in the
//low half). manifest.txt (round 2), manifest3.txt, manifest4.txt and manifest5.txt (rounds 3-5) say what each test
//drew. A test whose file is there is skipped, and one that stops the PSP is given up on, as results.c has it. The
//program computes nothing: the host runs the same program in Phobos's core (tests/psp/measure.cpp) and compares the
//files.

#include "measure.h"
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <psputils.h>
#include <stdio.h>
#include <string.h>

//VRAM: the text on the screen at its start (512x272, 32-bit); the tests draw into Target (512 pixels to a row, 256
//rows) and keep depth at Depth.
enum { Target = 0x100000, Depth = 0x180000, Stride = 512 };
#define VRAM ((volatile unsigned int*)0x44000000)  //VRAM, uncached: the CPU sees what the GE wrote
#define VRAM16 ((volatile unsigned short*)0x44000000)

static unsigned int __attribute__((aligned(16))) list[262144];
static unsigned int __attribute__((aligned(16))) texture[256 * 256];
static unsigned short __attribute__((aligned(16))) texture16[256 * 256];
static unsigned int pixels[256 * 256];

//The vertices the tests draw with, in through mode (positions are pixels): 16-bit texture coordinates and
//position, or floats where a test needs fractions of a pixel.
typedef struct { unsigned short u, v; unsigned int color; short x, y, z, pad; } Vertex;
typedef struct { float u, v; unsigned int color; float x, y, z; } FloatVertex;
enum {
  VertexType = GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
  FloatVertexType = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
};

static int failed;

//---- writing results

//The test that's running: every test starts with beginTest, which opens its file (results.c's begin), and ends by
//saving what it drew, which writes the file and renames it to .bin.
static Output current;

//Whether to run the test called name: not when it's done, or was given up on. A file it can't open counts as failing.
static int beginTest(const char* name) {
  int started = begin(&current, name);
  if(started < 0) failed = 1;
  return started > 0;
}

static void save(const char* name, const void* data, int bytes) {
  if(!writeOut(&current, name, data, bytes) || !finish(&current, name)) failed = 1;
}

//The first width x height pixels of the target, as words.
static void saveTarget(const char* name, int width, int height, int sixteenBits) {
  for(int y = 0; y < height; y++) {
    for(int x = 0; x < width; x++) {
      pixels[y * width + x] = sixteenBits ? VRAM16[Target / 2 + y * Stride + x] : VRAM[Target / 4 + y * Stride + x];
    }
  }
  save(name, pixels, width * height * 4);
}

//---- drawing

//Fills the target's first 256 rows: fill(x, y) for each pixel, 32-bit or 16-bit.
static void fillTarget(unsigned int (*fill)(int x, int y), int sixteenBits) {
  for(int y = 0; y < 256; y++) {
    for(int x = 0; x < 256; x++) {
      if(sixteenBits) VRAM16[Target / 2 + y * Stride + x] = fill(x, y);
      else VRAM[Target / 4 + y * Stride + x] = fill(x, y);
    }
  }
}
static unsigned int zero(int x, int y) { (void)x; (void)y; return 0; }

//The 256x256 8888 texture: texel(x, y) for each.
static void fillTexture(unsigned int (*texel)(int x, int y)) {
  for(int y = 0; y < 256; y++) {
    for(int x = 0; x < 256; x++) texture[y * 256 + x] = texel(x, y);
  }
  sceKernelDcacheWritebackAll();
}

//A list drawing into the target in the frame buffer format psm, with everything that could change a pixel turned
//off but the scissor (the target's 512x256).
static void start(int psm) {
  sceGuStart(GU_DIRECT, list);
  sceGuDrawBuffer(psm, (void*)Target, Stride);
  sceGuDepthBuffer((void*)Depth, Stride);
  sceGuOffset(2048 - 240, 2048 - 136);
  sceGuViewport(2048, 2048, 480, 272);
  sceGuScissor(0, 0, 512, 256);
  sceGuEnable(GU_SCISSOR_TEST);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuDisable(GU_ALPHA_TEST);
  sceGuDisable(GU_STENCIL_TEST);
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_DITHER);
  sceGuDisable(GU_COLOR_TEST);
  sceGuDisable(GU_COLOR_LOGIC_OP);
  sceGuDisable(GU_FRAGMENT_2X);
  sceGuDisable(GU_TEXTURE_2D);
  sceGuDisable(GU_CULL_FACE);
  sceGuDisable(GU_LIGHTING);
  for(int light = 0; light < 4; light++) sceGuDisable(GU_LIGHT0 + light);
  sceGuTexMapMode(GU_TEXTURE_COORDS, 0, 0);
  sceGuPixelMask(0);
  sceGuShadeModel(GU_SMOOTH);
}

//Ends the list and waits for the GE to have drawn it.
static void finishList(void) {
  sceGuFinish();
  sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

//The texture, 256x256 8888, nearest, clamped, with function tfx and its alpha (tcc).
static void useTexture(int tfx, int tcc) {
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_8888, 0, 0, 0);
  sceGuTexImage(0, 256, 256, 256, texture);
  sceGuTexFunc(tfx, tcc);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}

//A sprite from (x0, y0) to (x1, y1), its texture coordinates from (u0, v0) to (u1, v1), in color.
static void sprite(int x0, int y0, int x1, int y1, int u0, int v0, int u1, int v1, unsigned int color) {
  Vertex* vertices = sceGuGetMemory(2 * sizeof(Vertex));
  vertices[0] = (Vertex){u0, v0, color, x0, y0, 0, 0};
  vertices[1] = (Vertex){u1, v1, color, x1, y1, 0, 0};
  sceGuDrawArray(GU_SPRITES, VertexType, 2, 0, vertices);
}

//The whole texture over the target's 256x256, texel for pixel.
static void textureSquare(void) { sprite(0, 0, 256, 256, 0, 0, 256, 256, 0xffffffff); }

//256 rows, row y in color(y), each the texture's first row texel for pixel.
static void textureRows(unsigned int (*color)(int y)) {
  Vertex* vertices = sceGuGetMemory(512 * sizeof(Vertex));
  for(int y = 0; y < 256; y++) {
    vertices[y * 2] = (Vertex){0, 0, color(y), 0, y, 0, 0};
    vertices[y * 2 + 1] = (Vertex){256, 1, color(y), 256, y + 1, 0, 0};
  }
  sceGuDrawArray(GU_SPRITES, VertexType, 512, 0, vertices);
}

//---- the texels and colors the tests use (manifest.txt repeats them)

static unsigned int spread(int x) { return x | (255 - x) << 8 | (x * 7 & 0xff) << 16; }  //three values from one x
static unsigned int colorX_alphaY(int x, int y) { return spread(x) | y << 24; }
static unsigned int black_alphaY(int x, int y) { (void)x; return y << 24; }
static unsigned int colorX(int x, int y) { (void)y; return spread(x); }
static unsigned int colorX_alphaX(int x, int y) { (void)y; return spread(x) | x << 24; }
static unsigned int white_alphaX(int x, int y) { (void)y; return 0x00ffffff | x << 24; }
static unsigned int grayY(int x, int y) { (void)x; return y * 0x010101; }
static unsigned int grayY_alphaY(int x, int y) { (void)x; return y * 0x01010101u; }
static unsigned int constant(int x, int y) { (void)x; (void)y; return 0x40c080; }
static unsigned int gray(int y) { return 0xff000000u | y * 0x010101; }
static unsigned int whiteAlpha(int y) { return 0x00ffffff | y << 24; }

//---- the tests

//Blending: a texture (color from x, alpha from y) over the target with each blend.
static void blend(const char* name, unsigned int (*texel)(int, int), unsigned int (*under)(int, int), int op, int src,
                  int dst, unsigned int fixA, unsigned int fixB) {
  if(!beginTest(name)) return;
  fillTexture(texel);
  fillTarget(under, 0);
  start(GU_PSM_8888);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(op, src, dst, fixA, fixB);
  textureSquare();
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Texture functions: 256 rows over the texture's first row, row y's color color(y).
static void texfunc(const char* name, unsigned int (*texel)(int, int), unsigned int (*color)(int), int tfx, int tcc,
                    int doubled, unsigned int environment, int blendAlpha) {
  if(!beginTest(name)) return;
  fillTexture(texel);
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  useTexture(tfx, tcc);
  sceGuTexEnvColor(environment);
  if(doubled) sceGuEnable(GU_FRAGMENT_2X);
  if(blendAlpha) {  //the result's alpha, seen through blending white by it
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0);
  }
  textureRows(color);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The filter: a 4x4 texture of distinct texels drawn over size x size pixels (bigger than 4: magnified, smaller:
//shrunk), the filter for that direction linear, the other nearest; repeating or clamping. Saves 64x64 pixels.
static void filter(const char* name, int size, int clamp) {
  if(!beginTest(name)) return;
  for(int n = 0; n < 16; n++) {
    texture[n] = ((n * 0x35 + 0x11) & 0xff) | ((n * 0x5b + 0x40) & 0xff) << 8 | (n * 16) << 16;
  }
  sceKernelDcacheWritebackAll();
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_8888, 0, 0, 0);
  sceGuTexImage(0, 4, 4, 4, texture);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  int magnify = size > 4;
  sceGuTexFilter(magnify ? GU_NEAREST : GU_LINEAR, magnify ? GU_LINEAR : GU_NEAREST);
  sceGuTexWrap(clamp ? GU_CLAMP : GU_REPEAT, clamp ? GU_CLAMP : GU_REPEAT);
  sprite(0, 0, size, size, 0, 0, 4, 4, 0xffffffff);
  finishList();
  saveTarget(name, 64, 64, 0);
}

//Coverage: 256 cells of 16x16 pixels; in cell (i, j) a white shape whose corners sit i and j sixteenths of a pixel
//past whole pixels.
static void coverage(const char* name, int triangles) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  int count = triangles ? 3 : 2;
  FloatVertex* vertices = sceGuGetMemory(256 * count * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    float i = (cell & 15) / 16.0f, j = (cell >> 4) / 16.0f;
    float x = (cell & 15) * 16, y = (cell >> 4) * 16;
    FloatVertex* v = vertices + cell * count;
    if(triangles) {
      v[0] = (FloatVertex){0, 0, 0xffffffff, x + 2 + i, y + 2 + j, 0};
      v[1] = (FloatVertex){0, 0, 0xffffffff, x + 13 + j, y + 4 + i, 0};
      v[2] = (FloatVertex){0, 0, 0xffffffff, x + 5 + i, y + 13 + i, 0};
    } else {
      v[0] = (FloatVertex){0, 0, 0xffffffff, x + 3 + i, y + 3 + j, 0};
      v[1] = (FloatVertex){0, 0, 0xffffffff, x + 11 + j, y + 9 + i, 0};
    }
  }
  sceGuDrawArray(triangles ? GU_TRIANGLES : GU_SPRITES, FloatVertexType, 256 * count, 0, vertices);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Shared edges: in cell (i, j) a 12x12 square of two triangles meeting on a diagonal moved i sixteenths, added up, so
//a pixel drawn twice shows 2.
static void sharedEdges(const char* name) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
  FloatVertex* vertices = sceGuGetMemory(256 * 6 * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    float i = (cell & 15) / 16.0f, j = (cell >> 4) / 16.0f;
    float x = (cell & 15) * 16 + 2 + j, y = (cell >> 4) * 16 + 2;
    FloatVertex* v = vertices + cell * 6;
    unsigned int one = 0xff010101;
    v[0] = (FloatVertex){0, 0, one, x, y, 0};
    v[1] = (FloatVertex){0, 0, one, x + 12, y, 0};
    v[2] = (FloatVertex){0, 0, one, x + i, y + 12, 0};
    v[3] = (FloatVertex){0, 0, one, x + 12, y, 0};
    v[4] = (FloatVertex){0, 0, one, x + 12, y + 12, 0};
    v[5] = (FloatVertex){0, 0, one, x + i, y + 12, 0};
  }
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType, 256 * 6, 0, vertices);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Sprite corners: a 4x4 texture drawn 1:1 with its two corners each way round: top-left to bottom-right, bottom-right
//to top-left, bottom-left to top-right, top-right to bottom-left (one per 8x8 cell along the top).
static void spriteCorners(const char* name) {
  if(!beginTest(name)) return;
  for(int n = 0; n < 16; n++) texture[n] = (n + 1) * 0x0f0b07;
  sceKernelDcacheWritebackAll();
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(GU_PSM_8888, 0, 0, 0);
  sceGuTexImage(0, 4, 4, 4, texture);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sprite(2, 2, 6, 6, 0, 0, 4, 4, 0xffffffff);
  sprite(14, 6, 10, 2, 0, 0, 4, 4, 0xffffffff);
  sprite(18, 6, 22, 2, 0, 0, 4, 4, 0xffffffff);
  sprite(30, 2, 26, 6, 0, 0, 4, 4, 0xffffffff);
  finishList();
  saveTarget(name, 32, 8, 0);
}

//The 16-bit texture formats: all 65536 texels (texel (x, y) is y * 256 + x) drawn 1:1, as replace gives their color;
//or, with alphaOnly, their alpha: added to the vertices' white (which stays white), then blended over black by it.
static void textureFormat(const char* name, int psm, int alphaOnly) {
  if(!beginTest(name)) return;
  for(int n = 0; n < 65536; n++) texture16[n] = n;
  sceKernelDcacheWritebackAll();
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(psm, 0, 0, 0);
  sceGuTexImage(0, 256, 256, 256, texture16);
  sceGuTexFunc(alphaOnly ? GU_TFX_ADD : GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  if(alphaOnly) {
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0);
  }
  textureSquare();
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The 16-bit frame buffer formats: the texture (color from x, alpha from y) drawn 1:1 with replace over zeros, not
//dithered: how 8-bit channels become 5, 6 or 4 bits, and what the alpha (stencil) bits get with the stencil test off.
static void narrow(const char* name, int psm) {
  if(!beginTest(name)) return;
  fillTexture(colorX_alphaY);
  fillTarget(zero, 1);
  start(psm);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGBA);
  textureSquare();
  finishList();
  saveTarget(name, 256, 256, 1);
}

//Colors across triangles: a square of two triangles whose corners are black (top left), red (top right), green
//(bottom left) and blue (bottom right).
static void gouraud(const char* name) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  Vertex* v = sceGuGetMemory(6 * sizeof(Vertex));
  v[0] = (Vertex){0, 0, 0xff000000, 0, 0, 0, 0};
  v[1] = (Vertex){0, 0, 0xff0000ff, 256, 0, 0, 0};
  v[2] = (Vertex){0, 0, 0xff00ff00, 0, 256, 0, 0};
  v[3] = (Vertex){0, 0, 0xff0000ff, 256, 0, 0, 0};
  v[4] = (Vertex){0, 0, 0xffff0000, 256, 256, 0, 0};
  v[5] = (Vertex){0, 0, 0xff00ff00, 0, 256, 0, 0};
  sceGuDrawArray(GU_TRIANGLES, VertexType, 6, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Which texel each pixel takes, nearest: the texture (texel (x, y) is x | y << 8 | 0x80 << 16) from texel (0, 0) to
//(texels, texels) over size x size pixels, as a sprite or as a square of two triangles.
static unsigned int texelXY(int x, int y) { return x | y << 8 | 0x80 << 16; }
static void texelMapping(const char* name, int triangles, int size, int texels) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  if(triangles) {
    Vertex* v = sceGuGetMemory(6 * sizeof(Vertex));
    v[0] = (Vertex){0, 0, 0xffffffff, 0, 0, 0, 0};
    v[1] = (Vertex){texels, 0, 0xffffffff, size, 0, 0, 0};
    v[2] = (Vertex){0, texels, 0xffffffff, 0, size, 0, 0};
    v[3] = (Vertex){texels, 0, 0xffffffff, size, 0, 0, 0};
    v[4] = (Vertex){texels, texels, 0xffffffff, size, size, 0, 0};
    v[5] = (Vertex){0, texels, 0xffffffff, 0, size, 0, 0};
    sceGuDrawArray(GU_TRIANGLES, VertexType, 6, 0, v);
  } else {
    sprite(0, 0, size, size, 0, 0, texels, texels, 0xffffffff);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Dithering (pspsdk's matrix, which sceGuStart sets): a gray ramp (row y gray y) drawn with dithering on, in 8888 and
//in 5650.
static void dither(const char* name, int psm) {
  if(!beginTest(name)) return;
  fillTexture(grayY_alphaY);
  fillTarget(zero, psm != GU_PSM_8888);
  start(psm);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuEnable(GU_DITHER);
  textureSquare();
  finishList();
  saveTarget(name, 256, 256, psm != GU_PSM_8888);
}

//The stencil: a frame buffer whose stencil (alpha) is x's top bits, drawn over with the stencil test always passing
//and an operation on passing, in a 16-bit format.
static unsigned int stencil4444(int x, int y) { (void)y; return (x >> 4) << 12 | 0x123; }
static unsigned int stencil5551(int x, int y) { (void)y; return (x >> 7) << 15 | 0x123; }
static void stencil(const char* name, int psm, int operation) {
  if(!beginTest(name)) return;
  fillTarget(psm == GU_PSM_4444 ? stencil4444 : stencil5551, 1);
  start(psm);
  sceGuEnable(GU_STENCIL_TEST);
  sceGuStencilFunc(GU_ALWAYS, 0x55, 0xff);
  sceGuStencilOp(GU_KEEP, GU_KEEP, operation);
  sprite(0, 0, 256, 256, 0, 0, 0, 0, 0xff336699);
  finishList();
  saveTarget(name, 256, 256, 1);
}

//---- 3D

enum { FloatVertexType3D = GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D };
static ScePspFMatrix4 identity = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
//A perspective lens (near 0.5, far 10): w = -z, so something twice as far away is drawn half the size.
static ScePspFMatrix4 lens = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, -1.10526316f, -1}, {0, 0, -1.05263158f, 0}};

//The target's pixel (x, y) as the position that lands on it in 3D with identity matrices (x and y from -1 to 1
//across the 256 pixels, y up).
static float ndcX(float x) { return (x - 128) / 128; }
static float ndcY(float y) { return (128 - y) / 128; }

static void fillDepth(unsigned short value) {
  for(int y = 0; y < 256; y++) {
    for(int x = 0; x < 256; x++) VRAM16[Depth / 2 + y * Stride + x] = value;
  }
}

static void saveDepth(const char* name) {
  for(int y = 0; y < 256; y++) {
    for(int x = 0; x < 256; x++) pixels[y * 256 + x] = VRAM16[Depth / 2 + y * Stride + x];
  }
  save(name, pixels, 256 * 256 * 4);
}

//A list drawing in 3D into the target (8888): world and view the identity, the projection as given; the viewport
//putting x and y from -1 to 1 onto the target's 256x256 pixels (y up) and pspsdk's usual depth range,
//sceGuDepthRange(65535, 0) (z / w from -1 to 1 becoming depth 65535 to 0); every pixel passing the depth test and
//writing its depth; DEPTH_CLIP_ENABLE (GU_CLIP_PLANES) as asked. start3D clears the target and its depths first.
static void begin3D(ScePspFMatrix4* projection, int depthClamp) {
  start(GU_PSM_8888);
  sceGuOffset(2048 - 128, 2048 - 128);
  sceGuViewport(2048, 2048, 256, 256);
  sceGuDepthRange(65535, 0);
  if(depthClamp) sceGuEnable(GU_CLIP_PLANES);
  else sceGuDisable(GU_CLIP_PLANES);
  sceGuSetMatrix(GU_PROJECTION, projection);
  sceGuSetMatrix(GU_VIEW, &identity);
  sceGuSetMatrix(GU_MODEL, &identity);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthFunc(GU_ALWAYS);
  sceGuDepthMask(GU_FALSE);
  sceGuDisable(GU_FOG);
}
static void start3D(ScePspFMatrix4* projection, int depthClamp) {
  fillTarget(zero, 0);
  fillDepth(0);
  begin3D(projection, depthClamp);
}

//A floor receding under the lens from z -1 to z -4, the texture (texel (x, y) is x | y << 8 | 0x80 << 16) across it
//from its near edge (v 0) to its far one (v 1). what 0: the texel each pixel takes, perspective and all; 1: the
//depths written (16-bit, in the low half); 2: in white with fog (near 1, far 4, blue): the fog across it.
static void floor3D(const char* name, int what) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&lens, 1);
  if(what == 0) useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  if(what == 2) {
    sceGuEnable(GU_FOG);
    sceGuFog(1, 4, 0xff0000);
  }
  FloatVertex* v = sceGuGetMemory(6 * sizeof(FloatVertex));
  v[0] = (FloatVertex){0, 0, 0xffffffff, -1, -1, -1};
  v[1] = (FloatVertex){1, 0, 0xffffffff, 1, -1, -1};
  v[2] = (FloatVertex){0, 1, 0xffffffff, -1, -1, -4};
  v[3] = (FloatVertex){1, 0, 0xffffffff, 1, -1, -1};
  v[4] = (FloatVertex){1, 1, 0xffffffff, 1, -1, -4};
  v[5] = (FloatVertex){0, 1, 0xffffffff, -1, -1, -4};
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, v);
  finishList();
  if(what == 1) saveDepth(name);
  else saveTarget(name, 256, 256, 0);
}

//A 3D sprite under the lens from a near corner (z -1, top left) to a far one (z -4, bottom right), textured as the
//floor and fogged as it (near 1, far 4, blue): how a sprite whose corners lie at different depths takes its texels
//and its fog.
static void sprite3D(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&lens, 1);
  useTexture(GU_TFX_MODULATE, GU_TCC_RGB);
  sceGuEnable(GU_FOG);
  sceGuFog(1, 4, 0xff0000);
  FloatVertex* v = sceGuGetMemory(2 * sizeof(FloatVertex));
  v[0] = (FloatVertex){0, 0, 0xffffffff, -0.9f, 0.9f, -1};
  v[1] = (FloatVertex){1, 1, 0xffffffff, 0.9f, -0.8f, -4};  //at w 4: x 0.225, y -0.2 on the screen
  sceGuDrawArray(GU_SPRITES, FloatVertexType3D, 2, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The GE's rounding onto the screen, through the matrices (identity) and the viewport: in cell (i, j), a square of two
//triangles whose left edge lies i 256ths of a pixel past the sample point of the cell's pixel (4, 4) (7/16 in), and
//whose top edge j 256ths below it. That pixel is drawn while both edges still round to the sample point.
static void rounding3D(const char* name) {
  if(!beginTest(name)) return;
  start3D(&identity, 1);
  FloatVertex* vertices = sceGuGetMemory(256 * 6 * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    float x = (cell & 15) * 16, y = (cell >> 4) * 16;
    float left = ndcX(x + 4 + 7 / 16.0f + (cell & 15) / 256.0f), top = ndcY(y + 4 + 7 / 16.0f + (cell >> 4) / 256.0f);
    float right = ndcX(x + 12), bottom = ndcY(y + 12);
    FloatVertex* v = vertices + cell * 6;
    v[0] = (FloatVertex){0, 0, 0xffffffff, left, top, 0};
    v[1] = (FloatVertex){0, 0, 0xffffffff, right, top, 0};
    v[2] = (FloatVertex){0, 0, 0xffffffff, left, bottom, 0};
    v[3] = (FloatVertex){0, 0, 0xffffffff, right, top, 0};
    v[4] = (FloatVertex){0, 0, 0xffffffff, right, bottom, 0};
    v[5] = (FloatVertex){0, 0, 0xffffffff, left, bottom, 0};
  }
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 256 * 6, 0, vertices);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The near plane: with identity matrices it's z = -1. A triangle with corners red and green at z 0 and blue at z -3
//reaches past it, so it's cut a third of the way to the blue corner: where, and the colors the cut leaves. With
//DEPTH_CLIP_ENABLE off, the blue corner's z / w of -3 should drop it instead.
static void clip3D(const char* name, int depthClamp) {
  if(!beginTest(name)) return;
  start3D(&identity, depthClamp);
  FloatVertex* v = sceGuGetMemory(3 * sizeof(FloatVertex));
  v[0] = (FloatVertex){0, 0, 0xff0000ff, ndcX(16), ndcY(16), 0};
  v[1] = (FloatVertex){0, 0, 0xff00ff00, ndcX(240), ndcY(16), 0};
  v[2] = (FloatVertex){0, 0, 0xffff0000, ndcX(128), ndcY(240), -3};
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 3, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Which depths stop a primitive being drawn: in row 0 with DEPTH_CLIP_ENABLE on, in row 1 off, one 16x16 cell per case
//(identity matrices, so z / w is z; depth = 32767 - 32768 z): a triangle with its corners at the z values below, a
//point (at the cell's middle) or a 3D sprite.
static const float ruleDepths[16][3] = {
  {0, 0, 0}, {1.5f, 0, 0}, {1.5f, 1.5f, 1.5f}, {-1.5f, 0, 0}, {-1.5f, -1.5f, -1.5f}, {1.00002f, 0, 0},
  {1.00004f, 0, 0}, {0.99f, 0.99f, 0.99f}, {1.5f}, {-1.5f}, {0}, {1.5f, 0}, {1.5f, 1.5f}, {1, 0, 0}, {1, 1, 1},
  {0.5f, 0.5f, 0.5f},
};
static void rules3D(const char* name) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  fillDepth(0);
  for(int row = 0; row < 2; row++) {
    begin3D(&identity, row == 0);
    for(int k = 0; k < 16; k++) {
      float x = k * 16, y = row * 16;
      const float* z = ruleDepths[k];
      FloatVertex* v = sceGuGetMemory(3 * sizeof(FloatVertex));
      if(k >= 8 && k <= 10) {
        v[0] = (FloatVertex){0, 0, 0xffffffff, ndcX(x + 8.5f), ndcY(y + 8.5f), z[0]};
        sceGuDrawArray(GU_POINTS, FloatVertexType3D, 1, 0, v);
      } else if(k == 11 || k == 12) {
        v[0] = (FloatVertex){0, 0, 0xffffffff, ndcX(x + 2), ndcY(y + 2), z[0]};
        v[1] = (FloatVertex){0, 0, 0xffffffff, ndcX(x + 14), ndcY(y + 14), z[1]};
        sceGuDrawArray(GU_SPRITES, FloatVertexType3D, 2, 0, v);
      } else {
        v[0] = (FloatVertex){0, 0, 0xffffffff, ndcX(x + 2), ndcY(y + 2), z[0]};
        v[1] = (FloatVertex){0, 0, 0xffffffff, ndcX(x + 14), ndcY(y + 2), z[1]};
        v[2] = (FloatVertex){0, 0, 0xffffffff, ndcX(x + 2), ndcY(y + 14), z[2]};
        sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 3, 0, v);
      }
    }
    finishList();
  }
  saveTarget(name, 256, 32, 0);
}

//Culling: row 0 with sceGuFrontFace(GU_CW), row 1 with GU_CCW, in 3D; rows 2 and 3 the same in through mode. In each
//row: a triangle running clockwise on the screen, one running counterclockwise, and a strip of two (the first
//clockwise).
static void cull3D(const char* name) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  for(int row = 0; row < 4; row++) {
    int through = row >= 2;
    if(through) start(GU_PSM_8888);
    else {
      start(GU_PSM_8888);
      sceGuOffset(2048 - 128, 2048 - 128);
      sceGuViewport(2048, 2048, 256, 256);
      sceGuDepthRange(65535, 0);
      sceGuSetMatrix(GU_PROJECTION, &identity);
      sceGuSetMatrix(GU_VIEW, &identity);
      sceGuSetMatrix(GU_MODEL, &identity);
    }
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(row & 1 ? GU_CCW : GU_CW);
    float y = row * 16;
    float corners[3][4][2] = {
      {{2, y + 2}, {14, y + 2}, {2, y + 14}},                    //clockwise on the screen (y down)
      {{18, y + 2}, {18, y + 14}, {30, y + 2}},                  //counterclockwise
      {{34, y + 2}, {46, y + 2}, {34, y + 14}, {46, y + 14}},    //a strip: clockwise, then its second the other way
    };
    for(int shape = 0; shape < 3; shape++) {
      int count = shape == 2 ? 4 : 3;
      FloatVertex* v = sceGuGetMemory(4 * sizeof(FloatVertex));
      for(int n = 0; n < count; n++) {
        float px = corners[shape][n][0], py = corners[shape][n][1];
        v[n] = (FloatVertex){0, 0, 0xffffffff, through ? px : ndcX(px), through ? py : ndcY(py), 0};
      }
      sceGuDrawArray(shape == 2 ? GU_TRIANGLE_STRIP : GU_TRIANGLES, through ? FloatVertexType : FloatVertexType3D,
                     count, 0, v);
    }
    finishList();
  }
  saveTarget(name, 64, 64, 0);
}

//---- lighting

typedef struct { unsigned int color; float nx, ny, nz, x, y, z; } LitVertex;
enum { LitVertexType = GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D };

//A list lighting in 3D (identity matrices): light 0 on (start has turned the others off), the material's own colors
//(not the vertex's), the material and the ambient light black, light 0's colors black and not fading, but for what
//each case sets.
static void beginLit(void) {
  start3D(&identity, 1);
  sceGuEnable(GU_LIGHTING);
  sceGuEnable(GU_LIGHT0);
  sceGuColorMaterial(0);
  sceGuModelColor(0, 0, 0, 0);
  sceGuAmbient(0);
  sceGuLightMode(0);
  sceGuLightAtt(0, 1, 0, 0);
  sceGuLightColor(0, GU_AMBIENT, 0);
  sceGuLightColor(0, GU_DIFFUSE, 0);
  sceGuLightColor(0, GU_SPECULAR, 0);
}

//Cells first to first + count - 1 of the target's 256 (16x16 pixels, row by row), each a 3D sprite at z 0 in color
//whose normal is normal(k, count), k counting from 0 in these cells (unscaled: the GE makes it one long). A sprite
//takes its second vertex's color, lit there, so each cell shows one lit color.
static void litCells(int first, int count, unsigned int color, void (*normal)(int k, int count, float* n)) {
  LitVertex* v = sceGuGetMemory(count * 2 * sizeof(LitVertex));
  for(int k = 0; k < count; k++) {
    int cell = first + k;
    float n[3], x = (cell & 15) * 16, y = (cell >> 4) * 16;
    normal(k, count, n);
    v[k * 2] = (LitVertex){color, n[0], n[1], n[2], ndcX(x), ndcY(y), 0};
    v[k * 2 + 1] = (LitVertex){color, n[0], n[1], n[2], ndcX(x + 16), ndcY(y + 16), 0};
  }
  sceGuDrawArray(GU_SPRITES, LitVertexType, count * 2, 0, v);
}
//Turning from facing +z (k 0) to edge on (the last k): (k, 0, count - 1 - k).
static void sweep(int k, int count, float* n) { n[0] = k, n[1] = 0, n[2] = count - 1 - k; }
//Straight up, +z.
static void up(int k, int count, float* n) { (void)k, (void)count; n[0] = 0, n[1] = 0, n[2] = 1; }
//Spread over the half facing +z: the cells' column i and row j leaning (i - 7.5, j less the middle row) against 8 up.
static void hemisphere(int k, int count, float* n) {
  n[0] = (k & 15) - 7.5f, n[1] = (k >> 4) - (count / 16 - 1) / 2.0f, n[2] = 8;
}

//Diffuse: a directional light toward +z, white, the normal turning away across each half: in the top half on a
//material diffuse of (255, 64, 192); in the bottom half on the vertex's own color, the same, standing for it
//(MATERIAL_COLOR), which should come out the same.
static void lightDiffuse(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  ScePspFVector3 toward = {0, 0, 1};
  sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &toward);
  sceGuLightColor(0, GU_DIFFUSE, 0xffffff);
  sceGuModelColor(0, 0, 0xc040ff, 0);
  litCells(0, 128, 0xffffffff, sweep);
  sceGuModelColor(0, 0, 0, 0);
  sceGuColorMaterial(GU_DIFFUSE);
  litCells(128, 128, 0xffc040ff, sweep);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The shine: the same light shining (white specular, no diffuse) on a white specular material, the viewer along +z,
//the normal turning away across each half: the coefficient 2 in the top half, 7 in the bottom (the GE's own quick
//power, and the coefficient's four bits of fraction).
static void lightSpecular(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  ScePspFVector3 toward = {0, 0, 1};
  sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE_AND_SPECULAR, &toward);
  sceGuLightColor(0, GU_SPECULAR, 0xffffff);
  sceGuModelColor(0, 0, 0, 0xffffff);
  sceGuSpecular(2);
  litCells(0, 128, 0xffffffff, sweep);
  sceGuSpecular(7);
  litCells(128, 128, 0xffffffff, sweep);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//A spotlight half a unit above the middle (with identity matrices the target spans -1 to 1), white diffuse on white,
//its cone a cosine of 0.8, exponent 4, the normals up: in the top half pointing along +z, in the bottom along -z
//(which way the GE takes a spotlight's direction: PPSSPP lights the cone only with +z, toward the light). The spot
//is over the middle, so each half shows half of its pool.
static void lightSpot(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  ScePspFVector3 at = {0, 0, 0.5f};
  sceGuLight(0, GU_SPOTLIGHT, GU_DIFFUSE, &at);
  sceGuLightColor(0, GU_DIFFUSE, 0xffffff);
  sceGuModelColor(0, 0, 0xffffff, 0);
  for(int half = 0; half < 2; half++) {
    ScePspFVector3 along = {0, 0, half ? -1.0f : 1.0f};
    sceGuLightSpot(0, &along, 4, 0.8f);
    litCells(half * 128, 128, 0xffffffff, up);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//A point light a quarter of a unit above the middle, white diffuse on white, fading as 1 / (0.5 + d + 2d²) (all three
//terms), the normals up: the fading and the cosine together, across the distances.
static void lightPoint(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  ScePspFVector3 at = {0, 0, 0.25f};
  sceGuLight(0, GU_POINTLIGHT, GU_DIFFUSE, &at);
  sceGuLightColor(0, GU_DIFFUSE, 0xffffff);
  sceGuLightAtt(0, 0.5f, 1, 2);
  sceGuModelColor(0, 0, 0xffffff, 0);
  litCells(0, 256, 0xffffffff, up);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Environment mapping (lighting off): the texture (texel (x, y) is x | y << 8 | 0x80 << 16), replace; light 0
//directional toward +x, light 1 toward +y; TEXTURE_SHADE_MAPPING 1, as pspsdk's sceGuTexMapMode(GU_ENVIRONMENT_MAP,
//0, 1) and its "celshading" sample send it (PPSSPP takes u from light 1 and v from light 0); the normals spread over
//the half facing +z, in each half. In the bottom half light 0 shines, so its coordinate comes from half way to the
//viewer.
static void lightEnvironment(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&identity, 1);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  ScePspFVector3 x = {1, 0, 0}, y = {0, 1, 0};
  sceGuLight(1, GU_DIRECTIONAL, GU_DIFFUSE, &y);
  sceGuTexMapMode(GU_ENVIRONMENT_MAP, 0, 1);
  sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &x);
  litCells(0, 128, 0xffffffff, hemisphere);
  sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE_AND_SPECULAR, &x);
  litCells(128, 128, 0xffffffff, hemisphere);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The controller's timing, in microseconds: 16 times each, how long a second sceCtrlReadLatch right after one takes;
//how long sceCtrlReadBufferPositive takes just after a vertical blank; how long a second sceCtrlReadBufferPositive
//right after one takes. (A wait is about a frame, 16683.)
static void controllerTiming(const char* name) {
  if(!beginTest(name)) return;
  SceCtrlLatch latch;
  SceCtrlData pad;
  sceCtrlSetSamplingCycle(0);
  unsigned int* out = pixels;
  for(int n = 0; n < 16; n++) {
    sceDisplayWaitVblankStart();
    sceCtrlReadLatch(&latch);
    unsigned int before = sceKernelGetSystemTimeLow();
    sceCtrlReadLatch(&latch);
    *out++ = sceKernelGetSystemTimeLow() - before;
  }
  for(int n = 0; n < 16; n++) {
    sceDisplayWaitVblankStart();
    unsigned int before = sceKernelGetSystemTimeLow();
    sceCtrlReadBufferPositive(&pad, 1);
    *out++ = sceKernelGetSystemTimeLow() - before;
  }
  for(int n = 0; n < 16; n++) {
    sceCtrlReadBufferPositive(&pad, 1);
    unsigned int before = sceKernelGetSystemTimeLow();
    sceCtrlReadBufferPositive(&pad, 1);
    *out++ = sceKernelGetSystemTimeLow() - before;
  }
  save(name, pixels, 48 * 4);
}

static void writeManifest(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 2 (tools/psp-measure/ge.c in Phobos says what each test draws): each .bin but\n"
    "controller-timing.bin is the target's pixels after one test, a little-endian 32-bit word each, row by row\n"
    "(256x256 but filter-* 64x64, sprite-corners 32x8, 3d-rules 256x32 and 3d-cull 64x64; the stencil-*, narrow-*\n"
    "and dither-5650 frame buffers are 16-bit, in the low half, and 3d-floor-depth is the 16-bit depth buffer).\n"
    "spread(x) = x | (255 - x) << 8 | (x * 7 & 0xff) << 16. Textures are 8888 (texture-*: 16-bit, texel y * 256 + x),\n"
    "nearest, clamped; blends draw the texture with replace and its alpha. 3d-*: drawn through the matrices.\n"
    "light-*: 256 cells of 16x16 pixels, each a lit 3D sprite showing one lit color (the cases are in ge.c).\n"
    "controller-timing.bin: 48 times in microseconds: 16 second sceCtrlReadLatch calls, 16 sceCtrlReadBufferPositive\n"
    "calls just after a vertical blank, 16 second sceCtrlReadBufferPositive calls.\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  sceIoClose(file);
}

//---- round 3: what round 2 left open (docs/psp-core.md, "Results from the user's PSP")

//-- lighting with cosines known exactly

//A normal (a, 0, b) whose a² + b² is a square c² (a Pythagorean triple) is exactly c long, so once the GE makes it
//one long, its cosine with a light along +z is b / c with no rounding in the length itself. Four numbers whose
//squares add up to a square (a quadruple: x² + y² + z² = d²) do the same in three dimensions.
static const unsigned char triples[16][2] = {
  {3, 4}, {5, 12}, {8, 15}, {7, 24}, {20, 21}, {12, 35}, {9, 40}, {28, 45},
  {11, 60}, {16, 63}, {33, 56}, {48, 55}, {13, 84}, {36, 77}, {39, 80}, {65, 72},
};
static const unsigned char quadruples[16][3] = {
  {1, 2, 2}, {2, 3, 6}, {1, 4, 8}, {4, 4, 7}, {2, 6, 9}, {6, 6, 7}, {3, 4, 12}, {2, 5, 14},
  {2, 10, 11}, {1, 12, 12}, {8, 9, 12}, {1, 6, 18}, {6, 6, 17}, {6, 10, 15}, {4, 5, 20}, {4, 8, 19},
};
//Color levels where rounding shows: the top, just under and at powers of two, and a few between.
static const unsigned char levels[16] = {255, 254, 253, 200, 192, 129, 128, 127, 100, 64, 63, 33, 32, 16, 8, 1};
//sceGuLight turns its components into the GE's light kind: GU_POWERED_DIFFUSE into kind 2 (ambient and a "powered"
//diffuse, raised to the shine's power as a shine is), GU_DIFFUSE_AND_SPECULAR into kind 1, anything else (GU_DIFFUSE
//too) into kind 0, ambient and plain diffuse.

typedef struct { float x, y, z; } Normal;

//One row's 16 normals, of a kind: 0 (a, 0, b), cosine b / c; 1 (b, 0, a), cosine a / c; 2-5 kind 0's times 2, 1/2,
//64 and 1/64 (which the GE has to bring back to one long); 6 (0, a, b), leaning toward y instead; 7 the quadruples,
//cosine z / d. Every one is exact in floats.
static void cosineRow(int kind, Normal* normals) {
  static const float scales[6] = {1, 1, 2, 0.5f, 64, 1.0f / 64};
  for(int i = 0; i < 16; i++) {
    float a = triples[i][0], b = triples[i][1];
    if(kind == 7) normals[i] = (Normal){quadruples[i][0], quadruples[i][1], quadruples[i][2]};
    else if(kind == 6) normals[i] = (Normal){0, a, b};
    else if(kind == 1) normals[i] = (Normal){b, 0, a};
    else normals[i] = (Normal){a * scales[kind], 0, b * scales[kind]};
  }
}

//Three levels as a color, one per channel, five apart in the table so a cell's channels differ.
static unsigned int levelColor(int k) {
  return 0xff000000u | levels[k & 15] | levels[(k + 5) & 15] << 8 | levels[(k + 10) & 15] << 16;
}

//Row row of the target's 16x16 cells, each a lit 3D sprite at z 0 in its own color with its own normal. A sprite
//takes its second vertex's color, lit there, so each cell shows one lit color (as litCells).
static void litRow(int row, const unsigned int* colors, const Normal* normals) {
  LitVertex* v = sceGuGetMemory(32 * sizeof(LitVertex));
  for(int i = 0; i < 16; i++) {
    float x = i * 16, y = row * 16;
    const Normal* n = &normals[i];
    v[i * 2] = (LitVertex){colors[i], n->x, n->y, n->z, ndcX(x), ndcY(y), 0};
    v[i * 2 + 1] = (LitVertex){colors[i], n->x, n->y, n->z, ndcX(x + 16), ndcY(y + 16), 0};
  }
  sceGuDrawArray(GU_SPRITES, LitVertexType, 32, 0, v);
}

static void allColors(unsigned int color, unsigned int* colors) {
  for(int i = 0; i < 16; i++) colors[i] = color;
}

//The cosines: white diffuse light along +z on a white material (plain diffuse, light kind 0). Rows 0-7 the eight
//kinds of normals (cosineRow), rows 8-15 the same with the light's direction 3 long, which the GE should bring back
//to one long too.
static void lightCosines(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  sceGuLightColor(0, GU_DIFFUSE, 0xffffff);
  sceGuModelColor(0, 0, 0xffffff, 0);
  unsigned int colors[16];
  Normal normals[16];
  allColors(0xffffffff, colors);
  for(int row = 0; row < 16; row++) {
    ScePspFVector3 toward = {0, 0, row < 8 ? 1.0f : 3.0f};
    if(row % 8 == 0) sceGuLight(0, GU_DIRECTIONAL, GU_AMBIENT_AND_DIFFUSE, &toward);
    cosineRow(row % 8, normals);
    litRow(row, colors, normals);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Powered diffuse (light kind 2), which the core raises to the shine's power as PPSSPP reads it, never measured:
//the same light and material, rows 0-7 the eight kinds of normals with power 1, rows 8-15 with power 2.
static void lightPowered(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  ScePspFVector3 toward = {0, 0, 1};
  sceGuLight(0, GU_DIRECTIONAL, GU_POWERED_DIFFUSE, &toward);
  sceGuLightColor(0, GU_DIFFUSE, 0xffffff);
  sceGuModelColor(0, 0, 0xffffff, 0);
  unsigned int colors[16];
  Normal normals[16];
  allColors(0xffffffff, colors);
  for(int row = 0; row < 16; row++) {
    if(row % 8 == 0) sceGuSpecular(row < 8 ? 1.0f : 2.0f);
    cosineRow(row % 8, normals);
    litRow(row, colors, normals);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The shine alone (light kind 1, its diffuse black): white specular light along +z on a white specular material.
//With the viewer along +z too, the direction half way between them is +z, so the shine's cosine is the normal's:
//rows 0-7 the eight kinds of normals with power 1, rows 8-15 with power 2.
static void lightShine(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  ScePspFVector3 toward = {0, 0, 1};
  sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE_AND_SPECULAR, &toward);
  sceGuLightColor(0, GU_SPECULAR, 0xffffff);
  sceGuModelColor(0, 0, 0, 0xffffff);
  unsigned int colors[16];
  Normal normals[16];
  allColors(0xffffffff, colors);
  for(int row = 0; row < 16; row++) {
    if(row % 8 == 0) sceGuSpecular(row < 8 ? 1.0f : 2.0f);
    cosineRow(row % 8, normals);
    litRow(row, colors, normals);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Material colors: the vertex's color stands for the material's diffuse (MATERIAL_COLOR), three levels per row
//(levelColor), under white light along +z; across each row, kind 0's cosines.
static void lightMaterials(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  ScePspFVector3 toward = {0, 0, 1};
  sceGuLight(0, GU_DIRECTIONAL, GU_AMBIENT_AND_DIFFUSE, &toward);
  sceGuLightColor(0, GU_DIFFUSE, 0xffffff);
  sceGuColorMaterial(GU_DIFFUSE);
  unsigned int colors[16];
  Normal normals[16];
  cosineRow(0, normals);
  for(int row = 0; row < 16; row++) {
    allColors(levelColor(row), colors);
    litRow(row, colors, normals);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Light colors: row by row, the light's diffuse color takes three levels (levelColor), on a white material; across
//each row, kind 0's cosines.
static void lightColors(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  ScePspFVector3 toward = {0, 0, 1};
  sceGuLight(0, GU_DIRECTIONAL, GU_AMBIENT_AND_DIFFUSE, &toward);
  sceGuModelColor(0, 0, 0xffffff, 0);
  unsigned int colors[16];
  Normal normals[16];
  allColors(0xffffffff, colors);
  cosineRow(0, normals);
  for(int row = 0; row < 16; row++) {
    sceGuLightColor(0, GU_DIFFUSE, levelColor(row) & 0xffffff);
    litRow(row, colors, normals);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Ambient alone, no cosine: two colors multiplied and rounded. Row by row the light's ambient color takes three
//levels (levelColor of the row); across each row the vertex's color, standing for the material's ambient, takes
//them too (levelColor of the column), so each channel meets every pair of levels once. The light's diffuse stays
//black.
static void lightAmbient(const char* name) {
  if(!beginTest(name)) return;
  beginLit();
  ScePspFVector3 toward = {0, 0, 1};
  sceGuLight(0, GU_DIRECTIONAL, GU_AMBIENT_AND_DIFFUSE, &toward);
  sceGuColorMaterial(GU_AMBIENT);
  unsigned int colors[16];
  Normal normals[16];
  for(int i = 0; i < 16; i++) {
    colors[i] = levelColor(i);
    normals[i] = (Normal){0, 0, 1};
  }
  for(int row = 0; row < 16; row++) {
    sceGuLightColor(0, GU_AMBIENT, levelColor(row) & 0xffffff);
    litRow(row, colors, normals);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//-- colors and fog stepped across a primitive

//16 ramps, one per band of 16 pixels: the level goes from `from` to `to` over `width` pixels: across the whole
//target, a pixel short of it, at slopes from shallow to steep, backwards, hardly changing, in a few pixels.
typedef struct { short width; unsigned char from, to; } Ramp;
static const Ramp ramps[16] = {
  {256, 0, 255}, {255, 0, 255}, {240, 0, 255}, {200, 0, 255}, {128, 0, 255}, {100, 0, 255}, {64, 0, 255},
  {37, 0, 255}, {256, 255, 0}, {100, 255, 0}, {256, 10, 250}, {256, 100, 101}, {16, 0, 255}, {7, 0, 255},
  {37, 128, 0}, {253, 1, 254},
};
//A level as a color: red the level, green the level backwards, blue half of it, so each band shows three ramps.
static unsigned int rampColor(int level) { return 0xff000000u | level | (255 - level) << 8 | (level / 2) << 16; }

//In through mode: each band a square of two triangles from 0 to its width, along x (or, vertical, along y), the
//color changing along it alone.
static void rampColors(const char* name, int vertical) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  Vertex* vertices = sceGuGetMemory(16 * 6 * sizeof(Vertex));
  for(int band = 0; band < 16; band++) {
    const Ramp* r = &ramps[band];
    unsigned int from = rampColor(r->from), to = rampColor(r->to);
    short a = band * 16, b = band * 16 + 16, w = r->width;  //across the band: a to b; along it: 0 to w
    Vertex* v = vertices + band * 6;
    if(vertical) {
      v[0] = (Vertex){0, 0, from, a, 0, 0, 0};
      v[1] = (Vertex){0, 0, from, b, 0, 0, 0};
      v[2] = (Vertex){0, 0, to, a, w, 0, 0};
      v[3] = (Vertex){0, 0, from, b, 0, 0, 0};
      v[4] = (Vertex){0, 0, to, b, w, 0, 0};
      v[5] = (Vertex){0, 0, to, a, w, 0, 0};
    } else {
      v[0] = (Vertex){0, 0, from, 0, a, 0, 0};
      v[1] = (Vertex){0, 0, to, w, a, 0, 0};
      v[2] = (Vertex){0, 0, from, 0, b, 0, 0};
      v[3] = (Vertex){0, 0, to, w, a, 0, 0};
      v[4] = (Vertex){0, 0, to, w, b, 0, 0};
      v[5] = (Vertex){0, 0, from, 0, b, 0, 0};
    }
  }
  sceGuDrawArray(GU_TRIANGLES, VertexType, 16 * 6, 0, vertices);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The same bands in 3D (identity matrices), each from 3/16 of a pixel in to 5/16 short of its width: corners
//between pixels, as a triangle cut at the near plane has its new ones (3d-clip's colors were a level off).
static void rampColors3D(const char* name) {
  if(!beginTest(name)) return;
  start3D(&identity, 1);
  FloatVertex* vertices = sceGuGetMemory(16 * 6 * sizeof(FloatVertex));
  for(int band = 0; band < 16; band++) {
    const Ramp* r = &ramps[band];
    unsigned int from = rampColor(r->from), to = rampColor(r->to);
    float left = ndcX(3 / 16.0f), right = ndcX(r->width - 5 / 16.0f);
    float top = ndcY(band * 16), bottom = ndcY(band * 16 + 16);
    FloatVertex* v = vertices + band * 6;
    v[0] = (FloatVertex){0, 0, from, left, top, 0};
    v[1] = (FloatVertex){0, 0, to, right, top, 0};
    v[2] = (FloatVertex){0, 0, from, left, bottom, 0};
    v[3] = (FloatVertex){0, 0, to, right, top, 0};
    v[4] = (FloatVertex){0, 0, to, right, bottom, 0};
    v[5] = (FloatVertex){0, 0, from, left, bottom, 0};
  }
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 16 * 6, 0, vertices);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Fog stepped across a primitive (3d-floor-fog was a level off in places): white bands in 3D with fog (near 1, far
//4, blue), each band's depth going from zLeft at x 0 to zRight at its width, so its fog goes from none (z -1) to all
//(z -4) or part of the way. The projection keeps w at 1 and only scales z by 1/4 (so -1 to -4 stays inside the
//depth range): depth can't move x or y, and nothing is stepped for perspective.
static ScePspFMatrix4 quarterDepth = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0.25f, 0}, {0, 0, 0, 1}};
typedef struct { short width; float zLeft, zRight; } FogRamp;
static const FogRamp fogRamps[16] = {
  {256, -1, -4}, {255, -1, -4}, {200, -1, -4}, {128, -1, -4}, {100, -1, -4}, {64, -1, -4}, {37, -1, -4},
  {16, -1, -4}, {256, -4, -1}, {100, -4, -1}, {256, -1.25f, -3.75f}, {256, -2, -2.0625f}, {256, -1, -1.5f},
  {7, -1, -4}, {37, -3, -1}, {253, -1.015625f, -3.984375f},
};
static void rampFog(const char* name) {
  if(!beginTest(name)) return;
  start3D(&quarterDepth, 1);
  sceGuEnable(GU_FOG);
  sceGuFog(1, 4, 0xff0000);
  FloatVertex* vertices = sceGuGetMemory(16 * 6 * sizeof(FloatVertex));
  for(int band = 0; band < 16; band++) {
    const FogRamp* f = &fogRamps[band];
    float left = ndcX(0), right = ndcX(f->width), top = ndcY(band * 16), bottom = ndcY(band * 16 + 16);
    FloatVertex* v = vertices + band * 6;
    v[0] = (FloatVertex){0, 0, 0xffffffff, left, top, f->zLeft};
    v[1] = (FloatVertex){0, 0, 0xffffffff, right, top, f->zRight};
    v[2] = (FloatVertex){0, 0, 0xffffffff, left, bottom, f->zLeft};
    v[3] = (FloatVertex){0, 0, 0xffffffff, right, top, f->zRight};
    v[4] = (FloatVertex){0, 0, 0xffffffff, right, bottom, f->zRight};
    v[5] = (FloatVertex){0, 0, 0xffffffff, left, bottom, f->zLeft};
  }
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 16 * 6, 0, vertices);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//-- 3D edges, texels and sprites

//The rounding onto the screen where it decides a pixel (round 2's 3d-rounding stayed short of the middle): in cell
//(i, j) a square whose left edge lies i 256ths past the middle of the cell's pixel column 4 and its top edge j
//256ths below the middle of row 4; its right and bottom edges as far past the middles of column and row 12. Pixel
//(4, 4) is drawn while both its edges come back onto the middle (truncated to the sixteenth, all 16 cells of each),
//not once they round up to the next sixteenth; pixel (12, 12) the other way round.
static void roundingMiddle3D(const char* name) {
  if(!beginTest(name)) return;
  start3D(&identity, 1);
  FloatVertex* vertices = sceGuGetMemory(256 * 6 * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    float x = (cell & 15) * 16, y = (cell >> 4) * 16, i = (cell & 15) / 256.0f, j = (cell >> 4) / 256.0f;
    float left = ndcX(x + 4.5f + i), top = ndcY(y + 4.5f + j);
    float right = ndcX(x + 12.5f + i), bottom = ndcY(y + 12.5f + j);
    FloatVertex* v = vertices + cell * 6;
    v[0] = (FloatVertex){0, 0, 0xffffffff, left, top, 0};
    v[1] = (FloatVertex){0, 0, 0xffffffff, right, top, 0};
    v[2] = (FloatVertex){0, 0, 0xffffffff, left, bottom, 0};
    v[3] = (FloatVertex){0, 0, 0xffffffff, right, top, 0};
    v[4] = (FloatVertex){0, 0, 0xffffffff, right, bottom, 0};
    v[5] = (FloatVertex){0, 0, 0xffffffff, left, bottom, 0};
  }
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 256 * 6, 0, vertices);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//A wall receding under the lens, from z -1 at its left edge to z -4 at its right, the texture (texel (x, y) is
//x | y << 8 | 0x80 << 16) across it from u 0 to 1: perspective-correct texels stepped along x, where the floor
//(3d-floor-texels, 291 pixels a texel off) steps them along y.
static void wall3D(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&lens, 1);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  FloatVertex* v = sceGuGetMemory(6 * sizeof(FloatVertex));
  v[0] = (FloatVertex){0, 0, 0xffffffff, -1, 1, -1};
  v[1] = (FloatVertex){1, 0, 0xffffffff, 1, 1, -4};  //at w 4: x 0.25, y 0.25 on the screen
  v[2] = (FloatVertex){0, 1, 0xffffffff, -1, -1, -1};
  v[3] = (FloatVertex){1, 0, 0xffffffff, 1, 1, -4};
  v[4] = (FloatVertex){1, 1, 0xffffffff, 1, -1, -4};
  v[5] = (FloatVertex){0, 1, 0xffffffff, -1, -1, -1};
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Round 2's 3D sprite (3d-sprite, where neither the core's rule nor PPSSPP's held) taken apart: the same sprite under
//the lens, from (-0.9, 0.9) at nearZ to (0.9, -0.8) at farZ, white; with the texture (modulated) or not, and fog
//(near 1, far 4, blue) or not.
static void sprite3DParts(const char* name, int textured, int fogged, float nearZ, float farZ) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&lens, 1);
  if(textured) useTexture(GU_TFX_MODULATE, GU_TCC_RGB);
  if(fogged) {
    sceGuEnable(GU_FOG);
    sceGuFog(1, 4, 0xff0000);
  }
  FloatVertex* v = sceGuGetMemory(2 * sizeof(FloatVertex));
  v[0] = (FloatVertex){0, 0, 0xffffffff, -0.9f, 0.9f, nearZ};
  v[1] = (FloatVertex){1, 1, 0xffffffff, 0.9f, -0.8f, farZ};
  sceGuDrawArray(GU_SPRITES, FloatVertexType3D, 2, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//-- curved surfaces

//A 4x4 grid of control points in 3D (identity matrices), flat or curved: the curved one's edges bow out, and its
//middle points twist, so the surface's shape and its colors both show.
static const float flatGrid[4][4][2] = {
  {{-0.75f, 0.75f}, {-0.25f, 0.75f}, {0.25f, 0.75f}, {0.75f, 0.75f}},
  {{-0.75f, 0.25f}, {-0.25f, 0.25f}, {0.25f, 0.25f}, {0.75f, 0.25f}},
  {{-0.75f, -0.25f}, {-0.25f, -0.25f}, {0.25f, -0.25f}, {0.75f, -0.25f}},
  {{-0.75f, -0.75f}, {-0.25f, -0.75f}, {0.25f, -0.75f}, {0.75f, -0.75f}},
};
static const float curvedGrid[4][4][2] = {
  {{-0.75f, 0.75f}, {-0.25f, 0.875f}, {0.25f, 0.875f}, {0.75f, 0.75f}},
  {{-0.875f, 0.25f}, {-0.125f, 0.125f}, {0.375f, 0.375f}, {0.875f, 0.25f}},
  {{-0.875f, -0.25f}, {-0.375f, -0.375f}, {0.125f, -0.125f}, {0.875f, -0.25f}},
  {{-0.75f, -0.75f}, {-0.25f, -0.875f}, {0.25f, -0.875f}, {0.75f, -0.75f}},
};

//The curved surfaces (the core doesn't draw them yet): the grid, each control point a color of its own, as a Bezier
//patch (edges -1) or a spline whose edge types are edges for both directions (SPLINE's bits 16-19, which pspsdk
//passes on as given: which value means an open or closed edge is for the PSP to show), cut into divisions x
//divisions pieces (PATCH_DIVISION) drawn as triangles.
static void patch(const char* name, const float grid[4][4][2], int divisions, int edges) {
  if(!beginTest(name)) return;
  start3D(&identity, 1);
  FloatVertex* v = sceGuGetMemory(16 * sizeof(FloatVertex));
  for(int j = 0; j < 4; j++) {
    for(int i = 0; i < 4; i++) {
      unsigned int color = 0xff000000u | (i * 85) | (j * 85) << 8 | ((3 - i) * 85) << 16;
      v[j * 4 + i] = (FloatVertex){i, j, color, grid[j][i][0], grid[j][i][1], 0};
    }
  }
  sceGuPatchDivide(divisions, divisions);
  sceGuPatchPrim(GU_TRIANGLE_STRIP);
  if(edges < 0) sceGuDrawBezier(FloatVertexType3D, 4, 4, 0, v);
  else sceGuDrawSpline(FloatVertexType3D, 4, 4, edges, edges, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//-- the depth buffer's layout

//Round 2's floor depths didn't read back where the program looked. Here every pixel of a 256x64 area gets a depth
//of its own, y * 256 + x (a point in through mode, the depth test passing always and writing), over a depth buffer
//filled with 0xffff; then the whole depth buffer (512x256 values) is read back through VRAM copy `copy`
//(0x04000000 + copy * 0x200000, uncached), so wherever in it the values land and however that copy arranges them,
//they're in the file. The depth range is set here, full, since a retried case can be the first to draw after
//sceGuInit (which leaves it 0 to 0). Last in the round: no program here has read VRAM's other copies on a PSP before.
static void depthLayout(const char* name, int copy) {
  if(!beginTest(name)) return;
  for(int n = 0; n < Stride * 256; n++) VRAM16[Depth / 2 + n] = 0xffff;
  start(GU_PSM_8888);
  sceGuDepthRange(65535, 0);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthFunc(GU_ALWAYS);
  sceGuDepthMask(GU_FALSE);
  Vertex* v = sceGuGetMemory(256 * 64 * sizeof(Vertex));
  for(int y = 0; y < 64; y++) {
    for(int x = 0; x < 256; x++) v[y * 256 + x] = (Vertex){0, 0, 0xffffffff, x, y, y * 256 + x, 0};
  }
  sceGuDrawArray(GU_POINTS, VertexType, 256 * 64, 0, v);
  finishList();
  volatile unsigned short* depths = (volatile unsigned short*)(0x44000000 + copy * 0x200000 + Depth);
  for(int half = 0; half < 2; half++) {  //pixels holds 512x128 words at a time
    for(int n = 0; n < Stride * 128; n++) pixels[n] = depths[half * Stride * 128 + n];
    if(!writeOut(&current, name, pixels, Stride * 128 * 4)) {
      failed = 1;
      return;
    }
  }
  if(!finish(&current, name)) failed = 1;
}

static void writeManifest3(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest3.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 3 (tools/psp-measure/ge.c in Phobos says what each test draws): each .bin is the\n"
    "target's pixels after one test, a little-endian 32-bit word each, row by row, 256x256; but depth-layout-<n>,\n"
    "the whole depth buffer, 512x256 16-bit values (in the low half), read through VRAM copy n\n"
    "(0x04000000 + n * 0x200000) after each pixel (x, y) of a 256x64 area was given depth y * 256 + x over 0xffff.\n"
    "light-*: 16x16 cells of one lit color each; normals with cosines known exactly (Pythagorean triples and\n"
    "quadruples), levels 255 254 253 200 192 129 128 127 100 64 63 33 32 16 8 1 (the cases are in ge.c).\n"
    "ramp-*: 16 bands, each a ramp of color (or fog) over its own width. 3d-*: drawn through the matrices.\n"
    "bezier-*, spline-*: curved surfaces from a 4x4 grid of control points.\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  sceIoClose(file);
}

static void round3(void) {
  writeManifest3();
  lightCosines("light-cosines");
  lightPowered("light-powered");
  lightShine("light-shine");
  lightMaterials("light-materials");
  lightColors("light-colors");
  lightAmbient("light-ambient");
  rampColors("ramp-colors", 0);
  rampColors("ramp-colors-vertical", 1);
  rampColors3D("ramp-colors-3d");
  rampFog("ramp-fog");
  roundingMiddle3D("3d-rounding-middle");
  wall3D("3d-wall-texels");
  sprite3DParts("3d-sprite-fog", 0, 1, -1, -4);
  sprite3DParts("3d-sprite-texels", 1, 0, -1, -4);
  sprite3DParts("3d-sprite-flat", 1, 1, -2, -2);
  patch("bezier-flat", flatGrid, 4, -1);
  patch("bezier-curved", curvedGrid, 4, -1);
  patch("bezier-divide-8", curvedGrid, 8, -1);
  patch("spline-edges-0", curvedGrid, 4, 0);
  patch("spline-edges-3", curvedGrid, 4, 3);
  for(int copy = 0; copy < 4; copy++) {
    char name[32];
    snprintf(name, sizeof(name), "depth-layout-%d", copy);
    depthLayout(name, copy);
  }
}

//---- round 4: lines, bounding boxes and compressed textures (docs/psp-core.md, part 29), which Phobos's core draws
//by rules pspautotests' recordings settle only in part

//-- lines

//In cell (i, j) of the target's 16x16 cells, a line from i and j sixteenths past the cell's pixel (sx, sy) to
//(dx, dy) pixels further, or (reversed) from there back: both ends' places in their pixels at once, for which pixels
//a line lights at its ends and along it.
static void lineCells(const char* name, float sx, float sy, float dx, float dy, int reversed, int smooth) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  if(smooth) {  //anti-aliased, white blended over black by its alpha
    sceGuEnable(GU_LINE_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  }
  FloatVertex* v = sceGuGetMemory(256 * 2 * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    float x = (cell & 15) * 16 + sx + (cell & 15) / 16.0f, y = (cell >> 4) * 16 + sy + (cell >> 4) / 16.0f;
    FloatVertex a = {0, 0, 0xffffffff, x, y, 0}, b = {0, 0, 0xffffffff, x + dx, y + dy, 0};
    v[cell * 2] = reversed ? b : a;
    v[cell * 2 + 1] = reversed ? a : b;
  }
  sceGuDrawArray(GU_LINES, FloatVertexType, 512, 0, v);
  if(smooth) sceGuDisable(GU_LINE_SMOOTH);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Lines shorter than two pixels: in row k of the 16x16 cells, the direction k * 22.5 degrees; in column n, the length
//(n + 1) / 8 pixels; each from 5/16 and 11/16 into the cell's pixel (8, 8).
static void shortLines(const char* name) {
  static const float directions[16][2] = {
    {1, 0}, {0.92388f, 0.38268f}, {0.70711f, 0.70711f}, {0.38268f, 0.92388f}, {0, 1}, {-0.38268f, 0.92388f},
    {-0.70711f, 0.70711f}, {-0.92388f, 0.38268f}, {-1, 0}, {-0.92388f, -0.38268f}, {-0.70711f, -0.70711f},
    {-0.38268f, -0.92388f}, {0, -1}, {0.38268f, -0.92388f}, {0.70711f, -0.70711f}, {0.92388f, -0.38268f},
  };
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  FloatVertex* v = sceGuGetMemory(256 * 2 * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    float length = ((cell & 15) + 1) / 8.0f;
    float x = (cell & 15) * 16 + 8 + 5 / 16.0f, y = (cell >> 4) * 16 + 8 + 11 / 16.0f;
    const float* d = directions[cell >> 4];
    v[cell * 2] = (FloatVertex){0, 0, 0xffffffff, x, y, 0};
    v[cell * 2 + 1] = (FloatVertex){0, 0, 0xffffffff, x + d[0] * length, y + d[1] * length, 0};
  }
  sceGuDrawArray(GU_LINES, FloatVertexType, 512, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Line strips added up (each pixel drawn adds 1 to it): in cell (i, j) a strip of three lines whose two joints sit i
//and j sixteenths past pixel corners, turning sharply and gently, so a pixel lit twice shows 2.
static void lineStrips(const char* name) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
  for(int cell = 0; cell < 256; cell++) {
    float i = (cell & 15) / 16.0f, j = (cell >> 4) / 16.0f, x = (cell & 15) * 16, y = (cell >> 4) * 16;
    FloatVertex* v = sceGuGetMemory(4 * sizeof(FloatVertex));
    unsigned int one = 0xff010101;
    v[0] = (FloatVertex){0, 0, one, x + 1.5f, y + 2.25f, 0};
    v[1] = (FloatVertex){0, 0, one, x + 11 + i, y + 5 + j, 0};
    v[2] = (FloatVertex){0, 0, one, x + 3 + j, y + 9 + i, 0};
    v[3] = (FloatVertex){0, 0, one, x + 13.75f, y + 14.5f, 0};
    sceGuDrawArray(GU_LINE_STRIP, FloatVertexType, 4, 0, v);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Colors along lines: in band k (16 rows each), a level line at row k * 16 + 4 (through its pixels' middles) and a
//slanting one from row k * 16 + 6.25 down 7.5 rows, each over ramps[k]'s width with ramps[k]'s levels (rampColor):
//how the GE steps colors along a line.
static void lineColors(const char* name) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  FloatVertex* v = sceGuGetMemory(16 * 4 * sizeof(FloatVertex));
  for(int band = 0; band < 16; band++) {
    const Ramp* r = &ramps[band];
    unsigned int from = rampColor(r->from), to = rampColor(r->to);
    float y = band * 16;
    v[band * 4] = (FloatVertex){0, 0, from, 0, y + 4.5f, 0};
    v[band * 4 + 1] = (FloatVertex){0, 0, to, r->width, y + 4.5f, 0};
    v[band * 4 + 2] = (FloatVertex){0, 0, from, 0.25f, y + 6.25f, 0};
    v[band * 4 + 3] = (FloatVertex){0, 0, to, r->width - 0.75f, y + 13.75f, 0};
  }
  sceGuDrawArray(GU_LINES, FloatVertexType, 64, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Depth along lines (through mode, the depth written, the depth buffer read through VRAM's fourth copy, which reads it
//in order: round 3's depth-layout-3): in band k, a level line and a slanting one as lineColors draws, their depths
//from 0 to 65535, 1000 to 1100, 65535 to 0 and the like (depthRamps).
static void lineDepth(const char* name) {
  static const float depthRamps[16][2] = {  //(70007: past the 16 bits through mode's depths have)
    {0, 65535}, {65535, 0}, {1000, 1100}, {1100, 1000}, {0, 255}, {255, 0}, {32768, 32769}, {100, 200},
    {12345, 54321}, {54321, 12345}, {0, 1}, {1, 0}, {40000, 40255}, {65280, 65535}, {7, 70007}, {30000, 30016},
  };
  if(!beginTest(name)) return;
  for(int n = 0; n < Stride * 256; n++) VRAM16[Depth / 2 + n] = 0;
  start(GU_PSM_8888);
  sceGuDepthRange(65535, 0);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthFunc(GU_ALWAYS);
  sceGuDepthMask(GU_FALSE);
  FloatVertex* v = sceGuGetMemory(16 * 4 * sizeof(FloatVertex));
  for(int band = 0; band < 16; band++) {
    float y = band * 16, width = ramps[band].width, from = depthRamps[band][0], to = depthRamps[band][1];
    v[band * 4] = (FloatVertex){0, 0, 0xffffffff, 0, y + 4.5f, from};
    v[band * 4 + 1] = (FloatVertex){0, 0, 0xffffffff, width, y + 4.5f, to};
    v[band * 4 + 2] = (FloatVertex){0, 0, 0xffffffff, 0.25f, y + 6.25f, from};
    v[band * 4 + 3] = (FloatVertex){0, 0, 0xffffffff, width - 0.75f, y + 13.75f, to};
  }
  sceGuDrawArray(GU_LINES, FloatVertexType, 64, 0, v);
  finishList();
  volatile unsigned short* depths = (volatile unsigned short*)(0x44000000 + 3 * 0x200000 + Depth);
  for(int y = 0; y < 256; y++) {
    for(int x = 0; x < 256; x++) pixels[y * 256 + x] = depths[y * Stride + x];
  }
  save(name, pixels, 256 * 256 * 4);
}

//Texels along lines: the texture (texel (x, y) is x | y << 8 | 0x80 << 16), replace, across lines: in band k, a level
//line from texel row k * 16 + 4 over 256 texels on ramps[k]'s width, and a slanting one from texel (0, 0) to
//(255, 255) over it: which texel each pixel takes along a line.
static void lineTexels(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  FloatVertex* v = sceGuGetMemory(16 * 4 * sizeof(FloatVertex));
  for(int band = 0; band < 16; band++) {
    float y = band * 16, width = ramps[band].width, row = band * 16 + 4.5f;
    v[band * 4] = (FloatVertex){0, row, 0xffffffff, 0, y + 4.5f, 0};
    v[band * 4 + 1] = (FloatVertex){256, row, 0xffffffff, width, y + 4.5f, 0};
    v[band * 4 + 2] = (FloatVertex){0, 0, 0xffffffff, 0.25f, y + 6.25f, 0};
    v[band * 4 + 3] = (FloatVertex){256, 256, 0xffffffff, width - 0.75f, y + 13.75f, 0};
  }
  sceGuDrawArray(GU_LINES, FloatVertexType, 64, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Lines in 3D (identity matrices; begin3D's viewport puts x and y on the target's pixels): in cell (i, j) a shallow
//line from i and j sixteenths past the cell's pixel (4, 4), its colors red to blue and its z from 0.5 to -0.5;
//and, in the bottom right, lines under the lens that reach past the near plane and are cut there.
static void lines3D(const char* name) {
  if(!beginTest(name)) return;
  start3D(&identity, 1);
  FloatVertex* v = sceGuGetMemory(256 * 2 * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    float x = (cell & 15) * 16 + 4 + (cell & 15) / 16.0f, y = (cell >> 4) * 16 + 4 + (cell >> 4) / 16.0f;
    v[cell * 2] = (FloatVertex){0, 0, 0xff0000ff, ndcX(x), ndcY(y), 0.5f};
    v[cell * 2 + 1] = (FloatVertex){0, 0, 0xffff0000, ndcX(x + 7.5f), ndcY(y + 2.25f), -0.5f};
  }
  sceGuDrawArray(GU_LINES, FloatVertexType3D, 512, 0, v);
  finishList();
  begin3D(&lens, 1);
  static const float nearEnds[4] = {-0.4f, -0.3f, 0, 0.5f};  //past the lens's near plane (z above -0.5)
  FloatVertex* cut = sceGuGetMemory(8 * sizeof(FloatVertex));
  for(int n = 0; n < 4; n++) {  //from z -1 (w 1) to nearer than the near plane, behind the camera at the last
    cut[n * 2] = (FloatVertex){0, 0, 0xff00ff00, 0.1f + n * 0.2f, -0.5f, -1};
    cut[n * 2 + 1] = (FloatVertex){0, 0, 0xffff00ff, 0.1f + n * 0.2f + 0.3f, -0.9f, nearEnds[n]};
  }
  sceGuDrawArray(GU_LINES, FloatVertexType3D, 8, 0, cut);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//-- bounding boxes

typedef struct { float x, y, z; } Corner;
enum { CornerType3D = GU_VERTEX_32BITF | GU_TRANSFORM_3D, CornerTypeThrough = GU_VERTEX_32BITF | GU_TRANSFORM_2D };

//Box k's corners (count of them, mostly 8) for bbox below, its vertex type, the scissor rectangle it's tested
//against and whether DEPTH_CLIP_ENABLE is on. The target's 3D space is begin3D's, x and y from -1 to 1 across its
//256x256 pixels (ndcX, ndcY), the scissor rectangle a third of it, pixels 64-191 each way, unless the case says.
typedef struct { int count, through, clamp, lens; int left, top, right, bottom; Corner corners[8]; } Box;

static void boxCorners(Box* box, float x0, float y0, float z0, float x1, float y1, float z1) {
  for(int n = 0; n < 8; n++) {
    box->corners[n] = (Corner){n & 1 ? x1 : x0, n & 2 ? y1 : y0, n & 4 ? z1 : z0};
  }
  box->count = 8;
}

//The 256 cases: rows 0-1, a single vertex either side of the scissor rectangle's left and right edges (in pixels,
//from 16 to 0 sixteenths short of a pixel past them, to 16 past), and rows 2-3 its top and bottom; row 4, boxes
//inside, partly past each edge, and wholly past each; row 5, boxes past different edges at once (around the view
//among them); row 6, past the near and far planes, with DEPTH_CLIP_ENABLE on and off; row 7, under the lens,
//behind the camera (w below zero) wholly and partly; row 8, in through mode; rows 9-15, random boxes.
static unsigned int boxRandom = 0x13579bdf;
static float boxRange(float low, float high) {
  boxRandom = boxRandom * 1103515245 + 12345;
  return low + (high - low) * ((boxRandom >> 8) & 0xffff) / 65535.0f;
}
static void makeBox(int k, Box* box) {
  int row = k >> 4, n = k & 15;
  box->through = 0, box->clamp = 1, box->lens = 0;
  box->left = 64, box->top = 64, box->right = 191, box->bottom = 191;
  float offsets[16] = {-1.0f, -0.9375f, -0.5f, -0.0625f, 0, 0.0625f, 0.5f, 0.9375f, 1, 1.0625f, 1.5f, 2, -2, -1.5f,
                       -1.0625f, 3};
  float off = offsets[n];
  box->count = 1;
  if(row == 0) box->corners[0] = (Corner){ndcX(64 - 1 + off), ndcY(128), 0};       //about left - 1
  else if(row == 1) box->corners[0] = (Corner){ndcX(192 + off), ndcY(128), 0};     //about right + 1 (192)
  else if(row == 2) box->corners[0] = (Corner){ndcX(128), ndcY(64 - 1 + off), 0};
  else if(row == 3) box->corners[0] = (Corner){ndcX(128), ndcY(192 + off), 0};
  else if(row == 4) {
    static const float boxes[16][4] = {  //pixels: left, top, right, bottom
      {100, 100, 150, 150}, {40, 100, 70, 150}, {180, 100, 220, 150}, {100, 40, 150, 70}, {100, 180, 150, 220},
      {10, 100, 60, 150}, {196, 100, 250, 150}, {100, 10, 150, 60}, {100, 196, 150, 250}, {0, 0, 62, 62},
      {194, 194, 255, 255}, {62, 62, 63, 63}, {192.5f, 100, 193, 101}, {62.5f, 100, 62.9f, 101},
      {100, 62.9f, 101, 62.95f}, {64, 64, 191, 191},
    };
    const float* b = boxes[n];
    boxCorners(box, ndcX(b[0]), ndcY(b[1]), -0.5f, ndcX(b[2]), ndcY(b[3]), 0.5f);
  } else if(row == 5) {
    static const float boxes[16][4] = {
      {0, 0, 255, 30}, {0, 225, 255, 255}, {0, 0, 30, 255}, {225, 0, 255, 255}, {0, 0, 255, 255},
      {20, 20, 236, 236}, {10, 128, 245, 129}, {128, 10, 129, 245}, {-500, -500, 800, 800}, {0, 0, 60, 255},
      {-300, 30, 600, 50}, {30, -300, 50, 600}, {-300, 200, 600, 240}, {200, -300, 240, 600}, {0, 0, 255, 63},
      {50, 50, 70, 70},
    };
    const float* b = boxes[n];
    boxCorners(box, ndcX(b[0]), ndcY(b[1]), -0.5f, ndcX(b[2]), ndcY(b[3]), 0.5f);
  } else if(row == 6) {
    static const float depths[8][2] = {  //z near and far (w 1): -w to w is in reach
      {-0.5f, 0.5f}, {-3, -1.5f}, {1.5f, 3}, {-3, 0}, {0, 3}, {-1.0001f, -1.00005f}, {-1, -1}, {1, 1},
    };
    boxCorners(box, ndcX(100), ndcY(100), depths[n & 7][0], ndcX(150), ndcY(150), depths[n & 7][1]);
    box->clamp = n < 8;
  } else if(row == 7) {
    static const float depths[16][2] = {  //under the lens, w = -z: below 0 behind the camera
      {-2, -3}, {0.5f, 2}, {-0.25f, -3}, {1, 3}, {-3, 3}, {0.4f, 0.6f}, {-0.6f, -0.4f}, {-0.6f, 2},
      {-2, -3}, {0.5f, 2}, {-0.25f, -3}, {1, 3}, {-3, 3}, {0.4f, 0.6f}, {-0.6f, -0.4f}, {-0.6f, 2},
    };
    float size = n < 8 ? 0.2f : 3.0f;  //small around the middle, or big
    boxCorners(box, -size, -size, depths[n][0], size, size, depths[n][1]);
    box->lens = 1;
  } else if(row == 8) {
    static const float boxes[16][4] = {
      {100, 100, 150, 150}, {40, 100, 70, 150}, {180, 100, 220, 150}, {100, 40, 150, 70}, {100, 180, 150, 220},
      {63, 100, 63.9375f, 101}, {62.9375f, 100, 63, 101}, {192, 100, 193, 101}, {192.0625f, 100, 193, 101},
      {100, 63, 101, 63.9375f}, {100, 192.0625f, 101, 193}, {0, 0, 255, 255}, {-100, 0, 300, 30},
      {500, 500, 600, 600}, {-600, -600, -500, -500}, {191, 191, 192, 192},
    };
    const float* b = boxes[n];
    boxCorners(box, b[0], b[1], 0, b[2], b[3], 0);
    box->through = 1;
  } else {
    float x0 = boxRange(-1.5f, 1.5f), y0 = boxRange(-1.5f, 1.5f), z0 = boxRange(-1.5f, 1.5f);
    boxCorners(box, x0, y0, z0, x0 + boxRange(0, 1.2f), y0 + boxRange(0, 1.2f), z0 + boxRange(0, 1.2f));
    box->clamp = n & 1;
  }
}

//Bounding boxes: in cell k (16x16 pixels), box k's corners tested (sceGuBeginObject, which sends BOUNDING_BOX and a
//BJUMP, and sceGuEndObject, which aims it), and the cell filled in white only if the GE took the box to be in sight.
//(Everything sceGuGetMemory hands out is taken before sceGuBeginObject: it lets the GE run on, up to a BJUMP not
//aimed yet.)
static void boundingBoxes(const char* name) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  boxRandom = 0x13579bdf;
  for(int k = 0; k < 256; k++) {
    Box box;
    makeBox(k, &box);
    begin3D(box.lens ? &lens : &identity, box.clamp);
    sceGuDisable(GU_DEPTH_TEST);
    Corner* corners = sceGuGetMemory(8 * sizeof(Corner));
    memcpy(corners, box.corners, sizeof(box.corners));
    Vertex* v = sceGuGetMemory(2 * sizeof(Vertex));
    short x = (k & 15) * 16, y = (k >> 4) * 16;
    v[0] = (Vertex){0, 0, 0xffffffff, x, y, 0, 0};
    v[1] = (Vertex){0, 0, 0xffffffff, x + 16, y + 16, 0, 0};
    sceGuSendCommandi(0xd4, box.top << 10 | box.left);     //SCISSOR1
    sceGuSendCommandi(0xd5, box.bottom << 10 | box.right); //SCISSOR2
    sceGuBeginObject(box.through ? CornerTypeThrough : CornerType3D, box.count, 0, corners);
    sceGuSendCommandi(0xd4, 0);
    sceGuSendCommandi(0xd5, 255 << 10 | 511);
    sceGuDrawArray(GU_SPRITES, VertexType, 2, 0, v);
    sceGuEndObject();
    finishList();
  }
  saveTarget(name, 256, 256, 0);
}

//-- compressed textures (DXT)

static unsigned char __attribute__((aligned(16))) blocks[64 * 64];  //a 64x64 texture's blocks, or more of DXT1's

static unsigned int dxtRandom;
static unsigned int dxtNext(void) {
  dxtRandom = dxtRandom * 1103515245 + 12345;
  return dxtRandom >> 8;
}

//A 64x64 texture in format psm (8, 9 or 10) of 256 blocks of numbers from a fixed sequence (every 8th block's colors,
//and every 8th + 1's DXT5 alphas, made equal), drawn 1:1 at the target's top left with replace, for its colors, and
//over white at (128, 0), blended by its alpha (white times the texel's alpha), for its alpha.
static void dxtColors(const char* name, int psm) {
  if(!beginTest(name)) return;
  int blockBytes = psm == GU_PSM_DXT1 ? 8 : 16;
  dxtRandom = 0x2468ace0 + psm;
  for(int n = 0; n < 256 * blockBytes; n++) blocks[n] = dxtNext();
  for(int n = 0; n < 256; n++) {
    unsigned char* b = blocks + n * blockBytes;
    if(n % 8 == 0) b[6] = b[4], b[7] = b[5];
    if(psm == GU_PSM_DXT5 && n % 8 == 1) b[15] = b[14];
  }
  sceKernelDcacheWritebackAll();
  for(int y = 0; y < 64; y++) {
    for(int x = 0; x < 256; x++) VRAM[Target / 4 + y * Stride + x] = x >= 128 ? 0xffffffff : 0;
  }
  start(GU_PSM_8888);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(psm, 0, 0, 0);
  sceGuTexImage(0, 64, 64, 64, blocks);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  sprite(0, 0, 64, 64, 0, 0, 64, 64, 0xffffffff);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_FIX, GU_SRC_ALPHA, 0, 0);
  sprite(128, 0, 192, 64, 0, 0, 64, 64, 0xffffffff);
  finishList();
  saveTarget(name, 256, 64, 0);
}

//The blocks' order: a 32x32 DXT1 texture of solid blocks, block n's color n (red n * 8 in 565, green n >> 5), drawn
//with TEXTURE_BUFFER_WIDTH0 32 (at the top left), 64 (at (64, 0)), 36 (at (128, 0): not whole blocks of 8 bytes'
//16-byte rows), and swizzled (at (192, 0)): where each block lands, which tells how the GE walks them.
static void dxtLayout(const char* name) {
  if(!beginTest(name)) return;
  for(int n = 0; n < 512; n++) {
    unsigned char* b = blocks + n * 8;
    unsigned int color = (n & 31) << 11 | (n >> 5 & 63) << 5;
    b[0] = b[1] = b[2] = b[3] = 0;
    b[4] = color, b[5] = color >> 8, b[6] = 0, b[7] = 0;
  }
  sceKernelDcacheWritebackAll();
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexFilter(GU_NEAREST, GU_NEAREST);
  sceGuTexWrap(GU_CLAMP, GU_CLAMP);
  static const int widths[4] = {32, 64, 36, 32};
  for(int k = 0; k < 4; k++) {
    sceGuTexMode(GU_PSM_DXT1, 0, 0, k == 3);
    sceGuTexImage(0, 32, 32, widths[k], blocks);
    sprite(k * 64, 0, k * 64 + 32, 32, 0, 0, 32, 32, 0xffffffff);
  }
  finishList();
  saveTarget(name, 256, 32, 0);
}

//-- curved surfaces' vertices (docs/psp-core.md, part 33)

//A 4x4 grid of control points in a unit square (x right, y down), bowed and twisted, so that its vertices fall
//between whole pixels; each point's color, depth and texture coordinates chosen so that theirs fall between whole
//levels and texels too. A spline's 5x5 points (curvesSpline) take a fifth row and column of their own.
static const float curveGrid[4][4][2] = {
  {{0, 0}, {0.3125f, -0.0625f}, {0.6875f, 0.0625f}, {1, 0}},
  {{-0.0625f, 0.3333f}, {0.4375f, 0.25f}, {0.5625f, 0.4167f}, {1.0625f, 0.3125f}},
  {{0.0625f, 0.6875f}, {0.3125f, 0.75f}, {0.75f, 0.5625f}, {0.9375f, 0.6667f}},
  {{0, 1}, {0.2917f, 1.0625f}, {0.6875f, 0.9375f}, {1, 1}},
};
static unsigned int curveColor(int i, int j) {  //i and j up to 4
  return 0xff000000u | (i * 50 + j * 9) | (j * 50 + i * 11) << 8 | (255 - i * 30 - j * 25) << 16;
}
static float curveDepth(int i, int j) { return 311 + 6007 * i + 7919 * j + 977 * ((i * 3 + j * 5) % 7); }

typedef struct { unsigned int color; float x, y, z; } ColorVertex;
typedef struct { float x, y, z; } PlainVertex;
typedef struct { float nx, ny, nz, x, y, z; } NormalVertex;
enum {
  ColorVertexType = GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
  ColorVertexType3D = GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
  PlainVertexType3D = GU_VERTEX_32BITF | GU_TRANSFORM_3D,
  NormalVertexType3D = GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
};

//curveGrid's points in through mode (positions pixels, z the depth itself), size pixels square from (x, y), each in
//its curveColor and at its curveDepth, texture coordinates u = 75i + 7j + 3.3 and v = 75j + 5i + 1.7 (texels).
static FloatVertex* curvePoints(float x, float y, float size) {
  FloatVertex* v = sceGuGetMemory(16 * sizeof(FloatVertex));
  for(int j = 0; j < 4; j++) {
    for(int i = 0; i < 4; i++) {
      v[j * 4 + i] = (FloatVertex){75 * i + 7 * j + 3.3f, 75 * j + 5 * i + 1.7f, curveColor(i, j),
                                   x + curveGrid[j][i][0] * size, y + curveGrid[j][i][1] * size, curveDepth(i, j)};
    }
  }
  return v;
}

//The depth buffer's first 256x256 values through VRAM's fourth copy, which reads it in order (round 3's
//depth-layout-3), as words.
static void saveDepthInOrder(const char* name) {
  volatile unsigned short* depths = (volatile unsigned short*)(0x44000000 + 3 * 0x200000 + Depth);
  for(int y = 0; y < 256; y++) {
    for(int x = 0; x < 256; x++) pixels[y * 256 + x] = depths[y * Stride + x];
  }
  save(name, pixels, 256 * 256 * 4);
}

//Through mode, drawn as points (each vertex its pixel, in its own color, its depth written: the depth test passing
//always) over a depth buffer of 0.
static void startCurvePoints(void) {
  fillTarget(zero, 0);
  for(int n = 0; n < Stride * 256; n++) VRAM16[Depth / 2 + n] = 0;
  start(GU_PSM_8888);
  sceGuDepthRange(65535, 0);
  sceGuEnable(GU_DEPTH_TEST);
  sceGuDepthFunc(GU_ALWAYS);
  sceGuDepthMask(GU_FALSE);
  sceGuPatchPrim(GU_POINTS);
}

//Bezier patches' vertices: in cell k of 4x4 cells of 64 pixels, curveGrid 56 pixels square cut into k + 1 divisions
//each way (17x17 vertices in the last), as points: their pixels and colors, or (depths) their depths.
static void curvesBezier(const char* name, int depths) {
  if(!beginTest(name)) return;
  startCurvePoints();
  for(int k = 0; k < 16; k++) {
    sceGuPatchDivide(k + 1, k + 1);
    sceGuDrawBezier(FloatVertexType, 4, 4, 0, curvePoints((k & 3) * 64 + 4, (k >> 2) * 64 + 4, 56));
  }
  finishList();
  if(depths) saveDepthInOrder(name);
  else saveTarget(name, 256, 256, 0);
}

//Where the vertices fall, to the sixteenth: in cell (i, j) of 16x16 cells of 16 pixels, curveGrid 12 pixels square
//cut 5 times along u and 3 along v, its control points i sixteenths of a pixel right and j down. A vertex at x lights
//pixel x + i / 16 rounded down: the cell where it moves to the next pixel tells its sixteenths.
static void curvesPlaces(const char* name) {
  if(!beginTest(name)) return;
  startCurvePoints();
  sceGuPatchDivide(5, 3);
  for(int cell = 0; cell < 256; cell++) {
    float x = (cell & 15) * 16 + 2 + (cell & 15) / 16.0f, y = (cell >> 4) * 16 + 2 + (cell >> 4) / 16.0f;
    sceGuDrawBezier(FloatVertexType, 4, 4, 0, curvePoints(x, y, 12));
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Splines' vertices: in cell k (4x4 cells of 64 pixels), 5x5 control points (curveGrid's, a fifth row and column a
//quarter past its end, 40 pixels to its unit), with each pair of end types: along u k & 3 and along v k >> 2
//(SPLINE's bits: bit 0 the first end); cut 3 times along u and 2 along v, as points: pixels and colors, or depths.
static void curvesSpline(const char* name, int depths) {
  if(!beginTest(name)) return;
  startCurvePoints();
  sceGuPatchDivide(3, 2);
  for(int k = 0; k < 16; k++) {
    FloatVertex* v = sceGuGetMemory(25 * sizeof(FloatVertex));
    float x0 = (k & 3) * 64 + 6, y0 = (k >> 2) * 64 + 6;
    for(int j = 0; j < 5; j++) {
      for(int i = 0; i < 5; i++) {
        const float* g = curveGrid[j < 4 ? j : 3][i < 4 ? i : 3];
        float x = g[0] + (i == 4) * 0.25f, y = g[1] + (j == 4) * 0.25f;
        v[j * 5 + i] = (FloatVertex){0, 0, curveColor(i, j), x0 + x * 40, y0 + y * 40, curveDepth(i, j)};
      }
    }
    sceGuDrawSpline(FloatVertexType, 5, 5, k & 3, k >> 2, 0, v);
  }
  finishList();
  if(depths) saveDepthInOrder(name);
  else saveTarget(name, 256, 256, 0);
}

//Texture coordinates at the vertices: curvesBezier's cells, textured (texel (x, y) is x | y << 8 | 0x80 << 16,
//replace, nearest, clamped), as points: each takes the texel its u and v (texels, in through mode) fall in.
static void curvesTexels(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  startCurvePoints();
  useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  for(int k = 0; k < 16; k++) {
    sceGuPatchDivide(k + 1, k + 1);
    sceGuDrawBezier(FloatVertexType, 4, 4, 0, curvePoints((k & 3) * 64 + 4, (k >> 2) * 64 + 4, 56));
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Texture coordinates made up for a vertex type without them: in 3D (identity matrices) the texture as curvesTexels',
//its 256 texels across u's 0 to 1 (TEX_SCALE 1), as points in rows 0-127 and as triangles below. In cell k of 4x2
//cells of 64 pixels: a flat Bezier patch (k 0), one of 2x2 patches (7x7 points, 1), of 3x1 (10x4, 2); a 5x5 spline
//with both ends open (3), both closed (4), the first open (5); the first again with TEX_SCALE 0.5 and TEX_OFFSET 0.25
//(6), and in through mode (7); each cut 4 times per patch or piece. The texels tell whether u runs 0 to 1 across the
//surface, across each patch, or otherwise.
static void curvesMadeUp(const char* name) {
  static const int counts[8][2] = {{4, 4}, {7, 7}, {10, 4}, {5, 5}, {5, 5}, {5, 5}, {4, 4}, {4, 4}};
  static const int ends[8] = {0, 0, 0, 3, 0, 1, 0, 0};
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&identity, 1);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuPatchDivide(4, 4);
  for(int half = 0; half < 2; half++) {
    sceGuPatchPrim(half ? GU_TRIANGLE_STRIP : GU_POINTS);
    for(int k = 0; k < 8; k++) {
      int ucount = counts[k][0], vcount = counts[k][1];
      float x0 = (k & 3) * 64 + 4, y0 = half * 128 + (k >> 2) * 64 + 4;
      ColorVertex* v = sceGuGetMemory(ucount * vcount * sizeof(ColorVertex));
      for(int j = 0; j < vcount; j++) {
        for(int i = 0; i < ucount; i++) {
          float x = x0 + 56.0f * i / (ucount - 1), y = y0 + 56.0f * j / (vcount - 1);
          v[j * ucount + i] = (ColorVertex){0xffffffff, k == 7 ? x : ndcX(x), k == 7 ? y : ndcY(y), 0};
        }
      }
      sceGuTexScale(k == 6 ? 0.5f : 1, k == 6 ? 0.5f : 1);
      sceGuTexOffset(k == 6 ? 0.25f : 0, k == 6 ? 0.25f : 0);
      if(k == 7) sceGuDrawBezier(ColorVertexType, ucount, vcount, 0, v);
      else if(k >= 3 && k <= 5) sceGuDrawSpline(ColorVertexType3D, ucount, vcount, ends[k], ends[k], 0, v);
      else sceGuDrawBezier(ColorVertexType3D, ucount, vcount, 0, v);
    }
  }
  sceGuTexScale(1, 1);
  sceGuTexOffset(0, 0);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Normals made from a surface's slopes, for a vertex type without them: lit points in 3D (identity matrices; a white
//directional light and material, diffuse alone), each vertex as bright as the cosine between its normal and the
//light. In cell k of 4x4 cells of 64 pixels, curveGrid 56 pixels square, its middle points 0.25 toward the camera,
//cut 6 times each way: the light along +z, +x, +y and -z (k & 3), sceGuPatchFrontFace GU_CW or GU_CCW (k & 4), and
//(k 8-15) the same with each control point's normal given ((i - 1.5) / 4, (j - 1.5) / 4, 0.8), for comparison.
static void curvesLit(const char* name) {
  static const ScePspFVector3 lights[4] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
  if(!beginTest(name)) return;
  beginLit();
  sceGuDisable(GU_DEPTH_TEST);
  sceGuLightColor(0, GU_DIFFUSE, 0xffffff);
  sceGuModelColor(0, 0, 0xffffff, 0);
  sceGuPatchPrim(GU_POINTS);
  sceGuPatchDivide(6, 6);
  for(int k = 0; k < 16; k++) {
    sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &lights[k & 3]);
    sceGuPatchFrontFace(k & 4 ? GU_CCW : GU_CW);
    float x0 = (k & 3) * 64 + 4, y0 = (k >> 2) * 64 + 4;
    NormalVertex* v = sceGuGetMemory(16 * sizeof(NormalVertex));
    for(int j = 0; j < 4; j++) {
      for(int i = 0; i < 4; i++) {
        float z = (i == 1 || i == 2) && (j == 1 || j == 2) ? 0.25f : 0;
        v[j * 4 + i] = (NormalVertex){(i - 1.5f) / 4, (j - 1.5f) / 4, 0.8f, ndcX(x0 + curveGrid[j][i][0] * 56),
                                      ndcY(y0 + curveGrid[j][i][1] * 56), z};
      }
    }
    if(k < 8) {
      PlainVertex* p = sceGuGetMemory(16 * sizeof(PlainVertex));
      for(int n = 0; n < 16; n++) p[n] = (PlainVertex){v[n].x, v[n].y, v[n].z};
      sceGuDrawBezier(PlainVertexType3D, 4, 4, 0, p);
    } else {
      sceGuDrawBezier(NormalVertexType3D, 4, 4, 0, v);
    }
  }
  sceGuDisable(GU_LIGHTING);
  sceGuDisable(GU_LIGHT0);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Culling: in cell k of 4x4 cells of 64 pixels, a flat white patch as triangles (a strip's first triangle running
//counterclockwise on the screen, x running right along u and y down along v), with GU_CULL_FACE on (k & 1),
//sceGuFrontFace GU_CCW (k & 2; else GU_CW), GU_PATCH_CULL_FACE on (k & 4) and sceGuPatchFrontFace GU_CCW (k & 8;
//else GU_CW): which cells are drawn tells which of the four cull a patch's triangles, and which way round.
static void curvesCulling(const char* name) {
  if(!beginTest(name)) return;
  start3D(&identity, 1);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuPatchPrim(GU_TRIANGLE_STRIP);
  sceGuPatchDivide(3, 3);
  for(int k = 0; k < 16; k++) {
    if(k & 1) sceGuEnable(GU_CULL_FACE);
    else sceGuDisable(GU_CULL_FACE);
    sceGuFrontFace(k & 2 ? GU_CCW : GU_CW);
    if(k & 4) sceGuEnable(GU_PATCH_CULL_FACE);
    else sceGuDisable(GU_PATCH_CULL_FACE);
    sceGuPatchFrontFace(k & 8 ? GU_CCW : GU_CW);
    float x0 = (k & 3) * 64 + 4, y0 = (k >> 2) * 64 + 4;
    ColorVertex* v = sceGuGetMemory(16 * sizeof(ColorVertex));
    for(int j = 0; j < 4; j++) {
      for(int i = 0; i < 4; i++) v[j * 4 + i] = (ColorVertex){0xffffffff, ndcX(x0 + i * 18), ndcY(y0 + j * 18), 0};
    }
    sceGuDrawBezier(ColorVertexType3D, 4, 4, 0, v);
  }
  sceGuDisable(GU_CULL_FACE);
  sceGuDisable(GU_PATCH_CULL_FACE);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Which vertices a surface's strips join across patches, in rows of 16 pixels: a Bezier of 2 patches along u (7x4
//points, rows 8, 10, 12, 14) and a spline of 5x4 with open ends (the others), cut twice per patch or piece (rows
//8-11) or 3 times along u and once along v (rows 12-15), as lines (rows 8, 9, 12, 13) or flat-shaded triangles. (Rows
//0-7 are left empty: these were once in one picture with curves-count's, and kept where they were.)
static void curvesJoins(const char* name) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  for(int row = 8; row < 16; row++) {
    int spline = row & 1, ucount = spline ? 5 : 7;
    sceGuPatchPrim(row & 2 ? GU_TRIANGLE_STRIP : GU_LINE_STRIP);
    sceGuShadeModel(row & 2 ? GU_FLAT : GU_SMOOTH);
    if(row < 12) sceGuPatchDivide(2, 2);
    else sceGuPatchDivide(3, 1);
    ColorVertex* v = sceGuGetMemory(ucount * 4 * sizeof(ColorVertex));
    for(int j = 0; j < 4; j++) {
      for(int i = 0; i < ucount; i++) {
        unsigned int color = 0xff000000u | (40 + i * 30) | (40 + j * 60) << 8 | (row * 12) << 16;
        v[j * ucount + i] = (ColorVertex){color, 8 + i * 220.0f / (ucount - 1), row * 16 + 1 + j * 14 / 3.0f, 0};
      }
    }
    if(spline) sceGuDrawSpline(ColorVertexType, ucount, 4, 3, 3, 0, v);
    else sceGuDrawBezier(ColorVertexType, ucount, 4, 0, v);
  }
  sceGuShadeModel(GU_SMOOTH);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//How many vertices: in row n of 16 pixels (first to last), a flat patch 240 pixels wide (through mode) cut into
//divisions[n] along u and once along v, as points added up (each adds 1 to its pixel: blending with fixed factors),
//its two rows of vertices at y + 4 and y + 12: a row's sum is how many vertices a row of the patch has. Past
//pspsdk's 64 the GE might stall, so those run last, a few divisions to a test (and a display list) each: a test
//that stops the PSP twice is given up on alone (beginTest), and the ones before it are already saved.
static void curvesCount(const char* name, int first, int last) {
  static const int divisions[8] = {16, 63, 64, 65, 100, 128, 200, 255};
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
  sceGuPatchPrim(GU_POINTS);
  for(int row = first; row <= last; row++) {
    ColorVertex* v = sceGuGetMemory(16 * sizeof(ColorVertex));
    for(int j = 0; j < 4; j++) {
      for(int i = 0; i < 4; i++) v[j * 4 + i] = (ColorVertex){0xff010101, 8 + i * 80, row * 16 + 4 + j * 8 / 3.0f, 0};
    }
    sceGuPatchDivide(divisions[row], 1);
    sceGuDrawBezier(ColorVertexType, 4, 4, 0, v);
  }
  sceGuDisable(GU_BLEND);
  finishList();
  saveTarget(name, 256, 256, 0);
}

static void writeManifest4(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest4.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 4 (tools/psp-measure/ge.c in Phobos says what each test draws): each .bin is the\n"
    "target's pixels after one test, a little-endian 32-bit word each, row by row, 256x256; but lines-depth, the\n"
    "depth buffer's 256x256 16-bit values (low half) read through VRAM's fourth copy; dxt1/3/5-colors 256x64 (the\n"
    "texture's colors at the left, white blended by its alpha from x 128); dxt-layout 256x32.\n"
    "lines-*: lines in through mode (lines-3d through the matrices), in 16x16 cells at every sixteenth of a pixel.\n"
    "bbox: 256 cells, each white if the GE took its bounding box to be in sight (the cases are in ge.c).\n"
    "curves-*: curved surfaces (BEZIER, SPLINE), mostly drawn as points, a vertex each: curves-bezier and -spline\n"
    "their pixels and colors, -bezier-depths and -spline-depths the depth buffer's 256x256 values there (low half,\n"
    "through VRAM's fourth copy); -places the vertices' sixteenths, -texels their texture coordinates, -made-up\n"
    "texture coordinates a vertex type lacks, -lit normals made from the slopes, -culling, -joins which\n"
    "vertices strips join, -count how many vertices at 16-65 divisions (-count-128 at 100 and 128, -count-200 and\n"
    "-count-255 at those).\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  sceIoClose(file);
}

static void round4(void) {
  writeManifest4();
  lineCells("lines-shallow", 4, 4, 7.5f, 2.25f, 0, 0);
  lineCells("lines-shallow-reversed", 4, 4, 7.5f, 2.25f, 1, 0);
  lineCells("lines-steep", 4, 4, 2.25f, 7.5f, 0, 0);
  lineCells("lines-steep-reversed", 4, 4, 2.25f, 7.5f, 1, 0);
  lineCells("lines-diagonal", 4, 4, 6, 6, 0, 0);
  lineCells("lines-rising", 4, 10, 7, -5.5f, 0, 0);
  lineCells("lines-level", 4, 6, 7.5f, 0, 0, 0);
  lineCells("lines-upright", 6, 4, 0, 7.5f, 0, 0);
  lineCells("lines-smooth", 4, 4, 7.5f, 2.25f, 0, 1);
  shortLines("lines-short");
  lineStrips("lines-strips");
  lineColors("lines-colors");
  lineDepth("lines-depth");
  lineTexels("lines-texels");
  lines3D("lines-3d");
  boundingBoxes("bbox");
  dxtColors("dxt1-colors", GU_PSM_DXT1);
  dxtColors("dxt3-colors", GU_PSM_DXT3);
  dxtColors("dxt5-colors", GU_PSM_DXT5);
  dxtLayout("dxt-layout");
  curvesBezier("curves-bezier", 0);
  curvesBezier("curves-bezier-depths", 1);
  curvesPlaces("curves-places");
  curvesSpline("curves-spline", 0);
  curvesSpline("curves-spline-depths", 1);
  curvesTexels("curves-texels");
  curvesMadeUp("curves-made-up");
  curvesLit("curves-lit");
  curvesCulling("curves-culling");
  curvesJoins("curves-joins");
  curvesCount("curves-count", 0, 3);
  curvesCount("curves-count-128", 4, 5);
  curvesCount("curves-count-200", 6, 6);
  curvesCount("curves-count-255", 7, 7);
}


//---- round 5: what round 3 left open once part 48 fitted how the GE steps colors, fog and depth (docs/psp-core.md)

//A fixed sequence of numbers, started afresh by each test that takes them (so a test run again draws the same).
static unsigned int probeRandom;
static unsigned int probeNext(void) {
  probeRandom = probeRandom * 1103515245 + 12345;
  return probeRandom >> 8;
}

//-- the steps, on shapes they weren't fitted to

//64 triangles, one in each 32x32 cell of the target (8x8 cells), corners anywhere in the cell to the sixteenth of a
//pixel and colors from the sequence, in through mode: part 48's rule (corner, steps through a 16-bit reciprocal,
//rounded down) on slanting edges, every corner first. With depths too (from the sequence, written always), the
//depth buffer read through VRAM's fourth copy.
static void stepTriangles(FloatVertex* v, int depths) {
  probeRandom = 0x5eed1234;
  for(int cell = 0; cell < 64; cell++) {
    float x = (cell & 7) * 32 + 1, y = (cell >> 3) * 32 + 1;
    for(int k = 0; k < 3; k++) {
      float cx = x + (probeNext() % 480) / 16.0f, cy = y + (probeNext() % 480) / 16.0f;
      unsigned int color = 0xff000000u | (probeNext() & 0xffffff);
      unsigned int depth = probeNext() & 0xffff;
      v[cell * 3 + k] = (FloatVertex){0, 0, color, cx, cy, depths ? (float)depth : 0};
    }
  }
}
static void stepsCheck(const char* name, int depths) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  for(int n = 0; n < Stride * 256; n++) VRAM16[Depth / 2 + n] = 0;
  start(GU_PSM_8888);
  if(depths) {
    sceGuDepthRange(65535, 0);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_ALWAYS);
    sceGuDepthMask(GU_FALSE);
  }
  FloatVertex* v = sceGuGetMemory(64 * 3 * sizeof(FloatVertex));
  stepTriangles(v, depths);
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType, 64 * 3, 0, v);
  finishList();
  if(depths) saveDepthInOrder(name);
  else saveTarget(name, 256, 256, 0);
}

//The near plane's cut with each corner past it in turn, each way round: in band k (rows 42k + 2 to 42k + 38), round
//2's 3d-clip triangle (red and green corners at the top, blue at the bottom middle, identity matrices) squashed into
//the band, its blue corner at z -1.5 (cut two thirds of the way to it). Bands 0-2 list the corners turning as
//3d-clip's do (clockwise on the screen), the blue one first, second and third (band 2 as in 3d-clip); bands 3-5 the
//other way round, blue first, second and third. The four corners left are split into two triangles one way or the
//other, which the colors' steps show.
static void clipSplit(const char* name) {
  if(!beginTest(name)) return;
  start3D(&identity, 1);
  FloatVertex* v = sceGuGetMemory(18 * sizeof(FloatVertex));
  for(int band = 0; band < 6; band++) {
    float top = band * 42;
    FloatVertex red = {0, 0, 0xff0000ff, ndcX(16), ndcY(top + 2), 0};
    FloatVertex green = {0, 0, 0xff00ff00, ndcX(240), ndcY(top + 2), 0};
    FloatVertex blue = {0, 0, 0xffff0000, ndcX(128), ndcY(top + 38), -1.5f};
    FloatVertex* t = v + band * 3;
    if(band == 0) t[0] = blue, t[1] = red, t[2] = green;
    if(band == 1) t[0] = green, t[1] = blue, t[2] = red;
    if(band == 2) t[0] = red, t[1] = green, t[2] = blue;
    if(band == 3) t[0] = blue, t[1] = green, t[2] = red;
    if(band == 4) t[0] = red, t[1] = blue, t[2] = green;
    if(band == 5) t[0] = green, t[1] = red, t[2] = blue;
  }
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 18, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//-- lighting's share

//Lighting's share (round 3: not 256ths after all, part 48): four normals, each in four rows of cells (rows 4j to
//4j + 3 for normal j), white-free products: each row its own light color, each cell its own vertex color standing
//for the material's diffuse, levels 96-255 from the sequence, so that floor(product * share / 1024) at 192 products
//pins each normal's share far finer than a 256th. Light along +z, one long; plain diffuse (kind 0, no ambient).
static const short shareNormals[4][4][3] = {
  {{33, 0, 56}, {16, 0, 63}, {65, 0, 72}, {101, 0, 26}},    //round 3's two odd cells, one as long, round 2's k 101
  {{99, 0, 168}, {48, 0, 189}, {195, 0, 216}, {303, 0, 78}},  //the same, three times as long
  {{60, 0, 67}, {120, 0, 7}, {126, 0, 1}, {113, 0, 14}},    //more of round 2's light-diffuse normals
  {{56, 0, 33}, {63, 0, 16}, {72, 0, 65}, {26, 0, 101}},    //the first four turned: cosines a / c
};
static unsigned int shareLevel(void) { return 96 + probeNext() % 160; }
//Three levels as a color, red first (each taken in a statement of its own, so the order is fixed).
static unsigned int shareColor(void) {
  unsigned int red = shareLevel();
  unsigned int green = shareLevel();
  unsigned int blue = shareLevel();
  return red | green << 8 | blue << 16;
}
static void lightShare(const char* name, int set) {
  if(!beginTest(name)) return;
  probeRandom = 0x51a2e000u + set;
  beginLit();
  ScePspFVector3 toward = {0, 0, 1};
  sceGuLight(0, GU_DIRECTIONAL, GU_AMBIENT_AND_DIFFUSE, &toward);
  sceGuColorMaterial(GU_DIFFUSE);
  unsigned int colors[16];
  Normal normals[16];
  for(int row = 0; row < 16; row++) {
    const short* n = shareNormals[set][row >> 2];
    unsigned int light = shareColor();
    for(int i = 0; i < 16; i++) {
      colors[i] = 0xff000000u | shareColor();
      normals[i] = (Normal){n[0], n[1], n[2]};
    }
    sceGuLightColor(0, GU_DIFFUSE, light);
    litRow(row, colors, normals);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//-- perspective texels, to a texel in thousands

//The texture (texel (x, y) is x | y << 8 | 0x80 << 16) repeating, so that coordinates in the thousands of texels
//show to the texel (each pixel's texel is its coordinates' whole part less a multiple of 256, which a prediction
//within 128 texels tells): the GE's perspective divide and steps many times finer than round 2's floor and round 3's
//wall show them. (Coordinates change by under 128 texels from a pixel to the next, the most at the far end.)
static void useRepeatingTexture(void) {
  useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuTexWrap(GU_REPEAT, GU_REPEAT);
}
//what 0: round 3's wall with u from 0 to 4096 texels (16 in the vertex) across it, v 0 to 256 down; 1: u / w the
//same at all four corners (1024 texels at w 1, 4096 at w 4), so u shows 1 / w alone; 2: no perspective, w 3 at every
//corner (1 / 3 isn't a whole number of any power of two), u 0 to 1024 across, v 0 to 256 down.
static void perspectiveWall(const char* name, int what) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&lens, 1);
  useRepeatingTexture();
  FloatVertex* v = sceGuGetMemory(6 * sizeof(FloatVertex));
  float near = what == 2 ? -3 : -1, far = what == 2 ? -3 : -4, size = what == 2 ? 3 : 1;
  float uLeft = what == 1 ? 4 : 0, uRight = what == 0 ? 16 : what == 1 ? 16 : 4;
  v[0] = (FloatVertex){uLeft, 0, 0xffffffff, -size, size, near};
  v[1] = (FloatVertex){uRight, 0, 0xffffffff, size, size, far};
  v[2] = (FloatVertex){uLeft, 1, 0xffffffff, -size, -size, near};
  v[3] = (FloatVertex){uRight, 0, 0xffffffff, size, size, far};
  v[4] = (FloatVertex){uRight, 1, 0xffffffff, size, -size, far};
  v[5] = (FloatVertex){uLeft, 1, 0xffffffff, -size, -size, near};
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}
//Round 2's floor (3d-floor-texels) with u 0 to 4096 texels across and v 0 to 2048 into the distance.
static void perspectiveFloor(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&lens, 1);
  useRepeatingTexture();
  FloatVertex* v = sceGuGetMemory(6 * sizeof(FloatVertex));
  v[0] = (FloatVertex){0, 0, 0xffffffff, -1, -1, -1};
  v[1] = (FloatVertex){16, 0, 0xffffffff, 1, -1, -1};
  v[2] = (FloatVertex){0, 8, 0xffffffff, -1, -1, -4};
  v[3] = (FloatVertex){16, 0, 0xffffffff, 1, -1, -1};
  v[4] = (FloatVertex){16, 8, 0xffffffff, 1, -1, -4};
  v[5] = (FloatVertex){0, 8, 0xffffffff, -1, -1, -4};
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}
//Round 3's 3D sprite's texels (3d-sprite-texels, from z -1 to -4) with u and v 0 to 2048 texels.
static void perspectiveSprite(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&lens, 1);
  useRepeatingTexture();
  FloatVertex* v = sceGuGetMemory(2 * sizeof(FloatVertex));
  v[0] = (FloatVertex){0, 0, 0xffffffff, -0.9f, 0.9f, -1};
  v[1] = (FloatVertex){8, 8, 0xffffffff, 0.9f, -0.8f, -4};
  sceGuDrawArray(GU_SPRITES, FloatVertexType3D, 2, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

static void writeManifest5(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest5.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 5 (tools/psp-measure/ge.c in Phobos says what each test draws): each .bin is the\n"
    "target's pixels after one test, a little-endian 32-bit word each, row by row, 256x256; but steps-depth-check,\n"
    "the depth buffer's 256x256 16-bit values (low half) read through VRAM's fourth copy.\n"
    "steps-check, steps-depth-check: 64 random triangles in through mode, their colors (or depths) stepped.\n"
    "clip-split: the near plane's cut with the first, second and third corner past it, each way round (6 bands).\n"
    "light-share-0 to -3: four normals each, 192 light-times-material products per normal.\n"
    "persp-*: perspective texels with a repeating texture and coordinates up to 4096 texels: persp-wall (round 3's\n"
    "wall), persp-divide (u / w the same at every corner), persp-w3 (w 3 everywhere), "
    "persp-floor (round 2's floor),\n"
    "persp-sprite (round 3's 3D sprite).\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  sceIoClose(file);
}

static void round5(void) {
  writeManifest5();
  stepsCheck("steps-check", 0);
  stepsCheck("steps-depth-check", 1);
  clipSplit("clip-split");
  for(int set = 0; set < 4; set++) {
    char name[32];
    snprintf(name, sizeof(name), "light-share-%d", set);
    lightShare(name, set);
  }
  perspectiveWall("persp-wall", 0);
  perspectiveWall("persp-divide", 1);
  perspectiveWall("persp-w3", 2);
  perspectiveFloor("persp-floor");
  perspectiveSprite("persp-sprite");
}

//---- the rounds

static int guReady;  //whether sceGuInit has set the GE up: once, on the first round, kept until geEnd

//Round 2, the GE's first tests, and the controller's timing.
static void round2(void) {
  writeManifest();
  blend("blend-source", colorX_alphaY, zero, GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0);
  blend("blend-destination", black_alphaY, colorX, GU_ADD, GU_FIX, GU_SRC_ALPHA, 0, 0);
  blend("blend-inverse", black_alphaY, colorX, GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  blend("blend-double", colorX_alphaY, zero, GU_ADD, GU_DOUBLE_SRC_ALPHA, GU_FIX, 0, 0);
  blend("blend-over", colorX_alphaY, constant, GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
  blend("blend-subtract", colorX_alphaY, constant, GU_SUBTRACT, GU_SRC_ALPHA, GU_FIX, 0, 0x404040);
  blend("blend-reverse", colorX_alphaY, constant, GU_REVERSE_SUBTRACT, GU_SRC_ALPHA, GU_FIX, 0, 0x404040);
  blend("blend-min", colorX_alphaY, grayY, GU_MIN, GU_FIX, GU_FIX, 0, 0);
  blend("blend-max", colorX_alphaY, grayY, GU_MAX, GU_FIX, GU_FIX, 0, 0);
  blend("blend-abs", colorX_alphaY, grayY, GU_ABS, GU_FIX, GU_FIX, 0, 0);
  blend("blend-fixed", colorX_alphaY, grayY, GU_ADD, GU_FIX, GU_FIX, 0x80c040, 0x2001ff);
  blend("blend-factor-11", colorX_alphaY, grayY, GU_ADD, 11, 12, 0x80c040, 0x2001ff);
  blend("blend-other-color", colorX_alphaY, grayY, GU_ADD, GU_OTHER_COLOR, GU_ONE_MINUS_OTHER_COLOR, 0, 0);
  blend("blend-destination-alpha", colorX_alphaY, grayY_alphaY, GU_ADD, GU_DST_ALPHA, GU_FIX, 0, 0);
  texfunc("texfunc-modulate", colorX_alphaX, gray, GU_TFX_MODULATE, GU_TCC_RGB, 0, 0, 0);
  texfunc("texfunc-modulate-alpha", white_alphaX, whiteAlpha, GU_TFX_MODULATE, GU_TCC_RGBA, 0, 0, 1);
  texfunc("texfunc-decal", colorX_alphaX, gray, GU_TFX_DECAL, GU_TCC_RGBA, 0, 0, 0);
  texfunc("texfunc-blend", colorX_alphaX, gray, GU_TFX_BLEND, GU_TCC_RGB, 0, 0x40c080, 0);
  texfunc("texfunc-add", colorX_alphaX, gray, GU_TFX_ADD, GU_TCC_RGB, 0, 0, 0);
  texfunc("texfunc-modulate-2x", colorX_alphaX, gray, GU_TFX_MODULATE, GU_TCC_RGB, 1, 0, 0);
  texfunc("texfunc-decal-2x", colorX_alphaX, gray, GU_TFX_DECAL, GU_TCC_RGBA, 1, 0, 0);
  texfunc("texfunc-replace-2x", colorX_alphaX, gray, GU_TFX_REPLACE, GU_TCC_RGB, 1, 0, 0);
  filter("filter-magnify", 64, 0);
  filter("filter-magnify-clamp", 64, 1);
  filter("filter-magnify-37", 37, 0);
  filter("filter-shrink", 2, 0);
  coverage("coverage-sprites", 0);
  coverage("coverage-triangles", 1);
  sharedEdges("shared-edges");
  spriteCorners("sprite-corners");
  dither("dither-8888", GU_PSM_8888);
  dither("dither-5650", GU_PSM_5650);
  stencil("stencil-4444-increment", GU_PSM_4444, GU_INCR);
  stencil("stencil-4444-decrement", GU_PSM_4444, GU_DECR);
  stencil("stencil-5551-increment", GU_PSM_5551, GU_INCR);
  stencil("stencil-5551-decrement", GU_PSM_5551, GU_DECR);
  textureFormat("texture-5650", GU_PSM_5650, 0);
  textureFormat("texture-5551", GU_PSM_5551, 0);
  textureFormat("texture-4444", GU_PSM_4444, 0);
  textureFormat("texture-5551-alpha", GU_PSM_5551, 1);
  textureFormat("texture-4444-alpha", GU_PSM_4444, 1);
  narrow("narrow-5650", GU_PSM_5650);
  narrow("narrow-5551", GU_PSM_5551);
  narrow("narrow-4444", GU_PSM_4444);
  gouraud("gouraud");
  texelMapping("texels-sprite-shrunk", 0, 240, 256);
  texelMapping("texels-triangles-shrunk", 1, 240, 256);
  texelMapping("texels-sprite-stretched", 0, 256, 200);
  texelMapping("texels-triangles-stretched", 1, 256, 200);
  floor3D("3d-floor-texels", 0);
  floor3D("3d-floor-depth", 1);
  floor3D("3d-floor-fog", 2);
  sprite3D("3d-sprite");
  rounding3D("3d-rounding");
  clip3D("3d-clip", 1);
  clip3D("3d-clip-unclamped", 0);
  rules3D("3d-rules");
  cull3D("3d-cull");
  lightDiffuse("light-diffuse");
  lightSpecular("light-specular");
  lightSpot("light-spot");
  lightPoint("light-point");
  lightEnvironment("light-environment");
  controllerTiming("controller-timing");
}

//A round of the GE's tests (2 to 5) into results/ge. Returns 0 if something couldn't be written.
int geRound(int round) {
  if(round < 2 || round > 5) return 1;
  if(!guReady) {
    sceGuInit();
    guReady = 1;
  }
  useFolder("ge");
  failed = 0;
  if(round == 2) round2();
  else if(round == 3) round3();
  else if(round == 4) round4();
  else round5();
  return !failed;
}

void geEnd(void) {
  if(guReady) sceGuTerm();
}
