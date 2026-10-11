//Disc images made for the PSP's tests (tests/psp/disc.cpp and tests/psp/ares): an ISO 9660 file system holding the
//files given, laid out as a PSP's discs lay theirs out, and the same image packed as a CSO.
//
//The ISO (ECMA-119): sectors 0-15 empty; the primary volume descriptor at 16 ("CD001", the system "PSP GAME", the
//disc's size, its path table and its root folder's record); the terminator at 17; the path table (every folder,
//listed apart) at 18; then each folder's records, one sector each, and each file's data, from its own sector on.
//Folders hold their records for themselves and their parent first, then their entries sorted by name, as ISO 9660
//sorts them; files' names end in ";1".
//
//The CSO: a 24-byte header ("CISO", its size, the disc's size, the block size, the version, the alignment shift),
//an index entry per block and one for the end, then each block deflated (raw, with zlib), or stored as it is when
//deflating doesn't make it smaller: marked by the index entry's top bit in version 1, and by its taking a whole
//block's room in version 2. The last block is padded with zeros to a whole one first, as maxcso pads it.
#pragma once

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace disc_image {

struct File {
  std::string path;  //"PSP_GAME/SYSDIR/EBOOT.BIN": folders made as needed
  std::vector<std::uint8_t> data;
};

struct Image {
  std::vector<std::uint8_t> bytes;
  std::map<std::string, std::uint32_t> sectors;  //where each file and folder starts ("" is the root folder)
};

inline auto put16(std::uint8_t* p, std::uint32_t v) { p[0] = v; p[1] = v >> 8; }
inline auto put32(std::uint8_t* p, std::uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
inline auto putBoth32(std::uint8_t* p, std::uint32_t v) {  //little-endian, then big-endian
  put32(p, v);
  p[4] = v >> 24; p[5] = v >> 16; p[6] = v >> 8; p[7] = v;
}
inline auto putBoth16(std::uint8_t* p, std::uint32_t v) { put16(p, v); p[2] = v >> 8; p[3] = v; }

//The date every record carries: 2004-12-12 12:34:56 (years since 1900, then month, day, hour, minute, second, zone).
inline const std::uint8_t RecordDate[7] = {104, 12, 12, 12, 34, 56, 0};

inline auto makeIso(const std::vector<File>& files) -> Image {
  constexpr std::uint32_t Sector = 2048;
  //the folders: every file's parents, and the root
  std::set<std::string> folders = {""};
  for(auto& file : files) {
    for(auto slash = file.path.find('/'); slash != std::string::npos; slash = file.path.find('/', slash + 1)) {
      folders.insert(file.path.substr(0, slash));
    }
  }
  auto parentOf = [](const std::string& path) {
    auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
  };
  auto nameOf = [](const std::string& path) {
    auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
  };

  Image image;
  std::uint32_t next = 19;  //after the descriptors (16, 17) and the path table (18)
  std::vector<std::string> folderOrder;  //breadth first, as the path table lists them
  folderOrder.push_back("");
  for(std::size_t n = 0; n < folderOrder.size(); n++) {
    std::vector<std::string> children;
    for(auto& folder : folders) if(!folder.empty() && parentOf(folder) == folderOrder[n]) children.push_back(folder);
    std::sort(children.begin(), children.end());
    folderOrder.insert(folderOrder.end(), children.begin(), children.end());
  }
  for(auto& folder : folderOrder) image.sectors[folder] = next++;
  std::map<std::string, std::uint32_t> sizes;
  for(auto& folder : folderOrder) sizes[folder] = Sector;
  for(auto& file : files) {
    image.sectors[file.path] = next;
    sizes[file.path] = file.data.size();
    next += (file.data.size() + Sector - 1) / Sector;
  }
  image.bytes.assign(std::size_t(next) * Sector, 0);
  auto at = [&](std::uint32_t sector) { return image.bytes.data() + std::size_t(sector) * Sector; };

  //a directory record: its length, where it starts and its size, the date, whether it's a folder, its name
  auto record = [&](std::uint8_t* p, const std::string& name, std::uint32_t sector, std::uint32_t size,
                    bool folder) -> std::uint32_t {
    std::uint32_t length = 33 + name.size() + (name.size() % 2 == 0);  //padded to an even length
    p[0] = length;
    putBoth32(p + 2, sector);
    putBoth32(p + 10, size);
    std::memcpy(p + 18, RecordDate, 7);
    p[25] = folder ? 2 : 0;
    putBoth16(p + 28, 1);
    p[32] = name.size();
    std::memcpy(p + 33, name.data(), name.size());
    return length;
  };

  for(auto& folder : folderOrder) {
    std::uint8_t* p = at(image.sectors[folder]);
    std::uint32_t used = 0;
    used += record(p + used, std::string(1, '\0'), image.sectors[folder], Sector, true);
    used += record(p + used, std::string(1, '\1'), image.sectors[parentOf(folder)], Sector, true);
    std::vector<std::pair<std::string, std::string>> entries;  //(name on the disc, path)
    for(auto& child : folders) {
      if(!child.empty() && parentOf(child) == folder) entries.push_back({nameOf(child), child});
    }
    for(auto& file : files) {
      if(parentOf(file.path) == folder) entries.push_back({nameOf(file.path) + ";1", file.path});
    }
    std::sort(entries.begin(), entries.end());
    for(auto& [name, path] : entries) {
      bool isFolder = folders.count(path);
      used += record(p + used, name, image.sectors[path], sizes[path], isFolder);
    }
  }
  for(auto& file : files) if(!file.data.empty()) std::memcpy(at(image.sectors[file.path]), file.data.data(), file.data.size());

  //the path table: each folder's name, first sector and parent's number in the table (counting from 1)
  std::uint8_t* table = at(18);
  std::uint32_t tableSize = 0;
  for(auto& folder : folderOrder) {
    std::string name = folder.empty() ? std::string(1, '\0') : nameOf(folder);
    auto found = std::find(folderOrder.begin(), folderOrder.end(), parentOf(folder));
    std::uint32_t parent = 1 + (found - folderOrder.begin());
    table[tableSize] = name.size();
    put32(table + tableSize + 2, image.sectors[folder]);
    put16(table + tableSize + 6, parent);
    std::memcpy(table + tableSize + 8, name.data(), name.size());
    tableSize += 8 + name.size() + name.size() % 2;
  }

  std::uint8_t* descriptor = at(16);
  descriptor[0] = 1;
  std::memcpy(descriptor + 1, "CD001", 5);
  descriptor[6] = 1;
  std::memset(descriptor + 8, ' ', 64);
  std::memcpy(descriptor + 8, "PSP GAME", 8);
  std::memcpy(descriptor + 40, "TESTDISC", 8);
  putBoth32(descriptor + 80, next);
  putBoth16(descriptor + 120, 1);
  putBoth16(descriptor + 124, 1);
  putBoth16(descriptor + 128, Sector);
  putBoth32(descriptor + 132, tableSize);
  put32(descriptor + 140, 18);
  record(descriptor + 156, std::string(1, '\0'), image.sectors[""], Sector, true);
  std::uint8_t* terminator = at(17);
  terminator[0] = 255;
  std::memcpy(terminator + 1, "CD001", 5);
  terminator[6] = 1;
  return image;
}

inline auto makeCso(const std::vector<std::uint8_t>& iso, std::uint32_t blockSize = 2048, std::uint32_t alignment = 0,
                    std::uint32_t version = 1) -> std::vector<std::uint8_t> {
  std::uint32_t blocks = (iso.size() + blockSize - 1) / blockSize;
  std::vector<std::uint8_t> cso(24 + 4 * (blocks + 1), 0);
  std::memcpy(cso.data(), "CISO", 4);
  put32(cso.data() + 4, 24);
  put32(cso.data() + 8, std::uint32_t(iso.size()));
  put32(cso.data() + 12, std::uint32_t(std::uint64_t(iso.size()) >> 32));
  put32(cso.data() + 16, blockSize);
  cso[20] = version;
  cso[21] = alignment;
  auto align = [&] { while(cso.size() % (1u << alignment)) cso.push_back(0); };
  for(std::uint32_t n = 0; n < blocks; n++) {
    align();
    std::uint32_t entry = cso.size() >> alignment;
    std::vector<std::uint8_t> whole(blockSize, 0);
    std::uint32_t size = blockSize;
    std::size_t available = std::min<std::uint64_t>(blockSize, iso.size() - std::uint64_t(n) * blockSize);
    std::memcpy(whole.data(), iso.data() + std::size_t(n) * blockSize, available);
    const std::uint8_t* source = whole.data();
    std::vector<std::uint8_t> packed(compressBound(size));
    z_stream z{};
    deflateInit2(&z, 9, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY);  //raw deflate: no zlib header
    z.next_in = (Bytef*)source;
    z.avail_in = size;
    z.next_out = packed.data();
    z.avail_out = packed.size();
    deflate(&z, Z_FINISH);
    packed.resize(z.total_out);
    deflateEnd(&z);
    if(packed.size() >= size) {  //stored as it is
      if(version == 1) entry |= 0x8000'0000;
      cso.insert(cso.end(), source, source + size);
    } else {
      cso.insert(cso.end(), packed.begin(), packed.end());
    }
    put32(cso.data() + 24 + 4 * n, entry);
  }
  align();
  put32(cso.data() + 24 + 4 * blocks, std::uint32_t(cso.size() >> alignment));
  return cso;
}

}
