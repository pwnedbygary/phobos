//The PSP disc's title, disc ID, region and icon (disc-info.hpp): read off the image a few sectors at a time, every
//size bounded, by the disc reader (disc.cpp) and the SFO readers the front ends share (psp-runner and mia).

#include "disc-info.hpp"

#include <cctype>

#if defined(ARES_ENABLE_CHD)
#include <nall/decode/chd.hpp>
#endif

//A PARAM.SFO's text value by its key's name: its "\0PSF" head, where its keys and its values start, and how many
//there are; then for each, 16 bytes: where its key is (from the keys' start), its format, its value's length and
//room, and where its value is (from the values' start). Empty if the key isn't there.
auto sfoValue(const std::vector<u8>& sfo, const char* name) -> std::string {
  if(sfo.size() < 20 || memory::compare(sfo.data(), "\0PSF", 4)) return {};
  auto word = [&](u32 at) -> u32 { return sfo[at] | sfo[at + 1] << 8 | sfo[at + 2] << 16 | (u32)sfo[at + 3] << 24; };
  u64 keys = word(8), values = word(12);
  u32 count = word(16);
  for(u32 n = 0; n < count && 20u + n * 16u + 16u <= sfo.size(); n++) {
    u32 entry = 20u + n * 16u;
    u64 key = keys + (sfo[entry] | sfo[entry + 1] << 8);
    u32 length = word(entry + 4);
    u64 value = values + word(entry + 12);
    if(key >= sfo.size() || value >= sfo.size() || length > sfo.size() - value) continue;
    if(strncmp((const char*)&sfo[key], name, sfo.size() - key) != 0) continue;
    std::string text;
    for(u32 i = 0; i < length && sfo[value + i] && text.size() < 128; i++) {  //UTF-8, capped, controls dropped
      u8 byte = sfo[value + i];
      if(byte >= 0x20) text.push_back((char)byte);
    }
    return text;
  }
  return {};
}

//An EBOOT.PBP's PARAM.SFO (its title and details): the PBP starts "\0PBP", a version, then where each of its eight
//parts starts; PARAM.SFO is the first, and runs to the second. Empty if there's none.
auto paramSFO(vfs::file& file) -> std::vector<u8> {
  if(file.size() < 40) return {};
  u8 header[40];
  file.seek(0);
  file.read({header, sizeof(header)});
  if(memory::compare(header, "\0PBP", 4)) return {};
  auto word = [](const u8* p) -> u32 { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; };
  u32 start = word(header + 8), end = word(header + 12);
  if(end <= start || end - start > 64_KiB || end > file.size()) return {};
  std::vector<u8> sfo(end - start);
  file.seek(start);
  file.read({sfo.data(), sfo.size()});
  return sfo;
}

//The region a disc ID (its NPD ID) names: Japan's discs carry a "J", the US's a "U" and Europe's an "E" in the ID's
//third letter; the rest are "Unknown".
auto regionFromDiscId(const std::string& discId) -> std::string {
  if(discId.size() < 3) return "Unknown";
  switch(toupper(discId[2])) {
    case 'J': return "Japan";
    case 'U': return "US";
    case 'E': return "Europe";
  }
  return "Unknown";
}

//A PSP disc's title, disc ID, region and icon, read off its image: an ISO, one of the scene's compressed forms
//(CSO, ZSO, DAX, JSO), or a CHD, recognized by its head. It reads a little at a time (a few sectors, through the
//Reader), never the whole image, and bounds every size it finds.
auto readDiscInfo(Disc::Reader reader, u64 imageSize, std::string& error) -> DiscInfo {
  DiscInfo info;
  auto image = std::make_shared<Disc>();
  u8 magic[8] = {};
  bool isChd = reader(0, magic, sizeof(magic)) == sizeof(magic) && memory::compare(magic, "MComprHD", 8) == 0;
  if(isChd) {
    //a CHD: its sectors come unpacked from its hunks (nall's Decode::CHD), and the disc reads them as an ISO's
    //bytes, the way system.cpp's startDisc() does.
    #if defined(ARES_ENABLE_CHD)
    auto chd = std::make_shared<nall::Decode::CHD>();
    if(chd->load(reader, imageSize) && chd->dvd()) {
      auto sectors = [chd](u64 offset, void* data, u64 size) -> u64 {
        u64 done = 0;
        while(done < size) {
          u64 at = offset + done;
          auto sector = chd->read(u32(at / Disc::SectorSize));
          if(sector.size() != Disc::SectorSize) break;  //past the end, or a damaged hunk
          u64 count = std::min<u64>(size - done, Disc::SectorSize - at % Disc::SectorSize);
          memcpy((u8*)data + done, sector.data() + at % Disc::SectorSize, count);
          done += count;
        }
        return done;
      };
      if(!image->open(sectors, u64(chd->sectorCount()) * Disc::SectorSize, error)) return info;
    } else {
      return info;
    }
    #else
    return info;  //this build doesn't read CHD images
    #endif
  } else if(!image->open(reader, imageSize, error)) {
    return info;
  }
  //PSP_GAME/PARAM.SFO: its title and its disc ID, bounded at 64 KiB.
  Disc::Entry entry;
  if(image->find({"PSP_GAME", "PARAM.SFO"}, entry) && !entry.folder && entry.size <= 64_KiB) {
    u64 start = u64(entry.sector) * Disc::SectorSize;
    if(start < image->size()) {
      std::vector<u8> sfo(entry.size);
      if(image->read(start, entry.size, sfo.data())) {
        info.title = sfoValue(sfo, "TITLE");
        info.discId = sfoValue(sfo, "DISC_ID");
        info.region = regionFromDiscId(info.discId);
      }
    }
  }
  //PSP_GAME/ICON0.PNG: the disc's icon, bounded at 1 MiB.
  if(image->find({"PSP_GAME", "ICON0.PNG"}, entry) && !entry.folder && entry.size <= 1_MiB) {
    u64 start = u64(entry.sector) * Disc::SectorSize;
    if(start < image->size()) {
      info.icon.resize(entry.size);
      if(!image->read(start, entry.size, info.icon.data())) info.icon.clear();
    }
  }
  return info;
}
