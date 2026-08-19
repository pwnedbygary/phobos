// Decryption algorithms heavily based on/borrowed MAME
// See https://github.com/mamedev/mame/tree/master/src/devices/bus/neogeo
// and the MAME section in LICENCE in the ares license text.

struct NeoGeo : Mame {
  auto name() -> string override { return "Neo Geo"; }
  auto extensions() -> std::vector<string> override { return {"ng"}; }
  auto read(string location, string match) -> std::vector<u8>;
  auto load(string location) -> LoadResult override;
  auto board() -> string;
  auto save(string location) -> bool override;
  auto analyze(std::vector<u8>& p, std::vector<u8>& m, std::vector<u8>& c, std::vector<u8>& s, std::vector<u8>& vA, std::vector<u8>& vB) -> string;
  auto decrypt(std::vector<u8>& p, std::vector<u8>& m, std::vector<u8>& c, std::vector<u8>& s, std::vector<u8>& vA, std::vector<u8>& vB) -> void;
  auto decryptCmc42(std::vector<u8>& c , std::vector<u8>& s, u8 key) -> void;
  auto decryptCmc50(std::vector<u8>& c , std::vector<u8>& s, std::vector<u8>& m, u8 key) -> void;
  auto decryptPvcP(std::vector<u8>& p, bool home) -> void;
  auto decryptPcm2(std::vector<u8>& vA, int value) -> void;
  auto decryptK2k2P(std::vector<u8>& p, int variant) -> void;
  auto loadCmcFixedRom(std::vector<u8>& c, std::vector<u8>& s) -> void;
  auto decryptCmcGraphics(std::vector<u8>& c, u8 key) -> void;
  auto decryptCmcGraphicsInternal(u8 *r0, u8 *r1, u8 c0,  u8 c1, u8 *table0hi,u8 *table0lo,
                                  u8 *table1, int base, int invert) -> void;
  auto decryptCmcMusic(std::vector<u8>& m) -> void;
  auto decryptCmcMusicDescramble(u32 addr, u16 key) -> u32;

  enum : int {
    K2K2_SEC_KOF2002 = 0,
    K2K2_SEC_MATRIM  = 1,
    K2K2_SEC_SAMSHO5 = 2,
    K2K2_SEC_SAMSH5SP= 3,
  };

  Markup::Node info;

  #include "neo-geo-crypt.hpp"
  struct CmcContext {
    u8 *type0_t03;
    u8 *type0_t12;
    u8 *type1_t03;
    u8 *type1_t12;
    u8 *address_8_15_xor1;
    u8 *address_8_15_xor2;
    u8 *address_16_23_xor1;
    u8 *address_16_23_xor2;
    u8 *address_0_7_xor;
  } cmc;
};

auto NeoGeo::read(string location, string match) -> std::vector<u8> {
  // we expect mame style .zip rom images
  if(!location.iendsWith(".zip")) {}

  if(info) {
    if(match == "program.rom")   return loadRoms(location, info, "maincpu");
    if(match == "character.rom") return loadRoms(location, info, "sprites");
    if(match == "static.rom")    return loadRoms(location, info, "fixed");
    if(match == "voice-a.rom")   return loadRoms(location, info, "ymsnd-adpcma");
    if(match == "voice-b.rom")   return loadRoms(location, info, "ymsnd-adpcmb");
    if(match == "music.rom") {
      // music rom can be plaintext (audiocpu) or encrypted (audiocrypt)
      // we must load both types
      auto music = loadRoms(location, info, "audiocpu");
      if(music.size() == 0) return loadRoms(location, info, "audiocrypt");
      return music;
    }
  }

  return {};
}

auto NeoGeo::load(string location) -> LoadResult {
  std::vector<u8> programROM;    //P ROM (68K CPU program)
  std::vector<u8> musicROM;      //M ROM (Z80 APU program)
  std::vector<u8> characterROM;  //C ROM (sprite and background character graphics)
  std::vector<u8> staticROM;     //S ROM (fix layer static graphics)
  std::vector<u8> voiceAROM;     //V ROM (ADPCM-A voice samples)
  std::vector<u8> voiceBROM;     //V ROM (ADPCM-B voice samples)

  auto foundDatabase = Medium::loadDatabase();
  if(!foundDatabase) return { databaseNotFound, "Neo Geo.bml" };
  this->info = BML::unserialize(manifestDatabaseArcade(Medium::name(location)));

  if(file::exists(location)) {
    programROM   = NeoGeo::read(location, "program.rom");
    musicROM     = NeoGeo::read(location, "music.rom");
    characterROM = NeoGeo::read(location, "character.rom");
    staticROM    = NeoGeo::read(location, "static.rom");
    voiceAROM    = NeoGeo::read(location, "voice-a.rom");
    voiceBROM    = NeoGeo::read(location, "voice-b.rom");
  }
  
  string invalidRomInfo = "Ensure your ROM is in a MAME-compatible .zip format.";

  if(programROM.empty()  ) return { invalidROM, invalidRomInfo };
  if(musicROM.empty()    ) return { invalidROM, invalidRomInfo };
  if(characterROM.empty()) return { invalidROM, invalidRomInfo };
  if(staticROM.empty()   ) return { invalidROM, invalidRomInfo };
  if(voiceAROM.empty()   ) return { invalidROM, invalidRomInfo };
  //voiceB is optional

  //many games have encrypted roms, so let's decrypt them here
  decrypt(programROM, musicROM, characterROM, staticROM, voiceAROM, voiceBROM);

  Hash::SHA256 hash;
  hash.input(programROM);
  hash.input(musicROM);
  hash.input(characterROM);
  hash.input(staticROM);
  hash.input(voiceAROM);
  hash.input(voiceBROM);
  auto sha256 = hash.digest();

  this->location = location;
  this->manifest = analyze(programROM, musicROM, characterROM, staticROM, voiceAROM, voiceBROM);
  auto document = BML::unserialize(manifest);
  if(!document) return couldNotParseManifest;

  pak = std::make_shared<vfs::directory>();
  pak->setAttribute("sha256",  sha256);
  pak->setAttribute("title",   document["game/title"].string());
  pak->setAttribute("board",   document["game/board"].string());
  pak->append("manifest.bml",  manifest);
  pak->append("program.rom",   programROM);
  pak->append("music.rom",     musicROM);
  pak->append("character.rom", characterROM);
  pak->append("static.rom",    staticROM);
  pak->append("voice-a.rom",   voiceAROM);
  pak->append("voice-b.rom",   voiceBROM);
  return successful;
}

auto NeoGeo::save(string location) -> bool {
  auto document = BML::unserialize(manifest);

  return true;
}

auto NeoGeo::board() -> string {
  if(info) {
    for (auto element: info["game"]) {
      if(element.name() == "feature" && element["name"].string() == "slot") return element["value"].string();
    }
  }

  return "rom";
}

auto NeoGeo::analyze(std::vector<u8>& p, std::vector<u8>& m, std::vector<u8>& c, std::vector<u8>& s, std::vector<u8>& vA, std::vector<u8>& vB) -> string {
  string manifest;

  manifest += "game\n";
  manifest +={"  name:   ", Medium::name(location), "\n"};
  manifest +={"  title:  ", (info ? info["game/title"].string() : Medium::name(location)), "\n"};
  manifest +={"  board:  ", board(), "\n"};
  manifest +={"    memory type=ROM size=0x", hex( p.size(), 8L), " content=Program\n"};
  manifest +={"    memory type=ROM size=0x", hex( m.size(), 8L), " content=Music\n"};
  manifest +={"    memory type=ROM size=0x", hex( c.size(), 8L), " content=Character\n"};
  manifest +={"    memory type=ROM size=0x", hex( s.size(), 8L), " content=Static\n"};
  manifest +={"    memory type=ROM size=0x", hex(vA.size(), 8L), " content=VoiceA\n"};
  manifest +={"    memory type=ROM size=0x", hex(vB.size(), 8L), " content=VoiceB\n"};
  return manifest;
}

auto NeoGeo::decrypt(std::vector<u8>& p, std::vector<u8>& m, std::vector<u8>& c, std::vector<u8>& s, std::vector<u8>& vA, std::vector<u8>& vB) -> void {
  //cmc42
  if(board() == "cmc42_bangbead") return decryptCmc42(c, s, 0xf8);
  if(board() == "cmc42_ganryu"  ) return decryptCmc42(c, s, 0x07);
  if(board() == "cmc42_kof99k"  ) return decryptCmc42(c, s, 0x00);
  if(board() == "cmc42_mslug3h" ) return decryptCmc42(c, s, 0xad);
  if(board() == "cmc42_nitd"    ) return decryptCmc42(c, s, 0xff);
  if(board() == "cmc42_preisle2") return decryptCmc42(c, s, 0x9f);
  if(board() == "cmc42_s1945p"  ) return decryptCmc42(c, s, 0x05);
  if(board() == "cmc42_sengoku3") return decryptCmc42(c, s, 0xfe);
  if(board() == "cmc42_zupapa"  ) return decryptCmc42(c, s, 0xbd);
  //cmc50
  if(board() == "cmc50_jockeygp") return decryptCmc50(c, s, m, 0xac);
  if(board() == "cmc50_kof2001" ) return decryptCmc50(c, s, m, 0x1e);
  if(board() == "cmc50_kof2000n") return decryptCmc50(c, s, m, 0x00);
  //pvc (KOF2003 / KOF2003h: PRO-BK "PVC" P ROM encryption + CMC50 graphics + PCM2 V ROM)
  if(board() == "pvc_kf2k3" ) { decryptPvcP(p, false); decryptPcm2(vA, 5); return decryptCmc50(c, s, m, 0x9d); }
  if(board() == "pvc_kf2k3h") { decryptPvcP(p, true ); decryptPcm2(vA, 5); return decryptCmc50(c, s, m, 0x9d); }
  //pvc (MSlug5 / SVC: PVC P ROM encryption + CMC50 graphics + PCM2 V ROM)
  if(board() == "pvc_mslug5" ) return decryptCmc50(c, s, m, 0x19);
  if(board() == "pvc_svc"    ) { decryptPcm2(vA, 2); return decryptCmc50(c, s, m, 0x57); }
  //kof2k2-type (KOF2002 / Matrim / SamSho5 / SamSho5SP: block-swapped P ROM + CMC50 graphics + PCM2 V ROM)
  if(board() == "k2k2_kof2k2" ) { decryptK2k2P(p, K2K2_SEC_KOF2002); decryptPcm2(vA, 0); return decryptCmc50(c, s, m, 0xec); }
  if(board() == "k2k2_matrim" ) { decryptK2k2P(p, K2K2_SEC_MATRIM ); decryptPcm2(vA, 1); return decryptCmc50(c, s, m, 0x6a); }
  if(board() == "k2k2_samsh5" ) { decryptK2k2P(p, K2K2_SEC_SAMSHO5 ); return decryptCmc50(c, s, m, 0x0f); }
  if(board() == "k2k2_sams5s" ) { decryptK2k2P(p, K2K2_SEC_SAMSH5SP); return decryptCmc50(c, s, m, 0x0d); }
  if(board() == "k2k2_kf2k2p") { decryptK2k2P(p, K2K2_SEC_KOF2002 ); decryptPcm2(vA, 0); return decryptCmc50(c, s, m, 0xec); }
  //pcm2-only boards (CMC50 graphics + PCM2 V ROM)
  if(board() == "pcm2_mslug4") { decryptPcm2(vA, 6); return decryptCmc50(c, s, m, 0x31); }
  if(board() == "pcm2_rotd"  ) { decryptPcm2(vA, 3); return decryptCmc50(c, s, m, 0x3f); }
  if(board() == "pcm2_pnyaa" ) { decryptPcm2(vA, 4); return decryptCmc50(c, s, m, 0x2e); }
}

auto NeoGeo::decryptPvcP(std::vector<u8>& p, bool home) -> void {
  // P ROM "PRO-BK" PVC decryption for KOF2003 (MAME prot_pvc.cpp kof2003_decrypt_68k /
  // kof2003h_decrypt_68k). Operates on the MAME-format 0x900000 region:
  // p1c @0x000000, p2c @0x400000, p3c @0x800000 (16-bit words byte-swapped).
  static const u8 xor1[0x20] = { 0x3b, 0x6a, 0xf7, 0xb7, 0xe8, 0xa9, 0x20, 0x99, 0x9f, 0x39, 0x34, 0x0c, 0xc3, 0x9a, 0xa5, 0xc8, 0xb8, 0x18, 0xce, 0x56, 0x94, 0x44, 0xe3, 0x7a, 0xf7, 0xdd, 0x42, 0xf0, 0x18, 0x60, 0x92, 0x9f };
  static const u8 xor2kof2003[0x20] = { 0x2f, 0x02, 0x60, 0xbb, 0x77, 0x01, 0x30, 0x08, 0xd8, 0x01, 0xa0, 0xdf, 0x37, 0x0a, 0xf0, 0x65, 0x28, 0x03, 0xd0, 0x23, 0xd3, 0x03, 0x70, 0x42, 0xbb, 0x06, 0xf0, 0x28, 0xba, 0x0f, 0xf0, 0x7a };
  static const u8 xor2kof2003h[0x20] = { 0x2b, 0x09, 0xd0, 0x7f, 0x51, 0x0b, 0x10, 0x4c, 0x5b, 0x07, 0x70, 0x9d, 0x3e, 0x0b, 0xb0, 0xb6, 0x54, 0x09, 0xe0, 0xcc, 0x3d, 0x0d, 0x80, 0x99, 0x87, 0x03, 0x90, 0x82, 0xfe, 0x04, 0x20, 0x18 };
  const u8* xor2 = home ? xor2kof2003h : xor2kof2003;
  const int rom_size = 0x900000;
  std::vector<u8> buf(rom_size);

  for (int i = 0; i < 0x100000; i++)
    p[0x800000 + i] ^= p[0x100002 | i];

  for (int i = 0; i < 0x100000; i++)
    p[i] ^= xor1[(i ^ 1) % 0x20];

  for (int i = 0x100000; i < 0x800000; i++)
    p[i] ^= xor2[(i ^ 1) % 0x20];

  for (int i = 0x100000; i < 0x800000; i += 4) {
    u16 rom16 = p[(i + 1) ^ 1] | p[(i + 2) ^ 1] << 8;
    if(!home) rom16 = BITSWAP16(rom16, 15, 14, 13, 12, 5, 4, 7, 6, 9, 8, 11, 10, 3, 2, 1, 0);
    else      rom16 = BITSWAP16(rom16, 15, 14, 13, 12, 10, 11, 8, 9, 6, 7, 4, 5, 3, 2, 1, 0);
    p[(i + 1) ^ 1] = rom16 & 0xff;
    p[(i + 2) ^ 1] = rom16 >> 8;
  }

  for (int i = 0; i < 0x0100000 / 0x10000; i++) {
    int ofst = (i & 0xf0) + BITSWAP8((i & 0x0f), 7, 6, 5, 4, (home ? 1 : 0), (home ? 0 : 1), 2, 3);
    memcpy(&buf[i * 0x10000], &p[ofst * 0x10000], 0x10000);
  }

  for (int i = 0x100000; i < 0x900000; i += 0x100) {
    int ofst = (i & 0xf000ff) + ((i & 0x000f00) ^ (home ? 0x00400 : 0x00800)) + (BITSWAP8(((i & 0x0ff000) >> 12), (home ? 6 : 4), (home ? 7 : 5), (home ? 4 : 6), (home ? 5 : 7), (home ? 0 : 1), (home ? 1 : 0), 3, 2) << 12);
    memcpy(&buf[i], &p[ofst], 0x100);
  }
  memcpy(&p[0x000000], &buf[0x000000], 0x100000);
  memcpy(&p[0x100000], &buf[0x800000], 0x100000);
  memcpy(&p[0x200000], &buf[0x100000], 0x700000);
}

auto NeoGeo::decryptPcm2(std::vector<u8>& vA, int value) -> void {
  // NEOPCM2 V ROM swap (MAME prot_pcm2.cpp swap())
  static const u32 addrs[7][2] = {
    {0x000000, 0xa5000},
    {0xffce20, 0x01000},
    {0xfe2cf6, 0x4e001},
    {0xffac28, 0xc2000},
    {0xfeb2c0, 0x0a000},
    {0xff14ea, 0xa7001},
    {0xffb440, 0x02000}};
  static const u8 xordata[7][8] = {
    {0xf9, 0xe0, 0x5d, 0xf3, 0xea, 0x92, 0xbe, 0xef},
    {0xc4, 0x83, 0xa8, 0x5f, 0x21, 0x27, 0x64, 0xaf},
    {0xc3, 0xfd, 0x81, 0xac, 0x6d, 0xe7, 0xbf, 0x9e},
    {0xc3, 0xfd, 0x81, 0xac, 0x6d, 0xe7, 0xbf, 0x9e},
    {0xcb, 0x29, 0x7d, 0x43, 0xd2, 0x3a, 0xc2, 0xb4},
    {0x4b, 0xa4, 0x63, 0x46, 0xf0, 0x91, 0xea, 0x62},
    {0x4b, 0xa4, 0x63, 0x46, 0xf0, 0x91, 0xea, 0x62}};
  std::vector<u8> buf(0x1000000);
  uint8_t* src = &vA[0];
  memcpy(&buf[0], src, 0x1000000);
  for (int i = 0; i < 0x1000000; i++) {
    int j = BITSWAP24(i, 23, 22, 21, 20, 19, 18, 17, 0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 16);
    j ^= addrs[value][1];
    int d = ((i + addrs[value][0]) & 0xffffff);
    src[j] = buf[d] ^ xordata[value][j & 0x7];
  }
}

auto NeoGeo::decryptK2k2P(std::vector<u8>& p, int variant) -> void {
  // P ROM block swap for kof2k2-type carts (MAME prot_kof2k2.cpp)
  // 0 = kof2002, 1 = matrim (8x0x80000 blocks at 0x100000), 2 = samsho5, 3 = samsh5sp (16x0x80000 at 0)
  static const int sec8[8]  = {0x100000, 0x280000, 0x300000, 0x180000, 0x000000, 0x380000, 0x200000, 0x080000};
  static const int sec16a[16] = {0x000000, 0x080000, 0x700000, 0x680000, 0x500000, 0x180000, 0x200000, 0x480000, 0x300000, 0x780000, 0x600000, 0x280000, 0x100000, 0x580000, 0x400000, 0x380000};
  static const int sec16b[16] = {0x000000, 0x080000, 0x500000, 0x480000, 0x600000, 0x580000, 0x700000, 0x280000, 0x100000, 0x680000, 0x400000, 0x780000, 0x200000, 0x380000, 0x300000, 0x180000};
  if(variant <= 1) {
    u8* src = &p[0x100000];
    std::vector<u8> dst(src, src + 0x400000);
    for(int i = 0; i < 8; i++) memcpy(src + i * 0x80000, &dst[sec8[i]], 0x80000);
  } else if(variant == 2) {
    u8* src = &p[0];
    std::vector<u8> dst(src, src + 0x800000);
    for(int i = 0; i < 16; i++) memcpy(src + i * 0x80000, &dst[sec16a[i]], 0x80000);
  } else if(variant == 3) {
    u8* src = &p[0];
    std::vector<u8> dst(src, src + 0x800000);
    for(int i = 0; i < 16; i++) memcpy(src + i * 0x80000, &dst[sec16b[i]], 0x80000);
  }
}

auto NeoGeo::decryptCmc42(std::vector<u8>& c, std::vector<u8>& s, u8 key) -> void {
  cmc.type0_t03          = kof99_type0_t03;
  cmc.type0_t12          = kof99_type0_t12;
  cmc.type1_t03          = kof99_type1_t03;
  cmc.type1_t12          = kof99_type1_t12;
  cmc.address_8_15_xor1  = kof99_address_8_15_xor1;
  cmc.address_8_15_xor2  = kof99_address_8_15_xor2;
  cmc.address_16_23_xor1 = kof99_address_16_23_xor1;
  cmc.address_16_23_xor2 = kof99_address_16_23_xor2;
  cmc.address_0_7_xor    = kof99_address_0_7_xor;

  decryptCmcGraphics(c, key);
  loadCmcFixedRom(c, s);
}

auto NeoGeo::decryptCmc50(std::vector<u8>& c, std::vector<u8>& s, std::vector<u8>& m, u8 key) -> void {
  cmc.type0_t03          = kof2000_type0_t03;
  cmc.type0_t12          = kof2000_type0_t12;
  cmc.type1_t03          = kof2000_type1_t03;
  cmc.type1_t12          = kof2000_type1_t12;
  cmc.address_8_15_xor1  = kof2000_address_8_15_xor1;
  cmc.address_8_15_xor2  = kof2000_address_8_15_xor2;
  cmc.address_16_23_xor1 = kof2000_address_16_23_xor1;
  cmc.address_16_23_xor2 = kof2000_address_16_23_xor2;
  cmc.address_0_7_xor    = kof2000_address_0_7_xor;

  decryptCmcGraphics(c, key);
  loadCmcFixedRom(c, s);
  decryptCmcMusic(m);
}

auto NeoGeo::loadCmcFixedRom(std::vector<u8>& c, std::vector<u8>& s) -> void {
  // SROM is stored after CROM
  for (int i = 0; i < (int)s.size(); i++) {
    s[i] = c[(c.size() - s.size()) + ((i & ~0x1f) + ((i & 7) << 2) + ((~i & 8) >> 2) + ((i & 0x10) >> 4))];
  }
}

auto NeoGeo::decryptCmcGraphics(std::vector<u8>& c, u8 key) -> void {
  std::vector<u8> buf;
  buf.resize(c.size());

  // Data xor
  for (auto rpos = 0; rpos < (int)(c.size() / 4); rpos++) {
    decryptCmcGraphicsInternal(&buf[4 * rpos + 0], &buf[4 * rpos + 3], c[4 * rpos+0], c[4 * rpos+3],
            cmc. type0_t03, cmc.type0_t12, cmc.type1_t03, rpos, (rpos >> 8) & 1);
    decryptCmcGraphicsInternal(&buf[4 * rpos + 1], &buf[4 * rpos + 2], c[4 * rpos+1], c[4 * rpos+2],
            cmc.type0_t12, cmc.type0_t03, cmc.type1_t12, rpos,
            ((rpos >> 16) ^ cmc.address_16_23_xor2[(rpos >> 8) & 0xff]) & 1);
  }

  // Address xor
  for(auto rpos = 0; rpos < (int)(c.size() / 4); rpos++) {
    auto baser = rpos;
    baser ^= key;

    baser ^= cmc.address_8_15_xor1[(baser >> 16) & 0xff] << 8;
    baser ^= cmc.address_8_15_xor2[baser & 0xff] << 8;
    baser ^= cmc.address_16_23_xor1[baser & 0xff] << 16;
    baser ^= cmc.address_16_23_xor2[(baser >> 8) & 0xff] << 16;
    baser ^= cmc.address_0_7_xor[(baser >> 8) & 0xff];

    if(c.size() == 0x3000000) { // special handling for preisle2
      if (rpos < 0x2000000 / 4) baser &= (0x2000000 /4 ) - 1;
      else baser = 0x2000000 / 4 + (baser & ((0x1000000 / 4) - 1));
    }
    else if (c.size() == 0x6000000) { // special handling for kf2k3pcb
      if (rpos < 0x4000000 / 4) baser &= (0x4000000 / 4) - 1;
      else baser = 0x4000000 / 4 + (baser & ((0x1000000 / 4) - 1));
    }
    else baser &= (c.size() / 4) -1; // Clamp to the real rom size

    c[4 * rpos + 0] = buf[4 * baser + 0];
    c[4 * rpos + 1] = buf[4 * baser + 1];
    c[4 * rpos + 2] = buf[4 * baser + 2];
    c[4 * rpos + 3] = buf[4 * baser + 3];
  }
}

auto NeoGeo::decryptCmcGraphicsInternal(u8 *r0, u8 *r1, u8 c0,  u8 c1, u8 *table0hi,u8 *table0lo,
                                        u8 *table1, int base, int invert) -> void {
  uint8_t tmp, xor0, xor1;

  tmp = table1[(base & 0xff) ^ cmc.address_0_7_xor[(base >> 8) & 0xff]];
  xor0 = (table0hi[(base >> 8) & 0xff] & 0xfe) | (tmp & 0x01);
  xor1 = (tmp & 0xfe) | (table0lo[(base >> 8) & 0xff] & 0x01);

  if (invert) {
    *r0 = c1 ^ xor0;
    *r1 = c0 ^ xor1;
    return;
  }

  *r0 = c0 ^ xor0;
  *r1 = c1 ^ xor1;
}

auto NeoGeo::decryptCmcMusic(std::vector<u8>& m) -> void {
  std::vector<u8> input = m;

  //checksum of the first 64k of ROM is used as a key
  u16 key = 0;
  for(auto i = 0; i < 0x10000; i++) key += input[i];
  for(auto i = 0; i < (int)input.size(); i++) m[i] = input[decryptCmcMusicDescramble(i, key)];
}

auto NeoGeo::decryptCmcMusicDescramble(u32 address, u16 key) -> u32 {
  const int p1[8][16] = {
    {15,14,10, 7, 1, 2, 3, 8, 0,12,11,13, 6, 9, 5, 4},
    { 7, 1, 8,11,15, 9, 2, 3, 5,13, 4,14,10, 0, 6,12},
    { 8, 6,14, 3,10, 7,15, 1, 4, 0, 2, 5,13,11,12, 9},
    { 2, 8,15, 9, 3, 4,11, 7,13, 6, 0,10, 1,12,14, 5},
    { 1,13, 6,15,14, 3, 8,10, 9, 4, 7,12, 5, 2, 0,11},
    {11,15, 3, 4, 7, 0, 9, 2, 6,14,12, 1, 8, 5,10,13},
    {10, 5,13, 8, 6,15, 1,14,11, 9, 3, 0,12, 7, 4, 2},
    { 9, 3, 7, 0, 2,12, 4,11,14,10, 5, 8,15,13, 1, 6},
  };

  int block = (address >> 16) & 7;
  int aux = address & 0xffff;

  aux ^= BITSWAP16(key,12,0,2,4,8,15,7,13,10,1,3,6,11,9,14,5);
  aux = BITSWAP16(aux,
                    p1[block][15], p1[block][14], p1[block][13], p1[block][12],
                    p1[block][11], p1[block][10], p1[block][ 9], p1[block][ 8],
                    p1[block][ 7], p1[block][ 6], p1[block][ 5], p1[block][ 4],
                    p1[block][ 3], p1[block][ 2], p1[block][ 1], p1[block][ 0]);

  aux ^= m1_address_0_7_xor[(aux >> 8) & 0xff];
  aux ^= m1_address_8_15_xor[aux & 0xff] << 8;
  aux = BITSWAP16(aux, 7,15,14,6,5,13,12,4,11,3,10,2,9,1,8,0);

  return (block << 16) | aux;
}
