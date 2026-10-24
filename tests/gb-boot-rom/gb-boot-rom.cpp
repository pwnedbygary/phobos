//The Game Boy Color's boot ROM as the core loads it (ares/gb/system/system.cpp) and maps it
//(ares/gb/cartridge/memory.cpp), on the core as the desktop program builds and links it (phobos_core's objects;
//CMakeLists.txt's target gb-boot-rom, which ctest runs). Pan Docs ("Power-Up Sequence") gives the layout: 256 + 1792
//bytes, mapped at $0000-$00ff and $0200-$08ff, with the cartridge's header showing at $0100-$01ff between them. An
//image comes in one of two layouts: 2,048 bytes, the two parts end to end (ares's, which the app ships), or 2,304
//bytes, with 256 bytes in the header's place between them (a full dump, which the desktop program takes by its file
//name or its CRC32), which may have more bytes after it. A synthetic image in each layout, and full ones of 2,305 and
//4,096 bytes, whose bytes tell their offsets apart, are read back through the bus at every address from $0000 to $0fff,
//with the boot ROM mapped and again after the write to $ff50 that unmaps it, each byte against the offset its layout
//gives; so is a synthetic Game Boy image, 256 bytes at $0000-$00ff. Then each boot ROM named on the command line (ctest
//names the app's boot.cgb-0.rom and boot.cgb-1.rom) boots a cartridge as 2,048 and 2,304 bytes and as full images of
//2,305 and 4,096 bytes, as a Game Boy Color game and as a Game Boy one: the cartridge's first instructions store the
//registers the boot ROM handed over, which are checked against Pan Docs' ("Console state after boot ROM hand-off").
//usage: gb-boot-rom [CGB BOOT ROM...]
#include <gb/gb.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace ares {
  //what the runner (android/app/src/main/cpp/PhobosRunner.cpp) gives the core, without the runner: the system's pak
  //holds boot.rom, the cartridge's program.rom (no mapper)
  struct TestPlatform : Platform {
    std::vector<u8> bootROM;
    std::vector<u8> programROM;

    auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override {
      auto pak = std::make_shared<vfs::directory>();
      if(node->name() == "Game Boy" || node->name() == "Game Boy Color") {
        pak->append("boot.rom", vfs::memory::open(std::span<const u8>{bootROM.data(), bootROM.size()}));
      } else {
        pak->setAttribute("title", "PHOBOS TEST");
        pak->append("program.rom", vfs::memory::open(std::span<const u8>{programROM.data(), programROM.size()}));
      }
      return pak;
    }
  };
  static TestPlatform testPlatform;
  Platform* platform = &testPlatform;
}

namespace {

namespace GameBoy = ares::GameBoy;

u32 checks = 0;
u32 failures = 0;

//a synthetic image's byte at `offset`: the offset's low byte, with its page's own bits flipped. All the bytes a read
//of one address could return (the image's at the right offset, at one a page or more away, the cartridge's) come
//from places with the address's low byte, so each of them differs from the others.
auto imageByte(u32 offset) -> u8 { return u8(offset) ^ u8(0x11 * (offset >> 8)); }
auto cartridgeByte(u32 address) -> u8 { return u8(address) ^ 0xa5; }

//where the byte `data` read at `address` was, by those patterns
auto origin(u32 address, u8 data) -> std::string {
  u32 flip = u8(data ^ address);
  if(flip == 0xa5) return "the cartridge's";
  if(flip % 0x11) return "neither's";
  char text[32];
  snprintf(text, sizeof text, "the image's at $%03x", (flip / 0x11) << 8 | u8(address));
  return text;
}

//Pan Docs' layouts: the image's offset mapped at `address`, or -1 where the cartridge shows
auto cgb2048(u32 address) -> s32 {
  if(address <= 0x00ff) return address;
  if(address >= 0x0200 && address <= 0x08ff) return address - 0x100;
  return -1;
}

auto cgb2304(u32 address) -> s32 {
  if(address <= 0x00ff || (address >= 0x0200 && address <= 0x08ff)) return address;
  return -1;
}

auto dmg256(u32 address) -> s32 {
  if(address <= 0x00ff) return address;
  return -1;
}

struct Layout {
  const char* name;
  const char* system;
  u32 size;
  s32 (*offset)(u32 address);
};

//loads the system with a cartridge and powers it on
auto power(ares::Node::System& root, const char* system) -> bool {
  if(!GameBoy::load(root, system)) return false;
  if(auto fastBoot = root->find<ares::Node::Setting::Boolean>("Fast Boot")) fastBoot->setValue(false);
  auto port = root->find<ares::Node::Port>("Cartridge Slot");
  if(!port) return false;
  port->allocate();
  port->connect();
  root->power();
  return true;
}

//as the runner's unloadSystem() ends
auto unload(ares::Node::System& root) -> void {
  if(root) root->unload();
  root.reset();
  ares::scheduler.reset();
}

auto mapping(const Layout& layout) -> void {
  ares::testPlatform.bootROM.resize(layout.size);
  for(u32 offset = 0; offset < layout.size; offset++) ares::testPlatform.bootROM[offset] = imageByte(offset);
  ares::testPlatform.programROM.resize(0x8000);
  for(u32 address = 0; address < 0x8000; address++) ares::testPlatform.programROM[address] = cartridgeByte(address);

  ares::Node::System root;
  if(!power(root, layout.system)) {
    printf("%s: the core didn't load it\n", layout.name);
    checks++, failures++;
    unload(root);
    return;
  }
  for(bool mapped : {true, false}) {
    if(!mapped) GameBoy::bus.write(0xff50, 0x11);  //as the boot ROM's last instruction does
    u32 wrong = 0;
    std::string first;
    for(u32 address = 0x0000; address <= 0x0fff; address++) {
      s32 offset = mapped ? layout.offset(address) : -1;
      u8 expected = offset < 0 ? cartridgeByte(address) : imageByte(offset);
      u8 data = GameBoy::bus.read(address, 0xff);
      if(data == expected) continue;
      if(!wrong++) {
        char text[160];
        snprintf(text, sizeof text, "; the first, $%04x: $%02x, %s byte, not $%02x, %s", address, data,
          origin(address, data).c_str(), expected, origin(address, expected).c_str());
        first = text;
      }
    }
    printf("%s, %s: %u of the 4096 bytes at $0000-$0fff wrong%s\n", layout.name,
      mapped ? "the boot ROM mapped" : "after the write to $ff50", wrong, first.c_str());
    checks++, failures += wrong != 0;
  }
  unload(root);
}

//a cartridge (32 KiB, no mapper) whose header the boot ROM accepts: its logo is the boot ROM's own copy (at
//$0042-$0071 in either layout), its licensee isn't Nintendo, its checksum is right and `flag` is its CGB flag. Its code
//at $0150 stores the registers it was handed (SP at $c000-$c001, then A, B, C, D, E, H, L and F), then $42 at $c00a.
auto cartridge(const std::vector<u8>& bootROM, u8 flag) -> std::vector<u8> {
  std::vector<u8> rom(0x8000, 0xff);  //rst $38: a boot that goes anywhere else in the cartridge loops at $0038
  const u8 entry[] = {0x00, 0xc3, 0x50, 0x01};  //nop ; jp $0150
  memcpy(&rom[0x0100], entry, sizeof entry);
  memcpy(&rom[0x0104], &bootROM[0x0042], 48);
  memset(&rom[0x0134], 0x00, 0x014d - 0x0134);
  memcpy(&rom[0x0134], "PHOBOS TEST", 11);
  rom[0x0143] = flag;
  rom[0x0144] = '0';  //the new licensee code, "00", which $014b = $33 points to
  rom[0x0145] = '0';
  rom[0x014b] = 0x33;
  u8 checksum = 0;
  for(u32 address = 0x0134; address <= 0x014c; address++) checksum = checksum - rom[address] - 1;
  rom[0x014d] = checksum;
  const u8 code[] = {
    0x08, 0x00, 0xc0,              //ld ($c000),sp
    0xea, 0x02, 0xc0,              //ld ($c002),a
    0x78, 0xea, 0x03, 0xc0,        //ld a,b ; ld ($c003),a
    0x79, 0xea, 0x04, 0xc0,        //ld a,c ; ld ($c004),a
    0x7a, 0xea, 0x05, 0xc0,        //ld a,d ; ld ($c005),a
    0x7b, 0xea, 0x06, 0xc0,        //ld a,e ; ld ($c006),a
    0x7c, 0xea, 0x07, 0xc0,        //ld a,h ; ld ($c007),a
    0x7d, 0xea, 0x08, 0xc0,        //ld a,l ; ld ($c008),a
    0xf5, 0xe1,                    //push af ; pop hl  (none of the loads above changes F)
    0x7d, 0xea, 0x09, 0xc0,        //ld a,l ; ld ($c009),a
    0x3e, 0x42, 0xea, 0x0a, 0xc0,  //ld a,$42 ; ld ($c00a),a
    0x18, 0xfe,                    //jr $
  };
  memcpy(&rom[0x0150], code, sizeof code);
  return rom;
}

struct Registers {
  u8 a, f, b, c, d, e, h, l;
  u16 sp;
};

//Pan Docs' table ("CPU registers") for a cartridge whose licensee isn't Nintendo: CGB, and CGB (DMG mode)
const Registers colourGame = {0x11, 0x80, 0x00, 0x00, 0xff, 0x56, 0x00, 0x0d, 0xfffe};
const Registers monoGame   = {0x11, 0x80, 0x00, 0x00, 0x00, 0x08, 0x00, 0x7c, 0xfffe};

auto same(const Registers& x, const Registers& y) -> bool {
  return x.a == y.a && x.f == y.f && x.b == y.b && x.c == y.c && x.d == y.d && x.e == y.e && x.h == y.h
      && x.l == y.l && x.sp == y.sp;
}

auto describe(const Registers& r) -> std::string {
  char text[80];
  snprintf(text, sizeof text, "A=%02x F=%02x B=%02x C=%02x D=%02x E=%02x H=%02x L=%02x SP=%04x",
    r.a, r.f, r.b, r.c, r.d, r.e, r.h, r.l, r.sp);
  return text;
}

auto boot(const std::string& name, const std::vector<u8>& image, u8 flag, const Registers& expected) -> void {
  const char* game = flag & 0x80 ? "a Game Boy Color game" : "a Game Boy game";
  ares::testPlatform.bootROM = image;
  ares::testPlatform.programROM = cartridge(image, flag);
  checks++;

  ares::Node::System root;
  if(!power(root, "[Nintendo] Game Boy Color")) {
    printf("%s, %s: the core didn't load it\n", name.c_str(), game);
    failures++;
    unload(root);
    return;
  }
  GameBoy::bus.write(0xc00a, 0x00);
  //the boot takes about 3 s (186 frames for a Game Boy Color game); the cartridge's code, part of a frame after it
  u32 frame = 0, handover = 0;
  while(frame < 600 && (!handover || frame < handover + 2)) {
    root->run();
    frame++;
    if(!handover && !GameBoy::cartridge.bootromEnable) handover = frame;
  }
  auto at = [](u16 address) -> u8 { return GameBoy::bus.read(address, 0xff); };
  Registers found{at(0xc002), at(0xc009), at(0xc003), at(0xc004), at(0xc005), at(0xc006), at(0xc007), at(0xc008),
    u16(at(0xc000) | at(0xc001) << 8)};
  u8 done = at(0xc00a);
  unload(root);

  if(!handover) {
    printf("%s, %s: the boot ROM was still mapped after %u frames\n", name.c_str(), game, frame);
    failures++;
  } else if(done != 0x42) {
    printf("%s, %s: the boot ROM was unmapped in frame %u, but the cartridge's code at $0150 didn't run\n",
      name.c_str(), game, handover);
    failures++;
  } else if(!same(found, expected)) {
    printf("%s, %s: handed over in frame %u with %s, not Pan Docs' %s\n", name.c_str(), game, handover,
      describe(found).c_str(), describe(expected).c_str());
    failures++;
  } else {
    printf("%s, %s: handed over in frame %u with %s, as Pan Docs gives\n", name.c_str(), game, handover,
      describe(found).c_str());
  }
}

auto readFile(const char* path) -> std::vector<u8> {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}

int main(int argc, char** argv) {
  const Layout layouts[] = {
    {"Game Boy Color, a 2,048-byte image", "[Nintendo] Game Boy Color", 0x800, cgb2048},
    {"Game Boy Color, a 2,304-byte image", "[Nintendo] Game Boy Color", 0x900, cgb2304},
    {"Game Boy Color, a 2,305-byte image (a full dump and a byte)", "[Nintendo] Game Boy Color", 0x901, cgb2304},
    {"Game Boy Color, a 4,096-byte image (a full dump, padded)", "[Nintendo] Game Boy Color", 0x1000, cgb2304},
    {"Game Boy, a 256-byte image", "[Nintendo] Game Boy", 0x100, dmg256},
  };
  for(auto& layout : layouts) mapping(layout);

  for(int n = 1; n < argc; n++) {
    std::string name = argv[n];
    name = name.substr(name.find_last_of("/\\") + 1);
    auto file = readFile(argv[n]);
    std::vector<u8> compact, full;
    if(file.size() == 0x800) {
      compact = full = file;
      full.insert(full.begin() + 0x100, 0x100, 0x00);
    } else if(file.size() == 0x900) {
      compact = full = file;
      compact.erase(compact.begin() + 0x100, compact.begin() + 0x200);
    } else {
      printf("%s: %zu bytes, a Game Boy Color boot ROM in neither layout\n", name.c_str(), file.size());
      checks++, failures++;
      continue;
    }
    auto longer = full;
    longer.push_back(0x00);
    auto padded = full;
    padded.resize(0x1000, 0x00);
    for(auto& [layout, image] : {std::pair{"2,048 bytes", &compact}, std::pair{"2,304 bytes", &full},
                                 std::pair{"2,305 bytes (a full dump and a byte)", &longer},
                                 std::pair{"4,096 bytes (a full dump, padded)", &padded}}) {
      boot(name + " as " + layout, *image, 0x80, colourGame);
      boot(name + " as " + layout, *image, 0x00, monoGame);
    }
  }

  printf("gb-boot-rom: %u checks, %u failed\n", checks, failures);
  return failures ? 1 : 0;
}
