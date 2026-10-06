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
//
//Decoded textures. Looking a texel up the way the GE stores it takes a lot of steps: find the memory, unswizzle the
//address, read it, widen a 16-bit color or look an index up in the palette. Drawing does that four times a pixel
//when it filters, and the same texels over and over, so a texture is decoded once into a plain picture of 8888
//texels (decode()), each exactly what texel() would read, and drawing takes each texel from there in one step. The
//copy is good only while the memory it came from stays as it was: the GE watches those pages (Memory::watch()),
//and whoever writes any of them (the CPU, an HLE function, the GE drawing or copying, a state loaded) throws the
//copy away (textureWritten()), to be decoded afresh when next drawn with. The palette is part of what a texture of
//indices looks like: its copy is kept for the palette it was decoded with, found again by the palette's hash and
//checked byte for byte whenever the palette has changed since (clutVersion).
//
//Read from memory as before, texel by texel, are: DXT textures (not emulated yet: texel() notes it as it's used);
//a texture some of whose bytes have no memory behind them (each such read is reported, as before); and a texture
//the primitive may draw over itself (its frame buffer or depth buffer and the texture overlap), whose texels then
//change as the primitive draws, as they do on the PSP.

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
  drawnFirst(address, bytes);  //(from VRAM primitives waiting to be drawn draw over: threads.cpp)
  u8 before[sizeof(clut)];
  std::memcpy(before, clut, sizeof(clut));
  if(!memory.copyOut(clut, address, bytes)) {
    for(u32 n = 0; n < bytes; n++) clut[n] = memory.read(1, address + n);
  }
  if(std::memcmp(before, clut, sizeof(clut))) paletteChanged();
}

//The palette's contents changed: a new hash and version, by which decoded textures of indices find theirs.
auto GE::paletteChanged() -> void {
  u64 hash = 0xcbf2'9ce4'8422'2325;
  for(u8 byte : clut) hash = (hash ^ byte) * 0x100'0000'01b3;
  clutHash = hash;
  clutVersion++;
}

//The texel at (u, v), already inside the texture, as 8888, from clut and read(size, address): texel() reads memory
//as the CPU would, decode() reads the very same bytes straight from where they are in the host's memory.
template<typename Read>
static auto texelFrom(const GE::Sampler& t, const u8* clut, s32 u, s32 v, const Read& read) -> u32 {
  u32 bits = TexelBits[t.format], rowBytes = t.bufferWidth * bits / 8, byte = u32(u) * bits / 8, offset;
  if(!t.swizzled) offset = v * rowBytes + byte;
  else offset = (v / 8 * (rowBytes / 16) + byte / 16) * 128 + v % 8 * 16 + byte % 16;
  u32 at = t.address + offset;
  if(t.format < 3) return widen16(read(2, at), t.format);
  if(t.format == 3) return read(4, at);
  u32 index = t.format == 4 ? read(1, at) >> (u & 1) * 4 & 15
            : t.format == 5 ? read(1, at) : read(t.format == 6 ? 2 : 4, at);
  u32 entry = ((index >> t.clutShift) & t.clutMask) | (t.clutOffset & (t.clutFormat == 3 ? 0xff : 0x1ff));
  if(t.clutFormat == 3) {
    entry &= 0xff;
    return clut[entry * 4] | clut[entry * 4 + 1] << 8 | clut[entry * 4 + 2] << 16 | clut[entry * 4 + 3] << 24;
  }
  entry &= 0x1ff;
  return widen16(clut[entry * 2] | clut[entry * 2 + 1] << 8, t.clutFormat);
}

auto GE::texel(const Sampler& t, s32 u, s32 v) -> u32 {
  if(t.format >= 8) {
    note("compressed (DXT) textures aren't emulated yet");
    return 0;
  }
  return texelFrom(t, clut, u, v, [&](u32 size, u32 at) { return memory.read(size, at); });
}

//The bytes texel() reads for every texel inside the texture's first rows: from low up to (not including) high. For
//a swizzled texture, a little past them at most (the last block's whole width and height).
auto GE::textureBytes(const Sampler& t, u32 rows, u32& low, u32& high) const -> void {
  u32 width = std::min<u32>(t.width, 512), height = rows;
  u32 bits = TexelBits[t.format], rowBytes = t.bufferWidth * bits / 8, lastByte = (width - 1) * bits / 8;
  u32 size = t.format < 3 || t.format == 6 ? 2 : t.format == 3 || t.format == 7 ? 4 : 1;  //the last read's
  u32 last = !t.swizzled ? (height - 1) * rowBytes + lastByte
           : ((height - 1) / 8 * (rowBytes / 16) + lastByte / 16) * 128 + 7 * 16 + 15;
  low = t.address;
  high = t.address + last + size;
}

//Where in VRAM's 2 MiB the size bytes from address are, first to last (inclusive): false if they aren't in VRAM.
//Through the copies that rearrange each 16 KiB (memory.hpp), every 16 KiB the range touches.
static auto vramSpan(u32 address, u32 size, u32& first, u32& last) -> bool {
  u32 physical = address & 0x1fff'ffff;
  if(!size || physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMWindow) return false;
  u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize;
  u32 seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
  first = seen, last = std::min<u32>(seen + std::min<u32>(size, Memory::VRAMSize), Memory::VRAMSize) - 1;
  if(copy & 1) first &= ~0x3fffu, last |= 0x3fff;
  return true;
}

//Whether a primitive drawing with these settings, its pixels inside left-right and top-bottom (in the scissor
//rectangle), may write anywhere in VRAM from first to last (inclusive): those pixels' bytes in its frame buffer,
//row by row (a texture in the unused columns right of the picture isn't drawn over), and its depth buffer's, when
//it writes depth (as a whole: the GE reaches it with each 16 KiB rearranged, memory.hpp).
static auto drawsOver(const GE::PixelState& p, u32 first, u32 last, s32 left, s32 top, s32 right, s32 bottom)
  -> bool {
  if(left > right || top > bottom) return false;
  auto overlaps = [&](u32 from, u32 to, bool depth) {  //from VRAM offset from to to, before wrapping at its end
    if(to - from >= Memory::VRAMSize - 1) return true;
    from &= Memory::VRAMSize - 1, to &= Memory::VRAMSize - 1;
    if(depth) from &= ~0x3fffu, to |= 0x3fff;
    if(from > to) return first <= to || last >= from;  //wrapped round VRAM's end
    return first <= to && last >= from;
  };
  u32 bytes = p.format == 3 ? 4 : 2, rowBytes = p.stride * bytes;
  u32 from = p.frameBuffer + (top * p.stride + left) * bytes;
  u32 to = p.frameBuffer + (bottom * p.stride + right) * bytes;
  if(overlaps(from, to + bytes - 1, false)) {
    //the rows' span reaches the bytes: whether a row does. (Where the span wraps round VRAM's end, it counts.)
    if(to + bytes - 1 >= Memory::VRAMSize || !rowBytes) return true;
    u32 segment = (right - left + 1) * bytes;  //a row's bytes, from its first pixel's
    u32 lowest = first >= segment - 1 + from ? (first - (segment - 1) - from) / rowBytes : 0;
    for(u32 row = lowest; row <= u32(bottom - top) && from + row * rowBytes <= last; row++) {
      if(from + row * rowBytes + segment - 1 >= first) return true;
    }
  }
  bool depth = p.clear ? p.clearDepth : p.depthWrite;
  from = p.depthBuffer + (top * p.depthStride + left) * 2;
  return depth && overlaps(from, p.depthBuffer + (bottom * p.depthStride + right) * 2 + 1, true);
}

//The texture decoded, found in the cache or decoded now (and its texels pointed to by texture.decoded): kept by the
//caller while it draws, as the cache may let it go meanwhile. None, with texture.decoded none too, where it's read
//from memory as before (see the top of this file). region: where the primitive may draw; rows: how many of the
//texture's rows it may take texels from (all of them, past its height).
auto GE::decode(Sampler& t, const PixelState& pixel, const Region& region, u32 rows) -> std::shared_ptr<Decoded> {
  t.decoded = nullptr;
  if(t.format >= 8 || !memory.canWatch()) return {};
  rows = std::min({rows, t.height, 512u});
  u32 low, high;
  textureBytes(t, rows, low, high);
  if(!memory.reaches(low, high - low)) return {};
  if(u32 first, last; vramSpan(low, high - low, first, last)) {
    if(drawsOver(pixel, first, last, region.left, region.top, region.right, region.bottom)) return {};
  }
  bool indexed = t.format >= 4;
  TextureKey key{t.address, t.bufferWidth, t.format, std::min<u32>(t.width, 512), t.swizzled, 0, 0, 0, 0, 0};
  if(indexed) key.clutFormat = t.clutFormat, key.clutShift = t.clutShift, key.clutMask = t.clutMask,
              key.clutOffset = t.clutOffset, key.clutHash = clutHash;
  std::shared_ptr<Decoded> entry;
  if(textures.last && textures.last->key == key) entry = textures.last;
  else if(auto found = textures.entries.find(key); found != textures.entries.end()) entry = found->second;
  if(entry && indexed && entry->paletteChecked != clutVersion) {
    if(!std::memcmp(entry->palette.data(), clut, sizeof(clut))) entry->paletteChecked = clutVersion;
    else forget(entry.get()), entry.reset();  //another palette with the same hash: decoded afresh
  }
  //A texture is kept once, with as many rows as any primitive has reached: one reaching fewer draws from it as it
  //is, one reaching more gets a longer copy, the rows kept already copied into it (still as their memory is, or
  //they'd be gone) and the rest decoded.
  if(!entry || entry->rows < rows) {
    auto shorter = std::move(entry);
    drawnFirst(low, high - low);  //(from VRAM primitives waiting to be drawn draw over: threads.cpp)
    entry = std::make_shared<Decoded>();
    entry->key = key;
    entry->rows = rows;
    entry->texels.resize(key.width * rows);
    u32 from = 0;
    if(shorter) {
      std::copy(shorter->texels.begin(), shorter->texels.end(), entry->texels.begin());
      from = shorter->rows;
      forget(shorter.get());
    }
    auto decodeWith = [&](const auto& read) {
      for(u32 v = from; v < rows; v++) {
        for(u32 u = 0; u < key.width; u++) entry->texels[v * key.width + u] = texelFrom(t, clut, u, v, read);
      }
    };
    if(const u8* base = memory.pointer(low, high - low)) {
      decodeWith([&](u32 size, u32 at) -> u32 {
        const u8* bytes = base + (at - low);
        if(size == 1) return bytes[0];
        if(size == 2) return bytes[0] | bytes[1] << 8;
        return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | u32(bytes[3]) << 24;
      });
    } else {  //through VRAM's copies that rearrange it
      decodeWith([&](u32 size, u32 at) { return memory.read(size, at); });
    }
    if(indexed) entry->palette.assign(clut, clut + sizeof(clut)), entry->paletteChecked = clutVersion;
    memory.pagesOf(low, high - low, entry->firstPage, entry->lastPage);
    memory.watch(low, high - low);
    for(u32 page = entry->firstPage; page <= entry->lastPage; page++) textures.pages[page].push_back(entry.get());
    textures.bytes += entry->texels.size() * 4;
    textures.entries[key] = entry;
    entry->place = textures.recent.insert(textures.recent.begin(), entry.get());
    //too much kept: those unused longest go (this one, the last used, stays even past the budget by itself)
    while(textures.bytes > textures.budget && textures.recent.size() > 1) forget(textures.recent.back());
  } else {
    textures.recent.splice(textures.recent.begin(), textures.recent, entry->place);
  }
  textures.last = entry;
  t.decoded = entry->texels.data();
  t.decodedWidth = key.width;
  t.decodedRows = rows;
  return entry;
}

//Memory::watchedWritten(): the page changed, so every texture decoded from it goes.
auto GE::textureWritten(u32 page) -> void {
  auto found = textures.pages.find(page);
  if(found == textures.pages.end()) return;
  auto stale = found->second;  //(forget() edits the lists)
  for(auto* entry : stale) forget(entry);
}

//A decoded texture out of the cache (a primitive drawing with it keeps it until it's done).
auto GE::forget(Decoded* entry) -> void {
  for(u32 page = entry->firstPage; page <= entry->lastPage; page++) {
    auto found = textures.pages.find(page);
    if(found == textures.pages.end()) continue;
    auto& list = found->second;
    list.erase(std::remove(list.begin(), list.end(), entry), list.end());
    if(list.empty()) textures.pages.erase(found);
  }
  textures.bytes -= entry->texels.size() * 4;
  textures.recent.erase(entry->place);
  if(textures.last.get() == entry) textures.last.reset();
  auto found = textures.entries.find(entry->key);
  //(the last reference but a drawing primitive's: entry may be gone after this)
  if(found != textures.entries.end() && found->second.get() == entry) textures.entries.erase(found);
}

auto GE::dropTextures() -> void {
  textures.entries.clear();
  textures.pages.clear();
  textures.recent.clear();
  textures.last.reset();
  textures.bytes = 0;
}

//The texture at (u, v), in texels (a texel's middle is at +0.5): the texel there, or, filtered, the four whose
//middles are nearest, weighted by sixteenths: the top two blended, then the bottom two, then those two results, each
//step dropping its fraction (measured on a PSP, docs/psp-core.md: every pixel of the three filter-magnify files).
auto GE::sample(const Sampler& t, float u, float v) -> u32 {
  return sampleWith(t, t.linear, u, v);
}

//As sample(), filtered as linear says.
//A texture coordinate as sampling takes it: the texel it's in (first; nearest), or the two whose middles it lies
//between (first, second) and how far from the first to the second, in sixteenths (fraction; filtered), each already
//inside the texture (repeated or held at the edge). size and clamp: the texture's width and TEXTURE_WRAP's bit for u,
//or its height and bit for v.
alwaysinline auto GE::texelAxis(float coordinate, u32 size, bool clamp, bool linear) -> TexelAxis {
  auto inside = [&](s32 c) -> s32 {
    s32 last = std::min<s32>(size, 512) - 1;
    return clamp ? std::clamp(c, 0, last) : c & last;
  };
  //wild coordinates, from garbage (or none at all, from a division by zero)
  float held = std::isnan(coordinate) ? 0.0f : std::clamp(coordinate, -65536.0f, 65536.0f);
  if(!linear) return {inside(s32(std::floor(held))), 0, 0};
  s32 base = s32(std::floor(held * 256)) - 128;
  return {inside(base >> 8), inside((base >> 8) + 1), base >> 4 & 15};
}

//The texel at (x, y), inside the texture: decoded already, or read from memory. (A row past those draw.cpp worked
//out the primitive may reach isn't in the decoded copy.)
alwaysinline auto GE::fetch(const Sampler& t, s32 x, s32 y) -> u32 {
  assert(!t.decoded || u32(y) < t.decodedRows);
  return t.decoded ? t.decoded[y * t.decodedWidth + x] : texel(t, x, y);
}

//The four texels around (u, v) blended: the top two, then the bottom two, then those two results, each step dropping
//its fraction. The four channels go side by side, each in 16 bits of one number: a channel times sixteenths stays
//below 4096, so none spills into the next, and each shift's spill from the next is masked off.
alwaysinline auto GE::filtered(const Sampler& t, TexelAxis u, TexelAxis v) -> u32 {
  auto spread = [](u32 c) -> u64 {
    return (c & 0xff) | u64(c & 0xff00) << 8 | u64(c & 0xff'0000) << 16 | u64(c & 0xff00'0000) << 24;
  };
  constexpr u64 Channels = 0x00ff'00ff'00ff'00ff;
  u64 topLeft = spread(fetch(t, u.first, v.first)), topRight = spread(fetch(t, u.second, v.first));
  u64 bottomLeft = spread(fetch(t, u.first, v.second)), bottomRight = spread(fetch(t, u.second, v.second));
  u64 across = u.fraction, down = v.fraction;
  u64 upper = (topLeft * (16 - across) + topRight * across) >> 4 & Channels;
  u64 lower = (bottomLeft * (16 - across) + bottomRight * across) >> 4 & Channels;
  u64 mixed = (upper * (16 - down) + lower * down) >> 4 & Channels;
  return u32(mixed & 0xff) | u32(mixed >> 8 & 0xff00) | u32(mixed >> 16 & 0xff'0000) | u32(mixed >> 24 & 0xff00'0000);
}

alwaysinline auto GE::sampleWith(const Sampler& t, bool linear, float u, float v) -> u32 {
  auto across = texelAxis(u, t.width, t.clampU, linear), down = texelAxis(v, t.height, t.clampV, linear);
  return linear ? filtered(t, across, down) : fetch(t, across.first, down.first);
}

//How the texture's color (Ct) and the pixel's own (Cf) combine (TEXTURE_FUNCTION: bits 0-2 which, bit 8 whether the
//texture's alpha counts, bit 16 color doubling: the result's color times two), as pspgu.h describes them:
//  modulate Cv = Ct*Cf; decal Cv = Ct (with alpha: Cf*(1-At) + Ct*At); blend Cv = Cf*(1-Ct) + Cc*Ct, with Cc
//  TEXTURE_ENVIRONMENT_COLOR; replace Cv = Ct; add Cv = Cf+Ct. Alpha is Af, or with the texture's alpha At*Af (At
//  for replace; decal keeps Af).
//The PSP's rounding: products are (Cf+1)*Ct/256, except blend's, which rounds up.
static alwaysinline auto textureFunctionWith(u32 function, bool withAlpha, bool doubled, u32 environment, u32 color,
                                             u32 texel) -> u32 {
  s32 fragmentAlpha = channel(color, 3), texelAlpha = channel(texel, 3);
  s32 modulatedAlpha = withAlpha ? (fragmentAlpha + 1) * texelAlpha >> 8 : fragmentAlpha;
  s32 out[3], alpha = modulatedAlpha, twice = doubled ? 2 : 1, down = doubled ? 7 : 8;  //(none of it below zero)
  switch(function) {
  case 0:
    for(u32 n = 0; n < 3; n++) out[n] = (channel(color, n) + 1) * channel(texel, n) * twice >> 8;
    break;
  case 1:
    for(u32 n = 0; n < 3; n++) {
      s32 f = channel(color, n), t = channel(texel, n);
      out[n] = withAlpha ? ((f + 1) * (255 - texelAlpha) + (t + 1) * texelAlpha) >> down : t * twice;
    }
    alpha = fragmentAlpha;
    break;
  case 2:
    for(u32 n = 0; n < 3; n++) {
      s32 f = channel(color, n), t = channel(texel, n);
      out[n] = ((255 - t) * f + t * channel(environment, n) + 255) >> down;
    }
    break;
  case 3:
    for(u32 n = 0; n < 3; n++) out[n] = channel(texel, n) * twice;
    alpha = withAlpha ? texelAlpha : fragmentAlpha;
    break;
  default:  //add (and 5-7, which act as add)
    for(u32 n = 0; n < 3; n++) out[n] = (channel(color, n) + channel(texel, n)) * twice;
    break;
  }
  return pack(out[0], out[1], out[2], alpha);
}

auto GE::textureFunction(u32 color, u32 texel) const -> u32 {
  u32 function = commands[TextureFunction] & 7;
  bool withAlpha = commands[TextureFunction] >> 8 & 1, doubled = commands[TextureFunction] >> 16 & 1;
  return textureFunctionWith(function, withAlpha, doubled, commands[TextureEnvironmentColor], color, texel);
}

//The texture function as a primitive's Look took it from the commands.
alwaysinline auto GE::combine(const Look& look, u32 color, u32 texel) const -> u32 {
  return textureFunctionWith(look.function, look.withAlpha, look.doubled, look.environment, color, texel);
}
