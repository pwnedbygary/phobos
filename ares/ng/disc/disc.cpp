namespace ares::NeoGeo {

#include "disc.hpp"

Disc disc;

auto Disc::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("Neo Geo CD");

  tray = node->append<Node::Port>("Disc Tray");
  tray->setFamily("Neo Geo CD");
  tray->setType("Compact Disc");
  tray->setHotSwappable(true);
  tray->setAllocate([&](auto name) { return allocate(tray); });
  tray->setConnect([&] { return connect(); });
  tray->setDisconnect([&] { return disconnect(); });
}

auto Disc::unload() -> void {
  disconnect();
  tray.reset();
  node.reset();
}

auto Disc::allocate(Node::Port parent) -> Node::Peripheral {
  return cd = parent->append<Node::Peripheral>("Neo Geo CD Disc");
}

auto Disc::connect() -> void {
  if(!cd) return;
  if(!cd->setPak(pak = platform->pak(cd))) return disconnect();

  fd = pak->read("cd.rom");
  if(!fd) return disconnect();
  if(fd->size() < 2448) return disconnect();

  //decode the TOC from the disc lead-in (same layout as PlayStation: raw
  //2448-byte sectors, 96-byte subchannel at offset 2352)
  u32 sectors = fd->size() / 2448;
  std::vector<u8> subchannel;
  subchannel.resize(sectors * 96);
  for(u32 sector : range(sectors)) {
    fd->seek(sector * 2448 + 2352);
    fd->read({subchannel.data() + sector * 96, u32(96)});
  }
  session.decode(subchannel, 96);
}

auto Disc::disconnect() -> void {
  fd.reset();
  pak.reset();
  cd.reset();
}

auto Disc::readSectorRaw(u32 lba) -> std::vector<u8> {
  //nall::vfs::cdrom stores the image as a raw 2448-byte-per-sector stream that
  //begins at the disc lead-in: logical LBA N lives at physical sector
  //(LeadInSectors + LBAtoABA(N)).  Seeking at lba*2448 hits the blank lead-in
  //and yields all-zero sectors (PS1::Drive uses the same mapping).
  std::vector<u8> sector;
  if(!fd) return sector;
  s64 physical = (s64)CD::LeadInSectors + (s64)CD::LBAtoABA((s32)lba);
  if(physical < 0 || (u64)physical >= fd->size() / 2448) return sector;
  sector.resize(2352);
  fd->seek(2448ull * (u64)physical);
  fd->read({sector.data(), sector.size()});
  return sector;
}

}
