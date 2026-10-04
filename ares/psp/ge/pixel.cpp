//The pixel pipeline: what happens to each pixel a primitive covers, once its color is worked out (texture and all).
//In order:
//  1. the alpha test: the pixel's alpha against a reference, both masked (ALPHA_TEST: bits 0-2 the comparison, 8-15
//     the reference, 16-23 the mask); failing, the pixel is dropped
//  2. the color test: its color against a reference, both masked: equal or not (COLOR_TEST, _REFERENCE, _TEST_MASK)
//  3. the stencil test: the stencil, kept in the frame buffer's alpha bits, against a reference, both masked
//     (STENCIL_TEST); failing, the stencil is changed as STENCIL_OPERATION's "fail" says (bits 0-2) and the pixel
//     dropped
//  4. the depth test: the pixel's depth against the depth buffer's (DEPTH_TEST); failing, the stencil changes as
//     "depth fail" says (bits 8-10) and the pixel is dropped; passing, as "pass" says (bits 16-18), and the depth is
//     written unless DEPTH_MASK says not to (only with the test on)
//  5. blending with the frame buffer's color (BLEND_MODE: bits 0-3 the source's factor, 4-7 the destination's, 8-10
//     the operation; BLEND_FIXED_A and _B the fixed factors)
//  6. dithering: a value from -8 to 7, from a 4x4 matrix (DITHER0-3) by the pixel's position, added before the color
//     is held to 0-255
//  7. a logic operation with the frame buffer's color (LOGIC_OP), bitwise; the stencil isn't touched
//  8. the write, leaving the bits MASK_COLOR and MASK_ALPHA set alone
//The alpha written is the stencil: as it was, or as the stencil test made it. A 5650 frame buffer has none (it reads
//as 0); a 5551 one has one bit (reads 0 or 255); 4444 four bits; 8888 eight.
//Clear mode skips all of it: color, alpha (the stencil) and depth are written as CLEAR_MODE says.
//
//Comparisons (pspgu.h's GU_NEVER and on): 0 never, 1 always, 2 equal, 3 not equal, 4 less, 5 less or equal,
//6 greater, 7 greater or equal; the color test has only the first four. Stencil operations: 0 keep, 1 zero, 2 replace
//(with the reference, unmasked), 3 invert, 4 increment, 5 decrement, both stopping at the format's ends.
//(The order, and each step's arithmetic, are as PPSSPP's software renderer has them, which its authors checked
//against tests on the PSP.)

static auto passes(u32 comparison, s32 a, s32 b) -> bool {
  switch(comparison & 7) {
  case 0: return false;
  case 1: return true;
  case 2: return a == b;
  case 3: return a != b;
  case 4: return a < b;
  case 5: return a <= b;
  case 6: return a > b;
  }
  return a >= b;
}

//An 8888 color in a frame buffer format (0 5650, 1 5551, 2 4444, 3 8888), each channel keeping its top bits; and
//back, each widened by repeating its top bits.
static auto narrowPixel(u32 color, u32 format) -> u32 {
  u32 r = color & 0xff, g = color >> 8 & 0xff, b = color >> 16 & 0xff, a = color >> 24;
  if(format == 0) return r >> 3 | g >> 2 << 5 | b >> 3 << 11;
  if(format == 1) return r >> 3 | g >> 3 << 5 | b >> 3 << 10 | a >> 7 << 15;
  if(format == 2) return r >> 4 | g >> 4 << 4 | b >> 4 << 8 | a >> 4 << 12;
  return color;
}
static auto widenPixel(u32 pixel, u32 format) -> u32 {
  if(format == 3) return pixel;
  return widen16(pixel, format) & (format == 0 ? 0x00ff'ffff : 0xffff'ffff);  //5650's alpha reads as 0
}

static auto stencilOperation(u32 operation, u32 format, u32 reference, u32 stencil) -> u32 {
  switch(operation & 7) {
  case 0: return stencil;
  case 1: return 0;
  case 2: return reference;
  case 3: return ~stencil & 0xff;
  case 4:
    if(format == 1) return 0xff;
    if(format == 2) return stencil < 0xf0 ? stencil + 0x10 : stencil;
    return stencil < 0xff ? stencil + 1 : stencil;
  case 5:
    if(format == 1) return 0;
    if(format == 2) return stencil >= 0x10 ? stencil - 0x10 : stencil;
    return stencil ? stencil - 1 : 0;
  }
  return stencil;
}

auto GE::pixelState() const -> PixelState {
  PixelState p{};
  u32 mode = commands[ClearMode];
  p.clear = mode & 1;
  p.clearColor = mode >> 8 & 1, p.clearAlpha = mode >> 9 & 1, p.clearDepth = mode >> 10 & 1;
  p.frameBuffer = commands[FrameBufferPointer] & 0x1f'fff0;  //in VRAM, whatever the top bits
  p.stride = commands[FrameBufferWidth] & 0x7fc;
  p.format = commands[FrameBufferPixelFormat] & 3;
  p.depthBuffer = commands[DepthBufferPointer] & 0x1f'fff0;
  p.depthStride = commands[DepthBufferWidth] & 0x7fc;
  p.alphaTest = commands[AlphaTestEnable] & 1;
  p.colorTest = commands[ColorTestEnable] & 1;
  p.stencilTest = commands[StencilTestEnable] & 1;
  p.depthTest = commands[DepthTestEnable] & 1;
  p.blend = commands[AlphaBlendEnable] & 1;
  p.dither = commands[DitherEnable] & 1;
  p.logicOp = commands[LogicOpEnable] & 1;
  p.depthWrite = p.depthTest && !(commands[DepthMask] & 1);
  p.alphaFunction = commands[AlphaTest] & 7;
  p.alphaReference = commands[AlphaTest] >> 8 & 0xff;
  p.alphaMask = commands[AlphaTest] >> 16 & 0xff;
  p.colorFunction = commands[ColorTest] & 3;
  p.colorReference = commands[ColorReference] & 0xff'ffff;
  p.colorMask = commands[ColorTestMask] & 0xff'ffff;
  p.stencilFunction = commands[StencilTest] & 7;
  p.stencilReference = commands[StencilTest] >> 8 & 0xff;
  p.stencilMask = commands[StencilTest] >> 16 & 0xff;
  p.stencilFail = commands[StencilOperation] & 7;
  p.stencilDepthFail = commands[StencilOperation] >> 8 & 7;
  p.stencilPass = commands[StencilOperation] >> 16 & 7;
  p.depthFunction = commands[DepthTest] & 7;
  p.blendSource = commands[BlendMode] & 0xf;
  p.blendDestination = commands[BlendMode] >> 4 & 0xf;
  p.blendOperation = commands[BlendMode] >> 8 & 0xf;
  p.fixedA = commands[BlendFixedA] & 0xff'ffff;
  p.fixedB = commands[BlendFixedB] & 0xff'ffff;
  for(u32 row = 0; row < 4; row++) {
    for(u32 column = 0; column < 4; column++) {
      u32 raw = commands[Dither0 + row] >> column * 4 & 15;
      p.ditherMatrix[row * 4 + column] = raw < 8 ? s32(raw) : s32(raw) - 16;
    }
  }
  p.logic = commands[LogicOp] & 0xf;
  p.writeMask = narrowPixel((commands[MaskColor] & 0xff'ffff) | (commands[MaskAlpha] & 0xff) << 24, p.format);
  u32 scissor1 = commands[Scissor1], scissor2 = commands[Scissor2];
  u32 region1 = commands[Region1], region2 = commands[Region2];
  p.left   = std::max(scissor1 & 0x3ff, region1 & 0x3ff);
  p.top    = std::max(scissor1 >> 10 & 0x3ff, region1 >> 10 & 0x3ff);
  p.right  = std::min(scissor2 & 0x3ff, region2 & 0x3ff);
  p.bottom = std::min(scissor2 >> 10 & 0x3ff, region2 >> 10 & 0x3ff);
  p.low = ~0u, p.high = 0;
  return p;
}

//A pixel at (x, y), inside the scissor rectangle, with depth z and color (8888, each channel 0-255).
auto GE::drawPixel(PixelState& p, s32 x, s32 y, u32 z, u32 color) -> void {
  u32 bytes = p.format == 3 ? 4 : 2;
  //Both offsets wrap within VRAM and stay multiples of their pixel's size (the buffers start on 16 bytes, VRAM's size
  //is a power of two), so no pixel runs past VRAM's end.
  u32 at = (p.frameBuffer + (y * p.stride + x) * bytes) & (Memory::VRAMSize - 1);
  u32 depthAt = (p.depthBuffer + (y * p.depthStride + x) * 2) & (Memory::VRAMSize - 1);
  u8* vram = memory.vram.data();
  u32 old = 0, oldDepth = vram[depthAt] | vram[depthAt + 1] << 8;
  std::memcpy(&old, vram + at, bytes);
  u32 oldColor = widenPixel(old, p.format);
  auto touch = [&](u32 offset) { p.low = std::min(p.low, offset); p.high = std::max(p.high, offset); };
  auto writeDepth = [&] {
    vram[depthAt] = z, vram[depthAt + 1] = z >> 8;
    touch(depthAt);
  };
  auto write = [&](u32 value, u32 keep) {  //keep: bits of the old pixel that stay
    u32 pixel = (narrowPixel(value, p.format) & ~keep) | (old & keep);
    std::memcpy(vram + at, &pixel, bytes);
    touch(at);
  };
  u32 colorBits = p.format == 0 ? 0xffff : p.format == 1 ? 0x7fff : p.format == 2 ? 0x0fff : 0x00ff'ffff;
  u32 stencilBits = (p.format == 3 ? 0xffff'ffff : 0xffff) & ~colorBits;
  u32 stencil = oldColor >> 24;

  if(p.clear) {  //the vertex's alpha goes in as the stencil
    if(p.clearDepth) writeDepth();
    return write(color, p.writeMask | (p.clearColor ? 0 : colorBits) | (p.clearAlpha ? 0 : stencilBits));
  }
  s32 alpha = color >> 24;
  if(p.alphaTest && !passes(p.alphaFunction, alpha & p.alphaMask, p.alphaReference & p.alphaMask)) return;
  if(p.colorTest && p.colorFunction != 1) {
    bool equal = (color & p.colorMask) == (p.colorReference & p.colorMask);
    if(p.colorFunction == 0 || (p.colorFunction == 2) != equal) return;
  }
  bool depthPasses = !p.depthTest || passes(p.depthFunction, z, oldDepth);
  if(p.stencilTest) {
    u32 reference = p.stencilReference;
    u32 operation = p.stencilPass;
    bool drop = false;
    if(!passes(p.stencilFunction, reference & p.stencilMask, stencil & p.stencilMask)) operation = p.stencilFail, drop = true;
    else if(!depthPasses) operation = p.stencilDepthFail, drop = true;
    stencil = stencilOperation(operation, p.format, reference, stencil);
    if(drop) return write(stencil << 24, p.writeMask | colorBits);
  } else if(!depthPasses) {
    return;
  }
  if(p.depthWrite) writeDepth();

  s32 rgb[3];
  for(u32 n = 0; n < 3; n++) rgb[n] = channel(color, n);
  if(p.blend) {
    s32 sourceAlpha = alpha, destinationAlpha = oldColor >> 24;
    auto factor = [&](u32 which, u32 n, bool source) -> s32 {
      switch(which) {
      case 0: return channel(source ? oldColor : color, n);        //the other's color
      case 1: return 255 - channel(source ? oldColor : color, n);
      case 2: return sourceAlpha;
      case 3: return 255 - sourceAlpha;
      case 4: return destinationAlpha;
      case 5: return 255 - destinationAlpha;
      case 6: return 2 * sourceAlpha;
      case 7: return 255 - std::min(2 * sourceAlpha, 255);
      case 8: return 2 * destinationAlpha;
      case 9: return 255 - std::min(2 * destinationAlpha, 255);
      }
      return channel(source ? p.fixedA : p.fixedB, n);  //10-15: fixed
    };
    for(u32 n = 0; n < 3; n++) {
      s32 s = channel(color, n), d = channel(oldColor, n);
      s32 sourceTerm = (s * 2 + 1) * (factor(p.blendSource, n, true) * 2 + 1) / 1024;
      s32 destinationTerm = (d * 2 + 1) * (factor(p.blendDestination, n, false) * 2 + 1) / 1024;
      switch(p.blendOperation) {
      case 0:  rgb[n] = sourceTerm + destinationTerm; break;
      case 1:  rgb[n] = sourceTerm - destinationTerm; break;
      case 2:  rgb[n] = destinationTerm - sourceTerm; break;
      case 3:  rgb[n] = std::min(s, d); break;
      case 4:  rgb[n] = std::max(s, d); break;
      case 5:  rgb[n] = std::abs(s - d); break;
      default: rgb[n] = s; break;
      }
    }
  }
  if(p.dither) {
    for(u32 n = 0; n < 3; n++) rgb[n] += p.ditherMatrix[(y & 3) * 4 + (x & 3)];
  }
  u32 result = pack(rgb[0], rgb[1], rgb[2], 0) | stencil << 24;
  if(p.logicOp) {
    u32 s = result & 0x00ff'ffff, d = oldColor & 0x00ff'ffff, value = s;
    switch(p.logic) {
    case 0:  value = 0; break;
    case 1:  value = s & d; break;
    case 2:  value = s & ~d; break;
    case 3:  value = s; break;
    case 4:  value = ~s & d; break;
    case 5:  value = d; break;
    case 6:  value = s ^ d; break;
    case 7:  value = s | d; break;
    case 8:  value = ~(s | d); break;
    case 9:  value = ~(s ^ d); break;
    case 10: value = ~d; break;
    case 11: value = s | ~d; break;
    case 12: value = ~s; break;
    case 13: value = ~s | d; break;
    case 14: value = ~(s & d); break;
    case 15: value = 0xff'ffff; break;
    }
    result = (value & 0x00ff'ffff) | (result & 0xff00'0000);
  }
  write(result, p.writeMask);
}
