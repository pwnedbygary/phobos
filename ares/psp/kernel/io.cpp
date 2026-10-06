//Files and folders, and standard input, output and error.
//
//The PSP reaches its storage through devices: "ms0:" is the memory stick (homebrew, saves), "disc0:" the game's disc
//(also called "umd0:"). Here each device stands for a folder on the host (mount()): "ms0:/PSP/SAVEDATA/X/DATA.BIN" is
//that folder's PSP/SAVEDATA/X/DATA.BIN. Paths are worked out here, "." and ".." included, so a path can never climb
//out of its device's folder; and since the PSP's file system (FAT) ignores case, each name is found whatever its case
//on the host. Standard output and error go to output(): a program's printf ends up there.

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

//A path as the program gives it (a device and a path on it, or a path relative to the working folder) to its host
//file, and to its normalized form ("ms0:/PSP/GAME"). Returns 0, or why it can't: no such device, or a path that would
//climb out of its device (by "..", or through a symbolic link in its folder) or names something no PSP file could be
//called. Names not on the host yet (a file about to be created) keep the case given.
auto Kernel::resolve(const std::string& path, std::string& host, std::string& normalized) -> u32 {
  std::string device, rest;
  auto colon = path.find(':');
  if(colon == std::string::npos) {
    auto base = workingDirectory.find(':');
    if(base == std::string::npos) return ErrorFileNotFound;
    device = workingDirectory.substr(0, base);
    rest = workingDirectory.substr(base + 1) + "/" + path;
  } else {
    device = path.substr(0, colon);
    rest = path.substr(colon + 1);
  }
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
auto Kernel::writeStat(u32 address, const std::string& host) -> void {
  std::error_code error;
  bool folder = std::filesystem::is_directory(host, error);
  u64 size = folder ? 0 : std::filesystem::file_size(host, error);
  if(error) size = 0;
  memory.fill(address, 0, 88);
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

auto Kernel::sceKernelStdin() -> void { result(StandardInput); }
auto Kernel::sceKernelStdout() -> void { result(StandardOutput); }
auto Kernel::sceKernelStderr() -> void { result(StandardError); }

//(path, flags, mode): a file's descriptor. Flags (PSP_O_*) say whether it's read, written or both; whether it's
//created if missing, emptied, or only created if it isn't there already; and whether writes go to its end.
auto Kernel::sceIoOpen() -> void {
  std::string path = memory.readString(arg(0), 1024), host, normalized;
  u32 flags = arg(1);
  if(u32 error = resolve(path, host, normalized)) return result(error);
  bool read = flags & OpenRead, write = flags & OpenWrite;
  if(!read && !write) return result(ErrorInvalidArgument);
  std::error_code error;
  bool exists = std::filesystem::exists(host, error);
  if(exists && std::filesystem::is_directory(host, error)) return result(ErrorIsDirectory);
  if(exists && (flags & OpenCreate) && (flags & OpenExclusive)) return result(ErrorFileExists);
  if(!exists && !(write && (flags & OpenCreate))) return result(ErrorFileNotFound);
  auto mode = std::ios::binary | std::ios::in;
  if(write) mode |= std::ios::out;
  if(write && (!exists || (flags & OpenTruncate))) mode |= std::ios::trunc;  //made, or emptied
  auto stream = std::make_unique<std::fstream>(host, mode);
  if(!stream->is_open()) return result(ErrorNoPermission);
  u32 file = nextFile++;
  auto& open = files[file];
  open.path = normalized;
  open.host = host;
  open.flags = flags;
  open.stream = std::move(stream);
  result(file);
}

auto Kernel::sceIoClose() -> void {
  if(arg(0) <= StandardError) return result(0);
  auto found = files.find(arg(0));
  if(found == files.end() || found->second.folder) return result(ErrorBadFile);
  files.erase(found);
  result(0);
}

//(file, data, size): how many bytes were read (fewer at the end of the file).
auto Kernel::sceIoRead() -> void {
  u32 file = arg(0), data = arg(1), size = arg(2);
  if(file == StandardInput) return result(0);  //nothing to read
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || !(found->second.flags & OpenRead)) return result(ErrorBadFile);
  if(size && !memory.reaches(data, size)) return result(ErrorIllegalAddress);
  auto& open = found->second;
  std::vector<char> buffer(size);
  open.stream->clear();
  open.stream->seekg(std::streamoff(open.position));
  open.stream->read(buffer.data(), size);
  u32 got = u32(open.stream->gcount());
  open.stream->clear();
  memory.copyIn(data, buffer.data(), got);
  open.position += got;
  result(got);
}

//(file, data, size): how many bytes were written.
auto Kernel::sceIoWrite() -> void {
  u32 file = arg(0), data = arg(1), size = arg(2);
  if(file == StandardOutput || file == StandardError) {
    size = std::min<u32>(size, 64_KiB);
    std::string text(size, '\0');
    if(!memory.copyOut(text.data(), data, size)) return result(ErrorIllegalAddress);
    if(output) output(text);
    return result(size);
  }
  auto found = files.find(file);
  if(found == files.end() || found->second.folder || !(found->second.flags & OpenWrite)) return result(ErrorBadFile);
  if(size && !memory.reaches(data, size)) return result(ErrorIllegalAddress);
  auto& open = found->second;
  std::vector<char> buffer(size);
  memory.copyOut(buffer.data(), data, size);
  open.stream->clear();
  if(open.flags & OpenAppend) open.stream->seekp(0, std::ios::end);
  else open.stream->seekp(std::streamoff(open.position));
  open.stream->write(buffer.data(), size);
  open.stream->flush();
  if(!*open.stream) return result(ErrorIOError);
  open.position = u64(open.stream->tellp());
  result(size);
}

//Moves a file's position: from its start (whence 0), where it is (1), or its end (2). Returns where it is now.
auto Kernel::seek(u32 file, s64 offset, u32 whence, u64& position) -> u32 {
  auto found = files.find(file);
  if(found == files.end() || found->second.folder) return ErrorBadFile;
  auto& open = found->second;
  open.stream->flush();
  std::error_code error;
  s64 size = s64(std::filesystem::file_size(open.host, error));
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
  u32 error = seek(arg(0), s64(u64(arg(3)) << 32 | arg(2)), arg(4), position);
  cpu.ipu.r[3] = error ? 0xffff'ffff : u32(position >> 32);
  result(error ? error : u32(position));
}

//(file, 32-bit offset, whence): the new position.
auto Kernel::sceIoLseek32() -> void {
  u64 position = 0;
  u32 error = seek(arg(0), s32(arg(1)), arg(2), position);
  result(error ? error : u32(position));
}

auto Kernel::sceIoRemove() -> void {
  std::string host, normalized;
  if(u32 error = resolve(memory.readString(arg(0), 1024), host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(std::filesystem::is_directory(host, error)) return result(ErrorIsDirectory);
  result(std::filesystem::remove(host, error) ? 0 : ErrorNoPermission);
}

auto Kernel::sceIoMkdir() -> void {
  std::string host, normalized;
  if(u32 error = resolve(memory.readString(arg(0), 1024), host, normalized)) return result(error);
  std::error_code error;
  if(std::filesystem::exists(host, error)) return result(ErrorFileExists);
  result(std::filesystem::create_directory(host, error) ? 0 : ErrorFileNotFound);  //its parent isn't there
}

auto Kernel::sceIoRmdir() -> void {
  std::string host, normalized;
  if(u32 error = resolve(memory.readString(arg(0), 1024), host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(!std::filesystem::is_directory(host, error)) return result(ErrorNotDirectory);
  if(!std::filesystem::is_empty(host, error)) return result(ErrorDirectoryNotEmpty);
  result(std::filesystem::remove(host, error) ? 0 : ErrorNoPermission);
}

//(old path, new path): both on the same device; the new name mustn't be taken.
auto Kernel::sceIoRename() -> void {
  std::string oldHost, oldPath, newHost, newPath;
  if(u32 error = resolve(memory.readString(arg(0), 1024), oldHost, oldPath)) return result(error);
  if(u32 error = resolve(memory.readString(arg(1), 1024), newHost, newPath)) return result(error);
  if(oldPath.substr(0, oldPath.find(':')) != newPath.substr(0, newPath.find(':'))) return result(ErrorCrossDevice);
  std::error_code error;
  if(!std::filesystem::exists(oldHost, error)) return result(ErrorFileNotFound);
  if(std::filesystem::exists(newHost, error) && !sameName(oldPath, newPath)) return result(ErrorFileExists);
  std::filesystem::rename(oldHost, newHost, error);
  result(error ? ErrorNoPermission : 0);
}

//The folder relative paths start from.
auto Kernel::sceIoChdir() -> void {
  std::string host, normalized;
  if(u32 error = resolve(memory.readString(arg(0), 1024), host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(!std::filesystem::is_directory(host, error)) return result(ErrorNotDirectory);
  workingDirectory = normalized;
  result(0);
}

//(path, where to put its SceIoStat)
auto Kernel::sceIoGetstat() -> void {
  std::string host, normalized;
  if(u32 error = resolve(memory.readString(arg(0), 1024), host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(!memory.reaches(arg(1), 88)) return result(ErrorIllegalAddress);
  writeStat(arg(1), host);
  result(0);
}

//(path): a folder's descriptor, for reading its entries one at a time: "." and ".." first (except at a device's top,
//as on FAT), then the names in alphabetical order, whatever their case.
auto Kernel::sceIoDopen() -> void {
  std::string host, normalized;
  if(u32 error = resolve(memory.readString(arg(0), 1024), host, normalized)) return result(error);
  std::error_code error;
  if(!std::filesystem::exists(host, error)) return result(ErrorFileNotFound);
  if(!std::filesystem::is_directory(host, error)) return result(ErrorNotDirectory);
  OpenFile open;
  open.path = normalized;
  open.host = host;
  open.folder = true;
  if(normalized.back() != '/') open.entries = {".", ".."};
  std::vector<std::string> names;
  for(auto& entry : std::filesystem::directory_iterator(host, error)) names.push_back(entry.path().filename().string());
  std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
    for(size_t n = 0; n < a.size() && n < b.size(); n++) {
      auto x = std::tolower(u8(a[n])), y = std::tolower(u8(b[n]));
      if(x != y) return x < y;
    }
    return a.size() < b.size();
  });
  open.entries.insert(open.entries.end(), names.begin(), names.end());
  u32 file = nextFile++;
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
  std::string name = open.entries[open.nextEntry++];
  std::filesystem::path host = open.host;
  if(name == "..") host = host.parent_path();
  else if(name != ".") host /= name;
  writeStat(entry, host.string());
  memory.fill(entry + 88, 0, 256);
  memory.copyIn(entry + 88, name.c_str(), std::min<size_t>(name.size(), 255));
  if(u32 extra = memory.read(4, entry + 344); extra && memory.reaches(extra, 1044)) {
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
