//A PSP game: a homebrew program (an EBOOT.PBP, an ELF or a PRX) or a disc image (ISO, or CSO, an ISO compressed in
//blocks). The pak holds the file, read from the disk as the core asks for it (an image can be well over a gigabyte),
//as program.pbp, program.elf, program.prx, disc.iso or disc.cso; and attributes: the title, and the file's location,
//so the core can find the files beside a program.
struct PlayStationPortable : Medium {
  auto name() -> string override { return "PlayStation Portable"; }
  auto type() -> string override { return "Universal Media Disc"; }
  auto extensions() -> std::vector<string> override { return {"iso", "cso", "pbp", "elf", "prx"}; }
  auto load(string location) -> LoadResult override;
  auto save(string location) -> bool override;
  auto isPSP(string location) -> bool;
  auto paramSFO(string location) -> std::vector<u8>;
  auto sfoValue(const std::vector<u8>& sfo, const char* key) -> string;
  auto title(string location) -> string;
};

auto PlayStationPortable::load(string location) -> LoadResult {
  if(!file::exists(location)) return romNotFound;
  string name;
  if(location.iendsWith(".pbp")) name = "program.pbp";
  if(location.iendsWith(".elf")) name = "program.elf";
  if(location.iendsWith(".prx")) name = "program.prx";
  if(location.iendsWith(".iso")) name = "disc.iso";
  if(location.iendsWith(".cso")) name = "disc.cso";
  if(!name || !isPSP(location)) return invalidROM;
  //Recognized above, so a PSP disc isn't taken for another system's, but the core can't read disc images yet.
  if(name.beginsWith("disc.")) return invalidROM;

  this->location = location;
  pak = std::make_shared<vfs::directory>();
  pak->setAttribute("title", title(location));
  pak->setAttribute("location", location);
  pak->append(name, vfs::disk::open(location, vfs::read));
  return successful;
}

auto PlayStationPortable::save(string location) -> bool {
  return true;
}

//Whether the file is a PSP game, as those extensions are shared: an ISO says "PSP GAME" in its primary volume
//descriptor (the ISO 9660 header 32 KiB in, "CD001", then the system it's for); a CSO starts with "CISO"; an
//EBOOT.PBP starts with "\0PBP", and isn't a PlayStation game converted to run on a PSP (CATEGORY "ME" in its
//PARAM.SFO); an ELF or PRX starts with "\x7fELF" and is for MIPS (machine 8), the Allegrex's family.
auto PlayStationPortable::isPSP(string location) -> bool {
  auto file = file::open(location, file::mode::read);
  if(!file) return false;
  u8 head[64] = {};
  file.read({head, std::min<u64>(file.size(), sizeof(head))});
  if(location.iendsWith(".iso")) {
    if(file.size() < 0x8000 + 40) return false;
    u8 descriptor[40];
    file.seek(0x8000);
    file.read({descriptor, sizeof(descriptor)});
    return !memory::compare(descriptor + 1, "CD001", 5) && !memory::compare(descriptor + 8, "PSP GAME", 8);
  }
  if(location.iendsWith(".cso")) return !memory::compare(head, "CISO", 4);
  if(location.iendsWith(".pbp")) {
    if(memory::compare(head, "\0PBP", 4)) return false;
    return sfoValue(paramSFO(location), "CATEGORY") != "ME";
  }
  return !memory::compare(head, "\x7f" "ELF", 4) && (head[18] | head[19] << 8) == 8;
}

//An EBOOT.PBP's PARAM.SFO (its title and details): the PBP starts "\0PBP", a version, then where each of its eight
//parts starts; PARAM.SFO is the first, and runs to the second. Empty if there's none.
auto PlayStationPortable::paramSFO(string location) -> std::vector<u8> {
  auto file = file::open(location, file::mode::read);
  if(!file || file.size() < 40) return {};
  u8 header[40];
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

//The game's title: an EBOOT.PBP's own (TITLE in its PARAM.SFO), else the file's name. (A disc image's title is in its
//PSP_GAME/PARAM.SFO, which needs the image read: the file's name for now.)
auto PlayStationPortable::title(string location) -> string {
  string fallback = Medium::name(location);
  if(!location.iendsWith(".pbp")) return fallback;
  string title = sfoValue(paramSFO(location), "TITLE");
  return title ? title : fallback;
}
