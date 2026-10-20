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
//perspective texels to the texel in thousands. Round 6 takes what parts 60 and 64 left open: how long the GE takes to
//draw (lists of known work timed), lighting's cosine (the products in full, normals swept), the reciprocal's chords
//read off texels, what PATCH_CULL_ENABLE does, patch divisions past 64 along v, and the driver's statuses while the
//GE draws. Round 7 takes anti-aliased lines past round 4's one case (part 78): every octant, the vertices' alpha,
//blending over a known background, textures, 3D. Round 8 takes the GE's time again where the owner's captures show
//part 76's model of round 6 missing (part 107): big textures in strips, the other texture cases, a pixel's tests and
//blending, medium primitives, each part of a vertex, state between draws and CALLs, lists back to back and stall
//updates, narrow transfers, clears, the two ends' overlap, lists like the captured scenes', and the CPU beside the
//GE (with cpu.c's tests of the CPU alone). Round 9 takes what the fit of rounds 6 and 8 still guesses (part 110):
//triangles reading narrow textures, a texture's loads and the cache's reuse, vertex formats, time-lists again with its
//fix, and anchors from round 8. Each round is picked from the menu (main.c).
//
//Each test draws into VRAM (away from the text on the screen), reads the pixels back as they are and writes them to
//results/ge/<test>.bin: little-endian 32-bit words, one per pixel, row by row (a 16-bit frame buffer's pixels in the
//low half); the timing tests of rounds 6, 8 and 9 write times instead. manifest.txt (round 2), manifest3.txt to
//manifest9.txt (rounds 3-9) say what each test drew. A test whose file is there is skipped, and one that stops the PSP
//is given up on, as results.c has it. A list the GE hasn't finished after 5 seconds is a stall: the GE is reset,
//<test>.stalled says so, and the round goes on (waitForGe). The program computes nothing: the host runs the same
//program in Phobos's core (tests/psp/measure.cpp) and compares the files.

#include "measure.h"
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspintrman.h>
#include <psppower.h>
#include <psputils.h>
#include <stdio.h>
#include <string.h>

//VRAM: the text on the screen at its start (512x272, 32-bit); the tests draw into Target (512 pixels to a row, 256
//rows) and keep depth at Depth.
enum { Target = 0x100000, Depth = 0x180000, Stride = 512 };
#define VRAM ((volatile unsigned int*)0x44000000)  //VRAM, uncached: the CPU sees what the GE wrote
#define VRAM16 ((volatile unsigned short*)0x44000000)

static unsigned int __attribute__((aligned(16))) list[262144];
static unsigned int __attribute__((aligned(1024))) texture[256 * 256];  //(1 KiB boundaries since round 10b: part 112)
static unsigned short __attribute__((aligned(1024))) texture16[256 * 256];
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
//off but the scissor (the target's 512x256). setUp is that state alone, in whichever list GU is building.
static void setUp(int psm);
static void start(int psm) {
  sceGuStart(GU_DIRECT, list);
  setUp(psm);
}
static void setUp(int psm) {
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

//Adds text to a test's <name>.stalled (noteStall: the running GE test's).
static void noteStallIn(const Output* out, const char* text) {
  SceUID file = sceIoOpen(out->stalled, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
  if(file < 0) return;
  sceIoWrite(file, text, strlen(text));
  sceIoClose(file);
}
static void noteStall(const char* text) { noteStallIn(&current, text); }

//Waits for the GE to have drawn every list queued, for Patience at most, and returns 1 if it has. If it hasn't, the
//GE stalled: <name>.stalled says so, and what the list held (what), before anything is done about it, so that if
//that stops the PSP the next start gives up on the test at once (results.c). Then the lists are thrown away and the
//GE reset (sceGuBreak's cancel, sceGeBreak(1): the reset pspsdk offers a program), and the GU library started
//afresh (sceGuInit's own list puts back the GE's first state), so the tests after draw as before; the test goes on,
//saving what the GE had drawn.
static void resetGe(unsigned int waited, int state, const char* what);
static int waitForGe(const char* what) {
  unsigned int began = sceKernelGetSystemTimeLow(), waited = 0;
  int state;
  while((state = sceGeDrawSync(1)) > 0) {  //1 looks without waiting: 0 once every list is done
    waited = sceKernelGetSystemTimeLow() - began;
    if(waited >= Patience) break;
    sceKernelDelayThread(1000);
  }
  if(state <= 0) return 1;
  resetGe(waited, state, what);
  return 0;
}

//The stall's record and the reset, for the test `out` is running (cpu.c's too), after waiting `waited` microseconds
//with `look` still giving state.
void geReset(const Output* out, unsigned int waited, int state, const char* look, const char* what) {
  char text[320];
  snprintf(text, sizeof(text), "The GE hadn't finished after %u ms (%s gave %d: 2 drawing, 3 at a "
           "stall address).\nIts list: %s.\n", waited / 1000, look, state, what ? what : "see ge.c");
  noteStallIn(out, text);
  pspDebugScreenSetXY(35, out->line);  //past "running", and the "done" that replaces it
  print("the GE stalled: resetting");
  unsigned int broken = sceGuBreak(GU_BREAK_CANCEL);
  snprintf(text, sizeof(text), "Reset: sceGuBreak(GU_BREAK_CANCEL) gave 0x%08x", broken);
  noteStallIn(out, text);  //before sceGuInit, which waits for its own list: if the GE is still stuck, it waits for good
  sceGuTerm();
  unsigned int started = sceGuInit();
  snprintf(text, sizeof(text), ", sceGuInit 0x%08x. The .bin is what the GE had drawn.\n", started);
  noteStallIn(out, text);
  pspDebugScreenSetXY(35, out->line);
  print("the GE stalled: reset    ");
}
static void resetGe(unsigned int waited, int state, const char* what) {
  geReset(&current, waited, state, "sceGeDrawSync(1)", what);
}

//Ends the list and waits for the GE to have drawn it (waitForGe, what saying what the list holds); returns 0 if the GE
//stalled.
static int finishListOf(const char* what) {
  sceGuFinish();
  return waitForGe(what);
}
static void finishList(void) { finishListOf(0); }

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
//writing its depth; DEPTH_CLIP_ENABLE (GU_CLIP_PLANES) as asked. start3D clears the target and its depths first;
//setUp3D is the 3D state alone, after setUp.
static void setUp3D(ScePspFMatrix4* projection, int depthClamp);
static void begin3D(ScePspFMatrix4* projection, int depthClamp) {
  start(GU_PSM_8888);
  setUp3D(projection, depthClamp);
}
static void setUp3D(ScePspFMatrix4* projection, int depthClamp) {
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

//How many vertices: in row n of 16 pixels, a flat patch 240 pixels wide (through mode) cut into some divisions along
//u and once along v, as points added up (each adds 1 to its pixel: blending with fixed factors), its two rows of
//vertices at y + 4 and y + 12: a row's sum is how many vertices a row of the patch has.
static void startCount(void) {
  start(GU_PSM_8888);
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
  sceGuPatchPrim(GU_POINTS);
}
static void countRow(int row, int divisions) {
  ColorVertex* v = sceGuGetMemory(16 * sizeof(ColorVertex));
  for(int j = 0; j < 4; j++) {
    for(int i = 0; i < 4; i++) v[j * 4 + i] = (ColorVertex){0xff010101, 8 + i * 80, row * 16 + 4 + j * 8 / 3.0f, 0};
  }
  sceGuPatchDivide(divisions, 1);
  sceGuDrawBezier(ColorVertexType, 4, 4, 0, v);
}

//The counts, in rows 0 to 7 of their pictures, and their tests: 16, 63 and 64 in one, and past pspsdk's 64 one to a
//test, last in the round. The GE stalled there on the user's PSP (when the program still waited for it as long as it
//took, which froze the PSP): one that stalls now is reset (waitForGe) and its test saved as the GE left it, beside
//<name>.stalled, and the others are drawn each in a list of its own.
static const int countDivisions[8] = {16, 63, 64, 65, 100, 128, 200, 255};
static const struct { const char* name; int first, last; } countTests[] = {
  {"curves-count", 0, 2}, {"curves-count-65", 3, 3}, {"curves-count-100", 4, 4}, {"curves-count-128", 5, 5},
  {"curves-count-200", 6, 6}, {"curves-count-255", 7, 7},
};
enum { CountTests = sizeof(countTests) / sizeof(countTests[0]) };

static void curvesCount(const char* name, int first, int last) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  startCount();
  char what[128];
  int length = snprintf(what, sizeof(what), "Bezier patches of 4x4 points as points, 1 division along v, along u");
  for(int row = first; row <= last; row++) {
    countRow(row, countDivisions[row]);
    if(length < (int)sizeof(what)) length += snprintf(what + length, sizeof(what) - length, " %d", countDivisions[row]);
  }
  sceGuDisable(GU_BLEND);
  finishListOf(what);
  saveTarget(name, 256, 256, 0);
}

//The counts' tests as an earlier version of this program left them: a stall froze the PSP there, so what that left
//(<name>.part, .again, .stopped) would have them run once more at most, or not at all. It's cleared, once in a
//results folder (curves-count.timed marks that; if it can't be written, nothing is cleared), so they run with the
//wait timed. Finished ones (.bin) are kept.
static void countsAfresh(void) {
  static const char* kinds[] = {"part", "again", "stopped"};
  char path[320];
  snprintf(path, sizeof(path), "%s/curves-count.timed", folder);
  if(exists(path) || !mark(path)) return;
  for(int test = 0; test < CountTests; test++) {
    for(int kind = 0; kind < 3; kind++) {
      snprintf(path, sizeof(path), "%s/%s.%s", folder, countTests[test].name, kinds[kind]);
      sceIoRemove(path);
    }
  }
}

//The timed wait itself, as a stall has it: a list left waiting at its stall address (its start, never moved on), so
//the GE doesn't finish it and is reset (stall-check.stalled says so). Around it, 16 divisions' count in row 0 before
//the reset and in row 1 after, which should be alike.
static void stallCheck(const char* name) {
  static unsigned int __attribute__((aligned(16))) waiting[2];
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  startCount();
  countRow(0, 16);
  sceGuDisable(GU_BLEND);
  finishList();
  waiting[0] = 15 << 24;  //FINISH
  waiting[1] = 12 << 24;  //END
  sceKernelDcacheWritebackAll();
  int queued = sceGeListEnQueue(waiting, waiting, -1, 0);
  if(queued >= 0) waitForGe("a FINISH and an END, left waiting at its stall address, on purpose");
  else {
    char text[96];
    snprintf(text, sizeof(text), "sceGeListEnQueue gave 0x%08x: nothing was left waiting, nor reset.\n",
             (unsigned int)queued);
    noteStall(text);
  }
  startCount();
  countRow(1, 16);
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
    "vertices strips join, -count how many vertices at 16, 63 and 64 divisions (rows 0-2), -count-65, -100, -128,\n"
    "-200 and -255 at that many (rows 3-7), one to a test.\n"
    "stall-check: a list left at its stall address, so the GE doesn't finish it and is reset as after a stall;\n"
    "16 divisions' count drawn before the reset (row 0) and after it (row 1).\n"
    "<name>.stalled: the GE hadn't finished the test's list after 5 s and was reset: what the list held, how long it\n"
    "waited, what the reset gave. Its .bin is what the GE had drawn.\n"
    "curves-count.timed: the counts' runs left by the program before its GE waits were timed were cleared.\n"
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
  stallCheck("stall-check");
  countsAfresh();
  for(int test = 0; test < CountTests; test++) {
    curvesCount(countTests[test].name, countTests[test].first, countTests[test].last);
  }
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
    "<name>.stalled: the GE hadn't finished the test's list after 5 s and was reset (manifest4.txt says more).\n"
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

//---- round 6: what parts 60 and 64 left open (docs/psp-core.md, part 66): the GE's drawing time, lighting's cosine,
//the reciprocal's chords, what PATCH_CULL_ENABLE does, patch divisions past 64 along v, the driver's statuses

//Vertices too many for a list (2 MiB): lit points, cells, the timing tests' primitives, a block transfer's RAM.
static unsigned int __attribute__((aligned(1024))) big[524288];
//A list to time, built with GU_SEND or word by word (through the uncached copy of RAM, as GU writes its lists).
static unsigned int __attribute__((aligned(1024))) timed[262144];
#define TIMED ((unsigned int*)((unsigned int)timed | 0x40000000))
//Texels of the timing tests' CLUT4, CLUT8 and DXT textures, and a palette of 256 8888 colors.
static unsigned char __attribute__((aligned(1024))) bytes8[256 * 256];
static unsigned int __attribute__((aligned(1024))) palette[256];
//VRAM the text and the target leave free: 256 KiB for a texture, a transfer's source, vertices.
enum { Spare = 0xc0000 };

//A projection that keeps x and y and makes w the model's -z, with z 0 (never past the near or far plane): a vertex's
//w is whatever its z says, and x / w and y / w put it on the screen.
static ScePspFMatrix4 wOnly = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, -1}, {0, 0, 0, 0}};

//-- lighting's cosine (part 60): round 5's light-share-* fit no single share per normal times the products (the
//share each product allows doesn't overlap the others' for 15 of its 16 normals), so the products are taken in
//full at a few normals, and the normals swept with products as light-share-* chose them

//Every pixel of the target a lit point in 3D (identity matrices): big holds them row by row (256 x 256 LitVertex,
//each at its pixel's middle, its color standing for the material's diffuse); row y is lit by rowLights[y] lights
//alike (directional, toward `toward`, kind 0, their ambient black) of diffuse color rowLight[y]. The material's other
//colors and the ambient light are black, as beginLit leaves them.
static unsigned int rowLight[256];
static unsigned char rowLights[256];
static void litPoint(int x, int y, unsigned int color, const float* n) {
  ((LitVertex*)big)[y * 256 + x] =
    (LitVertex){0xff000000u | color, n[0], n[1], n[2], ndcX(x + 0.5f), ndcY(y + 0.5f), 0};
}
static void drawLitPoints(const char* name, const ScePspFVector3* toward) {
  sceKernelDcacheWritebackAll();
  beginLit();
  sceGuColorMaterial(GU_DIFFUSE);
  for(int k = 0; k < 4; k++) {
    sceGuLight(k, GU_DIRECTIONAL, GU_AMBIENT_AND_DIFFUSE, toward);
    sceGuLightAtt(k, 1, 0, 0);
    sceGuLightColor(k, GU_AMBIENT, 0);
    sceGuLightColor(k, GU_SPECULAR, 0);
  }
  int on = 1;
  for(int y = 0; y < 256; y++) {
    while(on < rowLights[y]) sceGuEnable(GU_LIGHT0 + on++);
    while(on > rowLights[y]) sceGuDisable(GU_LIGHT0 + --on);
    for(int k = 0; k < on; k++) sceGuLightColor(k, GU_DIFFUSE, rowLight[y]);
    sceGuDrawArray(GU_POINTS, LitVertexType, 256, 0, (LitVertex*)big + y * 256);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The products in full, at one normal (light along +z, one long): row y's light color (y, 255 - y, y) on column x's
//material (x, x, 255 - x), so red has every light level on every material level, green and blue the same turned.
//Normal (0, 0, 1) (the cosine 1), then three of light-share-0's: (33, 0, 56), (65, 0, 72), (101, 0, 26).
static const short productNormals[4][3] = {{0, 0, 1}, {33, 0, 56}, {65, 0, 72}, {101, 0, 26}};
static void lightProducts(const char* name, int set) {
  if(!beginTest(name)) return;
  float n[3] = {productNormals[set][0], productNormals[set][1], productNormals[set][2]};
  for(int y = 0; y < 256; y++) {
    rowLight[y] = y | (255 - y) << 8 | y << 16;
    rowLights[y] = 1;
    for(int x = 0; x < 256; x++) litPoint(x, y, x | x << 8 | (255 - x) << 16, n);
  }
  ScePspFVector3 toward = {0, 0, 1};
  drawLitPoints(name, &toward);
}

//The sweeps: 1024 normals a picture, normal k = base + k * step in the block of 8x8 pixels at column k % 32, row
//k / 32, each with 64 products: row y's light color from the sequence (levels 96-255, as light-share-*), white in
//each block's first row, and each pixel's material from the sequence, white in each block's first column. The light
//toward `light`. 0: (k, 0, 1023), one component swept; 1: (1023, 0, k), the other; 2: 0's three times as long; 3:
//0's over 1024 (about one long, a power of two); 4: (0, k, 1023), along y; 5: (1023, k, 0) with the light along +x;
//6: 0's with the light toward (1, 1, 1) (not one long); 7: 0's with the light toward (0, 0, 3).
typedef struct { float light[3], base[3], step[3]; } Sweep;
static const Sweep sweeps[8] = {
  {{0, 0, 1}, {0, 0, 1023}, {1, 0, 0}}, {{0, 0, 1}, {1023, 0, 0}, {0, 0, 1}}, {{0, 0, 1}, {0, 0, 3069}, {3, 0, 0}},
  {{0, 0, 1}, {0, 0, 1023.0f / 1024}, {1.0f / 1024, 0, 0}}, {{0, 0, 1}, {0, 0, 1023}, {0, 1, 0}},
  {{1, 0, 0}, {1023, 0, 0}, {0, 1, 0}}, {{1, 1, 1}, {0, 0, 1023}, {1, 0, 0}}, {{0, 0, 3}, {0, 0, 1023}, {1, 0, 0}},
};
static void lightSweep(const char* name, int set) {
  if(!beginTest(name)) return;
  const Sweep* s = &sweeps[set];
  probeRandom = 0x5eeb6000u + set;
  for(int y = 0; y < 256; y++) {
    rowLight[y] = y & 7 ? shareColor() : 0xffffff;
    rowLights[y] = 1;
    for(int x = 0; x < 256; x++) {
      int k = (y >> 3) * 32 + (x >> 3);
      float n[3] = {s->base[0] + k * s->step[0], s->base[1] + k * s->step[1], s->base[2] + k * s->step[2]};
      unsigned int material = x & 7 ? shareColor() : 0xffffff;
      litPoint(x, y, material, n);
    }
  }
  ScePspFVector3 toward = {s->light[0], s->light[1], s->light[2]};
  drawLitPoints(name, &toward);
}

//Lights added up: at the normal (33, 0, 56), rows 0-63 lit by one light, 64-127 by two alike, 128-191 three, 192-255
//four; row y's light (r, 63 - r, r) for r = y % 64, on column x's material (x, x, 255 - x). One light's part stays
//below 64, so four don't reach 255: whether each light's part is cut to a whole level before they're added shows.
static void lightSum(const char* name) {
  if(!beginTest(name)) return;
  float n[3] = {33, 0, 56};
  for(int y = 0; y < 256; y++) {
    int r = y & 63;
    rowLight[y] = r | (63 - r) << 8 | r << 16;
    rowLights[y] = 1 + (y >> 6);
    for(int x = 0; x < 256; x++) litPoint(x, y, x | x << 8 | (255 - x) << 16, n);
  }
  ScePspFVector3 toward = {0, 0, 1};
  drawLitPoints(name, &toward);
}

//-- the reciprocal's chords (part 60: 128 straight chords, two pinned by measurement, the rest by formula)

//A strip a pixel tall and 512 long in row `row` from x `left`: two triangles, w0 at its left end and w1 at its right,
//texture coordinate uv0 and uv1 there (along u, or along v with alongV; the other 0).
static void chordStrip(FloatVertex* v, float left, int row, float w0, float w1, float uv0, float uv1, int alongV) {
  float x0 = ndcX(left) * w0, x1 = ndcX(left + 512) * w1, top = ndcY(row), bottom = ndcY(row + 1);
  FloatVertex a = {alongV ? 0 : uv0, alongV ? uv0 : 0, 0xffffffff, x0, top * w0, -w0};
  FloatVertex b = {alongV ? 0 : uv1, alongV ? uv1 : 0, 0xffffffff, x1, top * w1, -w1};
  FloatVertex c = {alongV ? 0 : uv0, alongV ? uv0 : 0, 0xffffffff, x0, bottom * w0, -w0};
  FloatVertex d = {alongV ? 0 : uv1, alongV ? uv1 : 0, 0xffffffff, x1, bottom * w1, -w1};
  v[0] = a, v[1] = b, v[2] = c, v[3] = b, v[4] = d, v[5] = c;
}

//The reciprocal at a pixel, read off: in 32 rows a case, a strip (chordStrip) from w0 to w1, a power of two apart,
//where R is exact, its u (or v) scale * w at each end, so s is scale all along while q steps from 1 / w0 to 1 / w1, one
//unit of its 15 bits each sixteenth of a pixel (both steps exact). A pixel's u is then scale * R(q), and scale makes
//its texel (the repeating texture's, texelXY) the chord's 16-bit value's low 8 bits. Rows 32c to 32c + 15 have the
//strip's left end at -8/16, -7/16, ... 7/16 of a pixel, so the pixels' middles fall 1 to 4096 sixteenths along it; the
//next 16 the same 256 pixels further left, 4097 to 8192: every q of the binade on a pixel (13 bits below the power of
//two: chord and position's top 6 bits). The cases: w 1 to 2; the same at half the scale (texel: the value's bits 1-8,
//the coordinates below 32768 texels, in case the GE keeps no more: texel-range shows); 2 to 4, 1/2 to 1, 1024 to 2048,
//2^-10 to 2^-9 (other binades); 2 to 1 (the other way along); and 1 to 2 along v.
typedef struct { float w0, w1, scale; int alongV; } ChordCase;
static const ChordCase chordCases[8] = {
  {1, 2, 128, 0}, {1, 2, 64, 0}, {2, 4, 64, 0}, {0.5f, 1, 256, 0}, {1024, 2048, 0.125f, 0},
  {1.0f / 1024, 1.0f / 512, 131072, 0}, {2, 1, 128, 0}, {1, 2, 128, 1},
};
static void recipPixels(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&wOnly, 1);
  useRepeatingTexture();
  FloatVertex* v = sceGuGetMemory(256 * 6 * sizeof(FloatVertex));
  for(int row = 0; row < 256; row++) {
    const ChordCase* c = &chordCases[row >> 5];
    float left = (row & 16 ? -256 : 0) + ((row & 15) - 8) / 16.0f;
    chordStrip(v + row * 6, left, row, c->w0, c->w1, c->scale * c->w0, c->scale * c->w1, c->alongV);
  }
  sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 256 * 6, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The reciprocal at a pixel through the texture matrix (TEXTURE_MAP_MODE 1, from the model's position; part 60: never
//measured): at w 1, s, t and q are the matrix's as given, so q can start just below a power of two and the pixels read
//14-bit qs (recip-pixels' start on one, which leaves 13). Strips a pixel tall, 64 rows a case: 4 windows of 256 pixels
//along a 1024-pixel strip, each at 16 offsets (the strip's left end -8/16 to 7/16 of a pixel past them). Rows 0-63: q
//from 32767/32768 down one unit of 2^-15 each sixteenth of a pixel, s 128 (texel: the chord's low 8 bits); 64-127: q
//from 32767/16384 down a unit of 2^-14 each sixteenth, s 256. 128-191: q the same at every corner, in 16 groups of 4
//rows, 1, 0.75, 0.5, 0.375, 2^-20, +0, -0, -2^-20, -0.375, -0.5, -0.75, -1, 3, -3, 2^-126, -2^-126 (a q of 0 or below,
//also unmeasured, and tiny ones), s 0.25, t 0.3125. 192-255: w 1 at the left end of a 512-pixel strip and 2 at its
//right, the windows and offsets of recip-pixels' first case, the matrix's s 128 w (from the model's z, -w) and q 1
//(rows 192-223: recip-pixels' rows 0-31 if the matrix's s and q are taken as a vertex's own u and q), or s 96 w and q
//0.75 (224-255: the matrix's q times R(w), as part 60 has it, not measured).
static void recipProjected(const char* name) {
  static const float constants[16] = {1, 0.75f, 0.5f, 0.375f, 1.0f / 1048576, 0, -0.0f, -1.0f / 1048576, -0.375f,
                                      -0.5f, -0.75f, -1, 3, -3, 1.17549435e-38f, -1.17549435e-38f};
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  start3D(&wOnly, 1);
  useRepeatingTexture();
  sceGuTexMapMode(GU_TEXTURE_MATRIX, 0, 0);
  sceGuTexProjMapMode(GU_POSITION);
  for(int row = 0; row < 256; row++) {
    ScePspFMatrix4 m = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    float left = 0, length = 256, w0 = 1, w1 = 1;
    if(row < 128) {
      float top = row < 64 ? 32767.0f / 32768 : 32767.0f / 16384, slope = row < 64 ? 1.0f / 16 : 1.0f / 8;
      left = -256 * (row >> 4 & 3) + ((row & 15) - 8) / 16.0f, length = 1024;
      m.x.z = -slope;  //q's drop per unit of the model's x (128 pixels at w 1)
      m.w.x = row < 64 ? 128 : 256, m.w.z = top + slope * ndcX(left);
    } else if(row < 192) {
      m.w.x = 0.25f, m.w.y = 0.3125f, m.w.z = constants[(row - 128) >> 2];
    } else {
      left = (row & 16 ? -256 : 0) + ((row & 15) - 8) / 16.0f, length = 512, w1 = 2;
      m.z.x = row < 224 ? -128 : -96, m.w.z = row < 224 ? 1 : 0.75f;
    }
    sceGuSetMatrix(GU_TEXTURE, &m);
    float x0 = ndcX(left) * w0, x1 = ndcX(left + length) * w1, top = ndcY(row), bottom = ndcY(row + 1);
    ColorVertex* v = sceGuGetMemory(6 * sizeof(ColorVertex));
    ColorVertex a = {0xffffffff, x0, top * w0, -w0}, b = {0xffffffff, x1, top * w1, -w1};
    ColorVertex c = {0xffffffff, x0, bottom * w0, -w0}, d = {0xffffffff, x1, bottom * w1, -w1};
    v[0] = a, v[1] = b, v[2] = c, v[3] = b, v[4] = d, v[5] = c;
    sceGuDrawArray(GU_TRIANGLES, ColorVertexType3D, 6, 0, v);
  }
  finishList();
  saveTarget(name, 256, 256, 0);
}

//The reciprocal at a vertex: 4096 cells of 4x4 pixels (64x64), each two triangles (or, with sprites, a 3D sprite) at
//one w, u 512 at every corner (v 0). Its pixels' u is then 512 * R15(w) * R(R15(w)) (R15: R cut to 15 bits, as s
//and q are at a vertex), and its texel shows that product to 2^-17: (2^17 * product) mod 256, the product within
//2^-14 of 1. Cell c (row c / 64, column c % 64) has w in [1, 2) at chord c / 32 and position 8 * (c % 32) + 3 * odd
//(w's fraction bits 22-16 and 15-8), its lowest 8 bits `low`.
static void recipVertex(const char* name, int odd, int low, int sprites) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  FloatVertex* v = (FloatVertex*)big;
  int per = sprites ? 2 : 6;
  for(int c = 0; c < 4096; c++) {
    unsigned int bits = 0x3f800000u | (c >> 5) << 16 | ((c & 31) * 8 + odd * 3) << 8 | low;
    float w;
    memcpy(&w, &bits, 4);
    float x0 = ndcX((c & 63) * 4) * w, x1 = ndcX((c & 63) * 4 + 4) * w;
    float y0 = ndcY((c >> 6) * 4) * w, y1 = ndcY((c >> 6) * 4 + 4) * w;
    FloatVertex a = {512, 0, 0xffffffff, x0, y0, -w}, b = {512, 0, 0xffffffff, x1, y0, -w};
    FloatVertex e = {512, 0, 0xffffffff, x0, y1, -w}, d = {512, 0, 0xffffffff, x1, y1, -w};
    FloatVertex* t = v + c * per;
    if(sprites) t[0] = a, t[1] = d;
    else t[0] = a, t[1] = b, t[2] = e, t[3] = b, t[4] = d, t[5] = e;
  }
  sceKernelDcacheWritebackAll();
  start3D(&wOnly, 1);
  useRepeatingTexture();
  sceGuDrawArray(sprites ? GU_SPRITES : GU_TRIANGLES, FloatVertexType3D, 4096 * per, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Texture coordinates far past the texture, as the reciprocal's pictures take them (up to 2^17 texels; round 5's to
//4096): 32 rows of 8 pixels, 16 cells of 16 pixels across, each a sprite in through mode with its texture coordinates
//the same at both corners, u = 2^k + 17i + 3 and v = 2^k + 255 - 17i texels in row j, column i, k = 8 + j % 16
//(2^8 to 2^23, every one a float holds exactly), negative (-2^k plus the same) in rows 16-31. Repeating, texelXY:
//a cell shows u mod 256 in red and v mod 256 in green while the GE keeps the coordinate's low bits.
static void texelRange(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelXY);
  fillTarget(zero, 0);
  start(GU_PSM_8888);
  useRepeatingTexture();
  FloatVertex* v = sceGuGetMemory(32 * 16 * 2 * sizeof(FloatVertex));
  for(int j = 0; j < 32; j++) {
    float power = 1 << (8 + j % 16), sign = j < 16 ? 1 : -1;
    for(int i = 0; i < 16; i++) {
      float u = sign * power + 17 * i + 3, t = sign * power + 255 - 17 * i;
      v[(j * 16 + i) * 2] = (FloatVertex){u, t, 0xffffffff, i * 16, j * 8, 0};
      v[(j * 16 + i) * 2 + 1] = (FloatVertex){u, t, 0xffffffff, i * 16 + 16, j * 8 + 8, 0};
    }
  }
  sceGuDrawArray(GU_SPRITES, FloatVertexType, 32 * 16 * 2, 0, v);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//-- PATCH_CULL_ENABLE (curves.cpp: unknown; round 4's curves-culling saw it change nothing on flat patches facing
//the screen)

//The lit patches' light: white, directional toward the viewer (+z), diffuse; the material emissive 0x202020 and
//diffuse white, so a lit vertex is never black. Lighting itself stays off until a cell asks for it.
static void patchLight(void) {
  ScePspFVector3 toward = {0, 0, 1};
  sceGuLight(0, GU_DIRECTIONAL, GU_DIFFUSE, &toward);
  sceGuLightAtt(0, 1, 0, 0);
  sceGuLightColor(0, GU_AMBIENT, 0);
  sceGuLightColor(0, GU_DIFFUSE, 0xffffff);
  sceGuLightColor(0, GU_SPECULAR, 0);
  sceGuColorMaterial(0);
  sceGuModelColor(0x202020, 0, 0xffffff, 0);
  sceGuAmbient(0);
  sceGuLightMode(0);
}

//256 cells of 16x16 pixels, each a flat white patch 12 pixels square (4x4 points, cut twice each way) in 3D (identity
//matrices), cell k with: PATCH_CULL_ENABLE on (k & 1); sceGuPatchFrontFace GU_CCW (k & 2, else GU_CW); its points
//mirrored left to right, so its strips run the other way round on the screen (k & 4); normals (k >> 3 & 3): none in
//the vertex type, (0, 0, 1) toward the viewer, (0, 0, -1) away, (1, 0, 0) edge on; culling (k >> 5 & 3): off,
//CULL_FACE with sceGuFrontFace GU_CW, with GU_CCW, or off and lit (patchLight); drawn as triangles, or points
//(k & 128).
static void patchCull(const char* name) {
  static const float normalsOf[4][3] = {{0, 0, 1}, {0, 0, 1}, {0, 0, -1}, {1, 0, 0}};
  if(!beginTest(name)) return;
  start3D(&identity, 1);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuPatchDivide(2, 2);
  patchLight();
  for(int k = 0; k < 256; k++) {
    int normals = k >> 3 & 3, culling = k >> 5 & 3;
    if(k & 1) sceGuEnable(GU_PATCH_CULL_FACE);
    else sceGuDisable(GU_PATCH_CULL_FACE);
    sceGuPatchFrontFace(k & 2 ? GU_CCW : GU_CW);
    if(culling == 1 || culling == 2) {
      sceGuEnable(GU_CULL_FACE);
      sceGuFrontFace(culling == 1 ? GU_CW : GU_CCW);
    } else {
      sceGuDisable(GU_CULL_FACE);
    }
    if(culling == 3) {
      sceGuEnable(GU_LIGHTING);
      sceGuEnable(GU_LIGHT0);
    } else {
      sceGuDisable(GU_LIGHTING);
    }
    sceGuPatchPrim(k & 128 ? GU_POINTS : GU_TRIANGLE_STRIP);
    float x0 = (k & 15) * 16 + 2, y0 = (k >> 4 & 7) * 16 + 2 + (k & 128 ? 128 : 0);
    LitVertex* lit = sceGuGetMemory(16 * sizeof(LitVertex));
    ColorVertex* plain = sceGuGetMemory(16 * sizeof(ColorVertex));
    const float* n = normalsOf[normals];
    for(int j = 0; j < 4; j++) {
      for(int i = 0; i < 4; i++) {
        float x = ndcX(x0 + (k & 4 ? 3 - i : i) * 4), y = ndcY(y0 + j * 4);
        lit[j * 4 + i] = (LitVertex){0xffffffff, n[0], n[1], n[2], x, y, 0};
        plain[j * 4 + i] = (ColorVertex){0xffffffff, x, y, 0};
      }
    }
    if(normals) sceGuDrawBezier(LitVertexType, 4, 4, 0, lit);
    else sceGuDrawBezier(ColorVertexType3D, 4, 4, 0, plain);
  }
  sceGuDisable(GU_PATCH_CULL_FACE);
  sceGuDisable(GU_CULL_FACE);
  sceGuDisable(GU_LIGHTING);
  sceGuPatchPrim(GU_TRIANGLE_STRIP);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//Patches at the edges of what's drawn: 64 cells of 32x32 pixels (8x8), each with its own scissor rectangle (its
//middle 16x16 pixels) and a white patch (4x4 points, cut 4 times each way) in 3D under the lens, placed against it
//(placement (c >> 1) & 15, below), PATCH_CULL_ENABLE on in odd cells, drawn as triangles in cells 0-31 and as points in
//32-63. The placements, the patch's corners in pixels from the cell's top left, at w 1 unless said: 0 inside, (10,
//10) to (22, 22); 1-4 across the scissor's left, right, top and bottom edges; 5-8 wholly left, right, above and below
//it (in the cell); 9 around it, (2, 2) to (30, 30); 10 its right half past the near plane (w 0.25); 11 wholly past
//it; 12 its right half past the far plane (w 20); 13 the corners inside, the middle points bowed out past every edge;
//14 the corners outside, the middle points pulled in across it; 15 every point at (16, 16).
static void patchCullEdges(const char* name) {
  static const float boxes[16][4] = {
    {10, 10, 22, 22}, {2, 10, 14, 22}, {18, 10, 30, 22}, {10, 2, 22, 14}, {10, 18, 22, 30}, {0, 10, 6, 22},
    {26, 10, 32, 22}, {10, 0, 22, 6}, {10, 26, 22, 32}, {2, 2, 30, 30}, {10, 10, 22, 22}, {10, 10, 22, 22},
    {10, 10, 22, 22}, {10, 10, 22, 22}, {1, 1, 31, 31}, {16, 16, 16, 16},
  };
  if(!beginTest(name)) return;
  start3D(&lens, 1);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuPatchDivide(4, 4);
  for(int c = 0; c < 64; c++) {
    int place = c >> 1 & 15, x0 = (c & 7) * 32, y0 = (c >> 3) * 32;
    const float* box = boxes[place];
    if(c & 1) sceGuEnable(GU_PATCH_CULL_FACE);
    else sceGuDisable(GU_PATCH_CULL_FACE);
    sceGuPatchPrim(c & 32 ? GU_POINTS : GU_TRIANGLE_STRIP);
    sceGuSendCommandi(0xd4, (y0 + 8) << 10 | (x0 + 8));     //SCISSOR1
    sceGuSendCommandi(0xd5, (y0 + 23) << 10 | (x0 + 23));   //SCISSOR2
    ColorVertex* v = sceGuGetMemory(16 * sizeof(ColorVertex));
    for(int j = 0; j < 4; j++) {
      for(int i = 0; i < 4; i++) {
        float px = box[0] + (box[2] - box[0]) * i / 3, py = box[1] + (box[3] - box[1]) * j / 3, w = 1;
        int middle = (i == 1 || i == 2) && (j == 1 || j == 2);
        if(place == 13 && middle) px = i == 1 ? -8 : 40, py = j == 1 ? -8 : 40;
        if(place == 14 && middle) px = i == 1 ? 12 : 20, py = j == 1 ? 12 : 20;
        if((place == 10 && i >= 2) || place == 11) w = 0.25f;
        if(place == 12 && i >= 2) w = 20;
        v[j * 4 + i] = (ColorVertex){0xffffffff, ndcX(x0 + px) * w, ndcY(y0 + py) * w, -w};
      }
    }
    sceGuDrawBezier(ColorVertexType3D, 4, 4, 0, v);
  }
  sceGuDisable(GU_PATCH_CULL_FACE);
  sceGuPatchPrim(GU_TRIANGLE_STRIP);
  sceGuSendCommandi(0xd4, 0);
  sceGuSendCommandi(0xd5, 255 << 10 | 511);
  finishList();
  saveTarget(name, 256, 256, 0);
}

//-- patch divisions past 64 along v (round 4 measured u: 65 and more never finish, 128 counts as 0, so 1)

//How many vertices a column has: in column n of 16 pixels, a flat patch 240 pixels tall (through mode) cut once
//along u and some divisions along v, as points added up (startCount), its two columns of vertices at x + 4 and x +
//12: a column's sum is how many vertices a column of the patch has. The counts' tests as round 4's along u: 16, 63
//and 64 in one, then each past 64 alone, last in the round, each list given Patience.
static void countColumn(int column, int divisions) {
  ColorVertex* v = sceGuGetMemory(16 * sizeof(ColorVertex));
  for(int j = 0; j < 4; j++) {
    for(int i = 0; i < 4; i++) v[j * 4 + i] = (ColorVertex){0xff010101, column * 16 + 4 + i * 8 / 3.0f, 8 + j * 80, 0};
  }
  sceGuPatchDivide(1, divisions);
  sceGuDrawBezier(ColorVertexType, 4, 4, 0, v);
}
static const struct { const char* name; int first, last; } countTestsV[] = {
  {"curves-count-v", 0, 2}, {"curves-count-v-65", 3, 3}, {"curves-count-v-100", 4, 4}, {"curves-count-v-128", 5, 5},
  {"curves-count-v-200", 6, 6}, {"curves-count-v-255", 7, 7},
};
enum { CountTestsV = sizeof(countTestsV) / sizeof(countTestsV[0]) };
static void curvesCountV(const char* name, int first, int last) {
  if(!beginTest(name)) return;
  fillTarget(zero, 0);
  startCount();
  char what[128];
  int length = snprintf(what, sizeof(what), "Bezier patches of 4x4 points as points, 1 division along u, along v");
  for(int column = first; column <= last; column++) {
    countColumn(column, countDivisions[column]);
    if(length < (int)sizeof(what)) {
      length += snprintf(what + length, sizeof(what) - length, " %d", countDivisions[column]);
    }
  }
  sceGuDisable(GU_BLEND);
  finishListOf(what);
  saveTarget(name, 256, 256, 0);
}

//-- the GE's drawing time (part 64: a lower bound from Sony's peak rates)

//Each timing test runs its cases at two clock settings, first 222 MHz with the bus at 111 (a PSP's own, set whatever
//custom firmware chose), then 333 MHz with the bus at 166, and puts the clocks back as it found them (foundPll and the
//rest, read as round 6 starts); each case at a few sizes, each size `repeats` times in a
//row, a list of known work built in timed (its FINISH and END at its end) and queued alone with timingCallbacks. Its
//.bin holds, for each case and size: the case, the size, the clocks (CPU MHz << 16 | bus MHz, as scePower gives them
//back), repeats | values << 16, then for each run `values` times in microseconds (sceKernelGetSystemTimeLow), all
//from just before sceGeListEnQueue: to the finish callback, to sceGeListEnQueue's return, and (time-signals) to the
//signal callback. A run the GE didn't finish in Patience is a stall (reset as waitForGe resets it, noted in the
//test's .stalled), its times 0xffffffff and the rest of its size's too; after three, the test's remaining runs aren't
//tried. A list sceGeListEnQueue refuses counts as a stall too (noted, nothing reset), its run's second time the
//error. The program waits on a semaphore the finish callback signals, so nothing looks at the GE while it draws; it
//wakes every 2 ms to look at the flag the callback sets too, in case a callback may not signal a semaphore.
static int foundPll, foundCpu, foundBus;
static volatile unsigned int finishedAt, signalledAt;
static volatile int finished, signalled, finishes;  //finishes: the finish callbacks so far (round 8's lists)
static SceUID timingSemaphore = -1;
static int timingCallbacks = -1;
enum { Unfinished = 0xffffffffu };

static void timingFinish(int id, void* argument) {
  (void)id, (void)argument;
  finishedAt = sceKernelGetSystemTimeLow();
  finished = 1;
  finishes++;
  sceKernelSignalSema(timingSemaphore, 1);
}
static void timingSignal(int id, void* argument) {
  (void)id, (void)argument;
  signalledAt = sceKernelGetSystemTimeLow();
  signalled = 1;
}
static void timingStart(void) {  //once in a run of the program
  if(timingCallbacks >= 0) return;
  timingSemaphore = sceKernelCreateSema("timing", 0, 0, 1, 0);
  PspGeCallbackData callbacks = {timingSignal, 0, timingFinish, 0};
  timingCallbacks = sceGeSetCallback(&callbacks);
  sceKernelDcacheWritebackInvalidateRange(timed, sizeof(timed));  //(no cached line of it to land over a list later)
}
//0: 222/111 MHz; 1: 333/166; -1: as round 6 found them.
static void setClocks(int setting) {
  if(setting > 0) scePowerSetClockFrequency(333, 333, 166);
  else if(setting == 0) scePowerSetClockFrequency(222, 222, 111);
  else scePowerSetClockFrequency(foundPll, foundCpu, foundBus);
}

//Runs the list in timed once; times[0-2] as the test's .bin has them. 0 if the GE didn't finish it (a stall: reset).
static int runTimed(const char* what, unsigned int* times) {
  times[0] = times[1] = times[2] = Unfinished;
  while(sceKernelPollSema(timingSemaphore, 1) >= 0) continue;
  finished = signalled = 0;
  unsigned int began = sceKernelGetSystemTimeLow();
  int queued = sceGeListEnQueue(TIMED, 0, timingCallbacks, 0);
  unsigned int after = sceKernelGetSystemTimeLow(), now = after;
  if(queued < 0) {
    char text[200];
    snprintf(text, sizeof(text), "sceGeListEnQueue gave 0x%08x for %s.\n", (unsigned int)queued, what);
    noteStall(text);
    times[1] = queued;
    return 0;
  }
  while(!finished && now - began < Patience) {
    SceUInt timeout = 2000;
    sceKernelWaitSema(timingSemaphore, 1, &timeout);
    now = sceKernelGetSystemTimeLow();
  }
  int state = sceGeDrawSync(1);
  while(finished && state > 0 && now - began < Patience) {  //its END, just after the FINISH
    sceKernelDelayThread(50);
    state = sceGeDrawSync(1);
    now = sceKernelGetSystemTimeLow();
  }
  if(!finished || state > 0) {
    resetGe(now - began, state, what);
    return 0;
  }
  times[0] = finishedAt - began;
  times[1] = after - began;
  if(signalled) times[2] = signalledAt - began;
  return 1;
}

//A test's cases: what each draws (for manifest6.txt), what its size counts, and its sizes.
typedef struct { const char* name; const char* unit; int count; int sizes[4]; } TimedCase;
static unsigned int recorded;  //words of the test's .bin so far, in pixels
static void record(unsigned int word) {
  if(recorded < 256 * 256) pixels[recorded++] = word;
}
static void timeCases(const char* name, const TimedCase* cases, int count, int repeats, int values,
                      void (*build)(int c, int size)) {
  if(!beginTest(name)) return;
  timingStart();
  recorded = 0;
  int stalls = 0, done = 0;
  for(int fast = 0; fast < 2; fast++) {
    setClocks(fast);
    unsigned int clocks = scePowerGetCpuClockFrequencyInt() << 16 | scePowerGetBusClockFrequencyInt();
    for(int c = 0; c < count; c++) {
      for(int s = 0; s < cases[c].count; s++) {
        int size = cases[c].sizes[s], ready = stalls < 3 && timingCallbacks >= 0 && timingSemaphore >= 0;
        char what[160];
        snprintf(what, sizeof(what), "%s case %d (%s), size %d, %s", name, c, cases[c].name, size,
                 fast ? "333 MHz" : "222 MHz");
        if(ready) build(c, size);
        record(c), record(size), record(clocks), record(repeats | values << 16);
        for(int r = 0; r < repeats; r++) {
          unsigned int times[3] = {Unfinished, Unfinished, Unfinished};
          if(ready && !runTimed(what, times)) ready = 0, stalls++;
          for(int k = 0; k < values; k++) record(times[k]);
        }
      }
      progress(&current, name, ++done, 2 * count);
    }
  }
  setClocks(-1);
  save(name, pixels, recorded * 4);
}

//The timing tests' textures: texture (8888), texture16 and bytes8 from the sequence, the palette, and texture again
//in VRAM at Spare. Their texels don't matter, only how many and how they're laid out.
static void prepareTexels(void) {
  probeRandom = 0x7e3e1500;
  for(int n = 0; n < 256 * 256; n++) {
    unsigned int high = probeNext() << 8;
    texture[n] = high | (probeNext() & 0xff);
    texture16[n] = probeNext();
    bytes8[n] = probeNext();
    VRAM[Spare / 4 + n] = texture[n];
  }
  for(int n = 0; n < 256; n++) palette[n] = 0xff000000u | probeNext();
  sceKernelDcacheWritebackAll();
}
//A texture for a timed list: format psm, size x size texels at texels (buffer width bufferWidth), swizzled or not,
//nearest or bilinear, repeating, replacing the color; CLUT4 and CLUT8 take the palette, loaded in the list.
static void timedTexture(int psm, int swizzled, int size, int bufferWidth, const void* texels, int linear) {
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(psm, 0, 0, swizzled);
  sceGuTexImage(0, size, size, bufferWidth, texels);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
  sceGuTexWrap(GU_REPEAT, GU_REPEAT);
  if(psm == GU_PSM_T8 || psm == GU_PSM_T4) {
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad(psm == GU_PSM_T4 ? 2 : 32, palette);
  }
}
//A list built in timed, not queued (GU_SEND): drawing into the target in psm with start's state. sceGuFinish ends it.
static void timedList(int psm) {
  sceGuStart(GU_SEND, timed);
  setUp(psm);
}
//A sprite in through mode from (x0, y0) to (x1, y1), texture coordinates (u0, v0) to (u1, v1) in texels, in color.
static void timedSprite(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                        unsigned int color) {
  FloatVertex* v = sceGuGetMemory(2 * sizeof(FloatVertex));
  v[0] = (FloatVertex){u0, v0, color, x0, y0, 0};
  v[1] = (FloatVertex){u1, v1, color, x1, y1, 0};
  sceGuDrawArray(GU_SPRITES, FloatVertexType, 2, 0, v);
}

//Commands: lists of the same command over and over, word by word, for the GE's time a command (none draws).
static const TimedCase commandCases[] = {
  {"an empty list: FINISH and END", "commands", 1, {0}},
  {"NOP", "commands", 4, {1024, 4096, 16384, 65536}},
  {"TEXTURE_ENV_COLOR, a register, each a new value", "commands", 4, {1024, 4096, 16384, 65536}},
  {"TEX_FLUSH", "commands", 4, {1024, 4096, 16384, 65536}},
  {"the world matrix: WORLD_MATRIX_NUMBER 0 and 12 WORLD_MATRIX_DATA (the identity)", "matrices", 4,
   {64, 256, 1024, 4096}},
  {"JUMP to the next word", "jumps", 4, {256, 1024, 4096, 16384}},
  {"CALL to a RET elsewhere", "calls", 4, {256, 1024, 4096, 16384}},
};
static void buildCommands(int c, int size) {
  unsigned int* w = TIMED;
  unsigned int n = 0, address = (unsigned int)w & 0x0fffffff;
  //A JUMP's or CALL's address is BASE's 4 bits and its own 24: set once, as timed lies in one 16 MiB (else they're
  //left out, and the list is as good as empty).
  if(c >= 5 && (address >> 24) != ((address + sizeof(timed) - 1) >> 24)) size = 0;
  if(c >= 5) w[n++] = 0x10u << 24 | (address >> 8 & 0xf0000);
  for(int k = 0; k < size; k++) {
    if(c == 1) {
      w[n++] = 0;
    } else if(c == 2) {
      w[n++] = 0xcau << 24 | (k & 0xffffff);
    } else if(c == 3) {
      w[n++] = 0xcbu << 24;
    } else if(c == 4) {
      w[n++] = 0x3au << 24;
      for(int e = 0; e < 12; e++) w[n++] = 0x3bu << 24 | (e % 4 == 0 ? 0x3f8000 : 0);
    } else if(c == 5) {
      w[n] = 0x08u << 24 | ((address + 4 * (n + 1)) & 0xffffff);
      n++;
    } else if(c == 6) {
      w[n++] = 0x0au << 24 | ((address + 4 * 250000) & 0xffffff);
    }
  }
  w[250000] = 0x0bu << 24;  //RET
  w[n++] = 0x0fu << 24;     //FINISH
  w[n++] = 0x0cu << 24;     //END
}

//Primitives: many small ones of a kind, for the GE's time a vertex and a primitive. Primitive i at a 4x4-pixel spot
//of the target (64 to a row; past 4096 they go round again), white, the depth test off.
static const TimedCase primitiveCases[] = {
  {"through mode: triangles of about 2 pixels (color, float position)", "triangles", 3, {256, 1024, 4096}},
  {"through mode: triangles covering no pixel's middle", "triangles", 3, {256, 1024, 4096}},
  {"through mode: a triangle strip (count + 2 vertices)", "triangles", 3, {256, 1024, 4096}},
  {"through mode: a triangle fan (count + 2 vertices)", "triangles", 3, {256, 1024, 4096}},
  {"through mode: sprites 2x2 pixels", "sprites", 3, {256, 1024, 4096}},
  {"through mode: lines 2 pixels long", "lines", 3, {256, 1024, 4096}},
  {"through mode: a line strip (count + 1 vertices)", "lines", 3, {256, 1024, 4096}},
  {"through mode: points", "points", 3, {256, 1024, 4096}},
  {"through mode: textured triangles (a 32x32 8888 texture, nearest)", "triangles", 3, {256, 1024, 4096}},
  {"through mode: triangles through 16-bit indices", "triangles", 3, {256, 1024, 4096}},
  {"3D: triangles (identity matrices, w 1)", "triangles", 3, {256, 1024, 4096}},
  {"3D: textured triangles in perspective (the lens, w 2)", "triangles", 3, {256, 1024, 4096}},
  {"3D: lit triangles, one directional light", "triangles", 3, {256, 1024, 4096}},
  {"3D: lit triangles, four point lights fading, with their shine", "triangles", 3, {256, 1024, 4096}},
  {"3D: skinned triangles, 8 float weights", "triangles", 3, {256, 1024, 4096}},
  {"3D: skinned triangles, 2 float weights", "triangles", 3, {256, 1024, 4096}},
  {"3D: morphed triangles, 2 vertices each", "triangles", 3, {256, 1024, 4096}},
  {"3D: triangles culled as back faces", "triangles", 3, {256, 1024, 4096}},
  {"3D: triangles outside the scissor rectangle", "triangles", 3, {256, 1024, 4096}},
  {"3D: triangles past the near plane", "triangles", 3, {256, 1024, 4096}},
  {"3D: triangles cut by the near plane (one corner past it)", "triangles", 3, {256, 1024, 4096}},
  {"3D: triangles, their vertices in VRAM", "triangles", 3, {256, 1024, 4096}},
  {"3D: sprites 2x2 pixels", "sprites", 3, {256, 1024, 4096}},
  {"3D: lines 2 pixels long", "lines", 3, {256, 1024, 4096}},
  {"3D: points", "points", 3, {256, 1024, 4096}},
  {"3D: a triangle strip", "triangles", 3, {256, 1024, 4096}},
  {"3D: triangles through 16-bit indices", "triangles", 3, {256, 1024, 4096}},
  {"3D: Bezier patches 8x8 pixels, 4x4 points, cut 4x4", "patches", 3, {16, 64, 256}},
  {"3D: the same patches outside the scissor rectangle, PATCH_CULL_ENABLE off", "patches", 3, {16, 64, 256}},
  {"3D: the same, PATCH_CULL_ENABLE on", "patches", 3, {16, 64, 256}},
  {"3D: Bezier patches 16x16 pixels, cut 16x16", "patches", 3, {4, 16, 64}},
};
static float spotX(int i) { return (i & 63) * 4; }
static float spotY(int i) { return (i >> 6 & 63) * 4; }
//Strips' vertices: back and forth along rows 8 pixels apart, every other one 2 pixels down.
static float stripX(int k) { return k >> 7 & 1 ? 2 * (127 - (k & 127)) : 2 * (k & 127); }
static float stripY(int k) { return (k >> 7 & 31) * 8 + (k & 1) * 2; }
typedef struct { float weights[8]; unsigned int color; float x, y, z; } Skinned8;
typedef struct { float weights[2]; unsigned int color; float x, y, z; } Skinned2;
//Where a case's vertices go, in pixels, before they're written in its vertex type, and the w each takes in 3D.
static float planX[3 * 4096], planY[3 * 4096];
static float planW(int c, int n) { return c == 11 ? 2 : c == 19 || (c == 20 && n % 3 == 2) ? 0.25f : 1; }
static void buildPrimitives(int c, int count) {
  int threeD = c >= 10, vertices = 3 * count, prim = GU_TRIANGLES;
  float shift = c == 18 ? 600 : 0;  //outside the scissor rectangle (setUp's: columns 0-511)
  for(int i = 0; i < count; i++) {  //most cases' triangles: (x, y), (x + 2, y), (x, y + 2) at spot i
    float x = spotX(i) + shift, y = spotY(i);
    planX[i * 3] = x, planY[i * 3] = y;
    planX[i * 3 + 1] = x + 2, planY[i * 3 + 1] = y;
    planX[i * 3 + 2] = x, planY[i * 3 + 2] = y + 2;
  }
  if(c == 1) {  //corners moved in to (1/16, 1/16), (3/8, 1/16) and (1/16, 3/8): no pixel's middle inside
    for(int n = 0; n < vertices; n++) {
      planX[n] += n % 3 == 1 ? -1.625f : 0.0625f;
      planY[n] += n % 3 == 2 ? -1.625f : 0.0625f;
    }
  } else if(c == 2 || c == 6 || c == 25) {
    vertices = c == 6 ? count + 1 : count + 2, prim = c == 6 ? GU_LINE_STRIP : GU_TRIANGLE_STRIP;
    for(int k = 0; k < vertices; k++) planX[k] = stripX(k), planY[k] = stripY(k);
  } else if(c == 3) {  //around a square's sides from its middle, the triangles about 2 pixels each
    float r = 1;
    while(r * r * 2 < count) r += 0.25f;
    vertices = count + 2, prim = GU_TRIANGLE_FAN;
    planX[0] = 128, planY[0] = 128;
    for(int j = 0; j <= count; j++) {
      float t = 8 * r * j / count;
      int side = t < 2 * r ? 0 : t < 4 * r ? 1 : t < 6 * r ? 2 : 3;
      float along = t - side * 2 * r;
      planX[1 + j] = 128 + (side == 0 ? -r + along : side == 1 ? r : side == 2 ? r - along : -r);
      planY[1 + j] = 128 + (side == 0 ? -r : side == 1 ? -r + along : side == 2 ? r : r - along);
    }
  } else if(c == 4 || c == 5 || c == 22 || c == 23) {
    int lines = c == 5 || c == 23;
    vertices = 2 * count, prim = lines ? GU_LINES : GU_SPRITES;
    for(int i = 0; i < count; i++) {
      planX[i * 2] = spotX(i), planY[i * 2] = spotY(i) + (lines ? 0.5f : 0);
      planX[i * 2 + 1] = spotX(i) + 2, planY[i * 2 + 1] = spotY(i) + (lines ? 0.5f : 2);
    }
  } else if(c == 7 || c == 24) {
    vertices = count, prim = GU_POINTS;
    for(int i = 0; i < count; i++) planX[i] = spotX(i) + 0.5f, planY[i] = spotY(i) + 0.5f;
  }
  //The vertices in their type: through mode's in pixels; 3D's at their place on the screen times their w (under the
  //lens, w = -z), identity matrices otherwise.
  int type = threeD ? ColorVertexType3D : ColorVertexType;
  const void* at = big;
  unsigned short* indices = 0;
  for(int n = 0; n < vertices && c < 27; n++) {
    float w = planW(c, n), x = threeD ? ndcX(planX[n]) * w : planX[n], y = threeD ? ndcY(planY[n]) * w : planY[n];
    float z = c == 11 || c == 19 || c == 20 ? -w : 0;
    if(c == 8 || c == 11) {  //texture coordinates: the corner's in a 32x32 texture, in texels (through mode) or 0-1
      float u = (n % 3 == 1 ? 2 : 0) + (n / 3 & 7) * 4, v = (n % 3 == 2 ? 2 : 0) + (n / 3 >> 3 & 7) * 4;
      ((FloatVertex*)big)[n] = (FloatVertex){c == 11 ? u / 32 : u, c == 11 ? v / 32 : v, 0xffffffff, x, y, z};
    } else if(c == 12 || c == 13) {
      ((LitVertex*)big)[n] = (LitVertex){0xffffffff, 0, 0, 1, x, y, z};
    } else if(c == 14) {
      ((Skinned8*)big)[n] = (Skinned8){{0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f}, 0xffffffff,
                                       x, y, z};
    } else if(c == 15) {
      ((Skinned2*)big)[n] = (Skinned2){{0.5f, 0.5f}, 0xffffffff, x, y, z};
    } else if(c == 16) {  //two vertices each, morphed half and half
      ((ColorVertex*)big)[n * 2] = (ColorVertex){0xffffffff, x, y, z};
      ((ColorVertex*)big)[n * 2 + 1] = (ColorVertex){0xff808080, x, y, z};
    } else {
      ((ColorVertex*)big)[n] = (ColorVertex){0xffffffff, x, y, z};
    }
  }
  if(c == 8 || c == 11) type = c == 11 ? FloatVertexType3D : FloatVertexType;
  if(c == 12 || c == 13) type = LitVertexType;
  if(c == 14) type = GU_WEIGHTS(8) | GU_WEIGHT_32BITF | ColorVertexType3D;
  if(c == 15) type = GU_WEIGHTS(2) | GU_WEIGHT_32BITF | ColorVertexType3D;
  if(c == 16) type = GU_VERTICES(2) | ColorVertexType3D;
  if(c == 9 || c == 26) {
    indices = (unsigned short*)((ColorVertex*)big + vertices);
    for(int n = 0; n < vertices; n++) indices[n] = n;
    type |= GU_INDEX_16BIT;
  }
  if(c == 21) {
    memcpy((void*)(0x44000000 + Spare), big, vertices * sizeof(ColorVertex));
    at = (const void*)(0x04000000 + Spare);
  }
  int side = c == 30 ? 16 : 8, perRow = 256 / side;  //27-30: patches of 16 flat points, side 8 (or 16) pixels
  for(int i = 0; i < count && c >= 27; i++) {
    float x0 = (i % perRow) * side + (c == 28 || c == 29 ? 600 : 0), y0 = (i / perRow % perRow) * side;
    for(int n = 0; n < 16; n++) {
      ((ColorVertex*)big)[i * 16 + n] =
        (ColorVertex){0xffffffff, ndcX(x0 + (n & 3) * side / 3.0f), ndcY(y0 + (n >> 2) * side / 3.0f), 0};
    }
  }
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  if(threeD) {
    setUp3D(c == 11 || c == 19 || c == 20 ? &lens : &identity, 1);
    sceGuDisable(GU_DEPTH_TEST);
  }
  if(c == 8 || c == 11) timedTexture(GU_PSM_8888, 0, 32, 32, texture, 0);
  if(c == 12 || c == 13) {
    sceGuEnable(GU_LIGHTING);
    sceGuColorMaterial(GU_DIFFUSE);
    sceGuModelColor(0, 0, 0, 0xffffff);
    sceGuAmbient(0x202020);
    sceGuSpecular(8);
    for(int k = 0; k < (c == 12 ? 1 : 4); k++) {
      ScePspFVector3 place = {k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, c == 12 ? 1 : 0.75f};
      sceGuEnable(GU_LIGHT0 + k);
      sceGuLight(k, c == 12 ? GU_DIRECTIONAL : GU_POINTLIGHT, c == 12 ? GU_DIFFUSE : GU_DIFFUSE_AND_SPECULAR, &place);
      sceGuLightAtt(k, 1, 0.5f, 0.25f);
      sceGuLightColor(k, GU_AMBIENT, 0x101010);
      sceGuLightColor(k, GU_DIFFUSE, 0xc0c0c0);
      sceGuLightColor(k, GU_SPECULAR, 0x808080);
    }
  }
  if(c == 14 || c == 15) {
    for(int k = 0; k < 8; k++) sceGuBoneMatrix(k, &identity);
  }
  if(c == 16) {
    sceGuMorphWeight(0, 0.5f);
    sceGuMorphWeight(1, 0.5f);
  }
  if(c == 17) {
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CCW);  //a triangle running clockwise on the screen is a back face then (round 2's 3d-cull)
  }
  if(c >= 27) {
    if(c == 29) sceGuEnable(GU_PATCH_CULL_FACE);
    else sceGuDisable(GU_PATCH_CULL_FACE);
    sceGuPatchPrim(GU_TRIANGLE_STRIP);
    sceGuPatchDivide(side == 16 ? 16 : 4, side == 16 ? 16 : 4);
    for(int i = 0; i < count; i++) sceGuDrawBezier(ColorVertexType3D, 4, 4, 0, (ColorVertex*)big + i * 16);
    sceGuDisable(GU_PATCH_CULL_FACE);
  } else {
    sceGuDrawArray(prim, type, vertices, indices, at);
  }
  sceGuFinish();
}

//Fills: a layer is the target's 256x256 pixels drawn once (a sprite in through mode unless said), for the GE's time a
//pixel. Textures 256x256 in RAM, swizzled, nearest, 1:1, replacing the color, unless said.
static const TimedCase fillCases[] = {
  {"flat color, 8888 target", "layers", 3, {1, 2, 4}},
  {"flat color, 5650 target", "layers", 3, {1, 2, 4}},
  {"flat color, 5551 target", "layers", 3, {1, 2, 4}},
  {"flat color, 4444 target", "layers", 3, {1, 2, 4}},
  {"8888 texture, not swizzled", "layers", 3, {1, 2, 4}},
  {"8888 texture", "layers", 3, {1, 2, 4}},
  {"8888 texture in VRAM, not swizzled", "layers", 3, {1, 2, 4}},
  {"8888 texture in VRAM", "layers", 3, {1, 2, 4}},
  {"5650 texture", "layers", 3, {1, 2, 4}},
  {"5551 texture", "layers", 3, {1, 2, 4}},
  {"4444 texture", "layers", 3, {1, 2, 4}},
  {"CLUT8 texture (8888 palette)", "layers", 3, {1, 2, 4}},
  {"CLUT4 texture (8888 palette)", "layers", 3, {1, 2, 4}},
  {"CLUT8 texture, not swizzled", "layers", 3, {1, 2, 4}},
  {"CLUT4 texture, not swizzled", "layers", 3, {1, 2, 4}},
  {"DXT1 texture", "layers", 3, {1, 2, 4}},
  {"DXT3 texture", "layers", 3, {1, 2, 4}},
  {"DXT5 texture", "layers", 3, {1, 2, 4}},
  {"8888 texture, bilinear, half a texel off (four texels a pixel)", "layers", 3, {1, 2, 4}},
  {"8888 texture 64x64 magnified 4 times, nearest", "layers", 3, {1, 2, 4}},
  {"8888 texture 64x64 magnified 4 times, bilinear", "layers", 3, {1, 2, 4}},
  {"8888 texture shrunk 2 times (four 128x128 sprites), nearest", "layers", 3, {1, 2, 4}},
  {"8888 texture shrunk 2 times, bilinear", "layers", 3, {1, 2, 4}},
  {"CLUT8 texture, bilinear, half a texel off", "layers", 3, {1, 2, 4}},
  {"8888 texture, MODULATE", "layers", 3, {1, 2, 4}},
  {"blending (source alpha, 1 - source alpha), flat color alpha 0x80, 8888 target", "layers", 3, {1, 2, 4}},
  {"blending, flat color alpha 0x80, 5650 target", "layers", 3, {1, 2, 4}},
  {"depth test GEQUAL passing (depths 0, z 0), writing", "layers", 3, {1, 2, 4}},
  {"depth test GEQUAL failing (depths 0xffff, z 0)", "layers", 3, {1, 2, 4}},
  {"depth test GEQUAL passing, not writing", "layers", 3, {1, 2, 4}},
  {"alpha test GREATER 0 passing", "layers", 3, {1, 2, 4}},
  {"alpha test GREATER 255 failing", "layers", 3, {1, 2, 4}},
  {"stencil test ALWAYS, INCR, 8888 target", "layers", 3, {1, 2, 4}},
  {"logic operation XOR", "layers", 3, {1, 2, 4}},
  {"dithering, 5650 target", "layers", 3, {1, 2, 4}},
  {"pixel mask 0x00ff00ff", "layers", 3, {1, 2, 4}},
  {"3D: two triangles, flat color (identity matrices)", "layers", 3, {1, 2, 4}},
  {"3D: the same with fog", "layers", 3, {1, 2, 4}},
  {"3D: two triangles, 8888 texture, w 1", "layers", 3, {1, 2, 4}},
  {"3D: two triangles, 8888 texture, w 1 at the left to 4 at the right (the lens)", "layers", 3, {1, 2, 4}},
  {"through mode: two triangles, flat shading", "layers", 3, {1, 2, 4}},
  {"through mode: two triangles, colors across (smooth shading)", "layers", 3, {1, 2, 4}},
  {"clear mode: color, stencil and depth", "layers", 3, {1, 2, 4}},
  {"5650 texture, 5650 target", "layers", 3, {1, 2, 4}},
};
static void buildFill(int c, int layers) {
  int target = c == 1 || c == 26 || c == 34 || c == 43 ? GU_PSM_5650 : c == 2 ? GU_PSM_5551 : c == 3 ? GU_PSM_4444
                                                                                                    : GU_PSM_8888;
  if(c == 27 || c == 29) fillDepth(0);
  if(c == 28) fillDepth(0xffff);
  timedList(target);
  unsigned int color = c == 25 || c == 26 ? 0x80ffffff : 0xffffffff;
  float size = 256, texels = 256, offset = 0;
  void* vram = (void*)(0x04000000 + Spare);
  switch(c) {
  case 4: timedTexture(GU_PSM_8888, 0, 256, 256, texture, 0); break;
  case 5: case 24: timedTexture(GU_PSM_8888, 1, 256, 256, texture, 0); break;
  case 6: case 7: timedTexture(GU_PSM_8888, c == 7, 256, 256, vram, 0); break;
  case 8: case 43: timedTexture(GU_PSM_5650, 1, 256, 256, texture16, 0); break;
  case 9: timedTexture(GU_PSM_5551, 1, 256, 256, texture16, 0); break;
  case 10: timedTexture(GU_PSM_4444, 1, 256, 256, texture16, 0); break;
  case 11: case 13: timedTexture(GU_PSM_T8, c == 11, 256, 256, bytes8, 0); break;
  case 12: case 14: timedTexture(GU_PSM_T4, c == 12, 256, 256, bytes8, 0); break;
  case 15: timedTexture(GU_PSM_DXT1, 0, 256, 256, bytes8, 0); break;
  case 16: timedTexture(GU_PSM_DXT3, 0, 256, 256, bytes8, 0); break;
  case 17: timedTexture(GU_PSM_DXT5, 0, 256, 256, bytes8, 0); break;
  case 18: timedTexture(GU_PSM_8888, 1, 256, 256, texture, 1), offset = 0.5f; break;
  case 19: case 20: timedTexture(GU_PSM_8888, 1, 64, 256, texture, c == 20), texels = 64; break;
  case 21: case 22: timedTexture(GU_PSM_8888, 1, 256, 256, texture, c == 22), size = 128; break;
  case 23: timedTexture(GU_PSM_T8, 1, 256, 256, bytes8, 1), offset = 0.5f; break;
  case 25: case 26:
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    break;
  case 27: case 28: case 29:
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(c == 29 ? GU_TRUE : GU_FALSE);
    break;
  case 30: case 31:
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, c == 30 ? 0 : 255, 0xff);
    break;
  case 32:
    sceGuEnable(GU_STENCIL_TEST);
    sceGuStencilFunc(GU_ALWAYS, 0, 0xff);
    sceGuStencilOp(GU_KEEP, GU_KEEP, GU_INCR);
    break;
  case 33: sceGuEnable(GU_COLOR_LOGIC_OP), sceGuLogicalOp(GU_XOR); break;
  case 34: sceGuEnable(GU_DITHER); break;
  case 35: sceGuPixelMask(0x00ff00ff); break;
  case 36: case 37: case 38: case 39:
    setUp3D(c == 39 ? &lens : &identity, 1);
    sceGuDisable(GU_DEPTH_TEST);
    if(c == 37) sceGuEnable(GU_FOG), sceGuFog(1, 4, 0xff0000);
    if(c >= 38) timedTexture(GU_PSM_8888, 1, 256, 256, texture, 0);
    break;
  case 40: sceGuShadeModel(GU_FLAT); break;
  }
  if(c == 24) sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  if(c == 42) sceGuSendCommandi(0xd3, (GU_COLOR_BUFFER_BIT | GU_STENCIL_BUFFER_BIT | GU_DEPTH_BUFFER_BIT) << 8 | 1);
  if(c >= 36 && c <= 41) {  //two triangles over the target
    FloatVertex* v = sceGuGetMemory(6 * sizeof(FloatVertex));
    for(int n = 0; n < 6; n++) {
      int corner = (int[6]){0, 1, 2, 1, 3, 2}[n], right = corner & 1, bottom = corner >> 1;
      float w = c == 39 && right ? 4 : 1, x = right ? 256 : 0, y = bottom ? 256 : 0;
      unsigned int shade = c == 41 ? 0xff000000u | (right ? 0xff : 0) | (bottom ? 0xff00 : 0) | 0x400000 : color;
      if(c <= 39) v[n] = (FloatVertex){right, bottom, shade, ndcX(x) * w, ndcY(y) * w, c == 39 ? -w : 0};
      else v[n] = (FloatVertex){0, 0, shade, x, y, 0};
    }
    for(int layer = 0; layer < layers; layer++) {
      sceGuDrawArray(GU_TRIANGLES, c <= 39 ? FloatVertexType3D : FloatVertexType, 6, 0, v);
    }
  } else {
    for(int layer = 0; layer < layers; layer++) {
      for(int y = 0; y < 256; y += size) {
        for(int x = 0; x < 256; x += size) {
          timedSprite(x, y, x + size, y + size, offset, offset, texels + offset, texels + offset, color);
        }
      }
    }
  }
  if(c == 42) sceGuSendCommandi(0xd3, 0);
  sceGuFinish();
}

//Sizes: the target's 256x256 pixels tiled by sprites of one size (or two triangles a square), a layer the whole
//tiling once, flat color, for the time a pixel against a primitive's.
static const TimedCase sizeCases[] = {
  {"sprites 2x2", "layers", 3, {1, 2, 4}}, {"sprites 4x4", "layers", 3, {1, 2, 4}},
  {"sprites 8x8", "layers", 3, {1, 2, 4}}, {"sprites 16x16", "layers", 3, {1, 2, 4}},
  {"sprites 32x32", "layers", 3, {1, 2, 4}}, {"sprites 64x64", "layers", 3, {1, 2, 4}},
  {"sprites 128x128", "layers", 3, {1, 2, 4}}, {"sprites 256x1 (rows)", "layers", 3, {1, 2, 4}},
  {"sprites 1x256 (columns)", "layers", 3, {1, 2, 4}}, {"sprites 3x3 (255x255 pixels)", "layers", 3, {1, 2, 4}},
  {"sprites 17x17 (255x255 pixels)", "layers", 3, {1, 2, 4}}, {"triangles, squares 2x2", "layers", 3, {1, 2, 4}},
  {"triangles, squares 4x4", "layers", 3, {1, 2, 4}}, {"triangles, squares 8x8", "layers", 3, {1, 2, 4}},
  {"triangles, squares 16x16", "layers", 3, {1, 2, 4}}, {"triangles, squares 32x32", "layers", 3, {1, 2, 4}},
  {"triangles, squares 64x64", "layers", 3, {1, 2, 4}}, {"triangles, squares 128x128", "layers", 3, {1, 2, 4}},
};
static void buildSizes(int c, int layers) {
  static const short widths[11] = {2, 4, 8, 16, 32, 64, 128, 256, 1, 3, 17};
  static const short heights[11] = {2, 4, 8, 16, 32, 64, 128, 1, 256, 3, 17};
  int triangles = c >= 11, width = triangles ? 2 << (c - 11) : widths[c], height = triangles ? width : heights[c];
  int across = 256 / width, down = 256 / height, per = triangles ? 6 : 2, count = 0;
  Vertex* v = (Vertex*)big;
  for(int j = 0; j < down; j++) {
    for(int i = 0; i < across; i++) {
      short x = i * width, y = j * height, x1 = x + width, y1 = y + height;
      Vertex* t = v + count;
      if(triangles) {
        t[0] = (Vertex){0, 0, 0xffffffff, x, y, 0, 0}, t[1] = (Vertex){0, 0, 0xffffffff, x1, y, 0, 0};
        t[2] = (Vertex){0, 0, 0xffffffff, x, y1, 0, 0}, t[3] = t[1];
        t[4] = (Vertex){0, 0, 0xffffffff, x1, y1, 0, 0}, t[5] = t[2];
      } else {
        t[0] = (Vertex){0, 0, 0xffffffff, x, y, 0, 0}, t[1] = (Vertex){0, 0, 0xffffffff, x1, y1, 0, 0};
      }
      count += per;
    }
  }
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  for(int layer = 0; layer < layers; layer++) {
    for(int first = 0; first < count; first += 49152) {  //(a PRIM draws 65535 vertices at most)
      int part = count - first < 49152 ? count - first : 49152;
      sceGuDrawArray(triangles ? GU_TRIANGLES : GU_SPRITES, VertexType, part, 0, v + first);
    }
  }
  sceGuFinish();
}

//Block transfers (sceGuCopyImage), for the GE's time a byte and a row: from texture (RAM, rows of 256 pixels; 512 for
//the 512-pixel row) or VRAM at Spare (rows of 256), to the target (VRAM, rows of 512) or big (RAM, rows of 256).
static const TimedCase transferCases[] = {
  {"RAM to VRAM, 32-bit", "pixels square (a side)", 4, {16, 64, 128, 256}},
  {"RAM to VRAM, 16-bit", "pixels square (a side)", 4, {16, 64, 128, 256}},
  {"VRAM to VRAM, 32-bit", "pixels square (a side)", 4, {16, 64, 128, 256}},
  {"VRAM to VRAM, 16-bit", "pixels square (a side)", 4, {16, 64, 128, 256}},
  {"VRAM to RAM, 32-bit", "pixels square (a side)", 4, {16, 64, 128, 256}},
  {"RAM to RAM, 32-bit", "pixels square (a side)", 4, {16, 64, 128, 256}},
  {"RAM to VRAM, 32-bit, 4 pixels wide", "rows", 3, {64, 128, 256}},
  {"RAM to VRAM, 32-bit, one row", "pixels", 3, {64, 256, 512}},
  {"RAM to VRAM, 32-bit, then TEXSYNC", "pixels square (a side)", 4, {16, 64, 128, 256}},
};
static void buildTransfer(int c, int size) {
  int psm = c == 1 || c == 3 ? GU_PSM_5650 : GU_PSM_8888, width = c == 6 ? 4 : size, height = c == 7 ? 1 : size;
  void* ram = texture, *vram = (void*)(0x04000000 + Spare), *target = (void*)(0x04000000 + Target);
  int ramWidth = c == 7 ? 512 : 256;
  sceKernelDcacheWritebackInvalidateAll();  //(no cached line of big to land over what the GE writes there)
  sceGuStart(GU_SEND, timed);
  if(c == 2 || c == 3) sceGuCopyImage(psm, 0, 0, width, height, 256, vram, 0, 0, Stride, target);
  else if(c == 4) sceGuCopyImage(psm, 0, 0, width, height, 256, vram, 0, 0, 256, big);
  else if(c == 5) sceGuCopyImage(psm, 0, 0, width, height, 256, ram, 0, 0, 256, big);
  else sceGuCopyImage(psm, 0, 0, width, height, ramWidth, ram, 0, 0, Stride, target);
  if(c == 8) sceGuTexSync();
  sceGuFinish();
}

//The texture cache: cases 0-47 a 256x256 sprite tiled by a texture side x side texels (8 to 256; repeating, nearest)
//in 8888, 5650, CLUT8 or CLUT4, swizzled or not, in RAM, flushed as each list starts: the time a pixel as the
//texture outgrows the cache. Cases 48-55 the same texture again or a new one: `count` sprites (32x32 pixels, 8 to a
//row; 64x64 in 54 and 55, 4 to a row) each from a texture as big as itself.
#define CACHE_SIDES(format, layout)                                                                                  \
  {format ", " layout ", 8x8", "layers", 2, {1, 3}}, {format ", " layout ", 16x16", "layers", 2, {1, 3}},            \
  {format ", " layout ", 32x32", "layers", 2, {1, 3}}, {format ", " layout ", 64x64", "layers", 2, {1, 3}},          \
  {format ", " layout ", 128x128", "layers", 2, {1, 3}}, {format ", " layout ", 256x256", "layers", 2, {1, 3}}
#define CACHE_FORMAT(format) CACHE_SIDES(format, "not swizzled"), CACHE_SIDES(format, "swizzled")
static const TimedCase cacheCases[] = {
  CACHE_FORMAT("8888"), CACHE_FORMAT("5650"), CACHE_FORMAT("CLUT8"), CACHE_FORMAT("CLUT4"),
  {"again: one 32x32 8888 texture, set once", "sprites", 2, {16, 64}},
  {"again: the same, TEX_FLUSH before each sprite", "sprites", 2, {16, 64}},
  {"new: a 32x32 8888 texture of its own for each sprite (RAM, 4 KiB apart)", "sprites", 2, {16, 64}},
  {"new: the same in VRAM", "sprites", 2, {16, 64}},
  {"again: a 32x32 CLUT8 texture, its 256-color palette loaded before each sprite", "sprites", 2, {16, 64}},
  {"again: the same, the palette loaded once", "sprites", 2, {16, 64}},
  {"again: one 64x64 8888 texture (16 KiB), set once, 64x64 sprites", "sprites", 2, {16, 64}},
  {"again: the same, TEX_FLUSH before each sprite", "sprites", 2, {16, 64}},
};
static void buildCache(int c, int size) {
  static const int formats[4] = {GU_PSM_8888, GU_PSM_5650, GU_PSM_T8, GU_PSM_T4};
  static const int smallest[4] = {4, 8, 16, 32};  //texels in 16 bytes: the narrowest buffer width
  timedList(GU_PSM_8888);
  if(c < 48) {
    int f = c / 12, side = 8 << c % 6;
    const void* texels = f == 0 ? (const void*)texture : f == 1 ? (const void*)texture16 : bytes8;
    timedTexture(formats[f], c / 6 % 2, side, side > smallest[f] ? side : smallest[f], texels, 0);
    for(int layer = 0; layer < size; layer++) timedSprite(0, 0, 256, 256, 0, 0, 256, 256, 0xffffffff);
  } else {
    int pixels64 = c >= 54, side = pixels64 ? 64 : 32, perRow = 256 / side;
    if(c == 52 || c == 53) timedTexture(GU_PSM_T8, 0, 32, 32, bytes8, 0);
    else timedTexture(GU_PSM_8888, 0, side, side, texture, 0);
    for(int k = 0; k < size; k++) {
      if(c == 49 || c == 55) sceGuTexFlush();
      if(c == 50) sceGuTexImage(0, 32, 32, 32, texture + k * 1024);
      if(c == 51) sceGuTexImage(0, 32, 32, 32, (void*)(0x04000000 + Spare + k * 4096));
      if(c == 52) sceGuClutLoad(32, palette);
      float x = (k % perRow) * side, y = (k / perRow % perRow) * side;
      timedSprite(x, y, x + side, y + side, 0, 0, side, side, 0xffffffff);
    }
  }
  sceGuFinish();
}

//A SIGNAL's time: `layers` flat layers, a SIGNAL (GU_BEHAVIOR_CONTINUE: the GE goes on), as many layers again, then
//the FINISH. Each run's third time is the signal callback's (part 64: the core gives a SIGNAL at once).
static const TimedCase signalCases[] = {
  {"flat layers, a SIGNAL that lets the GE go on, as many flat layers", "layers each side", 3, {1, 4, 16}},
};
static void buildSignal(int c, int layers) {
  (void)c;
  timedList(GU_PSM_8888);
  for(int layer = 0; layer < layers; layer++) timedSprite(0, 0, 256, 256, 0, 0, 0, 0, 0xffffffff);
  sceGuSignal(GU_BEHAVIOR_CONTINUE, 1);
  for(int layer = 0; layer < layers; layer++) timedSprite(0, 0, 256, 256, 0, 0, 0, 0, 0xffffffff);
  sceGuFinish();
}

//The driver's statuses while the GE draws (part 64: what a list GU ends reads as between the GE's time and its FINISH's
//interrupt, held off, is unmeasured). Part 1: 16 layers of an 8888 texture in RAM, not swizzled (the slowest fill here)
//queued alone, and every 50 microseconds or so sceGeListSync(its id, 1) and sceGeDrawSync(1), to 4 looks after the
//latter says done (1024 looks at most, about 50 ms). Part 2: a GU list (GU_DIRECT, its stall address moving as it's
//built) whose 8 such layers are drawn with interrupts held off, then sceGuFinish (the stall address just past its END,
//as GU leaves it), still held off: every 160 microseconds sceGeDrawSync(1), for 20 ms; then interrupts let through, and
//the list's end waited for. The .bin: part 1's looks, the finish callback's time (or 0xffffffff), then the looks as
//(time, sceGeListSync, sceGeDrawSync), 1024 places; part 2's looks, how long interrupts were held off, sceGeDrawSync(1)
//just after, the time the list was done, then the looks as (time, sceGeDrawSync), 128 places; all times in microseconds
//from just before the list was queued (part 1) or interrupts held off (part 2), places unused 0xffffffff.
static void spinUntil(unsigned int began, unsigned int time) {
  while(sceKernelGetSystemTimeLow() - began < time) continue;
}
static void geStatuses(const char* name) {
  if(!beginTest(name)) return;
  timingStart();
  prepareTexels();
  enum { Looks = 1024, Held = 128 };  //part 1's places and part 2's
  for(int n = 0; n < 2 + Looks * 3 + 4 + Held * 2; n++) pixels[n] = Unfinished;
  timedList(GU_PSM_8888);
  timedTexture(GU_PSM_8888, 0, 256, 256, texture, 0);
  for(int layer = 0; layer < 16; layer++) timedSprite(0, 0, 256, 256, 0, 0, 256, 256, 0xffffffff);
  sceGuFinish();
  while(sceKernelPollSema(timingSemaphore, 1) >= 0) continue;
  finished = 0;
  unsigned int began = sceKernelGetSystemTimeLow();
  int id = sceGeListEnQueue(TIMED, 0, timingCallbacks, 0), looks = 0, after = 0;
  while(id >= 0 && looks < Looks) {
    unsigned int time = sceKernelGetSystemTimeLow() - began;
    int list = sceGeListSync(id, 1), draw = sceGeDrawSync(1);
    pixels[2 + looks * 3] = time, pixels[3 + looks * 3] = list, pixels[4 + looks * 3] = draw;
    looks++;
    if((draw == 0 && ++after >= 4) || time >= Patience) break;
    spinUntil(began, time + 50);
  }
  pixels[0] = looks;
  while(id >= 0 && !finished && sceKernelGetSystemTimeLow() - began < Patience) sceKernelDelayThread(1000);
  if(finished) pixels[1] = finishedAt - began;
  waitForGe("16 layers of an 8888 texture in RAM, queued alone");
  unsigned int* part2 = pixels + 2 + Looks * 3;
  Vertex* v = (Vertex*)big;
  v[0] = (Vertex){0, 0, 0xffffffff, 0, 0, 0, 0}, v[1] = (Vertex){256, 256, 0xffffffff, 256, 256, 0, 0};
  sceKernelDcacheWritebackAll();
  start(GU_PSM_8888);
  timedTexture(GU_PSM_8888, 0, 256, 256, texture, 0);
  unsigned int flags = sceKernelCpuSuspendIntr();
  began = sceKernelGetSystemTimeLow();
  for(int layer = 0; layer < 8; layer++) sceGuDrawArray(GU_SPRITES, VertexType, 2, 0, v);
  sceGuFinish();
  looks = 0;
  while(looks < Held) {
    unsigned int time = sceKernelGetSystemTimeLow() - began;
    if(time >= 20000) break;
    part2[4 + looks * 2] = time, part2[5 + looks * 2] = sceGeDrawSync(1);
    looks++;
    spinUntil(began, time + 160);
  }
  part2[1] = sceKernelGetSystemTimeLow() - began;
  sceKernelCpuResumeIntr(flags);
  part2[2] = sceGeDrawSync(1);
  waitForGe("8 layers of an 8888 texture in RAM, drawn with interrupts held off, then GU's FINISH and END");
  part2[3] = sceKernelGetSystemTimeLow() - began;
  part2[0] = looks;
  save(name, pixels, (2 + Looks * 3 + 4 + Held * 2) * 4);
}

static void writeManifest6(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest6.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 6 (tools/psp-measure/ge.c in Phobos says what each test draws; docs/psp-core.md,\n"
    "part 66, what each is for). Pictures: the target's pixels after one test, a little-endian 32-bit word each, row\n"
    "by row, 256x256.\n"
    "light-products-0..3: a lit point a pixel, row y's light (y, 255 - y, y) on column x's material (x, x, 255 - x),\n"
    "normals (0, 0, 1), (33, 0, 56), (65, 0, 72), (101, 0, 26). light-sweep-0..7: 1024 normals in 8x8 blocks, each\n"
    "with 64 products (light per row, material per pixel, levels 96-255 from a fixed sequence, white in each block's\n"
    "first row and column). light-sum: 1 to 4 lights alike, 64 rows each.\n"
    "recip-pixels, recip-projected: texels showing the GE's reciprocal at pixels (the chord's low 8 bits).\n"
    "recip-vertex-0..2, recip-sprites: 4096 cells of 4x4 pixels at one w each (in [1, 2)), u 512.\n"
    "texel-range: sprites whose texture coordinates are +-2^k plus a known part, k 8 to 23 (red: u mod 256).\n"
    "patch-cull: 256 patches (PATCH_CULL_ENABLE, PATCH_FACING, mirroring, normals, culling, lighting, points);\n"
    "patch-cull-edges: 64 patches against their cells' scissor rectangles, the near and far planes.\n"
    "curves-count-v: points added up along v at 16, 63 and 64 divisions (columns 0-2), -v-65, -v-100, -v-128,\n"
    "-v-200, -v-255 at that many (columns 3-7), one to a test.\n"
    "time-*: the GE's drawing time. Records of words: case, size, CPU MHz << 16 | bus MHz, repeats | values << 16,\n"
    "then per run `values` times in microseconds from just before sceGeListEnQueue: to the finish callback, to\n"
    "sceGeListEnQueue's return, (time-signals) to the signal callback; 0xffffffff for a run not finished (a stall,\n"
    "see <name>.stalled; a list sceGeListEnQueue refused: 0xffffffff, then its error). Each test at 222/111 MHz, then\n"
    "at 333/166 (the clocks as found put back after). The cases (case: what; size):\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  static const struct { const char* test; const TimedCase* cases; int count; } tests[] = {
    {"time-commands", commandCases, sizeof(commandCases) / sizeof(commandCases[0])},
    {"time-primitives", primitiveCases, sizeof(primitiveCases) / sizeof(primitiveCases[0])},
    {"time-fill", fillCases, sizeof(fillCases) / sizeof(fillCases[0])},
    {"time-sizes", sizeCases, sizeof(sizeCases) / sizeof(sizeCases[0])},
    {"time-transfers", transferCases, sizeof(transferCases) / sizeof(transferCases[0])},
    {"time-texcache", cacheCases, sizeof(cacheCases) / sizeof(cacheCases[0])},
    {"time-signals", signalCases, sizeof(signalCases) / sizeof(signalCases[0])},
  };
  char line[200];
  for(unsigned int t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
    for(int c = 0; c < tests[t].count; c++) {
      int length = snprintf(line, sizeof(line), "%s %d: %s; %s\n", tests[t].test, c, tests[t].cases[c].name,
                            tests[t].cases[c].unit);
      sceIoWrite(file, line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
    }
  }
  static const char end[] =
    "ge-statuses: the driver's statuses while a list draws, and with interrupts held off (layout in ge.c).\n"
    "<name>.stalled: the GE hadn't finished the test's list after 5 s and was reset (manifest4.txt says more).\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, end, sizeof(end) - 1);
  sceIoClose(file);
}

static void round6(void) {
  foundCpu = scePowerGetCpuClockFrequencyInt();
  foundBus = scePowerGetBusClockFrequencyInt();
  //The bus runs at half the PLL (333 MHz gives 166), which pspsdk has no call to read.
  foundPll = foundCpu > 2 * foundBus ? foundCpu : 2 * foundBus;
  writeManifest6();
  char name[32];
  for(int set = 0; set < 4; set++) {
    snprintf(name, sizeof(name), "light-products-%d", set);
    lightProducts(name, set);
  }
  for(int set = 0; set < 8; set++) {
    snprintf(name, sizeof(name), "light-sweep-%d", set);
    lightSweep(name, set);
  }
  lightSum("light-sum");
  recipPixels("recip-pixels");
  recipProjected("recip-projected");
  recipVertex("recip-vertex-0", 0, 0, 0);
  recipVertex("recip-vertex-1", 1, 0, 0);
  recipVertex("recip-vertex-2", 0, 0xff, 0);
  recipVertex("recip-sprites", 0, 0, 1);
  texelRange("texel-range");
  patchCull("patch-cull");
  patchCullEdges("patch-cull-edges");
  timeCases("time-commands", commandCases, sizeof(commandCases) / sizeof(commandCases[0]), 16, 2, buildCommands);
  timeCases("time-primitives", primitiveCases, sizeof(primitiveCases) / sizeof(primitiveCases[0]), 16, 2,
            buildPrimitives);
  prepareTexels();
  timeCases("time-fill", fillCases, sizeof(fillCases) / sizeof(fillCases[0]), 8, 2, buildFill);
  timeCases("time-sizes", sizeCases, sizeof(sizeCases) / sizeof(sizeCases[0]), 8, 2, buildSizes);
  prepareTexels();
  timeCases("time-transfers", transferCases, sizeof(transferCases) / sizeof(transferCases[0]), 8, 2, buildTransfer);
  prepareTexels();
  timeCases("time-texcache", cacheCases, sizeof(cacheCases) / sizeof(cacheCases[0]), 8, 2, buildCache);
  timeCases("time-signals", signalCases, sizeof(signalCases) / sizeof(signalCases[0]), 16, 3, buildSignal);
  geStatuses("ge-statuses");
  for(int test = 0; test < CountTestsV; test++) {
    curvesCountV(countTestsV[test].name, countTestsV[test].first, countTestsV[test].last);
  }
}

//---- round 7: anti-aliased lines (docs/psp-core.md, part 78), which round 4's lines-smooth pinned only for white,
//opaque lines falling to the right, blended over black, untextured, in through mode

//The line of cell (i, k) (cell = k * 16 + i) of the 16x16 cells, its two vertices at v: from (8 - dx / 2 + i / 16,
//8 - dy / 2 + j / 16) pixels into the cell, so every direction fits, to (dx, dy) further; its color, and its texture
//coordinates from (u0, v0) to (u1, v1). A line and its reverse at the same i and j are the same segment.
static void aaCell(FloatVertex* v, int cell, int j, float dx, float dy, unsigned int color, float u0, float v0,
                   float u1, float v1) {
  int i = cell & 15, k = cell >> 4;
  float x = i * 16 + 8 - dx / 2 + i / 16.0f, y = k * 16 + 8 - dy / 2 + j / 16.0f;
  v[0] = (FloatVertex){u0, v0, color, x, y, 0};
  v[1] = (FloatVertex){u1, v1, color, x + dx, y + dy, 0};
}

//Starts an anti-aliased test: the target filled, a list begun, ANTI_ALIAS_ENABLE on.
static int beginSmooth(const char* name, unsigned int (*fill)(int x, int y)) {
  if(!beginTest(name)) return 0;
  fillTarget(fill, 0);
  start(GU_PSM_8888);
  sceGuEnable(GU_LINE_SMOOTH);
  return 1;
}
static void endSmooth(const char* name) {
  sceGuDisable(GU_LINE_SMOOTH);
  finishList();
  saveTarget(name, 256, 256, 0);
}
static void overBlack(void) {
  sceGuEnable(GU_BLEND);
  sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

//Every way a line can go: row k's lines in direction k (shallow and steep, falling and rising, at two slopes, each
//pair of rows one direction and its reverse: slopes[k]), white blended over black by its alpha (as lines-smooth),
//cell (i, k)'s line i and (k with its low bit clear) sixteenths along (aaCell), so a pair's lines are the same
//segments drawn each way: the coverage of every octant, and which end it's stepped from.
static const float slopes[16][2] = {
  {7.5f, 2.25f}, {-7.5f, -2.25f}, {7.5f, -2.25f}, {-7.5f, 2.25f}, {2.25f, 7.5f}, {-2.25f, -7.5f}, {2.25f, -7.5f},
  {-2.25f, 7.5f}, {7.5f, 5.5f}, {-7.5f, -5.5f}, {7.5f, -5.5f}, {-7.5f, 5.5f}, {5.5f, 7.5f}, {-5.5f, -7.5f},
  {5.5f, -7.5f}, {-5.5f, 7.5f},
};
static void aaSlopes(const char* name) {
  if(!beginSmooth(name, zero)) return;
  overBlack();
  FloatVertex* v = sceGuGetMemory(256 * 2 * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    int k = cell >> 4;
    aaCell(v + cell * 2, cell, k & ~1, slopes[k][0], slopes[k][1], 0xffffffff, 0, 0, 0, 0);
  }
  sceGuDrawArray(GU_LINES, FloatVertexType, 512, 0, v);
  endSmooth(name);
}

//Level, upright and diagonal lines, the directions aa-slopes leaves out: row k in direction axes[k] (rows 0-7 7.5
//pixels long, 6 each way for the diagonals, rows 8-15 4 and 3), each pair of rows one direction and its reverse,
//white over black by its alpha; cell (i, k)'s line i sixteenths along x (aaCell), and a level one i sixteenths down
//too (j = i, else 0): each line i sixteenths across, a level one along its row's edge at i = 0 and through its
//pixels' middles at i = 8, an upright one likewise.
static const float axes[16][2] = {
  {7.5f, 0}, {-7.5f, 0}, {0, 7.5f}, {0, -7.5f}, {6, 6}, {-6, -6}, {6, -6}, {-6, 6},
  {4, 0}, {-4, 0}, {0, 4}, {0, -4}, {3, 3}, {-3, -3}, {3, -3}, {-3, 3},
};
static void aaAxes(const char* name) {
  if(!beginSmooth(name, zero)) return;
  overBlack();
  FloatVertex* v = sceGuGetMemory(256 * 2 * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    int k = cell >> 4;
    aaCell(v + cell * 2, cell, axes[k][1] == 0 ? cell & 15 : 0, axes[k][0], axes[k][1], 0xffffffff, 0, 0, 0, 0);
  }
  sceGuDrawArray(GU_LINES, FloatVertexType, 512, 0, v);
  endSmooth(name);
}

//The vertices' alpha against the coverage: lines-smooth's lines (cell (i, j)'s from (4 + i / 16, 4 + j / 16) pixels
//into the cell to (7.5, 2.25) further), each cell's vertex alpha its number (0 to 255), white blended over black:
//whether the coverage replaces the alpha or scales it.
static void aaAlpha(const char* name) {
  if(!beginSmooth(name, zero)) return;
  overBlack();
  FloatVertex* v = sceGuGetMemory(256 * 2 * sizeof(FloatVertex));
  for(int cell = 0; cell < 256; cell++) {
    float x = (cell & 15) * 16 + 4 + (cell & 15) / 16.0f, y = (cell >> 4) * 16 + 4 + (cell >> 4) / 16.0f;
    unsigned int color = (unsigned int)cell << 24 | 0xffffff;
    v[cell * 2] = (FloatVertex){0, 0, color, x, y, 0};
    v[cell * 2 + 1] = (FloatVertex){0, 0, color, x + 7.5f, y + 2.25f, 0};
  }
  sceGuDrawArray(GU_LINES, FloatVertexType, 512, 0, v);
  endSmooth(name);
}

//Blending with the coverage over a known background (0x80c06020: alpha 0x80): rows 2m and 2m + 1 in mode m below,
//the second with the alpha test (above 0) on; the line 0xff20a0e0, (7.5, 2.25), cell (i, j)'s i and j sixteenths
//along (aaCell). Mode 7 is clear mode (color, stencil and depth written), with blending off.
static unsigned int aaBackground(int x, int y) { (void)x; (void)y; return 0x80c06020; }
static void aaBlend(const char* name) {
  static const int modes[7][3] = {
    {-1, 0, 0}, {GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA}, {GU_ADD, GU_SRC_ALPHA, GU_FIX},
    {GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA}, {GU_REVERSE_SUBTRACT, GU_SRC_ALPHA, GU_FIX},
    {GU_ADD, GU_DST_ALPHA, GU_ONE_MINUS_DST_ALPHA}, {GU_ADD, 6, 7},  //(6, 7: twice the source alpha, 1 minus it)
  };
  if(!beginSmooth(name, aaBackground)) return;
  for(int row = 0; row < 16; row++) {
    int mode = row >> 1;
    if(mode < 7 && modes[mode][0] >= 0) {
      sceGuEnable(GU_BLEND);
      sceGuBlendFunc(modes[mode][0], modes[mode][1], modes[mode][2], 0xffffff, 0xffffff);
    } else {
      sceGuDisable(GU_BLEND);
    }
    if(row & 1) sceGuEnable(GU_ALPHA_TEST), sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    else sceGuDisable(GU_ALPHA_TEST);
    if(mode == 7) sceGuSendCommandi(0xd3, 1 | 7 << 8);  //CLEAR_MODE
    FloatVertex* v = sceGuGetMemory(16 * 2 * sizeof(FloatVertex));
    for(int i = 0; i < 16; i++) aaCell(v + i * 2, row * 16 + i, row, 7.5f, 2.25f, 0xff20a0e0, 0, 0, 0, 0);
    sceGuDrawArray(GU_LINES, FloatVertexType, 32, 0, v);
    if(mode == 7) sceGuSendCommandi(0xd3, 0);
  }
  sceGuDisable(GU_ALPHA_TEST);
  sceGuDisable(GU_BLEND);
  endSmooth(name);
}

//Textured anti-aliased lines over the texture texelAA (texel (x, y) is x | y << 8 | 0x80 << 16, its alpha x ^ y),
//(7.5, 2.25), cell (i, j)'s i and j sixteenths along (aaCell), which texels they take: rows 0-3 every vertex at texel
//(200.5, 100.5), replace, RGB; rows 4-7 from texel (0, 0) to (255, 255), replace, RGBA, over white by the source's
//alpha alone (the fixed 0 for the source, its alpha for the target), so each channel is the alpha: the texel's
//against the coverage; rows 8-11 at (200.5, 100.5), modulate, RGBA, white, blended over black; rows 12-15 at
//(200.5, 100.5), bilinear, replace, RGB. (The frame buffer's alpha is the stencil's, untouched without the stencil
//test: a pixel's alpha shows only through blending.)
static unsigned int texelAA(int x, int y) { return x | y << 8 | 0x80 << 16 | (unsigned int)((x ^ y) & 0xff) << 24; }
static unsigned int aaTexturedFill(int x, int y) { (void)x; return y >= 64 && y < 128 ? 0xffffffff : 0; }
static void aaTextured(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelAA);
  fillTarget(aaTexturedFill, 0);
  start(GU_PSM_8888);
  sceGuEnable(GU_LINE_SMOOTH);
  for(int group = 0; group < 4; group++) {
    useTexture(group == 2 ? GU_TFX_MODULATE : GU_TFX_REPLACE, group == 1 || group == 2 ? GU_TCC_RGBA : GU_TCC_RGB);
    if(group == 3) sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    if(group == 1) sceGuEnable(GU_BLEND), sceGuBlendFunc(GU_ADD, GU_FIX, GU_SRC_ALPHA, 0, 0);
    else if(group == 2) overBlack();
    else sceGuDisable(GU_BLEND);
    FloatVertex* v = sceGuGetMemory(64 * 2 * sizeof(FloatVertex));
    for(int n = 0; n < 64; n++) {
      int cell = group * 64 + n;
      if(group == 1) aaCell(v + n * 2, cell, cell >> 4, 7.5f, 2.25f, 0xffffffff, 0, 0, 255, 255);
      else aaCell(v + n * 2, cell, cell >> 4, 7.5f, 2.25f, 0xffffffff, 200.5f, 100.5f, 200.5f, 100.5f);
    }
    sceGuDrawArray(GU_LINES, FloatVertexType, 128, 0, v);
  }
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_TEXTURE_2D);
  endSmooth(name);
}

//Anti-aliased lines in 3D (lines3D's, without the lens: cell (i, j)'s from (4 + i / 16, 4 + j / 16) pixels into the
//cell to (7.5, 2.25) further): rows 0-7 red to blue, blended over black; rows 8-15 textured (texelAA, every vertex
//at (200.5, 100.5) / 256, replace, RGB), blending off, rows 12-15 with the texture scaled by a half and offset by
//(0.25, 0.125): whether 3D lines' coverage and texels are through mode's, and whether the texels go through the
//scale and offset.
static void aa3D(const char* name) {
  if(!beginTest(name)) return;
  fillTexture(texelAA);
  start3D(&identity, 1);
  sceGuEnable(GU_LINE_SMOOTH);
  for(int quarter = 0; quarter < 4; quarter++) {
    int textured = quarter >= 2;
    if(textured) useTexture(GU_TFX_REPLACE, GU_TCC_RGB), sceGuDisable(GU_BLEND);
    else overBlack();
    sceGuTexScale(quarter == 3 ? 0.5f : 1, quarter == 3 ? 0.5f : 1);
    sceGuTexOffset(quarter == 3 ? 0.25f : 0, quarter == 3 ? 0.125f : 0);
    FloatVertex* v = sceGuGetMemory(64 * 2 * sizeof(FloatVertex));
    for(int n = 0; n < 64; n++) {
      int cell = quarter * 64 + n;
      float x = (cell & 15) * 16 + 4 + (cell & 15) / 16.0f, y = (cell >> 4) * 16 + 4 + (cell >> 4) / 16.0f;
      float u = textured ? 200.5f / 256 : 0, t = textured ? 100.5f / 256 : 0;
      v[n * 2] = (FloatVertex){u, t, textured ? 0xffffffff : 0xff0000ff, ndcX(x), ndcY(y), 0.5f};
      v[n * 2 + 1] = (FloatVertex){u, t, textured ? 0xffffffff : 0xffff0000, ndcX(x + 7.5f), ndcY(y + 2.25f), -0.5f};
    }
    sceGuDrawArray(GU_LINES, FloatVertexType3D, 128, 0, v);
  }
  sceGuTexScale(1, 1);
  sceGuTexOffset(0, 0);
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_TEXTURE_2D);
  endSmooth(name);
}

static void writeManifest7(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest7.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 7 (tools/psp-measure/ge.c in Phobos says what each test draws; docs/psp-core.md,\n"
    "part 78, what each is for): anti-aliased lines (GU_LINE_SMOOTH). Pictures: the target's pixels after one test, a\n"
    "little-endian 32-bit word each, row by row, 256x256, 16x16 cells of a line each. In aa-slopes, aa-axes,\n"
    "aa-blend and aa-textured, cell (i, j)'s line (d its direction) starts (8 - dx / 2 + i / 16, 8 - dy / 2 + j / 16)\n"
    "pixels into the cell; in aa-alpha and aa-3d, (4 + i / 16, 4 + j / 16), going (7.5, 2.25).\n"
    "aa-slopes: white over black by its alpha, row k in direction k: (7.5, 2.25) and (7.5, 5.5) and each turned and\n"
    "mirrored (ge.c's slopes), each pair of rows one direction and its reverse over the same segments (j = k & ~1).\n"
    "aa-axes: as aa-slopes, row k in direction axes[k] (level, upright, diagonal, each way; rows 8-15 shorter), j 0\n"
    "but for level lines (j = i): a level line along its row's edge at i = 0, through the pixels' middles at i = 8.\n"
    "aa-alpha: round 4's lines-smooth with each cell's vertex alpha its number (0-255).\n"
    "aa-blend: 0xff20a0e0 over 0x80c06020 in 8 modes, two rows each (the second with the alpha test above 0): none,\n"
    "src alpha / 1 - src alpha, src alpha / fixed white, fixed white / 1 - src alpha, reverse subtract src alpha /\n"
    "fixed white, dst alpha / 1 - dst alpha, double src alpha / 1 - double src alpha, clear mode.\n"
    "aa-textured: texel (x, y) x | y << 8 | 0x80 << 16, alpha x ^ y; rows 0-3 at texel (200.5, 100.5) replace RGB,\n"
    "4-7 (0, 0) to (255, 255) replace RGBA over white by the alpha alone (fixed 0, source alpha: each channel the\n"
    "alpha), 8-11 (200.5, 100.5) modulate RGBA over black, 12-15 (200.5, 100.5) bilinear replace RGB; blending off\n"
    "but in rows 4-11 (the frame buffer's alpha byte is the stencil's).\n"
    "aa-3d: in 3D (identity matrices), rows 0-7 red to blue over black, 8-15 textured at (200.5, 100.5) / 256,\n"
    "rows 12-15 with the texture scale 0.5 and offset (0.25, 0.125).\n"
    "<name>.stalled: the GE hadn't finished the test's list after 5 s and was reset (manifest4.txt says more).\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  sceIoClose(file);
}

static void round7(void) {
  writeManifest7();
  aaSlopes("aa-slopes");
  aaAxes("aa-axes");
  aaAlpha("aa-alpha");
  aaBlend("aa-blend");
  aaTextured("aa-textured");
  aa3D("aa-3d");
}

//---- round 8: what round 6 left of the GE's time, chosen by what the owner's captures show part 76's model missing
//(docs/psp-core.md, part 107), and the CPU's time beside the GE's (cpu-contention; the CPU's own tests are cpu.c's)

//Each round 8 timing test runs its cases at the clock settings its `settings` bits say: At222 and At333 as round 6's
//(the CPU at 222 MHz with the bus at 111, 333 with 166); At333Cpu222, the CPU at 222 with the bus at 166 (the PLL at
//333), which tells the CPU's clock from the bus's if the PSP grants it; At266, 266 with the bus at 133, a third bus
//clock. Each record's clocks are what scePower gives back, whatever was asked.
enum { At222 = 1, At333 = 2, At333Cpu222 = 4, At266 = 8, AtTwo = At222 | At333, AtAll = 15 };
static void setClocks8(int setting) {
  static const short clocks[4][3] = {{222, 222, 111}, {333, 333, 166}, {333, 222, 166}, {266, 266, 133}};
  scePowerSetClockFrequency(clocks[setting][0], clocks[setting][1], clocks[setting][2]);
}

//A list sceGeListEnQueue refused, noted as a stall is (nothing reset).
static void refused(int queued, const char* what) {
  char text[200];
  snprintf(text, sizeof(text), "sceGeListEnQueue gave 0x%08x for %s.\n", (unsigned int)queued, what);
  noteStall(text);
}

//Waits for `lists` finish callbacks since finishes was zeroed (for Patience at most from `began`), then for the GE to
//stop, as runTimed waits for one; 0 if it didn't (a stall: noted and reset).
static int awaitFinishes(int lists, unsigned int began, const char* what) {
  unsigned int now = sceKernelGetSystemTimeLow();
  while(finishes < lists && now - began < Patience) {
    SceUInt timeout = 2000;
    sceKernelWaitSema(timingSemaphore, 1, &timeout);
    now = sceKernelGetSystemTimeLow();
  }
  int state = sceGeDrawSync(1);
  while(finishes >= lists && state > 0 && now - began < Patience) {  //the last one's END, just after its FINISH
    sceKernelDelayThread(50);
    state = sceGeDrawSync(1);
    now = sceKernelGetSystemTimeLow();
  }
  if(finishes < lists || state > 0) {
    resetGe(now - began, state, what);
    return 0;
  }
  return 1;
}

//A run of a case that isn't one list queued alone (runTimed's): times[0-3] as the test's .bin has them, 0 if the GE
//didn't finish (a stall: reset).
typedef int (*TimedRun)(int c, int size, const char* what, unsigned int* times);

//Round 6's timeCases, at the settings asked, each run by `run` (or runTimed, if none).
static void timeCases8(const char* name, const TimedCase* cases, int count, int repeats, int values, int settings,
                       void (*build)(int c, int size), TimedRun run) {
  if(!beginTest(name)) return;
  timingStart();
  recorded = 0;
  int stalls = 0, done = 0, total = 0;
  for(int setting = 0; setting < 4; setting++) total += (settings >> setting & 1) * count;
  for(int setting = 0; setting < 4; setting++) {
    if(!(settings >> setting & 1)) continue;
    setClocks8(setting);
    unsigned int clocks = scePowerGetCpuClockFrequencyInt() << 16 | scePowerGetBusClockFrequencyInt();
    for(int c = 0; c < count; c++) {
      for(int s = 0; s < cases[c].count; s++) {
        int size = cases[c].sizes[s], ready = stalls < 3 && timingCallbacks >= 0 && timingSemaphore >= 0;
        char what[200];
        snprintf(what, sizeof(what), "%s case %d (%s), size %d, at %u/%u MHz", name, c, cases[c].name, size,
                 clocks >> 16, clocks & 0xffff);
        if(ready) build(c, size);
        record(c), record(size), record(clocks), record(repeats | values << 16);
        for(int r = 0; r < repeats; r++) {
          unsigned int times[4] = {Unfinished, Unfinished, Unfinished, Unfinished};
          if(ready && !(run ? run(c, size, what, times) : runTimed(what, times))) ready = 0, stalls++;
          for(int k = 0; k < values; k++) record(times[k]);
        }
      }
      progress(&current, name, ++done, total);
    }
  }
  setClocks(-1);
  save(name, pixels, recorded * 4);
}

//-- lists (time-lists): what a list costs beside what it draws (part 76's List, 2170 clocks, from fits whose shortest
//lists came out 25-45% short), lists queued back to back, a list drawn while its stall address is moved on (a PSP's
//CPU builds a game's list as the GE draws it), and CALLs of small lists, as Super Monkey Ball Adventure draws (some
//3,500 a list). Each run's third time: case 1's when the last sceGeListEnQueue returned, cases 2's and 3's when the
//last sceGeListUpdateStallAddr returned, 0xffffffff for the rest.
static const TimedCase listCases[] = {
  {"a list of N sprites 4x4 pixels, flat", "sprites", 4, {0, 4, 16, 64}},
  {"N lists of 16 such sprites queued at once, timed to the last one's finish", "lists", 4, {1, 2, 4, 8}},
  {"64 sprites 256x16 (4 layers), the stall address moved on N times, as fast as the CPU goes", "pieces", 4,
   {1, 4, 16, 64}},
  {"the same, each move once the GE has reached the stall address", "pieces", 4, {1, 4, 16, 64}},
  {"N CALLs of a list drawing one 4x4 sprite (then its RET)", "calls", 4, {16, 64, 256, 1024}},
};
enum { ListSpacing = 16384, Called = 250000 };  //words between case 1's lists in timed; where the CALLed list is
//Cases 2 and 3: where each piece of the list ends (the last: past its END), as cached addresses (timed's, not
//TIMED's): sceGeListSync(id, 1) says the GE has reached the stall address only when the address it was given is the
//GE's own, which the driver keeps with its top three bits cut (uOFW's sceGeListSync), so an uncached one never
//matches; the GE reads the list the same either way. (Round 8 gave TIMED's: case 3's first wait ran out the run's 5 s,
//its other moves came at once, and awaitFinishes, on the same budget, took the GE still drawing them for a stall.)
static unsigned int pieceEnds[64];
static void buildLists(int c, int size) {
  for(int k = 0; k < (c == 1 ? size : 1); k++) {
    sceGuStart(GU_SEND, timed + k * ListSpacing);
    setUp(GU_PSM_8888);
    if(c == 0 || c == 1) {
      for(int i = 0; i < (c == 0 ? size : 16); i++) {
        timedSprite(spotX(i), spotY(i), spotX(i) + 4, spotY(i) + 4, 0, 0, 0, 0, 0xffffffff);
      }
    } else if(c == 2 || c == 3) {
      for(int i = 0; i < 64; i++) {
        timedSprite(0, (i & 15) * 16, 256, (i & 15) * 16 + 16, 0, 0, 0, 0, 0xffffffff);
        int piece = (i + 1) / (64 / size) - 1;
        if((i + 1) % (64 / size) == 0 && piece < size - 1) pieceEnds[piece] = (unsigned int)timed + sceGuCheckList();
      }
    } else {
      for(int i = 0; i < size; i++) sceGuCallList(timed + Called);
    }
    int bytes = sceGuFinish();
    if(c == 2 || c == 3) pieceEnds[size - 1] = (unsigned int)timed + bytes;
  }
  if(c == 4) {  //the CALLed list (GU_CALL's ends with RET)
    sceGuStart(GU_CALL, timed + Called);
    timedSprite(0, 0, 4, 4, 0, 0, 0, 0, 0xffffffff);
    sceGuFinish();
  }
}
static int runLists(int c, int size, const char* what, unsigned int* times) {
  if(c == 0 || c == 4) return runTimed(what, times);
  int lists = c == 1 ? size : 1, id = -1;
  while(sceKernelPollSema(timingSemaphore, 1) >= 0) continue;
  finishes = 0;
  unsigned int began = sceKernelGetSystemTimeLow(), after = began;
  for(int k = 0; k < lists; k++) {
    id = sceGeListEnQueue(TIMED + k * ListSpacing, c == 1 ? 0 : (void*)pieceEnds[0], timingCallbacks, 0);
    if(k == 0) after = sceKernelGetSystemTimeLow();
    if(id < 0) {
      refused(id, what);
      awaitFinishes(k, began, what);
      times[1] = id;
      return 0;
    }
  }
  //Each wait for "stall reached" has Patience from the move before it, and the list's end Patience from the last
  //move, so a wait that runs out is noted as that, not taken for the GE's stall (as round 8's were).
  unsigned int last = sceKernelGetSystemTimeLow();
  int reached = 1;
  for(int piece = 1; c != 1 && piece < size; piece++) {
    while(c == 3 && reached && sceGeListSync(id, 1) != PSP_GE_LIST_STALL_REACHED) {
      if(sceKernelGetSystemTimeLow() - last >= Patience) reached = 0;
    }
    sceGeListUpdateStallAddr(id, (void*)pieceEnds[piece]);
    last = sceKernelGetSystemTimeLow();
  }
  if(!awaitFinishes(lists, last, what)) return 0;
  if(!reached) {  //(the list done, but its time holds a wait that ran out: not kept)
    char text[240];
    snprintf(text, sizeof(text), "sceGeListSync(id, 1) didn't say the GE had reached a stall address within %d ms: "
             "%s.\n", Patience / 1000, what);
    noteStall(text);
    return 0;
  }
  times[0] = finishedAt - began, times[1] = after - began, times[2] = last - began;
  return 1;
}

//-- a SIGNAL against its list's end (time-signals2): N primitives, then a SIGNAL that lets the GE go on, then the
//FINISH; each run's third time the signal callback's. Round 6's time-signals had the callback within 84
//microseconds of queueing after 16 sprites over the whole target, their drawing up to 10 ms (the GE's front end
//handing them on to its back end), and Army of Two's came 0.27 ms before its list's FINISH after some 28,000
//primitives: how far ahead of the drawing the front end gets (part 106's GE::Cost::Queue, 16 for now), by the
//primitives' count, their size and their kind.
static const TimedCase signalCases2[] = {
  {"N sprites 64x64, flat, in turn over 256x256 of the target, then a SIGNAL that lets the GE go on, then the FINISH",
   "sprites", 4, {8, 16, 32, 128}},
  {"the same with sprites 8x8", "sprites", 4, {16, 64, 256, 1024}},
  {"the same with triangles, each half of a 64x64 square", "triangles", 4, {8, 16, 32, 128}},
};
static void buildSignals2(int c, int size) {
  timedList(GU_PSM_8888);
  int side = c == 1 ? 8 : 64, across = 256 / side;
  for(int i = 0; i < size; i++) {
    float x = (float)(i % across * side), y = (float)(i / across % across * side);
    if(c != 2) {
      timedSprite(x, y, x + side, y + side, 0, 0, 0, 0, 0xffffffff);
      continue;
    }
    FloatVertex* v = sceGuGetMemory(3 * sizeof(FloatVertex));
    v[0] = (FloatVertex){0, 0, 0xffffffff, x, y, 0};
    v[1] = (FloatVertex){0, 0, 0xffffffff, x + side, y, 0};
    v[2] = (FloatVertex){0, 0, 0xffffffff, x, y + side, 0};
    sceGuDrawArray(GU_TRIANGLES, FloatVertexType, 3, 0, v);
  }
  sceGuSignal(GU_BEHAVIOR_CONTINUE, 1);
  sceGuFinish();
}

//-- state between draws (time-state): 8x8 sprites over the target, each from a 16x16 texture (1 KiB, held whole by
//the cache), with one thing set before each, as games set state between draws (Super Monkey Ball Adventure: some
//84,000 words and 3,500 CALLs a list): whether a setting costs its words alone or holds the GE up (a pipeline drained
//before a texture or a palette changes, say).
static const TimedCase stateCases[] = {
  {"nothing between the sprites", "sprites", 2, {256, 1024}},
  {"TEX_FLUSH before each", "sprites", 2, {256, 1024}},
  {"the texture changed before each (two 16x16 textures in turn: its address, width, size, TEX_FLUSH)", "sprites", 2,
   {256, 1024}},
  {"the same texture set again before each", "sprites", 2, {256, 1024}},
  {"a 256-color palette loaded before each (a CLUT8 16x16 texture)", "sprites", 2, {256, 1024}},
  {"a 16-color palette loaded before each (a CLUT4 16x16 texture)", "sprites", 2, {256, 1024}},
  {"TEX_MODE before each, swizzling on and off in turn (GU's sceGuTexMode, with the TEX_FLUSH it adds)", "sprites", 2,
   {256, 1024}},
  {"the blend function changed before each (blending on, two functions in turn)", "sprites", 2, {256, 1024}},
  {"the depth function changed before each (ALWAYS and GEQUAL in turn, passing, not writing)", "sprites", 2,
   {256, 1024}},
  {"the world matrix set before each (12 words)", "sprites", 2, {256, 1024}},
  {"the view matrix set before each", "sprites", 2, {256, 1024}},
  {"the projection matrix set before each (16 words)", "sprites", 2, {256, 1024}},
  {"a bone matrix set before each", "sprites", 2, {256, 1024}},
  {"TEX_SYNC before each", "sprites", 2, {256, 1024}},
  {"the alpha test on and off in turn (ALWAYS)", "sprites", 2, {256, 1024}},
  {"each sprite's PRIM in a CALLed list of its own (its VTYPE and VADDR set before the CALL)", "sprites", 2,
   {256, 1024}},
  {"3D: each an 8x8 square of two textured triangles, the world matrix set before each", "sprites", 2, {256, 1024}},
};
static void buildState(int c, int count) {
  FloatVertex* v = (FloatVertex*)big;
  for(int k = 0; k < count; k++) {
    float x = (k & 63) * 8, y = (k >> 6 & 31) * 8;
    if(c == 16) {  //3D, within the 256x256 the viewport covers
      x = (k & 31) * 8, y = (k >> 5 & 31) * 8;
      for(int n = 0; n < 6; n++) {
        int corner = (int[6]){0, 1, 2, 1, 3, 2}[n], right = corner & 1, bottom = corner >> 1;
        v[k * 6 + n] =
          (FloatVertex){right * 0.5f, bottom * 0.5f, 0xffffffff, ndcX(x + right * 8), ndcY(y + bottom * 8), 0};
      }
    } else {
      v[k * 2] = (FloatVertex){0, 0, 0xffffffff, x, y, 0};
      v[k * 2 + 1] = (FloatVertex){8, 8, 0xffffffff, x + 8, y + 8, 0};
    }
  }
  unsigned int* called = TIMED + Called;  //case 15's list: a PRIM of two sprite corners, then RET
  called[0] = 0x04u << 24 | GU_SPRITES << 16 | 2, called[1] = 0x0bu << 24;
  if(c == 8) fillDepth(0);
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  if(c == 16) setUp3D(&identity, 1), sceGuDisable(GU_DEPTH_TEST);
  if(c == 4) timedTexture(GU_PSM_T8, 0, 16, 16, bytes8, 0);
  else if(c == 5) timedTexture(GU_PSM_T4, 0, 16, 32, bytes8, 0);  //(a CLUT4 texture's rows are 16 bytes at least)
  else timedTexture(GU_PSM_8888, 0, 16, 16, texture, 0);
  if(c == 7) sceGuEnable(GU_BLEND);
  if(c == 8) sceGuEnable(GU_DEPTH_TEST), sceGuDepthMask(GU_TRUE);
  if(c == 14) sceGuAlphaFunc(GU_ALWAYS, 0, 0xff);
  for(int k = 0; k < count; k++) {
    switch(c) {
    case 1: sceGuTexFlush(); break;
    case 2: sceGuTexImage(0, 16, 16, 16, texture + (k & 1) * 256); break;
    case 3: sceGuTexImage(0, 16, 16, 16, texture); break;
    case 4: sceGuClutLoad(32, palette); break;
    case 5: sceGuClutLoad(2, palette); break;
    case 6: sceGuTexMode(GU_PSM_8888, 0, 0, k & 1); break;
    case 7: sceGuBlendFunc(GU_ADD, k & 1 ? GU_SRC_ALPHA : GU_FIX, k & 1 ? GU_ONE_MINUS_SRC_ALPHA : GU_FIX, 0xffffff, 0);
            break;
    case 8: sceGuDepthFunc(k & 1 ? GU_GEQUAL : GU_ALWAYS); break;
    case 9: case 16: sceGuSetMatrix(GU_MODEL, &identity); break;
    case 10: sceGuSetMatrix(GU_VIEW, &identity); break;
    case 11: sceGuSetMatrix(GU_PROJECTION, &identity); break;
    case 12: sceGuBoneMatrix(k & 7, &identity); break;
    case 13: sceGuTexSync(); break;
    case 14: if(k & 1) sceGuEnable(GU_ALPHA_TEST); else sceGuDisable(GU_ALPHA_TEST); break;
    }
    if(c == 15) {
      unsigned int at = (unsigned int)(v + k * 2);
      sceGuSendCommandi(0x12, FloatVertexType);  //VTYPE
      sceGuSendCommandi(0x10, at >> 8 & 0xf0000);  //BASE
      sceGuSendCommandi(0x01, at & 0xffffff);  //VADDR
      sceGuCallList(called);
    } else if(c == 16) {
      sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, v + k * 6);
    } else {
      sceGuDrawArray(GU_SPRITES, FloatVertexType, 2, 0, v + k * 2);
    }
  }
  sceGuFinish();
}

//-- vertices (time-vertices): the GE's front end alone, each part of a vertex and each kind of work on it varied
//from a baseline, a strip of triangles in 3D (identity matrices), every one a back face and culled, so the back end
//has nothing to draw (part 76: a culled triangle costs its vertices alone). Through mode doesn't cull: its case draws
//triangles covering no pixel's middle, round 6's way.
static const TimedCase vertexCases[] = {
  {"a strip, float positions", "triangles", 3, {1024, 4096, 16384}},
  {"a strip, 16-bit positions", "triangles", 3, {1024, 4096, 16384}},
  {"a strip, 8-bit positions", "triangles", 3, {1024, 4096, 16384}},
  {"a strip, float positions and an 8888 color", "triangles", 3, {1024, 4096, 16384}},
  {"a strip, float positions and a 5650 color", "triangles", 3, {1024, 4096, 16384}},
  {"a strip, float positions and a float normal (unlit)", "triangles", 3, {1024, 4096, 16384}},
  {"a strip, float positions and an 8-bit normal (unlit)", "triangles", 3, {1024, 4096, 16384}},
  {"a strip, float positions and float texture coordinates (no texture)", "triangles", 3, {1024, 4096, 16384}},
  {"a strip, float positions and 16-bit texture coordinates (no texture)", "triangles", 3, {1024, 4096, 16384}},
  {"a triangle list, three vertices each", "triangles", 3, {1024, 4096, 16384}},
  {"a triangle fan", "triangles", 3, {1024, 4096, 16384}},
  {"the triangle list through 8-bit indices, 85 triangles a draw", "triangles", 3, {1020, 4080, 16320}},
  {"the triangle list through 16-bit indices, 85 triangles a draw", "triangles", 3, {1020, 4080, 16320}},
  {"16-bit indices, every triangle the same three vertices", "triangles", 3, {1024, 4096, 16384}},
  {"16-bit indices over a grid of 17x17 vertices, each in six triangles (512 triangles a draw)", "triangles", 3,
   {1024, 4096, 16384}},
  {"a strip lit by 1 directional light, diffuse (float normals)", "triangles", 3, {1024, 4096, 16384}},
  {"2 directional lights", "triangles", 3, {1024, 4096, 16384}},
  {"4 directional lights", "triangles", 3, {1024, 4096, 16384}},
  {"1 point light", "triangles", 3, {1024, 4096, 16384}},
  {"4 point lights", "triangles", 3, {1024, 4096, 16384}},
  {"1 spotlight", "triangles", 3, {1024, 4096, 16384}},
  {"4 spotlights", "triangles", 3, {1024, 4096, 16384}},
  {"1 directional light with its shine (specular)", "triangles", 3, {1024, 4096, 16384}},
  {"4 directional lights with their shine", "triangles", 3, {1024, 4096, 16384}},
  {"skinned: 1 bone, float weights", "triangles", 3, {1024, 4096, 16384}},
  {"skinned: 2 bones", "triangles", 3, {1024, 4096, 16384}},
  {"skinned: 4 bones", "triangles", 3, {1024, 4096, 16384}},
  {"skinned: 8 bones", "triangles", 3, {1024, 4096, 16384}},
  {"skinned: 4 bones, 16-bit weights", "triangles", 3, {1024, 4096, 16384}},
  {"skinned: 4 bones, 8-bit weights", "triangles", 3, {1024, 4096, 16384}},
  {"morphed: 2 vertices each", "triangles", 3, {1024, 4096, 16384}},
  {"morphed: 4", "triangles", 3, {1024, 4096, 16384}},
  {"morphed: 8", "triangles", 3, {1024, 4096, 16384}},
  {"texture coordinates through the texture matrix (float coordinates, a 16x16 texture)", "triangles", 3,
   {1024, 4096, 16384}},
  {"texture coordinates projected from the positions", "triangles", 3, {1024, 4096, 16384}},
  {"environment mapping (2 directional lights, float normals)", "triangles", 3, {1024, 4096, 16384}},
  {"fog", "triangles", 3, {1024, 4096, 16384}},
  {"through mode: a triangle list covering no pixel's middle (float positions)", "triangles", 3, {1024, 4096, 16384}},
};
//A vertex's parts, in the GE's order, each of 0 (none), 1, 2 or 4 bytes (4: a float; a color: 2 is 5650, 4 8888):
//`weights` skinning weights, texture coordinates, a color, a normal, a position.
typedef struct { int weights, weight, uv, color, normal, position; } Parts;
static int partBits(int bytes) { return bytes == 1 ? 1 : bytes == 2 ? 2 : bytes == 4 ? 3 : 0; }
static int partsType(const Parts* p) {
  int type = GU_TEXTURE_SHIFT(partBits(p->uv)) | GU_NORMAL_SHIFT(partBits(p->normal)) |
             GU_VERTEX_SHIFT(partBits(p->position));
  if(p->color) type |= p->color == 2 ? GU_COLOR_5650 : GU_COLOR_8888;
  if(p->weights) type |= GU_WEIGHT_SHIFT(partBits(p->weight)) | GU_WEIGHTS(p->weights);
  return type;
}
//A value of `bytes`: a float as it is, 8- and 16-bit ones from -1 to 1 (or 0 to 1) scaled to their range.
static unsigned char* putPart(unsigned char* at, int bytes, float value) {
  if(bytes == 1) *(signed char*)at = (signed char)(value * 127);
  else if(bytes == 2) *(short*)at = (short)(value * 32767);
  else *(float*)at = value;
  return at + bytes;
}
//Writes a vertex of these parts at (x, y, z), each part aligned to its own size and the whole to its largest, as
//the GE reads them; returns where the next goes.
static unsigned char* putVertex(unsigned char* at, const Parts* p, float x, float y, float z) {
  unsigned char* start = at;
  int largest = 1;
#define ALIGN_PART(bytes) \
  (at = start + (((at - start) + (bytes) - 1) & -(bytes)), largest = (bytes) > largest ? (bytes) : largest)
  if(p->weights) {
    ALIGN_PART(p->weight);
    for(int k = 0; k < p->weights; k++) at = putPart(at, p->weight, 1.0f / p->weights);
  }
  if(p->uv) ALIGN_PART(p->uv), at = putPart(putPart(at, p->uv, 0.25f), p->uv, 0.25f);
  if(p->color == 2) ALIGN_PART(2), *(unsigned short*)at = 0xffff, at += 2;
  if(p->color == 4) ALIGN_PART(4), *(unsigned int*)at = 0xffffffff, at += 4;
  if(p->normal) ALIGN_PART(p->normal), at = putPart(putPart(putPart(at, p->normal, 0), p->normal, 0), p->normal, 1);
  ALIGN_PART(p->position);
  at = putPart(putPart(putPart(at, p->position, x), p->position, y), p->position, z);
#undef ALIGN_PART
  return start + (((at - start) + largest - 1) & -largest);
}
//The lights of a lit case: `count` alike of a kind, toward the corners in front of the strip, colored, fading.
static void lightsOn(int kind, int count, int components) {
  sceGuEnable(GU_LIGHTING);
  sceGuAmbient(0x202020);
  sceGuColorMaterial(GU_DIFFUSE);
  sceGuModelColor(0, 0x404040, 0xffffff, 0x808080);
  sceGuSpecular(8);
  for(int k = 0; k < count; k++) {
    ScePspFVector3 at = {k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, 1}, toward = {0, 0, -1};
    sceGuEnable(GU_LIGHT0 + k);
    sceGuLight(k, kind, components, &at);
    sceGuLightAtt(k, 1, 0.5f, 0.25f);
    sceGuLightColor(k, GU_AMBIENT, 0x101010);
    sceGuLightColor(k, GU_DIFFUSE, 0xc0c0c0);
    sceGuLightColor(k, GU_SPECULAR, 0x808080);
    if(kind == GU_SPOTLIGHT) sceGuLightSpot(k, &toward, 4, 0.5f);
  }
}
//Every triangle counterclockwise on the screen, and culled as a back face with the front face clockwise: the strips
//a row of 256 triangles each, left to right, every other vertex 2 pixels down (count / 256 rows 4 pixels apart, a
//draw each); the list round 6's spots, (x, y), (x, y + 2), (x + 2, y); the fan around (128, 128).
static void buildVertices(int c, int count) {
  static const short weightCounts[6] = {1, 2, 4, 8, 4, 4}, weightBytes[6] = {4, 4, 4, 4, 2, 1};
  Parts p = {0, 0, 0, 0, 0, 4};
  if(c == 1) p.position = 2;
  if(c == 2) p.position = 1;
  if(c == 3 || c == 4) p.color = c == 3 ? 4 : 2;
  if(c == 5 || c == 6 || (c >= 15 && c <= 23) || c == 35) p.normal = c == 6 ? 1 : 4;
  if(c == 7 || c == 8 || c == 33 || c == 34) p.uv = c == 8 ? 2 : 4;
  if(c >= 24 && c <= 29) p.weights = weightCounts[c - 24], p.weight = weightBytes[c - 24];
  int morphs = c >= 30 && c <= 32 ? 2 << (c - 30) : 1, through = c == 37;
  int strip = c <= 8 || (c >= 15 && c <= 36), indexed = c >= 11 && c <= 14;
  int vertices = strip ? count / 256 * 258 : c == 10 ? count + 2 : c == 14 ? 17 * 17 : c == 13 ? 3 : 3 * count;
  unsigned char* at = (unsigned char*)big;
  for(int n = 0; n < vertices; n++) {
    float x, y;  //in pixels
    if(strip) {
      x = n % 258 * 255.0f / 257, y = n / 258 * 4 + (n & 1) * 2;  //(within -1 to 1 for 8- and 16-bit positions)
    } else if(c == 10) {
      float turn = n ? (n - 1) * 6.2831853f / count : 0;  //(around (128, 128), counterclockwise on the screen)
      x = n ? 128 + 64 * __builtin_cosf(turn) : 128, y = n ? 128 - 64 * __builtin_sinf(turn) : 128;
    } else if(c == 14) {
      x = (n % 17) * 8, y = (n / 17) * 8;
    } else {  //a triangle list: round 6's spots, (x, y), (x, y + 2), (x + 2, y): counterclockwise
      int i = n / 3, corner = n % 3;
      x = spotX(i) + (corner == 2 ? 2 : 0), y = spotY(i) + (corner == 1 ? 2 : 0);
      if(through) x += corner == 2 ? -1.625f : 0.0625f, y += corner == 1 ? -1.625f : 0.0625f;
    }
    for(int m = 0; m < morphs; m++) {
      at = through ? putVertex(at, &p, x, y, 0) : putVertex(at, &p, ndcX(x), ndcY(y), 0);
    }
  }
  int bytes = (at - (unsigned char*)big) / (vertices * morphs);  //a vertex's
  unsigned short* indices16 = (unsigned short*)(((unsigned int)at + 15) & ~15u);
  unsigned char* indices8 = (unsigned char*)indices16;
  int indexCount = c == 14 ? 16 * 16 * 6 : c == 13 ? 3 * count : 255;
  for(int n = 0; indexed && n < indexCount; n++) {
    if(c == 14) {  //square (i, j) of the grid: two triangles, counterclockwise
      int square = n / 6, i = square % 16, j = square / 16, corner = (int[6]){0, 2, 1, 1, 2, 3}[n % 6];
      indices16[n] = (j + (corner >> 1)) * 17 + i + (corner & 1);
    } else if(c == 13) {
      indices16[n] = n % 3;
    } else if(c == 11) {
      indices8[n] = n;
    } else {
      indices16[n] = n;
    }
  }
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  int type = partsType(&p) | (through ? GU_TRANSFORM_2D : GU_TRANSFORM_3D) | (morphs > 1 ? GU_VERTICES(morphs) : 0);
  if(!through) {
    setUp3D(&identity, 1);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CW);
  }
  if(c >= 15 && c <= 23) {
    int kind = c <= 17 || c >= 22 ? GU_DIRECTIONAL : c <= 19 ? GU_POINTLIGHT : GU_SPOTLIGHT;
    int lights = c == 15 || c == 18 || c == 20 || c == 22 ? 1 : c == 16 ? 2 : 4;
    lightsOn(kind, lights, c >= 22 ? GU_DIFFUSE_AND_SPECULAR : GU_DIFFUSE);
  }
  if(p.weights) {
    for(int k = 0; k < p.weights; k++) sceGuBoneMatrix(k, &identity);
  }
  for(int k = 0; k < morphs && morphs > 1; k++) sceGuMorphWeight(k, 1.0f / morphs);
  if(c >= 33 && c <= 35) {
    timedTexture(GU_PSM_8888, 0, 16, 16, texture, 0);
    sceGuSetMatrix(GU_TEXTURE, &identity);
    if(c == 35) {
      lightsOn(GU_DIRECTIONAL, 2, GU_DIFFUSE);
      sceGuTexMapMode(GU_ENVIRONMENT_MAP, 0, 1);
    } else {
      sceGuTexMapMode(GU_TEXTURE_MATRIX, 0, 0);
      sceGuTexProjMapMode(c == 33 ? GU_UV : GU_POSITION);
    }
  }
  if(c == 36) sceGuEnable(GU_FOG), sceGuFog(1, 4, 0xff0000);
  int prim = strip ? GU_TRIANGLE_STRIP : c == 10 ? GU_TRIANGLE_FAN : GU_TRIANGLES;
  if(strip) {
    for(int row = 0; row < count / 256; row++) {
      sceGuDrawArray(prim, type, 258, 0, (unsigned char*)big + row * 258 * morphs * bytes);
    }
  } else if(c == 11 || c == 12) {
    for(int first = 0; first < count; first += 85) {
      sceGuDrawArray(prim, type | (c == 11 ? GU_INDEX_8BIT : GU_INDEX_16BIT), 255, indices16,
                     (unsigned char*)big + first * 3 * bytes);
    }
  } else if(c == 14) {
    for(int drawn = 0; drawn < count; drawn += 512) {
      sceGuDrawArray(prim, type | GU_INDEX_16BIT, 16 * 16 * 6, indices16, big);
    }
  } else {
    sceGuDrawArray(prim, type | (c == 13 ? GU_INDEX_16BIT : 0), c == 13 ? 3 * count : vertices, c == 13 ? indices16 : 0,
                   big);
  }
  sceGuFinish();
}

//-- setup (time-setup): primitives of the sizes games draw, set up and drawn as games draw them: in 3D (identity
//matrices), squares tiling the target's 256x256, two triangles each, from a CLUT8 64x64 texture (swizzled, 4 KiB,
//repeating), the depth test passing and writing, unless said; one factor varied at a time (round 6's time-sizes had
//flat sprites and triangles in through mode only).
static const TimedCase setupCases[] = {
  {"squares 8x8", "layers", 2, {1, 2}},
  {"squares 16x16", "layers", 2, {1, 2}},
  {"squares 32x32", "layers", 2, {1, 2}},
  {"squares 64x64", "layers", 2, {1, 2}},
  {"squares 16x16 in perspective (w 1 at the left to 2 at the right)", "layers", 2, {1, 2}},
  {"squares 16x16 as a triangle strip a row", "layers", 2, {1, 2}},
  {"squares 16x16 as fans of four triangles about their middles", "layers", 2, {1, 2}},
  {"squares 16x16, half the triangles cut by the near plane (a corner past it)", "layers", 2, {1, 2}},
  {"squares 16x16, every triangle cut by the near plane", "layers", 2, {1, 2}},
  {"squares 16x16, every other square culled as back faces", "layers", 2, {1, 2}},
  {"squares 16x16, every square culled", "layers", 2, {1, 2}},
  {"rectangles 4 wide, 64 tall", "layers", 2, {1, 2}},
  {"rectangles 64 wide, 4 tall", "layers", 2, {1, 2}},
  {"squares 16x16, bilinear", "layers", 2, {1, 2}},
  {"squares 16x16, no texture", "layers", 2, {1, 2}},
  {"squares 16x16, no texture, no depth test", "layers", 2, {1, 2}},
  {"squares 16x16, no depth test", "layers", 2, {1, 2}},
  {"squares 16x16, an 8888 64x64 texture not swizzled (16 KiB)", "layers", 2, {1, 2}},
};
//A corner at the target's pixel (px, py), its texel (px, py) / 64 of the texture (repeating every 64 pixels); under
//the lens w is its distance, 1 at the left to 2 at the right (or 0.25, past the near plane, if it's cut), z -w.
static FloatVertex setupCorner(float px, float py, int lensed, int cut) {
  float w = !lensed ? 1 : cut ? 0.25f : 1 + px / 256;
  return (FloatVertex){px / 64, py / 64, 0xffffffff, ndcX(px) * w, ndcY(py) * w, lensed ? -w : 0};
}
static void buildSetup(int c, int layers) {
  int width = c <= 3 ? 8 << c : c == 11 ? 4 : c == 12 ? 64 : 16, height = c == 11 ? 64 : c == 12 ? 4 : width;
  int across = 256 / width, down = 256 / height, fans = c == 6, strips = c == 5, count = 0;
  int lensed = c == 4 || c == 7 || c == 8;
  FloatVertex* v = (FloatVertex*)big;
  //square (i, j)'s corner (dx, dy): 0 or 1 each way, or 0.5 its middle
  #define CORNER(i, j, dx, dy, cut) (v[count++] = setupCorner(((i) + (dx)) * width, ((j) + (dy)) * height, lensed, cut))
  for(int j = 0; j < down; j++) {
    for(int i = 0; i < across; i++) {
      int culled = c == 10 || (c == 9 && (i + j) % 2);
      if(fans) {
        CORNER(i, j, 0.5f, 0.5f, 0);
        CORNER(i, j, 0, 0, 0), CORNER(i, j, 0, 1, 0), CORNER(i, j, 1, 1, 0), CORNER(i, j, 1, 0, 0);
        CORNER(i, j, 0, 0, 0);
      } else if(strips) {
        if(i == 0) CORNER(0, j, 0, 0, 0), CORNER(0, j, 0, 1, 0);
        CORNER(i, j, 1, 0, 0), CORNER(i, j, 1, 1, 0);
      } else if(culled) {  //clockwise on the screen
        CORNER(i, j, 0, 0, 0), CORNER(i, j, 1, 0, 0), CORNER(i, j, 0, 1, 0);
        CORNER(i, j, 1, 0, 0), CORNER(i, j, 1, 1, 0), CORNER(i, j, 0, 1, 0);
      } else {  //counterclockwise
        int cutFirst = c == 8 || (c == 7 && (i + j) % 2), cutSecond = c == 8;
        CORNER(i, j, 0, 0, 0), CORNER(i, j, 0, 1, 0), CORNER(i, j, 1, 0, cutFirst);
        CORNER(i, j, 1, 0, 0), CORNER(i, j, 0, 1, 0), CORNER(i, j, 1, 1, cutSecond);
      }
    }
  }
  #undef CORNER
  fillDepth(0);
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  setUp3D(lensed ? &lens : &identity, 1);
  sceGuDepthFunc(GU_GEQUAL);
  sceGuDepthMask(GU_FALSE);
  if(c == 15 || c == 16) sceGuDisable(GU_DEPTH_TEST);
  if(c == 9 || c == 10) sceGuEnable(GU_CULL_FACE), sceGuFrontFace(GU_CCW);  //(clockwise ones are back faces)
  if(c == 17) timedTexture(GU_PSM_8888, 0, 64, 64, texture, 0);
  else if(c != 14 && c != 15) timedTexture(GU_PSM_T8, 1, 64, 64, bytes8, c == 13);
  for(int layer = 0; layer < layers; layer++) {
    if(fans) {
      for(int k = 0; k < count; k += 6) sceGuDrawArray(GU_TRIANGLE_FAN, FloatVertexType3D, 6, 0, v + k);
    } else if(strips) {
      int row = 2 * (across + 1);
      for(int k = 0; k < count; k += row) sceGuDrawArray(GU_TRIANGLE_STRIP, FloatVertexType3D, row, 0, v + k);
    } else {
      for(int first = 0; first < count; first += 49152) {  //(a PRIM draws 65535 vertices at most)
        sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, count - first < 49152 ? count - first : 49152, 0, v + first);
      }
    }
  }
  sceGuFinish();
}

//-- pixels (time-pixels): what a pixel's tests, blending and texture function cost (part 76 has the depth test's,
//the same passing or failing, and nothing for the rest), one varied at a time from round 6's textured fill: a
//256x256 sprite in through mode a layer, an 8888 texture (RAM, swizzled, 256x256, 1:1, replacing the color) into
//8888. Chains of Olympus's difficulty menu loses 58% of its pixels to the depth test (part 64): whether a failing
//pixel still fetches its texel is the question cases 2 to 4 ask.
static const TimedCase pixelCases[] = {
  {"the textured fill alone", "layers", 3, {1, 2, 4}},
  {"depth test GEQUAL passing, writing", "layers", 3, {1, 2, 4}},
  {"depth test failing", "layers", 3, {1, 2, 4}},
  {"depth test failing, the texture not swizzled (round 6's slowest fill)", "layers", 3, {1, 2, 4}},
  {"depth test passing, the texture not swizzled", "layers", 3, {1, 2, 4}},
  {"depth test passing, not writing", "layers", 3, {1, 2, 4}},
  {"alpha test passing (ALWAYS)", "layers", 3, {1, 2, 4}},
  {"alpha test failing (NEVER)", "layers", 3, {1, 2, 4}},
  {"stencil test passing (ALWAYS, REPLACE)", "layers", 3, {1, 2, 4}},
  {"stencil test failing (NEVER)", "layers", 3, {1, 2, 4}},
  {"blending: source alpha, 1 - source alpha", "layers", 3, {1, 2, 4}},
  {"blending: ADD, fixed factors", "layers", 3, {1, 2, 4}},
  {"blending: SUBTRACT", "layers", 3, {1, 2, 4}},
  {"blending: REVERSE_SUBTRACT", "layers", 3, {1, 2, 4}},
  {"blending: MIN", "layers", 3, {1, 2, 4}},
  {"blending: MAX", "layers", 3, {1, 2, 4}},
  {"blending: ABS", "layers", 3, {1, 2, 4}},
  {"blending: the destination's alpha, 1 - the destination's alpha", "layers", 3, {1, 2, 4}},
  {"blending (source alpha) into 5650", "layers", 3, {1, 2, 4}},
  {"texture function DECAL", "layers", 3, {1, 2, 4}},
  {"texture function BLEND (the environment color)", "layers", 3, {1, 2, 4}},
  {"texture function ADD", "layers", 3, {1, 2, 4}},
  {"color doubling, MODULATE", "layers", 3, {1, 2, 4}},
  {"color test passing (ALWAYS)", "layers", 3, {1, 2, 4}},
  {"a game's pixel: CLUT8 64x64 swizzled repeating, depth test passing and writing, blending", "layers", 3,
   {1, 2, 4}},
  {"the same, depth test failing (overdraw)", "layers", 3, {1, 2, 4}},
  {"the same as the first, into 5650", "layers", 3, {1, 2, 4}},
  {"no texture, depth test passing, writing", "layers", 3, {1, 2, 4}},
  {"no texture, depth test failing", "layers", 3, {1, 2, 4}},
};
static void buildPixels(int c, int layers) {
  int depth = (c >= 1 && c <= 5) || (c >= 24 && c <= 28), failing = c == 2 || c == 3 || c == 25 || c == 28;
  int game = c >= 24 && c <= 26, target = c == 18 || c == 26 ? GU_PSM_5650 : GU_PSM_8888;
  if(depth) fillDepth(failing ? 0xffff : 0);
  timedList(target);
  if(game) timedTexture(GU_PSM_T8, 1, 64, 64, bytes8, 0);
  else if(c < 27) timedTexture(GU_PSM_8888, c != 3 && c != 4, 256, 256, texture, 0);
  if(depth) {
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthMask(c == 5 ? GU_TRUE : GU_FALSE);
  }
  if(c == 6 || c == 7) sceGuEnable(GU_ALPHA_TEST), sceGuAlphaFunc(c == 6 ? GU_ALWAYS : GU_NEVER, 0, 0xff);
  if(c == 8 || c == 9) {
    sceGuEnable(GU_STENCIL_TEST);
    sceGuStencilFunc(c == 8 ? GU_ALWAYS : GU_NEVER, 0x80, 0xff);
    sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
  }
  static const short ops[8] = {GU_ADD, GU_ADD, GU_SUBTRACT, GU_REVERSE_SUBTRACT, GU_MIN, GU_MAX, GU_ABS, GU_ADD};
  if((c >= 10 && c <= 18) || game) {
    int op = c >= 10 && c <= 17 ? ops[c - 10] : GU_ADD, fixed = c >= 11 && c <= 16;
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(op, c == 17 ? GU_DST_ALPHA : fixed ? GU_FIX : GU_SRC_ALPHA,
                   c == 17 ? GU_ONE_MINUS_DST_ALPHA : fixed ? GU_FIX : GU_ONE_MINUS_SRC_ALPHA, 0x808080, 0x404040);
  }
  if(c >= 19 && c <= 22) {
    static const short functions[4] = {GU_TFX_DECAL, GU_TFX_BLEND, GU_TFX_ADD, GU_TFX_MODULATE};
    sceGuTexFunc(functions[c - 19], GU_TCC_RGBA);
    sceGuTexEnvColor(0x40c080);
    if(c == 22) sceGuEnable(GU_FRAGMENT_2X);
  }
  if(c == 23) sceGuEnable(GU_COLOR_TEST), sceGuColorFunc(GU_ALWAYS, 0, 0);
  for(int layer = 0; layer < layers; layer++) timedSprite(0, 0, 256, 256, 0, 0, 256, 256, 0x80ffffff);
  sceGuFinish();
}

//-- big textures in strips (time-strips): the target's 512x256 pixels drawn once a layer from a 512-texel-wide
//texture, texel for pixel, in sprites `width` texels wide and 256 tall, left to right, as games draw a big picture.
//Killzone reads a 512x512 VRAM buffer as a CLUT32 texture in strips 32 wide (some 9 million texels a second, most of
//its GE time in part 76's model, which charges VRAM's texels 1.93 clocks a byte whatever the strips) and Activision
//Hits Remixed's menus do the same; its copyright screen is a 512x256 CLUT8 texture (RAM, swizzled) drawn whole. Part
//76 measured big textures only drawn 256 texels across, and took Killzone's language menu, a 512x512 8888 texture in
//RAM not swizzled drawn in one sprite 480 wide, to run at 15 frames a second. A VRAM texture is VRAM's first
//megabyte, the text on the screen (its texels don't matter, only how they lie); a RAM one is in big. CLUT textures
//take the 256-color palette.
typedef struct { int psm, swizzled, vram, height; } BigTexture;
static const BigTexture bigTextures[10] = {
  {GU_PSM_8888, 0, 1, 512}, {GU_PSM_T32, 0, 1, 512}, {GU_PSM_5650, 0, 1, 512}, {GU_PSM_T8, 0, 1, 512},
  {GU_PSM_8888, 0, 0, 512}, {GU_PSM_8888, 1, 0, 512}, {GU_PSM_T8, 1, 0, 256}, {GU_PSM_T8, 0, 0, 256},
  {GU_PSM_5650, 1, 0, 512}, {GU_PSM_5650, 0, 0, 512},
};
#define STRIP_WIDTHS(texture)                                                                                       \
  {texture ", strips 8 wide", "layers", 2, {1, 2}}, {texture ", strips 16 wide", "layers", 2, {1, 2}},              \
  {texture ", strips 32 wide", "layers", 2, {1, 2}}, {texture ", strips 64 wide", "layers", 2, {1, 2}},             \
  {texture ", strips 128 wide", "layers", 2, {1, 2}}, {texture ", strips 256 wide", "layers", 2, {1, 2}},           \
  {texture ", one sprite 512 wide", "layers", 2, {1, 2}}
static const TimedCase stripCases[] = {
  STRIP_WIDTHS("VRAM, 8888 512x512"), STRIP_WIDTHS("VRAM, CLUT32 512x512"), STRIP_WIDTHS("VRAM, 5650 512x512"),
  STRIP_WIDTHS("VRAM, CLUT8 512x512"), STRIP_WIDTHS("RAM, 8888 512x512 not swizzled"),
  STRIP_WIDTHS("RAM, 8888 512x512 swizzled"), STRIP_WIDTHS("RAM, CLUT8 512x256 swizzled"),
  STRIP_WIDTHS("RAM, CLUT8 512x256 not swizzled"), STRIP_WIDTHS("RAM, 5650 512x512 swizzled"),
  STRIP_WIDTHS("RAM, 5650 512x512 not swizzled"),
};
#define VRAM_TEXTURE ((void*)0x04000000)
//A texture of round 8's, as timedTexture's but height x width texels (buffer width the width), any CLUT format.
static void texture8(int psm, int swizzled, int width, int height, const void* texels, int linear) {
  sceGuEnable(GU_TEXTURE_2D);
  sceGuTexMode(psm, 0, 0, swizzled);
  sceGuTexImage(0, width, height, width, texels);
  sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
  sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
  sceGuTexWrap(GU_REPEAT, GU_REPEAT);
  if(psm >= GU_PSM_T4 && psm <= GU_PSM_T32) {
    sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
    sceGuClutLoad(psm == GU_PSM_T4 ? 2 : 32, palette);
  }
}
//Sprites `width` pixels wide side by side over (x, y) to (x + across, y + down), their texels (u, v) on, `scale`
//texels a pixel, in one block of vertices (so GU puts one JUMP over them, not one a sprite).
static void spriteStrips(float x, float y, float across, float down, int width, float u, float v, float scale) {
  int count = (int)(across + width - 1) / width;
  FloatVertex* vertices = sceGuGetMemory(count * 2 * sizeof(FloatVertex));
  for(int k = 0; k < count; k++) {
    float left = k * width, right = left + width < across ? left + width : across;
    vertices[k * 2] = (FloatVertex){u + left * scale, v, 0xffffffff, x + left, y, 0};
    vertices[k * 2 + 1] = (FloatVertex){u + right * scale, v + down * scale, 0xffffffff, x + right, y + down, 0};
  }
  sceGuDrawArray(GU_SPRITES, FloatVertexType, count * 2, 0, vertices);
}
static void buildStrips(int c, int layers) {
  const BigTexture* t = &bigTextures[c / 7];
  timedList(GU_PSM_8888);
  texture8(t->psm, t->swizzled, 512, t->height, t->vram ? VRAM_TEXTURE : (void*)big, 0);
  for(int layer = 0; layer < layers; layer++) spriteStrips(0, 0, 512, 256, 8 << c % 7, 0, 0, 1);
  sceGuFinish();
}

//-- textures (time-textures): what else part 76 left: textures shrunk and stretched (Chains of Olympus and Ghost of
//Sparta sample their 512x512 frame buffers at two texels a pixel), mipmaps, perspective, a texture walked by columns,
//and a texture of its own for each primitive.
static const TimedCase textureCases[] = {
  {"VRAM 8888 512x512 shrunk 2:1 (512x256 texels onto 256x128 pixels), nearest, one sprite", "layers", 2, {1, 2}},
  {"the same, bilinear", "layers", 2, {1, 2}},
  {"the same, nearest, in strips 32 pixels (64 texels) wide", "layers", 2, {1, 2}},
  {"VRAM 8888 shrunk 4:1 (512x256 texels onto 128x64 pixels), nearest", "layers", 2, {1, 2}},
  {"VRAM CLUT32 shrunk 2:1, nearest", "layers", 2, {1, 2}},
  {"RAM 8888 512x512 swizzled, shrunk 2:1, nearest", "layers", 2, {1, 2}},
  {"RAM 8888 512x512 not swizzled, shrunk 2:1, nearest", "layers", 2, {1, 2}},
  {"VRAM 8888, 256x128 texels stretched 1:2 onto 512x256 pixels, nearest", "layers", 2, {1, 2}},
  {"the same, bilinear", "layers", 2, {1, 2}},
  {"3D: RAM 8888 256x256 swizzled shrunk 2:1 onto 128x128 pixels, no mipmaps", "layers", 2, {1, 2}},
  {"the same with 3 mipmap levels (256, 128, 64), the level chosen (auto), nearest", "layers", 2, {1, 2}},
  {"the same, level 1 set (const)", "layers", 2, {1, 2}},
  {"the same, the level chosen, linear between levels", "layers", 2, {1, 2}},
  {"3D: RAM 8888 256x256 swizzled over 256x256 pixels, w 1 at the left to 2 at the right", "layers", 2, {1, 2}},
  {"the same, w 1 to 4", "layers", 2, {1, 2}},
  {"the same as w 1 to 2, VRAM 8888 (512 wide)", "layers", 2, {1, 2}},
  {"RAM 8888 256x256 swizzled turned a quarter (u down the screen): two triangles in through mode", "layers", 2,
   {1, 2}},
  {"the same, not swizzled", "layers", 2, {1, 2}},
  {"the same, VRAM 8888 (512 wide)", "layers", 2, {1, 2}},
  {"sprites 32x32, each its own CLUT8 32x32 texture (RAM, swizzled, 1 KiB), one palette", "sprites", 2, {64, 256}},
  {"the same, CLUT8 64x64 textures (4 KiB), 32x32 texels of each", "sprites", 2, {64, 256}},
  {"the same, CLUT8 128x128 textures (16 KiB), 32x32 texels of each", "sprites", 2, {64, 256}},
  {"the same, 5650 64x64 textures (swizzled, 8 KiB), 32x32 texels of each", "sprites", 2, {64, 256}},
  {"the same as the first, each with its palette loaded (256 colors)", "sprites", 2, {64, 256}},
  {"the same, CLUT4 32x32 textures (512 bytes), one palette", "sprites", 2, {64, 256}},
};
static void buildTextures(int c, int size) {
  FloatVertex* v = (FloatVertex*)(big + 393216);  //(the last 512 KiB of big: the RAM textures use the first 1.5 MiB)
  if(c >= 9 && c <= 15) {  //3D: two triangles, the mipmaps' over the top-left 128x128 pixels
    for(int n = 0; n < 6; n++) {
      int corner = (int[6]){0, 1, 2, 1, 3, 2}[n], right = corner & 1, bottom = corner >> 1;
      float x = c <= 12 ? right * 128 : right * 256, y = c <= 12 ? bottom * 128 : bottom * 256;
      float w = c <= 12 || !right ? 1 : c == 14 ? 4 : 2, scale = c == 15 ? 0.5f : 1;
      v[n] = (FloatVertex){right * scale, bottom * scale, 0xffffffff, ndcX(x) * w, ndcY(y) * w, c <= 12 ? 0 : -w};
    }
  }
  if(c >= 16 && c <= 18) {  //turned: x along v, y along u
    for(int n = 0; n < 6; n++) {
      int corner = (int[6]){0, 1, 2, 1, 3, 2}[n], right = corner & 1, bottom = corner >> 1;
      v[n] = (FloatVertex){bottom * 256, right * 256, 0xffffffff, right * 256, bottom * 256, 0};
    }
  }
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  if(c <= 8) {
    int psm = c == 4 ? GU_PSM_T32 : GU_PSM_8888, vram = c <= 4 || c >= 7;
    texture8(psm, c == 5, 512, 512, vram ? VRAM_TEXTURE : (void*)big, c == 1 || c == 8);
    //(shrunk: 512x256 texels; stretched: 256x128)
    float scale = c == 3 ? 4 : c >= 7 ? 0.5f : 2, across = (c >= 7 ? 256 : 512) / scale, down = across / 2;
    for(int layer = 0; layer < size; layer++) spriteStrips(0, 0, across, down, c == 2 ? 32 : 512, 0, 0, scale);
  } else if(c <= 12) {
    setUp3D(&identity, 1);
    sceGuDisable(GU_DEPTH_TEST);
    texture8(GU_PSM_8888, 1, 256, 256, big, 0);
    if(c >= 10) {
      sceGuTexMode(GU_PSM_8888, 2, 0, 1);
      sceGuTexImage(1, 128, 128, 128, big + 65536);
      sceGuTexImage(2, 64, 64, 64, big + 65536 + 16384);
      sceGuTexLevelMode(c == 11 ? GU_TEXTURE_CONST : GU_TEXTURE_AUTO, c == 11 ? 1 : 0);
      sceGuTexFilter(c == 12 ? GU_LINEAR_MIPMAP_LINEAR : GU_NEAREST_MIPMAP_NEAREST, GU_NEAREST);
    }
    for(int layer = 0; layer < size; layer++) sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, v);
  } else if(c <= 15) {
    setUp3D(&lens, 1);
    sceGuDisable(GU_DEPTH_TEST);
    if(c == 15) texture8(GU_PSM_8888, 0, 512, 512, VRAM_TEXTURE, 0);
    else texture8(GU_PSM_8888, 1, 256, 256, big, 0);
    for(int layer = 0; layer < size; layer++) sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, v);
  } else if(c <= 18) {
    if(c == 18) texture8(GU_PSM_8888, 0, 512, 512, VRAM_TEXTURE, 0);
    else texture8(GU_PSM_8888, c == 16, 256, 256, big, 0);
    for(int layer = 0; layer < size; layer++) sceGuDrawArray(GU_TRIANGLES, FloatVertexType, 6, 0, v);
  } else {  //a texture of its own for each sprite, as many as big holds, in turn
    static const short sides[6] = {32, 64, 128, 64, 32, 32}, formats[6] = {GU_PSM_T8, GU_PSM_T8, GU_PSM_T8,
                                                                            GU_PSM_5650, GU_PSM_T8, GU_PSM_T4};
    int f = c - 19, side = sides[f], psm = formats[f];
    int bytes = psm == GU_PSM_5650 ? side * side * 2 : psm == GU_PSM_T4 ? side * side / 2 : side * side;
    int held = sizeof(big) / bytes;
    texture8(psm, 1, side, side, big, 0);
    for(int k = 0; k < size; k++) {
      sceGuTexImage(0, side, side, side, (unsigned char*)big + (k % held) * bytes);
      if(c == 23) sceGuClutLoad(32, palette);
      float x = (k & 15) * 32, y = (k >> 4 & 7) * 32;
      timedSprite(x, y, x + 32, y + 32, 0, 0, 32, 32, 0xffffffff);
    }
  }
  sceGuFinish();
}

//-- block transfers (time-transfers2): narrow ones and single rows (round 6's came out 28-47% short), and whole frame
//buffers copied in VRAM, as Chains of Olympus and Ghost of Sparta copy theirs. From texture or texture16 (RAM, rows
//of 256), big (RAM), or VRAM's first megabyte (rows of 512), to the target (rows of 512; a 480x272 copy runs over
//the depth buffer, which nothing here needs then) or big.
static const TimedCase transferCases2[] = {
  {"RAM to VRAM, 32-bit, 1 pixel wide", "rows", 2, {64, 256}},
  {"RAM to VRAM, 32-bit, 2 pixels wide", "rows", 2, {64, 256}},
  {"RAM to VRAM, 32-bit, 8 pixels wide", "rows", 2, {64, 256}},
  {"RAM to VRAM, 32-bit, 16 pixels wide", "rows", 2, {64, 256}},
  {"RAM to VRAM, 32-bit, 32 pixels wide", "rows", 2, {64, 256}},
  {"RAM to VRAM, 16-bit, 2 pixels wide", "rows", 2, {64, 256}},
  {"RAM to VRAM, 16-bit, 8 pixels wide", "rows", 2, {64, 256}},
  {"VRAM to VRAM, 32-bit, 8 pixels wide", "rows", 2, {64, 256}},
  {"RAM to VRAM, 32-bit, one row", "pixels", 4, {8, 32, 128, 512}},
  {"VRAM to VRAM, 32-bit, one row", "pixels", 4, {8, 32, 128, 512}},
  {"VRAM to VRAM, 32-bit, 480x272 (a frame buffer copied)", "copies", 3, {1, 2, 4}},
  {"VRAM to VRAM, 16-bit, 480x272", "copies", 3, {1, 2, 4}},
  {"VRAM to RAM, 32-bit, 480x272", "copies", 2, {1, 2}},
  {"RAM to VRAM, 32-bit, 256x256 from rows 1024 pixels apart", "copies", 2, {1, 2}},
};
static void buildTransfers2(int c, int size) {
  static const short widths[8] = {1, 2, 8, 16, 32, 2, 8, 8};
  void* screen = VRAM_TEXTURE, *target = (void*)(0x04000000 + Target);
  sceKernelDcacheWritebackInvalidateAll();  //(no cached line of big to land over what the GE writes there)
  sceGuStart(GU_SEND, timed);
  if(c <= 7) {
    int psm = c == 5 || c == 6 ? GU_PSM_5650 : GU_PSM_8888;
    void* from = c == 7 ? screen : psm == GU_PSM_5650 ? (void*)texture16 : (void*)texture;
    sceGuCopyImage(psm, 0, 0, widths[c], size, c == 7 ? 512 : 256, from, 0, 0, Stride, target);
  } else if(c <= 9) {
    sceGuCopyImage(GU_PSM_8888, 0, 0, size, 1, 512, c == 8 ? (void*)big : screen, 0, 0, Stride, target);
  } else {
    for(int k = 0; k < size; k++) {
      if(c == 12) sceGuCopyImage(GU_PSM_8888, 0, 0, 480, 272, 512, screen, 0, 0, 512, big);
      else if(c == 13) sceGuCopyImage(GU_PSM_8888, 0, 0, 256, 256, 1024, big, 0, 0, Stride, target);
      else sceGuCopyImage(c == 11 ? GU_PSM_5650 : GU_PSM_8888, 0, 0, 480, 272, 512, screen, 0, 0, Stride, target);
    }
  }
  sceGuFinish();
}

//-- clears (time-clears): clear mode over the target's 512x256 (round 6 had it only over 256x256, 8888, all three
//buffers): in one sprite, and in sprites 64 wide (as GU's fast clear draws), 8888 and 5650.
static const TimedCase clearCases[] = {
  {"clear mode, color only, 8888, one sprite", "clears", 3, {1, 2, 4}},
  {"clear mode, color, stencil and depth, 8888, one sprite", "clears", 3, {1, 2, 4}},
  {"clear mode, depth only, one sprite", "clears", 3, {1, 2, 4}},
  {"clear mode, color and depth, 8888, in sprites 64 wide", "clears", 3, {1, 2, 4}},
  {"clear mode, color, stencil and depth, 8888, in sprites 64 wide", "clears", 3, {1, 2, 4}},
  {"clear mode, color only, 5650, one sprite", "clears", 3, {1, 2, 4}},
  {"clear mode, color and depth, 5650, one sprite", "clears", 3, {1, 2, 4}},
  {"clear mode, color and depth, 5650, in sprites 64 wide", "clears", 3, {1, 2, 4}},
};
static void buildClears(int c, int clears) {
  enum { C = GU_COLOR_BUFFER_BIT, S = GU_STENCIL_BUFFER_BIT, D = GU_DEPTH_BUFFER_BIT };
  static const short flags[8] = {C, C | S | D, D, C | D, C | S | D, C, C | D, C | D};
  timedList(c >= 5 ? GU_PSM_5650 : GU_PSM_8888);
  for(int k = 0; k < clears; k++) {
    sceGuSendCommandi(0xd3, flags[c] << 8 | 1);  //CLEAR: on, with what it writes
    spriteStrips(0, 0, 512, 256, c == 3 || c == 4 || c == 7 ? 64 : 512, 0, 0, 0);
    sceGuSendCommandi(0xd3, 0);
  }
  sceGuFinish();
}

//-- overlap (time-overlap): whether the GE's two ends really go on at once, as part 76's model takes them (its time
//the longer end's), and how far the front end gets ahead. Front-end pieces (32 triangles skinned by 8 float weights,
//culled as back faces: vertices alone) and back-end pieces (a 32x32 square of two textured triangles in 3D, from an
//8888 64x64 texture not swizzled, 16 KiB: pixels and texels alone), about as long as each other, 256 of each.
static const TimedCase overlapCases[] = {
  {"the two kinds of piece in turn, N of each at a time", "pieces at a time", 3, {1, 16, 256}},
  {"the front-end pieces alone", "pieces", 1, {256}},
  {"the back-end pieces alone", "pieces", 1, {256}},
};
static void buildOverlap(int c, int run) {
  Skinned8* front = (Skinned8*)big;
  FloatVertex* back = (FloatVertex*)(big + 65536);
  for(int n = 0; n < 96; n++) {  //round 6's spots, counterclockwise
    int i = n / 3, corner = n % 3;
    float x = spotX(i) + (corner == 2 ? 2 : 0), y = spotY(i) + (corner == 1 ? 2 : 0);
    front[n] = (Skinned8){{0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f, 0.125f}, 0xffffffff, ndcX(x),
                          ndcY(y), 0};
  }
  for(int k = 0; k < 64; k++) {  //clockwise: drawn
    float x = (k & 7) * 32, y = (k >> 3) * 32;
    for(int n = 0; n < 6; n++) {
      int corner = (int[6]){0, 1, 2, 1, 3, 2}[n], right = corner & 1, bottom = corner >> 1;
      back[k * 6 + n] = (FloatVertex){right * 0.5f, bottom * 0.5f, 0xffffffff, ndcX(x + right * 32),
                                      ndcY(y + bottom * 32), 0};
    }
  }
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  setUp3D(&identity, 1);
  sceGuDisable(GU_DEPTH_TEST);
  sceGuEnable(GU_CULL_FACE);
  sceGuFrontFace(GU_CW);
  for(int k = 0; k < 8; k++) sceGuBoneMatrix(k, &identity);
  timedTexture(GU_PSM_8888, 0, 64, 64, texture, 0);
  for(int done = 0; done < 256; done += run) {
    for(int k = 0; k < run && c != 2; k++) {
      sceGuDrawArray(GU_TRIANGLES, GU_WEIGHTS(8) | GU_WEIGHT_32BITF | ColorVertexType3D, 96, 0, front);
    }
    for(int k = 0; k < run && c != 1; k++) {
      sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, back + (done + k) % 64 * 6);
    }
  }
  sceGuFinish();
}

//-- lists like the captured scenes' (time-scenes): built to what Phobos's profiles of them show (part 107), so every
//term in them is known: the bridge between the cases above and the games.
static const TimedCase sceneCases[] = {
  {"Killzone's scene list: 8 passes over 480x256 of a 512x512 CLUT32 texture in VRAM, in strips 32 wide, into 8888",
   "lists", 2, {1, 2}},
  {"Killzone's full-screen list: a 512x512 8888 texture in VRAM over 480x256 in strips 120 wide, into 5650",
   "lists", 3, {1, 2, 4}},
  {"Activision Hits Remixed's copyright screen: a clear, then a 512x256 CLUT8 texture (RAM, swizzled) over 480x256 "
   "in one sprite, into 5650", "lists", 2, {1, 2}},
  {"Activision Hits Remixed's menu: 2 passes of Killzone's CLUT32 strips, then 300 triangles lit by 4 point lights",
   "lists", 2, {1, 2}},
  {"Chains of Olympus's: 4,096 triangles in 16x16 squares (CLUT8, depth tested), a 512x512 8888 VRAM texture shrunk "
   "2:1 onto 256x128, a 480x272 copy in VRAM", "lists", 2, {1, 2}},
  {"Super Monkey Ball Adventure's: 200 CALLed lists, each setting a CLUT8 128x128 texture (RAM, swizzled), loading "
   "its palette and drawing 10 textured triangles about 5x5 pixels, depth tested", "lists", 2, {1, 2}},
  {"Killzone's language menu's background: a 512x512 8888 texture in RAM, not swizzled, in one sprite over 480x256, "
   "into 8888", "lists", 2, {1, 2}},
  {"Super Stardust Portable's intro's glow: a 512x512 8888 VRAM texture shrunk 2:1 onto 256x128 and 4:1 onto 128x64, "
   "then its corner 256 texels wide stretched over 480x256 (1.875 times), bilinear, added", "lists", 2, {1, 2}},
};
enum { SceneLists = 200, SceneListWords = 48 };  //Super Monkey Ball's CALLed lists, in timed from Called
static void buildScenes(int c, int size) {
  FloatVertex* v = (FloatVertex*)(big + 393216);  //(past the textures in big's first 1.5 MiB)
  LitVertex* lit = (LitVertex*)v;
  int target = c == 1 || c == 2 ? GU_PSM_5650 : GU_PSM_8888, triangles = c == 4 ? 4096 : c == 5 ? 2000 : 300;
  for(int t = 0; t < triangles && c >= 3 && c <= 5; t++) {
    float x, y, side;
    if(c == 4) x = (t / 2 & 15) * 16, y = (t / 32 & 15) * 16, side = 16;
    else x = (t % 50) * 5, y = (t / 50 % 50) * 5, side = c == 3 ? 8 : 5;
    for(int corner = 0; corner < 3; corner++) {
      int right = c == 4 && t % 2 ? corner != 1 : corner == 2, bottom = c == 4 && t % 2 ? corner != 0 : corner == 1;
      float px = x + right * side, py = y + bottom * side;
      if(c == 3) lit[t * 3 + corner] = (LitVertex){0xffffffff, 0, 0, 1, ndcX(px), ndcY(py), 0};
      else v[t * 3 + corner] = (FloatVertex){right * 0.25f, bottom * 0.25f, 0xffffffff, ndcX(px), ndcY(py), 0};
    }
  }
  if(c == 4 || c == 5) fillDepth(0);
  sceKernelDcacheWritebackAll();
  if(c == 5) {
    for(int k = 0; k < SceneLists; k++) {
      sceGuStart(GU_CALL, timed + Called + k * SceneListWords);
      sceGuTexImage(0, 128, 128, 128, big + (k % 96) * 4096);
      sceGuClutLoad(32, palette);
      sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 30, 0, v + k * 30);
      sceGuFinish();
    }
  }
  timedList(target);
  for(int list = 0; list < size; list++) {
    if(c == 0 || c == 3) {
      texture8(GU_PSM_T32, 0, 512, 512, VRAM_TEXTURE, 0);
      for(int pass = 0; pass < (c == 0 ? 8 : 2); pass++) spriteStrips(0, 0, 480, 256, 32, 0, 0, 1);
    }
    if(c == 1) {
      texture8(GU_PSM_8888, 0, 512, 512, VRAM_TEXTURE, 0);
      spriteStrips(0, 0, 480, 256, 120, 0, 0, 1);
    }
    if(c == 2) {
      sceGuDisable(GU_TEXTURE_2D);
      sceGuSendCommandi(0xd3, GU_COLOR_BUFFER_BIT << 8 | 1);
      spriteStrips(0, 0, 480, 256, 480, 0, 0, 0);
      sceGuSendCommandi(0xd3, 0);
      texture8(GU_PSM_T8, 1, 512, 256, big, 0);
      spriteStrips(0, 0, 480, 256, 480, 0, 0, 1);
    }
    if(c >= 3 && c <= 5) {
      setUp3D(&identity, 1);
      if(c == 3) {
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDisable(GU_DEPTH_TEST);
        lightsOn(GU_POINTLIGHT, 4, GU_DIFFUSE);
        sceGuDrawArray(GU_TRIANGLES, LitVertexType, 900, 0, lit);
        sceGuDisable(GU_LIGHTING);
      } else {
        sceGuDepthFunc(GU_GEQUAL);
        sceGuDepthMask(GU_FALSE);
        texture8(GU_PSM_T8, 1, c == 4 ? 64 : 128, c == 4 ? 64 : 128, c == 4 ? (void*)bytes8 : (void*)big, 0);
        if(c == 4) sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 3 * 4096, 0, v);
        else for(int k = 0; k < SceneLists; k++) sceGuCallList(timed + Called + k * SceneListWords);
        sceGuDisable(GU_DEPTH_TEST);
      }
      setUp(target);
    }
    if(c == 4) {
      texture8(GU_PSM_8888, 0, 512, 512, VRAM_TEXTURE, 0);
      spriteStrips(0, 0, 256, 128, 512, 0, 0, 2);
      sceGuDisable(GU_TEXTURE_2D);
      sceGuCopyImage(GU_PSM_8888, 0, 0, 480, 272, 512, VRAM_TEXTURE, 0, 0, Stride, (void*)(0x04000000 + Target));
    }
    if(c == 6) {
      texture8(GU_PSM_8888, 0, 512, 512, big, 0);
      spriteStrips(0, 0, 480, 256, 480, 0, 0, 1);
    }
    if(c == 7) {
      texture8(GU_PSM_8888, 0, 512, 512, VRAM_TEXTURE, 0);
      spriteStrips(0, 0, 256, 128, 512, 0, 0, 2);
      spriteStrips(256, 0, 128, 64, 512, 0, 0, 4);
      sceGuTexFilter(GU_LINEAR, GU_LINEAR);
      sceGuEnable(GU_BLEND);
      sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffff, 0xffffff);
      spriteStrips(0, 0, 480, 256, 480, 0, 0, 256.0f / 480);
      sceGuDisable(GU_BLEND);
    }
  }
  sceGuFinish();
}

//-- the CPU beside the GE (cpu-contention): whether the GE drawing from RAM slows the CPU's reads of RAM, and the
//CPU's reads the GE's drawing; they share the bus, and in the captured scenes the GE is busy 94-100% of the time
//while the PSP's CPU takes 7 to 9 times Phobos's time between a loop's waits (part 103). The GE's work: 32 layers of
//a 256x256 8888 texture not swizzled, from RAM (texture) or VRAM (its copy at Spare). Each run's two times: cases
//0-4, the CPU's loop in microseconds and sceGeDrawSync(1) as it ended (2: the GE still drawing); cases 5-7, as
//runTimed's (the finish callback, sceGeListEnQueue's return), the reading thread at a lower priority than this one,
//so it runs while this one waits.
static const TimedCase contentionCases[] = {
  {"the CPU reads big (2 MiB, cached, a word at a time) twice, the GE idle", "-", 1, {0}},
  {"the same, the GE drawing the layers from RAM", "-", 1, {0}},
  {"the same, the GE drawing the layers from VRAM", "-", 1, {0}},
  {"the CPU follows a chain of 32768 pointers 64 bytes apart about big, the GE idle", "-", 1, {0}},
  {"the same, the GE drawing the layers from RAM", "-", 1, {0}},
  {"the GE's layers from RAM alone", "-", 1, {0}},
  {"the same, a thread reading big over and over meanwhile", "-", 1, {0}},
  {"the GE's layers from VRAM, a thread reading big over and over meanwhile", "-", 1, {0}},
};
static volatile int reading;
static volatile unsigned int readSum;
static int readingThread(SceSize length, void* argument) {
  (void)length, (void)argument;
  while(reading) readSum += cpuRead(big, sizeof(big));
  return 0;
}
static void buildContention(int c, int size) {
  (void)size;
  if(c == 3 || c == 4) cpuChain(big, 32768, 64);
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  timedTexture(GU_PSM_8888, 0, 256, 256, c == 2 || c == 7 ? (void*)(0x04000000 + Spare) : (void*)texture, 0);
  for(int layer = 0; layer < 32; layer++) timedSprite(0, 0, 256, 256, 0, 0, 256, 256, 0xffffffff);
  sceGuFinish();
}
static int runContention(int c, int size, const char* what, unsigned int* times) {
  (void)size;
  if(c >= 5) {
    SceUID thread = -1;
    if(c >= 6) {
      reading = 1;
      thread = sceKernelCreateThread("reading", readingThread, 0x30, 0x1000, 0, 0);
      if(thread >= 0) sceKernelStartThread(thread, 0, 0);
    }
    int ran = runTimed(what, times);
    reading = 0;
    if(thread >= 0) {
      SceUInt timeout = 1000000;
      sceKernelWaitThreadEnd(thread, &timeout);
      sceKernelDeleteThread(thread);
    }
    return ran;
  }
  int drawing = c == 1 || c == 2 || c == 4;
  unsigned int began = 0;
  if(drawing) {
    while(sceKernelPollSema(timingSemaphore, 1) >= 0) continue;
    finishes = 0;
    began = sceKernelGetSystemTimeLow();
    int id = sceGeListEnQueue(TIMED, 0, timingCallbacks, 0);
    if(id < 0) {
      refused(id, what);
      times[1] = id;
      return 0;
    }
  }
  unsigned int start = sceKernelGetSystemTimeLow();
  readSum += c >= 3 ? cpuChase(big, 32768) : cpuRead(big, sizeof(big)) + cpuRead(big, sizeof(big));
  times[0] = sceKernelGetSystemTimeLow() - start;
  times[1] = sceGeDrawSync(1);
  return !drawing || awaitFinishes(1, began, what);
}

static void writeManifest8(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest8.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 8 (tools/psp-measure/ge.c and cpu.c in Phobos say what each test draws and runs;\n"
    "docs/psp-core.md, part 107, what each is for). time-* and cpu-contention: records of words as round 6's: case,\n"
    "size, CPU MHz << 16 | bus MHz (as scePower gave them back), repeats | values << 16, then per run `values` times\n"
    "in microseconds from just before sceGeListEnQueue: to the finish callback, to sceGeListEnQueue's return, and\n"
    "(time-lists) to the last sceGeListEnQueue's or sceGeListUpdateStallAddr's return, (time-signals2) to the signal\n"
    "callback; 0xffffffff for a run not\n"
    "finished (see <name>.stalled). time-lists and time-scenes run at 222/111, 333/166, 333/222/166 and 266/133 MHz\n"
    "as asked, the rest at the first two; the clocks as found are put back after each. cpu-contention's cases 0-4:\n"
    "the CPU's loop (us), then sceGeDrawSync(1) as it ended. cpu-*: records of words: case, size, CPU MHz << 16 |\n"
    "bus MHz, repeats | values << 16, then per run `values` words (cpu.c says which). The cases (test case: what;\n"
    "size):\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  static const struct { const char* test; const TimedCase* cases; int count; } tests[] = {
    {"time-lists", listCases, sizeof(listCases) / sizeof(listCases[0])},
    {"time-signals2", signalCases2, sizeof(signalCases2) / sizeof(signalCases2[0])},
    {"time-state", stateCases, sizeof(stateCases) / sizeof(stateCases[0])},
    {"time-vertices", vertexCases, sizeof(vertexCases) / sizeof(vertexCases[0])},
    {"time-setup", setupCases, sizeof(setupCases) / sizeof(setupCases[0])},
    {"time-pixels", pixelCases, sizeof(pixelCases) / sizeof(pixelCases[0])},
    {"time-strips", stripCases, sizeof(stripCases) / sizeof(stripCases[0])},
    {"time-textures", textureCases, sizeof(textureCases) / sizeof(textureCases[0])},
    {"time-transfers2", transferCases2, sizeof(transferCases2) / sizeof(transferCases2[0])},
    {"time-clears", clearCases, sizeof(clearCases) / sizeof(clearCases[0])},
    {"time-overlap", overlapCases, sizeof(overlapCases) / sizeof(overlapCases[0])},
    {"time-scenes", sceneCases, sizeof(sceneCases) / sizeof(sceneCases[0])},
    {"cpu-contention", contentionCases, sizeof(contentionCases) / sizeof(contentionCases[0])},
  };
  char line[320];
  for(unsigned int t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
    for(int c = 0; c < tests[t].count; c++) {
      int length = snprintf(line, sizeof(line), "%s %d: %s; %s\n", tests[t].test, c, tests[t].cases[c].name,
                            tests[t].cases[c].unit);
      sceIoWrite(file, line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
    }
  }
  cpuManifest(file);
  static const char end[] =
    "<name>.stalled: the GE hadn't finished the test's list after 5 s and was reset (manifest4.txt says more).\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, end, sizeof(end) - 1);
  sceIoClose(file);
}

static void round8(void) {
  foundCpu = scePowerGetCpuClockFrequencyInt();
  foundBus = scePowerGetBusClockFrequencyInt();
  foundPll = foundCpu > 2 * foundBus ? foundCpu : 2 * foundBus;  //(as round 6 reads them)
  writeManifest8();
  prepareTexels();
#define CASES(table) table, sizeof(table) / sizeof(table[0])
  timeCases8("time-lists", CASES(listCases), 16, 3, AtAll, buildLists, runLists);
  timeCases8("time-signals2", CASES(signalCases2), 8, 3, AtTwo, buildSignals2, 0);
  timeCases8("time-state", CASES(stateCases), 8, 2, AtTwo, buildState, 0);
  timeCases8("time-vertices", CASES(vertexCases), 16, 2, AtTwo, buildVertices, 0);
  timeCases8("time-setup", CASES(setupCases), 8, 2, AtTwo, buildSetup, 0);
  timeCases8("time-pixels", CASES(pixelCases), 8, 2, AtTwo, buildPixels, 0);
  timeCases8("time-strips", CASES(stripCases), 8, 2, AtTwo, buildStrips, 0);
  timeCases8("time-textures", CASES(textureCases), 8, 2, AtTwo, buildTextures, 0);
  timeCases8("time-transfers2", CASES(transferCases2), 8, 2, AtTwo, buildTransfers2, 0);
  timeCases8("time-clears", CASES(clearCases), 8, 2, AtTwo, buildClears, 0);
  timeCases8("time-overlap", CASES(overlapCases), 8, 2, AtTwo, buildOverlap, 0);
  timeCases8("time-scenes", CASES(sceneCases), 8, 2, AtAll, buildScenes, 0);
  timeCases8("cpu-contention", CASES(contentionCases), 4, 2, AtTwo, buildContention, runContention);
#undef CASES
  if(!cpuTests()) failed = 1;
}

//---- round 9: what the fit of rounds 6 and 8 (#269, part 106) still guesses where its gate misses (docs/psp-core.md,
//part 110): triangles reading narrow textures, a texture's loads and the cache's reuse, vertex formats, time-lists
//again with its fix, and anchors repeating round 8's cases, to show the PSP drawing as it did then. Each test is
//round 8's kind (timeCases8), at 222/111 and 333/166 MHz, time-lists2 at all four settings.

//-- anchors (time-anchors): round 8's cases, each built by round 8's own function at its sizes, so this session's
//times can be set beside that run's (round 6 repeated within 0.15% at the median in round 8's session). First, so
//they find the PSP as round 8's tests did.
static const TimedCase anchorCases[] = {
  {"round 8's time-strips 2: VRAM, 8888 512x512, strips 32 wide", "layers", 2, {1, 2}},
  {"round 8's time-strips 6: VRAM, 8888 512x512, one sprite 512 wide", "layers", 2, {1, 2}},
  {"round 8's time-strips 30: RAM, 8888 512x512 not swizzled, strips 32 wide", "layers", 2, {1, 2}},
  {"round 8's time-strips 37: RAM, 8888 512x512 swizzled, strips 32 wide", "layers", 2, {1, 2}},
  {"round 8's time-strips 41: RAM, 8888 512x512 swizzled, one sprite 512 wide", "layers", 2, {1, 2}},
  {"round 8's time-strips 44: RAM, CLUT8 512x256 swizzled, strips 32 wide", "layers", 2, {1, 2}},
  {"round 8's time-textures 13: 3D: RAM 8888 256x256 swizzled over 256x256 pixels, w 1 at the left to 2 at the right",
   "layers", 2, {1, 2}},
  {"round 8's time-textures 19: sprites 32x32, each its own CLUT8 32x32 texture (RAM, swizzled, 1 KiB), one palette",
   "sprites", 2, {64, 256}},
  {"round 8's time-state 0: nothing between the sprites", "sprites", 2, {256, 1024}},
  {"round 8's time-state 1: TEX_FLUSH before each", "sprites", 2, {256, 1024}},
  {"round 8's time-vertices 0: a strip, float positions", "triangles", 3, {1024, 4096, 16384}},
  {"round 8's time-vertices 2: a strip, 8-bit positions", "triangles", 3, {1024, 4096, 16384}},
  {"round 8's time-pixels 0: the textured fill alone", "layers", 3, {1, 2, 4}},
  {"round 8's time-setup 1: squares 16x16", "layers", 2, {1, 2}},
  {"round 8's time-scenes 0: Killzone's scene list", "lists", 2, {1, 2}},
};
static const struct { void (*build)(int c, int size); int c; } anchors[] = {
  {buildStrips, 2}, {buildStrips, 6}, {buildStrips, 30}, {buildStrips, 37}, {buildStrips, 41}, {buildStrips, 44},
  {buildTextures, 13}, {buildTextures, 19}, {buildState, 0}, {buildState, 1}, {buildVertices, 0}, {buildVertices, 2},
  {buildPixels, 0}, {buildSetup, 1}, {buildScenes, 0},
};
static void buildAnchors(int c, int size) { anchors[c].build(anchors[c].c, size); }

//-- triangles reading narrow textures (time-tristrips): round 8's time-strips drew textures 512 texels wide texel for
//pixel over the target's 512x256 in sprites 8 to 512 wide, and a sprite took about 4 to 7.5 times as long a pixel once
//its rows spanned 256 distinct texels (512 indices). Triangles took a 256x256 swizzled texture over 256x256 pixels in
//perspective (w 1 to 2) at 3.9 clocks a pixel where a 256-wide sprite of it took 13.3 (time-textures 13, time-pixels
//0), and #269's model takes triangles reading narrow textures at a sprite's rates, unmeasured: what games mostly draw.
//Cases 0-48: the strips as triangles, two to a strip (rectangle9), in through mode, from seven textures
//(stripTextures9); 49-62: sprites over the two swizzled VRAM textures, which round 8 didn't have; 63-74: squares 8 to
//64 pixels across, two triangles each, tiling the target as a mesh draws a big texture; 75-78: strips in 3D (identity
//matrices, w 1) beside through mode's; 79-83: strips 32 wide, bilinear (each pixel's middle on a texel's, as the
//nearest cases), triangles and sprites; 84-87: strips 32 pixels (64 texels) wide over the target's left 256x256, the
//texture shrunk 2:1 (#269's model takes the square root of the texels a pixel), triangles and sprites. A VRAM texture
//is VRAM's first megabyte (the text on the screen), a RAM one big, as in time-strips.
static const BigTexture stripTextures9[7] = {
  {GU_PSM_8888, 1, 0, 512}, {GU_PSM_8888, 0, 0, 512}, {GU_PSM_8888, 1, 1, 512}, {GU_PSM_8888, 0, 1, 512},
  {GU_PSM_T8, 1, 0, 256}, {GU_PSM_T8, 1, 1, 512}, {GU_PSM_5650, 1, 0, 512},
};
#define TRIANGLE_WIDTHS(texture)                                                                                    \
  {texture ", strips 8 wide as triangles", "layers", 2, {1, 2}},                                                    \
  {texture ", strips 16 wide as triangles", "layers", 2, {1, 2}},                                                   \
  {texture ", strips 32 wide as triangles", "layers", 2, {1, 2}},                                                   \
  {texture ", strips 64 wide as triangles", "layers", 2, {1, 2}},                                                   \
  {texture ", strips 128 wide as triangles", "layers", 2, {1, 2}},                                                  \
  {texture ", strips 256 wide as triangles", "layers", 2, {1, 2}},                                                  \
  {texture ", all 512 wide as two triangles", "layers", 2, {1, 2}}
#define SQUARE_SIDES(texture)                                                                                       \
  {texture ", squares 8x8 of two triangles", "layers", 2, {1, 2}},                                                  \
  {texture ", squares 16x16 of two triangles", "layers", 2, {1, 2}},                                                \
  {texture ", squares 32x32 of two triangles", "layers", 2, {1, 2}},                                                \
  {texture ", squares 64x64 of two triangles", "layers", 2, {1, 2}}
static const TimedCase triStripCases[] = {
  TRIANGLE_WIDTHS("RAM, 8888 512x512 swizzled"), TRIANGLE_WIDTHS("RAM, 8888 512x512 not swizzled"),
  TRIANGLE_WIDTHS("VRAM, 8888 512x512 swizzled"), TRIANGLE_WIDTHS("VRAM, 8888 512x512 not swizzled"),
  TRIANGLE_WIDTHS("RAM, CLUT8 512x256 swizzled"), TRIANGLE_WIDTHS("VRAM, CLUT8 512x512 swizzled"),
  TRIANGLE_WIDTHS("RAM, 5650 512x512 swizzled"),
  STRIP_WIDTHS("sprites: VRAM, 8888 512x512 swizzled"), STRIP_WIDTHS("sprites: VRAM, CLUT8 512x512 swizzled"),
  SQUARE_SIDES("RAM, 8888 512x512 swizzled"), SQUARE_SIDES("VRAM, 8888 512x512 swizzled"),
  SQUARE_SIDES("RAM, CLUT8 512x256 swizzled"),
  {"3D: RAM, 8888 512x512 swizzled, strips 32 wide as triangles", "layers", 2, {1, 2}},
  {"3D: RAM, 8888 512x512 swizzled, strips 256 wide as triangles", "layers", 2, {1, 2}},
  {"3D: VRAM, 8888 512x512 swizzled, strips 32 wide as triangles", "layers", 2, {1, 2}},
  {"3D: VRAM, 8888 512x512 swizzled, strips 256 wide as triangles", "layers", 2, {1, 2}},
  {"bilinear: RAM, 8888 512x512 swizzled, strips 32 wide as triangles", "layers", 2, {1, 2}},
  {"bilinear: VRAM, 8888 512x512 swizzled, strips 32 wide as triangles", "layers", 2, {1, 2}},
  {"bilinear: RAM, CLUT8 512x256 swizzled, strips 32 wide as triangles", "layers", 2, {1, 2}},
  {"bilinear: sprites: RAM, 8888 512x512 swizzled, strips 32 wide", "layers", 2, {1, 2}},
  {"bilinear: sprites: VRAM, 8888 512x512 swizzled, strips 32 wide", "layers", 2, {1, 2}},
  {"shrunk 2:1 onto 256x256: RAM, 8888 512x512 swizzled, strips 32 pixels wide as triangles", "layers", 2, {1, 2}},
  {"shrunk 2:1 onto 256x256: VRAM, 8888 512x512 swizzled, strips 32 pixels wide as triangles", "layers", 2, {1, 2}},
  {"shrunk 2:1 onto 256x256: sprites: RAM, 8888 512x512 swizzled, strips 32 pixels wide", "layers", 2, {1, 2}},
  {"shrunk 2:1 onto 256x256: sprites: VRAM, 8888 512x512 swizzled, strips 32 pixels wide", "layers", 2, {1, 2}},
};
//The two triangles of the target's rectangle (x0, y0) to (x1, y1), `scale` texels a pixel from a texture `height`
//texels tall (texel x * scale at pixel x): its top-left, top-right and bottom-left corners, then its top-right,
//bottom-right and bottom-left; in through mode, or in 3D over the target's 512x256 (wide3D), the texture coordinates
//then fractions of the texture.
static void rectangle9(FloatVertex* t, float x0, float y0, float x1, float y1, float scale, int in3D, int height) {
  FloatVertex corners[4];
  for(int k = 0; k < 4; k++) {
    float x = k & 1 ? x1 : x0, y = k & 2 ? y1 : y0, u = x * scale, v = y * scale;
    corners[k] = in3D ? (FloatVertex){u / 512, v / height, 0xffffffff, (x - 256) / 256, (128 - y) / 128, 0}
                      : (FloatVertex){u, v, 0xffffffff, x, y, 0};
  }
  t[0] = corners[0], t[1] = corners[1], t[2] = corners[2], t[3] = corners[1], t[4] = corners[3], t[5] = corners[2];
}
//3D over the target's 512x256 (identity matrices, w 1): x from -1 to 1 across its 512 pixels, y from 1 to -1 down its
//256 rows; the depth test off.
static void wide3D(void) {
  setUp3D(&identity, 1);
  sceGuOffset(2048 - 256, 2048 - 128);
  sceGuViewport(2048, 2048, 512, 256);
  sceGuDisable(GU_DEPTH_TEST);
}
//The target's top-left `across` x 256 pixels tiled by rectangles `width` x `height` (the last column cut to fit), each
//two triangles (rectangle9), in one block of vertices drawn `layers` times.
static void rectangles9(int across, int width, int height, float scale, int in3D, int texels, int layers) {
  int columns = (across + width - 1) / width, rows = 256 / height, count = columns * rows * 6;
  FloatVertex* v = sceGuGetMemory(count * sizeof(FloatVertex));
  for(int j = 0; j < rows; j++) {
    for(int i = 0; i < columns; i++) {
      float x0 = i * width, x1 = x0 + width < across ? x0 + width : across, y0 = j * height;
      rectangle9(v + (j * columns + i) * 6, x0, y0, x1, y0 + height, scale, in3D, texels);
    }
  }
  for(int layer = 0; layer < layers; layer++) {
    sceGuDrawArray(GU_TRIANGLES, in3D ? FloatVertexType3D : FloatVertexType, count, 0, v);
  }
}
static void buildTriStrips(int c, int layers) {
  static const int squareTextures[3] = {0, 2, 4}, textures3D[2] = {0, 2}, bilinearTextures[5] = {0, 2, 4, 0, 2};
  int texture, width = 32, height = 256, across = 512, sprites = 0, in3D = 0, linear = 0;
  float scale = 1;
  if(c < 49) texture = c / 7, width = 8 << c % 7;
  else if(c < 63) texture = c < 56 ? 2 : 5, width = 8 << (c - 49) % 7, sprites = 1;
  else if(c < 75) texture = squareTextures[(c - 63) / 4], width = height = 8 << (c - 63) % 4;
  else if(c < 79) texture = textures3D[(c - 75) / 2], width = (c - 75) % 2 ? 256 : 32, in3D = 1;
  else if(c < 84) texture = bilinearTextures[c - 79], sprites = c >= 82, linear = 1;
  else texture = c % 2 ? 2 : 0, sprites = c >= 86, across = 256, scale = 2;
  const BigTexture* t = &stripTextures9[texture];
  timedList(GU_PSM_8888);
  if(in3D) wide3D();
  texture8(t->psm, t->swizzled, 512, t->height, t->vram ? VRAM_TEXTURE : (void*)big, linear);
  if(sprites) {
    for(int layer = 0; layer < layers; layer++) spriteStrips(0, 0, across, 256, width, 0, 0, scale);
  } else {
    rectangles9(across, width, height, scale, in3D, t->height, layers);
  }
  sceGuFinish();
}

//-- a texture's loads and the cache's reuse (time-loads): round 8's time-textures 19-24 had a texture of its own for
//each 32x32 sprite cost the PSP about 7.4 microseconds a sprite whether it was 1, 4 or 16 KiB (32x32 texels of each
//drawn), where #269's model loads a texture of 8 KiB or less whole, by its size; and time-state had the same texture
//set again cost as much as another (sceGuTexImage's TEX_FLUSH reloads it). Each case draws N sprites, each its own
//PRIM, in turn over the target, `pixels` square (32 unless said), texel for pixel from the texture's top left
//(repeating where the texture is smaller), with this done before each (`what`): nothing (Once: the texture set with the
//list); a new texture (New: the next in big or VRAM's first megabyte, by sceGuTexImage: its address, buffer width, size
//and TEX_FLUSH); the same set again (Again); TEX_FLUSH alone (Flush); the address and buffer width alone, TEX_ADDR0 and
//TEX_BUFWIDTH0 without TEX_FLUSH, the same or a new one (SameAddress, NewAddress); two textures in turn, either way
//(InTurn, InTurnAddress). SetsAlone, FlushesAlone and LoadsAlone do N of New, Flush or a palette's load with no sprite
//between, then one sprite. `colors`: a palette loaded before each sprite too (8888 from RAM, 5650 from RAM, or 8888
//from VRAM by `palette`). The Mip cases draw squares 32x32 pixels of two triangles in 3D (identity matrices), each its
//own CLUT8 texture: 32x32 at 1:1 (MipLevel0); 64x64 with a 32x32 level 1, the level set to 1 (MipLevel1, 1:1 on it);
//64x64 without, shrunk 2:1 (MipShrunk); 64x64 with its level 1, the level chosen (MipAuto, 2:1); the last set once
//(MipAutoOnce). A row of texels is 16 bytes at least (an 8x8 CLUT8 texture's 16 texels apart). CLUT textures take the
//palette (256 colors, 8888) set with the texture.
enum { Once, New, Again, Flush, SameAddress, NewAddress, InTurn, InTurnAddress, SetsAlone, FlushesAlone, LoadsAlone,
       MipLevel0, MipLevel1, MipShrunk, MipAuto, MipAutoOnce };
typedef struct { short psm, swizzled, vram, side, pixels, what, colors, palette; } LoadCase;
static const LoadCase loads[] = {
  {GU_PSM_T8, 1, 0, 32, 32, Once}, {GU_PSM_T8, 1, 0, 8, 32, New}, {GU_PSM_T8, 1, 0, 16, 32, New},
  {GU_PSM_T8, 1, 0, 32, 32, New}, {GU_PSM_T8, 1, 0, 64, 32, New}, {GU_PSM_T8, 1, 0, 128, 32, New},
  {GU_PSM_T8, 1, 0, 256, 32, New}, {GU_PSM_T8, 1, 0, 64, 64, Once}, {GU_PSM_T8, 1, 0, 64, 64, New},
  {GU_PSM_T8, 1, 0, 128, 128, Once}, {GU_PSM_T8, 1, 0, 128, 128, New}, {GU_PSM_T8, 1, 0, 8, 8, Once},
  {GU_PSM_T8, 1, 0, 8, 8, New},
  {GU_PSM_T8, 1, 0, 32, 32, Again}, {GU_PSM_T8, 1, 0, 32, 32, Flush}, {GU_PSM_T8, 1, 0, 32, 32, SameAddress},
  {GU_PSM_T8, 1, 0, 32, 32, NewAddress}, {GU_PSM_T8, 1, 0, 32, 32, InTurn}, {GU_PSM_T8, 1, 0, 32, 32, InTurnAddress},
  {GU_PSM_8888, 1, 0, 32, 32, Once}, {GU_PSM_8888, 1, 0, 16, 32, New}, {GU_PSM_8888, 1, 0, 32, 32, New},
  {GU_PSM_8888, 1, 0, 64, 32, New},
  {GU_PSM_8888, 0, 0, 32, 32, Once}, {GU_PSM_8888, 0, 0, 16, 32, New}, {GU_PSM_8888, 0, 0, 32, 32, New},
  {GU_PSM_8888, 0, 0, 64, 32, New},
  {GU_PSM_8888, 0, 1, 32, 32, Once}, {GU_PSM_8888, 0, 1, 32, 32, New}, {GU_PSM_8888, 0, 1, 64, 32, New},
  {GU_PSM_8888, 1, 1, 32, 32, New},
  {GU_PSM_5650, 1, 0, 32, 32, Once}, {GU_PSM_5650, 1, 0, 32, 32, New},
  {GU_PSM_T4, 1, 0, 32, 32, Once}, {GU_PSM_T4, 1, 0, 32, 32, New}, {GU_PSM_T4, 1, 0, 64, 32, New},
  {GU_PSM_T8, 0, 0, 32, 32, New}, {GU_PSM_T8, 1, 1, 32, 32, New},
  {GU_PSM_T8, 1, 0, 32, 32, SetsAlone}, {GU_PSM_T8, 1, 0, 32, 32, FlushesAlone},
  {GU_PSM_T8, 1, 0, 32, 32, LoadsAlone, 256},
  {GU_PSM_T8, 1, 0, 32, 32, Once, 16}, {GU_PSM_T8, 1, 0, 32, 32, Once, 32}, {GU_PSM_T8, 1, 0, 32, 32, Once, 64},
  {GU_PSM_T8, 1, 0, 32, 32, Once, 128}, {GU_PSM_T8, 1, 0, 32, 32, Once, 256}, {GU_PSM_T8, 1, 0, 32, 32, Once, 256, 1},
  {GU_PSM_T8, 1, 0, 32, 32, Once, 256, 2}, {GU_PSM_T8, 1, 0, 32, 32, New, 256},
  {GU_PSM_T8, 1, 0, 32, 32, MipLevel0}, {GU_PSM_T8, 1, 0, 64, 32, MipLevel1}, {GU_PSM_T8, 1, 0, 64, 32, MipShrunk},
  {GU_PSM_T8, 1, 0, 64, 32, MipAuto}, {GU_PSM_T8, 1, 0, 64, 32, MipAutoOnce},
};
static const TimedCase loadCases[] = {
  {"CLUT8 swizzled in RAM, one 32x32 texture set once", "sprites", 2, {64, 256}},
  {"CLUT8 swizzled in RAM, a new 8x8 texture before each sprite (128 bytes; repeating 4 times each way)", "sprites", 2,
   {64, 256}},
  {"the same, a new 16x16 texture (256 bytes; repeating twice each way)", "sprites", 2, {64, 256}},
  {"the same, a new 32x32 texture (1 KiB)", "sprites", 2, {64, 256}},
  {"the same, a new 64x64 texture (4 KiB), its top-left 32x32 texels", "sprites", 2, {64, 256}},
  {"the same, a new 128x128 texture (16 KiB), its top-left 32x32 texels", "sprites", 2, {64, 256}},
  {"the same, a new 256x256 texture (64 KiB), its top-left 32x32 texels", "sprites", 2, {64, 256}},
  {"CLUT8 swizzled in RAM, one 64x64 texture set once, sprites 64x64", "sprites", 2, {64, 256}},
  {"the same, a new 64x64 texture before each sprite 64x64 (all of it)", "sprites", 2, {64, 256}},
  {"CLUT8 swizzled in RAM, one 128x128 texture set once, sprites 128x128", "sprites", 2, {64, 256}},
  {"the same, a new 128x128 texture before each sprite 128x128 (all of it)", "sprites", 2, {64, 256}},
  {"CLUT8 swizzled in RAM, one 8x8 texture set once, sprites 8x8", "sprites", 2, {64, 256}},
  {"the same, a new 8x8 texture before each sprite 8x8", "sprites", 2, {64, 256}},
  {"CLUT8 32x32: the same texture set again before each sprite (sceGuTexImage, with TEX_FLUSH)", "sprites", 2,
   {64, 256}},
  {"CLUT8 32x32: TEX_FLUSH alone before each sprite", "sprites", 2, {64, 256}},
  {"CLUT8 32x32: the same texture's address and buffer width before each sprite, no TEX_FLUSH", "sprites", 2,
   {64, 256}},
  {"CLUT8 32x32: a new texture's address and buffer width before each sprite, no TEX_FLUSH", "sprites", 2, {64, 256}},
  {"CLUT8 32x32: two textures in turn (sceGuTexImage, with TEX_FLUSH)", "sprites", 2, {64, 256}},
  {"CLUT8 32x32: two textures in turn, their addresses and buffer widths alone (no TEX_FLUSH)", "sprites", 2,
   {64, 256}},
  {"8888 swizzled in RAM, one 32x32 texture set once", "sprites", 2, {64, 256}},
  {"the same, a new 16x16 texture before each sprite (1 KiB; repeating)", "sprites", 2, {64, 256}},
  {"the same, a new 32x32 texture (4 KiB)", "sprites", 2, {64, 256}},
  {"the same, a new 64x64 texture (16 KiB), its top-left 32x32 texels", "sprites", 2, {64, 256}},
  {"8888 not swizzled in RAM, one 32x32 texture set once", "sprites", 2, {64, 256}},
  {"the same, a new 16x16 texture before each sprite (1 KiB; repeating)", "sprites", 2, {64, 256}},
  {"the same, a new 32x32 texture (4 KiB)", "sprites", 2, {64, 256}},
  {"the same, a new 64x64 texture (16 KiB), its top-left 32x32 texels", "sprites", 2, {64, 256}},
  {"8888 not swizzled in VRAM, one 32x32 texture set once", "sprites", 2, {64, 256}},
  {"the same, a new 32x32 texture before each sprite (4 KiB)", "sprites", 2, {64, 256}},
  {"the same, a new 64x64 texture (16 KiB), its top-left 32x32 texels", "sprites", 2, {64, 256}},
  {"8888 swizzled in VRAM, a new 32x32 texture before each sprite", "sprites", 2, {64, 256}},
  {"5650 swizzled in RAM, one 32x32 texture set once", "sprites", 2, {64, 256}},
  {"the same, a new 32x32 texture before each sprite (2 KiB)", "sprites", 2, {64, 256}},
  {"CLUT4 swizzled in RAM, one 32x32 texture set once", "sprites", 2, {64, 256}},
  {"the same, a new 32x32 texture before each sprite (512 bytes)", "sprites", 2, {64, 256}},
  {"the same, a new 64x64 texture (2 KiB), its top-left 32x32 texels", "sprites", 2, {64, 256}},
  {"CLUT8 not swizzled in RAM, a new 32x32 texture before each sprite", "sprites", 2, {64, 256}},
  {"CLUT8 swizzled in VRAM, a new 32x32 texture before each sprite", "sprites", 2, {64, 256}},
  {"N new CLUT8 32x32 textures set (sceGuTexImage) with no sprite between, then one sprite", "textures", 2,
   {64, 256}},
  {"N TEX_FLUSHes with no sprite between, then one sprite", "flushes", 2, {64, 256}},
  {"N palette loads (256 colors, 8888) with no sprite between, then one sprite", "loads", 2, {64, 256}},
  {"CLUT8 32x32 set once, a palette of 16 colors (8888, 64 bytes) loaded before each sprite", "sprites", 2, {64, 256}},
  {"the same, 32 colors (128 bytes)", "sprites", 2, {64, 256}},
  {"the same, 64 colors (256 bytes)", "sprites", 2, {64, 256}},
  {"the same, 128 colors (512 bytes)", "sprites", 2, {64, 256}},
  {"the same, 256 colors (1 KiB)", "sprites", 2, {64, 256}},
  {"the same, 256 colors in 5650 (512 bytes)", "sprites", 2, {64, 256}},
  {"the same, 256 colors (8888) from VRAM", "sprites", 2, {64, 256}},
  {"a new CLUT8 32x32 texture and its palette (256 colors) before each sprite", "sprites", 2, {64, 256}},
  {"3D: squares 32x32 of two triangles, each its own CLUT8 32x32 texture, 1:1", "squares", 2, {64, 256}},
  {"3D: the same, each its own CLUT8 64x64 texture with a 32x32 mipmap, level 1 set (1:1 on it)", "squares", 2,
   {64, 256}},
  {"3D: the same 64x64 textures without the mipmap, shrunk 2:1", "squares", 2, {64, 256}},
  {"3D: the same 64x64 textures with the mipmap, the level chosen, shrunk 2:1", "squares", 2, {64, 256}},
  {"3D: one such texture set once, the level chosen, shrunk 2:1", "squares", 2, {64, 256}},
};
//A texture's address and buffer width alone (TEX_ADDR0, TEX_BUFWIDTH0): sceGuTexImage's first two commands, without
//its TEX_SIZE0 and TEX_FLUSH.
static void textureAddress(const void* texels, int bufferWidth) {
  unsigned int at = (unsigned int)texels;
  sceGuSendCommandi(0xa0, at & 0xffffff);
  sceGuSendCommandi(0xa8, (at >> 8 & 0xf0000) | bufferWidth);
}
static void buildLoads(int c, int count) {
  const LoadCase* l = &loads[c];
  int doubled = l->psm == GU_PSM_8888 ? 8 : l->psm == GU_PSM_5650 ? 4 : l->psm == GU_PSM_T8 ? 2 : 1;  //bytes a texel x2
  int width = l->side > 32 / doubled ? l->side : 32 / doubled;  //(rows of 16 bytes at least)
  int mipmaps = l->what == MipLevel1 || l->what == MipAuto || l->what == MipAutoOnce, in3D = l->what >= MipLevel0;
  int alone = l->what == SetsAlone || l->what == FlushesAlone || l->what == LoadsAlone;
  int level = width * l->side * doubled / 2, bytes = level + (mipmaps ? level / 4 : 0);
  unsigned char* base = l->vram ? (unsigned char*)VRAM_TEXTURE : (unsigned char*)big;
  int held = (l->vram ? 1 << 20 : (int)sizeof(big)) / bytes, perRow = 512 / l->pixels, rows = 256 / l->pixels;
  const void* colors = l->palette == 2 ? (const void*)(0x04000000 + Spare) : palette;
  timedList(GU_PSM_8888);
  if(in3D) setUp3D(&identity, 1), sceGuDisable(GU_DEPTH_TEST);
  texture8(l->psm, l->swizzled, l->side, l->side, base, 0);
  sceGuTexImage(0, l->side, l->side, width, base);
  if(mipmaps) {
    sceGuTexMode(l->psm, 1, 0, l->swizzled);
    sceGuTexImage(1, l->side / 2, l->side / 2, width / 2, base + level);
    sceGuTexLevelMode(l->what == MipLevel1 ? GU_TEXTURE_CONST : GU_TEXTURE_AUTO, l->what == MipLevel1 ? 1 : 0);
    sceGuTexFilter(GU_NEAREST_MIPMAP_NEAREST, GU_NEAREST);
  }
  if(l->palette == 1) sceGuClutMode(GU_PSM_5650, 0, 0xff, 0);
  for(int k = 0; k < count; k++) {
    unsigned char* next = base + (k % held) * bytes, *turn = base + (k & 1) * bytes;
    switch(l->what) {
    case New: case SetsAlone: case MipLevel0: case MipShrunk: sceGuTexImage(0, l->side, l->side, width, next); break;
    case MipLevel1: case MipAuto:
      sceGuTexImage(0, l->side, l->side, width, next);
      sceGuTexImage(1, l->side / 2, l->side / 2, width / 2, next + level);
      break;
    case Again: sceGuTexImage(0, l->side, l->side, width, base); break;
    case Flush: case FlushesAlone: sceGuTexFlush(); break;
    case SameAddress: textureAddress(base, width); break;
    case NewAddress: textureAddress(next, width); break;
    case InTurn: sceGuTexImage(0, l->side, l->side, width, turn); break;
    case InTurnAddress: textureAddress(turn, width); break;
    }
    if(l->colors) sceGuClutLoad(l->colors / (l->palette == 1 ? 16 : 8), colors);
    if(alone && k < count - 1) continue;
    if(in3D) {
      float x = (k & 7) * 32, y = (k >> 3 & 7) * 32;
      FloatVertex* v = sceGuGetMemory(6 * sizeof(FloatVertex));
      for(int n = 0; n < 6; n++) {
        int corner = (int[6]){0, 1, 2, 1, 3, 2}[n], right = corner & 1, bottom = corner >> 1;
        v[n] = (FloatVertex){right, bottom, 0xffffffff, ndcX(x + right * 32), ndcY(y + bottom * 32), 0};
      }
      sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, v);
    } else {
      float x = (k % perRow) * l->pixels, y = (k / perRow % rows) * l->pixels;
      timedSprite(x, y, x + l->pixels, y + l->pixels, 0, 0, l->pixels, l->pixels, 0xffffffff);
    }
  }
  sceGuFinish();
}

//-- vertex formats (time-formats): the front end's time a vertex by its format, the same triangles in each format
//(round 8's time-vertices had 8-bit positions take 4.5 times #269's model's time and 16-bit ones 1.5 times, floats as
//fitted, its strips' corners falling a little differently in each format). A list of N triangles, three vertices
//each, every vertex once: in 32x32 cells of 4 pixels, corners (x, y), (x, y + 2), (x + 2, y), counterclockwise, in the
//middle 128x128 pixels in 3D (identity matrices) or the top-left 128x128 in through mode; positions in whole units that
//every format holds exactly (putVertex9). Cases 0-15: 3D, culled as back faces (the front face clockwise), positions
//alone and with a color, texture coordinates or a normal of each width, and whole vertices (texture coordinates, color,
//normal, position) of 16-bit, float and 8-bit parts; 16-20: through 16-bit indices (each vertex once), and 8-bit ones
//85 triangles a draw; 21-25: 3D, drawn (culling off: each about 2 pixels); 26-30: through mode, drawn (but 28, whose
//8-bit positions a PSP takes as 0: the front end alone); 31-33: strips of 128 triangles a row (a draw each), culled,
//positions of each width. No texture: texture coordinates are read, not used; normals unlit.
static unsigned short __attribute__((aligned(64))) indices9[3 * 16384];
enum { FormatCulled, FormatIndexed16, FormatIndexed8, FormatDrawn, FormatThrough, FormatStrip };
static const struct { Parts parts; int kind; } formats[] = {
  {{0, 0, 0, 0, 0, 4}, FormatCulled}, {{0, 0, 0, 0, 0, 2}, FormatCulled}, {{0, 0, 0, 0, 0, 1}, FormatCulled},
  {{0, 0, 0, 4, 0, 4}, FormatCulled}, {{0, 0, 0, 4, 0, 2}, FormatCulled}, {{0, 0, 0, 4, 0, 1}, FormatCulled},
  {{0, 0, 0, 2, 0, 2}, FormatCulled},
  {{0, 0, 4, 0, 0, 4}, FormatCulled}, {{0, 0, 2, 0, 0, 2}, FormatCulled}, {{0, 0, 1, 0, 0, 1}, FormatCulled},
  {{0, 0, 0, 0, 4, 4}, FormatCulled}, {{0, 0, 0, 0, 2, 2}, FormatCulled}, {{0, 0, 0, 0, 1, 1}, FormatCulled},
  {{0, 0, 2, 4, 2, 2}, FormatCulled}, {{0, 0, 4, 4, 4, 4}, FormatCulled}, {{0, 0, 1, 4, 1, 1}, FormatCulled},
  {{0, 0, 0, 0, 0, 4}, FormatIndexed16}, {{0, 0, 0, 0, 0, 2}, FormatIndexed16}, {{0, 0, 0, 0, 0, 1}, FormatIndexed16},
  {{0, 0, 2, 4, 2, 2}, FormatIndexed16}, {{0, 0, 0, 0, 0, 4}, FormatIndexed8},
  {{0, 0, 0, 0, 0, 4}, FormatDrawn}, {{0, 0, 0, 0, 0, 2}, FormatDrawn}, {{0, 0, 0, 0, 0, 1}, FormatDrawn},
  {{0, 0, 2, 4, 2, 2}, FormatDrawn}, {{0, 0, 0, 4, 0, 4}, FormatDrawn},
  {{0, 0, 0, 0, 0, 4}, FormatThrough}, {{0, 0, 0, 0, 0, 2}, FormatThrough}, {{0, 0, 0, 0, 0, 1}, FormatThrough},
  {{0, 0, 0, 4, 0, 2}, FormatThrough}, {{0, 0, 2, 4, 0, 2}, FormatThrough},
  {{0, 0, 0, 0, 0, 4}, FormatStrip}, {{0, 0, 0, 0, 0, 2}, FormatStrip}, {{0, 0, 0, 0, 0, 1}, FormatStrip},
};
static const TimedCase formatCases[] = {
  {"3D, culled: float positions (12 bytes a vertex)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: 16-bit positions (6)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: 8-bit positions (3)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: an 8888 color, float positions (16)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: an 8888 color, 16-bit positions (12)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: an 8888 color, 8-bit positions (8)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: a 5650 color, 16-bit positions (8)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: float texture coordinates and positions (20)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: 16-bit texture coordinates and positions (10)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: 8-bit texture coordinates and positions (5)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: float normals and positions (24)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: 16-bit normals and positions (12)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: 8-bit normals and positions (6)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: 16-bit texture coordinates, an 8888 color, 16-bit normals and positions (20)", "triangles", 3,
   {1024, 4096, 16384}},
  {"3D, culled: float texture coordinates, an 8888 color, float normals and positions (36)", "triangles", 3,
   {1024, 4096, 16384}},
  {"3D, culled: 8-bit texture coordinates, an 8888 color, 8-bit normals and positions (16)", "triangles", 3,
   {1024, 4096, 16384}},
  {"3D, culled, through 16-bit indices: float positions", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled, through 16-bit indices: 16-bit positions", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled, through 16-bit indices: 8-bit positions", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled, through 16-bit indices: case 13's vertices (20 bytes)", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled, through 8-bit indices, 85 triangles a draw: float positions", "triangles", 3, {1020, 4080, 16320}},
  {"3D, drawn: float positions", "triangles", 3, {1024, 4096, 16384}},
  {"3D, drawn: 16-bit positions", "triangles", 3, {1024, 4096, 16384}},
  {"3D, drawn: 8-bit positions", "triangles", 3, {1024, 4096, 16384}},
  {"3D, drawn: case 13's vertices", "triangles", 3, {1024, 4096, 16384}},
  {"3D, drawn: an 8888 color, float positions", "triangles", 3, {1024, 4096, 16384}},
  {"through mode, drawn: float positions", "triangles", 3, {1024, 4096, 16384}},
  {"through mode, drawn: 16-bit positions", "triangles", 3, {1024, 4096, 16384}},
  {"through mode: 8-bit positions (pspautotests saw a PSP take them as 0: nothing drawn)", "triangles", 3,
   {1024, 4096, 16384}},
  {"through mode, drawn: an 8888 color, 16-bit positions (12)", "triangles", 3, {1024, 4096, 16384}},
  {"through mode, drawn: 16-bit texture coordinates, an 8888 color, 16-bit positions (16)", "triangles", 3,
   {1024, 4096, 16384}},
  {"3D, culled: strips of 128 triangles, float positions", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: strips of 128 triangles, 16-bit positions", "triangles", 3, {1024, 4096, 16384}},
  {"3D, culled: strips of 128 triangles, 8-bit positions", "triangles", 3, {1024, 4096, 16384}},
};
//A vertex of these parts as putVertex lays it out (no weights), its position (x, y, 0) in whole units: in through
//mode pixels, each format the number itself (8-bit ones too, though pspautotests' gpu/primitives recorded a PSP
//drawing them at (0, 0)); in 3D 128ths of the viewport's half (a pixel each): an 8-bit k, a 16-bit 256k, a float
//k / 128. Returns where the next goes.
static unsigned char* putVertex9(unsigned char* at, const Parts* p, int x, int y, int through) {
  unsigned char* start = at;
  int largest = 1;
#define ALIGN_PART(bytes) \
  (at = start + (((at - start) + (bytes) - 1) & -(bytes)), largest = (bytes) > largest ? (bytes) : largest)
  if(p->uv) ALIGN_PART(p->uv), at = putPart(putPart(at, p->uv, 0.25f), p->uv, 0.25f);
  if(p->color == 2) ALIGN_PART(2), *(unsigned short*)at = 0xffff, at += 2;
  if(p->color == 4) ALIGN_PART(4), *(unsigned int*)at = 0xffffffff, at += 4;
  if(p->normal) ALIGN_PART(p->normal), at = putPart(putPart(putPart(at, p->normal, 0), p->normal, 0), p->normal, 1);
  ALIGN_PART(p->position);
#undef ALIGN_PART
  int position[3] = {x, y, 0};
  for(int k = 0; k < 3; k++, at += p->position) {
    if(p->position == 1) *(signed char*)at = position[k];
    else if(p->position == 2) *(short*)at = through ? position[k] : position[k] * 256;
    else *(float*)at = through ? position[k] : position[k] / 128.0f;
  }
  return start + (((at - start) + largest - 1) & -largest);
}
static void buildFormats(int c, int count) {
  const Parts* p = &formats[c].parts;
  int kind = formats[c].kind, through = kind == FormatThrough;
  int vertices = kind == FormatStrip ? count / 128 * 130 : 3 * count;
  unsigned char* at = (unsigned char*)big;
  for(int n = 0; n < vertices; n++) {
    int x, y;  //in pixels, from the 128x128's top left
    if(kind == FormatStrip) {
      x = n % 130, y = n / 130 % 32 * 4 + (n & 1) * 2;
    } else {
      int cell = n / 3 & 1023, corner = n % 3;
      x = (cell & 31) * 4 + (corner == 2 ? 2 : 0), y = (cell >> 5) * 4 + (corner == 1 ? 2 : 0);
    }
    at = through ? putVertex9(at, p, x, y, 1) : putVertex9(at, p, x - 64, 64 - y, 0);
  }
  int bytes = (at - (unsigned char*)big) / vertices;  //a vertex's
  for(int n = 0; kind == FormatIndexed16 && n < 3 * count; n++) indices9[n] = n;
  for(int n = 0; kind == FormatIndexed8 && n < 255; n++) ((unsigned char*)indices9)[n] = n;
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  int type = partsType(p) | (through ? GU_TRANSFORM_2D : GU_TRANSFORM_3D);
  if(!through) {
    setUp3D(&identity, 1);
    sceGuDisable(GU_DEPTH_TEST);
    if(kind != FormatDrawn) sceGuEnable(GU_CULL_FACE), sceGuFrontFace(GU_CW);
  }
  if(kind == FormatStrip) {
    for(int row = 0; row < count / 128; row++) {
      sceGuDrawArray(GU_TRIANGLE_STRIP, type, 130, 0, (unsigned char*)big + row * 130 * bytes);
    }
  } else if(kind == FormatIndexed8) {
    for(int first = 0; first < count; first += 85) {
      sceGuDrawArray(GU_TRIANGLES, type | GU_INDEX_8BIT, 255, indices9, (unsigned char*)big + first * 3 * bytes);
    }
  } else {
    int indexed = kind == FormatIndexed16;
    sceGuDrawArray(GU_TRIANGLES, type | (indexed ? GU_INDEX_16BIT : 0), 3 * count, indexed ? indices9 : 0, big);
  }
  sceGuFinish();
}

static void writeManifest9(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest9.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 9 (tools/psp-measure/ge.c in Phobos says what each test draws; docs/psp-core.md,\n"
    "part 110, what each is for). Records of words as round 8's (manifest8.txt): case, size, CPU MHz << 16 | bus MHz\n"
    "(as scePower gave them back), repeats | values << 16, then per run `values` times in microseconds from just\n"
    "before sceGeListEnQueue: to the finish callback, to sceGeListEnQueue's return, and (time-lists2) to the last\n"
    "sceGeListEnQueue's or sceGeListUpdateStallAddr's return; 0xffffffff for a run not finished (see\n"
    "<name>.stalled). time-lists2 is round 8's time-lists with its stall addresses cached, at 222/111, 333/166,\n"
    "333/222/166 and 266/133 MHz; the rest run at the first two; the clocks as found are put back after each.\n"
    "time-anchors' cases are round 8's, built the same way at the same sizes. The cases (test case: what; size):\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  static const struct { const char* test; const TimedCase* cases; int count; } tests[] = {
    {"time-anchors", anchorCases, sizeof(anchorCases) / sizeof(anchorCases[0])},
    {"time-tristrips", triStripCases, sizeof(triStripCases) / sizeof(triStripCases[0])},
    {"time-loads", loadCases, sizeof(loadCases) / sizeof(loadCases[0])},
    {"time-lists2", listCases, sizeof(listCases) / sizeof(listCases[0])},
    {"time-formats", formatCases, sizeof(formatCases) / sizeof(formatCases[0])},
  };
  char line[320];
  for(unsigned int t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
    for(int c = 0; c < tests[t].count; c++) {
      int length = snprintf(line, sizeof(line), "%s %d: %s; %s\n", tests[t].test, c, tests[t].cases[c].name,
                            tests[t].cases[c].unit);
      sceIoWrite(file, line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
    }
  }
  static const char end[] =
    "<name>.stalled: the GE hadn't finished the test's list after 5 s and was reset (manifest4.txt says more).\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, end, sizeof(end) - 1);
  sceIoClose(file);
}

static void round9(void) {
  foundCpu = scePowerGetCpuClockFrequencyInt();
  foundBus = scePowerGetBusClockFrequencyInt();
  foundPll = foundCpu > 2 * foundBus ? foundCpu : 2 * foundBus;  //(as round 6 reads them)
  writeManifest9();
  prepareTexels();
#define CASES(table) table, sizeof(table) / sizeof(table[0])
  timeCases8("time-anchors", CASES(anchorCases), 8, 2, AtTwo, buildAnchors, 0);
  timeCases8("time-tristrips", CASES(triStripCases), 8, 2, AtTwo, buildTriStrips, 0);
  timeCases8("time-loads", CASES(loadCases), 8, 2, AtTwo, buildLoads, 0);
  timeCases8("time-lists2", CASES(listCases), 16, 3, AtAll, buildLists, runLists);
  timeCases8("time-formats", CASES(formatCases), 16, 2, AtTwo, buildFormats, 0);
#undef CASES
}

//---- round 10: what #269's refit with round 9 (part 106) still misses, cases as its agent specified them from the
//games' logged state (docs/psp-core.md, part 112; synthetic: no game's lists, textures or vertices): lights by kind
//with and without their shine (Activision Hits Remixed's menu's first list), CLUT32 triangles (its second), wide
//triangles into a 16-bit frame buffer (its copyright screen), triangles in perspective and turned a quarter, mipmaps
//big and small, narrow strips, and the CPU reading and writing memory while the GE draws (Super Monkey Ball, Killzone,
//Ghost of Sparta); anchors first, to show the PSP drawing as it did then. Each test is round 8's kind (timeCases8),
//at 222/111 and 333/166 MHz unless said.

//-- anchors (time-anchors2): six of round 9's anchors (round 8's cases, built by round 8's functions at their sizes),
//then round 8's time-strips 37 (RAM 8888 512x512 swizzled, strips 32 wide) with its texture 0, 256, 512 and 768
//bytes past a 1 KiB boundary: round 9 drew it 2% faster than round 8, its texture on a boundary where round 8's was 256
//bytes past one (part 110: most likely the one case reading kilobyte runs, which then straddled a boundary).
static const TimedCase anchorCases10[] = {
  {"round 8's time-strips 2: VRAM, 8888 512x512, strips 32 wide", "layers", 2, {1, 2}},
  {"round 8's time-strips 37: RAM, 8888 512x512 swizzled, strips 32 wide", "layers", 2, {1, 2}},
  {"round 8's time-textures 13: 3D: RAM 8888 256x256 swizzled over 256x256 pixels, w 1 at the left to 2 at the right",
   "layers", 2, {1, 2}},
  {"round 8's time-textures 19: sprites 32x32, each its own CLUT8 32x32 texture (RAM, swizzled, 1 KiB), one palette",
   "sprites", 2, {64, 256}},
  {"round 8's time-vertices 0: a strip, float positions", "triangles", 3, {1024, 4096, 16384}},
  {"round 8's time-scenes 0: Killzone's scene list", "lists", 2, {1, 2}},
  {"time-strips 37's strips, the texture on a 1 KiB boundary", "layers", 2, {1, 2}},
  {"the same, the texture 256 bytes past a 1 KiB boundary (round 8's place)", "layers", 2, {1, 2}},
  {"the same, 512 bytes past", "layers", 2, {1, 2}},
  {"the same, 768 bytes past", "layers", 2, {1, 2}},
};
static void buildAnchors10(int c, int size) {
  static const int round9[6] = {0, 3, 6, 7, 10, 14};
  if(c < 6) {
    anchors[round9[c]].build(anchors[round9[c]].c, size);
    return;
  }
  unsigned char* texels = (unsigned char*)(((unsigned int)big + 1023) & ~1023u) + (c - 6) * 256;
  timedList(GU_PSM_8888);
  texture8(GU_PSM_8888, 1, 512, 512, texels, 0);
  for(int layer = 0; layer < size; layer++) spriteStrips(0, 0, 512, 256, 32, 0, 0, 1);
  sceGuFinish();
}

//The games' screen, 480x272: setUp's viewport and offset, the scissor too (the frame buffer's 272 rows run 32 KiB
//into Depth's place in 8888; nothing here tests depth then). A vertex of float texture coordinates and position alone
//(20 bytes), as the games' tiles and small strips have them.
static void screen480(void) {
  sceGuOffset(2048 - 240, 2048 - 136);
  sceGuViewport(2048, 2048, 480, 272);
  sceGuScissor(0, 0, 480, 272);
}
typedef struct { float u, v, x, y, z; } UvVertex;
enum { UvVertexType = GU_TEXTURE_32BITF | GU_VERTEX_32BITF };

//Rectangles `width` x `height` tiling the target's top-left `across` x `down` pixels (the last column cut to fit),
//each two triangles as rectangle9 lays them, their texels those under them (texel for pixel) or, turned, each
//rectangle's own texels a quarter turned (u down the screen, v across), in one block of vertices drawn `layers` times.
//`w` < 0: through mode; otherwise 3D under the lens over the target's 512x256 (wide3DLens), the texture coordinates
//fractions of a texture `side` texels square, each corner at the distance w gives (distance10): 1; 1 at the left of
//the rectangles to 1.25, 1.5, 2 or 4 at the right; 1 at the top to 2 at the bottom; 2; 2 at the left to 1.
enum { Flat = -1, W1, W1To1_25, W1To1_5, W1To2, W1To4, W1To2Down, W2, W2To1 };
static void wide3DLens(void) {
  setUp3D(&lens, 1);
  sceGuOffset(2048 - 256, 2048 - 128);
  sceGuViewport(2048, 2048, 512, 256);
  sceGuDisable(GU_DEPTH_TEST);
}
static float distance10(int w, float x, float y, int across, int down) {
  float right = x / across;
  switch(w) {
  case W1To1_25: return 1 + right / 4;
  case W1To1_5: return 1 + right / 2;
  case W1To2: return 1 + right;
  case W1To4: return 1 + 3 * right;
  case W1To2Down: return 1 + y / down;
  case W2: return 2;
  case W2To1: return 2 - right;
  default: return 1;
  }
}
static void rectangles10(int across, int down, int width, int height, int side, int turned, int w, int layers) {
  int columns = (across + width - 1) / width, rows = down / height, count = columns * rows * 6;
  FloatVertex* v = sceGuGetMemory(count * sizeof(FloatVertex));
  for(int j = 0; j < rows; j++) {
    for(int i = 0; i < columns; i++) {
      float x0 = i * width, x1 = x0 + width < across ? x0 + width : across, y0 = j * height, y1 = y0 + height;
      FloatVertex corners[4], *t = v + (j * columns + i) * 6;
      for(int k = 0; k < 4; k++) {
        float x = k & 1 ? x1 : x0, y = k & 2 ? y1 : y0;
        float u = turned ? x0 + (y - y0) : x, tv = turned ? y0 + (x - x0) : y;
        if(w < 0) {
          corners[k] = (FloatVertex){u, tv, 0xffffffff, x, y, 0};
        } else {
          float d = distance10(w, x, y, across, down);
          corners[k] = (FloatVertex){u / side, tv / side, 0xffffffff, (x - 256) / 256 * d, (128 - y) / 128 * d, -d};
        }
      }
      t[0] = corners[0], t[1] = corners[1], t[2] = corners[2], t[3] = corners[1], t[4] = corners[3], t[5] = corners[2];
    }
  }
  for(int layer = 0; layer < layers; layer++) {
    sceGuDrawArray(GU_TRIANGLES, w < 0 ? FloatVertexType : FloatVertexType3D, count, 0, v);
  }
}

//-- lights by kind (time-lights): Activision Hits Remixed's menu's first list, 19% short in #269's refit, is its front
//end's: 3D strips lit by two spotlights with their shine (specular), on some a directional one with its shine too.
//Round 8's time-vertices had one directional light with its shine cost 20.7 clocks a vertex, four 1.2 each, and no
//spotlight or point light with shine. Round 8's strips (culled, float normals and positions; lightsOn's lights: falling
//off (1, 0.5, 0.25), spots toward -z with exponent 4 and cutoff 0.5, shine's power 8), lit by 1 to 4 lights of each
//kind, diffuse alone and with their shine (round 8's cases 15 to 23 among them); one and four directional lights
//shining at powers 1 and 64; four directional lights, only light 0 shining, only light 3, lights 0 and 1; one point
//light shining, not falling off and falling off as the game's (0.145, 0, 0.145); the game's mixes (spots of exponent
//21.1 and cutoff 0.925 falling off as above): two spots shining, the same and a directional light shining, two spots
//not shining and a directional light not shining; the first skinned (2 weights, 8-bit); then drawn as the game draws
//(strips of 9 vertices, 12 bytes each: 8-bit texture coordinates and normals, 16-bit positions; triangles 16x20
//pixels; a CLUT4 64x64 texture, RAM, swizzled, bilinear; the depth test passing and writing, the alpha test; into
//8888) with the second mix, and unlit, at 1024 and 4096 strips.
enum { LightDirectional = 1, LightPoint = 2, LightSpot = 3, Game = 4 };  //(GU's kinds + 1; Game: the game's spots)
typedef struct { unsigned char kinds[4], shines[4]; short power; float fall[3]; short skinned, drawn; } LightCase;
#define LIGHT_COUNTS(kind, shine)                                                                                    \
  {{kind}, {shine}, 8, {1, 0.5f, 0.25f}}, {{kind, kind}, {shine, shine}, 8, {1, 0.5f, 0.25f}},                       \
  {{kind, kind, kind}, {shine, shine, shine}, 8, {1, 0.5f, 0.25f}},                                                  \
  {{kind, kind, kind, kind}, {shine, shine, shine, shine}, 8, {1, 0.5f, 0.25f}}
#define FOUR(kind) {kind, kind, kind, kind}
static const LightCase lights10[] = {
  LIGHT_COUNTS(LightDirectional, 0), LIGHT_COUNTS(LightDirectional, 1), LIGHT_COUNTS(LightPoint, 0),
  LIGHT_COUNTS(LightPoint, 1), LIGHT_COUNTS(LightSpot, 0), LIGHT_COUNTS(LightSpot, 1),
  {{LightDirectional}, {1}, 1, {1, 0.5f, 0.25f}}, {{LightDirectional}, {1}, 64, {1, 0.5f, 0.25f}},
  {FOUR(LightDirectional), FOUR(1), 1, {1, 0.5f, 0.25f}}, {FOUR(LightDirectional), FOUR(1), 64, {1, 0.5f, 0.25f}},
  {FOUR(LightDirectional), {1, 0, 0, 0}, 8, {1, 0.5f, 0.25f}},
  {FOUR(LightDirectional), {0, 0, 0, 1}, 8, {1, 0.5f, 0.25f}},
  {FOUR(LightDirectional), {1, 1, 0, 0}, 8, {1, 0.5f, 0.25f}},
  {{LightPoint}, {1}, 8, {1, 0, 0}}, {{LightPoint}, {1}, 8, {0.145f, 0, 0.145f}},
  {{Game, Game}, {1, 1}, 8, {0.145f, 0, 0.145f}}, {{Game, Game, LightDirectional}, {1, 1, 1}, 8, {0.145f, 0, 0.145f}},
  {{Game, Game, LightDirectional}, {0, 0, 0}, 8, {0.145f, 0, 0.145f}},
  {{Game, Game}, {1, 1}, 8, {0.145f, 0, 0.145f}, 1},
  {{Game, Game, LightDirectional}, {1, 1, 1}, 8, {0.145f, 0, 0.145f}, 0, 1}, {{0}, {0}, 8, {1, 0, 0}, 0, 1},
};
#define LIGHT_NAMES(kind, how)                                                                                       \
  {"a strip lit by 1 " kind how, "triangles", 3, {1024, 4096, 16384}},                                                \
  {"2 " kind "s" how, "triangles", 3, {1024, 4096, 16384}}, {"3 " kind "s" how, "triangles", 3, {1024, 4096, 16384}}, \
  {"4 " kind "s" how, "triangles", 3, {1024, 4096, 16384}}
static const TimedCase lightCases10[] = {
  LIGHT_NAMES("directional light", ", diffuse"), LIGHT_NAMES("directional light", ", diffuse and shine (specular)"),
  LIGHT_NAMES("point light", ", diffuse"), LIGHT_NAMES("point light", ", diffuse and shine"),
  LIGHT_NAMES("spotlight", ", diffuse"), LIGHT_NAMES("spotlight", ", diffuse and shine"),
  {"1 directional light with shine of power 1", "triangles", 3, {1024, 4096, 16384}},
  {"the same, power 64", "triangles", 3, {1024, 4096, 16384}},
  {"4 directional lights with shine of power 1", "triangles", 3, {1024, 4096, 16384}},
  {"the same, power 64", "triangles", 3, {1024, 4096, 16384}},
  {"4 directional lights, only light 0 with shine", "triangles", 3, {1024, 4096, 16384}},
  {"4 directional lights, only light 3 with shine", "triangles", 3, {1024, 4096, 16384}},
  {"4 directional lights, lights 0 and 1 with shine", "triangles", 3, {1024, 4096, 16384}},
  {"1 point light with shine, not falling off (1, 0, 0)", "triangles", 3, {1024, 4096, 16384}},
  {"1 point light with shine, falling off (0.145, 0, 0.145)", "triangles", 3, {1024, 4096, 16384}},
  {"the game's mix: 2 spotlights with shine (exponent 21.1, cutoff 0.925, falling off (0.145, 0, 0.145))",
   "triangles", 3, {1024, 4096, 16384}},
  {"the game's mix: the 2 spotlights and a directional light, all with shine", "triangles", 3, {1024, 4096, 16384}},
  {"the game's mix: the 2 spotlights and a directional light, none with shine", "triangles", 3, {1024, 4096, 16384}},
  {"skinned (2 weights, 8-bit), the 2 spotlights with shine", "triangles", 3, {1024, 4096, 16384}},
  {"drawn as the game draws: strips of 9 vertices (16x20-pixel triangles, CLUT4 64x64 bilinear, depth and alpha "
   "tests), the 2 spotlights and a directional light, all with shine", "strips", 2, {1024, 4096}},
  {"the same unlit", "strips", 2, {1024, 4096}},
};
_Static_assert(sizeof(lights10) / sizeof(lights10[0]) == sizeof(lightCases10) / sizeof(lightCases10[0]),
               "a line for each case");
static void buildLights10(int c, int count) {
  const LightCase* l = &lights10[c];
  int lights = 0;
  while(lights < 4 && l->kinds[lights]) lights++;
  Parts p = l->drawn ? (Parts){0, 0, 1, 0, 1, 2} : (Parts){l->skinned ? 2 : 0, l->skinned ? 1 : 0, 0, 0, 4, 4};
  int vertices = l->drawn ? count * 9 : count / 256 * 258;
  unsigned char* at = (unsigned char*)big;
  for(int n = 0; n < vertices; n++) {
    if(l->drawn) {  //strip n / 9 at a 128x20 cell of the target's 512x256 (4 across, 12 down, over and over)
      int strip = n / 9, k = n % 9;
      float x = (strip & 3) * 128 + k * 16, y = (strip / 4 % 12) * 20 + (k & 1) * 20;
      at = putPart(putPart(at, 1, k / 8.0f), 1, (k & 1) * 0.5f);  //(texture coordinates, then the normal, the position)
      at = putPart(putPart(putPart(at, 1, 0), 1, 0), 1, 1) + 1;
      at = putPart(putPart(putPart(at, 2, (x - 256) / 256), 2, (128 - y) / 128), 2, 0);
    } else {
      float x = n % 258 * 255.0f / 257, y = n / 258 * 4 + (n & 1) * 2;  //(round 8's strips: buildVertices)
      at = putVertex(at, &p, ndcX(x), ndcY(y), 0);
    }
  }
  int bytes = (at - (unsigned char*)big) / vertices;
  if(l->drawn) fillDepth(0);
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  if(l->drawn) {
    setUp3D(&identity, 1);
    sceGuOffset(2048 - 256, 2048 - 128);
    sceGuViewport(2048, 2048, 512, 256);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    texture8(GU_PSM_T4, 1, 64, 64, bytes8, 1);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  } else {
    setUp3D(&identity, 1);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE);
    sceGuFrontFace(GU_CW);
  }
  if(lights) {
    lightsOn(GU_DIRECTIONAL, lights, GU_DIFFUSE);  //(the material, the ambient light and each light's colors)
    sceGuSpecular(l->power);
    sceGuLightMode(GU_SINGLE_COLOR);
    for(int k = 0; k < lights; k++) {
      static const ScePspFVector3 toward = {0, 0, -1};
      ScePspFVector3 from = {k & 1 ? 0.5f : -0.5f, k & 2 ? 0.5f : -0.5f, 1};
      int kind = l->kinds[k] == Game ? GU_SPOTLIGHT : l->kinds[k] - 1;
      sceGuLight(k, kind, l->shines[k] ? GU_DIFFUSE_AND_SPECULAR : GU_DIFFUSE, &from);
      sceGuLightAtt(k, l->fall[0], l->fall[1], l->fall[2]);
      if(l->kinds[k] == Game) sceGuLightSpot(k, &toward, 21.1f, 0.925f);
      else if(kind == GU_SPOTLIGHT) sceGuLightSpot(k, &toward, 4, 0.5f);
    }
  }
  for(int k = 0; k < 2 && l->skinned; k++) sceGuBoneMatrix(k, &identity);
  int type = partsType(&p) | GU_TRANSFORM_3D;
  if(l->drawn) {
    for(int strip = 0; strip < count; strip++) {
      sceGuDrawArray(GU_TRIANGLE_STRIP, type, 9, 0, (unsigned char*)big + strip * 9 * bytes);
    }
  } else {
    for(int row = 0; row < count / 256; row++) {
      sceGuDrawArray(GU_TRIANGLE_STRIP, type, 258, 0, (unsigned char*)big + row * 258 * bytes);
    }
  }
  sceGuFinish();
}

//-- CLUT32 (time-clut32): Activision Hits Remixed's menu's second list, 56% over the PSP's time in #269's refit: a
//color pass over the picture its first list drew in an 8888 buffer, read back as a CLUT32 texture (512x512, VRAM, not
//swizzled, nearest), in tiles of 32x34 pixels (15 x 8 over 480x272), each a 3D strip of two triangles reading its own
//texels 1:1 (UvVertex), drawn three times in a row, the palette shifted 0, 8 and 16, the last two passes blended with
//fixed factors (added), a 1 KiB palette load before each pass but the first of each tile after the first (241 a list),
//into 5650. On the PSP the tiles' texels cost next to nothing, and no round has timed CLUT32 in triangles. A layer:
//all 120 tiles. Cases 0-14: that pattern; one pass a tile, no palette load after the first (the texels alone); the
//same bytes as 8888; a CLUT8 512x512 texture; three passes without the loads; three passes and the loads without a
//texture; one pass as sprites in through mode; into 8888; CLUT32 swizzled; from RAM (big); tiles 16x17 and 64x68 (the
//last column 32 wide); one pass reading an 8888 buffer just filled in the list (480x272, no texture: RenderedAt), and
//the same fill before tiles reading VRAM's first megabyte; bilinear. Then round 9's strips as triangles over the
//target's 512x256 from the CLUT32 texture (round 8's time-strips had it in sprites).
//VRAM past the 5650 target: an 8888 480x272 buffer there (rows of 512) runs through Depth's place, unused here, and
//96 KiB into VRAM's last 256 KiB (the contention thread's, at another time).
enum { RenderedAt = 0x150000 };
typedef struct { short psm, swizzled, vram, passes, loads, target, sprites, side, rendered, linear; } TileCase;
static const TileCase tiles10[] = {
  {GU_PSM_T32, 0, 1, 3, 1, GU_PSM_5650}, {GU_PSM_T32, 0, 1, 1, 0, GU_PSM_5650}, {GU_PSM_8888, 0, 1, 1, 0, GU_PSM_5650},
  {GU_PSM_T8, 0, 1, 1, 0, GU_PSM_5650}, {GU_PSM_T32, 0, 1, 3, 0, GU_PSM_5650}, {-1, 0, 1, 3, 1, GU_PSM_5650},
  {GU_PSM_T32, 0, 1, 1, 0, GU_PSM_5650, 1}, {GU_PSM_T32, 0, 1, 1, 0, GU_PSM_8888},
  {GU_PSM_T32, 1, 1, 1, 0, GU_PSM_5650}, {GU_PSM_T32, 0, 0, 1, 0, GU_PSM_5650},
  {GU_PSM_T32, 0, 1, 1, 0, GU_PSM_5650, 0, 1}, {GU_PSM_T32, 0, 1, 1, 0, GU_PSM_5650, 0, 2},
  {GU_PSM_T32, 0, 1, 1, 0, GU_PSM_5650, 0, 0, 1}, {GU_PSM_T32, 0, 1, 1, 0, GU_PSM_5650, 0, 0, 2},
  {GU_PSM_T32, 0, 1, 1, 0, GU_PSM_5650, 0, 0, 0, 1},
};
static const TimedCase clut32Cases[] = {
  {"the game's tiles: CLUT32 512x512 in VRAM, not swizzled, nearest, tiles 32x34 as 3D strips of two triangles, three "
   "passes (palette shifts 0, 8, 16; the last two blended, fixed factors), 240 palette loads a layer, into 5650",
   "layers", 2, {1, 2}},
  {"the same tiles, one pass, no palette load after the first", "layers", 2, {1, 2}},
  {"the same, the bytes read as 8888", "layers", 2, {1, 2}},
  {"the same, a CLUT8 512x512 texture", "layers", 2, {1, 2}},
  {"the game's three passes without the palette loads", "layers", 2, {1, 2}},
  {"the game's three passes and palette loads without a texture", "layers", 2, {1, 2}},
  {"one pass, the tiles as sprites in through mode", "layers", 2, {1, 2}},
  {"one pass, into 8888", "layers", 2, {1, 2}},
  {"one pass, CLUT32 swizzled", "layers", 2, {1, 2}},
  {"one pass, CLUT32 in RAM, not swizzled", "layers", 2, {1, 2}},
  {"one pass, tiles 16x17", "layers", 2, {1, 2}},
  {"one pass, tiles 64x68", "layers", 2, {1, 2}},
  {"one pass reading an 8888 buffer filled just before in the list (480x272, no texture)", "layers", 2, {1, 2}},
  {"the same fill, then one pass reading VRAM's first megabyte", "layers", 2, {1, 2}},
  {"one pass, bilinear", "layers", 2, {1, 2}},
  TRIANGLE_WIDTHS("VRAM, CLUT32 512x512 not swizzled, into 8888 over 512x256"),
};
static void drawTiles(const TileCase* t, int layers);
static void buildClut32(int c, int layers) {
  if(c >= 15) {
    timedList(GU_PSM_8888);
    texture8(GU_PSM_T32, 0, 512, 512, VRAM_TEXTURE, 0);
    rectangles10(512, 256, 8 << (c - 15), 256, 512, 0, Flat, layers);
    sceGuFinish();
    return;
  }
  drawTiles(&tiles10[c], layers);
}
//The tiles of a TileCase (time-clut32's, time-placement's), layers times, in a list of their own; tilesInto, in the
//list being built (time-shrink's whole list).
static void tilesInto(const TileCase* t, int layers);
static void drawTiles(const TileCase* t, int layers) {
  timedList(t->target);
  tilesInto(t, layers);
  sceGuFinish();
}
static void tilesInto(const TileCase* t, int layers) {
  static const short sides[3][2] = {{32, 34}, {16, 17}, {64, 68}};
  int w = sides[t->side][0], h = sides[t->side][1], columns = (480 + w - 1) / w, rows = 272 / h;
  int each = t->sprites ? 2 : 4, count = columns * rows;
  void* texels = t->vram ? VRAM_TEXTURE : (void*)big;
  if(!t->sprites) setUp3D(&identity, 1), sceGuDisable(GU_DEPTH_TEST);
  screen480();
  if(t->psm >= 0) texture8(t->psm, t->swizzled, 512, 512, texels, t->linear);
  else sceGuClutMode(GU_PSM_8888, 0, 0xff, 0), sceGuClutLoad(32, palette);
  UvVertex* v = sceGuGetMemory(count * each * sizeof(UvVertex));
  for(int tile = 0; tile < count; tile++) {
    float x0 = tile % columns * w, x1 = x0 + w < 480 ? x0 + w : 480, y0 = tile / columns * h, y1 = y0 + h;
    UvVertex* q = v + tile * each;
    if(t->sprites) {
      q[0] = (UvVertex){x0, y0, x0, y0, 0}, q[1] = (UvVertex){x1, y1, x1, y1, 0};
    } else {
      for(int k = 0; k < 4; k++) {  //top-left, top-right, bottom-left, bottom-right
        float x = k & 1 ? x1 : x0, y = k & 2 ? y1 : y0;
        q[k] = (UvVertex){x / 512, y / 512, (x - 240) / 240, (136 - y) / 136, 0};
      }
    }
  }
  int type = UvVertexType | (t->sprites ? GU_TRANSFORM_2D : GU_TRANSFORM_3D);
  for(int layer = 0; layer < layers; layer++) {
    if(t->rendered) {
      sceGuDisable(GU_TEXTURE_2D);
      sceGuDrawBuffer(GU_PSM_8888, (void*)RenderedAt, Stride);
      timedSprite(0, 0, 480, 272, 0, 0, 0, 0, 0xff808080);
      sceGuDrawBuffer(t->target, (void*)Target, Stride);
      sceGuEnable(GU_TEXTURE_2D);
      sceGuTexImage(0, 512, 512, 512, t->rendered == 1 ? (void*)(0x04000000 + RenderedAt) : VRAM_TEXTURE);
      sceGuTexFlush();
    }
    for(int tile = 0; tile < count; tile++) {
      for(int pass = 0; pass < t->passes; pass++) {
        if(t->loads && pass > 0) sceGuClutLoad(32, palette);
        if(t->passes > 1) {
          sceGuClutMode(GU_PSM_8888, 8 * pass, 0xff, 0);
          if(pass == 0) sceGuDisable(GU_BLEND);
          if(pass == 1) sceGuEnable(GU_BLEND), sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0x808080, 0x808080);
        }
        sceGuDrawArray(t->sprites ? GU_SPRITES : GU_TRIANGLE_STRIP, type, each, 0, v + tile * each);
      }
    }
  }
}

//-- wide triangles into 16-bit (time-widetri): Activision Hits Remixed's copyright screen's second list, 24% short in
//#269's refit: into 5650, twice a palette load (256 colors, 8888), a TEX_FLUSH and one 3D strip of two triangles
//(24-byte vertices) over the whole 480x272, reading a CLUT8 512x256 texture (RAM, swizzled), bilinear, modulated,
//blended (source alpha, 1 - source alpha) with the alpha test on. The PSP takes about what one sprite of it takes
//(round 8's time-scenes 2); the refit reads only 72% of a triangle's pixels wide, by round 9's rows. A layer: one
//pass. Cases 0-11: that pass; into 8888; nearest; not blended, no alpha test; as two triangles in through mode; as one
//sprite; round 8's time-scenes 2 (its list as it was: a clear, then one sprite, into 5650); 480 texels across (1:1)
//instead of 512; an 8888 512x256 texture (RAM, swizzled, its alpha 0xff); round 9's time-tristrips 34 (two triangles
//512 wide over 512x256, through mode, nearest, replacing) into 5650, and as round 9 drew it (into 8888); the pass's
//triangles split from top left to bottom right (the game's strip, top-left, top-right, bottom-left, bottom-right,
//splits from bottom left to top right). 12-17: round 9's case 34 into 5551 and 4444, and as one sprite 512 wide into
//8888, 5650, 5551 and 4444.
typedef struct { short psm, target, linear, blend, how, across, diagonal, round9; } WideCase;
enum { WideStrip, WideThrough, WideSprite, WideScene, WideRound9, WideRound9Sprite, WideRound9Itself };
static const WideCase wides[] = {
  {GU_PSM_T8, GU_PSM_5650, 1, 1, WideStrip, 512}, {GU_PSM_T8, GU_PSM_8888, 1, 1, WideStrip, 512},
  {GU_PSM_T8, GU_PSM_5650, 0, 1, WideStrip, 512}, {GU_PSM_T8, GU_PSM_5650, 1, 0, WideStrip, 512},
  {GU_PSM_T8, GU_PSM_5650, 1, 1, WideThrough, 512}, {GU_PSM_T8, GU_PSM_5650, 1, 1, WideSprite, 512},
  {GU_PSM_T8, GU_PSM_5650, 0, 0, WideScene}, {GU_PSM_T8, GU_PSM_5650, 1, 1, WideStrip, 480},
  {GU_PSM_8888, GU_PSM_5650, 1, 1, WideStrip, 512}, {GU_PSM_T8, GU_PSM_5650, 0, 0, WideRound9},
  {GU_PSM_T8, GU_PSM_8888, 0, 0, WideRound9Itself}, {GU_PSM_T8, GU_PSM_5650, 1, 1, WideStrip, 512, 1},
  {GU_PSM_T8, GU_PSM_5551, 0, 0, WideRound9}, {GU_PSM_T8, GU_PSM_4444, 0, 0, WideRound9},
  {GU_PSM_T8, GU_PSM_8888, 0, 0, WideRound9Sprite}, {GU_PSM_T8, GU_PSM_5650, 0, 0, WideRound9Sprite},
  {GU_PSM_T8, GU_PSM_5551, 0, 0, WideRound9Sprite}, {GU_PSM_T8, GU_PSM_4444, 0, 0, WideRound9Sprite},
};
static const TimedCase wideCases[] = {
  {"the game's pass: a 3D strip of two triangles over 480x272, CLUT8 512x256 (RAM, swizzled), bilinear, modulated, "
   "blended, the alpha test, into 5650", "layers", 2, {1, 2}},
  {"the same into 8888", "layers", 2, {1, 2}},
  {"the same into 5650, nearest", "layers", 2, {1, 2}},
  {"the same, bilinear, not blended, no alpha test", "layers", 2, {1, 2}},
  {"the game's pass as two triangles in through mode", "layers", 2, {1, 2}},
  {"the game's pass as one sprite", "layers", 2, {1, 2}},
  {"round 8's time-scenes 2: a clear, then the texture over 480x256 in one sprite, into 5650", "lists", 2, {1, 2}},
  {"the game's pass, 480 texels across (1:1)", "layers", 2, {1, 2}},
  {"the game's pass, an 8888 512x256 texture (RAM, swizzled)", "layers", 2, {1, 2}},
  {"round 9's time-tristrips 34 (512 wide as two triangles over 512x256, through mode, nearest) into 5650", "layers",
   2, {1, 2}},
  {"round 9's time-tristrips 34 as it was (into 8888)", "layers", 2, {1, 2}},
  {"the game's pass, its triangles split from top left to bottom right", "layers", 2, {1, 2}},
  {"round 9's time-tristrips 34 into 5551", "layers", 2, {1, 2}},
  {"round 9's time-tristrips 34 into 4444", "layers", 2, {1, 2}},
  {"the same texture as one sprite 512 wide over 512x256 (round 8's time-strips 48), into 8888", "layers", 2, {1, 2}},
  {"the same into 5650", "layers", 2, {1, 2}},
  {"the same into 5551", "layers", 2, {1, 2}},
  {"the same into 4444", "layers", 2, {1, 2}},
};
_Static_assert(sizeof(wides) / sizeof(wides[0]) == sizeof(wideCases) / sizeof(wideCases[0]), "a line for each case");
static void buildWide(int c, int layers) {
  const WideCase* t = &wides[c];
  if(t->how == WideScene) {
    buildScenes(2, layers);
    return;
  }
  if(t->how == WideRound9Itself) {
    buildTriStrips(34, layers);
    return;
  }
  if(t->psm == GU_PSM_8888) {
    for(int n = 0; n < 512 * 256; n++) big[n] |= 0xff000000u;  //(texels the alpha test passes)
    sceKernelDcacheWritebackAll();
  }
  timedList(t->target);
  if(t->how == WideRound9 || t->how == WideRound9Sprite) {
    texture8(t->psm, 1, 512, 256, big, 0);
    if(t->how == WideRound9) rectangles10(512, 256, 512, 256, 512, 0, Flat, layers);
    else for(int layer = 0; layer < layers; layer++) spriteStrips(0, 0, 512, 256, 512, 0, 0, 1);
    sceGuFinish();
    return;
  }
  if(t->how == WideStrip) setUp3D(&identity, 1), sceGuDisable(GU_DEPTH_TEST);
  screen480();
  texture8(t->psm, 1, 512, 256, big, t->linear);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  if(t->blend) {
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
  }
  int count = t->how == WideStrip ? 4 : t->how == WideThrough ? 6 : 2;
  FloatVertex* v = sceGuGetMemory(count * sizeof(FloatVertex));
  static const unsigned char orders[3][6] = {{0, 1, 2, 3}, {2, 0, 3, 1}, {0, 1, 2, 1, 3, 2}};
  for(int n = 0; n < count; n++) {
    int corner = t->how == WideSprite ? n * 3 : orders[t->how == WideThrough ? 2 : t->diagonal][n];
    int right = corner & 1, bottom = corner >> 1;
    if(t->how == WideStrip) {
      v[n] = (FloatVertex){right * t->across / 512.0f, bottom, 0xffffffff, right * 2 - 1, 1 - bottom * 2, 0};
    } else {
      v[n] = (FloatVertex){right * t->across, bottom * 256, 0xffffffff, right * 480, bottom * 272, 0};
    }
  }
  for(int layer = 0; layer < layers; layer++) {
    if(t->psm != GU_PSM_8888) sceGuClutLoad(32, palette);
    sceGuTexFlush();
    sceGuDrawArray(t->how == WideStrip ? GU_TRIANGLE_STRIP : t->how == WideThrough ? GU_TRIANGLES : GU_SPRITES,
                   t->how == WideStrip ? FloatVertexType3D : FloatVertexType, count, 0, v);
  }
  sceGuDisable(GU_BLEND);
  sceGuDisable(GU_ALPHA_TEST);
  sceGuFinish();
}

//-- triangles in perspective and turned a quarter (time-perspective): round 8's time-textures 13 to 15 and round 6's
//time-fill 39 drew textures as two triangles under the lens (w 1 at the left to 2 or 4 at the right) in about half
//the time #269's refit gives the same at w 1, and its time-textures 16 to 18 turned a texture a quarter (u down the
//screen) at 39 to 53% under the refit's time; none had the same drawn flat beside it. Cases 0-23: two triangles over
//the target's top-left 256x256 under the lens (rectangles10, wide3DLens), 256x256 texels of a texture (RAM 8888
//256x256 swizzled, VRAM 8888 512x512 as round 8's case 15, RAM CLUT8 256x256 swizzled), each corner at the distance
//the line says; squares 64 and 128 pixels wide over the 256x256, flat and w 1 to 2; w 2, and 2 to 1. 24-47: in
//through mode over the top-left 256x256, texel for pixel, squares 32 to 256 pixels wide of two triangles, as they are
//and each turned within itself, from RAM 8888 256x256 swizzled and not, and VRAM 8888 512x512.
typedef struct { short texture, w, square, turned; } PerspectiveCase;
#define W_RIGHTS(texture)                                                                                            \
  {texture, W1, 256}, {texture, W1To1_25, 256}, {texture, W1To1_5, 256}, {texture, W1To2, 256},                      \
  {texture, W1To4, 256}, {texture, W1To2Down, 256}
#define TURNS(texture)                                                                                               \
  {texture, Flat, 32}, {texture, Flat, 32, 1}, {texture, Flat, 64}, {texture, Flat, 64, 1}, {texture, Flat, 128},   \
  {texture, Flat, 128, 1}, {texture, Flat, 256}, {texture, Flat, 256, 1}
static const PerspectiveCase perspectives[] = {
  W_RIGHTS(0), W_RIGHTS(1), W_RIGHTS(2),
  {0, W1, 64}, {0, W1To2, 64}, {0, W1, 128}, {0, W1To2, 128}, {0, W2, 256}, {0, W2To1, 256},
  TURNS(0), TURNS(3), TURNS(1),
};
#define W_NAMES(texture)                                                                                             \
  {"3D: " texture ", two triangles over 256x256 pixels, w 1", "layers", 2, {1, 2}},                                   \
  {"the same, w 1 at the left to 1.25 at the right", "layers", 2, {1, 2}},                                            \
  {"the same, w 1 to 1.5", "layers", 2, {1, 2}}, {"the same, w 1 to 2", "layers", 2, {1, 2}},                         \
  {"the same, w 1 to 4", "layers", 2, {1, 2}},                                                                        \
  {"the same, w 1 at the top to 2 at the bottom", "layers", 2, {1, 2}}
#define TURN_NAMES(texture)                                                                                          \
  {"through mode: " texture ", squares 32x32 of two triangles over 256x256 pixels", "layers", 2, {1, 2}},             \
  {"the same, each turned a quarter (u down the screen)", "layers", 2, {1, 2}},                                       \
  {"the same, squares 64x64", "layers", 2, {1, 2}}, {"the same turned", "layers", 2, {1, 2}},                         \
  {"the same, squares 128x128", "layers", 2, {1, 2}}, {"the same turned", "layers", 2, {1, 2}},                       \
  {"the same, two triangles 256x256", "layers", 2, {1, 2}}, {"the same turned", "layers", 2, {1, 2}}
static const TimedCase perspectiveCases[] = {
  W_NAMES("RAM 8888 256x256 swizzled"), W_NAMES("VRAM 8888 512x512, its top-left 256x256"),
  W_NAMES("RAM CLUT8 256x256 swizzled"),
  {"3D: RAM 8888 256x256 swizzled, squares 64x64 of two triangles over 256x256 pixels, w 1", "layers", 2, {1, 2}},
  {"the same, w 1 at the left to 2 at the right", "layers", 2, {1, 2}},
  {"the same, squares 128x128, w 1", "layers", 2, {1, 2}},
  {"the same, w 1 at the left to 2 at the right", "layers", 2, {1, 2}},
  {"3D: RAM 8888 256x256 swizzled, two triangles over 256x256 pixels, w 2", "layers", 2, {1, 2}},
  {"the same, w 2 at the left to 1 at the right", "layers", 2, {1, 2}},
  TURN_NAMES("RAM 8888 256x256 swizzled"), TURN_NAMES("RAM 8888 256x256 not swizzled"),
  TURN_NAMES("VRAM 8888 512x512"),
};
_Static_assert(sizeof(perspectives) / sizeof(perspectives[0]) == sizeof(perspectiveCases) / sizeof(perspectiveCases[0]),
               "a line for each case");
static void buildPerspective(int c, int layers) {
  static const BigTexture textures[4] = {
    {GU_PSM_8888, 1, 0, 256}, {GU_PSM_8888, 0, 1, 512}, {GU_PSM_T8, 1, 0, 256}, {GU_PSM_8888, 0, 0, 256},
  };
  const PerspectiveCase* p = &perspectives[c];
  const BigTexture* t = &textures[p->texture];
  timedList(GU_PSM_8888);
  if(p->w != Flat) wide3DLens();
  texture8(t->psm, t->swizzled, t->height, t->height, t->vram ? VRAM_TEXTURE : (void*)big, 0);
  rectangles10(256, 256, p->square, p->square, t->height, p->turned, p->w, layers);
  sceGuFinish();
}

//-- mipmaps (time-mipmaps): round 8's time-textures 9 to 12 shrank a 256x256 RAM texture (8888, swizzled) 2:1 onto
//128x128 pixels in 3D without and with its levels (128 and 64), 75 to 80% under #269's refit's time with them; and
//Super Monkey Ball's small textures have 6 or 7 levels, trilinear. Cases 0-6: round 8's 9 to 12 as they were; level 1
//alone, a texture of its own at its place (128x128, 1:1); the level chosen, bilinear within it; nearest, the two
//nearest levels blended. 7-13: the same for CLUT8 (RAM, swizzled; level 0 64 KiB, its levels after it): its first
//level alone; the level chosen; level 1 set; trilinear; level 1 alone; bilinear within the level; levels blended.
//14-21: the game's small strips: 3D strips of 6 vertices (4 triangles of 15 pixels over 11x5.5, UvVertex) tiling the
//target's 512x256, each over the whole texture, the depth test passing and writing, into 8888; CLUT4 64x64 with 7
//levels and CLUT4 64x32 with 6 (RAM, swizzled; levels from 64 texels wide down to 1, rows of 16 bytes at least),
//trilinear with the level chosen less 2.625 (the game's TEX_LEVEL 0xd6), then nearest within the level chosen,
//bilinear without levels, nearest without levels.
typedef struct { short psm, levels, how, filter, height; } MipCase;  //(height: the small textures', 0 for the rest)
enum { LevelNone, LevelAuto, LevelConst, Level1Alone };
static const MipCase mipmaps10[] = {
  {GU_PSM_8888, 3, LevelAuto, GU_LINEAR_MIPMAP_NEAREST}, {GU_PSM_8888, 3, LevelAuto, GU_NEAREST_MIPMAP_LINEAR},
  {GU_PSM_8888, 3, Level1Alone, GU_NEAREST},
  {GU_PSM_T8, 3, LevelNone, GU_NEAREST}, {GU_PSM_T8, 3, LevelAuto, GU_NEAREST_MIPMAP_NEAREST},
  {GU_PSM_T8, 3, LevelConst, GU_NEAREST_MIPMAP_NEAREST}, {GU_PSM_T8, 3, LevelAuto, GU_LINEAR_MIPMAP_LINEAR},
  {GU_PSM_T8, 3, Level1Alone, GU_NEAREST}, {GU_PSM_T8, 3, LevelAuto, GU_LINEAR_MIPMAP_NEAREST},
  {GU_PSM_T8, 3, LevelAuto, GU_NEAREST_MIPMAP_LINEAR},
  {GU_PSM_T4, 7, LevelAuto, GU_LINEAR_MIPMAP_LINEAR, 64}, {GU_PSM_T4, 7, LevelAuto, GU_NEAREST_MIPMAP_NEAREST, 64},
  {GU_PSM_T4, 7, LevelNone, GU_LINEAR, 64}, {GU_PSM_T4, 7, LevelNone, GU_NEAREST, 64},
  {GU_PSM_T4, 6, LevelAuto, GU_LINEAR_MIPMAP_LINEAR, 32}, {GU_PSM_T4, 6, LevelAuto, GU_NEAREST_MIPMAP_NEAREST, 32},
  {GU_PSM_T4, 6, LevelNone, GU_LINEAR, 32}, {GU_PSM_T4, 6, LevelNone, GU_NEAREST, 32},
};
#define SMALL_MIPS(texture)                                                                                          \
  {"the game's small strips: " texture ", trilinear, the level chosen less 2.625", "triangles", 2, {4096, 16384}},    \
  {"the same, nearest within the level chosen", "triangles", 2, {4096, 16384}},                                      \
  {"the same, bilinear without levels", "triangles", 2, {4096, 16384}},                                               \
  {"the same, nearest without levels", "triangles", 2, {4096, 16384}}
static const TimedCase mipCases[] = {
  {"round 8's time-textures 9: 3D: RAM 8888 256x256 swizzled shrunk 2:1 onto 128x128 pixels, no mipmaps", "layers", 2,
   {1, 2}},
  {"round 8's time-textures 10: the same with 3 levels (256, 128, 64), the level chosen, nearest", "layers", 2, {1, 2}},
  {"round 8's time-textures 11: the same, level 1 set", "layers", 2, {1, 2}},
  {"round 8's time-textures 12: the same, the level chosen, linear between levels", "layers", 2, {1, 2}},
  {"the same, the level chosen, bilinear within it", "layers", 2, {1, 2}},
  {"the same, the level chosen, nearest, the two nearest levels blended", "layers", 2, {1, 2}},
  {"level 1 alone (128x128 at its place, a texture of its own) over 128x128 pixels, 1:1", "layers", 2, {1, 2}},
  {"3D: RAM CLUT8 256x256 swizzled shrunk 2:1 onto 128x128 pixels, no mipmaps", "layers", 2, {1, 2}},
  {"the same with its levels, the level chosen, nearest", "layers", 2, {1, 2}},
  {"the same, level 1 set", "layers", 2, {1, 2}},
  {"the same, the level chosen, trilinear", "layers", 2, {1, 2}},
  {"level 1 alone (128x128 at its place) over 128x128 pixels, 1:1", "layers", 2, {1, 2}},
  {"the same 256x256 with its levels, the level chosen, bilinear within it", "layers", 2, {1, 2}},
  {"the same, the level chosen, nearest, the two nearest levels blended", "layers", 2, {1, 2}},
  SMALL_MIPS("CLUT4 64x64, 7 levels"), SMALL_MIPS("CLUT4 64x32, 6 levels"),
};
_Static_assert(4 + sizeof(mipmaps10) / sizeof(mipmaps10[0]) == sizeof(mipCases) / sizeof(mipCases[0]),
               "a line for each case");
//A texture's levels, each after the last in RAM from `at`: `count` levels from `width` x `height` (halving each way
//to 1), swizzled or not, each row of 16 bytes at least and each level at least 8 rows (a swizzled block's); `bits` a
//texel. Sets them with the texture's mode and the level `how` says.
static void levels10(int psm, int bits, int width, int height, int count, unsigned char* at, int how, float bias) {
  sceGuTexMode(psm, how == LevelNone || how == Level1Alone ? 0 : count - 1, 0, 1);
  for(int k = 0; k < count; k++) {
    int w = width >> k ? width >> k : 1, h = height >> k ? height >> k : 1, buffer = w * bits < 128 ? 128 / bits : w;
    if(how == Level1Alone) {
      if(k == 1) sceGuTexImage(0, w, h, buffer, at);
    } else if(k == 0 || (how != LevelNone)) {
      sceGuTexImage(k, w, h, buffer, at);
    }
    at += buffer * (h < 8 ? 8 : h) * bits / 8;
  }
  sceGuTexLevelMode(how == LevelConst ? GU_TEXTURE_CONST : GU_TEXTURE_AUTO,
                    how == LevelAuto || how == LevelConst ? bias : 0);
}
//The small strips (time-mipmaps 14 to 21, time-contention's list H): 3D strips of 6 vertices (4 triangles of 15 pixels
//over 11x5.5, UvVertex) tiling the target's 512x256, each over the whole texture, the depth test passing and writing,
//into 8888, the CLUT4 texture `m` says; textured or not, the vertices in big or in VRAM's last 256 KiB (written
//through the uncached mirror), as asked (round 10c's lists).
static void smallStrips10(const MipCase* m, int size, int textured, int vram) {
  int strips = size / 4, height = m->height, mipmapped = m->how == LevelAuto;
  UvVertex* v = vram ? (UvVertex*)0x441c0000 : (UvVertex*)(big + 262144);  //(past the textures, in big's second MiB)
  for(int s = 0; s < strips; s++) {
    float x = s % 46 * 11, y = s / 46 % 46 * 5.5f;
    for(int k = 0; k < 6; k++) {
      float px = x + (k >> 1) * 5.5f, py = y + (k & 1) * 5.5f;
      v[s * 6 + k] = (UvVertex){(k >> 1) / 2.0f, k & 1, (px - 256) / 256, (128 - py) / 128, 0};
    }
  }
  fillDepth(0);
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  setUp3D(&identity, 1);
  sceGuOffset(2048 - 256, 2048 - 128);
  sceGuViewport(2048, 2048, 512, 256);
  sceGuDepthFunc(GU_GEQUAL);
  if(textured) {
    texture8(GU_PSM_T4, 1, 64, height, big, 0);
    levels10(GU_PSM_T4, 4, 64, height, m->levels, (unsigned char*)big, m->how, -2.625f);
    sceGuTexFilter(m->filter, mipmapped ? GU_LINEAR : m->filter);
  }
  UvVertex* drawn = vram ? (UvVertex*)0x041c0000 : v;
  for(int s = 0; s < strips; s++) {
    sceGuDrawArray(GU_TRIANGLE_STRIP, UvVertexType | GU_TRANSFORM_3D, 6, 0, drawn + s * 6);
  }
  sceGuFinish();
}
static void buildMipmaps(int c, int size) {
  if(c < 4) {
    buildTextures(9 + c, size);
    return;
  }
  const MipCase* m = &mipmaps10[c - 4];
  if(m->height) {  //the small CLUT4 textures, 64 texels wide
    smallStrips10(m, size, 1, 0);
    return;
  }
  FloatVertex* v = (FloatVertex*)(big + 393216);
  int side = m->how == Level1Alone ? 128 : 256;
  for(int n = 0; n < 6; n++) {  //(round 8's: two triangles over the top-left 128x128, identity matrices)
    int corner = (int[6]){0, 1, 2, 1, 3, 2}[n], right = corner & 1, bottom = corner >> 1;
    v[n] = (FloatVertex){right, bottom, 0xffffffff, ndcX(right * 128), ndcY(bottom * 128), 0};
  }
  sceKernelDcacheWritebackAll();
  timedList(GU_PSM_8888);
  setUp3D(&identity, 1);
  sceGuDisable(GU_DEPTH_TEST);
  int bits = m->psm == GU_PSM_8888 ? 32 : 8;
  texture8(m->psm, 1, side, side, big, 0);
  levels10(m->psm, bits, 256, 256, m->levels, (unsigned char*)big, m->how, m->how == LevelConst ? 1 : 0);
  sceGuTexFilter(m->filter, GU_NEAREST);
  for(int layer = 0; layer < size; layer++) sceGuDrawArray(GU_TRIANGLES, FloatVertexType3D, 6, 0, v);
  sceGuFinish();
}

//-- narrow strips (time-vram8): round 8's time-strips 21 (a VRAM CLUT8 512x512 texture, not swizzled, in sprites 8
//wide) and round 9's time-tristrips 35 (the same swizzled as triangles) took twice #269's refit's time (-50%). Strips
//4, 8, 12, 16 and 24 texels wide over the target's 512x256, 1:1, as sprites and as triangles (rectangles10), CLUT8
//and CLUT4 (16- and 32-texel fetches?), from VRAM's first megabyte not swizzled and swizzled, and from RAM swizzled.
static const TimedCase narrowCases[] = {
#define NARROW_WIDTHS(texture, how)                                                                                  \
  {texture ", strips 4 wide as " how, "layers", 2, {1, 2}}, {texture ", strips 8 wide as " how, "layers", 2, {1, 2}}, \
  {texture ", strips 12 wide as " how, "layers", 2, {1, 2}},                                                         \
  {texture ", strips 16 wide as " how, "layers", 2, {1, 2}},                                                         \
  {texture ", strips 24 wide as " how, "layers", 2, {1, 2}}
#define NARROW_PLACE(place)                                                                                          \
  NARROW_WIDTHS(place ", CLUT8 512x512", "sprites"), NARROW_WIDTHS(place ", CLUT8 512x512", "triangles"),           \
  NARROW_WIDTHS(place ", CLUT4 512x512", "sprites"), NARROW_WIDTHS(place ", CLUT4 512x512", "triangles")
  NARROW_PLACE("VRAM, not swizzled"), NARROW_PLACE("VRAM, swizzled"), NARROW_PLACE("RAM, swizzled"),
#undef NARROW_PLACE
#undef NARROW_WIDTHS
};
static void buildNarrow(int c, int layers) {
  static const short widths[5] = {4, 8, 12, 16, 24};
  int place = c / 20, psm = c / 10 % 2 ? GU_PSM_T4 : GU_PSM_T8, triangles = c / 5 % 2, width = widths[c % 5];
  timedList(GU_PSM_8888);
  texture8(psm, place > 0, 512, 512, place < 2 ? VRAM_TEXTURE : (void*)big, 0);
  if(triangles) rectangles10(512, 256, width, 256, 512, 0, Flat, layers);
  else for(int layer = 0; layer < layers; layer++) spriteStrips(0, 0, 512, 256, width, 0, 0, 1);
  sceGuFinish();
}

//-- the CPU beside the GE (time-contention, time-contention2): #269's refit leaves Super Monkey Ball's story 30%
//short (its copy, round 8's time-scenes 5, 9 to 13% with the CPU idle), Killzone's and Ghost of Sparta's later scenes
//14 to 34%: the PSP's CPU drawing on memory while the GE draws. Round 8's cpu-contention had one pattern (a thread
//reading RAM) against two lists. A grid: each GE list (two sizes) while a thread runs one of 17 patterns, and each
//pattern alone for 10 ms. The thread runs at a higher priority than this one, started before the list is queued but
//waiting on contendGo till it is, then runs its pattern until the finish callback's flag (or the 10 ms alone, or
//Patience: it never holds the PSP longer), counting its turns; this thread waits in the meantime, then for the GE as
//runTimed does. Each run's four words: the GE's time to its finish callback and sceGeListEnQueue's return (alone: the
//thread's 10 ms and 0), the thread's turns and their time in microseconds (0 and 0 without it).
//The patterns (contendRegion, 1 MiB the GE doesn't read; FREE_VRAM_CPU, VRAM's last 256 KiB, which only time-clut32's
//filled buffer touches, at another time): arithmetic alone (64 adds a turn); a line's 16 words read from 8 KiB (the
//data cache's hits); a line's 16 words read streaming over 1 MiB (a miss each), flat out and slowed by an empty loop
//between lines to a half, a quarter and an eighth of that (the loop's turns measured as each test's clocks are set,
//contendSlow); a line's 16 words written streaming over 1 MiB (write backs), flat out and a quarter; 64 KiB copied
//(memcpy) a turn over it; a step of a chain of pointers 64 bytes apart over it (a miss at a time); 16 words read
//through the uncached mirror; a line's 16 words read and written in VRAM; a game's mix, 12 additions and 2 reads and 2
//writes a turn (a quarter of its instructions) at random words of 16, 64 and 256 KiB and 1 MiB.
//The GE's lists: A an 8888 512x512 texture (RAM, swizzled) in one sprite 512 wide over 512x128 a layer (wide reads);
//B the same in VRAM; C 512x256 without a texture; D a strip of culled triangles, 24-byte vertices (16-bit texture
//coordinates and normals, float positions) from RAM; E1 NOPs; E2 Super Monkey Ball's CALLed lists (round 8's
//time-scenes 5); F 480x272 32-bit copied RAM to VRAM; G a new CLUT8 32x32 texture before each sprite (round 9's
//time-loads 3); H the game's small strips (time-mipmaps 14: CLUT4 64x64 trilinear, depth tested); I Ghost of Sparta's
//kind: strips of 10 vertices (36 bytes: 3 float weights, 16-bit texture coordinates and normals, float positions)
//skinned by 3 bones, lit by 3 directional lights, over CLUT4 and CLUT8 64x64 and 128x128 textures (RAM, swizzled) in
//turn, a new one with its palette every 45 strips, bilinear, depth tested, into 8888.
//time-contention2: the streaming reads (flat out and slowed) at 333/222/166 MHz, the bus apart from the CPU.
static unsigned int __attribute__((aligned(1024))) contendRegion[262144];
#define FREE_VRAM_CPU ((volatile unsigned int*)0x041c0000)  //(VRAM's last 256 KiB, through the CPU's cache)
enum {
  NoThread, Arithmetic, CachedReads, Streaming, Streaming2, Streaming4, Streaming8, Writing, Writing4, Copying,
  Chasing, Uncached, VramReads, VramWrites, Mix16, Mix64, Mix256, Mix1024,
  PollSema, PollListSync, PollDrawSync, WritesBefore, WritesSettled, WritesBesideOnce, Patterns  //(round 10b's)
};
static SceUID contendGo = -1, contendNever = -1;  //(contendNever: a semaphore nothing signals, for PollSema)
static int contendList;                           //the list PollListSync looks at
static volatile int contendQuit;
static volatile unsigned int contendTurns, contendBegan, contendEnded;
static unsigned int contendLimit, contendSlow[Patterns];
static int contendPattern;
//Reads (or writes) the 16 words of the line at `at`; the sum of what it read.
static unsigned int line10(const volatile unsigned int* at) {
  return at[0] + at[1] + at[2] + at[3] + at[4] + at[5] + at[6] + at[7] + at[8] + at[9] + at[10] + at[11] + at[12] +
         at[13] + at[14] + at[15];
}
static void writeLine10(volatile unsigned int* at, unsigned int value) {
  at[0] = value, at[1] = value, at[2] = value, at[3] = value, at[4] = value, at[5] = value, at[6] = value;
  at[7] = value, at[8] = value, at[9] = value, at[10] = value, at[11] = value, at[12] = value, at[13] = value;
  at[14] = value, at[15] = value;
}
static void idle10(unsigned int turns) {
  for(unsigned int k = turns; k > 0; k--) __asm__ volatile("");
}
//One turn of the pattern, turn `n`; what it read.
static unsigned int contendTurn(int pattern, unsigned int n, unsigned int* random) {
  volatile unsigned int* region = contendRegion;
  switch(pattern) {
  case Arithmetic: {
    unsigned int x = n, one = 1;
    __asm__ volatile(".set push\n.set noreorder\n.rept 64\naddu %[x], %[x], %[one]\n.endr\n.set pop\n"
                     : [x] "+r"(x) : [one] "r"(one));
    return x;
  }
  case CachedReads: return line10(region + (n & 127) * 16);
  case Streaming: case Streaming2: case Streaming4: case Streaming8: {
    unsigned int sum = line10(region + (n & 16383) * 16);
    idle10(contendSlow[pattern]);
    return sum;
  }
  case Writing: case Writing4:
    writeLine10(region + (n & 16383) * 16, n);
    idle10(contendSlow[pattern]);
    return 0;
  case Copying:
    memcpy((unsigned char*)contendRegion + 524288 + (n & 7) * 65536, (unsigned char*)contendRegion + (n & 7) * 65536,
           65536);
    return 0;
  case Chasing: return *random = *(const unsigned int*)*random;
  case Uncached: return line10((volatile unsigned int*)((unsigned int)(region + (n & 16383) * 16) | 0x40000000));
  case VramReads: return line10((volatile unsigned int*)FREE_VRAM_CPU + (n & 4095) * 16);
  case VramWrites: writeLine10((volatile unsigned int*)FREE_VRAM_CPU + (n & 4095) * 16, n); return 0;
  case PollSema: sceKernelPollSema(contendNever, 1); return sceKernelGetSystemTimeLow();
  case PollListSync: sceGeListSync(contendList, 1); return sceKernelGetSystemTimeLow();
  case PollDrawSync: sceGeDrawSync(1); return sceKernelGetSystemTimeLow();
  case WritesBefore: case WritesSettled: case WritesBesideOnce: writeLine10(region + (n & 16383) * 16, n); return 0;
  default: {  //Mix16 to Mix1024: 12 additions, 2 reads and 2 writes at random words of the set
    unsigned int mask = (pattern == Mix16 ? 16384 : pattern == Mix64 ? 65536 : pattern == Mix256 ? 262144 : 1048576) / 4
                        - 1, r = *random, a, b, sum;
    r = r * 1664525 + 1013904223, a = r >> 8 & mask;
    r = r * 1664525 + 1013904223, b = r >> 8 & mask;
    sum = region[a] + region[b] + a + b + r;
    region[b] = sum, region[a] = r;
    *random = r + sum + n;
    return sum;
  }
  }
}
static int contendingThread(SceSize length, void* argument) {
  (void)length, (void)argument;
  SceUInt timeout = Patience;
  if(sceKernelWaitSema(contendGo, 1, &timeout) < 0 || contendQuit) return 0;
  unsigned int began = sceKernelGetSystemTimeLow(), now = began, n = 0, sum = 0, random = (unsigned int)contendRegion;
  int pattern = contendPattern;
  unsigned int mask = pattern == Copying || pattern >= PollSema ? 0 : pattern == Uncached ? 3
                      : pattern >= Streaming && pattern <= Writing4 ? 7 : 63;
  contendBegan = began;
  while(pattern == WritesBesideOnce ? n < 16384 : !finished) {  //(WritesBesideOnce: 1 MiB once, whatever the GE)
    sum += contendTurn(pattern, n++, &random);
    if(!(n & mask)) {
      now = sceKernelGetSystemTimeLow();
      if(now - began >= contendLimit) break;
    }
  }
  contendEnded = sceKernelGetSystemTimeLow();
  contendTurns = n;
  readSum += sum;
  return 0;
}
//How many turns of the empty loop slow a pattern's turn to 1 / `share` of its time flat out, at the clocks as set.
static unsigned int slowFor(int pattern, int share) {
  unsigned int random = 0, began = sceKernelGetSystemTimeLow(), sum = 0;
  contendSlow[pattern] = 0;
  for(unsigned int n = 0; n < 4096; n++) sum += contendTurn(pattern, n, &random);
  unsigned int turn = sceKernelGetSystemTimeLow() - began;
  began = sceKernelGetSystemTimeLow();
  idle10(65536);
  unsigned int idle = sceKernelGetSystemTimeLow() - began;
  readSum += sum;
  return idle ? (unsigned int)((unsigned long long)(share - 1) * turn * 65536 / (4096ull * idle)) : 0;
}
typedef struct { short ge, pattern; } ContentionCase;
#define PATTERNS(ge)                                                                                                 \
  {ge, Arithmetic}, {ge, CachedReads}, {ge, Streaming}, {ge, Streaming2}, {ge, Streaming4}, {ge, Streaming8},        \
  {ge, Writing}, {ge, Writing4}, {ge, Copying}, {ge, Chasing}, {ge, Uncached}, {ge, VramReads}, {ge, VramWrites},    \
  {ge, Mix16}, {ge, Mix64}, {ge, Mix256}, {ge, Mix1024}
#define GE_ROW(ge) {ge, NoThread}, PATTERNS(ge)
static const ContentionCase contentions[] = {
  PATTERNS(0), GE_ROW(1), GE_ROW(2), GE_ROW(3), GE_ROW(4), GE_ROW(5), GE_ROW(6), GE_ROW(7), GE_ROW(8), GE_ROW(9),
  GE_ROW(10),
};
#define STREAMING(ge) {ge, Streaming}, {ge, Streaming2}, {ge, Streaming4}, {ge, Streaming8}
static const ContentionCase contentions2[] = {
  STREAMING(0), {1, NoThread}, STREAMING(1), {2, NoThread}, STREAMING(2), {3, NoThread}, STREAMING(3), {4, NoThread},
  STREAMING(4), {5, NoThread}, STREAMING(5), {6, NoThread}, STREAMING(6), {7, NoThread}, STREAMING(7), {8, NoThread},
  STREAMING(8), {9, NoThread}, STREAMING(9), {10, NoThread}, STREAMING(10),
};
#define PATTERN_NAMES(ge, unit, n, a, b)                                                                             \
  {ge "; the CPU: arithmetic alone", unit, n, {a, b}}, {ge "; the CPU: reads within 8 KiB", unit, n, {a, b}},        \
  {ge "; the CPU: streaming reads over 1 MiB", unit, n, {a, b}},                                                     \
  {ge "; the CPU: streaming reads, a half of flat out", unit, n, {a, b}},                                            \
  {ge "; the CPU: streaming reads, a quarter", unit, n, {a, b}},                                                     \
  {ge "; the CPU: streaming reads, an eighth", unit, n, {a, b}},                                                     \
  {ge "; the CPU: streaming writes over 1 MiB", unit, n, {a, b}},                                                    \
  {ge "; the CPU: streaming writes, a quarter", unit, n, {a, b}},                                                    \
  {ge "; the CPU: copies, 64 KiB at a time", unit, n, {a, b}},                                                       \
  {ge "; the CPU: a chain of pointers over 1 MiB", unit, n, {a, b}},                                                 \
  {ge "; the CPU: uncached reads", unit, n, {a, b}}, {ge "; the CPU: VRAM reads", unit, n, {a, b}},                  \
  {ge "; the CPU: VRAM writes", unit, n, {a, b}}, {ge "; the CPU: a mix over 16 KiB", unit, n, {a, b}},              \
  {ge "; the CPU: a mix over 64 KiB", unit, n, {a, b}}, {ge "; the CPU: a mix over 256 KiB", unit, n, {a, b}},       \
  {ge "; the CPU: a mix over 1 MiB", unit, n, {a, b}}
#define GE_NAMES(ge, unit, a, b) {ge "; the CPU idle", unit, 2, {a, b}}, PATTERN_NAMES(ge, unit, 2, a, b)
#define GE_LISTS(NAMES)                                                                                              \
  NAMES("A: RAM 8888 512x512 swizzled, one sprite 512 wide over 512x128", "layers", 1, 2),                          \
  NAMES("B: VRAM 8888 512x512, one sprite 512 wide over 512x128", "layers", 1, 2),                                  \
  NAMES("C: 512x256 without a texture", "layers", 8, 16),                                                           \
  NAMES("D: culled triangles, 24-byte vertices from RAM", "triangles", 32768, 65536),                               \
  NAMES("E1: NOPs", "commands", 65536, 131072), NAMES("E2: Super Monkey Ball's lists", "passes", 2, 4),              \
  NAMES("F: 480x272 32-bit copied RAM to VRAM", "copies", 2, 4),                                                    \
  NAMES("G: a new CLUT8 32x32 texture before each sprite", "sprites", 512, 1024),                                   \
  NAMES("H: the game's small strips (CLUT4 64x64, trilinear)", "triangles", 4096, 8192),                            \
  NAMES("I: Ghost of Sparta's kind (skinned, lit strips)", "strips", 1125, 2250)
static const TimedCase contentionCases10[] = {
  PATTERN_NAMES("the GE idle", "-", 1, 0, 0), GE_LISTS(GE_NAMES),
};
#define STREAMING_NAMES(ge, unit, a, b)                                                                              \
  {ge "; the CPU idle", unit, 2, {a, b}}, {ge "; the CPU: streaming reads over 1 MiB", unit, 2, {a, b}},             \
  {ge "; the CPU: streaming reads, a half of flat out", unit, 2, {a, b}},                                            \
  {ge "; the CPU: streaming reads, a quarter", unit, 2, {a, b}},                                                     \
  {ge "; the CPU: streaming reads, an eighth", unit, 2, {a, b}}
static const TimedCase contentionCases2[] = {
  {"the GE idle; the CPU: streaming reads over 1 MiB", "-", 1, {0}},
  {"the GE idle; the CPU: streaming reads, a half of flat out", "-", 1, {0}},
  {"the GE idle; the CPU: streaming reads, a quarter", "-", 1, {0}},
  {"the GE idle; the CPU: streaming reads, an eighth", "-", 1, {0}}, GE_LISTS(STREAMING_NAMES),
};
_Static_assert(sizeof(contentions) / sizeof(contentions[0]) ==
               sizeof(contentionCases10) / sizeof(contentionCases10[0]), "a line for each case");
_Static_assert(sizeof(contentions2) / sizeof(contentions2[0]) ==
               sizeof(contentionCases2) / sizeof(contentionCases2[0]), "a line for each case");
static const ContentionCase* contending;  //the test's table: contentions or contentions2
static unsigned int contendClocks;         //the clocks contendSlow was measured at
static void buildContention10(int c, int size) {
  const ContentionCase* k = &contending[c];
  unsigned int clocks = scePowerGetCpuClockFrequencyInt() << 16 | scePowerGetBusClockFrequencyInt();
  if(clocks != contendClocks) {
    contendClocks = clocks;
    contendSlow[Streaming2] = slowFor(Streaming, 2), contendSlow[Streaming4] = slowFor(Streaming, 4);
    contendSlow[Streaming8] = slowFor(Streaming, 8), contendSlow[Writing4] = slowFor(Writing, 4);
  }
  if(k->pattern == Chasing) cpuChain(contendRegion, 16384, 64);
  sceKernelDcacheWritebackInvalidateAll();
  switch(k->ge) {
  case 0: return;
  case 6: buildScenes(5, size); return;
  case 8: buildLoads(3, size); return;
  case 9: buildMipmaps(14, size); return;
  case 11: buildMipmaps(15, size); return;  //(round 10c's: H nearest within the level chosen)
  case 12: buildMipmaps(16, size); return;  //(bilinear without levels)
  case 13: buildMipmaps(17, size); return;  //(nearest without levels)
  case 14: smallStrips10(&mipmaps10[10], size, 0, 0); return;  //(untextured)
  case 15: smallStrips10(&mipmaps10[10], size, 1, 1); return;  //(its vertices in VRAM)
  case 16: smallStrips10(&mipmaps10[10], size, 0, 1); return;  //(untextured, its vertices in VRAM)
  }
  if(k->ge == 4 || k->ge == 10) {  //vertices into big
    static const Parts strip = {0, 0, 2, 0, 2, 4}, skinned = {3, 4, 2, 0, 2, 4};
    int vertices = k->ge == 4 ? size / 256 * 258 : size * 10;
    unsigned char* at = (unsigned char*)big;
    for(int n = 0; n < vertices; n++) {
      if(k->ge == 4) {
        at = putVertex(at, &strip, ndcX(n % 258 * 255.0f / 257), ndcY(n / 258 % 64 * 4 + (n & 1) * 2), 0);
      } else {  //strip n / 10 at a 36x8 cell of the target's 512x256, over and over
        int s = n / 10, i = n % 10;
        float x = s % 14 * 36 + i * 4, y = s / 14 % 32 * 8 + (i & 1) * 8;
        at = putVertex(at, &skinned, (x - 256) / 256, (128 - y) / 128, 0);
      }
    }
    if(k->ge == 10) fillDepth(0);
    sceKernelDcacheWritebackAll();
  }
  timedList(GU_PSM_8888);
  switch(k->ge) {
  case 1: case 2:
    texture8(GU_PSM_8888, k->ge == 1, 512, 512, k->ge == 1 ? (void*)big : VRAM_TEXTURE, 0);
    for(int layer = 0; layer < size; layer++) spriteStrips(0, 0, 512, 128, 512, 0, 0, 1);
    break;
  case 3:
    for(int layer = 0; layer < size; layer++) spriteStrips(0, 0, 512, 256, 512, 0, 0, 0);
    break;
  case 4:
    setUp3D(&identity, 1);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_CULL_FACE), sceGuFrontFace(GU_CW);
    for(int row = 0; row < size / 256; row++) {
      sceGuDrawArray(GU_TRIANGLE_STRIP, partsType(&(Parts){0, 0, 2, 0, 2, 4}) | GU_TRANSFORM_3D, 258, 0,
                     (unsigned char*)big + row * 258 * 24);
    }
    break;
  case 5:
    for(int n = 0; n < size; n++) sceGuSendCommandi(0, 0);  //(NOP)
    break;
  case 7:
    for(int copy = 0; copy < size; copy++) {
      sceGuCopyImage(GU_PSM_8888, 0, 0, 480, 272, 512, big, 0, 0, Stride, (void*)(0x04000000 + Target));
    }
    break;
  case 10: {
    setUp3D(&identity, 1);
    sceGuOffset(2048 - 256, 2048 - 128);
    sceGuViewport(2048, 2048, 512, 256);
    sceGuDepthFunc(GU_GEQUAL);
    lightsOn(GU_DIRECTIONAL, 3, GU_DIFFUSE);
    for(int bone = 0; bone < 3; bone++) sceGuBoneMatrix(bone, &identity);
    texture8(GU_PSM_T4, 1, 64, 64, bytes8, 1);
    int type = partsType(&(Parts){3, 4, 2, 0, 2, 4}) | GU_TRANSFORM_3D;
    for(int s = 0; s < size; s++) {
      if(s % 45 == 0) {  //a new texture and its palette: CLUT4 and CLUT8, 64x64 and 128x128, in turn
        int t = s / 45 % 4, psm = t & 1 ? GU_PSM_T8 : GU_PSM_T4, side = t & 2 ? 128 : 64;
        sceGuTexMode(psm, 0, 0, 1);
        sceGuTexImage(0, side, side, side, bytes8 + (s / 45 % 3) * 16384);
        sceGuClutLoad(psm == GU_PSM_T8 ? 32 : 2, palette);
      }
      sceGuDrawArray(GU_TRIANGLE_STRIP, type, 10, 0, (unsigned char*)big + s * 10 * 36);
    }
    break;
  }
  }
  sceGuFinish();
}
static int runContention10(int c, int size, const char* what, unsigned int* times) {
  (void)size;
  const ContentionCase* k = &contending[c];
  if(k->pattern == NoThread) {
    int ran = runTimed(what, times);
    times[2] = times[3] = 0;
    return ran;
  }
  if(k->pattern == WritesBefore || k->pattern == WritesSettled) {  //this thread's 1 MiB, then the list at once
    unsigned int began = sceKernelGetSystemTimeLow();
    for(unsigned int n = 0; n < 16384; n++) writeLine10(contendRegion + n * 16, n);
    if(k->pattern == WritesSettled) sceKernelDcacheWritebackAll();
    unsigned int wrote = sceKernelGetSystemTimeLow() - began;
    int ran = k->ge ? runTimed(what, times) : 1;
    if(!k->ge) times[0] = wrote, times[1] = 0;
    sceKernelDcacheWritebackInvalidateAll();
    times[2] = 16384, times[3] = wrote;
    return ran;
  }
  times[2] = times[3] = 0;
  if(contendGo < 0) contendGo = sceKernelCreateSema("contend", 0, 0, 1, 0);
  if(contendNever < 0) contendNever = sceKernelCreateSema("never", 0, 0, 1, 0);
  int priority = sceKernelGetThreadCurrentPriority() - 1;
  SceUID thread = contendGo < 0 ? -1 : sceKernelCreateThread("contending", contendingThread, priority, 0x2000, 0, 0);
  if(thread < 0) return k->ge ? runTimed(what, times) : 1;
  contendPattern = k->pattern, contendQuit = 0, contendTurns = contendBegan = contendEnded = 0;
  contendLimit = k->ge ? Patience : 10000;
  finished = 0;
  while(sceKernelPollSema(contendGo, 1) >= 0) continue;
  sceKernelStartThread(thread, 0, 0);  //(it waits on contendGo)
  int ran = 1;
  if(k->ge) {
    times[0] = times[1] = Unfinished;
    while(sceKernelPollSema(timingSemaphore, 1) >= 0) continue;
    finished = signalled = 0;
    unsigned int began = sceKernelGetSystemTimeLow();
    int queued = sceGeListEnQueue(TIMED, 0, timingCallbacks, 0);
    unsigned int after = sceKernelGetSystemTimeLow(), now = after;
    if(queued < 0) contendQuit = 1;
    contendList = queued;
    sceKernelSignalSema(contendGo, 1);  //(the thread runs from here till the list is done, then this one)
    if(queued < 0) {
      refused(queued, what);
      times[1] = queued;
      ran = 0;
    } else {
      while(!finished && now - began < Patience) {
        SceUInt timeout = 2000;
        sceKernelWaitSema(timingSemaphore, 1, &timeout);
        now = sceKernelGetSystemTimeLow();
      }
      int state = sceGeDrawSync(1);
      while(finished && state > 0 && now - began < Patience) {
        sceKernelDelayThread(50);
        state = sceGeDrawSync(1);
        now = sceKernelGetSystemTimeLow();
      }
      if(!finished || state > 0) {
        resetGe(now - began, state, what);
        ran = 0;
      } else {
        times[0] = finishedAt - began, times[1] = after - began;
      }
    }
  } else {
    unsigned int began = sceKernelGetSystemTimeLow();
    sceKernelSignalSema(contendGo, 1);
    times[0] = sceKernelGetSystemTimeLow() - began, times[1] = 0;
  }
  SceUInt timeout = 1000000;
  sceKernelWaitThreadEnd(thread, &timeout);
  sceKernelDeleteThread(thread);
  sceKernelDcacheWritebackInvalidateAll();  //(no line of contendRegion or VRAM left to land later)
  times[2] = contendTurns, times[3] = contendEnded - contendBegan;
  return ran;
}

static void writeManifest10(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest10.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 10 (tools/psp-measure/ge.c in Phobos says what each test draws and runs;\n"
    "docs/psp-core.md, part 112, what each is for). Records of words as round 8's (manifest8.txt): case, size, CPU\n"
    "MHz << 16 | bus MHz (as scePower gave them back), repeats | values << 16, then per run `values` words: times in\n"
    "microseconds from just before sceGeListEnQueue to the finish callback and to sceGeListEnQueue's return;\n"
    "time-contention and time-contention2 two more, a thread's turns of its pattern while the GE drew and their time\n"
    "(the GE idle: the first time is the thread's 10 ms); 0xffffffff for a run not finished (see <name>.stalled). All\n"
    "run at 222/111 and 333/166 MHz but time-contention2, at 333/222/166; the clocks as found are put back after "
    "each.\n"
    "time-anchors2's first six cases are round 8's, built the same way at the same sizes. The cases (test case:\n"
    "what; size):\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  static const struct { const char* test; const TimedCase* cases; int count; } tests[] = {
    {"time-anchors2", anchorCases10, sizeof(anchorCases10) / sizeof(anchorCases10[0])},
    {"time-lights", lightCases10, sizeof(lightCases10) / sizeof(lightCases10[0])},
    {"time-clut32", clut32Cases, sizeof(clut32Cases) / sizeof(clut32Cases[0])},
    {"time-widetri", wideCases, sizeof(wideCases) / sizeof(wideCases[0])},
    {"time-perspective", perspectiveCases, sizeof(perspectiveCases) / sizeof(perspectiveCases[0])},
    {"time-mipmaps", mipCases, sizeof(mipCases) / sizeof(mipCases[0])},
    {"time-vram8", narrowCases, sizeof(narrowCases) / sizeof(narrowCases[0])},
    {"time-contention", contentionCases10, sizeof(contentionCases10) / sizeof(contentionCases10[0])},
    {"time-contention2", contentionCases2, sizeof(contentionCases2) / sizeof(contentionCases2[0])},
  };
  char line[320];
  for(unsigned int t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
    for(int c = 0; c < tests[t].count; c++) {
      int length = snprintf(line, sizeof(line), "%s %d: %s; %s\n", tests[t].test, c, tests[t].cases[c].name,
                            tests[t].cases[c].unit);
      sceIoWrite(file, line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
    }
  }
  static const char end[] =
    "<name>.stalled: the GE hadn't finished the test's list after 5 s and was reset (manifest4.txt says more).\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, end, sizeof(end) - 1);
  sceIoClose(file);
}

static void round10(void) {
  foundCpu = scePowerGetCpuClockFrequencyInt();
  foundBus = scePowerGetBusClockFrequencyInt();
  foundPll = foundCpu > 2 * foundBus ? foundCpu : 2 * foundBus;  //(as round 6 reads them)
  writeManifest10();
  prepareTexels();
#define CASES(table) table, sizeof(table) / sizeof(table[0])
  timeCases8("time-anchors2", CASES(anchorCases10), 8, 2, AtTwo, buildAnchors10, 0);
  timeCases8("time-lights", CASES(lightCases10), 16, 2, AtTwo, buildLights10, 0);
  timeCases8("time-clut32", CASES(clut32Cases), 8, 2, AtTwo, buildClut32, 0);
  timeCases8("time-widetri", CASES(wideCases), 8, 2, AtTwo, buildWide, 0);
  timeCases8("time-perspective", CASES(perspectiveCases), 8, 2, AtTwo, buildPerspective, 0);
  timeCases8("time-mipmaps", CASES(mipCases), 8, 2, AtTwo, buildMipmaps, 0);
  timeCases8("time-vram8", CASES(narrowCases), 8, 2, AtTwo, buildNarrow, 0);
  contending = contentions, contendClocks = 0;
  timeCases8("time-contention", CASES(contentionCases10), 4, 4, AtTwo, buildContention10, runContention10);
  contending = contentions2, contendClocks = 0;
  timeCases8("time-contention2", CASES(contentionCases2), 4, 4, At333Cpu222, buildContention10, runContention10);
#undef CASES
}

//---- round 10b: a top-up to round 10, as #269's agent specified it for the refit (docs/psp-core.md, part 112): the CPU
//polling through system calls beside the GE, and its work before the list against beside it (time-contention3); and
//placement as a case of its own (time-placement). From round 10b on the arrays the GE reads are on 1 KiB boundaries
//(round 10's build had them 64 bytes off a 128-byte boundary, which made swizzled RAM textures 7% dearer).

//-- the CPU polling, before and beside (time-contention3): round 10's time-contention as it is (runContention10, its
//thread released after the enqueue), six of its lists (A, C, D, E2, H, I) at the same sizes, with round 10's
//spec's patterns 11 and 12, which came after round 10 was built: polling, a turn being sceKernelPollSema on a semaphore
//nothing signals, sceGeListSync(the list, 1) or sceGeDrawSync(1), and sceKernelGetSystemTimeLow (busy, but the cache's
//or the kernel's); 1 MiB written (16384 lines over contendRegion) by this thread before the list is queued at once,
//the same with sceKernelDcacheWritebackAll between (the dirty lines landed before the GE starts), and by the thread
//once beside the list, then stopping. Each pattern alone first (the polls for 10 ms, the writes once), then each list
//alone and beside each. A run's four words as time-contention's (the writes before: 16384 and their microseconds).
#define PATTERNS10B(ge)                                                                                              \
  {ge, PollSema}, {ge, PollListSync}, {ge, PollDrawSync}, {ge, WritesBefore}, {ge, WritesSettled},                   \
  {ge, WritesBesideOnce}
#define GE_ROW10B(ge) {ge, NoThread}, PATTERNS10B(ge)
static const ContentionCase contentions3[] = {
  PATTERNS10B(0), GE_ROW10B(1), GE_ROW10B(3), GE_ROW10B(4), GE_ROW10B(6), GE_ROW10B(9), GE_ROW10B(10),
};
#define PATTERN_NAMES10B(ge, unit, n, a, b)                                                                          \
  {ge "; the CPU polling: sceKernelPollSema and sceKernelGetSystemTimeLow a turn", unit, n, {a, b}},                  \
  {ge "; the CPU polling: sceGeListSync(the list, 1) and sceKernelGetSystemTimeLow a turn", unit, n, {a, b}},         \
  {ge "; the CPU polling: sceGeDrawSync(1) and sceKernelGetSystemTimeLow a turn", unit, n, {a, b}},                   \
  {ge "; the CPU writing 1 MiB (16384 lines) before the list is queued", unit, n, {a, b}},                            \
  {ge "; the same, then sceKernelDcacheWritebackAll, before the list is queued", unit, n, {a, b}},                    \
  {ge "; a thread writing 1 MiB once beside the list, then stopping", unit, n, {a, b}}
#define GE_NAMES10B(ge, unit, a, b) {ge "; the CPU idle", unit, 2, {a, b}}, PATTERN_NAMES10B(ge, unit, 2, a, b)
static const TimedCase contentionCases3[] = {
  PATTERN_NAMES10B("the GE idle", "-", 1, 0, 0),
  GE_NAMES10B("A: RAM 8888 512x512 swizzled, one sprite 512 wide over 512x128", "layers", 1, 2),
  GE_NAMES10B("C: 512x256 without a texture", "layers", 8, 16),
  GE_NAMES10B("D: culled triangles, 24-byte vertices from RAM", "triangles", 32768, 65536),
  GE_NAMES10B("E2: Super Monkey Ball's lists", "passes", 2, 4),
  GE_NAMES10B("H: the game's small strips (CLUT4 64x64, trilinear)", "triangles", 4096, 8192),
  GE_NAMES10B("I: Ghost of Sparta's kind (skinned, lit strips)", "strips", 1125, 2250),
};
_Static_assert(sizeof(contentions3) / sizeof(contentions3[0]) ==
               sizeof(contentionCases3) / sizeof(contentionCases3[0]), "a line for each case");

//-- placement (time-placement): round 10 had a swizzled RAM texture 64 bytes off a 128-byte boundary cost 7% more
//than on a 1 KiB boundary (time-anchors2), and its re-run of round 9 a 1 KiB palette from RAM 6 to 20% more. Here, from
//a 1 KiB boundary (big's): round 8's time-strips 37 strips (RAM 8888 512x512 swizzled, 32 wide) with the texture 0,
//16, 32, 64, 96, 128 and 512 bytes past it; the same not swizzled at 0 and 64; RAM CLUT8 512x256 swizzled at 0 and 64;
//a palette of 256 colors (8888, 1 KiB) loaded before each 32x32 sprite (a CLUT8 32x32 texture set once: round 9's
//time-loads 45), the palette 0 and 64 bytes past; and time-clut32's tiles once over from a CLUT32 texture swizzled in
//RAM (round 10 had CLUT32 in RAM not swizzled, and in VRAM both ways), on the boundary.
typedef struct { short what, psm, swizzled, offset; } PlacementCase;
enum { PlacedStrips, PlacedPalettes, PlacedTiles };
static const PlacementCase placements[] = {
  {PlacedStrips, GU_PSM_8888, 1, 0}, {PlacedStrips, GU_PSM_8888, 1, 16}, {PlacedStrips, GU_PSM_8888, 1, 32},
  {PlacedStrips, GU_PSM_8888, 1, 64}, {PlacedStrips, GU_PSM_8888, 1, 96}, {PlacedStrips, GU_PSM_8888, 1, 128},
  {PlacedStrips, GU_PSM_8888, 1, 512}, {PlacedStrips, GU_PSM_8888, 0, 0}, {PlacedStrips, GU_PSM_8888, 0, 64},
  {PlacedStrips, GU_PSM_T8, 1, 0}, {PlacedStrips, GU_PSM_T8, 1, 64}, {PlacedPalettes, GU_PSM_T8, 1, 0},
  {PlacedPalettes, GU_PSM_T8, 1, 64}, {PlacedTiles, GU_PSM_T32, 1, 0},
};
static const TimedCase placementCases[] = {
  {"RAM, 8888 512x512 swizzled, strips 32 wide (round 8's time-strips 37), the texture on a 1 KiB boundary", "layers",
   2, {1, 2}},
  {"the same, 16 bytes past it", "layers", 2, {1, 2}}, {"the same, 32 bytes past", "layers", 2, {1, 2}},
  {"the same, 64 bytes past (round 10's place)", "layers", 2, {1, 2}}, {"the same, 96 bytes past", "layers", 2, {1, 2}},
  {"the same, 128 bytes past", "layers", 2, {1, 2}}, {"the same, 512 bytes past", "layers", 2, {1, 2}},
  {"RAM, 8888 512x512 not swizzled, strips 32 wide, on the boundary", "layers", 2, {1, 2}},
  {"the same, 64 bytes past", "layers", 2, {1, 2}},
  {"RAM, CLUT8 512x256 swizzled, strips 32 wide, on the boundary", "layers", 2, {1, 2}},
  {"the same, 64 bytes past", "layers", 2, {1, 2}},
  {"CLUT8 32x32 set once, a palette of 256 colors (8888, 1 KiB) loaded before each sprite, the palette on the boundary",
   "sprites", 2, {64, 256}},
  {"the same, the palette 64 bytes past", "sprites", 2, {64, 256}},
  {"time-clut32's tiles once over (32x34, 3D, nearest, into 5650) from a CLUT32 512x512 texture swizzled in RAM",
   "layers", 2, {1, 2}},
};
_Static_assert(sizeof(placements) / sizeof(placements[0]) == sizeof(placementCases) / sizeof(placementCases[0]),
               "a line for each case");
static void buildPlacement(int c, int size) {
  const PlacementCase* p = &placements[c];
  unsigned char* boundary = (unsigned char*)(((unsigned int)big + 1023) & ~1023u);
  if(p->what == PlacedTiles) {
    static const TileCase tiles = {GU_PSM_T32, 1, 0, 1, 0, GU_PSM_5650};
    drawTiles(&tiles, size);
    return;
  }
  unsigned int* colors = (unsigned int*)(boundary + 1572864 + p->offset);  //(1.5 MiB in: past every texture here)
  if(p->what == PlacedPalettes) {
    memcpy(colors, palette, sizeof(palette));
    sceKernelDcacheWritebackAll();
  }
  timedList(GU_PSM_8888);
  if(p->what == PlacedStrips) {
    texture8(p->psm, p->swizzled, 512, p->psm == GU_PSM_T8 ? 256 : 512, boundary + p->offset, 0);
    for(int layer = 0; layer < size; layer++) spriteStrips(0, 0, 512, 256, 32, 0, 0, 1);
  } else {
    texture8(GU_PSM_T8, 1, 32, 32, boundary, 0);
    for(int k = 0; k < size; k++) {
      sceGuClutLoad(32, colors);
      float x = (k & 15) * 32, y = (k >> 4 & 7) * 32;
      timedSprite(x, y, x + 32, y + 32, 0, 0, 32, 32, 0xffffffff);
    }
  }
  sceGuFinish();
}

static void writeManifest10b(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest10b.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 10b (tools/psp-measure/ge.c in Phobos says what each test draws and runs;\n"
    "docs/psp-core.md, part 112, what each is for). Records of words as round 8's (manifest8.txt): case, size, CPU\n"
    "MHz << 16 | bus MHz (as scePower gave them back), repeats | values << 16, then per run `values` words: times in\n"
    "microseconds from just before sceGeListEnQueue to the finish callback and to sceGeListEnQueue's return;\n"
    "time-contention3 two more as round 10's time-contention, a thread's turns of its pattern and their time (the\n"
    "writes before the list: 16384 and their microseconds; the GE idle: the first time is the pattern's own); "
    "0xffffffff\n"
    "for a run not finished (see <name>.stalled). All run at 222/111 and 333/166 MHz; the clocks as found are put "
    "back\n"
    "after each. The arrays the GE reads are on 1 KiB boundaries. The cases (test case: what; size):\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  static const struct { const char* test; const TimedCase* cases; int count; } tests[] = {
    {"time-contention3", contentionCases3, sizeof(contentionCases3) / sizeof(contentionCases3[0])},
    {"time-placement", placementCases, sizeof(placementCases) / sizeof(placementCases[0])},
  };
  char line[320];
  for(unsigned int t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
    for(int c = 0; c < tests[t].count; c++) {
      int length = snprintf(line, sizeof(line), "%s %d: %s; %s\n", tests[t].test, c, tests[t].cases[c].name,
                            tests[t].cases[c].unit);
      sceIoWrite(file, line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
    }
  }
  static const char end[] =
    "<name>.stalled: the GE hadn't finished the test's list after 5 s and was reset (manifest4.txt says more).\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, end, sizeof(end) - 1);
  sceIoClose(file);
}

static void round10b(void) {
  foundCpu = scePowerGetCpuClockFrequencyInt();
  foundBus = scePowerGetBusClockFrequencyInt();
  foundPll = foundCpu > 2 * foundBus ? foundCpu : 2 * foundBus;  //(as round 6 reads them)
  writeManifest10b();
  prepareTexels();
#define CASES(table) table, sizeof(table) / sizeof(table[0])
  contending = contentions3, contendClocks = 0;
  timeCases8("time-contention3", CASES(contentionCases3), 4, 4, AtTwo, buildContention10, runContention10);
  timeCases8("time-placement", CASES(placementCases), 8, 2, AtTwo, buildPlacement, 0);
#undef CASES
}

//---- round 10c: what #269's refit and #279's model of the CPU's traffic still miss (docs/psp-core.md, part 114).
//Super Monkey Ball's small strips beside the CPU, taken apart (time-contention4); Activision Hits' menu's second and
//third lists as #269's agent specified them (time-shrink, time-magnify: SPECS-10c's A and B) and round 10's game-style
//strips taken apart (time-gamestrips: its C); seven of round 10's cases again (time-anchors3). Round 10's framework
//and safety; the arrays on 1 KiB boundaries.

//-- Super Monkey Ball's small strips beside the CPU (time-contention4): round 10's list H (time-mipmaps 14: 3D strips
//of 6 vertices of a CLUT4 64x64 RAM texture, 7 levels, trilinear) took 1.31 to 1.55 times as long beside the CPU's RAM
//traffic, about 16% more than #279's model gives a list that reads RAM for 37% of its clocks (its vertices 32%, its
//words 5%, its texels 0.6%). Taken apart: H as round 10 drew it; nearest within the level chosen (time-mipmaps 15);
//bilinear without levels (16); nearest without levels (17); untextured; H with its vertices in VRAM's last 256 KiB;
//untextured with them there. Each alone and beside round 10's streaming reads (flat out and a half) and its game's mix
//over 256 KiB, after each pattern alone; round 10's time-contention otherwise (runContention10, its four words a run).
#define PATTERNS10C(ge) {ge, Streaming}, {ge, Streaming2}, {ge, Mix256}
#define GE_ROW10C(ge) {ge, NoThread}, PATTERNS10C(ge)
static const ContentionCase contentions4[] = {
  PATTERNS10C(0), GE_ROW10C(9), GE_ROW10C(11), GE_ROW10C(12), GE_ROW10C(13), GE_ROW10C(14), GE_ROW10C(15),
  GE_ROW10C(16),
};
#define PATTERN_NAMES10C(ge, unit, n, a, b)                                                                          \
  {ge "; the CPU: streaming reads over 1 MiB", unit, n, {a, b}},                                                     \
  {ge "; the CPU: streaming reads, a half of flat out", unit, n, {a, b}},                                            \
  {ge "; the CPU: a mix over 256 KiB", unit, n, {a, b}}
#define GE_NAMES10C(ge)                                                                                              \
  {ge "; the CPU idle", "triangles", 2, {4096, 8192}}, PATTERN_NAMES10C(ge, "triangles", 2, 4096, 8192)
static const TimedCase contentionCases4[] = {
  PATTERN_NAMES10C("the GE idle", "-", 1, 0, 0),
  GE_NAMES10C("H: the game's small strips (CLUT4 64x64, 7 levels, trilinear), as round 10 drew them"),
  GE_NAMES10C("H nearest within the level chosen"), GE_NAMES10C("H bilinear without levels"),
  GE_NAMES10C("H nearest without levels"), GE_NAMES10C("H untextured"),
  GE_NAMES10C("H with its vertices in VRAM"), GE_NAMES10C("H untextured, its vertices in VRAM"),
};
_Static_assert(sizeof(contentions4) / sizeof(contentions4[0]) ==
               sizeof(contentionCases4) / sizeof(contentionCases4[0]), "a line for each case");

//-- the menu's second list (time-shrink): its 60 sprites shrink an 8888 512x512 VRAM picture (480x272 of it) 2:1,
//bilinear, into an 8888 buffer, after round 10's tiles (time-clut32 0: 3.23 ms of the PSP's 3.48 for the whole list);
//#269's refit charges the sprites 1.64 ms. As logged: 2D, 10-byte vertices (16-bit texture coordinates and positions),
//a draw of 2 vertices a sprite, 15 x 4 sprites 16x34 pixels over 240x136 at RenderedAt, each over 32x68 texels; not
//blended, no depth or alpha test. The whole list (the tiles into 5650, then the sprites, a layer each); the sprites
//alone; nearest; 1:1 (sprites 32x68 over 480x272); 4:1 (8x17 over 120x68); 2:1 from the picture swizzled in VRAM and
//from RAM, swizzled and not; one sprite 240x136 over the 480x272 texels (rows 240 wide). A layer: the sprites once.
typedef struct { short whole, shrink, vram, swizzled, linear, one; } ShrinkCase;
static const ShrinkCase shrinks[] = {
  {1, 2, 1, 0, 1, 0}, {0, 2, 1, 0, 1, 0}, {0, 2, 1, 0, 0, 0}, {0, 1, 1, 0, 1, 0}, {0, 4, 1, 0, 1, 0},
  {0, 2, 1, 1, 1, 0}, {0, 2, 0, 1, 1, 0}, {0, 2, 0, 0, 1, 0}, {0, 2, 1, 0, 1, 1},
};
static const TimedCase shrinkCases[] = {
  {"the whole list as logged: time-clut32 0's tiles into 5650, then 60 sprites into 8888 (VRAM 8888 512x512 not "
   "swizzled, 2:1, bilinear)", "layers", 2, {1, 2}},
  {"the 60 sprites alone (2:1, bilinear)", "layers", 2, {1, 4}}, {"the same, nearest", "layers", 2, {1, 4}},
  {"1:1 (sprites 32x68 over 480x272), bilinear", "layers", 2, {1, 4}},
  {"4:1 (sprites 8x17 over 120x68), bilinear", "layers", 2, {1, 4}},
  {"2:1, the picture swizzled in VRAM", "layers", 2, {1, 4}},
  {"2:1, the picture in RAM, swizzled", "layers", 2, {1, 4}},
  {"2:1, the picture in RAM, not swizzled", "layers", 2, {1, 4}},
  {"one sprite 240x136 over the 480x272 texels (VRAM, not swizzled, bilinear)", "layers", 2, {1, 4}},
};
_Static_assert(sizeof(shrinks) / sizeof(shrinks[0]) == sizeof(shrinkCases) / sizeof(shrinkCases[0]),
               "a line for each case");
typedef struct { unsigned short u, v; short x, y, z; } ShortVertex;  //(10 bytes, as the menu's sprites)
enum { ShortVertexType = GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D };
static void shrinkSprites(const ShrinkCase* k) {
  texture8(GU_PSM_8888, k->swizzled, 512, 512, k->vram ? VRAM_TEXTURE : (void*)big, k->linear);
  int count = k->one ? 1 : 60, w = k->one ? 240 : 32 / k->shrink, h = k->one ? 136 : 68 / k->shrink;
  int across = k->one ? 480 : 32, down = k->one ? 272 : 68;  //(texels a sprite)
  ShortVertex* v = sceGuGetMemory(count * 2 * sizeof(ShortVertex));
  for(int n = 0; n < count; n++) {
    int column = n % 15, row = n / 15;
    v[n * 2] = (ShortVertex){column * 32, row * 68, column * w, row * h, 0};
    v[n * 2 + 1] = (ShortVertex){column * 32 + across, row * 68 + down, column * w + w, row * h + h, 0};
  }
  for(int n = 0; n < count; n++) sceGuDrawArray(GU_SPRITES, ShortVertexType, 2, 0, v + n * 2);
}
static void buildShrink(int c, int layers) {
  const ShrinkCase* k = &shrinks[c];
  timedList(k->whole ? GU_PSM_5650 : GU_PSM_8888);
  screen480();
  for(int layer = 0; layer < layers; layer++) {
    if(k->whole) {
      sceGuDrawBuffer(GU_PSM_5650, (void*)Target, Stride);
      tilesInto(&tiles10[0], 1);
      sceGuDisable(GU_BLEND);
    }
    sceGuDrawBuffer(GU_PSM_8888, (void*)RenderedAt, Stride);
    shrinkSprites(k);
  }
  sceGuFinish();
}

//-- the menu's third list (time-magnify): 63 3D strips of 4 vertices (24 bytes: here FloatVertex) over 480x272 (9 x 7
//quads) magnifying an 8888 256x512 RAM texture, swizzled, 1.875 times (256 texels over 480 pixels), bilinear,
//modulated, blended (source alpha, 1 - source alpha), the alpha test (greater than 0), into 5650; +26.7% in #269's
//refit, which reads a triangle's narrow rows wide into 16-bit (part 112 reads round 10's copyright pass as bilinear on
//wide reads instead). Bilinear and nearest each: as logged; 1:1 and 4 times (8 x 8 quads over 256x256: 256 and 64
//texels across); into 8888; neither blended nor alpha-tested; the texture 512x256; CLUT8 256x512 (time-widetri's
//palette). A layer: every quad once.
typedef struct { short layout, psm, wide, target, blended, linear; } MagnifyCase;
enum { AsLogged, OneToOne, FourTimes };
#define LINEAR_AND_NEAREST(layout, psm, wide, target, blended)                                                       \
  {layout, psm, wide, target, blended, 1}, {layout, psm, wide, target, blended, 0}
static const MagnifyCase magnifies[] = {
  LINEAR_AND_NEAREST(AsLogged, GU_PSM_8888, 0, GU_PSM_5650, 1),
  LINEAR_AND_NEAREST(OneToOne, GU_PSM_8888, 0, GU_PSM_5650, 1),
  LINEAR_AND_NEAREST(FourTimes, GU_PSM_8888, 0, GU_PSM_5650, 1),
  LINEAR_AND_NEAREST(AsLogged, GU_PSM_8888, 0, GU_PSM_8888, 1),
  LINEAR_AND_NEAREST(AsLogged, GU_PSM_8888, 0, GU_PSM_5650, 0),
  LINEAR_AND_NEAREST(AsLogged, GU_PSM_8888, 1, GU_PSM_5650, 1),
  LINEAR_AND_NEAREST(AsLogged, GU_PSM_T8, 0, GU_PSM_5650, 1),
};
#define LINEAR_AND_NEAREST_NAMES(what)                                                                               \
  {what ", bilinear", "layers", 2, {1, 4}}, {"the same, nearest", "layers", 2, {1, 4}}
static const TimedCase magnifyCases[] = {
  LINEAR_AND_NEAREST_NAMES("as logged: RAM 8888 256x512 swizzled magnified 1.875 times over 480x272 (9 x 7 quads), "
                           "modulated, blended, the alpha test, into 5650"),
  LINEAR_AND_NEAREST_NAMES("1:1 (8 x 8 quads over 256x256)"),
  LINEAR_AND_NEAREST_NAMES("4 times (8 x 8 quads over 256x256, 64x64 texels)"),
  LINEAR_AND_NEAREST_NAMES("as logged, into 8888"),
  LINEAR_AND_NEAREST_NAMES("as logged, neither blended nor alpha-tested"),
  LINEAR_AND_NEAREST_NAMES("as logged, the texture 512x256"),
  LINEAR_AND_NEAREST_NAMES("as logged, CLUT8 256x512"),
};
_Static_assert(sizeof(magnifies) / sizeof(magnifies[0]) == sizeof(magnifyCases) / sizeof(magnifyCases[0]),
               "a line for each case");
static void buildMagnify(int c, int layers) {
  const MagnifyCase* k = &magnifies[c];
  int width = k->wide ? 512 : 256, height = k->wide ? 256 : 512, logged = k->layout == AsLogged;
  int across = logged ? 9 : 8, down = logged ? 7 : 8, right = logged ? 480 : 256, bottom = logged ? 272 : 256;
  float scale = logged ? 256.0f / 480 : k->layout == OneToOne ? 1 : 0.25f;  //(texels a pixel)
  timedList(k->target);
  setUp3D(&identity, 1);
  sceGuDisable(GU_DEPTH_TEST);
  screen480();
  texture8(k->psm, 1, width, height, big, k->linear);
  sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  if(k->blended) {
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
  }
  FloatVertex* v = sceGuGetMemory(across * down * 4 * sizeof(FloatVertex));
  for(int q = 0; q < across * down; q++) {
    float x0 = q % across * right / across, x1 = (q % across + 1) * right / across;
    float y0 = q / across * bottom / down, y1 = (q / across + 1) * bottom / down;
    for(int corner = 0; corner < 4; corner++) {  //top-left, top-right, bottom-left, bottom-right
      float x = corner & 1 ? x1 : x0, y = corner & 2 ? y1 : y0;
      float u = x * scale / width, t = y * scale / height;
      v[q * 4 + corner] = (FloatVertex){u, t, 0xffffffff, (x - 240) / 240, (136 - y) / 136, 0};
    }
  }
  for(int layer = 0; layer < layers; layer++) {
    for(int q = 0; q < across * down; q++) sceGuDrawArray(GU_TRIANGLE_STRIP, FloatVertexType3D, 4, 0, v + q * 4);
  }
  sceGuFinish();
}

//-- round 10's game-style strips taken apart (time-gamestrips): time-lights 37 and 38 (strips of 9 vertices of 12
//bytes: 8-bit texture coordinates and normal, 16-bit position; triangles of 16x20 pixels; a CLUT4 64x64 RAM texture,
//swizzled, bilinear, modulated; the depth test passing and writing, the alpha test, into 8888) took the same lit or
//not, 2.1 times #269's model's pixels. The strips unlit: as round 10 drew them (time-lights 38); nearest; without the
//alpha test; without the depth test; into 5650; untextured.
typedef struct { short linear, alpha, depth, target, textured; } GameStripCase;
static const GameStripCase gameStrips[] = {
  {1, 1, 1, GU_PSM_8888, 1}, {0, 1, 1, GU_PSM_8888, 1}, {1, 0, 1, GU_PSM_8888, 1}, {1, 1, 0, GU_PSM_8888, 1},
  {1, 1, 1, GU_PSM_5650, 1}, {1, 1, 1, GU_PSM_8888, 0},
};
static const TimedCase gameStripCases[] = {
  {"time-lights 38: strips of 9 vertices (16x20-pixel triangles, CLUT4 64x64 bilinear, depth and alpha tests, into "
   "8888), unlit", "strips", 2, {256, 1024}},
  {"the same, nearest", "strips", 2, {256, 1024}},
  {"the same bilinear, without the alpha test", "strips", 2, {256, 1024}},
  {"without the depth test", "strips", 2, {256, 1024}}, {"into 5650", "strips", 2, {256, 1024}},
  {"untextured", "strips", 2, {256, 1024}},
};
_Static_assert(sizeof(gameStrips) / sizeof(gameStrips[0]) == sizeof(gameStripCases) / sizeof(gameStripCases[0]),
               "a line for each case");
static void buildGameStrips(int c, int count) {
  const GameStripCase* g = &gameStrips[c];
  Parts p = {0, 0, 1, 0, 1, 2};
  unsigned char* at = (unsigned char*)big;
  for(int n = 0; n < count * 9; n++) {  //strip n / 9 at a 128x20 cell of the target's 512x256 (as buildLights10's)
    int strip = n / 9, k = n % 9;
    float x = (strip & 3) * 128 + k * 16, y = (strip / 4 % 12) * 20 + (k & 1) * 20;
    at = putPart(putPart(at, 1, k / 8.0f), 1, (k & 1) * 0.5f);
    at = putPart(putPart(putPart(at, 1, 0), 1, 0), 1, 1) + 1;
    at = putPart(putPart(putPart(at, 2, (x - 256) / 256), 2, (128 - y) / 128), 2, 0);
  }
  int bytes = (at - (unsigned char*)big) / (count * 9);
  fillDepth(0);
  sceKernelDcacheWritebackAll();
  timedList(g->target);
  setUp3D(&identity, 1);
  sceGuOffset(2048 - 256, 2048 - 128);
  sceGuViewport(2048, 2048, 512, 256);
  if(g->depth) sceGuDepthFunc(GU_GEQUAL);
  else sceGuDisable(GU_DEPTH_TEST);
  if(g->alpha) sceGuEnable(GU_ALPHA_TEST), sceGuAlphaFunc(GU_GREATER, 0, 0xff);
  if(g->textured) texture8(GU_PSM_T4, 1, 64, 64, bytes8, g->linear), sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
  int type = partsType(&p) | GU_TRANSFORM_3D;
  for(int strip = 0; strip < count; strip++) {
    sceGuDrawArray(GU_TRIANGLE_STRIP, type, 9, 0, (unsigned char*)big + strip * 9 * bytes);
  }
  sceGuFinish();
}

//-- anchors (time-anchors3): seven of round 10's cases, built by round 10's code at round 10's sizes.
static const TimedCase anchorCases10c[] = {
  {"round 10's time-anchors2 6: round 8's time-strips 37's strips (RAM 8888 512x512 swizzled, 32 wide), the texture on "
   "a 1 KiB boundary", "layers", 2, {1, 2}},
  {"round 10's time-clut32 0: the game's tiles (CLUT32 512x512 in VRAM, 32x34, three passes, 240 palette loads a "
   "layer, into 5650)", "layers", 2, {1, 2}},
  {"round 10's time-widetri 0: the game's pass (a strip of two triangles over 480x272, CLUT8 512x256 in RAM swizzled, "
   "bilinear, modulated, blended, the alpha test, into 5650)", "layers", 2, {1, 2}},
  {"round 10's time-perspective 3: RAM 8888 256x256 swizzled, two triangles over 256x256 pixels, w 1 to 2", "layers", 2,
   {1, 2}},
  {"round 10's time-mipmaps 1: round 8's time-textures 10 (RAM 8888 256x256 swizzled shrunk 2:1, 3 levels, the level "
   "chosen, nearest)", "layers", 2, {1, 2}},
  {"round 10's time-vram8 1: VRAM, not swizzled, CLUT8 512x512, strips 8 wide as sprites", "layers", 2, {1, 2}},
  {"round 10's time-lights 38: the game's strips drawn, unlit", "strips", 2, {1024, 4096}},
};
static void buildAnchors10c(int c, int size) {
  switch(c) {
  case 0: buildAnchors10(6, size); break;
  case 1: buildClut32(0, size); break;
  case 2: buildWide(0, size); break;
  case 3: buildPerspective(3, size); break;
  case 4: buildMipmaps(1, size); break;
  case 5: buildNarrow(1, size); break;
  default: buildLights10(38, size); break;
  }
}

static void writeManifest10c(void) {
  char path[320];
  snprintf(path, sizeof(path), "%s/manifest10c.txt", folder);
  SceUID file = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
  if(file < 0) return;
  static const char text[] =
    "psp-measure's GE tests, round 10c (tools/psp-measure/ge.c in Phobos says what each test draws and runs;\n"
    "docs/psp-core.md, part 114, what each is for). Records of words as round 8's (manifest8.txt): case, size, CPU\n"
    "MHz << 16 | bus MHz (as scePower gave them back), repeats | values << 16, then per run `values` words: times in\n"
    "microseconds from just before sceGeListEnQueue to the finish callback and to sceGeListEnQueue's return;\n"
    "time-contention4 two more as round 10's time-contention, a thread's turns of its pattern and their time (the GE\n"
    "idle: the first time is the pattern's own); 0xffffffff for a run not finished (see <name>.stalled). All run at\n"
    "222/111 and 333/166 MHz; the clocks as found are put back after each. The arrays the GE reads are on 1 KiB\n"
    "boundaries. The cases (test case: what; size):\n";
  sceIoWrite(file, text, sizeof(text) - 1);
  static const struct { const char* test; const TimedCase* cases; int count; } tests[] = {
    {"time-contention4", contentionCases4, sizeof(contentionCases4) / sizeof(contentionCases4[0])},
    {"time-shrink", shrinkCases, sizeof(shrinkCases) / sizeof(shrinkCases[0])},
    {"time-magnify", magnifyCases, sizeof(magnifyCases) / sizeof(magnifyCases[0])},
    {"time-gamestrips", gameStripCases, sizeof(gameStripCases) / sizeof(gameStripCases[0])},
    {"time-anchors3", anchorCases10c, sizeof(anchorCases10c) / sizeof(anchorCases10c[0])},
  };
  char line[320];
  for(unsigned int t = 0; t < sizeof(tests) / sizeof(tests[0]); t++) {
    for(int c = 0; c < tests[t].count; c++) {
      int length = snprintf(line, sizeof(line), "%s %d: %s; %s\n", tests[t].test, c, tests[t].cases[c].name,
                            tests[t].cases[c].unit);
      sceIoWrite(file, line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
    }
  }
  static const char end[] =
    "<name>.stalled: the GE hadn't finished the test's list after 5 s and was reset (manifest4.txt says more).\n"
    "<name>.stopped: a test that stopped the PSP twice, given up on.\n";
  sceIoWrite(file, end, sizeof(end) - 1);
  sceIoClose(file);
}

static void round10c(void) {
  foundCpu = scePowerGetCpuClockFrequencyInt();
  foundBus = scePowerGetBusClockFrequencyInt();
  foundPll = foundCpu > 2 * foundBus ? foundCpu : 2 * foundBus;  //(as round 6 reads them)
  writeManifest10c();
  prepareTexels();
#define CASES(table) table, sizeof(table) / sizeof(table[0])
  contending = contentions4, contendClocks = 0;
  timeCases8("time-contention4", CASES(contentionCases4), 4, 4, AtTwo, buildContention10, runContention10);
  timeCases8("time-shrink", CASES(shrinkCases), 8, 2, AtTwo, buildShrink, 0);
  timeCases8("time-magnify", CASES(magnifyCases), 8, 2, AtTwo, buildMagnify, 0);
  timeCases8("time-gamestrips", CASES(gameStripCases), 8, 2, AtTwo, buildGameStrips, 0);
  timeCases8("time-anchors3", CASES(anchorCases10c), 8, 2, AtTwo, buildAnchors10c, 0);
#undef CASES
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

//A round of the GE's tests (2 to 10, and 11 for round 10b) into results/ge. Returns 0 if something couldn't be written.
int geRound(int round) {
  if(round < 2 || round > 12) return 1;
  if(!guReady) {
    sceGuInit();
    guReady = 1;
  }
  useFolder("ge");
  failed = 0;
  if(round == 2) round2();
  else if(round == 3) round3();
  else if(round == 4) round4();
  else if(round == 5) round5();
  else if(round == 6) round6();
  else if(round == 7) round7();
  else if(round == 8) round8();
  else if(round == 9) round9();
  else if(round == 10) round10();
  else if(round == 11) round10b();
  else round10c();
  return !failed;
}

void geEnd(void) {
  if(guReady) sceGuTerm();
}
