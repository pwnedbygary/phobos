//Drawing. So far only clearing: in clear mode (CLEAR_MODE's bit 0, which the GU library's sceGuClear sets around a
//rectangle it draws over the screen), a sprite fills its rectangle with its color and depth, with no texture,
//blending or tests. CLEAR_MODE's bits 8, 9 and 10 say which of color, alpha (where the stencil lives) and depth it
//writes. Drawing anything else comes in the next part.
//
//The frame buffer: in VRAM (the GE draws nowhere else, whatever the address's top bits), FRAME_BUFFER_WIDTH pixels to
//a row, in FRAMEBUF_PIX_FORMAT's format: 0 5650, 1 5551, 2 4444 (16 bits a pixel; the last two keep alpha in their
//top bits), 3 8888. The depth buffer, 16 bits a pixel, is laid out the same way. (The addresses' and widths' masks are
//as PPSSPP has them, from tests on the PSP.)

auto GE::primitive(u32 kind, u32 count) -> void {
  auto format = vertexFormat();
  std::vector<Vertex> vertices;
  vertices.reserve(count);
  for(u32 n = 0; n < count; n++) {
    u32 index = format.indexFormat ? readIndex(n, format) : n;
    vertices.push_back(readVertex(vertexAddress + index * format.size, format));
  }
  //the next PRIM carries on where this one stopped: after its indices if it had them, else after its vertices
  if(format.indexFormat) indexAddress += count * (format.indexFormat == 2 ? 2 : 1);
  else vertexAddress += count * format.size;

  if(!(commands[ClearMode] & 1)) return note("drawing isn't emulated yet, only clearing");
  if(kind != Sprites) return note("clear mode with primitives other than sprites isn't emulated yet");
  if(!format.through) return note("clear mode in 3D (not through mode) isn't emulated yet");
  for(u32 n = 0; n + 1 < count; n += 2) clearRectangle(vertices[n], vertices[n + 1]);
}

auto GE::frameBufferAddress() const -> u32 { return Memory::VRAMBase | (commands[FrameBufferPointer] & 0x1f'fff0); }
auto GE::depthBufferAddress() const -> u32 { return Memory::VRAMBase | (commands[DepthBufferPointer] & 0x1f'fff0); }

//An 8888 color (red in the low byte) in a frame buffer format, each channel keeping its top bits.
static auto narrow(u32 color, u32 format) -> u32 {
  u32 r = color & 0xff, g = color >> 8 & 0xff, b = color >> 16 & 0xff, a = color >> 24;
  if(format == 0) return r >> 3 | g >> 2 << 5 | b >> 3 << 11;
  if(format == 1) return r >> 3 | g >> 3 << 5 | b >> 3 << 10 | a >> 7 << 15;
  if(format == 2) return r >> 4 | g >> 4 << 4 | b >> 4 << 8 | a >> 4 << 12;
  return color;
}

//Which of a pixel's bits are alpha in each format (5650 has none).
static auto alphaBits(u32 format) -> u32 {
  static constexpr u32 bits[4] = {0, 0x8000, 0xf000, 0xff00'0000};
  return bits[format];
}

//A screen position in pixels as the GE holds it, with four fraction bits (12.4 fixed point): a pixel's top-left
//corner is a multiple of 16. Wild values (from garbage vertices) are held to the GE's range.
static auto fixed(float position) -> s32 {
  if(!(position >= -4096 && position <= 4096)) position = position > 0 ? 4096 : -4096;
  return s32(position * 16);
}

//Writes value's bits under mask to count pixels of bytes each from address, a row at a time where the row is in one
//piece of memory, and reports the change.
static auto fillPixels(Memory& memory, u32 address, u32 count, u32 bytes, u32 value, u32 mask) -> void {
  if(u8* row = memory.pointer(address, count * bytes)) {
    for(u32 n = 0; n < count; n++, row += bytes) {
      u32 pixel = 0;
      std::memcpy(&pixel, row, bytes);
      pixel = (pixel & ~mask) | (value & mask);
      std::memcpy(row, &pixel, bytes);
    }
    memory.changed(address, count * bytes);
    return;
  }
  for(u32 n = 0; n < count; n++) {  //across the end of a piece of memory: a pixel at a time
    u32 at = address + n * bytes;
    memory.write(bytes, at, (memory.read(bytes, at) & ~mask) | (value & mask));
  }
}

//A rectangle in clear mode between two vertices' positions (corners in either order), in the second vertex's color
//and depth. A pixel is filled if its top-left corner is inside, from the smaller edge up to but not including the
//larger, so (0,0)-(480,272) fills 480x272 pixels; only those inside the scissor rectangle and the drawing region are
//touched.
auto GE::clearRectangle(const Vertex& from, const Vertex& to) -> void {
  s32 left   = (fixed(std::min(from.x, to.x)) + 15) >> 4, right  = (fixed(std::max(from.x, to.x)) + 15) >> 4;
  s32 top    = (fixed(std::min(from.y, to.y)) + 15) >> 4, bottom = (fixed(std::max(from.y, to.y)) + 15) >> 4;
  u32 scissor1 = commands[Scissor1], scissor2 = commands[Scissor2];
  u32 region1 = commands[Region1], region2 = commands[Region2];
  left   = std::max({left, s32(scissor1 & 0x3ff), s32(region1 & 0x3ff)});
  top    = std::max({top, s32(scissor1 >> 10 & 0x3ff), s32(region1 >> 10 & 0x3ff)});
  right  = std::min({right, s32(scissor2 & 0x3ff) + 1, s32(region2 & 0x3ff) + 1});
  bottom = std::min({bottom, s32(scissor2 >> 10 & 0x3ff) + 1, s32(region2 >> 10 & 0x3ff) + 1});
  if(left >= right || top >= bottom) return;

  u32 mode = commands[ClearMode], format = commands[FrameBufferPixelFormat] & 3;
  u32 bytes = format == 3 ? 4 : 2, alpha = alphaBits(format), all = format == 3 ? 0xffff'ffff : 0xffff;
  u32 mask = ((mode & 0x100 ? all & ~alpha : 0) | (mode & 0x200 ? alpha : 0));
  u32 color = narrow(to.color, format);
  u32 stride = commands[FrameBufferWidth] & 0x7fc, depthStride = commands[DepthBufferWidth] & 0x7fc;
  u32 depth = u32(std::clamp(to.z, 0.0f, 65535.0f));
  for(s32 y = top; y < bottom; y++) {
    if(mask) fillPixels(memory, frameBufferAddress() + (y * stride + left) * bytes, right - left, bytes, color, mask);
    if(mode & 0x400) fillPixels(memory, depthBufferAddress() + (y * depthStride + left) * 2, right - left, 2, depth, 0xffff);
  }
}
