//What tests/sgb-scheduler's two programs share: a platform that serves the paks the tests build and passes the
//schedulers' events on, a stall watchdog, a Game Boy boot stub and a synthetic Game Boy cartridge (no Nintendo code or
//data: the logo isn't needed, since the stub hands over without checking the header), and a hash of the Game Boy's
//state. A stall is a scheduler that gives no frame back: the program can't get control back to report it, so another
//thread ends it.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace ares {
  //what the runner (android/app/src/main/cpp/PhobosRunner.cpp) gives the cores, without the runner: each node's pak by
  //the node's name, from `directories` (as mia made it) or else from `paks`, and the schedulers' events to `onEvent`
  struct TestPlatform : Platform {
    struct Pak {
      std::map<std::string, std::vector<u8>> files;
      std::map<std::string, std::string> attributes;
    };
    std::map<std::string, Pak> paks;
    std::map<std::string, std::shared_ptr<vfs::directory>> directories;
    std::function<void (Event)> onEvent;

    auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override {
      if(auto direct = directories.find((const char*)node->name()); direct != directories.end()) return direct->second;
      auto found = paks.find((const char*)node->name());
      if(found == paks.end()) return {};
      auto pak = std::make_shared<vfs::directory>();
      for(auto& [name, value] : found->second.attributes) pak->setAttribute(name.c_str(), value.c_str());
      for(auto& [name, data] : found->second.files) {
        pak->append(name.c_str(), vfs::memory::open(std::span<const u8>{data.data(), data.size()}));
      }
      return pak;
    }

    auto event(Event event) -> void override {
      if(onEvent) onEvent(event);
    }
  };
  static TestPlatform testPlatform;
  Platform* platform = &testPlatform;
}

namespace test {

inline u32 failures = 0;
inline std::atomic<u64> progress{0};       //each frame or pass that comes back
inline std::atomic<const char*> current{"(starting)"};

inline auto check(bool ok, const std::string& what) -> bool {
  printf("  %s: %s\n", ok ? "ok" : "FAILED", what.c_str());
  if(!ok) failures++;
  return ok;
}

//ends the program, failed, when nothing comes back for `seconds`
inline auto watchdog(std::string program, u32 seconds) -> void {
  std::thread([program, seconds] {
    u64 last = ~0ull;
    u32 quiet = 0;
    while(true) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      u64 now = progress.load();
      quiet = now == last ? quiet + 1 : 0;
      last = now;
      if(quiet == seconds * 10) {
        printf("  FAILED: %s: stalled, no frame or pass came back for %u s\n", current.load(), seconds);
        printf("%s: stalled\n", program.c_str());
        fflush(stdout);
        std::_Exit(1);
      }
    }
  }).detach();
}

inline auto readFile(const std::string& path) -> std::vector<u8> {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

inline auto writeFile(const std::string& path, const u8* data, u32 size) -> bool {
  std::ofstream file(path, std::ios::binary);
  file.write((const char*)data, size);
  return (bool)file;
}

//a 256-byte boot ROM in the Game Boy's (and the Super Game Boy's) place: it sets the stack and hands over to the
//cartridge at $0100, its last instruction the write to $ff50 at $00fe, as every boot ROM's is
inline auto bootStub() -> std::vector<u8> {
  std::vector<u8> rom(0x100, 0x00);
  const u8 start[] = {0x31, 0xfe, 0xff, 0xc3, 0xfc, 0x00};  //ld sp,$fffe ; jp $00fc
  memcpy(&rom[0x00], start, sizeof start);
  const u8 end[] = {0x3e, 0x01, 0xe0, 0x50};  //ld a,$01 ; ldh ($50),a
  memcpy(&rom[0xfc], end, sizeof end);
  return rom;
}

//a 32 KiB cartridge (no mapper) whose code, from $0150, fills tile 0 with rows of $f0 (low plane) and $0f (high plane),
//so its left half is colour 1 and its right half colour 2, puts it all over the background, turns the LCD on, and then
//each frame counts frames at $c000-$c001 and scrolls the background by the count
inline auto cartridge() -> std::vector<u8> {
  std::vector<u8> rom(0x8000, 0xff);
  const u8 entry[] = {0x00, 0xc3, 0x50, 0x01};  //nop ; jp $0150
  memcpy(&rom[0x0100], entry, sizeof entry);
  memset(&rom[0x0134], 0x00, 0x0150 - 0x0134);
  memcpy(&rom[0x0134], "PHOBOS TEST", 11);
  const u8 code[] = {
    0xaf, 0xe0, 0x40,              //$0150 xor a ; ldh ($40),a        LCD off
    0x21, 0x00, 0x80,              //$0153 ld hl,$8000
    0x06, 0x08,                    //$0156 ld b,8
    0x3e, 0xf0, 0x22,              //$0158 ld a,$f0 ; ld (hl+),a     low plane
    0x3e, 0x0f, 0x22,              //$015b ld a,$0f ; ld (hl+),a     high plane
    0x05, 0x20, 0xf7,              //$015e dec b ; jr nz,$0158
    0x21, 0x00, 0x98,              //$0161 ld hl,$9800
    0x01, 0x00, 0x04,              //$0164 ld bc,$0400
    0xaf, 0x22,                    //$0167 xor a ; ld (hl+),a        tile 0 everywhere
    0x0b, 0x78, 0xb1, 0x20, 0xf9,  //$0169 dec bc ; ld a,b ; or c ; jr nz,$0167
    0xea, 0x00, 0xc0,              //$016e ld ($c000),a              the count, from 0
    0xea, 0x01, 0xc0,              //$0171 ld ($c001),a
    0x3e, 0xe4, 0xe0, 0x47,        //$0174 ld a,$e4 ; ldh ($47),a     BGP: colour n is shade n
    0x3e, 0x91, 0xe0, 0x40,        //$0178 ld a,$91 ; ldh ($40),a     LCD and background on, tiles at $8000
    0xf0, 0x44, 0xfe, 0x90,        //$017c ldh a,($44) ; cp $90       wait for line 144
    0x20, 0xfa,                    //$0180 jr nz,$017c
    0xfa, 0x00, 0xc0, 0x3c,        //$0182 ld a,($c000) ; inc a
    0xea, 0x00, 0xc0, 0xe0, 0x43,  //$0186 ld ($c000),a ; ldh ($43),a   SCX
    0x20, 0x07,                    //$018b jr nz,$0194
    0xfa, 0x01, 0xc0, 0x3c,        //$018d ld a,($c001) ; inc a
    0xea, 0x01, 0xc0,              //$0191 ld ($c001),a
    0xf0, 0x44, 0xfe, 0x90,        //$0194 ldh a,($44) ; cp $90       wait for line 144 to end
    0x28, 0xfa,                    //$0198 jr z,$0194
    0x18, 0xe0,                    //$019a jr $017c
  };
  memcpy(&rom[0x0150], code, sizeof code);
  return rom;
}

//the Game Boy's state the cartridge above changes: the CPU's registers, its RAM and the PPU's, and the scroll
inline auto stateHash() -> u64 {
  u64 hash = 0xcbf29ce484222325ull;
  auto add = [&](const void* data, u32 size) {
    for(u32 n = 0; n < size; n++) hash = (hash ^ ((const u8*)data)[n]) * 0x100000001b3ull;
  };
  auto memory = [&](auto& block) {
    for(u32 address = 0; address < block.size(); address++) {
      u8 value = block[address];
      add(&value, 1);
    }
  };
  using namespace ares::GameBoy;
  u16 registers[] = {cpu.r.af.word, cpu.r.bc.word, cpu.r.de.word, cpu.r.hl.word, cpu.r.sp.word, cpu.r.pc.word};
  add(registers, sizeof registers);
  memory(cpu.wram);
  memory(cpu.hram);
  memory(ppu.vram);
  memory(ppu.oam);
  u8 scroll[] = {(u8)ppu.status.ly, (u8)ppu.status.scx, (u8)ppu.status.scy};
  add(scroll, sizeof scroll);
  return hash;
}

inline auto registers() -> std::string {
  using namespace ares::GameBoy;
  char text[96];
  snprintf(text, sizeof text, "AF=%04x BC=%04x DE=%04x HL=%04x SP=%04x PC=%04x", (u32)cpu.r.af.word, (u32)cpu.r.bc.word,
    (u32)cpu.r.de.word, (u32)cpu.r.hl.word, (u32)cpu.r.sp.word, (u32)cpu.r.pc.word);
  return text;
}

//loads a Game Boy with the boot stub and the cartridge, and powers it on; false when the core refuses
inline auto loadGameBoy(ares::Node::System& root) -> bool {
  auto& paks = ares::testPlatform.paks;
  paks["Game Boy"] = {{{"boot.rom", bootStub()}}, {}};
  paks["Game Boy Cartridge"] = {{{"program.rom", cartridge()}}, {{"title", "PHOBOS TEST"}}};
  if(!ares::GameBoy::load(root, "[Nintendo] Game Boy")) return false;
  if(auto fastBoot = root->find<ares::Node::Setting::Boolean>("Fast Boot")) fastBoot->setValue(false);
  auto port = root->find<ares::Node::Port>("Cartridge Slot");
  if(!port) return false;
  port->allocate();
  port->connect();
  root->power();
  return true;
}

//as the runner's unloadSystem() ends, but for the Game Boy's own scheduler (threads.cpp resets it; a Game Boy's load
//resets it too), so that cores.cpp also builds on the code from before it
inline auto unload(ares::Node::System& root) -> void {
  if(root) root->unload();
  root.reset();
  ares::scheduler.reset();
}

inline auto runFrames(ares::Node::System& root, u32 frames) -> void {
  for(u32 n = 0; n < frames; n++) {
    root->run();
    progress++;
  }
}

}
