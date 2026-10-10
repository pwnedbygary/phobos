//The disc image reader and the ISO 9660 file system on it (disc.hpp describes both).

static auto little16(const u8* p) -> u32 { return p[0] | p[1] << 8; }
static auto little32(const u8* p) -> u32 { return p[0] | p[1] << 8 | p[2] << 16 | u32(p[3]) << 24; }

//Deflate, as raw (a CSO's), or wrapped in zlib's 2-byte header with a checksum after (a DAX's, and a JSO's that uses
//zlib), which is skipped: the header names deflate (its low four bits 8), checks itself (as a 16-bit number, a
//multiple of 31) and has no preset dictionary (bit 0x20), which no image uses. Fills out (room bytes) with at least
//wanted; false if the packed bytes are damaged or end short.
static auto inflate(const u8* in, u32 size, u8* out, u32 room, u32 wanted, bool wrapped) -> bool {
  if(wrapped) {
    if(size < 2 || (in[0] & 0x0f) != 8 || (in[0] << 8 | in[1]) % 31 || (in[1] & 0x20)) return false;
    in += 2, size -= 2;
  }
  u32 unpacked = room, consumed = size;
  auto source = const_cast<u8*>(in);  //puff only reads it
  return nall::Decode::puff::puff(out, &unpacked, source, &consumed) == 0 && unpacked >= wanted;
}

//Opens an image: a compressed one by what it starts with ("CISO", "ZISO", "DAX", "JISO"), else an ISO; whichever,
//the disc must have a volume descriptor at sector 16.
auto Disc::open(Reader reader, u64 imageSize, std::string& error) -> bool {
  this->reader = reader;
  format = Format::ISO;
  cachedBlock = -1;
  folders.clear();
  index.clear();
  sizes.clear();
  plainFrames.clear();
  u8 header[48] = {};
  u64 got = reader(0, header, std::min<u64>(imageSize, sizeof(header)));
  bool opened = true;
  bool cso = !memcmp(header, "CISO", 4) || !memcmp(header, "ZISO", 4);
  if(got >= 24 && cso) opened = openCSO(header, imageSize, error);
  else if(got >= 32 && !memcmp(header, "DAX\0", 4)) opened = openDAX(header, imageSize, error);
  else if(got >= 48 && !memcmp(header, "JISO", 4)) opened = openJSO(header, imageSize, error);
  else discSize = imageSize;
  if(!opened) return false;
  if(format != Format::ISO) block.resize(blockSize);
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

//A CSO's (or a ZSO's) header and index: each block's start, the last entry where the data ends. Each block starts
//where the one before it ends, and the last ends inside the file.
auto Disc::openCSO(const u8* header, u64 imageSize, std::string& error) -> bool {
  format = !memcmp(header, "ZISO", 4) ? Format::ZSO : Format::CSO;
  std::string kind = format == Format::ZSO ? "ZSO" : "CSO";
  discSize = u64(little32(header + 8)) | u64(little32(header + 12)) << 32;
  blockSize = little32(header + 16);
  version = header[20];
  alignment = header[21];
  if(!blockSize || blockSize % SectorSize || blockSize > 1_MiB || alignment > 31) {
    error = "the " + kind + "'s header is damaged";
    return false;
  }
  u64 blocks = (discSize + blockSize - 1) / blockSize;
  if(24 + 4 * (blocks + 1) > imageSize) {  //the index must fit in the file
    error = "the " + kind + "'s index is cut short";
    return false;
  }
  index.resize(blocks + 1);
  if(reader(24, index.data(), index.size() * 4) != index.size() * 4) {
    error = "the " + kind + "'s index is cut short";
    return false;
  }
  for(auto& entry : index) entry = little32((const u8*)&entry);  //as stored: little-endian, whatever the host's
  for(u64 n = 0; n + 1 < index.size(); n++) {
    if((index[n + 1] & 0x7fff'ffff) < (index[n] & 0x7fff'ffff)) {
      error = "the " + kind + "'s index is damaged";
      return false;
    }
  }
  if(u64(index.back() & 0x7fff'ffff) << alignment > imageSize) {
    error = "the " + kind + "'s index is damaged";
    return false;
  }
  return true;
}

//A DAX's tables: each 8 KiB frame's start (32 bits) and packed size (16 bits), then, from version 1, the areas left
//uncompressed (each its first frame and how many frames). Every frame lies inside the file, and the areas inside the
//disc, no more frames in all than it has (so a damaged count can't ask for a huge table, or a long time to read it).
auto Disc::openDAX(const u8* header, u64 imageSize, std::string& error) -> bool {
  format = Format::DAX;
  blockSize = 0x2000;
  discSize = little32(header + 4);
  u32 daxVersion = little32(header + 8), areas = daxVersion >= 1 ? little32(header + 12) : 0;
  u64 frames = (discSize + blockSize - 1) / blockSize;
  if(areas > frames) {
    error = "the DAX's uncompressed areas are damaged";
    return false;
  }
  u64 tables = 6 * frames + 8 * u64(areas);
  if(32 + tables > imageSize) {
    error = "the DAX's tables are cut short";
    return false;
  }
  std::vector<u8> table(tables);
  if(reader(32, table.data(), tables) != tables) {
    error = "the DAX's tables are cut short";
    return false;
  }
  index.resize(frames);
  sizes.resize(frames);
  for(u64 n = 0; n < frames; n++) index[n] = little32(&table[4 * n]), sizes[n] = little16(&table[4 * frames + 2 * n]);
  plainFrames.assign(frames, false);
  u64 plain = 0;
  for(u64 n = 0; n < areas; n++) {
    u64 first = little32(&table[6 * frames + 8 * n]), count = little32(&table[6 * frames + 8 * n + 4]);
    plain += count;
    if(first + count > frames || plain > frames) {
      error = "the DAX's uncompressed areas are damaged";
      return false;
    }
    for(u64 frame = first; frame < first + count; frame++) plainFrames[frame] = true;
  }
  for(u64 n = 0; n < frames; n++) {
    u64 stored = plainFrame(u32(n)) ? std::min<u64>(blockSize, discSize - n * blockSize) : sizes[n];
    if(index[n] + stored > imageSize) {
      error = "the DAX's index is damaged";
      return false;
    }
  }
  return true;
}

auto Disc::plainFrame(u32 number) const -> bool {
  return number < plainFrames.size() && plainFrames[number];
}

//A JSO's header and index: each block's start, and one more for the end. A block that takes a whole block's room
//(or, the last, the rest of the disc) is stored as it is; the others are packed with LZO or zlib, the header says
//which. Block headers (a JSO tool's option) aren't read.
auto Disc::openJSO(const u8* header, u64 imageSize, std::string& error) -> bool {
  format = Format::JSO;
  blockSize = little16(header + 6);
  discSize = little32(header + 12);
  if(!blockSize || blockSize % SectorSize || header[10] > 1) {
    error = "the JSO's header is damaged";
    return false;
  }
  if(header[8]) {
    error = "the JSO has block headers, which aren't read yet";
    return false;
  }
  lzo = header[10] == 0;
  u64 blocks = (discSize + blockSize - 1) / blockSize;
  if(48 + 4 * (blocks + 1) > imageSize) {
    error = "the JSO's index is cut short";
    return false;
  }
  index.resize(blocks + 1);
  if(reader(48, index.data(), index.size() * 4) != index.size() * 4) {
    error = "the JSO's index is cut short";
    return false;
  }
  for(auto& entry : index) entry = little32((const u8*)&entry);
  for(u64 n = 0; n + 1 < index.size(); n++) {
    if(index[n + 1] < index[n] || index[n + 1] - index[n] > blockSize) {
      error = "the JSO's index is damaged";
      return false;
    }
  }
  if(index.back() > imageSize) {
    error = "the JSO's index is damaged";
    return false;
  }
  return true;
}

//Unpacks a compressed image's block into `block`.
auto Disc::readBlock(u32 number) -> bool {
  if(s64(number) == cachedBlock) return true;
  cachedBlock = -1;  //whatever happens below, `block` won't hold the block it held
  if(u64(number) * blockSize >= discSize) return false;
  //the last block may hold less than a whole block of the disc
  u32 wanted = u32(std::min<u64>(blockSize, discSize - u64(number) * blockSize));
  enum class Packing { Stored, Deflate, Zlib, ZlibOrDeflate, LZ4, LZO } packing = Packing::Stored;
  u64 start = 0, stored = 0;
  if(format == Format::CSO || format == Format::ZSO) {
    if(number + 1 >= index.size()) return false;
    start = u64(index[number] & 0x7fff'ffff) << alignment;
    stored = (u64(index[number + 1] & 0x7fff'ffff) << alignment) - start;  //open() saw that it never goes back
    if(stored > blockSize + (1ull << alignment)) return false;  //no block takes more than its room and the padding
    bool flag = index[number] & 0x8000'0000;
    if(format == Format::ZSO) packing = flag ? Packing::Stored : Packing::LZ4;
    else if(version >= 2) packing = stored >= blockSize ? Packing::Stored : flag ? Packing::LZ4 : Packing::Deflate;
    else packing = flag ? Packing::Stored : Packing::Deflate;
  } else if(format == Format::DAX) {
    if(number >= index.size()) return false;
    start = index[number];
    packing = plainFrame(number) ? Packing::Stored : Packing::Zlib;
    stored = sizes[number];
  } else if(format == Format::JSO) {
    if(number + 1 >= index.size()) return false;
    start = index[number];
    stored = index[number + 1] - index[number];
    packing = stored == blockSize ? Packing::Stored : lzo ? Packing::LZO : Packing::ZlibOrDeflate;
  } else {
    return false;
  }
  if(packing == Packing::Stored) {
    if(reader(start, block.data(), wanted) != wanted) return false;
    cachedBlock = number;
    return true;
  }
  packed.resize(stored);
  if(reader(start, packed.data(), stored) != stored) return false;
  bool unpacked = false;
  auto inflated = [&](bool wrapped) {
    return inflate(packed.data(), u32(stored), block.data(), blockSize, wanted, wrapped);
  };
  switch(packing) {
  case Packing::Deflate: unpacked = inflated(false); break;
  case Packing::Zlib: unpacked = inflated(true); break;
  case Packing::ZlibOrDeflate: unpacked = inflated(true) || inflated(false); break;  //a JSO's: tools differ
  case Packing::LZ4: unpacked = unpackLZ4(packed.data(), u32(stored), block.data(), blockSize) >= wanted; break;
  case Packing::LZO: unpacked = unpackLZO(packed.data(), u32(stored), block.data(), blockSize) >= wanted; break;
  case Packing::Stored: break;
  }
  //a JSO's last block, shorter than the rest, may be stored as it is at its own length
  if(!unpacked && format == Format::JSO && stored == wanted) {
    memcpy(block.data(), packed.data(), wanted);
    unpacked = true;
  }
  if(!unpacked) return false;
  cachedBlock = number;
  return true;
}

//Bytes from the disc as unpacked.
auto Disc::readImage(u64 offset, u64 size, u8* data) -> bool {
  if(offset > discSize || size > discSize - offset) return false;
  if(format == Format::ISO) return reader(offset, data, size) == size;
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
  return listed(folder);
}

//The same, read once a folder: a disc never changes, and a path is looked up folder by folder each time a file is
//opened by it. God of War: Ghost of Sparta opens a file about 3,000 times a frame (refused its PGD key, it tries
//again), and each lookup read the folders' sectors afresh, unpacking a compressed image's blocks (a CHD's hunks) for
//them: in the library run it reached frame 60 in 900 seconds. A folder a sector of which couldn't be read is read
//again next time (a damaged record ends it for good, but a read that fails may not fail again).
auto Disc::listed(const Entry& folder) -> const std::vector<Entry>& {
  u64 key = u64(folder.sector) << 32 | folder.size;
  if(!folder.folder) key = ~0ull;  //(never a folder's: nothing listed)
  if(auto found = folders.find(key); found != folders.end()) return found->second;
  std::vector<Entry> entries;
  auto read = [&]() -> bool {  //false if a sector couldn't be read
    if(!folder.folder || folder.sector >= sectorCount) return true;
    u32 count = std::min<u32>({(folder.size + SectorSize - 1) / SectorSize, 256, sectorCount - folder.sector});
    u8 sector[SectorSize];
    for(u32 n = 0; n < count; n++) {
      if(!readSectors(folder.sector + n, 1, sector)) return false;
      for(u32 at = 0; at + 34 <= SectorSize && sector[at];) {  //a 0 length: the rest of the sector is empty
        u32 length = sector[at];
        Entry entry;
        if(at + length > SectorSize || !record(sector + at, entry)) return true;
        if(!(entry.name.size() == 1 && u8(entry.name[0]) <= 1)) entries.push_back(entry);
        at += length;
      }
    }
    return true;
  };
  if(!read()) return unkept = std::move(entries);
  return folders[key] = std::move(entries);
}

//The entry a path's names lead to from the root, each found whatever its case.
auto Disc::find(const std::vector<std::string>& names, Entry& entry) -> bool {
  Entry at = rootEntry;
  for(auto& name : names) {
    bool found = false;
    for(auto& candidate : listed(at)) {
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
