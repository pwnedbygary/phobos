//The system's utilities (ares/psp/kernel/utility.cpp): a dialog's statuses and their timing, saves made, loaded,
//sized, listed, erased and deleted in a memory stick folder (names that would reach elsewhere and buffers outside
//the program's memory refused), a message answered yes, the keyboard's fields accepted (an empty one given the
//nickname), network settings cancelled, the wrong type and the wrong status refused; optional modules loaded and
//unloaded; the nickname. Called directly, the clock moved on by hand.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 Parameters = 0x0892'0000, Buffer = 0x0893'0000, Free = 0x0894'0000, Used = 0x0894'0100;
constexpr u32 Needed = 0x0894'0200, List = 0x0894'0300, Entries = 0x0894'0400;

//A SceUtilitySavedataParam for (mode, game, save, file, the data's size), its answers' structures pointed at.
auto saveParameters(KernelMachine& m, u32 mode, const char* game, const char* save, u32 dataSize) -> void {
  m.system.memory.fill(Parameters, 0, 1536);
  m.system.memory.write(4, Parameters, 1536);
  m.system.memory.write(4, Parameters + 48, mode);
  m.system.memory.copyIn(Parameters + 60, game, strlen(game) + 1);
  m.system.memory.copyIn(Parameters + 76, save, strlen(save) + 1);
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
  CHECK(m.system.memory.read(4, Free + 4), 32768);
  CHECK(m.system.memory.read(4, Free + 8), 1024 * 1024);
  CHECK(m.system.memory.readString(Free + 12, 8) == "1 GB", true);
  CHECK(m.system.memory.readString(Used + 16, 20) == "SLOT0", true);
  CHECK(m.system.memory.read(4, Used + 36), 1);    //one cluster used
  CHECK(m.system.memory.read(4, Needed), 2);       //40000 bytes take two
  CHECK(m.system.memory.read(4, Needed + 4), 64);
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
  refused(20, "ULUS99999", "SLOT0", "../DATA.BIN", 0x8011'0348);
  //data files that are links leading out of their folders, into the stick and out of it: as if not there
  refused(1, "ULUS99999", "SLOT1", "DATA.BIN", 0x8011'0385);
  refused(0, "ULUS99999", "SLOT1", "DATA.BIN", 0x8011'0307);
  refused(0, "ULUS99999", "SLOT2", "DATA.BIN", 0x8011'0307);
  refused(20, "ULUS99999", "SLOT2", "DATA.BIN", 0x8011'0347);
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
  refused(8, "ULUS99999", ".", "", 0x8011'03c8);
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
  CHECK(m.system.memory.read(4, Used + 36), 1);  //DATA.BIN's 100 bytes: a cluster
  CHECK(run(11, ""), 0);
  CHECK(m.system.memory.read(4, List + 4), 1);  //SLOT0 alone
  CHECK(m.system.memory.readString(Entries + 52, 20) == "SLOT0", true);
}

//Erasing (modes 19 and 20) takes the data file named and nothing else, and with none named, nothing; one that isn't
//there is no data. Deleting a save (modes 6, 7, 9, 10 and 21) takes its folder and all it holds.
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
    CHECK(runSave(m), 0x8011'0347);
    saveParameters(m, mode, "ULUS99999", "SLOT0", 0);
    fileName(m, "");
    CHECK(runSave(m), 0);
    CHECK(stick.get("PSP/SAVEDATA/ULUS99999SLOT0/OTHER.BIN") == "other", true);
  }
  for(u32 mode : {6u, 7u, 9u, 10u, 21u}) {
    stick.put("PSP/SAVEDATA/ULUS99999SLOT0/DATA.BIN", "data");
    saveParameters(m, mode, "ULUS99999", "SLOT0", 0);
    CHECK(runSave(m), 0);
    CHECK(std::filesystem::exists(folder), false);
    CHECK(std::filesystem::exists(stick.path / "PSP/SAVEDATA"), true);
  }
}

//The other dialogs: a message answered yes at once (noted); network settings cancelled; one at a time; asked about
//as another kind, the wrong type; shut down before finishing, the wrong status.
static auto dialogs() -> void {
  KernelMachine m;
  u64 millisecond = Kernel::CPUFrequency / 1000;
  m.system.memory.fill(Parameters, 0, 580);
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
  CHECK(m.call("sceUtilityNetconfInitStart", {Parameters}), 0);
  m.kernel.cycles += 300 * millisecond;
  CHECK(m.call("sceUtilityNetconfUpdate", {1}), 0);
  CHECK(m.call("sceUtilityNetconfGetStatus", {}), 3);
  CHECK(m.system.memory.read(4, Parameters + 28), 1);  //cancelled
}

//The keyboard: each field's text accepted as it is (UNCHANGED), an empty field or one of spaces given the console's
//nickname (CHANGED), each as far as its room (its NUL among it) and its limit allow; a field with nowhere to put its
//text still has its result; the common part's result is 0. (Peace Walker, its keyboard cancelled, asked for its
//player's name again and again.)
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
  m.system.memory.write(4, Parameters + 48, 6);
  m.system.memory.write(4, Parameters + 52, Fields);
  field(0, u"Snake", 0x200, 15);
  field(1, u"", 0x200, 15);
  field(2, u"   ", 0x200, 15);
  field(3, u"Big Boss", 0x200, 3);
  field(4, u"", 2, 15);
  field(5, u"X", 0x200, 15, false);
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
  CHECK(answer(0) == u"Snake" && result(0) == 0, true);
  CHECK(answer(1) == u"PSP" && result(1) == 2, true);
  CHECK(answer(2) == u"PSP" && result(2) == 2, true);
  CHECK(answer(3) == u"Big" && result(3) == 0, true);
  CHECK(answer(4) == u"P" && result(4) == 2, true);
  CHECK(m.system.memory.read(2, Texts + 5 * 0x100 + 0x80) == 0xcccc && result(5) == 0, true);
  CHECK(m.call("sceUtilityOskShutdownStart", {}), 0);
  advance(40 * millisecond);
  CHECK(m.call("sceUtilityOskGetStatus", {}), 0);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//Optional modules: loaded once (again: already loaded), unloaded once (again: not loaded), numbers that aren't a
//module refused, the older network numbers standing for 0x100-0x106; the nickname.
static auto modules() -> void {
  KernelMachine m;
  CHECK(m.call("sceUtilityLoadModule", {0x301}), 0);
  CHECK(m.call("sceUtilityLoadModule", {0x301}), 0x8011'1102);
  CHECK(m.call("sceUtilityUnloadModule", {0x301}), 0);
  CHECK(m.call("sceUtilityUnloadModule", {0x301}), 0x8011'1103);
  CHECK(m.call("sceUtilityLoadModule", {0x308}), 0x8011'1101);
  CHECK(m.call("sceUtilityLoadModule", {0x700}), 0x8011'1101);
  CHECK(m.call("sceUtilityLoadNetModule", {1}), 0);
  CHECK(m.call("sceUtilityLoadModule", {0x100}), 0x8011'1102);  //the same module
  CHECK(m.call("sceUtilityUnloadNetModule", {1}), 0);
  CHECK(m.call("sceUtilityLoadNetModule", {8}), 0x8011'1101);
  CHECK(m.call("sceUtilityGetSystemParamString", {1, Buffer, 128}), 0);
  CHECK(m.system.memory.readString(Buffer, 128) == "PSP", true);
  CHECK(m.call("sceUtilityGetSystemParamString", {2, Buffer, 128}), 0x8011'0103);
}

auto utilityTests() -> Tests {
  return {
    {"utility savedata", savedata}, {"utility savedata names", savedataNames},
    {"utility savedata buffers", savedataBuffers}, {"utility savedata loops", savedataLoops},
    {"utility savedata erase", savedataErase}, {"utility dialogs", dialogs}, {"utility keyboard", keyboard},
    {"utility modules", modules},
  };
}

}
