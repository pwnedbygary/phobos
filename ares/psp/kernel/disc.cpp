//The disc image reader and the ISO 9660 file system on it (disc.hpp describes both).

static auto little32(const u8* p) -> u32 { return p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24; }

//Opens an image: a CSO if it starts with "CISO", else an ISO, which must have a volume descriptor at sector 16.
auto Disc::open(Reader reader, u64 imageSize, std::string& error) -> bool {
  this->reader = reader;
  compressed = false;
  cachedBlock = -1;
  index.clear();
  u8 header[24] = {};
  if(imageSize >= 24 && reader(0, header, 24) == 24 && !memcmp(header, "CISO", 4)) {
    compressed = true;
    discSize = u64(little32(header + 8)) | u64(little32(header + 12)) << 32;
    blockSize = little32(header + 16);
    version = header[20];
    alignment = header[21];
    if(!blockSize || blockSize % SectorSize || blockSize > 1_MiB || alignment > 31) {
      error = "the CSO's header is damaged";
      return false;
    }
    u64 blocks = (discSize + blockSize - 1) / blockSize;
    if(24 + 4 * (blocks + 1) > imageSize) {  //the index must fit in the file
      error = "the CSO's index is cut short";
      return false;
    }
    index.resize(blocks + 1);
    if(reader(24, index.data(), index.size() * 4) != index.size() * 4) {
      error = "the CSO's index is cut short";
      return false;
    }
    for(auto& entry : index) entry = little32((const u8*)&entry);  //as stored: little-endian, whatever the host's
    //each block starts where the one before it ends, and the last ends inside the file
    for(u64 n = 0; n + 1 < index.size(); n++) {
      if((index[n + 1] & 0x7fff'ffff) < (index[n] & 0x7fff'ffff)) {
        error = "the CSO's index is damaged";
        return false;
      }
    }
    if(u64(index.back() & 0x7fff'ffff) << alignment > imageSize) {
      error = "the CSO's index is damaged";
      return false;
    }
    block.resize(blockSize);
  } else {
    discSize = imageSize;
  }
  sectorCount = u32(std::min<u64>(discSize / SectorSize, 0xffff'ffff));

  u8 descriptor[SectorSize];
  if(!readSectors(16, 1, descriptor) || descriptor[0] != 1 || memcmp(descriptor + 1, "CD001", 5)) {
    error = "no ISO 9660 volume descriptor at sector 16";
    return false;
  }
  if(!record(descriptor + 156, rootEntry) || !rootEntry.folder) {
    error = "the disc's root folder record is damaged";
    return false;
  }
  rootEntry.name.clear();
  return true;
}

//Unpacks a CSO block into `block`.
auto Disc::readBlock(u32 number) -> bool {
  if(s64(number) == cachedBlock) return true;
  cachedBlock = -1;  //whatever happens below, `block` won't hold the block it held
  if(number + 1 >= index.size()) return false;
  u64 start = u64(index[number] & 0x7fff'ffff) << alignment;
  u64 end = u64(index[number + 1] & 0x7fff'ffff) << alignment;
  u64 stored = end - start;  //open() saw that the entries never go back
  if(stored > blockSize + (1ull << alignment)) return false;  //no block takes more than its room and the padding
  //the last block may hold less than a whole block of the disc
  u32 wanted = u32(std::min<u64>(blockSize, discSize - u64(number) * blockSize));
  bool plain = version >= 2 ? stored >= blockSize : (index[number] & 0x8000'0000);
  if(version >= 2 && !plain && (index[number] & 0x8000'0000)) return false;  //LZ4: not read here
  if(plain) {
    if(reader(start, block.data(), wanted) != wanted) return false;
  } else {
    packed.resize(stored);
    if(reader(start, packed.data(), stored) != stored) return false;
    u32 unpacked = blockSize, consumed = u32(stored);
    if(nall::Decode::puff::puff(block.data(), &unpacked, packed.data(), &consumed) != 0) return false;
    if(unpacked < wanted) return false;  //it ended short
  }
  cachedBlock = number;
  return true;
}

//Bytes from the disc as unpacked.
auto Disc::readImage(u64 offset, u64 size, u8* data) -> bool {
  if(offset > discSize || size > discSize - offset) return false;
  if(!compressed) return reader(offset, data, size) == size;
  while(size) {
    u32 number = u32(offset / blockSize), within = u32(offset % blockSize);
    if(!readBlock(number)) return false;
    u64 count = std::min<u64>(size, blockSize - within);
    memcpy(data, block.data() + within, count);
    offset += count, data += count, size -= count;
  }
  return true;
}

auto Disc::readSectors(u32 first, u32 count, u8* data) -> bool {
  return readImage(u64(first) * SectorSize, u64(count) * SectorSize, data);
}

auto Disc::read(u64 offset, u64 size, u8* data) -> bool {
  return readImage(offset, size, data);
}

//A directory record (at least 34 bytes) to an entry: its first sector, size and kind, date and name.
auto Disc::record(const u8* bytes, Entry& entry) -> bool {
  u32 length = bytes[0], nameLength = bytes[32];
  if(length < 34 || 33 + nameLength > length) return false;
  entry.sector = little32(bytes + 2);
  entry.size = little32(bytes + 10);
  memcpy(entry.date, bytes + 18, 7);
  entry.folder = bytes[25] & 2;
  entry.name.assign((const char*)bytes + 33, nameLength);
  if(auto version = entry.name.find(';'); version != std::string::npos) entry.name.resize(version);
  if(!entry.folder && entry.name.size() > 1 && entry.name.back() == '.') {
    entry.name.pop_back();  //"NAME.": a file with no extension
  }
  return true;
}

//A folder's entries, the disc's order (ISO 9660 sorts them by name), without its records for itself and its parent.
//A damaged record ends the folder there, and no folder is read past 256 sectors (some 10,000 entries) or the disc's
//end, so a damaged size can't have each lookup read the whole disc.
auto Disc::list(const Entry& folder) -> std::vector<Entry> {
  std::vector<Entry> entries;
  if(!folder.folder || folder.sector >= sectorCount) return entries;
  u32 count = std::min<u32>({(folder.size + SectorSize - 1) / SectorSize, 256, sectorCount - folder.sector});
  u8 sector[SectorSize];
  for(u32 n = 0; n < count; n++) {
    if(!readSectors(folder.sector + n, 1, sector)) break;
    for(u32 at = 0; at + 34 <= SectorSize && sector[at];) {  //a 0 length: the rest of the sector is empty
      u32 length = sector[at];
      Entry entry;
      if(at + length > SectorSize || !record(sector + at, entry)) return entries;
      if(!(entry.name.size() == 1 && u8(entry.name[0]) <= 1)) entries.push_back(entry);
      at += length;
    }
  }
  return entries;
}

//The entry a path's names lead to from the root, each found whatever its case.
auto Disc::find(const std::vector<std::string>& names, Entry& entry) -> bool {
  Entry at = rootEntry;
  for(auto& name : names) {
    bool found = false;
    for(auto& candidate : list(at)) {
      if(candidate.name.size() != name.size()) continue;
      bool same = true;
      for(size_t n = 0; n < name.size() && same; n++) {
        same = std::tolower(u8(name[n])) == std::tolower(u8(candidate.name[n]));
      }
      if(same) { at = candidate; found = true; break; }
    }
    if(!found) return false;
  }
  entry = at;
  return true;
}
