//psp-measure's GE and controller tests: what a real PSP's GE draws, and how its controller driver times its reads, in
//the cases where Phobos's PSP core follows PPSSPP's software renderer or uOFW's reading of the firmware rather than
//measurements of its own (see docs/psp-core.md, parts 8 to 12): blending's and the texture functions' rounding, the
//texture filter's weights, which pixels sprites and triangles cover, the sprite corners' quarter turn, dithering, the
//stencil's steps, and whether the controller's reads wait; in 3D, perspective-correct texels, the depths and fog
//written, the GE's rounding onto the screen, the cut at the near plane, which depths stop a primitive, and culling;
//and lighting: diffuse and the shine across the angles, a spotlight's cone, a point light's fading, environment
//mapping. These are round 2's (the first the GE had), picked from the menu (main.c).
//
//Each test draws into VRAM (away from the text on the screen), reads the pixels back as they are and writes them to
//results/ge/<test>.bin: little-endian 32-bit words, one per pixel, row by row (a 16-bit frame buffer's pixels in the
//low half). manifest.txt says what each test drew. A test whose file is there is skipped, and one that stops the PSP
//is given up on, as results.c has it. The program computes nothing: the host runs the same program in Phobos's core
//(tests/psp/measure.cpp) and compares the files.

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

//---- the rounds

static int guReady;  //whether sceGuInit has set the GE up: once, on the first round, kept until geEnd

//A round of the GE's tests into results/ge (so far only round 2, the first). Returns 0 if something couldn't be
//written.
int geRound(int round) {
  if(round != 2) return 1;
  if(!guReady) {
    sceGuInit();
    guReady = 1;
  }
  useFolder("ge");
  failed = 0;
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
  return !failed;
}

void geEnd(void) {
  if(guReady) sceGuTerm();
}
