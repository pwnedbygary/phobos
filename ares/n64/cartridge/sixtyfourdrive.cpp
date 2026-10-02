//[Phobos] A minimal 64drive control interface (registers at 0x1800'0000) and writable cartridge ROM,
//for the 64DD conversion cartridges (CIC-NUS-5167), which save their data into cartridge ROM.

auto Cartridge::SixtyFourDrive::piAddress(u32 address, PIDeviceTiming) -> bool {
  if(!present) return false;
  if(address < 0x1800'0000 || address > 0x1800'03ff) return false;
  piAddr = address & 0x3ff;
  return true;
}

auto Cartridge::SixtyFourDrive::readWord(u32 address) -> u32 {
  switch(address & 0x3fc) {
  case 0x200: return 0;            //STATUS: idle
  case 0x2ec: return 0x5544'4556;  //MAGIC: "UDEV"
  }
  return 0;
}

auto Cartridge::SixtyFourDrive::piReadHalf(PIDeviceTiming) -> maybe<u16> {
  u32 word = readWord(piAddr);
  u16 data = piAddr & 2 ? u16(word) : u16(word >> 16);
  piAddr = (piAddr + 2) & 0x3ff;
  return data;
}

auto Cartridge::SixtyFourDrive::piWriteHalf(u16 data, PIDeviceTiming) -> void {
  if(!(piAddr & 2)) {
    latch = data;
  } else if((piAddr & 0x3fc) == 0x208) {
    command(u32(latch) << 16 | data);  //COMMAND
  }
  piAddr = (piAddr + 2) & 0x3ff;
}

auto Cartridge::SixtyFourDrive::command(u32 data) -> void {
  switch(data & 0xff) {
  case 0xf0: romWrites = 1; break;  //enable cartridge ROM writes
  case 0xf1: romWrites = 0; break;  //disable cartridge ROM writes
  }
}

namespace {
  //save.cartrom: "P64W", a version, the ROM's size, then runs of written ROM, each an offset, a
  //length and that many bytes as the core stores them.
  constexpr u32 RomWritesMagic = 0x5036'3457;
  constexpr u32 RomWritesVersion = 1;

  auto putWord(std::vector<u8>& out, u32 value) -> void {
    for(u32 shift : {24, 16, 8, 0}) out.push_back(u8(value >> shift));
  }

  auto getWord(const u8* data) -> u32 {
    return u32(data[0]) << 24 | u32(data[1]) << 16 | u32(data[2]) << 8 | u32(data[3]);
  }
}

auto Cartridge::loadRomWrites() -> void {
  auto fp = pak->read("save.cartrom");
  if(!fp || fp->size() < 12) return;
  std::vector<u8> file(fp->size());
  fp->read({file.data(), file.size()});
  if(getWord(&file[0]) != RomWritesMagic || getWord(&file[4]) != RomWritesVersion) return;
  if(getWord(&file[8]) != rom.size) return;
  //all or nothing: a damaged file changes no ROM, so a save can't keep just part of it
  for(u64 at = 12; at < file.size();) {
    if(file.size() - at < 8) return;
    u32 offset = getWord(&file[at + 0]);
    u32 length = getWord(&file[at + 4]);
    at += 8;
    if(length > file.size() - at || offset > rom.size || length > rom.size - offset) return;
    at += length;
  }
  for(u64 at = 12; at < file.size();) {
    u32 offset = getWord(&file[at + 0]);
    u32 length = getWord(&file[at + 4]);
    at += 8;
    std::memcpy(rom.data + offset, &file[at], length);
    for(u32 block = offset >> 16; block < romDirty.size() && (u64)block << 16 < (u64)offset + length; block++) romDirty[block] = true;
    at += length;
  }
}

auto Cartridge::saveRomWrites() -> void {
  auto fp = pak->write("save.cartrom");
  if(!fp) return;
  std::vector<u8> file;
  for(u32 block = 0; block < romDirty.size();) {
    if(!romDirty[block]) { block++; continue; }
    u32 first = block;
    while(block < romDirty.size() && romDirty[block]) block++;
    u32 offset = first << 16;
    u32 length = std::min<u64>((u64)block << 16, rom.size) - offset;
    if(file.empty()) {
      putWord(file, RomWritesMagic);
      putWord(file, RomWritesVersion);
      putWord(file, rom.size);
    }
    putWord(file, offset);
    putWord(file, length);
    file.insert(file.end(), rom.data + offset, rom.data + offset + length);
  }
  if(file.empty()) return;
  fp->resize(file.size());
  fp->seek(0);
  fp->write({file.data(), file.size()});
}
