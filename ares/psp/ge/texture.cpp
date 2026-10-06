//Textures: the GE looks a texture up at each pixel's texture coordinates and combines it with the pixel's color.
//
//A texture is an image in memory (RAM or VRAM), as these commands describe it:
//  TEXTURE_ADDRESS0        where it starts (the address's top bits ride in TEXTURE_BUFFER_WIDTH0's bits 16-19)
//  TEXTURE_BUFFER_WIDTH0   texels from one row to the next (low 11 bits, rounded down to whole 16 bytes)
//  TEXTURE_SIZE0           its size, each side a power of two: bits 0-3 the width's, bits 8-11 the height's
//  TEXTURE_FORMAT          0 5650, 1 5551, 2 4444, 3 8888; 4-7 indices of 4, 8, 16 or 32 bits into the palette;
//                          8-10 compressed (DXT1, 3, 5: not yet)
//  TEXTURE_MODE bit 0      swizzled: stored in blocks 16 bytes wide and 8 rows high, each block's rows one after
//                          another, blocks left to right and then down (the layout pspsdk's swizzle code writes), so
//                          the GE reads a block at a time
//  TEXTURE_WRAP            past an edge, coordinates repeat (0) or stay at the edge (1); bit 0 across, bit 8 down
//  TEXTURE_FILTER          the nearest texel (0), or the four nearest blended (1): bit 0 when the texture is shrunk,
//                          bit 8 when it's enlarged
//Sides past 512 texels repeat (or stop) at 512, as the GE only counts that far.
//
//The palette (CLUT): CLUT_LOAD copies 32 bytes per count from CLUT_ADDRESS into the GE's own 1 KiB, where textures
//find it. CLUT_FORMAT gives its entries' format (0-3, as above) and turns an index into an entry: shifted right
//(bits 2-6), masked (bits 8-15), then 16 entries per step of offset (bits 16-20) added.
//
//(How texels are addressed, repeated and filtered, and the texture functions' arithmetic, are as PPSSPP's software
//renderer has them, which its authors checked against tests on the PSP. Mipmaps, DXT and 3D's texture coordinates come
//later.)

static constexpr u32 TexelBits[8] = {16, 16, 16, 32, 4, 8, 16, 32};

static auto channel(u32 color, u32 n) -> s32 { return color >> n * 8 & 0xff; }
static auto pack(s32 r, s32 g, s32 b, s32 a) -> u32 {
  auto held = [](s32 value) { return u32(std::clamp(value, 0, 255)); };
  return held(r) | held(g) << 8 | held(b) << 16 | held(a) << 24;
}

//A 16-bit color of format 0-2 as 8888 (red in the low byte), each narrow field widened by repeating its top bits.
static auto widen16(u32 c, u32 format) -> u32 {
  auto widen = [](u32 value, u32 bits) { return value << (8 - bits) | value >> (2 * bits - 8); };
  if(format == 0) return widen(c & 31, 5) | widen(c >> 5 & 63, 6) << 8 | widen(c >> 11 & 31, 5) << 16 | 0xff00'0000;
  if(format == 1) return widen(c & 31, 5) | widen(c >> 5 & 31, 5) << 8 | widen(c >> 10 & 31, 5) << 16 | (c >> 15 ? 0xff00'0000 : 0);
  return widen(c & 15, 4) | widen(c >> 4 & 15, 4) << 8 | widen(c >> 8 & 15, 4) << 16 | widen(c >> 12 & 15, 4) << 24;
}

auto GE::sampler() const -> Sampler {
  Sampler t{};
  t.format = commands[TextureFormat] & 0xf;
  t.address = (commands[TextureAddress0] & 0xff'fff0) | (commands[TextureBufferWidth0] << 8 & 0x0f00'0000);
  u32 bits = t.format < 8 ? TexelBits[t.format] : 4;
  u32 width = commands[TextureBufferWidth0] & 0x7ff & ~(128 / bits - 1);
  t.bufferWidth = width ? width : 128 / bits;  //at least 16 bytes
  t.width = 1 << (commands[TextureSize0] & 0xf);
  t.height = 1 << (commands[TextureSize0] >> 8 & 0xf);
  t.swizzled = commands[TextureMode] & 1;
  t.clampU = commands[TextureWrap] & 1;
  t.clampV = commands[TextureWrap] >> 8 & 1;
  t.clutFormat = commands[ClutFormat] & 3;
  t.clutShift = commands[ClutFormat] >> 2 & 0x1f;
  t.clutMask = commands[ClutFormat] >> 8 & 0xff;
  t.clutOffset = (commands[ClutFormat] >> 16 & 0x1f) << 4;
  return t;
}

//CLUT_LOAD: the palette copied into the GE.
auto GE::loadClut() -> void {
  u32 address = (commands[ClutAddress] & 0xff'fff0) | (commands[ClutAddressUpper] << 8 & 0x0f00'0000);
  u32 bytes = std::min<u32>((commands[ClutLoad] & 0x3f) * 32, sizeof(clut));
  if(!memory.copyOut(clut, address, bytes)) {
    for(u32 n = 0; n < bytes; n++) clut[n] = memory.read(1, address + n);
  }
}

//The texel at (u, v), already inside the texture, as 8888.
auto GE::texel(const Sampler& t, s32 u, s32 v) -> u32 {
  if(t.format >= 8) {
    note("compressed (DXT) textures aren't emulated yet");
    return 0;
  }
  u32 bits = TexelBits[t.format], rowBytes = t.bufferWidth * bits / 8, byte = u32(u) * bits / 8, offset;
  if(!t.swizzled) offset = v * rowBytes + byte;
  else offset = (v / 8 * (rowBytes / 16) + byte / 16) * 128 + v % 8 * 16 + byte % 16;
  u32 at = t.address + offset;
  if(t.format < 3) return widen16(memory.read(2, at), t.format);
  if(t.format == 3) return memory.read(4, at);
  u32 index = t.format == 4 ? memory.read(1, at) >> (u & 1) * 4 & 15
            : t.format == 5 ? memory.read(1, at) : memory.read(t.format == 6 ? 2 : 4, at);
  u32 entry = ((index >> t.clutShift) & t.clutMask) | (t.clutOffset & (t.clutFormat == 3 ? 0xff : 0x1ff));
  if(t.clutFormat == 3) {
    entry &= 0xff;
    return clut[entry * 4] | clut[entry * 4 + 1] << 8 | clut[entry * 4 + 2] << 16 | clut[entry * 4 + 3] << 24;
  }
  entry &= 0x1ff;
  return widen16(clut[entry * 2] | clut[entry * 2 + 1] << 8, t.clutFormat);
}

//The texture at (u, v), in texels (a texel's middle is at +0.5): the texel there, or, filtered, the four whose
//middles are nearest, weighted by sixteenths.
auto GE::sample(const Sampler& t, float u, float v) -> u32 {
  auto inside = [](s32 c, u32 size, bool clamp) -> s32 {
    s32 last = std::min<s32>(size, 512) - 1;
    return clamp ? std::clamp(c, 0, last) : c & last;
  };
  auto held = [](float value) { return std::clamp(value, -65536.0f, 65536.0f); };  //wild coordinates, from garbage
  if(!t.linear) {
    s32 x = s32(std::floor(held(u))), y = s32(std::floor(held(v)));
    return texel(t, inside(x, t.width, t.clampU), inside(y, t.height, t.clampV));
  }
  s32 baseU = s32(std::floor(held(u) * 256)) - 128, baseV = s32(std::floor(held(v) * 256)) - 128;
  s32 fractionU = baseU >> 4 & 15, fractionV = baseV >> 4 & 15;
  s32 x0 = baseU >> 8, y0 = baseV >> 8;
  s32 left = inside(x0, t.width, t.clampU), right = inside(x0 + 1, t.width, t.clampU);
  s32 top = inside(y0, t.height, t.clampV), bottom = inside(y0 + 1, t.height, t.clampV);
  u32 topLeft = texel(t, left, top), topRight = texel(t, right, top);
  u32 bottomLeft = texel(t, left, bottom), bottomRight = texel(t, right, bottom);
  s32 mixed[4];
  for(u32 n = 0; n < 4; n++) {
    s32 upper = channel(topLeft, n) * (16 - fractionU) + channel(topRight, n) * fractionU;
    s32 lower = channel(bottomLeft, n) * (16 - fractionU) + channel(bottomRight, n) * fractionU;
    mixed[n] = (upper * (16 - fractionV) + lower * fractionV) >> 8;
  }
  return pack(mixed[0], mixed[1], mixed[2], mixed[3]);
}

//How the texture's color (Ct) and the pixel's own (Cf) combine (TEXTURE_FUNCTION: bits 0-2 which, bit 8 whether the
//texture's alpha counts, bit 16 color doubling: the result's color times two), as pspgu.h describes them:
//  modulate Cv = Ct*Cf; decal Cv = Ct (with alpha: Cf*(1-At) + Ct*At); blend Cv = Cf*(1-Ct) + Cc*Ct, with Cc
//  TEXTURE_ENVIRONMENT_COLOR; replace Cv = Ct; add Cv = Cf+Ct. Alpha is Af, or with the texture's alpha At*Af (At
//  for replace; decal keeps Af).
//The PSP's rounding: products are (Cf+1)*Ct/256, except blend's, which rounds up.
auto GE::textureFunction(u32 color, u32 texel) const -> u32 {
  u32 function = commands[TextureFunction] & 7;
  bool withAlpha = commands[TextureFunction] >> 8 & 1, doubled = commands[TextureFunction] >> 16 & 1;
  u32 environment = commands[TextureEnvironmentColor];
  s32 fragmentAlpha = channel(color, 3), texelAlpha = channel(texel, 3);
  s32 modulatedAlpha = withAlpha ? (fragmentAlpha + 1) * texelAlpha / 256 : fragmentAlpha;
  s32 out[3], alpha = modulatedAlpha;
  for(u32 n = 0; n < 3; n++) {
    s32 f = channel(color, n), t = channel(texel, n);
    switch(function) {
    case 0: out[n] = (f + 1) * t * (doubled ? 2 : 1) / 256; break;
    case 1:
      out[n] = withAlpha ? ((f + 1) * (255 - texelAlpha) + (t + 1) * texelAlpha) / (doubled ? 128 : 256)
                         : t * (doubled ? 2 : 1);
      alpha = fragmentAlpha;
      break;
    case 2: out[n] = ((255 - t) * f + t * channel(environment, n) + 255) / (doubled ? 128 : 256); break;
    case 3: out[n] = t * (doubled ? 2 : 1); alpha = withAlpha ? texelAlpha : fragmentAlpha; break;
    default: out[n] = (f + t) * (doubled ? 2 : 1); break;  //add (and 5-7, which act as add)
    }
  }
  return pack(out[0], out[1], out[2], alpha);
}
