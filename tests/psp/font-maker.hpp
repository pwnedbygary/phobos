//Fonts made for the tests from pgf.hpp's description of the PGF format, so no font is in the repository: a writer
//(bits packed lowest first, pictures run-length encoded, glyph records, the tables), and the PSP's eighteen fonts as
//stand-ins (their names, sizes and order, a few glyphs each) in a folder the kernel reads them from.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace pgf_maker {

//Bits written lowest first, byte after byte, as a PGF packs them.
struct PgfBits {
  std::vector<std::uint8_t> bytes;
  std::uint64_t at = 0;
  auto put(std::uint64_t value, std::uint32_t count) -> void {
    for(std::uint32_t n = 0; n < count; n++, at++) {
      if(at / 8 >= bytes.size()) bytes.push_back(0);
      if(value >> n & 1) bytes[at / 8] |= 1 << at % 8;
    }
  }
  auto patch(std::uint64_t position, std::uint32_t value, std::uint32_t count) -> void {
    for(std::uint32_t n = 0; n < count; n++, position++) {
      bytes[position / 8] = (bytes[position / 8] & ~(1 << position % 8)) | (value >> n & 1) << position % 8;
    }
  }
  auto pad(std::uint32_t multiple) -> void { while(at % multiple) put(0, 1); }
};

//Shades as a PGF's pictures store them (pgf.hpp, "The picture"): a run of one shade as its count less one (below 8)
//and the shade; anything else as 16 less a count of up to 8, then those shades.
inline auto rle(PgfBits& out, const std::vector<std::uint8_t>& shades) -> void {
  std::uint32_t n = 0;
  while(n < shades.size()) {
    std::uint32_t run = 1;
    while(n + run < shades.size() && run < 8 && shades[n + run] == shades[n]) run++;
    if(run >= 2) {
      out.put(run - 1, 4);
      out.put(shades[n], 4);
      n += run;
      continue;
    }
    std::uint32_t count = 1;
    while(n + count < shades.size() && count < 8 &&
          !(n + count + 1 < shades.size() && shades[n + count] == shades[n + count + 1])) count++;
    out.put(16 - count, 4);
    for(std::uint32_t k = 0; k < count; k++) out.put(shades[n + k], 4);
    n += count;
  }
}

//A glyph of a font made here.
struct TestGlyph {
  std::uint16_t code = 0;
  std::uint32_t width = 0, height = 0;
  std::int32_t left = 0, top = 0;
  std::vector<std::uint8_t> pixels;            //row by row
  bool columns = false;              //stored column by column
  std::int32_t metrics[4][2] = {};            //dimensions, x adjustment, y adjustment, advance
  std::int32_t entries[4] = {-1, -1, -1, -1}; //each an entry of its table instead (-1: written out)
  std::uint32_t shadowFlags = 0, shadowID = 0;
  std::vector<std::uint16_t> parts;            //a composite's characters
  bool carriesShadow = false;        //its shadow follows it
  std::uint32_t shadowWidth = 0, shadowHeight = 0;
  std::int32_t shadowLeft = 0, shadowTop = 0;
  std::vector<std::uint8_t> shadowPixels;
  bool shadowColumns = false;
};

//A font made here, as pgf.hpp lays one out.
struct TestFont {
  std::uint32_t revision = 2;
  std::string name = "Test Sans", type = "Regular";
  std::uint32_t pointSize = 648, resolution = 8192;
  std::uint32_t first = 0x20, last = 0x7e;
  std::int32_t maxima[10] = {863, -196, 195, 1202, -727, 18, 1585, 1094, 1455, 1222};
  std::uint32_t maxWidth = 23, maxHeight = 20;
  std::vector<std::pair<std::int32_t, std::int32_t>> tables[4];
  std::vector<TestGlyph> glyphs;
  std::vector<std::uint16_t> shadowCodes;                      //the shadow map
  std::vector<std::pair<std::uint16_t, std::uint16_t>> ranges, partRanges;  //revision 3's

  static auto bitsFor(std::uint64_t largest) -> std::uint32_t {
    std::uint32_t bits = 1;
    while(bits < 32 && largest >> bits) bits++;
    return bits;
  }

  static auto packed(const std::vector<std::uint32_t>& values, std::uint32_t bits) -> std::vector<std::uint8_t> {
    PgfBits out;
    for(std::uint32_t value : values) out.put(value, bits);
    out.pad(32);
    return out.bytes;
  }

  static auto record(const TestGlyph& g) -> std::vector<std::uint8_t> {
    PgfBits out;
    out.put(0, 14);  //its length, below
    out.put(g.width, 7);
    out.put(g.height, 7);
    out.put(std::uint32_t(g.left) & 127, 7);
    out.put(std::uint32_t(g.top) & 127, 7);
    std::uint32_t kind = !g.parts.empty() ? 3 : g.columns ? 2 : 1;
    std::uint32_t flags = kind;
    for(std::uint32_t n = 0; n < 4; n++) if(g.entries[n] >= 0) flags |= 1 << (2 + n);
    out.put(flags, 6);
    out.put(g.shadowFlags, 7);
    out.put(g.shadowID, 9);
    for(std::uint32_t n = 0; n < 4; n++) {
      if(g.entries[n] >= 0) out.put(g.entries[n], 8);
      else out.put(std::uint32_t(g.metrics[n][0]), 32), out.put(std::uint32_t(g.metrics[n][1]), 32);
    }
    auto order = [](const std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t height,
                    bool columns) {
      std::vector<std::uint8_t> shades;
      for(std::uint32_t a = 0; a < (columns ? width : height); a++) {
        for(std::uint32_t b = 0; b < (columns ? height : width); b++) {
          shades.push_back(pixels[columns ? b * width + a : a * width + b]);
        }
      }
      return shades;
    };
    if(kind == 3) {
      for(std::uint16_t code : g.parts) out.put(code, 16);
      if(g.parts.size() < 3) out.put(0, 16);
    } else {
      rle(out, order(g.pixels, g.width, g.height, g.columns));
    }
    out.pad(8);
    out.patch(0, out.at / 8, 14);
    if(g.carriesShadow) {
      std::uint64_t start = out.at;
      out.put(0, 14);
      out.put(g.shadowWidth, 7);
      out.put(g.shadowHeight, 7);
      out.put(std::uint32_t(g.shadowLeft) & 127, 7);
      out.put(std::uint32_t(g.shadowTop) & 127, 7);
      out.put(g.shadowColumns ? 2 : 1, 6);
      rle(out, order(g.shadowPixels, g.shadowWidth, g.shadowHeight, g.shadowColumns));
      out.pad(8);
      out.patch(start, (out.at - start) / 8, 14);
    }
    out.pad(32);
    return out.bytes;
  }

  auto build() const -> std::vector<std::uint8_t> {
    std::uint32_t headerSize = revision == 3 ? 412 : 392;
    std::vector<std::vector<std::uint8_t>> records;
    std::vector<std::uint32_t> pointers;
    std::uint32_t offset = 0;
    for(auto& g : glyphs) {
      records.push_back(record(g));
      pointers.push_back(offset / 4);
      offset += records.back().size();
    }
    std::vector<std::uint32_t> codes;  //what the character map covers, in its order
    if(revision == 2) for(std::uint32_t code = first; code <= last; code++) codes.push_back(code);
    for(auto [start, count] : ranges) for(std::uint32_t n = 0; n < count; n++) codes.push_back(start + n);
    std::uint32_t none = (1u << bitsFor(glyphs.size())) - 1;
    std::vector<std::uint32_t> charMap;
    for(std::uint32_t code : codes) {
      std::uint32_t glyph = none;
      for(std::uint32_t n = 0; n < glyphs.size(); n++) if(glyphs[n].code == code) { glyph = n; break; }
      charMap.push_back(glyph);
    }
    std::uint32_t charMapBits = bitsFor(glyphs.size());
    std::uint32_t pointerBits = bitsFor(pointers.empty() ? 0 : pointers.back());
    std::vector<std::uint8_t> data(headerSize);
    auto w16 = [&](std::uint32_t at, std::uint32_t value) { data[at] = value; data[at + 1] = value >> 8; };
    auto w32 = [&](std::uint32_t at, std::uint32_t value) {
      for(std::uint32_t n = 0; n < 4; n++) data[at + n] = value >> n * 8;
    };
    w16(0x02, headerSize);
    memcpy(&data[0x04], "PGF0", 4);
    w32(0x08, revision);
    w32(0x0c, 6);
    w32(0x10, charMap.size());
    w32(0x14, glyphs.size());
    w32(0x18, charMapBits);
    w32(0x1c, pointerBits);
    data[0x20] = data[0x21] = data[0x22] = 4;
    w32(0x24, pointSize), w32(0x28, pointSize), w32(0x2c, resolution), w32(0x30, resolution);
    memcpy(&data[0x35], name.data(), name.size());
    memcpy(&data[0x75], type.data(), type.size());
    w16(0xb6, revision == 3 && !ranges.empty() ? ranges.front().first : first);
    w16(0xb8, revision == 3 && !ranges.empty() ? ranges.back().first + ranges.back().second - 1 : last);
    for(std::uint32_t n = 0; n < 10; n++) w32(0xd4 + n * 4, maxima[n]);
    w16(0xfc, maxWidth);
    w16(0xfe, maxHeight);
    for(std::uint32_t n = 0; n < 4; n++) data[0x102 + n] = tables[n].size();
    w32(0x16c, shadowCodes.size());
    w32(0x170, 16);
    w32(0x174, 1540), w32(0x178, 32), w32(0x17c, 32), w32(0x180, 960), w32(0x184, 960);
    if(revision == 3) {
      w32(0x188, 16), w16(0x18c, ranges.size()), w16(0x18e, 4);
      w32(0x190, 16), w16(0x194, partRanges.size()), w16(0x196, 4), w32(0x198, 3);
    }
    auto append = [&](const std::vector<std::uint8_t>& bytes) {
      data.insert(data.end(), bytes.begin(), bytes.end());
    };
    for(auto& table : tables) {
      for(auto [across, down] : table) {
        for(std::int32_t value : {across, down}) {
          for(std::uint32_t n = 0; n < 4; n++) data.push_back(std::uint32_t(value) >> n * 8);
        }
      }
    }
    append(packed({shadowCodes.begin(), shadowCodes.end()}, 16));
    for(auto* list : {&ranges, &partRanges}) {
      for(auto [start, count] : *list) {
        for(std::uint32_t value : {std::uint32_t(start), std::uint32_t(count)}) {
          data.push_back(value);
          data.push_back(value >> 8);
        }
      }
    }
    append(packed(charMap, charMapBits));
    append(packed(pointers, pointerBits));
    for(auto& r : records) append(r);
    return data;
  }
};

//pspautotests' charglyphimage: the 'A' of its ltn0.pgf as a PSP drew it at (0, 0), a byte a pixel (17 times its
//shade), 16 by 17, from 0 across and 17 up; and the '_' (14 by 2, 0 across and -3 up), as its charinfo recorded.
inline const char* recordedA[17] = {
  "00000000000066ffff88000000000000", "000000000000ddffffee000000000000", "000000000033ffccbbff440000000000",
  "000000000099ff7766ffbb0000000000", "0000000000ffff1111ffff1100000000", "0000000066ffbb0000aaff7700000000",
  "00000000ccff55000044ffdd00000000", "00000033ffff00000000eeff44000000", "00000099ff990000000088ffaa000000",
  "000000ffff330000000022ffff110000", "000066ffffccccccccccccffff660000", "0000ccffffffffffffffffffffdd0000",
  "0022ffff1100000000000000ffff3300", "0099ffbb0000000000000000aaff9900", "00eeff55000000000000000044ffff00",
  "55ffee00000000000000000000eeff66", "ccff990000000000000000000088ffcc",
};

inline auto shadesOf(const char* const* rows, std::uint32_t width, std::uint32_t height)
-> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> shades;
  for(std::uint32_t y = 0; y < height; y++) {
    for(std::uint32_t x = 0; x < width; x++) {
      shades.push_back(std::stoul(std::string(rows[y] + x * 2, 2), nullptr, 16) / 17);
    }
  }
  return shades;
}

//A blurred blob for a shadow: brightest in the middle.
inline auto blob(std::uint32_t width, std::uint32_t height) -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> shades;
  for(std::uint32_t y = 0; y < height; y++) {
    for(std::uint32_t x = 0; x < width; x++) {
      std::int32_t dx = 2 * std::int32_t(x) - std::int32_t(width) + 1;
      std::int32_t dy = 2 * std::int32_t(y) - std::int32_t(height) + 1;
      shades.push_back(std::max(0, 8 - (std::abs(dx) + std::abs(dy)) / 3));
    }
  }
  return shades;
}

//The PSP's eighteen fonts, in its order (the files' names), made here: each a few glyphs, its sizes and names as
//the PSP's list has them (7 points for ltn8 to ltn15), the Korean one a revision 3 font with composites. Font 1 (the
//place pspautotests' ltn0.pgf's tests stand in for) has the recorded 'A' and '_' and their measurements, the 'A'
//stored column by column as that font stores it; every font's 'B' differs, so each font's bytes are its own.
inline const char* systemFiles[18] = {"jpn0.pgf", "ltn0.pgf", "ltn1.pgf", "ltn2.pgf", "ltn3.pgf", "ltn4.pgf",
  "ltn5.pgf", "ltn6.pgf", "ltn7.pgf", "ltn8.pgf", "ltn9.pgf", "ltn10.pgf", "ltn11.pgf", "ltn12.pgf", "ltn13.pgf",
  "ltn14.pgf", "ltn15.pgf", "kr0.pgf"};

inline auto systemFont(std::uint32_t index) -> TestFont {
  TestFont font;
  font.name = index == 0 ? "FTT-NewRodin Pro DB" : index == 17 ? "AsiaKNHH-SONY-uni"
            : index % 2 ? "FTT-NewRodin Pro Latin" : "FTT-Matisse Pro Latin";
  if(index >= 9 && index <= 16) font.pointSize = 448;  //7 points
  font.tables[0] = {{0, 0}, {790, 914}};
  font.tables[1] = {{-44, -204}, {10, -444}};
  font.tables[2] = {{-137, 17}, {649, 17}};
  font.tables[3] = {{1138, 1094}, {993, 1094}, {416, 1094}};
  //shadow n is carried by code 0x20 + n's glyph: 'A' 33, '_' 63
  for(std::uint32_t n = 0x20; n < 0x60; n++) font.shadowCodes.push_back(n);
  TestGlyph space;
  space.code = 0x20;
  space.metrics[3][0] = 416, space.metrics[3][1] = 1094;
  space.shadowID = 33;
  font.glyphs.push_back(space);
  TestGlyph a;
  a.code = 'A', a.width = 16, a.height = 17, a.left = 0, a.top = 17, a.columns = true;
  a.pixels = shadesOf(recordedA, 16, 17);
  a.entries[0] = a.entries[1] = a.entries[2] = 0, a.entries[3] = 0;  //charinfo's: dimension, adjustments 0; 1138
  a.shadowFlags = 0x51, a.shadowID = 33;
  a.carriesShadow = true, a.shadowWidth = 16, a.shadowHeight = 16, a.shadowLeft = -4, a.shadowTop = 12;
  a.shadowPixels = blob(16, 16);
  font.glyphs.push_back(a);
  TestGlyph b;
  b.code = 'B', b.width = 5, b.height = 4, b.left = 1, b.top = 9;
  for(std::uint32_t n = 0; n < 20; n++) b.pixels.push_back((n * 7 + index) % 16);
  b.metrics[0][0] = 300 + index, b.metrics[0][1] = 600;  //written out, not table entries
  b.metrics[1][0] = 64, b.metrics[1][1] = -128;
  b.metrics[2][0] = 576, b.metrics[2][1] = 17;
  b.metrics[3][0] = 700, b.metrics[3][1] = 1094;
  b.shadowID = 33;
  font.glyphs.push_back(b);
  TestGlyph underscore;
  underscore.code = '_', underscore.width = 14, underscore.height = 2, underscore.left = 0, underscore.top = -3;
  underscore.pixels = std::vector<std::uint8_t>(14, 9);
  underscore.pixels.insert(underscore.pixels.end(), 14, 15);
  underscore.entries[0] = underscore.entries[1] = underscore.entries[2] = 0, underscore.entries[3] = 1;
  underscore.shadowFlags = 0x4e, underscore.shadowID = 63;
  underscore.carriesShadow = true, underscore.shadowWidth = 15, underscore.shadowHeight = 9;
  underscore.shadowLeft = -4, underscore.shadowTop = 3, underscore.shadowColumns = true;
  underscore.shadowPixels = blob(15, 9);
  font.glyphs.push_back(underscore);
  if(index == 17) {
    //Korean: a revision 3 font whose syllable 0xac00 is two parts (0x1100 and 0x1101, kept for composites alone),
    //0xac01 a part that isn't there, then a composite as a part (each passed over), then 0x1101
    font.revision = 3;
    font.ranges = {{0x20, 0x40}, {0x1100, 3}, {0xac00, 2}};
    font.partRanges = {{0x1100, 3}};
    TestGlyph first, second, syllable, odd;
    first.code = 0x1100, first.width = 4, first.height = 4, first.left = 1, first.top = 10;
    first.pixels = std::vector<std::uint8_t>(16, 12);
    second.code = 0x1101, second.width = 2, second.height = 8, second.left = 8, second.top = 12;
    second.pixels = std::vector<std::uint8_t>(16, 10);
    syllable.code = 0xac00, syllable.width = 12, syllable.height = 12, syllable.left = 0, syllable.top = 12;
    syllable.parts = {0x1100, 0x1101};
    odd.code = 0xac01, odd.width = 12, odd.height = 12, odd.left = 0, odd.top = 12;
    odd.parts = {0x1150, 0xac00, 0x1101};
    for(auto* g : {&first, &second, &syllable, &odd}) {
      g->metrics[3][0] = 1250, g->metrics[3][1] = 1365;
      font.glyphs.push_back(*g);
    }
  }
  return font;
}

//A folder holding the fonts (all eighteen, or those listed).
struct FontFolder {
  std::filesystem::path path;
  FontFolder(std::vector<std::uint32_t> which = {}) {
    std::random_device random;
    path = std::filesystem::temp_directory_path() / ("phobos-psp-fonts-" + std::to_string(random()));
    std::filesystem::create_directories(path);
    if(which.empty()) for(std::uint32_t n = 0; n < 18; n++) which.push_back(n);
    for(std::uint32_t n : which) {
      auto bytes = systemFont(n).build();
      put(systemFiles[n], std::string(bytes.begin(), bytes.end()));
    }
  }
  FontFolder(const FontFolder&) = delete;
  ~FontFolder() { std::error_code error; std::filesystem::remove_all(path, error); }
  auto put(const std::string& name, const std::string& bytes) const -> void {
    std::ofstream(path / name, std::ios::binary) << bytes;
  }
  auto get(const std::string& name) const -> std::string {
    std::ifstream file(path / name, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  }
};

}
