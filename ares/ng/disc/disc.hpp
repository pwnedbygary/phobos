#pragma once

// Neo Geo CD compact disc device.
// M2a skeleton: node/port plumbing plus sector access primitives. The CDD
// command processor and CDC DMA into the TRANSAREA upload zones land in M2b+.

struct Disc {
  Node::Object node;
  Node::Port tray;
  Node::Peripheral cd;
  VFS::Pak pak;
  VFS::File fd;
  CD::Session session;
  //CD audio at 44.1kHz. The frontend mixes streams in lockstep, so it has to run whether or not a
  //track plays: Cdd::tick() feeds it one sector's worth, audio or silence, 75 times a second.
  Node::Audio::Stream stream;

  //disc.cpp
  auto load(Node::Object) -> void;
  auto unload() -> void;
  auto allocate(Node::Port parent) -> Node::Peripheral;
  auto connect() -> void;
  auto disconnect() -> void;

  auto connected() const -> bool { return (bool)cd; }

  // Raw 2352-byte main-sector payload of a logical LBA (audio or mode2 data),
  // or an empty vector when out of range / no disc.
  auto readSectorRaw(u32 lba) -> std::vector<u8>;
};

extern Disc disc;
