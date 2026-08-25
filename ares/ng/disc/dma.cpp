namespace ares::NeoGeo {

Dma dma;

auto Dma::writeByte(n32 address, n8 data) -> void {
  //mirror the 68K bus: byte writes land in the upper lane at even addresses
  if(address & 1) cpu.write(0, 1, address, n16(data));
  else cpu.write(1, 0, address, n16(data << 8));
}

auto Dma::writeWord(n32 address, n16 data) -> void {
  cpu.write(1, 1, address, data);
}

auto Dma::readByte(n32 address) -> n8 {
  if(address & 1) return cpu.read(0, 1, address).byte(0);
  return cpu.read(1, 0, address).byte(1);
}

auto Dma::readWord(n32 address) -> n16 {
  return cpu.read(1, 1, address);
}

auto Dma::start() -> void {
  static FILE* f = nullptr;
  static u32 n = 0;
  if(!f) f = fopen("/data/user/0/com.phobos.emulator/files/dmalog.txt", "w");
  if(f && n++ < 1000) fprintf(f, "DMA mode=%04x a1=%08x a2=%08x count=%08x val=%04x dt=%02x if=%02x dac=%04x\n",
    (u32)mode, (u32)address1, (u32)address2, (u32)count, (u32)value1,
    (u32)cdc.wreg[6], (u32)cdc.wreg[1], (u32)((cdc.wreg[5]<<8)|cdc.wreg[4]));
  switch(mode) {
  case 0xcffd:
    //self-address write: write addr bytes 3..0 into consecutive words
    while(count--) {
      writeWord(address1 + 0, address1 >> 24);
      writeWord(address1 + 2, address1 >> 16);
      writeWord(address1 + 4, address1 >>  8);
      writeWord(address1 + 6, address1 >>  0);
      address1 += 8;
    }
    break;

  case 0xe2dd:
    //copy bytes from addr1 to addr2, skip odd bytes
    while(count--) {
      writeWord(address2 + 0, readByte(address1 + 0));
      writeWord(address2 + 2, readByte(address1 + 1));
      address1 += 2;
      address2 += 4;
    }
    break;

  case 0xfc2d:
    //copy from LC8951 external buffer to RAM, skip odd bytes
    if(auto data = cdc.initTransfer(count)) {
      while(count--) {
        writeByte(address1 + 0, data[0]);
        writeByte(address1 + 2, data[1]);
        address1 += 4;
        data += 2;
      }
      cdc.endTransfer();
    }
    break;

  case 0xfe3d:
  case 0xfe6d:
    //copy words from addr1 to addr2
    while(count--) {
      writeWord(address2, readWord(address1));
      address1 += 2;
      address2 += 2;
    }
    break;

  case 0xfef5:
    //self-address write: write addr words hi/lo into consecutive words
    while(count--) {
      writeWord(address1 + 0, address1 >> 16);
      writeWord(address1 + 2, address1 >>  0);
      address1 += 4;
    }
    break;

  case 0xffc5:
    //copy from LC8951 external buffer to RAM, packed bytes
    if(auto data = cdc.initTransfer(count)) {
      while(count--) {
        writeByte(address1 + 0, data[0]);
        writeByte(address1 + 1, data[1]);
        address1 += 2;
        data += 2;
      }
      cdc.endTransfer();
    }
    break;

  case 0xffcd:
  case 0xffdd:
    //fill
    while(count--) {
      writeWord(address1, value1);
      address1 += 2;
    }
    break;

  default:
    break;
  }
}

}