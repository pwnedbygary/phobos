#if defined(__ANDROID__)
#include <android/log.h>
#endif
struct Rom : Interface {
  using Interface::Interface;

  Memory::Readable<n16> prom;
  Memory::Readable<n8 > mrom;
  Memory::Readable<n8 > crom;
  Memory::Readable<n8 > srom;
  Memory::Readable<n8 > vromA;
  Memory::Readable<n8 > vromB;

  auto load() -> void override {
    Interface::load(prom, "program.rom");
    Interface::load(mrom, "music.rom");;
    Interface::load(crom, "character.rom");
    Interface::load(srom, "static.rom");
    Interface::load(vromA, "voice-a.rom");
    Interface::load(vromB, "voice-b.rom");
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "board rom: p=%u m=%u c=%u s=%u vA=%u vB=%u p0=%02x%02x%02x%02x",
      (unsigned)prom.size()*2, (unsigned)mrom.size(), (unsigned)crom.size(), (unsigned)srom.size(),
      (unsigned)vromA.size(), (unsigned)vromB.size(),
      prom.size()>1?prom[0].byte(1):0, prom.size()>1?prom[0].byte(0):0,
      (crom.size()>1?(unsigned)crom[0]:0u), (crom.size()>1?(unsigned)crom[1]:0u));
#endif
  }

  auto unload() -> void override {
    prom.reset();
    mrom.reset();
    crom.reset();
    srom.reset();
    vromA.reset();
    vromB.reset();
  }

  virtual auto programWord(u32 address) -> n16 {
    return prom[address >> 1];
  }

  virtual auto bankAddress(n24 address) -> u32 {
    return (romBank + 1) * 0x100000 | n20(address);
  }

  auto readP(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    if(address <= 0x0fffff) return programWord(address);
    if(address >= 0x200000 && address <= 0x2fffff) return programWord(bankAddress(address));
    return data;
  }

  auto writeP(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    if(lower && address >= 0x2ffff0 && address <= 0x2fffff) {
      u32 size = prom.size() * 2;
      if(size <= 0x100000) return;
      romBank = data.bit(0, 2);
      if((romBank + 1) * 0x100000 >= size) romBank = 0;
    }
  }

  auto readM(n32 address) -> n8 override {
    return mrom[address];
  }

  auto readC(n32 address) -> n8 override {
    return crom[address];
  }

  auto readS(n32 address) -> n8 override {
    return srom[address];
  }

  auto readVA(n32 address) -> n8 override {
    return vromA[address];
  }

  auto readVB(n32 address) -> n8 override {
    return vromB ? vromB[address] : readVA(address);
  }

  auto power() -> void override {
    romBank = 0;
  }

  auto serialize(serializer& s) -> void override {
    s(romBank);
  }

  n8 romBank;
};

