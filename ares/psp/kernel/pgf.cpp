//The PGF reader (pgf.hpp describes the format). Fonts are the user's own files, or a game's, so nothing in one is
//trusted: every offset and count is checked against the file before anything is read through it, and a glyph's
//record is read through a cursor that can't leave the file.

namespace {

//A cursor over a glyph's record: bits from each byte's lowest to its highest, byte after byte. Reading past the end
//of the font gives zeros and notes it, so a damaged record is found out rather than read beyond.
struct PgfBits {
  const u8* data;
  u64 size;          //bytes from data to the font's end
  u64 position = 0;  //in bits
  bool overrun = false;

  auto read(u32 count) -> u32 {
    u32 value = 0;
    for(u32 n = 0; n < count; n++, position++) {
      if(position >> 3 >= size) { overrun = true; continue; }
      value |= u32(data[position >> 3] >> (position & 7) & 1) << n;
    }
    return value;
  }

  //7 bits read as a signed number: -64 to 63
  auto signed7() -> s32 {
    s32 value = read(7);
    return value >= 64 ? value - 128 : value;
  }
};

auto pgfWord(const std::vector<u8>& bytes, u64 at) -> u32 {
  return u32(bytes[at]) | u32(bytes[at + 1]) << 8 | u32(bytes[at + 2]) << 16 | u32(bytes[at + 3]) << 24;
}

auto pgfHalf(const std::vector<u8>& bytes, u64 at) -> u32 {
  return u32(bytes[at]) | u32(bytes[at + 1]) << 8;
}

//A packed table of count entries of bits each takes whole 32-bit words.
auto pgfPacked(u64 count, u64 bits) -> u64 {
  return (count * bits + 31) / 32 * 4;
}

//A picture's run-length encoded shades (pgf.hpp: "The picture"), into glyph's pixels, row by row or column by column.
auto pgfPicture(PgfBits& bits, PGF::Glyph& glyph, bool columns) -> bool {
  u32 total = glyph.width * glyph.height, filled = 0;
  auto put = [&](u32 shade) {
    u32 x = columns ? filled / glyph.height : filled % glyph.width;
    u32 y = columns ? filled % glyph.height : filled / glyph.width;
    glyph.pixels[y * glyph.width + x] = shade;
    filled++;
  };
  while(filled < total) {
    u32 count = bits.read(4);
    if(count < 8) {
      u32 shade = bits.read(4);
      for(u32 n = 0; n <= count && filled < total; n++) put(shade);
    } else {
      for(u32 n = count; n < 16 && filled < total; n++) put(bits.read(4));
    }
    if(bits.overrun) return false;
  }
  return true;
}

}

auto PGF::open(std::vector<u8> data, std::string& error) -> bool {
  *this = {};
  if(data.size() < 392 || memcmp(&data[4], "PGF0", 4)) return error = "it isn't a PGF", false;
  revision = pgfWord(data, 0x08);
  if(revision != 2 && revision != 3) return error = "its revision isn't 2 or 3", false;
  u32 headerSize = pgfHalf(data, 0x02);
  if(headerSize < (revision == 3 ? 412u : 392u) || headerSize > data.size()) {
    return error = "its header's length is wrong", false;
  }
  charMapLength = pgfWord(data, 0x10);
  glyphCount = pgfWord(data, 0x14);
  charMapBits = pgfWord(data, 0x18);
  pointerBits = pgfWord(data, 0x1c);
  bitsPerPixel = data[0x22];
  //a character map covers 16-bit codes, so neither it nor the glyphs it points at go past 65536
  if(!glyphCount || glyphCount > 65536 || charMapLength > 65536) return error = "its glyph counts are wrong", false;
  if(!charMapBits || charMapBits > 32 || !pointerBits || pointerBits > 32) {
    return error = "its tables' entries have a size there can't be", false;
  }
  if(bitsPerPixel != 4) return error = "its pixels aren't 4 bits", false;
  for(u32 n : range(2)) {
    pointSize[n] = pgfWord(data, 0x24 + n * 4);
    resolution[n] = pgfWord(data, 0x2c + n * 4);
  }
  if(!resolution[0] || !resolution[1]) return error = "it has no resolution", false;
  auto text = [&](u32 at) {
    std::string value;
    for(u32 n = 0; n < 64 && data[at + n]; n++) value.push_back(char(data[at + n]));
    return value;
  };
  name = text(0x35);
  type = text(0x75);
  firstCode = pgfHalf(data, 0xb6);
  lastCode = pgfHalf(data, 0xb8);
  maxAscender = s32(pgfWord(data, 0xd4));
  maxDescender = s32(pgfWord(data, 0xd8));
  maxLeftX = s32(pgfWord(data, 0xdc));
  maxBaseY = s32(pgfWord(data, 0xe0));
  minCenterX = s32(pgfWord(data, 0xe4));
  maxTopY = s32(pgfWord(data, 0xe8));
  for(u32 n : range(2)) {
    maxAdvance[n] = s32(pgfWord(data, 0xec + n * 4));
    maxSize[n] = s32(pgfWord(data, 0xf4 + n * 4));
  }
  maxWidth = pgfHalf(data, 0xfc);
  maxHeight = pgfHalf(data, 0xfe);
  for(u32 n : range(4)) tableLength[n] = data[0x102 + n];
  shadowCount = pgfWord(data, 0x16c);
  shadowMapBits = pgfWord(data, 0x170);
  if(shadowCount > 65536 || (shadowCount && (!shadowMapBits || shadowMapBits > 32))) {
    return error = "its shadow map is wrong", false;
  }
  u32 rangeCount = revision == 3 ? pgfHalf(data, 0x18c) : 0;
  u32 partCount = revision == 3 ? pgfHalf(data, 0x194) : 0;

  //Where everything is, in the order pgf.hpp lists: each part must end inside the file, and the glyph records
  //start there (at least a byte of them).
  u64 at = headerSize;
  for(u32 n : range(4)) {
    tableAt[n] = at;
    at += u64(tableLength[n]) * 8;
  }
  u64 shadowMap = at;
  at += pgfPacked(shadowCount, shadowMapBits);
  u64 rangesAt = at;
  at += u64(rangeCount) * 4;
  u64 partsAt = at;
  at += u64(partCount) * 4;
  u64 charMap = at;
  at += pgfPacked(charMapLength, charMapBits);
  u64 pointers = at;
  at += pgfPacked(glyphCount, pointerBits);
  if(at >= data.size()) return error = "its tables run past its end", false;
  shadowMapAt = shadowMap;
  charMapAt = charMap;
  pointersAt = pointers;
  glyphsAt = at;
  u64 starts[10] = {tableAt[0], tableAt[1], tableAt[2], tableAt[3], shadowMap, rangesAt, partsAt, charMap, pointers,
                    at};
  for(u32 n : range(9)) parts[n] = {u32(starts[n]), u32(starts[n + 1] - starts[n])};

  //The codes the character map covers: from the first code to the last (revision 2), or its ranges (3).
  if(revision == 2) {
    if(lastCode >= firstCode) ranges.push_back({firstCode, lastCode - firstCode + 1});
  } else {
    for(u32 n : range(rangeCount)) {
      ranges.push_back({pgfHalf(data, rangesAt + n * 4), pgfHalf(data, rangesAt + n * 4 + 2)});
    }
    for(u32 n : range(partCount)) {
      partRanges.push_back({pgfHalf(data, partsAt + n * 4), pgfHalf(data, partsAt + n * 4 + 2)});
    }
  }
  bytes = std::move(data);
  return true;
}

//Entry index of a packed table at byte at, of bits each (the caller has checked it's one of the table's).
auto PGF::entry(u32 at, u32 bits, u32 index) const -> u32 {
  u64 position = u64(index) * bits;
  u32 value = 0;
  for(u32 n = 0; n < bits; n++, position++) value |= u32(bytes[at + (position >> 3)] >> (position & 7) & 1) << n;
  return value;
}

auto PGF::glyphFor(u32 code, bool parts) const -> s32 {
  if(!parts) {
    for(auto& range : partRanges) if(code >= range.first && code - range.first < range.count) return -1;
  }
  u64 base = 0;  //where the range's codes start in the character map
  for(auto& range : ranges) {
    if(code >= range.first && code - range.first < range.count) {
      u64 index = base + (code - range.first);
      if(index >= charMapLength) return -1;
      u32 glyph = entry(charMapAt, charMapBits, index);
      return glyph < glyphCount ? s32(glyph) : -1;
    }
    base += range.count;
  }
  return -1;
}

auto PGF::glyph(u32 index, Glyph& glyph) const -> bool {
  glyph = {};
  return record(index, glyph, true);
}

//A glyph's record (pgf.hpp: "A glyph record"). A composite's parts are drawn into its picture, each where its own
//left and top put it, added shade on shade (as libfont draws into a game's buffer) up to 15; a part can't be a
//composite itself (composite false), nor can a missing or damaged part spoil the rest.
auto PGF::record(u32 index, Glyph& glyph, bool composite) const -> bool {
  if(index >= glyphCount) return false;
  u64 start = glyphsAt + u64(entry(pointersAt, pointerBits, index)) * 4;
  if(start >= bytes.size()) return false;
  PgfBits bits{bytes.data() + start, bytes.size() - start};
  bits.read(14);  //the character part's length: only a shadow is looked for past it
  glyph.width = bits.read(7);
  glyph.height = bits.read(7);
  glyph.left = bits.signed7();
  glyph.top = bits.signed7();
  u32 flags = bits.read(6);
  glyph.shadowFlags = bits.read(7);
  glyph.shadowID = bits.read(9);
  s32* measures[4] = {glyph.dimension, glyph.bearingX, glyph.bearingY, glyph.advance};
  for(u32 n : range(4)) {
    if(flags >> (2 + n) & 1) {
      u32 number = bits.read(8);
      if(number >= tableLength[n]) return false;
      u64 at = tableAt[n] + u64(number) * 8;
      measures[n][0] = s32(pgfWord(bytes, at));
      measures[n][1] = s32(pgfWord(bytes, at + 4));
    } else {
      measures[n][0] = s32(bits.read(32));
      measures[n][1] = s32(bits.read(32));
    }
  }
  if(bits.overrun) return false;
  glyph.pixels.assign(glyph.width * glyph.height, 0);
  switch(flags & 3) {
  case 1: return pgfPicture(bits, glyph, false);
  case 2: return pgfPicture(bits, glyph, true);
  case 3: {
    if(!composite) return false;
    for(u32 parts = 0; parts < 3; parts++) {
      u32 code = bits.read(16);
      if(bits.overrun) return false;
      if(!code) break;
      s32 number = glyphFor(code, true);
      Glyph part;
      if(number < 0 || !record(number, part, false)) continue;
      s32 dx = part.left - glyph.left, dy = glyph.top - part.top;
      for(u32 y : range(part.height)) {
        for(u32 x : range(part.width)) {
          s64 px = s64(x) + dx, py = s64(y) + dy;
          if(px < 0 || py < 0 || px >= glyph.width || py >= glyph.height) continue;
          u8& shade = glyph.pixels[py * glyph.width + px];
          shade = std::min(15, shade + part.pixels[y * part.width + x]);
        }
      }
    }
    return true;
  }
  default: return true;  //no picture
  }
}

auto PGF::shadow(u32 id, Glyph& glyph) const -> bool {
  if(id >= shadowCount) return false;
  s32 host = glyphFor(entry(shadowMapAt, shadowMapBits, id), true);
  if(host < 0) return false;
  u64 start = glyphsAt + u64(entry(pointersAt, pointerBits, host)) * 4;
  if(start + 2 > bytes.size()) return false;
  start += pgfHalf(bytes, start) & 0x3fff;  //past the host's own part
  if(start >= bytes.size()) return false;
  PgfBits bits{bytes.data() + start, bytes.size() - start};
  bits.read(14);
  glyph.width = bits.read(7);
  glyph.height = bits.read(7);
  glyph.left = bits.signed7();
  glyph.top = bits.signed7();
  u32 flags = bits.read(6);
  if(bits.overrun || ((flags & 3) != 1 && (flags & 3) != 2)) return false;
  glyph.pixels.assign(glyph.width * glyph.height, 0);
  return pgfPicture(bits, glyph, (flags & 3) == 2);
}
