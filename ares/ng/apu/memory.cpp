auto APU::read(n16 address) -> n8 {
  if(NeoGeo::Model::NeoGeoCD()) return ram[address];
  if(address <= 0x7fff) return cartridge.readM(address);
  //the banks index the M ROM from its start (MAME adds 64KiB only because its region holds a copy of
  //the first 64KiB ahead of the ROM); a bank reaches no further than 256KiB
  u32 mask = (cartridge.mromSize() - 1) & 0x3ffff;
  if(address <= 0xbfff) return cartridge.readM(((u32)rom.bankA << 14 & mask) | n14(address));
  if(address <= 0xdfff) return cartridge.readM(((u32)rom.bankB << 13 & mask) | n13(address));
  if(address <= 0xefff) return cartridge.readM(((u32)rom.bankC << 12 & mask) | n12(address));
  if(address <= 0xf7ff) return cartridge.readM(((u32)rom.bankD << 11 & mask) | n11(address));
  if(address <= 0xffff) return ram[address & 0x7ff];
  unreachable;
}

auto APU::write(n16 address, n8 data) -> void {
  if(NeoGeo::Model::NeoGeoCD()) {
    ram[address] = data;
    return;
  }
  if(address >= 0xf800) {
    ram[address & 0x7ff] = data;
    return;
  }
}

auto APU::in(n16 address) -> n8 {
  switch(n8(address & 0xf)) {
  case 0x00: nmi.pending = false; return communication.input;
  case 0x04: return opnb.read(0);
  case 0x05: return opnb.read(1);
  case 0x06: return opnb.read(2);
  case 0x07: return opnb.read(3);
  case 0x08: rom.bankD = address.byte(1); return 0;
  case 0x09: rom.bankC = address.byte(1); return 0;
  case 0x0a: rom.bankB = address.byte(1); return 0;
  case 0x0b: rom.bankA = address.byte(1); return 0;
  case 0x0c: return 0;  //SDRD1
  }
  return 0;
}

auto APU::out(n16 address, n8 data) -> void {
  switch(n8(address & 0x1f)) {
  case range4(0x08, 0x0b): nmi.enable = 1; return;
  case 0x18:               nmi.enable = 0; return;
  }

  switch(address & 0xf) {
  case 0x00: communication.input = 0;     return;
  case 0x04: return opnb.write(0, data);
  case 0x05: return opnb.write(1, data);
  case 0x06: return opnb.write(2, data);
  case 0x07: return opnb.write(3, data);
  case 0x0c: communication.output = data; return;
  }
}
