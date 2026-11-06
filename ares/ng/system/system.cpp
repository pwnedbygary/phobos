#include <ng/ng.hpp>
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

auto RTC::main() -> void {
  tick();
  step(1);
}

auto RTC::step(u32 clocks) -> void {
  Thread::step(clocks);
  synchronize(cpu);
}

auto RTC::power() -> void {
  uPD4990A::power();
  Thread::create(32'768, std::bind_front(&RTC::main, this));
}

auto System::ArcadeControls::load(Node::Object parent) -> void {
  port = parent->append<Node::Object>("Arcade");
  device = port->append<Node::Object>("Controls");
  coin[0] = device->append<Node::Input::Button>("Player 1 Coin");
  coin[1] = device->append<Node::Input::Button>("Player 2 Coin");
  service = device->append<Node::Input::Button>("Service");
  test = device->append<Node::Input::Button>("Test");
}

auto System::ArcadeControls::unload() -> void {
  for(auto& button : coin) button.reset();
  service.reset();
  test.reset();
  device.reset();
  port.reset();
}

auto System::ArcadeControls::poll() -> void {
  for(auto& button : coin) platform->input(button);
  platform->input(service);
  platform->input(test);
}

auto System::game() -> string {
  if(cartridge.node) {
    return cartridge.title();
  }

  return "(no cartridge connected)";
}

auto System::run() -> void {
  scheduler.enter();
  while(watchdogReset) {
    watchdogReset = false;
    power(true);
    scheduler.enter();
  }
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
  if(!node->setPak(pak = platform->pak(node))) return false;

  wram.allocate(64_KiB >> 1);
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
  for(auto& byte : rtc.data) byte = 0;
  if(NeoGeo::Model::NeoGeoMVS()) {
    if(auto fp = pak->read("time.rtc")) {
      for(auto& byte : rtc.data) byte = fp->read();
    }
  }
  rtc.load();
  cpu.load(node);
  apu.load(node);
  lspc.load(node);
  opnb.load(node);
  if(NeoGeo::Model::NeoGeoCD()) disc.load(node);
  if(!NeoGeo::Model::NeoGeoCD()) cartridgeSlot.load(node);
  controllerPort1.load(node);
  controllerPort2.load(node);
  if(NeoGeo::Model::NeoGeoMVS()) arcadeControls.load(node);
  if(!NeoGeo::Model::NeoGeoCD()) cardSlot.load(node);
  debugger.load(node);
  return true;
}

auto System::unload() -> void {
  if(!node) return;
  save();
  dips.unload();
  debugger.unload(node);
  cpu.unload();
  apu.unload();
  lspc.unload();
  opnb.unload();
  if(NeoGeo::Model::NeoGeoCD()) disc.unload();
  if(!NeoGeo::Model::NeoGeoCD()) cartridgeSlot.unload();
  controllerPort1.unload();
  controllerPort2.unload();
  arcadeControls.unload();
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
  rtc.save();
  if(NeoGeo::Model::NeoGeoMVS()) {
    if(auto fp = pak->write("time.rtc")) {
      for(auto byte : rtc.data) fp->write(byte);
    }
  }
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
    __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "power: bios.rom %u bytes, model=%d", (unsigned)fp->size(), (int)information.model);
    bios.allocate(fp->size() >> 1);
    for(auto address : range(bios.size())) {
      // Neo Geo BIOS dumps are MAME 16-bit word-swapped ROMs: read the little-endian pair.
      bios.program(address, fp->readl(2L));
    }
  }

  __android_log_print(ANDROID_LOG_INFO, "NeoGeo", "power: bios words=%u cd=%d", (unsigned)bios.size(), (int)NeoGeo::Model::NeoGeoCD());
  //libretro neocd tells the CDZ's BIOS by these words at $C0006C; the front and top loaders have
  //single-speed drives
  if(NeoGeo::Model::NeoGeoCD()) {
    cdd.doubleSpeed = bios.size() > 0x37 && bios[0x36] == 0x00c0 && bios[0x37] == 0xa3e8;
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

  if(!reset) rtc.power();
  if(cartridge.node) cartridge.power();
  cardSlot.power(reset);
  cpu.power(reset);
  apu.power(reset);
  lspc.power(reset);
  opnb.power(reset);
  scheduler.power(cpu);

  io = {};

  //Neo Geo CD: the real BIOS's controller-type probe never runs, leaving the four controller slot
  //TYPE bytes 0, and the BIOS's input handler masks all controller input to zero (Start/D-pad never
  //register in the CD Player menu). Seed both byte lanes of the struct area with the joystick type (3).
  if(NeoGeo::Model::NeoGeoCD()) {
    for(u32 addr = 0x10fd90; addr <= 0x10fdae; addr += 2) {
      system.wram[addr >> 1].byte(0) = 3;
      system.wram[addr >> 1].byte(1) = 3;
    }
  }
}

auto System::readC(n32 address) -> n8 {
  if(NeoGeo::Model::NeoGeoCD()) {
    //Sprite DRAM is a 16-bit word array; the 68K's byte stores land in the high lane at even
    //addresses and the low lane at odd addresses, so byte @address lives in lane !(address&1).
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
  if(NeoGeo::Model::NeoGeoCD()) return pcmRam.read(address);
  return cartridge.readVB(address);
}

//Diagnostic: dump the NGCD graphics memories as raw files under `dir` (on-device HUD inspection).
auto System::dumpNgGfx(const string& dir) -> void {
  if(!NeoGeo::Model::NeoGeoCD()) return;
  auto dump = [&](const char* name, const void* data, u32 bytes) -> void {
    string path = {dir, "/", name};
    FILE* fp = fopen(path.data(), "wb");
    if(!fp) return;
    fwrite(data, 1, bytes, fp);
    fclose(fp);
  };
  dump("ng_spr.raw", (const u8*)system.spriteRam.data(), 4_MiB);
  dump("ng_fix.raw", (const u8*)system.fixRam.data(), 128_KiB);
  dump("ng_vram.raw", (const u8*)lspc.vram.data(), 68_KiB);
  dump("ng_pram.raw", (const u8*)lspc.pram.data(), 16_KiB);
  dump("ng_wram.raw", (const u8*)system.wram.data(), 2_MiB);
}

}
