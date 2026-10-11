//The font library's tests. PGF fonts are made here from pgf.hpp's description of the format, so no font is in the
//repository: read back by the reader, refused or read safely when damaged; then sceLibFont driven by programs, end to
//end: its memory from the program's own alloc and free, its list of fonts, finding and opening them, measurements and
//pictures drawn into the program's buffers in each pixel format, and save states part way through. What a PSP does is
//what pspautotests' font tests recorded, quoted where a test checks it.
#include "kernel-machine.hpp"
#include "font-maker.hpp"

namespace allegrex_test::psp {

using namespace pgf_maker;

using ares::PlayStationPortable::PGF;

namespace {

//Where the tests' programs keep things.
constexpr u32 Code = 0x0880'0000, Alloc = 0x0880'4000, Free = 0x0880'4800, SlowAlloc = 0x0880'5000;
constexpr u32 Heap = 0x0894'0000, HeapTop = 0x0893'0000, Log = 0x0893'1000;
constexpr u32 Parameters = 0x0893'8000, Style = 0x0893'9000, Image = 0x0893'a000, Buffer = 0x0893'b000;
constexpr u32 Answer = 0x0893'e000, Error = 0x0893'e010;

//The program's memory functions, as a game's: alloc(user data, size) hands out from a heap (16 bytes at a time,
//after the last), free(user data, block) gives nothing back; both write what they did to the log (a count, the call
//it fails at, then pairs: 1 and the size, or 2 and the block). The slow alloc waits 100 microseconds first.
auto memoryFunctions(KernelMachine& m) -> void {
  auto logged = [&](Assembler& a, u32 kind, u32 value) {
    a.li(t0, Log);
    a.put(lw(t1, 0, t0));        //how many calls so far
    a.put(sll(t2, t1, 3));
    a.put(addu(t2, t2, t0));
    a.li(t3, kind);
    a.put(sw(t3, 8, t2));
    a.put(sw(value, 12, t2));
    a.put(addiu(t1, t1, 1));
    a.put(sw(t1, 0, t0));
  };
  Assembler alloc{m, Alloc};
  logged(alloc, 1, a1);
  alloc.li(t0, Log);
  alloc.put(lw(t4, 4, t0));      //the call it fails at (0: none)
  alloc.put(lw(t1, 0, t0));
  alloc.put(bne(t4, t1, 3));
  alloc.put(nop);
  alloc.put(jr(ra));
  alloc.put(addu(v0, zero, zero));
  alloc.li(t0, HeapTop);
  alloc.put(lw(v0, 0, t0));
  alloc.put(addiu(t1, a1, 15));
  alloc.put(srl(t1, t1, 4));
  alloc.put(sll(t1, t1, 4));
  alloc.put(addu(t1, t1, v0));
  alloc.put(jr(ra));
  alloc.put(sw(t1, 0, t0));
  Assembler free{m, Free};
  logged(free, 2, a1);
  free.put(jr(ra));
  free.put(nop);
  Assembler slow{m, SlowAlloc};
  slow.put(addiu(sp, sp, -16));
  slow.put(sw(ra, 0, sp));
  slow.put(sw(a0, 4, sp));
  slow.put(sw(a1, 8, sp));
  slow.li(a0, 100);
  slow.call("sceKernelDelayThread");
  slow.put(lw(a0, 4, sp));
  slow.put(lw(a1, 8, sp));
  slow.put(jal(Alloc));
  slow.put(nop);
  slow.put(lw(ra, 0, sp));
  slow.put(jr(ra));
  slow.put(addiu(sp, sp, 16));
  m.system.memory.write(4, HeapTop, Heap);
  m.system.memory.write(4, Log, 0);
  m.system.memory.write(4, Log + 4, 0);
}

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

//The log's calls since entry `from`: each its kind (1 alloc, 2 free) and size or block.
auto logged(KernelMachine& m, u32 from = 0) -> std::vector<std::pair<u32, u32>> {
  std::vector<std::pair<u32, u32>> calls;
  for(u32 n = from; n < word(m, Log); n++) calls.push_back({word(m, Log + 8 + n * 8), word(m, Log + 12 + n * 8)});
  return calls;
}

//Calls a library function from a program (a thread, which runs the program's memory functions for it), the
//arguments in a0-a3 and t0-t3: its result (and its error, where it took one, at Error).
auto viaProgram(KernelMachine& m, const char* name, std::initializer_list<u32> arguments, bool recompile = false)
-> u32 {
  Assembler a{m, Code};
  u32 n = 0;
  for(u32 value : arguments) a.li(n < 4 ? a0 + n : t0 + n - 4, value), n++;
  a.call(name);
  a.li(t9, Answer);
  a.put(sw(v0, 0, t9));
  a.call("sceKernelExitThread");
  m.runProgram(Code, recompile);
  return word(m, Answer);
}

//The parameters sceFontNewLib takes: user data, how many fonts, then the memory functions (the rest 0).
auto parameters(KernelMachine& m, u32 fonts, u32 alloc = Alloc, u32 free = Free) -> u32 {
  m.system.memory.fill(Parameters, 0, 0x2c);
  m.system.memory.write(4, Parameters + 0x00, 0x1234'5678);
  m.system.memory.write(4, Parameters + 0x04, fonts);
  m.system.memory.write(4, Parameters + 0x0c, alloc);
  m.system.memory.write(4, Parameters + 0x10, free);
  return Parameters;
}

//A machine with the fonts in a folder and the program's memory functions, and a library made.
struct FontMachine {
  KernelMachine m;
  u32 library = 0;
  FontMachine(const FontFolder& folder, u32 fonts = 4) {
    m.kernel.fontsFrom(folder.path.string());
    memoryFunctions(m);
    library = viaProgram(m, "sceFontNewLib", {parameters(m, fonts), Error});
  }
};

auto floatOf(u32 bits) -> float { return std::bit_cast<float>(bits); }

}

//Fonts made here read back as they were made: the header, the character map (its first code to its last; a code
//it doesn't map, or outside it, maps to nothing), each glyph's picture stored row by row and column by column, its
//measurements written out and as table entries, shadows, and a revision 3 font's composites built from their parts.
static auto pgfReadBack() -> void {
  for(u32 index : {1u, 9u, 17u}) {
    auto made = systemFont(index);
    PGF pgf;
    std::string error;
    CHECK(pgf.open(made.build(), error), true);
    CHECK(pgf.revision, index == 17 ? 3 : 2);
    CHECK(pgf.name == made.name && pgf.type == "Regular", true);
    CHECK(pgf.pointSize[0], index == 9 ? 448 : 648);
    CHECK(pgf.resolution[1], 8192);
    CHECK(pgf.glyphCount, made.glyphs.size());
    CHECK(pgf.shadowCount, 64);
    CHECK(pgf.maxSize[0] == 1455 && pgf.maxDescender == -196 && pgf.maxWidth == 23 && pgf.bitsPerPixel == 4, true);
    CHECK(pgf.glyphFor('A'), 1);
    CHECK(pgf.glyphFor('C'), -1);     //in the map's range, but no glyph
    CHECK(pgf.glyphFor(0x1f), -1);    //below its first code
    CHECK(pgf.glyphFor(0x7f), -1);    //past its last
    for(u32 n : range(made.glyphs.size())) {
      auto& g = made.glyphs[n];
      PGF::Glyph glyph;
      CHECK(pgf.glyph(n, glyph), true);
      CHECK(glyph.width == g.width && glyph.height == g.height && glyph.left == g.left && glyph.top == g.top, true);
      CHECK(glyph.shadowFlags == g.shadowFlags && glyph.shadowID == g.shadowID, true);
      for(u32 k : range(4)) {
        s32 across = g.entries[k] >= 0 ? made.tables[k][g.entries[k]].first : g.metrics[k][0];
        s32 down = g.entries[k] >= 0 ? made.tables[k][g.entries[k]].second : g.metrics[k][1];
        s32* measured[4] = {glyph.dimension, glyph.bearingX, glyph.bearingY, glyph.advance};
        check(__LINE__, "a measurement", measured[k][0] == across && measured[k][1] == down, true);
      }
      if(g.parts.empty()) check(__LINE__, "a picture", glyph.pixels == g.pixels, true);
      if(g.carriesShadow) {
        PGF::Glyph shadow;
        CHECK(pgf.shadow(g.shadowID, shadow), true);
        CHECK(shadow.width == g.shadowWidth && shadow.height == g.shadowHeight, true);
        CHECK(shadow.left == g.shadowLeft && shadow.top == g.shadowTop && shadow.pixels == g.shadowPixels, true);
      }
    }
    PGF::Glyph none;
    CHECK(pgf.shadow(64, none), false);  //past the shadow map
    if(index != 17) continue;
    //the parts are kept for composites: no character of their own; the composite is them, each at its place
    CHECK(pgf.glyphFor(0x1100), -1);
    CHECK(pgf.glyphFor(0x1100, true) >= 0, true);
    PGF::Glyph syllable, odd;
    CHECK(pgf.glyph(pgf.glyphFor(0xac00), syllable), true);
    CHECK(pgf.glyph(pgf.glyphFor(0xac01), odd), true);
    std::vector<u8> expected(144, 0);
    for(u32 y : range(4)) for(u32 x : range(4)) expected[(2 + y) * 12 + 1 + x] = 12;  //1 across, 12 - 10 down
    for(u32 y : range(8)) for(u32 x : range(2)) expected[y * 12 + 8 + x] = 10;
    CHECK(syllable.pixels == expected, true);
    for(u32 y : range(4)) for(u32 x : range(4)) expected[(2 + y) * 12 + 1 + x] = 0;
    CHECK(odd.pixels == expected, true);  //only 0x1101: a missing part and a composite part passed over
  }
  //where parts overlap, their shades add up to 15
  TestFont font;
  font.revision = 3;
  font.ranges = {{0x41, 3}};
  TestGlyph one, two, both;
  one.code = 0x41, one.width = one.height = 2, one.pixels = {9, 9, 9, 9}, one.top = 2;
  two.code = 0x42, two.width = two.height = 2, two.pixels = {9, 3, 9, 3}, two.left = 1, two.top = 2;
  both.code = 0x43, both.width = 3, both.height = 2, both.top = 2, both.parts = {0x41, 0x42};
  font.glyphs = {one, two, both};
  PGF pgf;
  std::string error;
  CHECK(pgf.open(font.build(), error), true);
  PGF::Glyph glyph;
  CHECK(pgf.glyph(2, glyph), true);
  CHECK(glyph.pixels == std::vector<u8>({9, 15, 3, 9, 15, 3}), true);
}

//Damaged fonts: one damaged in its header or tables is refused as it opens; one damaged in a glyph opens, and that
//glyph alone is missing. Nothing in a font, whatever it holds, reads outside it: every length it could be cut to, and
//a thousand fonts with bytes changed at random, open or are refused, and every glyph and shadow reads or is refused,
//with the address and undefined behavior sanitizers watching.
static auto pgfDamaged() -> void {
  auto good = systemFont(17).build();
  auto opens = [](std::vector<u8> bytes) {
    PGF pgf;
    std::string error;
    return pgf.open(std::move(bytes), error);
  };
  CHECK(opens(good), true);
  auto damaged = [&](u32 at, u8 value) {
    auto bytes = good;
    bytes[at] = value;
    return opens(bytes);
  };
  CHECK(damaged(0x04, 'X'), false);   //not "PGF0"
  CHECK(damaged(0x08, 4), false);     //revision 4
  CHECK(damaged(0x02, 0x87), false);  //a header shorter than a revision 3 header (0x187)
  CHECK(damaged(0x03, 0x7f), false);  //a header longer than the file
  CHECK(damaged(0x18, 0), false);     //character map entries of 0 bits
  CHECK(damaged(0x18, 33), false);    //or of 33
  CHECK(damaged(0x1c, 0), false);     //pointers of 0 bits
  CHECK(damaged(0x22, 8), false);     //8 bits a pixel
  CHECK(damaged(0x2d, 0), false);     //no resolution across (8192: its second byte)
  CHECK(damaged(0x102, 255), false);  //a dimensions table running past the end
  CHECK(damaged(0x16e, 1), false);    //a shadow map past the end (65536 + 64 entries)
  CHECK(damaged(0x170, 0), false);    //shadow map entries of 0 bits
  CHECK(damaged(0x18c, 0xff), false); //ranges past the end
  auto bytes = good;
  for(u32 n : range(4)) bytes[0x14 + n] = 0;
  CHECK(opens(bytes), false);  //no glyphs
  CHECK(opens({}), false);
  CHECK(opens(std::vector<u8>(391)), false);

  //A glyph whose pointer goes past the end, whose table entry isn't in its table, or whose picture runs past the end:
  //that glyph is missing, the rest read.
  PGF pgf;
  std::string error;
  auto font = systemFont(1);
  font.glyphs[2].entries[1] = 7;  //'B': an x adjustment entry its table (2 entries) hasn't got
  CHECK(pgf.open(font.build(), error), true);
  PGF::Glyph glyph;
  CHECK(pgf.glyph(2, glyph), false);
  CHECK(pgf.glyph(1, glyph), true);
  CHECK(pgf.glyph(font.glyphs.size(), glyph), false);  //no such glyph
  auto cut = systemFont(1).build();
  cut.resize(cut.size() - 40);  //into the last glyph's record ('_', its shadow)
  CHECK(pgf.open(cut, error), true);
  CHECK(pgf.glyph(1, glyph), true);
  CHECK(pgf.shadow(63, glyph), false);

  //Every length a font can be cut to, and fonts with bytes changed at random: nothing reads outside them.
  auto everything = [](const std::vector<u8>& bytes) {
    PGF pgf;
    std::string error;
    if(!pgf.open(bytes, error)) return 0u;
    u32 read = 0;
    PGF::Glyph glyph;
    for(u32 n : range(pgf.glyphCount)) read += pgf.glyph(n, glyph);
    for(u32 n : range(pgf.shadowCount)) read += pgf.shadow(n, glyph);
    for(u32 code : {0x20u, 0x41u, 0x5fu, 0x1100u, 0xac00u, 0xac01u}) read += pgf.glyphFor(code) >= 0;
    return read;
  };
  CHECK(everything(good) > 0, true);
  for(u32 length : range(good.size() + 1)) {
    everything(std::vector<u8>(good.begin(), good.begin() + length));
  }
  std::mt19937 random(2023);
  for(u32 trial : range(1000)) {
    auto bytes = good;
    for(u32 changes = 1 + trial % 8; changes; changes--) bytes[random() % bytes.size()] = random();
    if(trial % 3 == 0) bytes[0x10 + random() % 0x8] = random();  //the counts and sizes
    everything(bytes);
  }
}

//The library's memory, from the program's own alloc and free as pspautotests recorded: sceFontNewLib asks for its 76
//bytes, the handles' (76 each), their data (560 each) and the list (18 styles of 168 bytes), numFonts capped at 9
//(newlib: 9, 10, 100 and -1 all take 8824 bytes; 4, 5644); it fills its own fields (the parameters as given, 128 dots
//an inch, 18 fonts, '_' the alternative character) and the list. A font opened from its tables asks for each table
//(open: each system font's total), one read whole for its length and 12, one of the program's memory for 12; one
//loaded already, for nothing. Closing the last handle gives the tables back (fonttest's order), and sceFontDoneLib
//closes what's open and gives the library's own back (its list, handles, data, then the library). A failing alloc
//ends the function, out of memory, giving back what it was given; no alloc or free is refused.
static auto fontsMemory() -> void {
  FontFolder folder;
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.kernel.fontsFrom(folder.path.string());
    memoryFunctions(m);
    m.system.memory.write(4, Error, 0x1337);
    u32 library = viaProgram(m, "sceFontNewLib", {parameters(m, 4), Error}, recompile);
    CHECK(library, Heap);
    CHECK(word(m, Error), 0);
    auto calls = logged(m);
    CHECK(calls == (std::vector<std::pair<u32, u32>>{{1, 76}, {1, 304}, {1, 2240}, {1, 3024}}), true);
    std::vector<u8> given(0x2c), kept(0x2c);
    m.system.memory.copyOut(given.data(), Parameters, 0x2c);
    m.system.memory.copyOut(kept.data(), library, 0x2c);
    CHECK(given == kept, true);
    CHECK(floatOf(word(m, library + 0x38)) == 128.0f && floatOf(word(m, library + 0x3c)) == 128.0f, true);
    CHECK(word(m, library + 0x40), 18);
    CHECK(m.system.memory.read(2, library + 0x48), 0x5f);
    CHECK(m.system.memory.read(2, library + 0x4a), 0);
    u32 list = word(m, library + 0x44);
    CHECK(m.system.memory.readString(list + 0x60, 64) == "jpn0.pgf", true);
    CHECK(m.system.memory.readString(list + 17 * 168 + 0x60, 64) == "kr0.pgf", true);

    //a system font from its tables: each one's length; again: nothing; whole: its length and 12
    u32 before = word(m, Log);
    u32 handle = viaProgram(m, "sceFontOpen", {library, 1, 0, Error}, recompile);
    CHECK(handle != 0 && word(m, Error) == 0, true);
    PGF pgf;
    std::string error;
    pgf.open(systemFont(1).build(), error);
    std::vector<std::pair<u32, u32>> tables;
    for(auto& part : pgf.parts) tables.push_back({1, part.length});
    CHECK(logged(m, before) == tables, true);
    CHECK(tables[5].second == 0 && tables[6].second == 0, true);  //revision 2: no range lists (fonttest: 0, 0)
    //its tables in its blocks as they are in the file: the first, the dimensions, after the library's four blocks
    //(the heap hands out 16 bytes at a time: 76 takes 80)
    std::vector<u8> table(pgf.parts[0].length);
    m.system.memory.copyOut(table.data(), Heap + 80 + 304 + 2240 + 3024, table.size());
    CHECK(std::equal(table.begin(), table.end(), pgf.data().begin() + pgf.parts[0].at), true);
    before = word(m, Log);
    u32 again = viaProgram(m, "sceFontOpen", {library, 1, 0, Error}, recompile);
    CHECK(again != handle && word(m, Error) == 0 && word(m, Log) == before, true);
    CHECK(viaProgram(m, "sceFontOpen", {library, 2, 1, Error}, recompile) != 0, true);
    u32 length = folder.get("ltn1.pgf").size();
    CHECK(logged(m, before) == (std::vector<std::pair<u32, u32>>{{1, length}, {1, 12}}), true);
    //the fourth handle, then a fifth: too many
    u32 fourth = viaProgram(m, "sceFontOpenUserMemory", {library, Code, 4, Error}, recompile);
    CHECK(fourth == 0 && word(m, Error) == 0x8046'000a, true);  //not a PGF
    fourth = viaProgram(m, "sceFontOpen", {library, 3, 0, Error}, recompile);
    CHECK(fourth != 0, true);
    CHECK(viaProgram(m, "sceFontOpen", {library, 4, 0, Error}, recompile), 0);
    CHECK(word(m, Error), 0x8046'0009);
    //closing: a handle of two gives nothing back; the last gives the tables back in fonttest's order
    before = word(m, Log);
    CHECK(viaProgram(m, "sceFontClose", {again}, recompile), 0);
    CHECK(word(m, Log), before);
    CHECK(viaProgram(m, "sceFontClose", {handle}, recompile), 0);
    auto frees = logged(m, before);
    CHECK(frees.size(), 9);
    //fonttest's order: the pointers, the character map, the two range lists, the shadow map, then the four tables
    u32 table0 = Heap + 80 + 304 + 2240 + 3024;
    std::vector<u32> blocks;
    for(u32 n = 0, at = table0; n < 9; at += (pgf.parts[n].length + 15) & ~15u, n++) blocks.push_back(at);
    std::vector<std::pair<u32, u32>> order;
    for(u32 n : {8, 7, 5, 6, 4, 0, 1, 2, 3}) order.push_back({2, blocks[n]});
    CHECK(frees == order, true);
    CHECK(viaProgram(m, "sceFontClose", {handle}, recompile), 0x8046'0003);  //closed already
    //sceFontDoneLib: the two still open (whole: its 12 bytes, then its file; the third's tables), then the library's
    before = word(m, Log);
    CHECK(viaProgram(m, "sceFontDoneLib", {library}, recompile), 0);
    frees = logged(m, before);
    CHECK(frees.size(), 2 + 9 + 4);
    if(frees.size() == 15) {
      CHECK(frees[11].second == list && frees[12].second == word(m, library + 0x2c), true);
      CHECK(frees[13].second == word(m, library + 0x30) && frees[14].second == library, true);
    }
    CHECK(word(m, library + 0x40), 0);  //what it lists is cleared
    CHECK(m.kernel.fontLibraries.empty() && m.kernel.openFonts.empty() && m.kernel.fontCalls.empty(), true);
  }
  //numFonts: 9 at most (-1 as unsigned)
  for(auto [fonts, handles] : {std::pair{0u, 0u}, {1u, 76u}, {9u, 684u}, {100u, 684u}, {~0u, 684u}}) {
    KernelMachine m;
    m.kernel.fontsFrom(folder.path.string());
    memoryFunctions(m);
    viaProgram(m, "sceFontNewLib", {parameters(m, fonts), Error});
    check(__LINE__, "numFonts' handles", logged(m)[1].second, handles);
  }
  //no alloc, no free: refused before anything is asked for
  for(auto [alloc, free] : {std::pair{0u, Free}, {Alloc, 0u}, {0u, 0u}}) {
    KernelMachine m;
    m.kernel.fontsFrom(folder.path.string());
    memoryFunctions(m);
    CHECK(viaProgram(m, "sceFontNewLib", {parameters(m, 4, alloc, free), Error}), 0);
    CHECK(word(m, Error), 0x8046'0003);
    CHECK(word(m, Log), 0);
  }
  //alloc failing at its first call (newlib's "Failing alloc"), then at its third: what it gave goes back
  for(u32 failAt : {1u, 3u}) {
    KernelMachine m;
    m.kernel.fontsFrom(folder.path.string());
    memoryFunctions(m);
    m.system.memory.write(4, Log + 4, failAt);
    CHECK(viaProgram(m, "sceFontNewLib", {parameters(m, 4), Error}), 0);
    CHECK(word(m, Error), 0x8046'0001);
    auto calls = logged(m);
    if(failAt == 1) CHECK(calls == (std::vector<std::pair<u32, u32>>{{1, 76}}), true);
    if(failAt == 3) {
      CHECK(calls.size(), 5);
      if(calls.size() == 5) CHECK(calls[3] == std::pair(2u, Heap + 80) && calls[4] == std::pair(2u, Heap), true);
    }
    CHECK(m.kernel.fontLibraries.empty() && m.kernel.fontCalls.empty(), true);
  }
  //an open failing part way gives back its tables and frees its handle
  {
    FontMachine f(folder, 1);
    auto& m = f.m;
    m.system.memory.write(4, Log + 4, word(m, Log) + 4);
    CHECK(viaProgram(m, "sceFontOpen", {f.library, 0, 0, Error}), 0);
    CHECK(word(m, Error), 0x8046'0001);
    auto calls = logged(m, 4);
    CHECK(calls.size(), 4 + 3);
    CHECK(f.m.kernel.fontLibraries.at(f.library).open[0], false);
    m.system.memory.write(4, Log + 4, 0);
    CHECK(viaProgram(m, "sceFontOpen", {f.library, 0, 0, Error}) != 0, true);
    CHECK(roundTrip(m, [&](KernelMachine& fresh) { fresh.kernel.fontsFrom(folder.path.string()); }), true);
  }
}

//The list of fonts and finding them, as pspautotests' fontlist, fontinfobyindex, optimum and find recorded on a PSP
//with its eighteen.
static auto fontsFound() -> void {
  FontFolder folder;
  FontMachine f(folder);
  auto& m = f.m;
  u32 library = f.library;
  CHECK(m.call("sceFontGetNumFontList", {library, Error}), 18);
  CHECK(word(m, Error), 0);
  CHECK(m.call("sceFontGetNumFontList", {0, Error}), 0);
  CHECK(word(m, Error), 0x8046'0002);
  //the list: jpn0 first, as fontlist recorded it; as many as there's room for
  m.system.memory.fill(Buffer, 0xdd, 168 * 20);
  CHECK(m.call("sceFontGetFontList", {library, Buffer, 1}), 0);
  CHECK(floatOf(word(m, Buffer + 0x00)) == 10.125f && floatOf(word(m, Buffer + 0x08)) == 128.0f, true);
  CHECK(m.system.memory.read(2, Buffer + 0x14), 1);    //sans-serif
  CHECK(m.system.memory.read(2, Buffer + 0x16), 103);  //demi-bold
  CHECK(m.system.memory.read(2, Buffer + 0x1a), 1);    //Japanese
  CHECK(m.system.memory.read(2, Buffer + 0x1e), 1);
  CHECK(m.system.memory.readString(Buffer + 0x20, 64) == "FTT-NewRodin Pro DB", true);
  CHECK(word(m, Buffer + 0xa4) == 0 && word(m, Buffer + 0xa8) == 0xdddd'dddd, true);  //168 bytes, no more
  CHECK(m.call("sceFontGetFontList", {library, Buffer, 30}), 0);
  CHECK(m.system.memory.readString(Buffer + 17 * 168 + 0x60, 64) == "kr0.pgf", true);
  CHECK(word(m, Buffer + 18 * 168), 0xdddd'dddd);
  CHECK(m.call("sceFontGetFontList", {library, 0, 0}), 0x8046'0003);
  CHECK(m.call("sceFontGetFontList", {0, Buffer, 0}), 0x8046'0002);
  CHECK(m.call("sceFontGetFontInfoByIndexNumber", {library, Buffer, 9}), 0);
  CHECK(floatOf(word(m, Buffer)) == 7.0f && m.system.memory.readString(Buffer + 0x60, 64) == "ltn8.pgf", true);
  for(u32 index : {~0u, 18u, 30u}) {
    CHECK(m.call("sceFontGetFontInfoByIndexNumber", {library, Buffer, index}), 0x8046'0003);
  }
  CHECK(m.call("sceFontGetFontInfoByIndexNumber", {library, 0, 0}), 0x8046'0003);
  CHECK(m.call("sceFontGetFontInfoByIndexNumber", {0, Buffer, 0}), 0x8046'0002);

  //finding: a style asking for a size (at a resolution), a family, a style, a language, a country, a file name
  struct Asked { float h = 0, v = 0, hRes = 0, vRes = 0; u16 family = 0, style = 0, language = 0, country = 0;
                 const char* file = ""; };
  auto find = [&](const char* name, Asked asked) {
    m.system.memory.fill(Style, 0, 168);
    for(auto [at, value] : {std::pair{0u, asked.h}, {4u, asked.v}, {8u, asked.hRes}, {12u, asked.vRes}}) {
      m.system.memory.write(4, Style + at, std::bit_cast<u32>(value));
    }
    m.system.memory.write(2, Style + 0x14, asked.family);
    m.system.memory.write(2, Style + 0x16, asked.style);
    m.system.memory.write(2, Style + 0x1a, asked.language);
    m.system.memory.write(2, Style + 0x1e, asked.country);
    m.system.memory.copyIn(Style + 0x60, asked.file, strlen(asked.file));
    m.system.memory.write(4, Error, 0x1337);
    u32 found = m.call(name, {library, Style, Error});
    check(__LINE__, "the error", word(m, Error), 0);
    return s32(found);
  };
  auto resolution = [&](float h, float v) {
    m.system.fpu.r[12] = std::bit_cast<u32>(h), m.system.fpu.r[13] = std::bit_cast<u32>(v);
    return m.call("sceFontSetResolution", {library});
  };
  float infinity = std::numeric_limits<float>::infinity();
  //optimum's sizes: the nearest (8.5624 points and below the 7-point fonts' first, 9; from 8.5625 the first)
  for(auto [h, found] : {std::pair{-1.0f, 0}, {0.01f, 9}, {3.0f, 9}, {7.0f, 9}, {8.5624f, 9}, {8.5625f, 0},
                         {10.125f, 0}, {13.0f, 0}, {infinity, 0}}) {
    check(__LINE__, "optimum's H", find("sceFontFindOptimumFont", {.h = h}), found);
    check(__LINE__, "find's H", find("sceFontFindFont", {.h = h}), h == -1 ? 0 : h == 7 ? 9 : h == 10.125f ? 0 : -1);
  }
  for(float v : {0.01f, 7.0f, 10.125f, 13.0f, infinity}) {  //a height alone: optimum 9, find nothing
    check(__LINE__, "optimum's V", find("sceFontFindOptimumFont", {.v = v}), 9);
    check(__LINE__, "find's V", find("sceFontFindFont", {.v = v}), -1);
  }
  CHECK(find("sceFontFindOptimumFont", {.v = -1}), 0);
  CHECK(find("sceFontFindFont", {.v = -1}), 0);
  CHECK(find("sceFontFindOptimumFont", {.h = 7, .v = 7}), 9);
  CHECK(find("sceFontFindOptimumFont", {.h = 11, .v = 11}), 0);
  CHECK(find("sceFontFindOptimumFont", {.v = 11}), 9);
  CHECK(resolution(256, 256), 0);
  CHECK(find("sceFontFindOptimumFont", {.h = 8.5624f}), 0);
  CHECK(find("sceFontFindOptimumFont", {.h = 4.2812f}), 9);
  CHECK(find("sceFontFindOptimumFont", {.h = 8.5624f, .hRes = 128, .vRes = 128}), 9);
  CHECK(find("sceFontFindFont", {.h = 7}), -1);
  CHECK(find("sceFontFindFont", {.h = 3.5f}), 9);
  CHECK(find("sceFontFindFont", {.h = 7, .hRes = 128, .vRes = 128}), 9);
  for(auto [h, v] : {std::pair{256.0f, 128.0f}, {128.0f, 256.0f}}) {
    CHECK(resolution(h, v), 0);
    CHECK(find("sceFontFindOptimumFont", {.v = 8.5624f}), 9);
    CHECK(find("sceFontFindOptimumFont", {.v = 4.2812f}), 9);
    CHECK(find("sceFontFindOptimumFont", {.h = 8.5624f, .v = 8.5624f}), 9);
    CHECK(find("sceFontFindOptimumFont", {.h = 4.2812f, .v = 4.2812f}), 9);
  }
  CHECK(resolution(128, 128), 0);
  //families, styles, languages, countries: optimum the last of those that match, find the first
  for(auto [asked, optimum, first] : {std::tuple{Asked{.family = 1}, 17, 0}, {Asked{.family = 2}, 16, 2},
                                      {Asked{.style = 1}, 17, 1}, {Asked{.style = 5}, 14, 5},
                                      {Asked{.style = 6}, 16, 7}, {Asked{.style = 103}, 0, 0},
                                      {Asked{.language = 1}, 0, 0}, {Asked{.country = 1}, 16, 0},
                                      {Asked{.file = "ltn0.pgf"}, 1, 1}, {Asked{.file = "ltn0.pg"}, 0, -1}}) {
    check(__LINE__, "optimum", find("sceFontFindOptimumFont", asked), optimum);
    check(__LINE__, "find", find("sceFontFindFont", asked), first);
  }
  CHECK(find("sceFontFindOptimumFont", {.h = 10, .family = 2, .style = 5, .language = 2}), 6);
  CHECK(find("sceFontFindOptimumFont", {}), 0);
  //no library, no style: 0, and the errors recorded
  CHECK(m.call("sceFontFindOptimumFont", {0, Style, Error}), 0);
  CHECK(word(m, Error), 0x8046'0002);
  CHECK(m.call("sceFontFindOptimumFont", {library, 0, Error}), 0);
  CHECK(word(m, Error), 0x8046'0002);
  CHECK(m.call("sceFontFindFont", {library, 0, Error}), 0);
  CHECK(word(m, Error), 0x8046'0003);
  //a library done with lists nothing: optimum and find give the first; its index numbers are refused
  CHECK(viaProgram(m, "sceFontDoneLib", {library}), 0);
  CHECK(m.call("sceFontGetNumFontList", {library, Error}), 0);
  CHECK(find("sceFontFindOptimumFont", {}), 0);
  CHECK(m.call("sceFontGetFontInfoByIndexNumber", {library, Buffer, 0}), 0x8046'0003);
  CHECK(roundTrip(m, [&](KernelMachine& fresh) { fresh.kernel.fontsFrom(folder.path.string()); }), true);
}

//A font's details, a character's, and its picture's size, as pspautotests' fontinfo and charinfo recorded them for
//the font font 1 stands in for: the 'A' 16 by 17 from 0, 17, its measurements its table entries (0, 0, 0 and 1138),
//'_' for a character the font hasn't got (14 by 2 from 0, -3, advancing 993), and nothing for one below its first.
static auto fontsMeasured() -> void {
  FontFolder folder;
  FontMachine f(folder);
  auto& m = f.m;
  u32 font = viaProgram(m, "sceFontOpen", {f.library, 1, 0, Error});
  u32 whole = viaProgram(m, "sceFontOpen", {f.library, 2, 1, Error});
  m.system.memory.fill(Buffer, 0xdd, 0x200);
  CHECK(m.call("sceFontGetFontInfo", {font, Buffer}), 0);
  s32 recorded[10] = {1455, 1222, 863, -196, 195, 1202, -727, 18, 1585, 1094};
  for(u32 n : range(10)) {
    check(__LINE__, "an I value", word(m, Buffer + n * 4), u32(recorded[n]));
    check(__LINE__, "an F value", floatOf(word(m, Buffer + 0x28 + n * 4)) == recorded[n] / 64.0f, true);
  }
  CHECK(floatOf(word(m, Buffer + 0x28)) == 22.734375f, true);  //fontinfo's maxGlyphWidthF
  CHECK(m.system.memory.read(2, Buffer + 0x50) == 23 && m.system.memory.read(2, Buffer + 0x52) == 20, true);
  CHECK(word(m, Buffer + 0x54), 4);   //its glyphs
  CHECK(word(m, Buffer + 0x58), 64);  //its shadows: it was read a piece at a time
  CHECK(m.system.memory.readString(Buffer + 0x5c + 0x60, 64) == "ltn0.pgf", true);
  CHECK(word(m, Buffer + 0x104), 0xdddd'dd04);  //4 bits a pixel, its padding untouched
  CHECK(m.call("sceFontGetFontInfo", {whole, Buffer}), 0);
  CHECK(word(m, Buffer + 0x58), 0);  //read whole into memory: none, as fontinfo recorded
  CHECK(m.call("sceFontGetFontInfo", {0, Buffer}), 0x8046'0003);
  CHECK(m.call("sceFontGetFontInfo", {font, 0}), 0x8046'0003);
  auto info = [&](u32 code, bool shadow = false) {
    m.system.memory.fill(Buffer, 0xdd, 0x48);
    u32 answer = m.call(shadow ? "sceFontGetShadowInfo" : "sceFontGetCharInfo", {font, code, Buffer});
    check(__LINE__, "char info", answer, 0);
    std::vector<s32> fields;
    for(u32 n : range(14)) fields.push_back(s32(word(m, Buffer + n * 4)));
    fields.push_back(s32(m.system.memory.read(2, Buffer + 0x38)));
    fields.push_back(s32(m.system.memory.read(2, Buffer + 0x3a)));
    check(__LINE__, "60 bytes, no more", word(m, Buffer + 0x3c), 0xdddd'dddd);
    return fields;
  };
  //charinfo: "bitmap 16x17, from 0,17, metrics: 0x0, asc=-137, desc=-137, bearH=-44x-137, bearV=-204x17,
  //advanceH=1138, advanceV=1094, shadowFlags=0051, shadowId=33"
  CHECK(info('A') == (std::vector<s32>{16, 17, 0, 17, 0, 0, -137, -137, -44, -137, -204, 17, 1138, 1094, 0x51, 33}),
        true);
  auto underscore = std::vector<s32>{14, 2, 0, -3, 0, 0, -137, -137, -44, -137, -204, 17, 993, 1094, 0x4e, 63};
  for(u32 code : {0xd7ffu, 0xfffeu, 0xffffu, u32('C')}) {
    check(__LINE__, "the alternative", info(code) == underscore, true);
  }
  for(u32 code : {0u, 1u, 7u}) check(__LINE__, "nothing", info(code) == std::vector<s32>(16, 0), true);
  //'B': its measurements written out; the descender is the ascender less the height
  CHECK(info('B') == (std::vector<s32>{5, 4, 1, 9, 301, 600, 576, -24, 64, 576, -128, 17, 700, 1094, 0, 33}), true);
  //shadowinfo: "bitmap 16x16, from -4,12", the rest the character's
  CHECK(info('A', true) == (std::vector<s32>{16, 16, -4, 12, 0, 0, -137, -137, -44, -137, -204, 17, 1138, 1094, 0x51,
                                             33}), true);
  CHECK(info(0xd7ff, true)[0] == 15 && info(0xd7ff, true)[1] == 9 && info(0xd7ff, true)[2] == -4, true);
  //the alternative character: none (0), or one the font hasn't got: nothing (altcharcode)
  for(u32 code : {0u, 0xffffu}) {
    CHECK(m.call("sceFontSetAltCharacterCode", {f.library, code}), 0);
    CHECK(info(0xffff)[1], 0);
  }
  CHECK(m.call("sceFontSetAltCharacterCode", {f.library, 0x1005f}), 0);  //its low 16 bits
  CHECK(m.system.memory.read(2, f.library + 0x48), 0x5f);
  CHECK(info(0xffff)[1], 2);
  CHECK(m.call("sceFontSetAltCharacterCode", {0, 0x5f}), 0x8046'0002);
  //the picture's size: charimagerect's (16, 17), shadowimagerect's (16, 16), two 16-bit numbers
  m.system.memory.write(4, Buffer + 4, 0xdddd'dddd);
  CHECK(m.call("sceFontGetCharImageRect", {font, 'A', Buffer}), 0);
  CHECK(word(m, Buffer) == 0x0011'0010 && word(m, Buffer + 4) == 0xdddd'dddd, true);
  CHECK(m.call("sceFontGetShadowImageRect", {font, 'A', Buffer}), 0);
  CHECK(word(m, Buffer), 0x0010'0010);
  CHECK(m.call("sceFontGetCharImageRect", {font, 0, Buffer}), 0);
  CHECK(word(m, Buffer), 0);
  CHECK(m.call("sceFontGetCharImageRect", {0, 'A', Buffer}), 0x8046'0003);
  CHECK(m.call("sceFontGetCharImageRect", {font, 'A', 0}), 0x8046'0003);
  //a closed handle answers while another holds its font (open's "GetCharInfo on closed"), not once it's gone
  u32 second = viaProgram(m, "sceFontOpen", {f.library, 1, 0, Error});
  CHECK(viaProgram(m, "sceFontClose", {second}), 0);
  CHECK(m.call("sceFontGetCharInfo", {second, 'A', Buffer}), 0);
  CHECK(viaProgram(m, "sceFontClose", {font}), 0);
  CHECK(m.call("sceFontGetCharInfo", {second, 'A', Buffer}), 0x8046'0003);
  //the Korean font: a composite's picture, and codes kept for parts drawn as the alternative
  u32 korean = viaProgram(m, "sceFontOpen", {f.library, 17, 0, Error});
  CHECK(m.call("sceFontGetCharImageRect", {korean, 0xac00, Buffer}), 0);
  CHECK(word(m, Buffer), 0x000c'000c);
  CHECK(m.call("sceFontGetCharImageRect", {korean, 0x1100, Buffer}), 0);
  CHECK(word(m, Buffer), 0x0002'000e);  //'_'
  CHECK(roundTrip(m, [&](KernelMachine& fresh) { fresh.kernel.fontsFrom(folder.path.string()); }), true);
}

//Pictures drawn into the program's buffer, as pspautotests' charglyphimage, charglyphimageclip and
//charglyphimagexfrac recorded them (quoted where checked): a byte a pixel (17 times its shade) or 4 bits (the left
//pixel low), added to what's there up to the brightest; at a fraction of a pixel across, each pixel shared between
//two columns (the next's share rounded down); the fraction down dropped; clipped to the buffer and the clip
//rectangle; the other three formats leaving the buffer as it was.
static auto fontsDrawn() -> void {
  FontFolder folder;
  FontMachine f(folder);
  auto& m = f.m;
  u32 font = viaProgram(m, "sceFontOpen", {f.library, 1, 0, Error});
  auto image = [&](u32 format, s32 x, s32 y, u32 width = 20, u32 height = 20, u32 line = 20) {
    m.system.memory.write(4, Image + 0x00, format);
    m.system.memory.write(4, Image + 0x04, u32(x));
    m.system.memory.write(4, Image + 0x08, u32(y));
    m.system.memory.write(2, Image + 0x0c, width);
    m.system.memory.write(2, Image + 0x0e, height);
    m.system.memory.write(2, Image + 0x10, line);
    m.system.memory.write(4, Image + 0x14, Buffer);
  };
  auto row = [&](u32 y, u32 line = 20) {
    std::string text;
    for(u32 x : range(line)) {
      char hex[3];
      snprintf(hex, sizeof(hex), "%02x", m.system.memory.read(1, Buffer + y * line + x));
      text += hex;
    }
    return text;
  };
  auto draw = [&](const char* name, std::vector<u32> clip = {}, u8 fill = 0) {
    m.system.memory.fill(Buffer, fill, 80 * 20);
    std::vector<u32> arguments = {font, 'A', Image};
    arguments.insert(arguments.end(), clip.begin(), clip.end());
    for(u32 n : range(arguments.size())) m.system.ipu.r[n < 4 ? 4 + n : 8 + n - 4] = arguments[n];
    m.kernel.syscall(m.kernel.importCode("test", Kernel::nid(name)));
    return m.system.ipu.r[2];
  };
  //at (0, 0), a byte a pixel: the picture itself
  image(2, 0, 0);
  CHECK(draw("sceFontGetCharGlyphImage"), 0);
  for(u32 y : range(17)) check(__LINE__, "the 'A'", row(y) == std::string(recordedA[y]) + "00000000", true);
  CHECK(row(17) == std::string(40, '0'), true);
  //at 10 across, at 10 and a half, and at 10 and 63/64: charglyphimage's rows
  image(2, 10 << 6, 0);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(0) == "0000000000000000000000000000000066ffff88", true);
  CHECK(row(16) == "00000000000000000000ccff9900000000000000", true);
  image(2, (10 << 6) + 32, 0);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(0) == "0000000000000000000000000000000033b3ffc3", true);
  CHECK(row(1) == "000000000000000000000000000000006feefff6", true);
  CHECK(row(16) == "0000000000000000000066e6cc4c000000000000", true);
  image(2, (10 << 6) + 63, 0);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(0) == "000000000000000000000000000000000268fffe", true);
  CHECK(row(10) == "0000000000000000000000000268ffffcccccccc", true);
  //the fraction down dropped; up and left, cut by the buffer's edges
  image(2, 0, (10 << 6) + 63);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(10) == std::string(recordedA[0]) + "00000000" && row(9) == std::string(40, '0'), true);
  image(2, -10 * 64, 0);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(1) == std::string(40, '0') && row(2) == "4400000000000000000000000000000000000000", true);
  CHECK(row(16) == "00000088ffcc0000000000000000000000000000", true);
  image(2, 0, -10 * 64);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(0) == "000066ffffccccccccccccffff66000000000000" && row(7) == std::string(40, '0'), true);
  //4 bits a pixel, the left one low: charglyphimage's "4 bpp"
  image(0, 0, 0, 20, 20, 20);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(0) == "000000f68f000000000000000000000000000000", true);
  CHECK(row(16) == "fc090000000080cf000000000000000000000000", true);
  //the other formats, and a bad one: the buffer left as it was
  for(u32 format : {1u, 3u, 4u, 5u, ~0u}) {
    image(format, 0, 0);
    CHECK(draw("sceFontGetCharGlyphImage", {}, 0x44), 0);
    check(__LINE__, "untouched", row(5) == std::string(40, '4'), true);
  }
  //added up to the brightest: onto 0x44 and 0xff (xfrac's destinations), and every row onto one (linesize 0)
  image(2, 0, 0);
  draw("sceFontGetCharGlyphImage", {}, 0x44);
  CHECK(row(0) == "444444444444aaffffcc44444444444444444444", true);
  draw("sceFontGetCharGlyphImage", {}, 0xff);
  CHECK(row(0) == std::string(40, 'f'), true);
  image(2, 0, 0, 20, 20, 0);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(0) == "ffffffffffffffffffffffffffffffff00000000" && row(1) == std::string(40, '0'), true);
  image(2, 0, 0, 20, 20, 1);  //linesize 1: each row a byte on
  draw("sceFontGetCharGlyphImage");
  CHECK(row(0) == "00000000000066ffffffffffffffffffffffffff" && row(1) == "ffffffffffffffffffffffcc0000000000000000",
        true);
  image(2, 0, 0, 0, 20);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(0) == std::string(40, '0') && row(16) == std::string(40, '0'), true);  //no width: nothing
  image(2, 0, 0, 0xffff, 20);
  draw("sceFontGetCharGlyphImage");
  CHECK(row(16) == std::string(recordedA[16]) + "00000000", true);  //a width of -1: 65535
  //clipped: charglyphimageclip's (x, y, width, height), width and height unsigned
  image(2, 0, 0);
  draw("sceFontGetCharGlyphImage_Clip", {5, 0, 20, 20});
  CHECK(row(5) == "0000000000ffbb0000aaff770000000000000000", true);
  draw("sceFontGetCharGlyphImage_Clip", {5, 0, 5, 20});
  CHECK(row(2) == "000000000033ffccbbff00000000000000000000", true);
  draw("sceFontGetCharGlyphImage_Clip", {0, 5, 20, 5});
  CHECK(row(4) == std::string(40, '0') && row(5) == "0000000066ffbb0000aaff770000000000000000", true);
  CHECK(row(10) == std::string(40, '0'), true);
  draw("sceFontGetCharGlyphImage_Clip", {u32(-1), u32(-1), u32(-2), u32(-1)});
  CHECK(row(16) == std::string(recordedA[16]) + "00000000", true);
  draw("sceFontGetCharGlyphImage_Clip", {0, 0, 0, 20});
  CHECK(row(16) == std::string(40, '0'), true);
  //the shadow, from its own place
  draw("sceFontGetShadowGlyphImage");
  PGF pgf;
  std::string error;
  pgf.open(systemFont(1).build(), error);
  PGF::Glyph shadow;
  pgf.shadow(33, shadow);
  u32 sum = 0;
  for(u32 y : range(16)) {
    for(u32 x : range(16)) sum += m.system.memory.read(1, Buffer + y * 20 + x) == shadow.pixels[y * 16 + x] * 17u;
  }
  CHECK(sum, 256);
  draw("sceFontGetShadowGlyphImage_Clip", {0, 0, 4, 20});
  CHECK(m.system.memory.read(1, Buffer + 8 * 20 + 4), 0);
  //nothing to draw into, no font, a code below the first: refused, refused, nothing drawn
  m.system.ipu.r[4] = font, m.system.ipu.r[5] = 'A', m.system.ipu.r[6] = 0;
  CHECK(m.call("sceFontGetCharGlyphImage", {font, 'A', 0}), 0x8046'0003);
  CHECK(m.call("sceFontGetCharGlyphImage", {0, 'A', Image}), 0x8046'0003);
  image(2, 0, 0);
  m.system.memory.fill(Buffer, 0, 400);
  CHECK(m.call("sceFontGetCharGlyphImage", {font, 7, Image}), 0);
  CHECK(row(0) == std::string(40, '0') && row(16) == std::string(40, '0'), true);
  //a buffer running off the end of memory: drawn as far as memory goes, nothing else touched
  m.system.memory.write(4, Image + 0x14, 0x09ff'fff0);
  m.system.memory.write(2, Image + 0x10, 1000);
  CHECK(m.call("sceFontGetCharGlyphImage", {font, 'A', Image}), 0);
  CHECK(m.system.unmapped.empty(), true);
  CHECK(roundTrip(m, [&](KernelMachine& fresh) { fresh.kernel.fontsFrom(folder.path.string()); }), true);
}

//The program's own fonts: one in its memory (read where it is; a length taken as unsigned, 0 refused, no memory
//refused), one in a file (whole into its memory; or from its tables, which needs its file callbacks set, as on a
//PSP); a file that isn't there, or isn't a PGF, refused as pspautotests' openfile and openmem recorded.
static auto fontsOwn() -> void {
  FontFolder folder;
  FontMachine f(folder);
  auto& m = f.m;
  HostFolder stick;
  auto bytes = systemFont(3).build();
  stick.put("font.pgf", std::string(bytes.begin(), bytes.end()));
  stick.put("text.txt", "not a font");
  m.kernel.mount("ms0", stick.path.string());
  m.system.memory.copyIn(0x0896'0000, bytes.data(), bytes.size());
  u32 before = word(m, Log);
  u32 memory = viaProgram(m, "sceFontOpenUserMemory", {f.library, 0x0896'0000, u32(bytes.size()), Error});
  CHECK(memory != 0 && word(m, Error) == 0, true);
  CHECK(logged(m, before) == (std::vector<std::pair<u32, u32>>{{1, 12}}), true);
  CHECK(m.call("sceFontGetCharInfo", {memory, 'B', Buffer}), 0);
  CHECK(word(m, Buffer + 0x10), 303);  //font 3's 'B'
  CHECK(viaProgram(m, "sceFontOpenUserMemory", {f.library, 0x0896'0000, ~0u, Error}) != 0, true);  //-1: OK
  CHECK(viaProgram(m, "sceFontOpenUserMemory", {f.library, 0x0896'0000, 0, Error}), 0);
  CHECK(word(m, Error), 0x8046'0003);
  CHECK(viaProgram(m, "sceFontOpenUserMemory", {f.library, 0, 100, Error}), 0);
  CHECK(word(m, Error), 0x8046'0003);
  CHECK(viaProgram(m, "sceFontOpenUserMemory", {0, 0x0896'0000, 100, Error}), 0);
  CHECK(word(m, Error), 0x8046'0002);
  CHECK(viaProgram(m, "sceFontClose", {memory}), 0);
  //a file, whole: its length and 12
  before = word(m, Log);
  u32 file = viaProgram(m, "sceFontOpenUserFile", {f.library, m.string("ms0:/font.pgf"), 1, Error});
  CHECK(file != 0 && word(m, Error) == 0, true);
  CHECK(logged(m, before) == (std::vector<std::pair<u32, u32>>{{1, u32(bytes.size())}, {1, 12}}), true);
  //from its tables: without the callbacks refused; with them, read (through the kernel's files here)
  CHECK(viaProgram(m, "sceFontOpenUserFile", {f.library, m.string("ms0:/font.pgf"), 0, Error}), 0);
  CHECK(word(m, Error), 0x8046'0003);
  for(u32 offset : {0x14, 0x18, 0x1c, 0x20}) m.system.memory.write(4, f.library + offset, Free);
  before = word(m, Log);
  u32 shared = viaProgram(m, "sceFontOpenUserFile", {f.library, m.string("ms0:/font.pgf"), 0, Error});
  CHECK(shared != 0 && shared != file && word(m, Log) == before, true);  //the same file: loaded already
  CHECK(viaProgram(m, "sceFontOpenUserFile", {f.library, m.string("ms0:/none.pgf"), 1, Error}), 0);
  CHECK(word(m, Error), 0x8046'0005);
  CHECK(viaProgram(m, "sceFontOpenUserFile", {f.library, m.string("ms0:/text.txt"), 1, Error}), 0);
  CHECK(word(m, Error), 0x8046'000a);
  CHECK(viaProgram(m, "sceFontOpenUserFile", {f.library, 0, 1, Error}), 0);
  CHECK(word(m, Error), 0x8046'0003);
  auto prepare = [&](KernelMachine& fresh) {
    fresh.kernel.fontsFrom(folder.path.string());
    fresh.kernel.mount("ms0", stick.path.string());
  };
  CHECK(roundTrip(m, prepare), true);
  //a state whose file has changed since: refused
  auto state = saveState(m);
  stick.put("font.pgf", std::string(bytes.begin(), bytes.end() - 4));
  KernelMachine fresh;
  prepare(fresh);
  CHECK(loadState(fresh, state), false);
  stick.put("font.pgf", std::string(bytes.begin(), bytes.end()));

  //A file opened by a path relative to the working folder (sceIoChdir's) is kept by its whole path: a state's fonts
  //are read again before the state's working folder is put back, and a fresh machine's is the memory stick's top.
  {
    FontMachine g(folder);
    stick.put("FONTS/A.PGF", std::string(bytes.begin(), bytes.end()));
    g.m.kernel.mount("ms0", stick.path.string());
    CHECK(g.m.call("sceIoChdir", {g.m.string("ms0:/FONTS")}), 0);
    u32 relative = viaProgram(g.m, "sceFontOpenUserFile", {g.library, g.m.string("A.PGF"), 1, Error});
    CHECK(relative != 0 && word(g.m, Error) == 0, true);
    CHECK(g.m.kernel.openFonts.size() == 1 && g.m.kernel.openFonts.begin()->second.path == "ms0:/FONTS/A.PGF", true);
    KernelMachine other;
    prepare(other);
    CHECK(other.kernel.workingDirectory == "ms0:/" && loadState(other, saveState(g.m)), true);
    CHECK(roundTrip(g.m, prepare), true);
  }

  //A font in memory given a length past its end (openmem's -1) is read 8 MiB at most, not to the end of RAM, as it
  //opens and as a state brings it back; opened again where the library has it open, it's shared without its memory
  //being read again (here written over since).
  {
    FontMachine g(folder);
    g.m.system.memory.copyIn(0x0896'0000, bytes.data(), bytes.size());
    u32 first = viaProgram(g.m, "sceFontOpenUserMemory", {g.library, 0x0896'0000, ~0u, Error});
    CHECK(first != 0 && word(g.m, Error) == 0, true);
    auto read = [](KernelMachine& k) {
      u64 most = 0;
      for(auto& [id, font] : k.kernel.openFonts) if(font.pgf) most = std::max<u64>(most, font.pgf->size());
      return most;
    };
    CHECK(read(g.m) >= bytes.size() && read(g.m) <= 8_MiB, true);
    CHECK(g.m.call("sceFontGetCharInfo", {first, 'B', Buffer}), 0);
    CHECK(word(g.m, Buffer + 0x10), 303);
    KernelMachine other;
    prepare(other);
    CHECK(loadState(other, saveState(g.m)), true);
    CHECK(read(other) >= bytes.size() && read(other) <= 8_MiB, true);
    g.m.system.memory.fill(0x0896'0000, 0xee, bytes.size());
    u32 before = word(g.m, Log);
    u32 again = viaProgram(g.m, "sceFontOpenUserMemory", {g.library, 0x0896'0000, ~0u, Error});
    CHECK(again != 0 && again != first && word(g.m, Error) == 0 && word(g.m, Log) == before, true);
  }
}

//The resolution and conversions, as pspautotests' resolution recorded them: 128 dots an inch to start; above 0
//taken, infinity too; 0, -1 and minus infinity refused (INVALID_PARAMETER), not-a-number too; points to pixels and
//back at the library's resolution, which a library done with keeps.
static auto fontsResolution() -> void {
  FontFolder folder;
  FontMachine f(folder);
  auto& m = f.m;
  auto set = [&](float h, float v, u32 library) {
    m.system.fpu.r[12] = std::bit_cast<u32>(h), m.system.fpu.r[13] = std::bit_cast<u32>(v);
    return m.call("sceFontSetResolution", {library});
  };
  auto convert = [&](const char* name, float value, u32 library) {
    m.system.fpu.r[12] = std::bit_cast<u32>(value);
    m.system.memory.write(4, Error, 0x1337);
    m.call(name, {library, Error});
    return floatOf(m.system.fpu.r[0]);
  };
  float infinity = std::numeric_limits<float>::infinity();
  CHECK(set(1, 1, f.library), 0);
  for(auto [h, v] : {std::pair{-1.0f, -1.0f}, {0.0f, 0.0f}, {-1.0f, 128.0f}, {-infinity, -infinity}, {NAN, 1.0f}}) {
    check(__LINE__, "refused", set(h, v, f.library), 0x8046'0003);
  }
  CHECK(floatOf(word(m, f.library + 0x38)) == 1.0f && floatOf(word(m, f.library + 0x3c)) == 1.0f, true);
  CHECK(set(infinity, infinity, f.library), 0);
  CHECK(set(128, 128, 0), 0x8046'0002);
  CHECK(set(128, 128, f.library), 0);
  CHECK(convert("sceFontPixelToPointH", 5, f.library) == 2.8125f && word(m, Error) == 0, true);
  CHECK(convert("sceFontPointToPixelV", 5, f.library) == 5 * 128.0f / 72, true);  //8.888889
  CHECK(set(64, 64, f.library), 0);
  CHECK(convert("sceFontPixelToPointV", -5, f.library) == -5.625f, true);
  CHECK(convert("sceFontPointToPixelH", 5, f.library) == 5 * 64.0f / 72, true);  //4.444445
  CHECK(convert("sceFontPointToPixelH", 5, 0) == 0.0f && word(m, Error) == 0x8046'0002, true);
  viaProgram(m, "sceFontDoneLib", {f.library});
  CHECK(convert("sceFontPixelToPointH", 5, f.library) == 5.625f, true);
}

//States: a library's functions part way through, in the program's alloc (which waits there), carry on in another
//machine as they would have in the first; open fonts come back from the folder; a state whose fonts aren't there, or
//aren't the same, is refused, the machine as it was, and the kernel says which (a runner given no fonts folder, or
//another, can't load Peace Walker's states).
static auto fontsStates() -> void {
  FontFolder folder;
  auto prepare = [&](KernelMachine& m) { m.kernel.fontsFrom(folder.path.string()); };
  //each moment of an open: one machine runs it through; others are saved part way, loaded, and run on
  auto opening = [&](KernelMachine& m) {
    memoryFunctions(m);
    Assembler a{m, Code};
    a.li(a0, parameters(m, 4, SlowAlloc));
    a.li(a1, Error);
    a.call("sceFontNewLib");
    a.put(addu(s0, v0, zero));
    a.put(addu(a0, v0, zero));
    a.li(a1, 17);
    a.li(a2, 0);
    a.li(a3, Error);
    a.call("sceFontOpen");
    a.li(t9, Answer);
    a.put(sw(v0, 0, t9));
    a.put(sw(s0, 4, t9));
    a.call("sceKernelExitThread");
    m.system.recompiler.enabled = false;
    m.system.power(Code);
    s32 uid = m.kernel.createThread("main", Code, 0x20, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
  };
  KernelMachine whole;
  prepare(whole);
  opening(whole);
  whole.kernel.run(Kernel::CPUFrequency / 10);
  u32 handle = word(whole, Answer);
  CHECK(handle != 0 && word(whole, Error) == 0, true);
  CHECK(word(whole, Log), 4 + 9);
  for(u64 at : {Kernel::CPUFrequency / 20'000, Kernel::CPUFrequency / 2000, Kernel::CPUFrequency / 500}) {
    KernelMachine part;
    prepare(part);
    opening(part);
    part.kernel.run(at);
    CHECK(part.kernel.fontCalls.size(), 1);  //waiting in its alloc
    auto state = saveState(part);
    KernelMachine fresh;
    prepare(fresh);
    CHECK(loadState(fresh, state), true);
    CHECK(saveState(fresh) == state, true);
    fresh.kernel.run(Kernel::CPUFrequency / 10);
    CHECK(word(fresh, Answer) == handle && word(fresh, Answer + 4) == word(whole, Answer + 4), true);
    CHECK(logged(fresh) == logged(whole), true);
    CHECK(fresh.kernel.openFonts.size() == 1 && fresh.kernel.fontCalls.empty(), true);
    //into a machine without its fonts, or with another kr0.pgf: refused, the machine as it was
    KernelMachine none;
    CHECK(loadState(none, state), false);
    CHECK(!none.notes.empty() && none.notes.back().find("font library open") != std::string::npos, true);
    FontFolder other({0, 17});
    auto changed = systemFont(17);
    changed.name = "Another";
    auto bytes = changed.build();
    other.put("kr0.pgf", std::string(bytes.begin(), bytes.end()));
    KernelMachine differs;
    differs.kernel.fontsFrom(other.path.string());
    if(at == Kernel::CPUFrequency / 500) {  //its font is being read in by then (its tables asked for)
      CHECK(loadState(differs, state), false);
    }
  }
  //a machine with a font open, its library's fields as the program changed them: round trips; without the font's
  //file it's refused
  FontMachine f(folder);
  u32 font = viaProgram(f.m, "sceFontOpen", {f.library, 5, 0, Error});
  CHECK(font != 0, true);
  CHECK(roundTrip(f.m, prepare), true);
  auto state = saveState(f.m);
  FontFolder partial({0, 1, 2});
  KernelMachine without;
  without.kernel.fontsFrom(partial.path.string());
  CHECK(loadState(without, state), false);
  CHECK(!without.notes.empty() && without.notes.back().find("open system font ltn4.pgf isn't in the fonts folder") !=
        std::string::npos, true);
  KernelMachine with;
  prepare(with);
  CHECK(loadState(with, state), true);
  CHECK(with.call("sceFontGetCharInfo", {font, 'B', Buffer}), 0);
  CHECK(word(with, Buffer + 0x10), 305);

  //A call opening a font of the program's memory, waiting in the program's alloc: such a font is read where it is,
  //never whole into memory (mode 1, a file's or a system font's), so a state saying it is is refused. (Loaded, its
  //call ran on into the ending of a font read whole, which writes the record into a second block it never asked for.)
  KernelMachine m;
  prepare(m);
  memoryFunctions(m);
  auto bytes = systemFont(1).build();
  m.system.memory.copyIn(0x0896'0000, bytes.data(), bytes.size());
  Assembler a{m, Code};
  a.li(a0, parameters(m, 4));
  a.li(a1, Error);
  a.call("sceFontNewLib");
  a.put(addu(s0, v0, zero));
  a.li(t0, SlowAlloc);
  a.put(sw(t0, 0x0c, s0));  //the library's alloc, the slow one from here on
  a.put(addu(a0, s0, zero));
  a.li(a1, 0x0896'0000);
  a.li(a2, u32(bytes.size()));
  a.li(a3, Error);
  a.call("sceFontOpenUserMemory");
  a.li(t9, Answer);
  a.put(sw(v0, 0, t9));
  a.call("sceKernelExitThread");
  m.system.recompiler.enabled = false;
  m.system.power(Code);
  s32 uid = m.kernel.createThread("main", Code, 0x20, 0x4000, 0, 0);
  m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
  auto waiting = [&] {
    return m.kernel.fontCalls.size() == 1 && m.kernel.fontCalls.begin()->second.kind == Kernel::FontCall::Open;
  };
  for(u32 step = 0; step < 1000 && !waiting(); step++) m.kernel.run(Kernel::CPUFrequency / 100'000);
  CHECK(waiting(), true);
  if(!waiting()) return;
  auto& call = m.kernel.fontCalls.begin()->second;
  CHECK(call.opening.source == 2 && call.opening.mode == 0 && !call.ended && call.got.empty(), true);
  KernelMachine fresh;
  prepare(fresh);
  CHECK(loadState(fresh, saveState(m)), true);
  call.opening.mode = 1;
  KernelMachine damaged;
  prepare(damaged);
  CHECK(loadState(damaged, saveState(m)), false);
}

//Without the owner's fonts (no folder, an empty one, or one of other files) the library is the stand-in ("fonts
//missing", media.cpp); with some of them, the rest are listed but can't be opened.
static auto fontsFolder() -> void {
  KernelMachine m;
  m.kernel.fontsFrom("");
  CHECK(m.kernel.fontsInstalled(), false);
  HostFolder empty;
  empty.put("readme.txt", "x");
  m.kernel.fontsFrom(empty.path.string());
  CHECK(m.kernel.fontsInstalled() == false && m.kernel.systemFonts.empty(), true);
  CHECK(m.notes.size(), 1);
  FontFolder some({1, 9});
  some.put("ltn2.pgf", "damaged");
  auto bytes = systemFont(0).build();
  some.put("JPN0.PGF", std::string(bytes.begin(), bytes.end()));  //whatever the case
  FontMachine f(some);
  CHECK(f.m.kernel.systemFonts.size(), 18);
  CHECK(f.m.kernel.systemFonts[0].pgf != nullptr && f.m.kernel.systemFonts[1].pgf != nullptr, true);
  CHECK(viaProgram(f.m, "sceFontOpen", {f.library, 0, 0, Error}) != 0, true);
  CHECK(viaProgram(f.m, "sceFontOpen", {f.library, 4, 0, Error}), 0);
  CHECK(word(f.m, Error), 0x8046'0005);  //not there
  CHECK(viaProgram(f.m, "sceFontOpen", {f.library, 3, 0, Error}), 0);
  CHECK(word(f.m, Error), 0x8046'000a);  //damaged
  CHECK(viaProgram(f.m, "sceFontOpen", {f.library, 18, 0, Error}), 0);
  CHECK(word(f.m, Error), 0x8046'0003);
  CHECK(viaProgram(f.m, "sceFontOpen", {0, 1, 0, Error}), 0);
  CHECK(word(f.m, Error), 0x8046'0002);
  //a file bigger than any of the PSP's fonts (over 4 MiB) isn't read, even one that is a font
  FontFolder big({1});
  auto font = systemFont(5).build();
  font.resize(4_MiB + 1);
  big.put("ltn4.pgf", std::string(font.begin(), font.end()));
  KernelMachine large;
  large.kernel.fontsFrom(big.path.string());
  CHECK(large.kernel.systemFonts.size(), 18);
  if(large.kernel.systemFonts.size() != 18) return;
  CHECK(large.kernel.systemFonts[5].present && !large.kernel.systemFonts[5].pgf, true);
  CHECK(large.kernel.systemFonts[1].pgf != nullptr, true);
  bool noted = false;
  for(auto& note : large.notes) noted |= note.find("ltn4.pgf") != std::string::npos;
  CHECK(noted, true);
}

auto fontTests() -> Tests {
  return {{"pgf read back", pgfReadBack}, {"pgf damaged", pgfDamaged}, {"fonts memory", fontsMemory},
          {"fonts found", fontsFound}, {"fonts measured", fontsMeasured}, {"fonts drawn", fontsDrawn},
          {"fonts of the program's own", fontsOwn}, {"fonts resolution", fontsResolution},
          {"fonts states", fontsStates}, {"fonts folder", fontsFolder}};
}

}
