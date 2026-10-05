//Modules a program loads (ares/psp/kernel/modules.cpp): PRXs built here, plain and encrypted (encrypt.hpp), loaded
//from the memory stick and the disc by a program running on both engines: module_start and module_stop run on
//threads of their own while the caller waits, one module's imports reach the functions another exports, unloading
//sends them back to the kernel; Sony's modules are stood in for; the module IDs and information; refusals; and a
//state holding loaded modules carries on as the machine did.
#include "kernel-machine.hpp"
#include "encrypt.hpp"
#include "disc-image.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::Disc;

static constexpr u32 Results = KernelMachine::Results;
static constexpr u32 AddNID = 0x1122'3344;  //TestLib's add(a, b)

//A module as pspdev links one, at 0 with a relocation for every address in it: code from 0, the module info at
//0x200, export entries at 0x240, import entries at 0x2c0, export tables at 0x300, import NIDs at 0x380 and stubs at
//0x3c0, names at 0x400; 0x1000 bytes in memory.
struct TestModule {
  struct Library { std::string name; std::vector<std::pair<u32, u32>> functions; };  //NIDs and code offsets
  std::string name = "TESTMODULE";
  u16 attributes = 0;
  std::vector<u32> code;                              //at 0
  std::vector<u32> jumps;                             //offsets of jal words in it, whose targets move with it
  s32 start = -1, stop = -1;                          //module_start's and module_stop's offsets, if it has them
  std::vector<Library> exports;
  std::vector<std::pair<std::string, u32>> imports;   //a stub for each, at 0x3c0 + 8n

  auto build() -> std::vector<u8> {
    ElfBuilder elf;
    ElfBuilder::Segment segment;
    segment.memorySize = 0x1000;
    auto& b = segment.bytes;
    Bytes relocations;
    auto pointer = [&](u32 at, u32 offset) { b.put32(at, offset); relocation(relocations, at, 2); };
    for(u32 n = 0; n < code.size(); n++) b.put32(n * 4, code[n]);
    for(u32 at : jumps) relocation(relocations, at, 4);
    u32 names = 0x400;
    auto string = [&](const std::string& text) {
      u32 at = names;
      b.putString(at, text);
      names += (text.size() + 4) & ~3u;
      return at;
    };

    //the module's own entry (no library name): module_start and module_stop, and module_info, a variable
    u32 entry = 0x240, table = 0x300;
    std::vector<std::pair<u32, u32>> own;
    if(start >= 0) own.push_back({0xd632'acdb, u32(start)});
    if(stop >= 0) own.push_back({0xcee8'593c, u32(stop)});
    std::vector<Library> entries = {{"", own}};
    entries.insert(entries.end(), exports.begin(), exports.end());
    for(auto& library : entries) {
      bool mine = library.name.empty();
      u32 variables = mine, functions = library.functions.size(), count = functions + variables;
      if(mine) b.put32(entry, 0); else pointer(entry, string(library.name));
      b.put32(entry + 4, mine ? 0x8000'0000 : 0x0001'0000);
      b.put32(entry + 8, 4 | variables << 8 | functions << 16);
      pointer(entry + 12, table);
      for(u32 n = 0; n < functions; n++) {
        b.put32(table + n * 4, library.functions[n].first);
        pointer(table + (count + n) * 4, library.functions[n].second);
      }
      if(mine) b.put32(table + functions * 4, 0xf01d'73a7), pointer(table + (count + functions) * 4, 0x200);
      entry += 16, table += count * 8;
    }
    u32 exportsEnd = entry;

    //imports: one entry per library, in the order given
    entry = 0x2c0;
    for(u32 n = 0; n < imports.size();) {
      u32 first = n;
      while(n < imports.size() && imports[n].first == imports[first].first) n++;
      pointer(entry, string(imports[first].first));
      b.put32(entry + 4, 0x0011'0000);
      b.put32(entry + 8, 5 | (n - first) << 16);
      pointer(entry + 12, 0x380 + first * 4);
      pointer(entry + 16, 0x3c0 + first * 8);
      for(u32 i = first; i < n; i++) {
        b.put32(0x380 + i * 4, imports[i].second);
        b.put32(0x3c0 + i * 8, jr(ra));
        b.put32(0x3c0 + i * 8 + 4, nop);
      }
      entry += 20;
    }

    b.put16(0x200, attributes);
    b.put8(0x202, 1);
    b.putString(0x204, name);
    pointer(0x220, 0x800);
    pointer(0x224, 0x240);
    pointer(0x228, exportsEnd);
    pointer(0x22c, 0x2c0);
    pointer(0x230, entry);
    b.at(0x600);
    elf.entry = start >= 0 ? start : 0;
    elf.segments.push_back(segment);
    elf.sections.push_back({".text", 1, 0, {}});
    elf.sections.push_back({".rodata.sceModuleInfo", 1, 0x200, {}});
    elf.sections.push_back({".rel.text", 0x7000'00a0, 0, relocations});
    return elf.build();
  }
};

//Code that puts value at address (t9 its scratch register).
static auto store(std::vector<u32>& code, u32 address, u32 value) -> void {
  code.insert(code.end(), {lui(t9, address >> 16), ori(t9, t9, address & 0xffff), lui(t8, value >> 16),
                           ori(t8, t8, value & 0xffff), sw(t8, 0, t9)});
}

//"TESTLIB": exports TestLib's add(a, b); its module_start writes 0xabcd at Results and returns 7, its module_stop
//writes 0x5707 at Results + 4 and returns 3.
static auto libraryModule() -> TestModule {
  TestModule m;
  m.name = "TESTLIB";
  m.start = 0;
  store(m.code, Results, 0xabcd);
  m.code.insert(m.code.end(), {jr(ra), addiu(v0, zero, 7)});
  m.stop = m.code.size() * 4;
  store(m.code, Results + 4, 0x5707);
  m.code.insert(m.code.end(), {jr(ra), addiu(v0, zero, 3)});
  u32 add = m.code.size() * 4;
  m.code.insert(m.code.end(), {jr(ra), addu(v0, a0, a1)});
  m.exports = {{"TestLib", {{AddNID, add}}}};
  return m;
}

//"TESTUSER": imports TestLib's add; its module_start writes its argument's length at Results + 12 and the
//argument's first word at Results + 16, then add(40, 2) at Results + 8, and returns 0. No module_stop.
static auto userModule() -> TestModule {
  TestModule m;
  m.name = "TESTUSER";
  m.start = 0;
  m.imports = {{"TestLib", AddNID}};
  m.code = {addiu(sp, sp, -16), sw(ra, 12, sp), lui(t1, Results >> 16), ori(t1, t1, Results & 0xffff),
            sw(a0, 12, t1), lw(t0, 0, a1), sw(t0, 16, t1), addiu(a0, zero, 40)};
  m.jumps.push_back(m.code.size() * 4);
  m.code.insert(m.code.end(), {jal(0x3c0), addiu(a1, zero, 2), lui(t1, Results >> 16),
                               ori(t1, t1, Results & 0xffff), sw(v0, 8, t1), lw(ra, 12, sp), addiu(sp, sp, 16),
                               jr(ra), addiu(v0, zero, 0)});
  return m;
}

//A machine whose own areas (the test program's code, its stubs, strings and results) are handed out first, so the
//modules it loads go above them.
static auto machine(KernelMachine& m) -> void {
  m.kernel.allocate(0x13'0000, 2, 0x0880'0000, "the test's own");
}

//A file on a host folder.
static auto put(HostFolder& folder, const char* name, const std::vector<u8>& bytes) -> void {
  folder.put(name, std::string(bytes.begin(), bytes.end()));
}

static auto word(System& s, u32 address) -> u32 { return s.memory.read(Allegrex::Word, address); }

//A program, on both engines, loads TESTLIB (plain) and TESTUSER (encrypted) from the memory stick, starts the first
//(its module_start runs, and its result comes back), then the second with an argument: its import of add reaches
//TESTLIB's, and 40 + 2 comes back. It stops TESTLIB (module_stop runs) and unloads it: TESTUSER's stub goes back to
//the kernel, TESTLIB's memory to the user partition. Each module_start's thread is gone once it returns.
static auto startAndLink() -> void {
  HostFolder stick;
  put(stick, "A.PRX", libraryModule().build());
  put(stick, "B.PRX", psp_encrypt::encrypt(userModule().build()));
  for(bool recompile : {false, true}) {
    KernelMachine m;
    machine(m);
    m.kernel.mount("ms0", stick.path.string());
    Assembler main{m, 0x0880'1000};
    main.li(s0, Results);
    for(auto [path, at] : {std::pair{"ms0:/A.PRX", 0x100}, std::pair{"ms0:/B.PRX", 0x104}}) {
      main.li(a0, m.string(path)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelLoadModule");
      main.put(sw(v0, at, s0));
    }
    main.put(lw(s1, 0x100, s0));
    main.put(lw(s2, 0x104, s0));
    main.put(addu(a0, s1, zero)); main.li(a1, 0); main.li(a2, 0); main.li(a3, Results + 0x140); main.li(t0, 0);
    main.call("sceKernelStartModule");
    main.put(sw(v0, 0x108, s0));
    main.put(addu(a0, s2, zero)); main.li(a1, 4); main.li(a2, m.string("ARG")); main.li(a3, Results + 0x144);
    main.li(t0, 0);
    main.call("sceKernelStartModule");
    main.put(sw(v0, 0x10c, s0));
    main.put(addu(a0, s1, zero)); main.li(a1, 0); main.li(a2, 0); main.li(a3, Results + 0x148); main.li(t0, 0);
    main.call("sceKernelStopModule");
    main.put(sw(v0, 0x110, s0));
    main.put(addu(a0, s1, zero));
    main.call("sceKernelUnloadModule");
    main.put(sw(v0, 0x114, s0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);

    u32 a = word(m.system, Results + 0x100), b = word(m.system, Results + 0x104);
    CHECK(m.kernel.exited, true);
    CHECK(a >= 0x100 && a < 0x8000'0000 && b > a && b < 0x8000'0000, true);
    CHECK(word(m.system, Results + 0x108), a);
    CHECK(word(m.system, Results), 0xabcd);
    CHECK(word(m.system, Results + 0x140), 7);
    CHECK(word(m.system, Results + 0x10c), b);
    CHECK(word(m.system, Results + 8), 42);
    CHECK(word(m.system, Results + 12), 4);
    CHECK(word(m.system, Results + 16), 'A' | 'R' << 8 | 'G' << 16);
    CHECK(word(m.system, Results + 0x144), 0);
    CHECK(word(m.system, Results + 0x110), a);
    CHECK(word(m.system, Results + 4), 0x5707);
    CHECK(word(m.system, Results + 0x148), 3);
    CHECK(word(m.system, Results + 0x114), a);
    CHECK(m.kernel.modules.count(a), 0);
    CHECK(m.kernel.modules.count(b), 1);
    CHECK(m.kernel.threads.size(), 1);  //the program's own: each module_start's went once it returned
    if(m.kernel.modules.count(b)) {
      auto& user = m.kernel.modules[b].module;
      CHECK(user.name == "TESTUSER", true);
      CHECK(m.kernel.modules[b].status == Kernel::ModuleStatus::Started, true);
      if(user.imports.size() == 1) {  //back to the kernel: TestLib's add has gone with TESTLIB
        CHECK(word(m.system, user.imports[0].stub), jr(ra));
        CHECK(word(m.system, user.imports[0].stub + 4) & 0x3f, 0x0c);
      }
      CHECK(user.imports.size(), 1);
    }
    bool blockKept = std::any_of(m.kernel.blocks.begin(), m.kernel.blocks.end(), [](auto& block) {
      return block.name == "ms0:/A.PRX";
    });
    CHECK(blockKept, false);
  }
}

//Linking with no thread to wait in (the functions called directly): TESTUSER loaded before TESTLIB is linked once
//TESTLIB comes, its stub a jump to add; and loaded from the disc, by path and as a run of sectors.
static auto linking() -> void {
  auto image = disc_image::makeIso({
    {"PSP_GAME/USRDIR/LIB.PRX", libraryModule().build()},
    {"PSP_GAME/USRDIR/USER.PRX", psp_encrypt::encrypt(userModule().build(), {.tag = 0xc0cb'167c})},
  });
  auto disc = std::make_shared<Disc>();
  std::string error;
  auto bytes = std::make_shared<std::vector<u8>>(image.bytes);
  disc->open([bytes](u64 offset, void* out, u64 size) -> u64 {
    size = offset < bytes->size() ? std::min<u64>(size, bytes->size() - offset) : 0;
    memcpy(out, bytes->data() + offset, size);
    return size;
  }, bytes->size(), error);
  KernelMachine m;
  machine(m);
  m.kernel.disc = disc;
  u32 user = m.call("sceKernelLoadModule", {m.string("disc0:/PSP_GAME/USRDIR/USER.PRX"), 0, 0});
  CHECK(m.kernel.modules.count(user), 1);
  if(!m.kernel.modules.count(user)) return;
  u32 stub = m.kernel.modules[user].module.imports.at(0).stub;
  CHECK(word(m.system, stub), jr(ra));  //nothing exports add yet: the kernel's
  Disc::Entry entry;
  CHECK(disc->find({"PSP_GAME", "USRDIR", "LIB.PRX"}, entry), true);
  char path[64];
  std::snprintf(path, sizeof(path), "disc0:/sce_lbn0x%x_size0x%x", entry.sector, entry.size);
  u32 library = m.call("sceKernelLoadModule", {m.string(path), 0, 0});
  CHECK(m.kernel.modules.count(library), 1);
  if(!m.kernel.modules.count(library)) return;
  u32 add = 0;
  for(auto& e : m.kernel.modules[library].module.exports) if(e.nid == AddNID) add = e.address;
  CHECK(add != 0, true);
  CHECK(word(m.system, stub), j(add));
  CHECK(word(m.system, stub + 4), nop);
}

//Sony's modules, as early games carry them: encrypted and named "sce..." (here an encrypted one whose tag Phobos has
//no key for: it isn't even decrypted), a kernel module, and a plain one named "Sce...": each stood in for, an ID and
//nothing in memory; started and stopped at once (status 0), unloaded. LoadModuleByID reads one from inside a file,
//from where it's been seeked to.
static auto standIns() -> void {
  HostFolder stick;
  TestModule sas = libraryModule();
  sas.name = "sceSAScore";
  auto encrypted = psp_encrypt::encrypt(sas.build(), {.name = "sceSAScore"});
  psp_encrypt::put32(encrypted.data() + 0xd0, 0x1234'5678);  //a tag with no key
  put(stick, "SAS.PRX", encrypted);
  TestModule kernelModule = libraryModule();
  kernelModule.name = "AnyDriver";
  kernelModule.attributes = 0x1006;
  put(stick, "DRIVER.PRX", kernelModule.build());
  TestModule plain = libraryModule();
  plain.name = "SceLibrary";
  put(stick, "PLAIN.PRX", plain.build());
  //an archive holding modules after a ~SCE header each, as GTA Liberty City Stories keeps them: one of Sony's at
  //1000, and TESTLIB, encrypted, after it
  std::string wrapper(0x40, '\0');
  wrapper.replace(0, 5, "~SCE@");
  auto library = psp_encrypt::encrypt(libraryModule().build());
  auto archive = std::string(1000, 'x') + wrapper + std::string(encrypted.begin(), encrypted.end()) + wrapper +
                 std::string(library.begin(), library.end()) + "trailing";
  stick.put("ARCHIVE.BIN", archive);

  KernelMachine m;
  machine(m);
  m.kernel.mount("ms0", stick.path.string());
  u32 before = m.kernel.blocks.size();
  for(auto [path, name] : {std::pair{"ms0:/SAS.PRX", "sceSAScore"}, std::pair{"ms0:/DRIVER.PRX", "AnyDriver"},
                           std::pair{"ms0:/PLAIN.PRX", "SceLibrary"}}) {
    u32 uid = m.call("sceKernelLoadModule", {m.string(path), 0, 0});
    CHECK(m.kernel.modules.count(uid), 1);
    if(!m.kernel.modules.count(uid)) continue;
    auto& loaded = m.kernel.modules[uid];
    CHECK(loaded.standIn && loaded.module.name == name && !loaded.block, true);
    CHECK(m.kernel.blocks.size(), before);
    m.system.memory.write(Allegrex::Word, Results, 0x1111);
    CHECK(m.call("sceKernelStartModule", {uid, 0, 0, Results, 0}), uid);
    CHECK(word(m.system, Results), 0);
    CHECK(m.call("sceKernelStopModule", {uid, 0, 0, 0, 0}), uid);
    CHECK(m.call("sceKernelUnloadModule", {uid}), uid);
    CHECK(m.kernel.modules.count(uid), 0);
  }
  u32 file = m.call("sceIoOpen", {m.string("ms0:/ARCHIVE.BIN"), 0x0001, 0});
  m.call("sceIoLseek32", {file, 1000, 0});
  u32 uid = m.call("sceKernelLoadModuleByID", {file, 0, 0});
  CHECK(m.kernel.modules.count(uid) && m.kernel.modules[uid].standIn, true);
  CHECK(m.kernel.modules.count(uid) && m.kernel.modules[uid].module.name == "sceSAScore", true);
  m.call("sceIoLseek32", {file, u32(1000 + 0x40 + encrypted.size()), 0});
  uid = m.call("sceKernelLoadModuleByID", {file, 0, 0});
  CHECK(m.kernel.modules.count(uid) && !m.kernel.modules[uid].standIn, true);
  CHECK(m.kernel.modules.count(uid) && m.kernel.modules[uid].module.name == "TESTLIB", true);
}

//The module IDs: by address (the program's own, a module's, nowhere), the caller's (from where it called), the list,
//and a module's information.
static auto identities() -> void {
  HostFolder stick;
  auto bytes = libraryModule().build();
  stick.put("A.PRX", std::string(bytes.begin(), bytes.end()));
  KernelMachine m;
  machine(m);
  m.kernel.mount("ms0", stick.path.string());
  m.kernel.programUID = m.kernel.newUID();
  m.kernel.module.name = "PROGRAM";
  m.kernel.module.segments = {{0x0880'0000, 0x1000}};
  u32 a = m.call("sceKernelLoadModule", {m.string("ms0:/A.PRX"), 0, 0});
  CHECK(m.kernel.modules.count(a), 1);
  if(!m.kernel.modules.count(a)) return;
  auto& library = m.kernel.modules[a].module;
  u32 base = library.segments.at(0).address;
  CHECK(m.call("sceKernelGetModuleIdByAddress", {base + 0x10}), a);
  CHECK(m.call("sceKernelGetModuleIdByAddress", {0x4880'0010}), m.kernel.programUID);  //uncached, the same
  CHECK(m.call("sceKernelGetModuleIdByAddress", {0x0900'0000}), Kernel::ErrorUnknownModule);
  m.system.ipu.r[ra] = base + 0x20;
  CHECK(m.call("sceKernelGetModuleId", {}), a);
  m.system.ipu.r[ra] = 0x0880'0100;
  CHECK(m.call("sceKernelGetModuleId", {}), m.kernel.programUID);
  CHECK(m.call("sceKernelGetModuleIdList", {Results, 8, Results + 8}), 0);
  CHECK(word(m.system, Results), m.kernel.programUID);
  CHECK(word(m.system, Results + 4), a);
  CHECK(word(m.system, Results + 8), 2);
  m.system.memory.write(Allegrex::Word, Results, 96);
  CHECK(m.call("sceKernelQueryModuleInfo", {a, Results}), 0);
  CHECK(m.system.memory.read(Allegrex::Byte, Results + 4), 1);
  CHECK(word(m.system, Results + 8), base);
  CHECK(word(m.system, Results + 24), 0x1000);
  CHECK(word(m.system, Results + 44), library.gp);
  CHECK(m.system.memory.readString(Results + 68, 28) == "TESTLIB", true);
  m.system.memory.write(Allegrex::Word, Results, 8);  //only as far as its size says
  m.system.memory.write(Allegrex::Word, Results + 8, 0x7777);
  CHECK(m.call("sceKernelQueryModuleInfo", {a, Results}), 0);
  CHECK(word(m.system, Results + 8), 0x7777);
  CHECK(m.call("sceKernelQueryModuleInfo", {0x7777, Results}), Kernel::ErrorUnknownModule);
}

//Refused: a file that isn't there, a folder, what isn't a module, an encrypted module that can't be decrypted, an
//unknown ID, starting twice, stopping what isn't started or is stopped already, unloading a started module, a
//status out of memory, LoadModuleByID on what isn't a file. And a module that can't fit leaves nothing behind.
//(Called with no thread to wait in, module_start runs once the kernel runs.)
static auto refusals() -> void {
  HostFolder stick;
  stick.put("TEXT.PRX", "not a module at all");
  put(stick, "A.PRX", libraryModule().build());
  auto encrypted = psp_encrypt::encrypt(libraryModule().build());
  psp_encrypt::put32(encrypted.data() + 0xd0, 0x1234'5678);
  put(stick, "LOCKED.PRX", encrypted);
  stick.put("DIR/X", "x");
  KernelMachine m;
  machine(m);
  m.kernel.mount("ms0", stick.path.string());
  auto load = [&](const char* path) { return m.call("sceKernelLoadModule", {m.string(path), 0, 0}); };
  CHECK(load("ms0:/NONE.PRX"), Kernel::ErrorFileNotFound);
  CHECK(load("ms0:/DIR"), Kernel::ErrorIsDirectory);
  CHECK(load("ms0:/TEXT.PRX"), Kernel::ErrorIllegalObject);
  CHECK(load("ms0:/LOCKED.PRX"), Kernel::ErrorUnsupportedPrxType);
  CHECK(m.kernel.modules.empty(), true);
  CHECK(m.call("sceKernelStartModule", {0x7777, 0, 0, 0, 0}), Kernel::ErrorUnknownModule);
  CHECK(m.call("sceKernelUnloadModule", {0x7777}), Kernel::ErrorUnknownModule);
  CHECK(m.call("sceKernelLoadModuleByID", {0x7777, 0, 0}), Kernel::ErrorBadFile);
  u32 a = load("ms0:/A.PRX");
  CHECK(m.kernel.modules.count(a), 1);
  if(!m.kernel.modules.count(a)) return;
  auto status = [&] { return m.kernel.modules[a].status; };
  CHECK(m.call("sceKernelStopModule", {a, 0, 0, 0, 0}), Kernel::ErrorNotStarted);
  CHECK(m.call("sceKernelStartModule", {a, 0, 0, 0x0a00'0000, 0}), Kernel::ErrorIllegalAddress);
  m.call("sceKernelStartModule", {a, 0, 0, 0, 0});
  CHECK(status() == Kernel::ModuleStatus::Starting, true);
  CHECK(m.call("sceKernelStartModule", {a, 0, 0, 0, 0}), Kernel::ErrorAlreadyStarted);
  CHECK(m.call("sceKernelUnloadModule", {a}), Kernel::ErrorNotStopped);
  m.kernel.run(Kernel::VblankCycles);
  CHECK(status() == Kernel::ModuleStatus::Started && word(m.system, Results) == 0xabcd, true);
  CHECK(m.call("sceKernelUnloadModule", {a}), Kernel::ErrorNotStopped);
  m.call("sceKernelStopModule", {a, 0, 0, 0, 0});
  m.kernel.run(Kernel::VblankCycles);
  CHECK(status() == Kernel::ModuleStatus::Stopped && word(m.system, Results + 4) == 0x5707, true);
  CHECK(m.call("sceKernelStopModule", {a, 0, 0, 0, 0}), Kernel::ErrorAlreadyStopped);
  CHECK(m.call("sceKernelUnloadModule", {a}), a);
  CHECK(m.kernel.threads.empty(), true);  //module_start's and module_stop's threads gone

  //no room: the block it would take can't be had, and nothing is left of it
  u32 left = m.kernel.largestFree();
  m.kernel.allocate(left, 0, 0, "everything");
  u32 blocks = m.kernel.blocks.size();
  CHECK(load("ms0:/A.PRX"), Kernel::ErrorNoMemory);
  CHECK(m.kernel.blocks.size() == blocks && m.kernel.modules.empty(), true);
}

auto moduleTests() -> Tests {
  return {
    {"modules start and link", startAndLink}, {"modules linking", linking}, {"modules stand-ins", standIns},
    {"modules identities", identities}, {"modules refusals", refusals},
  };
}

}
