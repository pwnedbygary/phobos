//The system's utilities (sceUtility): the dialogs games show through the system (saving and loading, messages, the
//on-screen keyboard, network settings...), and the optional modules games load through it.
//
//Dialogs. One at a time, each through four functions: InitStart (with its parameters, a structure in the program's
//memory that also takes the answer), GetStatus, Update (called every frame while it shows) and ShutdownStart. Its
//status goes 1 (starting) for a while, 2 (running: Update does its work, then) 3 (finished: the answer is in the
//parameters), and after ShutdownStart 4 (shutting down) for a while, then 0 (none); a save dialog closed from a
//thread worse than its own threads and 0x20 goes to 0 at once (dialogShutdown()). The whiles are PPSSPP's
//(200 ms to start a save dialog, 300 for a message, 2 ms to close a save dialog...). A dialog asked about while
//another kind was started last is the wrong type. As pspautotests' utility/dialog tests recorded (status, sizes,
//abort, priority): a dialog starts only while none is current, not even one shutting down (INVALID_STATUS, asked
//before the size), and only with one of its kind's parameter sizes (INVALID_PARAM_SIZE, which leaves the kind as it
//was); Update does its work only while the dialog runs (starting, or after it's gone: INVALID_STATUS); a message
//aborted while it runs finishes once 8 Updates have come since it ran (at least one after the abort; one still
//running here has had none, its first answering it), its result 0 and its button untouched. A front end that shows
//the dialogs itself (the option "Dialogs") is asked by those a PSP shows the player, which run until it answers
//(dialogs.cpp). The answers given here, with nothing drawn on the screen, to every other dialog and to all of them
//without such a front end:
//  - Saving and loading (savedata): a save is a folder on the memory stick, PSP/SAVEDATA/<game name><save name>,
//    holding its data files, the PARAM.SFO the PSP writes beside them (the save's titles, and which of its files are
//    secure: those the save modes and the secure data modes wrote) and the icons the game gave. Loading reads a data
//    file into the program's buffer and the save's titles into the parameters, or says there's no save, or no such
//    file in it; saving writes the file, the icons and PARAM.SFO; the sizes modes tell the memory stick's free space
//    (the stick io.cpp describes), what a save takes and what saving would take, the list mode what saves there are,
//    and the files mode what a save holds, its secure, normal and system files apart; deleting a save takes its
//    folder, erasing a data file the file alone (and its name from PARAM.SFO's list). Names that would lead out of
//    the save's folder, and buffers outside the program's memory, are refused. The PSP also encrypts the secure files
//    and hashes them in PARAM.SFO: not done here (the files are only Phobos's to read; the hashes are zeros). A
//    list's load takes the save its dialog would focus (that one broken, the one the same focus takes among those
//    that aren't), a list's save that names none (or a pattern, which names none there) the list's first name, the
//    name written back either way (savedataUnshown()); the list mode lists the saves its save name takes as a pattern
//    ('*', '?').
//  - A message: answered at once, as if the player pressed Yes (or OK), the message noted.
//  - The keyboard: each field's text entered as it is, an empty one given the console's nickname as the field's input
//    types allow it, each CHANGED (keyboard()).
//  - Network settings, game sharing, the web browser: cancelled, as if the player backed out.
//  - Installing a game's data to the memory stick (gamedata install): the disc's PSP_GAME/INSDIR copied into a save's
//    folder, a piece at each Update (install.cpp). Ace Combat: Joint Assault asks about it (GetStatus) at boot, and
//    goes on when it's told no install dialog was started (the wrong type, as every dialog answers).
//
//Modules: games load the system's optional libraries (sound codecs, network, ...) before using them. Their functions
//are the HLE kernel's own, so loading one just marks it loaded (a second load, or unloading one that isn't, fails as
//on the PSP).

namespace {
  //the dialogs' statuses, and kinds
  enum : u32 { DialogNone = 0, DialogStarting = 1, DialogRunning = 2, DialogFinished = 3, DialogClosing = 4 };
  enum : u32 { DialogSavedata = 1, DialogMessage, DialogKeyboard, DialogNetwork, DialogSharing, DialogBrowser,
               DialogInstall };
  //errors (PPSSPP's ErrorCodes.h, from the PSP; the wrong size from pspautotests' utility/dialog/shared.c)
  constexpr u32 UtilityInvalidStatus = 0x8011'0001, UtilityInvalidSize = 0x8011'0004, UtilityWrongType = 0x8011'0005;
  constexpr u32 UtilityBadParameterID = 0x8011'0103;
  constexpr u32 ModuleBadID = 0x8011'1101, ModuleLoaded = 0x8011'1102, ModuleNotLoaded = 0x8011'1103;
  constexpr u32 SavedataLoadNoData = 0x8011'0307, SavedataReadNoData = 0x8011'0327, SavedataSizesNoData = 0x8011'03c7;
  constexpr u32 SavedataDeleteNoData = 0x8011'0347, SavedataSaveAccess = 0x8011'0385;
  //a save whose PARAM.SFO is broken, and one without the file asked for (utility/savedata/loadbroken, deletebroken)
  constexpr u32 SavedataLoadBroken = 0x8011'0306, SavedataLoadNoFile = 0x8011'0309;
  constexpr u32 SavedataReadBroken = 0x8011'0326, SavedataReadNoFile = 0x8011'0329;
  //what the files mode leaves in bind (utility/savedata/filelist: 0x3fd), and loads (PPSSPP's notes)
  constexpr u32 SavedataBind = 1021;
  //What a PSP's encryption adds to a secure file: the 16 bytes in front of its data (PPSSPP's notes on the PSP), which
  //the sizes modes count, as the PSP stores the file. Phobos doesn't encrypt, so its secure files are as long as
  //their data.
  constexpr u32 SecureFileOverhead = 16;
  //and each group's bad parameter, 8 into it (the same table)
  constexpr u32 SavedataLoadParameter = 0x8011'0308, SavedataReadParameter = 0x8011'0328;
  constexpr u32 SavedataDeleteParameter = 0x8011'0348, SavedataSaveParameter = 0x8011'0388;
  constexpr u32 SavedataSizesParameter = 0x8011'03c8;
  constexpr u32 DialogCancelled = 1;  //a dialog's result when the player backs out
  constexpr u32 KeyboardChanged = 2;  //a keyboard field's result when the player enters its text
  //the keyboard's fields (SceUtilityOskData), and bounds of ours on them (pspsdk gives no limits)
  constexpr u32 KeyboardFieldSize = 52, KeyboardFields = 16, KeyboardMostText = 1024;
  constexpr u32 MessageAbortUpdates = 8;  //(utility/dialog/abort: that many Updates from the start, less any already)
  constexpr u32 SavedataClosePriority = 0x20;  //a save dialog's last closing step's (utility/savedata/shutdownstatus)
  //Whether InitStart takes a kind's parameters at this size (their common part's first word): the sizes
  //utility/dialog/sizes skipped as the ones each kind takes (every other size up to 0x800 refused), and those
  //utility/dialog/htmlviewer found the web browser takes.
  auto dialogSize(u32 kind, u32 size) -> bool {
    auto any = [&](std::initializer_list<u32> sizes) {
      return std::find(sizes.begin(), sizes.end(), size) != sizes.end();
    };
    switch(kind) {
    case DialogSavedata: return any({0x5c8, 0x5dc, 0x600});
    case DialogMessage:  return any({0x23c, 0x244, 0x2c4});
    case DialogKeyboard: return any({0x40, 0x44});
    case DialogNetwork:  return any({0x38, 0x40, 0x44});
    case DialogSharing:  return any({0x50, 0x54, 0x64});
    case DialogBrowser:  return any({0x70, 0x78, 0x80, 0x98, 0xa4, 0xa8});
    case DialogInstall:  return any({0x590, 0x598});
    }
    return false;
  }
  //how long a dialog takes to start and to close, in microseconds (PPSSPP's)
  auto dialogTimes(u32 kind, u32& start, u32& close) -> void {
    start = kind == DialogMessage || kind == DialogKeyboard ? 300'000 : 200'000;
    close = kind == DialogSavedata ? 2'000 : kind == DialogMessage ? 26'000 : 40'000;
    if(kind == DialogNetwork) close = 200'000;
  }
  //What a savedata mode says of parameters it can't take: the bad parameter of the group its other errors come from.
  auto savedataRefusal(u32 mode) -> u32 {
    switch(mode) {
    case 0: case 2: case 4: return SavedataLoadParameter;
    case 1: case 3: case 5: case 13: case 14: case 17: case 18: return SavedataSaveParameter;
    case 6: case 7: case 9: case 10: case 21: return SavedataDeleteParameter;
    case 8: return SavedataSizesParameter;
    default: return SavedataReadParameter;  //reading, erasing (deletebroken), and the list, files and size modes
    }
  }

  //Whether a name a program gives a save (its game's, its own, its data file's) is one plain name, ending inside its
  //field with room for its NUL: not empty, not "." or "..", with no '/', '\' or ':' (separators, or a drive, to some
  //hosts) and no control character. Each is joined to a folder on the host, where a path would lead elsewhere.
  auto plainName(const std::string& name, u32 field) -> bool {
    if(name.empty() || name.size() >= field || name == "." || name == "..") return false;
    if(name.find_first_of("/\\:") != std::string::npos) return false;
    return std::none_of(name.begin(), name.end(), [](char c) { return u8(c) < 0x20; });
  }

  //Whether a save's name is a pattern, as the list mode takes its save's name ('*' for any run of characters, '?' for
  //any one: utility/savedata/idlist's "A?C" listed its one save, ABC). It names no save a memory stick can hold: FAT
  //takes neither character in a name (the same test's open of a file in ms0:/PSP/SAVEDATA/TEST99901A?C/ was
  //0x80010016, invalid, where one not there is 0x80010002).
  auto savePattern(const std::string& name) -> bool {
    return name.find_first_of("*?") != std::string::npos;
  }

  //Whether a pattern (savePattern()'s) takes a name whole: '*' any run of its characters (none too), '?' any one, any
  //other character itself, as it's given (chosen: a FAT stick keeps a name's case, and the folders a game lists are
  //its own, named as it names them).
  auto patternTakes(const std::string& pattern, const std::string& name) -> bool {
    u64 at = 0, from = 0, star = std::string::npos, resume = 0;
    while(from < name.size()) {
      if(at < pattern.size() && pattern[at] == '*') {
        star = at++;
        resume = from;
      } else if(at < pattern.size() && (pattern[at] == '?' || pattern[at] == name[from])) {
        at++, from++;
      } else if(star != std::string::npos) {  //(the last '*' takes one more character, and the rest is tried again)
        at = star + 1;
        from = ++resume;
      } else {
        return false;
      }
    }
    while(at < pattern.size() && pattern[at] == '*') at++;
    return at == pattern.size();
  }

  //A keyboard field's text in the program's memory (UTF-16, ending in a NUL): as far as it goes there, and no more
  //than KeyboardMostText characters; none from address 0.
  auto keyboardText(Memory& memory, u32 address) -> std::vector<u16> {
    std::vector<u16> text;
    for(u32 at = address; address && text.size() < KeyboardMostText && memory.reaches(at, 2); at += 2) {
      u16 character = memory.read(2, at);
      if(!character) break;
      text.push_back(character);
    }
    return text;
  }

  //The keyboard's input types (psputility_osk.h's PSP_UTILITY_OSK_INPUTTYPE_*, a field's at 16 in SceUtilityOskData,
  //0 for all): which a character is typed in, by what it is. The Latin letters are ASCII's and U+00C0-024F's (Latin-1's
  //and Latin Extended-A's and -B's, the accented letters), lower or upper by case, a letter that's neither (title
  //case, or none) upper. Full-width letters, digits and signs (U+FF01-FF5E, and U+3000-303F's spaces and marks) are
  //the Japanese ones, and so are the kana's own marks (the voicing and iteration marks, the middle dot, the long vowel
  //mark: kana too) and every other character that's printed and classed nowhere else, JIS X 0208's other symbols
  //among them (stars, notes, arrows, the reference mark, the ellipsis, the full-width yen, Greek, box drawing). The URL
  //keyboard types every printable ASCII character. The app classes them alike (android/.../util/PspDialogs.kt), both
  //checked against the same cases (psp-keyboard-cases.txt, beside the app's tests).
  enum : u32 {
    InputLatinDigit = 0x1, InputLatinSymbol = 0x2, InputLatinLower = 0x4, InputLatinUpper = 0x8,
    InputJapaneseDigit = 0x100, InputJapaneseSymbol = 0x200, InputJapaneseLower = 0x400, InputJapaneseUpper = 0x800,
    InputHiragana = 0x1000, InputHalfKatakana = 0x2000, InputKatakana = 0x4000, InputKanji = 0x8000,
    InputRussianLower = 0x1'0000, InputRussianUpper = 0x2'0000, InputKorean = 0x4'0000, InputURL = 0x8'0000,
    InputKnown = 0xf'ff0f, InputDigits = InputLatinDigit | InputJapaneseDigit,
    InputHalfWidth = InputLatinDigit | InputLatinSymbol | InputLatinLower | InputLatinUpper | InputURL,
    InputFullWidth = InputJapaneseDigit | InputJapaneseSymbol | InputJapaneseLower | InputJapaneseUpper
                   | InputHiragana | InputKatakana | InputKanji,
  };
  //U+00C0-024F, a character each: l a lower case letter, U any other letter, - not a letter (× and ÷), by Unicode's
  //general categories
  constexpr char KeyboardLatin[] =
    "UUUUUUUUUUUUUUUUUUUUUUU-UUUUUUUl"  //U+00C0
    "lllllllllllllllllllllll-llllllll"  //U+00E0
    "UlUlUlUlUlUlUlUlUlUlUlUlUlUlUlUl"  //U+0100
    "UlUlUlUlUlUlUlUlUlUlUlUllUlUlUlU"  //U+0120
    "lUlUlUlUllUlUlUlUlUlUlUlUlUlUlUl"  //U+0140
    "UlUlUlUlUlUlUlUlUlUlUlUlUUlUlUll"  //U+0160
    "lUUlUlUUlUUUllUUUUlUUlUUUlllUUlU"  //U+0180
    "UlUlUlUUlUllUlUUlUUUlUlUUllUUlll"  //U+01A0
    "UUUUUUlUUlUUlUlUlUlUlUlUlUlUllUl"  //U+01C0
    "UlUlUlUlUlUlUlUllUUlUlUUUlUlUlUl"  //U+01E0
    "UlUlUlUlUlUlUlUlUlUlUlUlUlUlUlUl"  //U+0200
    "UlUlUlUlUlUlUlUlUlUlllllllUUlUUl"  //U+0220
    "lUlUUUUlUlUlUlUl";                 //U+0240

  //Whether a character is printed: not a control character, half of a pair (a character past U+FFFF, which the PSP
  //hasn't got), a combining mark, an invisible format character, a variation selector, private use, a special or a
  //noncharacter.
  auto keyboardPrinted(u16 c) -> bool {
    if(c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0xad) return false;
    for(auto [first, last] : {std::pair<u16, u16>{0x0300, 0x036f}, {0x200b, 0x200f}, {0x2028, 0x202e},
                              {0x2060, 0x206f}, {0x20d0, 0x20ff}, {0xd800, 0xf8ff}, {0xfdd0, 0xfdef},
                              {0xfe00, 0xfe0f}, {0xfe20, 0xfe2f}, {0xfeff, 0xfeff}, {0xfff0, 0xffff}}) {
      if(c >= first && c <= last) return false;
    }
    return true;
  }

  auto keyboardTypes(u16 c) -> u32 {
    if(c >= '0' && c <= '9') return InputLatinDigit | InputURL;
    if(c >= 'a' && c <= 'z') return InputLatinLower | InputURL;
    if(c >= 'A' && c <= 'Z') return InputLatinUpper | InputURL;
    if(c >= 0x20 && c <= 0x7e) return InputLatinSymbol | InputURL;
    if(c >= 0xc0 && c <= 0x24f && KeyboardLatin[c - 0xc0] != '-') {
      return KeyboardLatin[c - 0xc0] == 'l' ? InputLatinLower : InputLatinUpper;
    }
    if(c >= 0xff10 && c <= 0xff19) return InputJapaneseDigit;
    if(c >= 0xff41 && c <= 0xff5a) return InputJapaneseLower;
    if(c >= 0xff21 && c <= 0xff3a) return InputJapaneseUpper;
    if((c >= 0xff01 && c <= 0xff5e) || (c >= 0x3000 && c <= 0x303f)) return InputJapaneseSymbol;
    if(c >= 0x309b && c <= 0x309e) return InputHiragana | InputJapaneseSymbol;  //(voicing, iteration marks)
    if(c == 0x30fc) return InputHiragana | InputKatakana | InputJapaneseSymbol;  //(the long vowel mark)
    if(c == 0x30fb || c == 0x30fd || c == 0x30fe) return InputKatakana | InputJapaneseSymbol;  //(middle dot, iteration)
    if(c >= 0x3041 && c <= 0x309f) return InputHiragana;
    if(c >= 0x30a0 && c <= 0x30ff) return InputKatakana;
    if(c >= 0xff61 && c <= 0xff9f) return InputHalfKatakana;
    if((c >= 0x4e00 && c <= 0x9fff) || (c >= 0x3400 && c <= 0x4dbf)) return InputKanji;
    if((c >= 0x0430 && c <= 0x044f) || c == 0x0451) return InputRussianLower;
    if((c >= 0x0410 && c <= 0x042f) || c == 0x0401) return InputRussianUpper;
    if((c >= 0xac00 && c <= 0xd7a3) || (c >= 0x1100 && c <= 0x11ff) || (c >= 0x3130 && c <= 0x318f)) return InputKorean;
    if(keyboardPrinted(c)) return InputJapaneseSymbol;
    return 0;
  }

  //The console's nickname for an empty field, "PSP", or where the field can't take it a name it can: "pi-esu-pi" in
  //hiragana (katakana too, by keyboardConform()) and in half-width katakana, the Cyrillic and Hangul for "PSP", or
  //digits alone.
  const std::u16string KeyboardNicknames[] = {
    u"PSP", u"\u3074\u30fc\u3048\u3059\u3074\u30fc", u"\uff8b\uff9f\uff70\uff74\uff7d\uff8b\uff9f\uff70",
    u"\u041f\u0421\u041f", u"\ud53c\uc5d0\uc2a4\ud53c", u"1",
  };

  //A save's PARAM.SFO, in the disc's format (disc-info.cpp's sfoValue()): a head ("\0PSF", version 1.1, where the
  //keys and the values start, how many there are), 16 bytes for each value (where its key is among the keys, its
  //format, its length and its room, where it is among the values), the keys (each ending in a NUL, padded together
  //to 4 bytes), then each value in its room. Its values here by key, in the keys' order, as the PSP writes them: text
  //(format 0x0204, ending in a NUL), bytes (0x0004) or a number (0x0404). A save's are CATEGORY ("MS": a memory
  //stick's save), PARENTAL_LEVEL, SAVEDATA_DETAIL, SAVEDATA_DIRECTORY (its folder's name), SAVEDATA_FILE_LIST (its
  //secure files: 99 entries of 32 bytes, each a name in 13 and the encrypted file's hash in 16), SAVEDATA_PARAMS (128
  //bytes: how the files were encrypted, and hashes of the SFO), SAVEDATA_TITLE and TITLE.
  struct SaveSFO {
    struct Value { u16 format = 0; u32 room = 0; std::vector<u8> bytes; };
    std::map<std::string, Value> values;
    static constexpr u32 ListEntries = 99, ListEntry = 32, ListName = 13;

    //The file at path (savePath()'s, or none): whether one is there. One that isn't an SFO (not its head; a key, a
    //value or its room outside the file; more room all together than the values' part of it holds; more than 64 KiB)
    //is broken, and none of it is kept.
    auto read(const std::string& path, bool& broken) -> bool {
      namespace fs = std::filesystem;
      std::error_code error;
      values.clear();
      broken = false;
      if(path.empty() || !fs::is_regular_file(path, error)) return false;
      broken = true;
      u64 size = fs::file_size(path, error);
      if(error || size < 20 || size > 64_KiB) return true;
      std::vector<u8> sfo(size);
      if(!std::ifstream(path, std::ios::binary).read((char*)sfo.data(), size)) return true;
      auto word = [&](u64 at) -> u32 {
        return sfo[at] | sfo[at + 1] << 8 | sfo[at + 2] << 16 | (u32)sfo[at + 3] << 24;
      };
      if(std::memcmp(sfo.data(), "\0PSF", 4)) return true;
      u64 keys = word(8), start = word(12), count = word(16), rooms = 0;
      if(count > (size - 20) / 16 || start > size) return true;
      for(u64 entry = 20; entry < 20 + count * 16; entry += 16) {
        u64 key = keys + (sfo[entry] | sfo[entry + 1] << 8), at = start + word(entry + 12), length = word(entry + 4);
        u64 room = std::max<u64>(word(entry + 8), length);
        auto end = key < size ? std::find(sfo.begin() + key, sfo.end(), 0) : sfo.end();
        rooms += room;
        if(end == sfo.end() || at > size || room > size - at || rooms > size - start) return values.clear(), true;
        auto& value = values[std::string(sfo.begin() + key, end)];
        value.format = sfo[entry + 2] | sfo[entry + 3] << 8;
        value.room = room;
        value.bytes.assign(sfo.begin() + at, sfo.begin() + at + length);
      }
      broken = false;
      return true;
    }

    //How long it is written (no more than 64 KiB is).
    auto size() const -> u64 {
      u64 keysSize = 0, valuesSize = 0;
      for(auto& [key, value] : values) keysSize += key.size() + 1, valuesSize += value.room;
      return 20 + values.size() * 16 + ((keysSize + 3) & ~3ull) + valuesSize;
    }

    //(written through a file beside it, renamed over it: a save cut off while writing leaves the last PARAM.SFO)
    auto write(const std::string& path) const -> bool {
      if(path.empty() || size() > 64_KiB) return false;
      u64 keys = 20 + values.size() * 16, keysSize = 0, valuesSize = 0;
      for(auto& [key, value] : values) keysSize += key.size() + 1, valuesSize += value.room;
      keysSize = (keysSize + 3) & ~3ull;
      std::vector<u8> sfo(keys + keysSize + valuesSize);
      auto put = [&](u64 at, u64 number, u32 bytes) { while(bytes--) sfo[at++] = number, number >>= 8; };
      std::memcpy(sfo.data(), "\0PSF", 4);
      put(4, 0x0101, 4), put(8, keys, 4), put(12, keys + keysSize, 4), put(16, values.size(), 4);
      u64 entry = 20, key = 0, at = 0;
      for(auto& [name, value] : values) {
        put(entry, key, 2), put(entry + 2, value.format, 2), put(entry + 4, value.bytes.size(), 4);
        put(entry + 8, value.room, 4), put(entry + 12, at, 4);
        std::copy(name.begin(), name.end(), sfo.begin() + keys + key);
        std::copy(value.bytes.begin(), value.bytes.end(), sfo.begin() + keys + keysSize + at);
        entry += 16, key += name.size() + 1, at += value.room;
      }
      std::string partial = path + ".new";
      std::error_code removed, renamed;
      std::filesystem::remove(partial, removed);  //(a link there is taken away, not written through)
      std::ofstream stream(partial, std::ios::binary | std::ios::trunc);
      stream.write((const char*)sfo.data(), sfo.size());
      stream.close();
      if(stream) std::filesystem::rename(partial, path, renamed);
      if(!stream || renamed) std::filesystem::remove(partial, removed);
      return stream && !renamed;
    }

    auto text(const std::string& key) const -> std::string {
      auto found = values.find(key);
      if(found == values.end()) return {};
      auto& bytes = found->second.bytes;
      return {bytes.begin(), std::find(bytes.begin(), bytes.end(), 0)};
    }

    auto number(const std::string& key) const -> u32 {
      auto found = values.find(key);
      if(found == values.end() || found->second.bytes.size() < 4) return 0;
      auto& bytes = found->second.bytes;
      return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | (u32)bytes[3] << 24;
    }

    //(text cut to its room, its NUL among it)
    auto setText(const std::string& key, std::string text, u32 room) -> void {
      text.resize(std::min<u64>(text.size(), room - 1));
      values[key] = {0x0204, room, {text.begin(), text.end()}};
      values[key].bytes.push_back(0);
    }

    auto setNumber(const std::string& key, u32 number) -> void {
      values[key] = {0x0404, 4, {u8(number), u8(number >> 8), u8(number >> 16), u8(number >> 24)}};
    }

    auto setBytes(const std::string& key, std::vector<u8> bytes) -> void {
      u32 room = bytes.size();
      values[key] = {0x0004, room, std::move(bytes)};
    }

    //SAVEDATA_FILE_LIST, made empty if there's none (every PSP save has one)
    auto fileList() -> std::vector<u8>& {
      auto& value = values["SAVEDATA_FILE_LIST"];
      if(value.bytes.size() < ListEntries * ListEntry) value.bytes.resize(ListEntries * ListEntry);
      value.format = 0x0004;
      value.room = std::max<u32>(value.room, value.bytes.size());
      return value.bytes;
    }

    //The names SAVEDATA_FILE_LIST holds: the save's secure files.
    auto secure() const -> std::vector<std::string> {
      std::vector<std::string> names;
      auto found = values.find("SAVEDATA_FILE_LIST");
      if(found == values.end()) return names;
      auto& list = found->second.bytes;
      for(u64 at = 0; at + ListEntry <= list.size(); at += ListEntry) {
        auto entry = list.begin() + at;
        std::string name(entry, std::find(entry, entry + ListName, 0));
        if(!name.empty()) names.push_back(name);
      }
      return names;
    }

    //Names a file secure in SAVEDATA_FILE_LIST (its hash left as zeros), or takes its name out; whether the list
    //changed. A full list takes no more (99 secure files, the PSP's most).
    auto listed(const std::string& name, bool secure) -> bool {
      auto& list = fileList();
      u64 empty = list.size();
      for(u64 at = 0; at + ListEntry <= list.size(); at += ListEntry) {
        auto entry = list.begin() + at;
        std::string held(entry, std::find(entry, entry + ListName, 0));
        if(held == name) {
          if(secure) return false;
          std::fill(entry, entry + ListEntry, 0);
          return true;
        }
        if(held.empty() && empty == list.size()) empty = at;
      }
      if(!secure || empty == list.size()) return false;
      std::fill(list.begin() + empty, list.begin() + empty + ListEntry, 0);
      std::copy(name.begin(), name.begin() + std::min<u64>(name.size(), ListName - 1), list.begin() + empty);
      return true;
    }
  };
}

//A pending status change that's due happens.
auto Kernel::dialogDue() -> void {
  if(dialog.changeAt && cycles >= dialog.changeAt) {
    dialog.status = dialog.next;
    dialog.changeAt = 0;
  }
}

//(kind, parameters): starts a dialog, unless one is current (starting, running, finished or shutting down), or its
//parameters aren't one of the sizes its kind takes.
auto Kernel::dialogStart(u32 kind) -> void {
  dialogDue();
  if(dialog.status != DialogNone) return result(UtilityInvalidStatus);
  if(!memory.reaches(arg(0), 48)) return result(ErrorInvalidPointer);
  if(!dialogSize(kind, memory.read(4, arg(0)))) return result(UtilityInvalidSize);
  u32 start, close;
  dialogTimes(kind, start, close);
  dialog.kind = kind;
  dialog.parameters = arg(0);
  dialog.status = DialogStarting;
  dialog.next = DialogRunning;
  dialog.changeAt = cycles + u64(start) * (CPUFrequency / 1'000'000);
  dialog.abortUpdates = 0;
  dialog.runningUpdates = 0;
  dialog.error = 0;
  dialog.deleted = false;
  result(0);
}

auto Kernel::dialogStatus(u32 kind) -> void {
  if(dialog.kind != kind) return result(UtilityWrongType);
  dialogDue();
  result(dialog.status);
}

//Called every frame while it shows: running, it gives its answer and finishes (an aborted message counts its Updates
//down first), or asks a front end that shows it and waits for its answer (dialogs.cpp). Finished, there's nothing
//more to do.
auto Kernel::dialogUpdate(u32 kind) -> void {
  if(dialog.kind != kind) return result(UtilityWrongType);
  dialogDue();
  if(dialog.status != DialogRunning && dialog.status != DialogFinished) return result(UtilityInvalidStatus);
  if(dialog.status == DialogRunning && !dialog.abortUpdates) dialog.runningUpdates++;
  if(dialog.status == DialogRunning && kind == DialogInstall) {
    installUpdate();
  } else if(dialog.status == DialogRunning && dialog.abortUpdates) {
    if(--dialog.abortUpdates == 0) {
      memory.write(4, dialog.parameters + 28, dialog.error);
      dialog.status = DialogFinished;
    }
  } else if(dialog.status == DialogRunning && frontDialogs && (dialogShownID() || dialogAsks(kind))) {
    dialogAsk();
  } else if(dialog.status == DialogRunning) {
    u32 answer = DialogCancelled;
    if(kind == DialogSavedata) answer = savedataUnshown(dialog.parameters);
    if(kind == DialogMessage) {
      std::string text = memory.readString(dialog.parameters + 60, 512);
      note("message dialog (answered yes): " + (text.empty() ? "error " + hexWord(memory.read(4, dialog.parameters
           + 56)) : text));
      memory.write(4, dialog.parameters + 576, 1);  //the button pressed: Yes (or OK)
      answer = 0;
    }
    if(kind == DialogKeyboard) answer = keyboard(dialog.parameters);
    memory.write(4, dialog.parameters + 28, answer);  //the common part's result
    dialog.status = DialogFinished;
  }
  result(0);
}

//Closes a finished dialog. A save dialog closes in the utility's own threads, last at priority 0x20, without waiting:
//a caller worse than them has no dialog by the time it runs again. pspautotests' utility/savedata/shutdownstatus
//recorded callers at 33 to 36 and 111 reading NONE at their first look after ShutdownStart, and one at 32 SHUTDOWN,
//then NONE (the dialog's graphics and access threads at 17 and 19; its font and sound threads' priorities made no
//difference). Before that last step the threads run at the graphics and access threads' priorities its parameters
//give (the common part's words at 12 and 16), PPSSPP's description has it, so a caller better than either sees it
//closing too. A caller worse than all three finds the dialog closed at once here, the closing's time taken by the
//utility's threads, not counted; a priority no thread can have (outside 1 to 0x7f) holds no caller off. Crash Tag
//Team Racing's main thread, at 0x3f, closes its save dialog and starts the next one at once: refused as still
//closing, it waited for that dialog for good. A caller the utility's threads can't take the CPU from (interrupts or
//dispatching held off, a call into the program) or none sees the dialog closing for its while, and the other dialogs
//close as before (utility/dialog/priority: a message closed from 111 still read SHUTDOWN).
auto Kernel::dialogShutdown(u32 kind) -> void {
  if(dialog.kind != kind) return result(UtilityWrongType);
  dialogDue();
  if(dialog.status != DialogFinished) return result(UtilityInvalidStatus);
  result(0);
  auto holds = [&](u32 offset) {  //a thread's priority from the parameters, or 0
    u32 priority = memory.read(4, dialog.parameters + offset);
    return priority >= 0x01 && priority <= 0x7f ? priority : 0;
  };
  if(kind == DialogSavedata && current && !interrupting && !dispatchSuspended && interruptsEnabled
  && current->priority > std::max({holds(12), holds(16), SavedataClosePriority})) {
    dialog.status = DialogNone;
    dialog.changeAt = 0;
    return;
  }
  u32 start, close;
  dialogTimes(kind, start, close);
  dialog.status = DialogClosing;
  dialog.next = DialogNone;
  dialog.changeAt = cycles + u64(close) * (CPUFrequency / 1'000'000);
}

//Text as a field of input types `input` and that many lines takes it, as typed on a PSP's keyboard, which has only
//those characters: each character it doesn't take becomes one it does where there's one (the other width of an
//ASCII letter, digit or sign, the other case of an ASCII, full-width or Russian letter, the other kana), and is left
//out where there isn't. A space, which every keyboard but the digits' has, is full-width (U+3000) where the field
//takes full-width characters and no half-width ones; a line break stays in a field of several lines, whatever its
//types, and no other control character anywhere, nor half of a pair (a character past U+FFFF, which the PSP hasn't
//got). Half-width katakana are taken only as they come (no other kana becomes one). A field taking all (0, or none
//of the types known) takes everything else. Phobos's choice, as the app's keyboard enters text
//(android/.../util/PspDialogs.kt's accept(), checked against the same cases): the game's own text is kept as it is,
//by the automatic answer (keyboard()) and where a front end answers with it unedited (dialogs.cpp); any other answer
//is typed so.
auto Kernel::keyboardConform(const std::vector<u16>& text, u32 input, u32 lines) -> std::vector<u16> {
  std::vector<u16> conformed;
  input &= InputKnown;
  if(!input) {
    for(u16 c : text) {
      bool kept = c == '\n' ? lines > 1 : c >= 0x20 && !(c >= 0x7f && c <= 0x9f) && !(c >= 0xd800 && c <= 0xdfff);
      if(kept) conformed.push_back(c);
    }
    return conformed;
  }
  auto takes = [&](u16 c) { return (keyboardTypes(c) & input) != 0; };
  auto width = [](u16 c) -> u16 {
    if(c >= 0x21 && c <= 0x7e) return c + 0xfee0;
    if(c >= 0xff01 && c <= 0xff5e) return c - 0xfee0;
    return c;
  };
  auto other = [](u16 c) -> u16 {  //the other case, or kana
    if((c >= 'a' && c <= 'z') || (c >= 0xff41 && c <= 0xff5a) || (c >= 0x0430 && c <= 0x044f)) return c - 0x20;
    if((c >= 'A' && c <= 'Z') || (c >= 0xff21 && c <= 0xff3a) || (c >= 0x0410 && c <= 0x042f)) return c + 0x20;
    if(c == 0x0451) return 0x0401;
    if(c == 0x0401) return 0x0451;
    if(c >= 0x3041 && c <= 0x3096) return c + 0x60;
    if(c >= 0x30a1 && c <= 0x30f6) return c - 0x60;
    return c;
  };
  for(u16 c : text) {
    if(c == '\n') {
      if(lines > 1) conformed.push_back(c);
      continue;
    }
    if(c == ' ' || c == 0x3000) {
      if(!(input & ~InputDigits)) continue;
      bool full = (input & InputFullWidth) && !(input & InputHalfWidth);
      conformed.push_back(full ? 0x3000 : ' ');
      continue;
    }
    for(u16 candidate : {c, width(c), other(c), width(other(c))}) {
      if(takes(candidate)) { conformed.push_back(candidate); break; }
    }
  }
  return conformed;
}

//The keyboard (psputility_osk.h's SceUtilityOskParams: the common part, then how many fields and where their
//SceUtilityOskData are), answered as a player entering each field's text and confirming it would: what the field
//started with goes into its output, as it is, or for a field holding nothing but spaces (ASCII's or full-width ones)
//the console's nickname, "PSP", as a player asked for a name would type one (Peace Walker, asking for its player's
//name in an empty field, refused an empty answer, "at least 1 characters", and asked again for good). The nickname is
//typed as the field's input types allow (keyboardConform()): 7th Dragon 2020 and 2020-II take only full-width
//letters (0x0c00) and get it full-width (U+FF30 U+FF33 U+FF30), where ASCII "PSP" left the first's hero nameless. A
//field taking none of its letters gets the first of KeyboardNicknames it takes whole (kana alone: "pi-esu-pi" in kana,
//digits alone: "1"); one taking none of those whole (symbols alone, kanji alone) keeps its own text. Each field's
//result is CHANGED (2), as a PSP is taken to say of every field the player confirms, edited or not: Ridge Racer 2
//fills its field with a default driver name, "DAMACY", and takes UNCHANGED (0) as it takes CANCELLED (1), going back
//to its title without a profile, which every player keeping that name would meet if a PSP said UNCHANGED. (Killzone
//and Space Invaders Extreme also act only on CHANGED; Midnight Club 3 takes anything but CANCELLED.) The text is
//UTF-16, as far as the field's room (outtextlength, its NUL among it) and limit (outtextlimit) allow: a limit of 0 is
//none (taken as no characters, it cut the nickname to nothing), and a field with no room gets nothing written, not
//even its NUL. Returns the common part's result: 0.
auto Kernel::keyboard(u32 parameters) -> u32 {
  u32 count = memory.read(4, parameters + 48), fields = memory.read(4, parameters + 52);
  for(u32 n = 0; n < std::min(count, KeyboardFields); n++) {
    u32 field = fields + n * KeyboardFieldSize;
    if(!memory.reaches(field, KeyboardFieldSize)) break;
    std::vector<u16> text = keyboardText(memory, memory.read(4, field + 32));
    bool blank = std::all_of(text.begin(), text.end(), [](u16 c) { return c == ' ' || c == 0x3000; });
    for(auto& nickname : KeyboardNicknames) {
      if(!blank) break;
      auto typed = keyboardConform({nickname.begin(), nickname.end()}, memory.read(4, field + 16), 1);
      if(typed.size() == nickname.size()) text = typed, blank = false;
    }
    keyboardField(field, text);
    memory.write(4, field + 44, KeyboardChanged);
  }
  return 0;
}

//A field's text into its output (SceUtilityOskData's outtext), as far as its room (outtextlength, the NUL among it)
//and its limit (outtextlimit, 0 for none) allow, a pair (a game's own text may hold one) not cut in two; nothing
//written where it has no room.
auto Kernel::keyboardField(u32 field, std::vector<u16> text) -> void {
  u32 room = memory.read(4, field + 36), output = memory.read(4, field + 40), limit = memory.read(4, field + 48);
  u64 most = room ? room - 1 : 0;
  if(limit) most = std::min<u64>(most, limit);
  if(most && most < text.size() && text[most - 1] >= 0xd800 && text[most - 1] <= 0xdbff) most--;
  text.resize(std::min<u64>(text.size(), most));
  if(room && output && memory.reaches(output, (text.size() + 1) * 2)) {
    for(u32 at = 0; at < text.size(); at++) memory.write(2, output + at * 2, text[at]);
    memory.write(2, output + text.size() * 2, 0);
  }
}

//A word as eight hexadecimal digits.
auto Kernel::hexWord(u32 value) -> std::string {
  char text[12];
  std::snprintf(text, sizeof(text), "%08x", value);
  return text;
}

//Where a save is on the host: its folder, ms0:/PSP/SAVEDATA/<folder> (the game's name and the save's), or given a
//data file's name, that file in it; nothing if there's no memory stick. resolve() keeps a path inside the memory
//stick; a save's must also be where a save belongs, links followed: the folder one of SAVEDATA's own (never SAVEDATA
//itself, nor anything above it), the file one of the folder's own. A path leading anywhere else is nothing too.
auto Kernel::savePath(const std::string& folder, const std::string& file) -> std::string {
  namespace fs = std::filesystem;
  std::string saves, host, normalized;
  if(resolve("ms0:/PSP/SAVEDATA", saves, normalized)) return {};
  if(resolve("ms0:/PSP/SAVEDATA/" + folder, host, normalized) || fs::path(host).parent_path() != saves) return {};
  if(file.empty()) return host;
  std::string inside = host;
  if(resolve("ms0:/PSP/SAVEDATA/" + folder + "/" + file, host, normalized)) return {};
  return fs::path(host).parent_path() == inside ? host : std::string{};
}

//Does what a savedata request asks (savedataMode()), traced for a host that traces them (savedataTrace): the request
//as the game made it, with the save chosen from its list where one was (by a front end's player or the automatic
//answer), then its answer. A chosen save's name is written into the request's save name first, as a PSP's dialog
//writes it (PPSSPP's notes: loads and saves from a list copy it back).
auto Kernel::savedata(u32 p, const std::string* chosen) -> u32 {
  std::string request = savedataTrace ? savedataRequest(p, chosen) : std::string{};
  if(chosen) {
    memory.fill(p + 76, 0, 20);
    memory.copyIn(p + 76, chosen->data(), std::min<u64>(chosen->size(), 19));
  }
  u32 answer = savedataMode(p, chosen);
  if(savedataTrace) savedataTrace(request + " -> " + savedataAnswer(p, answer));
  return answer;
}

namespace {
  //A name as the trace quotes it: bytes outside printable ASCII, the quote and the backslash as \xNN.
  auto traceName(const std::string& text) -> std::string {
    std::string out = "\"";
    for(char c : text) {
      if(u8(c) >= 0x20 && u8(c) < 0x7f && c != '"' && c != '\\') { out += c; continue; }
      char escaped[8];
      std::snprintf(escaped, sizeof(escaped), "\\x%02x", u8(c));
      out += escaped;
    }
    return out + "\"";
  }
}

//A savedata request as the trace gives it: its mode (pspsdk's name), the structure's size, the game's and the save's
//names (as the game gave them), a list's names (8 of them, and how many in all), a list dialog's focus (in the sizes
//that have it) and the save chosen from the list (by a front end's player or the automatic answer), the data file's
//name, the buffer, its size and the data's, and whether the save it names (with none named, in a list's load or save
//from a list a pattern naming none, and none chosen, a list's first, as savedataMode() takes it) is on the memory
//stick; the sizes mode's msData, the list mode's most, the files mode's most of each kind, the size mode's files; a
//save mode's title.
auto Kernel::savedataRequest(u32 p, const std::string* chosen) -> std::string {
  //(the parameters' words where they're in memory: a read past it is noted and counted in the machine's state)
  auto word = [&](u32 at) -> u32 { return memory.reaches(p + at, 4) ? memory.read(4, p + at) : 0; };
  namespace fs = std::filesystem;
  static constexpr const char* Modes[] = {"autoload", "autosave", "load", "save", "listload", "listsave",
    "listdelete", "listalldelete", "sizes", "autodelete", "delete", "list", "files", "makedatasecure", "makedata",
    "readdatasecure", "readdata", "writedatasecure", "writedata", "erasesecure", "erase", "deletedata", "getsize"};
  u32 size = word(0), mode = word(48), names = word(96);
  std::string game = memory.readString(p + 60, 13), save = memory.readString(p + 76, 20);
  std::string text = "mode " + std::to_string(mode);
  if(mode < std::size(Modes)) text += " (" + std::string(Modes[mode]) + ")";
  text += ", size " + std::to_string(size) + ", game " + traceName(game) + ", save " + traceName(save);
  std::string used = save == "<>" ? std::string{} : save, first;
  bool listed = (mode == 4 || mode == 5) && names && !memory.readString(names, 20).empty();  //(as savedataMode())
  if(listed && savePattern(used)) used.clear();
  if(names && mode >= 4 && mode <= 6) {
    u32 count = 0;
    text += ", list";
    for(; count < 1024 && memory.reaches(names + count * 20, 20); count++) {  //(as far as a save dialog reads)
      std::string name = memory.readString(names + count * 20, 20);
      if(name.empty()) break;
      if(!count) first = name == "<>" ? std::string{} : name;
      if(count < 8) text += " " + traceName(name);
    }
    text += count ? count > 8 ? " (" + std::to_string(count) + " in all)" : std::string{} : std::string{" empty"};
  }
  if(mode >= 4 && mode <= 7 && size > 1480) text += ", focus " + std::to_string(word(1480));  //(as dialogSaves())
  if(chosen) {
    text += ", chosen " + traceName(*chosen);
    used = *chosen;
  } else if((mode == 4 || mode == 5) && used.empty()) {
    used = first;
  }
  text += ", file " + traceName(memory.readString(p + 100, 13));
  text += ", buffer 0x" + hexWord(word(116)) + " of " + std::to_string(word(120));
  text += ", data " + std::to_string(word(124));
  if(mode != 7 && mode != 8 && mode != 11) {
    std::error_code error;
    bool plain = plainName(game, 13) && (used.empty() || plainName(used, 20));
    std::string folder = plain ? savePath(game + used) : std::string{};
    bool there = !folder.empty() && fs::is_directory(folder, error);
    text += ", " + traceName(game + used) + (there ? " there" : " not there");
  }
  if(u32 data = word(1492); mode == 8 && data && memory.reaches(data, 36)) {
    text += ", msData " + traceName(memory.readString(data, 13)) + " " + traceName(memory.readString(data + 16, 20));
  }
  if(u32 list = word(1524); mode == 11 && list && memory.reaches(list, 12)) {
    text += ", most " + std::to_string(memory.read(4, list));
  }
  if(u32 info = size >= 1536 ? word(1528) : 0; mode == 12 && info && memory.reaches(info, 36)) {
    text += ", most " + std::to_string(memory.read(4, info)) + " secure " + std::to_string(memory.read(4, info + 4))
          + " normal " + std::to_string(memory.read(4, info + 8)) + " system";
  }
  if(u32 info = size >= 1536 ? word(1532) : 0; mode == 22 && info && memory.reaches(info, 60)) {
    text += ", files " + std::to_string(memory.read(4, info)) + " secure " + std::to_string(memory.read(4, info + 4))
          + " normal";
  }
  if(mode == 1 || mode == 3 || mode == 5 || mode == 13 || mode == 14 || mode == 17 || mode == 18) {
    text += ", title " + traceName(memory.readString(p + 128, 128));
  }
  return text;
}

//A savedata request's answer as the trace gives it: the result (named where it's one of savedataMode()'s), then what
//the mode wrote back: a load's bytes read; a list load's or save's save name; the list mode's saves; the files mode's
//counts; the sizes mode's free clusters, msData's clusters and what saving would take; the size mode's KB needed.
auto Kernel::savedataAnswer(u32 p, u32 answer) -> std::string {
  //(the parameters' words where they're in memory: a read past it is noted and counted in the machine's state)
  auto word = [&](u32 at) -> u32 { return memory.reaches(p + at, 4) ? memory.read(4, p + at) : 0; };
  static const std::map<u32, const char*> Names = {
    {DialogCancelled, "cancelled"}, {SavedataLoadBroken, "load: data broken"},
    {SavedataLoadNoData, "load: no data"}, {SavedataLoadParameter, "load: bad parameter"},
    {SavedataLoadNoFile, "load: file not found"}, {SavedataReadBroken, "read: data broken"},
    {SavedataReadNoData, "read: no data"}, {SavedataReadParameter, "read: bad parameter"},
    {SavedataReadNoFile, "read: file not found"}, {SavedataDeleteNoData, "delete: no data"},
    {SavedataDeleteParameter, "delete: bad parameter"}, {SavedataSaveAccess, "save: access"},
    {SavedataSaveParameter, "save: bad parameter"}, {SavedataSizesNoData, "sizes: no data"},
    {SavedataSizesParameter, "sizes: bad parameter"}};
  u32 size = word(0), mode = word(48);
  std::string text = "0x" + hexWord(answer);
  if(auto name = Names.find(answer); name != Names.end()) text += " (" + std::string(name->second) + ")";
  if(!answer && (mode == 0 || mode == 2 || mode == 4 || mode == 15 || mode == 16)) {
    text += ", read " + std::to_string(word(124));
  }
  if(mode == 4 || mode == 5) text += ", save " + traceName(memory.readString(p + 76, 20));
  if(u32 list = word(1524); mode == 11 && list && memory.reaches(list, 12)) {
    u32 count = memory.read(4, list + 4), entries = memory.read(4, list + 8);
    text += ", listed " + std::to_string(count);
    for(u32 n = 0; n < std::min(count, 8u) && memory.reaches(entries + n * 72, 72); n++) {
      text += " " + traceName(memory.readString(entries + n * 72 + 52, 20));
    }
  }
  if(u32 info = size >= 1536 ? word(1528) : 0; mode == 12 && info && memory.reaches(info, 36)) {
    text += ", listed " + std::to_string(memory.read(4, info + 12)) + " secure "
          + std::to_string(memory.read(4, info + 16)) + " normal " + std::to_string(memory.read(4, info + 20))
          + " system";
  }
  if(mode == 8) {
    u32 free = word(1488), data = word(1492), needed = word(1496);
    if(free && memory.reaches(free, 12)) text += ", free " + std::to_string(memory.read(4, free + 4)) + " clusters";
    if(data && memory.reaches(data, 40)) text += ", msData " + std::to_string(memory.read(4, data + 36)) + " clusters";
    if(needed && memory.reaches(needed, 4)) text += ", needed " + std::to_string(memory.read(4, needed)) + " clusters";
  }
  if(u32 info = size >= 1536 ? word(1532) : 0; mode == 22 && info && memory.reaches(info, 60)) {
    text += ", needed " + std::to_string(memory.read(4, info + 36)) + " KB, overwrite "
          + std::to_string(memory.read(4, info + 48)) + " KB";
  }
  return text;
}

//Does what a SceUtilitySavedataParam (psputility_savedata.h) asks, by its mode; the dialog's result. The layout: the
//common part (48 bytes, its first word the structure's size), the mode at 48, bind at 52, the game's name at 60 (13
//bytes), the save's at 76 (20), a list of save names at 96, the data file's name at 100 (13), its buffer at 116, the
//buffer's size at 120 and the data's at 124, the save's titles from 128 (sfoParam: the title in 128 bytes, the save's
//own title in 128, its detail in 1024, then the parental level, a byte), the icons' and sound's files from 1412 (16
//bytes each: buffer, its size, the file's size, a word), then from 1488 where to put the sizes mode's answers (free
//space, a save's size, what saving would take), and in the larger structure of firmware 2.00 on (1536 bytes), from
//1524 the list mode's, at 1528 the files mode's and at 1532 the size mode's.
//
//The names become a folder and a file on the host: each must be a plain name (plainName(); the game's can't be left
//out, the save's can), and the paths where a save belongs (savePath()). The buffer must be in the program's memory as
//far as what's copied in or out, which memory's size bounds: a load reads no more than the buffer takes. What isn't
//so is refused as a bad parameter, before anything is made, read or deleted. The sizes mode reads no names of its
//own, so none are asked of it: the save it measures is msData's, whose names are checked there. chosen: the save a
//front end's player chose from a list (dialogs.cpp), taken in place of the request's own.
auto Kernel::savedataMode(u32 p, const std::string* chosen) -> u32 {
  namespace fs = std::filesystem;
  u32 mode = memory.read(4, p + 48);
  std::string game = memory.readString(p + 60, 13), save = memory.readString(p + 76, 20);
  std::string fileName = memory.readString(p + 100, 13);
  u32 buffer = memory.read(4, p + 116), bufferSize = memory.read(4, p + 120), dataSize = memory.read(4, p + 124);
  u32 refused = savedataRefusal(mode);
  std::error_code error;
  if(mode == 11) return savedataList(p, game, save);
  //"<>" names no save of its own: the PSP takes it as none, the save's folder the game's name alone, as the save's
  //name and as a list's choice (where it stands for any name). Taken as a name, it made folders no PSP's memory stick
  //can hold ('<' and '>' aren't allowed there), which the games looking for their saves again never found: Shining
  //Blade, Space Invaders Extreme and Valkyria Chronicles II made a save and called it corrupted.
  auto unnamed = [](std::string& name) { if(name == "<>") name.clear(); };
  unnamed(save);
  //Nor does a pattern (savePattern()) in a list's load or save from a list (one with a first name), where the save's
  //name only names the save the dialog focuses at first (dialogFocus()): Dissidia Final Fantasy and its 012 keep
  //"GameData*" from the list mode for their list loads, which looked for a folder of that name, found none, and went
  //back to the title. Anywhere else a pattern is taken as a name, which no save has: a load, read, files request or
  //delete finds none, as before (a list delete with no front end deletes the save it names), and a save is refused
  //below. (Chosen: with no list a pattern isn't taken as none, which would make it the game's folder alone, a save
  //of its own the game may keep there.)
  u32 names = memory.read(4, p + 96);
  bool listed = (mode == 4 || mode == 5) && names && !memory.readString(names, 20).empty();
  if(listed && savePattern(save)) save.clear();
  if(chosen) {
    save = *chosen;
  } else if((mode == 4 || mode == 5) && save.empty() && names) {  //from a list: its first name
    save = memory.readString(names, 20);
    if(save.empty()) return refused;
    unnamed(save);
  }
  //(Valhalla Knights 2 asks for sizes with no names, and took a refusal for a memory stick too full to save on.)
  if(mode != 8 && (!plainName(game, 13) || (!save.empty() && !plainName(save, 20)))) return refused;
  //A save mode is refused a pattern as a name that isn't plain is, making nothing (chosen: no recording saves under
  //one, and no memory stick holds its folder). Writing data (17, 18) under one is refused so too, where it was NO_DATA.
  bool saving = mode == 1 || mode == 3 || mode == 5 || mode == 13 || mode == 14 || mode == 17 || mode == 18;
  if(saving && savePattern(save)) return refused;
  bool filed = mode <= 5 || (mode >= 13 && mode <= 20);  //the modes that read, write or erase the data file
  if(filed && !fileName.empty() && !plainName(fileName, 13)) return refused;
  std::string folder = savePath(game + save);
  std::string file = filed && !fileName.empty() ? savePath(game + save, fileName) : std::string{};
  bool exists = !folder.empty() && fs::is_directory(folder, error);
  //The save's PARAM.SFO (SaveSFO): its titles, and which of its files are secure. A PSP calls a save without one
  //broken (utility/savedata/loadbroken and deletebroken: its loads, reads, files and erases DATA_BROKEN), but a save
  //without one here is v2.0.0's, from before Phobos wrote them, and still loads. Chosen: its files are normal ones,
  //as if its PARAM.SFO named none secure (a PSP's file its PARAM.SFO doesn't name is normal: filelist), nothing known
  //of its titles; so a game that checks its files' kinds calls such a save corrupted once, and makes it again (the
  //next save mode writes its PARAM.SFO). Broken is a PARAM.SFO that isn't one.
  SaveSFO sfo;
  bool broken = false;
  auto describe = [&] { return exists && sfo.read(savePath(game + save, "PARAM.SFO"), broken); };
  switch(mode) {
  case 0: case 2: case 4: case 15: case 16: {  //load (auto, chosen, from a list), read (secure or not)
    //Each in the loads' group of errors or the reads': no save is NO_DATA (utility/savedata/loaddata); as loadbroken
    //and loademptyfilename recorded, one whose PARAM.SFO is broken is DATA_BROKEN (before its file is looked for),
    //one without the file named FILE_NOT_FOUND, and with no file named, nothing is read and the answer is 0. Loaded,
    //the save's titles go into sfoParam, and bind is 1021, as the files mode leaves it (PPSSPP's notes, both).
    bool reading = mode >= 15;
    if(!exists) return reading ? SavedataReadNoData : SavedataLoadNoData;
    bool described = describe();
    if(broken) return reading ? SavedataReadBroken : SavedataLoadBroken;
    u32 count = 0;
    if(!fileName.empty()) {
      u32 none = reading ? SavedataReadNoFile : SavedataLoadNoFile;
      if(file.empty() || !fs::is_regular_file(file, error)) return none;
      u64 size = fs::file_size(file, error);
      if(error) return none;
      count = std::min<u64>(size, bufferSize);
      if(count && !memory.reaches(buffer, count)) return refused;
      std::vector<u8> bytes(count);
      std::ifstream stream(file, std::ios::binary);
      stream.read((char*)bytes.data(), count);
      count = stream.gcount();
      memory.copyIn(buffer, bytes.data(), count);
    }
    memory.write(4, p + 124, count);  //what it read
    if(described) {
      auto title = [&](u32 at, const std::string& text, u32 room) {
        memory.fill(at, 0, room);
        memory.copyIn(at, text.data(), std::min<u64>(text.size(), room - 1));
      };
      title(p + 128, sfo.text("TITLE"), 128);
      title(p + 256, sfo.text("SAVEDATA_TITLE"), 128);
      title(p + 384, sfo.text("SAVEDATA_DETAIL"), 1024);
      memory.write(1, p + 1408, sfo.number("PARENTAL_LEVEL"));
    }
    memory.write(4, p + 52, SavedataBind);
    return 0;
  }
  case 1: case 3: case 5: case 13: case 14: case 17: case 18: {  //save (auto, chosen, to a list), make, write
    u32 size = std::min(dataSize, bufferSize);
    if(folder.empty() || (!fileName.empty() && file.empty())) return SavedataSaveAccess;
    if(!fileName.empty() && size && !memory.reaches(buffer, size)) return refused;
    //Writing data (17, 18) only adds a file to a save that's there: with none, the write is NO_DATA and nothing is
    //made, as utility/savedata/saveemptyfilename recorded. (Hot Shots Golf: Open Tee 2 writes its data so before it
    //has made a save, and took the zeros it had written, kept, for a corrupted save.)
    if((mode == 17 || mode == 18) && !exists) return SavedataReadNoData;
    fs::create_directories(folder, error);
    if(!fileName.empty()) {
      std::vector<u8> bytes(size);
      memory.copyOut(bytes.data(), buffer, size);
      dropCuts(file);  //(written over here: a descriptor that emptied it is cut no more, io.cpp)
      std::ofstream stream(file, std::ios::binary | std::ios::trunc);
      stream.write((const char*)bytes.data(), size);
      if(!stream) return SavedataSaveAccess;
    }
    savedataIcons(p, game + save);
    //The save modes' files are secure, as the secure modes' are: a PSP encrypts a save's file with the game's key, or
    //given none without one (secureversion: a save with no key has its SAVEDATA_PARAMS say mode 1, encrypted).
    return savedataWritten(p, game + save, fileName, mode != 14 && mode != 18) ? 0 : SavedataSaveAccess;
  }
  case 12: {  //the files mode: what the save holds (savedataFiles())
    //Asked for a list of more than 99 secure files, 8192 normal ones or, by a program built with SDK 2.60 or later,
    //the 5 system files, a bad parameter (PPSSPP's notes on the PSP). No save is NO_DATA (deletedata,
    //deleteemptyfilename), one whose PARAM.SFO is broken DATA_BROKEN (loadbroken), nothing written either way; and
    //bind is 1021 (filelist). Chosen: with no list to fill (none given, or the smaller structure), the answers are the
    //same.
    u32 info = memory.read(4, p) >= 1536 ? memory.read(4, p + 1528) : 0;
    if(info && !memory.reaches(info, 36)) return refused;
    auto over = [&](u32 kind, u32 most) {
      return memory.read(4, info + 24 + kind * 4) && memory.read(4, info + kind * 4) > most;
    };
    if(info && (over(0, 99) || over(1, 8192) || (sdkVersion >= 0x0206'0000 && over(2, 5)))) return refused;
    if(!exists) return SavedataReadNoData;
    describe();
    if(broken) return SavedataReadBroken;
    memory.write(4, p + 52, SavedataBind);
    if(info) savedataFiles(info, folder, sfo.secure());
    return 0;
  }
  case 8: {  //sizes: the memory stick's free space; a save's own size; what saving this one would take
    //(SceUtilitySavedataMsFreeInfo, SceUtilitySavedataMsDataInfo and SceUtilitySavedataUsedDataInfo: each where
    //the game wants it, or 0 for not wanted.) The save measured is the one msData names, by its own game and save
    //names: pspautotests' utility/savedata/sizes had the request's save name "ASDF", none there, and msData's "ABC",
    //and got ABC's size and 0. Without msData there's nothing to look for, and the answer is 0 (Chili Con Carnage
    //and Ace Combat X ask for the free space alone, and had been told there was no save: the first then said there
    //wasn't room for one, the second waited for good). Chosen: msData naming a save that isn't there is told so
    //(SIZES_NO_DATA), the rest still answered. The save's secure files (its PARAM.SFO's) are counted as long as a
    //PSP stores them, encrypted (savedataClusters()).
    u32 free = memory.read(4, p + 1488), data = memory.read(4, p + 1492), needed = memory.read(4, p + 1496);
    if(free) {
      memory.write(4, free, StickClusterSize);
      memory.write(4, free + 4, StickFreeClusters);
      memory.write(4, free + 8, StickFreeClusters * (StickClusterSize / 1024));
      stickText(free + 12, StickFreeClusters * (StickClusterSize / 1024));
    }
    auto usage = [&](u32 at, u64 clusters) {  //clusters, KiB, as text, then in 32 KiB steps: the same here
      u32 kilobytes = clusters * (StickClusterSize / 1024);
      memory.write(4, at, clusters);
      memory.write(4, at + 4, kilobytes);
      stickText(at + 8, kilobytes);
      memory.write(4, at + 16, kilobytes);
      stickText(at + 20, kilobytes);
    };
    u32 answer = 0;
    if(data) {  //(the game's name in 16 bytes, the save's in 20, then where its size goes)
      std::string other = memory.readString(data, 13), otherSave = memory.readString(data + 16, 20);
      unnamed(otherSave);
      bool plain = plainName(other, 13) && (otherSave.empty() || plainName(otherSave, 20));
      std::string otherFolder = plain ? savePath(other + otherSave) : std::string{};
      if(!otherFolder.empty() && fs::is_directory(otherFolder, error)) {
        SaveSFO measured;
        bool unread = false;
        measured.read(savePath(other + otherSave, "PARAM.SFO"), unread);
        usage(data + 36, savedataClusters(otherFolder, measured.secure()));
      } else {
        answer = SavedataSizesNoData;
      }
    }
    if(needed) usage(needed, savedataNeeded(p, dataSize));
    return answer;
  }
  case 22: {  //the size mode: the memory stick's free space, and what the files the game lists would take
    //(A PspUtilitySavedataSizeInfo at 1532, in the larger structure alone: how many secure and normal files, where
    //each list of them is (a 64-bit size and a 16-byte name each), then the "sector" size, the free sectors, KiB and
    //their text, and the KiB and text still needed beyond the free space to save them new and over a save, as
    //pspsdk's neededKB and overwriteKB name them. pspautotests' utility/savedata/getsize recorded the free space
    //written, a 32 KiB cluster to a sector, and with no files listed the rest left as it was. A secure file takes
    //what a PSP stores, encrypted (SecureFileOverhead). Chosen: a listed file takes its size in whole clusters, newly
    //or over a save alike; what's needed is what they take past the free space, 0 for anything a game saves on this
    //stick, its text written only when something is needed (left as it was otherwise, as the recording leaves what
    //has nothing to say).) The answer is 0 for a save that's there, as getsize recorded (it asks only with its save
    //made), and for one that isn't NO_DATA, the sizes written all the same. No recording asks with no save; Def Jam:
    //Fight for NY's code shows the answer: its first save asks this before writing, making the save when the answer is
    //NO_DATA (0x80110327) and neededKB 0, and writing into it when it's 0 and overwriteKB 0 (its code's two ways on).
    //Told 0 with no save, it wrote into none, was refused (NO_DATA: writing needs the save) and asked to go on without
    //saving; and Hot Shots Golf: Open Tee 2, which asks it after finding no save to load, said its save was corrupted.
    //Chosen: with no size information to fill in (none given, or the smaller structure), the answer is the same, by
    //the save alone.
    u32 info = memory.read(4, p) >= 1536 ? memory.read(4, p + 1532) : 0;
    u32 answer = exists ? 0 : SavedataReadNoData;
    if(!info || !memory.reaches(info, 60)) return answer;
    memory.write(4, info + 16, StickClusterSize);
    memory.write(4, info + 20, StickFreeClusters);
    memory.write(4, info + 24, StickFreeClusters * (StickClusterSize / 1024));
    stickText(info + 28, StickFreeClusters * (StickClusterSize / 1024));
    u64 clusters = 0;
    u32 files = 0;
    for(u32 list : {0u, 1u}) {  //the secure files, then the normal ones
      u32 count = memory.read(4, info + list * 4), entries = memory.read(4, info + 8 + list * 4);
      for(u32 n = 0; entries && n < std::min(count, 64u) && memory.reaches(entries + n * 24, 24); n++, files++) {
        u64 size = memory.read(4, entries + n * 24) | u64(memory.read(4, entries + n * 24 + 4)) << 32;
        if(list == 0) size += SecureFileOverhead;
        clusters += (size + StickClusterSize - 1) / StickClusterSize;
      }
    }
    if(files) {
      u64 kilobytes = clusters * (StickClusterSize / 1024);
      u64 free = u64(StickFreeClusters) * (StickClusterSize / 1024);
      u32 shortfall = std::min<u64>(kilobytes > free ? kilobytes - free : 0, 0x7fff'ffff);
      memory.write(4, info + 36, shortfall);
      memory.write(4, info + 48, shortfall);
      if(shortfall) {
        stickText(info + 40, shortfall);
        stickText(info + 52, shortfall);
      }
    }
    return answer;
  }
  case 6: case 7: case 9: case 10: case 21: {  //deleting a save: its folder goes, with all it holds
    if(!exists) return SavedataDeleteNoData;
    fs::remove_all(folder, error);
    if(!error) dropCuts(folder);  //(a descriptor still open in it holds what's gone: io.cpp)
    return 0;
  }
  case 19: case 20: {  //erasing (secure or not): the data file named goes, and its name from PARAM.SFO's list
    //In the reads' group of errors, as deletebroken recorded: a save whose PARAM.SFO is broken is DATA_BROKEN, one
    //without the file named FILE_NOT_FOUND; and no save is NO_DATA (PPSSPP's notes, as the name's leaving the list).
    //With no file named, nothing goes (deleteemptyfilename).
    if(!exists) return SavedataReadNoData;
    bool described = describe();
    if(broken) return SavedataReadBroken;
    if(fileName.empty()) return 0;
    if(file.empty() || !fs::is_regular_file(file, error)) return SavedataReadNoFile;
    if(fs::remove(file, error)) dropCuts(file);
    if(described && sfo.listed(fileName, false)) {
      std::string path = savePath(game + save, "PARAM.SFO");
      dropCuts(path);
      sfo.write(path);
    }
    return 0;
  }
  default:  //a mode not known: nothing to tell
    return exists ? 0 : SavedataReadNoData;
  }
}

//What a save mode leaves beside the file it writes (savedata()'s 1, 3, 5, 13, 14, 17 and 18): the save's PARAM.SFO,
//as a PSP writes it (makedata and filelist: the make modes leave one; saveemptyfilename: the save modes too, with no
//data file). Its titles from the parameters' sfoParam (TITLE, SAVEDATA_TITLE, SAVEDATA_DETAIL, PARENTAL_LEVEL), but
//from a request with no title, which keeps the save's (PPSSPP's rule: games write their data with sfoParam left
//empty); CATEGORY "MS"; SAVEDATA_DIRECTORY, the folder's name; SAVEDATA_FILE_LIST naming the file written if it's
//secure, its hash zeros (chosen: written as a normal file, its name is taken out, so the list names what's secure on
//the stick); SAVEDATA_PARAMS, 128 zeros, nothing encrypted (PPSSPP's mark of a save it didn't encrypt). A broken
//PARAM.SFO is written afresh, as is one too big to write again with all this (only a hand-made one could be).
//Whether it was written.
auto Kernel::savedataWritten(u32 p, const std::string& folder, const std::string& file, bool secure) -> bool {
  std::string path = savePath(folder, "PARAM.SFO");
  if(path.empty()) return false;
  SaveSFO sfo;
  bool broken = false;
  sfo.read(path, broken);
  auto describe = [&] {
    std::string title = memory.readString(p + 128, 128);
    if(!title.empty() || !sfo.values.count("TITLE")) {
      sfo.setText("TITLE", title, 128);
      sfo.setText("SAVEDATA_TITLE", memory.readString(p + 256, 128), 128);
      sfo.setText("SAVEDATA_DETAIL", memory.readString(p + 384, 1024), 1024);
      sfo.setNumber("PARENTAL_LEVEL", memory.read(1, p + 1408));
    }
    sfo.setText("CATEGORY", "MS", 4);
    sfo.setText("SAVEDATA_DIRECTORY", folder, 64);
    sfo.fileList();
    if(!file.empty()) sfo.listed(file, secure);
    sfo.setBytes("SAVEDATA_PARAMS", std::vector<u8>(128));
  };
  describe();
  if(sfo.size() > 64_KiB) {
    sfo.values.clear();
    describe();
  }
  dropCuts(path);  //(replaced: a descriptor that emptied this PARAM.SFO is cut no more, io.cpp)
  return sfo.write(path);
}

//The icons' and sound's files a save mode is given (from 1412, 16 bytes each: buffer, its size, the file's size),
//written beside the data, as a PSP's saves hold them and as PPSSPP writes them: ICON0.PNG, ICON1.PMF, PIC1.PNG and
//SND0.AT3, each whose buffer is given and holds the file, over the save's own; one not given leaves the save's as it
//was. Chosen: one whose buffer is smaller than the file, or outside the program's memory, is left out, the save
//still made.
auto Kernel::savedataIcons(u32 p, const std::string& folder) -> void {
  static constexpr const char* Names[] = {"ICON0.PNG", "ICON1.PMF", "PIC1.PNG", "SND0.AT3"};
  for(u32 n = 0; n < 4; n++) {
    u32 at = p + 1412 + n * 16, buffer = memory.read(4, at), room = memory.read(4, at + 4);
    u32 size = memory.read(4, at + 8);
    if(!buffer || !size || size > room || !memory.reaches(buffer, size)) continue;
    std::string path = savePath(folder, Names[n]);
    if(path.empty()) continue;
    std::vector<u8> bytes(size);
    memory.copyOut(bytes.data(), buffer, size);
    dropCuts(path);
    std::ofstream(path, std::ios::binary | std::ios::trunc).write((const char*)bytes.data(), size);
  }
}

//What a save on the memory stick takes, in clusters: each of its files in whole clusters, a secure one (secure
//names it, from the save's PARAM.SFO) as a PSP stores it, encrypted, and the folder's own cluster (pspautotests'
//utility/savedata/sizes: a save of three small files, 4 clusters). As far as the host can tell (nothing here throws).
auto Kernel::savedataClusters(const std::string& folder, const std::vector<std::string>& secure) -> u64 {
  namespace fs = std::filesystem;
  u64 clusters = 1;
  std::error_code error;
  for(fs::directory_iterator at(folder, error), end; !error && at != end; at.increment(error)) {
    std::error_code unread;
    if(!at->is_regular_file(unread)) continue;
    u64 size = at->file_size(unread);
    if(std::count(secure.begin(), secure.end(), at->path().filename().string())) size += SecureFileOverhead;
    if(!unread) clusters += (size + StickClusterSize - 1) / StickClusterSize;
  }
  return clusters;
}

//What saving the request's save would take, in clusters: its data file as a PSP stores it (dataSize bytes,
//encrypted: the save modes' files are secure; and with no file named, none: PPSSPP's notes), the icons' and sound's
//files it carries (their sizes, at 1412 on: ICON0, ICON1, PIC1, SND0), its PARAM.SFO (a cluster) and the folder's
//own cluster. pspautotests' utility/savedata/sizes: 16 bytes of data and no other file, 3 clusters (96 KB).
auto Kernel::savedataNeeded(u32 p, u32 dataSize) -> u64 {
  auto clusters = [](u64 bytes) { return (bytes + StickClusterSize - 1) / StickClusterSize; };
  bool named = memory.read(1, p + 100);
  u64 total = (named ? clusters(u64(dataSize) + SecureFileOverhead) : 0) + 2;
  for(u32 file = 0; file < 4; file++) {
    u32 at = p + 1412 + file * 16;
    if(memory.read(4, at)) total += clusters(memory.read(4, at + 8));
  }
  return total;
}

//The list mode (savedata()'s 11): the game's saves the request's save name takes as a pattern (savePattern(),
//patternTakes()), its folders in SAVEDATA whose names are the game's and then a name the pattern takes, into a
//SceUtilitySavedataIdListInfo (from 1524: the most to list, how many were listed, the entries). utility/savedata/
//idlist's "A?C" listed ABC, its one save: the name is a pattern, not a save's name. That the saves it doesn't take are
//left out, and a name with neither '*' nor '?' lists that save alone, is chosen, as the games mean their patterns:
//every folder starting with the game's name was listed, and Final Fantasy Type-0's "GAMEDATA*", asked for one save,
//was given its SYSTEMDATA. An empty save name, or "<>", lists every save of the game's (chosen, as before: no
//recording lists with either, and they name no save of their own anywhere else). Each entry's dates are its
//PARAM.SFO's, as PPSSPP gives them (the request names no file to date a save by), or for a save without one, its
//folder's (chosen; none for a folder that leads off the stick). As far as the host can tell them (nothing here throws).
auto Kernel::savedataList(u32 p, const std::string& game, const std::string& save) -> u32 {
  namespace fs = std::filesystem;
  u32 list = memory.read(4, p + 1524);
  if(!list) return 0;
  u32 most = memory.read(4, list), entries = memory.read(4, list + 8), count = 0;
  std::string saves, normalized;
  if(!resolve("ms0:/PSP/SAVEDATA", saves, normalized)) {
    std::vector<std::string> names;
    std::error_code error;
    bool every = save.empty() || save == "<>";
    for(fs::directory_iterator at(saves, error), end; !error && at != end; at.increment(error)) {
      std::error_code unread;
      auto name = at->path().filename().string();
      if(!at->is_directory(unread) || name.compare(0, game.size(), game)) continue;
      if(every || patternTakes(save, name.substr(game.size()))) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    for(auto& name : names) {
      if(count == most) break;
      u32 at = entries + count++ * 72;  //its mode, three dates (16 bytes each), then its name (20)
      if(!memory.reaches(at, 72)) continue;
      memory.fill(at, 0, 72);
      memory.write(4, at, 0x11ff);    //a folder
      std::error_code unread;
      std::string dated = savePath(name, "PARAM.SFO");
      if(dated.empty() || !fs::is_regular_file(dated, unread)) dated = savePath(name);
      if(!dated.empty()) writeTimes(at + 4, dated);
      memory.copyIn(at + 52, name.c_str() + game.size(), std::min<u32>(name.size() - game.size() + 1, 20));
    }
  }
  memory.write(4, list + 4, count);
  return 0;
}

//The files mode (savedata()'s 12): the save's files, into a SceUtilitySavedataFileListInfo (at info: the most of
//each kind to list, how many were listed, then where each kind's entries go: the secure files', the normal ones',
//the system's). The system's are PARAM.SFO and the icons' and sound's files; the secure ones those PARAM.SFO's
//SAVEDATA_FILE_LIST names; the rest are normal. Each entry is a SceIoStat's first 64 bytes: utility/savedata/filelist
//recorded each kind's first entry's mode (a file, readable, writable and runnable by all) and name, and the word after
//the mode left as the program had it (the secure entry's, the one it reads); the size and the three dates are as
//sceIoGetstat gives them, a secure file's size its data's (PPSSPP's notes: the 16 bytes a PSP's encryption adds left
//out, which the sizes modes count; here a secure file has none); then the name (16 bytes). Each kind's count is how
//many were listed: no more than its most, and none without a list to put them in. Folders aren't listed, nor names
//with small letters (PPSSPP's notes on the PSP); nor, chosen, links, or names no request could give (longer than a
//data file's name's 13 bytes, or not plain: plainName()). In the names' order (chosen: the PSP lists a folder in its
//own order, which a host doesn't keep). As far as the host can tell (nothing here throws).
auto Kernel::savedataFiles(u32 info, const std::string& host, const std::vector<std::string>& secure) -> void {
  namespace fs = std::filesystem;
  static const std::vector<std::string> System = {"PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "PIC1.PNG", "SND0.AT3"};
  std::vector<std::string> names;
  std::error_code error;
  for(fs::directory_iterator at(host, error), end; !error && at != end; at.increment(error)) {
    std::error_code unread;
    auto name = at->path().filename().string();
    if(at->is_symlink(unread) || !at->is_regular_file(unread) || unread || !plainName(name, 13)) continue;
    if(std::none_of(name.begin(), name.end(), [](char c) { return c >= 'a' && c <= 'z'; })) names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  u32 counts[3] = {};
  for(auto& name : names) {
    u32 kind = std::count(System.begin(), System.end(), name) ? 2
             : std::count(secure.begin(), secure.end(), name) ? 0 : 1;
    u32 most = memory.read(4, info + kind * 4), entries = memory.read(4, info + 24 + kind * 4);
    u32 entry = entries + counts[kind] * 80;
    if(!entries || counts[kind] >= most || !memory.reaches(entry, 80)) continue;
    counts[kind]++;
    auto path = (fs::path(host) / name).string();
    std::error_code unread;
    u64 size = fs::file_size(path, unread);
    if(unread) size = 0;
    memory.write(4, entry, 0x21ff);
    memory.write(4, entry + 8, u32(size));
    memory.write(4, entry + 12, u32(size >> 32));
    writeTimes(entry + 16, path);
    memory.fill(entry + 64, 0, 16);
    memory.copyIn(entry + 64, name.data(), name.size());
  }
  for(u32 kind : {0u, 1u, 2u}) memory.write(4, info + 12 + kind * 4, counts[kind]);
}

auto Kernel::sceUtilitySavedataInitStart() -> void { dialogStart(DialogSavedata); }
auto Kernel::sceUtilitySavedataGetStatus() -> void { dialogStatus(DialogSavedata); }
auto Kernel::sceUtilitySavedataUpdate() -> void { dialogUpdate(DialogSavedata); }
auto Kernel::sceUtilitySavedataShutdownStart() -> void { dialogShutdown(DialogSavedata); }
auto Kernel::sceUtilityMsgDialogInitStart() -> void { dialogStart(DialogMessage); }
auto Kernel::sceUtilityMsgDialogGetStatus() -> void { dialogStatus(DialogMessage); }
auto Kernel::sceUtilityMsgDialogUpdate() -> void { dialogUpdate(DialogMessage); }
auto Kernel::sceUtilityMsgDialogShutdownStart() -> void { dialogShutdown(DialogMessage); }

//The game taking its message down while it runs: it fades out over Updates, then finishes (dialogUpdate()). A front
//end showing it takes it down at once.
auto Kernel::sceUtilityMsgDialogAbort() -> void {
  if(dialog.kind != DialogMessage) return result(UtilityWrongType);
  dialogDue();
  if(dialog.status != DialogRunning) return result(UtilityInvalidStatus);
  //(utility/dialog/abort: max(8 - Updates so far, 1) more Updates from when it started running)
  if(!dialog.abortUpdates) {
    dialog.abortUpdates = dialog.runningUpdates < MessageAbortUpdates
      ? MessageAbortUpdates - dialog.runningUpdates : 1;
  }
  dialogWithdraw();
  result(0);
}
auto Kernel::sceUtilityOskInitStart() -> void { dialogStart(DialogKeyboard); }
auto Kernel::sceUtilityOskGetStatus() -> void { dialogStatus(DialogKeyboard); }
auto Kernel::sceUtilityOskUpdate() -> void { dialogUpdate(DialogKeyboard); }
auto Kernel::sceUtilityOskShutdownStart() -> void { dialogShutdown(DialogKeyboard); }
auto Kernel::sceUtilityNetconfInitStart() -> void { dialogStart(DialogNetwork); }
auto Kernel::sceUtilityNetconfGetStatus() -> void { dialogStatus(DialogNetwork); }
auto Kernel::sceUtilityNetconfUpdate() -> void { dialogUpdate(DialogNetwork); }
auto Kernel::sceUtilityNetconfShutdownStart() -> void { dialogShutdown(DialogNetwork); }
auto Kernel::sceUtilityGameSharingInitStart() -> void { dialogStart(DialogSharing); }
auto Kernel::sceUtilityGameSharingGetStatus() -> void { dialogStatus(DialogSharing); }
auto Kernel::sceUtilityGameSharingUpdate() -> void { dialogUpdate(DialogSharing); }
auto Kernel::sceUtilityGameSharingShutdownStart() -> void { dialogShutdown(DialogSharing); }
auto Kernel::sceUtilityHtmlViewerInitStart() -> void { dialogStart(DialogBrowser); }
auto Kernel::sceUtilityHtmlViewerGetStatus() -> void { dialogStatus(DialogBrowser); }
auto Kernel::sceUtilityHtmlViewerUpdate() -> void { dialogUpdate(DialogBrowser); }
auto Kernel::sceUtilityHtmlViewerShutdownStart() -> void { dialogShutdown(DialogBrowser); }

//(module: psputility_modules.h's PSP_MODULE_*): network (0x100-0x108), USB devices (0x200-0x203), sound and video
//codecs (0x300-0x308), the network platform (0x400-0x402), DRM (0x500), infrared (0x600). pspsdk's list stops at
//0x106 and 0x307; later firmwares have more: 0x308 (taken here to be the MP4 library, sceMp4, whose functions the
//kernel would stand in for as for the rest), which Ace Combat: Joint Assault loads at boot, stopping if it's refused,
//and network modules to 0x108, which Macross: Triangle Frontier loads, trying again every few frames for as long as
//it's refused (0x107 is taken too, each group's modules being numbered without gaps). 0x109 and 0x309 are refused.
static auto utilityModuleKnown(u32 module) -> bool {
  u32 group = module >> 8, index = module & 0xff;
  static constexpr u8 Counts[] = {0, 9, 4, 9, 3, 1, 1};  //how many modules in each group
  return group >= 1 && group <= 6 && index < Counts[group];
}

auto Kernel::sceUtilityLoadModule() -> void {
  u32 module = arg(0);
  if(!utilityModuleKnown(module)) return result(ModuleBadID);
  if(std::count(utilityModules.begin(), utilityModules.end(), module)) return result(ModuleLoaded);
  utilityModules.push_back(module);
  result(0);
}

auto Kernel::sceUtilityUnloadModule() -> void {
  u32 module = arg(0);
  if(!utilityModuleKnown(module)) return result(ModuleBadID);
  auto found = std::find(utilityModules.begin(), utilityModules.end(), module);
  if(found == utilityModules.end()) return result(ModuleNotLoaded);
  utilityModules.erase(found);
  result(0);
}

//(network module 1-7: psputility_netmodules.h's PSP_NET_MODULE_*), the older way to load them: as the modules
//0x100-0x106.
auto Kernel::sceUtilityLoadNetModule() -> void {
  if(arg(0) < 1 || arg(0) > 7) return result(ModuleBadID);
  cpu.ipu.r[4] = 0x100 + arg(0) - 1;
  sceUtilityLoadModule();
}

auto Kernel::sceUtilityUnloadNetModule() -> void {
  if(arg(0) < 1 || arg(0) > 7) return result(ModuleBadID);
  cpu.ipu.r[4] = 0x100 + arg(0) - 1;
  sceUtilityUnloadModule();
}

//(sound or video module 0-7: psputility_avmodules.h's PSP_AV_MODULE_*), the older way to load them: as the modules
//0x300-0x307.
auto Kernel::sceUtilityLoadAvModule() -> void {
  if(arg(0) > 7) return result(ModuleBadID);
  cpu.ipu.r[4] = 0x300 + arg(0);
  sceUtilityLoadModule();
}

auto Kernel::sceUtilityUnloadAvModule() -> void {
  if(arg(0) > 7) return result(ModuleBadID);
  cpu.ipu.r[4] = 0x300 + arg(0);
  sceUtilityUnloadModule();
}

//(USB module 1-5: psputility_usbmodules.h's PSP_USB_MODULE_*), the older way to load the USB devices' modules:
//PSPCM (1), the accessory port's driver (2), and the microphone (3), camera (4) and GPS (5), which need the accessory
//driver first, as the modules 0x200 and 0x201-0x203. The accessory driver has no number of its
//own among psputility_modules.h's (the later firmwares' modules load it with the device's), so it's taken, and let
//go, without being kept. ATV Offroad Fury Pro loads one at boot and stops at a break if it isn't loaded.
static auto usbModule(u32 module) -> u32 {
  static constexpr u32 Modules[] = {0, 0x200, 0, 0x201, 0x202, 0x203};
  return module < 6 ? Modules[module] : 0;
}

auto Kernel::sceUtilityLoadUsbModule() -> void {
  if(arg(0) < 1 || arg(0) > 5) return result(ModuleBadID);
  if(!usbModule(arg(0))) return result(0);
  cpu.ipu.r[4] = usbModule(arg(0));
  sceUtilityLoadModule();
}

auto Kernel::sceUtilityUnloadUsbModule() -> void {
  if(arg(0) < 1 || arg(0) > 5) return result(ModuleBadID);
  if(!usbModule(arg(0))) return result(0);
  cpu.ipu.r[4] = usbModule(arg(0));
  sceUtilityUnloadModule();
}

//(which, where, its size): the system's text settings: the player's nickname (1) is "PSP". Others aren't known.
auto Kernel::sceUtilityGetSystemParamString() -> void {
  if(arg(0) != 1) return result(UtilityBadParameterID);
  const char nickname[] = "PSP";
  if(arg(2) < sizeof(nickname)) return result(0x8011'0102);  //the string too long for its room
  memory.copyIn(arg(1), nickname, sizeof(nickname));
  result(0);
}

//(which, text): setting them is accepted and forgotten: the settings stay the system's own.
auto Kernel::sceUtilitySetSystemParamString() -> void {
  result(0);
}

//(which, value): the number settings likewise (Colin McRae Rally 2005 sets 3, the wireless LAN's power saving).
auto Kernel::sceUtilitySetSystemParamInt() -> void {
  result(0);
}
