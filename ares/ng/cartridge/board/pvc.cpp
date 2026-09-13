struct PVC : Interface {
  using Interface::Interface;

  Memory::Readable<n16> prom;
  Memory::Readable<n8 > mrom;
  Memory::Readable<n8 > crom;
  Memory::Readable<n8 > srom;
  Memory::Readable<n8 > vromA;
  Memory::Readable<n8 > vromB;
  Memory::Writable<n16> ram;

  auto load() -> void override {
    Interface::load(prom, "program.rom");
    Interface::load(mrom, "music.rom");
    Interface::load(crom, "character.rom");
    Interface::load(srom, "static.rom");
    Interface::load(vromA, "voice-a.rom");
    Interface::load(vromB, "voice-b.rom");
    ram.allocate(0x1000, 0);
  }

  auto unload() -> void override {
    prom.reset();
    mrom.reset();
    crom.reset();
    srom.reset();
    vromA.reset();
    vromB.reset();
    ram.reset();
  }

  auto readP(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    if(address >= 0x2fe000 && address <= 0x2fffff) {
      return ram[(address - 0x2fe000) >> 1];
    }
    if(address <= 0x0fffff) return prom[address >> 1];
    if(address >= 0x200000 && address < 0x2fe000) {
      return prom[(bankBase + (address - 0x200000)) >> 1];
    }
    return data;
  }

  auto writeP(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    if(address < 0x2fe000 || address > 0x2fffff) return;

    auto offset = (address - 0x2fe000) >> 1;
    if(upper) ram[offset].byte(1) = data.byte(1);
    if(lower) ram[offset].byte(0) = data.byte(0);

    if(offset == 0xff0) {
      auto pen = ram[0xff0];
      auto b = ((pen & 0x000f) << 1) | ((pen & 0x1000) >> 12);
      auto g = ((pen & 0x00f0) >> 3) | ((pen & 0x2000) >> 13);
      auto r = ((pen & 0x0f00) >> 7) | ((pen & 0x4000) >> 14);
      auto s = (pen & 0x8000) >> 15;
      ram[0xff1] = (g << 8) | b;
      ram[0xff2] = (s << 8) | r;
    }

    if(offset == 0xff4 || offset == 0xff5) {
      auto gb = ram[0xff4];
      auto sr = ram[0xff5];
      ram[0xff6] = ((gb & 0x001e) >> 1) | ((gb & 0x1e00) >> 5)
                 | ((sr & 0x001e) << 7) | ((gb & 0x0001) << 12)
                 | ((gb & 0x0100) << 5) | ((sr & 0x0001) << 14)
                 | ((sr & 0x0100) << 7);
    }

    if(offset >= 0xff8) {
      auto bankAddress = (ram[0xff8] >> 8) | (ram[0xff9] << 8);
      ram[0xff8] = (ram[0xff8] & 0xfe00) | 0x00a0;
      ram[0xff9] &= 0x7fff;
      bankBase = bankAddress + 0x100000;
    }
  }

  auto readM(n32 address) -> n8 override { return mrom[address]; }
  auto mromSize() -> u32 override { return mrom.size(); }
  auto readC(n32 address) -> n8 override { return crom[address]; }
  auto cromMask() -> u32 override { return crom.mask(); }
  auto readS(n32 address) -> n8 override { return srom[address]; }
  auto readVA(n32 address) -> n8 override { return vromA[address]; }
  auto readVB(n32 address) -> n8 override { return vromB ? vromB[address] : vromA[address]; }

  auto power() -> void override {
    bankBase = 0x100000;
  }

  auto serialize(serializer& s) -> void override {
    s(ram);
    s(bankBase);
  }

  n24 bankBase = 0x100000;
};
