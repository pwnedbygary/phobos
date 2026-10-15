//The game data install dialog (ares/psp/kernel/install.cpp): the disc's PSP_GAME/INSDIR copied into the memory stick's
//PSP/SAVEDATA/<game><data> a piece at each Update, its progress written, then a PARAM.SFO describing it from the
//parameters; its statuses and their timing; each answer (installed, cancelled, no memory stick, no space, the stick
//unwritable, a mode it doesn't take); an install cut short by the game, a reset, Phobos killed or a state loaded,
//which leaves the saves as they were, and the copies it leaves taken away at the next boot or install; the copies'
//folder kept from programs; a copy the host changes meanwhile given up; an install over one already there; a front
//end shown its progress in mode 1; the trace's lines. Called directly, the clock moved on by hand.
#include "kernel-machine.hpp"
#include "disc-image.hpp"

#include <unistd.h>

namespace allegrex_test::psp {

namespace {
constexpr u32 Parameters = 0x0892'0000;
constexpr u64 Millisecond = Kernel::CPUFrequency / 1000;
constexpr u64 Piece = 1_MiB;  //what an Update copies (install.cpp's InstallBytesPerUpdate)
using Request = Kernel::DialogRequest;
using Answer = Kernel::DialogAnswer;
namespace fs = std::filesystem;

//size bytes of a pattern of seed's
auto pattern(u64 size, u32 seed) -> std::vector<u8> {
  std::vector<u8> bytes(size);
  u32 state = seed * 2654435761u + 1;
  for(auto& byte : bytes) state = state * 1103515245 + 12345, byte = state >> 16;
  return bytes;
}

auto text(const std::vector<u8>& bytes) -> std::string { return {bytes.begin(), bytes.end()}; }

//The disc's INSDIR: an icon, a picture, a data file two and a half pieces long, an empty file, and a folder (not
//copied); and a file elsewhere on the disc.
const std::vector<u8> Icon = pattern(4096, 1), Picture = pattern(10240, 2), Data = pattern(Piece * 5 / 2, 3);
const u64 Total = Icon.size() + Picture.size() + Data.size();
auto installFiles() -> std::vector<disc_image::File> {
  return {
    {"PSP_GAME/INSDIR/ICON0.PNG", Icon}, {"PSP_GAME/INSDIR/PIC1.PNG", Picture}, {"PSP_GAME/INSDIR/DATA.BIN", Data},
    {"PSP_GAME/INSDIR/EMPTY.DAT", {}}, {"PSP_GAME/INSDIR/EXTRA/NOT.BIN", pattern(100, 4)},
    {"PSP_GAME/USRDIR/OTHER.BIN", pattern(100, 5)},
  };
}

//A machine with a memory stick (a fresh host folder) and the disc in the drive.
struct InstallMachine : KernelMachine {
  HostFolder stick;
  std::vector<std::string> traced;
  InstallMachine(bool front = false) {
    kernel.mount("ms0", stick.path.string());
    kernel.disc = discFrom(disc_image::makeIso(installFiles()).bytes);
    kernel.frontDialogs = front;
    kernel.savedataTrace = [this](const std::string& line) { traced.push_back(line); };
  }
  auto advance(u64 cycles) -> void { kernel.cycles += cycles; kernel.events(); }
  auto status() -> u32 { return call("sceUtilityGamedataInstallGetStatus", {}); }
  auto update() -> u32 { return call("sceUtilityGamedataInstallUpdate", {1}); }
  auto result() -> u32 { return system.memory.read(4, Parameters + 28); }
  auto progress() -> u32 { return system.memory.read(4, Parameters + 1372); }
  auto saved(const std::string& name) -> fs::path { return stick.path / "PSP/SAVEDATA" / name; }
  auto copies() -> fs::path { return stick.path / ".phobos-install"; }
  //started, and running after its 200 ms
  auto start() -> bool {
    if(call("sceUtilityGamedataInstallInitStart", {Parameters}) || status() != 1) return false;
    advance(199 * Millisecond);
    if(status() != 1) return false;
    advance(1 * Millisecond);
    return status() == 2;
  }
  //Updates until it's finished (at most `most`): how many it took (0: it didn't finish)
  auto finish(u32 most = 100) -> u32 {
    for(u32 n = 1; n <= most; n++) {
      if(update()) return 0;
      if(status() == 3) return n;
    }
    return 0;
  }
  //shut down, and gone 40 ms later
  auto close() -> bool {
    if(call("sceUtilityGamedataInstallShutdownStart", {}) || status() != 4) return false;
    advance(40 * Millisecond);
    return status() == 0;
  }
};

//The parameters: their size, the mode, the game's name and the data's, the titles and parental level 3; English,
//circle confirming; the result, the progress and the two words after it set to see what's written.
auto installParameters(KernelMachine& m, u32 size, s32 mode, const char* game, const char* data) -> void {
  auto& memory = m.system.memory;
  memory.fill(Parameters, 0, 0x598);
  memory.write(4, Parameters, size);
  memory.write(4, Parameters + 4, 1);
  memory.write(4, Parameters + 28, 0xcccc'cccc);
  memory.write(4, Parameters + 48, mode);
  memory.copyIn(Parameters + 52, game, strlen(game) + 1);
  memory.copyIn(Parameters + 68, data, strlen(data) + 1);
  memory.copyIn(Parameters + 88, "A Game -Final Mix-", 19);
  memory.copyIn(Parameters + 216, "Install Data", 13);
  memory.copyIn(Parameters + 344, "Deleting this will cause\nload times to increase.", 49);
  memory.write(1, Parameters + 1368, 3);
  memory.write(4, Parameters + 1372, 0xcccc'cccc);
  memory.write(4, Parameters + 1376, 0x1111'1111);
  memory.write(4, Parameters + 1380, 0x2222'2222);
}

auto readFile(const fs::path& path) -> std::string {
  std::ifstream file(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

//A PARAM.SFO's value by its key (its bytes as long as its length, its format), empty if it isn't there.
auto sfoField(const std::string& sfo, const std::string& key, u32* format = nullptr) -> std::string {
  auto word = [&](u64 at) -> u32 {
    if(at + 4 > sfo.size()) return 0;
    return u8(sfo[at]) | u8(sfo[at + 1]) << 8 | u8(sfo[at + 2]) << 16 | u32(u8(sfo[at + 3])) << 24;
  };
  for(u64 entry = 20; entry < 20 + u64(word(16)) * 16 && entry + 16 <= sfo.size(); entry += 16) {
    u64 name = u64(word(8)) + (word(entry) & 0xffff), at = u64(word(12)) + word(entry + 12), length = word(entry + 4);
    if(name >= sfo.size() || sfo.compare(name, key.size() + 1, key.c_str(), key.size() + 1)) continue;
    if(format) *format = word(entry) >> 16;
    return at <= sfo.size() ? sfo.substr(at, length) : std::string{};
  }
  return {};
}

auto sfoText(const std::string& sfo, const std::string& key) -> std::string {
  auto value = sfoField(sfo, key);
  return value.substr(0, value.find('\0'));
}

//Every file in a host folder and below, by its path in it, with its bytes.
auto tree(const fs::path& folder) -> std::map<std::string, std::string> {
  std::map<std::string, std::string> files;
  std::error_code error;
  for(fs::recursive_directory_iterator at(folder, error), end; !error && at != end; at.increment(error)) {
    if(at->is_regular_file()) files[fs::relative(at->path(), folder).generic_string()] = readFile(at->path());
  }
  return files;
}

//A byte file beside the Android app's tests, which read the same bytes (android/app/src/test/resources).
auto golden(const char* name) -> fs::path {
  return fs::path(__FILE__).parent_path() / "../../android/app/src/test/resources" / name;
}

//The disc's INSDIR installed whole in a folder: its files, byte for byte, and PARAM.SFO.
auto installed(const fs::path& folder) -> bool {
  return readFile(folder / "ICON0.PNG") == text(Icon) && readFile(folder / "PIC1.PNG") == text(Picture)
      && readFile(folder / "DATA.BIN") == text(Data) && fs::is_regular_file(folder / "EMPTY.DAT")
      && fs::file_size(folder / "EMPTY.DAT") == 0 && !fs::exists(folder / "EXTRA") && !fs::exists(folder / "NOT.BIN")
      && fs::is_regular_file(folder / "PARAM.SFO");
}
}

//An install from start to end, in both sizes: started, running after 200 ms (Abort refused while it starts, and a
//second start refused), each Update copying a piece and writing the progress (39 and 79 after the first two of the
//data's two and a half pieces, the third copying the rest and finishing: 100, installed); the files byte for byte the
//disc's, INSDIR's folder left out, the empty file made; PARAM.SFO from the parameters (TITLE, SAVEDATA_TITLE,
//SAVEDATA_DETAIL, PARENTAL_LEVEL, CATEGORY "MS", SAVEDATA_DIRECTORY, an empty SAVEDATA_FILE_LIST, SAVEDATA_PARAMS);
//the words after the progress left as the game had them; nothing of the copy left beside the saves. Finished, an
//Update does nothing more and Abort is refused; it shuts down in 40 ms. The savedata utility then finds the save
//(the size mode Kingdom Hearts asks first: 0). The trace's two lines.
static auto installWhole() -> void {
  for(u32 size : {0x590u, 0x598u}) {
    InstallMachine m;
    CHECK(m.status(), 0x8011'0005);  //(no dialog yet: the wrong type)
    installParameters(m, size, 0, "ULJM99999", "DATA");
    CHECK(m.call("sceUtilityGamedataInstallInitStart", {Parameters}), 0);
    CHECK(m.call("sceUtilityGamedataInstallInitStart", {Parameters}), 0x8011'0001);
    CHECK(m.call("sceUtilityGamedataInstallAbort", {}), 0x8011'0001);  //(not running yet)
    CHECK(m.update(), 0x8011'0001);
    CHECK(m.status(), 1);
    m.advance(199 * Millisecond);
    CHECK(m.status(), 1);
    m.advance(Millisecond);
    CHECK(m.status(), 2);
    CHECK(m.update(), 0);
    CHECK(m.status(), 2);
    CHECK(m.progress(), Piece * 100 / Total);
    CHECK(fs::exists(m.saved("ULJM99999DATA")), false);  //(nothing there until it's all there)
    CHECK(m.update(), 0);
    CHECK(m.status(), 2);
    CHECK(m.progress(), 2 * Piece * 100 / Total);
    CHECK(m.update(), 0);
    CHECK(m.status(), 3);
    CHECK(m.progress(), 100);
    CHECK(m.result(), 0);
    CHECK(m.system.memory.read(4, Parameters + 1376), 0x1111'1111);
    CHECK(m.system.memory.read(4, Parameters + 1380), 0x2222'2222);
    auto folder = m.saved("ULJM99999DATA");
    CHECK(installed(folder), true);
    CHECK(tree(m.saved("")).size(), 5);  //(its five files, and nothing else among the saves)
    CHECK(fs::exists(m.copies()), false);
    auto sfo = readFile(folder / "PARAM.SFO");
    CHECK(sfoText(sfo, "TITLE") == "A Game -Final Mix-", true);
    CHECK(sfoText(sfo, "SAVEDATA_TITLE") == "Install Data", true);
    CHECK(sfoText(sfo, "SAVEDATA_DETAIL") == "Deleting this will cause\nload times to increase.", true);
    u32 format = 0;
    CHECK(sfoField(sfo, "PARENTAL_LEVEL", &format) == std::string("\3\0\0\0", 4) && format == 0x0404, true);
    CHECK(sfoText(sfo, "CATEGORY") == "MS", true);
    CHECK(sfoText(sfo, "SAVEDATA_DIRECTORY") == "ULJM99999DATA", true);
    auto list = sfoField(sfo, "SAVEDATA_FILE_LIST");
    CHECK(list.size() == 99 * 32 && list.find_first_not_of('\0') == std::string::npos, true);
    CHECK(sfoField(sfo, "SAVEDATA_PARAMS") == std::string(128, '\0'), true);
    CHECK(m.update(), 0);  //(finished: nothing more to do)
    CHECK(m.status(), 3);
    CHECK(m.call("sceUtilityGamedataInstallAbort", {}), 0x8011'0001);
    CHECK(m.result(), 0);
    CHECK(m.close(), true);
    CHECK(m.update(), 0x8011'0001);
    CHECK(installed(folder), true);
    //the size mode, as Kingdom Hearts asks it before its install: the save there now
    auto& memory = m.system.memory;
    memory.fill(Parameters, 0, 1536);
    memory.write(4, Parameters, 1536);
    memory.write(4, Parameters + 48, 22);
    memory.copyIn(Parameters + 60, "ULJM99999", 10);
    memory.copyIn(Parameters + 76, "DATA", 5);
    CHECK(m.call("sceUtilitySavedataInitStart", {Parameters}), 0);
    m.advance(200 * Millisecond);
    CHECK(m.call("sceUtilitySavedataUpdate", {1}), 0);
    CHECK(m.result(), 0);
    CHECK(m.traced.size(), 3);
    if(m.traced.size() == 3) {
      CHECK(m.traced[0] == "gamedata install: mode 0, size " + std::to_string(size) + ", game \"ULJM99999\", data "
            "\"DATA\", title \"Install Data\", 4 files " + std::to_string(Total) + " bytes from disc0:/PSP_GAME/INSDIR, "
            "\"ULJM99999DATA\" not there", true);
      CHECK(m.traced[1] == "gamedata install: \"ULJM99999DATA\" -> 0x00000000 (installed), 4 files "
            + std::to_string(Total) + " bytes copied", true);
    }
  }
}

//The disc as a host folder standing for disc0: (a program's own folder, as the desktop's homebrew runs): INSDIR's
//files from it, in their names' order, the same install.
static auto installFromFolder() -> void {
  InstallMachine m;
  m.kernel.disc = nullptr;
  HostFolder disc;
  for(auto& file : installFiles()) disc.put(file.path, text(file.data));
  m.kernel.mount("disc0", disc.path.string());
  installParameters(m, 0x598, 0, "ULJM99999", "DATA");
  CHECK(m.start(), true);
  CHECK(m.finish(), 3);
  CHECK(m.result(), 0);
  CHECK(installed(m.saved("ULJM99999DATA")), true);
  CHECK(fs::exists(m.copies()), false);
  CHECK(m.close(), true);
}

//Each answer: nothing to install (no INSDIR, or nothing but a folder in it) cancelled, as the dialog answered before;
//no memory stick 0x80111901; modes of 2 and more 0x80111908 (one of -1 installs, as 0 does); a game's or data's name
//that isn't one plain name, or that make a folder's name ending in '.' or ' ' (which FAT and Windows would cut to
//another's), or a file where the folder goes, 0x80111904 (the stick couldn't be written), the file left as it was; and
//an install larger than the memory stick's free space 0x80111902. Each at the first Update, before anything is copied.
static auto installAnswers() -> void {
  auto answer = [](InstallMachine& m, u32 updates) -> u32 {
    if(!m.start() || m.finish() != updates) return 0xdead'dead;
    u32 result = m.result();
    return m.close() ? result : 0xdead'dead;
  };
  {
    InstallMachine m;
    m.kernel.disc = discFrom(disc_image::makeIso({{"PSP_GAME/USRDIR/OTHER.BIN", pattern(100, 5)}}).bytes);
    installParameters(m, 0x598, 0, "ULJM99999", "DATA");
    CHECK(answer(m, 1), 1);
    m.kernel.disc = discFrom(disc_image::makeIso({{"PSP_GAME/INSDIR/EXTRA/NOT.BIN", pattern(100, 5)}}).bytes);
    CHECK(answer(m, 1), 1);
    m.kernel.disc = nullptr;
    CHECK(answer(m, 1), 1);
    CHECK(fs::exists(m.saved("")) || fs::exists(m.copies()), false);
  }
  {
    InstallMachine m;
    m.kernel.mount("ms0", "");
    installParameters(m, 0x598, 0, "ULJM99999", "DATA");
    CHECK(answer(m, 1), 0x8011'1901);
  }
  {
    InstallMachine m;
    installParameters(m, 0x598, 2, "ULJM99999", "DATA");
    CHECK(answer(m, 1), 0x8011'1908);
    installParameters(m, 0x598, 0x7fff'ffff, "ULJM99999", "DATA");
    CHECK(answer(m, 1), 0x8011'1908);
    CHECK(fs::exists(m.saved("")) || fs::exists(m.copies()), false);
    installParameters(m, 0x598, -1, "ULJM99999", "DATA");
    CHECK(answer(m, 3), 0);
    CHECK(installed(m.saved("ULJM99999DATA")), true);
  }
  {
    InstallMachine m;
    m.stick.put("PSP/OUTSIDE/KEEP.BIN", "kept");
    for(auto [game, data] : {std::pair{"../OUTSIDE", "DATA"}, {"ULJM99999", "../../OUTSIDE"}, {"ULJM99999", "A/B"},
                             {"", "DATA"}, {"ULJM:9999", "DATA"}, {"ULJM99999", "DATA."}, {"ULJM99999", "DATA "},
                             {"ULJM99999 ", ""}, {"ULJM99999", "..."}}) {
      installParameters(m, 0x598, 0, game, data);
      CHECK(answer(m, 1), 0x8011'1904);
    }
    CHECK(tree(m.stick.path).size() == 1 && m.stick.get("PSP/OUTSIDE/KEEP.BIN") == "kept", true);
    CHECK(fs::exists(m.copies()), false);
    m.stick.put("PSP/SAVEDATA/ULJM99999DATA", "a file, not a folder");
    installParameters(m, 0x598, 0, "ULJM99999", "DATA");
    CHECK(answer(m, 1), 0x8011'1904);
    CHECK(m.stick.get("PSP/SAVEDATA/ULJM99999DATA") == "a file, not a folder", true);
    CHECK(fs::exists(m.copies()), false);
  }
  {
    //a disc whose INSDIR names a file of 0x7fffffff bytes (more than the stick's 57344 free clusters of 32 KiB)
    auto files = installFiles();
    files.push_back({"PSP_GAME/INSDIR/HUGE.BIN", pattern(16, 6)});
    auto image = disc_image::makeIso(files);
    u8* records = image.bytes.data() + u64(image.sectors["PSP_GAME/INSDIR"]) * 2048;
    for(u32 at = 0; at < 2048 && records[at]; at += records[at]) {
      if(records[at + 32] == 10 && !memcmp(records + at + 33, "HUGE.BIN;1", 10)) {
        disc_image::putBoth32(records + at + 10, 0x7fff'ffff);
      }
    }
    InstallMachine m;
    m.kernel.disc = discFrom(image.bytes);
    installParameters(m, 0x598, 0, "ULJM99999", "DATA");
    CHECK(answer(m, 1), 0x8011'1902);
    CHECK(fs::exists(m.saved("")) || fs::exists(m.copies()), false);
    std::string sized = "5 files " + std::to_string(Total + 0x7fff'ffff) + " bytes";
    CHECK(m.traced.size() == 1 && m.traced[0].find(sized) != std::string::npos
          && m.traced[0].find("-> 0x80111902 (no space)") != std::string::npos, true);
  }
}

//An install cut short leaves the saves as they were. The game aborting it after a piece: cancelled (1), finished,
//nothing among the saves and the copy taken away; started again, it installs whole. The machine reset after a piece:
//the same. Phobos killed after two pieces (the memory stick as the copy had left it then, copied aside): the save's
//folder isn't there, only the copy, in the stick's copies' folder; the next boot (the stick put in) takes it away, the
//saves as they were. A copy put back while a machine runs: the size mode says no data for it (no half folder a game
//would take for its install); an install of another game's data takes it away, and so does the same data's, which
//installs whole. A copy that can't be taken away (a folder in it its owner can't write, where permissions hold): the
//install gives up at its first Update, nothing among the saves; once it can be, the next install takes it away.
static auto installInterrupted() -> void {
  {
    InstallMachine m;
    m.stick.put("PSP/SAVEDATA/ULJM99999SAVE/DATA.BIN", "a save");
    installParameters(m, 0x598, 0, "ULJM99999", "DATA");
    CHECK(m.start() && !m.update() && m.status() == 2, true);
    CHECK(fs::exists(m.copies() / "ULJM99999DATA" / "DATA.BIN"), true);
    CHECK(m.call("sceUtilityGamedataInstallAbort", {}), 0);
    CHECK(m.status(), 3);
    CHECK(m.result(), 1);
    CHECK(m.call("sceUtilityGamedataInstallAbort", {}), 0x8011'0001);
    CHECK(fs::exists(m.saved("ULJM99999DATA")) || fs::exists(m.copies()), false);
    CHECK(m.close(), true);
    CHECK(m.start() && m.finish() == 3 && m.result() == 0, true);
    CHECK(installed(m.saved("ULJM99999DATA")), true);
    CHECK(m.stick.get("PSP/SAVEDATA/ULJM99999SAVE/DATA.BIN") == "a save", true);
    CHECK(m.close(), true);
  }
  {
    InstallMachine m;
    installParameters(m, 0x598, 0, "ULJM99999", "DATA");
    CHECK(m.start() && !m.update() && m.status() == 2, true);
    m.kernel.power();
    CHECK(fs::exists(m.saved("ULJM99999DATA")) || fs::exists(m.copies()), false);
  }
  HostFolder left;  //(the memory stick as the kill left it)
  {
    InstallMachine killed;
    installParameters(killed, 0x598, 0, "ULJM99999", "DATA");
    CHECK(killed.start() && !killed.update() && !killed.update() && killed.status() == 2, true);
    auto copy = tree(killed.stick.path);
    CHECK(copy.size() == 1 && copy[".phobos-install/ULJM99999DATA/DATA.BIN"].size() == 2 * Piece, true);
    std::error_code error;
    fs::copy(killed.stick.path, left.path, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    CHECK(error.value(), 0);
  }
  HostFolder stick;
  auto putBack = [&] {
    std::error_code error;
    fs::copy(left.path, stick.path, fs::copy_options::recursive | fs::copy_options::overwrite_existing, error);
    return !error && tree(stick.path / ".phobos-install").size() == 1;
  };
  stick.put("PSP/SAVEDATA/ULJM99999SAVE/DATA.BIN", "a save");
  CHECK(putBack(), true);
  InstallMachine m;
  m.kernel.mount("ms0", stick.path.string());  //(the next boot)
  auto saves = tree(stick.path);
  CHECK(saves.size() == 1 && saves["PSP/SAVEDATA/ULJM99999SAVE/DATA.BIN"] == "a save", true);
  CHECK(putBack(), true);
  auto& memory = m.system.memory;
  memory.fill(Parameters, 0, 1536);
  memory.write(4, Parameters, 1536);
  memory.write(4, Parameters + 48, 22);
  memory.copyIn(Parameters + 60, "ULJM99999", 10);
  memory.copyIn(Parameters + 76, "DATA", 5);
  CHECK(m.call("sceUtilitySavedataInitStart", {Parameters}), 0);
  m.advance(200 * Millisecond);
  CHECK(m.call("sceUtilitySavedataUpdate", {1}), 0);
  CHECK(m.result(), 0x8011'0327);  //(no data)
  CHECK(m.call("sceUtilitySavedataShutdownStart", {}), 0);
  m.advance(200 * Millisecond);
  installParameters(m, 0x598, 0, "ULJM88888", "DATA");
  CHECK(m.start() && m.finish() == 3 && m.result() == 0 && m.close(), true);
  CHECK(installed(stick.path / "PSP/SAVEDATA/ULJM88888DATA"), true);
  CHECK(fs::exists(stick.path / ".phobos-install"), false);
  CHECK(putBack(), true);
  installParameters(m, 0x598, 0, "ULJM99999", "DATA");
  CHECK(m.start() && m.finish() == 3 && m.result() == 0 && m.close(), true);
  CHECK(installed(stick.path / "PSP/SAVEDATA/ULJM99999DATA"), true);
  CHECK(fs::exists(stick.path / ".phobos-install"), false);
  CHECK(stick.get("PSP/SAVEDATA/ULJM99999SAVE/DATA.BIN") == "a save", true);
  if(geteuid() == 0) return;  //(permissions don't hold for root: nothing can't be taken away)
  stick.put(".phobos-install/OLD/LOCKED/FILE.BIN", "can't be taken away");
  fs::permissions(stick.path / ".phobos-install/OLD/LOCKED", fs::perms::owner_read | fs::perms::owner_exec);
  fs::remove_all(stick.path / "PSP/SAVEDATA/ULJM99999DATA");
  auto before = tree(stick.path / "PSP");
  CHECK(m.start() && m.finish() == 1 && m.result() == 0x8011'1904 && m.close(), true);
  CHECK(tree(stick.path / "PSP") == before, true);
  CHECK(stick.get(".phobos-install/OLD/LOCKED/FILE.BIN") == "can't be taken away", true);
  fs::permissions(stick.path / ".phobos-install/OLD/LOCKED", fs::perms::owner_all);
  CHECK(m.start() && m.finish() == 3 && m.result() == 0 && m.close(), true);
  CHECK(installed(stick.path / "PSP/SAVEDATA/ULJM99999DATA"), true);
  CHECK(fs::exists(stick.path / ".phobos-install"), false);
}

//The copies' folder at the memory stick's top is the install's own: while an install copies into it, programs can't see
//it (sceIoDopen of ms0:/ leaves it out) or reach it, whatever its case and however the path is written (sceIoGetstat,
//sceIoOpen to read or to make a file, sceIoMkdir, sceIoRemove, sceIoRename to it or from it, sceIoDopen, from the
//working folder too, with dots or spaces at its name's end, and through links to it at the top and in PSP: not
//found), and the install goes on and installs whole. A link put in its place, to a folder off
//the stick: the stick put in takes the link away and nothing behind it; one put there while the machine runs makes
//the next install give up (0x80111904) with nothing behind it touched. Links inside it, to a folder and a file off the
//stick: the stick put in, and an install's start, take the links away and never what they lead to.
static auto installCopiesFolder() -> void {
  constexpr u32 Entry = 0x0893'0000;
  InstallMachine m;
  m.stick.put("PSP/GAME/HB/EBOOT.PBP", "homebrew");
  m.stick.put("NOTE.TXT", "a note");
  installParameters(m, 0x598, 0, "ULJM99999", "DATA");
  CHECK(m.start() && !m.update() && m.status() == 2, true);
  CHECK(fs::exists(m.copies() / "ULJM99999DATA" / "DATA.BIN"), true);
  std::vector<std::string> top;
  u32 listing = m.call("sceIoDopen", {m.string("ms0:/")});
  while(m.call("sceIoDread", {listing, Entry}) == 1) top.push_back(m.system.memory.readString(Entry + 88, 256));
  CHECK(m.call("sceIoDclose", {listing}), 0);
  CHECK(top == std::vector<std::string>({"NOTE.TXT", "PSP"}), true);
  for(auto path : {"ms0:/.phobos-install", "ms0:/.PHOBOS-INSTALL/ULJM99999DATA/DATA.BIN", "ms0:/PSP/../.phobos-install",
                   "fatms0:/.Phobos-Install/ULJM99999DATA"}) {
    CHECK(m.call("sceIoGetstat", {m.string(path), Entry}), Kernel::ErrorFileNotFound);
  }
  std::string copied = "ms0:/.phobos-install/ULJM99999DATA/DATA.BIN";
  CHECK(m.call("sceIoOpen", {m.string(copied), 0x0001, 0}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoOpen", {m.string(copied), 0x0602, 0777}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/.phobos-install/NEW.BIN"), 0x0602, 0777}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoMkdir", {m.string("ms0:/.phobos-install/X"), 0777}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoMkdir", {m.string("ms0:/.phobos-install"), 0777}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoRemove", {m.string(copied)}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoRmdir", {m.string("ms0:/.phobos-install/ULJM99999DATA")}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoRename", {m.string("ms0:/NOTE.TXT"), m.string("ms0:/.phobos-install")}),
        Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoRename", {m.string("ms0:/.phobos-install"), m.string("ms0:/OUT")}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoDopen", {m.string("ms0:/.phobos-install")}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoChdir", {m.string("ms0:/")}), 0);
  CHECK(m.call("sceIoGetstat", {m.string(".phobos-install/ULJM99999DATA"), Entry}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoMkdir", {m.string("ms0:/.phobos-install."), 0777}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/.Phobos-Install. ./ULJM99999DATA"), Entry}), Kernel::ErrorFileNotFound);
  {
    std::error_code error;
    fs::create_directory_symlink(".phobos-install", m.stick.path / "ALIAS", error);
    fs::create_directory_symlink("../.phobos-install", m.stick.path / "PSP/INTO", error);
    CHECK(error.value(), 0);
    for(auto path : {"ms0:/ALIAS/ULJM99999DATA/DATA.BIN", "ms0:/PSP/INTO/ULJM99999DATA", "ms0:/ALIAS"}) {
      CHECK(m.call("sceIoGetstat", {m.string(path), Entry}), Kernel::ErrorFileNotFound);
    }
    CHECK(m.call("sceIoRemove", {m.string("ms0:/ALIAS/ULJM99999DATA/DATA.BIN")}), Kernel::ErrorFileNotFound);
    CHECK(m.call("sceIoDopen", {m.string("ms0:/PSP/INTO")}), Kernel::ErrorFileNotFound);
    fs::remove(m.stick.path / "ALIAS", error);
    fs::remove(m.stick.path / "PSP/INTO", error);
  }
  CHECK(m.stick.get("NOTE.TXT") == "a note" && fs::exists(m.copies() / "ULJM99999DATA" / "DATA.BIN"), true);
  CHECK(m.finish() == 2 && m.result() == 0 && m.close(), true);
  CHECK(installed(m.saved("ULJM99999DATA")), true);
  HostFolder outside;
  outside.put("KEEP.BIN", "off the stick");
  auto linkedInPlace = [&] {
    std::error_code error;
    fs::create_directory_symlink(outside.path, m.copies(), error);
    return !error;
  };
  CHECK(linkedInPlace(), true);
  m.kernel.mount("ms0", m.stick.path.string());
  CHECK(fs::exists(fs::symlink_status(m.copies())), false);
  CHECK(tree(outside.path).size() == 1 && outside.get("KEEP.BIN") == "off the stick", true);
  CHECK(linkedInPlace(), true);
  CHECK(m.start() && m.finish() == 1 && m.result() == 0x8011'1904 && m.close(), true);
  CHECK(tree(outside.path).size() == 1 && outside.get("KEEP.BIN") == "off the stick", true);
  CHECK(installed(m.saved("ULJM99999DATA")), true);
  {
    std::error_code error;
    fs::remove(m.copies(), error);
    CHECK(error.value(), 0);
  }
  //links inside the copies' folder, to a folder and to a file off the stick: the stick put in, and an install's start,
  //take the links away, never what they lead to
  auto linked = [&] {
    std::error_code error;
    fs::create_directories(m.copies() / "OLD", error);
    fs::create_directory_symlink(outside.path, m.copies() / "OLD" / "FOLDER", error);
    fs::create_symlink(outside.path / "KEEP.BIN", m.copies() / "FILE.BIN", error);
    return fs::is_symlink(m.copies() / "OLD" / "FOLDER", error) && fs::is_symlink(m.copies() / "FILE.BIN", error);
  };
  CHECK(linked(), true);
  m.kernel.mount("ms0", m.stick.path.string());
  CHECK(fs::exists(fs::symlink_status(m.copies())), false);
  CHECK(tree(outside.path).size() == 1 && outside.get("KEEP.BIN") == "off the stick", true);
  CHECK(linked(), true);
  CHECK(m.start() && m.finish() == 3 && m.result() == 0 && m.close(), true);
  CHECK(fs::exists(fs::symlink_status(m.copies())), false);
  CHECK(tree(outside.path).size() == 1 && outside.get("KEEP.BIN") == "off the stick", true);
  CHECK(installed(m.saved("ULJM99999DATA")), true);
}

//The copy changed behind the install's back between Updates, by the host (no program can reach it), as a tool keeping
//the memory stick in step with another device's might change it: the data file being copied taken away, cut short to
//4 KiB, or a link to a file off the stick put in its place, or such a link where a file is yet to go; on a disc of a
//small file and a large one, the small file (copied whole at the first Update) taken away or cut short before the
//last; and, on a disc whose first file fills the first Update, the copy's own folder, or the copies' folder it's in,
//put in place of by a link to a folder off the stick. Each time the install gives up with 0x80111904 (the stick
//couldn't be written) rather than install short or write through the link: nothing among the saves, no copy left,
//nothing off the stick written or taken away; the next install installs whole. And the game aborting it, or the
//machine reset, after another leftover was put in the copies' folder, or a link to a folder off the stick holding a
//folder of the copy's name was put in its place: no copies' folder left, and nothing behind the link touched.
static auto installCopyChanged() -> void {
  HostFolder outside;
  outside.put("KEEP.BIN", "off the stick");
  auto untouched = [&] { return tree(outside.path).size() == 1 && outside.get("KEEP.BIN") == "off the stick"; };
  auto copied = [](InstallMachine& m, const char* name) { return m.copies() / "ULJM99999DATA" / name; };
  struct Change {
    std::function<bool(InstallMachine&)> make;
    u32 updates;  //the Updates after the first until it gives up
  };
  std::vector<Change> changes = {
    {[&](InstallMachine& m) {
       std::error_code error;
       return fs::remove(copied(m, "DATA.BIN"), error);
     }, 1},
    {[&](InstallMachine& m) {
       std::error_code error;
       fs::resize_file(copied(m, "DATA.BIN"), 4096, error);
       return !error;
     }, 1},
    {[&](InstallMachine& m) {
       std::error_code error;
       fs::remove(copied(m, "DATA.BIN"), error);
       fs::create_symlink(outside.path / "KEEP.BIN", copied(m, "DATA.BIN"), error);
       return !error;
     }, 1},
    {[&](InstallMachine& m) {
       std::error_code error;
       fs::create_symlink(outside.path / "KEEP.BIN", copied(m, "ICON0.PNG"), error);
       return !error;
     }, 2},
  };
  for(auto& change : changes) {
    InstallMachine m;
    installParameters(m, 0x598, 0, "ULJM99999", "DATA");
    CHECK(m.start() && !m.update() && m.status() == 2, true);
    CHECK(change.make(m), true);
    CHECK(m.finish() == change.updates && m.result() == 0x8011'1904, true);
    CHECK(fs::exists(m.saved("ULJM99999DATA")) || fs::exists(fs::symlink_status(m.copies())), false);
    CHECK(untouched(), true);
    CHECK(m.close(), true);
    CHECK(m.start() && m.finish() == 3 && m.result() == 0 && m.close(), true);
    CHECK(installed(m.saved("ULJM99999DATA")), true);
  }
  for(bool cut : {false, true}) {
    InstallMachine m;
    m.kernel.disc = discFrom(disc_image::makeIso({{"PSP_GAME/INSDIR/A.BIN", pattern(1000, 7)},
                                                   {"PSP_GAME/INSDIR/B.BIN", pattern(Piece * 2, 8)}}).bytes);
    installParameters(m, 0x598, 0, "ULJM99999", "DATA");
    CHECK(m.start() && !m.update() && m.status() == 2, true);
    std::error_code error;
    CHECK(fs::file_size(copied(m, "A.BIN"), error), 1000);
    if(cut) fs::resize_file(copied(m, "A.BIN"), 10, error);
    else fs::remove(copied(m, "A.BIN"), error);
    CHECK(error.value(), 0);
    CHECK(m.finish() == 2 && m.result() == 0x8011'1904, true);
    CHECK(fs::exists(m.saved("ULJM99999DATA")) || fs::exists(fs::symlink_status(m.copies())), false);
    CHECK(m.close(), true);
  }
  for(bool own : {true, false}) {
    HostFolder off;
    off.put("ULJM99999DATA/KEEP.BIN", "off the stick");
    InstallMachine m;
    m.kernel.disc = discFrom(disc_image::makeIso({{"PSP_GAME/INSDIR/A.BIN", pattern(Piece, 9)},
                                                   {"PSP_GAME/INSDIR/B.BIN", pattern(1000, 10)}}).bytes);
    installParameters(m, 0x598, 0, "ULJM99999", "DATA");
    CHECK(m.start() && !m.update() && m.status() == 2, true);
    auto swapped = own ? m.copies() / "ULJM99999DATA" : m.copies();
    std::error_code error;
    fs::remove_all(swapped, error);
    fs::create_directory_symlink(own ? off.path / "ULJM99999DATA" : off.path, swapped, error);
    CHECK(error.value(), 0);
    CHECK(m.finish() == 1 && m.result() == 0x8011'1904, true);
    CHECK(tree(off.path).size() == 1 && off.get("ULJM99999DATA/KEEP.BIN") == "off the stick", true);
    CHECK(fs::exists(m.saved("ULJM99999DATA")) || fs::exists(fs::symlink_status(m.copies())), false);
    CHECK(m.close(), true);
  }
  for(bool link : {false, true}) {
    for(bool reset : {false, true}) {
      HostFolder off;
      off.put("ULJM99999DATA/KEEP.BIN", "off the stick");
      InstallMachine m;
      installParameters(m, 0x598, 0, "ULJM99999", "DATA");
      CHECK(m.start() && !m.update() && m.status() == 2, true);
      std::error_code error;
      if(link) {
        fs::remove_all(m.copies(), error);
        fs::create_directory_symlink(off.path, m.copies(), error);
      } else {
        m.stick.put(".phobos-install/OLD/STALE.BIN", "left");
      }
      CHECK(error.value(), 0);
      if(reset) m.kernel.power();
      else CHECK(!m.call("sceUtilityGamedataInstallAbort", {}) && m.status() == 3 && m.result() == 1, true);
      CHECK(fs::exists(fs::symlink_status(m.copies())), false);
      CHECK(tree(off.path).size() == 1 && off.get("ULJM99999DATA/KEEP.BIN") == "off the stick", true);
      CHECK(fs::exists(m.saved("ULJM99999DATA")), false);
      if(!reset) CHECK(m.close(), true);
    }
  }
}

//A state saved part way through the copy: the copy isn't in it, so the state round-trips as any does; loaded (into
//another machine with the same memory stick and disc), the copy is taken away and the install starts again at the
//next Update (its progress from the start), then installs whole; the machine it was saved from, loading it too, the
//same.
static auto installStates() -> void {
  InstallMachine m;
  installParameters(m, 0x598, 0, "ULJM99999", "DATA");
  CHECK(m.start() && !m.update() && !m.update() && m.status() == 2, true);
  CHECK(m.progress(), 2 * Piece * 100 / Total);
  auto prepare = [&](KernelMachine& fresh) {
    fresh.kernel.mount("ms0", m.stick.path.string());
    fresh.kernel.disc = discFrom(disc_image::makeIso(installFiles()).bytes);
  };
  CHECK(roundTrip(m, prepare), true);
  auto state = saveState(m);
  KernelMachine fresh;
  prepare(fresh);
  CHECK(loadState(fresh, state), true);
  CHECK(fresh.call("sceUtilityGamedataInstallGetStatus", {}), 2);
  CHECK(fresh.call("sceUtilityGamedataInstallUpdate", {1}), 0);
  CHECK(fresh.system.memory.read(4, Parameters + 1372), Piece * 100 / Total);
  CHECK(fresh.call("sceUtilityGamedataInstallUpdate", {1}), 0);
  CHECK(fresh.call("sceUtilityGamedataInstallGetStatus", {}), 2);
  CHECK(fresh.call("sceUtilityGamedataInstallUpdate", {1}), 0);
  CHECK(fresh.call("sceUtilityGamedataInstallGetStatus", {}), 3);
  CHECK(fresh.system.memory.read(4, Parameters + 28), 0);
  CHECK(installed(m.saved("ULJM99999DATA")), true);
  CHECK(fs::exists(m.copies()), false);
  fs::remove_all(m.saved(""));
  CHECK(loadState(m, state), true);
  CHECK(m.status() == 2 && m.finish() == 3 && m.result() == 0, true);
  CHECK(installed(m.saved("ULJM99999DATA")), true);
  CHECK(fs::exists(m.copies()), false);
}

//An install over the data's folder already there (Kingdom Hearts asks first, "Install data for this game already
//exists. Overwrite it?"), here a save the savedata utility made (MAKEDATASECURE: its DATA.BIN named secure in its
//PARAM.SFO): INSDIR's files written over (a longer DATA.BIN there cut to the disc's), PARAM.SFO's titles and the
//request's other keys replaced and its own kept (SAVEDATA_FILE_LIST still naming DATA.BIN), a file of the folder's own
//left as it was; installed. Other saves untouched, byte for byte. A PARAM.SFO there that isn't one is made afresh.
static auto installOver() -> void {
  InstallMachine m;
  auto& memory = m.system.memory;
  constexpr u32 Buffer = 0x0893'0000;
  memory.fill(Parameters, 0, 1536);
  memory.write(4, Parameters, 1536);
  memory.write(4, Parameters + 48, 13);
  memory.copyIn(Parameters + 60, "ULJM99999", 10);
  memory.copyIn(Parameters + 76, "DATA", 5);
  memory.copyIn(Parameters + 100, "DATA.BIN", 9);
  memory.copyIn(Buffer, "an older install's", 18);
  memory.write(4, Parameters + 116, Buffer);
  memory.write(4, Parameters + 120, 0x1000);
  memory.write(4, Parameters + 124, 18);
  memory.copyIn(Parameters + 128, "Old title", 10);
  CHECK(m.call("sceUtilitySavedataInitStart", {Parameters}), 0);
  m.advance(200 * Millisecond);
  CHECK(m.call("sceUtilitySavedataUpdate", {1}), 0);
  CHECK(m.result(), 0);
  CHECK(m.call("sceUtilitySavedataShutdownStart", {}), 0);
  m.advance(200 * Millisecond);
  auto folder = m.saved("ULJM99999DATA");
  CHECK(sfoText(readFile(folder / "PARAM.SFO"), "TITLE") == "Old title", true);
  m.stick.put("PSP/SAVEDATA/ULJM99999DATA/DATA.BIN", text(pattern(Piece * 3, 9)));
  m.stick.put("PSP/SAVEDATA/ULJM99999DATA/ICON0.PNG", "an older icon");
  m.stick.put("PSP/SAVEDATA/ULJM99999DATA/KEEP.TXT", "the folder's own");
  m.stick.put("PSP/SAVEDATA/ULJM99999SAVE/DATA.BIN", "a save");
  m.stick.put("PSP/SAVEDATA/ULUS10000DATA/DATA.BIN", "another game's");
  auto before = tree(m.saved(""));
  installParameters(m, 0x598, 0, "ULJM99999", "DATA");
  CHECK(m.start() && m.finish() == 3 && m.result() == 0 && m.close(), true);
  CHECK(installed(folder), true);
  CHECK(m.stick.get("PSP/SAVEDATA/ULJM99999DATA/KEEP.TXT") == "the folder's own", true);
  CHECK(fs::file_size(folder / "DATA.BIN"), Data.size());
  auto sfo = readFile(folder / "PARAM.SFO");
  CHECK(sfoText(sfo, "TITLE") == "A Game -Final Mix-", true);
  CHECK(sfoText(sfo, "SAVEDATA_TITLE") == "Install Data", true);
  CHECK(sfoText(sfo, "SAVEDATA_DIRECTORY") == "ULJM99999DATA", true);
  CHECK(sfoField(sfo, "SAVEDATA_FILE_LIST").substr(0, 9) == std::string("DATA.BIN\0", 9), true);
  auto after = tree(m.saved(""));
  for(auto name : {"ULJM99999SAVE/DATA.BIN", "ULUS10000DATA/DATA.BIN"}) CHECK(after[name] == before[name], true);
  CHECK(after.size(), before.size() + 2);  //(EMPTY.DAT and PIC1.PNG added; DATA.BIN, ICON0.PNG, PARAM.SFO replaced)
  CHECK(fs::exists(m.copies()), false);
  m.stick.put("PSP/SAVEDATA/ULJM99999DATA/PARAM.SFO", "not an SFO");
  installParameters(m, 0x598, 0, "ULJM99999", "DATA");
  CHECK(m.start() && m.finish() == 3 && m.result() == 0 && m.close(), true);
  sfo = readFile(folder / "PARAM.SFO");
  CHECK(sfoText(sfo, "TITLE") == "A Game -Final Mix-" && sfoText(sfo, "CATEGORY") == "MS", true);
  CHECK(sfoField(sfo, "SAVEDATA_FILE_LIST").find_first_not_of('\0'), std::string::npos);
}

//With a front end that shows the dialogs: in mode 1 the PSP's progress screen, a request (kind 7) made afresh as the
//progress moves on, its focus the progress and its one save the install (the data's name, the titles, the bytes it
//copies, INSDIR's icon, whether the folder's there), which takes no answer; taken down as it finishes. Its byte form
//against the file the Android app's tests read (PSP_WRITE_DIALOG_BYTES=1 writes it instead). In mode 0, nothing
//shown, as a PSP shows nothing (Kingdom Hearts draws its own).
static auto installShown() -> void {
  InstallMachine m(true);
  installParameters(m, 0x598, 1, "ULJM99999", "DATA");
  CHECK(m.start(), true);
  CHECK(m.kernel.dialogShownID(), 0);
  CHECK(m.update(), 0);
  auto first = m.kernel.dialogShown();
  CHECK(first.id != 0 && first.kind == 7 && first.mode == 1 && first.focus == Piece * 100 / Total, true);
  CHECK(first.circle && first.language == 1 && first.saves.size() == 1, true);
  if(first.saves.size() == 1) {
    auto& save = first.saves[0];
    CHECK(save.name == "DATA" && save.title == "A Game -Final Mix-" && save.saveTitle == "Install Data", true);
    CHECK(save.detail == "Deleting this will cause\nload times to increase.", true);
    CHECK(save.size == Total && !save.there && save.icon == Icon, true);
  }
  auto bytes = Kernel::dialogBytes(first);
  if(std::getenv("PSP_WRITE_DIALOG_BYTES")) {
    std::ofstream(golden("psp-dialog-install.bin"), std::ios::binary).write((const char*)bytes.data(), bytes.size());
  }
  std::ifstream stream(golden("psp-dialog-install.bin"), std::ios::binary);
  CHECK(std::vector<u8>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>()) == bytes, true);
  Answer answer;
  answer.id = first.id, answer.button = 1, answer.choice = 0;
  CHECK(m.kernel.dialogAnswer(answer), false);
  CHECK(m.update(), 0);
  auto second = m.kernel.dialogShown();
  CHECK(second.id != first.id && second.focus == 2 * Piece * 100 / Total, true);
  CHECK(m.update(), 0);
  CHECK(m.status() == 3 && m.result() == 0 && m.kernel.dialogShownID() == 0, true);
  CHECK(m.close(), true);
  installParameters(m, 0x598, 0, "ULJM99999", "DATA");
  CHECK(m.start(), true);
  for(u32 n = 0; n < 3; n++) {
    CHECK(m.update(), 0);
    CHECK(m.kernel.dialogShownID(), 0);
  }
  CHECK(m.status() == 3 && m.result() == 0, true);
}

auto installTests() -> Tests {
  return {
    {"game data install: INSDIR installed, its statuses, progress and PARAM.SFO", installWhole},
    {"game data install: from a host folder standing for the disc", installFromFolder},
    {"game data install: each answer", installAnswers},
    {"game data install: cut short by the game, a reset, Phobos killed", installInterrupted},
    {"game data install: the copies' folder, hidden from programs, links in its place", installCopiesFolder},
    {"game data install: its copy changed behind its back", installCopyChanged},
    {"game data install: a state saved part way", installStates},
    {"game data install: over an install there", installOver},
    {"game data install: the progress screen shown by a front end", installShown},
  };
}

}
