//Modules a program loads (ares/psp/kernel/modules.cpp): PRXs built here, plain and encrypted (encrypt.hpp), loaded
//from the memory stick and the disc by a program running on both engines: module_start and module_stop run on
//threads of their own while the caller waits (however they end, terminated too, their threads go), one module's
//imports reach the functions another (or the program) exports, unloading sends them back to the kernel; a module
//unloads itself, either way the SDKs have it; what runs as module_start, and on what thread; Sony's modules are
//stood in for; the module IDs and information; refusals, and sizes a damaged header claims; and a state saved while
//a module_start runs carries on as the machine did.
#include "kernel-machine.hpp"
#include "encrypt.hpp"
#include "disc-image.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::Disc;

static constexpr u32 Results = KernelMachine::Results;
static constexpr u32 AddNID = 0x1122'3344;      //TestLib's add(a, b)
static constexpr u32 QuitNID = 0x5e1f'0001;     //SelfLib's quit(), and StatusLib's
static constexpr u32 StopUnloadSelfWithStatusNID = 0x8f2d'f740;  //not its name's hash (kernel.cpp's addNID)
static constexpr u32 ProgramNID = 0x9a0c'0001;  //a function the program exports in ProgLib
//the variables a module exports for itself that set its module_start's and its module_stop's threads (the names'
//NIDs: module_start_thread_parameter, module_stop_thread_parameter)
static constexpr u32 StartParameters = 0x0f7c'276c, StopParameters = 0xcf0c'c697;

//A module as pspdev links one, at 0 with a relocation for every address in it: code from 0, the module info at
//0x200, export entries at 0x240, import entries at 0x2c0, export tables at 0x300, import NIDs at 0x380 and stubs at
//0x3c0, names at 0x400, any more words from 0x500; 0x1000 bytes in memory.
struct TestModule {
  struct Library { std::string name; std::vector<std::pair<u32, u32>> functions; };  //NIDs and code offsets
  std::string name = "TESTMODULE";
  u16 attributes = 0;
  std::vector<u32> code;                              //at 0
  std::vector<u32> jumps;                             //offsets of jal words in it, whose targets move with it
  s32 start = -1, stop = -1;                          //module_start's and module_stop's offsets, if it has them
  s32 entryPoint = -1;                                //the ELF header's entry, if not module_start's offset (or 0)
  std::vector<std::pair<u32, u32>> variables;         //what it exports for itself besides module_info: NIDs, offsets
  std::map<u32, u32> words;                           //more words, by offset (from 0x500)
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
    for(auto [at, word] : words) b.put32(at, word);
    u32 names = 0x400;
    auto string = [&](const std::string& text) {
      u32 at = names;
      b.putString(at, text);
      names += (text.size() + 4) & ~3u;
      return at;
    };

    //the module's own entry (no library name): module_start and module_stop, and its variables, module_info first
    u32 entry = 0x240, table = 0x300;
    std::vector<std::pair<u32, u32>> own, ownVariables = {{0xf01d'73a7, 0x200}};
    if(start >= 0) own.push_back({0xd632'acdb, u32(start)});
    if(stop >= 0) own.push_back({0xcee8'593c, u32(stop)});
    ownVariables.insert(ownVariables.end(), variables.begin(), variables.end());
    std::vector<Library> entries = {{"", own}};
    entries.insert(entries.end(), exports.begin(), exports.end());
    for(auto& library : entries) {
      bool mine = library.name.empty();
      u32 functions = library.functions.size(), count = functions + (mine ? ownVariables.size() : 0);
      if(mine) b.put32(entry, 0); else pointer(entry, string(library.name));
      b.put32(entry + 4, mine ? 0x8000'0000 : 0x0001'0000);
      b.put32(entry + 8, 4 | (count - functions) << 8 | functions << 16);
      pointer(entry + 12, table);
      for(u32 n = 0; n < functions; n++) {
        b.put32(table + n * 4, library.functions[n].first);
        pointer(table + (count + n) * 4, library.functions[n].second);
      }
      for(u32 n = functions; n < count; n++) {  //its own variables, after the functions
        b.put32(table + n * 4, ownVariables[n - functions].first);
        pointer(table + (count + n) * 4, ownVariables[n - functions].second);
      }
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
    elf.entry = entryPoint >= 0 ? entryPoint : start >= 0 ? start : 0;
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

//Code that calls the function whose stub is at offset stub, with delaySlot after the jal.
static auto callStub(TestModule& m, u32 stub, u32 delaySlot = nop) -> void {
  m.jumps.push_back(m.code.size() * 4);
  m.code.insert(m.code.end(), {jal(stub), delaySlot});
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

//"TESTSELF": SelfLib's quit() unloads the module it's in, sceKernelSelfStopUnloadModule(1, 4, Results + 0x30), and
//if that's refused writes what it said at Results + 0x34 and returns. module_stop writes 0x5709 at Results + 0x20,
//the length of its argument at Results + 0x24 and the argument's first word at Results + 0x28. module_start returns
//at once, or is quit() itself, or sleeps for good.
enum class SelfStart { Returns, Quits, Sleeps };
static auto selfModule(SelfStart how) -> TestModule {
  TestModule m;
  m.name = "TESTSELF";
  m.imports = {{"ModuleMgrForUser", Kernel::nid("sceKernelSelfStopUnloadModule")},
               {"ThreadManForUser", Kernel::nid("sceKernelSleepThread")}};
  m.code = {addiu(sp, sp, -16), sw(ra, 12, sp), addiu(a0, zero, 1), addiu(a1, zero, 4),
            lui(a2, (Results + 0x30) >> 16)};
  callStub(m, 0x3c0, ori(a2, a2, (Results + 0x30) & 0xffff));
  m.code.insert(m.code.end(), {lui(t1, Results >> 16), ori(t1, t1, Results & 0xffff), sw(v0, 0x34, t1),
                               lw(ra, 12, sp), jr(ra), addiu(sp, sp, 16)});
  m.exports = {{"SelfLib", {{QuitNID, 0}}}};
  m.stop = m.code.size() * 4;
  store(m.code, Results + 0x20, 0x5709);
  m.code.insert(m.code.end(), {sw(a0, 4, t9), lw(t0, 0, a1), sw(t0, 8, t9), jr(ra), addiu(v0, zero, 0)});
  if(how == SelfStart::Quits) m.start = 0;
  if(how == SelfStart::Returns) {
    m.start = m.code.size() * 4;
    m.code.insert(m.code.end(), {jr(ra), addiu(v0, zero, 0)});
  }
  if(how == SelfStart::Sleeps) {
    m.start = m.code.size() * 4;
    callStub(m, 0x3c8);
    m.code.push_back(halt);  //never woken
  }
  return m;
}

//"TESTSTATUS": StatusLib's quit() unloads the module it's in as later SDKs do,
//sceKernelStopUnloadSelfModuleWithStatus(1, 4, Results + 0x30, Results + 0x40, options), and if that's refused
//writes what it said at Results + 0x34 and returns. module_stop writes 0x5709 at Results + 0x20, the length of its
//argument at Results + 0x24 and the argument's first word at Results + 0x28, then has its own thread's information
//put at info (sceKernelReferThreadStatus). module_start returns at once.
static auto statusModule(u32 options, u32 info) -> TestModule {
  TestModule m;
  m.name = "TESTSTATUS";
  m.imports = {{"ModuleMgrForUser", StopUnloadSelfWithStatusNID},
               {"ThreadManForUser", Kernel::nid("sceKernelReferThreadStatus")}};
  m.code = {addiu(sp, sp, -16), sw(ra, 12, sp), addiu(a0, zero, 1), addiu(a1, zero, 4),
            lui(a2, (Results + 0x30) >> 16), ori(a2, a2, (Results + 0x30) & 0xffff),
            lui(a3, (Results + 0x40) >> 16), ori(a3, a3, (Results + 0x40) & 0xffff), lui(t0, options >> 16)};
  callStub(m, 0x3c0, ori(t0, t0, options & 0xffff));
  m.code.insert(m.code.end(), {lui(t1, Results >> 16), ori(t1, t1, Results & 0xffff), sw(v0, 0x34, t1),
                               lw(ra, 12, sp), jr(ra), addiu(sp, sp, 16)});
  m.exports = {{"StatusLib", {{QuitNID, 0}}}};
  m.stop = m.code.size() * 4;
  m.code.insert(m.code.end(), {addiu(sp, sp, -16), sw(ra, 12, sp)});
  store(m.code, Results + 0x20, 0x5709);
  m.code.insert(m.code.end(), {sw(a0, 4, t9), lw(t0, 0, a1), sw(t0, 8, t9), addiu(a0, zero, 0),
                               lui(a1, info >> 16)});
  callStub(m, 0x3c8, ori(a1, a1, info & 0xffff));
  m.code.insert(m.code.end(), {lw(ra, 12, sp), addiu(sp, sp, 16), jr(ra), addiu(v0, zero, 0)});
  m.start = m.code.size() * 4;
  m.code.insert(m.code.end(), {jr(ra), addiu(v0, zero, 0)});
  return m;
}

//"TESTEXIT": module_start writes 0xe1 at Results + 0x50 and ends with sceKernelExitThread(5) rather than returning;
//module_stop writes 0xe2 at Results + 0x54 and ends with sceKernelExitThread(6).
static auto exitModule() -> TestModule {
  TestModule m;
  m.name = "TESTEXIT";
  m.imports = {{"ThreadManForUser", Kernel::nid("sceKernelExitThread")}};
  m.start = 0;
  store(m.code, Results + 0x50, 0xe1);
  callStub(m, 0x3c0, addiu(a0, zero, 5));
  m.code.push_back(halt);  //never reached
  m.stop = m.code.size() * 4;
  store(m.code, Results + 0x54, 0xe2);
  callStub(m, 0x3c0, addiu(a0, zero, 6));
  m.code.push_back(halt);
  return m;
}

//"TESTDELAY": module_start writes 0xa1 at Results + 0x80, waits 10 milliseconds (sceKernelDelayThread), writes 0xb2
//at Results + 0x84 and returns 9.
static auto delayModule() -> TestModule {
  TestModule m;
  m.name = "TESTDELAY";
  m.imports = {{"ThreadManForUser", Kernel::nid("sceKernelDelayThread")}};
  m.start = 0;
  m.code = {addiu(sp, sp, -16), sw(ra, 12, sp)};
  store(m.code, Results + 0x80, 0xa1);
  callStub(m, 0x3c0, addiu(a0, zero, 10000));
  store(m.code, Results + 0x84, 0xb2);
  m.code.insert(m.code.end(), {lw(ra, 12, sp), addiu(sp, sp, 16), jr(ra), addiu(v0, zero, 9)});
  return m;
}

//"TESTLIVES": its module_start starts a thread of its own, "lives" (priority 0x30), and returns. The thread calls
//ProgLib's function (the program's) once, writing what it gave at Results + 0x68, then counts at Results + 0x64,
//once a millisecond (sceKernelDelayThread), for good.
static auto livesModule() -> TestModule {
  TestModule m;
  m.name = "TESTLIVES";
  m.imports = {{"ThreadManForUser", Kernel::nid("sceKernelCreateThread")},
               {"ThreadManForUser", Kernel::nid("sceKernelStartThread")},
               {"ThreadManForUser", Kernel::nid("sceKernelDelayThread")}, {"ProgLib", ProgramNID}};
  m.words = {{0x500, 'l' | 'i' << 8 | 'v' << 16 | 'e' << 24}, {0x504, 's'}};
  m.start = 0;
  //where it is, found by a branch that links (ra: the word after its delay slot), as nothing here is relocated
  m.code = {addiu(sp, sp, -16), sw(ra, 12, sp), bgezal(zero, 1), nop};
  u32 here = m.code.size() * 4, loopAt = m.code.size();  //(the loop's offset, put in below)
  m.code.insert(m.code.end(), {addiu(a1, ra, 0), addiu(a0, ra, 0x500 - here), addiu(a2, zero, 0x30),
                               addiu(a3, zero, 0x1000), addiu(t0, zero, 0), addiu(t1, zero, 0)});
  callStub(m, 0x3c0);
  m.code.insert(m.code.end(), {addu(a0, v0, zero), addiu(a1, zero, 0)});
  callStub(m, 0x3c8, addiu(a2, zero, 0));
  m.code.insert(m.code.end(), {lw(ra, 12, sp), addiu(sp, sp, 16), jr(ra), addiu(v0, zero, 0)});
  u32 loop = m.code.size() * 4;
  m.code[loopAt] = addiu(a1, ra, loop - here);
  callStub(m, 0x3d8);
  m.code.insert(m.code.end(), {lui(s0, Results >> 16), ori(s0, s0, Results & 0xffff), sw(v0, 0x68, s0)});
  u32 count = m.code.size();
  m.code.insert(m.code.end(), {lw(t0, 0x64, s0), addiu(t0, t0, 1), sw(t0, 0x64, s0)});
  callStub(m, 0x3d0, addiu(a0, zero, 1000));
  m.code.push_back(beq(zero, zero, s32(count) - s32(m.code.size()) - 1));
  m.code.push_back(nop);
  return m;
}

//"TESTBOOT", a program that boots another, as Killzone: Liberation's does: it loads and starts the module at path
//(its ID at Results + 0x60), then stops and unloads itself as later SDKs do,
//sceKernelStopUnloadSelfModuleWithStatus(1, 4, Results + 0x30, 0, 0), writing what it said at Results + 0x34 should that be refused. Its module_stop writes
//0x5709 at Results + 0x20, its argument's length and first word at Results + 0x24 and 0x28, waits 10 milliseconds
//and writes 0x570b at Results + 0x2c. It exports ProgLib's function, which returns 0x77.
static auto bootModule(u32 path) -> TestModule {
  TestModule m;
  m.name = "TESTBOOT";
  m.imports = {{"ModuleMgrForUser", Kernel::nid("sceKernelLoadModule")},
               {"ModuleMgrForUser", Kernel::nid("sceKernelStartModule")},
               {"ModuleMgrForUser", StopUnloadSelfWithStatusNID},
               {"ThreadManForUser", Kernel::nid("sceKernelDelayThread")}};
  m.start = 0;
  m.code = {lui(a0, path >> 16), ori(a0, a0, path & 0xffff), addiu(a1, zero, 0)};
  callStub(m, 0x3c0, addiu(a2, zero, 0));
  m.code.insert(m.code.end(), {lui(s0, Results >> 16), ori(s0, s0, Results & 0xffff), sw(v0, 0x60, s0),
                               addu(a0, v0, zero), addiu(a1, zero, 0), addiu(a2, zero, 0), addiu(a3, zero, 0)});
  callStub(m, 0x3c8, addiu(t0, zero, 0));
  m.code.insert(m.code.end(), {addiu(a0, zero, 1), addiu(a1, zero, 4), lui(a2, (Results + 0x30) >> 16),
                               ori(a2, a2, (Results + 0x30) & 0xffff), addiu(a3, zero, 0)});
  callStub(m, 0x3d0, addiu(t0, zero, 0));
  m.code.insert(m.code.end(), {sw(v0, 0x34, s0), beq(zero, zero, -1), nop});
  m.stop = m.code.size() * 4;
  m.code.insert(m.code.end(), {addiu(sp, sp, -16), sw(ra, 12, sp)});
  store(m.code, Results + 0x20, 0x5709);
  m.code.insert(m.code.end(), {sw(a0, 4, t9), lw(t0, 0, a1), sw(t0, 8, t9)});
  callStub(m, 0x3d8, addiu(a0, zero, 10000));
  store(m.code, Results + 0x2c, 0x570b);
  m.code.insert(m.code.end(), {lw(ra, 12, sp), addiu(sp, sp, 16), jr(ra), addiu(v0, zero, 0)});
  u32 function = m.code.size() * 4;
  m.code.insert(m.code.end(), {jr(ra), addiu(v0, zero, 0x77)});
  m.exports = {{"ProgLib", {{ProgramNID, function}}}};
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
//TESTLIB comes, its stub a jump to add; loaded from the disc, by path and as a run of sectors. And a module that
//imports from a library the program exports is linked to it.
static auto linking() -> void {
  TestModule programUser;
  programUser.name = "TESTPROGRAMUSER";
  programUser.imports = {{"ProgLib", ProgramNID}};
  programUser.code = {jr(ra), nop};
  auto image = disc_image::makeIso({
    {"PSP_GAME/USRDIR/LIB.PRX", libraryModule().build()},
    {"PSP_GAME/USRDIR/USER.PRX", psp_encrypt::encrypt(userModule().build(), {.tag = 0xc0cb'167c})},
    {"PSP_GAME/USRDIR/PROGUSER.PRX", programUser.build()},
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

  m.kernel.module.exports = {{"ProgLib", ProgramNID, 0x0880'2000, false}};
  u32 user2 = m.call("sceKernelLoadModule", {m.string("disc0:/PSP_GAME/USRDIR/PROGUSER.PRX"), 0, 0});
  CHECK(m.kernel.modules.count(user2), 1);
  if(!m.kernel.modules.count(user2)) return;
  stub = m.kernel.modules[user2].module.imports.at(0).stub;
  CHECK(word(m.system, stub), j(0x0880'2000));
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

//What a damaged ~PSP header claims costs nothing. sceKernelLoadModuleByID refuses a module said to be shorter than
//its ~PSP header, or longer than 64 MiB (the PSP's memory), however short the file. Reading an open file asks the
//host for no more than the file has left from where it's at (a 4 KiB file asked for 16 MiB never has room made for
//more), and for 64 MiB at most.
static auto sizes() -> void {
  HostFolder stick;
  auto file = psp_encrypt::encrypt(libraryModule().build());
  file.resize(4096, 0);
  KernelMachine m;
  machine(m);
  m.kernel.mount("ms0", stick.path.string());
  u32 blocks = m.kernel.blocks.size();
  for(u32 claimed : {0x14fu, u32(64_MiB) + 1, 0xffff'fff0u}) {
    psp_encrypt::put32(file.data() + 0x2c, claimed);
    put(stick, "CLAIMS.PRX", file);
    u32 open = m.call("sceIoOpen", {m.string("ms0:/CLAIMS.PRX"), 0x0001, 0});
    CHECK(m.call("sceKernelLoadModuleByID", {open, 0, 0}), Kernel::ErrorIllegalObject);
    CHECK(m.call("sceIoClose", {open}), 0);
  }
  CHECK(m.kernel.modules.empty() && m.kernel.blocks.size() == blocks, true);

  stick.put("SMALL.BIN", std::string(4096, 's'));
  u32 small = m.call("sceIoOpen", {m.string("ms0:/SMALL.BIN"), 0x0001, 0});
  m.call("sceIoLseek32", {small, 96, 0});
  std::vector<u8> data;
  CHECK(m.kernel.readOpenFile(m.kernel.files[small], 16_MiB, data), 0);
  CHECK(data.size(), 4000);
  CHECK(data.capacity() <= 4096, true);
  CHECK(m.kernel.files[small].position, 4096);
  stick.put("HUGE.BIN", "h");
  std::filesystem::resize_file(stick.path / "HUGE.BIN", 64_MiB + 4096);  //nothing written: it takes no room
  u32 huge = m.call("sceIoOpen", {m.string("ms0:/HUGE.BIN"), 0x0001, 0});
  CHECK(m.kernel.readOpenFile(m.kernel.files[huge], 128_MiB, data), 0);
  CHECK(data.size(), 64_MiB);
  CHECK(data.size() && data[0] == 'h', true);
}

//A module unloads itself (sceKernelSelfStopUnloadModule): the thread that asked ends, and is deleted, those waiting
//for it told how it ended; the module's module_stop runs with the argument given, on a thread of its own; then the
//module goes, its memory back to the user partition, and the program runs on. From its module_start too: the thread
//that started it gets its ID, and the exit status as module_start's result. Refused while another thread runs its
//module_start. The program itself calling it goes as any module does; code no module holds is refused.
static auto unloadThemselves() -> void {
  HostFolder stick;
  put(stick, "SELF.PRX", selfModule(SelfStart::Returns).build());
  put(stick, "QUITS.PRX", selfModule(SelfStart::Quits).build());
  put(stick, "SLEEPS.PRX", selfModule(SelfStart::Sleeps).build());
  auto quit = [](KernelMachine& m, u32 uid) {
    u32 address = 0;
    for(auto& e : m.kernel.modules[uid].module.exports) if(e.nid == QuitNID) address = e.address;
    return address;
  };
  auto kept = [](KernelMachine& m, const char* path) {
    return std::any_of(m.kernel.blocks.begin(), m.kernel.blocks.end(), [&](auto& b) { return b.name == path; });
  };

  //a thread calls quit(), and another waits for it to end
  for(bool recompile : {false, true}) {
    KernelMachine m;
    machine(m);
    m.system.recompiler.enabled = recompile;
    m.kernel.mount("ms0", stick.path.string());
    u32 uid = m.call("sceKernelLoadModule", {m.string("ms0:/SELF.PRX"), 0, 0});
    CHECK(m.kernel.modules.count(uid), 1);
    if(!m.kernel.modules.count(uid)) continue;
    m.call("sceKernelStartModule", {uid, 0, 0, 0, 0});
    m.kernel.run(Kernel::VblankCycles);
    CHECK(m.kernel.modules[uid].status == Kernel::ModuleStatus::Started, true);
    m.system.memory.write(Allegrex::Word, Results + 0x30, 0x4152'4731);
    s32 quitter = m.kernel.createThread("quitter", quit(m, uid), 0x20, 0x1000, 0, 0);
    Assembler waiter{m, 0x0880'1000};
    waiter.li(a0, quitter); waiter.li(a1, 0);
    waiter.call("sceKernelWaitThreadEnd");
    waiter.li(t0, Results + 0x38);
    waiter.put(sw(v0, 0, t0));
    waiter.call("sceKernelSleepThread");
    s32 waits = m.kernel.createThread("waiter", 0x0880'1000, 0x10, 0x1000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[waits], 0, 0);
    m.kernel.startThread(*m.kernel.threads[quitter], 0, 0);
    m.kernel.run(Kernel::VblankCycles);
    CHECK(m.kernel.exited, false);
    CHECK(m.kernel.modules.count(uid), 0);
    CHECK(kept(m, "ms0:/SELF.PRX"), false);
    CHECK(word(m.system, Results + 0x20), 0x5709);
    CHECK(word(m.system, Results + 0x24), 4);
    CHECK(word(m.system, Results + 0x28), 0x4152'4731);
    CHECK(word(m.system, Results + 0x38), 1);
    CHECK(m.kernel.threads.count(quitter), 0);
    CHECK(m.kernel.threads.size(), 1);  //the waiter, asleep: quit()'s thread and module_stop's are gone
  }

  //module_start unloads its own module
  {
    KernelMachine m;
    machine(m);
    m.kernel.mount("ms0", stick.path.string());
    u32 uid = m.call("sceKernelLoadModule", {m.string("ms0:/QUITS.PRX"), 0, 0});
    Assembler main{m, 0x0880'1000};
    main.li(s0, Results);
    main.li(a0, uid); main.li(a1, 0); main.li(a2, 0); main.li(a3, Results + 0x40); main.li(t0, 0);
    main.call("sceKernelStartModule");
    main.put(sw(v0, 0x44, s0));
    main.call("sceKernelSleepThread");
    m.system.memory.write(Allegrex::Word, Results + 0x30, 0x0bad'cafe);
    m.runProgram(0x0880'1000, false);
    CHECK(m.kernel.exited, false);
    CHECK(word(m.system, Results + 0x44), uid);
    CHECK(word(m.system, Results + 0x40), 1);
    CHECK(m.kernel.modules.count(uid), 0);
    CHECK(kept(m, "ms0:/QUITS.PRX"), false);
    CHECK(word(m.system, Results + 0x20), 0x5709);
    CHECK(word(m.system, Results + 0x28), 0x0bad'cafe);
    CHECK(m.kernel.threads.size(), 1);  //the program's own
  }

  //refused while module_start runs on another thread (asleep here): quit() returns, saying so
  {
    KernelMachine m;
    machine(m);
    m.kernel.mount("ms0", stick.path.string());
    u32 uid = m.call("sceKernelLoadModule", {m.string("ms0:/SLEEPS.PRX"), 0, 0});
    m.call("sceKernelStartModule", {uid, 0, 0, 0, 0});
    m.kernel.run(Kernel::VblankCycles);
    s32 quitter = m.kernel.createThread("quitter", quit(m, uid), 0x20, 0x1000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[quitter], 0, 0);
    m.kernel.run(Kernel::VblankCycles);
    CHECK(m.kernel.exited, false);
    CHECK(word(m.system, Results + 0x34), Kernel::ErrorNotStopped);
    CHECK(m.kernel.modules.count(uid) && m.kernel.modules[uid].status == Kernel::ModuleStatus::Starting, true);
  }

  //the program itself, which goes as any module would (programUID 0 from then on), its thread with it; and code no
  //module holds, which is refused, and runs on
  for(bool program : {false, true}) {
    KernelMachine m;
    u32 uid = 0;
    if(program) {
      uid = m.kernel.programUID = m.kernel.newUID();
      m.kernel.module.segments = {{0x0880'1000, 0x1000}};
    }
    Assembler main{m, 0x0880'1000};
    main.li(a0, 0); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelSelfStopUnloadModule");
    main.li(t0, Results);
    main.put(sw(v0, 0, t0));
    main.call("sceKernelExitGame");
    m.system.memory.write(Allegrex::Word, Results, 0x1234);
    m.runProgram(0x0880'1000, false);
    CHECK(m.kernel.exited, !program);
    CHECK(word(m.system, Results), program ? 0x1234 : Kernel::ErrorCanNotStop);
    CHECK(m.kernel.programUID, 0);
    CHECK(m.kernel.modules.count(uid), 0);
    CHECK(m.kernel.threads.size(), program ? 0 : 1);
  }
}

//A module unloads itself as later SDKs do (sceKernelStopUnloadSelfModuleWithStatus, known by its NID alone): as with
//sceKernelSelfStopUnloadModule, the thread that asked ends and is deleted, the one waiting for it given its exit
//status; module_stop runs with the argument, on a thread the options (the fifth argument) make; the module and its
//memory go, and the program runs on to its own end. The program itself calling it goes as a module, and code no
//module holds is refused.
static auto unloadWithStatus() -> void {
  constexpr u32 Options = Results + 0x180, Info = Results + 0x1a0;
  HostFolder stick;
  put(stick, "STATUS.PRX", statusModule(Options, Info).build());
  for(bool recompile : {false, true}) {
    KernelMachine m;
    machine(m);
    m.system.recompiler.enabled = recompile;
    m.kernel.mount("ms0", stick.path.string());
    u32 uid = m.call("sceKernelLoadModule", {m.string("ms0:/STATUS.PRX"), 0, 0});
    CHECK(m.kernel.modules.count(uid), 1);
    if(!m.kernel.modules.count(uid)) continue;
    m.call("sceKernelStartModule", {uid, 0, 0, 0, 0});
    m.kernel.run(Kernel::VblankCycles);
    CHECK(m.kernel.modules[uid].status == Kernel::ModuleStatus::Started, true);
    m.system.memory.write(Allegrex::Word, Results + 0x30, 0x4152'4731);
    u32 at = Options;  //SceKernelSMOption: its size, a partition, then the thread's stack size, priority, attributes
    for(u32 value : {20u, 0u, 0x3000u, 0x31u, 0u}) m.system.memory.write(Allegrex::Word, at, value), at += 4;
    m.system.memory.write(Allegrex::Word, Info, 104);  //SceKernelThreadInfo's size
    u32 quit = 0;
    for(auto& e : m.kernel.modules[uid].module.exports) if(e.nid == QuitNID) quit = e.address;
    s32 quitter = m.kernel.createThread("quitter", quit, 0x20, 0x1000, 0, 0);
    //the program waits for quit()'s thread to end, gives module_stop (below it) a millisecond, and leaves
    Assembler program{m, 0x0880'1000};
    program.li(s0, Results);
    program.li(a0, quitter); program.li(a1, 0);
    program.call("sceKernelWaitThreadEnd");
    program.put(sw(v0, 0x38, s0));
    program.li(a0, 1000);
    program.call("sceKernelDelayThread");
    program.li(t0, 0x600d);
    program.put(sw(t0, 0x3c, s0));
    program.call("sceKernelExitGame");
    s32 waits = m.kernel.createThread("main", 0x0880'1000, 0x10, 0x1000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[waits], 0, 0);
    m.kernel.startThread(*m.kernel.threads[quitter], 0, 0);
    m.kernel.run(Kernel::VblankCycles);
    CHECK(m.kernel.exited, true);
    CHECK(word(m.system, Results + 0x3c), 0x600d);  //the program's own end, after the module had gone
    CHECK(word(m.system, Results + 0x38), 1);
    CHECK(word(m.system, Results + 0x34), 0);       //quit() never returned
    CHECK(word(m.system, Results + 0x20), 0x5709);
    CHECK(word(m.system, Results + 0x24), 4);
    CHECK(word(m.system, Results + 0x28), 0x4152'4731);
    CHECK(word(m.system, Info + 52), 0x3000);       //module_stop's thread: the options' stack size and priority
    CHECK(word(m.system, Info + 64), 0x31);
    CHECK(m.kernel.modules.count(uid), 0);
    bool kept = std::any_of(m.kernel.blocks.begin(), m.kernel.blocks.end(), [](auto& block) {
      return block.name == "ms0:/STATUS.PRX" || block.name == "stack: TESTSTATUS";
    });
    CHECK(kept, false);
    CHECK(m.kernel.threads.count(quitter), 0);
    CHECK(m.kernel.threads.size(), 1);  //the program's: quit()'s thread and module_stop's are gone
  }

  for(bool program : {false, true}) {
    KernelMachine m;
    if(program) {
      m.kernel.programUID = m.kernel.newUID();
      m.kernel.module.segments = {{0x0880'1000, 0x1000}};
    }
    u32 stub = KernelMachine::Stubs + 0x800;  //a stub of its own, for a function known by its NID alone
    m.system.memory.write(Allegrex::Word, stub, jr(ra));
    m.system.memory.write(Allegrex::Word, stub + 4,
                          syscall(m.kernel.importCode("ModuleMgrForUser", StopUnloadSelfWithStatusNID)));
    Assembler main{m, 0x0880'1000};
    main.li(a0, 1); main.li(a1, 0); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.put(jal(stub));
    main.put(nop);
    main.print("refused\n");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, false);
    CHECK(m.kernel.exited, !program);
    CHECK(m.output == (program ? "" : "refused\n"), true);
    CHECK(m.kernel.programUID, 0);
    CHECK(m.kernel.threads.size(), program ? 0 : 1);
  }
}

//A thread terminating another's module_start (sceKernelTerminateThread, or TerminateDeleteThread), as it sleeps: the
//thread is deleted, as when it exits, so neither it nor its stack stays; the module counts as started, and the thread
//that started it is given its ID, with the termination as module_start's result. On both engines.
static auto terminatedThreads() -> void {
  HostFolder stick;
  put(stick, "SLEEPS.PRX", selfModule(SelfStart::Sleeps).build());
  for(auto function : {"sceKernelTerminateThread", "sceKernelTerminateDeleteThread"}) {
    for(bool recompile : {false, true}) {
      KernelMachine m;
      machine(m);
      m.kernel.mount("ms0", stick.path.string());
      u32 uid = m.call("sceKernelLoadModule", {m.string("ms0:/SLEEPS.PRX"), 0, 0});
      CHECK(m.kernel.modules.count(uid), 1);
      if(!m.kernel.modules.count(uid)) continue;
      Assembler main{m, 0x0880'1000};
      main.li(s0, Results);
      main.li(a0, uid); main.li(a1, 0); main.li(a2, 0); main.li(a3, Results + 0x40); main.li(t0, 0);
      main.call("sceKernelStartModule");
      main.put(sw(v0, 0x44, s0));
      main.call("sceKernelSleepThread");
      m.runProgram(0x0880'1000, recompile, Kernel::VblankCycles);
      u32 thread = m.kernel.modules[uid].thread;
      CHECK(m.kernel.modules[uid].status == Kernel::ModuleStatus::Starting && m.kernel.threads.count(thread), true);
      Assembler killer{m, 0x0880'2000};
      killer.li(s0, Results);
      killer.li(a0, thread);
      killer.call(function);
      killer.put(sw(v0, 0x48, s0));
      killer.call("sceKernelSleepThread");
      s32 kills = m.kernel.createThread("killer", 0x0880'2000, 0x30, 0x1000, 0, 0);
      m.kernel.startThread(*m.kernel.threads[kills], 0, 0);
      m.kernel.run(Kernel::VblankCycles);
      CHECK(word(m.system, Results + 0x48), 0);
      CHECK(m.kernel.threads.count(thread), 0);
      bool stack = std::any_of(m.kernel.blocks.begin(), m.kernel.blocks.end(), [](auto& block) {
        return block.name == "stack: TESTSELF";
      });
      CHECK(stack, false);
      CHECK(m.kernel.modules[uid].status == Kernel::ModuleStatus::Started && !m.kernel.modules[uid].thread, true);
      CHECK(word(m.system, Results + 0x44), uid);
      CHECK(word(m.system, Results + 0x40), Kernel::ErrorThreadTerminated);
      CHECK(m.kernel.threads.size(), 2);  //the program's and the killer's, both asleep
    }
  }
}

//module_start and module_stop that end with sceKernelExitThread rather than returning: the caller gets each one's
//exit status as its result, and their threads go all the same, their stacks back to the user partition.
static auto exitThreads() -> void {
  HostFolder stick;
  put(stick, "EXIT.PRX", exitModule().build());
  for(bool recompile : {false, true}) {
    KernelMachine m;
    machine(m);
    m.kernel.mount("ms0", stick.path.string());
    Assembler main{m, 0x0880'1000};
    main.li(s0, Results);
    main.li(a0, m.string("ms0:/EXIT.PRX")); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelLoadModule");
    main.put(sw(v0, 0x58, s0));
    main.put(addu(s1, v0, zero));
    main.put(addu(a0, s1, zero)); main.li(a1, 0); main.li(a2, 0); main.li(a3, Results + 0x5c); main.li(t0, 0);
    main.call("sceKernelStartModule");
    main.put(sw(v0, 0x60, s0));
    main.put(addu(a0, s1, zero)); main.li(a1, 0); main.li(a2, 0); main.li(a3, Results + 0x64); main.li(t0, 0);
    main.call("sceKernelStopModule");
    main.put(sw(v0, 0x68, s0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    u32 uid = word(m.system, Results + 0x58);
    CHECK(m.kernel.exited, true);
    CHECK(word(m.system, Results + 0x50), 0xe1);
    CHECK(word(m.system, Results + 0x60), uid);
    CHECK(word(m.system, Results + 0x5c), 5);
    CHECK(word(m.system, Results + 0x54), 0xe2);
    CHECK(word(m.system, Results + 0x68), uid);
    CHECK(word(m.system, Results + 0x64), 6);
    CHECK(m.kernel.modules.count(uid) && m.kernel.modules[uid].status == Kernel::ModuleStatus::Stopped, true);
    CHECK(m.kernel.threads.size(), 1);  //the program's own
    bool stacks = std::any_of(m.kernel.blocks.begin(), m.kernel.blocks.end(), [](auto& block) {
      return block.name == "stack: TESTEXIT";
    });
    CHECK(stacks, false);
  }
}

//What runs as module_start, and on what thread. A module with no module_start runs its ELF entry point, when that's
//in the module (here writing 0xe7 at Results + 0x70), and starts at once when it isn't. A module's own thread
//parameters (module_start_thread_parameter and module_stop_thread_parameter: a count, then a priority, a stack size
//and attributes) make its functions' threads; the options given to sceKernelStartModule go over them, but for what
//they leave as 0; and a count short of 3 leaves the rest as they were.
static auto entryPoints() -> void {
  HostFolder stick;
  TestModule entry;
  entry.name = "TESTENTRY";
  entry.code = {halt, halt};
  entry.entryPoint = entry.code.size() * 4;
  store(entry.code, Results + 0x70, 0xe7);
  entry.code.insert(entry.code.end(), {jr(ra), addiu(v0, zero, 0)});
  put(stick, "ENTRY.PRX", entry.build());
  entry.entryPoint = 0x10000;  //past its 0x1000 bytes
  put(stick, "OUTSIDE.PRX", entry.build());
  auto parameters = libraryModule();
  parameters.name = "TESTPARAMETERS";
  parameters.variables = {{StartParameters, 0x500}, {StopParameters, 0x510}};
  parameters.words = {{0x500, 3}, {0x504, 0x31}, {0x508, 0x3000}, {0x50c, 0x8000'0000},
                      {0x510, 2}, {0x514, 0x33}, {0x518, 0x2000}, {0x51c, 0x1234}};
  put(stick, "PARAMETERS.PRX", parameters.build());

  KernelMachine m;
  machine(m);
  m.kernel.mount("ms0", stick.path.string());
  auto load = [&](const char* path) { return m.call("sceKernelLoadModule", {m.string(path), 0, 0}); };
  u32 a = load("ms0:/ENTRY.PRX"), b = load("ms0:/OUTSIDE.PRX"), c = load("ms0:/PARAMETERS.PRX");
  CHECK(m.kernel.modules.count(a) && m.kernel.modules.count(b) && m.kernel.modules.count(c), true);
  if(!m.kernel.modules.count(a) || !m.kernel.modules.count(b) || !m.kernel.modules.count(c)) return;
  CHECK(m.call("sceKernelStartModule", {b, 0, 0, 0, 0}), b);
  CHECK(m.kernel.modules[b].status == Kernel::ModuleStatus::Started && !m.kernel.modules[b].thread, true);
  m.call("sceKernelStartModule", {a, 0, 0, 0, 0});  //(its result lost as the CPU goes to the thread it made)
  CHECK(m.kernel.modules[a].status == Kernel::ModuleStatus::Starting, true);
  m.kernel.run(Kernel::VblankCycles);
  CHECK(word(m.system, Results + 0x70), 0xe7);
  CHECK(m.kernel.modules[a].status == Kernel::ModuleStatus::Started, true);

  //the thread sceKernelStartModule or sceKernelStopModule made for a module's function
  auto made = [&](u32 uid, u32 priority, u32 stackSize, u32 attributes) {
    auto found = m.kernel.threads.find(m.kernel.modules[uid].thread);
    if(found == m.kernel.threads.end()) return false;
    auto& t = *found->second;
    return t.priority == priority && t.stackSize == stackSize && t.attributes == attributes;
  };
  m.call("sceKernelStartModule", {c, 0, 0, 0, 0});
  CHECK(made(c, 0x31, 0x3000, 0x8000'0000), true);
  m.kernel.run(Kernel::VblankCycles);
  m.call("sceKernelStopModule", {c, 0, 0, 0, 0});
  CHECK(made(c, 0x33, 0x2000, 0x8000'4000), true);  //a count of 2: the attributes as they were
  m.kernel.run(Kernel::VblankCycles);
  CHECK(m.call("sceKernelUnloadModule", {c}), c);
  c = load("ms0:/PARAMETERS.PRX");
  u32 options = Results + 0x78, at = options;  //SceKernelSMOption: its size, a partition, then the thread's
  for(u32 value : {20u, 0u, 0x4000u, 0x32u, 0u}) m.system.memory.write(Allegrex::Word, at, value), at += 4;
  m.call("sceKernelStartModule", {c, 0, 0, 0, options});
  CHECK(made(c, 0x32, 0x4000, 0x8000'0000), true);
}

//The machine's state as the system saves it (memory, the CPU, the GE and the kernel), and loaded into another.
static auto save(KernelMachine& m) -> std::vector<u8> {
  serializer s;
  m.system.memory.serialize(s);
  m.system.serialize(s);
  m.system.ge.serialize(s);
  m.kernel.serialize(s);
  return {s.data(), s.data() + s.size()};
}

static auto load(KernelMachine& m, const std::vector<u8>& state) -> bool {
  serializer s{state.data(), u32(state.size())};
  m.system.memory.serialize(s);
  m.system.serialize(s);
  bool valid = m.system.ge.serialize(s);
  return m.kernel.serialize(s) && valid;
}

//A state saved while a module_start runs (it waits in a delay, and the thread that started it waits for it) loads
//into another machine, which carries on as the first does: module_start ends, its result reaches the thread that
//waited, its thread goes, and the program runs on to its end. On both engines.
static auto stateWhileStarting() -> void {
  HostFolder stick;
  put(stick, "DELAY.PRX", delayModule().build());
  for(bool recompile : {false, true}) {
    KernelMachine m;
    machine(m);
    m.kernel.mount("ms0", stick.path.string());
    Assembler main{m, 0x0880'1000};
    main.li(s0, Results);
    main.li(a0, m.string("ms0:/DELAY.PRX")); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelLoadModule");
    main.put(sw(v0, 0x88, s0));
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0); main.li(a3, Results + 0x8c); main.li(t0, 0);
    main.call("sceKernelStartModule");
    main.put(sw(v0, 0x90, s0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency / 1000);  //a millisecond: module_start waits
    u32 uid = word(m.system, Results + 0x88);
    CHECK(m.kernel.modules.count(uid), 1);
    if(!m.kernel.modules.count(uid)) continue;
    CHECK(m.kernel.modules[uid].status == Kernel::ModuleStatus::Starting && m.kernel.modules[uid].thread, true);
    CHECK(word(m.system, Results + 0x80) == 0xa1 && word(m.system, Results + 0x84) == 0, true);
    auto state = save(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    n.kernel.mount("ms0", stick.path.string());
    CHECK(load(n, state), true);
    CHECK(save(n) == state, true);
    for(auto* each : {&m, &n}) {
      auto& k = each->kernel;
      k.run(Kernel::CPUFrequency / 10);
      CHECK(k.exited, true);
      CHECK(word(each->system, Results + 0x84), 0xb2);
      CHECK(word(each->system, Results + 0x8c), 9);
      CHECK(word(each->system, Results + 0x90), uid);
      CHECK(k.modules.count(uid) && k.modules[uid].status == Kernel::ModuleStatus::Started, true);
      CHECK(k.modules.count(uid) && !k.modules[uid].thread && k.threads.size() == 1, true);
    }
  }
}

//"TESTEXEC", a program another starts in its place: writes its argument's length at Results + 0x100 and, if it has
//one, the argument's first word at Results + 0x104, then 0xb0b0 at Results + 0x108, and leaves (sceKernelExitGame).
static auto execModule() -> TestModule {
  TestModule m;
  m.name = "TESTEXEC";
  m.imports = {{"LoadExecForUser", Kernel::nid("sceKernelExitGame")}};
  m.start = 0;
  m.code = {lui(t1, (Results + 0x100) >> 16), ori(t1, t1, (Results + 0x100) & 0xffff), sw(a0, 0, t1),
            beq(a0, zero, 3), nop, lw(t0, 0, a1), sw(t0, 4, t1)};
  store(m.code, Results + 0x108, 0xb0b0);
  callStub(m, 0x3c0);
  m.code.push_back(halt);  //never reached
  return m;
}

//sceKernelLoadExec, as pspautotests' modules/loadexec/loader recorded (the call never returns; the new program runs
//on): a program writes "A", starts TESTEXEC in its place (plain, then encrypted) and would write "never". The new
//program has the machine to itself: the old one's threads, memory and marks gone, the clock started afresh, and its
//argument its path with no parameters, the bytes given with them, or none for none. What can't be started is refused
//with the old program left running: a file there isn't, one that isn't a program, an argument over 4 KiB. A state
//saved once the new program has run loads into another machine. On both engines.
static auto loadExec() -> void {
  HostFolder stick;
  put(stick, "EXEC.PRX", execModule().build());
  put(stick, "EXEC.BIN", psp_encrypt::encrypt(execModule().build()));
  stick.put("TEXT.TXT", "not a program");
  struct Case { const char* path; s32 length; const char* argument; u32 expectedLength, expectedWord; } cases[] = {
    {"ms0:/EXEC.PRX", -1, nullptr, 14, 0x3a30'736d},  //no parameters: its path, "ms0:/EXEC.PRX" and its NUL
    {"ms0:/EXEC.BIN", 6, "hello", 6, 0x6c6c'6568},     //the bytes given
    {"ms0:/EXEC.PRX", 0, nullptr, 0, 0},               //none
  };
  for(bool recompile : {false, true}) {
    for(auto& c : cases) {
      KernelMachine m;
      m.kernel.mount("ms0", stick.path.string());
      constexpr u32 Parameters = Results + 0x40;
      Assembler main{m, 0x0880'1000};
      main.print("A");
      main.li(t0, Results); main.li(t1, 0xa0a0); main.put(sw(t1, 0x108, t0));
      main.li(a0, m.string(c.path)); main.li(a1, c.length < 0 ? 0 : Parameters);
      main.call("sceKernelLoadExec");
      main.print("never");
      main.call("sceKernelExitGame");
      m.system.memory.write(4, Parameters, 16);
      m.system.memory.write(4, Parameters + 4, c.length < 0 ? 0 : u32(c.length));
      m.system.memory.write(4, Parameters + 8, c.argument ? m.string(c.argument) : 0);
      m.system.memory.write(4, Parameters + 12, 0);
      m.runProgram(0x0880'1000, recompile);  //it ends as the new program is put in
      CHECK(m.output == "A", true);
      CHECK(m.kernel.exited, false);
      CHECK(m.kernel.module.name == "TESTEXEC", true);
      CHECK(m.kernel.threads.size(), 1);
      CHECK(m.kernel.cycles < Kernel::VblankCycles, true);
      CHECK(word(m.system, Results + 0x108), 0);  //the old program's mark went with its memory
      m.kernel.run(Kernel::CPUFrequency / 10);
      CHECK(m.kernel.exited, true);
      CHECK(m.output == "A", true);
      CHECK(word(m.system, Results + 0x108), 0xb0b0);
      CHECK(word(m.system, Results + 0x100), c.expectedLength);
      CHECK(word(m.system, Results + 0x104), c.expectedWord);
      CHECK(m.notes.size(), 0);
      CHECK(roundTrip(m, [&](KernelMachine& fresh) { fresh.kernel.mount("ms0", stick.path.string()); }), true);
    }
  }
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  constexpr u32 Parameters = Results + 0x40;
  m.system.memory.write(4, Parameters, 16);
  m.system.memory.write(4, Parameters + 4, 4097);
  m.system.memory.write(4, Parameters + 8, m.string("long"));
  Assembler main{m, 0x0880'1000};
  main.li(s0, Results);
  for(auto [path, parameters, at] : {std::tuple{"ms0:/NONE.PRX", 0u, 0x10}, {"ms0:/TEXT.TXT", 0u, 0x14},
                                     {"ms0:/EXEC.PRX", Parameters, 0x18}}) {
    main.li(a0, m.string(path)); main.li(a1, parameters);
    main.call("sceKernelLoadExec");
    main.put(sw(v0, at, s0));
  }
  main.call("sceKernelExitGame");
  m.runProgram(0x0880'1000, false);
  CHECK(m.kernel.exited, true);
  CHECK(word(m.system, Results + 0x10), Kernel::ErrorFileNotFound);
  CHECK(word(m.system, Results + 0x14), Kernel::ErrorIllegalObject);
  CHECK(word(m.system, Results + 0x18), Kernel::ErrorIllegalSize);
  CHECK(m.kernel.module.name.empty(), true);  //the old program still
}

//A program that loads and starts a module, then stops and unloads itself, as Killzone: Liberation's boot program
//does (on both engines): only it goes, as a module would, its module_stop run first, while the module it started,
//and that module's thread, run on. The module's import of the program's function reaches it till then, and goes back
//to the kernel with it. A state saved while the program's module_stop runs carries on as the machine does.
static auto programUnloadsItself() -> void {
  HostFolder stick;
  put(stick, "LIVES.PRX", livesModule().build());
  constexpr u32 Path = KernelMachine::Strings + 0x800;
  auto boot = bootModule(Path).build();
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.system.recompiler.enabled = recompile;
    std::string error;
    CHECK(m.kernel.load(boot.data(), boot.size(), "ms0:/PSP/GAME/BOOT/EBOOT.PBP", error), true);
    if(!error.empty()) continue;
    m.kernel.mount("ms0", stick.path.string());
    std::string path = "ms0:/LIVES.PRX";
    m.system.memory.copyIn(Path, path.c_str(), path.size() + 1);
    m.system.memory.write(Allegrex::Word, Results + 0x30, 0x4152'4731);
    u32 program = m.kernel.programUID;
    m.kernel.run(Kernel::CPUFrequency / 200);  //5 milliseconds: the program's module_stop waits
    CHECK(m.kernel.exited, false);
    CHECK(m.kernel.programUID, 0);
    CHECK(m.kernel.modules.count(program) && m.kernel.modules[program].status == Kernel::ModuleStatus::Unloading,
          true);
    CHECK(word(m.system, Results + 0x20), 0x5709);
    CHECK(word(m.system, Results + 0x2c), 0);
    CHECK(roundTrip(m), true);
    KernelMachine n;
    CHECK(loadState(n, saveState(m)), true);
    for(auto* machine : {&m, &n}) machine->kernel.run(Kernel::CPUFrequency / 10);
    CHECK(saveState(n) == saveState(m), true);

    u32 lives = word(m.system, Results + 0x60);
    CHECK(m.kernel.exited, false);
    CHECK(m.kernel.modules.count(program), 0);
    CHECK(m.kernel.modules.count(lives), 1);
    CHECK(word(m.system, Results + 0x34), 0);  //it never came back
    CHECK(word(m.system, Results + 0x24), 4);
    CHECK(word(m.system, Results + 0x28), 0x4152'4731);
    CHECK(word(m.system, Results + 0x2c), 0x570b);
    CHECK(word(m.system, Results + 0x68), 0x77);
    CHECK(word(m.system, Results + 0x64) > 50, true);  //a count a millisecond, for most of a tenth of a second
    CHECK(m.kernel.threads.size(), 1);  //"lives": the program's own thread and its module_stop's are gone
    bool kept = std::any_of(m.kernel.blocks.begin(), m.kernel.blocks.end(), [](auto& b) {
      return b.name == "TESTBOOT";
    });
    CHECK(kept, false);
    if(m.kernel.modules.count(lives)) {
      auto& imports = m.kernel.modules[lives].module.imports;
      CHECK(imports.size(), 4);
      if(imports.size() == 4) CHECK(word(m.system, imports[3].stub + 4) & 0x3f, 0x0c);  //a syscall again
    }
    //the module list has the module alone, and the program can't be asked about
    u32 list = Results + 0x100;
    CHECK(n.call("sceKernelGetModuleIdList", {list, 16, list + 16}), 0);
    CHECK(word(n.system, list + 16), 1);
    CHECK(word(n.system, list), lives);
    n.system.memory.write(Allegrex::Word, list + 0x20, 96);
    CHECK(n.call("sceKernelQueryModuleInfo", {program, list + 0x20}), Kernel::ErrorUnknownModule);
  }
}

auto moduleTests() -> Tests {
  return {
    {"modules start and link", startAndLink}, {"modules linking", linking}, {"modules stand-ins", standIns},
    {"modules identities", identities}, {"modules refusals", refusals}, {"modules sizes", sizes},
    {"modules unload themselves", unloadThemselves}, {"modules unload with a status", unloadWithStatus},
    {"modules exit threads", exitThreads}, {"modules terminated threads", terminatedThreads},
    {"modules entry points", entryPoints}, {"modules state", stateWhileStarting},
    {"programs started in another's place", loadExec}, {"programs unloading themselves", programUnloadsItself},
  };
}

}
