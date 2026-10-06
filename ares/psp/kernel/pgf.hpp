#pragma once

#include <string>
#include <vector>

//A PGF: the PSP's font format, the files in flash0:/font (and some games carry their own). Each holds one font at one
//size: a picture of every character it has, in 16 shades of grey, ready to be put on screen.
//
//Pictures this small (a character is about 20 pixels square) would waste room stored pixel by pixel, so a PGF packs
//everything as tightly as the numbers need, and shares what characters have in common:
//  - Tables of measurements. Many characters are the same width, or sit the same way on the line, so the font keeps
//    a short list of each kind of measurement and a character just says which entry is its own.
//  - A character map: for each character code (Unicode, 16 bits), which picture ("glyph") is that character's.
//  - The glyphs themselves, each a small record: its picture's size and place, its measurements, and the picture,
//    squeezed by run-length encoding (RLE: "seven blank pixels, then these three", rather than ten pixels one by
//    one).
//  - Shadows: a second, blurred picture to draw under a character, for text over busy backgrounds. Characters that
//    look alike share one, so the font keeps fewer shadows than glyphs, each carried in one glyph's record.
//
//Nothing in it is a whole byte unless it happens to be: a number takes as many bits as the font says (a 9-bit glyph
//number, a 14-bit offset), and the bits run from each byte's lowest bit to its highest, byte after byte, so a number
//can start part way into one byte and end in another. All the bigger numbers are little-endian. Sizes and positions
//are fixed point in 64ths (26.6: 648 is 10.125), of a point for font sizes and of a pixel for measurements.
//
//The layout, written from the owner's own firmware fonts (firmware 6.61: all eighteen, read field by field until
//every glyph and shadow in them decoded to exactly its record's length) and from what pspautotests' font tests
//recorded on a PSP (the measurements, pictures and shadows its libfont gave for a PGF of known contents):
//
//The header (392 bytes; 412 in revision 3, the Korean font's; its own length is the 16-bit number at 2):
//   0x04  "PGF0"
//   0x08  the revision (2, or 3)
//   0x10  how many entries the character map has     0x14  how many glyphs there are
//   0x18  bits in a character map entry               0x1c  bits in a glyph pointer
//   0x22  bits in a pixel (4: 16 shades)
//   0x24  the size across and (0x28) down, in 64ths of a point (648: 10.125 points)
//   0x2c  the resolution across and (0x30) down, in 64ths of a dot an inch (8192: 128)
//   0x35  the font's name (64 bytes: "FTT-NewRodin Pro Latin")    0x75  its type ("Regular", "Bold Italic"...)
//   0xb6  the first character code it maps, and (0xb8) the last
//   0xd4  the biggest ascender, descender, left x, base y, the smallest center x, the biggest top y, advance across
//         and down, width and height of any glyph (each 32 bits, in 64ths of a pixel)
//   0xfc  the widest and tallest picture, in pixels (16 bits each)
//   0x102 how many entries each table of measurements has: dimensions, x adjustments, y adjustments, advances
//   0x16c how many shadows there are, and (0x170) bits in a shadow map entry
//   revision 3: 0x18c how many ranges of character codes the character map covers, 0x194 how many in a second list
//The rest of the header (what's at 0x20, 0xba, 0x174-0x187 and the like) is the same in every firmware font and
//nothing here needs it.
//
//Then, one after another:
//  - the four tables of measurements, each entry two 32-bit numbers (across, then down): dimensions (a glyph's size
//    as drawn), x adjustments and y adjustments (where the picture sits from the pen: its bearings), advances (how
//    far the pen moves on after it);
//  - the shadow map: one character code per shadow, saying whose glyph record carries that shadow;
//  - revision 3: the character map's ranges (a 16-bit first code and a 16-bit count each: the character map lists
//    those ranges' codes one after another), then the second list (ranges of codes the font uses only as parts of
//    other characters: see the composite glyphs below);
//  - the character map: a glyph number for each character code from the first (revision 2) or in the ranges
//    (revision 3); a number past the last glyph (all ones) means the font hasn't got that character;
//  - the glyph pointers: where each glyph's record starts, counted in 4-byte words from the first glyph's;
//  - the glyph records.
//The shadow map, the character map and the pointers are packed tables (each entry the font's number of bits, the
//table padded to whole 32-bit words).
//
//A glyph record, field by field (bits, as above):
//   14  how many bytes the character's own part takes: its shadow, if it carries one, follows there
//    7  the picture's width, 7 its height (in pixels)
//    7  its left edge from the pen, 7 its top edge above the baseline (both signed: -64 to 63)
//    6  flags: the lowest two say how the picture is stored (1 row by row, 2 column by column, 3 a composite), and
//       the next four whether the dimensions, x adjustment, y adjustment and advance are an entry of their table
//    7  shadow flags (what they mean isn't known; reported as they are)
//    9  its shadow's number: an entry of the shadow map
//   then each of the four measurements: an 8-bit entry number if its flag says so, else its two values (32 bits
//   each, across then down)
//   then the picture.
//A shadow's record starts the same way (the 14, 7, 7, 7, 7 and 6 bits), its picture straight after.
//
//The picture (RLE, ELI5): the pixels come in the order the flags say (row by row: across the top row, then the next;
//column by column: down the left column, then the next), as a stream of 4-bit nibbles, low nibble of each byte first.
//Read a nibble n. If n is below 8, the next nibble is a shade, and it's repeated n + 1 times ("0 0 0 0 0": 4, 0).
//If n is 8 or more, the next 16 - n nibbles are shades, each one pixel ("15 9 3": 13, 15, 9, 3). Until the picture is
//full. A shade is 0 (nothing) to 15 (fully drawn).
//
//A composite (Korean's syllables, each built from two or three letter shapes): instead of a picture, up to three
//16-bit character codes (0 ends the list early), each a glyph drawn at its own place (its left and top) inside the
//composite's picture.

namespace ares::PlayStationPortable {

struct PGF {
  //What a glyph is: its picture and its measurements, as libfont reports them (the shadows' measurements are their
  //character's: only the picture and its place are a shadow's own).
  struct Glyph {
    u32 width = 0, height = 0;    //the picture, in pixels
    s32 left = 0, top = 0;        //its top left corner from the pen: across, and up from the baseline
    s32 dimension[2] = {};        //each pair across, then down, in 64ths of a pixel
    s32 bearingX[2] = {};
    s32 bearingY[2] = {};
    s32 advance[2] = {};
    u32 shadowFlags = 0, shadowID = 0;
    std::vector<u8> pixels;       //width * height shades, 0 to 15, row by row
  };

  //Reads a font from its bytes (a file's whole contents); false, with error saying why, if they aren't a PGF this
  //can read: the header, every table and the start of the glyph records must lie inside them. A glyph is checked as
  //it's read (glyph()), so a font damaged further in still opens, and that glyph alone is missing.
  auto open(std::vector<u8> data, std::string& error) -> bool;

  //The glyph a character code maps to, or -1. parts: the codes revision 3 keeps for composites' parts count too
  //(only a composite asks for those).
  auto glyphFor(u32 code, bool parts = false) const -> s32;

  //Glyph number index, picture and all; false if its record is damaged (runs past the font's end, or names an entry
  //its table hasn't got).
  auto glyph(u32 index, Glyph& glyph) const -> bool;

  //Shadow number id: its picture and place (glyph's other fields are left as they were); false if there's no such
  //shadow or its record is damaged.
  auto shadow(u32 id, Glyph& glyph) const -> bool;

  auto size() const -> u32 { return bytes.size(); }
  auto data() const -> const std::vector<u8>& { return bytes; }

  //The parts libfont reads into the program's memory when it opens a font from a file a piece at a time
  //(sceFontOpen's mode 0), in the order they're in the file: the four tables of measurements, the shadow map,
  //revision 3's two lists of ranges (none in revision 2), the character map and the pointers.
  struct Span { u32 at = 0, length = 0; };
  Span parts[9];

  //from the header
  u32 revision = 0;
  u32 glyphCount = 0;
  u32 shadowCount = 0;
  u32 bitsPerPixel = 0;
  u32 pointSize[2] = {};          //in 64ths of a point, across and down
  u32 resolution[2] = {};         //in 64ths of a dot an inch
  std::string name, type;
  u32 firstCode = 0, lastCode = 0;
  s32 maxAscender = 0, maxDescender = 0, maxLeftX = 0, maxBaseY = 0, minCenterX = 0, maxTopY = 0;
  s32 maxAdvance[2] = {}, maxSize[2] = {};  //in 64ths of a pixel
  u32 maxWidth = 0, maxHeight = 0;          //in pixels

private:
  struct Range { u32 first, count; };
  std::vector<u8> bytes;
  u32 tableAt[4] = {}, tableLength[4] = {};  //dimensions, x adjustments, y adjustments, advances
  u32 shadowMapAt = 0, shadowMapBits = 0;
  u32 charMapAt = 0, charMapLength = 0, charMapBits = 0;
  u32 pointersAt = 0, pointerBits = 0;
  u32 glyphsAt = 0;
  std::vector<Range> ranges;      //the codes the character map covers, in its order
  std::vector<Range> partRanges;  //revision 3: those kept for composites' parts

  auto entry(u32 at, u32 bits, u32 index) const -> u32;
  auto record(u32 index, Glyph& glyph, bool composite) const -> bool;
};

}
