//Neo Geo CD upload/fetch layout regression checks (host build, no BIOS/disc).
//
//Drives the real ares::NeoGeo code (Dma::start, CPU::write transfer-area
//zones, System::readC, LSPC::render) with synthetic data and compares the
//result against the layouts implemented by three independent emulators:
//  Geolith   geo_cd.c (cd_dma_execute, transfer_write_*), geo_lspc.c (sprcalc, tpix)
//  NeoCD     memory.cpp (dmaOp*), memory_mapped.cpp (AREA_SPR), video.cpp (drawSprite)
//  MAME      snk/neogeocd.cpp (do_dma), snk/neogeo_spr.cpp (tile code/mask)
//Geolith and NeoCD store sprite DRAM as a plain byte array in 68K byte order;
//sprite expectations below use that view.

#include <ng/ng.hpp>
#include <cstdio>
#include <vector>

namespace ares { Platform* platform = nullptr; }

namespace ares::NeoGeo::LayoutTest {

struct TestPlatform : ares::Platform {
  std::vector<u8> bios = std::vector<u8>(512 * 1024, 0);
  auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override {
    auto dir = std::make_shared<vfs::directory>();
    if(node->name() == "Neo Geo CD") {
      dir->append("bios.rom", vfs::memory::open({bios.data(), bios.size()}));
    }
    return dir;
  }
};

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...) do { ++checks; if(!(cond)) { ++failures; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

//68K-visible byte at a sprite DRAM byte address (the renderer's view)
static auto sprByte(u32 address) -> u8 { return system.readC(address); }

static auto clearSprite() -> void {
  for(u32 i = 0; i < system.spriteRam.size(); i++) system.spriteRam[i] = 0;
}

static auto clearFix() -> void {
  for(u32 i = 0; i < system.fixRam.size(); i++) system.fixRam[i] = 0;
}

static auto setWramBytes(u32 address, const std::vector<u8>& bytes) -> void {
  for(u32 i = 0; i < bytes.size(); i += 2) {
    system.wram[(address + i) >> 1] = n16(bytes[i] << 8 | bytes[i + 1]);
  }
}

static auto armCdcBuffer(const std::vector<u8>& bytes, u16 dac) -> void {
  for(u32 i = 0; i < bytes.size(); i++) cdc.buffer[dac + i] = bytes[i];
  cdc.wreg[4] = dac & 0xff;  //DACL
  cdc.wreg[5] = dac >> 8;    //DACH
  cdc.wreg[1] |= 0x02;       //IFCTRL.DOUTEN
  cdc.wreg[6] = 0xff;        //DTTRG written
  u16 dbc = bytes.size() - 1;
  cdc.wreg[2] = dbc & 0xff;
  cdc.wreg[3] = dbc >> 8;
}

static auto runDma(u16 mode, u32 a1, u32 a2, u32 count) -> void {
  dma.mode = mode;
  dma.address1 = a1;
  dma.address2 = a2;
  dma.count = count;
  dma.start();
}

static auto pattern(u32 n, u32 seed) -> std::vector<u8> {
  std::vector<u8> v(n);
  u32 x = seed;
  for(auto& b : v) { x = x * 1103515245 + 12345; b = x >> 16; }
  return v;
}

//DMA 0xe2dd, RAM -> transfer area. Geolith/NeoCD: per source word D write
//BYTE_SWAP_16(D), then D. Byte-wide DRAM keeps the low byte of each word write
//-> source bytes in order (MAME agrees for byte-wide DRAM); word-wide SPR DRAM
//-> [s1, s0, s0, s1].
static auto testE2ddFix() -> void {
  std::printf("DMA 0xe2dd -> FIX (byte-wide DRAM)\n");
  clearFix();
  auto src = pattern(64, 1);
  setWramBytes(0x100000, src);
  system.io.uploadZone = 5;
  runDma(0xe2dd, 0x100000, 0xe00000, src.size() / 2);
  for(u32 i = 0; i < src.size(); i++) {
    CHECK(system.fixRam[i] == src[i], "fix[%u] = %02x, reference %02x", i, (u32)system.fixRam[i], src[i]);
  }
}

static auto testE2ddSpr() -> void {
  std::printf("DMA 0xe2dd -> SPR (word-wide DRAM)\n");
  clearSprite();
  auto src = pattern(64, 2);
  setWramBytes(0x100000, src);
  system.io.uploadZone = 0;
  system.io.spriteUploadBank = 0;
  runDma(0xe2dd, 0x100000, 0xe00000, src.size() / 2);
  for(u32 i = 0; i < src.size() / 2; i++) {
    u8 s0 = src[i * 2 + 0], s1 = src[i * 2 + 1];
    u8 ref[4] = {s1, s0, s0, s1};
    for(u32 k = 0; k < 4; k++) {
      CHECK(sprByte(i * 4 + k) == ref[k], "spr[%u] = %02x, reference %02x", i * 4 + k, (u32)sprByte(i * 4 + k), ref[k]);
    }
  }
}

//PCM zone: index (offset >> 1) + bank * 0x80000; Z80 zone: index offset >> 1
//(Geolith transfer_write_16 / NeoCD memory_mapped.cpp).
static auto testE2ddPcmZ80() -> void {
  std::printf("DMA 0xe2dd -> PCM bank 1 and Z80 (byte-wide DRAM)\n");
  auto src = pattern(64, 6);
  setWramBytes(0x100000, src);

  for(u32 i = 0; i < src.size(); i++) system.pcmRam[0x80000 + i] = 0;
  system.io.uploadZone = 1;
  system.io.pcmUploadBank = 1;
  runDma(0xe2dd, 0x100000, 0xe00000, src.size() / 2);
  for(u32 i = 0; i < src.size(); i++) {
    CHECK(system.pcmRam[0x80000 + i] == src[i], "pcm[%05x] = %02x, reference %02x", 0x80000 + i, (u32)system.pcmRam[0x80000 + i], src[i]);
  }

  for(u32 i = 0; i < src.size(); i++) apu.ram[i] = 0;
  system.io.uploadZone = 4;
  runDma(0xe2dd, 0x100000, 0xe00000, src.size() / 2);
  for(u32 i = 0; i < src.size(); i++) {
    CHECK(apu.ram[i] == src[i], "z80[%u] = %02x, reference %02x", i, (u32)apu.ram[i], src[i]);
  }
}

//DMA 0xfc2d, LC8951 buffer -> transfer area. Per buffer word D write D >> 8,
//then D: byte-wide DRAM -> [d0, d1]; SPR DRAM -> [00, d0, d0, d1].
static auto testFc2d() -> void {
  std::printf("DMA 0xfc2d -> FIX and SPR\n");
  auto src = pattern(64, 3);

  clearFix();
  armCdcBuffer(src, 0x1234);
  system.io.uploadZone = 5;
  runDma(0xfc2d, 0xe00000, 0, src.size() / 2);
  for(u32 i = 0; i < src.size(); i++) {
    CHECK(system.fixRam[i] == src[i], "fix[%u] = %02x, reference %02x", i, (u32)system.fixRam[i], src[i]);
  }

  clearSprite();
  armCdcBuffer(src, 0x1234);
  system.io.uploadZone = 0;
  system.io.spriteUploadBank = 0;
  runDma(0xfc2d, 0xe00000, 0, src.size() / 2);
  for(u32 i = 0; i < src.size() / 2; i++) {
    u8 d0 = src[i * 2 + 0], d1 = src[i * 2 + 1];
    u8 ref[4] = {0x00, d0, d0, d1};
    for(u32 k = 0; k < 4; k++) {
      CHECK(sprByte(i * 4 + k) == ref[k], "spr[%u] = %02x, reference %02x", i * 4 + k, (u32)sprByte(i * 4 + k), ref[k]);
    }
  }
}

//DMA 0xffc5, LC8951 buffer -> transfer area: SPR bytes equal buffer bytes.
static auto testFfc5() -> void {
  std::printf("DMA 0xffc5 -> SPR bank 2\n");
  clearSprite();
  auto src = pattern(2048, 4);
  armCdcBuffer(src, 0x7a04);
  system.io.uploadZone = 0;
  system.io.spriteUploadBank = 2;
  runDma(0xffc5, 0xe00000 + 0x40000, 0, src.size() / 2);
  u32 base = 2 * 0x100000 + 0x40000;
  for(u32 i = 0; i < src.size(); i++) {
    CHECK(sprByte(base + i) == src[i], "spr[%06x] = %02x, reference %02x", base + i, (u32)sprByte(base + i), src[i]);
  }
}

//Reference decode of one sprite pixel on CD: planes 0..3 from row bytes
//[+1, +0, +3, +2], left half (pixels 0..7) at +64, bit 0 = leftmost pixel.
static auto refPixel(u32 tile, u32 row, u32 x) -> u32 {
  u32 base = tile * 128 + (x < 8 ? 64 : 0) + row * 4;
  u32 bit = x & 7;
  u32 v0 = sprByte(base + 1) >> bit & 1;
  u32 v1 = sprByte(base + 0) >> bit & 1;
  u32 v2 = sprByte(base + 3) >> bit & 1;
  u32 v3 = sprByte(base + 2) >> bit & 1;
  return v0 | v1 << 1 | v2 << 2 | v3 << 3;
}

static constexpr u32 SpriteX = 64;
static constexpr u32 SpriteLine = 32;  //screen line of sprite row 0

static auto setupSprite(u16 tileWord, u16 attrWord) -> void {
  for(u32 i = 0; i < lspc.vram.size(); i++) lspc.vram[i] = 0;
  u32 sprite = 1;
  u32 sy = 512 - SpriteLine;
  lspc.vram[0x8000 | sprite] = 0x0fff;         //hshrink 15, vshrink 0xff
  lspc.vram[0x8200 | sprite] = (sy << 7) | 1;  //y position, 1 tile tall
  lspc.vram[0x8400 | sprite] = SpriteX << 7;   //x position
  lspc.vram[sprite << 6 | 0] = tileWord;
  lspc.vram[sprite << 6 | 1] = attrWord;
}

static auto preparePalette(u32 palette) -> void {
  lspc.io.pramBank = 0;
  lspc.pram[0xfff] = 0x0000;
  for(u32 c = 1; c < 16; c++) lspc.pram[palette << 4 | c] = 0x1000 + palette * 0x10 + c;
}

static auto renderedIndex(u32 row, u32 x, u32 palette) -> s32 {
  auto pixels = lspc.screen->pixels();
  u32 value = pixels[(SpriteLine + row) * 320 + SpriteX + x] & 0xffff;
  for(u32 c = 1; c < 16; c++) {
    if(value == (u32)lspc.pram[palette << 4 | c]) return c;
  }
  return 0;
}

//render the sprite through LSPC::render and compare all 16x16 pixels with the
//reference decode of refTile
static auto compareSprite(const char* label, u16 tileWord, u16 attrWord, u32 refTile) -> void {
  clearFix();  //the FIX layer is drawn over sprites; keep FIX tile 0 transparent
  u32 palette = attrWord >> 8;
  bool hflip = attrWord & 1;
  bool vflip = attrWord & 2;
  preparePalette(palette);
  setupSprite(tileWord, attrWord);
  u32 mismatches = 0;
  for(u32 row = 0; row < 16; row++) {
    lspc.render(SpriteLine + row);
    for(u32 x = 0; x < 16; x++) {
      s32 got = renderedIndex(row, x, palette);
      s32 ref = refPixel(refTile, vflip ? 15 - row : row, hflip ? 15 - x : x);
      if(got != ref) mismatches++;
    }
  }
  CHECK(mismatches == 0, "%s: tile word %04x attr %04x -> %u/256 pixels differ from reference tile %04x",
    label, tileWord, attrWord, mismatches, refTile);
}

//CD tile index = even word & 0x7fff (Geolith crommask, NeoCD tileIndex & 0x7FFF,
//MAME 4MiB sprite region mask); these cases leave the odd word's MSB field at 0.
static auto testSpriteFetch() -> void {
  std::printf("Sprite fetch: tile number + plane order vs reference decode\n");
  clearSprite();
  system.io.uploadZone = 0;
  auto fillTile = [&](u32 tile, u32 seed) {
    auto data = pattern(128, seed);
    system.io.spriteUploadBank = tile >> 13;
    for(u32 i = 0; i < 128; i += 2) {
      u32 address = 0xe00000 | ((tile * 128 + i) & 0xfffff);
      cpu.write(1, 1, address, n16(data[i] << 8 | data[i + 1]));
    }
  };
  u32 tiles[] = {0x0123, 0x2123, 0x4123, 0x7123};
  for(u32 t : tiles) fillTile(t, t * 7 + 11);

  compareSprite("bank 0",           0x0123, 0x0100, 0x0123);
  compareSprite("bank 3",           0x7123, 0x0200, 0x7123);
  compareSprite("even word bit 15", 0x8123, 0x0100, 0x0123);
  compareSprite("hflip",            0x2123, 0x0301, 0x2123);
  compareSprite("vflip",            0x4123, 0x0302, 0x4123);
}

//FIX glyphs uploaded from work RAM with 0xe2dd and drawn by LSPC::render must
//decode like the source bytes: FIX byte ((x << 2 & 24) ^ 16) | row holds pixel
//x in the low nibble and pixel x+1 in the high nibble (S ROM layout; Geolith
//geo_lspc_fixline_default, NeoCD Video::drawFix).
static auto testFixText() -> void {
  std::printf("Chain: work RAM -> 0xe2dd -> FIX -> LSPC::render\n");
  for(u32 i = 0; i < lspc.vram.size(); i++) lspc.vram[i] = 0;
  clearFix();
  auto glyphs = pattern(4 * 32, 5);  //FIX tiles 0x41..0x44
  setWramBytes(0x100000, glyphs);
  system.io.uploadZone = 5;
  runDma(0xe2dd, 0x100000, 0xe00000 + 0x41 * 64, glyphs.size() / 2);
  u32 palette = 5;
  preparePalette(palette);
  u32 col0 = 10, row = 12;
  for(u32 i = 0; i < 4; i++) lspc.vram[0x7000 + (col0 + i) * 0x20 + row] = palette << 12 | (0x41 + i);
  u32 mismatches = 0;
  for(u32 y = 0; y < 8; y++) {
    lspc.render(row * 8 + y);
    auto pixels = lspc.screen->pixels();
    for(u32 i = 0; i < 4; i++) {
      for(u32 x = 0; x < 8; x++) {
        u8 byte = glyphs[i * 32 + (((x << 2 & 24) ^ 16) | y)];
        s32 ref = x & 1 ? byte >> 4 : byte & 15;
        u32 value = pixels[(row * 8 + y) * 320 + (col0 + i) * 8 + x] & 0xffff;
        s32 got = 0;
        for(u32 c = 1; c < 16; c++) if(value == (u32)lspc.pram[palette << 4 | c]) got = c;
        if(got != ref) mismatches++;
      }
    }
  }
  CHECK(mismatches == 0, "FIX glyphs: %u/256 pixels differ from the source bytes", mismatches);
}

//Full chain: CD buffer bytes -> 0xffc5 -> SPR -> LSPC::render must equal the
//reference decode of the buffer bytes themselves.
static auto testDiscToScreen() -> void {
  std::printf("Chain: CDC buffer -> 0xffc5 -> LSPC::render\n");
  clearSprite();
  auto sector = pattern(2048, 99);  //16 tiles
  armCdcBuffer(sector, 0x0934);
  system.io.uploadZone = 0;
  system.io.spriteUploadBank = 1;
  runDma(0xffc5, 0xe00000 + 0x20 * 128, 0, sector.size() / 2);  //bank 1, tile 0x2020
  u32 tile = 0x2000 + 0x20 + 5;
  for(u32 row = 0; row < 16; row++) {
    for(u32 x = 0; x < 16; x++) {
      u32 base = 5 * 128 + (x < 8 ? 64 : 0) + row * 4;
      u32 bit = x & 7;
      u32 ref = (sector[base + 1] >> bit & 1) | (sector[base + 0] >> bit & 1) << 1
              | (sector[base + 3] >> bit & 1) << 2 | (sector[base + 2] >> bit & 1) << 3;
      u32 fetched = refPixel(tile, row, x);
      CHECK(fetched == ref, "tile %04x row %u x %u: memory decode %u, sector decode %u", tile, row, x, fetched, ref);
    }
  }
  compareSprite("disc tile", tile, 0x0400, tile);
}

//Save states: every piece of CD state the core keeps comes back after a save and a load.
struct CdSnapshot {
  std::vector<u16> sprite;
  std::vector<u8> pcm, fix, z80, buffer;
  std::vector<u64> drive, decoder, transfer, io;
  std::vector<f64> audio;
};

static auto snapshotCd() -> CdSnapshot {
  CdSnapshot s;
  for(u32 i = 0; i < system.spriteRam.size(); i++) s.sprite.push_back(system.spriteRam[i]);
  for(u32 i = 0; i < system.pcmRam.size(); i++) s.pcm.push_back(system.pcmRam[i]);
  for(u32 i = 0; i < system.fixRam.size(); i++) s.fix.push_back(system.fixRam[i]);
  for(u32 i = 0; i < apu.ram.size(); i++) s.z80.push_back(apu.ram[i]);
  for(u32 i = 0; i < Cdc::BufferSize; i++) s.buffer.push_back(cdc.buffer[i]);
  for(u32 i = 0; i < 10; i++) s.drive.push_back(cdd.rx[i] | cdd.tx[i] << 8);
  s.drive.insert(s.drive.end(), {
    cdd.wordCount, cdd.clock, cdd.statusHack, cdd.status, cdd.curStatus, cdd.settleCounter,
    cdd.min, cdd.sec, cdd.frame, cdd.ext, cdd.control, cdd.statusCdc, (u64)(u32)cdd.curLba,
    cdd.curTrack, cdd.reg2, cdd.latch16, cdd.type1Pending, cdd.type2Pending, cdd.type3Pending,
    cdd.type1Ack, cdd.type2Ack, cdd.type3Ack, cdd.prohibitIrq,
  });
  for(u32 i = 0; i < 16; i++) s.decoder.push_back(cdc.wreg[i] | cdc.rreg[i] << 8);
  s.decoder.insert(s.decoder.end(), {cdc.reg0, cdc.reg1, cdc.decode});
  s.transfer = {dma.address1, dma.address2, dma.value1, dma.value2, dma.count, dma.mode};
  s.io = {
    system.io.irqMask2, system.io.ctrlSelector, system.io.uploadZone, system.io.spriteUploadBank,
    system.io.pcmUploadBank, lspc.io.cddCounter, apu.held,
  };
  s.audio = {cdd.fade, cdd.lastLeft, cdd.lastRight};
  return s;
}

static auto fillCd(u32 seed) -> void {
  auto p = pattern(0x10000, seed);
  auto word = [&](u32 i) -> u32 { return p[i & 0xffff] | p[(i + 1) & 0xffff] << 8; };
  for(u32 i = 0; i < system.spriteRam.size(); i++) system.spriteRam[i] = word(i * 3);
  for(u32 i = 0; i < system.pcmRam.size(); i++) system.pcmRam[i] = p[(i * 7) & 0xffff];
  for(u32 i = 0; i < system.fixRam.size(); i++) system.fixRam[i] = p[(i * 5) & 0xffff];
  for(u32 i = 0; i < apu.ram.size(); i++) apu.ram[i] = p[i & 0xffff] ^ 0xa5;
  for(u32 i = 0; i < Cdc::BufferSize; i++) cdc.buffer[i] = p[(i * 11) & 0xffff];
  for(u32 i = 0; i < 10; i++) { cdd.rx[i] = p[400 + i] & 15; cdd.tx[i] = p[410 + i] & 15; }
  cdd.wordCount = p[420] % 10;
  cdd.clock = p[421];
  cdd.statusHack = p[422] & 15;
  cdd.status = word(423);
  cdd.curStatus = word(425);
  cdd.settleCounter = word(427);
  cdd.min = word(429);
  cdd.sec = word(431);
  cdd.frame = word(433);
  cdd.ext = word(435);
  cdd.control = word(437);
  cdd.statusCdc = word(439);
  cdd.curLba = (s32)(word(441) << 16 | word(443)) >> 8;
  cdd.curTrack = p[445];
  cdd.reg2 = word(446);
  cdd.latch16 = word(448);
  cdd.type1Pending = p[450];
  cdd.type2Pending = p[451];
  cdd.type3Pending = p[452];
  cdd.type1Ack = p[453];
  cdd.type2Ack = p[454];
  cdd.type3Ack = p[455];
  cdd.prohibitIrq = p[456];
  cdd.fade = p[457] / 255.0;
  cdd.lastLeft = (p[458] - 128) / 128.0;
  cdd.lastRight = (p[459] - 128) / 128.0;
  for(u32 i = 0; i < 16; i++) { cdc.wreg[i] = p[500 + i]; cdc.rreg[i] = p[520 + i]; }
  cdc.reg0 = word(540);
  cdc.reg1 = word(542);
  cdc.decode = word(544);
  dma.address1 = word(600) << 16 | word(602);
  dma.address2 = word(604) << 16 | word(606);
  dma.value1 = word(608);
  dma.value2 = word(610);
  dma.count = word(612) << 16 | word(614);
  dma.mode = word(616);
  system.io.irqMask2 = word(700);
  system.io.ctrlSelector = p[702];
  system.io.uploadZone = p[703];
  system.io.spriteUploadBank = p[704];
  system.io.pcmUploadBank = p[705];
  lspc.io.cddCounter = word(706) % (6'000'000 / 75);
  apu.held = p[708];
}

static auto testSerializeRoundTrip() -> void {
  std::printf("Save state round trip: CD memories, drive, decoder and DMA\n");
  fillCd(1);
  auto saved = snapshotCd();
  auto state = system.serialize(false);
  fillCd(2);
  CHECK(snapshotCd().sprite != saved.sprite, "the second fill should differ from the first");
  serializer reader{state.data(), state.size()};
  CHECK(system.unserialize(reader), "unserialize refused the state");
  auto loaded = snapshotCd();
  CHECK(loaded.sprite == saved.sprite, "sprite RAM differs after the round trip");
  CHECK(loaded.pcm == saved.pcm, "PCM RAM differs after the round trip");
  CHECK(loaded.fix == saved.fix, "FIX RAM differs after the round trip");
  CHECK(loaded.z80 == saved.z80, "Z80 RAM differs after the round trip");
  CHECK(loaded.buffer == saved.buffer, "decoder buffer differs after the round trip");
  CHECK(loaded.drive == saved.drive, "drive state differs after the round trip");
  CHECK(loaded.decoder == saved.decoder, "decoder registers differ after the round trip");
  CHECK(loaded.transfer == saved.transfer, "DMA registers differ after the round trip");
  CHECK(loaded.io == saved.io, "CD I/O registers differ after the round trip");
  CHECK(loaded.audio == saved.audio, "CD audio fade state differs after the round trip");
}

static auto run() -> int {
  TestPlatform testPlatform;
  ares::platform = &testPlatform;

  Node::System root;
  if(!NeoGeo::load(root, "[SNK] Neo Geo CD")) { std::printf("load failed\n"); return 2; }
  system.power(false);

  //full-size vertical zoom (vshrink 0xff): line N -> tile N >> 4, row N & 15
  for(u32 line = 0; line < 256; line++) lspc.vscale[0xff][line] = line;

  testE2ddFix();
  testE2ddSpr();
  testE2ddPcmZ80();
  testFc2d();
  testFfc5();
  testSpriteFetch();
  testFixText();
  testDiscToScreen();
  testSerializeRoundTrip();

  std::printf("\n%d checks, %d failures\n", checks, failures);
  root.reset();
  return failures ? 1 : 0;
}

}

int main() {
  return ares::NeoGeo::LayoutTest::run();
}
