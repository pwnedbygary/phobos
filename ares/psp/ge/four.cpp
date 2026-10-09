//Drawing four pixels at a time: raster.cpp draws a triangle's or sprite's rows this way when submit() lets it
//(fours).
//
//A row is drawn in fours: four pixels side by side, from a column that's a multiple of four, each in a lane of the
//host's SIMD vectors (GCC's and Clang's own vector types, which become NEON on ARM64 and SSE on x86-64). Each lane
//does exactly what drawing its pixel by itself does (raster.cpp, texture.cpp, pixel.cpp), so that every pixel
//comes out the same, bit for bit:
//  - Whole numbers: the same additions, multiplications, shifts and comparisons, lane by lane, in 32 bits, none of
//    which overflows (where one might, it says why not), but for a triangle's stepped colors, fog and depth, which
//    run round on purpose and still leave every lane inside the triangle its value (triangleFours()).
//  - Floating point: the same expressions, written the same way, with a vector in every product, so that each lane
//    rounds as the single number did. Where the host has fused multiply-adds (ARM64), the compiler fuses a product
//    into the sum it's part of by the expression's shape, for vectors as for single numbers; a product of two single
//    numbers, made a vector afterwards, wouldn't be fused, so none is written that way.
//  - The order: no pixel of a primitive depends on another (raster.cpp) but through the frame and depth buffers it's
//    drawn into. Four pixels read and written at once see what drawing them one after another would, as long as the
//    job's frame buffer and depth buffer don't overlap, so that each pixel's bytes are its own, and its texture isn't
//    read from memory as it draws (texture.cpp): fourFriendly() sees to both.
//  - The depth test comes before the texture. Without the stencil test (which isn't drawn here), failing any test
//    only drops the pixel, and the depth test needs only the pixel's depth: testing it first drops the same pixels,
//    before their texels are looked up for nothing.
//A row's first and last fours may have lanes outside it. They're worked out and dropped (their texels taken from the
//texture's first), and their bytes of the frame and depth buffers are neither read nor written: where a frame buffer
//is barely wider than the area drawn, they can be another row's pixels, which another thread may be drawing. A four
//wholly inside its row reads and writes its four pixels at once, those dropped with the very bytes they had (its own
//pixels, in its own row, which only this thread draws meanwhile); one that isn't, each lane inside it by itself.
//What isn't drawn here (points, lines, the stencil test, logic operations, the jobs fourFriendly() turns down) is
//drawn a pixel at a time, as before.

static constexpr GE::s32x4 Lanes = {0, 1, 2, 3};

static alwaysinline auto splatLanes(s32 value) -> GE::s32x4 { return GE::s32x4{} + value; }
static alwaysinline auto anyLane(GE::s32x4 mask) -> bool {
  u64 halves[2];
  std::memcpy(halves, &mask, sizeof(halves));
  return (halves[0] | halves[1]) != 0;
}
//Lane by lane, yes where the mask's lane is set (all bits), else no.
static alwaysinline auto pickLanes(GE::s32x4 mask, GE::s32x4 yes, GE::s32x4 no) -> GE::s32x4 {
  return (yes & mask) | (no & ~mask);
}
static alwaysinline auto pickLanes(GE::s32x4 mask, GE::f32x4 yes, GE::f32x4 no) -> GE::f32x4 {
  return (GE::f32x4)pickLanes(mask, (GE::s32x4)yes, (GE::s32x4)no);
}
//std::clamp() of each lane (whole numbers).
static alwaysinline auto heldLanes(GE::s32x4 value, s32 low, s32 high) -> GE::s32x4 {
  value = pickLanes(value < low, splatLanes(low), value);
  return pickLanes(value > high, splatLanes(high), value);
}
//std::clamp() of each lane, as it treats floats: (value < low) ? low : (high < value) ? high : value, so that a lane
//that isn't a number stays one.
static alwaysinline auto heldLanes(GE::f32x4 value, float low, float high) -> GE::f32x4 {
  value = pickLanes(value < low, GE::f32x4{} + low, value);
  return pickLanes(value > high, GE::f32x4{} + high, value);
}
//s32(std::floor(value)) of each lane, for values within 2^24 of zero (texelAxis() holds them there). Cut toward zero,
//then one less where that went up: the conversions are exact there, so this is the floor.
static alwaysinline auto floorLanes(GE::f32x4 value) -> GE::s32x4 {
  GE::s32x4 cut = __builtin_convertvector(value, GE::s32x4);
  return cut + (__builtin_convertvector(cut, GE::f32x4) > value);  //(a comparison that holds is -1)
}
//passes() (pixel.cpp) lane by lane.
static alwaysinline auto passesLanes(u32 comparison, GE::s32x4 a, GE::s32x4 b) -> GE::s32x4 {
  switch(comparison & 7) {
  case 0: return GE::s32x4{};
  case 1: return splatLanes(-1);
  case 2: return a == b;
  case 3: return a != b;
  case 4: return a < b;
  case 5: return a <= b;
  case 6: return a > b;
  }
  return a >= b;
}
//Whether the job may be drawn four pixels at a time (see the top of this file): a triangle or a sprite, without the
//stencil test or a logic operation, its texture (if any) kept decoded, its frame buffer's bytes apart from its depth
//buffer's (when it reaches it) and neither running round VRAM's end; and for a triangle, its edge functions within
//32 bits at every pixel its fours may reach (three columns either side of its box).
auto GE::fourFriendly(const Job& job) const -> bool {
  if(!fourPixels || (job.kind != Job::Kind::Sprite && job.kind != Job::Kind::Triangle)) return false;
  auto& look = *job.look;
  auto& p = look.pixel;
  if(!p.clear && (p.stencilTest || p.logicOp)) return false;
  if(look.textured && !look.texture.decoded) return false;
  u32 bytes = p.format == 3 ? 4 : 2;
  u64 colorLow = p.frameBuffer + (u64(job.firstY) * p.stride + job.firstX) * bytes;
  u64 colorHigh = p.frameBuffer + (u64(job.lastY) * p.stride + job.lastX) * bytes + bytes - 1;
  if(colorHigh >= Memory::VRAMSize) return false;
  if(p.clear ? p.clearDepth : p.depthTest) {
    u64 depthLow = p.depthBuffer + (u64(job.firstY) * p.depthStride + job.firstX) * 2;
    u64 depthHigh = p.depthBuffer + (u64(job.lastY) * p.depthStride + job.lastX) * 2 + 1;
    if(depthHigh >= Memory::VRAMSize) return false;
    depthLow &= ~0x3fffull, depthHigh |= 0x3fff;  //(each 16 KiB rearranged: memory.hpp)
    if(depthLow <= colorHigh && colorLow <= depthHigh) return false;
  }
  if(job.kind == Job::Kind::Triangle) {
    auto& r = job.triangle;
    s64 farX = std::max(std::abs(s64(job.firstX - 3) * 16 + 8), std::abs(s64(job.lastX + 3) * 16 + 8));
    s64 farY = std::max(std::abs(s64(job.firstY) * 16 + 8), std::abs(s64(job.lastY) * 16 + 8));
    for(u32 k = 0; k < 3; k++) {
      u32 from = (k + 1) % 3, to = (k + 2) % 3;
      s64 a = r.y[from] - r.y[to], b = r.x[to] - r.x[from], c = r.y[to] * r.x[from] - r.x[to] * r.y[from];
      if(std::abs(a) * farX + std::abs(b) * farY + std::abs(c) + std::abs(a) * 64 >= 0x7fff'ffff) return false;
    }
    //Its stepped values (triangleFours()) at its pixels within 32 bits: steps under 2^24 a sixteenth, which their
    //rounding (under 1 + 2^9 each, the reciprocal kept to 16 bits) can take at most 2^28 from the true blend over
    //the 2^17 sixteenths a triangle spans each way, beside a depth's 2^30. (Only those its pixels use.)
    bool needsZ = p.depthRange || (p.clear ? p.clearDepth : p.depthTest);
    auto small = [](const Job::Stepped& c) { return std::abs(c.across) < 1 << 24 && std::abs(c.down) < 1 << 24; };
    for(u32 n = 0; n < 4; n++) if(!r.flat && !small(r.colors[n])) return false;
    for(u32 n = 0; n < 3; n++) if(!r.flat && r.shines && !small(r.shine[n])) return false;
    if((p.fog && !small(r.fog)) || (needsZ && !small(r.depth))) return false;
  }
  return true;
}

//The depth range test, and the depth test, of four pixels whose depths are worked out (see the top of this file): the
//lanes failing them are dropped. Returns whether any lane is left. (The depth buffer's four values, read here, stay
//in four.depth for the write.)
template<u32 Format>
alwaysinline auto GE::depthFirst(const PixelState& p, s32 x, s32 y, Four& four) -> bool {
  if(p.depthRange) four.live &= (four.z >= s32(p.minDepth)) & (four.z <= s32(p.maxDepth));
  if(p.clear ? p.clearDepth : p.depthTest) {
    //The four depths are side by side even as the GE rearranges its depth buffer: the four's first byte is a
    //multiple of 8 (the buffer starts on 16 bytes, its rows are multiples of 4 pixels long, the four starts on a
    //multiple of 4), and the rearrangement leaves an offset's low 5 bits as they are (memory.hpp).
    u32 at = Memory::vramOffset(3, (p.depthBuffer + (y * p.depthStride + x) * 2) & (Memory::VRAMSize - 1));
    u16 depths[4] = {};
    if(four.full) std::memcpy(depths, memory.vram.data() + at, sizeof(depths));
    else for(u32 n = 0; n < 4; n++) if(four.inside[n]) std::memcpy(&depths[n], memory.vram.data() + at + n * 2, 2);
    four.depth = GE::s32x4{depths[0], depths[1], depths[2], depths[3]};
    if(!p.clear) four.live &= passesLanes(p.depthFunction, four.z, four.depth);
  }
  return anyLane(four.live);
}

//texelAxis() (texture.cpp) lane by lane: the texel(s) each lane's coordinate is in, inside the texture, and the
//fraction between them (filtered).
static alwaysinline auto texelAxisLanes(GE::f32x4 coordinate, u32 size, bool clamp, bool linear, GE::s32x4 (&axis)[3])
  -> void {
  s32 last = std::min<s32>(size, 512) - 1;
  auto inside = [&](GE::s32x4 c) { return clamp ? heldLanes(c, 0, last) : c & last; };
  GE::f32x4 held = pickLanes(coordinate != coordinate, GE::f32x4{}, heldLanes(coordinate, -65536.0f, 65536.0f));
  if(!linear) {
    axis[0] = inside(floorLanes(held)), axis[1] = axis[2] = GE::s32x4{};
    return;
  }
  GE::s32x4 base = floorLanes(held * 256) - 128;
  axis[0] = inside(base >> 8), axis[1] = inside((base >> 8) + 1), axis[2] = base >> 4 & 15;
}

//The texels at four pixels' texel axes (u and v: first, second, fraction), filtered or not, as sampleWith() takes
//them, as four lanes each of red, green, blue and alpha. The texture is kept decoded (fourFriendly()); lanes not
//live take the texture's first texel, as theirs may lie past the rows kept.
alwaysinline auto GE::texelsFour(const Look& look, bool linear, s32x4 live, const s32x4 (&u)[3], const s32x4 (&v)[3],
                                 s32x4 (&texel)[4]) const -> void {
  auto& t = look.texture;
  s32 width = t.decodedWidth;
  auto gather = [&](s32x4 x, s32x4 y) -> u32x4 {
    s32x4 at = pickLanes(live, y * width + x, s32x4{});
    return u32x4{t.decoded[at[0]], t.decoded[at[1]], t.decoded[at[2]], t.decoded[at[3]]};
  };
  if(!linear) {
    u32x4 texels = gather(u[0], v[0]);
    for(u32 n = 0; n < 4; n++) texel[n] = s32x4(texels >> n * 8 & 0xff);
    return;
  }
  u32x4 topLeft, topRight, bottomLeft, bottomRight;
  if(width >= 2 && !anyLane(live & (u[1] != u[0] + 1))) {
    //Each right texel just after its left one (not repeated round, nor held at the edge): the two read at once.
    //(Lanes not live read the first two texels, which a texture at least two wide has.)
    s32x4 top = pickLanes(live, v[0] * width + u[0], s32x4{}), bottom = pickLanes(live, v[1] * width + u[0], s32x4{});
    u64 upper[4], lower[4];
    for(u32 n = 0; n < 4; n++) {
      std::memcpy(&upper[n], t.decoded + top[n], 8);
      std::memcpy(&lower[n], t.decoded + bottom[n], 8);
    }
    topLeft = u32x4{u32(upper[0]), u32(upper[1]), u32(upper[2]), u32(upper[3])};
    topRight = u32x4{u32(upper[0] >> 32), u32(upper[1] >> 32), u32(upper[2] >> 32), u32(upper[3] >> 32)};
    bottomLeft = u32x4{u32(lower[0]), u32(lower[1]), u32(lower[2]), u32(lower[3])};
    bottomRight = u32x4{u32(lower[0] >> 32), u32(lower[1] >> 32), u32(lower[2] >> 32), u32(lower[3] >> 32)};
  } else {
    topLeft = gather(u[0], v[0]), topRight = gather(u[1], v[0]);
    bottomLeft = gather(u[0], v[1]), bottomRight = gather(u[1], v[1]);
  }
  //filtered(): the top two blended across, then the bottom two, then those two down, each step dropping its
  //fraction; two channels side by side in each lane's 32 bits (red and blue, then green and alpha), 16 bits each, as
  //filtered() has four in 64: a channel times sixteenths stays below 4096, so none spills into the next, and each
  //shift's spill from the next is masked off.
  constexpr u32 Two = 0x00ff'00ff;
  u32x4 across = u32x4(u[2]), down = u32x4(v[2]);
  auto blend = [&](u32x4 topLeft, u32x4 topRight, u32x4 bottomLeft, u32x4 bottomRight) -> u32x4 {
    u32x4 upper = (topLeft * (16 - across) + topRight * across) >> 4 & Two;
    u32x4 lower = (bottomLeft * (16 - across) + bottomRight * across) >> 4 & Two;
    return (upper * (16 - down) + lower * down) >> 4 & Two;
  };
  u32x4 redBlue = blend(topLeft & Two, topRight & Two, bottomLeft & Two, bottomRight & Two);
  u32x4 greenAlpha = blend(topLeft >> 8 & Two, topRight >> 8 & Two, bottomLeft >> 8 & Two, bottomRight >> 8 & Two);
  texel[0] = s32x4(redBlue & 0xff), texel[1] = s32x4(greenAlpha & 0xff);
  texel[2] = s32x4(redBlue >> 16), texel[3] = s32x4(greenAlpha >> 16);
}

//The texture function (textureFunctionWith(), texture.cpp) lane by lane, on the pixels' colors and their texels,
//each channel held to 0-255 at the end as pack() holds it. (Every sum stays below 2^18.)
alwaysinline auto GE::combineFour(const Look& look, s32x4 (&color)[4], const s32x4 (&texel)[4]) const -> void {
  s32x4 fragmentAlpha = color[3], texelAlpha = texel[3];
  s32x4 alpha = look.withAlpha ? (fragmentAlpha + 1) * texelAlpha >> 8 : fragmentAlpha;
  s32 twice = look.doubled ? 2 : 1, down = look.doubled ? 7 : 8;
  s32x4 out[3];
  switch(look.function) {
  case 0:
    for(u32 n = 0; n < 3; n++) out[n] = (color[n] + 1) * texel[n] * twice >> 8;
    break;
  case 1:
    for(u32 n = 0; n < 3; n++) {
      out[n] = look.withAlpha ? ((color[n] + 1) * (255 - texelAlpha) + (texel[n] + 1) * texelAlpha) >> down
                              : texel[n] * twice;
    }
    alpha = fragmentAlpha;
    break;
  case 2:
    for(u32 n = 0; n < 3; n++) {
      out[n] = ((255 - texel[n]) * color[n] + texel[n] * s32(channel(look.environment, n)) + 255) >> down;
    }
    break;
  case 3:
    for(u32 n = 0; n < 3; n++) out[n] = texel[n] * twice;
    alpha = look.withAlpha ? texelAlpha : fragmentAlpha;
    break;
  default:
    for(u32 n = 0; n < 3; n++) out[n] = (color[n] + texel[n]) * twice;
    break;
  }
  for(u32 n = 0; n < 3; n++) color[n] = heldLanes(out[n], 0, 255);
  color[3] = heldLanes(alpha, 0, 255);
}

//The pixel pipeline (drawPixelAs(), pixel.cpp) for four pixels whose color, depth and fog are worked out, and which
//depthFirst() has seen: the alpha test, fog, the color test, the depth written, blending, dithering, and the frame
//buffer written, lane by lane. All four lanes are written at once, those dropped with the very bytes they had.
template<u32 Format>
alwaysinline auto GE::pixelsFour(const PixelState& p, s32 x, s32 y, Four& four) -> void {
  constexpr u32 bytes = Format == 3 ? 4 : 2;
  constexpr u32 colorBits = Format == 0 ? 0xffff : Format == 1 ? 0x7fff : Format == 2 ? 0x0fff : 0x00ff'ffff;
  constexpr u32 stencilBits = (Format == 3 ? 0xffff'ffff : 0xffff) & ~colorBits;
  u8* vram = memory.vram.data();
  //(the four's bytes are side by side and on a multiple of their size: see depthFirst())
  u32 at = (p.frameBuffer + (y * p.stride + x) * bytes) & (Memory::VRAMSize - 1);
  u32x4 old;
  if(four.full && bytes == 4) {
    std::memcpy(&old, vram + at, 16);
  } else if(four.full) {
    u16 halves[4];
    std::memcpy(halves, vram + at, 8);
    old = u32x4{halves[0], halves[1], halves[2], halves[3]};
  } else {
    u32 pixels[4] = {};
    for(u32 n = 0; n < 4; n++) {
      if(!four.inside[n]) continue;
      if constexpr(bytes == 4) std::memcpy(&pixels[n], vram + at + n * 4, 4);
      else { u16 half; std::memcpy(&half, vram + at + n * 2, 2); pixels[n] = half; }
    }
    old = u32x4{pixels[0], pixels[1], pixels[2], pixels[3]};
  }
  auto& color = four.color;
  auto writeDepth = [&] {
    u32 offset = Memory::vramOffset(3, (p.depthBuffer + (y * p.depthStride + x) * 2) & (Memory::VRAMSize - 1));
    s32x4 depth = pickLanes(four.live, four.z, four.depth);
    u16 depths[4] = {u16(depth[0]), u16(depth[1]), u16(depth[2]), u16(depth[3])};
    if(four.full) std::memcpy(vram + offset, depths, sizeof(depths));
    else for(u32 n = 0; n < 4; n++) if(four.live[n]) std::memcpy(vram + offset + n * 2, &depths[n], 2);
  };
  //narrowPixel() lane by lane: each channel's top bits
  auto narrow = [&](const s32x4 (&value)[4]) -> u32x4 {
    u32x4 r = u32x4(value[0]), g = u32x4(value[1]), b = u32x4(value[2]), a = u32x4(value[3]);
    if constexpr(Format == 0) return r >> 3 | g >> 2 << 5 | b >> 3 << 11;
    if constexpr(Format == 1) return r >> 3 | g >> 3 << 5 | b >> 3 << 10 | a >> 7 << 15;
    if constexpr(Format == 2) return r >> 4 | g >> 4 << 4 | b >> 4 << 8 | a >> 4 << 12;
    return r | g << 8 | b << 16 | a << 24;
  };
  auto write = [&](u32x4 pixel, u32 keep) {
    pixel = (pixel & ~keep) | (old & keep);
    pixel = u32x4(pickLanes(four.live, s32x4(pixel), s32x4(old)));
    for(u32 n = 0; n < 4 && !four.full; n++) {
      if(!four.live[n]) continue;
      if constexpr(bytes == 4) { u32 word = pixel[n]; std::memcpy(vram + at + n * 4, &word, 4); }
      else { u16 half = pixel[n]; std::memcpy(vram + at + n * 2, &half, 2); }
    }
    if(!four.full) return;
    if constexpr(bytes == 4) std::memcpy(vram + at, &pixel, 16);
    else {
      u16 halves[4] = {u16(pixel[0]), u16(pixel[1]), u16(pixel[2]), u16(pixel[3])};
      std::memcpy(vram + at, halves, 8);
    }
  };

  if(p.clear) {  //the vertex's alpha goes in as the stencil
    if(p.clearDepth) writeDepth();
    return write(narrow(color), p.writeMask | (p.clearColor ? 0 : colorBits) | (p.clearAlpha ? 0 : stencilBits));
  }
  s32x4 alpha = color[3];
  if(p.alphaTest) {
    four.live &= passesLanes(p.alphaFunction, alpha & s32(p.alphaMask), splatLanes(p.alphaReference & p.alphaMask));
  }
  if(p.fog) {  //(color * fog + fog color * (255 - fog) + 255 stays below 65536, and shifted, below 256)
    for(u32 n = 0; n < 3; n++) {
      color[n] = (color[n] * four.fog + s32(channel(p.fogColor, n)) * (255 - four.fog) + 255) >> 8;
    }
  }
  if(p.colorTest && p.colorFunction != 1) {
    s32x4 equal = splatLanes(-1);
    for(u32 n = 0; n < 3; n++) {
      s32 mask = channel(p.colorMask, n);
      equal &= (color[n] & mask) == s32(channel(p.colorReference, n) & mask);
    }
    four.live &= p.colorFunction == 0 ? s32x4{} : p.colorFunction == 2 ? equal : ~equal;
  }
  if(!anyLane(four.live)) return;
  if(p.depthWrite) writeDepth();

  //the frame buffer's colors, widened (widenPixel()): each narrow channel's top bits repeated; 5650's alpha 0
  s32x4 oldColor[4];
  {
    s32x4 c = s32x4(old);
    if constexpr(Format == 0) {
      s32x4 r = c & 31, g = c >> 5 & 63, b = c >> 11 & 31;
      oldColor[0] = r << 3 | r >> 2, oldColor[1] = g << 2 | g >> 4, oldColor[2] = b << 3 | b >> 2;
      oldColor[3] = s32x4{};
    } else if constexpr(Format == 1) {
      s32x4 r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
      oldColor[0] = r << 3 | r >> 2, oldColor[1] = g << 3 | g >> 2, oldColor[2] = b << 3 | b >> 2;
      oldColor[3] = pickLanes((c >> 15 & 1) != 0, splatLanes(255), s32x4{});
    } else if constexpr(Format == 2) {
      for(u32 n = 0; n < 4; n++) { s32x4 v = c >> n * 4 & 15; oldColor[n] = v << 4 | v; }
    } else {
      for(u32 n = 0; n < 4; n++) oldColor[n] = s32x4(old >> n * 8 & 0xff);
    }
  }
  s32x4 rgb[4] = {color[0], color[1], color[2], s32x4{}};
  if(p.blend) {
    s32x4 sourceAlpha = alpha, destinationAlpha = oldColor[3];
    auto factors = [&](u32 which, const s32x4 (&other)[4], u32 fixed, s32x4 (&factor)[3]) {
      switch(which) {
      case 0: for(u32 n = 0; n < 3; n++) factor[n] = other[n]; return;
      case 1: for(u32 n = 0; n < 3; n++) factor[n] = 255 - other[n]; return;
      case 2: factor[0] = factor[1] = factor[2] = sourceAlpha; return;
      case 3: factor[0] = factor[1] = factor[2] = 255 - sourceAlpha; return;
      case 4: factor[0] = factor[1] = factor[2] = destinationAlpha; return;
      case 5: factor[0] = factor[1] = factor[2] = 255 - destinationAlpha; return;
      case 6: factor[0] = factor[1] = factor[2] = 2 * sourceAlpha; return;
      case 7: factor[0] = factor[1] = factor[2] = 255 - heldLanes(2 * sourceAlpha, 0, 255); return;
      case 8: factor[0] = factor[1] = factor[2] = 2 * destinationAlpha; return;
      case 9: factor[0] = factor[1] = factor[2] = 255 - heldLanes(2 * destinationAlpha, 0, 255); return;
      }
      for(u32 n = 0; n < 3; n++) factor[n] = splatLanes(channel(fixed, n));
    };
    s32x4 sourceFactor[3], destinationFactor[3];
    factors(p.blendSource, oldColor, p.fixedA, sourceFactor);
    factors(p.blendDestination, color, p.fixedB, destinationFactor);
    //(each term's product is at most 511 * 1021, well inside 32 bits)
    for(u32 n = 0; n < 3; n++) {
      s32x4 s = color[n], d = oldColor[n];
      s32x4 sourceTerm = (s * 2 + 1) * (sourceFactor[n] * 2 + 1) >> 10;
      s32x4 destinationTerm = (d * 2 + 1) * (destinationFactor[n] * 2 + 1) >> 10;
      switch(p.blendOperation) {
      case 0:  rgb[n] = sourceTerm + destinationTerm; break;
      case 1:  rgb[n] = sourceTerm - destinationTerm; break;
      case 2:  rgb[n] = destinationTerm - sourceTerm; break;
      case 3:  rgb[n] = pickLanes(s < d, s, d); break;
      case 4:  rgb[n] = pickLanes(s > d, s, d); break;
      case 5:  rgb[n] = pickLanes(s < d, d - s, s - d); break;
      default: rgb[n] = s; break;
      }
    }
  }
  if(p.dither) {  //(the four starts on a multiple of four: lane n is column n of the matrix)
    const s32* row = &p.ditherMatrix[(y & 3) * 4];
    s32x4 dither = {row[0], row[1], row[2], row[3]};
    for(u32 n = 0; n < 3; n++) rgb[n] += dither;
  }
  for(u32 n = 0; n < 3; n++) rgb[n] = heldLanes(rgb[n], 0, 255);
  //The alpha written is the stencil, as it was (widenPixel()'s alpha); narrowed again, that's the frame buffer's own
  //alpha bits, which the write keeps instead.
  write(narrow(rgb), p.writeMask | stencilBits);
}

//A sprite's rows, four pixels at a time: spriteRows() (raster.cpp), lane by lane. Its depth and color are the same
//at every pixel; across x, its texture coordinate's texel axes (and in 3D, its 1 / w) are worked out once a column,
//down y once a row (in 3D the coordinate down y over the column's 1 / w, at every pixel).
template<u32 Format>
auto GE::spriteFours(const Job& job, s32 fromY, s32 toY) -> void {
  auto& s = job.sprite;
  auto& look = *job.look;
  auto& p = look.pixel;
  auto& t = look.texture;
  s32 firstY = std::max(job.firstY, fromY), lastY = std::min(job.lastY, toY);
  s32 firstX = job.firstX & ~3, columns = ((job.lastX | 3) - firstX + 1);  //(the fours' columns)
  bool textured = look.textured, divided = s.divided && textured;
  //the columns' texel axes across x, and in 3D their 1 / w (as spriteRows() works them out, for the extra
  //columns of the first and last fours too)
  alignas(16) s32 acrossFirst[1032], acrossSecond[1032], acrossFraction[1032];
  alignas(16) f64 inverses[1032];
  if(textured) {
    for(s32 x = firstX; x < firstX + columns; x++) {
      TexelAxis axis;
      if(divided) {
        f64 alongX = f64(x * 16 + 8 - s.left) / (s.right - s.left);
        f64 inverse = s.leftInverse + (s.rightInverse - s.leftInverse) * alongX;
        float acrossX = (s.leftAcross + (s.rightAcross - s.leftAcross) * alongX) / inverse;
        axis = s.turned ? texelAxis(acrossX, t.height, t.clampV, job.linear)
                        : texelAxis(acrossX, t.width, t.clampU, job.linear);
        inverses[x - firstX] = inverse;
      } else {
        float column = spriteAxis(s.columnFirst, s.columnStep, s.columnStart, x);
        axis = s.turned ? texelAxis(column, t.height, t.clampV, job.linear)
                        : texelAxis(column, t.width, t.clampU, job.linear);
      }
      acrossFirst[x - firstX] = axis.first, acrossSecond[x - firstX] = axis.second;
      acrossFraction[x - firstX] = axis.fraction;
    }
  }
  s32x4 baseColor[4], shine[3];
  for(u32 n = 0; n < 4; n++) baseColor[n] = splatLanes(channel(s.color, n));
  for(u32 n = 0; n < 3; n++) shine[n] = splatLanes(channel(s.specular, n));
  u32 downSize = s.turned ? t.width : t.height;
  bool downClamp = s.turned ? t.clampU : t.clampV;
  for(s32 y = firstY; y <= lastY; y++) {
    s32x4 downAxis[3];
    f64 downOverW = 0;
    if(textured && divided) {
      f64 alongY = f64(y * 16 + 8 - s.top) / (s.bottom - s.top);
      downOverW = s.topDown + (s.bottomDown - s.topDown) * alongY;
    } else if(textured) {
      float row = spriteAxis(s.rowFirst, s.rowStep, s.rowStart, y);
      TexelAxis axis = texelAxis(row, downSize, downClamp, job.linear);
      downAxis[0] = splatLanes(axis.first), downAxis[1] = splatLanes(axis.second);
      downAxis[2] = splatLanes(axis.fraction);
    }
    for(s32 x = firstX; x <= job.lastX; x += 4) {
      Four four;
      s32x4 column = x + Lanes;
      four.live = four.inside = (column >= job.firstX) & (column <= job.lastX);
      four.full = x >= job.firstX && x + 3 <= job.lastX;
      four.z = splatLanes(s.z);
      if(!depthFirst<Format>(p, x, y, four)) continue;
      for(u32 n = 0; n < 4; n++) four.color[n] = baseColor[n];
      if(textured) {
        u32 at = x - firstX;
        s32x4 acrossAxis[3];
        std::memcpy(&acrossAxis[0], acrossFirst + at, 16);
        std::memcpy(&acrossAxis[1], acrossSecond + at, 16);
        std::memcpy(&acrossAxis[2], acrossFraction + at, 16);
        if(divided) {  //float downY = downOverW / column.inverse, a lane at a time in doubles, two by two
          f64x2 over = f64x2{} + downOverW, near, far;
          std::memcpy(&near, inverses + at, 16);
          std::memcpy(&far, inverses + at + 2, 16);
          f64x2 nearDown = over / near, farDown = over / far;
          f32x4 downY = {float(nearDown[0]), float(nearDown[1]), float(farDown[0]), float(farDown[1])};
          texelAxisLanes(downY, downSize, downClamp, job.linear, downAxis);
        }
        s32x4 texel[4];
        if(s.turned) texelsFour(look, job.linear, four.live, downAxis, acrossAxis, texel);
        else texelsFour(look, job.linear, four.live, acrossAxis, downAxis, texel);
        combineFour(look, four.color, texel);
      }
      if(s.specular) {
        for(u32 n = 0; n < 3; n++) four.color[n] = heldLanes(four.color[n] + shine[n], 0, 255);
      }
      if(p.fog) {  //fogAt(): the right half's fog from the middle column on
        four.fog = pickLanes(column * 16 + 8 + 1 >= s.middle, splatLanes(s.rightFog), splatLanes(s.leftFog));
      }
      pixelsFour<Format>(p, x, y, four);
    }
  }
}

//A triangle's rows, four pixels at a time: triangleRows() (raster.cpp), lane by lane. Each row's pixels inside it are
//found as there; its fours then run from the multiple of four at or before the first. The edge functions at a four's
//pixels are whole numbers within 32 bits (fourFriendly()), so turned into floats they're what the 64-bit ones give.
template<u32 Format>
auto GE::triangleFours(const Job& job, s32 fromY, s32 toY) -> void {
  auto& r = job.triangle;
  auto& look = *job.look;
  auto& p = look.pixel;
  auto& t = look.texture;
  s64 a[3], b[3], c[3], least[3];
  for(u32 k = 0; k < 3; k++) {
    u32 from = (k + 1) % 3, to = (k + 2) % 3;
    a[k] = r.y[from] - r.y[to], b[k] = r.x[to] - r.x[from], c[k] = r.y[to] * r.x[from] - r.x[to] * r.y[from];
    least[k] = a[k] > 0 || (a[k] == 0 && b[k] > 0) ? 0 : 1;
  }
  bool needsZ = p.depthRange || (p.clear ? p.clearDepth : p.depthTest);
  bool blended = !r.flat, shining = !r.flat && r.shines;
  //Colors, the shine, fog and depth stepped (triangleRows()), lane by lane: each one's 16384ths at a four's pixels,
  //and a four's step, kept in 32 bits that may run round. Every lane inside the triangle keeps its value, which
  //fits (a level or a depth, and how far the steps' rounding takes it: fourFriendly() sees to that); the lanes
  //outside it may come out anything.
  const Job::Stepped* stepped[9] = {&r.colors[0], &r.colors[1], &r.colors[2], &r.colors[3],
                                    &r.shine[0], &r.shine[1], &r.shine[2], &r.fog, &r.depth};
  u32x4 values[9] = {}, steps[9] = {};
  u32 used[9], uses = 0;
  for(u32 n = 0; n < 9; n++) {
    if(n < 4 ? blended : n < 7 ? shining : n < 8 ? p.fog : needsZ) {
      used[uses++] = n, steps[n] = u32x4{} + u32(stepped[n]->across * 64);
    }
  }
  auto levels = [&](u32 n) { return heldLanes(s32x4(values[n]) >> 14, 0, n < 8 ? 255 : 65535); };
  s32x4 flatColor[4], flatShine[3];
  for(u32 n = 0; n < 4; n++) flatColor[n] = splatLanes(channel(r.flatColor, n));
  for(u32 n = 0; n < 3; n++) flatShine[n] = splatLanes(channel(r.flatSpecular, n));
  //(shadeAs() adds a shine only where it isn't 0; adding 0 changes nothing, so a lane adds whatever it has)
  bool addsShine = shining || r.flatSpecular;
  for(s32 y = std::max(job.firstY, fromY); y <= std::min(job.lastY, toY); y++) {
    s64 sampleY = s64(y) * 16 + 8;
    s64 sampleX = s64(job.firstX) * 16 + 8;
    s64 start = job.firstX, stop = job.lastX;
    for(u32 k = 0; k < 3; k++) {  //(as triangleRows())
      s64 have = a[k] * sampleX + b[k] * sampleY + c[k] - least[k], step = a[k] * 16;
      if(step > 0 && have < 0) start = std::max(start, job.firstX + (-have + step - 1) / step);
      if(step < 0) stop = have < 0 ? -1 : std::min(stop, job.firstX + have / -step);
      if(step == 0 && have < 0) stop = -1;
    }
    if(start > stop) continue;
    s32 first = s32(start) & ~3;
    s32x4 edge[3];
    for(u32 k = 0; k < 3; k++) {
      s64 at = a[k] * (s64(first) * 16 + 8) + b[k] * sampleY + c[k];
      edge[k] = s32(at) + s32(a[k] * 16) * Lanes;
    }
    for(u32 k = 0; k < uses; k++) {
      auto& c = *stepped[used[k]];
      s128 at = c.start + s128(s64(first) * 16 + 8 - r.startX) * c.across + s128(sampleY - r.startY) * c.down;
      values[used[k]] = u32(at) + u32(c.across * 16) * u32x4(Lanes);
    }
    auto next = [&] { for(u32 k = 0; k < uses; k++) values[used[k]] += steps[used[k]]; };
    for(s32 x = first; x <= stop; x += 4, next()) {
      Four four;
      s32x4 column = x + Lanes;
      four.live = four.inside = (column >= s32(start)) & (column <= s32(stop));
      four.full = x >= start && x + 3 <= stop;
      f32x4 w0 = __builtin_convertvector(edge[0], f32x4);
      f32x4 w1 = __builtin_convertvector(edge[1], f32x4);
      f32x4 w2 = __builtin_convertvector(edge[2], f32x4);
      for(u32 k = 0; k < 3; k++) edge[k] += s32(a[k] * 64);
      four.z = needsZ ? levels(8) : s32x4{};
      if(!depthFirst<Format>(p, x, y, four)) continue;
      if(blended) for(u32 n = 0; n < 4; n++) four.color[n] = levels(n);
      else for(u32 n = 0; n < 4; n++) four.color[n] = flatColor[n];
      if(look.textured) {
        f32x4 u, v;
        if(r.perspective) {
          f32x4 ka = w0 / r.w[0], kb = w1 / r.w[1], kc = w2 / r.w[2];
          f32x4 divisor = ka * r.q[0] + kb * r.q[1] + kc * r.q[2];
          u = (ka * r.u[0] + kb * r.u[1] + kc * r.u[2]) / divisor;
          v = (ka * r.v[0] + kb * r.v[1] + kc * r.v[2]) / divisor;
        } else {  //(in doubles, two lanes at a time; each lane's difference is a whole number, exact as a double)
          f64 across = f64(s64(x) * 16 + 8 - r.startX);
          f64x2 near = f64x2{across, across + 16}, far = f64x2{across + 32, across + 48};
          f64x2 down = f64x2{} + f64(sampleY - r.startY);
          f64x2 nearU = r.uStart + near / 16 * r.uAcross + down / 16 * r.uDown;
          f64x2 farU = r.uStart + far / 16 * r.uAcross + down / 16 * r.uDown;
          f64x2 nearV = r.vStart + near / 16 * r.vAcross + down / 16 * r.vDown;
          f64x2 farV = r.vStart + far / 16 * r.vAcross + down / 16 * r.vDown;
          u = f32x4{float(nearU[0]), float(nearU[1]), float(farU[0]), float(farU[1])};
          v = f32x4{float(nearV[0]), float(nearV[1]), float(farV[0]), float(farV[1])};
        }
        s32x4 uAxis[3], vAxis[3], texel[4];
        texelAxisLanes(u, t.width, t.clampU, job.linear, uAxis);
        texelAxisLanes(v, t.height, t.clampV, job.linear, vAxis);
        texelsFour(look, job.linear, four.live, uAxis, vAxis, texel);
        combineFour(look, four.color, texel);
      }
      if(addsShine) {
        for(u32 n = 0; n < 3; n++) {
          four.color[n] = heldLanes(four.color[n] + (shining ? levels(4 + n) : flatShine[n]), 0, 255);
        }
      }
      if(p.fog) four.fog = levels(7);
      pixelsFour<Format>(p, x, y, four);
    }
  }
}
