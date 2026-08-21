struct SMA : Interface {
  using Interface::Interface;

  Memory::Readable<n16> prom;
  Memory::Readable<n8 > mrom;
  Memory::Readable<n8 > crom;
  Memory::Readable<n8 > srom;
  Memory::Readable<n8 > vromA;
  Memory::Readable<n8 > vromB;

  u16 bankSel = 0;
  u32 bankBase = 0x100000;
  u16 rng = 0x2345;

  enum class Type { KOF99, KOF2000, GAROU, GAROUH, MSLUG3, MSLUG3A, UNKNOWN } type = Type::UNKNOWN;

  auto load() -> void override {
    Interface::load(prom, "program.rom");
    Interface::load(mrom, "music.rom");
    Interface::load(crom, "character.rom");
    Interface::load(srom, "static.rom");
    Interface::load(vromA, "voice-a.rom");
    Interface::load(vromB, "voice-b.rom");

    // board name is stored as pak attribute "board" (MIA sets it from Database)
    auto board = pak->attribute("board");
    // slot strings from MAME slot.cpp: sma_kof99, sma_garou, sma_garouh, sma_mslug3, sma_mslug3a, sma_kof2k (kof2000)
    if(board == "sma_kof99") type = Type::KOF99;
    else if(board == "sma_kof2k") type = Type::KOF2000;
    else if(board == "sma_garou") type = Type::GAROU;
    else if(board == "sma_garouh") type = Type::GAROUH;
    else if(board == "sma_mslug3") type = Type::MSLUG3;
    else if(board == "sma_mslug3a") type = Type::MSLUG3A;
    else type = Type::UNKNOWN;
    __android_log_print(ANDROID_LOG_DEBUG, "PhobosSMA", "SMA load board=%s type=%d prom=%zu", (const char*)board, (int)type, prom.size());
  }

  auto unload() -> void override {
    prom.reset();
    mrom.reset();
    crom.reset();
    srom.reset();
    vromA.reset();
    vromB.reset();
  }

  // bitswap helpers (match MAME bitswap<6> etc)
  inline auto bitswap6(int v, int b5,int b4,int b3,int b2,int b1,int b0) -> int {
    return ((v>>b5&1)<<5)|((v>>b4&1)<<4)|((v>>b3&1)<<3)|((v>>b2&1)<<2)|((v>>b1&1)<<1)|((v>>b0&1)<<0);
  }

  inline auto kof99_bank_base(u16 sel) -> u32 {
    static const int bankoffset[64] = {
      0x000000, 0x100000, 0x200000, 0x300000,
      0x3cc000, 0x4cc000, 0x3f2000, 0x4f2000,
      0x407800, 0x507800, 0x40d000, 0x50d000,
      0x417800, 0x517800, 0x420800, 0x520800,
      0x424800, 0x524800, 0x429000, 0x529000,
      0x42e800, 0x52e800, 0x431800, 0x531800,
      0x54d000, 0x551000, 0x567000, 0x592800,
      0x588800, 0x581800, 0x599800, 0x594800,
      0x598000
    };
    int d = bitswap6(sel, 5,12,10,8,6,14);
    d &= 63;
    if(d >= (int)(sizeof(bankoffset)/sizeof(bankoffset[0]))) return 0x100000;
    return 0x100000 + bankoffset[d];
  }

  inline auto kof2000_bank_base(u16 sel) -> u32 {
    static const int bankoffset[64] = {
      0x000000, 0x100000, 0x200000, 0x300000,
      0x3f7800, 0x4f7800, 0x3ff800, 0x4ff800,
      0x407800, 0x507800, 0x40f800, 0x50f800,
      0x416800, 0x516800, 0x41d800, 0x51d800,
      0x424000, 0x524000, 0x523800, 0x623800,
      0x526000, 0x626000, 0x528000, 0x628000,
      0x52a000, 0x62a000, 0x52b800, 0x62b800,
      0x52d000, 0x62d000, 0x52e800, 0x62e800,
      0x618000, 0x619000, 0x61a000, 0x61a800
    };
    int d = bitswap6(sel, 5,10,3,7,14,15);
    d &= 63;
    if(d >= (int)(sizeof(bankoffset)/sizeof(bankoffset[0]))) return 0x100000;
    return 0x100000 + bankoffset[d];
  }

  inline auto garou_bank_base(u16 sel) -> u32 {
    static const int bankoffset[64] = {
      0x000000, 0x100000, 0x200000, 0x300000,
      0x280000, 0x380000, 0x2d0000, 0x3d0000,
      0x2f0000, 0x3f0000, 0x400000, 0x500000,
      0x420000, 0x520000, 0x440000, 0x540000,
      0x498000, 0x598000, 0x4a0000, 0x5a0000,
      0x4a8000, 0x5a8000, 0x4b0000, 0x5b0000,
      0x4b8000, 0x5b8000, 0x4c0000, 0x5c0000,
      0x4c8000, 0x5c8000, 0x4d0000, 0x5d0000,
      0x458000, 0x558000, 0x460000, 0x560000,
      0x468000, 0x568000, 0x470000, 0x570000,
      0x478000, 0x578000, 0x480000, 0x580000,
      0x488000, 0x588000, 0x490000, 0x590000,
      0x5d0000, 0x5d8000, 0x5e0000, 0x5e8000,
      0x5f0000, 0x5f8000, 0x600000
    };
    int d = bitswap6(sel, 12,14,6,7,9,5);
    d &= 63;
    if(d >= (int)(sizeof(bankoffset)/sizeof(bankoffset[0]))) return 0x100000;
    return 0x100000 + bankoffset[d];
  }

  inline auto garouh_bank_base(u16 sel) -> u32 {
    static const int bankoffset[64] = {
      0x000000, 0x100000, 0x200000, 0x300000,
      0x280000, 0x380000, 0x2d0000, 0x3d0000,
      0x2c8000, 0x3c8000, 0x400000, 0x500000,
      0x420000, 0x520000, 0x440000, 0x540000,
      0x598000, 0x698000, 0x5a0000, 0x6a0000,
      0x5a8000, 0x6a8000, 0x5b0000, 0x6b0000,
      0x5b8000, 0x6b8000, 0x5c0000, 0x6c0000,
      0x5c8000, 0x6c8000, 0x5d0000, 0x6d0000,
      0x458000, 0x558000, 0x460000, 0x560000,
      0x468000, 0x568000, 0x470000, 0x570000,
      0x478000, 0x578000, 0x480000, 0x580000,
      0x488000, 0x588000, 0x490000, 0x590000,
      0x5d8000, 0x6d8000, 0x5e0000, 0x6e0000,
      0x5e8000, 0x6e8000, 0x6e8000
    };
    int d = bitswap6(sel, 13,11,2,14,8,4);
    d &= 63;
    if(d >= (int)(sizeof(bankoffset)/sizeof(bankoffset[0]))) return 0x100000;
    return 0x100000 + bankoffset[d];
  }

  inline auto mslug3_bank_base(u16 sel) -> u32 {
    static const int bankoffset[64] = {
      0x000000, 0x020000, 0x040000, 0x060000,
      0x070000, 0x090000, 0x0b0000, 0x0d0000,
      0x0e0000, 0x0f0000, 0x120000, 0x130000,
      0x140000, 0x150000, 0x180000, 0x190000,
      0x1a0000, 0x1b0000, 0x1e0000, 0x1f0000,
      0x200000, 0x210000, 0x240000, 0x250000,
      0x260000, 0x270000, 0x2a0000, 0x2b0000,
      0x2c0000, 0x2d0000, 0x300000, 0x310000,
      0x320000, 0x330000, 0x360000, 0x370000,
      0x380000, 0x390000, 0x3c0000, 0x3d0000,
      0x400000, 0x410000, 0x440000, 0x450000,
      0x460000, 0x470000, 0x4a0000, 0x4b0000,
      0x4c0000
    };
    int d = bitswap6(sel, 9,3,6,15,12,14);
    d &= 63;
    if(d >= (int)(sizeof(bankoffset)/sizeof(bankoffset[0]))) return 0x100000;
    return 0x100000 + bankoffset[d];
  }

  inline auto mslug3a_bank_base(u16 sel) -> u32 {
    static const int bankoffset[64] = {
      0x000000, 0x030000, 0x040000, 0x070000,
      0x080000, 0x0a0000, 0x0c0000, 0x0e0000,
      0x0f0000, 0x100000, 0x130000, 0x140000,
      0x150000, 0x160000, 0x190000, 0x1a0000,
      0x1b0000, 0x1c0000, 0x1f0000, 0x200000,
      0x210000, 0x220000, 0x250000, 0x260000,
      0x270000, 0x280000, 0x2b0000, 0x2c0000,
      0x2d0000, 0x2e0000, 0x310000, 0x320000,
      0x330000, 0x340000, 0x370000, 0x380000,
      0x390000, 0x3a0000, 0x3d0000, 0x3e0000,
      0x400000, 0x410000, 0x440000, 0x450000,
      0x460000, 0x470000, 0x4a0000, 0x4b0000,
      0x4c0000
    };
    int d = bitswap6(sel, 11,12,6,1,3,15);
    d &= 63;
    if(d >= (int)(sizeof(bankoffset)/sizeof(bankoffset[0]))) return 0x100000;
    return 0x100000 + bankoffset[d];
  }

  inline auto updateBankBase(u16 sel) -> void {
    switch(type) {
      case Type::KOF99: bankBase = kof99_bank_base(sel); break;
      case Type::KOF2000: bankBase = kof2000_bank_base(sel); break;
      case Type::GAROU: bankBase = garou_bank_base(sel); break;
      case Type::GAROUH: bankBase = garouh_bank_base(sel); break;
      case Type::MSLUG3: bankBase = mslug3_bank_base(sel); break;
      case Type::MSLUG3A: bankBase = mslug3a_bank_base(sel); break;
      default: bankBase = 0x100000 + (bitswap6(sel,5,12,10,8,6,14) & 7)*0x100000; break; // fallback generic
    }
  }

  inline auto random_r() -> u16 {
    u16 old = rng;
    u16 newbit = ((rng >> 2) ^ (rng >> 3) ^ (rng >> 5) ^ (rng >> 6) ^ (rng >> 7) ^ (rng >> 11) ^ (rng >> 12) ^ (rng >> 15)) & 1;
    rng = (rng << 1) | newbit;
    return old;
  }

  inline auto isBankWrite(n24 addr) -> bool {
    // Per wiki: kof99 $2FFFF1, garou $2FFFC0, mslug3 $2FFFE5, kof2000 $2FFFED
    // Check exact word address (even) or byte odd variant
    switch(type) {
      case Type::KOF99:
        if(addr == 0x2FFFF1 || addr == 0x2FFFF0) return true;
        break;
      case Type::GAROU:
      case Type::GAROUH:
        if(addr == 0x2FFFC0 || addr == 0x2FFFC1) return true;
        break;
      case Type::MSLUG3:
      case Type::MSLUG3A:
        if(addr == 0x2FFFE5 || addr == 0x2FFFE4) return true;
        break;
      case Type::KOF2000:
        if(addr == 0x2FFFED || addr == 0x2FFFEC) return true;
        break;
      default:
        if(addr == 0x2FFFF1 || addr == 0x2FFFF0) return true;
        if(addr == 0x2FFFC0 || addr == 0x2FFFC1) return true;
        if(addr == 0x2FFFE5 || addr == 0x2FFFE4) return true;
        if(addr == 0x2FFFED || addr == 0x2FFFEC) return true;
        break;
    }
    return false;
  }

  inline auto isPresenceRead(n24 addr) -> bool {
    // $2FE447 presence (odd byte at 0x2FE447). Accept word at 0x2FE446.
    if(addr == 0x2FE447) return true;
    if(addr == 0x2FE446) return true;
    return false;
  }

  inline auto isPRNRead(n24 addr) -> bool {
    // Per wiki: KOF99/mslug3 PRN $2FFFF8/A; KOF2000 $2FFFD8/A; Garou $2FFFCC/F0
    switch(type) {
      case Type::KOF99:
      case Type::MSLUG3:
      case Type::MSLUG3A:
        if(addr == 0x2FFFF8 || addr == 0x2FFFF9 || addr == 0x2FFFFA || addr == 0x2FFFFB) return true;
        if((addr & ~1) == 0x2FFFF8) return true;
        if((addr & ~1) == 0x2FFFFA) return true;
        break;
      case Type::KOF2000:
        if(addr == 0x2FFFD8 || addr == 0x2FFFD9 || addr == 0x2FFFDA || addr == 0x2FFFDB) return true;
        if((addr & ~1) == 0x2FFFD8) return true;
        if((addr & ~1) == 0x2FFFDA) return true;
        break;
      case Type::GAROU:
      case Type::GAROUH:
        if(addr == 0x2FFFCC || addr == 0x2FFFCD || addr == 0x2FFFF0 || addr == 0x2FFFF1) return true;
        if((addr & ~1) == 0x2FFFCC) return true;
        if((addr & ~1) == 0x2FFFF0) return true;
        break;
      default:
        if((addr & ~1) == 0x2FFFF8) return true;
        if((addr & ~1) == 0x2FFFFA) return true;
        if((addr & ~1) == 0x2FFFD8) return true;
        if((addr & ~1) == 0x2FFFDA) return true;
        if((addr & ~1) == 0x2FFFCC) return true;
        break;
    }
    return false;
  }

  auto readP(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    // SMA protection: presence
    if(isPresenceRead(address)) {
      __android_log_print(ANDROID_LOG_DEBUG, "PhobosSMA", "SMA presence read $%06x -> 9A37", (int)address);
      return 0x9A37;
    }
    // PRN
    if(isPRNRead(address)) {
      auto v = random_r();
      __android_log_print(ANDROID_LOG_DEBUG, "PhobosSMA", "SMA PRN read $%06x -> %04x", (int)address, v);
      return v;
    }

    if(address <= 0x0fffff) return prom[address >> 1];
    if(address >= 0x200000 && address <= 0x2fffff) {
      // SMA banking: handle bank register alias inside window? The PRN/presence already handled above,
      // but bank write area inside window should also return open bus? For reads, return banked ROM.
      // Avoid recursion: if isBankWrite, reads should probably return open? But MAME returns banked ROM except for protection regs.
      // So for reads at bank write address, return prom via banking as well? However those addresses are I/O, not ROM. We'll return data (open) for those.
      if(isBankWrite(address)) return data; // don't confuse with ROM
      // Use computed bankBase
      u32 phys = bankBase + (address - 0x200000);
      // mask to prom size (power-of-2 mask not accurate for 9M but prom handles out-of-range via mask? We'll clamp)
      if((phys >> 1) < prom.size()) return prom[phys >> 1];
      return data;
    }
    return data;
  }

  auto writeP(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    if(address >= 0x2fe000 && address <= 0x2fffff) {
      __android_log_print(ANDROID_LOG_DEBUG, "PhobosSMA", "SMA write $%06x upper=%d lower=%d data=%04x type=%d", (int)address, (int)upper, (int)lower, (int)data, (int)type);
    }
    if(isBankWrite(address)) {
      // Data is 16-bit word; MAME expects lower bits scrambled. Accept full word.
      // For byte writes, extract relevant byte
      u16 sel = data;
      if(lower && !upper) sel = data.byte(0) | (data.byte(0) << 8);
      else if(upper && !lower) sel = data.byte(1) << 8 | data.byte(1);
      bankSel = sel;
      updateBankBase(bankSel);
      __android_log_print(ANDROID_LOG_DEBUG, "PhobosSMA", "SMA bank write $%06x data=%04x sel=%04x -> base=%06x type=%d", (int)address, (int)data, bankSel, bankBase, (int)type);
      return;
    }
    if(address >= 0x200000 && address <= 0x2fffff) {
      // __android_log_print(ANDROID_LOG_DEBUG, "PhobosSMA", "SMA generic write $%06x data=%04x", (int)address, (int)data);
    }
    // Also handle PRN reads that might be written? ignore.
    // Fallback: if generic write at any 0x200000-0x2FFFFF area incorrectly tries to bankswitch (some games write bank via low byte)
    // For robustness, also handle lower-byte generic bank writes at the exact bank addr (some cores use byte writes)
    // But SMA is word-oriented; we already handle word.
  }

  auto readM(n32 address) -> n8 override { return mrom[address]; }
  auto readC(n32 address) -> n8 override { return crom[address]; }
  auto cromMask() -> u32 override { return crom.mask(); }
  auto readS(n32 address) -> n8 override { return srom[address]; }
  auto readVA(n32 address) -> n8 override { return vromA[address]; }
  auto readVB(n32 address) -> n8 override { return vromB ? vromB[address] : vromA[address]; }

  auto power() -> void override {
    bankSel = 0;
    bankBase = 0x100000;
    rng = 0x2345;
  }

  auto serialize(serializer& s) -> void override {
    s(bankSel);
    s(bankBase);
    s(rng);
  }
};
