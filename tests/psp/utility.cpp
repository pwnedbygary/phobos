//The system's utilities (ares/psp/kernel/utility.cpp): a dialog's statuses and their timing, saves made, loaded,
//sized, listed, erased and deleted in a memory stick folder (names that would reach elsewhere and buffers outside
//the program's memory refused), their files listed secure, normal and the system's, their PARAM.SFO written and read
//back, saves without one (v2.0.0's) still loading, a game's save beside its own files through memstick:, list loads
//and saves answered with no front end as a PSP's dialog would be, each request traced for a host, the memory
//stick's free space told alike everywhere, a message answered yes, the keyboard's fields entered (an empty one given
//the nickname), network settings cancelled, the wrong type and the wrong status refused; a game's data install
//cancelled; optional modules loaded and unloaded; the nickname. Called directly, the clock moved on by hand.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 Parameters = 0x0892'0000, Buffer = 0x0893'0000, Free = 0x0894'0000, Used = 0x0894'0100;
constexpr u32 Needed = 0x0894'0200, List = 0x0894'0300, Entries = 0x0894'0400;
constexpr u32 FileList = 0x0896'0000, FileEntries = 0x0896'1000, Stat = 0x0897'0000;

//A SceUtilitySavedataParam for (mode, game, save, file, the data's size), its answers' structures pointed at, the
//sizes mode's save to measure (msData's names) the same save.
auto saveParameters(KernelMachine& m, u32 mode, const char* game, const char* save, u32 dataSize) -> void {
  m.system.memory.fill(Parameters, 0, 1536);
  m.system.memory.write(4, Parameters, 1536);
  m.system.memory.write(4, Parameters + 48, mode);
  m.system.memory.copyIn(Parameters + 60, game, strlen(game) + 1);
  m.system.memory.copyIn(Parameters + 76, save, strlen(save) + 1);
  m.system.memory.fill(Used, 0, 64);
  m.system.memory.copyIn(Used, game, strlen(game) + 1);
  m.system.memory.copyIn(Used + 16, save, strlen(save) + 1);
  m.system.memory.copyIn(Parameters + 100, "DATA.BIN", 9);
  m.system.memory.write(4, Parameters + 116, Buffer);
  m.system.memory.write(4, Parameters + 120, 0x1000);
  m.system.memory.write(4, Parameters + 124, dataSize);
  m.system.memory.write(4, Parameters + 1488, Free);
  m.system.memory.write(4, Parameters + 1492, Used);
  m.system.memory.write(4, Parameters + 1496, Needed);
  m.system.memory.write(4, Parameters + 1524, List);
  m.system.memory.write(4, List, 10);
  m.system.memory.write(4, List + 8, Entries);
}

//The data file's name in the parameters (its field is 13 bytes: a name as long has no NUL in it).
auto fileName(KernelMachine& m, const char* name) -> void {
  m.system.memory.fill(Parameters + 100, 0, 16);
  m.system.memory.copyIn(Parameters + 100, name, strlen(name));
}

//A whole savedata dialog: started, running after 200 ms, its work done by an update, closed 2 ms after it's shut
//down; its result.
auto runSave(KernelMachine& m) -> u32 {
  u64 millisecond = Kernel::CPUFrequency / 1000;
  CHECK(m.call("sceUtilitySavedataInitStart", {Parameters}), 0);
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 1);
  m.kernel.cycles += 199 * millisecond;
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 1);
  m.kernel.cycles += millisecond;
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 2);
  CHECK(m.call("sceUtilitySavedataUpdate", {1}), 0);
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 3);
  CHECK(m.call("sceUtilitySavedataShutdownStart", {}), 0);
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 4);
  m.kernel.cycles += 2 * millisecond;
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 0);
  return m.system.memory.read(4, Parameters + 28);
}

//The save's titles in the parameters' sfoParam: the title, the save's own, its detail, the parental level.
auto saveTitles(KernelMachine& m, const char* title, const char* saveTitle, const char* detail, u8 parental) -> void {
  m.system.memory.fill(Parameters + 128, 0, 1284);
  m.system.memory.copyIn(Parameters + 128, title, strlen(title));
  m.system.memory.copyIn(Parameters + 256, saveTitle, strlen(saveTitle));
  m.system.memory.copyIn(Parameters + 384, detail, strlen(detail));
  m.system.memory.write(1, Parameters + 1408, parental);
}

//The files mode's SceUtilitySavedataFileListInfo at FileList, the parameters pointing at it: the most of each kind to
//list (secure, normal, system), the counts 0xcccccccc, each kind's entries 0x800 apart from FileEntries, every
//entry's first two words 0x1337 and the rest 0, to see what's written.
auto fileList(KernelMachine& m, u32 secure, u32 normal, u32 system) -> void {
  auto& memory = m.system.memory;
  memory.write(4, Parameters + 1528, FileList);
  memory.fill(FileList + 12, 0xcc, 12);
  u32 most[] = {secure, normal, system};
  for(u32 kind : {0u, 1u, 2u}) {
    memory.write(4, FileList + kind * 4, most[kind]);
    memory.write(4, FileList + 24 + kind * 4, FileEntries + kind * 0x800);
    memory.fill(FileEntries + kind * 0x800, 0, 0x800);
    for(u32 at = FileEntries + kind * 0x800; at < FileEntries + (kind + 1) * 0x800 - 80; at += 80) {
      memory.write(4, at, 0x1337);
      memory.write(4, at + 4, 0x1337);
    }
  }
}

//A file the files mode listed: its kind (0 secure, 1 normal, 2 system) and place; its name.
auto listedFile(KernelMachine& m, u32 kind, u32 n) -> std::string {
  return m.system.memory.readString(FileEntries + kind * 0x800 + n * 80 + 64, 16);
}

//A PARAM.SFO's value by its key: its bytes (as long as its length), and its format and room; empty if it isn't there.
auto sfoField(const std::string& sfo, const std::string& key, u32* format = nullptr, u32* room = nullptr)
-> std::string {
  auto word = [&](u64 at) -> u32 {
    if(at + 4 > sfo.size()) return 0;
    return u8(sfo[at]) | u8(sfo[at + 1]) << 8 | u8(sfo[at + 2]) << 16 | u32(u8(sfo[at + 3])) << 24;
  };
  for(u64 entry = 20; entry < 20 + u64(word(16)) * 16 && entry + 16 <= sfo.size(); entry += 16) {
    u64 name = u64(word(8)) + (word(entry) & 0xffff), at = u64(word(12)) + word(entry + 12), length = word(entry + 4);
    if(name >= sfo.size() || sfo.compare(name, key.size() + 1, key.c_str(), key.size() + 1)) continue;
    if(format) *format = word(entry) >> 16;
    if(room) *room = word(entry + 8);
    return at <= sfo.size() ? sfo.substr(at, length) : std::string{};
  }
  return {};
}

//The name in entry n of a SAVEDATA_FILE_LIST (32 bytes each, a name in the first 13); empty past the list's end.
auto listName(const std::string& list, u32 n) -> std::string {
  if(n * 32 + 32 > list.size()) return {};
  return {list.c_str() + n * 32, strnlen(list.c_str() + n * 32, 13)};
}
}

//Saves: none to load at first; one saved (its data file in PSP/SAVEDATA/<game><save>), loaded back, sized (the
//memory stick's free space, its size, what saving would take), listed, deleted, and none again.
static auto savedata() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 0x8011'0005);  //no dialog yet: the wrong type
  saveParameters(m, 0, "ULUS99999", "SLOT0", 0);  //autoload
  CHECK(runSave(m), 0x8011'0307);
  m.system.memory.copyIn(Buffer, "saved bytes", 11);
  saveParameters(m, 1, "ULUS99999", "SLOT0", 11);  //autosave
  CHECK(runSave(m), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN") == "saved bytes", true);
  m.system.memory.fill(Buffer, 0, 16);
  saveParameters(m, 2, "ULUS99999", "SLOT0", 0);  //load
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.readString(Buffer, 16) == "saved bytes", true);
  CHECK(m.system.memory.read(4, Parameters + 124), 11);
  saveParameters(m, 8, "ULUS99999", "SLOT0", 40000);  //sizes
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.read(4, Free), 32_KiB);
  CHECK(m.system.memory.read(4, Free + 4), Kernel::StickFreeClusters);
  CHECK(m.system.memory.read(4, Free + 8), Kernel::StickFreeClusters * 32);
  CHECK(m.system.memory.readString(Free + 12, 8) == "1 GB", true);
  CHECK(m.system.memory.readString(Used + 16, 20) == "SLOT0", true);
  CHECK(m.system.memory.read(4, Used + 36), 3);    //one cluster of data, its PARAM.SFO's, and the folder's
  CHECK(m.system.memory.read(4, Needed), 4);       //40000 bytes take two, its PARAM.SFO one, the folder one
  CHECK(m.system.memory.read(4, Needed + 4), 128);
  saveParameters(m, 11, "ULUS99999", "", 0);  //list
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.read(4, List + 4), 1);
  CHECK(m.system.memory.readString(Entries + 52, 20) == "SLOT0", true);
  saveParameters(m, 10, "ULUS99999", "SLOT0", 0);  //delete
  CHECK(runSave(m), 0);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0"), false);
  saveParameters(m, 16, "ULUS99999", "SLOT0", 0);  //read: none
  CHECK(runSave(m), 0x8011'0327);
  saveParameters(m, 8, "ULUS99999", "SLOT0", 0);
  CHECK(runSave(m), 0x8011'03c7);
  saveParameters(m, 10, "ULUS99999", "SLOT0", 0);
  CHECK(runSave(m), 0x8011'0347);
  //writing data (secure or not) into a save that isn't there: NO_DATA, nothing made (utility/savedata/
  //saveemptyfilename); into one made first, its file written
  for(u32 mode : {17u, 18u}) {
    saveParameters(m, mode, "ULUS99999", "SLOT1", 11);
    CHECK(runSave(m), 0x8011'0327);
    CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT1"), false);
    fileName(m, "");
    CHECK(runSave(m), 0x8011'0327);
    CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT1"), false);
  }
  saveParameters(m, 14, "ULUS99999", "SLOT1", 0);  //make data
  fileName(m, "");
  CHECK(runSave(m), 0);
  saveParameters(m, 17, "ULUS99999", "SLOT1", 11);
  CHECK(runSave(m), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT1/DATA.BIN") == "saved bytes", true);
}

//A save's names become a folder and a file on the host, so each must be one plain name, and the save's paths must be
//where a save belongs. Each of these reached elsewhere (a load read /etc/hosts into the program's memory; a delete
//with no game's name took every save, with ".." the memory stick's PSP folder, homebrew and all, with "../.." the
//memory stick itself; a save as the game "../GAME/HB" wrote over homebrew) and is refused now, as a bad parameter in
//the mode's own group of errors, with nothing on the memory stick or beside it touched; a save folder or data file
//that's a link to elsewhere is as if it weren't there. Names that are plain, if odd, still save.
static auto savedataNames() -> void {
  HostFolder outer;  //the memory stick is a folder in it, so a name reaching out of the stick lands in it
  outer.put("stick/PSP/GAME/HB/EBOOT.PBP", "homebrew");
  outer.put("stick/PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN", "saved");
  outer.put("stick/PSP/X", "on the stick");
  outer.put("X", "beside the stick");
  namespace fs = std::filesystem;
  auto savedata = outer.path / "stick/PSP/SAVEDATA";
  fs::create_directory_symlink(outer.path / "stick/PSP/GAME", savedata / "LINKSAVE");
  fs::create_directories(savedata / "ULUS99999SLOT1");
  fs::create_symlink(outer.path / "stick/PSP/GAME/HB/EBOOT.PBP", savedata / "ULUS99999SLOT1/DATA.BIN");
  fs::create_directories(savedata / "ULUS99999SLOT2");
  fs::create_symlink(outer.path / "X", savedata / "ULUS99999SLOT2/DATA.BIN");
  KernelMachine m;
  m.kernel.mount("ms0", (outer.path / "stick").string());
  auto untouched = [&] {
    return outer.get("stick/PSP/GAME/HB/EBOOT.PBP") == "homebrew" && outer.get("stick/PSP/X") == "on the stick"
        && outer.get("stick/PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN") == "saved" && outer.get("X") == "beside the stick";
  };
  auto refused = [&](u32 mode, const char* game, const char* save, const char* file, u32 error) {
    saveParameters(m, mode, game, save, 5);
    fileName(m, file);
    m.system.memory.copyIn(Buffer, "data!", 5);
    m.system.memory.fill(Buffer + 5, 0, 32);
    check(__LINE__, (std::string(game) + "|" + save + "|" + file).c_str(), runSave(m), error);
    check(__LINE__, "nothing read in", m.system.memory.readString(Buffer, 32) == "data!", true);
    check(__LINE__, "nothing touched", untouched(), true);
  };
  //the data file's name
  refused(0, "ULUS99999", "SLOT0", "../../../../X", 0x8011'0308);  //as long as its field
  refused(2, "ULUS99999", "SLOT0", "../../X", 0x8011'0308);
  refused(16, "ULUS99999", "SLOT0", "/etc/hosts", 0x8011'0328);
  refused(15, "ULUS99999", "SLOT0", "..", 0x8011'0328);
  refused(1, "ULUS99999", "SLOT0", "..\\..\\X", 0x8011'0388);
  refused(3, "ULUS99999", "SLOT0", "C:X", 0x8011'0388);
  refused(20, "ULUS99999", "SLOT0", "../DATA.BIN", 0x8011'0328);
  //data files that are links leading out of their folders, into the stick and out of it: as if not there
  refused(1, "ULUS99999", "SLOT1", "DATA.BIN", 0x8011'0385);
  refused(0, "ULUS99999", "SLOT1", "DATA.BIN", 0x8011'0309);
  refused(0, "ULUS99999", "SLOT2", "DATA.BIN", 0x8011'0309);
  refused(20, "ULUS99999", "SLOT2", "DATA.BIN", 0x8011'0329);
  CHECK(fs::is_symlink(savedata / "ULUS99999SLOT2/DATA.BIN"), true);
  //the game's and the save's names, the save's from a list too
  refused(1, "../GAME/HB", "", "EBOOT.PBP", 0x8011'0388);
  saveParameters(m, 5, "ULUS99999", "", 5);  //a save to a list's first name
  fileName(m, "EBOOT.PBP");
  m.system.memory.copyIn(Buffer + 0x100, "/../../GAME/HB", 15);
  m.system.memory.write(4, Parameters + 96, Buffer + 0x100);
  CHECK(runSave(m), 0x8011'0388);
  CHECK(untouched(), true);
  m.system.memory.write(1, Buffer + 0x100, 0);  //the list's first name empty
  saveParameters(m, 4, "ULUS99999", "", 5);
  m.system.memory.write(4, Parameters + 96, Buffer + 0x100);
  CHECK(runSave(m), 0x8011'0308);
  refused(8, "ULUS99999", ".", "", 0x8011'03c7);  //(no names of its own: msData's "." is no save there)
  refused(12, "ULUS99999SLOT0", "..", "", 0x8011'0328);
  refused(0, "ULUS999999999", "SLOT0", "DATA.BIN", 0x8011'0308);  //a game's name as long as its field
  refused(0, "ULUS99999", "SLOT0SLOT0SLOT0SLOT0", "DATA.BIN", 0x8011'0308);  //and a save's
  //plain names, if odd, still save and load
  m.system.memory.copyIn(Buffer, "odd", 3);
  saveParameters(m, 1, "ULUS99999", "SLOT 1.<>", 3);
  fileName(m, "ABCDEFGH.BIN");
  CHECK(runSave(m), 0);
  CHECK(outer.get("stick/PSP/SAVEDATA/ULUS99999SLOT 1.<>/ABCDEFGH.BIN") == "odd", true);
  m.system.memory.fill(Buffer, 0, 8);
  saveParameters(m, 0, "ULUS99999", "SLOT 1.<>", 0);
  fileName(m, "ABCDEFGH.BIN");
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.readString(Buffer, 8) == "odd", true);
  //deleting: with no game's name, "..", a save's name climbing out (either slash), "../..", and a save folder that's
  //a link leading out of SAVEDATA (as if not there)
  refused(10, "LINK", "SAVE", "", 0x8011'0347);
  CHECK(fs::exists(outer.path / "stick/PSP/GAME/HB"), true);
  refused(10, "", "", "", 0x8011'0348);
  refused(10, "..", "", "", 0x8011'0348);
  refused(6, "ULUS99999", "/../..", "", 0x8011'0348);
  refused(7, "ULUS99999", "SLOT0\\..\\..", "", 0x8011'0348);
  refused(9, "../..", "", "", 0x8011'0348);
}

//"<>" names no save: as the save's name, or as the list's choice in the list modes, the save's folder is the game's
//name alone, and a sizes request's msData "<>" measures that folder. No folder with "<>" in its name is made.
static auto savedataUnnamed() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  m.system.memory.copyIn(Buffer, "game alone", 10);
  saveParameters(m, 1, "ULUS99999", "<>", 10);  //autosave
  CHECK(runSave(m), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999/DATA.BIN") == "game alone", true);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999<>"), false);
  m.system.memory.fill(Buffer, 0, 16);
  saveParameters(m, 2, "ULUS99999", "<>", 0);  //load
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.readString(Buffer, 16) == "game alone", true);
  saveParameters(m, 8, "ULUS99999", "<>", 0);  //sizes: msData's "<>" is the same folder
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.read(4, Used + 36), 3);
  //a list save: "<>" as the save's name takes the list's first name, and "<>" there is the game's name alone
  m.system.memory.copyIn(Buffer, "from a list", 11);
  saveParameters(m, 5, "ULUS99999", "<>", 11);
  m.system.memory.copyIn(Buffer + 0x100, "SLOT9", 6);
  m.system.memory.write(4, Parameters + 96, Buffer + 0x100);
  CHECK(runSave(m), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT9/DATA.BIN") == "from a list", true);
  saveParameters(m, 5, "ULUS99998", "<>", 11);
  m.system.memory.copyIn(Buffer + 0x100, "<>", 3);
  m.system.memory.write(4, Parameters + 96, Buffer + 0x100);
  CHECK(runSave(m), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99998/DATA.BIN") == "from a list", true);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99998<>"), false);
}

//The program's buffer must be in its memory as far as what's copied in or out: a save from memory that isn't there
//(which wrote a file of zeros, of up to 4 GiB) or that runs past RAM's end, and a load or a read into such memory
//(which said it had read the file), are refused before anything is made or read. A load reads no more than the
//buffer takes, and tells how much it read.
static auto savedataBuffers() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  constexpr u32 RAMEnd = 0x0a00'0000;  //the test machine's 32 MiB
  saveParameters(m, 1, "ULUS99999", "SLOT0", 0x1000);
  m.system.memory.write(4, Parameters + 116, 0);  //nothing there
  CHECK(runSave(m), 0x8011'0388);
  saveParameters(m, 17, "ULUS99999", "SLOT0", 0x1000);
  m.system.memory.write(4, Parameters + 116, RAMEnd - 0x800);
  CHECK(runSave(m), 0x8011'0388);
  saveParameters(m, 1, "ULUS99999", "SLOT0", 0xffff'ffff);  //4 GiB from nothing
  m.system.memory.write(4, Parameters + 116, 0);
  m.system.memory.write(4, Parameters + 120, 0xffff'ffff);
  CHECK(runSave(m), 0x8011'0388);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0"), false);
  m.system.memory.copyIn(Buffer, "0123456789abcdef", 16);
  saveParameters(m, 1, "ULUS99999", "SLOT0", 16);
  CHECK(runSave(m), 0);
  saveParameters(m, 0, "ULUS99999", "SLOT0", 0);
  m.system.memory.write(4, Parameters + 116, 0);
  CHECK(runSave(m), 0x8011'0308);
  CHECK(m.system.memory.read(4, Parameters + 124), 0);  //nothing said to be read
  saveParameters(m, 16, "ULUS99999", "SLOT0", 0);
  m.system.memory.write(4, Parameters + 116, RAMEnd - 8);
  CHECK(runSave(m), 0x8011'0328);
  CHECK(m.system.memory.read(4, Parameters + 124), 0);
  saveParameters(m, 2, "ULUS99999", "SLOT0", 0);  //into 10 bytes: 10 read
  m.system.memory.write(4, Parameters + 120, 10);
  m.system.memory.fill(Buffer, 0, 16);
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.read(4, Parameters + 124), 10);
  CHECK(m.system.memory.readString(Buffer, 16) == "0123456789", true);
  saveParameters(m, 2, "ULUS99999", "SLOT0", 0);  //into the last 16 bytes of RAM: all of it
  m.system.memory.write(4, Parameters + 116, RAMEnd - 16);
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.read(4, Parameters + 124), 16);
  CHECK(m.system.memory.readString(RAMEnd - 16, 16) == "0123456789abcdef", true);
}

//The sizes and list modes read the memory stick without throwing: a link that loops (the host can't follow it) in a
//save's folder counts nothing, and one in SAVEDATA lists nothing.
static auto savedataLoops() -> void {
  HostFolder stick;
  stick.put("PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN", std::string(100, 'x'));
  std::filesystem::create_symlink("LOOP", stick.path / "PSP/SAVEDATA/ULUS99999SLOT0/LOOP");
  std::filesystem::create_symlink("ULUS99999LOOP", stick.path / "PSP/SAVEDATA/ULUS99999LOOP");
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto run = [&](u32 mode, const char* save) {
    saveParameters(m, mode, "ULUS99999", save, 0);
    try { return runSave(m); } catch(const std::exception&) { m.kernel.dialog.status = 0; return 0xffff'ffffu; }
  };
  CHECK(run(8, "SLOT0"), 0);
  CHECK(m.system.memory.read(4, Used + 36), 2);  //DATA.BIN's 100 bytes: a cluster, and the folder's
  CHECK(run(11, ""), 0);
  CHECK(m.system.memory.read(4, List + 4), 1);  //SLOT0 alone
  CHECK(m.system.memory.readString(Entries + 52, 20) == "SLOT0", true);
}

//Erasing (modes 19 and 20) takes the data file named and nothing else, and with none named, nothing; in the reads'
//group of errors (utility/savedata/deletebroken), one that isn't there is FILE_NOT_FOUND, and no save NO_DATA.
//Deleting a save (modes 6, 7, 9, 10 and 21) takes its folder and all it holds.
static auto savedataErase() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto folder = stick.path / "PSP/SAVEDATA/ULUS99999SLOT0";
  for(u32 mode : {19u, 20u}) {
    stick.put("PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN", "data");
    stick.put("PSP/SAVEDATA/ULUS99999SLOT0/OTHER.BIN", "other");
    saveParameters(m, mode, "ULUS99999", "SLOT0", 0);  //DATA.BIN
    CHECK(runSave(m), 0);
    CHECK(std::filesystem::exists(folder / "DATA.BIN"), false);
    CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/OTHER.BIN") == "other", true);
    saveParameters(m, mode, "ULUS99999", "SLOT0", 0);
    CHECK(runSave(m), 0x8011'0329);
    saveParameters(m, mode, "ULUS99999", "SLOT0", 0);
    fileName(m, "");
    CHECK(runSave(m), 0);
    CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/OTHER.BIN") == "other", true);
    saveParameters(m, mode, "ULUS99999", "NONE", 0);
    CHECK(runSave(m), 0x8011'0327);
  }
  for(u32 mode : {6u, 7u, 9u, 10u, 21u}) {
    stick.put("PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN", "data");
    saveParameters(m, mode, "ULUS99999", "SLOT0", 0);
    CHECK(runSave(m), 0);
    CHECK(std::filesystem::exists(folder), false);
    CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA"), true);
  }
}

//The files mode (12): a save's files in three lists, the secure ones (those the save modes and the secure data modes
//wrote), the normal ones and the system's (PARAM.SFO and the icons), each entry a SceIoStat's first 64 bytes (the word
//after the mode left as it was) and the name, and bind 1021, as utility/savedata/filelist recorded. Each list no
//longer than its most, and none without entries to fill; names with small letters (PPSSPP's notes), folders, links and
//names over 12 characters left out. Asking for more than 99 secure files, 8192 normal ones or (built with SDK 2.60 on)
//5 system ones is a bad parameter (PPSSPP's notes); no save is NO_DATA (deletedata), a PARAM.SFO that isn't one
//DATA_BROKEN (loadbroken), nothing written either way; with no list to fill, the answers are the same. The icons a save
//mode is given are written, but one whose buffer is smaller than its file or outside memory.
static auto savedataFiles() -> void {
  namespace fs = std::filesystem;
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  constexpr u32 RAMEnd = 0x0a00'0000;  //the test machine's 32 MiB
  memory.copyIn(Buffer, "0123456789abcdef", 16);
  saveParameters(m, 13, "ULUS99999", "SLOT0", 16);  //make data secure: DATA.BIN, and the icons
  for(auto [n, buffer, room, size] : std::initializer_list<std::tuple<u32, u32, u32, u32>>{
      {0, Buffer, 16, 4},        //ICON0.PNG: "0123"
      {1, Buffer, 4, 8},         //ICON1.PMF: 8 bytes in a 4-byte buffer: left out
      {2, RAMEnd - 8, 16, 16},   //PIC1.PNG: past memory's end: left out
      {3, Buffer + 4, 6, 6}}) {  //SND0.AT3: "456789"
    memory.write(4, Parameters + 1412 + n * 16, buffer);
    memory.write(4, Parameters + 1416 + n * 16, room);
    memory.write(4, Parameters + 1420 + n * 16, size);
  }
  CHECK(runSave(m), 0);
  auto save = stick.path / "PSP/SAVEDATA/ULUS99999SLOT0";
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/ICON0.PNG") == "0123", true);
  CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/SND0.AT3") == "456789", true);
  CHECK(fs::exists(save / "ICON1.PMF") || fs::exists(save / "PIC1.PNG"), false);
  saveParameters(m, 18, "ULUS99999", "SLOT0", 5);  //write data: OTHER.BIN, normal
  fileName(m, "OTHER.BIN");
  CHECK(runSave(m), 0);
  saveParameters(m, 17, "ULUS99999", "SLOT0", 7);  //write data secure: A.BIN
  fileName(m, "A.BIN");
  CHECK(runSave(m), 0);
  stick.put("PSP/SAVEDATA/ULUS99999SLOT0/lower.bin", "small letters");
  stick.put("PSP/SAVEDATA/ULUS99999SLOT0/ABCDEFGHI.BIN", "thirteen characters");
  fs::create_directories(save / "FOLDER");
  fs::create_symlink(save / "OTHER.BIN", save / "LINK");
  saveParameters(m, 12, "ULUS99999", "SLOT0", 0);
  fileList(m, 10, 10, 10);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Parameters + 52), 1021);
  CHECK(memory.read(4, FileList + 12), 2);
  CHECK(memory.read(4, FileList + 16), 1);
  CHECK(memory.read(4, FileList + 20), 3);
  CHECK(listedFile(m, 0, 0) == "A.BIN" && listedFile(m, 0, 1) == "DATA.BIN", true);
  CHECK(listedFile(m, 1, 0) == "OTHER.BIN", true);
  CHECK(listedFile(m, 2, 0) == "ICON0.PNG" && listedFile(m, 2, 1) == "PARAM.SFO", true);
  CHECK(listedFile(m, 2, 2) == "SND0.AT3", true);
  CHECK(memory.read(4, FileEntries + 0x800 + 80), 0x1337);  //nothing more
  //an entry: a file's mode, the next word as it was, the size, and the dates sceIoGetstat gives
  u32 entry = FileEntries + 80;  //DATA.BIN
  CHECK(memory.read(4, entry), 0x21ff);
  CHECK(memory.read(4, entry + 4), 0x1337);
  CHECK(memory.read(4, entry + 8), 16);
  CHECK(memory.read(4, entry + 12), 0);
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN"), Stat}), 0);
  for(u32 at : {16u, 20u, 24u, 28u, 48u, 52u, 56u, 60u}) CHECK(memory.read(4, entry + at), memory.read(4, Stat + at));
  CHECK(memory.read(4, FileEntries + 2 * 0x800 + 80 + 8), 4912);  //PARAM.SFO's size, as a PSP's
  //each list no longer than its most; with no entries to fill, none listed
  fileList(m, 1, 10, 10);
  memory.write(4, FileList + 28, 0);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, FileList + 12), 1);
  CHECK(listedFile(m, 0, 0) == "A.BIN", true);
  CHECK(memory.read(4, FileEntries + 80), 0x1337);
  CHECK(memory.read(4, FileList + 16), 0);
  CHECK(memory.read(4, FileList + 20), 3);
  //too many asked for: a bad parameter, nothing written
  for(auto [kind, most] : {std::pair{0u, 100u}, {1u, 8193u}}) {
    fileList(m, 10, 10, 10);
    memory.write(4, FileList + kind * 4, most);
    memory.write(4, Parameters + 52, 0);
    CHECK(runSave(m), 0x8011'0328);
    CHECK(memory.read(4, FileList + 12), 0xcccc'cccc);
    CHECK(memory.read(4, Parameters + 52), 0);
  }
  fileList(m, 99, 8192, 6);  //six system files: too many only for SDK 2.60 on
  CHECK(runSave(m), 0);
  m.kernel.sdkVersion = 0x0206'0010;
  CHECK(runSave(m), 0x8011'0328);
  fileList(m, 99, 8192, 5);
  CHECK(runSave(m), 0);
  memory.write(4, FileList + 8, 6);  //(but with no entries to fill, the most is no matter)
  memory.write(4, FileList + 32, 0);
  CHECK(runSave(m), 0);
  m.kernel.sdkVersion = 0;
  //no save: NO_DATA; a PARAM.SFO that isn't one: DATA_BROKEN; nothing written
  saveParameters(m, 12, "ULUS99999", "NONE", 0);
  fileList(m, 10, 10, 10);
  CHECK(runSave(m), 0x8011'0327);
  CHECK(memory.read(4, FileList + 12), 0xcccc'cccc);
  CHECK(memory.read(4, Parameters + 52), 0);
  stick.put("PSP/SAVEDATA/ULUS99999SLOT1/DATA.BIN", "data");
  stick.put("PSP/SAVEDATA/ULUS99999SLOT1/PARAM.SFO", std::string("\0PSF\1\1\0\0", 8));
  saveParameters(m, 12, "ULUS99999", "SLOT1", 0);
  fileList(m, 10, 10, 10);
  CHECK(runSave(m), 0x8011'0326);
  CHECK(memory.read(4, FileList + 12), 0xcccc'cccc);
  CHECK(memory.read(4, Parameters + 52), 0);
  //no list to fill (none given, or the smaller structure's): the same answers
  saveParameters(m, 12, "ULUS99999", "SLOT0", 0);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Parameters + 52), 1021);
  fileList(m, 10, 10, 10);
  memory.write(4, Parameters, 1500);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, FileList + 12), 0xcccc'cccc);
  saveParameters(m, 12, "ULUS99999", "NONE", 0);
  CHECK(runSave(m), 0x8011'0327);
}

//PARAM.SFO, written beside a save as a PSP writes it, in the disc's format (the disc's reader reads its title), its
//keys in order and 4912 bytes: CATEGORY "MS", PARENTAL_LEVEL, SAVEDATA_DETAIL, SAVEDATA_DIRECTORY (the folder's
//name), SAVEDATA_FILE_LIST naming the secure files (their hashes zeros), SAVEDATA_PARAMS (zeros), SAVEDATA_TITLE and
//TITLE, the titles from the request's sfoParam; a request with no title keeps the save's. Loads give the titles back
//in sfoParam (with no file named too, nothing read, 0); erasing a file takes its name out of the list, and writing it
//as a normal file; the list mode dates a save by its PARAM.SFO; deleting takes it with the save.
static auto savedataParamSFO() -> void {
  namespace fs = std::filesystem;
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  memory.copyIn(Buffer, "saved bytes", 11);
  saveParameters(m, 1, "ULUS99999", "SLOT0", 11);  //autosave, with no key: still secure
  saveTitles(m, "Game", "Chapter 1", "A detail\nin two lines", 3);
  CHECK(runSave(m), 0);
  auto sfo = stick.get("PSP/SAVEDATA/ULUS99999SLOT0/PARAM.SFO");
  CHECK(sfo.size(), 4912);
  if(sfo.size() != 4912) return;
  CHECK(sfo.compare(0, 8, std::string("\0PSF\1\1\0\0", 8)), 0);
  CHECK(ares::PlayStationPortable::sfoValue({sfo.begin(), sfo.end()}, "TITLE") == "Game", true);
  u32 format = 0, room = 0, at = 20;
  for(auto [key, kind, space] : std::initializer_list<std::tuple<const char*, u32, u32>>{
      {"CATEGORY", 0x0204, 4}, {"PARENTAL_LEVEL", 0x0404, 4}, {"SAVEDATA_DETAIL", 0x0204, 1024},
      {"SAVEDATA_DIRECTORY", 0x0204, 64}, {"SAVEDATA_FILE_LIST", 0x0004, 3168}, {"SAVEDATA_PARAMS", 0x0004, 128},
      {"SAVEDATA_TITLE", 0x0204, 128}, {"TITLE", 0x0204, 128}}) {
    u32 name = 148 + u8(sfo[at]) + u8(sfo[at + 1]) * 256;
    check(__LINE__, key, name < sfo.size() && sfo.compare(name, strlen(key) + 1, key, strlen(key) + 1) == 0, true);
    sfoField(sfo, key, &format, &room);
    check(__LINE__, key, format, kind);
    check(__LINE__, key, room, space);
    at += 16;
  }
  CHECK(sfoField(sfo, "CATEGORY") == std::string("MS\0", 3), true);
  CHECK(sfoField(sfo, "PARENTAL_LEVEL") == std::string("\3\0\0\0", 4), true);
  CHECK(sfoField(sfo, "SAVEDATA_DETAIL") == std::string("A detail\nin two lines\0", 22), true);
  CHECK(sfoField(sfo, "SAVEDATA_DIRECTORY") == std::string("ULUS99999SLOT0\0", 15), true);
  CHECK(sfoField(sfo, "SAVEDATA_TITLE") == std::string("Chapter 1\0", 10), true);
  CHECK(sfoField(sfo, "TITLE") == std::string("Game\0", 5), true);
  CHECK(sfoField(sfo, "SAVEDATA_PARAMS") == std::string(128, '\0'), true);
  auto list = sfoField(sfo, "SAVEDATA_FILE_LIST");
  CHECK(listName(list, 0) == "DATA.BIN" && list.find_first_not_of('\0', 8) == list.npos, true);
  //loaded: the titles in sfoParam, and bind 1021; with no file named, 0, the same, nothing read
  for(const char* file : {"DATA.BIN", ""}) {
    saveParameters(m, 0, "ULUS99999", "SLOT0", 0);
    fileName(m, file);
    memory.fill(Buffer, 0, 16);
    memory.fill(Parameters + 128, 0xcc, 1284);
    CHECK(runSave(m), 0);
    CHECK(memory.readString(Buffer, 16) == (*file ? "saved bytes" : ""), true);
    CHECK(memory.read(4, Parameters + 124), *file ? 11 : 0);
    CHECK(memory.readString(Parameters + 128, 128) == "Game", true);
    CHECK(memory.readString(Parameters + 256, 128) == "Chapter 1", true);
    CHECK(memory.readString(Parameters + 384, 1024) == "A detail\nin two lines", true);
    for(u32 end : {256u, 384u, 1408u}) CHECK(memory.read(4, Parameters + end - 4), 0);  //the rest of each cleared
    CHECK(memory.read(1, Parameters + 1408), 3);
    CHECK(memory.read(1, Parameters + 1409), 0xcc);
    CHECK(memory.read(4, Parameters + 52), 1021);
  }
  //writing data with no title keeps the save's titles, with one takes them; a normal file isn't listed
  saveParameters(m, 18, "ULUS99999", "SLOT0", 5);
  fileName(m, "OTHER.BIN");
  CHECK(runSave(m), 0);
  sfo = stick.get("PSP/SAVEDATA/ULUS99999SLOT0/PARAM.SFO");
  CHECK(sfoField(sfo, "SAVEDATA_TITLE") == std::string("Chapter 1\0", 10), true);
  CHECK(sfoField(sfo, "SAVEDATA_FILE_LIST").find("OTHER.BIN"), std::string::npos);
  saveParameters(m, 17, "ULUS99999", "SLOT0", 5);
  fileName(m, "SECURE.BIN");
  saveTitles(m, "Game", "Chapter 2", "Later", 0);
  CHECK(runSave(m), 0);
  sfo = stick.get("PSP/SAVEDATA/ULUS99999SLOT0/PARAM.SFO");
  CHECK(sfoField(sfo, "SAVEDATA_TITLE") == std::string("Chapter 2\0", 10), true);
  CHECK(sfoField(sfo, "PARENTAL_LEVEL") == std::string(4, '\0'), true);
  list = sfoField(sfo, "SAVEDATA_FILE_LIST");
  CHECK(listName(list, 0) == "DATA.BIN" && listName(list, 1) == "SECURE.BIN", true);
  //erased, a secure file's name goes from the list; written as a normal file, so does it (chosen)
  saveParameters(m, 19, "ULUS99999", "SLOT0", 0);
  CHECK(runSave(m), 0);
  list = sfoField(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/PARAM.SFO"), "SAVEDATA_FILE_LIST");
  CHECK(list.size() == 3168 && list.find("DATA.BIN") == list.npos && list.find("SECURE.BIN") == 32, true);
  saveParameters(m, 18, "ULUS99999", "SLOT0", 5);
  fileName(m, "SECURE.BIN");
  CHECK(runSave(m), 0);
  list = sfoField(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/PARAM.SFO"), "SAVEDATA_FILE_LIST");
  CHECK(list == std::string(3168, '\0'), true);
  //made, a non-secure save's file isn't listed either; a save with no file named still has its PARAM.SFO
  saveParameters(m, 14, "ULUS99999", "SLOT1", 11);
  CHECK(runSave(m), 0);
  CHECK(sfoField(stick.get("PSP/SAVEDATA/ULUS99999SLOT1/PARAM.SFO"), "SAVEDATA_FILE_LIST") == std::string(3168, '\0'),
        true);
  saveParameters(m, 3, "ULUS99999", "SLOT2", 0);
  fileName(m, "");
  CHECK(runSave(m), 0);
  CHECK(sfoField(stick.get("PSP/SAVEDATA/ULUS99999SLOT2/PARAM.SFO"), "CATEGORY") == std::string("MS\0", 3), true);
  //a full list (99 secure files, the PSP's most) takes no more: the hundredth is a normal file
  for(u32 n = 0; n < 100; n++) {
    char name[16];
    std::snprintf(name, sizeof(name), "F%02u.BIN", n);
    saveParameters(m, n ? 17 : 13, "ULUS99998", "FULL", 5);
    fileName(m, name);
    check(__LINE__, name, runSave(m), 0);
  }
  list = sfoField(stick.get("PSP/SAVEDATA/ULUS99998FULL/PARAM.SFO"), "SAVEDATA_FILE_LIST");
  CHECK(listName(list, 0) == "F00.BIN" && listName(list, 98) == "F98.BIN" && list.find("F99.BIN") == list.npos, true);
  constexpr u32 Many = 0x0898'0000;  //(room for 99 entries)
  saveParameters(m, 12, "ULUS99998", "FULL", 0);
  fileList(m, 99, 10, 0);
  memory.write(4, FileList + 24, Many);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, FileList + 12) == 99 && memory.read(4, FileList + 16) == 1, true);
  CHECK(memory.readString(Many + 98 * 80 + 64, 16) == "F98.BIN" && listedFile(m, 1, 0) == "F99.BIN", true);
  //the list mode: each save dated by its PARAM.SFO (the dates sceIoGetstat gives it), one without by its folder
  auto older = [&](const fs::path& path, int hours) {
    fs::last_write_time(path, fs::last_write_time(path) - std::chrono::hours(hours));
  };
  older(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0/PARAM.SFO", 48);
  fs::create_directories(stick.path / "PSP/SAVEDATA/ULUS99999SLOT3");
  older(stick.path / "PSP/SAVEDATA/ULUS99999SLOT3", 72);
  saveParameters(m, 11, "ULUS99999", "", 0);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, List + 4), 4);
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/PSP/SAVEDATA/ULUS99999SLOT0/PARAM.SFO"), Stat}), 0);
  for(u32 at = 0; at < 16; at += 4) CHECK(memory.read(4, Entries + 36 + at), memory.read(4, Stat + 48 + at));
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/PSP/SAVEDATA/ULUS99999SLOT3"), Stat}), 0);
  for(u32 at = 0; at < 16; at += 4) CHECK(memory.read(4, Entries + 3 * 72 + 36 + at), memory.read(4, Stat + 48 + at));
  CHECK(memory.read(2, Entries + 36) != memory.read(2, Entries + 3 * 72 + 36) || memory.read(2, Entries + 40) !=
        memory.read(2, Entries + 3 * 72 + 40), true);
  //deleting takes PARAM.SFO with the save
  saveParameters(m, 10, "ULUS99999", "SLOT0", 0);
  CHECK(runSave(m), 0);
  CHECK(fs::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0"), false);
}

//A save without PARAM.SFO is v2.0.0's, from before Phobos wrote them (a PSP would call it broken): it still loads,
//every load and read taking its data as before, the titles in the parameters left as they were; the files mode
//lists its files as normal ones (no PARAM.SFO names any secure); erasing works; and a save mode writing into it
//gives it its PARAM.SFO, naming the file it writes.
static auto savedataOlder() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  stick.put("PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN", "older save");
  stick.put("PSP/SAVEDATA/ULUS99999SLOT0/OTHER.BIN", "other");
  for(u32 mode : {0u, 2u, 4u, 15u, 16u}) {
    saveParameters(m, mode, "ULUS99999", "SLOT0", 0);
    saveTitles(m, "Kept", "Kept too", "", 1);
    memory.fill(Buffer, 0, 16);
    check(__LINE__, "mode", runSave(m), 0);
    check(__LINE__, "data", memory.readString(Buffer, 16) == "older save", true);
    check(__LINE__, "titles", memory.readString(Parameters + 128, 128) == "Kept", true);
    check(__LINE__, "titles", memory.readString(Parameters + 256, 128) == "Kept too", true);
    check(__LINE__, "level", memory.read(1, Parameters + 1408), 1);
  }
  saveParameters(m, 12, "ULUS99999", "SLOT0", 0);
  fileList(m, 10, 10, 10);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, FileList + 12), 0);
  CHECK(memory.read(4, FileList + 16), 2);
  CHECK(memory.read(4, FileList + 20), 0);
  CHECK(listedFile(m, 1, 0) == "DATA.BIN" && listedFile(m, 1, 1) == "OTHER.BIN", true);
  saveParameters(m, 20, "ULUS99999", "SLOT0", 0);
  fileName(m, "OTHER.BIN");
  CHECK(runSave(m), 0);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0/OTHER.BIN"), false);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULUS99999SLOT0/PARAM.SFO"), false);
  memory.copyIn(Buffer, "new", 3);
  saveParameters(m, 17, "ULUS99999", "SLOT0", 3);
  fileName(m, "NEW.BIN");
  CHECK(runSave(m), 0);
  auto list = sfoField(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/PARAM.SFO"), "SAVEDATA_FILE_LIST");
  CHECK(listName(list, 0) == "NEW.BIN" && list.find_first_not_of('\0', 7) == list.npos, true);
  saveParameters(m, 12, "ULUS99999", "SLOT0", 0);
  fileList(m, 10, 10, 10);
  CHECK(runSave(m), 0);
  CHECK(listedFile(m, 0, 0) == "NEW.BIN" && listedFile(m, 1, 0) == "DATA.BIN" && listedFile(m, 2, 0) == "PARAM.SFO",
        true);
}

//Colin McRae Rally 2005 Plus's save, its calls as the game makes them (traced): memstick: assigned to the memory
//stick's file system; at boot, its save looked for through it, CMR5TS.DAT opened for reading asynchronously and the
//request polled: FILE_NOT_FOUND (none there, not NO_SUCH_DEVICE); an autosave (mode 1) in the parameters' second size
//(1500 bytes), OPTIONS.DAT named but no data given, with ICON0 and PIC1, making the save's folder, PARAM.SFO and
//icons; the game's own files opened in the folder through memstick:, each asynchronously and polled, TIMES.DAT and
//CMR5TS.DAT made (write, create, truncate), then TIMES.DAT again (read, write, create); the autosave again with
//OPTIONS.DAT's data. On the next boot (another machine on the same stick) CMR5TS.DAT is opened twice, the same way,
//the autoload (mode 0, no titles or icons given) reads OPTIONS.DAT, and TIMES.DAT is opened once. What the files are
//given and read here is the test's (sceIoWrite, sceIoRead), as are the checks after the second boot's calls:
//memstick: whatever its case, its files' status and folders, a rename to ms0: (the same device), a relative open from
//a working folder on it, and memstick: with no sceIoAssign at all (the stick all the same: deviceName()'s choice).
static auto savedataMemstick() -> void {
  constexpr u32 Icon0 = 0x0898'0000, Pic1 = 0x0898'1000, Polled = 0x0898'2000;
  HostFolder stick;
  std::string folder = "memstick:/PSP/SAVEDATA/ULES001110000/";
  auto request = [&](KernelMachine& m, u32 mode, u32 buffer, u32 size) {
    auto& memory = m.system.memory;
    saveParameters(m, mode, "ULES00111", "0000", size);
    memory.write(4, Parameters, 1500);
    memory.write(4, Parameters + 56, mode == 1);  //overwrite
    fileName(m, "OPTIONS.DAT");
    memory.write(4, Parameters + 116, buffer);
    memory.write(4, Parameters + 120, buffer ? 0x1000 : 0);
  };
  auto autosave = [&](KernelMachine& m, u32 buffer, u32 size) {
    auto& memory = m.system.memory;
    request(m, 1, buffer, size);
    saveTitles(m, "Colin McRae Rally 2005 plus", "", "Save game for Colin McRae Rally 2005 Plus", 2);
    memory.copyIn(Icon0, "ICON0", 5);
    memory.copyIn(Pic1, "PIC1", 4);
    for(auto [at, data, length] : {std::tuple{1412u, Icon0, 5u}, std::tuple{1444u, Pic1, 4u}}) {
      memory.write(4, Parameters + at, data);
      memory.write(4, Parameters + at + 4, length);
      memory.write(4, Parameters + at + 8, length);
    }
  };
  //an asynchronous open, its request polled once it's done: the request's result (the file, or why it wasn't opened)
  auto openAsync = [&](KernelMachine& m, const std::string& path, u32 flags) -> u32 {
    u32 file = m.call("sceIoOpenAsync", {m.string(path), flags, 0x1ff});
    CHECK(file >= 3 && file < 0x8000'0000, true);
    m.kernel.cycles = m.kernel.files[file].asyncDoneAt;
    m.kernel.events();
    m.system.memory.fill(Polled, 0xcc, 8);
    CHECK(m.call("sceIoPollAsync", {file, Polled}), 0);
    return m.system.memory.read(4, Polled);
  };
  auto write = [&](KernelMachine& m, u32 file, const char* text) {
    m.system.memory.copyIn(Buffer, text, strlen(text));
    CHECK(m.call("sceIoWrite", {file, Buffer, u32(strlen(text))}), strlen(text));
    CHECK(m.call("sceIoClose", {file}), 0);
  };
  auto read = [&](KernelMachine& m, u32 file) -> std::string {
    m.system.memory.fill(Buffer, 0, 16);
    u32 count = m.call("sceIoRead", {file, Buffer, 16});
    CHECK(m.call("sceIoClose", {file}), 0);
    return count <= 16 ? m.system.memory.readString(Buffer, count) : std::string{};
  };
  auto open = [&](KernelMachine& m, const std::string& path) { return m.call("sceIoOpen", {m.string(path), 1, 0}); };
  auto assign = [&](KernelMachine& m) {
    CHECK(m.call("sceIoAssign", {m.string("memstick:"), m.string("msstor0p1:/"), m.string("fatms0:"), 0, 0, 0}), 0);
  };
  {
    KernelMachine m;
    m.kernel.mount("ms0", stick.path.string());
    assign(m);
    CHECK(openAsync(m, folder + "CMR5TS.DAT", 0x0001), Kernel::ErrorFileNotFound);
    CHECK(m.system.memory.read(4, Polled + 4), 0xffff'ffff);
    autosave(m, 0, 0);
    CHECK(runSave(m), 0);
    CHECK(stick.get("PSP/SAVEDATA/ULES001110000/ICON0.PNG") == "ICON0", true);
    CHECK(stick.get("PSP/SAVEDATA/ULES001110000/PIC1.PNG") == "PIC1", true);
    CHECK(sfoField(stick.get("PSP/SAVEDATA/ULES001110000/PARAM.SFO"), "TITLE").starts_with("Colin McRae"), true);
    CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULES001110000/OPTIONS.DAT"), true);  //no data: empty
    CHECK(stick.get("PSP/SAVEDATA/ULES001110000/OPTIONS.DAT").empty(), true);
    write(m, openAsync(m, folder + "TIMES.DAT", 0x0602), "times");
    write(m, openAsync(m, folder + "CMR5TS.DAT", 0x0602), "cmr5");
    CHECK(read(m, openAsync(m, folder + "TIMES.DAT", 0x0203)) == "times", true);
    m.system.memory.copyIn(Buffer, "options", 7);
    autosave(m, Buffer, 7);
    CHECK(runSave(m), 0);
    CHECK(stick.get("PSP/SAVEDATA/ULES001110000/OPTIONS.DAT") == "options", true);
    CHECK(stick.get("PSP/SAVEDATA/ULES001110000/TIMES.DAT") == "times", true);
    CHECK(stick.get("PSP/SAVEDATA/ULES001110000/CMR5TS.DAT") == "cmr5", true);
  }
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  assign(m);
  for(u32 n = 0; n < 2; n++) CHECK(read(m, openAsync(m, folder + "CMR5TS.DAT", 0x0001)) == "cmr5", true);
  request(m, 0, Buffer, 0);  //the autoload
  m.system.memory.fill(Buffer, 0, 16);
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.readString(Buffer, 16) == "options", true);
  CHECK(m.system.memory.read(4, Parameters + 124), 7);
  CHECK(read(m, openAsync(m, folder + "TIMES.DAT", 0x0001)) == "times", true);
  CHECK(m.call("sceIoGetstat", {m.string(folder + "TIMES.DAT"), Stat}), 0);
  CHECK(m.system.memory.read(4, Stat + 8), 5);
  CHECK(read(m, open(m, "MEMSTICK:/PSP/SAVEDATA/ULES001110000/CMR5TS.DAT")) == "cmr5", true);
  u32 listing = m.call("sceIoDopen", {m.string(folder)});
  CHECK(listing >= 3 && listing < 0x8000'0000, true);
  CHECK(m.call("sceIoDclose", {listing}), 0);
  CHECK(m.call("sceIoRename", {m.string(folder + "TIMES.DAT"), m.string("ms0:/PSP/TIMES.BAK")}), 0);
  CHECK(stick.get("PSP/SAVEDATA/ULES001110000/TIMES.BAK") == "times", true);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/ULES001110000/TIMES.DAT"), false);
  CHECK(m.call("sceIoChdir", {m.string("memstick:/PSP/SAVEDATA/ULES001110000")}), 0);
  CHECK(read(m, open(m, "TIMES.BAK")) == "times", true);
  KernelMachine unassigned;
  unassigned.kernel.mount("ms0", stick.path.string());
  CHECK(read(unassigned, open(unassigned, folder + "CMR5TS.DAT")) == "cmr5", true);
}

//A list's load or save answered with no front end, naming no save of its own: a load takes what a PSP's dialog shows
//first, the focused save among those there (7th Dragon 2020 lists DATA00 to DATA49; its DATA03 loads), nothing there
//NO_DATA; a save the list's first name ("<>" the game's folder alone, whatever else of the game's is there). A loaded
//or saved name is written back into the request's save name, as a choice is (Brandish: The Dark Revenant's loads
//went on only with it). A request naming its save takes it; a list with no first name is refused, as before, a save
//making nothing and a load reading nothing, the game's folder there or not.
static auto savedataListAnswers() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  stick.put("PSP/SAVEDATA/NPJH50459DATA03/DATA.BIN", "third");
  stick.put("PSP/SAVEDATA/NPJH50459DATA05/DATA.BIN", "fifth");
  stick.put("PSP/SAVEDATA/NPJH50459OTHER/DATA.BIN", "other");
  auto listed = [&](u32 mode, const char* save, std::vector<const char*> names, u32 focus) {
    saveParameters(m, mode, "NPJH50459", save, mode == 5 ? 3 : 0);
    m.system.memory.fill(Buffer + 0x100, 0, 0x400);
    for(u32 n = 0; n < names.size(); n++) m.system.memory.copyIn(Buffer + 0x100 + n * 20, names[n], strlen(names[n]));
    m.system.memory.write(4, Parameters + 96, Buffer + 0x100);
    m.system.memory.write(4, Parameters + 1480, focus);
    m.system.memory.fill(Buffer, 0, 8);
  };
  auto saved = [&] { return m.system.memory.readString(Parameters + 76, 20); };
  std::vector<const char*> list = {"DATA00", "DATA01", "DATA02", "DATA03", "DATA04", "DATA05"};
  listed(4, "", list, 0);
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.readString(Buffer, 8) == "third" && saved() == "DATA03", true);
  listed(4, "<>", list, 2);  //the last there
  CHECK(runSave(m), 0);
  CHECK(m.system.memory.readString(Buffer, 8) == "fifth" && saved() == "DATA05", true);
  listed(4, "", {"DATA00", "DATA01"}, 0);
  CHECK(runSave(m), 0x8011'0307);
  CHECK(saved() == "", true);
  listed(4, "DATA01", list, 0);  //its own save, not there
  CHECK(runSave(m), 0x8011'0307);
  listed(4, "", {"<>"}, 0);  //the game's saves: DATA03 first
  CHECK(runSave(m), 0);
  CHECK(saved() == "DATA03", true);
  listed(5, "", {""}, 0);  //no first name
  m.system.memory.copyIn(Buffer, "new", 3);
  CHECK(runSave(m), 0x8011'0388);
  CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA/NPJH50459"), false);
  listed(5, "", {"DATA01", "DATA02"}, 7);  //the first name, whatever the focus
  m.system.memory.copyIn(Buffer, "new", 3);
  CHECK(runSave(m), 0);
  CHECK(stick.get("PSP/SAVEDATA/NPJH50459DATA01/DATA.BIN") == "new" && saved() == "DATA01", true);
  listed(5, "<>", {"<>"}, 0);  //the game's folder alone, DATA03 and the rest there
  m.system.memory.copyIn(Buffer, "own", 3);
  CHECK(runSave(m), 0);
  CHECK(stick.get("PSP/SAVEDATA/NPJH50459/DATA.BIN") == "own", true);
  CHECK(stick.get("PSP/SAVEDATA/NPJH50459DATA03/DATA.BIN") == "third", true);
  listed(4, "", {""}, 0);  //no first name, the game's folder there
  CHECK(runSave(m), 0x8011'0308);
  CHECK(m.system.memory.readString(Buffer, 8) == "", true);
}

//The savedata trace (the runner's --trace-savedata): each request a line, as it came, then its answer; nothing traced
//without a host taking it.
static auto savedataTrace() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  std::vector<std::string> lines;
  m.kernel.savedataTrace = [&](const std::string& text) { lines.push_back(text); };
  m.system.memory.copyIn(Buffer, "data", 4);
  saveParameters(m, 1, "ULUS99999", "SLOT0", 4);
  CHECK(runSave(m), 0);
  saveParameters(m, 4, "ULUS99999", "", 0);
  m.system.memory.fill(Buffer + 0x100, 0, 60);
  m.system.memory.copyIn(Buffer + 0x100, "SLOT9", 5);
  m.system.memory.copyIn(Buffer + 0x114, "SLOT0", 5);
  m.system.memory.write(4, Parameters + 96, Buffer + 0x100);
  CHECK(runSave(m), 0);
  saveParameters(m, 3, "ULUS99999", "A\"\x01", 4);
  CHECK(runSave(m), 0x8011'0388);
  saveParameters(m, 2, "ULUS99999", "SLOT1", 0);
  CHECK(runSave(m), 0x8011'0307);
  std::vector<std::string> expected = {
    "mode 1 (autosave), size 1536, game \"ULUS99999\", save \"SLOT0\", file \"DATA.BIN\", buffer 0x08930000 of 4096, "
    "data 4, \"ULUS99999SLOT0\" not there, title \"\" -> 0x00000000",
    "mode 4 (listload), size 1536, game \"ULUS99999\", save \"SLOT0\", list \"SLOT9\" \"SLOT0\", chosen \"SLOT0\", "
    "file \"DATA.BIN\", buffer 0x08930000 of 4096, data 0, \"ULUS99999SLOT0\" there -> 0x00000000, read 4, "
    "save \"SLOT0\"",
    "mode 3 (save), size 1536, game \"ULUS99999\", save \"A\\x22\\x01\", file \"DATA.BIN\", buffer 0x08930000 of 4096, "
    "data 4, \"ULUS99999A\\x22\\x01\" not there, title \"\" -> 0x80110388 (save: bad parameter)",
    "mode 2 (load), size 1536, game \"ULUS99999\", save \"SLOT1\", file \"DATA.BIN\", buffer 0x08930000 of 4096, "
    "data 0, \"ULUS99999SLOT1\" not there -> 0x80110307 (load: no data)"};
  CHECK(lines.size(), expected.size());
  for(u32 n = 0; n < std::min(lines.size(), expected.size()); n++) {
    if(lines[n] != expected[n]) std::printf("  trace line %u: %s\n", n, lines[n].c_str());
    CHECK(lines[n] == expected[n], true);
  }
  m.kernel.savedataTrace = nullptr;
  saveParameters(m, 2, "ULUS99999", "SLOT0", 0);
  CHECK(runSave(m), 0);
  CHECK(lines.size(), expected.size());
}

//A save missing a file, as utility/savedata/loadbroken and deletebroken recorded (a save made, then its data file
//taken away): loads FILE_NOT_FOUND (0x80110309), reads and erases the reads' (0x80110329); the files mode lists what's
//left; deleting takes it. One whose PARAM.SFO isn't one is broken: loads DATA_BROKEN (0x80110306), reads, erases and
//the files mode the reads' (0x80110326), its file there or not, and left there; deleting takes it. Broken too, and
//none of it kept, is a PARAM.SFO whose values' rooms reach past the file, or past its values' part all together, whose
//count is past the file, or whose key has no end: the next save mode writes it afresh.
static auto savedataBroken() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  auto make = [&](bool data, bool sfo) {
    memory.copyIn(Buffer, "0123456789abcdef", 16);
    saveParameters(m, 13, "TEST99901", "ABC", 16);
    CHECK(runSave(m), 0);
    if(!data) std::filesystem::remove(stick.path / "PSP/SAVEDATA/TEST99901ABC/DATA.BIN");
    if(!sfo) stick.put("PSP/SAVEDATA/TEST99901ABC/PARAM.SFO", "broken");
  };
  auto trial = [&](u32 mode, u32 answer, bool data, bool sfo) {
    make(data, sfo);
    saveParameters(m, mode, "TEST99901", "ABC", 16);
    check(__LINE__, "mode", runSave(m), answer);
  };
  for(u32 mode : {0u, 2u, 4u}) trial(mode, 0x8011'0309, false, true);
  for(u32 mode : {15u, 16u, 19u, 20u}) trial(mode, 0x8011'0329, false, true);
  make(false, true);
  saveParameters(m, 12, "TEST99901", "ABC", 0);
  fileList(m, 5, 5, 5);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, FileList + 12) == 0 && memory.read(4, FileList + 16) == 0, true);
  CHECK(memory.read(4, FileList + 20) == 1 && listedFile(m, 2, 0) == "PARAM.SFO", true);
  for(bool data : {false, true}) {
    for(u32 mode : {0u, 2u, 4u}) trial(mode, 0x8011'0306, data, false);
    for(u32 mode : {12u, 15u, 16u, 19u, 20u}) {
      trial(mode, 0x8011'0326, data, false);
      check(__LINE__, "left", std::filesystem::exists(stick.path / "PSP/SAVEDATA/TEST99901ABC/DATA.BIN"), data);
    }
    for(u32 mode : {9u, 10u, 21u}) {
      trial(mode, 0, data, false);
      check(__LINE__, "deleted", std::filesystem::exists(stick.path / "PSP/SAVEDATA/TEST99901ABC"), false);
    }
  }
  //written into, a broken PARAM.SFO is made afresh
  make(true, false);
  saveParameters(m, 17, "TEST99901", "ABC", 16);
  CHECK(runSave(m), 0);
  saveParameters(m, 15, "TEST99901", "ABC", 16);
  CHECK(runSave(m), 0);
  //PARAM.SFOs that only look like one: broken, and written afresh, 4912 bytes, by the next save mode
  make(true, true);
  auto clean = stick.get("PSP/SAVEDATA/TEST99901ABC/PARAM.SFO");
  CHECK(clean.size(), 4912);
  if(clean.size() != 4912) return;
  auto put = [](std::string& sfo, u32 at, u32 word) { for(u32 n = 0; n < 4; n++) sfo[at + n] = char(word >> 8 * n); };
  std::string noEnd = std::string("\0PSF\1\1\0\0", 8) + std::string(12, '\0') + std::string(16, '\0') + "....TITLE";
  put(noEnd, 8, 40), put(noEnd, 12, 36), put(noEnd, 16, 1), put(noEnd, 24, 4), put(noEnd, 28, 4);
  std::vector<std::pair<const char*, std::string>> crafted = {
    {"a room of 4 GiB", clean}, {"rooms past the values' part", clean}, {"a count past the file", clean},
    {"a key with no end", noEnd}};
  put(crafted[0].second, 20 + 8, 0xffff'f000);  //CATEGORY's room
  put(crafted[1].second, 20 + 8, 4912 - 264);   //CATEGORY's room the whole values' part, the others besides
  put(crafted[2].second, 16, 0xffff'ffff);
  for(auto& [name, sfo] : crafted) {
    stick.put("PSP/SAVEDATA/TEST99901ABC/PARAM.SFO", sfo);
    saveParameters(m, 0, "TEST99901", "ABC", 16);
    check(__LINE__, name, runSave(m), 0x8011'0306);
    saveParameters(m, 17, "TEST99901", "ABC", 16);
    check(__LINE__, name, runSave(m), 0);
    auto written = stick.get("PSP/SAVEDATA/TEST99901ABC/PARAM.SFO");
    check(__LINE__, name, written.size(), 4912);
    check(__LINE__, name, listName(sfoField(written, "SAVEDATA_FILE_LIST"), 0) == "DATA.BIN", true);
  }
  //an SFO, but too big to write again with a save's values (64 KiB, its one key's room the rest): it loads, and the
  //next save mode writes it afresh
  std::string big(64_KiB, '\0');
  big.replace(0, 8, std::string("\0PSF\1\1\0\0", 8));
  put(big, 8, 36), put(big, 12, 40), put(big, 16, 1);
  put(big, 20, 0x0004'0000), put(big, 24, 1), put(big, 28, 64_KiB - 40);  //"Z", raw bytes, 1 of them
  big[36] = 'Z';
  stick.put("PSP/SAVEDATA/TEST99901ABC/PARAM.SFO", big);
  saveParameters(m, 0, "TEST99901", "ABC", 16);
  CHECK(runSave(m), 0);
  saveParameters(m, 17, "TEST99901", "ABC", 16);
  CHECK(runSave(m), 0);
  auto rewritten = stick.get("PSP/SAVEDATA/TEST99901ABC/PARAM.SFO");
  CHECK(rewritten.size(), 4912);
  CHECK(listName(sfoField(rewritten, "SAVEDATA_FILE_LIST"), 0) == "DATA.BIN", true);
}

//The memory stick's free space, the same wherever it's told (a 2 GB stick, mostly empty: io.cpp): the capacity
//request (clusters, those free, a sector's bytes and a cluster's sectors) and the savedata utility's sizes modes.
//The sizes mode (8), as utility/savedata/sizes recorded: with the free space alone asked (Chili Con Carnage's and
//Ace Combat X's request) it answers 0; the save measured is msData's (its own game and save names, the request's save
//name ignored), its files in clusters and the folder's own (three small files, 4); msData naming none there is no
//data, the rest still answered; what saving would take counts the data (if a file's named), the icons' and sound's
//files the request carries, its PARAM.SFO and the folder. The size mode (22), as utility/savedata/getsize recorded:
//the free space in 32 KiB "sectors", and with no files listed nothing else written; with files, what they take in
//whole clusters past the free space (none for 128 KiB, its text left alone; 256 MiB for a 2 GiB file); not in the
//smaller structure of before firmware 2.00; 0 for a save that's there, and NO_DATA for one that isn't, the sizes
//written all the same (Def Jam: Fight for NY makes its save on that answer). Secure files count as a PSP stores them,
//encrypted, 16 bytes longer: a save's (its PARAM.SFO names them), a save mode's data, and the size mode's secure
//list. Sizes as text in whole units: KB, MB, GB.
static auto stickSpace() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto& memory = m.system.memory;
  constexpr u32 In = 0x0895'0000, Out = 0x0895'0100, Sizes = 0x0895'0200, Files = 0x0895'0300;
  memory.write(4, In, Out);
  CHECK(m.call("sceIoDevctl", {m.string("ms0:"), 0x0242'5818, In, 4, 0, 0}), 0);
  std::array<u32, 5> capacity = {61'440, 57'344, 57'344, 512, 64};
  for(u32 n = 0; n < 5; n++) CHECK(memory.read(4, Out + n * 4), capacity[n]);

  //the free space alone: 0, and the devctl's free clusters
  saveParameters(m, 8, "ULUS10216", "", 0);
  memory.write(4, Parameters + 1492, 0);
  memory.write(4, Parameters + 1496, 0);
  memory.fill(Used, 0xcc, 64);
  memory.fill(Needed, 0xcc, 32);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Free), 32_KiB);
  CHECK(memory.read(4, Free + 4), memory.read(4, Out + 4));
  CHECK(memory.read(4, Free + 8), 57'344 * 32);
  CHECK(memory.readString(Free + 12, 8) == "1 GB", true);
  CHECK(memory.read(4, Used + 36), 0xcccc'cccc);
  CHECK(memory.read(4, Needed), 0xcccc'cccc);

  //msData naming a save not there: no data, what saving would take still told (16 bytes, and two icons)
  memory.copyIn(Buffer, "0123456789abcdef", 16);
  saveParameters(m, 8, "ULUS99999", "ASDF", 16);
  memory.copyIn(Used + 16, "ABC", 4);
  memory.write(4, Parameters + 1412, Buffer);  //ICON0: 40000 bytes, two clusters
  memory.write(4, Parameters + 1420, 40'000);
  memory.write(4, Parameters + 1444, Buffer);  //PIC1: 70000 bytes, three
  memory.write(4, Parameters + 1452, 70'000);
  memory.write(4, Parameters + 1428 + 8, 99'999);  //ICON1's size with no buffer: nothing
  CHECK(runSave(m), 0x8011'03c7);
  CHECK(memory.read(4, Free + 4), 57'344);
  std::array<u32, 2> needed = {1 + 2 + 3 + 2, (1 + 2 + 3 + 2) * 32};
  CHECK(memory.read(4, Needed), needed[0]);
  CHECK(memory.read(4, Needed + 4), needed[1]);
  CHECK(memory.readString(Needed + 8, 8) == "256 KB", true);
  CHECK(memory.read(4, Needed + 16), needed[1]);
  CHECK(memory.readString(Needed + 20, 8) == "256 KB", true);
  //and naming one there: its three files (each under a cluster) and its folder, 4 clusters; 16 bytes of data, 3
  stick.put("PSP/SAVEDATA/ULUS99999ABC/DATA.BIN", "data");
  stick.put("PSP/SAVEDATA/ULUS99999ABC/OTHER.BIN", "other");
  stick.put("PSP/SAVEDATA/ULUS99999ABC/PARAM.SFO", std::string(4096, 's'));
  saveParameters(m, 8, "ULUS99999", "ASDF", 16);
  memory.copyIn(Used + 16, "ABC", 4);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Used + 36), 4);
  CHECK(memory.read(4, Used + 40), 128);
  CHECK(memory.readString(Used + 44, 8) == "128 KB", true);
  CHECK(memory.read(4, Needed), 3);
  CHECK(memory.readString(Needed + 8, 8) == "96 KB", true);
  saveParameters(m, 8, "ULUS99999", "ABC", 2_MiB);  //2 MiB of data, 16 bytes more stored: 67 clusters, 2144 KiB
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Needed + 4), 2144);
  CHECK(memory.readString(Needed + 8, 8) == "2 MB", true);
  fileName(m, "");  //no file named: no data, its PARAM.SFO and the folder
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Needed), 2);
  //a save's secure file of 32 KiB takes two clusters, as a PSP stores it; a normal one, one
  memory.fill(Buffer, 's', 32_KiB);
  saveParameters(m, 13, "ULUS99999", "SECURE", 32_KiB);
  memory.write(4, Parameters + 120, 32_KiB);
  CHECK(runSave(m), 0);
  saveParameters(m, 18, "ULUS99999", "SECURE", 32_KiB);
  memory.write(4, Parameters + 120, 32_KiB);
  fileName(m, "OTHER.BIN");
  CHECK(runSave(m), 0);
  saveParameters(m, 8, "ULUS99999", "SECURE", 0);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Used + 36), 2 + 1 + 1 + 1);  //DATA.BIN, OTHER.BIN, PARAM.SFO, the folder
  //with no names of the request's own (Valhalla Knights 2's), answered just the same; a save not there, no data
  saveParameters(m, 8, "", "", 16);
  memory.copyIn(Used, "ULUS99999", 10);
  memory.copyIn(Used + 16, "ABC", 4);
  memory.fill(Free, 0xcc, 32);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Used + 36), 4);
  CHECK(memory.read(4, Free + 4), 57'344);
  memory.copyIn(Used + 16, "XYZ", 4);
  CHECK(runSave(m), 0x8011'03c7);

  //the size mode: the free space; no files listed, nothing else; two secure files and a normal one, 4 clusters
  saveParameters(m, 22, "ULUS99999", "ABC", 0);
  memory.write(4, Parameters + 1532, Sizes);
  memory.fill(Sizes, 0xcc, 64);
  memory.write(4, Sizes, 0);
  memory.write(4, Sizes + 4, 0);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Sizes + 16), 32_KiB);
  CHECK(memory.read(4, Sizes + 20), 57'344);
  CHECK(memory.read(4, Sizes + 24), 57'344 * 32);
  CHECK(memory.readString(Sizes + 28, 8) == "1 GB", true);
  for(u32 at : {36u, 40u, 48u, 52u}) CHECK(memory.read(4, Sizes + at), 0xcccc'cccc);
  for(auto [n, size] : {std::pair{0u, 100u}, {1u, 40'000u}, {2u, 32'768u}}) {
    memory.fill(Files + n * 24, 0, 24);
    memory.write(4, Files + n * 24, size);
  }
  memory.write(4, Sizes, 2);
  memory.write(4, Sizes + 4, 1);
  memory.write(4, Sizes + 8, Files);
  memory.write(4, Sizes + 12, Files + 48);
  saveParameters(m, 22, "ULUS99999", "NONE", 0);  //a save not there: no data, the sizes told all the same
  memory.write(4, Parameters + 1532, Sizes);
  memory.fill(Sizes + 16, 0xcc, 44);
  CHECK(runSave(m), 0x8011'0327);
  CHECK(memory.read(4, Sizes + 20), 57'344);
  //they take 128 KiB, well within the free space: nothing more needed, new or over a save, the text left alone
  for(u32 at : {36u, 48u}) CHECK(memory.read(4, Sizes + at), 0);
  for(u32 at : {40u, 52u}) CHECK(memory.read(4, Sizes + at), 0xcccc'cccc);
  //a normal file of 2 GiB: 2,097,152 KiB, 262,144 past the 1,835,008 free, in each field and its text
  memory.write(4, Files + 48, 0x8000'0000);  //(a 64-bit size, low word first)
  memory.write(4, Files + 52, 0);
  memory.write(4, Sizes, 0);  //no secure files
  CHECK(runSave(m), 0x8011'0327);
  for(u32 at : {36u, 48u}) CHECK(memory.read(4, Sizes + at), 262'144);
  for(u32 at : {40u, 52u}) CHECK(memory.readString(Sizes + at, 8) == "256 MB", true);
  //and the save there: 0, the same sizes
  saveParameters(m, 22, "ULUS99999", "ABC", 0);
  memory.write(4, Parameters + 1532, Sizes);
  memory.fill(Sizes + 36, 0xcc, 24);
  CHECK(runSave(m), 0);
  for(u32 at : {36u, 48u}) CHECK(memory.read(4, Sizes + at), 262'144);
  //a secure file of 2 GiB is 16 bytes longer stored: a cluster more, 262,176 KiB past the free space
  memory.write(4, Sizes, 1);
  memory.write(4, Sizes + 4, 0);
  memory.write(4, Sizes + 8, Files + 48);
  CHECK(runSave(m), 0);
  for(u32 at : {36u, 48u}) CHECK(memory.read(4, Sizes + at), 262'176);
  //with no size information to fill in, the same answers
  memory.write(4, Parameters + 1532, 0);
  CHECK(runSave(m), 0);
  saveParameters(m, 22, "ULUS99999", "NONE", 0);
  memory.write(4, Parameters + 1532, 0);
  CHECK(runSave(m), 0x8011'0327);
  //the smaller structure has no size mode's answer
  memory.fill(Sizes, 0xcc, 64);
  saveParameters(m, 22, "ULUS99999", "ABC", 0);
  memory.write(4, Parameters, 1500);
  memory.write(4, Parameters + 1532, Sizes);
  CHECK(runSave(m), 0);
  CHECK(memory.read(4, Sizes + 20), 0xcccc'cccc);
}

//The other dialogs: a message answered yes at once (noted); network settings cancelled; one at a time; asked about
//as another kind, the wrong type; shut down before finishing, the wrong status.
static auto dialogs() -> void {
  KernelMachine m;
  u64 millisecond = Kernel::CPUFrequency / 1000;
  m.system.memory.fill(Parameters, 0, 580);
  m.system.memory.write(4, Parameters, 580);  //(0x244, one of a message's sizes)
  m.system.memory.copyIn(Parameters + 60, "Save failed.", 13);
  CHECK(m.call("sceUtilityMsgDialogInitStart", {Parameters}), 0);
  CHECK(m.call("sceUtilityMsgDialogInitStart", {Parameters}), 0x8011'0001);  //one at a time
  CHECK(m.call("sceUtilitySavedataInitStart", {Parameters}), 0x8011'0001);
  CHECK(m.call("sceUtilitySavedataGetStatus", {}), 0x8011'0005);
  CHECK(m.call("sceUtilityMsgDialogShutdownStart", {}), 0x8011'0001);  //not finished
  m.kernel.cycles += 300 * millisecond;
  CHECK(m.call("sceUtilityMsgDialogGetStatus", {}), 2);
  CHECK(m.call("sceUtilityMsgDialogUpdate", {1}), 0);
  CHECK(m.call("sceUtilityMsgDialogGetStatus", {}), 3);
  CHECK(m.system.memory.read(4, Parameters + 576), 1);  //yes
  CHECK(m.system.memory.read(4, Parameters + 28), 0);
  CHECK(m.notes.size() == 1 && m.notes[0] == "message dialog (answered yes): Save failed.", true);
  CHECK(m.call("sceUtilityMsgDialogShutdownStart", {}), 0);
  m.kernel.cycles += 26 * millisecond;
  CHECK(m.call("sceUtilityMsgDialogGetStatus", {}), 0);
  m.system.memory.fill(Parameters, 0, 580);
  m.system.memory.write(4, Parameters, 0x44);
  CHECK(m.call("sceUtilityNetconfInitStart", {Parameters}), 0);
  m.kernel.cycles += 300 * millisecond;
  CHECK(m.call("sceUtilityNetconfUpdate", {1}), 0);
  CHECK(m.call("sceUtilityNetconfGetStatus", {}), 3);
  CHECK(m.system.memory.read(4, Parameters + 28), 1);  //cancelled
}

//What pspautotests' utility/dialog tests recorded. Each kind takes only its own sizes (sizes): every other size is
//refused, and the refusal leaves the kind as it was. A dialog starts only while none is current: while another
//starts, the wrong status even with a wrong size (status asks first), and still while one shuts down (priority).
//Update does nothing but while the dialog runs (starting, or gone: the wrong status). A message aborted while it
//runs stays running for 8 more Updates (here its first Update answers it, so one still running has had none), then
//finishes, its result 0 and its button as it was (abort); aborted when finished, the wrong status. A state saved
//meanwhile loads, the count with it.
static auto dialogStatuses() -> void {
  KernelMachine m;
  auto& memory = m.system.memory;
  u64 millisecond = Kernel::CPUFrequency / 1000;
  auto advance = [&](u64 cycles) {
    m.kernel.cycles += cycles;
    m.kernel.events();
  };
  struct Kind { const char* name; std::vector<u32> sizes; };
  std::vector<Kind> kinds = {
    {"sceUtilitySavedata", {0x5c8, 0x5dc, 0x600}}, {"sceUtilityMsgDialog", {0x23c, 0x244, 0x2c4}},
    {"sceUtilityOsk", {0x40, 0x44}}, {"sceUtilityNetconf", {0x38, 0x40, 0x44}},
    {"sceUtilityGameSharing", {0x50, 0x54, 0x64}}, {"sceUtilityHtmlViewer", {0x70, 0x78, 0x80, 0x98, 0xa4, 0xa8}},
    {"sceUtilityGamedataInstall", {0x590, 0x598}},
  };
  memory.fill(Parameters, 0, 0x800);
  for(auto& kind : kinds) {
    string start = {kind.name, "InitStart"}, status = {kind.name, "GetStatus"};
    string shutdown = {kind.name, "ShutdownStart"};
    for(u32 size = 0; size <= 0x800; size += 4) {
      bool takes = std::find(kind.sizes.begin(), kind.sizes.end(), size) != kind.sizes.end();
      if(takes) continue;
      memory.write(4, Parameters, size);
      CHECK(m.call(start.data(), {Parameters}), 0x8011'0004);
    }
    CHECK(m.call(status.data(), {}), 0x8011'0005);  //(the refusals left no kind)
    for(u32 size : kind.sizes) {
      memory.write(4, Parameters, size);
      CHECK(m.call(start.data(), {Parameters}), 0);
      memory.write(4, Parameters, 0);
      CHECK(m.call(start.data(), {Parameters}), 0x8011'0001);  //the status before the size
      CHECK(m.call("sceUtilityMsgDialogInitStart", {Parameters}), 0x8011'0001);
      advance(400 * millisecond);
      CHECK(m.call(status.data(), {}), 2);
      CHECK(m.call(shutdown.data(), {}), 0x8011'0001);  //(running)
      m.call(string{kind.name, "Update"}.data(), {1});
      CHECK(m.call(status.data(), {}), 3);
      CHECK(m.call(shutdown.data(), {}), 0);
      CHECK(m.call(shutdown.data(), {}), 0x8011'0001);
      memory.write(4, Parameters, size);
      CHECK(m.call(start.data(), {Parameters}), 0x8011'0001);  //(shutting down)
      advance(400 * millisecond);
      CHECK(m.call(status.data(), {}), 0);
    }
  }
  m.notes.clear();  //(the message's answer)
  //Update while starting, and after the dialog's gone; Abort of another kind, and not running
  memory.write(4, Parameters, 0x244);
  memory.copyIn(Parameters + 60, "Aborted.", 9);
  CHECK(m.call("sceUtilityMsgDialogAbort", {}), 0x8011'0005);
  CHECK(m.call("sceUtilityMsgDialogInitStart", {Parameters}), 0);
  CHECK(m.call("sceUtilityMsgDialogUpdate", {1}), 0x8011'0001);
  CHECK(m.call("sceUtilityMsgDialogAbort", {}), 0x8011'0001);
  advance(400 * millisecond);
  //aborted while running, before its first Update (which would have answered it)
  memory.write(4, Parameters + 28, 0xcccc'cccc);
  memory.write(4, Parameters + 576, 0xcccc'cccc);
  CHECK(m.call("sceUtilityMsgDialogAbort", {}), 0);
  CHECK(m.call("sceUtilityMsgDialogAbort", {}), 0);  //(no more than once)
  for(u32 update : range(7)) {
    CHECK(m.call("sceUtilityMsgDialogUpdate", {1}), 0);
    CHECK(m.call("sceUtilityMsgDialogGetStatus", {}), 2);
    if(update == 3) CHECK(roundTrip(m), true);
  }
  CHECK(m.call("sceUtilityMsgDialogUpdate", {1}), 0);
  CHECK(m.call("sceUtilityMsgDialogGetStatus", {}), 3);
  CHECK(memory.read(4, Parameters + 28), 0);
  CHECK(memory.read(4, Parameters + 576), 0xcccc'cccc);
  CHECK(m.call("sceUtilityMsgDialogAbort", {}), 0x8011'0001);
  CHECK(m.call("sceUtilityMsgDialogUpdate", {1}), 0);  //(finished: nothing more to do)
  CHECK(m.call("sceUtilityMsgDialogShutdownStart", {}), 0);
  advance(400 * millisecond);
  CHECK(m.call("sceUtilityMsgDialogUpdate", {1}), 0x8011'0001);
  CHECK(m.notes.size(), 0);
}

//A save dialog closed from threads of several priorities, its graphics and access threads at 17 and 19 as the games
//set them (pspautotests' utility/savedata/shutdownstatus): a caller worse than 0x20 and both threads finds it closed
//at its first look after ShutdownStart, no time gone, and starts the next one at once, as Crash Tag Team Racing's
//main thread (0x3f) does; so does one whose parameters give priorities no thread can have. Found closing, its next
//start refused: a caller at 0x20, callers better than the graphics or the access thread, one with interrupts or
//dispatching held off, an alarm's handler interrupting a worse thread, and a message's caller even at 111
//(utility/dialog/priority). A state saved while a dialog closes loads into another machine, which carries on to the
//same answers. On both engines.
static auto savedataClosing() -> void {
  constexpr u32 Out = 0x0895'0000, Message = 0x0896'0000, Handler = 0x0880'3000;
  enum How : u32 { Plain, Interrupts, Dispatch, Alarm };
  struct Case { u32 caller, graphics, access; How how; u32 seen, again; };
  std::vector<Case> cases = {
    {0x21, 0x11, 0x13, Plain, 0, 0},
    {0x6f, 0x11, 0x13, Plain, 0, 0},
    {0x20, 0x11, 0x13, Plain, 4, 0x8011'0001},
    {0x30, 0x40, 0x13, Plain, 4, 0x8011'0001},
    {0x30, 0x11, 0x40, Plain, 4, 0x8011'0001},
    {0x21, 0x00, 0x80, Plain, 0, 0},
    {0x21, 0xffff'ffff, 0x13, Plain, 0, 0},
    {0x6f, 0x11, 0x13, Interrupts, 4, 0x8011'0001},
    {0x6f, 0x11, 0x13, Dispatch, 4, 0x8011'0001},
    {0x6f, 0x11, 0x13, Alarm, 4, 0x8011'0001},
  };
  for(bool recompile : {false, true}) {
    HostFolder stick;
    KernelMachine m;
    auto& memory = m.system.memory;
    m.kernel.mount("ms0", stick.path.string());
    saveParameters(m, 0, "TEST99909", "ABC", 16);  //loading a save that isn't there
    memory.fill(Message, 0, 580);
    memory.write(4, Message, 580);
    memory.copyIn(Message + 60, "Saved.", 7);
    u32 alarmCase = 0;
    for(u32 n = 0; n < cases.size(); n++) if(cases[n].how == Alarm) alarmCase = n;
    Assembler handler{m, Handler};  //closes the dialog from the alarm's case, and asks again
    auto store = [&](Assembler& a, u32 offset) { a.li(t0, Out + offset); a.put(sw(v0, 0, t0)); };
    handler.put(addiu(sp, sp, -16)); handler.put(sw(ra, 12, sp));
    handler.call("sceUtilitySavedataShutdownStart"); store(handler, alarmCase * 16);
    handler.call("sceUtilitySavedataGetStatus"); store(handler, alarmCase * 16 + 8);
    handler.li(a0, Parameters); handler.call("sceUtilitySavedataInitStart"); store(handler, alarmCase * 16 + 12);
    handler.put(lw(ra, 12, sp)); handler.put(addiu(sp, sp, 16));
    handler.li(v0, 0); handler.put(jr(ra)); handler.put(nop);
    Assembler a{m, 0x0880'1000};
    auto priority = [&](u32 value) { a.li(a0, 0); a.li(a1, value); a.call("sceKernelChangeThreadPriority"); };
    auto delay = [&](u32 microseconds) { a.li(a0, microseconds); a.call("sceKernelDelayThread"); };
    for(u32 n = 0; n < cases.size(); n++) {
      auto& c = cases[n];
      a.li(t0, Parameters); a.li(t1, c.graphics); a.put(sw(t1, 12, t0)); a.li(t1, c.access); a.put(sw(t1, 16, t0));
      a.li(a0, Parameters); a.call("sceUtilitySavedataInitStart");
      delay(250'000);
      a.li(a0, 1); a.call("sceUtilitySavedataUpdate");
      priority(c.caller);
      if(c.how == Alarm) {  //its handler interrupts the caller spinning
        a.li(a0, 1000); a.li(a1, Handler); a.li(a2, 0); a.call("sceKernelSetAlarm");
        a.li(t1, 300'000);
        u32 loop = a.here();
        a.put(addiu(t1, t1, -1));
        a.put(bne(t1, zero, int32_t(loop - (a.here() + 4)) / 4)); a.put(nop);
        delay(10'000);
      } else {
        if(c.how == Interrupts) { a.call("sceKernelCpuSuspendIntr"); a.put(addu(s1, v0, zero)); }
        if(c.how == Dispatch) { a.call("sceKernelSuspendDispatchThread"); a.put(addu(s1, v0, zero)); }
        a.call("sceKernelGetSystemTimeLow"); a.put(addu(s0, v0, zero));
        a.call("sceUtilitySavedataShutdownStart"); store(a, n * 16);
        a.call("sceKernelGetSystemTimeLow"); a.put(subu(v0, v0, s0)); store(a, n * 16 + 4);
        a.call("sceUtilitySavedataGetStatus"); store(a, n * 16 + 8);
        a.li(a0, Parameters); a.call("sceUtilitySavedataInitStart"); store(a, n * 16 + 12);
        if(c.how == Interrupts) { a.put(addu(a0, s1, zero)); a.call("sceKernelCpuResumeIntr"); }
        if(c.how == Dispatch) { a.put(addu(a0, s1, zero)); a.call("sceKernelResumeDispatchThread"); }
      }
      priority(0x20);
      delay(250'000);  //whichever dialog is left, finished and closed (Update and ShutdownStart refused if none)
      a.li(a0, 1); a.call("sceUtilitySavedataUpdate");
      a.call("sceUtilitySavedataShutdownStart");
      delay(10'000);
    }
    a.li(a0, Message); a.call("sceUtilityMsgDialogInitStart");
    delay(350'000);
    a.li(a0, 1); a.call("sceUtilityMsgDialogUpdate");
    priority(0x6f);
    a.call("sceUtilityMsgDialogShutdownStart"); store(a, 0x100);
    a.call("sceUtilityMsgDialogGetStatus"); store(a, 0x104);
    a.li(a0, Parameters); a.call("sceUtilitySavedataInitStart"); store(a, 0x108);
    a.call("sceKernelExitGame");
    m.system.recompiler.enabled = recompile;
    m.system.power(0x0880'1000);
    s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
    auto& main = *m.kernel.threads[uid];
    m.kernel.startThread(main, 0, 0);
    KernelMachine fresh;
    fresh.kernel.mount("ms0", stick.path.string());
    fresh.system.recompiler.enabled = recompile;
    bool saved = false;
    for(u32 step = 0; step < 100'000 && !m.kernel.exited; step++) {
      m.kernel.run(Kernel::CPUFrequency / 10'000);
      if(!saved && main.status == Kernel::Status::Waiting && m.kernel.dialog.status == 4) {
        auto state = saveState(m);
        CHECK(loadState(fresh, state), true);
        CHECK(saveState(fresh) == state, true);
        saved = true;
      }
    }
    CHECK(saved, true);
    CHECK(m.kernel.exited, true);
    for(u32 step = 0; step < 100'000 && !fresh.kernel.exited; step++) fresh.kernel.run(Kernel::CPUFrequency / 100);
    CHECK(fresh.kernel.exited, true);
    for(u32 n = 0; n < cases.size(); n++) {
      auto& c = cases[n];
      CHECK(memory.read(4, Out + n * 16), 0);
      CHECK(memory.read(4, Out + n * 16 + 4) < 100, true);  //(nobody waits)
      CHECK(memory.read(4, Out + n * 16 + 8), c.seen);
      CHECK(memory.read(4, Out + n * 16 + 12), c.again);
    }
    CHECK(memory.read(4, Out + 0x100), 0);
    CHECK(memory.read(4, Out + 0x104), 4);
    CHECK(memory.read(4, Out + 0x108), 0x8011'0001);
    std::vector<u8> ours(0x110), theirs(0x110);
    memory.copyOut(ours.data(), Out, ours.size());
    fresh.system.memory.copyOut(theirs.data(), Out, theirs.size());
    CHECK(ours == theirs, true);
    CHECK(m.notes.size() == 1 && m.notes[0] == "message dialog (answered yes): Saved.", true);
    CHECK(fresh.notes == m.notes, true);
  }
}

//Installing a game's data: asked about before any install dialog started, the wrong type (Ace Combat: Joint Assault
//asks at boot, and goes on); parameters of a size it doesn't take refused; started, it runs and is cancelled, by
//Update or by Abort (which, not running, is the wrong status, and asked of another dialog the wrong type); it shuts
//down as the others do; a state saved meanwhile loads.
static auto gamedataInstall() -> void {
  KernelMachine m;
  u64 millisecond = Kernel::CPUFrequency / 1000;
  auto advance = [&](u64 cycles) {  //(the kernel catching up with the vertical blanks, as a state wants)
    m.kernel.cycles += cycles;
    m.kernel.events();
  };
  CHECK(m.call("sceUtilityGamedataInstallGetStatus", {}), 0x8011'0005);
  CHECK(m.call("sceUtilityGamedataInstallAbort", {}), 0x8011'0005);
  m.system.memory.fill(Parameters, 0, 0x598);
  for(u32 size : {0u, 0x58cu, 0x594u, 0x5a0u}) {
    m.system.memory.write(4, Parameters, size);
    CHECK(m.call("sceUtilityGamedataInstallInitStart", {Parameters}), 0x8011'0004);
  }
  for(u32 size : {0x590u, 0x598u}) {
    for(bool abort : {false, true}) {
      m.system.memory.write(4, Parameters, size);
      m.system.memory.write(4, Parameters + 28, 0xcccc'cccc);
      CHECK(m.call("sceUtilityGamedataInstallInitStart", {Parameters}), 0);
      CHECK(m.call("sceUtilityGamedataInstallInitStart", {Parameters}), 0x8011'0001);  //one at a time
      CHECK(m.call("sceUtilityMsgDialogGetStatus", {}), 0x8011'0005);
      CHECK(m.call("sceUtilityGamedataInstallAbort", {}), 0x8011'0001);  //not running yet
      CHECK(m.call("sceUtilityGamedataInstallGetStatus", {}), 1);
      advance(200 * millisecond);
      CHECK(m.call("sceUtilityGamedataInstallGetStatus", {}), 2);
      CHECK(roundTrip(m), true);
      CHECK(m.call(abort ? "sceUtilityGamedataInstallAbort" : "sceUtilityGamedataInstallUpdate", {1}), 0);
      CHECK(m.call("sceUtilityGamedataInstallGetStatus", {}), 3);
      CHECK(m.system.memory.read(4, Parameters + 28), 1);  //cancelled
      CHECK(m.call("sceUtilityGamedataInstallShutdownStart", {}), 0);
      advance(40 * millisecond);
      CHECK(m.call("sceUtilityGamedataInstallGetStatus", {}), 0);
    }
  }
  CHECK(m.notes.size(), 0);
}

//The keyboard: each field's text entered as it is, an empty field or one of spaces given the console's nickname,
//each as far as its room (its NUL among it) and its limit allow, a limit of 0 being none, and each CHANGED; a field
//with nowhere to put its text, or no room for any, still has its result, nothing written there (a NUL had gone into
//a field of no room, and a limit of 0 had cut the nickname to nothing); the common part's result is 0. (Peace
//Walker, its keyboard cancelled, asked for its player's name again and again; Ridge Racer 2, its default driver name
//answered UNCHANGED, went back to its title as if the player had cancelled.)
static auto keyboard() -> void {
  KernelMachine m;
  u64 millisecond = Kernel::CPUFrequency / 1000;
  constexpr u32 Fields = Buffer, Texts = Buffer + 0x400;
  auto write16 = [&](u32 at, const std::u16string& text) {
    for(u32 n = 0; n < text.size(); n++) m.system.memory.write(2, at + n * 2, text[n]);
    m.system.memory.write(2, at + text.size() * 2, 0);
  };
  auto read16 = [&](u32 at) {
    std::u16string text;
    for(u16 c; (c = m.system.memory.read(2, at)) && text.size() < 64; at += 2) text.push_back(c);
    return text;
  };
  auto field = [&](u32 n, const std::u16string& initial, u32 room, u32 limit, bool output = true) {
    u32 at = Fields + n * 52, in = Texts + n * 0x100, out = in + 0x80;
    m.system.memory.fill(at, 0, 52);
    write16(in, initial);
    m.system.memory.fill(out, 0xcc, 0x40);  //(so a missing NUL shows)
    m.system.memory.write(4, at + 32, in);
    m.system.memory.write(4, at + 36, room);
    m.system.memory.write(4, at + 40, output ? out : 0);
    m.system.memory.write(4, at + 44, 0x77);
    m.system.memory.write(4, at + 48, limit);
  };
  auto answer = [&](u32 n) { return read16(Texts + n * 0x100 + 0x80); };
  auto result = [&](u32 n) { return m.system.memory.read(4, Fields + n * 52 + 44); };
  m.system.memory.fill(Parameters, 0, 64);
  m.system.memory.write(4, Parameters, 64);
  m.system.memory.write(4, Parameters + 48, 11);
  m.system.memory.write(4, Parameters + 52, Fields);
  field(0, u"Snake", 0x200, 15);
  field(1, u"", 0x200, 15);
  field(2, u"   ", 0x200, 15);
  field(3, u"Big Boss", 0x200, 3);
  field(4, u"", 2, 15);
  field(5, u"X", 0x200, 15, false);
  field(6, u"", 0x200, 0);
  field(7, u"Ocelot", 4, 0);
  field(8, u"", 0, 15);
  field(9, u"Kaz", 0, 0);
  field(10, u"DAMACY", 8, 7);  //(Ridge Racer 2's driver name: room for 7 and the NUL, at most 7)
  auto advance = [&](u64 cycles) {  //(the kernel catching up with the vertical blanks, as a state wants)
    m.kernel.cycles += cycles;
    m.kernel.events();
  };
  CHECK(m.call("sceUtilityOskInitStart", {Parameters}), 0);
  advance(300 * millisecond);
  CHECK(m.call("sceUtilityOskGetStatus", {}), 2);
  CHECK(m.call("sceUtilityOskUpdate", {1}), 0);
  CHECK(m.call("sceUtilityOskGetStatus", {}), 3);
  CHECK(m.system.memory.read(4, Parameters + 28), 0);
  CHECK(answer(0) == u"Snake" && result(0) == 2, true);
  CHECK(answer(1) == u"PSP" && result(1) == 2, true);
  CHECK(answer(2) == u"PSP" && result(2) == 2, true);
  CHECK(answer(3) == u"Big" && result(3) == 2, true);
  CHECK(answer(4) == u"P" && result(4) == 2, true);
  CHECK(m.system.memory.read(2, Texts + 5 * 0x100 + 0x80) == 0xcccc && result(5) == 2, true);
  CHECK(answer(6) == u"PSP" && result(6) == 2, true);
  CHECK(answer(7) == u"Oce" && result(7) == 2, true);
  CHECK(m.system.memory.read(2, Texts + 8 * 0x100 + 0x80) == 0xcccc && result(8) == 2, true);
  CHECK(m.system.memory.read(2, Texts + 9 * 0x100 + 0x80) == 0xcccc && result(9) == 2, true);
  CHECK(answer(10) == u"DAMACY" && result(10) == 2, true);
  CHECK(m.call("sceUtilityOskShutdownStart", {}), 0);
  advance(40 * millisecond);
  CHECK(m.call("sceUtilityOskGetStatus", {}), 0);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//Optional modules: loaded once (again: already loaded), unloaded once (again: not loaded), 0x107, 0x108 and 0x308
//among them, numbers that aren't a module refused, the older network numbers standing for 0x100-0x106 and the older
//USB numbers for 0x200-0x203; the nickname.
static auto modules() -> void {
  KernelMachine m;
  CHECK(m.call("sceUtilityLoadModule", {0x301}), 0);
  CHECK(m.call("sceUtilityLoadModule", {0x301}), 0x8011'1102);
  CHECK(m.call("sceUtilityUnloadModule", {0x301}), 0);
  CHECK(m.call("sceUtilityUnloadModule", {0x301}), 0x8011'1103);
  CHECK(m.call("sceUtilityLoadModule", {0x308}), 0);  //later firmwares' (Ace Combat: Joint Assault loads it)
  CHECK(m.call("sceUtilityUnloadModule", {0x308}), 0);
  CHECK(m.call("sceUtilityLoadModule", {0x309}), 0x8011'1101);
  CHECK(m.call("sceUtilityLoadModule", {0x108}), 0);  //later firmwares' (Macross: Triangle Frontier loads it)
  CHECK(m.call("sceUtilityLoadModule", {0x108}), 0x8011'1102);
  CHECK(m.call("sceUtilityLoadModule", {0x107}), 0);
  CHECK(roundTrip(m), true);
  CHECK(m.call("sceUtilityUnloadModule", {0x108}), 0);
  CHECK(m.call("sceUtilityUnloadModule", {0x107}), 0);
  CHECK(m.call("sceUtilityLoadModule", {0x109}), 0x8011'1101);
  CHECK(m.call("sceUtilityLoadModule", {0x700}), 0x8011'1101);
  CHECK(m.call("sceUtilityLoadNetModule", {1}), 0);
  CHECK(m.call("sceUtilityLoadModule", {0x100}), 0x8011'1102);  //the same module
  CHECK(m.call("sceUtilityUnloadNetModule", {1}), 0);
  CHECK(m.call("sceUtilityLoadNetModule", {8}), 0x8011'1101);
  //the older USB numbers: 1 and 3-5 standing for 0x200-0x203, the accessory driver (2) taken without a number
  CHECK(m.call("sceUtilityLoadUsbModule", {5}), 0);
  CHECK(m.call("sceUtilityLoadModule", {0x203}), 0x8011'1102);  //the same module
  CHECK(m.call("sceUtilityLoadUsbModule", {5}), 0x8011'1102);
  CHECK(m.call("sceUtilityUnloadUsbModule", {5}), 0);
  CHECK(m.call("sceUtilityUnloadUsbModule", {5}), 0x8011'1103);
  CHECK(m.call("sceUtilityLoadUsbModule", {1}), 0);
  CHECK(m.call("sceUtilityUnloadModule", {0x200}), 0);
  CHECK(m.call("sceUtilityLoadUsbModule", {2}), 0);
  CHECK(m.call("sceUtilityUnloadUsbModule", {2}), 0);
  CHECK(m.call("sceUtilityLoadUsbModule", {0}), 0x8011'1101);
  CHECK(m.call("sceUtilityLoadUsbModule", {6}), 0x8011'1101);
  CHECK(m.call("sceUtilityUnloadUsbModule", {6}), 0x8011'1101);
  CHECK(m.call("sceUtilityGetSystemParamString", {1, Buffer, 128}), 0);
  CHECK(m.system.memory.readString(Buffer, 128) == "PSP", true);
  CHECK(m.call("sceUtilityGetSystemParamString", {2, Buffer, 128}), 0x8011'0103);
  //number settings taken and forgotten: the language stays English (1)
  CHECK(m.call("sceUtilitySetSystemParamInt", {3, 0}), 0);
  CHECK(m.call("sceUtilitySetSystemParamInt", {8, 2}), 0);
  m.system.memory.write(4, Buffer, 7);
  CHECK(m.call("sceUtilityGetSystemParamInt", {8, Buffer}), 0);
  CHECK(m.system.memory.read(4, Buffer), 1);
}

auto utilityTests() -> Tests {
  return {
    {"utility savedata", savedata}, {"utility savedata names", savedataNames},
    {"utility savedata unnamed (\"<>\")", savedataUnnamed},
    {"utility savedata buffers", savedataBuffers}, {"utility savedata loops", savedataLoops},
    {"utility savedata erase", savedataErase}, {"utility savedata files", savedataFiles},
    {"utility savedata PARAM.SFO", savedataParamSFO}, {"utility savedata without PARAM.SFO (v2.0.0's)", savedataOlder},
    {"utility savedata broken saves", savedataBroken},
    {"utility savedata through memstick: (Colin McRae Rally 2005 Plus)", savedataMemstick},
    {"utility savedata lists answered with no front end (7th Dragon 2020, Brandish)", savedataListAnswers},
    {"utility savedata trace", savedataTrace},
    {"memory stick free space", stickSpace}, {"utility dialogs", dialogs},
    {"utility keyboard", keyboard}, {"utility modules", modules},
    {"utility game data install", gamedataInstall}, {"utility dialog sizes and statuses", dialogStatuses},
    {"utility save dialog closed by priority", savedataClosing},
  };
}

}
