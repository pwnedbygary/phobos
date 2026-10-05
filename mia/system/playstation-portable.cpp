struct PlayStationPortable : System {
  auto name() -> string override { return "PlayStation Portable"; }
  auto load(string location) -> LoadResult override;
  auto save(string location) -> bool override;
};

//The PSP needs no firmware: its operating system is Phobos's own (ares/psp/kernel, high-level emulation).
auto PlayStationPortable::load(string location) -> LoadResult {
  this->location = locate();
  pak = std::make_shared<vfs::directory>();
  return successful;
}

auto PlayStationPortable::save(string location) -> bool {
  return true;
}
