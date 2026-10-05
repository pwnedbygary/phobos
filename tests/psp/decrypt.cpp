//The ~PSP format (ares/psp/kernel/decrypt.cpp): programs encrypted here the way Sony's tools encrypt them
//(encrypt.hpp, docs/psp-core.md's part 18 run backwards) under a tag of each type, packed with gzip and not, come
//back exactly, and load and run; every tag in the tables round-trips; and damaged files, files cut short, unknown
//tags, packings Phobos doesn't unpack and damaged gzip streams are refused, each saying why, without reading or
//writing outside what they hold (the sanitizers watch).
#include "elf.hpp"
#include "kernel-machine.hpp"
#include "encrypt.hpp"
#include "../../ares/psp/kernel/loader.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::Loader;
using ares::PlayStationPortable::Module;
using ares::PlayStationPortable::decryptProgram;
namespace Keys = ares::PlayStationPortable::Keys;

//A small PRX: code that puts 42 in v0 and halts, its module info ("DECRYPTED") at 0x100, then words that pack well;
//0x905 bytes of segment, so the program isn't a whole number of AES blocks.
static auto testProgram() -> std::vector<u8> {
  ElfBuilder elf;
  ElfBuilder::Segment segment;
  auto& b = segment.bytes;
  b.put32(0x00, addiu(v0, zero, 42));
  b.put32(0x04, halt);
  b.put8(0x102, 1);
  b.putString(0x104, "DECRYPTED");
  for(u32 at = 0x200; at < 0x900; at += 4) b.put32(at, 0x1234'0000 + at);
  b.put8(0x904, 0x77);
  segment.memorySize = 0x1000;
  elf.segments.push_back(segment);
  elf.sections.push_back({".rodata.sceModuleInfo", 1, 0x100, {}});
  return elf.build();
}

static auto contains(const std::string& text, const char* part) -> bool {
  return text.find(part) != std::string::npos;
}

//The decrypter's answer for a file, and the program it gave.
static auto decrypt(const std::vector<u8>& file, std::vector<u8>& program) -> std::string {
  return decryptProgram(file.data(), file.size(), program);
}

//A tag of each type, packed with gzip and not: decrypted, the very program; loaded, it runs. Tags listed with two
//keys (a kernel module's 0x00000000 and 0x4467415d) come back with either, the 16-byte one found after the 144-byte
//one fails its check. Type 5 leaves 0xd4 unchecked; type 6 hashes its signature's end, zeros or not.
static auto types() -> void {
  auto program = testProgram();
  struct Case { u32 tag; bool secondKey; u8 signature; };
  for(auto [tag, secondKey, signature] : {
    Case{0x0800'0000, false, 0}, Case{0x0000'0000, false, 0}, Case{0x0300'0000, false, 0},  //type 0
    Case{0xc0cb'167c, false, 0}, Case{0x4467'415d, false, 0}, Case{0x3ace'4dce, false, 0},  //type 1
    Case{0xd916'05f0, false, 0}, Case{0x457b'0cf0, false, 0}, Case{0x0000'0000, true, 0},   //type 2
    Case{0x4467'415d, true, 0}, Case{0x2fd3'11f0, false, 0},                                //type 5
    Case{0xd916'80f0, false, 0xa7}, Case{0x457b'80f0, false, 0},                            //type 6
  }) {
    for(bool gzip : {false, true}) {
      psp_encrypt::Options options;
      options.tag = tag, options.secondKey = secondKey, options.signature = signature, options.gzip = gzip;
      auto file = psp_encrypt::encrypt(program, options);
      std::vector<u8> decrypted;
      CHECK(file.size() > 0x150, true);
      CHECK(decrypt(file, decrypted).empty(), true);
      CHECK(decrypted == program, true);
      for(bool recompile : {false, true}) {
        System s;
        s.recompiler.enabled = recompile;
        Module module;
        CHECK(Loader::load(s.memory, file.data(), file.size(), 0x0890'0000, {}, module).empty(), true);
        CHECK(module.name == "DECRYPTED", true);
        s.power(module.entry);
        s.run(100);
        CHECK(s.ipu.r[v0], 42);
      }
    }
  }
}

//Every tag in the tables: a program encrypted under it (with its 16-byte key when it has two) comes back. Their
//keyseeds are KIRK's own keys, none of those a PSP makes per console.
static auto everyTag() -> void {
  auto program = testProgram();
  auto roundTrip = [&](u32 tag, u32 keyseed, bool secondKey) {
    CHECK(keyseed < 0x80 && !(keyseed >= 0x20 && keyseed <= 0x2f) && !(keyseed >= 0x6c && keyseed <= 0x7b), true);
    psp_encrypt::Options options;
    options.tag = tag, options.secondKey = secondKey;
    std::vector<u8> decrypted;
    auto why = decrypt(psp_encrypt::encrypt(program, options), decrypted);
    bool same = why.empty() && decrypted == program;
    CHECK(same, true);
    if(!same) std::printf("  tag %08x: %s\n", tag, why.c_str());
  };
  for(auto& entry : Keys::padTags) roundTrip(entry.tag, entry.keyseed, false);
  for(auto& entry : Keys::seedTags) roundTrip(entry.tag, entry.keyseed, true);
}

//Refused, each saying why: what isn't a ~PSP file, one cut short at every length up to its header's end or in its
//program, a tag with no key and one with another's, a header damaged in any byte (each type: whatever byte it is,
//as each is hashed, or names the key; but type 5's 0xd4, which nothing reads), a program said to be elsewhere or
//bigger than the file, and packings Phobos doesn't unpack.
static auto refusals() -> void {
  auto program = testProgram();
  std::vector<u8> decrypted;
  CHECK(contains(decrypt(program, decrypted), "not an encrypted program"), true);
  auto file = psp_encrypt::encrypt(program);
  for(u32 size = 4; size < 0x150; size++) {
    std::vector<u8> cut(file.begin(), file.begin() + size);
    CHECK(contains(decrypt(cut, decrypted), "cut short in its header"), true);
  }
  for(u32 size : {0x150u, 0x160u, u32(file.size() - 1)}) {
    std::vector<u8> cut(file.begin(), file.begin() + size);
    CHECK(contains(decrypt(cut, decrypted), "runs past the end of the file"), true);
  }

  auto other = file;
  psp_encrypt::put32(other.data() + 0xd0, 0x1234'5678);
  CHECK(contains(decrypt(other, decrypted), "whose tag names a key Phobos doesn't have"), true);
  CHECK(contains(decrypt(other, decrypted), "tag 0x12345678"), true);
  psp_encrypt::put32(other.data() + 0xd0, 0xd916'06f0);
  CHECK(contains(decrypt(other, decrypted), "doesn't check out"), true);

  for(u32 tag : {0x0800'0000u, 0xc0cb'167cu, 0xd916'05f0u, 0x2fd3'12f0u, 0xd916'81f0u}) {
    psp_encrypt::Options options;
    options.tag = tag;
    options.signature = 0x3c;
    auto good = psp_encrypt::encrypt(program, options);
    u32 refused = 0, kept = 0;
    for(u32 at = 0; at < 0x150; at++) {
      auto damaged = good;
      damaged[at] ^= 0x01;
      auto why = decrypt(damaged, decrypted);
      bool unread = tag == 0x2fd3'12f0 && at == 0xd4;
      if(unread) kept += why.empty() && decrypted == program;
      else refused += !why.empty() && decrypted.empty();
    }
    CHECK(refused, tag == 0x2fd3'12f0 ? 0x14f : 0x150);
    CHECK(kept, tag == 0x2fd3'12f0 ? 1 : 0);
  }

  psp_encrypt::Options options;
  options.padding = 0x40;
  CHECK(contains(decrypt(psp_encrypt::encrypt(program, options), decrypted), "isn't where a ~PSP file keeps it"),
        true);
  options = {};
  for(u32 size : {0xffff'ff00u, 0xffff'ffffu, u32(program.size() + 16)}) {
    options.dataSize = size;
    CHECK(contains(decrypt(psp_encrypt::encrypt(program, options), decrypted), "runs past the end"), true);
  }
  options = {};
  options.gzip = true;
  using Packing = std::pair<u32, const char*>;
  for(auto [packing, says] : {Packing{1, "2RLZ"}, Packing{2, "KL4E"}, Packing{5, "Phobos doesn't know (5)"}}) {
    options.packing = packing;
    CHECK(contains(decrypt(psp_encrypt::encrypt(program, options), decrypted), says), true);
  }
}

//gzip: a program packed with a header carrying every optional field (extra bytes, a name, a comment, the header's
//CRC) unpacks; a damaged stream, CRC or size, a stream cut short, a name running off the end, and an unpacked size
//the ~PSP header gets wrong, are refused. And a damaged program the decrypter can't tell (the PSP's own signature
//over it isn't checked) comes out different, unless it's packed, when gzip's CRC tells.
static auto packing() -> void {
  auto program = testProgram();
  std::vector<u8> decrypted;
  psp_encrypt::Options options;
  options.gzip = true;
  //each damage done to the packed data before it's encrypted
  for(auto damage : std::initializer_list<void (*)(psp_encrypt::Bytes&)>{
    [](psp_encrypt::Bytes& data) { data[data.size() / 2] ^= 0x10; },     //the stream
    [](psp_encrypt::Bytes& data) { data[data.size() - 8] ^= 0x01; },     //the CRC
    [](psp_encrypt::Bytes& data) { data[data.size() - 4] ^= 0x01; },     //the size
    [](psp_encrypt::Bytes& data) { data.resize(data.size() - 9); },      //cut short
    [](psp_encrypt::Bytes& data) { data[3] = 0x08; std::fill(data.begin() + 10, data.end(), 'A'); },  //no end
    [](psp_encrypt::Bytes& data) { data[2] = 7; },                       //not deflate
  }) {
    options.damage = damage;
    CHECK(contains(decrypt(psp_encrypt::encrypt(program, options), decrypted), "packing (gzip) is damaged"), true);
    CHECK(decrypted.empty(), true);
  }
  options.damage = nullptr;
  options.programSize = program.size() + 1;
  CHECK(contains(decrypt(psp_encrypt::encrypt(program, options), decrypted), "packing (gzip) is damaged"), true);
  options.programSize = 0;

  //every optional field of the header: zlib's own stream with a header written here
  options.damage = [](psp_encrypt::Bytes& data) {
    psp_encrypt::Bytes fields = {0x1f, 0x8b, 8, 0x1e, 0, 0, 0, 0, 0, 3, 3, 0, 'x', 'y', 'z'};
    for(char c : std::string("PROGRAM.ELF")) fields.push_back(c);
    fields.push_back(0);
    for(char c : std::string("a comment")) fields.push_back(c);
    fields.push_back(0);
    fields.push_back(0x12), fields.push_back(0x34);  //the header's CRC (unchecked)
    fields.insert(fields.end(), data.begin() + 10, data.end());
    data = fields;
  };
  CHECK(decrypt(psp_encrypt::encrypt(program, options), decrypted).empty() && decrypted == program, true);

  //the program damaged, its packing not: it decrypts to something else; packed, the damage is refused
  auto file = psp_encrypt::encrypt(program);
  file[0x150 + 0x300] ^= 0x01;
  CHECK(decrypt(file, decrypted).empty() && decrypted != program && decrypted.size() == program.size(), true);
  options = {};
  options.gzip = true;
  file = psp_encrypt::encrypt(program, options);
  file[0x150 + 0x30] ^= 0x01;
  CHECK(contains(decrypt(file, decrypted), "packing (gzip) is damaged"), true);
}

//The kernel loads an encrypted program as any other (the loader decrypts it), and refuses one it can't decrypt with
//the decrypter's reason, leaving nothing behind.
static auto kernelLoads() -> void {
  auto program = testProgram();
  KernelMachine m;
  std::string error;
  auto file = psp_encrypt::encrypt(program);
  CHECK(m.kernel.load(file.data(), file.size(), "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN", error), true);
  CHECK(m.kernel.module.name == "DECRYPTED", true);
  CHECK(m.kernel.threads.size(), 1);
  psp_encrypt::put32(file.data() + 0xd0, 0x0bad'0bad);
  CHECK(m.kernel.load(file.data(), file.size(), "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN", error), false);
  CHECK(contains(error, "whose tag names a key Phobos doesn't have"), true);
  CHECK(m.kernel.threads.empty() && m.kernel.blocks.empty(), true);
}

auto decryptTests() -> Tests {
  return {
    {"decrypt types", types}, {"decrypt every tag", everyTag}, {"decrypt refusals", refusals},
    {"decrypt packing", packing}, {"decrypt kernel loads", kernelLoads},
  };
}

}
