//The loader (ares/psp/kernel/loader): programs built by elf.hpp, each part checked, and the relocated code run on
//the CPU. With PSP_TEST_PROGRAMS set to a folder holding hello.elf, hello.prx and EBOOT.PBP (a hello-world program
//built with pspdev's toolchain, as docs/psp-core.md describes), it also loads those.
#include "elf.hpp"
#include "../../ares/psp/kernel/loader.hpp"

#include <cstdlib>
#include <fstream>

namespace allegrex_test::psp {

using ares::PlayStationPortable::Loader;
using ares::PlayStationPortable::Module;

constexpr u32 ModuleStart = 0xd632'acdb, ModuleInfoNid = 0xf01d'73a7;  //the NIDs of module_start and module_info

//The test program, linked as if it started at 0 (a PRX) or at linkedAt (a static executable):
//  0x000  code, using addresses the loader must move: a jal, and addresses built with lui and addiu (one already
//         carrying into the upper half, one lui shared by two lower halves, one that only carries once moved to a
//         base whose low half is 0x4000 or more, and the same with its lower half marked as a plain 16-bit
//         relocation); then halt
//  0x040  the function the jal calls: v0 = 7, return
//  0x100  a pointer to the text at 0x180
//  0x200  the module info
//  0x240  imports: two from sceDisplay; one function and one variable from ThreadManForUser
//  0x280  their NIDs; 0x2a0 their stubs
//  0x2c0  the module's own exports: module_start (a function) and module_info (a variable)
//  0x300  names
//  0x9000 an address the code builds (past the segment's bytes, in its zeroed part)
struct TestProgram {
  ElfBuilder elf;
  Bytes relocations;

  TestProgram(u32 type = 0xffa0, u32 linkedAt = 0, bool withSections = true) {
    elf.type = type;
    elf.withSections = withSections;
    elf.entry = linkedAt;
    ElfBuilder::Segment segment;
    segment.address = linkedAt;
    segment.memorySize = 0xa000;
    auto& b = segment.bytes;
    auto relocate = [&](u32 offset, u32 kind) { if(type == 0xffa0) relocation(relocations, offset, kind); };
    auto address = [&](u32 offset) { return linkedAt + offset; };
    auto hi = [&](u32 offset) { return (address(offset) + 0x8000) >> 16; };
    auto lo = [&](u32 offset) { return address(offset) & 0xffff; };

    b.put32(0x00, jal(address(0x40)));                   relocate(0x00, 4);
    b.put32(0x04, nop);
    b.put32(0x08, lui(s0, hi(0x9000)));                  relocate(0x08, 5);
    b.put32(0x0c, addiu(s0, s0, lo(0x9000)));            relocate(0x0c, 6);
    b.put32(0x10, lui(at, hi(0x100)));                   relocate(0x10, 5);
    b.put32(0x14, addiu(s1, at, lo(0x100)));             relocate(0x14, 6);
    b.put32(0x18, lw(s2, int32_t(lo(0x100)), at));       relocate(0x18, 6);
    b.put32(0x1c, lui(s3, hi(0x5000)));                  relocate(0x1c, 5);
    b.put32(0x20, addiu(s3, s3, lo(0x5000)));            relocate(0x20, 6);
    b.put32(0x24, lui(s4, hi(0x5000)));                  relocate(0x24, 5);
    b.put32(0x28, addiu(s4, s4, lo(0x5000)));            relocate(0x28, 1);
    b.put32(0x2c, halt);
    b.put32(0x40, addiu(v0, zero, 7));
    b.put32(0x44, jr(ra));
    b.put32(0x48, nop);
    b.put32(0x100, address(0x180));                      relocate(0x100, 2);
    b.putString(0x180, "relocated");

    b.put16(0x200, 0);                                   //module info: attributes, version 1.2, name
    b.put8(0x202, 1); b.put8(0x203, 2);
    b.putString(0x204, "TESTPRX");
    b.put32(0x220, address(0x8000));                     relocate(0x220, 2);  //gp
    b.put32(0x224, address(0x2c0));                      relocate(0x224, 2);  //exports
    b.put32(0x228, address(0x2d0));                      relocate(0x228, 2);
    b.put32(0x22c, address(0x240));                      relocate(0x22c, 2);  //imports
    b.put32(0x230, address(0x240 + 20 + 24));            relocate(0x230, 2);

    b.put32(0x240, address(0x300));                      relocate(0x240, 2);  //sceDisplay: 5 words, 2 functions
    b.put32(0x244, 0x4009'0000);
    b.put32(0x248, 5 | 0 << 8 | 2 << 16);
    b.put32(0x24c, address(0x280));                      relocate(0x24c, 2);
    b.put32(0x250, address(0x2a0));                      relocate(0x250, 2);
    b.put32(0x254, address(0x310));                      relocate(0x254, 2);  //ThreadManForUser: 6 words, 1 + 1
    b.put32(0x258, 0x4009'0000);
    b.put32(0x25c, 6 | 1 << 8 | 1 << 16);
    b.put32(0x260, address(0x288));                      relocate(0x260, 2);
    b.put32(0x264, address(0x2b0));                      relocate(0x264, 2);
    b.put32(0x268, address(0x2b8));                      relocate(0x268, 2);
    b.put32(0x280, 0x289d'82fe); b.put32(0x284, 0x0e20'f177); b.put32(0x288, 0xceadeb47);
    for(u32 stub = 0x2a0; stub < 0x2b8; stub += 8) { b.put32(stub, jr(ra)); b.put32(stub + 4, nop); }

    b.put32(0x2c0, 0);                                   //the module's own exports: 4 words, 1 + 1
    b.put32(0x2c4, 0x8000'0000);
    b.put32(0x2c8, 4 | 1 << 8 | 1 << 16);
    b.put32(0x2cc, address(0x2d0));                      relocate(0x2cc, 2);
    b.put32(0x2d0, ModuleStart); b.put32(0x2d4, ModuleInfoNid);
    b.put32(0x2d8, address(0x00));                       relocate(0x2d8, 2);
    b.put32(0x2dc, address(0x200));                      relocate(0x2dc, 2);
    b.putString(0x300, "sceDisplay");
    b.putString(0x310, "ThreadManForUser");
    b.at(0x400);

    segment.physical = type == 0xffa0 ? 0 : linkedAt;  //a PRX's says where the module info is, once laid out
    elf.segments.push_back(segment);
    elf.sections.push_back({".text", 1, linkedAt + 0, {}});
    elf.sections.push_back({".rodata.sceModuleInfo", 1, linkedAt + 0x200, {}});
    if(type == 0xffa0) elf.sections.push_back({".rel.text", 0x7000'00a0, 0, relocations});
  }

  //The file, with the PRX's module info offset filled in (its segment's file offset + 0x200), and, for a stripped
  //PRX, the relocations as a program header.
  auto build() -> std::vector<u8> {
    if(elf.type == 0xffa0 && !elf.withSections) {
      ElfBuilder::Segment table;
      table.type = 0x7000'00a0;
      table.bytes = relocations;
      elf.segments.push_back(table);
    }
    auto first = elf.build();
    u32 segmentOffset = first[52 + 4] | first[52 + 5] << 8 | first[52 + 6] << 16 | first[52 + 7] << 24;
    if(elf.type == 0xffa0) elf.segments[0].physical = segmentOffset + 0x200;
    return elf.build();
  }
};

//Runs the loaded test program's code from its entry, and checks the addresses it built.
static auto runCode(System& s, const Module& module, bool recompile) -> void {
  s.recompiler.enabled = recompile;
  s.power(module.entry);
  s.run(1000);
  u32 base = module.segments.empty() ? 0 : module.segments[0].address;
  CHECK(s.scc.halted, 1);
  CHECK(s.ipu.r[v0], 7);
  CHECK(s.ipu.r[s0], base + 0x9000);
  CHECK(s.ipu.r[s1], base + 0x100);
  CHECK(s.ipu.r[s2], base + 0x180);
  CHECK(s.ipu.r[s3], base + 0x5000);
  CHECK(s.ipu.r[s4], base + 0x5000);
  CHECK(s.exceptions.size(), 0);
}

static auto loadInto(System& s, const std::vector<u8>& file, u32 base, Module& module,
                     std::vector<std::pair<std::string, u32>>* asked = nullptr) -> std::string {
  return Loader::load(s.memory, file.data(), file.size(), base, [&](const std::string& library, u32 nid) -> u32 {
    if(asked) asked->push_back({library, nid});
    return 0x100 + (asked ? asked->size() : 0);
  }, module);
}

//A PRX at 0x08900000: every relocation, the module info, the imports' stubs, the exports; then its code runs.
static auto prx() -> void {
  for(bool recompile : {false, true}) {
    System s;
    TestProgram program;
    auto file = program.build();
    Module module;
    std::vector<std::pair<std::string, u32>> asked;
    constexpr u32 Base = 0x0890'0000;
    CHECK(loadInto(s, file, Base, module, &asked).empty(), true);
    CHECK(module.relocatable, true);
    CHECK(module.base, Base);
    CHECK(module.name == "TESTPRX", true);
    CHECK(module.version[0], 1);
    CHECK(module.version[1], 2);
    CHECK(module.gp, Base + 0x8000);
    CHECK(module.entry, Base);
    CHECK(module.moduleInfo, Base + 0x200);
    CHECK(module.segments.size(), 1);
    if(!module.segments.empty()) CHECK(module.segments[0].size, 0xa000);
    CHECK(s.memory.read(Allegrex::Word, Base + 0x100), Base + 0x180);
    CHECK(s.memory.readString(s.memory.read(Allegrex::Word, Base + 0x100), 16) == "relocated", true);
    CHECK(s.memory.read(Allegrex::Word, Base + 0x9000), 0);  //the zeroed part
    CHECK(s.memory.read(Allegrex::Word, Base + 0x00), jal(Base + 0x40));

    //imports: the kernel was asked for a code for each function, and each stub now passes it on
    CHECK(asked.size(), 3);
    CHECK(module.imports.size(), 3);
    if(asked.size() == 3 && module.imports.size() == 3) {
      CHECK(asked[0].first == "sceDisplay", true);
      CHECK(asked[0].second, 0x289d'82fe);
      CHECK(asked[2].first == "ThreadManForUser", true);
      CHECK(asked[2].second, 0xceadeb47);
      CHECK(module.imports[1].stub, Base + 0x2a8);
      CHECK(s.memory.read(Allegrex::Word, Base + 0x2a8), jr(ra));
      CHECK(s.memory.read(Allegrex::Word, Base + 0x2ac), syscall(0x102));
      CHECK(s.memory.read(Allegrex::Word, Base + 0x2b4), syscall(0x103));
    }
    CHECK(module.skipped.size(), 1);  //ThreadManForUser's variable

    CHECK(module.exports.size(), 2);
    if(module.exports.size() == 2) {
      CHECK(module.exports[0].library.empty(), true);
      CHECK(module.exports[0].nid, ModuleStart);
      CHECK(module.exports[0].address, Base);
      CHECK(module.exports[0].variable, false);
      CHECK(module.exports[1].nid, ModuleInfoNid);
      CHECK(module.exports[1].address, Base + 0x200);
      CHECK(module.exports[1].variable, true);
    }

    runCode(s, module, recompile);
  }
}

//The same PRX stripped of its sections: the module info is found through the first program header, and the
//relocations through their own. At 0x08804000, moving 0x5000 carries into the upper half.
static auto strippedPrx() -> void {
  for(bool recompile : {false, true}) {
    System s;
    TestProgram program(0xffa0, 0, false);
    auto file = program.build();
    Module module;
    CHECK(loadInto(s, file, 0x0880'4000, module).empty(), true);
    CHECK(module.name == "TESTPRX", true);
    CHECK(module.moduleInfo, 0x0880'4200);
    CHECK(module.gp, 0x0880'c000);
    CHECK(s.memory.read(Allegrex::Word, 0x0880'4100), 0x0880'4180);
    CHECK(module.imports.size(), 3);
    CHECK(module.exports.size(), 2);
    runCode(s, module, recompile);
  }
}

//A PRX that kept its sections but has its relocations in a program header: they're found there.
static auto relocationsInProgramHeader() -> void {
  System s;
  TestProgram program;
  program.elf.sections.pop_back();  //no relocation section
  ElfBuilder::Segment table;
  table.type = 0x7000'00a0;
  table.bytes = program.relocations;
  program.elf.segments.push_back(table);
  auto file = program.build();
  Module module;
  CHECK(loadInto(s, file, 0x0880'4000, module).empty(), true);
  CHECK(s.memory.read(Allegrex::Word, 0x0880'4100), 0x0880'4180);
  runCode(s, module, false);
}

//A static executable, linked at 0x08804000 as pspdev's are: nothing moves, the base asked for is ignored.
static auto staticExecutable() -> void {
  System s;
  TestProgram program(2, 0x0880'4000);
  auto file = program.build();
  Module module;
  CHECK(loadInto(s, file, 0x0890'0000, module).empty(), true);
  CHECK(module.relocatable, false);
  CHECK(module.base, 0);
  CHECK(module.entry, 0x0880'4000);
  CHECK(module.gp, 0x0880'c000);
  CHECK(module.moduleInfo, 0x0880'4200);
  CHECK(s.memory.read(Allegrex::Word, 0x0880'4100), 0x0880'4180);
  CHECK(s.memory.read(Allegrex::Word, 0x0880'42ac), syscall(0x100));
  CHECK(module.imports.size(), 3);
  if(!module.exports.empty()) CHECK(module.exports[0].address, 0x0880'4000);
}

//An EBOOT.PBP: the header's eight offsets, the program at the seventh.
static auto pbp() -> void {
  TestProgram program;
  auto elf = program.build();
  Bytes pbp;
  pbp.put32(0, 0x5042'5000);
  pbp.put32(4, 0x0001'0000);
  u32 offsets[8] = {40, 60, 60, 60, 60, 60, 64, 0};  //PARAM.SFO; empty icons and sound; DATA.PSP; DATA.PSAR
  offsets[7] = 64 + elf.size();
  for(u32 n = 0; n < 8; n++) pbp.put32(8 + n * 4, offsets[n]);
  pbp.putString(40, "PSF stand-in");
  for(u32 i = 0; i < elf.size(); i++) pbp.put8(64 + i, elf[i]);
  pbp.put32(64 + elf.size(), 0);  //DATA.PSAR: a word
  u64 offset = 0, length = 0;
  CHECK(Loader::programInPBP(pbp.data.data(), pbp.data.size(), offset, length), true);
  CHECK(offset, 64);
  CHECK(length, elf.size());
  System s;
  Module module;
  CHECK(Loader::load(s.memory, pbp.data.data() + offset, length, 0x0890'0000, {}, module).empty(), true);
  CHECK(module.name == "TESTPRX", true);
  pbp.put32(8 + 7 * 4, 0);  //DATA.PSAR's offset left out: the program runs to the end of the file
  CHECK(Loader::programInPBP(pbp.data.data(), pbp.data.size(), offset, length), true);
  CHECK(length, pbp.data.size() - 64);
  CHECK(Loader::programInPBP(elf.data(), elf.size(), offset, length), false);
}

//What the loader refuses, and why.
static auto refused() -> void {
  auto why = [](std::vector<u8> file, u32 base = 0x0890'0000) {
    System s;
    Module module;
    return Loader::load(s.memory, file.data(), file.size(), base, {}, module);
  };
  auto contains = [](const std::string& text, const char* part) { return text.find(part) != std::string::npos; };
  CHECK(contains(why({'~', 'P', 'S', 'P', 0, 0, 0, 0}), "encrypted"), true);
  {
    Bytes encrypted;  //a ~PSP header: the module's name and its encryption type (9: a game disc's EBOOT.BIN)
    encrypted.putString(0, "~PSP");
    encrypted.putString(0x0a, "TESTGAME");
    encrypted.put8(0x7c, 9);
    encrypted.at(0x150);
    auto message = why(encrypted.data);
    CHECK(contains(message, "module \"TESTGAME\"") && contains(message, "encryption type 9"), true);
  }
  CHECK(contains(why({1, 2, 3}), "not an ELF"), true);
  {
    TestProgram program;
    program.elf.machine = 3;  //x86
    CHECK(contains(why(program.build()), "not a MIPS"), true);
  }
  {
    TestProgram program;
    auto file = program.build();
    file.resize(60);  //cut off in the program headers
    CHECK(contains(why(file), "program headers"), true);
  }
  {
    TestProgram program;
    auto file = program.build();
    u32 at = 52 + 16;  //the first segment's file size, made too big for the file
    u32 big = file.size() * 2;
    file[at] = big; file[at + 1] = big >> 8; file[at + 2] = big >> 16; file[at + 3] = big >> 24;
    u32 memoryAt = 52 + 20;
    file[memoryAt] = big; file[memoryAt + 1] = big >> 8; file[memoryAt + 2] = big >> 16; file[memoryAt + 3] = big >> 24;
    CHECK(contains(why(file), "past the end of the file"), true);
  }
  {
    TestProgram program(2, 0x0a00'0000);  //linked past the end of RAM
    CHECK(contains(why(program.build()), "doesn't fit in memory"), true);
  }
  {
    TestProgram program;
    relocation(program.relocations, 0x2c, 7);  //GPREL16, which pspdev removes
    program.elf.sections.back().bytes = program.relocations;
    CHECK(contains(why(program.build()), "type 7"), true);
  }
  {
    TestProgram program;
    relocation(program.relocations, 0x2c, 2, 0, 5);  //a segment that isn't there
    program.elf.sections.back().bytes = program.relocations;
    CHECK(contains(why(program.build()), "segment that isn't there"), true);
  }
  {
    TestProgram program(0xffa0, 0, false);
    ElfBuilder::Segment packed;
    packed.type = 0x7000'00a1;
    packed.bytes.put32(0, 0);
    program.elf.segments.push_back(packed);
    CHECK(contains(why(program.build()), "packed relocations"), true);
  }
  {
    TestProgram program;  //sections kept, but its only relocations packed: refused all the same
    program.elf.sections.pop_back();
    ElfBuilder::Segment packed;
    packed.type = 0x7000'00a1;
    packed.bytes.put32(0, 0);
    program.elf.segments.push_back(packed);
    CHECK(contains(why(program.build()), "packed relocations"), true);
  }
  {
    TestProgram program(2, 0x0880'4000, false);  //a static executable without sections: nowhere to find module info
    CHECK(contains(why(program.build()), "no module info"), true);
  }
  {
    TestProgram program(2, 0x0880'4000);
    auto& b = program.elf.segments[0].bytes;
    b.put32(0x24c, 0x0a00'0000);  //sceDisplay's NIDs past the end of RAM
    CHECK(contains(why(program.build()), "point outside memory"), true);
  }
}

//pspdev's hello world, when PSP_TEST_PROGRAMS points at it: its static executable, its PRX, and its EBOOT.PBP.
static auto realPrograms() -> void {
  const char* folder = std::getenv("PSP_TEST_PROGRAMS");
  if(!folder || !*folder) return;
  auto read = [&](const char* name) {
    std::ifstream file(std::string(folder) + "/" + name, std::ios::binary);
    return std::vector<u8>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  };
  auto verify = [&](const std::vector<u8>& file, u32 base, bool relocatable) {
    System s;
    Module module;
    std::vector<std::pair<std::string, u32>> asked;
    CHECK(loadInto(s, file, base, module, &asked).empty(), true);
    CHECK(module.name == "HELLO", true);
    CHECK(module.relocatable, relocatable);
    CHECK(module.imports.size() >= 40, true);
    CHECK(asked.size(), module.imports.size());
    bool display = false, threads = false, start = false;
    for(auto& import : module.imports) {
      display |= import.library == "sceDisplay";
      threads |= import.library == "ThreadManForUser";
      if(import.library == "sceDisplay") CHECK(s.memory.read(Allegrex::Word, import.stub), jr(ra));
    }
    for(auto& entry : module.exports) start |= entry.nid == ModuleStart;
    CHECK(display && threads && start, true);
    CHECK(module.entry >= module.segments[0].address, true);
  };
  verify(read("hello.elf"), 0, false);
  verify(read("hello.prx"), 0x0880'4000, true);
  auto pbp = read("EBOOT.PBP");
  u64 offset = 0, length = 0;
  CHECK(Loader::programInPBP(pbp.data(), pbp.size(), offset, length), true);
  verify(std::vector<u8>(pbp.begin() + offset, pbp.begin() + offset + length), 0, false);
}

auto loaderTests() -> Tests {
  return {
    {"loader prx", prx}, {"loader stripped prx", strippedPrx}, {"loader relocations in a program header", relocationsInProgramHeader},
    {"loader static executable", staticExecutable},
    {"loader pbp", pbp}, {"loader refusals", refused}, {"loader real programs", realPrograms},
  };
}

}
