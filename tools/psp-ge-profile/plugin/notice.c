//psp-ge-profile's on-screen notices: a few lines of text in a box at the screen's top left (the plugin is on, for
//which game, in which mode; a capture is about to start; it was saved, or why not), or, while a capture or a dry
//run's window is on, only a small blinking square at the top right, so the measurement stays clean: 64 pixels a
//flip, drawn after the sceDisplaySetFrameBuf call's record is closed, so no recorded time includes them.
//
//They're drawn into the frame buffer the game hands sceDisplaySetFrameBuf, once the firmware has accepted it, in its
//pixel format, through the uncached address so the display sees the writes at once. If the hook sees no flips (it
//isn't in place, or the game doesn't flip), the control thread draws the text into the buffer sceDisplayGetFrameBuf
//reports instead, every 10 ms while there's text to show: it flickers, but it shows. Nothing is drawn unless the
//buffer is plausible: 64 to 1024 pixels wide, a known format, all of the screen's 272 rows inside VRAM or inside the
//user RAM every model has. A text's time starts when it's first drawn, so one set before the game's first frame is
//still seen. The font is this file's own: 5x7 glyphs, drawn twice their size.

#include "plugin.h"

enum { ScreenWidth = 480, ScreenHeight = 272 };
enum { GlyphWidth = 5, GlyphHeight = 7, Scale = 2, Advance = (GlyphWidth + 1) * Scale, LineHeight = (GlyphHeight + 2) * Scale };
enum { Lines = 3, LineSize = 32, BoxX = 8, BoxY = 8, Padding = 6, MarkerSize = 8 };
enum { FallbackQuiet = 200000 };  //microseconds without a hooked flip before the control thread draws instead

//Colors as R | G << 8 | B << 16 | A << 24, the PSP's 8888 layout.
enum { Black = 0xFF000000u, White = 0xFFFFFFFFu, Yellow = 0xFF00FFFFu, Red = 0xFF0000FFu };

//Rows top to bottom; bit 0x80 is the leftmost of a glyph's five columns. Lower case is drawn as upper case.
static const u8 font[64][GlyphHeight] = {
  ['(' - 32] = {0x10, 0x20, 0x40, 0x40, 0x40, 0x20, 0x10},
  [')' - 32] = {0x40, 0x20, 0x10, 0x10, 0x10, 0x20, 0x40},
  ['-' - 32] = {0x00, 0x00, 0x00, 0x78, 0x00, 0x00, 0x00},
  ['.' - 32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60},
  ['/' - 32] = {0x08, 0x10, 0x10, 0x20, 0x40, 0x40, 0x80},
  ['0' - 32] = {0x70, 0x88, 0x98, 0xA8, 0xC8, 0x88, 0x70},
  ['1' - 32] = {0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70},
  ['2' - 32] = {0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xF8},
  ['3' - 32] = {0x70, 0x88, 0x08, 0x30, 0x08, 0x88, 0x70},
  ['4' - 32] = {0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10},
  ['5' - 32] = {0xF8, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70},
  ['6' - 32] = {0x70, 0x80, 0x80, 0xF0, 0x88, 0x88, 0x70},
  ['7' - 32] = {0xF8, 0x08, 0x10, 0x20, 0x20, 0x20, 0x20},
  ['8' - 32] = {0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70},
  ['9' - 32] = {0x70, 0x88, 0x88, 0x78, 0x08, 0x08, 0x70},
  [':' - 32] = {0x00, 0x60, 0x60, 0x00, 0x60, 0x60, 0x00},
  ['A' - 32] = {0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88},
  ['B' - 32] = {0xF0, 0x88, 0x88, 0xF0, 0x88, 0x88, 0xF0},
  ['C' - 32] = {0x70, 0x88, 0x80, 0x80, 0x80, 0x88, 0x70},
  ['D' - 32] = {0xF0, 0x88, 0x88, 0x88, 0x88, 0x88, 0xF0},
  ['E' - 32] = {0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0xF8},
  ['F' - 32] = {0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0x80},
  ['G' - 32] = {0x70, 0x88, 0x80, 0xB8, 0x88, 0x88, 0x70},
  ['H' - 32] = {0x88, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88},
  ['I' - 32] = {0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70},
  ['J' - 32] = {0x38, 0x10, 0x10, 0x10, 0x90, 0x90, 0x60},
  ['K' - 32] = {0x88, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x88},
  ['L' - 32] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xF8},
  ['M' - 32] = {0x88, 0xD8, 0xA8, 0xA8, 0x88, 0x88, 0x88},
  ['N' - 32] = {0x88, 0xC8, 0xA8, 0x98, 0x88, 0x88, 0x88},
  ['O' - 32] = {0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70},
  ['P' - 32] = {0xF0, 0x88, 0x88, 0xF0, 0x80, 0x80, 0x80},
  ['Q' - 32] = {0x70, 0x88, 0x88, 0x88, 0xA8, 0x90, 0x68},
  ['R' - 32] = {0xF0, 0x88, 0x88, 0xF0, 0xA0, 0x90, 0x88},
  ['S' - 32] = {0x78, 0x80, 0x80, 0x70, 0x08, 0x08, 0xF0},
  ['T' - 32] = {0xF8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20},
  ['U' - 32] = {0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70},
  ['V' - 32] = {0x88, 0x88, 0x88, 0x88, 0x88, 0x50, 0x20},
  ['W' - 32] = {0x88, 0x88, 0x88, 0xA8, 0xA8, 0xD8, 0x88},
  ['X' - 32] = {0x88, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88},
  ['Y' - 32] = {0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0x20},
  ['Z' - 32] = {0xF8, 0x08, 0x10, 0x20, 0x40, 0x80, 0xF8},
  ['_' - 32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8},
};

//Two copies of the text, so the control thread fills one while a flip may be drawing the other.
static char text[2][Lines][LineSize];
static volatile int shown, bannerOn, markerOn, enabled = 1;
static volatile u32 bannerLength, bannerUntil;  //bannerUntil 0: not drawn yet
static volatile u32 lastFlip, firstFlip;

typedef struct { volatile u8* bytes; u32 stride, pixel, format, width; } FrameBuffer;

static int frameBuffer(FrameBuffer* buffer, void* top, int width, int format) {
  u32 address = (u32)(Address)top, physical = address & 0x1FFFFFFF, end;
  if(!address || format < 0 || format > 3 || width < 64 || width > 1024) return 0;
  if(physical >= 0x04000000 && physical < 0x04200000) end = 0x04200000;
  else if(physical >= 0x08800000 && physical < 0x0A000000) end = 0x0A000000;
  else return 0;
  u32 pixel = format == 3 ? 4 : 2;
  if(physical & (pixel - 1)) return 0;
  if(physical + (u32)width * ScreenHeight * pixel > end) return 0;
  buffer->bytes = UncachedBytes(physical);
  buffer->stride = width;
  buffer->pixel = pixel;
  buffer->format = format;
  buffer->width = width < ScreenWidth ? width : ScreenWidth;
  return 1;
}

//The display's formats: 0 is 5650, 1 is 5551, 2 is 4444, 3 is 8888, red in the low bits of each.
static u32 pixelValue(u32 format, u32 color) {
  u32 r = color & 0xFF, g = color >> 8 & 0xFF, b = color >> 16 & 0xFF, a = color >> 24;
  switch(format) {
  case 0: return r >> 3 | (g >> 2) << 5 | (b >> 3) << 11;
  case 1: return r >> 3 | (g >> 3) << 5 | (b >> 3) << 10 | (a >> 7) << 15;
  case 2: return r >> 4 | (g >> 4) << 4 | (b >> 4) << 8 | (a >> 4) << 12;
  }
  return color;
}

//Clipped to the screen.
static void fill(const FrameBuffer* buffer, int x, int y, int w, int h, u32 value) {
  if(x < 0) w += x, x = 0;
  if(y < 0) h += y, y = 0;
  if(x + w > (int)buffer->width) w = buffer->width - x;
  if(y + h > ScreenHeight) h = ScreenHeight - y;
  for(int row = y; row < y + h; row++) {
    volatile u8* line = buffer->bytes + ((u32)row * buffer->stride + x) * buffer->pixel;
    if(buffer->pixel == 4) for(int n = 0; n < w; n++) ((volatile u32*)line)[n] = value;
    else for(int n = 0; n < w; n++) ((volatile u16*)line)[n] = value;
  }
}

static void drawLine(const FrameBuffer* buffer, int x, int y, const char* line, u32 value) {
  for(; *line; line++, x += Advance) {
    int c = *line >= 'a' && *line <= 'z' ? *line - 32 : *line;
    if(c < 32 || c >= 96) continue;
    const u8* glyph = font[c - 32];
    for(int row = 0; row < GlyphHeight; row++) {
      for(int column = 0; column < GlyphWidth; column++) {
        if(glyph[row] & 0x80 >> column) fill(buffer, x + column * Scale, y + row * Scale, Scale, Scale, value);
      }
    }
  }
}

static int bannerShowing(u32 now) {
  return bannerOn && (!bannerUntil || (s32)(bannerUntil - now) > 0);
}

static void drawBanner(const FrameBuffer* buffer, u32 now) {
  if(!bannerUntil) bannerUntil = (now + bannerLength) | 1;
  int copy = shown, lines = 0, longest = 0;
  for(int n = 0; n < Lines; n++) {
    int length = 0;
    while(text[copy][n][length]) length++;
    if(length) lines = n + 1;
    if(length > longest) longest = length;
  }
  if(!lines) return;
  fill(buffer, BoxX, BoxY, longest * Advance - Scale + 2 * Padding, lines * LineHeight - 2 * Scale + 2 * Padding,
       pixelValue(buffer->format, Black));
  for(int n = 0; n < lines; n++) {
    drawLine(buffer, BoxX + Padding, BoxY + Padding + n * LineHeight, text[copy][n],
             pixelValue(buffer->format, n ? White : Yellow));
  }
}

//Red and white in turn, about twice a second.
static void drawMarker(const FrameBuffer* buffer, u32 now) {
  fill(buffer, buffer->width - MarkerSize - BoxX, BoxY, MarkerSize, MarkerSize,
       pixelValue(buffer->format, now >> 18 & 1 ? Red : White));
}

void noticeEnable(int on) {
  enabled = on;
}

void noticeShow(const char* line1, const char* line2, const char* line3, u32 milliseconds) {
  const char* lines[Lines] = {line1, line2, line3};
  int copy = !shown;
  for(int n = 0; n < Lines; n++) {
    int length = 0;
    for(const char* c = lines[n]; c && *c && length < LineSize - 1; c++) text[copy][n][length++] = *c;
    text[copy][n][length] = 0;
  }
  bannerOn = 0;
  shown = copy;
  bannerLength = milliseconds * 1000;
  bannerUntil = 0;
  bannerOn = 1;
}

void noticeClear(void) {
  bannerOn = 0;
}

void noticeMarker(int on) {
  markerOn = on;
}

//From the sceDisplaySetFrameBuf hook, with the frame buffer the firmware has just accepted.
void noticeDraw(void* top, int width, int format) {
  u32 now = sceKernelGetSystemTimeLow();
  lastFlip = now;
  if(!firstFlip) firstFlip = now | 1;
  if(!enabled) return;
  int banner = bannerShowing(now);
  if(!banner && !markerOn) return;
  FrameBuffer buffer;
  if(!frameBuffer(&buffer, top, width, format)) return;
  if(banner) drawBanner(&buffer, now);
  else drawMarker(&buffer, now);
}

int noticeWantsFallback(void) {
  if(!enabled || !firmware.displayGetFrameBuf) return 0;
  u32 now = sceKernelGetSystemTimeLow();
  if(firstFlip && (s32)(now - lastFlip) < FallbackQuiet) return 0;
  return bannerShowing(now);
}

//From the control thread, never during a capture: the text only, into the buffer about to be shown.
void noticeFallback(void) {
  if(!noticeWantsFallback()) return;
  void* top = 0;
  int width = 0, format = 0;
  if(firmware.displayGetFrameBuf(&top, &width, &format, 1) < 0) return;
  FrameBuffer buffer;
  if(frameBuffer(&buffer, top, width, format)) drawBanner(&buffer, sceKernelGetSystemTimeLow());
}

u32 noticeFirstFlip(void) {
  return firstFlip;
}
