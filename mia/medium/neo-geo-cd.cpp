struct NeoGeoCD : CompactDisc {
  auto name() -> string override { return "Neo Geo CD"; }
  auto extensions() -> std::vector<string> override { return {"cue", "chd", "zip", "bin"}; }
  auto load(string location) -> LoadResult override;
  auto save(string location) -> bool override;
};

auto NeoGeoCD::load(string location) -> LoadResult {
  // Disc images (.cue/.chd) are attached as cd.rom so the core's disc device
  // can read sectors once the CD drive emulation lands (M2). The system BIOS
  // itself comes from the fw_ng_cd firmware via the SYSTEM pak, never from
  // this medium.
  if(location.iendsWith(".cue") || location.iendsWith(".chd")) {
    if(!inode::exists(location)) return romNotFound;

    this->location = location;

    string manifest;
    manifest += "game\n";
    manifest += {"  name:   ", Medium::name(location), "\n"};
    manifest += {"  title:  ", Medium::name(location), "\n"};
    manifest += {"  region: ", "NTSC-J, NTSC-U, PAL", "\n"};

    auto document = BML::unserialize(manifest);
    pak = std::make_shared<vfs::directory>();
    pak->setAttribute("title",  document["game/title"].string());
    pak->setAttribute("region", document["game/region"].string());
    pak->append("manifest.bml", manifest);
    pak->append("cd.rom", vfs::cdrom::open(location));

    return successful;
  }

  // Legacy M1 fallback (BIOS-in-medium experiments): accept unconditionally.
  this->location = location;
  return successful;
}

auto NeoGeoCD::save(string location) -> bool {
  return true;
}
