//Files and controls (ares/psp/kernel io.cpp and ctrl.cpp): the memory stick as a temporary host folder, and the
//buttons and stick as the system sets them; with PSP_TEST_PROGRAMS, a real program that uses both through newlib.
#include "kernel-machine.hpp"
#include "disc-image.hpp"

#include <array>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <utime.h>

namespace allegrex_test::psp {

constexpr u32 Buffer = 0x0893'0000;

static auto readBack(KernelMachine& m, u32 length) -> std::string {
  std::string text(length, '\0');
  m.system.memory.copyOut(text.data(), Buffer, length);
  return text;
}

//Opening, reading, writing, seeking and closing, with each open mode.
static auto fileBasics() -> void {
  HostFolder folder;
  folder.put("data/File.txt", "hello");
  KernelMachine m;
  m.kernel.mount("ms0", folder.path.string());

  u32 file = m.call("sceIoOpen", {m.string("ms0:/DATA/FILE.TXT"), 0x0001, 0});  //found whatever the case
  CHECK(file >= 3 && file < 0x8000'0000, true);
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 5);
  CHECK(readBack(m, 5) == "hello", true);
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 0);  //at the end
  CHECK(m.call("sceIoLseek32", {file, 0, 2}), 5);
  m.call("sceIoLseek", {file, 0, 1, 0, 0});  //a 64-bit offset: a2 low, a3 high; whence in t0
  CHECK(m.system.ipu.r[2], 1);
  CHECK(m.system.ipu.r[3], 0);
  CHECK(m.call("sceIoRead", {file, Buffer, 2}), 2);
  CHECK(readBack(m, 2) == "el", true);
  CHECK(m.call("sceIoWrite", {file, Buffer, 2}), Kernel::ErrorBadFile);  //opened for reading only
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(m.call("sceIoClose", {file}), Kernel::ErrorBadFile);

  m.system.memory.copyIn(Buffer, "ABCD", 4);
  u32 created = m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0202, 0});  //write, create
  CHECK(m.call("sceIoWrite", {created, Buffer, 4}), 4);
  CHECK(m.call("sceIoRead", {created, Buffer, 4}), Kernel::ErrorBadFile);  //opened for writing only
  m.call("sceIoClose", {created});
  CHECK(folder.get("data/new.bin") == "ABCD", true);
  u32 emptied = m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0402, 0});  //write, truncate
  m.call("sceIoWrite", {emptied, Buffer, 2});
  m.call("sceIoClose", {emptied});
  CHECK(folder.get("data/new.bin") == "AB", true);
  u32 appended = m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0102, 0});  //write, append
  m.call("sceIoWrite", {appended, Buffer + 2, 2});
  m.call("sceIoClose", {appended});
  CHECK(folder.get("data/new.bin") == "ABCD", true);
  u32 both = m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0003, 0});  //read and write, keeping it
  m.system.memory.copyIn(Buffer, "x", 1);
  m.call("sceIoLseek32", {both, 1, 0});
  m.call("sceIoWrite", {both, Buffer, 1});
  m.call("sceIoLseek32", {both, 0, 0});
  CHECK(m.call("sceIoRead", {both, Buffer, 4}), 4);
  CHECK(readBack(m, 4) == "AxCD", true);
  m.call("sceIoClose", {both});

  CHECK(m.call("sceIoOpen", {m.string("ms0:/data/new.bin"), 0x0a02, 0}), Kernel::ErrorFileExists);  //exclusive
  CHECK(m.call("sceIoOpen", {m.string("ms0:/missing"), 0x0001, 0}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/missing"), 0x0002, 0}), Kernel::ErrorFileNotFound);  //no create
  CHECK(m.call("sceIoOpen", {m.string("ms0:/data"), 0x0001, 0}), Kernel::ErrorIsDirectory);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/data/File.txt"), 0, 0}), Kernel::ErrorInvalidArgument);
  CHECK(m.call("sceIoLseek32", {file, 0, 0}), Kernel::ErrorBadFile);  //closed
}

//Folders: making, listing, status, renaming, removing, and relative paths from the working folder.
static auto fileFolders() -> void {
  HostFolder folder;
  folder.put("data/File.txt", "hello");
  folder.put("data/another.bin", "x");
  KernelMachine m;
  m.kernel.mount("ms0", folder.path.string());

  CHECK(m.call("sceIoMkdir", {m.string("ms0:/saves"), 0777}), 0);
  CHECK(m.call("sceIoMkdir", {m.string("ms0:/saves"), 0777}), Kernel::ErrorFileExists);
  CHECK(m.call("sceIoMkdir", {m.string("ms0:/no/such/parent"), 0777}), Kernel::ErrorFileNotFound);
  CHECK(std::filesystem::is_directory(folder.path / "saves"), true);

  CHECK(m.call("sceIoGetstat", {m.string("ms0:/saves"), Buffer}), 0);
  CHECK(m.system.memory.read(4, Buffer), 0x11ff);
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/data/file.txt"), Buffer}), 0);
  CHECK(m.system.memory.read(4, Buffer), 0x21ff);
  CHECK(m.system.memory.read(4, Buffer + 8), 5);
  CHECK(m.system.memory.read(2, Buffer + 48) >= 2020, true);  //the year it was changed
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/data/none"), Buffer}), Kernel::ErrorFileNotFound);

  auto list = [&](const char* path) {
    std::vector<std::string> names;
    u32 listing = m.call("sceIoDopen", {m.string(path)});
    if(listing >= 0x8000'0000) return names;
    while(m.call("sceIoDread", {listing, Buffer}) == 1) names.push_back(m.system.memory.readString(Buffer + 88, 256));
    CHECK(m.call("sceIoDclose", {listing}), 0);
    return names;
  };
  auto data = list("ms0:/data");
  CHECK(data.size(), 4);
  if(data.size() == 4) CHECK(data[0] == "." && data[1] == ".." && data[2] == "another.bin" && data[3] == "File.txt", true);
  auto top = list("ms0:/");
  CHECK(top.size(), 2);  //no "." or ".." at a device's top
  if(top.size() == 2) CHECK(top[0] == "data" && top[1] == "saves", true);
  CHECK(m.call("sceIoDopen", {m.string("ms0:/data/File.txt")}), Kernel::ErrorNotDirectory);

  CHECK(m.call("sceIoChdir", {m.string("ms0:/data")}), 0);
  CHECK(m.kernel.workingDirectory == "ms0:/data", true);
  u32 relative = m.call("sceIoOpen", {m.string("FILE.TXT"), 0x0001, 0});
  CHECK(m.call("sceIoRead", {relative, Buffer, 5}), 5);
  m.call("sceIoClose", {relative});
  CHECK(m.call("sceIoChdir", {m.string("File.txt")}), Kernel::ErrorNotDirectory);

  CHECK(m.call("sceIoRename", {m.string("ms0:/data/File.txt"), m.string("ms0:/data/Renamed.txt")}), 0);
  CHECK(std::filesystem::exists(folder.path / "data/Renamed.txt"), true);
  CHECK(m.call("sceIoRename", {m.string("ms0:/data/another.bin"), m.string("ms0:/data/renamed.txt")}),
        Kernel::ErrorFileExists);
  CHECK(m.call("sceIoRmdir", {m.string("ms0:/data")}), Kernel::ErrorDirectoryNotEmpty);
  CHECK(m.call("sceIoRemove", {m.string("ms0:/data/renamed.TXT")}), 0);
  CHECK(m.call("sceIoRemove", {m.string("ms0:/data/another.bin")}), 0);
  CHECK(m.call("sceIoRemove", {m.string("ms0:/data/another.bin")}), Kernel::ErrorFileNotFound);
  CHECK(m.call("sceIoRemove", {m.string("ms0:/saves")}), Kernel::ErrorIsDirectory);
  CHECK(m.call("sceIoRmdir", {m.string("ms0:/data")}), 0);
  CHECK(std::filesystem::exists(folder.path / "data"), false);
}

//A file's dates (sceIoGetstat's, as the savedata lists give them too) are in local time, as a memory stick's FAT
//keeps them and as the clock's local time has it: 4 hours west of Greenwich, and in a zone with daylight saving time,
//summer and winter. None is later than the PSP's clock as local times go: with the clock an hour behind the file (a
//state saved earlier loaded), and in the hour daylight saving time gives back (the file changed in its first pass,
//the clock in its second, later in UTC but earlier in local time), the date is the clock's now. Before 1980 (in local
//time), or for a file that isn't there, it's 1980-01-01 00:00:00: no FAT stick holds an earlier date. The system's
//time zone and daylight saving time settings agree with the clock's local time: the zone without daylight saving
//time, in both hemispheres, and in Ireland's rules, whose winter is the zone files' "daylight saving" time.
static auto fileDates() -> void {
  HostFolder folder;
  KernelMachine m;
  m.kernel.mount("ms0", folder.path.string());
  using Date = std::array<u32, 6>;
  auto dates = [&](u32 at) {  //a SceIoStat's three: made, read, changed
    std::array<Date, 3> read;
    for(u32 n = 0; n < 18; n++) read[n / 6][n % 6] = m.system.memory.read(2, at + n / 6 * 16 + n % 6 * 2);
    return read;
  };
  //A file made afresh and changed (seconds since 1970, before now, so that its made date, its birth time where the
  //host keeps one but no later than the change, is the change too), the clock's now: its dates.
  u32 files = 0;
  auto dated = [&](s64 changed, s64 clock) {
    std::string name = "data/F" + std::to_string(files++) + ".TXT";
    folder.put(name, "hello");
    utimbuf times{std::time_t(changed), std::time_t(changed)};
    utime((folder.path / name).c_str(), &times);
    m.kernel.startTime = u64(clock) * 1'000'000;
    m.system.memory.fill(Buffer, 0xcc, 88);
    CHECK(m.call("sceIoGetstat", {m.string("ms0:/" + name), Buffer}), 0);
    return dates(Buffer + 16);
  };
  auto all = [](const Date& date) { return std::array<Date, 3>{date, date, date}; };
  auto setting = [&](u32 which) {
    m.system.memory.write(4, Buffer, 0xcccc'cccc);
    CHECK(m.call("sceUtilityGetSystemParamInt", {which, Buffer}), 0);
    return m.system.memory.read(4, Buffer);
  };
  //2025-10-06 12:34:56 UTC; 2025-07-01 and 2025-01-15, 12:00 UTC
  constexpr s64 Changed = 1'759'754'096, Summer = 1'751'371'200, Winter = 1'736'942'400, Day = 86'400;
  {
    HostZone zone("WEST+4");
    CHECK((dated(Changed, Changed + Day) == all({2025, 10, 6, 8, 34, 56})), true);
    CHECK((dated(Changed, Changed - 3'600) == all({2025, 10, 6, 7, 34, 56})), true);
    CHECK(m.call("sceRtcGetCurrentClockLocalTime", {Buffer}), 0);
    CHECK((dates(Buffer)[0] == Date{2025, 10, 6, 7, 34, 56}), true);
    CHECK(setting(6), u32(-240));
    CHECK(setting(7), 0);
  }
  {
    HostZone zone("EST5EDT,M3.2.0,M11.1.0");
    CHECK((dated(Summer, Summer + Day) == all({2025, 7, 1, 8, 0, 0})), true);
    CHECK(setting(6), u32(-300));
    CHECK(setting(7), 1);
    CHECK((dated(Winter, Winter + Day) == all({2025, 1, 15, 7, 0, 0})), true);
    CHECK(setting(6), u32(-300));
    CHECK(setting(7), 0);
    //2025-11-02 05:30 UTC is 01:30 EDT; 06:10 UTC, 01:10 EST
    CHECK((dated(1'762'061'400, 1'762'063'800) == all({2025, 11, 2, 1, 10, 0})), true);
    CHECK((dated(172'065'600, Summer) == all({1980, 1, 1, 0, 0, 0})), true);  //1975-06-15
    CHECK((dated(315'532'800, Summer) == all({1980, 1, 1, 0, 0, 0})), true);  //1979-12-31 19:00 EST
    CHECK((dated(315'550'801, Summer) == all({1980, 1, 1, 0, 0, 1})), true);
    m.system.memory.fill(Buffer, 0xcc, 48);
    m.kernel.writeTimes(Buffer, (folder.path / "data/none").string());
    CHECK((dates(Buffer) == all({1980, 1, 1, 0, 0, 0})), true);
  }
  for(auto [rules, zone, summer] : {std::tuple{"AEST-10AEDT,M10.1.0,M4.1.0/3", 600u, Winter},  //Sydney
                                    std::tuple{"IST-1GMT0,M10.5.0,M3.5.0/1", 0u, Summer}}) {  //Ireland
    HostZone zoned(rules);
    m.kernel.startTime = u64(summer) * 1'000'000;
    check(__LINE__, rules, setting(6), zone);
    check(__LINE__, rules, setting(7), 1);
    m.kernel.startTime = u64(summer == Summer ? Winter : Summer) * 1'000'000;
    check(__LINE__, rules, setting(6), zone);
    check(__LINE__, rules, setting(7), 0);
  }
  {
    HostZone zone("JST-9");
    CHECK(setting(6), 540);
    CHECK(setting(7), 0);
  }
}

//Paths stay inside their device's folder; unmounted devices, aliases and backslashes.
static auto fileContainment() -> void {
  HostFolder outer;
  std::filesystem::path stick = outer.path / "stick";
  std::filesystem::create_directories(stick / "data");
  KernelMachine m;
  m.kernel.mount("ms0", stick.string());
  for(const char* path : {"ms0:/../outside.txt", "ms0:/data/../../outside.txt", "../outside.txt"}) {
    CHECK(m.call("sceIoOpen", {m.string(path), 0x0202, 0}), Kernel::ErrorFileNotFound);
  }
  CHECK(std::filesystem::exists(outer.path / "outside.txt"), false);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/C:/windows.txt"), 0x0202, 0}), Kernel::ErrorInvalidArgument);
  CHECK(m.call("sceIoOpen", {m.string("disc0:/PSP_GAME/PARAM.SFO"), 0x0001, 0}), Kernel::ErrorDeviceNotFound);
  HostFolder disc;
  disc.put("PSP_GAME/PARAM.SFO", "sfo");
  m.kernel.mount("disc0", disc.path.string());
  u32 aliased = m.call("sceIoOpen", {m.string("umd0:/psp_game/param.sfo"), 0x0001, 0});
  CHECK(m.call("sceIoRead", {aliased, Buffer, 8}), 3);
  u32 backslashed = m.call("sceIoOpen", {m.string("disc0:\\PSP_GAME\\PARAM.SFO"), 0x0001, 0});
  CHECK(backslashed >= 3 && backslashed < 0x8000'0000, true);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/./data/./new.txt"), 0x0202, 0}) < 0x8000'0000, true);
  CHECK(std::filesystem::exists(stick / "data/new.txt"), true);

  //symbolic links in the folder: one leading out of it is refused, file or folder; one staying inside works
  outer.put("secret.txt", "secret");
  std::filesystem::create_symlink(outer.path / "secret.txt", stick / "link");
  std::filesystem::create_directory_symlink(outer.path, stick / "outward");
  std::filesystem::create_directory_symlink(stick / "data", stick / "inward");
  CHECK(m.call("sceIoOpen", {m.string("ms0:/link"), 0x0001, 0}), Kernel::ErrorNoPermission);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/outward/secret.txt"), 0x0001, 0}), Kernel::ErrorNoPermission);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/outward/made.txt"), 0x0202, 0}), Kernel::ErrorNoPermission);
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/link"), Buffer}), Kernel::ErrorNoPermission);
  CHECK(m.call("sceIoDopen", {m.string("ms0:/outward")}), Kernel::ErrorNoPermission);
  CHECK(std::filesystem::exists(outer.path / "made.txt"), false);
  u32 inward = m.call("sceIoOpen", {m.string("ms0:/inward/new.txt"), 0x0001, 0});
  CHECK(inward >= 3 && inward < 0x8000'0000, true);
}

//A directory entry's private part, when the program asks for it: the short 8.3 name in capitals, and the long name,
//laid out by the program's SDK version as io/shortname recorded. With none (as homebrew gives), or 0x03070010 (Need
//for Speed: ProStreet's), the short name in 13 bytes and the long name right after it, nothing past its end (the
//program's first word written over, whatever it declared); with 0x06060010, the declared size (0x414) kept, the
//short name 4 bytes in and the long name 20 in, and nothing at all when the size declared is smaller. A folder's
//"." and ".." have themselves for both names.
static auto fileShortNames() -> void {
  HostFolder folder;
  folder.put("Verylongname.extension", "");
  folder.put("File.txt", "");
  folder.put("sub/x", "");
  KernelMachine m;
  auto& memory = m.system.memory;
  m.kernel.mount("ms0", folder.path.string());
  constexpr u32 Extra = Buffer + 0x1000;
  struct Entry { std::string name, older, newer; u32 first, fourth, past; };
  auto list = [&](const char* path, u32 declared) {
    std::vector<Entry> entries;
    u32 listing = m.call("sceIoDopen", {m.string(path)});
    while(true) {
      memory.fill(Extra, 0xee, 0x500);
      memory.write(4, Extra, declared);
      memory.write(4, Buffer + 344, Extra);
      if(m.call("sceIoDread", {listing, Buffer}) != 1) break;
      Entry e;
      e.name = memory.readString(Buffer + 88, 256);
      e.older = memory.readString(Extra, 13) + "|" + memory.readString(Extra + 13, 256);
      e.newer = memory.readString(Extra + 4, 16) + "|" + memory.readString(Extra + 20, 1024);
      e.first = memory.read(4, Extra), e.fourth = memory.read(4, Extra + 4);
      e.past = memory.read(1, Extra + 13 + e.name.size() + 1);  //just past the older layout's long name
      entries.push_back(e);
    }
    m.call("sceIoDclose", {listing});
    return entries;
  };
  for(u32 sdk : {0u, 0x0307'0010u}) {
    m.kernel.sdkVersion = sdk;
    auto entries = list("ms0:/", 0x414);  //(in order, whatever their case: File.txt, sub, Verylongname...)
    CHECK(entries.size(), 3);
    if(entries.size() == 3) {
      CHECK(entries[0].name == "File.txt" && entries[0].older == "FILE.TXT|File.txt", true);
      CHECK(entries[1].name == "sub" && entries[1].older == "SUB|sub", true);
      CHECK(entries[2].name == "Verylongname.extension", true);
      CHECK(entries[2].older == "VERYLONG.EXT|Verylongname.extension", true);
      for(auto& e : entries) CHECK(e.first != 0x414 && e.past == 0xee, true);
    }
    auto inside = list("ms0:/sub", 0);
    CHECK(inside.size(), 3);
    if(inside.size() == 3) {
      CHECK(inside[0].older == ".|." && inside[1].older == "..|.." && inside[2].older == "X|x", true);
    }
  }
  m.kernel.sdkVersion = 0x0606'0010;
  auto entries = list("ms0:/", 0x414);
  CHECK(entries.size(), 3);
  if(entries.size() == 3) {
    CHECK(entries[0].newer == "FILE.TXT|File.txt" && entries[0].first == 0x414, true);
    CHECK(entries[1].newer == "SUB|sub" && entries[2].newer == "VERYLONG.EXT|Verylongname.extension", true);
  }
  entries = list("ms0:/", 0x413);  //declared smaller than a SceIoFatDirentPrivate: nothing written
  CHECK(entries.size(), 3);
  for(auto& e : entries) CHECK(e.first == 0x413 && e.fourth == 0xeeee'eeee, true);
}

//The buttons and stick: sampled at each vertical blank and kept; the latest peeked at once, oldest first, each with
//the time it was taken; inverted when negative; the stick only when sampled in analog mode; buttons only the system
//sees left out; the counts allowed.
static auto controllerPeek() -> void {
  KernelMachine m;
  auto vblank = [&] { m.kernel.cycles = m.kernel.nextVblank; m.kernel.events(); };
  m.kernel.controller.buttons = 0x4010 | 0x10'0000;  //cross and up, and volume up, which games don't see
  m.kernel.controller.analogX = 200;
  vblank();
  u32 taken = u32(m.kernel.cycles / (Kernel::CPUFrequency / 1'000'000));
  m.kernel.cycles += Kernel::CPUFrequency / 100;  //some time passes before the program looks
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 2}), 2);
  CHECK(m.system.memory.read(4, Buffer + 4), 0);  //the one before: nothing held, as if sampled since power on
  CHECK(m.system.memory.read(1, Buffer + 8), 128);
  CHECK(m.system.memory.read(4, Buffer + 16), taken);  //the latest last, with the time it was taken
  CHECK(m.system.memory.read(4, Buffer + 16 + 4), 0x4010);
  CHECK(m.system.memory.read(1, Buffer + 16 + 8), 128);  //digital: the stick reads as centred
  CHECK(m.call("sceCtrlSetSamplingMode", {1}), 0);
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 1}), 1);
  CHECK(m.system.memory.read(1, Buffer + 8), 128);  //that sample was taken before the change
  vblank();
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 1}), 1);
  CHECK(m.system.memory.read(1, Buffer + 8), 200);
  CHECK(m.system.memory.read(1, Buffer + 9), 128);
  CHECK(m.call("sceCtrlPeekBufferNegative", {Buffer, 1}), 1);
  CHECK(m.system.memory.read(4, Buffer + 4), ~0x4010u);
  m.call("sceCtrlGetSamplingMode", {Buffer});
  CHECK(m.system.memory.read(4, Buffer), 1);
  //the count is a byte, and the PSP keeps 64 samples: up to 64 may be asked for (MACH's title reads all 64), more is
  //refused, and a count that wraps can't overflow
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 65}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 0x1000'0041}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 0x1000'0001}), 1);
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 63}), 63);
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 64}), 64);
  CHECK(m.system.memory.read(4, Buffer + 63 * 16 + 4), 0x4010);  //the latest, last of the 64
  CHECK(m.call("sceCtrlReadBufferPositive", {Buffer, 65}), Kernel::ErrorInvalidSize);
  CHECK(m.call("sceCtrlReadBufferPositive", {Buffer, 64}) != Kernel::ErrorInvalidSize, true);
}

//The host's right stick in bytes 10 and 11 of every SceCtrlData (ctrl.cpp's convention, not a PSP's: a PSP writes
//zeros there), 128 each way when left alone, in every call that gives samples: peek and read, positive and negative,
//one sample or all 64; centred in digital mode, as the stick is; the last four bytes zero. A state doesn't keep it:
//the samples loaded read it centred, the next one takes it again.
static auto controllerRightStick() -> void {
  KernelMachine m;
  auto vblank = [&] { m.kernel.cycles = m.kernel.nextVblank; m.kernel.events(); };
  auto filled = [&] { m.system.memory.fill(Buffer, 0xee, 64 * 16); };
  auto right = [&](u32 sample) {
    return m.system.memory.read(1, Buffer + sample * 16 + 10) | m.system.memory.read(1, Buffer + sample * 16 + 11) << 8;
  };
  auto zeros = [&](u32 sample) { return m.system.memory.read(4, Buffer + sample * 16 + 12) == 0; };
  vblank();
  filled();
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 1}), 1);  //digital, untouched: centred, then zeros
  CHECK(right(0), 0x8080);
  CHECK(zeros(0), true);
  m.kernel.controller.rightX = 30, m.kernel.controller.rightY = 220;
  vblank();
  CHECK(m.call("sceCtrlPeekBufferPositive", {Buffer, 1}), 1);  //digital: centred, as the stick
  CHECK(right(0), 0x8080);
  CHECK(m.call("sceCtrlSetSamplingMode", {1}), 0);
  vblank();
  m.kernel.controller.analogX = 200, m.kernel.controller.analogY = 10;
  vblank();
  for(auto name : {"sceCtrlPeekBufferPositive", "sceCtrlPeekBufferNegative"}) {
    filled();
    CHECK(m.call(name, {Buffer, 64}), 64);
    CHECK(right(61), 0x8080);                                   //sampled digital
    CHECK(right(62) == (220 << 8 | 30) && right(63) == (220 << 8 | 30), true);
    CHECK(m.system.memory.read(1, Buffer + 63 * 16 + 8) == 200 && m.system.memory.read(1, Buffer + 62 * 16 + 8) == 128,
          true);                                                //(the left stick as it was at each)
    CHECK(zeros(0) && zeros(62) && zeros(63), true);
  }
  m.kernel.controller.rightX = 255, m.kernel.controller.rightY = 0;
  vblank();
  filled();
  CHECK(m.call("sceCtrlReadBufferPositive", {Buffer, 1}), 1);
  CHECK(right(0), 0x00ff);
  CHECK(zeros(0), true);
  m.kernel.controller.rightX = 64, m.kernel.controller.rightY = 192;
  vblank();
  m.kernel.controller.rightX = m.kernel.controller.rightY = 128;  //let go
  vblank();
  filled();
  CHECK(m.call("sceCtrlReadBufferNegative", {Buffer, 3}), 2);  //the two new ones, then the oldest kept
  CHECK(right(0) == (192 << 8 | 64) && right(1) == 0x8080 && right(2) == 0x8080, true);
  CHECK(zeros(0) && zeros(1) && zeros(2), true);
  CHECK(m.system.memory.read(4, Buffer + 4), ~0u);              //(negative: nothing held)
  //a state holds none of it (saved again after the stick and every sample's right values change, it's the same
  //bytes); loaded back into this machine, whose newest sample holds 1 and 2, its samples read centred; the next takes
  //the stick
  m.kernel.controller.rightX = 1, m.kernel.controller.rightY = 2;
  vblank();
  auto state = saveState(m);
  m.kernel.controller.rightX = 99;
  for(auto& sample : m.kernel.controller.samples) sample.rightX = 7, sample.rightY = 9;
  CHECK(saveState(m) == state, true);
  for(auto& sample : m.kernel.controller.samples) sample.rightX = 1, sample.rightY = 2;
  CHECK(loadState(m, state), true);
  m.call("sceCtrlPeekBufferPositive", {Buffer, 1});
  CHECK(right(0), 0x8080);
  CHECK(m.system.memory.read(1, Buffer + 8), 200);              //(the left stick is kept)
  m.kernel.controller.rightX = 3, m.kernel.controller.rightY = 4;
  vblank();
  m.call("sceCtrlPeekBufferPositive", {Buffer, 1});
  CHECK(right(0), 4 << 8 | 3);
}

//The latch: presses and releases between reads, the buttons held and not held at some sample meanwhile, and how many
//samples; reading starts it afresh, and doesn't wait for another sample.
static auto controllerLatch() -> void {
  KernelMachine m;
  auto vblank = [&] { m.kernel.cycles = m.kernel.nextVblank; m.kernel.events(); };
  m.kernel.controller.buttons = 0x4010;  //cross and up
  vblank();
  m.call("sceCtrlReadLatch", {Buffer});
  m.kernel.controller.buttons = 0x4010 | 0x2000;  //circle pressed
  vblank();
  m.kernel.controller.buttons = 0x4010;           //and let go
  vblank();
  CHECK(m.call("sceCtrlPeekLatch", {Buffer}), 2);
  CHECK(m.system.memory.read(4, Buffer + 0), 0x2000);     //made
  CHECK(m.system.memory.read(4, Buffer + 4), 0x2000);     //broken
  CHECK(m.system.memory.read(4, Buffer + 8), 0x6010);     //held at some sample
  CHECK(m.system.memory.read(4, Buffer + 12), ~0x4010u);  //not held at some sample
  //the same into a buffer at an unaligned address in VRAM's fourth copy, whose words cross its 32-byte pieces
  constexpr u32 Unaligned = 0x0460'001e;
  CHECK(m.call("sceCtrlPeekLatch", {Unaligned}), 2);
  CHECK(m.system.memory.read(4, Unaligned + 0), 0x2000);
  CHECK(m.system.memory.read(4, Unaligned + 12), ~0x4010u);
  CHECK(m.call("sceCtrlReadLatch", {Buffer}), 2);         //the same, then it starts afresh
  CHECK(m.system.memory.read(4, Buffer + 8), 0x6010);
  CHECK(m.call("sceCtrlReadLatch", {Buffer}), 0);         //at once, with nothing gathered
  CHECK(m.system.memory.read(4, Buffer + 0), 0);
  CHECK(m.system.memory.read(4, Buffer + 8), 0);
}

//Reading the buffer when samples have come since the last read: those samples at once, oldest first, and how many
//(at most 63 wait); only the newest if there are more than asked for; places past the newest filled from the oldest
//kept.
static auto controllerReadNew() -> void {
  KernelMachine m;
  auto vblank = [&] { m.kernel.cycles = m.kernel.nextVblank; m.kernel.events(); };
  m.kernel.controller.buttons = 0x1000;  //triangle, for longer than the 64 samples kept
  for(u32 n = 0; n < 70; n++) vblank();
  CHECK(m.kernel.controller.unread, 63);
  CHECK(m.call("sceCtrlReadBufferPositive", {Buffer, 1}), 1);
  for(u32 buttons : {0x10u, 0x20u, 0x40u}) m.kernel.controller.buttons = buttons, vblank();  //up, right, down
  CHECK(m.call("sceCtrlReadBufferPositive", {Buffer, 4}), 3);
  CHECK(m.system.memory.read(4, Buffer + 0 * 16 + 4), 0x10);
  CHECK(m.system.memory.read(4, Buffer + 1 * 16 + 4), 0x20);
  CHECK(m.system.memory.read(4, Buffer + 2 * 16 + 4), 0x40);
  CHECK(m.system.memory.read(4, Buffer + 3 * 16 + 4), 0x1000);
  CHECK(m.kernel.controller.unread, 0);
  for(u32 buttons : {0x80u, 0x100u, 0x200u}) m.kernel.controller.buttons = buttons, vblank();  //left, L, R
  CHECK(m.call("sceCtrlReadBufferNegative", {Buffer, 2}), 2);
  CHECK(m.system.memory.read(4, Buffer + 0 * 16 + 4), ~0x100u);
  CHECK(m.system.memory.read(4, Buffer + 1 * 16 + 4), ~0x200u);
}

//A sampling cycle: its range, a sample on its timer though no frame has ended, about three a frame at 5555
//microseconds (none from the vertical blank meanwhile), and back to one at each vertical blank.
static auto controllerCycle() -> void {
  KernelMachine m;
  CHECK(m.call("sceCtrlSetSamplingCycle", {100}), Kernel::ErrorInvalidValue);
  CHECK(m.call("sceCtrlSetSamplingCycle", {20001}), Kernel::ErrorInvalidValue);
  CHECK(m.call("sceCtrlSetSamplingCycle", {5555}), 0);
  m.call("sceCtrlGetSamplingCycle", {Buffer});
  CHECK(m.system.memory.read(4, Buffer), 5555);
  m.kernel.controller.buttons = 0x4000;
  m.kernel.cycles = m.kernel.controller.nextSample;
  m.kernel.events();
  CHECK(m.kernel.controller.sampled, 0x4000);
  CHECK(m.kernel.vblanks, 0);
  m.kernel.cycles = m.kernel.nextVblank;
  m.kernel.events();
  u32 samples = m.kernel.controller.latch.samples;
  m.kernel.cycles = m.kernel.nextVblank;  //one whole frame later
  m.kernel.events();
  CHECK(m.kernel.controller.latch.samples - samples, 3);
  CHECK(m.call("sceCtrlSetSamplingCycle", {0}), 5555);
  samples = m.kernel.controller.latch.samples;
  m.kernel.controller.buttons = 0;
  m.kernel.cycles = m.kernel.nextVblank;
  m.kernel.events();
  CHECK(m.kernel.controller.latch.samples - samples, 1);
  CHECK(m.kernel.controller.sampled, 0);
}

//A program waits for the cross button, reading the controller each frame: by reading alone, which waits for the next
//sample, or by waiting for the frame and then reading, which finds that frame's sample there at once. Either way it
//keeps up with the frames: the player presses the cross on the tenth, and the tenth read sees it.
static auto controllerRead() -> void {
  for(bool waitForFrame : {false, true}) for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler main{m, 0x0880'1000};
    main.put(addu(s0, zero, zero));
    u32 loop = main.here();
    if(waitForFrame) main.call("sceDisplayWaitVblankStart");
    main.li(a0, Buffer); main.li(a1, 1);
    main.call("sceCtrlReadBufferPositive");
    main.put(addiu(s0, s0, 1));
    main.li(t0, Buffer); main.put(lw(t1, 4, t0));
    main.put(andi(t1, t1, 0x4000));
    main.put(beq(t1, zero, int32_t(loop - (main.here() + 4)) / 4));
    main.put(nop);
    main.li(t0, KernelMachine::Results); main.put(sw(s0, 0, t0));
    main.call("sceKernelExitGame");
    m.system.recompiler.enabled = recompile;
    m.system.power(0x0880'1000);
    s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
    while(!m.kernel.exited && m.kernel.vblanks < 100) {
      if(m.kernel.vblanks == 9) m.kernel.controller.buttons = 0x4000;
      m.kernel.run(Kernel::VblankCycles);
    }
    CHECK(m.kernel.exited, true);
    CHECK(m.system.memory.read(4, KernelMachine::Results), 10);  //one read per frame, the tenth with the cross
  }
}

//Two threads reading at once: the PSP lets one wait for the next sample, and refuses the other.
static auto controllerTwoReaders() -> void {
  KernelMachine m;
  for(u32 n : {0u, 1u}) {
    Assembler reader{m, 0x0880'1000 + n * 0x100};
    reader.li(a0, Buffer + n * 16); reader.li(a1, 1);
    reader.call("sceCtrlReadBufferPositive");
    reader.li(t0, KernelMachine::Results + n * 4); reader.put(sw(v0, 0, t0));
    reader.put(addu(a0, zero, zero)); reader.call("sceKernelExitThread");
  }
  m.system.power(0x0880'1000);
  for(u32 n : {0u, 1u}) {  //the first has the higher priority, so it's first to read
    s32 uid = m.kernel.createThread("reader", 0x0880'1000 + n * 0x100, 0x20 + n, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
  }
  m.kernel.run(Kernel::VblankCycles * 2);
  CHECK(m.system.memory.read(4, KernelMachine::Results + 0), 1);  //waited, then had the next sample
  CHECK(m.system.memory.read(4, KernelMachine::Results + 4), Kernel::ErrorEventFlagMulti);
}

//tools/psp-test-programs' system program, through newlib: it writes a file and reads it back (whole, and from the
//middle after a seek), reads one the host put there, lists the folder, then waits for the cross button and reports
//the stick.
static auto systemProgram() -> void {
  const char* programs = testPrograms();
  if(!programs) return;
  for(bool recompile : {false, true}) {
    HostFolder stick;
    stick.put("data/given.txt", "given by the host\n");
    std::ifstream stream(std::string(programs) + "/system.elf", std::ios::binary);
    std::vector<u8> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    KernelMachine m;
    m.system.recompiler.enabled = recompile;
    m.kernel.mount("ms0", stick.path.string());
    std::string error;
    CHECK(m.kernel.load(file.data(), file.size(), "ms0:/PSP/GAME/SYSTEM/EBOOT.PBP", error), true);
    while(!m.kernel.exited && m.kernel.vblanks < 200) {
      if(m.kernel.vblanks == 20) {
        m.kernel.controller.buttons = 0x4000;
        m.kernel.controller.analogX = 200;
        m.kernel.controller.analogY = 50;
      }
      m.kernel.run(Kernel::VblankCycles);
    }
    CHECK(m.kernel.exited, true);
    std::string expected =
      "read back: written by a PSP program\n"
      "from the middle: a PSP program\n"
      "given: given by the host\n"
      "entry: .\nentry: ..\nentry: given.txt\nentry: written.txt\n";
    CHECK(m.output.substr(0, expected.size()) == expected, true);
    CHECK(m.output.find("stick 200,50\n") != std::string::npos, true);
    if(m.output.substr(0, expected.size()) != expected) std::printf("  output: [%s]\n", m.output.c_str());
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
    CHECK(stick.get("data/written.txt") == "written by a PSP program\n", true);
  }
}

//Renaming, as pspautotests' io/file/rename recorded: the file takes the new path's last name in its own folder,
//whatever folder that path names (from ms0:/PSP, "../t2.txt" renamed to "t2a.txt", or "../t3.txt" to
//"ms0:/PSP/t3a.txt", lands in ms0:/); a name taken already, the old one itself among them, FILE_EXISTS (so a new path
//into a folder that isn't there finds the old file's own name taken); another device XDEV; an old file or folder that
//isn't there FILE_NOT_FOUND; wildcards INVALID_ARGUMENT. And Peace Walker's: its working folder on the disc, a
//temporary file on the memory stick renamed to a bare "TDLSFILE.SYS" stays beside it (it was refused as read-only).
static auto fileRename() -> void {
  HostFolder stick;
  for(const char* name : {"t1.txt", "t2.txt", "t3.txt", "PSP/keep", "PSP/SAVEDATA/GAME/TEMP0000.PW0"}) {
    stick.put(name, name);
  }
  auto image = disc_image::makeIso({{"PSP_GAME/USRDIR/DATA.BIN", {1, 2, 3}}});
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  m.kernel.disc = discFrom(image.bytes);
  auto rename = [&](const char* from, const char* to) {
    return m.call("sceIoRename", {m.string(from), m.string(to)});
  };
  auto there = [&](const char* name) { return std::filesystem::exists(stick.path / name); };
  CHECK(rename("ms0:/t1.txt", "ms0:/t1a.txt"), 0);
  CHECK(there("t1a.txt") && !there("t1.txt"), true);
  CHECK(rename("ms0:/t1a.txt", "ms0:/t2.txt"), Kernel::ErrorFileExists);
  CHECK(m.call("sceIoChdir", {m.string("ms0:/PSP")}), 0);
  CHECK(rename("../t2.txt", "t2a.txt"), 0);
  CHECK(there("t2a.txt") && !there("PSP/t2a.txt"), true);  //its own folder, not the working one
  CHECK(rename("../t3.txt", "ms0:/PSP/t3a.txt"), 0);
  CHECK(there("t3a.txt") && !there("PSP/t3a.txt"), true);  //nor the one the new path names
  stick.put("t1.txt", "again");
  stick.put("t2.txt", "again");
  CHECK(rename("ms0:/t1.txt", "host0:/t1.txt"), Kernel::ErrorCrossDevice);
  CHECK(rename("ms0:/t2.txt", "ms0:/NOT_THERE/t2.txt"), Kernel::ErrorFileExists);
  CHECK(rename("ms0:/NOT_THERE/t3.txt", "ms0:/t3.txt"), Kernel::ErrorFileNotFound);
  CHECK(rename("ms0:/t3.txt", "ms0:/t3b.txt"), Kernel::ErrorFileNotFound);
  CHECK(rename("ms0:/t1.txt", "ms0:/t1.txt"), Kernel::ErrorFileExists);
  CHECK(rename("ms0:/t*.txt", "ms0:/t1b.txt"), Kernel::ErrorInvalidArgument);
  CHECK(rename("ms0:/t1.txt", "ms0:/t*.txt"), Kernel::ErrorInvalidArgument);
  CHECK(rename("ms0:/t?.txt", "ms0:/t?.txt"), Kernel::ErrorInvalidArgument);
  CHECK(stick.get("t1.txt") == "again" && stick.get("t2.txt") == "again", true);  //nothing refused moved
  CHECK(m.call("sceIoChdir", {m.string("disc0:/PSP_GAME/USRDIR")}), 0);
  CHECK(rename("ms0:/PSP/SAVEDATA/GAME/TEMP0000.PW0", "TDLSFILE.SYS"), 0);
  CHECK(there("PSP/SAVEDATA/GAME/TDLSFILE.SYS") && !there("PSP/SAVEDATA/GAME/TEMP0000.PW0"), true);
  CHECK(rename("disc0:/PSP_GAME/USRDIR/DATA.BIN", "OTHER.BIN"), Kernel::ErrorReadOnly);
  CHECK(roundTrip(m, [&](KernelMachine& n) {
    n.kernel.mount("ms0", stick.path.string());
    n.kernel.disc = discFrom(image.bytes);
  }), true);
}

//scePspNpDrm_user without DRM: a licensee key set and cleared, a name checked, and a game's own file on the stick
//readied and measured while open (its size, as nothing is decrypted), then read as it is; a file not open, or a
//folder, is BAD_FILE.
static auto downloadDrm() -> void {
  HostFolder folder;
  folder.put("PSP/GAME/NPUH00000/DATA.EDAT", "the game's own data");
  KernelMachine m;
  m.kernel.mount("ms0", folder.path.string());
  m.system.memory.fill(Buffer, 0x5a, 16);
  CHECK(m.call("sceNpDrmSetLicenseeKey", {Buffer}), 0);
  CHECK(m.call("sceNpDrmRenameCheck", {m.string("ms0:/PSP/GAME/NPUH00000/DATA.EDAT")}), 0);
  u32 file = m.call("sceIoOpen", {m.string("ms0:/PSP/GAME/NPUH00000/DATA.EDAT"), 0x0001, 0});
  CHECK(file >= 3 && file < 0x8000'0000, true);
  CHECK(m.call("sceNpDrmEdataSetupKey", {file}), 0);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {file}), 19);
  CHECK(m.call("sceIoRead", {file, Buffer, 64}), 19);
  CHECK(readBack(m, 19) == "the game's own data", true);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {file}), 19);  //wherever it's read to
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(m.call("sceNpDrmEdataSetupKey", {file}), Kernel::ErrorBadFile);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {file}), Kernel::ErrorBadFile);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {0xdead'beef}), Kernel::ErrorBadFile);
  u32 directory = m.call("sceIoDopen", {m.string("ms0:/PSP/GAME/NPUH00000")});
  CHECK(directory < 0x8000'0000, true);
  CHECK(m.call("sceNpDrmEdataSetupKey", {directory}), Kernel::ErrorBadFile);
  CHECK(m.call("sceNpDrmEdataGetDataSize", {directory}), Kernel::ErrorBadFile);
  CHECK(m.call("sceNpDrmClearLicenseeKey", {}), 0);
  CHECK(m.notes.size(), 0);
}

//Descriptors handed out: the lowest number free, from 3 (io/open/tty0 recorded small numbers counting up), a closed
//file's number given to the next file or folder, whatever it is; an asynchronous open's descriptor holding its
//number until its result is taken; the movie player's file keeping its number while the player has a movie, and a
//thread's put-aside wait for a request keeping its file's; once 3 to 63 are all in use, nothing more opens until one
//is closed; and a file numbered past them (an older state's) kept. (Crush indexes a table of 16 by them.)
static auto fileNumbers() -> void {
  HostFolder stick;
  stick.put("A.TXT", "a");
  stick.put("B.TXT", "b");
  auto image = disc_image::makeIso({{"DATA.BIN", std::vector<u8>(100, 1)}});
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  m.kernel.disc = discFrom(image.bytes);
  auto open = [&](const char* path) { return m.call("sceIoOpen", {m.string(path), 0x0001, 0}); };
  CHECK(open("ms0:/A.TXT"), 3);
  CHECK(open("ms0:/B.TXT"), 4);
  CHECK(m.call("sceIoDopen", {m.string("ms0:/")}), 5);
  CHECK(open("disc0:/DATA.BIN"), 6);
  CHECK(m.call("sceIoClose", {4}), 0);
  CHECK(open("disc0:/DATA.BIN"), 4);  //the lowest free, whatever it was before
  CHECK(m.call("sceIoDclose", {5}), 0);
  CHECK(m.call("sceIoClose", {3}), 0);
  CHECK(m.call("sceIoDopen", {m.string("disc0:/")}), 3);
  CHECK(open("ms0:/MISSING.TXT"), Kernel::ErrorFileNotFound);  //a failed open takes none
  CHECK(open("ms0:/A.TXT"), 5);
  CHECK(m.call("sceIoOpenAsync", {m.string("ms0:/MISSING.TXT"), 0x0001, 0}), 7);  //holds its error's number
  CHECK(open("ms0:/B.TXT"), 8);
  m.kernel.run(Kernel::VblankCycles);
  CHECK(m.call("sceIoPollAsync", {7, Buffer}), 0);
  CHECK(s32(m.system.memory.read(4, Buffer)), s32(Kernel::ErrorFileNotFound));
  CHECK(open("ms0:/A.TXT"), 7);  //once its result is taken
  m.kernel.psmfPlayer.status = 2;  //the movie player has a movie through 9 (its file dropped as a state loaded)
  m.kernel.psmfPlayer.file = 9;
  CHECK(open("ms0:/A.TXT"), 10);
  m.kernel.psmfPlayer = {};
  CHECK(open("ms0:/A.TXT"), 9);
  for(u32 number = 11; number < Kernel::MostFiles; number++) CHECK(open("ms0:/A.TXT"), number);
  CHECK(open("ms0:/A.TXT"), Kernel::ErrorTooManyFiles);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/NEW.TXT"), 0x0602, 0}), Kernel::ErrorTooManyFiles);
  CHECK(std::filesystem::exists(stick.path / "NEW.TXT"), false);  //refused before anything was made
  CHECK(m.call("sceIoDopen", {m.string("ms0:/")}), Kernel::ErrorTooManyFiles);
  CHECK(open("disc0:/DATA.BIN"), Kernel::ErrorTooManyFiles);
  CHECK(m.call("sceIoDopen", {m.string("disc0:/")}), Kernel::ErrorTooManyFiles);
  CHECK(m.call("sceIoOpenAsync", {m.string("ms0:/A.TXT"), 0x0001, 0}), Kernel::ErrorTooManyFiles);
  CHECK(m.call("sceIoOpenAsync", {m.string("ms0:/MISSING.TXT"), 0x0001, 0}), Kernel::ErrorTooManyFiles);
  CHECK(m.call("sceIoClose", {40}), 0);
  CHECK(open("ms0:/B.TXT"), 40);
  //numbered past them, as a state of an older layout may have a file: kept, its old number free again
  auto node = m.kernel.files.extract(40);
  node.key() = 1000;
  m.kernel.files.insert(std::move(node));
  CHECK(open("ms0:/B.TXT"), 40);
  //a number a thread's wait for a request still names isn't given to another: the thread made ready to run its
  //callbacks, its wait kept, then running them with the wait put aside
  CHECK(m.call("sceIoClose", {41}), 0);
  s32 waiter = m.kernel.createThread("waiter", 0x0880'1000, 0x20, 0x1000, 0, 0);
  auto& aside = *m.kernel.threads[waiter];
  aside.status = Kernel::Status::Ready, aside.wait = Kernel::Wait::Async, aside.waitID = 41;
  CHECK(open("ms0:/B.TXT"), Kernel::ErrorTooManyFiles);
  aside.wait = Kernel::Wait::None, aside.waitID = 0, aside.inCallback = true;
  aside.waitBeforeCallback.wait = Kernel::Wait::Async, aside.waitBeforeCallback.id = 41;
  CHECK(open("ms0:/B.TXT"), Kernel::ErrorTooManyFiles);
  aside.status = Kernel::Status::Dormant, aside.inCallback = false, aside.waitBeforeCallback = {};
  CHECK(open("ms0:/B.TXT"), 41);
  CHECK(roundTrip(m, [&](KernelMachine& n) {
    n.kernel.mount("ms0", stick.path.string());
    n.kernel.disc = discFrom(image.bytes);
  }), true);
}

//The descriptors a program has (sceIoGetFdList), as uOFW describes the PSP's kernel listing them: standard input,
//output and error, then every file and folder in the order they were opened, whatever their numbers (a closed file's
//number given again comes last), a descriptor kept for an asynchronous request's result among them until it's taken;
//as many as fit, their count the result, all of them counted where asked (nowhere for NULL); a list or count reaching
//the kernel's half of the addresses, or a negative size, refused. A state keeps the order, and a file opened after
//it loads comes last. (Crush counts its open files so.)
static auto fileList() -> void {
  HostFolder stick;
  stick.put("A.TXT", "a");
  stick.put("B.TXT", "b");
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  constexpr u32 List = Buffer, Count = Buffer + 0x100;
  auto listed = [&](KernelMachine& on, u32 room) {
    on.system.memory.write(4, Count, 0xdead);
    u32 put = on.call("sceIoGetFdList", {List, room, Count});
    std::vector<u32> numbers;
    for(u32 n = 0; n < put && n < 64; n++) numbers.push_back(on.system.memory.read(4, List + n * 4));
    return numbers;
  };
  auto open = [&](KernelMachine& on, const char* path) {
    return on.call("sceIoOpen", {on.string(path), 0x0001, 0});
  };
  CHECK(listed(m, 16) == std::vector<u32>({0, 1, 2}), true);
  CHECK(m.system.memory.read(4, Count), 3);
  CHECK(open(m, "ms0:/A.TXT"), 3);
  CHECK(open(m, "ms0:/B.TXT"), 4);
  CHECK(m.call("sceIoDopen", {m.string("ms0:/")}), 5);
  CHECK(m.call("sceIoClose", {3}), 0);
  CHECK(open(m, "ms0:/B.TXT"), 3);  //the lowest number, opened last
  CHECK(m.call("sceIoOpenAsync", {m.string("ms0:/MISSING.TXT"), 0x0001, 0}), 6);  //holds its error's number
  CHECK(listed(m, 16) == std::vector<u32>({0, 1, 2, 4, 5, 3, 6}), true);
  CHECK(m.system.memory.read(4, Count), 7);
  CHECK(listed(m, 4) == std::vector<u32>({0, 1, 2, 4}), true);  //as many as fit
  CHECK(m.system.memory.read(4, Count), 7);
  CHECK(m.call("sceIoGetFdList", {0, 0, Count}), 0);  //nowhere to put any: counted all the same
  CHECK(m.system.memory.read(4, Count), 7);
  CHECK(m.call("sceIoGetFdList", {List, 16, 0}), 7);  //nowhere for the count
  m.system.memory.write(4, Count, 0xdead);
  CHECK(m.call("sceIoGetFdList", {0x8890'0000, 16, Count}), Kernel::ErrorIllegalAddress);
  CHECK(m.call("sceIoGetFdList", {List, u32(-1), Count}), Kernel::ErrorIllegalAddress);
  CHECK(m.call("sceIoGetFdList", {List, 16, 0x8890'0000}), Kernel::ErrorIllegalAddress);
  CHECK(m.call("sceIoGetFdList", {List, 16, 0x7fff'fffe}), Kernel::ErrorIllegalAddress);  //its 4 bytes reach past
  CHECK(m.system.memory.read(4, Count), 0xdead);
  //a size counted as negative whose descriptors' bytes wrap round below the kernel's half: none fit, all counted
  for(u32 room : {0x8000'0000u, 0xc000'0000u, 0x8000'0001u}) {
    m.system.memory.write(4, List, 0xdead);
    CHECK(m.call("sceIoGetFdList", {List, room, Count}), 0);
    CHECK(m.system.memory.read(4, List), 0xdead);
    CHECK(m.system.memory.read(4, Count), 7);
    m.system.memory.write(4, Count, 0xdead);
  }
  auto state = saveState(m);
  m.kernel.run(Kernel::VblankCycles);
  CHECK(m.call("sceIoPollAsync", {6, Count}), 0);  //the result taken: 6 is no more
  CHECK(listed(m, 16) == std::vector<u32>({0, 1, 2, 4, 5, 3}), true);
  KernelMachine n;
  n.kernel.mount("ms0", stick.path.string());
  CHECK(loadState(n, state), true);
  CHECK(open(n, "ms0:/A.TXT"), 7);
  CHECK(listed(n, 16) == std::vector<u32>({0, 1, 2, 4, 5, 3, 6, 7}), true);
}

//A memory stick file emptied as it's opened to write (PSP_O_TRUNC) holds nothing, but keeps its bytes for a write
//past its end, as PPSSPP's notes on the hardware record (its issue 579, on a PSP's stick): written "test", then
//opened to be emptied again (here with LittleBigPlanet's 0x02000000 too), sought to 4 and written "abc", it reads back
//"testabc"; opened to be emptied and closed with nothing written, it's empty. Meanwhile it holds nothing for its
//status, reads, seeks from its end and writes to its end, its bytes coming back only into a gap a write leaves, and
//only as far as they went (zeros past them). A write from or before its end cuts it there and then, as a save
//rewritten from its start is (so written short of its old length, it ends there), its old bytes gone from gaps after.
//Emptied again while another descriptor keeps its bytes, it's cut to that one's length first. A state keeps an emptied
//file's lengths, and one loaded keeps its cut while the host file is as long as it was; the kernel loading a state
//lets go of the files it had as they are (the system then closes them: ares/system.cpp), and powering on closes them.
static auto fileTruncation() -> void {
  HostFolder stick;
  stick.put("SAVE/DATA.BIN", "0123456789");
  auto mounted = [&](KernelMachine& on) { on.kernel.mount("ms0", stick.path.string()); };
  KernelMachine m;
  mounted(m);
  auto open = [&](KernelMachine& on, u32 flags) {
    return on.call("sceIoOpen", {on.string("ms0:/SAVE/DATA.BIN"), flags, 0777});
  };
  auto write = [&](KernelMachine& on, u32 file, const std::string& text) {
    on.system.memory.copyIn(Buffer, text.data(), text.size());
    return on.call("sceIoWrite", {file, Buffer, u32(text.size())});
  };
  auto status = [&](KernelMachine& on) {  //its status's size
    if(on.call("sceIoGetstat", {on.string("ms0:/SAVE/DATA.BIN"), Buffer + 0x100}) != 0) return ~0u;
    return on.system.memory.read(4, Buffer + 0x108);
  };
  auto size = [&] { return status(m); };
  u32 file = open(m, 0x0602);  //write, create, truncate
  CHECK(m.call("sceIoLseek32", {file, 0, 0}), 0);
  CHECK(write(m, file, "test"), 4);
  CHECK(stick.get("SAVE/DATA.BIN") == "test", true);  //written from its start: cut then
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "test", true);
  file = open(m, 0x0200'0602);
  CHECK(m.call("sceIoLseek32", {file, 4, 0}), 4);
  CHECK(write(m, file, "abc"), 3);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "testabc", true);
  file = open(m, 0x0602);
  CHECK(stick.get("SAVE/DATA.BIN") == "testabc", true);  //nothing written yet: nothing cut
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN").empty(), true);

  stick.put("SAVE/DATA.BIN", "0123456789");
  file = open(m, 0x0603);  //read and write, create, truncate
  CHECK(size(), 0);
  CHECK(m.call("sceIoLseek32", {file, 0, 2}), 0);  //from its end
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 0);
  CHECK(m.call("sceIoLseek32", {file, 3, 0}), 3);
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 0);  //sought into what it keeps: nothing yet
  CHECK(write(m, file, "ab"), 2);  //0 to 2 back
  CHECK(size(), 5);
  CHECK(m.call("sceIoLseek32", {file, 0, 0}), 0);
  CHECK(m.call("sceIoRead", {file, Buffer, 16}), 5);
  CHECK(readBack(m, 5) == "012ab", true);
  CHECK(m.call("sceIoLseek32", {file, 0, 2}), 5);
  CHECK(m.call("sceIoLseek32", {file, 12, 0}), 12);  //past its old length: 5 to 9 back, zeros after them
  CHECK(write(m, file, "Z"), 1);
  CHECK(size(), 13);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == std::string("012ab56789\0\0Z", 13), true);

  stick.put("SAVE/DATA.BIN", "0123456789");
  file = open(m, 0x0603);
  CHECK(m.call("sceIoLseek32", {file, 3, 0}), 3);
  CHECK(write(m, file, "ab"), 2);  //past its end: 0 to 2 back, 5 to 9 kept out of sight
  CHECK(stick.get("SAVE/DATA.BIN") == "012ab56789", true);
  CHECK(m.call("sceIoLseek32", {file, 1, 0}), 1);
  CHECK(write(m, file, "x"), 1);  //before its end: cut to it first
  CHECK(stick.get("SAVE/DATA.BIN") == "0x2ab", true);
  CHECK(m.call("sceIoLseek32", {file, 7, 0}), 7);
  CHECK(write(m, file, "Z"), 1);  //past its end again: zeros, its old bytes gone
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == std::string("0x2ab\0\0Z", 8), true);

  stick.put("SAVE/DATA.BIN", "0123456789");
  file = open(m, 0x0602);
  CHECK(write(m, file, "xy"), 2);
  CHECK(stick.get("SAVE/DATA.BIN") == "xy", true);  //written short: it ends there, then and there
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "xy", true);
  stick.put("SAVE/DATA.BIN", "0123456789");
  file = open(m, 0x0702);  //write, append, create, truncate
  CHECK(write(m, file, "ab"), 2);  //at its end, 0
  CHECK(write(m, file, "cd"), 2);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "abcd", true);

  stick.put("SAVE/DATA.BIN", "0123456789");
  u32 first = open(m, 0x0602);
  CHECK(m.call("sceIoLseek32", {first, 2, 0}), 2);
  CHECK(write(m, first, "ab"), 2);
  CHECK(stick.get("SAVE/DATA.BIN") == "01ab456789", true);  //its bytes past 4 kept on the host until it's cut
  u32 second = open(m, 0x0602);
  CHECK(stick.get("SAVE/DATA.BIN") == "01ab", true);  //the first's length first
  CHECK(m.call("sceIoLseek32", {second, 6, 0}), 6);
  CHECK(write(m, second, "cd"), 2);
  CHECK(m.call("sceIoClose", {second}), 0);
  CHECK(m.call("sceIoClose", {first}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == std::string("01ab\0\0cd", 8), true);
  CHECK(m.notes.size(), 0);

  stick.put("SAVE/DATA.BIN", "0123456789");
  KernelMachine n;
  mounted(n);
  file = open(n, 0x0602);
  CHECK(n.call("sceIoLseek32", {file, 2, 0}), 2);
  CHECK(write(n, file, "ab"), 2);
  auto& emptied = n.kernel.files[file];
  CHECK(emptied.truncated && emptied.length == 4 && emptied.hostLength == 10, true);
  auto during = saveState(n);
  CHECK(roundTrip(n, mounted), true);
  n.kernel.files.clear();
  KernelMachine o;
  mounted(o);
  CHECK(loadState(o, during), true);  //its host file as long as it was: its cut kept
  CHECK(o.kernel.files.count(file) && o.kernel.files[file].truncated && o.kernel.files[file].length == 4, true);
  CHECK(status(o), 4);
  CHECK(o.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "01ab", true);
  for(std::string newer : {"NEWER-SAVE-DATA", "xyz"}) {  //written since, longer or shorter: never cut, nor padded
    stick.put("SAVE/DATA.BIN", newer);
    KernelMachine p;
    mounted(p);
    CHECK(loadState(p, during), true);
    CHECK(p.kernel.files.count(file) && !p.kernel.files[file].truncated, true);
    CHECK(status(p), newer.size());
    CHECK(p.call("sceIoClose", {file}), 0);
    CHECK(stick.get("SAVE/DATA.BIN") == newer, true);
  }

  stick.put("SAVE/DATA.BIN", "0123456789");
  KernelMachine q;
  mounted(q);
  auto before = saveState(q);
  file = open(q, 0x0602);
  CHECK(loadState(q, before), true);  //the file it had let go of as it was
  CHECK(stick.get("SAVE/DATA.BIN") == "0123456789", true);
  file = open(q, 0x0602);
  q.kernel.power();
  CHECK(stick.get("SAVE/DATA.BIN").empty(), true);
}

//Helpers for the emptied files' cases below: a write of text from Buffer, and a file's status size (~0: none).
static auto putText(KernelMachine& m, u32 file, const std::string& text) -> u32 {
  m.system.memory.copyIn(Buffer, text.data(), text.size());
  return m.call("sceIoWrite", {file, Buffer, u32(text.size())});
}
static auto statusSize(KernelMachine& m, const char* path) -> u32 {
  if(m.call("sceIoGetstat", {m.string(path), Buffer + 0x100}) != 0) return ~0u;
  return m.system.memory.read(4, Buffer + 0x108);
}

//An emptied file's cut (io.cpp's cut()) reaches the file it emptied and nothing else. Removed while it's open, its
//cut is dropped, and a file made in its place, as long, is left whole; renamed while open, it follows (its status, its
//cut), and so does one in a folder renamed; replaced behind the kernel's back by another program's file renamed over
//it, as long, the replacement is left whole (the cut goes to the file held, which no path names now); and written over
//by something else, longer or shorter, it's never cut, nor made longer.
static auto fileCutReach() -> void {
  HostFolder stick;
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  auto open = [&](const char* path) { return m.call("sceIoOpen", {m.string(path), 0x0602, 0777}); };
  auto seek = [&](u32 file, u32 to) { return m.call("sceIoLseek32", {file, to, 0}); };
  stick.put("SAVE/A.BIN", "0123456789");
  u32 file = open("ms0:/SAVE/A.BIN");
  CHECK(m.call("sceIoRemove", {m.string("ms0:/SAVE/A.BIN")}), 0);
  u32 made = open("ms0:/SAVE/A.BIN");
  CHECK(putText(m, made, "made after"), 10);
  CHECK(m.call("sceIoClose", {made}), 0);
  CHECK(statusSize(m, "ms0:/SAVE/A.BIN"), 10);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/A.BIN") == "made after", true);

  stick.put("SAVE/B.BIN", "0123456789");
  file = open("ms0:/SAVE/B.BIN");
  CHECK(seek(file, 2), 2);
  CHECK(putText(m, file, "ab"), 2);
  CHECK(m.call("sceIoRename", {m.string("ms0:/SAVE/B.BIN"), m.string("ms0:/SAVE/C.BIN")}), 0);
  CHECK(statusSize(m, "ms0:/SAVE/C.BIN"), 4);
  CHECK(m.kernel.files[file].path == "ms0:/SAVE/C.BIN", true);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/C.BIN") == "01ab", true);
  stick.put("SAVE/D/E.BIN", "0123456789");
  file = open("ms0:/SAVE/D/E.BIN");
  CHECK(m.call("sceIoRename", {m.string("ms0:/SAVE/D"), m.string("ms0:/SAVE/F")}), 0);
  CHECK(m.kernel.files[file].path == "ms0:/SAVE/F/E.BIN", true);
  CHECK(statusSize(m, "ms0:/SAVE/F/E.BIN"), 0);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/F/E.BIN").empty(), true);

  stick.put("SAVE/G.BIN", "0123456789");
  file = open("ms0:/SAVE/G.BIN");
  stick.put("SAVE/G.NEW", "9876543210");
  std::error_code error;
  std::filesystem::rename(stick.path / "SAVE/G.NEW", stick.path / "SAVE/G.BIN", error);
  CHECK(bool(error), false);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/G.BIN") == "9876543210", true);

  for(std::string other : {"0123456789ABCDEF", "xyz"}) {
    stick.put("SAVE/H.BIN", "0123456789");
    file = open("ms0:/SAVE/H.BIN");
    CHECK(seek(file, 2), 2);
    CHECK(putText(m, file, "ab"), 2);
    stick.put("SAVE/H.BIN", other);  //(written over in place)
    CHECK(m.call("sceIoClose", {file}), 0);
    CHECK(stick.get("SAVE/H.BIN") == other, true);
  }
  CHECK(m.notes.size(), 0);
}

//Phobos killed with an emptied file open (no close, no powering off: the process gone, the host closing its files),
//as the next boot finds the file: before anything was written, its bytes all there; written from its start, as far
//as it was written, as emptying it at once would have left it; written past its end only, its bytes all there with
//the new ones over them.
static auto fileCutKilled() -> void {
  HostFolder stick;
  auto killed = [&](const std::function<void(KernelMachine&, u32)>& writes) {
    stick.put("SAVE/DATA.BIN", "0123456789");
    KernelMachine k;
    k.kernel.mount("ms0", stick.path.string());
    u32 file = k.call("sceIoOpen", {k.string("ms0:/SAVE/DATA.BIN"), 0x0602, 0777});
    writes(k, file);
    k.kernel.files.clear();
    return stick.get("SAVE/DATA.BIN");
  };
  CHECK(killed([](KernelMachine&, u32) {}) == "0123456789", true);
  CHECK(killed([](KernelMachine& k, u32 file) { putText(k, file, "ab"); }) == "ab", true);
  CHECK(killed([](KernelMachine& k, u32 file) {
    k.call("sceIoLseek32", {file, 2, 0});
    putText(k, file, "ab");
  }) == "01ab456789", true);
}

//The independent review's cases (part 95's): each what the stick holds as master's immediate emptying left it.
//R1: an emptied file removed while open, another renamed onto its path: the replacement's status is its own, and
//closing the old descriptor leaves it whole.
static auto fileReviewRenameOnto() -> void {
  HostFolder stick;
  stick.put("SAVE/DATA.BIN", "0123456789");
  stick.put("SAVE/NEW.BIN", "NEWSAVE-NEWSAVE-NEWSAVE");
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  u32 file = m.call("sceIoOpen", {m.string("ms0:/SAVE/DATA.BIN"), 0x0602, 0777});
  CHECK(m.call("sceIoRemove", {m.string("ms0:/SAVE/DATA.BIN")}), 0);
  CHECK(m.call("sceIoRename", {m.string("ms0:/SAVE/NEW.BIN"), m.string("ms0:/SAVE/DATA.BIN")}), 0);
  CHECK(statusSize(m, "ms0:/SAVE/DATA.BIN"), 23);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "NEWSAVE-NEWSAVE-NEWSAVE", true);
}

//R2: an emptied file written short, renamed while open, then closed: the renamed file ends where it was written.
static auto fileReviewRenameAway() -> void {
  HostFolder stick;
  stick.put("SAVE/TMP.BIN", "0123456789");
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  u32 file = m.call("sceIoOpen", {m.string("ms0:/SAVE/TMP.BIN"), 0x0602, 0777});
  CHECK(putText(m, file, "ab"), 2);
  CHECK(m.call("sceIoRename", {m.string("ms0:/SAVE/TMP.BIN"), m.string("ms0:/SAVE/FINAL.BIN")}), 0);
  CHECK(statusSize(m, "ms0:/SAVE/FINAL.BIN"), 2);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/FINAL.BIN") == "ab", true);
}

//R3: a save rewritten shorter, not yet closed, then a state made before it loaded: the stick holds the rewrite, and
//the game reads it back.
static auto fileReviewStateMidRewrite() -> void {
  HostFolder stick;
  stick.put("SAVE/DATA.BIN", "0123456789");
  KernelMachine n;
  n.kernel.mount("ms0", stick.path.string());
  auto before = saveState(n);
  u32 file = n.call("sceIoOpen", {n.string("ms0:/SAVE/DATA.BIN"), 0x0602, 0777});
  CHECK(putText(n, file, "ab"), 2);
  CHECK(loadState(n, before), true);
  CHECK(stick.get("SAVE/DATA.BIN") == "ab", true);
  u32 again = n.call("sceIoOpen", {n.string("ms0:/SAVE/DATA.BIN"), 0x0001, 0});
  CHECK(n.call("sceIoRead", {again, Buffer, 64}), 2);
  CHECK(n.call("sceIoClose", {again}), 0);
}

//R4: a state made after a shorter rewrite's writes, before its close; the game saves again, longer; the state loaded,
//the game closes the descriptor it restored: the newer save is left whole.
static auto fileReviewStateNewerSave() -> void {
  HostFolder stick;
  stick.put("SAVE/DATA.BIN", "0123456789");
  KernelMachine n;
  n.kernel.mount("ms0", stick.path.string());
  u32 file = n.call("sceIoOpen", {n.string("ms0:/SAVE/DATA.BIN"), 0x0602, 0777});
  CHECK(putText(n, file, "ab"), 2);
  auto during = saveState(n);
  CHECK(n.call("sceIoClose", {file}), 0);
  u32 newer = n.call("sceIoOpen", {n.string("ms0:/SAVE/DATA.BIN"), 0x0602, 0777});
  CHECK(putText(n, newer, "NEWER-SAVE-DATA"), 15);
  CHECK(n.call("sceIoClose", {newer}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "NEWER-SAVE-DATA", true);
  KernelMachine o;
  o.kernel.mount("ms0", stick.path.string());
  CHECK(loadState(o, during), true);
  CHECK(statusSize(o, "ms0:/SAVE/DATA.BIN"), 15);
  CHECK(o.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "NEWER-SAVE-DATA", true);
}

//R5: the host file shortened behind an emptied descriptor's back (a newer, shorter save under a state; then within a
//session, as a savedata utility's rewrite would): closing the descriptor never pads it.
static auto fileReviewNeverLonger() -> void {
  HostFolder stick;
  stick.put("SAVE/DATA.BIN", "0123456789");
  KernelMachine n;
  n.kernel.mount("ms0", stick.path.string());
  u32 file = n.call("sceIoOpen", {n.string("ms0:/SAVE/DATA.BIN"), 0x0602, 0777});
  CHECK(putText(n, file, "abcdefgh"), 8);
  auto during = saveState(n);
  CHECK(n.call("sceIoClose", {file}), 0);
  stick.put("SAVE/DATA.BIN", "xyz");
  KernelMachine o;
  o.kernel.mount("ms0", stick.path.string());
  CHECK(loadState(o, during), true);
  CHECK(o.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "xyz", true);
  stick.put("SAVE/DATA.BIN", "0123456789");
  KernelMachine p;
  p.kernel.mount("ms0", stick.path.string());
  file = p.call("sceIoOpen", {p.string("ms0:/SAVE/DATA.BIN"), 0x0602, 0777});
  CHECK(putText(p, file, "abcdefgh"), 8);
  stick.put("SAVE/DATA.BIN", "xyz");
  CHECK(p.call("sceIoClose", {file}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "xyz", true);
}

//R6: Phobos killed (no close, nothing powered off) after a shorter rewrite's writes: the next boot finds the rewrite.
static auto fileReviewKilled() -> void {
  HostFolder stick;
  stick.put("SAVE/DATA.BIN", "0123456789");
  {
    KernelMachine k;
    k.kernel.mount("ms0", stick.path.string());
    u32 file = k.call("sceIoOpen", {k.string("ms0:/SAVE/DATA.BIN"), 0x0602, 0777});
    CHECK(putText(k, file, "ab"), 2);
    k.kernel.files.clear();
  }
  CHECK(stick.get("SAVE/DATA.BIN") == "ab", true);
}

//R7: a reader and a plain writer opened before an emptying open. The reader finds the emptied file empty; the writer's
//write at 5 makes it 6 bytes long, its first 5 the old ones come back (as a write past an emptied file's end brings
//them: master, emptying at once, read zeros there); closing the emptying descriptor leaves the 6, and the writer's next
//byte goes after them.
static auto fileReviewTwoHandles() -> void {
  HostFolder stick;
  stick.put("SAVE/DATA.BIN", "0123456789");
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  u32 reader = m.call("sceIoOpen", {m.string("ms0:/SAVE/DATA.BIN"), 0x0001, 0});
  u32 writer = m.call("sceIoOpen", {m.string("ms0:/SAVE/DATA.BIN"), 0x0002, 0});
  u32 emptied = m.call("sceIoOpen", {m.string("ms0:/SAVE/DATA.BIN"), 0x0602, 0777});
  CHECK(m.call("sceIoRead", {reader, Buffer, 16}), 0);
  CHECK(m.call("sceIoLseek32", {writer, 5, 0}), 5);
  CHECK(putText(m, writer, "Z"), 1);
  CHECK(statusSize(m, "ms0:/SAVE/DATA.BIN"), 6);
  CHECK(m.call("sceIoRead", {reader, Buffer, 16}), 6);
  CHECK(readBack(m, 6) == "01234Z", true);
  CHECK(m.call("sceIoClose", {emptied}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "01234Z", true);
  CHECK(putText(m, writer, "Y"), 1);
  CHECK(m.call("sceIoClose", {writer}), 0);
  CHECK(m.call("sceIoClose", {reader}), 0);
  CHECK(stick.get("SAVE/DATA.BIN") == "01234ZY", true);
}

//The file I/O trace (Kernel::ioTrace, the runner's --trace-io): a line for each IoFileMgrForUser call, led by the
//PSP's time and the thread (none here: called directly), with a descriptor's path and position, a read's or write's
//buffer, size and the hash of its bytes, an open's flags and mode, and the answer; an asynchronous request's due time
//and result, and a line as it's done. The calls answer as they do untraced, and nothing is traced without a hook.
static auto fileTrace() -> void {
  HostFolder stick;
  stick.put("A.BIN", std::string(4, '\0'));
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  std::vector<std::string> lines;
  m.kernel.ioTrace = [&](const std::string& line) { lines.push_back(line); };
  u32 file = m.call("sceIoOpen", {m.string("ms0:/A.BIN"), 0x0603, 0644});
  CHECK(file, 3);
  m.system.memory.copyIn(Buffer, "abcd", 4);
  CHECK(m.call("sceIoWrite", {file, Buffer, 4}), 4);
  CHECK(m.call("sceIoLseek32", {file, 0, 0}), 0);
  m.system.memory.fill(Buffer, 0, 8);
  CHECK(m.call("sceIoReadAsync", {file, Buffer, 8}), 0);
  m.kernel.cycles = m.kernel.files[file].asyncDoneAt;
  m.kernel.events();
  CHECK(m.call("sceIoPollAsync", {file, Buffer + 16}), 0);
  CHECK(m.call("sceIoGetstat", {m.string("ms0:/A.BIN"), Buffer + 32}), 0);
  CHECK(m.call("sceIoClose", {file}), 0);
  CHECK(lines.size(), 8);
  if(lines.size() != 8) return;
  auto has = [&](u32 n, const std::string& text) { return lines[n].find(text) != std::string::npos; };
  CHECK(has(0, " -: sceIoOpen \"ms0:/A.BIN\" flags 0x603 (read write create truncate) mode 0644 -> 3"), true);
  CHECK(has(1, ": sceIoWrite fd 3 ms0:/A.BIN @0x0 from 0x8930000 size 0x4 hash ") && has(1, " -> 4"), true);
  CHECK(has(2, ": sceIoLseek32 fd 3 ms0:/A.BIN @0x4 offset 0 whence 0 -> 0"), true);
  CHECK(has(3, ": sceIoReadAsync fd 3 ms0:/A.BIN @0x0 into 0x8930000 size 0x8 -> 0 (due in 101 us, result 0x4)"),
        true);
  CHECK(has(4, ": done fd 3 ms0:/A.BIN @0x4 -> 0x4 hash ") && !has(4, "zeros"), true);
  CHECK(has(5, ": sceIoPollAsync fd 3 ms0:/A.BIN @0x4 -> 0 (result 0x4)"), true);
  CHECK(has(6, ": sceIoGetstat \"ms0:/A.BIN\" -> 0 (size 4)"), true);
  CHECK(has(7, ": sceIoClose fd 3 ms0:/A.BIN @0x4 -> 0"), true);
  CHECK(lines[0].rfind("0 us -: ", 0) == 0 && lines[4].rfind("101 us: done", 0) == 0, true);
  m.kernel.ioTrace = {};
  CHECK(m.call("sceIoOpen", {m.string("ms0:/A.BIN"), 0x0001, 0}), 3);
  CHECK(lines.size(), 8);
  CHECK(stick.get("A.BIN") == "abcd", true);
}

auto fileTests() -> Tests {
  return {
    {"files basics", fileBasics}, {"files folders", fileFolders},
    {"files emptied as they're opened keep their bytes for a write past the end", fileTruncation},
    {"files emptied: the cut reaches only the file emptied", fileCutReach},
    {"files emptied: Phobos killed part way", fileCutKilled},
    {"files review R1: a file renamed onto an emptied file's path", fileReviewRenameOnto},
    {"files review R2: an emptied file renamed while open", fileReviewRenameAway},
    {"files review R3: a state loaded part way through a rewrite", fileReviewStateMidRewrite},
    {"files review R4: a state's emptied file and a newer save", fileReviewStateNewerSave},
    {"files review R5: a file shortened behind an emptied file's back", fileReviewNeverLonger},
    {"files review R6: Phobos killed after a rewrite's writes", fileReviewKilled},
    {"files review R7: a reader and a writer beside an emptying open", fileReviewTwoHandles},
    {"files traced", fileTrace},
    {"files dates in local time, none after the clock or before 1980; the system's time zone", fileDates},
    {"files containment", fileContainment},
    {"files rename", fileRename}, {"files numbered lowest free first", fileNumbers},
    {"files listed in the order they were opened", fileList},
    {"files short names", fileShortNames}, {"controller peek", controllerPeek},
    {"controller right stick", controllerRightStick}, {"controller latch", controllerLatch},
    {"controller new samples", controllerReadNew}, {"controller cycle", controllerCycle},
    {"controller read", controllerRead}, {"controller two readers", controllerTwoReaders},
    {"system program", systemProgram}, {"download DRM on a game's own files", downloadDrm},
  };
}

}
