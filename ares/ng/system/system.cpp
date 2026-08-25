#include <algorithm>
#include <android/log.h>

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
}

auto System::readC(n32 address) -> n8 {
  if(NeoGeo::Model::NeoGeoCD()) return spriteRam.read(address >> 1).byte(address & 1);
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

};
