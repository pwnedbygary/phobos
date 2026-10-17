//The GE profile (kernel.hpp's GeProfile): what tools/psp-ge-profile's plugin records on a PSP, recorded here as the
//game runs, so its decoder prints the same tables for both. The plugin sees a call through the syscall it patches,
//from the game's call to its return; here a profiled function is recorded from its start (its arguments then) to its
//end, or, if its thread waits in it, to where the thread goes on (switchTo()), its result what the wait gave it. The
//FINISH and SIGNAL callbacks it sees through the interrupt manager's monitor, as the game's handler starts and
//returns; here, as the call into the program starts and returns (interrupts.cpp), from where the driver queued it.
//Its vertical blanks are the monitor's sight of the blank's interrupt; here, each blank's own time. The GE's state at
//each record is the one sceGeDrawSync(1) would give, as the plugin samples it. And the model's charges, which the
//plugin can't see: each piece of drawing geTime() charges the GE for, its pixels and primitives, the cycles, and when
//the GE is done with all it has been given since.

auto Kernel::geProfileStart(GeProfile& profile) -> void {
  geProfile = &profile;
  profile.startTime = geProfileTime();
  profile.startVcount = vblanks;
  profile.cpuMhz = powerState.cpu;
  profile.busMhz = powerState.bus;
  profile.firmware = DevkitVersion;
}

//The calls still waiting stay unfinished (exit 0), as the plugin leaves a call it's asleep in when its capture ends.
auto Kernel::geProfileStop() -> void {
  if(!geProfile) return;
  geProfile->endTime = geProfileTime();
  geProfile->endVcount = vblanks;
  geProfile->cpuMhzEnd = powerState.cpu;
  geProfile->busMhzEnd = powerState.bus;
  geProfile = nullptr;
  callProfile = 0;
  for(auto& call : calls) call.profile = 0;
  for(auto& [uid, thread] : threads) thread->geProfileCall = thread->geProfileCallAside = 0;
}

//sceKernelGetSystemTimeLow's clock: microseconds.
auto Kernel::geProfileTime() const -> u32 {
  return u32(cycles / (CPUFrequency / 1'000'000));
}

//A record's end: never 0, which the format keeps for a call that hasn't ended (the plugin's guard alike).
static auto geProfileExit(u32 time) -> u32 {
  return time ? time : 1;
}

//sceGeDrawSync(1)'s answer (0 nothing to do, 2 drawing, 3 stopped at a stall address), with, while a list draws, the
//address of the GE's next command above it, as the plugin reads it from the GE.
auto Kernel::geProfileState() const -> u32 {
  for(u32 index : geQueue) {
    if(geLists[index].state == GeList::State::Completed) continue;
    return (ge.list.address & ~3u) | (geAtStall(index) ? 3 : 2);
  }
  return 0;
}

//The thread's place in the profile's table, added as it's first seen (255 for any past the table's 254 threads;
//0: none, or an interrupt handler).
auto Kernel::geProfileThread(const Thread* thread) -> u8 {
  if(!thread) return 0;
  auto& table = geProfile->threads;
  for(u32 n = 1; n < table.size(); n++) if(table[n].first == thread->uid) return n;
  if(table.size() >= 255) return 255;
  table.push_back({thread->uid, thread->name});
  return table.size() - 1;
}

//A record made now, unfinished: its number (and 1).
auto Kernel::geProfileRecord(u8 kind, u8 thread, u32 a, u32 b, u32 c) -> u32 {
  GeProfile::Record record;
  record.kind = kind;
  record.thread = thread;
  record.vcount = vblanks;
  record.entry = geProfileTime();
  record.ge = geProfileState();
  record.a = a, record.b = b, record.c = c;
  geProfile->records.push_back(record);
  return geProfile->records.size();
}

//A profiled function's call, as it starts: what profile.h has of its arguments. A frame buffer's format comes with
//its sync mode above it; callbacks come as their SIGNAL and FINISH functions and where the program had them, read
//only as the function reads them (with a slot free for them) and only from main RAM, where a read changes nothing
//(else 0, as the plugin leaves words it won't read).
auto Kernel::geProfileBegin(u8 kind) -> u32 {
  u32 a = arg(0), b = arg(1), c = arg(2);
  if(kind == GeProfile::SetFrameBuf) c = (arg(2) & 0xff) | (arg(3) & 0xff) << 8;
  if(kind == GeProfile::SetCallback) {
    u32 offset = 0;
    bool slot = std::any_of(std::begin(geCallbacks), std::end(geCallbacks), [](auto& each) { return !each.used; });
    bool read = slot && !(arg(0) & 3) && memory.ramOffset(arg(0), 12, offset);
    a = read ? memory.read(4, arg(0)) : 0, b = read ? memory.read(4, arg(0) + 8) : 0, c = arg(0);
  }
  return geProfileRecord(kind, geProfileThread(interrupting ? nullptr : current), a, b, c);
}

//As it ends: finished now with its result, unless its thread waits in it, then as the thread goes on: a wait it's
//still in, or was made ready in only to run its callbacks (the wait kept), or whose callbacks it's running now,
//started in the call, its wait or its return put aside till they're done (events.cpp's runCallbacks() and
//backFromCallbacks()). A call one of those callbacks makes is its own. A thread that didn't wait but lost the CPU to
//a better one it woke has its result in its context.
auto Kernel::geProfileEnd(u32 record, Thread* caller, bool fromCallback) -> void {
  if(!geProfile || record > geProfile->records.size()) return;
  if(caller && (caller->status == Status::Waiting || caller->wait != Wait::None)) {
    caller->geProfileCall = record;
    return;
  }
  if(caller && caller->inCallback && !fromCallback) {
    caller->geProfileCallAside = record;
    return;
  }
  auto& r = geProfile->records[record - 1];
  r.exit = geProfileExit(geProfileTime());
  r.result = caller && caller != current ? caller->context.gpr[2] : cpu.ipu.r[2];
}

//A thread back from the wait a profiled call made it wait in, or from the callbacks the call ran: the call returns
//now, with what it gave (v0, the thread's registers just loaded).
auto Kernel::geProfileResumed(Thread& thread) -> void {
  u32 record = thread.geProfileCall;
  thread.geProfileCall = 0;
  if(!geProfile || record > geProfile->records.size()) return;
  auto& r = geProfile->records[record - 1];
  r.exit = geProfileExit(geProfileTime());
  r.result = cpu.ipu.r[2];
}

//A vertical blank: its own start, however late the kernel's loop came to it, and the count it makes.
auto Kernel::geProfileVblank() -> void {
  u32 record = geProfileRecord(GeProfile::Vblank, 0, vblanks, 0, 0);
  auto& r = geProfile->records[record - 1];
  r.entry = u32((nextVblank - VblankCycles) / (CPUFrequency / 1'000'000));
  r.exit = geProfileExit(r.entry);
}

//A list's callback, as the driver queues it (geCall()): its callback number, the FINISH's or SIGNAL's low 16 bits,
//where the list had got to (past the END after them), and a SIGNAL's own word. Its times are the call's (below).
auto Kernel::geProfileCallback(bool finish, const GeList& list, u32 id) -> u32 {
  u32 record = geProfileRecord(finish ? GeProfile::Finish : GeProfile::Signal, 0, u32(list.callback), id,
                               ge.list.address);
  if(!finish) geProfile->records[record - 1].result = ge.signalWord;
  return record;
}

auto Kernel::geProfileCallStarted(u32 record) -> void {
  if(!geProfile || record > geProfile->records.size()) return;
  geProfile->records[record - 1].entry = geProfileTime();
}

auto Kernel::geProfileCallReturned(u32 record) -> void {
  if(!geProfile || record > geProfile->records.size()) return;
  geProfile->records[record - 1].exit = geProfileExit(geProfileTime());
}

//A piece of drawing the GE was charged for (geTime()): its pixels and primitives, the cycles, when the GE is done with
//all it has, and the list it's in.
auto Kernel::geProfileWork(u64 pixels, u64 primitives, u64 owed) -> void {
  auto fit = [](u64 value) { return u32(std::min<u64>(value, 0xffff'ffff)); };
  u32 record = geProfileRecord(GeProfile::ModelWork, geProfileThread(interrupting ? nullptr : current),
                               fit(pixels), fit(primitives), fit(owed));
  auto& r = geProfile->records[record - 1];
  r.exit = geProfileExit(u32(geDoneAt / (CPUFrequency / 1'000'000)));
  r.result = geRunning >= 0 ? GeListIDs + geRunning : 0xffff'ffff;
}

//profile.h's layout, little-endian. The flags: the interrupt kinds are there (the plugin's monitor), the recording
//started where the host asked (a time, not the buttons), and the GE's state sampled at each record. Every kind's
//"hook" is in place; ARK's version 0, the model ~0 (none: neither is the plugin's to know here).
auto GeProfile::file(const std::string& version, const std::string& discId, const std::string& title) const
  -> std::vector<u8> {
  std::vector<u8> out;
  auto word = [&](u32 value) { for(u32 n = 0; n < 4; n++) out.push_back(value >> n * 8); };
  auto text = [&](const std::string& value, u32 size) {
    for(u32 n = 0; n < size; n++) out.push_back(n + 1 < size && n < value.size() ? value[n] : 0);
  };
  u32 count = records.size(), table = 256 + count * 32;
  for(char c : std::string("PHGEPROF")) out.push_back(c);
  word(1), word(256), word(32), word(count);
  word(2 | 4 | 8), word(0);
  text(version, 16), text(discId, 16), text(title, 64);
  word(startTime), word(endTime), word(startVcount), word(endVcount);
  word(cpuMhz), word(busMhz), word(cpuMhzEnd), word(busMhzEnd);
  word(firmware), word(0), word(0), word(0xffff'ffff);
  word(0x801f'fffe);  //kinds 1 to 20 (the vertical blank's with the monitor's), and the monitor (31), as the PSP's
  word(1), word(0), word(count), word(table), word(threads.size()), word(0), word(count * 32);
  for(u32 n = 0; n < 12; n++) word(0);
  for(auto& r : records) {
    out.push_back(r.kind), out.push_back(r.thread), out.push_back(r.vcount), out.push_back(r.vcount >> 8);
    word(r.entry), word(r.exit), word(r.ge), word(r.a), word(r.b), word(r.c), word(r.result);
  }
  for(auto& [uid, name] : threads) word(uid), text(name, 32);
  return out;
}
