//The Game Boy's and the Super Game Boy's cores on their schedulers, as the desktop program builds and links them
//(phobos_core's objects; CMakeLists.txt's target sgb-cores, which ctest runs as gb-save-state, sgb-smoke and
//sgb-owner-files). It uses no interface the Game Boy's own scheduler added, so that it builds on the code from before
//that too: there sgb-smoke stalls, and there the fixture below was made.
//T3, gb-save-state FIXTURE: a Game Boy with the boot stub and the synthetic cartridge (common.hpp), 60 frames, a save
//state (serialize(true), whose Synchronize pass needs each Game Boy thread at its safe point, on the Game Boy's
//scheduler), 60 frames, the state loaded and the same 60 frames again: the Game Boy's state (its registers, RAM, VRAM
//and scroll) the same both times, and as the code before the change had it (kExpected, which make-fixture printed
//there). Then FIXTURE, the state that code saved at frame 60 of the same run ("v144"), loaded and run 60 frames: the
//same state again.
//T4, sgb-smoke: a Super Famicom with the Super Game Boy's board (SGB-R-10 as ares/System/Super Famicom/boards.bml has
//it) and paks the test builds: an IPL of the test's own (an SPC700 loop) for the SMP, the boot stub as the ICD's Game
//Boy boot ROM, a 65816 program that starts the Game Boy (a write to $6003 with bit 7 rising, as the Super Game Boy's
//BIOS starts it) and then reads the ICD's LY port ($6000) and its four banks of lines through the VRAM port ($6001,
//$7800) into WRAM over and over, and the synthetic cartridge as the Game Boy game. Frames must keep coming back for 5
//emulated seconds; LY must change; the tile the Game Boy draws must reach the 65816 program through the ICD's output;
//the Super Famicom's state must save.
//T5, sgb-owner-files SYSTEM: the same with the owner's files, which never enter the repository: a Super Game Boy
//cartridge ($PHOBOS_SGB_CART) and its Game Boy boot ROM ($PHOBOS_SGB_BOOT), a Game Boy game ($PHOBOS_GB_GAME), and
//SYSTEM's ipl.rom and boards.bml (ctest names ares/System/Super Famicom). 20 emulated seconds past the Game Boy's
//start (its boot ROM handing over to the game), the game's picture in the ICD's output, and a state saved 10 s on and
//loaded twice, the same 120 frames after each load. (The run after a load needn't be the run without one: the ICD
//saves the Game Boy's threads without their stacks wherever the Game Boy's scheduler left them, as upstream's does;
//it prints which.) Without the three files it reports itself skipped (77).
//usage: sgb-cores gb-save-state FIXTURE | make-fixture FIXTURE | sgb-smoke | sgb-owner-files SYSTEM
#include <gb/gb.hpp>
#include <sfc/sfc.hpp>
#include <mia/mia.hpp>

#include <set>

#include "common.hpp"

namespace {

//make-fixture's output with the code from before the change (master 1024054c4): the state 60 frames after FIXTURE's
constexpr u64 kExpected = 0x7bbd9d3ce3c4cd08ull;
const std::string kExpectedRegisters = "AF=8f50 BC=0000 DE=0000 HL=9c00 SP=fffe PC=017e";

auto hexText(u64 value) -> std::string {
  char text[24];
  snprintf(text, sizeof text, "%016llx", (unsigned long long)value);
  return text;
}

auto gbSaveState(const std::string& fixture, bool make) -> int {
  test::current = make ? "make-fixture" : "T3: Game Boy save states";
  printf("%s\n", test::current.load());
  ares::Node::System root;
  if(!test::check(test::loadGameBoy(root), "a Game Boy loads and powers on")) return 1;
  test::runFrames(root, 60);
  auto state = root->serialize(true);
  test::progress++;
  if(make) {
    bool written = test::writeFile(fixture, state.data(), state.size());
    test::runFrames(root, 60);
    printf("  %s: %u bytes%s; 60 frames on: state %s, %s\n", fixture.c_str(), state.size(),
      written ? "" : " (NOT written)", hexText(test::stateHash()).c_str(), test::registers().c_str());
    test::unload(root);
    return written ? 0 : 1;
  }
  test::check(state.size() > 0, "frame 60: the state saved (" + std::to_string(state.size()) + " bytes)");
  test::runFrames(root, 60);
  u64 live = test::stateHash();
  std::string liveRegisters = test::registers();
  serializer reading{state.data(), state.size()};
  bool loaded = root->unserialize(reading);
  test::progress++;
  test::check(loaded, "it loads");
  if(loaded) {
    test::runFrames(root, 60);
    test::check(test::stateHash() == live && test::registers() == liveRegisters,
      "60 frames on, the state is as it was before the save was loaded: " + hexText(live) + ", " + liveRegisters);
  }
  test::check(live == kExpected && liveRegisters == kExpectedRegisters,
    "and as the code before the change ran it: " + hexText(kExpected) + ", " + kExpectedRegisters);
  auto old = test::readFile(fixture);
  serializer oldState{old.data(), (u32)old.size()};
  loaded = !old.empty() && root->unserialize(oldState);
  test::progress++;
  test::check(loaded, "the code before the change's state (" + std::to_string(old.size()) + " bytes, v144) loads");
  if(loaded) {
    test::runFrames(root, 60);
    test::check(test::stateHash() == kExpected && test::registers() == kExpectedRegisters,
      "and continues as that code continued it: " + hexText(test::stateHash()) + ", " + test::registers());
  }
  test::unload(root);
  return test::failures ? 1 : 0;
}

//SGB-R-10, the Super Game Boy's board, as ares/System/Super Famicom/boards.bml has it
const std::string kBoards =
  "board: SGB-R-10\n"
  "  memory type=ROM content=Program\n"
  "    map address=00-7d,80-ff:8000-ffff mask=0x8000\n"
  "    map address=40-7d,c0-ff:0000-7fff mask=0x8000\n"
  "  processor identifier=ICD revision=2\n"
  "    map address=00-3f,80-bf:6000-67ff,7000-7fff\n"
  "    memory type=ROM content=Boot architecture=SM83\n"
  "    slot type=GameBoy\n";

auto bytes(const std::string& text) -> std::vector<u8> {
  return {text.begin(), text.end()};
}

//64 bytes at $ffc0 for the SMP: bra $ffc0, and the reset vector pointing there
auto ipl() -> std::vector<u8> {
  std::vector<u8> rom(64, 0x00);
  rom[0] = 0x2f, rom[1] = 0xfe;
  rom[62] = 0xc0, rom[63] = 0xff;
  return rom;
}

//32 KiB of LoROM: the Game Boy started, then, over and over, the LY port to WRAM $0000, the VRAM port's four banks of
//320 bytes (20 tiles of eight lines, as the Super Game Boy's BIOS reads them) to $0100-$05ff, and a count of the loops
//at $0002
auto sfcProgram() -> std::vector<u8> {
  std::vector<u8> rom(0x8000, 0x00);
  const u8 code[] = {
    0x78, 0x18, 0xfb, 0xc2, 0x10,  //$8000 sei ; clc ; xce ; rep #$10   native mode, 8-bit A, 16-bit X and Y
    0xa9, 0x81, 0x8d, 0x03, 0x60,  //$8005 lda #$81 ; sta $6003         the Game Boy out of reset, the clock /5
    0xad, 0x00, 0x60,              //$800a lda $6000                    the LY port
    0x8d, 0x00, 0x00,              //$800d sta $0000
    0xa0, 0x00, 0x00,              //$8010 ldy #$0000
    0x9c, 0x03, 0x00,              //$8013 stz $0003                    the bank
    0xad, 0x03, 0x00,              //$8016 lda $0003
    0x8d, 0x01, 0x60,              //$8019 sta $6001                    the VRAM port: that bank, from its start
    0xa2, 0x40, 0x01,              //$801c ldx #$0140
    0xad, 0x00, 0x78,              //$801f lda $7800
    0x99, 0x00, 0x01,              //$8022 sta $0100,y
    0xc8, 0xca, 0xd0, 0xf6,        //$8025 iny ; dex ; bne $801f
    0xee, 0x03, 0x00,              //$8029 inc $0003
    0xad, 0x03, 0x00,              //$802c lda $0003
    0xc9, 0x04, 0xd0, 0xe3,        //$802f cmp #$04 ; bne $8016
    0xee, 0x02, 0x00,              //$8033 inc $0002
    0x80, 0xd2,                    //$8036 bra $800a
  };
  memcpy(&rom[0x0000], code, sizeof code);
  rom[0x7ffc] = 0x00, rom[0x7ffd] = 0x80;  //the reset vector: $8000
  return rom;
}

//whether a plane 0 and plane 1 byte pair is a row of the cartridge's tile (rows of $f0 and $0f), scrolled by any amount
auto tileRow(u8 plane0, u8 plane1) -> bool {
  for(u32 shift = 0; shift < 8; shift++) {
    u8 rotated = (u8)(0xf0 << shift | 0xf0 >> (8 - shift));
    if(plane0 == rotated && plane1 == (u8)~rotated) return true;
  }
  return false;
}

//the rows of the cartridge's tile among the four banks the 65816 program copied (20 tiles of eight lines each)
auto tileRowsCopied() -> u32 {
  auto& wram = ares::SuperFamicom::cpu.wram;
  u32 rows = 0;
  for(u32 bank = 0; bank < 4; bank++) {
    for(u32 tile = 0; tile < 20; tile++) {
      for(u32 y = 0; y < 8; y++) {
        u32 address = 0x100 + bank * 320 + tile * 16 + y * 2;
        rows += tileRow(wram[address], wram[address + 1]);
      }
    }
  }
  return rows;
}

//the byte values in the ICD's four banks, read through its VRAM port as the BIOS reads them (from the host, between
//frames: the BIOS selects its bank again before each read of one)
auto outputValues() -> std::set<u8> {
  auto& icd = ares::SuperFamicom::icd;
  std::set<u8> values;
  for(u32 bank = 0; bank < 4; bank++) {
    icd.writeIO(0x6001, bank);
    for(u32 n = 0; n < 320; n++) values.insert((u8)icd.readIO(0x7800, 0x00));
  }
  return values;
}

//as the runner loads the Super Game Boy: the Super Famicom, its cartridge, then the Game Boy's slot that appears
//under the ICD as the cartridge connects
auto loadSuperGameBoy(ares::Node::System& root) -> bool {
  ares::SuperFamicom::option("Pixel Accuracy", "false");  //the runner's choice for both: the fast PPU
  if(!ares::SuperFamicom::load(root, "[Nintendo] Super Famicom (NTSC)")) return false;
  auto port = root->find<ares::Node::Port>("Cartridge Slot");
  if(!port) return false;
  port->allocate();
  port->connect();
  bool gameBoy = false;
  for(auto& nested : root->find<ares::Node::Port>()) {
    if(nested->type() != "Cartridge" || nested->family() != "Game Boy" || nested->connected()) continue;
    if(nested->allocate()) {
      nested->connect();
      gameBoy = (bool)nested->connected();
    }
  }
  if(!gameBoy) return false;
  root->power();
  return true;
}

//the state a save state must bring back: the Game Boy's (common.hpp) and the Super Famicom's WRAM, where the BIOS
//keeps what it read of the ICD's output
auto sgbHash() -> u64 {
  u64 hash = test::stateHash();
  auto& wram = ares::SuperFamicom::cpu.wram;
  for(u32 n = 0; n < std::size(wram); n++) hash = (hash ^ (u8)wram[n]) * 0x100000001b3ull;
  return hash;
}

auto sgbSmoke() -> int {
  test::current = "T4: a Super Game Boy, synthetic";
  printf("%s\n", test::current.load());
  auto& paks = ares::testPlatform.paks;
  paks["Super Famicom"] = {{{"boards.bml", bytes(kBoards)}, {"ipl.rom", ipl()}}, {}};
  paks["Super Famicom Cartridge"] = {{{"program.rom", sfcProgram()}, {"sm83.boot.rom", test::bootStub()}},
    {{"title", "PHOBOS SGB TEST"}, {"region", "NTSC"}, {"board", "SGB-R-10"}}};
  paks["Game Boy Cartridge"] = {{{"program.rom", test::cartridge()}}, {{"title", "PHOBOS TEST"}}};
  ares::Node::System root;
  if(!test::check(loadSuperGameBoy(root), "a Super Famicom with the Super Game Boy and the Game Boy cartridge loads"))
    return 1;
  auto& wram = ares::SuperFamicom::cpu.wram;
  std::set<u8> ly;
  u8 loops = wram[2];
  u32 frames = 0, looping = 0;
  for(; frames < 300; frames++) {
    root->run();
    test::progress++;
    ly.insert(wram[0]);
    looping += (u8)wram[2] != loops;
    loops = wram[2];
  }
  test::check(frames == 300, "300 frames came back (5 emulated seconds)");
  test::check(looping >= 290, "the 65816 program kept looping: its count moved in " + std::to_string(looping) +
    " of 300 frames");
  test::check(ares::GameBoy::cpu.r.pc.word >= 0x0100 && ares::GameBoy::cpu.wram[0] != 0,
    "the Game Boy ran its cartridge: PC " + hexText(ares::GameBoy::cpu.r.pc.word).substr(12) + ", its frame count " +
    std::to_string((u8)ares::GameBoy::cpu.wram[0]));
  test::check(ly.size() >= 4, "the LY port changed: " + std::to_string(ly.size()) + " values read");
  u32 rows = tileRowsCopied();
  test::check(rows >= 512, "the Game Boy's tile reached the 65816 program through the ICD's output: " +
    std::to_string(rows) + " of 640 rows");
  auto state = root->serialize(true);
  test::progress++;
  test::check(state.size() > 0, "the Super Famicom's state saved (" + std::to_string(state.size()) + " bytes)");
  test::runFrames(root, 60);
  test::check(true, "60 more frames came back");
  test::unload(root);
  return test::failures ? 1 : 0;
}

auto sgbOwnerFiles(const std::string& system) -> int {
  auto env = [](const char* name) -> std::string {
    auto value = getenv(name);
    return value ? value : "";
  };
  std::string cartPath = env("PHOBOS_SGB_CART"), bootPath = env("PHOBOS_SGB_BOOT"), gamePath = env("PHOBOS_GB_GAME");
  if(cartPath.empty() || bootPath.empty() || gamePath.empty()) {
    printf("sgb-owner-files: skipped (it needs PHOBOS_SGB_CART, PHOBOS_SGB_BOOT and PHOBOS_GB_GAME)\n");
    return 77;
  }
  test::current = "T5: a Super Game Boy with the owner's files";
  printf("%s\n", test::current.load());
  auto cart = test::readFile(cartPath), boot = test::readFile(bootPath);
  auto ipl = test::readFile(system + "/ipl.rom"), boards = test::readFile(system + "/boards.bml");
  if(!test::check(cart.size() >= 256 * 1024 && boot.size() == 256 && ipl.size() == 64 && !boards.empty(),
    "the files read: the cartridge " + std::to_string(cart.size()) + " bytes, the boot ROM " +
    std::to_string(boot.size()) + ", the IPL " + std::to_string(ipl.size()) + ", boards.bml " +
    std::to_string(boards.size()))) return 1;
  bool sgb2 = cart.size() == 512 * 1024;
  auto& paks = ares::testPlatform.paks;
  paks["Super Famicom"] = {{{"boards.bml", boards}, {"ipl.rom", ipl}}, {}};
  paks["Super Famicom Cartridge"] = {{{"program.rom", cart}, {"sm83.boot.rom", boot}},
    {{"title", "Super Game Boy"}, {"region", "NTSC"}, {"board", sgb2 ? "SHVC-SGB2-01" : "SGB-R-10"}}};
  if(sgb2) paks["Super Famicom Cartridge"].attributes["oscillator"] = "20971520";
  auto game = mia::Medium::create("Game Boy");
  if(!test::check(game && game->load(gamePath.c_str()).result == successful && game->pak, "the game loads in mia"))
    return 1;
  ares::testPlatform.directories["Game Boy Cartridge"] = game->pak;
  ares::Node::System root;
  if(!test::check(loadSuperGameBoy(root), std::string{"the Super Famicom loads with the "} + (sgb2 ? "SGB2" : "SGB1") +
    " cartridge and the game")) return 1;
  auto start = std::chrono::steady_clock::now();
  u32 frames = 0, started = 0;
  for(; frames < 600 && !started; frames++, test::progress++) {
    root->run();
    if(ares::GameBoy::cpu.r.pc.word >= 0x0100) started = frames + 1;  //past the boot ROM, in the game
  }
  if(!test::check(started, "the BIOS started the Game Boy, whose boot ROM handed over to the game (frame " +
    std::to_string(started) + ")")) return 1;
  u32 saveAt = started + 601, end = started + 1203;  //10 and 20 emulated seconds on, at 60.1 frames a second
  for(; frames < saveAt; frames++, test::progress++) root->run();
  auto state = root->serialize(true);
  test::progress++;
  test::check(state.size() > 0, "10 s on, the state saved (" + std::to_string(state.size()) + " bytes)");
  test::runFrames(root, 120);
  u64 live = sgbHash();
  u64 afterLoad[2] = {};
  bool loaded = true;
  for(u32 n = 0; n < 2 && loaded; n++) {
    serializer reading{state.data(), state.size()};
    loaded = root->unserialize(reading);
    test::progress++;
    if(loaded) test::runFrames(root, 120), afterLoad[n] = sgbHash();
  }
  test::check(loaded, "it loads, twice");
  test::check(loaded && afterLoad[1] == afterLoad[0], "120 frames after each load, the state is the same");
  printf("  note: 120 frames after a load, the state is %s 120 frames after the save without one\n",
    afterLoad[0] == live ? "the same as" : "not the same as");
  test::runFrames(root, end - (saveAt + 120));  //on to 20 s along the last load's run
  frames = end + 2 * 120;  //run on this host: to the save, 120 on, 120 after each load, then on to `end`
  auto values = outputValues();
  test::check(values.size() >= 3, "20 s past the Game Boy's start, the ICD's output holds a picture: " +
    std::to_string(values.size()) + " byte values");
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  printf("  %u frames in %.1f s of this host's time (no speed claim)\n", frames, seconds);
  test::unload(root);
  return test::failures ? 1 : 0;
}

}

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  std::string mode = argc > 1 ? argv[1] : "";
  std::string arg = argc > 2 ? argv[2] : "";
  bool ownerFiles = getenv("PHOBOS_SGB_CART") && getenv("PHOBOS_SGB_BOOT") && getenv("PHOBOS_GB_GAME");
  if(mode == "sgb-owner-files" && !ownerFiles) return sgbOwnerFiles(arg);  //skipped, before the watchdog starts
  test::watchdog("sgb-cores " + mode, 10);
  int status = 2;
  if(mode == "gb-save-state" || mode == "make-fixture") status = gbSaveState(arg, mode == "make-fixture");
  else if(mode == "sgb-smoke") status = sgbSmoke();
  else if(mode == "sgb-owner-files") status = sgbOwnerFiles(arg);
  else printf("usage: sgb-cores gb-save-state FIXTURE | make-fixture FIXTURE | sgb-smoke | sgb-owner-files SYSTEM\n");
  printf("sgb-cores %s: %u failed\n", mode.c_str(), test::failures);
  return status;
}
