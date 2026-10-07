//The pixel pipeline (ares/psp/ge/pixel.cpp's drawPixelAs(), the same steps in the same order, whole numbers
//throughout): the depth range test, alpha test, fog, color test, stencil and depth tests, the depth written,
//blending, dithering, the logic operation and the write through the masks.
//
//Each thread draws two pixels side by side, the first in an even column (raster.comp), and keeps their frame buffer
//and depth buffer words here while it draws the batch's jobs, in order: for 16-bit frame buffers and the depth
//buffer the two pixels share a word, for 8888 each has its own. No other thread touches those words (gpu.cpp sends
//the GPU only batches the GE has found to be drawable in parallel: no two pixels share a byte), so the words are
//read once before the first job and written once after the last.
uint pairColor[2];  //16-bit: both pixels in the first; 8888: one each
uint pairDepth;     //both pixels' depths
bool colorWritten = false, depthWritten = false;

uint frameBufferFormat;  //the batch's (every job in a batch draws into one frame buffer)

uint readPixel(int lane) {
  return frameBufferFormat == 3u ? pairColor[lane] : pairColor[0] >> uint(lane * 16) & 0xffffu;
}
void writePixel(int lane, uint value) {
  if(frameBufferFormat == 3u) pairColor[lane] = value;
  else pairColor[0] = (pairColor[0] & ~(0xffffu << uint(lane * 16))) | (value & 0xffffu) << uint(lane * 16);
  colorWritten = true;
}
uint readDepth(int lane) { return pairDepth >> uint(lane * 16) & 0xffffu; }
void writeDepth(int lane, uint z) {
  pairDepth = (pairDepth & ~(0xffffu << uint(lane * 16))) | (z & 0xffffu) << uint(lane * 16);
  depthWritten = true;
}

//GU_NEVER, GU_ALWAYS, GU_EQUAL, GU_NOTEQUAL, GU_LESS, GU_LEQUAL, GU_GREATER, GU_GEQUAL
bool passes(uint comparison, int a, int b) {
  switch(comparison & 7u) {
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

//A 16-bit color of format 0-2 as 8888, each field widened by repeating its top bits (texture.cpp's widen16()).
uint widenField(uint value, uint bits) { return value << (8u - bits) | value >> (2u * bits - 8u); }
uint widen16(uint c, uint format) {
  if(format == 0u) {
    return widenField(c & 31u, 5u) | widenField(c >> 5 & 63u, 6u) << 8 | widenField(c >> 11 & 31u, 5u) << 16 |
           0xff000000u;
  }
  if(format == 1u) {
    return widenField(c & 31u, 5u) | widenField(c >> 5 & 31u, 5u) << 8 | widenField(c >> 10 & 31u, 5u) << 16 |
           ((c >> 15) != 0u ? 0xff000000u : 0u);
  }
  return widenField(c & 15u, 4u) | widenField(c >> 4 & 15u, 4u) << 8 | widenField(c >> 8 & 15u, 4u) << 16 |
         widenField(c >> 12 & 15u, 4u) << 24;
}
//An 8888 color in a frame buffer format, each channel keeping its top bits; and back (5650's alpha reads as 0).
uint narrowPixel(uint color, uint format) {
  uint r = color & 0xffu, g = color >> 8 & 0xffu, b = color >> 16 & 0xffu, a = color >> 24;
  if(format == 0u) return r >> 3 | g >> 2 << 5 | b >> 3 << 11;
  if(format == 1u) return r >> 3 | g >> 3 << 5 | b >> 3 << 10 | a >> 7 << 15;
  if(format == 2u) return r >> 4 | g >> 4 << 4 | b >> 4 << 8 | a >> 4 << 12;
  return color;
}
uint widenPixel(uint pixel, uint format) {
  if(format == 3u) return pixel;
  return widen16(pixel, format) & (format == 0u ? 0x00ffffffu : 0xffffffffu);
}

//keep, zero, replace, invert, increment, decrement: the last two stopping at the format's ends
uint stencilOperation(uint operation, uint format, uint reference, uint stencil) {
  switch(operation & 7u) {
  case 0u: return stencil;
  case 1u: return 0u;
  case 2u: return reference;
  case 3u: return ~stencil & 0xffu;
  case 4u:
    if(format == 1u) return 0xffu;
    if(format == 2u) return stencil < 0xf0u ? stencil + 0x10u : stencil;
    return stencil < 0xffu ? stencil + 1u : stencil;
  case 5u:
    if(format == 1u) return 0u;
    if(format == 2u) return stencil >= 0x10u ? stencil - 0x10u : stencil;
    return stencil != 0u ? stencil - 1u : 0u;
  }
  return stencil;
}

//A blend factor's three channels (BLEND_MODE's source or destination factor): of the other's color, the source's or
//destination's alpha, doubled, or fixed.
ivec3 blendFactor(uint which, uint other, int sourceAlpha, int destinationAlpha, uint fixedColor) {
  switch(which) {
  case 0u: return ivec3(channel(other, 0), channel(other, 1), channel(other, 2));
  case 1u: return ivec3(255 - channel(other, 0), 255 - channel(other, 1), 255 - channel(other, 2));
  case 2u: return ivec3(sourceAlpha);
  case 3u: return ivec3(255 - sourceAlpha);
  case 4u: return ivec3(destinationAlpha);
  case 5u: return ivec3(255 - destinationAlpha);
  case 6u: return ivec3(2 * sourceAlpha);
  case 7u: return ivec3(255 - min(2 * sourceAlpha, 255));
  case 8u: return ivec3(2 * destinationAlpha);
  case 9u: return ivec3(255 - min(2 * destinationAlpha, 255));
  }
  return ivec3(channel(fixedColor, 0), channel(fixedColor, 1), channel(fixedColor, 2));
}

uint logicOperation(uint operation, uint s, uint d) {
  switch(operation) {
  case 0u: return 0u;
  case 1u: return s & d;
  case 2u: return s & ~d;
  case 3u: return s;
  case 4u: return ~s & d;
  case 5u: return d;
  case 6u: return s ^ d;
  case 7u: return s | d;
  case 8u: return ~(s | d);
  case 9u: return ~(s ^ d);
  case 10u: return ~d;
  case 11u: return s | ~d;
  case 12u: return ~s;
  case 13u: return ~s | d;
  case 14u: return ~(s & d);
  }
  return 0xffffffu;
}

//The pixel at (x, y) (the thread's pixel lane), with depth z, color (8888) and fog (0-255), drawn with the look's
//pipeline.
void drawPixel(uint look, int lane, int x, int y, uint z, uint color, uint fog) {
  uint flags = lookWord(look, LookFlags);
  uint format = frameBufferFormat;
  uint depths = lookWord(look, LookDepths);
  if((flags & FlagDepthRange) != 0u && (z < (depths & 0xffffu) || z > depths >> 16)) return;
  uint old = readPixel(lane);
  uint colorBits = format == 0u ? 0xffffu : format == 1u ? 0x7fffu : format == 2u ? 0x0fffu : 0x00ffffffu;
  uint stencilBits = (format == 3u ? 0xffffffffu : 0xffffu) & ~colorBits;
  uint writeMask = lookWord(look, LookWriteMask);
  uint stencil = widenPixel(old, format) >> 24;

  if((flags & FlagClear) != 0u) {  //the vertex's alpha goes in as the stencil
    if((flags & FlagClearDepth) != 0u) writeDepth(lane, z);
    uint keep = writeMask | ((flags & FlagClearColor) != 0u ? 0u : colorBits) |
                ((flags & FlagClearAlpha) != 0u ? 0u : stencilBits);
    writePixel(lane, (narrowPixel(color, format) & ~keep) | (old & keep));
    return;
  }
  int alpha = int(color >> 24);
  if((flags & FlagAlphaTest) != 0u) {
    uint test = lookWord(look, LookAlpha);
    int mask = int(test >> 16 & 0xffu);
    if(!passes(test, alpha & mask, int(test >> 8 & 0xffu) & mask)) return;
  }
  if((flags & FlagFog) != 0u) {
    uint fogColor = lookWord(look, LookFogColor);
    uint fogged = color & 0xff000000u;
    for(int n = 0; n < 3; n++) {
      int mixed = (channel(color, n) * int(fog) + channel(fogColor, n) * int(255u - fog) + 255) >> 8;
      fogged |= uint(mixed) << uint(n * 8);
    }
    color = fogged;
  }
  uint colorTest = lookWord(look, LookColorTest) & 3u;
  if((flags & FlagColorTest) != 0u && colorTest != 1u) {
    uint mask = lookWord(look, LookColorMask);
    bool equal = (color & mask) == (lookWord(look, LookColorReference) & mask);
    if(colorTest == 0u || (colorTest == 2u) != equal) return;
  }
  bool depthPasses = true;
  if((flags & FlagDepthTest) != 0u) {
    depthPasses = passes(lookWord(look, LookDepthTest), int(z), int(readDepth(lane)));
  }
  if((flags & FlagStencilTest) != 0u) {
    uint test = lookWord(look, LookStencil), operations = lookWord(look, LookStencilOps);
    uint reference = test >> 8 & 0xffu, mask = test >> 16 & 0xffu;
    uint operation = operations >> 8 & 7u;
    bool dropped = false;
    if(!passes(test, int(reference & mask), int(stencil & mask))) {
      operation = operations & 7u, dropped = true;
    } else if(!depthPasses) {
      operation = operations >> 4 & 7u, dropped = true;
    }
    stencil = stencilOperation(operation, format, reference, stencil);
    if(dropped) {
      uint keep = writeMask | colorBits;
      writePixel(lane, (narrowPixel(stencil << 24, format) & ~keep) | (old & keep));
      return;
    }
  } else if(!depthPasses) {
    return;
  }
  if((flags & FlagDepthWrite) != 0u) writeDepth(lane, z);

  uint oldColor = (flags & (FlagBlend | FlagLogicOp)) != 0u ? widenPixel(old, format) : 0u;
  ivec3 rgb = ivec3(channel(color, 0), channel(color, 1), channel(color, 2));
  if((flags & FlagBlend) != 0u) {
    uint mode = lookWord(look, LookBlend);
    int destinationAlpha = int(oldColor >> 24);
    uint fixedA = lookWord(look, LookFixedA), fixedB = lookWord(look, LookFixedB);
    ivec3 sourceFactor = blendFactor(mode & 15u, oldColor, alpha, destinationAlpha, fixedA);
    ivec3 destinationFactor = blendFactor(mode >> 4 & 15u, color, alpha, destinationAlpha, fixedB);
    uint operation = mode >> 8 & 15u;
    for(int n = 0; n < 3; n++) {
      int s = channel(color, n), d = channel(oldColor, n);
      int sourceTerm = (s * 2 + 1) * (sourceFactor[n] * 2 + 1) >> 10;
      int destinationTerm = (d * 2 + 1) * (destinationFactor[n] * 2 + 1) >> 10;
      if(operation == 0u) rgb[n] = sourceTerm + destinationTerm;
      else if(operation == 1u) rgb[n] = sourceTerm - destinationTerm;
      else if(operation == 2u) rgb[n] = destinationTerm - sourceTerm;
      else if(operation == 3u) rgb[n] = min(s, d);
      else if(operation == 4u) rgb[n] = max(s, d);
      else if(operation == 5u) rgb[n] = abs(s - d);
      else rgb[n] = s;
    }
  }
  if((flags & FlagDither) != 0u) {
    uint row = uint(y & 3), column = uint(x & 3);
    uint raw = lookWord(look, LookDither + (row >> 1)) >> ((row & 1u) * 16u + column * 4u) & 15u;
    int dither = raw < 8u ? int(raw) : int(raw) - 16;
    rgb += ivec3(dither);
  }
  uint result = pack(rgb.r, rgb.g, rgb.b, 0) | stencil << 24;
  if((flags & FlagLogicOp) != 0u) {
    uint value = logicOperation(lookWord(look, LookLogic), result & 0x00ffffffu, oldColor & 0x00ffffffu);
    result = (value & 0x00ffffffu) | (result & 0xff000000u);
  }
  writePixel(lane, (narrowPixel(result, format) & ~writeMask) | (old & writeMask));
}
