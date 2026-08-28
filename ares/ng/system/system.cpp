#include <algorithm>
#include <android/log.h>
#include <cstdio>

namespace ares::NeoGeo {

auto enumerate() -> std::vector<string> {
  return {
    "[SNK] Neo Geo AES",
    "[SNK] Neo Geo MVS",
    "[SNK] Neo Geo CD",
  };
}

auto load(Node::System& node, string name) -> bool {
  auto list = enumerate();
  if(std::find(list.begin(), list.end(), name) == list.end()) return false;
  return system.load(node, name);
}

System system;
#include "debugger.cpp"
#include "serialization.cpp"

auto System::game() -> string {
  if(cartridge.node) {
    return cartridge.title();
  }

  return "(no cartridge connected)";
}

auto System::run() -> void {
  scheduler.enter();
}

auto System::load(Node::System& root, string name) -> bool {
  if(node) unload();

  information = {};
  if(name.find("Neo Geo AES")) {
    information.name = "Neo Geo AES";
    information.model = Model::NeoGeoAES;
  }
  if(name.find("Neo Geo MVS")) {
    information.name = "Neo Geo MVS";
    information.model = Model::NeoGeoMVS;
  }
  if(name.find("Neo Geo CD")) {
    information.name = "Neo Geo CD";
    information.model = Model::NeoGeoCD;
  }

  node = std::make_shared<Core::System>(information.name);
  node->setAttribute("configuration", name);
  node->setGame(std::bind_front(&System::game, this));
  node->setRun(std::bind_front(&System::run, this));
  node->setPower(std::bind_front(&System::power, this));
  node->setSave(std::bind_front(&System::save, this));
  node->setUnload(std::bind_front(&System::unload, this));
  node->setSerialize([this](bool save) -> serializer { return serialize(save); });
  node->setUnserialize(std::bind_front(&System::unserialize, this));
  root = node;
  if(!node->setPak(pak = platform->pak(node))) { node.reset(); return false; }

  // Neo Geo cannot boot without a BIOS. The VFS attaches it from neogeo.zip
  // (sp-e.sp1 / UniBIOS) or <home>/System/Neo Geo MVS/bios.rom. If neither is
  // present, system.bios stays unallocated and the 68K crashes (SIGSEGV, null
  // Memory::Readable) the moment it reads the reset vectors from 0xc00000.
  // Fail clearly here instead of booting into a crash.
  if(!pak->read("bios.rom")) {
    __android_log_print(ANDROID_LOG_ERROR, "NeoGeo", "load: MVS/AES BIOS required — provide neogeo.zip (sp-e.sp1/UniBIOS) or System/Neo Geo MVS/bios.rom");
    node.reset();
    return false;
  }

  if(NeoGeo::Model::NeoGeoCD()) {
    wram.allocate(2_MiB >> 1);
    spriteRam.allocate(4_MiB >> 1);
    pcmRam.allocate(1_MiB);
    fixRam.allocate(128_KiB);
  } else {
    wram.allocate(64_KiB >> 1);
    if(NeoGeo::Model::NeoGeoMVS()) {
      sram.allocate(64_KiB >> 1);
    }
  }

  scheduler.reset();
  cpu.load(node);
  apu.load(node);
  lspc.load(node);
  opnb.load(node);
  if(NeoGeo::Model::NeoGeoCD()) disc.load(node);
  if(!NeoGeo::Model::NeoGeoCD()) cartridgeSlot.load(node);
  controllerPort1.load(node);
  controllerPort2.load(node);
  if(!NeoGeo::Model::NeoGeoCD()) cardSlot.load(node);
  debugger.load(node);
  return true;
}

auto System::unload() -> void {
  if(!node) return;
  save();
  debugger.unload(node);
  cpu.unload();
  apu.unload();
  lspc.unload();
  opnb.unload();
  if(NeoGeo::Model::NeoGeoCD()) disc.unload();
  if(!NeoGeo::Model::NeoGeoCD()) cartridgeSlot.unload();
  controllerPort1.unload();
  controllerPort2.unload();
  if(!NeoGeo::Model::NeoGeoCD()) cardSlot.unload();
  wram.reset();
  sram.reset();
  spriteRam.reset();
  pcmRam.reset();
  fixRam.reset();
  pak.reset();
  node.reset();
}

auto System::save() -> void {
  if(!node) return;
  if(!NeoGeo::Model::NeoGeoCD()) {
    cartridge.save();
    cardSlot.save();
  }
}

auto System::power(bool reset) -> void {
  for(auto& setting : node->find<Node::Setting::Setting>()) setting->setLatch();

  if(NeoGeo::Model::NeoGeoCD()) {
    cdd.reset();
    cdc.reset();
  }

  if(auto fp = pak->read("bios.rom")) {
    bios.allocate(fp->size() >> 1);
    for(auto address : range(bios.size())) {
      // Neo Geo BIOS dumps are MAME 16-bit word-swapped ROMs (the 68K-visible
      // word is the little-endian pair of file bytes). Upstream reads them
      // big-endian (readm), which makes the 68K execute a byte-swapped BIOS.
      bios.program(address, fp->readl(2L));
    }
  }

  if(NeoGeo::Model::NeoGeoMVS()) {
    if(auto fp = pak->read("static.rom")) {
      srom.allocate(fp->size() >> 1);
      for(auto address : range(srom.size())) {
        srom.program(address, fp->readl(2L));
      }
    }
  }

  //LSPC vertical zoom table ("000-lo.lo" from the Neo Geo BIOS set). Without this the
  //sprite row/tile selector (vscale) stays 0xFF and every sprite collapses to tile 15/row 15.
  if(auto fp = pak->read("zoomy.rom")) {
    lspc.loadZoomy(fp->data(), fp->size());
  }

  if(cartridge.node) cartridge.power();
  cardSlot.power(reset);
  cpu.power(reset);
  apu.power(reset);
  lspc.power(reset);
  opnb.power(reset);
  scheduler.power(cpu);

  io = {};

  //Neo Geo CD: the real BIOS's controller-type probe ($C0930C) never runs in
  //this emulator's boot, so the four controller slot TYPE bytes stay 0 and the
  //BIOS's input handler ($C09584/$C095D4) masks ALL controller input to zero —
  //Start/D-pad never register in the CD Player menu, so the game can't boot.
  //The libretro neocd core handles the same pads with a simple always-connected
  //joystick model. Seed BOTH byte lanes of the controller-struct area
  //($7D90-$7DAF at a5=$108000 → $10FD90-$10FDAF) with the joystick type (3):
  //the 68k reads byte N from word N>>1's OTHER lane (even address → byte 1,
  //odd → byte 0), so writing only one lane left the type bytes at 0 and the
  //mask path still zeroed the input. The BIOS overwrites the held/edge/repeat
  //fields every frame; only the type bytes matter and now always read 3.
  if(NeoGeo::Model::NeoGeoCD()) {
    for(u32 addr = 0x10fd90; addr <= 0x10fdae; addr += 2) {
      system.wram[addr >> 1].byte(0) = 3;
      system.wram[addr >> 1].byte(1) = 3;
    }
  }
}

auto System::readC(n32 address) -> n8 {
  if(NeoGeo::Model::NeoGeoCD()) {
    //Sprite DRAM is a 16-bit word array; the 68K's byte stores land in the
    //high lane at even addresses and the low lane at odd addresses (see
    //CPU::write case 0), so byte @address lives in lane !(address&1). Reading
    //lane (address&1) returned every word's bytes swapped — scrambled sprite
    //pixels. (libretro neocd models the transfer area as a plain byte array
    //byte@A -> sprRam[A], which is exactly this lane mapping.)
    return spriteRam.read(address >> 1).byte(!(address & 1));
  }
  return cartridge.readC(address);
}

auto System::readS(n32 address) -> n8 {
  if(NeoGeo::Model::NeoGeoCD()) return fixRam.read(address);
  return cartridge.readS(address);
}

auto System::readVA(n32 address) -> n8 {
  if(NeoGeo::Model::NeoGeoCD()) return pcmRam.read(address);
  return cartridge.readVA(address);
}

auto System::readVB(n32 address) -> n8 {
  if(NeoGeo::Model::NeoGeoCD()) return 0xff;
  return cartridge.readVB(address);
}

//Diagnostic: dump the NGCD graphics memories as raw files under `dir` so the
//HUD "black rectangle" bug can be inspected offline (are the HUD fix/sprite
//tiles the untouched 0xFF fill, or a broken palette?).
auto System::dumpNgGfx(const string& dir) -> void {
  if(!NeoGeo::Model::NeoGeoCD()) return;
  auto dump = [&](const char* name, const void* data, u32 bytes) -> void {
    string path = {dir, "/", name};
    FILE* fp = fopen(path.data(), "wb");
    if(!fp) { __android_log_print(ANDROID_LOG_ERROR, "NeoGeo", "dumpNgGfx: cannot open %s", name); return; }
    fwrite(data, 1, bytes, fp);
    fclose(fp);
  };
  dump("ng_spr.raw", (const u8*)system.spriteRam.data(), 4_MiB);
  dump("ng_fix.raw", (const u8*)system.fixRam.data(),    128_KiB);
  dump("ng_vram.raw",(const u8*)lspc.vram.data(),        68_KiB);
  dump("ng_pram.raw",(const u8*)lspc.pram.data(),        16_KiB);
  __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "dumpNgGfx: wrote ng_gfx files to %s", dir.data());
}

};
