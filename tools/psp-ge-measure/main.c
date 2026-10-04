//psp-ge-measure: records what a real PSP's GE draws, and how its controller driver times its reads, in the cases where
//Phobos's PSP core follows PPSSPP's software renderer or uOFW's reading of the firmware rather than measurements of
//its own (see docs/psp-core.md, parts 8 to 10): blending's and the texture functions' rounding, the texture filter's
//weights, which pixels sprites and triangles cover, the sprite corners' quarter turn, dithering, the stencil's steps,
//and whether the controller's reads wait.
//
//Each test draws into VRAM (away from the text on the screen), reads the pixels back as they are and writes them to
//results/<test>.bin beside EBOOT.PBP: little-endian 32-bit words, one per pixel, row by row (a 16-bit frame buffer's
//pixels in the low half). manifest.txt says what each test drew. The program computes nothing: the host runs the
//same program in Phobos's core (tests/psp/measure.cpp) and compares the files.
//
//Build with pspdev's toolchain (https://github.com/pspdev/pspdev): make, which gives EBOOT.PBP. Run: copy it to a
//folder under PSP/GAME on the memory stick (say PSP/GAME/GEMEASURE), start it from the XMB (custom firmware that runs
//homebrew), press X. It takes a few seconds and writes about 13 MB.

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <psputils.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("GEMEASURE", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define print pspDebugScreenPrintf

//make SMOKE=1 builds a version for an emulator (PPSSPPHeadless has no buttons): it starts at once and leaves when
//done.
#ifdef SMOKE
enum { Smoke = 1 };
#else
enum { Smoke = 0 };
#endif

//VRAM: the text on the screen at its start (512x272, 32-bit); the tests draw into Target (512 pixels to a row, 256
//rows) and keep depth at Depth.
enum { Target = 0x100000, Depth = 0x180000, Stride = 512 };
#define VRAM ((volatile unsigned int*)0x44000000)  //VRAM, uncached: the CPU sees what the GE wrote
#define VRAM16 ((volatile unsigned short*)0x44000000)

static unsigned int __attribute__((aligned(16))) list[262144];
static unsigned int __attribute__((aligned(16))) texture[256 * 256];
static unsigned short __attribute__((aligned(16))) texture16[256 * 256];
static unsigned int pixels[256 * 256];
static char folder[256];

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

static void writeFile(const char* name, const void* data, int bytes) {
  char path[320];
  snprintf(path, sizeof(path), "%s/%s.bin", folder, name);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0 || sceIoWrite(file, data, bytes) != bytes) {
    print("%-22s can't write %s\n", name, path);
    failed = 1;
  }
  if(file >= 0) sceIoClose(file);
}

//The first width x height pixels of the target, as words.
static void saveTarget(const char* name, int width, int height, int sixteenBits) {
  for(int y = 0; y < height; y++) {
    for(int x = 0; x < width; x++) {
      pixels[y * width + x] = sixteenBits ? VRAM16[Target / 2 + y * Stride + x] : VRAM[Target / 4 + y * Stride + x];
    }
  }
  writeFile(name, pixels, width * height * 4);
  print("%-22s done\n", name);
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
  sceGuPixelMask(0);
  sceGuShadeModel(GU_SMOOTH);
}

static void finish(void) {
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
  fillTexture(texel);
  fillTarget(under, 0);
  start(GU_PSM_8888);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(op, src, dst, fixA, fixB);
  textureSquare();
  finish();
  saveTarget(name, 256, 256, 0);
}

//Texture functions: 256 rows over the texture's first row, row y's color color(y).
static void texfunc(const char* name, unsigned int (*texel)(int, int), unsigned int (*color)(int), int tfx, int tcc,
                    int doubled, unsigned int environment, int blendAlpha) {
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
  finish();
  saveTarget(name, 256, 256, 0);
}

//The filter: a 4x4 texture of distinct texels drawn over size x size pixels (bigger than 4: magnified, smaller:
//shrunk), the filter for that direction linear, the other nearest; repeating or clamping. Saves 64x64 pixels.
static void filter(const char* name, int size, int clamp) {
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
  finish();
  saveTarget(name, 64, 64, 0);
}

//Coverage: 256 cells of 16x16 pixels; in cell (i, j) a white shape whose corners sit i and j sixteenths of a pixel
//past whole pixels.
static void coverage(const char* name, int triangles) {
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
  finish();
  saveTarget(name, 256, 256, 0);
}

//Shared edges: in cell (i, j) a 12x12 square of two triangles meeting on a diagonal moved i sixteenths, added up, so
//a pixel drawn twice shows 2.
static void sharedEdges(void) {
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
  finish();
  saveTarget("shared-edges", 256, 256, 0);
}

//Sprite corners: a 4x4 texture drawn 1:1 with its two corners each way round: top-left to bottom-right, bottom-right
//to top-left, bottom-left to top-right, top-right to bottom-left (one per 8x8 cell along the top).
static void spriteCorners(void) {
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
  finish();
  saveTarget("sprite-corners", 32, 8, 0);
}

//The 16-bit texture formats: all 65536 texels (texel (x, y) is y * 256 + x) drawn 1:1, as replace gives their color;
//or, with alphaOnly, their alpha: added to the vertices' white (which stays white), then blended over black by it.
static void textureFormat(const char* name, int psm, int alphaOnly) {
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
  finish();
  saveTarget(name, 256, 256, 0);
}

//The 16-bit frame buffer formats: the texture (color from x, alpha from y) drawn 1:1 with replace over zeros, not
//dithered: how 8-bit channels become 5, 6 or 4 bits, and what the alpha (stencil) bits get with the stencil test off.
static void narrow(const char* name, int psm) {
  fillTexture(colorX_alphaY);
  fillTarget(zero, 1);
  start(psm);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGBA);
  textureSquare();
  finish();
  saveTarget(name, 256, 256, 1);
}

//Colors across triangles: a square of two triangles whose corners are black (top left), red (top right), green
//(bottom left) and blue (bottom right).
static void gouraud(void) {
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
  finish();
  saveTarget("gouraud", 256, 256, 0);
}

//Which texel each pixel takes, nearest: the texture (texel (x, y) is x | y << 8 | 0x80 << 16) from texel (0, 0) to
//(texels, texels) over size x size pixels, as a sprite or as a square of two triangles.
static unsigned int texelXY(int x, int y) { return x | y << 8 | 0x80 << 16; }
static void texelMapping(const char* name, int triangles, int size, int texels) {
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
  finish();
  saveTarget(name, 256, 256, 0);
}

//Dithering (pspsdk's matrix, which sceGuStart sets): a gray ramp (row y gray y) drawn with dithering on, in 8888 and
//in 5650.
static void dither(const char* name, int psm) {
  fillTexture(grayY_alphaY);
  fillTarget(zero, psm != GU_PSM_8888);
  start(psm);
  useTexture(GU_TFX_REPLACE, GU_TCC_RGB);
  sceGuEnable(GU_DITHER);
  textureSquare();
  finish();
  saveTarget(name, 256, 256, psm != GU_PSM_8888);
}

//The stencil: a frame buffer whose stencil (alpha) is x's top bits, drawn over with the stencil test always passing
//and an operation on passing, in a 16-bit format.
static unsigned int stencil4444(int x, int y) { (void)y; return (x >> 4) << 12 | 0x123; }
static unsigned int stencil5551(int x, int y) { (void)y; return (x >> 7) << 15 | 0x123; }
static void stencil(const char* name, int psm, int operation) {
  fillTarget(psm == GU_PSM_4444 ? stencil4444 : stencil5551, 1);
  start(psm);
  sceGuEnable(GU_STENCIL_TEST);
  sceGuStencilFunc(GU_ALWAYS, 0x55, 0xff);
  sceGuStencilOp(GU_KEEP, GU_KEEP, operation);
  sprite(0, 0, 256, 256, 0, 0, 0, 0, 0xff336699);
  finish();
  saveTarget(name, 256, 256, 1);
}

//The controller's timing, in microseconds: 16 times each, how long a second sceCtrlReadLatch right after one takes;
//how long sceCtrlReadBufferPositive takes just after a vertical blank; how long a second sceCtrlReadBufferPositive
//right after one takes. (A wait is about a frame, 16683.)
static void controllerTiming(void) {
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
  writeFile("controller-timing", pixels, 48 * 4);
  print("%-22s done\n", "controller-timing");
}

static void writeManifest(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-ge-measure (tools/psp-ge-measure/main.c in Phobos says what each test draws): each .bin but\n"
    "controller-timing.bin is the target's pixels after one test, a little-endian 32-bit word each, row by row\n"
    "(256x256 but filter-* 64x64 and sprite-corners 32x8; the stencil-*, narrow-* and dither-5650 frame buffers are\n"
    "16-bit, in the low half). spread(x) = x | (255 - x) << 8 | (x * 7 & 0xff) << 16. Textures are 8888 (texture-*:\n"
    "16-bit, texel y * 256 + x), nearest, clamped; blends draw the texture with replace and its alpha.\n"
    "controller-timing.bin: 48 times in microseconds: 16 second sceCtrlReadLatch calls, 16 sceCtrlReadBufferPositive\n"
    "calls just after a vertical blank, 16 second sceCtrlReadBufferPositive calls.\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  sceIoClose(file);
}

int main(int argc, char** argv) {
  pspDebugScreenInit();
  print("psp-ge-measure: what this PSP's GE draws, and its controller's timing\n\n");
  SceCtrlData pad;
  if(!Smoke) {
    print("Press X to start (about 13 MB is written).\n\n");
    do sceCtrlReadBufferPositive(&pad, 1); while(!(pad.Buttons & PSP_CTRL_CROSS));
  }
  strncpy(folder, argc > 0 ? argv[0] : "ms0:/PSP/GAME/GEMEASURE/EBOOT.PBP", sizeof(folder) - 16);
  char* slash = strrchr(folder, '/');
  if(slash) *slash = 0;
  strcat(folder, "/results");
  sceIoMkdir(folder, 0777);
  print("writing to %s\n\n", folder);
  writeManifest();

  sceGuInit();
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
  sharedEdges();
  spriteCorners();
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
  gouraud();
  texelMapping("texels-sprite-shrunk", 0, 240, 256);
  texelMapping("texels-triangles-shrunk", 1, 240, 256);
  texelMapping("texels-sprite-stretched", 0, 256, 200);
  texelMapping("texels-triangles-stretched", 1, 256, 200);
  sceGuTerm();
  controllerTiming();

  print(failed ? "\nSomething couldn't be written. Press X to leave.\n" : "\nAll done. Press X to leave.\n");
  if(!Smoke) {
    do sceCtrlReadBufferPositive(&pad, 1); while(pad.Buttons & PSP_CTRL_CROSS);
    do sceCtrlReadBufferPositive(&pad, 1); while(!(pad.Buttons & PSP_CTRL_CROSS));
  }
  sceKernelExitGame();
  return 0;
}
