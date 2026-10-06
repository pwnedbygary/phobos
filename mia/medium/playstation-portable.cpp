//A PSP game: a homebrew program (an EBOOT.PBP, an ELF or a PRX) or a disc image: an ISO, one of the PSP scene's
//forms of it compressed in blocks (CSO, ZSO, DAX, JSO), or a CHD (MAME's). The pak holds the file, read from the disk
//as the core asks for it (an image can be well over a gigabyte), as program.pbp, program.elf, program.prx, or
//disc.iso (or .cso, .zso, .dax, .jso, .chd); and attributes: the title, and the file's location, so the core can find
//the files beside a program.
//
//What a file is comes from its contents, not its name (PlayStation games share .pbp, .iso and .chd): an ISO says "PSP
//GAME" in its primary volume descriptor (the ISO 9660 header 32 KiB in, "CD001", then the system it's for); the
//compressed forms start with "CISO", "ZISO", "DAX" and "JISO"; a CHD starts with "MComprHD", and a DVD's (as a UMD's
//is) holds 2048-byte units where a CD's (a PlayStation game's) holds 2448; an EBOOT.PBP starts with "\0PBP" and
//isn't a PlayStation game converted to run on a PSP (CATEGORY "ME" in its PARAM.SFO); an ELF or PRX starts with
//"\x7fELF" and is for MIPS (machine 8), the Allegrex's family.
//
//On Android, a file the app was handed by the system's file picker can come as a descriptor, named
//"/proc/self/fd/N". Opening that name again may be refused (the app has no permission to the file's path, only the
//open file), so it's read through the descriptor itself.
struct PlayStationPortable : Medium {
  auto name() -> string override { return "PlayStation Portable"; }
  auto type() -> string override { return "Universal Media Disc"; }
  auto extensions() -> std::vector<string> override {
    return {"iso", "cso", "zso", "dax", "jso", "chd", "pbp", "elf", "prx"};
  }
  auto load(string location) -> LoadResult override;
  auto save(string location) -> bool override;
  auto open(string location) -> std::shared_ptr<vfs::file>;
  auto kind(string location, vfs::file& file) -> string;
  auto paramSFO(vfs::file& file) -> std::vector<u8>;
  auto sfoValue(const std::vector<u8>& sfo, const char* key) -> string;
};

//The file at location: through its descriptor if that's what the name stands for, else from the disk.
auto PlayStationPortable::open(string location) -> std::shared_ptr<vfs::file> {
  #if defined(API_POSIX)
  if(location.beginsWith("/proc/self/fd/")) return vfs::descriptor::open(location.slice(14).natural());
  #endif
  if(!file::exists(location)) return {};
  return vfs::disk::open(location, vfs::read);
}

auto PlayStationPortable::load(string location) -> LoadResult {
  auto file = open(location);
  if(!file) return romNotFound;
  string name = kind(location, *file);
  if(!name) {
    //a CD's CHD (2448-byte units) holds no UMD: say how a PSP game's is made, which front ends show
    u8 head[64] = {};
    file->seek(0);
    file->read({head, (size_t)std::min<u64>(file->size(), sizeof(head))});
    u32 unit = head[60] << 24 | head[61] << 16 | head[62] << 8 | head[63];
    if(!memory::compare(head, "MComprHD", 8) && unit == 2448) {
      return {invalidROM, "It's a CD's CHD. A PSP game's CHD is made from its ISO with chdman createdvd."};
    }
    return invalidROM;
  }

  //the title: an EBOOT.PBP's own (TITLE in its PARAM.SFO), else the file's name (a disc image's title is in its
  //PSP_GAME/PARAM.SFO, which needs the image read: the file's name for now)
  string title = name == "program.pbp" ? sfoValue(paramSFO(*file), "TITLE") : string{};
  this->location = location;
  pak = std::make_shared<vfs::directory>();
  pak->setAttribute("title", title ? title : Medium::name(location));
  pak->setAttribute("location", location);
  file->seek(0);
  pak->append(name, file);
  return successful;
}

auto PlayStationPortable::save(string location) -> bool {
  return true;
}

//What the file is (the name it goes in the pak under), or nothing if it isn't a PSP game. A PRX is told from an ELF
//by its name alone: both are ELF files, and the core starts either the same way.
auto PlayStationPortable::kind(string location, vfs::file& file) -> string {
  u8 head[64] = {};
  file.seek(0);
  file.read({head, (size_t)std::min<u64>(file.size(), sizeof(head))});
  if(!memory::compare(head, "CISO", 4)) return "disc.cso";
  if(!memory::compare(head, "ZISO", 4)) return "disc.zso";
  if(!memory::compare(head, "DAX\0", 4)) return "disc.dax";
  if(!memory::compare(head, "JISO", 4)) return "disc.jso";
  //a CHD of version 5: its unit size (big-endian) 60 bytes in
  if(!memory::compare(head, "MComprHD", 8)) {
    u32 version = head[12] << 24 | head[13] << 16 | head[14] << 8 | head[15];
    u32 unit = head[60] << 24 | head[61] << 16 | head[62] << 8 | head[63];
    return version == 5 && unit == 2048 ? "disc.chd" : "";
  }
  if(file.size() >= 0x8000 + 40) {
    u8 descriptor[40];
    file.seek(0x8000);
    file.read({descriptor, sizeof(descriptor)});
    if(!memory::compare(descriptor + 1, "CD001", 5)) {
      return !memory::compare(descriptor + 8, "PSP GAME", 8) ? "disc.iso" : "";
    }
  }
  if(!memory::compare(head, "\0PBP", 4)) return sfoValue(paramSFO(file), "CATEGORY") != "ME" ? "program.pbp" : "";
  if(!memory::compare(head, "\x7f" "ELF", 4) && (head[18] | head[19] << 8) == 8) {
    return location.iendsWith(".prx") ? "program.prx" : "program.elf";
  }
  return {};
}

//An EBOOT.PBP's PARAM.SFO (its title and details): the PBP starts "\0PBP", a version, then where each of its eight
//parts starts; PARAM.SFO is the first, and runs to the second. Empty if there's none.
auto PlayStationPortable::paramSFO(vfs::file& file) -> std::vector<u8> {
  if(file.size() < 40) return {};
  u8 header[40];
  file.seek(0);
  file.read({header, 40});
  if(memory::compare(header, "\0PBP", 4)) return {};
  auto word = [](const u8* p) -> u32 { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; };
  u32 start = word(header + 8), end = word(header + 12);
  if(end <= start || end - start > 64_KiB || end > file.size()) return {};
  std::vector<u8> sfo(end - start);
  file.seek(start);
  file.read({sfo.data(), sfo.size()});
  return sfo;
}

//A text value from a PARAM.SFO: "\0PSF", a version, where its keys and its values start, and how many there are;
//then for each, 16 bytes: where its key is (from the keys' start), its format, its value's length and room, and where
//its value is (from the values' start). Empty if the key isn't there.
auto PlayStationPortable::sfoValue(const std::vector<u8>& sfo, const char* name) -> string {
  if(sfo.size() < 20 || memory::compare(sfo.data(), "\0PSF", 4)) return {};
  auto word = [&](u32 at) -> u32 { return sfo[at] | sfo[at + 1] << 8 | sfo[at + 2] << 16 | (u32)sfo[at + 3] << 24; };
  u32 keys = word(8), values = word(12), count = word(16);
  for(u32 n = 0; n < count && 20 + n * 16 + 16 <= sfo.size(); n++) {
    u32 entry = 20 + n * 16;
    u32 key = keys + (sfo[entry] | sfo[entry + 1] << 8), length = word(entry + 4), value = values + word(entry + 12);
    if(key >= sfo.size() || value >= sfo.size() || length > sfo.size() - value) continue;
    if(strncmp((const char*)&sfo[key], name, sfo.size() - key) != 0) continue;
    std::string text;
    for(u32 i = 0; i < length && sfo[value + i]; i++) text.push_back((char)sfo[value + i]);  //UTF-8, ending in a 0
    return string{text.c_str()};
  }
  return {};
}
