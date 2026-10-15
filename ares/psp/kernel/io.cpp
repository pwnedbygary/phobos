//Files and folders, and standard input, output and error.
//
//The PSP reaches its storage through devices: "ms0:" is the memory stick (homebrew, saves), "disc0:" the game's disc
//(also called "umd0:"). Here each device stands for a folder on the host (mount()): "ms0:/PSP/SAVEDATA/X/DATA.BIN" is
//that folder's PSP/SAVEDATA/X/DATA.BIN. Paths are worked out here, "." and ".." included, so a path can never climb
//out of its device's folder; and since the PSP's file system (FAT) ignores case, each name is found whatever its case
//on the host. Standard output and error go to output(): a program's printf ends up there.
//
//The disc can instead be an image of one (disc, disc.hpp), when the system puts one in the drive: its files are read
//from the image, and nothing on it can be written. Games also read it by sector numbers rather than names (a path
//"sce_lbn<first sector>_size<bytes>" on disc0:), or through umd0:, the whole disc, read a sector at a time.

enum : u32 {
  StandardInput = 0, StandardOutput = 1, StandardError = 2,
  OpenRead = 0x0001, OpenWrite = 0x0002, OpenAppend = 0x0100, OpenCreate = 0x0200, OpenTruncate = 0x0400,
  OpenExclusive = 0x0800,  //pspiofilemgr_fcntl.h
};

#include "pgd.hpp"
using ares::PlayStationPortable::Pgd;

//NPDRM's (download games') licensee key (sceNpDrm_user): set before a game opens its protected EDATA files, which
//wrap a PGD. The key the game gives is the version key the PGD decrypts with (part that follows); how a download's
//module key is really derived from a console's fixed key isn't emulated yet, so this is the best the key gives.
static u8 npdrmLicenseeKey[16];
static bool npdrmLicenseeKeySet = false;

//The device a name stands for: the PSP knows the disc as umd0: and disc0:, and the memory stick as ms0: and fatms0:.
//memstick: is the stick too: Colin McRae Rally 2005 Plus assigns it to the stick's file system (sceIoAssign()) and
//reads and writes its own files in its save's folder through it; PPSSPP's notes take it as ms0: whatever was
//assigned. Chosen: the stick whether a game assigned it or not, as the kernel keeps no assigned names (they would
//be state).
static auto deviceName(std::string name) -> std::string {
  for(auto& c : name) c = std::tolower(u8(c));
  if(name == "umd0" || name == "umd1" || name == "umd") return "disc0";
  if(name == "fatms0" || name == "ms" || name == "memstick") return "ms0";
  return name;
}

static auto sameName(const std::string& a, const std::string& b) -> bool {
  if(a.size() != b.size()) return false;
  for(size_t n = 0; n < a.size(); n++) if(std::tolower(u8(a[n])) != std::tolower(u8(b[n]))) return false;
  return true;
}

//Whether a name at the memory stick's top names the game data install's copies' folder (Kernel::InstallCopies): in
//any case, and with dots or spaces at its end, which Windows and Linux's FAT driver drop from a name.
static auto copiesName(std::string name) -> bool {
  while(!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
  return sameName(name, Kernel::InstallCopies);
}

//A path's names, after its device: "." and ".." worked out, either slash as a separator. Returns 0, or why the path
//can't be: it climbs above its device's top, or holds a colon (FAT allows none, and a host might read it as a drive).
static auto pathNames(const std::string& rest, std::vector<std::string>& names) -> u32 {
  std::string name;
  for(size_t n = 0; n <= rest.size(); n++) {
    if(n < rest.size() && rest[n] != '/' && rest[n] != '\\') { name.push_back(rest[n]); continue; }
    if(name == "..") {
      if(names.empty()) return Kernel::ErrorFileNotFound;
      names.pop_back();
    } else if(!name.empty() && name != ".") {
      if(name.find(':') != std::string::npos) return Kernel::ErrorInvalidArgument;
      names.push_back(name);
    }
    name.clear();
  }
  return 0;
}

//A new file's number: the lowest not in use, from 3 up (0 to 2 are standard input, output and error). A PSP hands
//out small numbers in turn (pspautotests' io/open/tty0, under PSPLink, got 5, 6, 7 and on); that a closed file's is
//given again, lowest first, is chosen, as a table of descriptors would have it. Games keep tables of their own by
//them: Crush counts each file's bytes read in 16 words just before its file buffer, so a number past 15 counts into
//the data it has just read. Not given to another: the movie player's file's while it has a movie (even if a state
//loaded without the file), and one a thread's wait for a request still names (its file closed while the thread was
//made ready to run its callbacks, or ran them with the wait put aside: resumeWait() finds it gone). A descriptor kept
//for an asynchronous result alone is in use until the result is taken (async.cpp). 0 once 3 to 63 are all in use
//(MostFiles: chosen, no recording shows a PSP's limit): the file can't be opened.
auto Kernel::newFile() -> u32 {
  auto awaited = [&](u32 number) {
    for(auto& [uid, thread] : threads) {
      if(thread->wait == Wait::Async && thread->waitID == number) return true;
      auto& aside = thread->waitBeforeCallback;
      if(thread->inCallback && aside.wait == Wait::Async && aside.id == number) return true;
    }
    return false;
  };
  for(u32 number = 3; number < MostFiles; number++) {
    if(files.count(number) || (psmfPlayer.status >= 2 && psmfPlayer.file == number) || awaited(number)) continue;
    return number;
  }
  return 0;
}

//The folder a program's path is in, written as resolve() writes paths ("ms0:/PSP/GAME/HELLO" for
//ms0:\PSP\GAME\HELLO\EBOOT.PBP), or nothing if the path names no device.
static auto programFolder(const std::string& path) -> std::string {
  auto colon = path.find(':');
  if(colon == std::string::npos) return {};
  std::vector<std::string> names;
  if(pathNames(path.substr(colon + 1), names)) names.clear();
  if(!names.empty()) names.pop_back();  //the program's own file
  std::string folder = deviceName(path.substr(0, colon)) + ":/";
  for(size_t n = 0; n < names.size(); n++) folder += (n ? "/" : "") + names[n];
  return folder;
}

//Lets a device's paths reach a host folder (an empty folder unmounts it). A memory stick put in (as the system powers
//on) has the copies game data installs left in their own folder taken away (install.cpp's installLeftovers(): Phobos
//stopped part way through one), as no install is running then.
auto Kernel::mount(const std::string& device, const std::string& folder) -> void {
  if(folder.empty()) devices.erase(deviceName(device));
  else devices[deviceName(device)] = folder;
  if(deviceName(device) == "ms0" && !folder.empty()) installLeftovers(folder);
}

//A path's device as the program wrote it and the rest of it; a path with no device goes on from the working folder.
//False if there's no device to be had.
auto Kernel::split(const std::string& path, std::string& device, std::string& rest) const -> bool {
  auto colon = path.find(':');
  if(colon == std::string::npos) {
    auto base = workingDirectory.find(':');
    if(base == std::string::npos) return false;
    device = workingDirectory.substr(0, base);
    rest = workingDirectory.substr(base + 1) + "/" + path;
  } else {
    device = path.substr(0, colon);
    rest = path.substr(colon + 1);
  }
  return true;
}

//Whether a path is on the disc image: its device is the disc's, there's an image in the drive, and no host folder
//stands for the disc instead (as a homebrew program's own folder does).
auto Kernel::onDisc(const std::string& path) const -> bool {
  std::string device, rest;
  return disc && split(path, device, rest) && deviceName(device) == "disc0" && !devices.count("disc0");
}

//Whether a path on the disc is through umd0: (or umd1:, umd:), which is the whole disc whatever follows it, read a
//sector at a time, rather than through disc0:, the disc's file system (PPSSPP's notes on the hardware).
static auto throughUmd(const std::string& path) -> bool {
  auto colon = path.find(':');
  if(colon == std::string::npos) return false;  //relative: from the working folder, which is never on umd0:
  std::string device = path.substr(0, colon);
  for(auto& c : device) c = std::tolower(u8(c));
  return device == "umd0" || device == "umd1" || device == "umd";
}

//A path as the program gives it (a device and a path on it, or a path relative to the working folder) to its host
//file, and to its normalized form ("ms0:/PSP/GAME"). Returns 0, or why it can't: no such device, or a path that would
//climb out of its device (by "..", or through a symbolic link in its folder) or names something no PSP file could be
//called. Names not on the host yet (a file about to be created) keep the case given. The game data install's copies'
//folder at the memory stick's top (Kernel::InstallCopies) isn't there for programs, however it's named (copiesName())
//or reached: through a link, or by another name the host's FAT gives it (a short 8.3 name), the path's real place is
//compared with it by name and as a file. Nothing in it is theirs, and the next install takes it all away.
auto Kernel::resolve(const std::string& path, std::string& host, std::string& normalized) -> u32 {
  std::string device, rest;
  if(!split(path, device, rest)) return ErrorFileNotFound;
  device = deviceName(device);
  auto mounted = devices.find(device);
  if(mounted == devices.end()) return ErrorDeviceNotFound;
  std::vector<std::string> names;
  if(u32 error = pathNames(rest, names)) return error;
  if(device == "ms0" && !names.empty() && copiesName(names[0])) return ErrorFileNotFound;
  normalized = device + ":/";
  std::filesystem::path at = mounted->second;
  for(size_t n = 0; n < names.size(); n++) {
    if(n) normalized += "/";
    normalized += names[n];
    std::error_code error;
    if(std::filesystem::exists(at / names[n], error)) { at /= names[n]; continue; }
    std::filesystem::path match;
    if(std::filesystem::is_directory(at, error)) {
      for(auto& entry : std::filesystem::directory_iterator(at, error)) {
        if(sameName(entry.path().filename().string(), names[n])) { match = entry.path(); break; }
      }
    }
    at = match.empty() ? at / names[n] : match;
  }
  //What the path really names, links followed, must still be inside the device's folder.
  std::error_code error;
  auto root = std::filesystem::weakly_canonical(mounted->second, error);
  if(!error && root.filename().empty()) root = root.parent_path();
  auto real = std::filesystem::weakly_canonical(at, error);
  if(error || std::mismatch(root.begin(), root.end(), real.begin(), real.end()).first != root.end()) {
    return ErrorNoPermission;
  }
  if(device == "ms0" && !names.empty() && real != root) {
    auto top = root / *std::next(real.begin(), std::distance(root.begin(), root.end()));
    auto copies = root / InstallCopies;
    if(copiesName(top.filename().string())) return ErrorFileNotFound;
    if(std::filesystem::exists(copies, error) && std::filesystem::equivalent(top, copies, error)) {
      return ErrorFileNotFound;
    }
  }
  host = real.string();
  return 0;
}

//Fills a SceIoStat (pspiofilemgr_stat.h, 88 bytes) for a host file or folder: its mode (FIO_S_IFDIR or FIO_S_IFREG,
//readable, writable and runnable by all, as FAT has no permissions), its attributes (FAT's: 0x10 a folder, 0x20 a
//file, as pspautotests' io/stat recorded a PSP's memory stick), size, and when it was made, read and changed. A file
//its owner may not write is read-only, as sceIoChstat() makes one: the mode without its write bits and the attributes
//with 0x01 (io/stat/readonly: 0x216d and 0x21); a folder never is, as sceIoChstat() makes none. Its last six words
//(st_private) are left as the program had them, as the PSP leaves them (PPSSPP's notes).
auto Kernel::writeStat(u32 address, const std::string& host) -> void {
  std::error_code error;
  bool folder = std::filesystem::is_directory(host, error);
  u64 size = folder ? 0 : std::filesystem::file_size(host, error);
  if(error) size = 0;
  if(!folder) size = fileLength(host, size);
  auto permissions = std::filesystem::status(host, error).permissions();
  bool readOnly = !folder && !error
               && (permissions & std::filesystem::perms::owner_write) == std::filesystem::perms::none;
  memory.fill(address, 0, 64);
  memory.write(4, address + 0, (folder ? 0x11ff : 0x21ff) & ~(readOnly ? 0222 : 0));
  memory.write(4, address + 4, (folder ? 0x0010 : 0x0020) | readOnly);
  memory.write(4, address + 8, u32(size));
  memory.write(4, address + 12, u32(size >> 32));
  writeTimes(address + 16, host);
}

//When a host file or folder was made, read and changed, in seconds since 1970 (all 0 if it isn't there), as
//nall::inode::timestamp() has them: made, where the host keeps it (Windows' ctime; the BSDs' and macOS's birth time,
//unless later than the change), else changed; read no earlier than changed. Through stat() itself: the nall::string
//that nall::inode::timestamp() takes trips GCC 13's -Wmaybe-uninitialized here, which CI's tests build with -Werror.
auto Kernel::hostTimes(const std::string& host) -> std::array<s64, 3> {
  struct stat data{};
  if(stat(host.c_str(), &data)) return {};
  s64 changed = data.st_mtime, made = changed;
  #if defined(PLATFORM_WINDOWS)
  made = data.st_ctime;
  #elif defined(PLATFORM_MACOS) || defined(PLATFORM_BSD)
  if(data.st_birthtime > 0) made = std::min<s64>(data.st_birthtime, changed);
  #endif
  return {made, std::max<s64>(data.st_atime, changed), changed};
}

//When a host file or folder was made, read and changed: three ScePspDateTimes (year, month, day, hour, minute,
//second, then microseconds, left 0), one after another, as a SceIoStat and the savedata utility's lists hold them.
//In local time, the clock's (sceRtcGetCurrentClockLocalTime's) zone: a memory stick's FAT keeps a file's local time,
//no zone. Given in UTC, a save made in the last hours west of Greenwich was dated after the clock's now: After Burner:
//Black Falcon, on a host 4 hours behind UTC, said "No game data was found" for a save made in those 4 hours without
//asking the savedata utility for it, and found it once dated in the past. No date is later than the clock's local
//now either: the PSP's clock (startTime, a whole second, and the time the PSP has run) falls behind the host's while
//the emulation is paused or slower than a PSP, and after a state saved earlier is loaded, and the hour daylight saving
//time gives back comes twice; a file the host changed after the clock's now is dated now. A FAT stick holds no date
//before 1980: a time before then, or none, is 1980-01-01 00:00:00.
auto Kernel::writeTimes(u32 address, const std::string& host) -> void {
  static constexpr s64 From1970To1980 = 315'532'800;  //seconds
  static constexpr u16 Earliest[6] = {1980, 1, 1, 0, 0, 0};
  s64 now = s64((startTime + cycles / (CPUFrequency / 1'000'000)) / 1'000'000), nowLocal = 0;
  bool clocked = localSeconds(now, nowLocal);
  auto times = hostTimes(host);
  for(u32 n : range(3)) {
    u32 at = address + n * 16;
    s64 seconds = times[n], local = 0;
    if(!localSeconds(seconds, local)) local = 0;
    if(clocked && local > nowLocal) seconds = now, local = nowLocal;
    memory.fill(at, 0, 16);
    if(local >= From1970To1980) {
      writeDate(at, u64(seconds) * 1'000'000, true);
    } else if(memory.reaches(at, 16)) {
      for(u32 field : range(6)) memory.write(2, at + field * 2, Earliest[field]);
    }
  }
}

//A path on the disc to what it names, its normalized form ("disc0:/PSP_GAME/SYSDIR") and its names. Returns 0, or
//why it can't.
auto Kernel::resolveOnDisc(const std::string& path, Disc::Entry& entry, std::string& normalized,
                           std::vector<std::string>& names) -> u32 {
  std::string device, rest;
  if(!split(path, device, rest)) return ErrorFileNotFound;
  names.clear();
  if(u32 error = pathNames(rest, names)) return error;
  normalized = "disc0:/";
  for(size_t n = 0; n < names.size(); n++) normalized += (n ? "/" : "") + names[n];
  if(!disc->find(names, entry)) return ErrorFileNotFound;
  return 0;
}

//Fills a SceIoStat for something on the disc: a folder or a file, readable and runnable by all but not writable; its
//size; when it was recorded, as all three times; and its first sector in st_private[0], where games look for it (the
//other five words left as the program had them).
auto Kernel::writeStat(u32 address, const Disc::Entry& entry) -> void {
  memory.fill(address, 0, 64);
  memory.write(4, address + 0, entry.folder ? 0x116d : 0x216d);
  memory.write(4, address + 4, entry.folder ? 0x0015 : 0x0025);
  memory.write(4, address + 8, entry.size);
  for(u32 at : {16u, 32u, 48u}) {  //ScePspDateTime: year, month, day, hour, minute, second
    memory.write(2, address + at + 0, 1900 + entry.date[0]);
    memory.write(2, address + at + 2, entry.date[1]);
    memory.write(2, address + at + 4, entry.date[2]);
    memory.write(2, address + at + 6, entry.date[3]);
    memory.write(2, address + at + 8, entry.date[4]);
    memory.write(2, address + at + 10, entry.date[5]);
  }
  memory.write(4, address + 64, entry.sector);
}

//A run of sectors by number: "sce_lbn" and the first sector, then "_size" somewhere after it and the size in bytes.
//Both numbers are hexadecimal, with or without "0x", and end at the first character that isn't a digit; anything
//after them is ignored, slashes too, and a number with no digits is 0 ("sce_lbn0x10_size0x100", "sce_lbn10_size100",
//even "sce_lbn/_size1/"): PPSSPP's notes on the hardware. Whatever its case.
static auto sectorRun(std::string name, u32& first, u64& size) -> bool {
  for(auto& c : name) c = std::tolower(u8(c));
  if(name.compare(0, 7, "sce_lbn")) return false;
  auto sizeAt = name.find("_size", 7);
  if(sizeAt == std::string::npos) return false;
  auto number = [&](size_t at) -> u64 {
    if(!name.compare(at, 2, "0x")) at += 2;
    u64 value = 0;
    for(; at < name.size() && std::isxdigit(u8(name[at])) && value <= 0xffff'ffff; at++) {
      value = value * 16 + (std::isdigit(u8(name[at])) ? name[at] - '0' : name[at] - 'a' + 10);
    }
    return value;
  };
  u64 sector = number(7);
  if(sector > 0xffff'ffff) return false;
  first = u32(sector);
  size = number(sizeAt + 5);
  return true;
}

//Whether a path on disc0: names a run of sectors (sectorRun()), from its names after the device put back together.
auto Kernel::runOnDisc(const std::string& path, u32& first, u64& bytes) const -> bool {
  std::string device, rest;
  std::vector<std::string> names;
  if(!split(path, device, rest) || pathNames(rest, names) || names.empty()) return false;
  std::string joined;
  for(size_t n = 0; n < names.size(); n++) joined += (n ? "/" : "") + names[n];
  return sectorRun(joined, first, bytes);
}

//Opens something on the disc: through umd0:, the whole disc, whatever the path; through disc0:, a run of sectors by
//number (sectorRun()), as games read their data, or a file by its path. Nothing on a disc can be written, and opening
//to write fails; other flags (creating, emptying, adding to the end) are let be (PPSSPP's notes on the hardware).
//Returns the descriptor, or an error.
auto Kernel::openOnDisc(const std::string& path, u32 flags) -> u32 {
  if(flags & OpenWrite) return ErrorInvalidFlag;
  if(!(flags & OpenRead)) return ErrorInvalidArgument;
  std::string device, rest;
  split(path, device, rest);
  std::vector<std::string> names;
  if(u32 error = pathNames(rest, names)) return error;
  OpenFile open;
  open.flags = flags;
  open.onDisc = true;
  u32 first = 0;
  u64 bytes = 0;
  if(throughUmd(path)) {
    for(auto& c : device) c = std::tolower(u8(c));
    open.path = device + ":";
    open.sectors = true;
    open.size = disc->sectors();
  } else if(runOnDisc(path, first, bytes)) {
    if(first > disc->sectors()) return ErrorFileNotFound;
    open.path = "disc0:/" + names[0];
    open.sector = first;
    open.size = bytes;
  } else {
    Disc::Entry entry;
    std::string normalized;
    if(u32 error = resolveOnDisc(path, entry, normalized, names)) return error;
    if(entry.folder) return ErrorIsDirectory;
    open.path = normalized;
    open.sector = entry.sector;
    open.size = entry.size;
  }
  u32 file = newFile();
  if(!file) return ErrorTooManyFiles;
  open.opened = ++filesOpened;
  files[file] = std::move(open);
  return file;
}

auto Kernel::sceKernelStdin() -> void { result(StandardInput); }
auto Kernel::sceKernelStdout() -> void { result(StandardOutput); }
auto Kernel::sceKernelStderr() -> void { result(StandardError); }

//Whether a host path is folder's, or inside it (both as resolve() makes them).
static auto hostWithin(const std::string& path, const std::string& folder) -> bool {
  if(folder.empty() || path.compare(0, folder.size(), folder)) return false;
  return path.size() == folder.size() || path[folder.size()] == '/' || path[folder.size()] == '\\';
}

//Whether a PSP path is folder's, or inside it, whatever the case of either (a FAT stick's names ignore it).
static auto pathWithin(const std::string& path, const std::string& folder) -> bool {
  if(folder.empty() || path.size() < folder.size() || !sameName(path.substr(0, folder.size()), folder)) return false;
  return path.size() == folder.size() || path[folder.size()] == '/';
}

//Whether two host paths name one file: the very path, or another name for it (a hard link, or the other spelling of
//a host whose names ignore case and whose paths keep a name as it was asked for), by the host's own identity of it.
static auto sameHostFile(const std::string& a, const std::string& b) -> bool {
  if(a == b) return true;
  std::error_code error;
  return std::filesystem::equivalent(a, b, error) && !error;
}

//Opens a file (path, flags): its descriptor, or an error. Flags (PSP_O_*) say whether it's read, written or both;
//whether it's created if missing, emptied, or only created if it isn't there already; and whether writes go to its
//end. sceIoOpen, and sceIoOpenAsync (async.cpp).
//
//A memory stick file emptied as it's opened to write (PSP_O_TRUNC) holds nothing from then on, but its bytes aren't
//lost: a write past its end, after a seek, finds them still there between the end and the write, as PPSSPP's notes on
//the hardware record (its issue 579: on a PSP's stick, a file written "test", then opened to be emptied again, sought
//to 4 and written "abc", read back "testabc"; opened to be emptied and closed with nothing written, it was empty).
//LittleBigPlanet installs so: its file library opens GUI.ARC to be emptied for each 1 MiB it writes, seeks to where
//that MiB goes and writes it, and finds the whole file there when it reads it back (emptied for good, it found its
//first 4 MiB zero, and called its data corrupt). So the host file keeps its bytes, and the descriptor counts the
//PSP's length of the file (truncated, length), from 0: what reads, seeks from the end and the file's status see,
//raised by each write past it. Past the bytes it had, a gap left by a write past the end holds zeros (chosen: a FAT
//stick's would hold whatever its clusters held). The host file is cut to that length (cut()) as the descriptor is
//closed, or as soon as a write starts at or before the length, as a save rewritten from its start does (writeFile()):
//chosen, not measured, so that Phobos killed part way, which cuts nothing, leaves no worse than emptying the file at
//once would (a save rewritten from its start as far as it got), and the old bytes whole if nothing had been written.
//LittleBigPlanet writes each MiB of its install but the first past the length, and its profile saves (STORY.SLV,
//PS.BIN and others, emptied as they're opened) from their starts. An empty file has nothing to keep, and one emptied
//while another descriptor still keeps its bytes is cut to that one's length first: one length at a time.
auto Kernel::openFile(const std::string& path, u32 flags) -> u32 {
  std::string host, normalized;
  if(onDisc(path)) return openOnDisc(path, flags);
  if(u32 error = resolve(path, host, normalized)) return error;
  bool read = flags & OpenRead, write = flags & OpenWrite;
  if(!read && !write) return ErrorInvalidArgument;
  std::error_code error;
  bool exists = std::filesystem::exists(host, error);
  if(exists && std::filesystem::is_directory(host, error)) return ErrorIsDirectory;
  if(exists && (flags & OpenCreate) && (flags & OpenExclusive)) return ErrorFileExists;
  if(!exists && !(write && (flags & OpenCreate))) return ErrorFileNotFound;
  auto mode = std::ios::binary | std::ios::in;
  if(write) mode |= std::ios::out;
  if(write && !exists) mode |= std::ios::trunc;  //made
  u32 file = newFile();
  if(!file) return ErrorTooManyFiles;  //before the file is made or emptied
  bool emptied = write && exists && (flags & OpenTruncate);
  if(emptied) {
    for(auto& [other, open] : files) if(open.truncated && sameHostFile(open.host, host)) cut(open);
  }
  auto stream = std::make_unique<std::fstream>(host, mode);
  if(!stream->is_open()) return ErrorNoPermission;
  auto& open = files[file];
  open.path = normalized;
  open.host = host;
  open.flags = flags;
  open.stream = std::move(stream);
  open.opened = ++filesOpened;
  if(!emptied) return file;
  open.truncated = true;
  s64 bytes = hold(open);
  if(bytes > 0) open.length = 0, open.hostLength = bytes;
  else dropCut(open);
  if(bytes < 0) std::filesystem::resize_file(host, 0, error);  //(not held, so emptied now)
  return file;
}

//The PSP's length of a host file the host has hostBytes of: those, unless a descriptor open on it has emptied it
//(openFile()), whose length it is then (no more than the host has, if the host file was cut behind its back).
auto Kernel::fileLength(const std::string& host, u64 hostBytes) -> u64 {
  for(auto& [file, open] : files) {
    if(open.truncated && sameHostFile(open.host, host)) return std::min(open.length, hostBytes);
  }
  return hostBytes;
}

//A write to a host file that ended at end: an emptied file's length (openFile()) grows to it, and so does the host
//file's as the kernel left it. Once its length reaches the host file's, there's nothing past it left to cut.
auto Kernel::fileWritten(const std::string& host, u64 end) -> void {
  for(auto& [file, open] : files) {
    if(!open.truncated || !sameHostFile(open.host, host)) continue;
    open.length = std::max(open.length, end);
    open.hostLength = std::max(open.hostLength, end);
    if(open.length >= open.hostLength) dropCut(open);
  }
}

//An emptied file's host file held to be cut (openFile(), and a state's emptied file opened again: serialization.cpp):
//opened again, beside its stream, through the C library, as cut() shortens it through that. Its length, or -1 if it
//can't be held.
auto Kernel::hold(OpenFile& open) -> s64 {
  open.held.reset(std::fopen(open.host.c_str(), "rb+"));
  if(!open.held || std::fseek(open.held.get(), 0, SEEK_END)) return -1;
  return std::ftell(open.held.get());
}

//The bytes an emptied file's cut would drop (from the PSP's length of it to the host file's length as the kernel left
//it), read through the held file: their FNV-1a hash, or none if they can't all be read. A state records it, and keeps
//the cut only while the host file still holds those bytes (serialization.cpp): a newer save as long isn't cut.
auto Kernel::hashTail(OpenFile& open) -> std::optional<u64> {
  std::FILE* held = open.held.get();
  if(!held || open.length >= open.hostLength) return {};
  if(open.stream) open.stream->flush();
  if(std::fseek(held, long(open.length), SEEK_SET)) return {};
  u64 hash = 0xcbf2'9ce4'8422'2325;
  std::vector<u8> chunk(64_KiB);
  for(u64 left = open.hostLength - open.length; left;) {
    u64 got = std::fread(chunk.data(), 1, std::min<u64>(left, chunk.size()), held);
    if(!got) return {};
    for(u64 n = 0; n < got; n++) hash = (hash ^ chunk[n]) * 0x100'0000'01b3;
    left -= got;
  }
  return hash;
}

//An emptied file cut (openFile()): its host file shortened to the PSP's length of it, dropping the bytes it kept past
//them. Through the file held, never a path: a file renamed meanwhile is cut where it went, one removed (and whatever
//was put in its place) never. Only while the host file has the length the kernel left it (hostLength: one written
//longer or shorter by something else since isn't cut), and only ever shorter; the cut made or not, the descriptor is a
//plain one from then on.
auto Kernel::cut(OpenFile& open) -> void {
  if(!open.truncated) return;
  if(open.stream) open.stream->flush();
  std::FILE* held = open.held.get();
  long bytes = held && !std::fseek(held, 0, SEEK_END) ? std::ftell(held) : -1;
  if(bytes >= 0 && u64(bytes) == open.hostLength && u64(bytes) > open.length) {
    #if defined(_WIN32)
    if(_chsize_s(_fileno(held), s64(open.length))) {}  //(refused: the bytes stay, nothing lost)
    #else
    if(ftruncate(fileno(held), off_t(open.length))) {}
    #endif
  }
  dropCut(open);
}

//An emptied file's cut dropped, not made: the descriptor is a plain one, the host file as it is.
auto Kernel::dropCut(OpenFile& open) -> void {
  open.held.reset();
  open.truncated = false, open.length = 0, open.hostLength = 0, open.tailHash = 0;
}

//A host file, or folder, that something other than its descriptors removed, replaced or wrote (sceIoRemove, the
//savedata utility): the cuts of the descriptors that emptied it, or a file in it, dropped. What's at its path isn't
//theirs to cut, and what they emptied is gone or written over.
auto Kernel::dropCuts(const std::string& host) -> void {
  for(auto& [file, open] : files) {
    if(open.truncated && (hostWithin(open.host, host) || sameHostFile(open.host, host))) dropCut(open);
  }
}

//A file or folder renamed (sceIoRename): the descriptors open on it, or on what's in it, follow it to its new path,
//as their host files do (a state opens them again there, and an emptied file's length goes with it). The PSP's paths
//match whatever their case, as a FAT stick's names do: the rename may spell them otherwise than the open did. (A host
//path the host spelled otherwise, as a host whose names ignore case may keep them, is found again from the new one.)
auto Kernel::filesMoved(const std::string& oldHost, const std::string& oldPath, const std::string& newHost,
                        const std::string& newPath) -> void {
  for(auto& [file, open] : files) {
    if(open.onDisc) continue;
    bool hosted = hostWithin(open.host, oldHost), named = pathWithin(open.path, oldPath);
    if(hosted) open.host = newHost + open.host.substr(oldHost.size());
    if(named) open.path = newPath + open.path.substr(oldPath.size());
    std::string host, normalized;
    if(named && !hosted && !resolve(open.path, host, normalized)) open.host = host;
  }
}

//A host file closed: its stream flushed and let go of, and if it was emptied as it was opened (openFile()), cut
//(cut()). As the program closes it (sceIoClose, sceIoCloseAsync); as the kernel powers on (a program ending or another
//starting, the machine switched off), which closes every file the program had, as a PSP's kernel does as a program
//ends; and as a state is loaded over the program (System::unserialize(), once the state has loaded).
auto Kernel::closeFile(OpenFile& open) -> void {
  open.stream.reset();
  cut(open);
}

//(path, flags, mode): a file's descriptor.
auto Kernel::sceIoOpen() -> void {
  result(openFile(memory.readString(arg(0), 1024), arg(1)));
}

//(file): closed, unless an asynchronous request on it is still under way (ASYNC_BUSY). A descriptor kept only for
//an asynchronous request's result goes too, its result never taken.
auto Kernel::sceIoClose() -> void {
  if(arg(0) <= StandardError) return result(0);
  auto found = files.find(arg(0));
  if(found == files.end() || found->second.folder) return result(ErrorBadFile);
  if(asyncBusy(arg(0))) return result(ErrorAsyncBusy);
  closeFile(found->second);
  files.erase(found);
  result(0);
}

//Reads from an open file into the program's memory: size bytes (or sectors, for umd0: and runs of sectors opened
//through it), fewer at the end. Returns how many, or an error. (An asynchronous read counts them as it's made, and
//moves them as it's done: async.cpp.)
auto Kernel::readFile(u32 file, u32 data, u32 size) -> u32 {
  if(file == StandardInput) return 0;  //nothing to read
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || !(found->second.flags & OpenRead)) return ErrorBadFile;
  auto& open = found->second;
  u64 count = 0;
  if(u32 error = readCount(open, data, size, count)) return error;
  s64 moved = readMove(open, open.position, count, data);
  if(moved < 0) return ErrorIOError;
  if(open.pgd) pgdDecryptRead(open, data, u64(moved));
  open.position += moved;
  return u32(moved);
}

//How long an open file is, for its PGD: a disc file's size, or a memory stick file's length as the PSP sees it, as
//its reads, seeks and status do (fileLength(): the host file's, unless a descriptor emptied it as it was opened and
//keeps its bytes). open.size is kept for the disc's files alone, and is 0 for the memory stick's.
auto Kernel::pgdFileLength(OpenFile& open) -> u64 {
  if(open.onDisc) return open.size;
  open.stream->flush();
  open.stream->clear();
  open.stream->seekg(0, std::ios::end);
  s64 length = s64(open.stream->tellg());
  open.stream->clear();
  return length > 0 ? fileLength(open.host, u64(length)) : 0;
}

//size bytes of an open file from at on, wherever it lies (the disc, or the host): false if they can't all be read,
//as none past its length (pgdFileLength()) can.
auto Kernel::pgdFileRead(OpenFile& open, u64 at, u64 size, u8* bytes) -> bool {
  u64 length = pgdFileLength(open);
  if(at > length || size > length - at) return false;
  if(open.onDisc) return disc && disc->read(u64(open.sector) * Disc::SectorSize + at, size, bytes);
  open.stream->clear();
  open.stream->seekg(std::streamoff(at));
  open.stream->read(reinterpret_cast<char*>(bytes), size);
  bool got = u64(open.stream->gcount()) == size;
  open.stream->clear();
  return got;
}

//A PGD file's header descriptor, decrypted once (pgd.cpp): why it can't, or nothing. Until then, pgdDataOffset says
//where the PGD itself starts in the file: 0 for a UMD data file, which is a PGD whole; past the "\0PSPEDAT" header for
//an NPDRM EDATA (sceNpDrmEdataSetupKey); or where ioctl 0x04100002 put it. The PSP's PGD layer reads the 0x90-byte
//header there, and the descriptor's data offset counts from there too ("tpu"'s decompilation of it: the data at the
//PGD's offset plus the descriptor's, the table of block MACs past it), so once the descriptor is read pgdDataOffset
//says where the data starts: 0x90 for a UMD file, 0x120 for a download's EDATA.
auto Kernel::pgdDescriptor(OpenFile& open) -> std::string {
  u64 at = open.pgdDataOffset, length = pgdFileLength(open);
  if(at > length) return "a PGD that starts past its file's end";
  u8 header[0x90] = {};
  u64 read = std::min<u64>(sizeof(header), length - at);  //the header is 0x90 bytes, or the file if it's shorter
  if(!pgdFileRead(open, at, read, header)) return "couldn't read a PGD file's header";
  //The PGD's own size, from its start to the file's end, not how much of its header was read: the descriptor says
  //where the data starts and how long it is, and that is checked against the whole PGD (pgd.cpp). Handed the 0x90 read
  //above, every PGD file would be refused for holding more data than its own header.
  Pgd::Descriptor descriptor;
  auto why = Pgd::descriptor(header, length - at, open.pgdVersionKey, descriptor);
  if(!why.empty()) return why;
  //the key the descriptor came out under, which the data must also be decrypted with (pgd.cpp may have recovered it
  //from the header's own MAC when the key the game gave didn't match).
  memcpy(open.pgdVersionKey, descriptor.versionKey, 16);
  open.pgdDataOffset = u32(at + descriptor.dataOffset);
  open.pgdDataSize = descriptor.dataSize;
  open.pgdBlockSize = descriptor.blockSize;
  memcpy(open.pgdDataKey, descriptor.dataKey, 16);
  open.pgdReady = true;
  return {};
}

//A PGD file's reads: unlocked with its key (ioctl 0x04100001), the file reads as its decrypted data, its 0x90-byte
//header (and an EDATA's own before it) hidden: file offset p is data byte p, "CPK " and all. The ciphertext lives
//behind the header, so it's read from there and decrypted over what the read moved. readCount() already clamped the
//read to the data, so length here is the whole count whenever the position is inside the data. Set up the descriptor
//first.
auto Kernel::pgdDecryptRead(OpenFile& open, u32 data, u64 count) -> void {
  if(count == 0 || !open.pgd) return;
  if(!open.pgdReady) {
    if(auto why = pgdDescriptor(open); !why.empty()) return;  //leave the reads as they are
  }
  if(u64(open.position) >= open.pgdDataSize) return;  //past the data: nothing decrypted
  u64 length = std::min<u64>(count, u64(open.pgdDataSize) - open.position);
  //The data's ciphertext, at its place behind the header: read again, as the read moved the file's own bytes.
  std::vector<u8> cipher(length);
  bool got = false;
  if(open.onDisc) {
    got = disc->read(u64(open.sector) * Disc::SectorSize + u64(open.pgdDataOffset) + open.position, length, cipher.data());
  } else if(open.stream) {
    open.stream->clear();
    open.stream->seekg(std::streamoff(u64(open.pgdDataOffset) + open.position));
    open.stream->read(reinterpret_cast<char*>(cipher.data()), length);
    got = u64(open.stream->gcount()) == length;
    open.stream->clear();
  }
  if(!got) return;  //a damaged image: leave the read as it is
  Pgd::decryptData(cipher.data(), u32(open.position), u32(length), open.pgdDataKey, open.pgdVersionKey);
  memory.copyIn(data, cipher.data(), u32(length));
}

//How many bytes (or sectors) a read of size from the file's position gets, fewer at its end: 0 with them in count, or
//why it can't (a destination not in the program's memory: for the disc, the bytes it gets; for a host file, all it
//asked for). Nothing moves. A PGD file unlocked with its key reads as its decrypted data (pgdDecryptRead(), which
//reads file offset p as data byte p): the descriptor is set up here, so the count is already clamped to the data,
//not the whole file, for sync reads (readFile) and async ones (sceIoReadAsync) alike. Still locked, or refused its
//descriptor, it reads as a plain file.
auto Kernel::readCount(OpenFile& open, u32 data, u32 size, u64& count) -> u32 {
  if(open.pgd && !open.pgdReady) pgdDescriptor(open);
  if(open.onDisc) {
    u64 unit = open.sectors ? Disc::SectorSize : 1;
    u64 start = u64(open.sector) * Disc::SectorSize + open.position * unit;
    //no further than the disc goes, whatever the run said: in sectors, its last whole one; in bytes, its last byte
    u64 end = open.sectors ? u64(disc->sectors()) * Disc::SectorSize : disc->size();
    count = std::min<u64>(size, open.size > open.position ? open.size - open.position : 0);
    count = std::min<u64>(count, start < end ? (end - start) / unit : 0);
    if(open.pgd && open.pgdReady)
      count = std::min<u64>(count, u64(open.position) < open.pgdDataSize ? u64(open.pgdDataSize) - open.position : 0);
    u64 bytes = count * unit;
    if(bytes && (bytes > 0xffff'ffff || !memory.reaches(data, u32(bytes)))) return ErrorIllegalAddress;
    return 0;
  }
  if(size && !memory.reaches(data, size)) return ErrorIllegalAddress;
  open.stream->flush();
  open.stream->clear();
  open.stream->seekg(0, std::ios::end);
  s64 length = s64(open.stream->tellg());
  open.stream->clear();
  if(length >= 0) length = s64(fileLength(open.host, u64(length)));
  count = length > s64(open.position) ? std::min<u64>(size, u64(length) - open.position) : 0;
  if(open.pgd && open.pgdReady)
    count = std::min<u64>(count, u64(open.position) < open.pgdDataSize ? u64(open.pgdDataSize) - open.position : 0);
  return 0;
}

//Moves up to count bytes (or sectors) of the file, from position at, into the program's memory at data, which
//readCount() found can take them: how many it moved (a host file may have been cut shorter since), or -1 if the disc
//image can't be read there (a damaged image).
auto Kernel::readMove(OpenFile& open, u64 at, u64 count, u32 data) -> s64 {
  if(!count) return 0;
  if(open.onDisc) {
    u64 unit = open.sectors ? Disc::SectorSize : 1;
    std::vector<u8> buffer(count * unit);
    if(!disc->read(u64(open.sector) * Disc::SectorSize + at * unit, buffer.size(), buffer.data())) return -1;
    memory.copyIn(data, buffer.data(), u32(buffer.size()));
    return s64(count);
  }
  std::vector<char> buffer(count);
  open.stream->clear();
  open.stream->seekg(std::streamoff(at));
  open.stream->read(buffer.data(), buffer.size());
  u32 got = u32(open.stream->gcount());
  open.stream->clear();
  memory.copyIn(data, buffer.data(), got);
  return got;
}

//A synchronous read or write waits for its file's device, as on a PSP: pspautotests' intr/waits recorded sceIoRead
//and sceIoWrite on a memory stick file refused in an interrupt handler (ILLEGAL_CONTEXT) and with interrupts or
//dispatching held off (CAN_NOT_WAIT), as functions that wait are, a bad file being refused first. 0 if the calling
//thread may wait, else the error the call is refused with. (Outside a handler with no thread running, as when a test
//calls the kernel itself, there's no one to wait: the call is done at once.)
auto Kernel::fileWaitRefused() const -> u32 {
  if(interrupting) return ErrorIllegalContext;
  if(current && (!interruptsEnabled || dispatchSuspended)) return ErrorCanNotWait;
  return 0;
}

//A synchronous read or write is done as it's made (its bytes move now), and the calling thread then waits the time
//the same request would take its device made asynchronously (asyncDuration(), async.cpp), other threads running
//meanwhile, before it returns value. GTA's disc streaming counts on that: its streaming thread calls a request's
//callback as its last read ends, and the callback drops the request unless the thread that made it, of a lower
//priority, has run meanwhile to note it. An error returns at once.
auto Kernel::fileWait(u32 file, u32 value, bool onDisc, u64 bytes) -> void {
  result(value);
  if(!current || s32(value) < 0) return;
  current->waitCount = value;
  block(Wait::File, file, cycles + asyncDuration(onDisc, bytes));
}

//(file, data, size): how many bytes were read (fewer at the end of the file), once its device has taken their time.
//Standard input has nothing to read, at once.
auto Kernel::sceIoRead() -> void {
  u32 file = arg(0);
  if(asyncBusy(file)) return result(ErrorAsyncBusy);
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || !(found->second.flags & OpenRead)) {
    return result(readFile(file, arg(1), arg(2)));
  }
  if(u32 error = fileWaitRefused()) return result(error);
  u32 got = readFile(file, arg(1), arg(2));
  auto& open = found->second;
  fileWait(file, got, open.onDisc, s32(got) < 0 ? 0 : u64(got) * (open.sectors ? Disc::SectorSize : 1));
}

//Writes from the program's memory to an open file (file, data, size): how many bytes were written, or an error.
auto Kernel::writeFile(u32 file, u32 data, u32 size) -> u32 {
  if(file == StandardOutput || file == StandardError) {
    size = std::min<u32>(size, 64_KiB);
    std::string text(size, '\0');
    if(!memory.copyOut(text.data(), data, size)) return ErrorIllegalAddress;
    if(output) output(text);
    return size;
  }
  auto found = files.find(file);
  if(found == files.end()) return ErrorBadFile;
  auto& open = found->second;
  //only a host file opened for writing: nothing on the disc can be written
  if(open.folder || open.onDisc || !(open.flags & OpenWrite)) return ErrorBadFile;
  if(size && !memory.reaches(data, size)) return ErrorIllegalAddress;
  std::vector<char> buffer(size);
  memory.copyOut(buffer.data(), data, size);
  open.stream->clear();
  u64 at = open.position;
  if(open.flags & OpenAppend) {  //at the PSP's end of the file
    open.stream->seekp(0, std::ios::end);
    if(s64 end = s64(open.stream->tellp()); end >= 0) at = fileLength(open.host, end);
  }
  //(from or before an emptied file's end: it's being rewritten in order, and is cut first: openFile())
  for(auto& [other, emptied] : files) {
    if(size && emptied.truncated && at <= emptied.length && sameHostFile(emptied.host, open.host)) cut(emptied);
  }
  open.stream->seekp(std::streamoff(at));
  open.stream->write(buffer.data(), size);
  open.stream->flush();
  if(!*open.stream) return ErrorIOError;
  open.position = u64(open.stream->tellp());
  fileWritten(open.host, open.position);
  return size;
}

//(file, data, size): how many bytes were written, once the device has taken their time. Standard output and error
//take none.
auto Kernel::sceIoWrite() -> void {
  u32 file = arg(0);
  if(asyncBusy(file)) return result(ErrorAsyncBusy);
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || found->second.onDisc || !(found->second.flags & OpenWrite)) {
    return result(writeFile(file, arg(1), arg(2)));
  }
  if(u32 error = fileWaitRefused()) return result(error);
  u32 wrote = writeFile(file, arg(1), arg(2));
  fileWait(file, wrote, false, s32(wrote) < 0 ? 0 : wrote);
}

//Moves a file's position: from its start (whence 0), where it is (1), or its end (2). Returns where it is now. A
//memory stick file ends where the PSP sees it end (fileLength(): short of the host file's end while a descriptor that
//emptied it as it was opened keeps its bytes). A PGD file unlocked with its key is its decrypted data
//(pgdDecryptRead()), so its end is the data's, not the file's, headers and block MACs and all, and no seek goes past
//it, as the PSP's PGD layer seeks it ("tpu"'s decompilation: from the descriptor's size, each result clamped to it).
//A descriptor that emptied its file is never a PGD: a PGD file is open only to read (the disc's files can't be opened
//to write, and sceNpDrmEdataSetupKey readies an EDATA only so), and only a descriptor opened to write empties its file
//as it's opened (openFile()). The two meet where another descriptor empties a readied EDATA: its end stays its data's
//while its reads find nothing past the PSP's length (readCount()), as the PGD layer, which answers from its
//descriptor, plausibly does too.
auto Kernel::seek(u32 file, s64 offset, u32 whence, u64& position) -> u32 {
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || found->second.resultOnly) return ErrorBadFile;
  auto& open = found->second;
  s64 size = s64(open.size);
  if(!open.onDisc) {
    open.stream->flush();
    std::error_code error;
    size = s64(fileLength(open.host, std::filesystem::file_size(open.host, error)));
  }
  if(open.pgd && !open.pgdReady) pgdDescriptor(open);
  if(open.pgd && open.pgdReady) size = open.pgdDataSize;
  s64 base = whence == 0 ? 0 : whence == 1 ? s64(open.position) : whence == 2 ? size : -1;
  if(base < 0 || base + offset < 0) return ErrorInvalidArgument;
  open.position = u64(base + offset);
  if(open.pgd && open.pgdReady) open.position = std::min<u64>(open.position, open.pgdDataSize);
  position = open.position;
  return 0;
}

//(file, 64-bit offset in a2 and a3, whence in t0): the new position, in v0 and v1. The PSP's compiler (EABI) puts a
//64-bit argument in an even-odd pair of registers, so the offset skips a1 (psp-gcc's own calls do exactly this).
auto Kernel::sceIoLseek() -> void {
  u64 position = 0;
  u32 error = asyncBusy(arg(0)) ? ErrorAsyncBusy : seek(arg(0), s64(u64(arg(3)) << 32 | arg(2)), arg(4), position);
  cpu.ipu.r[3] = error ? 0xffff'ffff : u32(position >> 32);
  result(error ? error : u32(position));
}

//(file, 32-bit offset, whence): the new position.
auto Kernel::sceIoLseek32() -> void {
  u64 position = 0;
  u32 error = asyncBusy(arg(0)) ? ErrorAsyncBusy : seek(arg(0), s32(arg(1)), arg(2), position);
  result(error ? error : u32(position));
}

auto Kernel::sceIoRemove() -> void {
  std::string path = memory.readString(arg(0), 1024), host, normalized;
  if(onDisc(path)) return result(ErrorReadOnly);
  if(u32 error = resolve(path, host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(std::filesystem::is_directory(host, error)) return result(ErrorIsDirectory);
  bool removed = std::filesystem::remove(host, error);
  if(removed) dropCuts(host);  //(a descriptor still open on it holds what's gone)
  result(removed ? 0 : ErrorNoPermission);
}

auto Kernel::sceIoMkdir() -> void {
  std::string path = memory.readString(arg(0), 1024), host, normalized;
  if(onDisc(path)) return result(ErrorReadOnly);
  if(u32 error = resolve(path, host, normalized)) return result(error);
  std::error_code error;
  if(std::filesystem::exists(host, error)) return result(ErrorFileExists);
  result(std::filesystem::create_directory(host, error) ? 0 : ErrorFileNotFound);  //its parent isn't there
}

auto Kernel::sceIoRmdir() -> void {
  std::string path = memory.readString(arg(0), 1024), host, normalized;
  if(onDisc(path)) return result(ErrorReadOnly);
  if(u32 error = resolve(path, host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(!std::filesystem::is_directory(host, error)) return result(ErrorNotDirectory);
  if(!std::filesystem::is_empty(host, error)) return result(ErrorDirectoryNotEmpty);
  result(std::filesystem::remove(host, error) ? 0 : ErrorNoPermission);
}

//(old path, new path): the file takes the new path's last name and stays in its own folder, whatever folder the new
//path names, as pspautotests' io/file/rename recorded: "../a.txt" from ms0:/PSP renamed to "b.txt", or to
//"ms0:/PSP/b.txt", is ms0:/b.txt. So a name taken already there is refused (FILE_EXISTS), the old name itself among
//them, and so is the new path's folder not being there no matter. Wildcards ('*', '?') in either path are refused
//(INVALID_ARGUMENT), a new path on another device too (XDEV), then an old file that isn't there (FILE_NOT_FOUND).
//Peace Walker installs its data so, writing a temporary file and renaming it to "TDLSFILE.SYS" (which had been
//looked for in the working folder, on the disc, and refused as read-only).
auto Kernel::sceIoRename() -> void {
  std::string from = memory.readString(arg(0), 1024), to = memory.readString(arg(1), 1024);
  if((from + to).find_first_of("*?") != std::string::npos) return result(ErrorInvalidArgument);
  auto deviceOf = [&](const std::string& path) {
    std::string device, rest;
    return split(path, device, rest) ? deviceName(device) : std::string{};
  };
  if(to.find(':') != std::string::npos && deviceOf(to) != deviceOf(from)) return result(ErrorCrossDevice);
  if(onDisc(from)) return result(ErrorReadOnly);
  std::string oldHost, oldPath;
  if(u32 error = resolve(from, oldHost, oldPath)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(oldHost, error)) return result(ErrorFileNotFound);
  std::string name = to.substr(to.find_last_of("/\\:") == std::string::npos ? 0 : to.find_last_of("/\\:") + 1);
  if(name.empty() || name == "." || name == "..") return result(ErrorInvalidArgument);
  std::string newHost, newPath;
  if(u32 error = resolve(oldPath.substr(0, oldPath.find_last_of('/') + 1) + name, newHost, newPath)) {
    return result(error);
  }
  if(std::filesystem::exists(newHost, error)) return result(ErrorFileExists);
  std::filesystem::rename(oldHost, newHost, error);
  if(!error) filesMoved(oldHost, oldPath, newHost, newPath);
  result(error ? ErrorNoPermission : 0);
}

//The folder relative paths start from.
auto Kernel::sceIoChdir() -> void {
  std::string path = memory.readString(arg(0), 1024), host, normalized;
  if(onDisc(path)) {
    if(throughUmd(path)) return result(ErrorNotDirectory);  //umd0: is the whole disc, no folder
    Disc::Entry entry;
    std::vector<std::string> names;
    if(u32 error = resolveOnDisc(path, entry, normalized, names)) return result(error);
    if(!entry.folder) return result(ErrorNotDirectory);
    workingDirectory = normalized;
    return result(0);
  }
  if(u32 error = resolve(path, host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(!std::filesystem::is_directory(host, error)) return result(ErrorNotDirectory);
  workingDirectory = normalized;
  result(0);
}

//(name, block device, file system, mode, ...): a device named for a block device's file system (pspiofilemgr.h's
//example assigns flash0: to lflash0:0,0 through flashfat0:). The devices here have their names for good
//(deviceName()), so it changes nothing: Metal Gear Acid and Ridge Racer assign disc0: to umd0: through isofs0:,
//which it already is, and Colin McRae Rally 2005 Plus memstick: to msstor0p1:/ through fatms0:, the memory stick.
auto Kernel::sceIoAssign() -> void {
  result(0);
}

//(path, where to put its SceIoStat)
auto Kernel::sceIoGetstat() -> void {
  std::string path = memory.readString(arg(0), 1024), host, normalized;
  //the PSP refuses the status of a device's top ("ms0:/"), as PPSSPP's notes record
  auto colon = path.find(':');
  if(colon != std::string::npos && path.find_first_not_of("/\\", colon + 1) == path.npos) {
    return result(ErrorInvalidArgument);
  }
  if(onDisc(path)) {
    Disc::Entry entry;
    std::vector<std::string> names;
    u32 first = 0;
    u64 bytes = 0;
    if(throughUmd(path)) {  //the whole disc, its size in sectors as umd0:'s sizes are
      memcpy(entry.date, disc->root().date, 7);
      entry.size = disc->sectors();
    } else if(runOnDisc(path, first, bytes)) {  //a run of sectors: a file that size, starting there
      memcpy(entry.date, disc->root().date, 7);
      entry.sector = first;
      entry.size = u32(std::min<u64>(bytes, 0xffff'ffff));
    } else if(u32 error = resolveOnDisc(path, entry, normalized, names)) {
      return result(error);
    }
    if(!memory.reaches(arg(1), 88)) return result(ErrorIllegalAddress);
    writeStat(arg(1), entry);
    return result(0);
  }
  if(u32 error = resolve(path, host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(!memory.reaches(arg(1), 88)) return result(ErrorIllegalAddress);
  writeStat(arg(1), host);
  result(0);
}

//(path, status, bits): changes what the bits name of a file's or folder's status (pspiofilemgr_stat.h's FIO_CST_*: 0x01
//the mode, 0x02 the attributes, 0x04 the size, 0x08, 0x10 and 0x20 the times made, read and changed, 0x40 the private
//words). On a PSP's memory stick the mode's write bits and the attributes' 0x01 are one read-only flag, as
//pspautotests' io/stat/readonly recorded: a file made 0444 reads mode 0x216d and attributes 0x21, made 0777 again
//0x21ff and 0x20, and given the attributes 0x21 alone the same as 0444, each change answered 0. Here the flag is the
//host file's write permission, its owner's (writeStat() reads it back): made read-only, no one may write it; made
//writable, its owner may again. Where both are named the attributes say which (chosen: the recording names both only
//once, agreeing). A host that can't keep it (a phone's shared storage may not) answers 0 all the same, as the PSP
//would. The recording has a file's flag alone: a folder's is answered 0 and changed nowhere (chosen: a host folder
//that can't be written takes no files, while Windows' and Linux's FAT drivers ignore a folder's flag; a game setting
//it on PSP/SAVEDATA would have made saving impossible). The device's top is refused as sceIoGetstat refuses it
//(io/stat: "ms0:" 0x80010016). The rest isn't changed (no recording has any of it), and is answered 0 too. Kingdom
//Hearts Birth by Sleep Final Mix's install takes the flag off its folders where sceIoGetstat says they have it, and
//off its data file before each piece it writes, and gives the install up when this fails.
auto Kernel::sceIoChstat() -> void {
  namespace fs = std::filesystem;
  std::string path = memory.readString(arg(0), 1024), host, normalized;
  u32 status = arg(1), bits = arg(2);
  auto colon = path.find(':');
  if(colon != std::string::npos && path.find_first_not_of("/\\", colon + 1) == path.npos) {
    return result(ErrorInvalidArgument);
  }
  if(onDisc(path)) return result(ErrorReadOnly);
  if(u32 error = resolve(path, host, normalized)) return result(error);
  std::error_code error;
  if(!fs::exists(host, error)) return result(ErrorFileNotFound);
  if(!memory.reaches(status, 64)) return result(ErrorIllegalAddress);
  if(bits & 3 && fs::is_regular_file(host, error)) {
    bool writable = bits & 2 ? !(memory.read(4, status + 4) & 1) : (memory.read(4, status) & 0222) != 0;
    if(writable) fs::permissions(host, fs::perms::owner_write, fs::perm_options::add, error);
    else fs::permissions(host, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                         fs::perm_options::remove, error);
  }
  result(0);
}

//(path): a folder's descriptor, for reading its entries one at a time: "." and ".." first (except at a device's top,
//as on FAT), then the names in alphabetical order, whatever their case.
auto Kernel::sceIoDopen() -> void {
  std::string path = memory.readString(arg(0), 1024), host, normalized;
  if(onDisc(path)) {  //the disc's order (ISO 9660 sorts its names), with no "." or ".." (PPSSPP's notes)
    OpenFile open;
    open.folder = true;
    open.onDisc = true;
    if(throughUmd(path)) {  //the whole disc, which holds no entries
      open.path = path;
    } else {
      Disc::Entry entry;
      std::vector<std::string> names;
      if(u32 error = resolveOnDisc(path, entry, normalized, names)) return result(error);
      if(!entry.folder) return result(ErrorNotDirectory);
      open.path = normalized;
      for(auto& child : disc->list(entry)) {
        open.entries.push_back(child.name);
        open.discEntries.push_back(child);
      }
    }
    u32 file = newFile();
    if(!file) return result(ErrorTooManyFiles);
    open.opened = ++filesOpened;
    files[file] = std::move(open);
    return result(file);
  }
  if(u32 error = resolve(path, host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(!std::filesystem::is_directory(host, error)) return result(ErrorNotDirectory);
  OpenFile open;
  open.path = normalized;
  open.host = host;
  open.folder = true;
  if(normalized.back() != '/') open.entries = {".", ".."};
  //names no PSP path can name are left out: a '\' parts a path as '/' does, and a ':' ends a device's name; and at the
  //memory stick's top, the game data install's copies' folder, which resolve() keeps from programs
  std::vector<std::string> names;
  bool stickTop = normalized == "ms0:/";
  for(auto& entry : std::filesystem::directory_iterator(host, error)) {
    auto name = entry.path().filename().string();
    if(stickTop && copiesName(name)) continue;
    if(name.find_first_of("\\:") == std::string::npos) names.push_back(name);
  }
  std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
    for(size_t n = 0; n < a.size() && n < b.size(); n++) {
      auto x = std::tolower(u8(a[n])), y = std::tolower(u8(b[n]));
      if(x != y) return x < y;
    }
    return a.size() < b.size();
  });
  open.entries.insert(open.entries.end(), names.begin(), names.end());
  u32 file = newFile();
  if(!file) return result(ErrorTooManyFiles);
  open.opened = ++filesOpened;
  files[file] = std::move(open);
  result(file);
}

//(folder, where to put the entry, a SceIoDirent of 352 bytes): 1 and the next entry (its SceIoStat and name), or 0
//once there are no more. If the program points d_private somewhere, a memory stick entry's short (8.3) and long names
//go there too, laid out by the SDK version the program gave, as pspautotests' io/shortname recorded: with none, or
//0x03070110, the short name in 13 bytes from the start and the long name right after it; with 0x06060010 a
//SceIoFatDirentPrivate, the size the program declared in its first word left there, the short name in 16 bytes after
//it and the long name in 1024 after that. The change is taken at 0x03080000 (the test's "documented as changing at
//SDK 3.08"). Nothing goes past the long name's end in the older layout, nor into a SceIoFatDirentPrivate declared
//smaller than one (chosen): Need for Speed: ProStreet (0x03070010) gives 272 bytes on its stack, and the newer
//layout's 1044 bytes, written there, zeroed its saved registers, and it returned to address 0.
auto Kernel::sceIoDread() -> void {
  auto found = files.find(arg(0));
  if(found == files.end() || !found->second.folder) return result(ErrorBadFile);
  auto& open = found->second;
  u32 entry = arg(1);
  if(!memory.reaches(entry, 352)) return result(ErrorIllegalAddress);
  if(open.nextEntry >= open.entries.size()) return result(0);
  u32 index = open.nextEntry++;
  std::string name = open.entries[index];
  if(open.onDisc) {
    writeStat(entry, open.discEntries[index]);
  } else {
    std::filesystem::path host = open.host;
    if(name == "..") host = host.parent_path();
    else if(name != ".") host /= name;
    writeStat(entry, host.string());
  }
  memory.fill(entry + 88, 0, 256);
  memory.copyIn(entry + 88, name.c_str(), std::min<size_t>(name.size(), 255));
  //on the memory stick only: the disc's names have no short forms (PPSSPP's notes)
  if(u32 extra = memory.read(4, entry + 344); !open.onDisc && extra) {
    std::string base = name, extension;  //an 8.3 short name: up to eight letters, a dot, up to three, in capitals
    if(auto dot = name.rfind('.'); dot != std::string::npos && dot > 0) base = name.substr(0, dot), extension = name.substr(dot + 1);
    std::string shortName = base.substr(0, 8) + (extension.empty() ? "" : "." + extension.substr(0, 3));
    for(auto& c : shortName) c = std::toupper(u8(c));
    if(name == "." || name == "..") shortName = name;  //as io/shortname recorded them
    shortName.resize(std::min<size_t>(shortName.size(), 12));
    std::string longName = name.substr(0, 255);
    if(sdkVersion < 0x0308'0000) {
      if(memory.reaches(extra, 13 + longName.size() + 1)) {
        memory.fill(extra, 0, 13);
        memory.copyIn(extra, shortName.c_str(), shortName.size());
        memory.copyIn(extra + 13, longName.c_str(), longName.size() + 1);
      }
    } else if(memory.reaches(extra, 1044) && memory.read(4, extra) >= 1044) {
      memory.fill(extra + 4, 0, 1040);
      memory.copyIn(extra + 4, shortName.c_str(), shortName.size());
      memory.copyIn(extra + 20, longName.c_str(), longName.size());
    }
  }
  result(1);
}

auto Kernel::sceIoDclose() -> void {
  auto found = files.find(arg(0));
  if(found == files.end() || !found->second.folder) return result(ErrorBadFile);
  files.erase(found);
  result(0);
}

//(where to put descriptors, how many fit there, where to put how many there are): the descriptors the program has,
//as uOFW describes the PSP's kernel (iofilemgr_kernel.h's int sceIoGetFdList(SceUID *fds, int numFd, int *count);
//pspsdk has no prototype, and no pspautotests program records it): every file and folder a program has opened, in
//the order they were opened, standard input, output and error first (its kernel opens them as it starts), as many as
//fit put there; how many were put there is the result, and how many there are goes where the third argument says,
//unless that's NULL. A descriptor kept only for an asynchronous request's result is still the program's (the
//kernel's record of a file stays until the result is taken), and so is the movie player's (the player library opens
//its file as the program's code). The standard streams are listed as 0, 1 and 2, as sceKernelStdin and its siblings
//give them here (chosen: uOFW's PSP lists them by its kernel's own IDs for them). The places are checked as the
//kernel checks any it's given (ILLEGAL_ADDR for a list, its size of 4 bytes a descriptor, or a count reaching the
//kernel's half of the addresses, as most negative sizes do), and a place nothing of the program's memory backs is
//refused too (chosen: the PSP would fault writing there). A size counted as negative whose 4 bytes a descriptor wrap
//round to less passes, and fits none: the kernel counts how many fit as a signed number. Crush counts its open files
//with it, to find files left open.
auto Kernel::sceIoGetFdList() -> void {
  u32 list = arg(0), room = arg(1), count = arg(2), size = room * 4;
  if(s32(list | size | (list + size)) < 0 || s32(count | (count + 4)) < 0) return result(ErrorIllegalAddress);
  std::vector<std::pair<u64, u32>> opened;
  for(auto& [file, open] : files) opened.push_back({open.opened, file});
  std::sort(opened.begin(), opened.end());
  std::vector<u32> numbers = {StandardInput, StandardOutput, StandardError};
  for(auto& [order, file] : opened) numbers.push_back(file);
  u32 put = s32(room) > 0 ? std::min<u32>(room, numbers.size()) : 0;
  if((put && !memory.reaches(list, put * 4)) || (count && !memory.reaches(count, 4))) {
    return result(ErrorIllegalAddress);
  }
  for(u32 n = 0; n < put; n++) memory.write(4, list + n * 4, numbers[n]);
  if(count) memory.write(4, count, numbers.size());
  result(put);
}

//A request a file's device answers rather than reading or writing (file, command, in, in length, out, out length):
//its result, and how many bytes it put in out (moved: an asynchronous request takes the time of moving them). A file
//on the disc answers those PPSSPP's notes on the hardware describe (games use them to find where a file starts on
//the disc and read it by sector); anything else, and any other device's file, isn't supported. sceIoIoctl, and
//sceIoIoctlAsync (async.cpp).
auto Kernel::ioctl(u32 file, u32 command, u32 in, u32 inLength, u32 out, u32 outLength, u64* moved) -> u32 {
  u64 unused;
  if(!moved) moved = &unused;
  *moved = 0;
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || found->second.resultOnly) return ErrorBadFile;
  auto& open = found->second;
  if(!open.onDisc) return ErrorFunctionNotSupported;
  auto writeOut = [&](u32 size, auto&& write) -> u32 {
    if(outLength < size || !memory.reaches(out, size)) return ErrorInvalidArgument;
    write();
    *moved = size;
    return 0;
  };
  //a seek's request: a 64-bit offset, a word nobody knows, and where from (as sceIoLseek's whence). Not before the
  //start, nor past the end (unlike sceIoLseek): that fails with `outside`.
  auto seekBy = [&](u32 outside) -> u32 {
    if(inLength < 4 || !memory.reaches(in, 16)) return ErrorInvalidArgument;
    s64 offset = s64(u64(memory.read(4, in + 4)) << 32 | memory.read(4, in));
    u32 whence = memory.read(4, in + 12);
    s64 base = whence == 0 ? 0 : whence == 1 ? s64(open.position) : whence == 2 ? s64(open.size) : -1;
    if(base < 0 || base + offset < 0 || base + offset > s64(open.size)) return outside;
    open.position = u64(base + offset);
    return 0;
  };
  switch(command) {
  case 0x0102'0001:  //the disc's volume descriptor (sector 16)
    if(open.sectors) return ErrorFunctionNotSupported;
    return writeOut(Disc::SectorSize, [&] {
      u8 sector[Disc::SectorSize];
      if(disc->readSectors(16, 1, sector)) memory.copyIn(out, sector, Disc::SectorSize);
    });
  case 0x0102'0002: {  //the disc's path table (its folders, listed apart), as the volume descriptor places it
    if(open.sectors) return ErrorFunctionNotSupported;
    u8 descriptor[Disc::SectorSize];
    if(!disc->readSectors(16, 1, descriptor)) return ErrorIOError;
    u32 size = little32(descriptor + 132), first = little32(descriptor + 140);
    if(outLength < size || !memory.reaches(out, size)) return ErrorInvalidArgument;
    std::vector<u8> table(size);
    if(!disc->read(u64(first) * Disc::SectorSize, size, table.data())) return ErrorIOError;
    memory.copyIn(out, table.data(), size);
    *moved = size;
    return 0;
  }
  case 0x0102'0003:  //the disc's sector size
    return writeOut(4, [&] { memory.write(4, out, Disc::SectorSize); });
  case 0x0102'0004:  //where the file's position is
    return writeOut(4, [&] { memory.write(4, out, u32(open.position)); });
  case 0x0101'0005:  //seek in the file
    return seekBy(ErrorIOError);
  case 0x0102'0006:  //the file's first sector
    return writeOut(4, [&] { memory.write(4, out, open.sector); });
  case 0x0102'0007:  //the file's size (umd0:'s in sectors, as all its sizes are)
    if(!memory.reaches(out, 8) || out & 3) return ErrorInvalidArgument;
    memory.write(4, out, u32(open.size));
    memory.write(4, out + 4, u32(open.size >> 32));
    *moved = 8;
    return 0;
  case 0x0103'0008:  //read from the file (in: how many bytes)
  case 0x01f3'0003: {  //read whole sectors from umd0: (in: how many, at least one)
    if(inLength < 4 || !memory.reaches(in, 4)) return ErrorInvalidArgument;
    u32 size = memory.read(4, in);
    if(size > outLength || (command == 0x01f3'0003 && !size)) return ErrorInvalidArgument;
    u32 got = readFile(file, out, size);
    if(s32(got) > 0) *moved = u64(got) * (open.sectors ? Disc::SectorSize : 1);
    return got;
  }
  case 0x01d2'0001:  //where umd0:'s position is, in sectors
    return writeOut(4, [&] { memory.write(4, out, u32(open.position)); });
  case 0x01f1'00a6:  //seek in umd0:, in sectors
    return seekBy(ErrorInvalidFileSize);
  case 0x0410'0001: {  //the file's key (16 bytes), for data encrypted as PGD, which the PSP decrypts as it's read
    //A file without PGD's header ("\0PGD") is read as it is: the fan translations of 7th Dragon 2020 and its sequel
    //carry their INSDIR data decrypted, and the games set its key all the same, taking anything but success as the
    //drive failing and opening the file again, for good. A file with the header keeps the key, and reads as its
    //decrypted data with the header hidden (pgdDecryptRead()); its key (0x10) is in the clear. The header is looked
    //for where the PGD starts: the file's start, or where 0x04100002 put it. A file already unlocked stays as it is:
    //its descriptor came out under the key its own header's MAC holds, whatever key is given now.
    if(open.sectors || inLength < 16 || !memory.reaches(in, 16)) return ErrorFunctionNotSupported;
    if(open.pgdReady) return 0;
    u32 at = open.pgd ? open.pgdDataOffset : 0;
    u8 head[4] = {};
    if(open.size >= u64(at) + 4 && !disc->read(u64(open.sector) * Disc::SectorSize + at, 4, head)) return ErrorIOError;
    if(!memcmp(head, "\0PGD", 4)) {
      memcpy(open.pgdVersionKey, memory.pointer(in, 16), 16);
      open.pgd = true;
      open.pgdReady = false;
      open.pgdDataOffset = at;
    }
    return 0;
  }
  //where the PGD starts, relative to the file: the PSP's PGD layer reads its header there when the key is given
  //(0x04100001), and its data where its descriptor says, counted from there ("tpu"'s decompilation; PPSSPP's notes
  //have npdrm give an EDATA's 0x90 so).
  case 0x0410'0002: {
    if(open.sectors || inLength < 4 || !memory.reaches(in, 4)) return ErrorFunctionNotSupported;
    open.pgd = true;
    open.pgdReady = false;
    open.pgdDataOffset = memory.read(4, in);
    return 0;
  }
  }
  return ErrorFunctionNotSupported;
}

//(file, command, in, in length, out, out length). A read (0x01030008, or 0x01f30003's sectors) is a read as
//sceIoRead's is: refused where the thread can't wait before anything moves, a bad file refused first, and then
//waiting for the time its bytes take the drive (fileWait()). The other requests are answered at once.
auto Kernel::sceIoIoctl() -> void {
  u32 file = arg(0), command = arg(1);
  if(asyncBusy(file)) return result(ErrorAsyncBusy);
  auto found = files.find(file);
  bool read = command == 0x0103'0008 || command == 0x01f3'0003;
  if(!read || found == files.end() || found->second.folder || found->second.resultOnly || !found->second.onDisc
  || !(found->second.flags & OpenRead)) {
    return result(ioctl(file, command, arg(2), arg(3), arg(4), arg(5)));
  }
  if(u32 error = fileWaitRefused()) return result(error);
  u64 moved = 0;
  u32 got = ioctl(file, command, arg(2), arg(3), arg(4), arg(5), &moved);
  fileWait(file, got, true, moved);
}

//The memory stick, as every function that tells its size or free space reports it (the capacity request below, the
//savedata utility's sizes modes): a PSP with a large and mostly empty stick, a 2 GB one, 61,440 clusters of 32 KiB
//(64 sectors of 512 bytes; 1,920 MiB) with 57,344 (1,792 MiB) free. Large beside any save (tens or hundreds of KiB),
//and small enough that its bytes fit in 31 bits: games add sizes up in 32-bit words, in bytes (clusters times the
//cluster's size), where 2 GiB or more free wraps round to a negative number or, at a multiple of 4 GiB, to almost
//none, and a game says there's no room. (The PSP pspautotests' utility/savedata programs recorded on had a far
//larger stick, 16 GiB less 10 sectors free, which is why its figures aren't copied.) The free space stays as it is
//whatever is written: a game asks before it saves, and a few saves are nothing beside it.

//A size as the savedata utility writes it for people, in 8 bytes: whole units, cut down ("96 KB", "128 KB", and
//"15 GB" for 16,777,211 KiB, as utility/savedata recorded); MB between KB and GB (chosen).
auto Kernel::stickText(u32 at, u64 kilobytes) -> void {
  char text[24];
  if(kilobytes < 1024) std::snprintf(text, sizeof(text), "%u KB", u32(kilobytes));
  else if(kilobytes < 1024 * 1024) std::snprintf(text, sizeof(text), "%u MB", u32(kilobytes / 1024));
  else std::snprintf(text, sizeof(text), "%u GB", u32(kilobytes / (1024 * 1024)));
  u8 bytes[8] = {};
  std::memcpy(bytes, text, std::min<size_t>(std::strlen(text), 7));
  memory.copyIn(at, bytes, 8);
}

//(device, command, in, in length, out, out length): a request to a whole device. The disc drive's and the memory
//stick's, as PPSSPP's notes on the hardware describe them: the drive holds a game disc, ready, and finished
//anything it was asked to read ahead; the memory stick is in, writable, formatted (FAT), its size and free space
//the stick's above. Anything else isn't supported.
auto Kernel::sceIoDevctl() -> void {
  std::string device = memory.readString(arg(0), 64);
  u32 command = arg(1), in = arg(2), inLength = arg(3), out = arg(4), outLength = arg(5);
  for(auto& c : device) c = std::tolower(u8(c));
  if(!device.empty() && device.back() == ':') device.pop_back();
  auto inWord = [&](u32& value) {
    if(inLength < 4 || !memory.reaches(in, 4)) return false;
    value = memory.read(4, in);
    return true;
  };
  auto outWord = [&](u32 value, u32 at = 0) {
    if(outLength < at + 4 || !memory.reaches(out + at, 4) || out & 3) return result(ErrorDevctlBadParameters);
    memory.write(4, out + at, value);
    result(0);
  };
  u32 word = 0;
  switch(command) {
  case 0x01e1'8030:  //does the disc's region match the PSP's? The answer is the result: 1, it does
    return result(inLength >= 16 ? 1 : ErrorInvalidArgument);
  case 0x01f2'0001:  //the disc's kind: a game
    return outWord(0x10, 4);
  case 0x01f2'0002:  //the sector the drive is at
    return outWord(0x10);
  case 0x01f2'0003:  //the disc's last sector
    return outWord(disc ? disc->sectors() - 1 : 0);
  case 0x01f1'00a3: case 0x01f1'00a4:  //seek, and read ahead into the drive's cache: done at once
  case 0x01f3'00a7: case 0x01f3'00a8: case 0x01f3'00a9:  //wait for, poll and cancel that: finished
  case 0x01f1'00a6: case 0x01f1'00a8: case 0x01f1'00a9:  //unknown, taking a word
    return result(inWord(word) ? 0 : ErrorDevctlBadParameters);
  case 0x01f3'00a5:  //read ahead, and say how it went: the first request is done
    if(!inWord(word)) return result(ErrorDevctlBadParameters);
    return outWord(1);
  }
  bool stick = device == "ms0" || device == "mscmhc0" || device == "memstick" || device == "fatms0";
  if(!stick) return result(ErrorFunctionNotSupported);
  switch(command) {
  case 0x0202'5801:  //the memory stick driver's state: a stick is in
    return outWord(4);
  case 0x0202'5806:  //is a stick in? 1, yes
    return outWord(1);
  case 0x0242'5823:  //is it formatted (FAT)? 1, yes
    return outWord(1);
  case 0x0242'5824:  //is it write-protected? 0, no
    return outWord(0);
  case 0x0241'5823: case 0x0240'd81e:  //turn FAT on; clear the driver's file table cache
    return result(0);
  case 0x0242'5818: {  //its size: in holds where to put a SceDevInf (pspiofilemgr_devctl.h): its clusters, those
    //free, free ones again (pspsdk's "cluster of empty logical block"), a sector's bytes, a cluster's sectors
    if(!inWord(word) || !memory.reaches(word, 20)) return result(ErrorDevctlBadParameters);
    memory.write(4, word + 0, StickClusters);
    memory.write(4, word + 4, StickFreeClusters);
    memory.write(4, word + 8, StickFreeClusters);
    memory.write(4, word + 12, StickSectorSize);
    memory.write(4, word + 16, StickClusterSize / StickSectorSize);
    return result(0);
  }
  case 0x0201'5804: case 0x0241'5821:  //register a callback for the stick going in or out (it never does)
    if(!inWord(word) || !callbacks.count(word)) return result(ErrorInvalidArgument);
    memoryStickCallbacks.push_back(word);
    //It's told at once that a stick is in (pspmscm.h's MS_CB_EVENT_INSERTED, 1), to run when its thread next runs
    //callbacks, as pspautotests' mstick recorded for fatms0's: the game's next sceKernelCheckCallback ran it with a
    //count of 1 and the event 1. Games that learn of the stick only so (50 Cent: Bulletproof) otherwise say none is
    //inserted. mscmhc0's register is taken to do the same. (The result first: a better thread the notice wakes
    //takes the CPU, and the caller's registers with it.)
    result(0);
    if(notifyCallback(word, 1)) reschedule();
    return;
  case 0x0201'5805: case 0x0241'5822: {  //unregister one
    if(!inWord(word)) return result(ErrorInvalidArgument);
    auto at = std::find(memoryStickCallbacks.begin(), memoryStickCallbacks.end(), word);
    if(at == memoryStickCallbacks.end()) return result(ErrorInvalidArgument);
    memoryStickCallbacks.erase(at);
    return result(0);
  }
  }
  result(ErrorFunctionNotSupported);
}

//The file I/O trace (the runner's --trace-io; ioTrace, empty unless a host sets it): each IoFileMgrForUser call a
//line, the PSP's time in microseconds and the calling thread first, then the call as the program made it and its
//answer. A descriptor comes with its file's path and the position the call found it at; a read or write with its
//buffer, its size and a hash of the bytes it moved ("zeros" if they all were); an open with its flags and mode; a seek
//with its offset and where from. An asynchronous request's line says when it's due, and a line of its own when it's
//done (traceIoDone()). A call that made its thread wait says so: what it returns comes as the thread wakes.
auto Kernel::traceIo(const Function& function) -> void {
  std::string name = function.name;
  u32 a[6];
  for(u32 n : range(6)) a[n] = arg(n);
  Thread* caller = current;
  u64 issued = cycles;
  auto hex = [](u64 value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", (unsigned long long)value);
    return std::string(text);
  };
  auto quoted = [&](u32 address) { return "\"" + memory.readString(address, 1024) + "\""; };
  auto described = [&](u32 file) {
    std::string text = "fd " + std::to_string(file);
    auto found = files.find(file);
    if(found == files.end()) return text + " (none)";
    text += " " + found->second.path;
    if(!found->second.folder && !found->second.resultOnly) text += " @" + hex(found->second.position);
    return text;
  };
  auto bytes = [&](u32 address, u64 size) { return traceBytes(address, size); };
  auto octal = [](u32 value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0%o", value);
    return std::string(text);
  };
  auto flagNames = [](u32 flags) {
    static const std::pair<u32, const char*> names[] = {{OpenRead, "read"}, {OpenWrite, "write"},
      {OpenAppend, "append"}, {OpenCreate, "create"}, {OpenTruncate, "truncate"}, {OpenExclusive, "exclusive"}};
    std::string text;
    for(auto& [bit, word] : names) if(flags & bit) text += std::string(text.empty() ? " (" : " ") + word;
    return text.empty() ? text : text + ")";
  };
  bool fileFirst = name != "sceIoOpen" && name != "sceIoOpenAsync" && name != "sceIoRemove" && name != "sceIoMkdir"
                && name != "sceIoRmdir" && name != "sceIoRename" && name != "sceIoChdir" && name != "sceIoAssign"
                && name != "sceIoGetstat" && name != "sceIoChstat" && name != "sceIoDopen" && name != "sceIoDevctl"
                && name != "sceIoGetFdList" && !(name == "sceIoChangeAsyncPriority" && a[0] == 0xffff'ffff);
  std::string call = fileFirst ? described(a[0]) : std::string{};
  bool read = name == "sceIoRead" || name == "sceIoReadAsync";
  bool write = name == "sceIoWrite" || name == "sceIoWriteAsync";
  if(name == "sceIoOpen" || name == "sceIoOpenAsync") {
    call = quoted(a[0]) + " flags " + hex(a[1]) + flagNames(a[1]) + " mode " + octal(a[2]);
  } else if(read || write) {
    call += std::string(read ? " into " : " from ") + hex(a[1]) + " size " + hex(a[2]);
    if(write) call += bytes(a[1], a[2]);
  } else if(name == "sceIoLseek" || name == "sceIoLseekAsync") {
    call += " offset " + std::to_string(s64(u64(a[3]) << 32 | a[2])) + " whence " + std::to_string(a[4]);
  } else if(name == "sceIoLseek32" || name == "sceIoLseek32Async") {
    call += " offset " + std::to_string(s32(a[1])) + " whence " + std::to_string(a[2]);
  } else if(name == "sceIoRename") {
    call = quoted(a[0]) + " to " + quoted(a[1]);
  } else if(name == "sceIoAssign") {
    call = quoted(a[0]) + " to " + quoted(a[1]) + " " + quoted(a[2]);
  } else if(name == "sceIoMkdir") {
    call = quoted(a[0]) + " mode " + octal(a[1]);
  } else if(name == "sceIoChstat") {
    call = quoted(a[0]) + " bits " + hex(a[2]);
    if(memory.reaches(a[1], 64)) {
      call += " mode " + octal(memory.read(4, a[1])) + " attr " + hex(memory.read(4, a[1] + 4));
    }
  } else if(!fileFirst && name != "sceIoDevctl" && name != "sceIoGetFdList" && name != "sceIoChangeAsyncPriority") {
    call = quoted(a[0]);  //remove, rmdir, chdir, getstat, dopen
  } else if(name == "sceIoIoctl" || name == "sceIoIoctlAsync" || name == "sceIoDevctl") {
    if(name == "sceIoDevctl") call = quoted(a[0]);
    call += " command " + hex(a[1]) + " in " + hex(a[2]) + " (" + std::to_string(a[3]) + ") out " + hex(a[4]) + " ("
          + std::to_string(a[5]) + ")";
  } else if(name == "sceIoChangeAsyncPriority") {
    call = (a[0] == 0xffff'ffff ? std::string("fd -1") : call) + " priority " + hex(a[1]);
  } else if(name == "sceIoSetAsyncCallback") {
    call += " callback " + hex(a[1]) + " argument " + hex(a[2]);
  } else if(name == "sceIoGetAsyncStat") {
    call += a[1] ? " poll" : " wait";
  } else if(name == "sceIoGetFdList") {
    call = "into " + hex(a[0]) + " room " + std::to_string(a[1]);
  }

  (this->*function.handler)();

  u32 v0 = caller && caller != current ? caller->context.gpr[2] : cpu.ipu.r[2];
  u32 v1 = caller && caller != current ? caller->context.gpr[3] : cpu.ipu.r[3];
  bool waits = caller && caller->status == Status::Waiting;
  std::string answer = s32(v0) < 0 ? hex(v0) : std::to_string(v0);
  if(name == "sceIoLseek" && s32(v0) >= 0) answer = std::to_string(u64(v1) << 32 | v0);
  if(name == "sceIoRead" && s32(v0) > 0) answer += bytes(a[1], v0);
  if(name == "sceIoGetstat" && !v0) answer += " (size " + std::to_string(u64(memory.read(4, a[1] + 12)) << 32
                                                 | memory.read(4, a[1] + 8)) + ")";
  if(name == "sceIoDread" && v0 == 1) answer += " (" + memory.readString(a[1] + 88, 256) + ")";
  u32 pointer = name == "sceIoGetAsyncStat" ? a[2] : a[1];
  bool taken = name == "sceIoPollAsync" || name == "sceIoWaitAsync" || name == "sceIoWaitAsyncCB"
            || name == "sceIoGetAsyncStat";
  if(taken && !v0 && !waits && pointer && memory.reaches(pointer, 8)) {
    answer += " (result " + hex(u64(memory.read(4, pointer + 4)) << 32 | memory.read(4, pointer)) + ")";
  }
  u32 file = name == "sceIoOpenAsync" ? v0 : a[0];
  if(auto found = files.find(file); name.find("Async") != std::string::npos && !taken && s32(v0) >= 0
  && found != files.end() && found->second.async == OpenFile::Async::Pending && found->second.asyncDoneAt >= issued) {
    answer += " (due in " + std::to_string((found->second.asyncDoneAt - issued) / (CPUFrequency / 1'000'000))
            + " us, result " + hex(found->second.asyncResult) + ")";
  }
  if(waits) answer += " (waits)";
  char head[64];
  std::snprintf(head, sizeof(head), "%llu us %s: ", (unsigned long long)(issued / (CPUFrequency / 1'000'000)),
                caller ? caller->name.c_str() : "-");
  ioTrace(head + name + " " + call + " -> " + answer);
}

//The trace's line for an asynchronous request done (asyncDone()): the file, its position now, the result, and for a
//read the hash of the bytes that landed.
auto Kernel::traceIoDone(u32 file, u32 data, u64 bytes) -> void {
  auto& open = files[file];
  char text[96];
  std::snprintf(text, sizeof(text), "%llu us: done fd %u ", (unsigned long long)(cycles / (CPUFrequency / 1'000'000)),
                file);
  std::string line = text + open.path;
  std::snprintf(text, sizeof(text), " @0x%llx -> 0x%llx", (unsigned long long)open.position,
                (unsigned long long)open.asyncResult);
  ioTrace(line + text + (data ? traceBytes(data, bytes) : std::string{}));
}

//The trace's word on the bytes a read or write moved: " zeros" if they all were, else " hash" and their FNV-1a hash
//(nothing for none, or bytes not in the program's memory).
auto Kernel::traceBytes(u32 address, u64 size) -> std::string {
  if(!size || size > 0x7fff'ffff || !memory.reaches(address, u32(size))) return {};
  std::vector<u8> data(size);
  memory.copyOut(data.data(), address, u32(size));
  u32 hash = 0x811c'9dc5;
  bool zeros = true;
  for(u8 byte : data) hash = (hash ^ byte) * 0x0100'0193, zeros = zeros && !byte;
  if(zeros) return " zeros";
  char text[24];
  std::snprintf(text, sizeof(text), " hash %08x", hash);
  return text;
}

//scePspNpDrm_user, the DRM of games sold as downloads: a game gives its licensee key (16 bytes) before it opens its
//own protected files (EDATA, ".PSPEDAT" with a PGD inside, as the PSP Developer Wiki describes them), which the PSP
//then decrypts as they're read. There is no DRM here: no licence is checked, a game's plain files are read as they
//are, and an EDATA's PGD is decrypted as a UMD data file's is (sceNpDrmEdataSetupKey). uOFW's npdrm exports name the
//user library's five functions; none has a pspautotests program.
//(the licensee key, 16 bytes): the key a download game hands before it opens its protected EDATA files. It's the
//version key the PGD decrypts with (part that follows), stored for sceNpDrmEdataSetupKey to put on them.
auto Kernel::sceNpDrmSetLicenseeKey() -> void {
  if(memory.reaches(arg(0), 16)) {
    memory.copyOut(npdrmLicenseeKey, arg(0), 16);
    npdrmLicenseeKeySet = true;
  }
  result(0);
}

//The licensee key, gone.
auto Kernel::sceNpDrmClearLicenseeKey() -> void {
  npdrmLicenseeKeySet = false;
  result(0);
}

//(name): whether a protected file still has the name it was sold with: always, here.
auto Kernel::sceNpDrmRenameCheck() -> void { result(0); }

//(file): readies an open file's decryption; one not open (or a folder) is BAD_FILE. A protected file is EDATA: a
//"\0PSPEDAT" header, then a whole PGD (the PSP Developer Wiki's PSP_EDAT and PGD pages). The header's word at 0x0c
//says where the PGD starts, in its low 24 bits (its top byte holds flags: "tpu"'s edata.c, written from npdrm): 0x90
//in a download's EDATA, whose header ends with its licence's MAC at 0x80, 0x80 in one without (the wiki's
//examples; PPSSPP's notes take 0x90 always). npdrm gives the PGD layer that offset (ioctl 0x04100002) and the key
//(0x04100001), so the PGD is read as a UMD file's is from there on: its header, then its data where its descriptor
//says, counted from the PGD's start (0x120 into the file, in the usual EDATA). The licensee key is put on it as the
//version key (pgd.cpp recovers the right one from the header's MAC when it isn't). Anything else is read as it is: a
//plain file, or an EDATA holding something other than a PGD. Only a file open to read alone is readied, as npdrm's
//callers open their EDATA (0x40000001) and as the PSP's PGD layer takes them ("tpu"'s decompilation refuses a
//0x40000000 file opened to write, 0x80510201), so a PGD is never a descriptor that emptied its file as it was opened
//(openFile(), seek()); one emptied through another descriptor is as long as the PSP sees it (pgdFileLength()).
auto Kernel::sceNpDrmEdataSetupKey() -> void {
  auto found = files.find(arg(0));
  if(found == files.end() || found->second.folder || found->second.resultOnly) return result(ErrorBadFile);
  auto& open = found->second;
  if(npdrmLicenseeKeySet && !open.sectors && !(open.flags & OpenWrite)) {
    u8 head[16] = {}, magic[4] = {};
    u64 length = pgdFileLength(open);
    if(length >= sizeof(head) && pgdFileRead(open, 0, sizeof(head), head) && !memcmp(head, "\0PSPEDAT", 8)) {
      u32 at = (head[12] | head[13] << 8 | head[14] << 16 | u32(head[15]) << 24) & 0xff'ffff;
      if(u64(at) + 0x90 <= length && pgdFileRead(open, at, 4, magic) && Pgd::is(magic)) {
        memcpy(open.pgdVersionKey, npdrmLicenseeKey, 16);
        open.pgd = true;
        open.pgdReady = false;
        open.pgdDataOffset = at;
      }
    }
  }
  result(0);
}

//(file): the size of an open file's data: an EDATA's readied by sceNpDrmEdataSetupKey, its PGD's data (the
//descriptor's size, which the PGD layer's ioctl 0x04100010 answers); anything else, the file's own size (a memory
//stick file's as the PSP sees it: fileLength()).
auto Kernel::sceNpDrmEdataGetDataSize() -> void {
  auto found = files.find(arg(0));
  if(found == files.end() || found->second.folder || found->second.resultOnly) return result(ErrorBadFile);
  auto& open = found->second;
  if(open.pgd && !open.pgdReady) pgdDescriptor(open);
  if(open.pgd && open.pgdReady) return result(open.pgdDataSize);
  u64 size = open.size;
  if(!open.onDisc) {
    open.stream->flush();
    std::error_code error;
    size = std::filesystem::file_size(open.host, error);
    if(error) size = 0;
    size = fileLength(open.host, size);
  }
  result(u32(std::min<u64>(size, 0x7fff'ffff)));
}
