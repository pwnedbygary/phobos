//Save states' parts below the system (ares/psp/kernel/serialization.cpp, the CPU's and the GE's): what a state
//carries, taken from one machine and loaded into another. Open files come back where they were, opened again on the
//host (one gone since is dropped, as is a file on a disc that isn't in the drive); a folder half listed, the kernel's
//objects and its threads come back as they were; every field a state carries reaches it and comes back; and a state
//holding what no machine could (a list longer than the state, a position past its end...) is refused.
#include "kernel-machine.hpp"
#include "disc-image.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::Disc;
constexpr u32 Buffer = 0x0893'0000;

static auto discOf(const std::vector<u8>& bytes) -> std::shared_ptr<Disc> {
  auto disc = std::make_shared<Disc>();
  auto data = std::make_shared<std::vector<u8>>(bytes);
  std::string error;
  disc->open([data](u64 offset, void* out, u64 size) -> u64 {
    size = offset < data->size() ? std::min<u64>(size, data->size() - offset) : 0;
    memcpy(out, data->data() + offset, size);
    return size;
  }, bytes.size(), error);
  return disc;
}

static auto kernelStates() -> void {
  HostFolder stick;
  stick.put("A.TXT", "abcdefgh");
  stick.put("B.TXT", "12345678");
  stick.put("LIST/ONE", "1");
  stick.put("LIST/TWO", "2");
  stick.put("LIST/A\\B", "x");  //names no PSP path can name: left out of the folder's listing
  stick.put("LIST/C:D", "x");
  std::vector<u8> data(5000);
  for(u32 n = 0; n < data.size(); n++) data[n] = u8(n * 3);
  auto image = disc_image::makeIso({{"DATA.BIN", data}});

  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  m.kernel.disc = discOf(image.bytes);
  u32 a = m.call("sceIoOpen", {m.string("ms0:/A.TXT"), 0x0001, 0});
  u32 b = m.call("sceIoOpen", {m.string("ms0:/B.TXT"), 0x0001, 0});
  u32 d = m.call("sceIoOpen", {m.string("disc0:/DATA.BIN"), 0x0001, 0});
  u32 folder = m.call("sceIoDopen", {m.string("ms0:/LIST")});
  m.call("sceIoRead", {a, Buffer, 3});
  m.call("sceIoRead", {d, Buffer, 1000});
  m.call("sceIoDread", {folder, Buffer});  //"."
  CHECK(m.kernel.files[folder].entries.size(), 4);  //".", "..", ONE, TWO
  u32 semaphore = m.call("sceKernelCreateSema", {m.string("sema"), 0, 2, 5, 0});
  u32 flag = m.call("sceKernelCreateEventFlag", {m.string("flag"), 0, 0x0f, 0});
  u32 block = m.call("sceKernelAllocPartitionMemory", {2, m.string("block"), 0, 0x1000, 0});
  s32 thread = m.kernel.createThread("worker", 0x0880'1000, 0x30, 0x1000, 0, 0x1234);
  m.kernel.threads[thread]->context.gpr[16] = 0xdead'beef;
  serializer s;
  CHECK(m.kernel.serialize(s), true);

  //into another machine, given the same devices; B.TXT is gone from the host by then
  stick.put("A.TXT", "ABCDEFGH");  //changed: what's read after loading comes from the host as it is
  std::filesystem::remove(stick.path / "B.TXT");
  KernelMachine n;
  n.kernel.mount("ms0", stick.path.string());
  n.kernel.disc = discOf(image.bytes);
  serializer load{s.data(), s.size()};
  CHECK(n.kernel.serialize(load), true);
  CHECK(n.call("sceIoRead", {a, Buffer, 3}), 3);  //from where it was
  CHECK(n.system.memory.readString(Buffer, 3) == "DEF", true);
  CHECK(n.call("sceIoRead", {b, Buffer, 3}), Kernel::ErrorBadFile);  //dropped
  CHECK(n.call("sceIoRead", {d, Buffer, 4}), 4);
  CHECK(n.system.memory.read(1, Buffer), u8(1000 * 3));
  CHECK(n.call("sceIoDread", {folder, Buffer}), 1);  //".." next
  CHECK(n.system.memory.readString(Buffer + 88, 16) == "..", true);
  CHECK(n.call("sceKernelPollSema", {semaphore, 2}), 0);
  CHECK(n.call("sceKernelPollSema", {semaphore, 1}), Kernel::ErrorSemaphoreZero);
  CHECK(n.call("sceKernelPollEventFlag", {flag, 0x0f, 0, Buffer}), 0);
  CHECK(n.call("sceKernelGetBlockHeadAddr", {block}) >= Kernel::UserMemory, true);
  CHECK(n.kernel.threads.count(thread), 1);
  if(n.kernel.threads.count(thread)) {
    CHECK(n.kernel.threads[thread]->context.gpr[16], 0xdead'beef);
    CHECK(n.kernel.threads[thread]->gp, 0x1234);
    CHECK(n.kernel.threads[thread]->name == "worker", true);
  }
  CHECK(n.call("sceIoOpen", {n.string("ms0:/A.TXT"), 0x0001, 0}) > d, true);  //new descriptors go on from the old

  //a list longer than the rest of the state, and a text longer than it: refused, as they run out of state
  std::vector<u8> damaged(s.data(), s.data() + s.size());
  u32 nameLength = m.kernel.module.name.size();
  u32 at = 4 + nameLength + 2 + 2 + 1 + 16;  //the module's segment count, after its name and fields
  damaged[at] = damaged[at + 1] = damaged[at + 2] = damaged[at + 3] = 0xff;
  KernelMachine o;
  serializer broken{damaged.data(), u32(damaged.size())};
  CHECK(o.kernel.serialize(broken), false);
  damaged.assign(s.data(), s.data() + s.size());
  damaged[0] = damaged[1] = damaged[2] = damaged[3] = 0xff;  //the module's name's length
  KernelMachine p;
  serializer longName{damaged.data(), u32(damaged.size())};
  CHECK(p.kernel.serialize(longName), false);

  //a file on the disc, loaded where no disc image is in the drive (none, or a host folder standing for disc0:):
  //dropped, as the files on it are gone
  for(bool folder : {false, true}) {
    KernelMachine q;
    q.kernel.mount("ms0", stick.path.string());
    if(folder) q.kernel.mount("disc0", stick.path.string());
    serializer again{s.data(), s.size()};
    CHECK(q.kernel.serialize(again), true);
    CHECK(q.kernel.files.count(d), 0);
    CHECK(q.kernel.files.count(a), 1);
  }
}

//The test machine's state, as the system saves it but for memory (whose own checks are the ares tests'): the CPU,
//the GE and the kernel.
static auto save(KernelMachine& m) -> std::vector<u8> {
  serializer s;
  m.system.serialize(s);
  m.system.ge.serialize(s);
  m.kernel.serialize(s);
  return {s.data(), s.data() + s.size()};
}

//Loads such a state: false if any part of it is refused, or it ends part way.
static auto load(KernelMachine& m, const std::vector<u8>& bytes) -> bool {
  serializer s{bytes.data(), u32(bytes.size())};
  m.system.serialize(s);
  bool valid = m.system.ge.serialize(s);
  valid = m.kernel.serialize(s) && valid;
  return valid && s.size() <= bytes.size();
}

//Every field a state carries reaches it and comes back. A machine with one of everything (two threads, a display
//list on the GE's queue with a SIGNAL call on its stack, files and folders on the memory stick and the disc...) has
//each field changed in turn, and each change must change the state: a field the state leaves out wouldn't. Then the
//state, every field changed, loads into a fresh machine given the same devices, which must make the very same state:
//a field that comes back wrong wouldn't. Last, values no machine could hold are refused, one at a time.
static auto stateFields() -> void {
  using GeList = Kernel::GeList;
  HostFolder stick;
  stick.put("A.TXT", "abcdefgh");
  stick.put("B.TXT", "12345678");
  stick.put("LIST/ONE", "1");
  auto image = disc_image::makeIso({
    {"DATA.BIN", std::vector<u8>(5000, 7)}, {"DIR/ONE.BIN", {1}}, {"DIR/TWO.BIN", {2}},
  });
  auto devices = [&](KernelMachine& m) {
    m.kernel.mount("ms0", stick.path.string());
    m.kernel.disc = discOf(image.bytes);
  };
  KernelMachine a;
  devices(a);
  auto& k = a.kernel;
  auto& cpu = a.system;
  auto& ge = a.system.ge;

  //one of everything
  k.module.segments = {{0x0880'4000, 0x100}};
  u32 programBlock = k.allocate(0x100, 2, 0x0880'4000, "program")->uid;  //where start() would put it
  k.module.imports = {{"Lib", 0x1111, 0x0880'5000}};
  k.module.exports = {{"Lib", 0x2222, 0x0880'6000, false}};
  k.module.skipped = {"left out"};
  k.programUID = k.newUID();
  u32 otherProgramID = k.newUID();  //what the program's ID changes to below
  //a module loaded into a block of its own, its module_start running on thread one; and one of Sony's, stood in
  //for (started at once, nothing of it in memory)
  u32 moduleID = k.newUID(), sonyID = k.newUID();
  u32 moduleBlock = k.allocate(0x1000, 0, 0, "ms0:/A.PRX")->uid;
  u32 spareBlock = k.allocate(0x1000, 0, 0, "spare")->uid;  //what the module's block changes to below
  auto& loaded = k.modules[moduleID];
  loaded.uid = moduleID;
  loaded.path = "ms0:/A.PRX";
  loaded.block = moduleBlock;
  loaded.module.name = "LOADED";
  loaded.module.segments = {{0x0881'0000, 0x100}};
  auto& sony = k.modules[sonyID];
  sony.uid = sonyID;
  sony.path = "ms0:/SAS.PRX";
  sony.standIn = true;
  sony.status = Kernel::ModuleStatus::Started;
  sony.module.name = "sceSAScore";
  a.stub("sceKernelDelayThread");
  s32 one = k.createThread("one", 0x0880'1000, 0x20, 0x1000, 0, 0);
  s32 two = k.createThread("two", 0x0880'2000, 0x30, 0x1000, 0, 0);
  loaded.status = Kernel::ModuleStatus::Starting;
  loaded.thread = one;
  k.current = k.threads[one].get();
  u32 semaphore = a.call("sceKernelCreateSema", {a.string("sema"), 0, 1, 5, 0});
  k.lwMutexes[k.nextUID++] = 0x0880'9000;
  u32 flag = a.call("sceKernelCreateEventFlag", {a.string("flag"), 0, 3, 0});
  u32 callback = a.call("sceKernelCreateCallback", {a.string("callback"), 0x0880'7000, 0x42});
  k.memoryStickCallbacks = {callback};
  u32 plainBlock = k.allocate(0x1000, 0, 0, "block")->uid;
  u32 spareStack = k.allocate(0xf00, 0, 0, "spare stack")->address;  //what thread one's stack moves to below
  u32 fixedID = a.call("sceKernelCreateFpl", {a.string("fpl"), 2, 0, 16, 2, 0});
  u32 variableID = a.call("sceKernelCreateVpl", {a.string("vpl"), 2, 0, 0x100, 0});
  u32 spareID = a.call("sceKernelCreateFpl", {a.string("spare"), 2, 0, 16, 1, 0});
  u32 pipeID = a.call("sceKernelCreateMsgPipe", {a.string("pipe"), 2, 0, 0x100, 0});
  u32 mailboxID = a.call("sceKernelCreateMbx", {a.string("box"), 0, 0});
  u32 file = a.call("sceIoOpen", {a.string("ms0:/A.TXT"), 0x0001, 0});
  u32 other = a.call("sceIoOpen", {a.string("ms0:/B.TXT"), 0x0001, 0});
  u32 folder = a.call("sceIoDopen", {a.string("ms0:/LIST")});
  u32 top = a.call("sceIoDopen", {a.string("ms0:/")});  //a device's top: no "." or ".." in it
  u32 discFile = a.call("sceIoOpen", {a.string("disc0:/DATA.BIN"), 0x0001, 0});
  u32 discFolder = a.call("sceIoDopen", {a.string("disc0:/DIR")});
  CHECK(k.files.size(), 6);
  for(u32 open : {file, other, folder, top, discFile, discFolder}) if(!k.files.count(open)) return;
  CHECK(k.files[discFolder].discEntries.size(), 2);
  u32 list = a.call("sceGeListEnQueue", {0x0890'8000, 0x0890'8000, u32(-1), 0}) - Kernel::GeListIDs;  //stalled
  u32 list2 = a.call("sceGeListEnQueue", {0x0890'9000, 0x0890'9000, u32(-1), 0}) - Kernel::GeListIDs;  //queued
  CHECK(list < 64 && list2 < 64 && k.geRunning == s32(list), true);
  if(list >= 64 || list2 >= 64) return;
  k.geLists[list].stack.push_back({ge.list, 0x1000'0000});  //a SIGNAL call made
  //the second list completed, its finish callback running (geRunning and geFinishing go to it below); the first not
  //started yet, so that starting it is a change
  k.geLists[list2].state = Kernel::GeList::State::Completed;
  k.geLists[list2].started = true;
  k.geLists[list].started = false;
  k.calls.push_back({0x0880'7000, 0x0880'8000, {1, 2, 3}, false});  //after the calls above: a syscall would start it

  std::vector<std::pair<std::string, std::function<void()>>> changes = {
    //the CPU
    {"ipu.r", [&] { cpu.ipu.r[9] ^= 0x1234; }}, {"ipu.lo", [&] { cpu.ipu.lo ^= 1; }},
    {"ipu.hi", [&] { cpu.ipu.hi ^= 1; }}, {"ipu.pc", [&] { cpu.ipu.pc ^= 0x40; }},
    {"ipu.pd", [&] { cpu.ipu.pd ^= 0x80; }}, {"fpu.r", [&] { cpu.fpu.r[4] ^= 0x3f80'0000; }},
    {"fpu.csr", [&] { cpu.fpu.csr ^= 1; }}, {"scc.r", [&] { cpu.scc.r[12] ^= 1; }},
    {"scc.interrupts", [&] { cpu.scc.interrupts ^= 1; }}, {"scc.halted", [&] { cpu.scc.halted = !cpu.scc.halted; }},
    {"vfpu.r", [&] { cpu.vfpu.r[77] ^= 0x4000'0000; }}, {"vfpu.pfxs", [&] { cpu.vfpu.pfxs ^= 1; }},
    {"vfpu.pfxt", [&] { cpu.vfpu.pfxt ^= 1; }}, {"vfpu.pfxd", [&] { cpu.vfpu.pfxd ^= 1; }},
    {"vfpu.cc", [&] { cpu.vfpu.cc ^= 1; }}, {"vfpu.rcx", [&] { cpu.vfpu.rcx[3] ^= 1; }},
    //the GE
    {"ge.commands", [&] { ge.commands[0x10] ^= 1; }}, {"ge.clut", [&] { ge.clut[100] ^= 1; }},
    {"ge.list.address", [&] { ge.list.address ^= 0x100; }}, {"ge.list.stall", [&] { ge.list.stall ^= 0x100; }},
    {"ge.list.offset", [&] { ge.list.offset ^= 0x100; }}, {"ge.list.depth", [&] { ge.list.depth = 2; }},
    {"ge.list.returnAddress", [&] { ge.list.returnAddress[1] ^= 4; }},
    {"ge.list.returnOffset", [&] { ge.list.returnOffset[1] ^= 4; }},
    {"ge.vertexAddress", [&] { ge.vertexAddress ^= 4; }}, {"ge.indexAddress", [&] { ge.indexAddress ^= 4; }},
    {"ge.signalWord", [&] { ge.signalWord ^= 1; }}, {"ge.finishWord", [&] { ge.finishWord ^= 1; }},
    {"ge.endWord", [&] { ge.endWord ^= 1; }}, {"ge.bones", [&] { ge.bones[95] ^= 1; }},
    {"ge.world", [&] { ge.world[11] ^= 1; }}, {"ge.view", [&] { ge.view[11] ^= 1; }},
    {"ge.projection", [&] { ge.projection[15] ^= 1; }}, {"ge.textureMatrix", [&] { ge.textureMatrix[11] ^= 1; }},
    {"ge.boneIndex", [&] { ge.boneIndex = 5; }}, {"ge.worldIndex", [&] { ge.worldIndex = 5; }},
    {"ge.viewIndex", [&] { ge.viewIndex = 5; }}, {"ge.projectionIndex", [&] { ge.projectionIndex = 5; }},
    {"ge.textureIndex", [&] { ge.textureIndex = 5; }}, {"ge.pending", [&] { ge.pending = GE::Stop::Finished; }},
    //the program
    {"module.name", [&] { k.module.name += "x"; }}, {"module.attributes", [&] { k.module.attributes ^= 1; }},
    {"module.version[0]", [&] { k.module.version[0] ^= 1; }},
    {"module.version[1]", [&] { k.module.version[1] ^= 1; }},
    {"module.relocatable", [&] { k.module.relocatable = !k.module.relocatable; }},
    {"module.base", [&] { k.module.base ^= 4; }}, {"module.entry", [&] { k.module.entry ^= 4; }},
    {"module.gp", [&] { k.module.gp ^= 4; }}, {"module.moduleInfo", [&] { k.module.moduleInfo ^= 4; }},
    {"segment address", [&] { k.module.segments[0].address ^= 4; }},
    {"segment size", [&] { k.module.segments[0].size ^= 4; }},
    {"module import library", [&] { k.module.imports[0].library += "x"; }},
    {"module import nid", [&] { k.module.imports[0].nid ^= 1; }},
    {"module import stub", [&] { k.module.imports[0].stub ^= 4; }},
    {"export library", [&] { k.module.exports[0].library += "x"; }},
    {"export nid", [&] { k.module.exports[0].nid ^= 1; }},
    {"export address", [&] { k.module.exports[0].address ^= 4; }},
    {"export variable", [&] { k.module.exports[0].variable = true; }},
    {"skipped", [&] { k.module.skipped[0] += "x"; }},
    {"import library", [&] { k.imports[0].library += "x"; }}, {"import nid", [&] { k.imports[0].nid ^= 1; }},
    {"import reported", [&] { k.imports[0].reported = true; }},
    {"exited", [&] { k.exited = true; }}, {"cycles", [&] { k.cycles += 3 * Kernel::VblankCycles + 12345; }},
    {"nextUID", [&] { k.nextUID += 7; }}, {"startTime", [&] { k.startTime += 7; }},
    //the modules it loaded: each change leaves a module a machine could have (Sony's taken for a module of the
    //game's with nothing in memory, the module's module_stop running on thread two)
    {"programUID", [&] { k.programUID = otherProgramID; }}, {"loaded path", [&] { loaded.path += "x"; }},
    {"loaded standIn", [&] { sony.standIn = false; }}, {"loaded block", [&] { loaded.block = spareBlock; }},
    {"loaded status", [&] { loaded.status = Kernel::ModuleStatus::Stopping; }},
    {"loaded thread", [&] { loaded.thread = two; }}, {"loaded module", [&] { loaded.module.name += "x"; }},
    {"loaded segment", [&] { loaded.module.segments[0].size ^= 4; }},
  };
  //a thread's every field, and its registers. Each change leaves a value a fresh machine doesn't have, so that one
  //coming back wrong (as a fresh machine's) shows.
  auto& t = *k.threads[one];
  auto contextChanges = [&](const std::string& whose, Kernel::Context* c) {
    std::vector<std::pair<std::string, std::function<void()>>> fields = {
      {"gpr", [c] { c->gpr[5] ^= 1; }}, {"lo", [c] { c->lo ^= 1; }}, {"hi", [c] { c->hi ^= 1; }},
      {"pc", [c] { c->pc ^= 4; }}, {"pd", [c] { c->pd ^= 4; }}, {"fpr", [c] { c->fpr[3] ^= 1; }},
      {"fcsr", [c] { c->fcsr ^= 1; }}, {"vpr", [c] { c->vpr[100] ^= 1; }}, {"pfxs", [c] { c->pfxs ^= 1; }},
      {"pfxt", [c] { c->pfxt ^= 1; }}, {"pfxd", [c] { c->pfxd ^= 1; }}, {"cc", [c] { c->cc ^= 1; }},
    };
    for(auto& [name, change] : fields) changes.push_back({whose + " " + name, change});
  };
  contextChanges("thread", &t.context);
  contextChanges("interrupted", &k.interrupted);
  contextChanges("before its callback", &t.beforeCallback);
  auto& sema = k.semaphores[semaphore];
  auto& eventFlag = k.eventFlags[flag];
  auto& cb = k.callbacks[callback];
  auto& block = *std::find_if(k.blocks.begin(), k.blocks.end(), [&](auto& b) { return b.uid == plainBlock; });
  auto stackOf = [&](const Kernel::Thread& thread) -> Kernel::Block& {  //a thread's stack's block, found afresh
    return *std::find_if(k.blocks.begin(), k.blocks.end(), [&](auto& b) { return b.address == thread.stackBlock; });
  };
  auto& host = k.files[file];
  auto& hostFolder = k.files[folder];
  auto& onDisc = k.files[discFile];
  auto& discList = k.files[discFolder];
  auto& c = k.controller;
  auto& l = k.geLists[list];
  auto& gc = k.geCallbacks[3];
  auto& fixedPool = k.pools[fixedID];
  auto& variablePool = k.pools[variableID];
  auto& sparePool = k.pools[spareID];
  auto& sasVoice = k.sas.voices[3];
  auto& pipe = k.pipes[pipeID];
  auto& mailbox = k.mailboxes[mailboxID];
  std::vector<std::pair<std::string, std::function<void()>>> more = {
    //a message pipe, its buffer moved to a block of its own and halved, holding bytes; a mailbox holding a packet
    {"pipe name", [&] { pipe.name += "x"; }}, {"pipe attributes", [&] { pipe.attributes ^= 1; }},
    {"pipe block and address", [&] {
      auto block = k.allocate(0x100, 0, 0, "another pipe buffer");
      pipe.block = block->uid, pipe.address = block->address;
    }},
    {"pipe size", [&] { pipe.size = 0x80; }}, {"pipe start", [&] { pipe.start = 5; }},
    {"pipe used", [&] { pipe.used = 7; }},
    {"mailbox name", [&] { mailbox.name += "x"; }}, {"mailbox attributes", [&] { mailbox.attributes ^= 1; }},
    {"mailbox messages", [&] { mailbox.messages.push_back(0x0880'1000); }},
    {"atracIDs", [&] { k.atracIDs = 5; }}, {"dispatchSuspended", [&] { k.dispatchSuspended = true; }},
    {"fontResolution", [&] { k.fontResolution[1] = 144.0f; }},
    {"thread name", [&] { t.name += "x"; }}, {"thread entry", [&] { t.entry ^= 4; }},
    {"thread priority", [&] { t.priority ^= 1; }}, {"thread initialPriority", [&] { t.initialPriority ^= 1; }},
    //(a stack is a block of its own, of its size: thread one's shrinks with its block, then moves to the spare)
    {"thread stackSize", [&] { stackOf(t).size = t.stackSize = 0xf00; }},
    {"thread stackBlock", [&] { t.stackBlock = spareStack; }},
    {"thread attributes", [&] { t.attributes ^= 1; }}, {"thread gp", [&] { t.gp ^= 4; }},
    {"thread status", [&] { t.status = Kernel::Status::Ready; }},
    {"thread wait", [&] { t.wait = Kernel::Wait::Sleep; }}, {"thread waitID", [&] { t.waitID ^= 1; }},
    {"thread waitCount", [&] { t.waitCount ^= 1; }}, {"thread waitMode", [&] { t.waitMode ^= 1; }},
    {"thread waitPointer", [&] { t.waitPointer ^= 4; }}, {"thread wakeAt", [&] { t.wakeAt ^= 1; }},
    {"thread timeoutPointer", [&] { t.timeoutPointer ^= 4; }}, {"thread readySince", [&] { t.readySince ^= 1; }},
    {"thread exitStatus", [&] { t.exitStatus ^= 1; }}, {"thread wakeupCount", [&] { t.wakeupCount ^= 1; }},
    {"thread callbacks", [&] { t.callbacks = true; }}, {"thread inCallback", [&] { t.inCallback = true; }},
    {"thread callbackID", [&] { t.callbackID = callback; }},
    {"wait before callback", [&] { t.waitBeforeCallback.wait = Kernel::Wait::Sleep; }},
    {"wait before callback id", [&] { t.waitBeforeCallback.id ^= 1; }},
    {"wait before callback count", [&] { t.waitBeforeCallback.count ^= 1; }},
    {"wait before callback mode", [&] { t.waitBeforeCallback.mode ^= 1; }},
    {"wait before callback pointer", [&] { t.waitBeforeCallback.pointer ^= 4; }},
    {"wait before callback timeoutPointer", [&] { t.waitBeforeCallback.timeoutPointer ^= 4; }},
    {"wait before callback wakeAt", [&] { t.waitBeforeCallback.wakeAt ^= 1; }},
    {"wait before callback callbacks", [&] { t.waitBeforeCallback.callbacks = true; }},
    {"thread waitDone", [&] { t.waitDone ^= 1; }}, {"thread waitResult", [&] { t.waitResult ^= 4; }},
    {"wait before callback done", [&] { t.waitBeforeCallback.done ^= 1; }},
    {"wait before callback resultPointer", [&] { t.waitBeforeCallback.resultPointer ^= 4; }},
    {"the thread running", [&] { k.current = k.threads[two].get(); }},
    {"readySequence", [&] { k.readySequence += 7; }}, {"nextVblank", [&] { k.nextVblank = k.cycles + 1000; }},
    {"vblanks", [&] { k.vblanks += 7; }},
    {"semaphore name", [&] { sema.name += "x"; }},
    {"semaphore attributes", [&] { sema.attributes ^= 1; }}, {"semaphore count", [&] { sema.count = 3; }},
    {"semaphore maximum", [&] { sema.maximum ^= 1; }},
    {"lwMutex", [&] { k.lwMutexes.begin()->second ^= 4; }},
    {"event flag name", [&] { eventFlag.name += "x"; }},
    {"event flag attributes", [&] { eventFlag.attributes ^= 1; }},
    {"event flag initial", [&] { eventFlag.initial ^= 1; }}, {"event flag pattern", [&] { eventFlag.pattern ^= 1; }},
    {"callback name", [&] { cb.name += "x"; }},
    {"callback function", [&] { cb.function ^= 4; }}, {"callback argument", [&] { cb.argument ^= 1; }},
    {"callback thread", [&] { cb.thread ^= 1; }}, {"callback notifyCount", [&] { cb.notifyCount ^= 1; }},
    {"callback notifyArg", [&] { cb.notifyArg ^= 1; }},
    {"exitCallback", [&] { k.exitCallback ^= 1; }}, {"memoryStickCallbacks", [&] { k.memoryStickCallbacks[0] ^= 1; }},
    {"umdCallback", [&] { k.umdCallback ^= 1; }},
    {"block uid", [&] { block.uid = k.nextUID++; }}, {"block name", [&] { block.name += "x"; }},
    {"block address", [&] { block.address ^= 0x100; }}, {"block size", [&] { block.size ^= 0x100; }},
    {"largeMemory", [&] { k.largeMemory = true; }}, {"sdkVersion", [&] { k.sdkVersion ^= 1; }},
    {"compilerVersion", [&] { k.compilerVersion ^= 1; }},
    {"power callbacks", [&] { k.powerState.callbacks[5] = callback; }},
    {"power pll", [&] { k.powerState.pll = 333; }},
    {"power cpu", [&] { k.powerState.cpu = 333; }}, {"power bus", [&] { k.powerState.bus = 166; }},
    {"power volatileLocked", [&] { k.powerState.volatileLocked = true; }},
    {"semaphore initial", [&] { sema.initial ^= 1; }}, {"thread suspended", [&] { t.suspended = true; }},
    //sound: channel 3 halfway through a buffer, the DMA running; the SRC channel with both buffers armed
    {"audio reserved", [&] { k.audio.channels[3].reserved = true; }},
    {"audio sampleCount", [&] { k.audio.channels[3].sampleCount = 64; }},
    {"audio format", [&] { k.audio.channels[3].format = 0x10; }},
    {"audio leftVolume", [&] { k.audio.channels[3].leftVolume = 1; }},
    {"audio rightVolume", [&] { k.audio.channels[3].rightVolume = 1; }},
    {"audio buffer", [&] { k.audio.channels[3].buffer = 0x0880'0000; }},
    {"audio length", [&] { k.audio.channels[3].length = 128; }},
    {"audio remaining", [&] { k.audio.channels[3].remaining = 64; }},
    {"dma running", [&] { k.audio.dma.running = true; }},
    {"dma nextBlock", [&] { k.audio.dma.nextBlock = k.cycles + 1000; }},
    {"dma fraction", [&] { k.audio.dma.fraction = 7; }},
    {"src reserved", [&] { k.audio.src.reserved = true; }},
    {"src sampleCount", [&] { k.audio.src.sampleCount = 17; }},
    {"src rate", [&] { k.audio.src.rate = 8'000; }},
    {"src buffer address", [&] { k.audio.src.buffers[0].address = 0x0880'1000; }},
    {"src buffer sampleCount", [&] { k.audio.src.buffers[0].sampleCount = 17; }},
    {"src buffer volume", [&] { k.audio.src.buffers[0].volume = 1; }},
    {"src second buffer", [&] { k.audio.src.buffers[1] = {0x0880'2000, 18, 2}; }},
    {"src armed", [&] { k.audio.src.armed = 2; }},
    {"src retireAt", [&] {  //as late as a buffer armed now retires: heard from SrcAhead frames on
      k.audio.src.retireAt = k.cycles + k.frameCycle(Kernel::Audio::SrcAhead) + k.srcDuration(17)
                           - Kernel::Audio::SrcLead;
    }},
    {"src completion", [&] { k.audio.src.completion = true; }},
    //what the channels have added to the output and the system hasn't taken: 64 frames from 100 before now, the
    //first holding a sample; the SRC channel 3 samples into its first buffer, as far ahead of the clock as it gets
    {"output start", [&] { k.audio.output.start = k.audio.output.end = k.sampleFrame(k.cycles) - 100; }},
    {"output end", [&] { k.audio.output.end = k.audio.output.start + 64; }},
    {"output samples", [&] { k.audio.output.samples[k.audio.output.start % Kernel::Audio::OutputFrames * 2] = 5; }},
    {"src renderedTo", [&] { k.audio.src.renderedTo = k.sampleFrame(k.cycles) + Kernel::Audio::SrcAhead; }},
    {"src position", [&] { k.audio.src.position = 3 * 44'100; }},
    {"vblank handler function", [&] { k.vblankSubs[3].function = 0x0880'3000; }},
    {"vblank handler argument", [&] { k.vblankSubs[3].argument = 1; }},
    {"vblank handler gp", [&] { k.vblankSubs[3].gp = 4; }},
    {"vblank handler enabled", [&] { k.vblankSubs[3].enabled = true; }},
    {"GE handler function", [&] { k.geSubs[31].function = 0x0880'3100; }},
    {"GE handler argument", [&] { k.geSubs[31].argument = 1; }},
    {"GE handler gp", [&] { k.geSubs[31].gp = 4; }},
    {"GE handler enabled", [&] { k.geSubs[31].enabled = true; }},
    {"vblankPending", [&] { k.vblankPending = true; }},
    {"dialog kind", [&] { k.dialog.kind = 2; }}, {"dialog status", [&] { k.dialog.status = 3; }},
    {"dialog next", [&] { k.dialog.next = 2; }}, {"dialog changeAt", [&] { k.dialog.changeAt = 5; }},
    {"dialog parameters", [&] { k.dialog.parameters = 0x0880'0000; }},
    {"utilityModules", [&] { k.utilityModules.push_back(0x301); }},
    //(each pool as a machine could leave it, which loading checks: the spare made a variable pool whole, the fixed
    //pool's block renumbered with it, its blocks halved and doubled)
    {"pool name", [&] { fixedPool.name += "x"; }}, {"pool attributes", [&] { fixedPool.attributes ^= 1; }},
    {"pool variable", [&] {
      sparePool.variable = true;
      sparePool.blockSize = 0;
      sparePool.used.clear();
    }},
    {"pool block", [&] {
      for(auto& b : k.blocks) if(b.uid == fixedPool.block) b.uid = k.nextUID;
      fixedPool.block = k.nextUID++;
    }},
    {"pool address", [&] { fixedPool.address ^= 4; }}, {"pool size", [&] { variablePool.size -= 8; }},
    {"pool blockSize", [&] {
      fixedPool.blockSize /= 2;
      fixedPool.used.resize(fixedPool.used.size() * 2);
    }},
    {"pool used", [&] { fixedPool.used[1] = 1; }},
    {"pool pieces", [&] { variablePool.pieces[variablePool.address] = 16; }},
    //sceSas: its settings, and voice 3 made a VAG voice, keyed on and released
    {"sas initialized", [&] { k.sas.initialized = true; }}, {"sas core", [&] { k.sas.core = 0x0890'4000; }},
    {"sas grain", [&] { k.sas.grain = 64; }}, {"sas voiceCount", [&] { k.sas.voiceCount = 16; }},
    {"sas outputMode", [&] { k.sas.outputMode = 1; }}, {"sas paused", [&] { k.sas.paused ^= 2; }},
    {"sas endFlags", [&] { k.sas.endFlags ^= 4; }}, {"sas effectType", [&] { k.sas.effectType = 3; }},
    {"sas effectDelay", [&] { k.sas.effectDelay = 5; }}, {"sas effectFeedback", [&] { k.sas.effectFeedback = 6; }},
    {"sas effectLeft", [&] { k.sas.effectLeft = 7; }}, {"sas effectRight", [&] { k.sas.effectRight = 8; }},
    {"sas effectDry", [&] { k.sas.effectDry = 0; }}, {"sas effectWet", [&] { k.sas.effectWet = 1; }},
    {"sas voice source", [&] { sasVoice.source = Kernel::Sas::Source::Vag; sasVoice.size = 0x100; }},
    {"sas voice address", [&] { sasVoice.address ^= 0x40; }}, {"sas voice size", [&] { sasVoice.size = 0x200; }},
    {"sas voice loop", [&] { sasVoice.loop = 1; }}, {"sas voice pitch", [&] { sasVoice.pitch = 0x2000; }},
    {"sas voice volumes", [&] { sasVoice.volumes[2] = -5; }}, {"sas voice rates", [&] { sasVoice.rates[1] = 77; }},
    {"sas voice curves", [&] { sasVoice.curves[3] = 3; }},
    {"sas voice sustainLevel", [&] { sasVoice.sustainLevel = 0x1234; }},
    {"sas voice playing", [&] { sasVoice.playing = true; }}, {"sas voice on", [&] { sasVoice.on = true; }},
    {"sas voice phase", [&] { sasVoice.phase = Kernel::Sas::Phase::Release; }},
    {"sas voice height", [&] { sasVoice.height = 0x100; }}, {"sas voice delay", [&] { sasVoice.delay = 5; }},
    {"sas voice position", [&] { sasVoice.position = 0x5000; }},
    {"sas voice loopBlock", [&] { sasVoice.loopBlock = 2; }},
    {"sas voice decoded", [&] { sasVoice.decoded[0] = -1234; }},
    {"sas voice started", [&] { sasVoice.started = true; }},
    //files: the host file opened again as another, for writing too; the other host file counted as the disc's; the
    //disc's file a folder, read a sector at a time
    {"file path", [&] { host.path = "ms0:/B.TXT"; }},
    {"file flags", [&] { host.flags |= 0x0002; }},  //PSP_O_WRONLY
    {"file position", [&] { host.position = 5; }}, {"file onDisc", [&] { k.files[other].onDisc = true; }},
    //its asynchronous request: one done, its result not taken; the other file closed asynchronously, its
    //descriptor kept for the result
    {"file async", [&] { host.async = Kernel::OpenFile::Async::Done; }},
    {"file asyncDoneAt", [&] { host.asyncDoneAt = k.cycles + 1000; }},
    {"file asyncResult", [&] { host.asyncResult = 0xffff'ffff'8002'0323ull; }},
    {"file asyncCallback", [&] { host.asyncCallback = callback; }},
    {"file asyncArgument", [&] { host.asyncArgument = 0x55; }},
    {"file resultOnly", [&] {
      auto& closed = k.files[other];
      closed.resultOnly = true;
      closed.flags = 0;
      closed.async = Kernel::OpenFile::Async::Pending;
      closed.asyncDoneAt = k.cycles + 2000;
    }},
    {"folder entries", [&] { hostFolder.entries[2] += "x"; }},  //".", "..", then "ONE"
    {"folder nextEntry", [&] { hostFolder.nextEntry = 1; }},
    {"disc file folder", [&] { onDisc.folder = true; }}, {"disc file sectors", [&] { onDisc.sectors = true; }},
    {"disc file sector", [&] { onDisc.sector ^= 1; }}, {"disc file size", [&] { onDisc.size ^= 1; }},
    {"disc folder entries", [&] { discList.entries[1] += "x"; }},
    {"disc folder entry name", [&] { discList.discEntries[1].name += "x"; }},
    {"disc folder entry sector", [&] { discList.discEntries[1].sector ^= 1; }},
    {"disc folder entry size", [&] { discList.discEntries[1].size ^= 1; }},
    {"disc folder entry folder", [&] { discList.discEntries[1].folder = true; }},
    {"disc folder entry date", [&] { discList.discEntries[1].date[6] ^= 1; }},
    {"disc folder nextEntry", [&] { discList.nextEntry = 2; }},
    {"nextFile", [&] { k.nextFile += 10; }}, {"workingDirectory", [&] { k.workingDirectory = "ms0:/LIST"; }},
    //the controller, the display
    {"buttons", [&] { c.buttons ^= 1; }}, {"analogX", [&] { c.analogX ^= 1; }}, {"analogY", [&] { c.analogY ^= 1; }},
    {"cycle", [&] { c.cycle = 5555; }}, {"mode", [&] { c.mode ^= 1; }},
    {"nextSample", [&] { c.nextSample = k.cycles + 1000; }},
    {"sample time", [&] { c.samples[10].time ^= 1; }}, {"sample buttons", [&] { c.samples[10].buttons ^= 1; }},
    {"sample x", [&] { c.samples[10].x ^= 1; }}, {"sample y", [&] { c.samples[10].y ^= 1; }},
    {"next", [&] { c.next = 5; }}, {"unread", [&] { c.unread = 7; }}, {"sampled", [&] { c.sampled ^= 1; }},
    {"latch made", [&] { c.latch.made ^= 1; }}, {"latch broken", [&] { c.latch.broken ^= 1; }},
    {"latch held", [&] { c.latch.held ^= 1; }}, {"latch released", [&] { c.latch.released ^= 1; }},
    {"latch samples", [&] { c.latch.samples ^= 1; }},
    {"display frameBuffer", [&] { k.display.frameBuffer ^= 0x10; }},
    {"display bufferWidth", [&] { k.display.bufferWidth ^= 0x10; }},
    {"display pixelFormat", [&] { k.display.pixelFormat ^= 1; }},
    //calls into the program
    {"call function", [&] { k.calls[0].function ^= 4; }}, {"call gp", [&] { k.calls[0].gp ^= 4; }},
    {"call arguments", [&] { k.calls[0].arguments[2] ^= 1; }},
    {"call resumesGe", [&] { k.calls[0].resumesGe = true; }}, {"call vblank", [&] { k.calls[0].vblank = true; }},
    {"interrupting", [&] { k.interrupting = true; }}, {"interruptsEnabled", [&] { k.interruptsEnabled = false; }},
    {"rescheduleAfter", [&] { k.rescheduleAfter = true; }},
    {"interruptedHalted", [&] { k.interruptedHalted = true; }},
    {"callResumesGe", [&] { k.callResumesGe = true; }},
    //the GE driver: the first list paused, the second finishing
    {"list state", [&] { l.state = GeList::State::Paused; }}, {"list started", [&] { l.started = true; }},
    {"list pausing", [&] { l.pausing = true; }}, {"list syncing", [&] { l.syncing = true; }},
    {"list address", [&] { l.registers.address ^= 4; }}, {"list stall", [&] { l.registers.stall ^= 4; }},
    {"list offset", [&] { l.registers.offset ^= 4; }}, {"list depth", [&] { l.registers.depth = 1; }},
    {"list returnAddress", [&] { l.registers.returnAddress[0] ^= 4; }},
    {"list returnOffset", [&] { l.registers.returnOffset[0] ^= 4; }},
    {"list base", [&] { l.base ^= 1; }}, {"list callback", [&] { l.callback = 3; }},
    {"list context", [&] { l.context ^= 4; }}, {"list stackLimit", [&] { l.stackLimit = 40; }},
    {"stack registers", [&] { l.stack[0].registers.address ^= 4; }},
    {"stack depth", [&] { l.stack[0].registers.depth = 2; }}, {"stack base", [&] { l.stack[0].base ^= 1; }},
    {"list signalID", [&] { l.signalID ^= 1; }},
    {"geFree's order", [&] { std::swap(k.geFree[0], k.geFree[1]); }},
    {"geCallback used", [&] { gc.used = true; }}, {"geCallback signalFunction", [&] { gc.signalFunction ^= 4; }},
    {"geCallback signalArgument", [&] { gc.signalArgument ^= 1; }},
    {"geCallback finishFunction", [&] { gc.finishFunction ^= 4; }},
    {"geCallback finishArgument", [&] { gc.finishArgument ^= 1; }}, {"geCallback gp", [&] { gc.gp ^= 4; }},
    {"geRunning", [&] { k.geRunning = list2; }}, {"geBusy", [&] { k.geBusy = true; }},
    {"geSuspended", [&] { k.geSuspended = true; }}, {"geFinishing", [&] { k.geFinishing = list2; }},
    {"geLeft", [&] { k.geLeft = 12345; }},
  };
  changes.insert(changes.end(), more.begin(), more.end());
  for(auto& [field, change] : changes) {
    auto before = save(a);
    change();
    check(__LINE__, field.c_str(), save(a) != before, true);
  }

  KernelMachine b;
  devices(b);
  auto state = save(a);
  CHECK(load(b, state), true);
  CHECK(save(b) == state, true);

  //what no machine could hold, one at a time, each into a fresh machine; then the first machine as it was
  Kernel::Thread ghost{};
  ghost.uid = 0x7777;
  auto refuses = [&](const char* what, auto&& damage) {
    damage();
    KernelMachine fresh;
    devices(fresh);
    check(__LINE__, what, load(fresh, save(a)), false);
    CHECK(load(a, state), true);
  };
  u64 period = 5555 * (Kernel::CPUFrequency / 1'000'000);  //the sampling cycle's, in cycles
  refuses("the GE three CALLs deep", [&] { ge.list.depth = 3; });
  refuses("an END that means nothing", [&] { ge.pending = GE::Stop::Stalled; });
  refuses("a thread running that isn't there", [&] { k.current = &ghost; });
  refuses("a disc folder's names without their entries", [&] { k.files[discFolder].discEntries.pop_back(); });
  refuses("a disc file open for writing", [&] { k.files[discFile].flags |= 0x0002; });
  //files' asynchronous requests as no machine has them: a state there isn't, one on a folder, one due further off
  //than any request takes, a descriptor kept for a result it hasn't got, or open for reading; a thread waiting on a
  //file with no request
  using Async = Kernel::OpenFile::Async;
  refuses("an asynchronous request state there isn't", [&] { k.files[file].async = Async(3); });
  refuses("an asynchronous request on a folder", [&] { k.files[folder].async = Async::Done; });
  refuses("an asynchronous request due in over a minute", [&] {
    k.files[discFile].async = Async::Pending;
    k.files[discFile].asyncDoneAt = k.cycles + 61 * Kernel::CPUFrequency;
  });
  refuses("an asynchronous request a frame overdue", [&] {
    k.files[discFile].async = Async::Pending;
    k.files[discFile].asyncDoneAt = k.cycles - Kernel::VblankCycles;
  });
  refuses("a descriptor for a result it hasn't got", [&] { k.files[other].async = Async::None; });
  refuses("a descriptor for a result, open for reading", [&] { k.files[other].flags = 0x0001; });
  refuses("an asynchronous callback not handed out yet", [&] { k.files[file].asyncCallback = k.nextUID; });
  refuses("a thread waiting on a file with no request", [&] {
    auto& thread = *k.threads.at(two);
    thread.status = Kernel::Status::Waiting, thread.wait = Kernel::Wait::Async, thread.waitID = discFile;
  });
  refuses("a thread waiting on a file whose request is done (nothing would wake it)", [&] {
    auto& thread = *k.threads.at(two);
    thread.status = Kernel::Status::Waiting, thread.wait = Kernel::Wait::Async, thread.waitID = file;
  });
  refuses("a wait there isn't", [&] { k.threads.at(two)->wait = Kernel::Wait(99); });
  refuses("a file number handed out twice", [&] { k.nextFile = discFolder; });
  refuses("an ID handed out twice", [&] { k.nextUID = u32(one); });
  refuses("a semaphore under another's ID", [&] { k.semaphores.begin()->second.uid ^= 1; });
  refuses("an event flag under another's ID", [&] { k.eventFlags.begin()->second.uid ^= 1; });
  refuses("a callback under another's ID", [&] { k.callbacks.begin()->second.uid ^= 1; });
  //an ID not handed out yet (nextUID's), one kind of object at a time
  refuses("a thread's ID not handed out yet", [&] {
    auto thread = std::move(k.threads.at(two));
    k.threads.erase(two);
    thread->uid = k.nextUID;
    k.threads[k.nextUID] = std::move(thread);
  });
  refuses("a semaphore's ID not handed out yet", [&] {
    auto copy = k.semaphores.begin()->second;
    copy.uid = k.nextUID;
    k.semaphores[k.nextUID] = copy;
  });
  refuses("a mutex's ID not handed out yet", [&] { k.lwMutexes[k.nextUID] = 0x0880'9100; });
  refuses("an event flag's ID not handed out yet", [&] {
    auto copy = k.eventFlags.begin()->second;
    copy.uid = k.nextUID;
    k.eventFlags[k.nextUID] = copy;
  });
  refuses("a callback's ID not handed out yet", [&] {
    auto copy = k.callbacks.begin()->second;
    copy.uid = k.nextUID;
    k.callbacks[k.nextUID] = copy;
  });
  refuses("a block's ID not handed out yet", [&] { k.blocks.back().uid = k.nextUID; });
  refuses("a thread's callback not handed out yet", [&] { k.threads.at(one)->callbackID = k.nextUID; });
  //threads' stacks as no machine has them: not a block, not its block's size, or under 0x200 bytes; two threads on
  //one; one of 4 GiB, its block too (sceKernelGetThreadStackFreeSize made room for it all); and a block below the
  //user partition
  refuses("a thread's stack that isn't a block", [&] { k.threads.at(one)->stackBlock += 0x100; });
  refuses("a thread's stack not its block's size", [&] { k.threads.at(one)->stackSize += 0x100; });
  refuses("a thread's stack of 0x100 bytes", [&] {
    auto& thread = *k.threads.at(one);
    stackOf(thread).size = thread.stackSize = 0x100;
  });
  refuses("two threads on one stack", [&] {
    auto& thread = *k.threads.at(two);
    thread.stackBlock = k.threads.at(one)->stackBlock;
    thread.stackSize = k.threads.at(one)->stackSize;
  });
  refuses("a thread's stack of 4 GiB", [&] {
    auto& thread = *k.threads.at(one);
    stackOf(thread).size = thread.stackSize = 0xffff'f000;
  });
  refuses("a block below the user partition", [&] { k.blocks.front().address = Kernel::UserMemory - 0x1000; });
  refuses("a buffer in a slot with the DMA stopped", [&] { k.audio.dma.running = false; });
  refuses("a mixer channel's count not a multiple of 64", [&] { k.audio.channels[3].sampleCount = 100; });
  refuses("a slot with more left than its buffer holds", [&] { k.audio.channels[3].remaining = 192; });
  refuses("a slot holding a buffer of 0 samples", [&] { k.audio.channels[3].length = 0; });
  refuses("a mixer volume past 0xFFFF", [&] { k.audio.channels[3].leftVolume = 0x10000; });
  refuses("a format neither stereo nor mono", [&] { k.audio.channels[3].format = 0x20; });
  refuses("the mixer's next block over a block away", [&] {
    k.audio.dma.nextBlock = k.cycles + Kernel::Audio::BlockCycles + 2;
  });
  refuses("the mixer's next block a frame overdue", [&] { k.audio.dma.nextBlock = k.cycles - Kernel::VblankCycles; });
  refuses("a block's fraction of 49 49ths", [&] { k.audio.dma.fraction = 49; });
  refuses("three buffers on the SRC channel", [&] { k.audio.src.armed = 3; });
  refuses("an SRC channel at 0 Hz", [&] { k.audio.src.rate = 0; });
  refuses("an SRC rate it doesn't take", [&] { k.audio.src.rate = 36'000; });
  refuses("SRC buffers armed with the channel released", [&] { k.audio.src.reserved = false; });
  refuses("an SRC buffer of 16 samples", [&] { k.audio.src.buffers[1].sampleCount = 16; });
  refuses("an SRC buffer retiring after its own time from SrcAhead frames on", [&] { k.audio.src.retireAt++; });
  //the output as no machine leaves it: frames ending before they start, more than the ring holds, frames the clock
  //hasn't reached taken, a sum louder than every channel at its loudest; the SRC channel past one beyond its armed
  //samples, somewhere in samples with none armed, or adding to frames further ahead than a buffer's lead takes it
  auto& output = k.audio.output;
  refuses("output frames ending before they start", [&] { output.end = output.start - 1; });
  refuses("more output frames than the ring holds", [&] { output.end = output.start + 4097; });
  refuses("output taken past the clock", [&] { output.start = output.end = k.sampleFrame(k.cycles) + 1; });
  refuses("an output sum past 2^21", [&] { output.samples[output.start % 4096 * 2 + 1] = (1 << 21) + 1; });
  refuses("the SRC channel past its samples", [&] { k.audio.src.position = (17 + 18 + 1) * 44'100; });
  refuses("the SRC channel somewhere with nothing armed", [&] { k.audio.src.armed = 0; });
  refuses("the SRC channel more than SrcAhead frames ahead", [&] { k.audio.src.renderedTo++; });
  refuses("the SRC channel a ring ahead", [&] { k.audio.src.renderedTo = k.sampleFrame(k.cycles) + 4096; });
  //(each refusal loads the machine again, threads and all: they're looked up afresh)
  auto waitsOnAudio = [&](Kernel::Thread& waiter, u32 id) {
    waiter.status = Kernel::Status::Waiting, waiter.wait = Kernel::Wait::Audio, waiter.waitID = id;
  };
  refuses("a thread waiting on a mixer channel with nothing in its slot", [&] {
    waitsOnAudio(*k.threads.at(one), 0);
  });
  refuses("two threads waiting on one mixer channel", [&] {
    for(auto& [uid, waiter] : k.threads) waitsOnAudio(*waiter, 3);
  });
  refuses("a thread waiting for an SRC buffer with only one armed", [&] {
    k.audio.src.armed = 1;
    waitsOnAudio(*k.threads.at(one), Kernel::Audio::WaitSrc);
  });
  refuses("a vertical blank handler on sub-interrupt 18, the display driver's", [&] {
    k.vblankSubs[18].function = 0x0880'3000;
  });
  //message pipes and mailboxes as their functions never leave them
  auto pipeOne = [&]() -> Kernel::MessagePipe& { return k.pipes.at(pipeID); };
  refuses("a pipe under another's ID", [&] { pipeOne().uid ^= 1; });
  refuses("a pipe's ID not handed out yet", [&] {
    auto copy = pipeOne();
    copy.uid = k.nextUID, copy.block = 0, copy.address = 0, copy.size = 0, copy.start = copy.used = 0;
    k.pipes[k.nextUID] = copy;
  });
  refuses("a pipe holding more than its buffer", [&] { pipeOne().used = pipeOne().size + 1; });
  refuses("a pipe's ring starting past its buffer", [&] { pipeOne().start = pipeOne().size; });
  refuses("a pipe without a buffer holding bytes", [&] {
    pipeOne().block = 0, pipeOne().address = 0, pipeOne().size = 0;
  });
  refuses("a pipe's buffer not at its block", [&] { pipeOne().address += 0x10; });
  refuses("a pipe's buffer bigger than its block", [&] { pipeOne().size = 0x200; });
  refuses("a pipe's block a pool's", [&] {
    auto& pool = k.pools.at(fixedID);
    auto block = std::find_if(k.blocks.begin(), k.blocks.end(), [&](auto& b) { return b.uid == pool.block; });
    pipeOne().block = block->uid, pipeOne().address = block->address, pipeOne().size = 16, pipeOne().start = 0;
    pipeOne().used = 0;
  });
  refuses("a mailbox's packet queued twice", [&] { k.mailboxes.at(mailboxID).messages.push_back(0x0880'1000); });
  refuses("a mailbox's packet where there's no memory", [&] { k.mailboxes.at(mailboxID).messages.push_back(16); });
  refuses("a thread waiting on a pipe that isn't there", [&] {
    auto& thread = *k.threads.at(two);
    thread.status = Kernel::Status::Waiting, thread.wait = Kernel::Wait::PipeSend, thread.waitID = semaphore;
  });
  refuses("a thread waiting on a pipe for more than its buffer", [&] {
    auto& thread = *k.threads.at(two);
    thread.status = Kernel::Status::Waiting, thread.wait = Kernel::Wait::PipeReceive, thread.waitID = pipeID;
    thread.waitCount = 0x81, thread.waitDone = 0;
  });
  refuses("a thread waiting on a pipe having moved it all", [&] {
    auto& thread = *k.threads.at(two);
    thread.status = Kernel::Status::Waiting, thread.wait = Kernel::Wait::PipeReceive, thread.waitID = pipeID;
    thread.waitCount = 0x10, thread.waitDone = 0x10;
  });
  refuses("a thread waiting on a pipe with no memory behind its buffer", [&] {
    auto& thread = *k.threads.at(two);
    thread.status = Kernel::Status::Waiting, thread.wait = Kernel::Wait::PipeReceive, thread.waitID = pipeID;
    thread.waitPointer = 0x10, thread.waitCount = 0x10, thread.waitDone = 0;
  });
  refuses("a thread whose callback runs, back to a pipe with no memory behind the rest of its message", [&] {
    auto& thread = *k.threads.at(two);
    auto& before = thread.waitBeforeCallback;
    before.wait = Kernel::Wait::PipeSend, before.id = pipeID;
    before.pointer = 0x0a00'0000 - 0x10, before.count = 0x40, before.done = 0x8;
  });
  refuses("a thread waiting on a mailbox that isn't there", [&] {
    auto& thread = *k.threads.at(two);
    thread.status = Kernel::Status::Waiting, thread.wait = Kernel::Wait::Mailbox, thread.waitID = semaphore;
  });
  refuses("ATRAC IDs past six", [&] { k.atracIDs = 0x40; });
  refuses("a font resolution of 0", [&] { k.fontResolution[0] = 0; });
  //sceSas as its functions never leave it
  using Sas = Kernel::Sas;
  refuses("a sas grain it doesn't take", [&] { k.sas.grain = 0x5c; });
  refuses("sas voices past 32", [&] { k.sas.voiceCount = 33; });
  refuses("a sas effect type there isn't", [&] { k.sas.effectType = 9; });
  refuses("a sas voice on that isn't playing", [&] { k.sas.voices[3].playing = false; });
  refuses("a sas envelope over the top", [&] { k.sas.voices[3].height = 0x4000'0001; });
  refuses("a sas curve there isn't", [&] { k.sas.voices[3].curves[0] = 6; });
  refuses("a sas phase there isn't", [&] { k.sas.voices[3].phase = Sas::Phase(4); });
  refuses("a sas voice waiting longer than 32 samples", [&] { k.sas.voices[3].delay = 33; });
  refuses("a sas VAG voice of 8 bytes", [&] { k.sas.voices[3].size = 8; });
  refuses("a sas PCM voice past its samples", [&] {
    auto& voice = k.sas.voices[3];
    voice.source = Sas::Source::Pcm, voice.size = 16, voice.loop = -1, voice.position = 16 << 12;
  });
  refuses("a sas PCM voice looping past its samples", [&] {
    auto& voice = k.sas.voices[3];
    voice.source = Sas::Source::Pcm, voice.size = 16, voice.loop = 16, voice.position = 0;
  });
  refuses("a pool under another's ID", [&] { k.pools.begin()->second.uid ^= 1; });
  //pools as no machine leaves them (giving a fixed pool's block back divides by its block size; handing out a
  //variable pool's room trusts its pieces to be inside it): each found afresh, as each load makes the pools anew
  auto fixedOne = [&]() -> Kernel::Pool& { return k.pools.at(fixedID); };
  auto variableOne = [&]() -> Kernel::Pool& { return k.pools.at(variableID); };
  refuses("a fixed pool's blocks of 0 bytes", [&] { fixedOne().blockSize = 0; });
  refuses("a fixed pool's blocks not adding up to it", [&] { fixedOne().used.push_back(0); });
  refuses("a fixed pool with pieces", [&] { fixedOne().pieces[fixedOne().address] = 16; });
  refuses("a variable pool with a block size", [&] { variableOne().blockSize = 16; });
  refuses("a variable pool with blocks", [&] { variableOne().used.push_back(0); });
  refuses("a pool whose block isn't there", [&] { fixedOne().block = semaphore; });
  refuses("a pool starting before its block", [&] { fixedOne().address -= 0x100; });
  refuses("a pool ending past its block", [&] { variableOne().size += 0x100; });
  refuses("a piece before its pool", [&] { variableOne().pieces[variableOne().address - 16] = 16; });
  refuses("a piece past its pool's end", [&] {
    auto& pool = variableOne();
    pool.pieces[pool.address + pool.size - 8] = 16;
  });
  refuses("pieces overlapping", [&] {
    auto& pool = variableOne();
    pool.pieces[pool.address + 16] = 16;
    pool.pieces[pool.address + 24] = 16;
  });
  refuses("a piece wrapping round", [&] { variableOne().pieces[variableOne().address + 16] = 0xffff'fff0; });
  refuses("a piece of no bytes", [&] { variableOne().pieces[variableOne().address] = 0; });
  refuses("a module under another's ID", [&] { k.modules.begin()->second.uid ^= 1; });
  refuses("a module's ID not handed out yet", [&] {
    auto copy = k.modules.begin()->second;
    copy.uid = k.nextUID;
    k.modules[k.nextUID] = copy;
  });
  refuses("a module's block not handed out yet", [&] { k.modules.begin()->second.block = k.nextUID; });
  refuses("a module's thread not handed out yet", [&] { k.modules.begin()->second.thread = k.nextUID; });
  refuses("a module status there isn't", [&] { k.modules.begin()->second.status = Kernel::ModuleStatus(9); });
  refuses("the program's ID not handed out yet", [&] { k.programUID = k.nextUID; });
  //a module as no machine has one: under the program's ID; with a thread while none of its functions runs, or
  //without one while one does, or a thread that isn't there; a block that isn't one, a thread's stack, a memory
  //pool's, another module's, or any block at all for one of Sony's
  refuses("a module under the program's ID", [&] {
    auto copy = k.modules.at(sonyID);
    copy.uid = k.programUID;
    k.modules[k.programUID] = copy;
  });
  refuses("a module's thread while it isn't starting or stopping", [&] {
    k.modules.at(moduleID).status = Kernel::ModuleStatus::Started;
  });
  refuses("a module stopping with no thread", [&] { k.modules.at(moduleID).thread = 0; });
  refuses("a module's thread that isn't there", [&] { k.modules.at(moduleID).thread = semaphore; });
  refuses("a module's block that isn't a block", [&] { k.modules.at(moduleID).block = semaphore; });
  refuses("a module's block that's a thread's stack", [&] {
    for(auto& b : k.blocks) if(b.address == k.threads.at(two)->stackBlock) k.modules.at(moduleID).block = b.uid;
  });
  refuses("a module's block that's a memory pool's", [&] { k.modules.at(moduleID).block = fixedOne().block; });
  refuses("a block two modules have", [&] { k.modules.at(sonyID).block = k.modules.at(moduleID).block; });
  //and each block one owner at most, a pool's too: two pools on one block, a pool inside a thread's stack, or inside
  //the program's block (each pool inside the block it names, as poolHolds() wants); and the program's block there
  auto movePool = [&](Kernel::Pool& pool, const Kernel::Block& to, u32 offset) {
    pool.block = to.uid;
    pool.address = to.address + offset;
  };
  auto blockOf = [&](u32 uid) -> Kernel::Block& {
    return *std::find_if(k.blocks.begin(), k.blocks.end(), [&](auto& b) { return b.uid == uid; });
  };
  refuses("two pools on one block", [&] { movePool(k.pools.at(spareID), blockOf(fixedOne().block), 0x80); });
  refuses("a pool inside a thread's stack", [&] { movePool(fixedOne(), stackOf(*k.threads.at(two)), 0); });
  refuses("a pool inside the program's block", [&] { movePool(fixedOne(), blockOf(programBlock), 0); });
  refuses("the program's block not there", [&] { k.module.segments[0].address += 0x1000; });
  refuses("a stand-in with a block", [&] {
    k.modules.at(sonyID).standIn = true;
    k.modules.at(sonyID).block = moduleBlock;  //no other module's, since the module moved to the spare block
  });
  //a host folder's names as no listing makes them: reading it would join them to its place on the host
  refuses("a folder name reaching out of its folder", [&] { k.files[folder].entries.push_back("../../etc"); });
  refuses("a folder name that's a whole path", [&] { k.files[folder].entries.push_back("/etc"); });
  refuses("a folder without its own entries first", [&] { k.files[folder].entries[0] = "ONE"; });
  refuses("a parent at a device's top", [&] {
    k.files[top].entries.insert(k.files[top].entries.begin(), {".", ".."});
  });
  refuses("a parent alone at a device's top", [&] { k.files[top].entries.push_back(".."); });
  refuses("a parent among a folder's names", [&] { k.files[folder].entries.push_back(".."); });
  refuses("a folder name no path can name", [&] { k.files[folder].entries.push_back("C:D"); });
  refuses("a folder name with a backslash", [&] { k.files[folder].entries.push_back("A\\B"); });
  refuses("IDs counted past 2^31", [&] { k.nextUID = 0xffff'ffff; });
  refuses("file numbers counted past 2^31", [&] { k.nextFile = 0xffff'ffff; });
  refuses("a clock past a century", [&] {
    k.cycles = 1ull << 60;
    k.nextVblank = k.controller.nextSample = k.cycles + 1000;
  });
  refuses("a vertical blank more than a frame ahead", [&] { k.nextVblank = k.cycles + Kernel::VblankCycles + 1; });
  refuses("a vertical blank a frame overdue", [&] { k.nextVblank = k.cycles - Kernel::VblankCycles; });
  refuses("a sampling cycle sceCtrlSetSamplingCycle refuses", [&] { k.controller.cycle = 100; });
  refuses("a sample more than a cycle ahead", [&] { k.controller.nextSample = k.cycles + period + 1; });
  refuses("a sample a frame overdue", [&] { k.controller.nextSample = k.cycles - Kernel::VblankCycles; });
  refuses("a controller wait for 64 samples", [&] {
    k.threads.at(one)->wait = Kernel::Wait::Controller;
    k.threads.at(one)->waitCount = 64;
  });
  refuses("a sample past the ring", [&] { k.controller.next = 64; });
  refuses("more unread samples than the ring keeps", [&] { k.controller.unread = 64; });
  refuses("a display mode there isn't", [&] { k.display.mode = 1; });
  refuses("a display size there isn't", [&] { k.display.width = 4096; });
  refuses("a list three CALLs deep", [&] { k.geLists[list].registers.depth = 3; });
  refuses("a SIGNAL call three CALLs deep", [&] { k.geLists[list].stack[0].registers.depth = 3; });
  refuses("a list's stack deeper than it allows", [&] { k.geLists[list].stackLimit = 0; });
  refuses("a list allowing 256", [&] { k.geLists[list].stackLimit = 256; });
  refuses("a list state there isn't", [&] { k.geLists[list].state = GeList::State(9); });
  refuses("a list running that never started", [&] {
    k.geLists[list].state = GeList::State::Running;
    k.geLists[list].started = false;
  });
  refuses("a list completed that never started", [&] {
    k.geLists[k.geFree.front()].state = GeList::State::Completed;
    k.geLists[k.geFree.front()].started = false;
  });
  refuses("a list finishing that hasn't completed", [&] { k.geFinishing = list; });
  refuses("more GE commands left than a frame's", [&] { k.geLeft = Kernel::GeBudget + 1; });
  refuses("a list queued twice", [&] { k.geQueue.push_back(list); });
  refuses("a list neither queued nor free", [&] { k.geFree.pop_back(); });
  refuses("list 64", [&] { k.geFree.back() = 64; });
  refuses("a queued list out of the queue", [&] { k.geLists[k.geFree.front()].state = GeList::State::Queued; });
  refuses("a list never queued in the queue", [&] { k.geLists[list].state = GeList::State::None; });
  refuses("the GE running a list out of the queue", [&] { k.geRunning = k.geFree.front(); });
  refuses("the GE running list 64", [&] { k.geRunning = 64; });
  refuses("a list finishing out of the queue", [&] { k.geFinishing = k.geFree.front(); });
  CHECK(save(a) == state, true);
}

//IDs and file numbers count up and are never handed out twice, so they stop short of 2^31 (a number with its top bit
//set would read as an error): what would need another fails instead, as out of memory, or with too many files open.
//A thread's stack, made first, goes again when the thread can't have an ID. A machine that has handed out every one
//still saves, and loads into another.
static auto idsRunOut() -> void {
  HostFolder stick;
  stick.put("A.TXT", "a");
  auto image = disc_image::makeIso({{"DATA.BIN", std::vector<u8>(100, 1)}});
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  m.kernel.disc = discOf(image.bytes);
  m.kernel.nextUID = Kernel::LastUID;  //one left: the stack takes it
  auto blocks = m.kernel.blocks.size();
  CHECK(m.kernel.createThread("late", 0x0880'1000, 0x20, 0x1000, 0, 0), s32(Kernel::ErrorNoMemory));
  CHECK(m.kernel.blocks.size(), blocks);
  CHECK(m.call("sceKernelCreateSema", {m.string("none"), 0, 0, 1, 0}), Kernel::ErrorNoMemory);
  CHECK(m.call("sceKernelCreateEventFlag", {m.string("none"), 0, 0, 0}), Kernel::ErrorNoMemory);
  CHECK(m.call("sceKernelCreateCallback", {m.string("none"), 0x0880'7000, 0}), Kernel::ErrorNoMemory);
  CHECK(m.call("sceKernelCreateLwMutex", {Buffer, m.string("none"), 0, 0, 0}), Kernel::ErrorNoMemory);
  CHECK(m.kernel.allocate(0x100, 0, 0, "none") == nullptr, true);
  m.kernel.nextUID = Kernel::LastUID;
  CHECK(m.call("sceKernelCreateSema", {m.string("last"), 0, 0, 1, 0}), Kernel::LastUID);
  m.kernel.nextFile = Kernel::LastUID;
  CHECK(m.call("sceIoOpen", {m.string("ms0:/A.TXT"), 0x0001, 0}), Kernel::LastUID);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/A.TXT"), 0x0001, 0}), Kernel::ErrorTooManyFiles);
  CHECK(m.call("sceIoOpen", {m.string("ms0:/NEW.TXT"), 0x0602, 0}), Kernel::ErrorTooManyFiles);
  CHECK(std::filesystem::exists(stick.path / "NEW.TXT"), false);  //refused before anything was made
  CHECK(m.call("sceIoDopen", {m.string("ms0:/")}), Kernel::ErrorTooManyFiles);
  CHECK(m.call("sceIoOpen", {m.string("disc0:/DATA.BIN"), 0x0001, 0}), Kernel::ErrorTooManyFiles);
  CHECK(m.call("sceIoDopen", {m.string("disc0:/")}), Kernel::ErrorTooManyFiles);
  serializer s;
  CHECK(m.kernel.serialize(s), true);
  KernelMachine n;
  n.kernel.mount("ms0", stick.path.string());
  n.kernel.disc = discOf(image.bytes);
  serializer load{s.data(), s.size()};
  CHECK(n.kernel.serialize(load), true);
  CHECK(n.kernel.nextUID, Kernel::LastUID + 1);
  CHECK(n.kernel.nextFile, Kernel::LastUID + 1);
}

auto stateTests() -> Tests {
  return {{"kernel states", kernelStates}, {"state fields", stateFields}, {"kernel ids run out", idsRunOut}};
}

}
