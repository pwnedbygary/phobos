//sceLibFont, the system's font library: it draws the PSP's own fonts (flash0:/font, the PGF files pgf.cpp reads) and
//PGFs games carry, a character at a time, into a buffer of the game's, for games that print with them. The fonts are
//the PSP's firmware, which Phobos never has: they come from the owner's own PSP (a flash0 dump), in a host folder
//the system gives (option "Fonts": System::power() hands it to fontsFrom()). Without them the library is what it was
//before there were fonts: it starts and finds none (the stand-in at the end of this file).
//
//What the library does and answers is what pspautotests' font tests recorded on a PSP: its memory, its list of fonts,
//finding and opening them, every measurement, and the pictures to the byte, in every pixel format, clipped and at
//fractions of a pixel. The structures and errors are pspautotests' libfont.h and vitasdk's (the PS Vita's libpgf is
//the same library). No other emulator's code was read. Where neither shows what a PSP does, the code says what was
//chosen.
//
//The library works in the game's memory, as Sony's does (it's a user module, libfont.prx, that games carry and call):
//sceFontNewLib's parameters name the game's own alloc and free functions, which the library calls for every block it
//keeps, in the sizes and order pspautotests recorded: its own 76 bytes (the handle the game holds, which games may
//read and write: tests read its resolution and set its callbacks there), each open font's tables, and so on. It calls
//them as a thread's callbacks are called (events.cpp): the thread that called the library runs the game's function
//on its own stack, returning to the trampoline's sixth syscall (fontReturned()), and the library goes on from there
//until it returns to the game with its result (FontCall). The fonts' pictures are read from their bytes on the host,
//which come back from where they came from (the folder, the game's file or memory) when a state is loaded.
//
//Not as Sony's library does it: a font file opened a piece at a time (sceFontOpenUserFile's mode 0) is read through
//the kernel's own files, not the game's open, read, seek and close callbacks (which must be set, as on a PSP, but
//aren't called), so a game whose callbacks read an archive of its own wouldn't find its font; and drawing a glyph
//asks the game for no memory (a PSP borrows two blocks a font, the glyph's bytes and its picture, till it closes).

namespace {
  constexpr u32 StandInLibrary = 0x0000'f001;  //the handle the stand-in gives (nothing a game reads through)

  //the library's errors (vitasdk's SCE_FONT_ERROR_*)
  constexpr u32 FontOutOfMemory      = 0x8046'0001;
  constexpr u32 FontInvalidLibrary   = 0x8046'0002;
  constexpr u32 FontInvalidParameter = 0x8046'0003;
  constexpr u32 FontOpenFailed       = 0x8046'0005;  //a file that can't be opened (not there)
  constexpr u32 FontTooManyOpen      = 0x8046'0009;
  constexpr u32 FontInvalidData      = 0x8046'000a;  //a file that isn't a PGF this can read

  //The library's structures in the game's memory. Its own (libfont.h's FontLibrary, 0x4c bytes): the parameters as
  //given (0x2c bytes: user data, how many fonts, a cache, then the alloc, free, open, close, read, seek, error and
  //ioFinish functions), two pointers, the resolution (0x38, 0x3c: floats), how many fonts the system has (0x40), the
  //list of them (0x44) and the alternative character (0x48, 16 bits). Each font open takes a handle (76 bytes) and
  //data (560) of the blocks sceFontNewLib asked for; what's in those no game reads, and they're left as they were.
  constexpr u32 HandleSize = 76, HandleDataSize = 560, MemoryFontSize = 12, MaxFonts = 9;
  constexpr u32 StyleSize = 0xa8, InfoSize = 0x105, CharInfoSize = 0x3c;

  //The most read of a font: a system font's file (the biggest of the PSP's, jpn0.pgf, is 1.5 MB), and of the game's
  //memory for a font held there (fontMemory()).
  constexpr u32 FontFileMost = 4_MiB, MemoryFontMost = 8_MiB;

  //The PSP's eighteen fonts, in the order its libfont lists them (pspautotests' fontlist recorded jpn0 first, and
  //open each one's memory, which the owner's files match font for font; find and optimum recorded which comes first
  //for each family and style), and what the list says of each beyond its file: family (1 sans-serif, 2 serif),
  //style (1 regular, 2 italic, 5 bold, 6 bold italic, 103 demi-bold), language (1 Japanese, 2 Latin, 3 Korean) and
  //country (1, but the Korean font's, which isn't 1 there: 3 is chosen).
  struct SystemFontEntry { const char* file; u16 family, style, language, country; };
  constexpr SystemFontEntry systemFontTable[18] = {
    {"jpn0.pgf", 1, 103, 1, 1},
    {"ltn0.pgf", 1, 1, 2, 1}, {"ltn1.pgf", 2, 1, 2, 1}, {"ltn2.pgf", 1, 2, 2, 1}, {"ltn3.pgf", 2, 2, 2, 1},
    {"ltn4.pgf", 1, 5, 2, 1}, {"ltn5.pgf", 2, 5, 2, 1}, {"ltn6.pgf", 1, 6, 2, 1}, {"ltn7.pgf", 2, 6, 2, 1},
    {"ltn8.pgf", 1, 1, 2, 1}, {"ltn9.pgf", 2, 1, 2, 1}, {"ltn10.pgf", 1, 2, 2, 1}, {"ltn11.pgf", 2, 2, 2, 1},
    {"ltn12.pgf", 1, 5, 2, 1}, {"ltn13.pgf", 2, 5, 2, 1}, {"ltn14.pgf", 1, 6, 2, 1}, {"ltn15.pgf", 2, 6, 2, 1},
    {"kr0.pgf", 1, 1, 3, 3},
  };

  //A font's bytes, to tell them from others when a state comes back (as the system tells programs apart).
  auto fontHash(const std::vector<u8>& bytes) -> u64 {
    u64 value = 0xcbf2'9ce4'8422'2325;
    for(u8 byte : bytes) value = (value ^ byte) * 0x100'0000'01b3;
    return value;
  }

  auto floatBits(float value) -> u32 {
    u32 bits;
    memcpy(&bits, &value, 4);
    return bits;
  }

  auto bitsFloat(u32 bits) -> float {
    float value;
    memcpy(&value, &bits, 4);
    return value;
  }
}

//The system fonts from a host folder: the eighteen by name, whatever their case there, each read and checked whole,
//one bigger than FontFileMost not read at all. One missing or damaged keeps its place in the list, and can't be
//opened (sceFontOpen says which); with none at all the library is the stand-in.
auto Kernel::fontsFrom(const std::string& folder) -> void {
  systemFonts.clear();
  if(folder.empty()) return;
  std::map<std::string, std::filesystem::path> found;
  std::error_code error;
  for(auto& item : std::filesystem::directory_iterator(folder, error)) {
    std::string name = item.path().filename().string();
    for(auto& c : name) c = std::tolower(u8(c));
    found[name] = item.path();
  }
  std::string missing;
  for(auto& entry : systemFontTable) {
    SystemFont font;
    font.file = entry.file;
    if(auto at = found.find(entry.file); at != found.end()) {
      font.present = true;
      std::error_code sizeError;
      u64 size = std::filesystem::file_size(at->second, sizeError);
      if(sizeError || size > FontFileMost) {
        note(std::string{"fonts: "} + entry.file + " isn't read: " +
             (sizeError ? "it isn't a file" : "it's bigger than any of the PSP's fonts"));
      } else {
        std::ifstream file(at->second, std::ios::binary);
        std::vector<u8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        font.hash = fontHash(bytes);
        auto pgf = std::make_shared<PGF>();
        std::string problem;
        if(pgf->open(std::move(bytes), problem)) font.pgf = pgf;
        else note(std::string{"fonts: "} + entry.file + " can't be read: " + problem);
      }
    }
    if(!font.pgf) missing += std::string{missing.empty() ? "" : ", "} + entry.file;
    systemFonts.push_back(std::move(font));
  }
  if(!fontsInstalled()) {
    systemFonts.clear();
    return note("fonts: none of the PSP's fonts are in " + folder);
  }
  if(!missing.empty()) note("fonts: missing from " + folder + ", or damaged: " + missing);
}

//Whether the system's fonts are there (any of them): if not, the library is the stand-in.
auto Kernel::fontsInstalled() const -> bool {
  for(auto& font : systemFonts) if(font.pgf) return true;
  return false;
}

auto Kernel::fontLibraryAt(u32 address) -> FontLibrary* {
  auto found = fontLibraries.find(address);
  return address && found != fontLibraries.end() ? &found->second : nullptr;
}

//The library and handle a font handle is (one of a library's handles: its block, 76 bytes a handle); false if none.
auto Kernel::fontHandle(u32 handle, FontLibrary*& library, u32& slot) -> bool {
  if(!handle) return false;
  for(auto& [address, candidate] : fontLibraries) {
    if(!candidate.slots || handle < candidate.handles) continue;
    u32 offset = handle - candidate.handles;
    if(offset % HandleSize || offset / HandleSize >= candidate.slots) continue;
    library = &candidate;
    slot = offset / HandleSize;
    return true;
  }
  return false;
}

//The font a handle stands for: the one it last opened, while that's loaded. A closed handle still answers while
//another handle keeps its font loaded, as on a PSP (pspautotests' "GetCharInfo on closed"); one whose font has gone
//is refused (where a PSP would read freed memory).
auto Kernel::fontFor(u32 handle) -> OpenFont* {
  FontLibrary* library;
  u32 slot;
  if(!fontHandle(handle, library, slot)) return nullptr;
  auto found = openFonts.find(library->fonts[slot]);
  return found != openFonts.end() && found->second.pgf ? &found->second : nullptr;
}

//How many fonts the library at address lists (what it keeps at 0x40, which sceFontDoneLib clears), no more than the
//system has; 0 if there's no memory there.
auto Kernel::fontCount(u32 library) -> u32 {
  if(!memory.reaches(library + 0x40, 4)) return 0;
  return std::min<u32>(memory.read(4, library + 0x40), systemFonts.size());
}

//A font's style (a FontStyle, 168 bytes): its sizes and resolution from its file; and for a system font (system: its
//place in the list) what the list says of it (a font of the game's has none: pspautotests' fontinfo recorded zeros
//and empty names).
auto Kernel::fontStyle(u32 address, const PGF& pgf, s32 system) -> void {
  u8 style[StyleSize] = {};
  auto put32 = [&](u32 at, u32 value) { for(u32 n : range(4)) style[at + n] = value >> n * 8; };
  auto put16 = [&](u32 at, u32 value) { style[at] = value; style[at + 1] = value >> 8; };
  put32(0x00, floatBits(pgf.pointSize[0] / 64.0f));
  put32(0x04, floatBits(pgf.pointSize[1] / 64.0f));
  put32(0x08, floatBits(pgf.resolution[0] / 64.0f));
  put32(0x0c, floatBits(pgf.resolution[1] / 64.0f));
  if(system >= 0) {
    auto& entry = systemFontTable[system];
    put16(0x14, entry.family);
    put16(0x16, entry.style);
    put16(0x1a, entry.language);
    put16(0x1e, entry.country);
    memcpy(&style[0x20], pgf.name.data(), std::min<u64>(pgf.name.size(), 63));
    memcpy(&style[0x60], entry.file, strlen(entry.file));
  }
  memory.copyIn(address, style, StyleSize);
}

//A system font's style as the list gives it; a missing one keeps the list's say, with no sizes.
auto Kernel::systemFontStyle(u32 address, u32 index) -> void {
  static const PGF none;
  fontStyle(address, systemFonts[index].pgf ? *systemFonts[index].pgf : none, index);
}

//The glyph a font draws for a character code, picture and measurements: its own; or else the library's alternative
//character's ('_' to start with: sceFontSetAltCharacterCode); or nothing at all (an empty glyph: no picture, every
//measurement 0) for a code below the font's first, or when the alternative is missing too, as pspautotests'
//charinfo, charglyphimage and altcharcode recorded. A damaged glyph counts as missing. A shadow is the character's
//measurements with its shadow's picture and place.
auto Kernel::fontGlyph(const OpenFont& font, u32 code, PGF::Glyph& glyph, bool shadow) -> void {
  auto& pgf = *font.pgf;
  glyph = {};
  if(code < pgf.firstCode) return;
  s32 index = pgf.glyphFor(code);
  if(index < 0 || !pgf.glyph(index, glyph)) {
    u32 alternative = memory.reaches(font.library + 0x48, 2) ? memory.read(2, font.library + 0x48) : 0x5f;
    index = alternative < pgf.firstCode ? -1 : pgf.glyphFor(alternative);
    if(index < 0 || !pgf.glyph(index, glyph)) return void(glyph = {});
  }
  if(!shadow) return;
  PGF::Glyph picture;
  if(!pgf.shadow(glyph.shadowID, picture)) picture = {};
  glyph.width = picture.width;
  glyph.height = picture.height;
  glyph.left = picture.left;
  glyph.top = picture.top;
  glyph.pixels = std::move(picture.pixels);
}

//Draws a glyph's picture into the game's buffer, as the GlyphImage at image (24 bytes) says: the pixel format, the
//picture's top left corner (x, y, in 64ths of a pixel), the buffer's width and height in pixels and bytes a line (16
//bits each), and the buffer. Only two formats draw: 0, 4 bits a pixel, the left one in the low nibble, and 2, a byte
//a pixel (shade n as 17n); 1 (4 bits, reversed), 3 (24) and 4 (32) leave the buffer as it was, as pspautotests'
//charglyphimagexfrac recorded. Each pixel is added to what the buffer holds, up to the brightest (a glyph never
//erases what's there), and the vertical fraction is dropped. A horizontal fraction f (64ths) shares each pixel v
//between its column and the next: the next gets v * f / 64 rounded down, its own column the rest (charglyphimage and
//charglyphimagexfrac recorded exactly that). Drawing keeps to the buffer and to the clip rectangle (left and top,
//then width and height taken as unsigned: sceFontGetCharGlyphImage_Clip's), and to memory there is.
auto Kernel::fontDraw(const PGF::Glyph& glyph, u32 image, s64 clipLeft, s64 clipTop, u64 clipWidth, u64 clipHeight)
-> void {
  u32 format = memory.read(4, image + 0x00);
  s32 x64 = s32(memory.read(4, image + 0x04));
  s32 y64 = s32(memory.read(4, image + 0x08));
  u32 width = memory.read(2, image + 0x0c), height = memory.read(2, image + 0x0e);
  u32 line = memory.read(2, image + 0x10);
  u32 buffer = memory.read(4, image + 0x14);
  if(format != 0 && format != 2) return;
  u64 spanX = std::min<u64>(clipWidth, 1 << 20), spanY = std::min<u64>(clipHeight, 1 << 20);
  s64 left = std::max<s64>(0, clipLeft), right = std::min<s64>(width, clipLeft + s64(spanX));
  s64 top = std::max<s64>(0, clipTop), bottom = std::min<s64>(height, clipTop + s64(spanY));
  s64 x0 = x64 >> 6, y0 = y64 >> 6;
  u32 fraction = x64 & 63;
  auto add = [&](s64 x, s64 y, u32 amount) {
    if(!amount || x < left || x >= right || y < top || y >= bottom) return;
    u32 address = buffer + u32(y * line) + u32(format == 2 ? x : x >> 1);
    u8* byte = memory.pointer(address);
    if(!byte) return;
    if(format == 2) {
      *byte = std::min<u32>(255, *byte + amount);
    } else {
      u32 shift = (x & 1) * 4;
      u32 nibble = std::min<u32>(15, (*byte >> shift & 15) + amount);
      *byte = (*byte & ~(15 << shift)) | nibble << shift;
    }
    memory.changed(address, 1);
  };
  for(u32 y : range(glyph.height)) {
    for(u32 x : range(glyph.width)) {
      u32 shade = glyph.pixels[y * glyph.width + x];
      if(format == 2) shade *= 17;
      u32 spill = shade * fraction / 64;
      add(x0 + x, y0 + y, shade - spill);
      add(x0 + x + 1, y0 + y, spill);
    }
  }
}

//Calls into the game. The library's functions that need memory (sceFontNewLib, the opens, sceFontClose and
//sceFontDoneLib) ask the game's alloc for it, and give it back through the game's free, one call at a time. The
//thread that called the library runs each from where it called the library, with its registers as they were there but
//for the arguments (the parameters' user data, then a size or a block), a stack below its own, and a return to the
//trampoline's sixth syscall. The game's function may wait or be interrupted, like any of its code. From there
//(fontReturned()), the library does what's next: give back what's left to give, ask for what's left to ask for, then
//its ending (fontEnd()), and return to the game (fontFinish()).
auto Kernel::fontNext(FontCall& call) -> void {
  auto run = [&](u32 function, u32 argument) {
    restore(call.caller);
    cpu.ipu.r[4] = call.userData;
    cpu.ipu.r[5] = argument;
    cpu.ipu.r[29] = (call.caller.gpr[29] - 0x40) & ~15u;
    cpu.ipu.r[31] = Trampoline + 32;
    cpu.ipu.pc = function;
    cpu.ipu.pd = function + 4;
  };
  while(true) {
    std::erase(call.frees, 0u);  //nothing to give back for a block alloc didn't give
    if(!call.frees.empty()) return run(call.free, call.frees.front());
    if(!call.ended && call.got.size() < call.asks.size()) return run(call.alloc, call.asks[call.got.size()]);
    if(call.ended) return fontFinish();
    call.ended = true;
    fontEnd(call);
  }
}

//The trampoline's sixth syscall: the game's alloc or free returned to the library (alloc's block in v0). A block
//alloc couldn't give (none, for a size above 0) ends the function: what it gave goes back, and the function fails,
//out of memory, as pspautotests' newlib recorded.
auto Kernel::fontReturned() -> void {
  if(!current) return;
  auto found = fontCalls.find(current->uid);
  if(found == fontCalls.end()) return;
  auto& call = found->second;
  u32 value = cpu.ipu.r[2];
  if(!call.frees.empty()) {
    call.frees.erase(call.frees.begin());
  } else if(!call.ended && call.got.size() < call.asks.size()) {
    call.got.push_back(value);
    if(!value && call.asks[call.got.size() - 1]) {
      call.ended = true;
      call.result = 0;
      call.error = FontOutOfMemory;
      call.frees.assign(call.got.rbegin(), call.got.rend());
      if(call.kind == FontCall::Open) fontRelease(call);
    }
  }
  fontNext(call);
}

//An open that won't be: the handle it held for its font is free again.
auto Kernel::fontRelease(FontCall& call) -> void {
  if(auto library = fontLibraryAt(call.library)) library->open[call.slot] = false;
}

//A thread ended, or went, part way through a library function: the function ends with it, the memory the program gave
//it staying given (as on a PSP, where it would be lost the same way), and a handle it held for a font free again.
auto Kernel::fontAbandoned(u32 thread) -> void {
  auto found = fontCalls.find(thread);
  if(found == fontCalls.end()) return;
  if(found->second.kind == FontCall::Open && !found->second.ended) fontRelease(found->second);
  fontCalls.erase(found);
}

//The calling thread's library function returns to the game: its result, and its error where it asked for one.
auto Kernel::fontFinish() -> void {
  auto found = fontCalls.find(current->uid);
  auto& call = found->second;
  restore(call.caller);
  cpu.ipu.r[2] = call.result;
  if(call.errorAt && memory.reaches(call.errorAt, 4)) memory.write(4, call.errorAt, call.error);
  fontCalls.erase(found);
}

//Starts a library function that calls into the game, on the calling thread: false if it can't (from no thread or an
//interrupt handler, which can't run the game's functions this way, or a thread in a library function already).
auto Kernel::fontStart(FontCall call) -> bool {
  if(!current || interrupting || fontCalls.count(current->uid)) return false;
  save(call.caller);
  auto& started = fontCalls[current->uid] = std::move(call);
  fontNext(started);
  return true;
}

//A library function's ending, its memory all given: what it made, and what it returns.
auto Kernel::fontEnd(FontCall& call) -> void {
  if(call.kind == FontCall::NewLib) {
    u32 library = call.got[0];
    //the parameters as given, then the library's own fields (pspautotests' newlib: the parameters the same, 128 dots
    //an inch both ways, 18 fonts, '_' for characters a font hasn't got, the rest 0)
    std::vector<u8> parameters(0x2c);
    memory.copyOut(parameters.data(), call.library, 0x2c);
    memory.copyIn(library, parameters.data(), 0x2c);
    u32 words[] = {call.got[1], call.got[2], 0, floatBits(128.0f), floatBits(128.0f), u32(systemFonts.size()),
                   call.got[3], 0x5f};
    for(u32 n : range(8)) {
      if(memory.reaches(library + 0x2c + n * 4, 4)) memory.write(4, library + 0x2c + n * 4, words[n]);
    }
    for(u32 n : range(systemFonts.size())) systemFontStyle(call.got[3] + n * StyleSize, n);
    FontLibrary made;
    made.address = library;
    made.slots = call.slots;
    made.handles = call.got[1];
    made.data = call.got[2];
    made.list = call.got[3];
    fontLibraries[library] = made;
    call.result = library;
    call.error = 0;
    return;
  }
  if(call.kind == FontCall::Open) {
    auto library = fontLibraryAt(call.library);
    if(!library) {  //done with meanwhile, by another thread
      call.frees.assign(call.got.rbegin(), call.got.rend());
      call.result = 0;
      call.error = FontInvalidLibrary;
      return;
    }
    auto& font = call.opening;
    auto& bytes = font.pgf->data();
    if(font.source == 2) {
      //a font of the program's memory, read where it is (whatever its mode: it asked for the record alone)
      u32 record[3] = {font.address, font.length, 0};
      for(u32 n : range(3)) {
        if(memory.reaches(call.got[0] + n * 4, 4)) memory.write(4, call.got[0] + n * 4, record[n]);
      }
    } else if(font.mode == 1) {
      //the whole file in the program's memory, and a small record of where (what's in it is a guess: the place, the
      //length and a position)
      memory.copyIn(call.got[0], bytes.data(), bytes.size());
      u32 record[3] = {call.got[0], u32(bytes.size()), 0};
      for(u32 n : range(3)) {
        if(memory.reaches(call.got[1] + n * 4, 4)) memory.write(4, call.got[1] + n * 4, record[n]);
      }
    } else {
      //the tables, read into the blocks as they are in the file (pspautotests' openfile: read straight into them)
      for(u32 n : range(9)) {
        auto& part = font.pgf->parts[n];
        memory.copyIn(call.got[n], bytes.data() + part.at, part.length);
      }
    }
    font.id = nextFontID++;
    font.blocks = call.got;
    font.references = 1;
    library->fonts[call.slot] = font.id;
    library->open[call.slot] = true;
    call.result = library->handles + call.slot * HandleSize;
    call.error = 0;
    openFonts[font.id] = std::move(font);
    return;
  }
  call.result = 0;  //memory given back: closing, or the library done with
  call.error = 0;
}

//The memory a font took, in the order closing gives it back: a font read a piece at a time, as pspautotests' fonttest
//recorded (the pointers, the character map, the two lists of ranges, the shadow map, then the four tables); the rest,
//last taken first (an order not recorded).
auto Kernel::fontGiveBack(const OpenFont& font) -> std::vector<u32> {
  auto& b = font.blocks;
  if(b.size() == 9) return {b[8], b[7], b[5], b[6], b[4], b[0], b[1], b[2], b[3]};
  return {b.rbegin(), b.rend()};
}

//The memory sceFontOpen and sceFontOpenUserFile ask for: a file read whole (mode 1), its bytes and a 12-byte record
//(pspautotests' open and openfile: the file's length and 12); else its tables, one block each (open: every system
//font's memory, as the owner's files give it). sceFontOpenUserMemory asks for the record alone (openmem: 12).
static auto fontAsks(const PGF& pgf, u32 mode, u32 source) -> std::vector<u32> {
  if(source == 2) return {MemoryFontSize};
  if(mode == 1) return {pgf.size(), MemoryFontSize};
  std::vector<u32> asks;
  for(auto& part : pgf.parts) asks.push_back(part.length);
  return asks;
}

//Opens a font for library (the opens' common part, past their own checks): a font the library has loaded already
//gets another handle, nothing asked for (pspautotests: "While open: OK (allocated 0)"); else the font is loaded into
//memory the game gives. Each open takes one of the library's handles, numFonts of them (TOO_MANY_OPEN_FONTS when
//they're all open, as with four open of four). Only a font the library hasn't loaded needs its glyphs (pgf) given.
auto Kernel::fontOpen(FontLibrary& library, OpenFont font, u32 errorAt) -> void {
  auto refuse = [&](u32 error) {
    if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, error);
    result(0);
  };
  u32 slot = 0;
  while(slot < library.slots && library.open[slot]) slot++;
  if(slot == library.slots) return refuse(FontTooManyOpen);
  for(auto& [id, loaded] : openFonts) {
    if(loaded.library != library.address || loaded.source != font.source) continue;
    if(font.source == 0 ? loaded.index != font.index : font.source == 1 ? loaded.path != font.path
                        : loaded.address != font.address) continue;
    loaded.references++;
    library.fonts[slot] = id;
    library.open[slot] = true;
    if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, 0);
    return result(library.handles + slot * HandleSize);
  }
  FontCall call;
  call.kind = FontCall::Open;
  call.library = library.address;
  call.errorAt = errorAt;
  call.slot = slot;
  call.userData = memory.read(4, library.address + 0x00);
  call.alloc = memory.read(4, library.address + 0x0c);
  call.free = memory.read(4, library.address + 0x10);
  call.asks = fontAsks(*font.pgf, font.mode, font.source);
  font.library = library.address;
  call.opening = std::move(font);
  if(!fontCallable(call.alloc)) return refuse(FontInvalidParameter);
  library.open[slot] = true;  //held while its memory comes
  library.fonts[slot] = 0;
  if(!fontStart(std::move(call))) {
    library.open[slot] = false;
    refuse(FontInvalidParameter);
  }
}

//Whether the game has a function there for the library to call.
auto Kernel::fontCallable(u32 function) -> bool {
  return function && !(function & 3) && memory.reaches(function, 4);
}

//Copies the game's memory from address, length bytes, as far as RAM goes and no more than MemoryFontMost: a font it
//holds there. Games may give a length far past the font's end (pspautotests' openmem opened one with -1), which
//would otherwise copy the rest of RAM at every open and every state loaded.
auto Kernel::fontMemory(u32 address, u32 length) -> std::vector<u8> {
  u32 physical = address & 0x1fff'ffff;
  if(physical < Memory::RAMBase || physical - Memory::RAMBase >= memory.ram.size()) return {};
  length = std::min<u64>({length, memory.ram.size() - (physical - Memory::RAMBase), MemoryFontMost});
  std::vector<u8> bytes(length);
  memory.copyOut(bytes.data(), address, length);
  return bytes;
}

//A font from a state, read again from where it came from: a system font from the owner's folder, a file from the
//program's devices, memory from the program's own (which the state has just put back). False if it isn't there any
//more, or isn't the same (another firmware's font, say), or doesn't read.
auto Kernel::fontReload(OpenFont& font) -> bool {
  font.pgf.reset();
  if(font.source == 0) {
    if(font.index >= systemFonts.size() || !systemFonts[font.index].pgf) return false;
    if(systemFonts[font.index].hash != font.hash) return false;
    font.pgf = systemFonts[font.index].pgf;
    return true;
  }
  std::vector<u8> bytes;
  if(font.source == 1) {
    if(readWhole(font.path, bytes) || fontHash(bytes) != font.hash) return false;
  } else {
    bytes = fontMemory(font.address, font.length);
  }
  auto pgf = std::make_shared<PGF>();
  std::string problem;
  if(!pgf->open(std::move(bytes), problem)) return false;
  font.pgf = pgf;
  return true;
}

//(parameters, where to put an error): a library, in memory from the game's alloc, as its parameters say: numFonts of
//them open at once (9 at most: pspautotests' newlib recorded the memory of 9 for 9, 10, 100 and -1); 0 and the error
//without alloc and free functions (INVALID_PARAMETER). It asks for its own 76 bytes, then the handles' (76 each),
//their data (560 each), and the list of the system's fonts (168 bytes each), as newlib's totals and fonttest's calls
//have it, and lists the system's 18 fonts in it.
auto Kernel::sceFontNewLib() -> void {
  if(!fontsInstalled()) return standInNewLib();
  u32 parameters = arg(0), errorAt = arg(1);
  auto refuse = [&](u32 error) {
    if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, error);
    result(0);
  };
  if(!memory.reaches(parameters, 0x2c)) return refuse(FontInvalidParameter);
  FontCall call;
  call.kind = FontCall::NewLib;
  call.library = parameters;
  call.errorAt = errorAt;
  call.userData = memory.read(4, parameters + 0x00);
  call.alloc = memory.read(4, parameters + 0x0c);
  call.free = memory.read(4, parameters + 0x10);
  if(!call.alloc || !call.free || !fontCallable(call.alloc)) return refuse(FontInvalidParameter);
  call.slots = std::min<u32>(memory.read(4, parameters + 0x04), MaxFonts);
  call.asks = {0x4c, call.slots * HandleSize, call.slots * HandleDataSize, u32(systemFonts.size()) * StyleSize};
  if(!fontStart(std::move(call))) refuse(FontInvalidParameter);
}

//(library): every font still open closes, its memory going back, then the library's own, through the game's free (its
//list, its handles, their data, then its own 76 bytes: fonttest's order). What it lists is cleared first, as
//pspautotests found it on a library done with (no fonts in it). 0.
auto Kernel::sceFontDoneLib() -> void {
  if(!fontsInstalled()) return result(0);
  auto library = fontLibraryAt(arg(0));
  if(!library) return result(FontInvalidLibrary);
  u32 address = library->address;
  FontCall call;
  call.kind = FontCall::Give;
  call.library = address;
  call.userData = memory.read(4, address + 0x00);
  call.free = memory.read(4, address + 0x10);
  if(memory.reaches(address + 0x40, 4)) memory.write(4, address + 0x40, 0);
  for(auto at = openFonts.begin(); at != openFonts.end();) {
    if(at->second.library != address) { at++; continue; }
    for(u32 block : fontGiveBack(at->second)) call.frees.push_back(block);
    at = openFonts.erase(at);
  }
  for(u32 block : {library->list, library->handles, library->data, address}) call.frees.push_back(block);
  fontLibraries.erase(address);
  result(0);
  if(fontCallable(call.free)) fontStart(std::move(call));
}

//(font handle): the handle closes; the last of a font's handles to close gives its memory back (fontGiveBack()). A
//handle that isn't open is refused (INVALID_PARAMETER: what a PSP does with it isn't known). 0.
auto Kernel::sceFontClose() -> void {
  if(!fontsInstalled()) return result(0);
  FontLibrary* library;
  u32 slot;
  if(!fontHandle(arg(0), library, slot) || !library->open[slot]) return result(FontInvalidParameter);
  library->open[slot] = false;
  result(0);
  auto found = openFonts.find(library->fonts[slot]);
  if(found == openFonts.end() || --found->second.references) return;
  FontCall call;
  call.kind = FontCall::Give;
  call.library = library->address;
  call.userData = memory.read(4, library->address + 0x00);
  call.free = memory.read(4, library->address + 0x10);
  call.frees = fontGiveBack(found->second);
  openFonts.erase(found);
  if(fontCallable(call.free)) fontStart(std::move(call));
}

//(library, index, mode, where to put an error): system font number index, as the list has it. Mode 1 reads its file
//whole into the game's memory; any other reads its tables (pspautotests' open: -1 and 2 as 0). A font not in the
//owner's folder can't be opened (HANDLER_OPEN_FAILED, a file that isn't there), nor a damaged one
//(INVALID_FONT_DATA).
auto Kernel::sceFontOpen() -> void {
  if(!fontsInstalled()) return standInOpen(arg(3));
  auto library = fontLibraryAt(arg(0));
  s32 index = arg(1);
  u32 errorAt = arg(3);
  auto refuse = [&](u32 error) {
    if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, error);
    result(0);
  };
  if(!library) return refuse(FontInvalidLibrary);
  if(index < 0 || u32(index) >= fontCount(library->address)) return refuse(FontInvalidParameter);
  auto& system = systemFonts[index];
  if(!system.pgf) return refuse(system.present ? FontInvalidData : FontOpenFailed);
  OpenFont font;
  font.source = 0;
  font.index = index;
  font.mode = arg(2) == 1 ? 1 : 0;
  font.hash = system.hash;
  font.pgf = system.pgf;
  fontOpen(*library, std::move(font), errorAt);
}

//(library, a PGF in the game's memory, its length, where to put an error): a font the game holds, read where it is.
//Its length counts as unsigned (pspautotests' openmem: -1 and longer than the font open), but 0 doesn't, nor does no
//memory (INVALID_PARAMETER). Of a length past the font's end, 8 MiB at most are read (fontMemory()).
auto Kernel::sceFontOpenUserMemory() -> void {
  if(!fontsInstalled()) return standInOpen(arg(3));
  auto library = fontLibraryAt(arg(0));
  u32 address = arg(1), length = arg(2), errorAt = arg(3);
  auto refuse = [&](u32 error) {
    if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, error);
    result(0);
  };
  if(!library) return refuse(FontInvalidLibrary);
  if(!address || !length) return refuse(FontInvalidParameter);
  OpenFont font;
  font.source = 2;
  font.address = address;
  font.length = length;
  //one the library has open at that address already is shared as it is (fontOpen()): its memory isn't read again
  bool loaded = false;
  for(auto& [id, open] : openFonts) {
    if(open.library == library->address && open.source == 2 && open.address == address) loaded = true;
  }
  if(!loaded) {
    auto pgf = std::make_shared<PGF>();
    std::string problem;
    if(!pgf->open(fontMemory(address, length), problem)) return refuse(FontInvalidData);
    font.pgf = pgf;
  }
  fontOpen(*library, std::move(font), errorAt);
}

//(library, a file's path, mode, where to put an error): a PGF file of the game's. Mode 1 reads it whole into the
//game's memory; mode 0 (any other) needs the library's open, close, read and seek callbacks set (INVALID_PARAMETER
//without any of them: pspautotests' openfile), which a PSP reads the file through, a piece at a time. Here it's read
//through the kernel's files either way (see the top of this file). A file that isn't there: HANDLER_OPEN_FAILED. A
//path relative to the working folder (sceIoChdir's) is kept whole.
auto Kernel::sceFontOpenUserFile() -> void {
  if(!fontsInstalled()) return standInOpen(arg(3));
  auto library = fontLibraryAt(arg(0));
  u32 path = arg(1), mode = arg(2) == 1 ? 1 : 0, errorAt = arg(3);
  auto refuse = [&](u32 error) {
    if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, error);
    result(0);
  };
  if(!library) return refuse(FontInvalidLibrary);
  if(!path) return refuse(FontInvalidParameter);
  if(mode == 0) {
    for(u32 offset : {0x14, 0x18, 0x1c, 0x20}) {
      if(!memory.read(4, library->address + offset)) return refuse(FontInvalidParameter);
    }
  }
  OpenFont font;
  font.source = 1;
  font.path = memory.readString(path, 255);
  //kept by its whole path: a path with no device goes on from the working folder (as split() reads it), which a
  //state puts back only after its fonts have been read again (fontReload())
  if(font.path.find(':') == std::string::npos && workingDirectory.find(':') != std::string::npos) {
    font.path = workingDirectory + "/" + font.path;
  }
  font.mode = mode;
  std::vector<u8> bytes;
  if(readWhole(font.path, bytes)) return refuse(FontOpenFailed);
  font.hash = fontHash(bytes);
  auto pgf = std::make_shared<PGF>();
  std::string problem;
  if(!pgf->open(std::move(bytes), problem)) return refuse(FontInvalidData);
  font.pgf = pgf;
  fontOpen(*library, std::move(font), errorAt);
}

//(library, where to put an error): how many fonts the system has: what the library lists (18, or 0 once it's done
//with: pspautotests' fontlist). No library: 0 and INVALID_LIBID. Like the rest of the functions on a library's own
//fields, this reads them where they are, so a library done with (its memory freed but not yet reused) answers as on a
//PSP.
auto Kernel::sceFontGetNumFontList() -> void {
  if(!fontsInstalled()) return standInCount(arg(1));
  u32 library = arg(0), errorAt = arg(1);
  u32 error = library && memory.reaches(library, 0x4c) ? 0 : FontInvalidLibrary;
  if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, error);
  result(error ? 0 : fontCount(library));
}

//(library, where to put the styles, how many it has room for): the first ones in the list, as many as there's room
//for (none for 0 or less); 0. No library: INVALID_LIBID; no room: INVALID_PARAMETER.
auto Kernel::sceFontGetFontList() -> void {
  if(!fontsInstalled()) return result(0);
  u32 library = arg(0), styles = arg(1);
  s32 room = arg(2);
  if(!library || !memory.reaches(library, 0x4c)) return result(FontInvalidLibrary);
  if(!styles) return result(FontInvalidParameter);
  u32 count = std::min<s64>(std::max(room, 0), fontCount(library));
  for(u32 n : range(count)) systemFontStyle(styles + n * StyleSize, n);
  result(0);
}

//(library, a style, index): font number index's style in the list. No library: INVALID_LIBID; no style, or a number
//past the list's (a library done with lists none): INVALID_PARAMETER (pspautotests' fontinfobyindex).
auto Kernel::sceFontGetFontInfoByIndexNumber() -> void {
  if(!fontsInstalled()) return result(ErrorNotFound);
  u32 library = arg(0), style = arg(1);
  s32 index = arg(2);
  if(!library || !memory.reaches(library, 0x4c)) return result(FontInvalidLibrary);
  if(!style || index < 0 || u32(index) >= fontCount(library)) return result(FontInvalidParameter);
  systemFontStyle(style, index);
  result(0);
}

//How well each of the library's fonts fits a style the game asks for (sceFontFindOptimumFont and sceFontFindFont), as
//pspautotests' optimum and find recorded it. A style asks for what it gives: a size (above 0), a family, a style, a
//sub-style, a language, a region, a country (each not 0), a name or a file name (each not empty); a font's matches
//are how many of those it has (names whole: "ltn0.pg" isn't ltn0.pgf's). The size asked for is a point size at a
//resolution, the style's own where it gives one, else the library's (sceFontSetResolution), measured in a font as
//points at its own resolution; with both sizes given the smaller counts, and a height alone (fontV, no fontH) asks
//for 0, as the recordings have it: no font has that size exactly (find never finds one by its height), and the
//smallest fits it best (optimum gives the first 7-point font for any height). Returns false for a style that asks for
//nothing.
auto Kernel::fontFits(u32 library, u32 style, std::vector<u32>& matches, std::vector<float>& distances,
                      bool& sized) -> bool {
  u8 s[StyleSize];
  memory.copyOut(s, style, StyleSize);
  auto word = [&](u32 at) { return u32(s[at] | s[at + 1] << 8 | s[at + 2] << 16 | s[at + 3] << 24); };
  auto half = [&](u32 at) { return u32(s[at] | s[at + 1] << 8); };
  auto text = [&](u32 at) { return std::string((const char*)&s[at], strnlen((const char*)&s[at], 64)); };
  float h = bitsFloat(word(0x00)), v = bitsFloat(word(0x04));
  float hRes = bitsFloat(word(0x08)), vRes = bitsFloat(word(0x0c));
  if(!(hRes > 0)) hRes = bitsFloat(memory.read(4, library + 0x38));
  if(!(vRes > 0)) vRes = bitsFloat(memory.read(4, library + 0x3c));
  bool hasH = h > 0, hasV = v > 0;
  sized = hasH || hasV;
  u32 wanted[6] = {half(0x14), half(0x16), half(0x18), half(0x1a), half(0x1c), half(0x1e)};
  std::string name = text(0x20), file = text(0x60);
  bool asks = sized || !name.empty() || !file.empty();
  for(u32 n : range(6)) if(wanted[n]) asks = true;
  u32 count = fontCount(library);
  matches.assign(count, 0);
  distances.assign(count, 0);
  for(u32 index : range(count)) {
    auto& entry = systemFontTable[index];
    auto& pgf = systemFonts[index].pgf;
    u32 has[6] = {entry.family, entry.style, 0, entry.language, 0, entry.country};
    for(u32 n : range(6)) if(wanted[n] && wanted[n] == has[n]) matches[index]++;
    if(!name.empty() && pgf && name == pgf->name) matches[index]++;
    if(!file.empty() && file == entry.file) matches[index]++;
    if(sized && pgf) {
      float fontRes = pgf->resolution[0] / 64.0f, size = pgf->pointSize[0] / 64.0f;
      float asked = hasH ? h * hRes / fontRes : 0;
      if(hasV && v * vRes / (pgf->resolution[1] / 64.0f) < asked) asked = v * vRes / (pgf->resolution[1] / 64.0f);
      distances[index] = std::fabs(size - asked);
    }
    if(sized && !pgf) distances[index] = INFINITY;
  }
  return asks;
}

//(library, a style, where to put an error): the font that fits the style best (its number), as pspautotests' optimum
//recorded: of the fonts with the most matches (when any matches), the one nearest the size asked for (the first of
//those as near), else the last of them; with no match and no size, or a style asking for nothing, the first font.
//No library: 0 and INVALID_LIBID, and no style: the same (both as recorded).
auto Kernel::sceFontFindOptimumFont() -> void {
  if(!fontsInstalled()) return standInFind(arg(2));
  u32 library = arg(0), style = arg(1), errorAt = arg(2);
  auto answer = [&](u32 value, u32 error) {
    if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, error);
    result(value);
  };
  if(!library || !memory.reaches(library, 0x4c) || !style || !memory.reaches(style, StyleSize)) {
    return answer(0, FontInvalidLibrary);
  }
  std::vector<u32> matches;
  std::vector<float> distances;
  bool sized;
  if(!fontFits(library, style, matches, distances, sized) || matches.empty()) return answer(0, 0);
  u32 most = *std::max_element(matches.begin(), matches.end());
  s32 best = -1;
  for(u32 index : range(matches.size())) {
    if(matches[index] != most) continue;
    if(best < 0 || (sized ? distances[index] < distances[best] : true)) best = index;
  }
  if(!sized && !most) best = 0;
  answer(best, 0);
}

//(library, a style, where to put an error): the first font that has everything the style asks for, the size exactly;
//-1 if none does (pspautotests' find), its error 0; a style asking for nothing gives the first font. No library: 0
//and INVALID_LIBID; no style: 0 and INVALID_PARAMETER.
auto Kernel::sceFontFindFont() -> void {
  if(!fontsInstalled()) return standInFind(arg(2));
  u32 library = arg(0), style = arg(1), errorAt = arg(2);
  auto answer = [&](u32 value, u32 error) {
    if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, error);
    result(value);
  };
  if(!library || !memory.reaches(library, 0x4c)) return answer(0, FontInvalidLibrary);
  if(!style || !memory.reaches(style, StyleSize)) return answer(0, FontInvalidParameter);
  std::vector<u32> matches;
  std::vector<float> distances;
  bool sized;
  if(!fontFits(library, style, matches, distances, sized)) return answer(0, 0);
  //everything asked for: as many matches as the style gives criteria (each font's best), counted once more here
  u8 s[StyleSize];
  memory.copyOut(s, style, StyleSize);
  u32 asked = 0;
  for(u32 at = 0x14; at < 0x20; at += 2) if(s[at] | s[at + 1]) asked++;
  if(s[0x20]) asked++;
  if(s[0x60]) asked++;
  for(u32 index : range(matches.size())) {
    if(matches[index] == asked && (!sized || distances[index] == 0)) return answer(index, 0);
  }
  answer(0xffff'ffff, 0);
}

//(font handle, where to put its details): the font's FontInfo, 0x105 bytes of its 0x108 (pspautotests' fontinfo:
//its last three bytes, padding, left as they were): the header's largest measurements, as written and as floats (in
//pixels), the widest and tallest picture, how many glyphs it has, its shadows (none for a font read into memory, as
//fontinfo recorded), its style, and 4 bits a pixel. No font or no room: INVALID_PARAMETER.
auto Kernel::sceFontGetFontInfo() -> void {
  if(!fontsInstalled()) return result(ErrorNotFound);
  auto font = fontFor(arg(0));
  u32 info = arg(1);
  if(!font || !info || !memory.reaches(info, InfoSize)) return result(FontInvalidParameter);
  auto& pgf = *font->pgf;
  u8 bytes[InfoSize] = {};
  auto put32 = [&](u32 at, u32 value) { for(u32 n : range(4)) bytes[at + n] = value >> n * 8; };
  s32 values[10] = {pgf.maxSize[0], pgf.maxSize[1], pgf.maxAscender, pgf.maxDescender, pgf.maxLeftX,
                    pgf.maxBaseY, pgf.minCenterX, pgf.maxTopY, pgf.maxAdvance[0], pgf.maxAdvance[1]};
  for(u32 n : range(10)) {
    put32(n * 4, values[n]);
    put32(0x28 + n * 4, floatBits(values[n] / 64.0f));
  }
  bytes[0x50] = pgf.maxWidth; bytes[0x51] = pgf.maxWidth >> 8;
  bytes[0x52] = pgf.maxHeight; bytes[0x53] = pgf.maxHeight >> 8;
  put32(0x54, pgf.glyphCount);
  put32(0x58, font->mode == 1 || font->source == 2 ? 0 : pgf.shadowCount);
  memory.copyIn(info, bytes, InfoSize);
  fontStyle(info + 0x5c, pgf, font->source == 0 ? s32(font->index) : -1);
  memory.write(1, info + 0x104, pgf.bitsPerPixel);
  result(0);
}

//A character's FontCharInfo (60 bytes): its picture's size and place, then its measurements in 64ths of a pixel
//(pspautotests' charinfo: the dimensions; the ascender, which is the y adjustment across, and the descender, that
//less the height; the x and y adjustments across and down; the advances), its shadow's flags and number.
auto Kernel::fontCharInfo(const PGF::Glyph& glyph, u32 info) -> void {
  u32 words[14] = {glyph.width, glyph.height, u32(glyph.left), u32(glyph.top),
                   u32(glyph.dimension[0]), u32(glyph.dimension[1]), u32(glyph.bearingY[0]),
                   u32(glyph.bearingY[0] - glyph.dimension[1]), u32(glyph.bearingX[0]), u32(glyph.bearingY[0]),
                   u32(glyph.bearingX[1]), u32(glyph.bearingY[1]), u32(glyph.advance[0]), u32(glyph.advance[1])};
  for(u32 n : range(14)) memory.write(4, info + n * 4, words[n]);
  memory.write(2, info + 0x38, glyph.shadowFlags);
  memory.write(2, info + 0x3a, glyph.shadowID);
}

//The functions on one character of a font (its code, 16 bits): its details, its picture's size, the picture drawn
//(whole, or clipped), and the same of its shadow. No font, or nowhere to put the answer: INVALID_PARAMETER.
auto Kernel::fontCharacter(bool shadow, u32 what) -> void {
  if(!fontsInstalled()) return result(ErrorNotFound);
  auto font = fontFor(arg(0));
  u32 code = arg(1) & 0xffff, out = arg(2);
  u32 size = what == 0 ? CharInfoSize : what == 1 ? 4 : 0x18;
  if(!font || !out || !memory.reaches(out, size)) return result(FontInvalidParameter);
  PGF::Glyph glyph;
  fontGlyph(*font, code, glyph, shadow);
  if(what == 0) fontCharInfo(glyph, out);
  if(what == 1) {
    memory.write(2, out + 0, glyph.width);
    memory.write(2, out + 2, glyph.height);
  }
  if(what == 2) fontDraw(glyph, out, 0, 0, ~0ull, ~0ull);
  if(what == 3) fontDraw(glyph, out, s32(arg(3)), s32(arg(4)), arg(5), arg(6));
  result(0);
}

auto Kernel::sceFontGetCharInfo() -> void { fontCharacter(false, 0); }
auto Kernel::sceFontGetCharImageRect() -> void { fontCharacter(false, 1); }
auto Kernel::sceFontGetCharGlyphImage() -> void { fontCharacter(false, 2); }
auto Kernel::sceFontGetCharGlyphImage_Clip() -> void { fontCharacter(false, 3); }
auto Kernel::sceFontGetShadowInfo() -> void { fontCharacter(true, 0); }
auto Kernel::sceFontGetShadowImageRect() -> void { fontCharacter(true, 1); }
auto Kernel::sceFontGetShadowGlyphImage() -> void { fontCharacter(true, 2); }
auto Kernel::sceFontGetShadowGlyphImage_Clip() -> void { fontCharacter(true, 3); }

//(font handle): what Sony's library keeps of the glyphs it read goes; here nothing is kept. 0.
auto Kernel::sceFontFlush() -> void {
  result(0);
}

//(library, a character code): the character drawn for those a font hasn't got, its low 16 bits (pspautotests'
//altcharcode: -1 is 0xffff, 0x10000 is 0). No library: INVALID_LIBID. 0.
auto Kernel::sceFontSetAltCharacterCode() -> void {
  if(!fontsInstalled()) return result(0);
  u32 library = arg(0);
  if(!library || !memory.reaches(library, 0x4c)) return result(FontInvalidLibrary);
  memory.write(2, library + 0x48, arg(1) & 0xffff);
  result(0);
}

//(library, horizontal and vertical resolution, floats in f12 and f13): the dots an inch the library works in, kept in
//it (0x38, 0x3c). Each must be above 0 (pspautotests' resolution: -1, 0 and minus infinity refused with
//INVALID_PARAMETER, the resolution kept; infinity taken); not-a-number, which crashed the recording, is refused too.
//Without fonts installed this is the stand-in's below.
auto Kernel::sceFontSetResolution() -> void {
  if(!fontsInstalled()) return standInResolution();
  u32 library = arg(0);
  float h = bitsFloat(cpu.fpu.r[12]), v = bitsFloat(cpu.fpu.r[13]);
  if(!library || !memory.reaches(library, 0x4c)) return result(FontInvalidLibrary);
  if(!(h > 0) || !(v > 0)) return result(FontInvalidParameter);
  memory.write(4, library + 0x38, floatBits(h));
  memory.write(4, library + 0x3c, floatBits(v));
  result(0);
}

//(library, a size, a float in f12, where to put an error): points to pixels, or pixels to points, horizontally or
//vertically, at the library's resolution; a float in f0. No library: 0.0 and INVALID_LIBID (pspautotests'
//resolution, whose library done with still converts at its last resolution).
auto Kernel::fontScale(bool toPixels, u32 axis) -> void {
  float value = bitsFloat(cpu.fpu.r[12]);
  if(!fontsInstalled()) {
    if(arg(1)) memory.write(4, arg(1), 0);
    return resultFloat(toPixels ? value * fontResolution[axis] / 72.0f : value * 72.0f / fontResolution[axis]);
  }
  u32 library = arg(0), errorAt = arg(1);
  bool there = library && memory.reaches(library, 0x4c);
  if(errorAt && memory.reaches(errorAt, 4)) memory.write(4, errorAt, there ? 0 : FontInvalidLibrary);
  if(!there) return resultFloat(0.0f);
  float resolution = bitsFloat(memory.read(4, library + 0x38 + axis * 4));
  resultFloat(toPixels ? value * resolution / 72.0f : value * 72.0f / resolution);
}

auto Kernel::sceFontPointToPixelH() -> void { fontScale(true, 0); }
auto Kernel::sceFontPointToPixelV() -> void { fontScale(true, 1); }
auto Kernel::sceFontPixelToPointH() -> void { fontScale(false, 0); }
auto Kernel::sceFontPixelToPointV() -> void { fontScale(false, 1); }

//The stand-in, when the owner's fonts aren't there (no folder given, or none of the eighteen in it): the library
//starts and finds no fonts installed. sceFontNewLib gives a library (a handle nothing reads through), which lists
//none and opens none; finding or opening a font is refused with an error, as is anything only a font would answer,
//and the rest succeed. Games that print with the system's fonts then print nothing; the ones tried go on past it.
//Which error a PSP would give for a font it hasn't got can't be known (every PSP has them): NOT_FOUND (uOFW's
//errors.h) is written where the call takes an error's address, and returned where it returns one. Points and pixels
//convert at 128 dots an inch, until sceFontSetResolution changes it, for every library at once.
auto Kernel::standInNewLib() -> void {
  if(arg(1)) memory.write(4, arg(1), 0);
  result(StandInLibrary);
}

auto Kernel::standInCount(u32 errorAt) -> void {
  if(errorAt) memory.write(4, errorAt, 0);
  result(0);
}

auto Kernel::standInFind(u32 errorAt) -> void {
  if(errorAt) memory.write(4, errorAt, ErrorNotFound);
  result(0xffff'ffff);
}

auto Kernel::standInOpen(u32 errorAt) -> void {
  if(errorAt) memory.write(4, errorAt, ErrorNotFound);
  result(0);
}

//Each resolution above 0 and below 10^9 (a state holds no more); anything else, not-a-number and the infinities too,
//is refused and the resolution kept, with uOFW's INVALID_VALUE, as scePower's clocks and sceCtrl's sampling cycle
//refuse theirs.
auto Kernel::standInResolution() -> void {
  float resolution[2] = {bitsFloat(cpu.fpu.r[12]), bitsFloat(cpu.fpu.r[13])};
  for(float value : resolution) if(!(value > 0 && value < 1e9f)) return result(ErrorInvalidValue);
  fontResolution[0] = resolution[0];
  fontResolution[1] = resolution[1];
  result(0);
}
