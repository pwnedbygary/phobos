struct NeoGeoCD : Medium {
  auto name() -> string override { return "Neo Geo CD"; }
  auto extensions() -> std::vector<string> override { return {"zip", "bin"}; }
  auto load(string location) -> LoadResult override;
  auto save(string location) -> bool override;
};

auto NeoGeoCD::load(string location) -> LoadResult {
  // Neo Geo CD boots from its system BIOS, which the host platform attaches to
  // the system pak (platform->pak -> "bios.rom" from the fw_ng_cd firmware).
  // There is no required game medium at the BIOS-boot stage (M1); the CD disc
  // itself is loaded in M2. Accept the load unconditionally so the CD system
  // can boot to its menu.
  this->location = location;
  return successful;
}

auto NeoGeoCD::save(string location) -> bool {
  return true;
}
