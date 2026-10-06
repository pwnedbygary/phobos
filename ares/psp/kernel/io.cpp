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

//The device a name stands for: the PSP knows the disc as umd0: and disc0:, and the memory stick as ms0: and fatms0:.
static auto deviceName(std::string name) -> std::string {
  for(auto& c : name) c = std::tolower(u8(c));
  if(name == "umd0" || name == "umd1" || name == "umd") return "disc0";
  if(name == "fatms0" || name == "ms") return "ms0";
  return name;
}

static auto sameName(const std::string& a, const std::string& b) -> bool {
  if(a.size() != b.size()) return false;
  for(size_t n = 0; n < a.size(); n++) if(std::tolower(u8(a[n])) != std::tolower(u8(b[n]))) return false;
  return true;
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

//A new file's number: they count up as IDs do (newUID()), are never handed out twice, and run out the same way (0
//then: the file can't be opened).
auto Kernel::newFile() -> u32 {
  return nextFile <= LastUID ? nextFile++ : 0;
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

//Lets a device's paths reach a host folder (an empty folder unmounts it).
auto Kernel::mount(const std::string& device, const std::string& folder) -> void {
  if(folder.empty()) devices.erase(deviceName(device));
  else devices[deviceName(device)] = folder;
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
//called. Names not on the host yet (a file about to be created) keep the case given.
auto Kernel::resolve(const std::string& path, std::string& host, std::string& normalized) -> u32 {
  std::string device, rest;
  if(!split(path, device, rest)) return ErrorFileNotFound;
  device = deviceName(device);
  auto mounted = devices.find(device);
  if(mounted == devices.end()) return ErrorDeviceNotFound;
  std::vector<std::string> names;
  if(u32 error = pathNames(rest, names)) return error;
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
  host = real.string();
  return 0;
}

//Fills a SceIoStat (pspiofilemgr_stat.h, 88 bytes) for a host file or folder: its kind (FIO_S_IFDIR or FIO_S_IFREG,
//readable, writable and runnable by all, as FAT has no permissions), size, and when it was made, read and changed.
//Its last six words (st_private) are left as the program had them, as the PSP leaves them (PPSSPP's notes).
auto Kernel::writeStat(u32 address, const std::string& host) -> void {
  std::error_code error;
  bool folder = std::filesystem::is_directory(host, error);
  u64 size = folder ? 0 : std::filesystem::file_size(host, error);
  if(error) size = 0;
  memory.fill(address, 0, 64);
  memory.write(4, address + 0, folder ? 0x11ff : 0x21ff);
  memory.write(4, address + 4, folder ? 0x0017 : 0x0027);
  memory.write(4, address + 8, u32(size));
  memory.write(4, address + 12, u32(size >> 32));
  auto putTime = [&](u32 at, nall::inode::time which) {  //ScePspDateTime: year, month, day, hour, minute, second
    std::time_t seconds = std::time_t(nall::inode::timestamp(host.c_str(), which));
    if(auto when = std::gmtime(&seconds)) {
      memory.write(2, at + 0, when->tm_year + 1900);
      memory.write(2, at + 2, when->tm_mon + 1);
      memory.write(2, at + 4, when->tm_mday);
      memory.write(2, at + 6, when->tm_hour);
      memory.write(2, at + 8, when->tm_min);
      memory.write(2, at + 10, when->tm_sec);
    }
  };
  putTime(address + 16, nall::inode::time::create);
  putTime(address + 32, nall::inode::time::access);
  putTime(address + 48, nall::inode::time::modify);
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
  files[file] = std::move(open);
  return file;
}

auto Kernel::sceKernelStdin() -> void { result(StandardInput); }
auto Kernel::sceKernelStdout() -> void { result(StandardOutput); }
auto Kernel::sceKernelStderr() -> void { result(StandardError); }

//Opens a file (path, flags): its descriptor, or an error. Flags (PSP_O_*) say whether it's read, written or both;
//whether it's created if missing, emptied, or only created if it isn't there already; and whether writes go to its
//end. sceIoOpen, and sceIoOpenAsync (async.cpp).
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
  if(write && (!exists || (flags & OpenTruncate))) mode |= std::ios::trunc;  //made, or emptied
  if(nextFile > LastUID) return ErrorTooManyFiles;  //before the file is made or emptied
  auto stream = std::make_unique<std::fstream>(host, mode);
  if(!stream->is_open()) return ErrorNoPermission;
  u32 file = newFile();
  auto& open = files[file];
  open.path = normalized;
  open.host = host;
  open.flags = flags;
  open.stream = std::move(stream);
  return file;
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
  files.erase(found);
  result(0);
}

//Reads from an open file into the program's memory: size bytes (or sectors, for umd0: and runs of sectors opened
//through it), fewer at the end. Returns how many, or an error.
auto Kernel::readFile(u32 file, u32 data, u32 size) -> u32 {
  if(file == StandardInput) return 0;  //nothing to read
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || !(found->second.flags & OpenRead)) return ErrorBadFile;
  auto& open = found->second;
  if(open.onDisc) {
    u64 unit = open.sectors ? Disc::SectorSize : 1;
    u64 start = u64(open.sector) * Disc::SectorSize + open.position * unit;
    //no further than the disc goes, whatever the run said: in sectors, its last whole one; in bytes, its last byte
    u64 end = open.sectors ? u64(disc->sectors()) * Disc::SectorSize : disc->size();
    u64 count = std::min<u64>(size, open.size > open.position ? open.size - open.position : 0);
    count = std::min<u64>(count, start < end ? (end - start) / unit : 0);
    u64 bytes = count * unit;
    if(bytes && (bytes > 0xffff'ffff || !memory.reaches(data, u32(bytes)))) return ErrorIllegalAddress;
    std::vector<u8> buffer(bytes);
    if(bytes && !disc->read(start, bytes, buffer.data())) return ErrorIOError;
    memory.copyIn(data, buffer.data(), u32(bytes));
    open.position += count;
    return u32(count);
  }
  if(size && !memory.reaches(data, size)) return ErrorIllegalAddress;
  std::vector<char> buffer(size);
  open.stream->clear();
  open.stream->seekg(std::streamoff(open.position));
  open.stream->read(buffer.data(), size);
  u32 got = u32(open.stream->gcount());
  open.stream->clear();
  memory.copyIn(data, buffer.data(), got);
  open.position += got;
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

//A synchronous read or write is done as it's made (its bytes move now), as an asynchronous request is (async.cpp),
//and the calling thread then waits the time the same request would take its device (asyncDuration()), other
//threads running meanwhile, before it returns value. GTA's disc streaming counts on that: its streaming thread calls
//a request's callback as its last read ends, and the callback drops the request unless the thread that made it, of
//a lower priority, has run meanwhile to note it. An error returns at once.
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
  if(open.flags & OpenAppend) open.stream->seekp(0, std::ios::end);
  else open.stream->seekp(std::streamoff(open.position));
  open.stream->write(buffer.data(), size);
  open.stream->flush();
  if(!*open.stream) return ErrorIOError;
  open.position = u64(open.stream->tellp());
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

//Moves a file's position: from its start (whence 0), where it is (1), or its end (2). Returns where it is now.
auto Kernel::seek(u32 file, s64 offset, u32 whence, u64& position) -> u32 {
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || found->second.resultOnly) return ErrorBadFile;
  auto& open = found->second;
  s64 size = s64(open.size);
  if(!open.onDisc) {
    open.stream->flush();
    std::error_code error;
    size = s64(std::filesystem::file_size(open.host, error));
  }
  s64 base = whence == 0 ? 0 : whence == 1 ? s64(open.position) : whence == 2 ? size : -1;
  if(base < 0 || base + offset < 0) return ErrorInvalidArgument;
  open.position = u64(base + offset);
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
  result(std::filesystem::remove(host, error) ? 0 : ErrorNoPermission);
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
  //names no PSP path can name are left out: a '\' parts a path as '/' does, and a ':' ends a device's name
  std::vector<std::string> names;
  for(auto& entry : std::filesystem::directory_iterator(host, error)) {
    auto name = entry.path().filename().string();
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
  files[file] = std::move(open);
  result(file);
}

//(folder, where to put the entry, a SceIoDirent of 352 bytes): 1 and the next entry (its SceIoStat and name), or 0
//once there are no more. If the program points d_private at a SceIoFatDirentPrivate, its short (8.3) and long names go
//there too.
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
  if(u32 extra = memory.read(4, entry + 344); !open.onDisc && extra && memory.reaches(extra, 1044)) {
    std::string base = name, extension;  //an 8.3 short name: up to eight letters, a dot, up to three, in capitals
    if(auto dot = name.rfind('.'); dot != std::string::npos && dot > 0) base = name.substr(0, dot), extension = name.substr(dot + 1);
    std::string shortName = base.substr(0, 8) + (extension.empty() ? "" : "." + extension.substr(0, 3));
    for(auto& c : shortName) c = std::toupper(u8(c));
    memory.fill(extra + 4, 0, 1040);
    memory.copyIn(extra + 4, shortName.c_str(), std::min<size_t>(shortName.size(), 12));
    memory.copyIn(extra + 20, name.c_str(), std::min<size_t>(name.size(), 1023));
  }
  result(1);
}

auto Kernel::sceIoDclose() -> void {
  auto found = files.find(arg(0));
  if(found == files.end() || !found->second.folder) return result(ErrorBadFile);
  files.erase(found);
  result(0);
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
  }
  return ErrorFunctionNotSupported;
}

//(file, command, in, in length, out, out length)
auto Kernel::sceIoIoctl() -> void {
  if(asyncBusy(arg(0))) return result(ErrorAsyncBusy);
  result(ioctl(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5)));
}

//(device, command, in, in length, out, out length): a request to a whole device. The disc drive's and the memory
//stick's, as PPSSPP's notes on the hardware describe them: the drive holds a game disc, ready, and finished
//anything it was asked to read ahead; the memory stick is in, writable, formatted (FAT), with up to 1 GiB free
//(games add sizes up in 32 bits). Anything else isn't supported.
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
  case 0x0242'5818: {  //its size: in holds where to put clusters (max, free), sectors, sector size, sectors a cluster
    if(!inWord(word) || !memory.reaches(word, 20)) return result(ErrorDevctlBadParameters);
    u64 free = 1_GiB;
    if(auto folder = devices.find("ms0"); folder != devices.end()) {
      std::error_code error;
      auto space = std::filesystem::space(folder->second, error);
      if(!error) free = std::min<u64>(free, space.available);
    }
    u32 cluster = 32_KiB, clusters = u32(free / cluster);
    memory.write(4, word + 0, clusters);
    memory.write(4, word + 4, clusters);
    memory.write(4, word + 8, clusters);
    memory.write(4, word + 12, 512);
    memory.write(4, word + 16, cluster / 512);
    return result(0);
  }
  case 0x0201'5804: case 0x0241'5821:  //register a callback for the stick going in or out (it never does)
    if(!inWord(word) || !callbacks.count(word)) return result(ErrorInvalidArgument);
    memoryStickCallbacks.push_back(word);
    return result(0);
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
