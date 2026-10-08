//The desktop Library's PSP disc title and icon (PspDiscInfo.hpp): the disc's title, disc ID and icon, read off
//the image by the shared reader (ares/psp/kernel/disc-info.cpp), and cached beside the settings (the data
//folder's psp-icons/) under the file's path+size+mtime, as the Android app's psp-icons/ cache: a second visit
//doesn't re-open the disc.
#include "PspDiscInfo.hpp"
#include "Platform.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nall/nall.hpp>
using namespace nall;
#include <nall/vfs.hpp>
#include <ares/psp/kernel/disc-info.hpp>

namespace fs = std::filesystem;

namespace phobos::desktop {

//The file's size and last-modified time, as the cache key takes them (its path, its size and its mtime).
static auto fileStamp(const std::string& path, u64& size) -> u64 {
  std::error_code error;
  size = (u64)fs::file_size(toPath(path), error);
  if (error) return 0;
  auto time = fs::last_write_time(toPath(path), error);
  if (error) return 0;
  return (u64)time.time_since_epoch().count();
}

auto pspIconKey(const std::string& path, u64 size, u64 mtime) -> std::string {
  auto key = path + "\0" + std::to_string(size) + "\0" + std::to_string(mtime);
  auto digest = Hash::SHA256({(const u8*)key.data(), key.size()}).digest();
  return std::string(digest.data(), digest.size());
}

PspIconCache::PspIconCache(std::string dir) : directory(std::move(dir)) {
  std::error_code error;
  fs::create_directories(toPath(directory), error);
}

auto PspIconCache::info(const std::string& path) -> std::optional<std::pair<std::string, std::string>> {
  u64 size;
  auto mtime = fileStamp(path, size);
  auto key = pspIconKey(path, size, mtime);
  std::ifstream in(toPath(directory + "/" + key + ".info"));
  if (!in) return std::nullopt;
  std::string title, discId;
  std::getline(in, title);
  std::getline(in, discId);
  return std::pair{title, discId};
}

auto PspIconCache::store(const std::string& path, const std::string& title, const std::string& discId,
                         const std::vector<u8>& icon) -> void {
  u64 size;
  auto mtime = fileStamp(path, size);
  auto key = pspIconKey(path, size, mtime);
  {
    std::ofstream out(toPath(directory + "/" + key + ".info"), std::ios::binary);
    if (!out) return;
    out << title << "\n" << discId;
  }
  if (!icon.empty()) {
    std::ofstream out(toPath(directory + "/" + key + ".png"), std::ios::binary);
    if (out) out.write((const char*)icon.data(), (std::streamsize)icon.size());
  }
}

//The file's bytes, read a piece at a time from its host file, as the system reads a disc image's (system.cpp's
//startDisc()): straight from its mapping when the file is mapped into memory, else seek and read.
static auto readFrom(std::shared_ptr<vfs::file> fp, u64 offset, void* data, u64 size) -> u64 {
  if (offset >= fp->size()) return 0;
  size = std::min<u64>(size, fp->size() - offset);
  if (auto bytes = fp->data()) memcpy(data, bytes + offset, size);
  else { fp->seek(offset); fp->read({(u8*)data, size}); }
  return size;
}

//A PSP disc image the list reads its title from: one of the disc's formats (ISO, CSO, ZSO, DAX, JSO, CHD), not
//a PBP (whose title is its folder's name) nor an ELF (a homebrew program, with no disc to read).
auto pspDiscImage(const std::string& file) -> bool {
  auto dot = file.rfind('.');
  if (dot == std::string::npos) return false;
  auto extension = file.substr(dot + 1);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return extension == "iso" || extension == "cso" || extension == "zso"
      || extension == "dax" || extension == "jso" || extension == "chd";
}

auto pspDiscTitle(const std::string& file, PspIconCache& cache) -> std::string {
  if (auto cached = cache.info(file)) return cached->first;  //a hit: the title, cached beside the icon
  auto fp = vfs::disk::open(file.c_str(), vfs::read);
  if (!fp) return {};
  auto read = [fp](u64 offset, void* data, u64 size) -> u64 { return readFrom(fp, offset, data, size); };
  std::string problem;
  auto info = ares::PlayStationPortable::readDiscInfo(read, fp->size(), problem);
  if (info.title.empty()) return {};
  cache.store(file, info.title, info.discId, info.icon);
  return info.title;
}

auto listTitle(const std::string& title) -> std::string {
  for (auto byte : title) if (byte < 0x20 || byte >= 0x7f) return {};
  return title;
}

}
