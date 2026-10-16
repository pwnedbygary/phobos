//The GE on a thread of its own: thread.hpp says how the kernel's GE and the render GE share the work.

thread_local bool GE::onGeThread = false;

GeThread::GeThread(GE& control, GE& render) : control(control), render(render) {
  words.reserve(WordsMost);
  versions = std::make_unique<GeVersions>(&GE::hostPage, &control.memory);
  render.versions = versions.get();
  render.reporting = this;
  if(const char* check = std::getenv("PHOBOS_GE_CHECK")) checking = check[0] && check[0] != '0';
  //(the render GE's watches are pages the kernel's GE's memory watches already: textures' pages, which it watches as
  //it hands their PRIMs over, and VRAM's, all out of the CPU's page table while the thread is on, its code run by the
  //interpreter. So none needs telling it, nor do its changes to VRAM: watching only so that the render GE keeps
  //textures decoded, Memory::canWatch())
  render.memory.watching = [](u32) {};
  render.memory.unwatched = nullptr;
  render.memory.written = nullptr;
  render.memory.unmapped = nullptr;
  thread = std::thread([this] { take(); });
}

GeThread::~GeThread() {
  drain();
  ring.stop();
  thread.join();
  render.memory.watching = nullptr;
  render.memory.written = nullptr;
  render.versions = nullptr;
  render.reporting = nullptr;
}

auto GeThread::append(Kind kind, u32 size) -> u8* {
  u8* at = ring.reserve(sizeof(Header) + size);
  if(!at) return nullptr;
  Header header{kind, size, ++serial};
  std::memcpy(at, &header, sizeof(header));
  return at + sizeof(header);
}

//(a list word as run() read it, at its address, or ~0 for one from no list (the kernel's): runs of words one after
//another kept as a run)
auto GeThread::word(u32 word, u32 address) -> void {
  if(words.size() == WordsMost) flushWords();
  if(wordRuns.empty() || address == ~0u || wordRuns.back().first == ~0u ||
     ((wordRuns.back().first + wordRuns.back().second * 4) & 0x0fff'ffff) != address) {
    wordRuns.push_back({address, 0});
  }
  wordRuns.back().second++;
  words.push_back(word);
}

auto GeThread::flushWords() -> void {
  if(words.empty()) return;
  u32 count = words.size(), runCount = wordRuns.size();
  u8* at = append(Kind::Words, sizeof(u32) + runCount * 2 * sizeof(u32) + count * sizeof(u32));
  std::memcpy(at, &runCount, sizeof(u32));
  for(u32 n = 0; n < runCount; n++) {
    u32 run[2] = {wordRuns[n].first, wordRuns[n].second};
    std::memcpy(at + sizeof(u32) + n * sizeof(run), run, sizeof(run));
  }
  std::memcpy(at + sizeof(u32) + runCount * 2 * sizeof(u32), words.data(), count * sizeof(u32));
  ring.publish();
  words.clear(), wordRuns.clear();
}

//(a PRIM's or curved surface's record: its word, addresses and copies; one too big for the ring is drawn by the
//emulation thread, the render GE's code there and then, once the thread has drained)
static auto copiedBytes(const GeThread::Copies& copies) -> u32 {
  u32 bytes = 0;
  for(u32 n = 0; n < copies.ranges; n++) bytes += copies.size[n];
  return bytes;
}

auto GeThread::primitive(u32 word, u32 wordAddress, u32 vertexAddress, u32 indexAddress, bool hardware, bool certain,
                         const Copies& copies, const u8* bytes) -> u64 {
  flushWords();
  bool patching = word >> 24 != GE::Primitive;
  u32 size = 6 * sizeof(u32) + sizeof(Copies) + copiedBytes(copies);
  u8* at = append(patching ? Kind::Patch : Kind::Primitive, size);
  if(!at) {  //(as the record it would be, there and then: the place of the next)
    drain();
    if(wordAddress != ~0u) listWordDrawn(wordAddress);
    if(checking) render.modelPrimitive(replica, word, vertexAddress, indexAddress, copies, bytes, hardware, serial + 1);
    render.replayPrimitive(word, vertexAddress, indexAddress, copies, bytes, patching);
    undrawn = ~0u;
    return serial + 1;
  }
  u32 head[6] = {word, wordAddress, vertexAddress, indexAddress, hardware, certain};
  std::memcpy(at, head, sizeof(head));
  std::memcpy(at + sizeof(head), &copies, sizeof(Copies));
  std::memcpy(at + sizeof(head) + sizeof(Copies), bytes, copiedBytes(copies));
  ring.publish();
  return serial;
}

auto GeThread::clut(u32 word, u32 wordAddress, u32 address, const u8* bytes, u32 size) -> void {
  flushWords();
  u8* at = append(Kind::Clut, 4 * sizeof(u32) + size);
  u32 head[4] = {word, wordAddress, address, size};
  std::memcpy(at, head, sizeof(head));
  std::memcpy(at + sizeof(head), bytes, size);
  ring.publish();
}

auto GeThread::drawnFirst(u32 address, u32 size) -> u64 {
  flushWords();
  u32 head[2] = {address, size};
  std::memcpy(append(Kind::DrawnFirst, sizeof(head)), head, sizeof(head));
  ring.publish();
  return serial;
}

auto GeThread::read(u32 address, u32 size, bool loading) -> u64 {
  flushWords();
  u32 head[3] = {address, size, loading};
  std::memcpy(append(Kind::Read, sizeof(head)), head, sizeof(head));
  ring.publish();
  return serial;
}

//(a write's record: its address, size and place (cx), and, checking, the VRAM bytes it wrote, as the model was told
//of them: wroteVRAM())
auto GeThread::write(u32 address, u32 size, Kind kind, u64 cx) -> u64 {
  flushWords();
  u32 ranges = written.size(), head[3] = {address, size, ranges};
  u8* at = append(kind, sizeof(head) + sizeof(cx) + ranges * 2 * sizeof(u32));
  std::memcpy(at, head, sizeof(head));
  std::memcpy(at + sizeof(head), &cx, sizeof(cx));
  for(u32 n = 0; n < ranges; n++) {
    u32 range[2] = {written[n].first, written[n].second};
    std::memcpy(at + sizeof(head) + sizeof(cx) + n * sizeof(range), range, sizeof(range));
  }
  ring.publish();
  written.clear();
  return serial;
}

auto GeThread::transfer(u64 cx) -> u64 {
  return write(0, 0, Kind::Transfer, cx);
}

auto GeThread::before(u32 address, u32 size, Memory::Writer::Kind kind) -> void {
  flushWords();
  takeReports();
  writePlace = 0;
  if(conflicts(address, size)) {
    writePlace = write(address, size, kind == Memory::Writer::Kind::Store ? Kind::Store : Kind::Copy, serial + 1);
    return waitFor(writePlace);
  }
  //(where the render GE may still read or write back the bytes at an earlier place, or the write's own record's
  //checks, which come before it in the stream's order though made after it, may finish over them)
  if(behind() || gpuBusy(address, size)) save(address, size, 2 * (serial + 1));  //(the record after() appends next)
}

auto GeThread::after(u32 address, u32 size, Memory::Writer::Kind kind) -> void {
  if(!writePlace) write(address, size, kind == Memory::Writer::Kind::Store ? Kind::Store : Kind::Copy, serial);
  else wrote(writePlace);  //(its record went first: the bytes it wrote told after)
  writePlace = 0;
  protect(address, size);
  versions->free(2 * done.load(std::memory_order_acquire));
}

//(the pages of a write that a texture the render GE may still decode from is in, watched again after the write's
//changed() let them go: VRAM's are heard of anyway, out of the CPU's page table)
auto GeThread::protect(u32 address, u32 size) -> void {
  u32 first, last, physical = address & 0x1fff'ffff;
  if(physical >= Memory::VRAMBase && physical - Memory::VRAMBase < Memory::VRAMWindow) return;
  if(!control.memory.pagesOf(address, size, first, last)) return;
  u64 passed = done.load(std::memory_order_acquire);
  for(u32 page = first; page <= last && page < textureUntil.size(); page++) {
    if(textureUntil[page] > passed && !control.memory.watched[page]) control.memory.watch(page * Memory::PageSize, 1);
  }
}

//(checking: the VRAM bytes written since a record that went before them, with their place, for the render GE's model)
auto GeThread::wrote(u64 at) -> void {
  for(auto [first, last] : written) {
    u32 head[2] = {first, last};
    u8* to = append(Kind::Wrote, sizeof(head) + sizeof(at));
    std::memcpy(to, head, sizeof(head));
    std::memcpy(to + sizeof(head), &at, sizeof(at));
    ring.publish();
  }
  written.clear();
}

//(where a write lands on pixels the drawing threads may still draw, or a texture a PRIM the software renderer may still
//draw reads from memory as it's drawn: the model's, the reports taken)
auto GeThread::conflicts(u32 address, u32 size) -> bool {
  u32 first, last;
  if(!model.softwareTextures.empty() && control.memory.pagesOf(address, size, first, last)) {
    for(auto& t : model.softwareTextures) {
      if(t.at > model.drawnKnown && t.first <= last && first <= t.last) return true;
    }
  }
  u32 physical = address & 0x1fff'ffff;
  if(physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMWindow) return false;
  u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize, seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
  u32 end = std::min<u32>(seen + size, Memory::VRAMSize);
  for(u32 at = seen; at < end;) {  //(through a rearranging copy, each piece's bytes)
    u32 run = copy & 1 ? std::min(end - at, 32 - at % 32) : end - at;
    u32 offset = copy & 1 ? Memory::vramOffset(copy, at) : at;
    if(model.software.newerThan(offset, offset + run - 1, model.drawnKnown)) return true;
    at += run;
  }
  return false;
}

//(whether size bytes from address lie on pages of VRAM the GPU may have drawn in since the last finish reported, the
//pages a finish may write back: through a rearranging copy, each piece's; the reports taken)
auto GeThread::gpuBusy(u32 address, u32 size) -> bool {
  u32 physical = address & 0x1fff'ffff;
  if(physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMWindow) return false;
  u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize, seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
  u32 end = std::min<u32>(seen + size, Memory::VRAMSize);
  for(u32 at = seen; at < end;) {
    u32 run = copy & 1 ? std::min(end - at, 32 - at % 32) : end - at;
    u32 offset = copy & 1 ? Memory::vramOffset(copy, at) : at;
    for(u32 page = offset >> 12; page <= (offset + run - 1) >> 12; page++) {
      if(model.gpu.pageNewest[page] > model.finishKnown) return true;
    }
    at += run;
  }
  return false;
}

//(the value rule over size bytes from address, the reports taken: VRAM's, through any copy; the rest's never dirty)
auto GeThread::dirtyRange(u32 address, u32 size) -> bool {
  u32 physical = address & 0x1fff'ffff;
  if(physical < Memory::VRAMBase || physical - Memory::VRAMBase >= Memory::VRAMWindow) return false;
  u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize, seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
  u32 end = std::min<u32>(seen + size, Memory::VRAMSize);
  for(u32 at = seen; at < end;) {
    u32 run = copy & 1 ? std::min(end - at, 32 - at % 32) : end - at;
    u32 offset = copy & 1 ? Memory::vramOffset(copy, at) : at;
    if(model.dirty(offset, offset + run - 1)) return true;
    at += run;
  }
  return false;
}

//(the old contents of size bytes from address kept as versions with the place at, each page's piece: VRAM's through
//any copy as the bytes lie, the first copy's pages; RAM's and the scratchpad's as they are)
auto GeThread::save(u32 address, u32 size, u64 at) -> void {
  u32 physical = address & 0x1fff'ffff;
  auto pieces = [&](u32 base, u32 offset, u32 count) {  //(bytes offset to offset + count in an area at base)
    for(u32 done = 0; done < count;) {
      u32 page = (base + offset + done) / GeVersions::PageBytes, within = (base + offset + done) % GeVersions::PageBytes;
      u32 part = std::min(count - done, GeVersions::PageBytes - within);
      versions->save(page, within, part, at);
      done += part;
    }
  };
  if(physical >= Memory::VRAMBase && physical - Memory::VRAMBase < Memory::VRAMWindow) {
    u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize, seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
    u32 end = std::min<u32>(seen + size, Memory::VRAMSize);
    for(u32 offset = seen; offset < end;) {
      u32 run = copy & 1 ? std::min(end - offset, 32 - offset % 32) : end - offset;
      pieces(Memory::VRAMBase, copy & 1 ? Memory::vramOffset(copy, offset) : offset, run);
      offset += run;
    }
  } else if(physical >= Memory::RAMBase && physical - Memory::RAMBase < control.memory.ram.size()) {
    pieces(Memory::RAMBase, physical - Memory::RAMBase,
           u32(std::min<u64>(size, control.memory.ram.size() - (physical - Memory::RAMBase))));
  } else if(physical >= Memory::ScratchpadBase && physical - Memory::ScratchpadBase < Memory::ScratchpadSize) {
    pieces(Memory::ScratchpadBase, physical - Memory::ScratchpadBase,
           std::min<u32>(size, Memory::ScratchpadSize - (physical - Memory::ScratchpadBase)));
  }
}

//The emulation thread's write to VRAM's bytes first to last (offsets), as it makes it: their place now (cx), and,
//checking, the render GE's copy of the model told (inside a write it hears of from a record, by that record).
auto GeThread::wroteVRAM(u32 first, u32 last) -> void {
  flushWords();
  model.write(first, last, serial);
  if(!checking) return;
  if(control.memory.recording) return written.push_back({first, last});
  u32 head[2] = {first, last};
  u64 at = serial;
  u8* to = append(Kind::Wrote, sizeof(head) + sizeof(at));
  std::memcpy(to, head, sizeof(head));
  std::memcpy(to + sizeof(head), &at, sizeof(at));
  ring.publish();
}

auto GeThread::runStart() -> void {
  flushWords();
  dirtyPage = ~0u;
  runs++;
  for(u64 ended; runs - (ended = runsEnded.load(std::memory_order_acquire)) > RunsKept;) {
    runsEnded.wait(ended, std::memory_order_acquire);
  }
  append(Kind::RunStart, 0);
  ring.publish();
}

auto GeThread::runEnd() -> void {
  flushWords();
  append(Kind::RunEnd, 0);
  ring.publish();
}

auto GeThread::state(const GE& from) -> void {
  flushWords();
  GE::State state = from.stateOf();
  std::memcpy(append(Kind::State, sizeof(state)), &state, sizeof(state));
  ring.publish();
}

auto GeThread::owedOf(u64 run, bool wait, Owed& owed) -> bool {
  u64 ended = runsEnded.load(std::memory_order_acquire);
  if(ended < run && !wait) return false;
  if(ended < run) flushWords();
  while(ended < run) runsEnded.wait(ended, std::memory_order_acquire), ended = runsEnded.load(std::memory_order_acquire);
  owed = owedRuns[run % RunsKept];
  return true;
}

auto GeThread::owedSoFar(u64 run, Owed& owed) const -> bool {
  u64 before = soFarVersion.load(std::memory_order_acquire);
  if(before & 1) return false;
  u64 at = soFarRun.load(std::memory_order_relaxed);
  owed.pixels = soFarPixels.load(std::memory_order_relaxed);
  owed.primitives = soFarPrimitives.load(std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_acquire);
  return at == run && soFarVersion.load(std::memory_order_relaxed) == before;
}

//(the GE thread's) the run under way's counts so far
auto GeThread::publishSoFar() -> void {
  soFarVersion.fetch_add(1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  soFarRun.store(takenRuns, std::memory_order_relaxed);
  soFarPixels.store(render.owedPixels, std::memory_order_relaxed);
  soFarPrimitives.store(render.owedPrimitives, std::memory_order_relaxed);
  soFarVersion.fetch_add(1, std::memory_order_release);
}

//The value rule, the emulation thread's side (model.hpp): what the render GE has reported taken into the model (the
//records it has done, its finishes, the software pixels it has drawn, its depth put back; the put points it has passed
//let go), and the emulation thread's reads of VRAM looked at by it.
auto GeThread::takeReports() -> void {
  model.passed = done.load(std::memory_order_acquire);
  model.finishKnown = std::max(model.finishKnown, finishReported.load(std::memory_order_acquire));
  model.drawnKnown = std::max(model.drawnKnown, drawnReported.load(std::memory_order_acquire));
  if(u64 made = puts.load(std::memory_order_acquire); made != putsTaken) {
    putsTaken = made;
    for(u32 block = 0; block < GeModel::Blocks; block++) {
      model.putKnown[block] = std::max(model.putKnown[block], putReported[block].load(std::memory_order_relaxed));
    }
  }
  if(!model.putPoints.empty()) {
    std::erase_if(model.putPoints, [&](const GeModel::PutPoint& point) { return point.at <= model.passed; });
  }
  if(lostReported.load(std::memory_order_acquire)) hardware = false;
  if(uncertain.load(std::memory_order_acquire)) model.unsure = true;
  if(!model.softwareTextures.empty()) {
    std::erase_if(model.softwareTextures, [&](const GeModel::TexturePages& t) { return t.at <= model.drawnKnown; });
  }
  if(versions->versions()) versions->free(2 * model.passed);
}

auto GeThread::dirty(u32 first, u32 last) -> bool {
  takeReports();
  return model.dirty(first, last);
}

auto GeThread::waitFor(u64 at) -> void {
  flushWords();
  if(done.load(std::memory_order_acquire) < at) waits++;
  for(u64 now; (now = done.load(std::memory_order_acquire)) < at;) done.wait(now, std::memory_order_acquire);
  takeReports();
}

//A read of VRAM (Memory::reading: the CPU's loads, an HLE function's; loading: a load, read()'s, whose check through
//VRAM's depth views asks only about the drawing threads): handed over where pointer()'s check could act in the render
//GE's memory (its pages busy there, as the model knows it), waited for where the render GE may still change the bytes;
//else read as memory has them.
auto GeThread::load(u32 address, u32 size, bool loading) -> void {
  u32 physical = address & 0x1fff'ffff;
  u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize, seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
  u32 end = std::min<u32>(seen + size, Memory::VRAMSize);
  if(end <= seen) return;
  u32 low = seen, high = end - 1;  //(the pages pointer()'s check looks at: Memory::busyCheck())
  if(copy & 1) low &= ~0x3fffu, high |= 0x3fff;
  takeReports();
  bool busy = false;
  for(u32 page = low >> 12; page <= high >> 12; page++) busy = busy || model.busy(page);
  bool dirtied = false;
  for(u32 at = seen; at < end && !dirtied;) {  //(the bytes themselves: through a rearranging copy, each piece's)
    u32 run = copy & 1 ? std::min(end - at, 32 - at % 32) : end - at;
    u32 offset = copy & 1 ? Memory::vramOffset(copy, at) : at;
    dirtied = model.dirty(offset, offset + run - 1);
    at += run;
  }
  if(!busy && !dirtied) return;
  u64 record = read(address, size, loading);
  if(dirtied) waitFor(record);
}

//A list word the control pass is about to read at address (run()): whether its page is all clean, its words then read
//without asking again till the next PRIM; else the word looked at, and where it's dirty, the drawnFirst() the render
//GE makes for it handed over first, and waited for. (The render GE makes run()'s drawnFirst() for every word itself,
//from the words' addresses: listWordDrawn().)
auto GeThread::listWord(u32 address) -> bool {
  u32 first, last;
  if(!vramSpan(address & ~0xfffu, 0x1000, first, last)) return true;  //(RAM: the render GE never changes it)
  takeReports();
  if(address >> 12 != dirtyPage) {
    if(!model.dirty(first, last)) return true;
    dirtyPage = address >> 12;
  }
  vramSpan(address, 4, first, last);
  if(model.dirty(first, last)) waitFor(drawnFirst(address, 4));
  return false;
}

//Bytes the control pass reads (a PRIM's vertices and indices, a palette) after the drawnFirst() the render GE makes for
//them as it does the record: where the render GE may still change them, that drawnFirst() handed over first, and
//waited for (the render GE's own then finds nothing left to do).
auto GeThread::readClean(u32 address, u32 size) -> void {
  u32 first, last;
  if(!vramSpan(address, size, first, last)) return;
  if(dirty(first, last)) waitFor(drawnFirst(address, size));
}

//The control pass's own drawnFirst() (a bounding box's vertices: no record of its own), of VRAM's bytes first to last:
//handed over where the render GE's drawing may reach them (pages busy there), waited for where it may still change
//them.
auto GeThread::controlRead(u32 address, u32 size, u32 first, u32 last) -> void {
  takeReports();
  bool busy = false;
  for(u32 page = first >> 12; page <= last >> 12; page++) busy = busy || model.busy(page);
  bool dirtied = model.dirty(first, last);
  if(!busy && !dirtied) return;
  u64 at = drawnFirst(address, size);
  if(dirtied) waitFor(at);
}

auto GeThread::reportPuts(const std::bitset<GeModel::Blocks>& blocks, u64 place) -> void {
  for(u32 block = 0; block < GeModel::Blocks; block++) {
    if(blocks[block]) putReported[block].store(place ? place - 1 : 0, std::memory_order_relaxed);
  }
  puts.fetch_add(1, std::memory_order_release);
}

//(checking, the render GE's) A byte of VRAM a finish or a depth put-back changes as it does the record at place: it
//must be dirty for the model as the emulation thread may have it there, knowing of every finish and put-back before.
auto GeThread::checkChanged(u32 offset) -> void {
  if(replica.dirty(offset, offset)) return;
  std::fprintf(stderr, "PSP GE thread: VRAM's byte %06x changed by the render GE at record %llu, clean for the "
               "emulation thread (the value rule's check: ge/model.hpp)\n", offset, (unsigned long long)render.place);
  std::abort();
}

auto GeThread::hook(Memory& memory, Memory& renderMemory) -> void {
  memory.drawer = [this, &renderMemory]() -> Memory& { drain(); return renderMemory; };
  memory.reading = [this](u32 address, u32 size, bool loading) { load(address, size, loading); };
  memory.wroteVRAM = [this](u32 first, u32 last) { wroteVRAM(first, last); };
  memory.writer = this;
  rendererChanged();
}

auto GeThread::unhook(Memory& memory) -> void {
  memory.drawer = nullptr, memory.reading = nullptr, memory.wroteVRAM = nullptr;
  memory.writer = nullptr, memory.recording = false;
}

auto GeThread::rendererChanged() -> void {
  hardware = render.renderer && render.renderer->ready();
  model.scaled = replica.scaled = render.renderer && render.renderer->scale() > 1;
}

auto GeThread::drain() -> void {
  flushWords();
  for(u64 at; (at = done.load(std::memory_order_acquire)) != serial;) done.wait(at, std::memory_order_acquire);
  for(auto& text : notes) control.note(text);  //(each once, whichever GE noted it)
  notes.clear();
  rendererChanged();  //(its resolution, or the GPU lost, as the render GE has it now)
  if(versions->versions()) versions->free(2 * serial);  //(it has passed them all)
}

auto GeThread::take() -> void {
  GE::onGeThread = true;
  while(const u8* at = ring.next()) {
    Header header;
    std::memcpy(&header, at, sizeof(header));
    render.place = header.serial;
    if(checking) replica.passed = header.serial - 1;
    if(gate) gate(header.serial);
    replay(header, at + sizeof(header));
    ring.release();
    //(every software pixel of the records so far in memory: none waits to be drawn)
    if(render.drawnAll()) {
      drawnReported.store(header.serial, std::memory_order_release);
      if(checking) replica.drawnKnown = header.serial;
    }
    done.store(header.serial, std::memory_order_release);
    done.notify_all();
  }
}

//(the render GE) run()'s drawnFirst() for a list word it read at address: where its page isn't the one last found
//with nothing drawn over it (since the list's last PRIM, curved surface or block transfer, or its start)
auto GeThread::listWordDrawn(u32 address) -> void {
  if(address >> 12 == undrawn) return;
  render.drawnFirst(address, 4);
  undrawn = render.undrawnPage(address) ? address >> 12 : ~0u;
}

auto GeThread::replay(const Header& header, const u8* data) -> void {
  switch(header.kind) {
  case Kind::Words: {
    u32 runCount;
    std::memcpy(&runCount, data, sizeof(u32));
    const u8* word = data + sizeof(u32) + runCount * 2 * sizeof(u32);
    for(u32 n = 0; n < runCount; n++) {
      u32 run[2];
      std::memcpy(run, data + sizeof(u32) + n * sizeof(run), sizeof(run));
      for(u32 k = 0; k < run[1]; k++, word += sizeof(u32)) {
        u32 value;
        std::memcpy(&value, word, sizeof(u32));
        if(run[0] != ~0u) listWordDrawn((run[0] + k * 4) & 0x0fff'ffff);
        render.apply(value);
        if(value >> 24 == GE::TransferStart) undrawn = ~0u;
      }
    }
    break;
  }
  case Kind::Primitive: case Kind::Patch: {
    u32 head[6];
    std::memcpy(head, data, sizeof(head));
    Copies copies;
    std::memcpy(&copies, data + sizeof(head), sizeof(Copies));
    const u8* bytes = data + sizeof(head) + sizeof(Copies);
    if(head[1] != ~0u) listWordDrawn(head[1]);
    if(checking) {  //(the model's own copy, which declares what the PRIM may read: readDeclared())
      replica.declares = true;
      render.modelPrimitive(replica, head[0], head[2], head[3], copies, bytes, head[4], header.serial);
      render.declared = &replica.interest;
    }
    u64 refusals = render.refusals, roomRefusals = render.roomRefusals;
    render.replayPrimitive(head[0], head[2], head[3], copies, bytes, header.kind == Kind::Patch);
    render.declared = nullptr;
    //(the model took the renderer as surely drawing it, so the software renderer's depth writes weren't its: a
    //refusal there is a fault in the model's certainty, which from then on takes no PRIM as sure; one for want of
    //room on the GPU, which only the host decides, is outside what's exact, as a GPU lost is: the model takes no PRIM
    //as sure from then on either)
    if(head[5] && render.roomRefusals != roomRefusals) uncertain.store(true, std::memory_order_release);
    if(head[5] && render.refusals != refusals) {
      if(checking) {
        std::fprintf(stderr, "PSP GE thread: the renderer refused a PRIM the model took as surely its own, at record "
                     "%llu (ge/model.cpp)\n", (unsigned long long)header.serial);
        std::abort();
      }
      uncertain.store(true, std::memory_order_release);
    }
    undrawn = ~0u;
    publishSoFar();
    break;
  }
  case Kind::Clut: {
    u32 head[4];
    std::memcpy(head, data, sizeof(head));
    if(head[1] != ~0u) listWordDrawn(head[1]);
    render.replayClut(head[0], head[2], data + sizeof(head), head[3]);
    break;
  }
  case Kind::DrawnFirst: {
    u32 head[2];
    std::memcpy(head, data, sizeof(head));
    render.drawnFirst(head[0], head[1]);
    break;
  }
  case Kind::Read: {
    u32 head[3];
    std::memcpy(head, data, sizeof(head));
    render.memory.busyCheck(head[0], head[1], head[2]);
    break;
  }
  case Kind::Wrote: {
    u32 head[2];
    u64 at;
    std::memcpy(head, data, sizeof(head));
    std::memcpy(&at, data + sizeof(head), sizeof(at));
    if(checking) replica.write(head[0], head[1], at);
    break;
  }
  case Kind::Store: case Kind::Copy: case Kind::Transfer: {  //(the emulation thread's write: its checks, as made there)
    u32 head[3];
    u64 cx;
    std::memcpy(head, data, sizeof(head));
    std::memcpy(&cx, data + sizeof(head), sizeof(cx));
    if(header.kind == Kind::Store) render.memory.storeChecks(head[1], head[0]);
    else if(header.kind == Kind::Copy) render.memory.copyChecks(head[0], head[1]);
    else render.transferChecks();
    for(u32 n = 0; n < head[2] && checking; n++) {
      u32 range[2];
      std::memcpy(range, data + sizeof(head) + sizeof(cx) + n * sizeof(range), sizeof(range));
      replica.write(range[0], range[1], cx);
    }
    break;
  }
  case Kind::RunStart:
    takenRuns++;
    render.owedPixels = render.owedPrimitives = 0;
    publishSoFar();
    undrawn = ~0u;
    render.runStart();
    break;
  case Kind::RunEnd:
    render.runEnd();
    owedRuns[takenRuns % RunsKept] = {render.owedPixels, render.owedPrimitives};
    render.owedPixels = render.owedPrimitives = 0;
    runsEnded.store(takenRuns, std::memory_order_release);
    runsEnded.notify_all();
    break;
  case Kind::State: {
    GE::State state;
    std::memcpy(&state, data, sizeof(state));
    render.takeState(state);
    break;
  }
  }
}

//The GE's side.

//(the render GE) Its renderer's finish, its depth put back over VRAM's bytes low to high (past its end, its start's),
//the GPU lost: told to the thread, and, checking, its copy of the model knowing of them once the bytes they changed
//were looked at.
auto GE::reportFinish() -> void {
  //(none once the GPU is lost: the software renderer draws from then on, its batches maybe still being drawn after
  //such a "finish", which puts nothing in memory)
  if(!reporting || (renderer && !renderer->ready())) return;
  reporting->reportFinish(place);
  if(reporting->checking) reporting->replica.finishKnown = std::max(reporting->replica.finishKnown, place ? place - 1 : 0);
}

auto GE::reportPuts(u64 low, u64 high) -> void {
  if(!reporting || high < low) return;
  std::bitset<GeModel::Blocks> blocks;
  for(u64 block = low >> 14; block <= high >> 14 && block < (low >> 14) + GeModel::Blocks; block++) {
    blocks.set(block % GeModel::Blocks);
  }
  reporting->reportPuts(blocks, place);
  if(!reporting->checking) return;
  for(u32 block = 0; block < GeModel::Blocks; block++) {
    if(blocks[block]) reporting->replica.putKnown[block] = std::max(reporting->replica.putKnown[block], place ? place - 1 : 0);
  }
}

auto GE::reportLost() -> void {
  if(reporting) reporting->reportLost();
}

//(checking) A read of memory the render GE makes as it does a PRIM, outside what the emulation thread's model declares
//it may read then (its G-interest: the design's 3.7, R19, R22, R24): fails loudly.
static auto undeclared(u32 page, u64 place) -> void {
  std::fprintf(stderr, "PSP GE thread: the render GE read memory's page %05x at record %llu, outside what the emulation "
               "thread's model declares it may read there (its G-interest: ge/model.hpp)\n", page,
               (unsigned long long)place);
  std::abort();
}

thread_local const ReadsAsOf* GE::readingAsOf = nullptr;

auto GE::readPlace() const -> u64 {
  return readingAsOf ? readingAsOf->place : place;
}

auto GE::readInterest() const -> const GeModel::Interest* {
  if(readingAsOf) return readingAsOf->declared ? &readingAsOf->interest : nullptr;
  return declared;
}

auto GE::readDeclared(u64 low, u64 high) -> void {
  auto* interest = readInterest();
  if(!interest || high < low) return;
  u64 last = std::min<u64>(high, low + Memory::VRAMSize - 1);
  for(u64 page = low / Memory::PageSize; page <= last / Memory::PageSize; page++) {
    if(!interest->vram[page % GeModel::Pages]) {
      undeclared(Memory::VRAMBase / Memory::PageSize + page % GeModel::Pages, readPlace());
    }
  }
}

auto GE::readDeclared(u32 address, u32 size) -> void {
  auto* interest = readInterest();
  u32 first, last;
  if(!interest || !memory.pagesOf(address, size, first, last)) return;
  u32 base = Memory::VRAMBase / Memory::PageSize;
  for(u32 page = first; page <= last; page++) {
    bool inVRAM = page >= base && page < base + GeModel::Pages;
    if(inVRAM ? !interest->vram[page - base] : page < interest->first || page > interest->last) {
      undeclared(page, readPlace());
    }
  }
}

auto GE::drawnAll() const -> bool {
  return !launched() && drawing.batch->jobs.empty();
}

//The GE whose drawing state counts for this one's: the render GE, the thread drained (the kernel's GE, threaded), or
//this one.
auto GE::drawer() -> GE& {
  if(!render) return *this;
  thread->drain();
  return *render;
}

auto GE::stateOf() const -> State {
  State state;
  std::memcpy(state.commands, commands, sizeof(commands));
  std::memcpy(state.bones, bones, sizeof(bones)), std::memcpy(state.world, world, sizeof(world));
  std::memcpy(state.view, view, sizeof(view)), std::memcpy(state.projection, projection, sizeof(projection));
  std::memcpy(state.textureMatrix, textureMatrix, sizeof(textureMatrix));
  state.boneIndex = boneIndex, state.worldIndex = worldIndex, state.viewIndex = viewIndex;
  state.projectionIndex = projectionIndex, state.textureIndex = textureIndex;
  return state;
}

auto GE::takeState(const State& state) -> void {
  std::memcpy(commands, state.commands, sizeof(commands));
  std::memcpy(bones, state.bones, sizeof(bones)), std::memcpy(world, state.world, sizeof(world));
  std::memcpy(view, state.view, sizeof(view)), std::memcpy(projection, state.projection, sizeof(projection));
  std::memcpy(textureMatrix, state.textureMatrix, sizeof(textureMatrix));
  boneIndex = state.boneIndex, worldIndex = state.worldIndex, viewIndex = state.viewIndex;
  projectionIndex = state.projectionIndex, textureIndex = state.textureIndex;
}

//(the render GE) A state word as run() takes it: kept, and the matrices' data where their NUMBER commands point.
auto GE::apply(u32 word) -> void {
  u32 command = word >> 24, argument = word & 0xff'ffff;
  commands[command] = word;
  auto data = [&](u32* matrix, u32 size, u32& index) { if(index < size) matrix[index++] = argument; };
  switch(command) {
  case BoneMatrixNumber:       boneIndex = argument & 0x7f; break;
  case BoneMatrixData:         data(bones, 96, boneIndex); break;
  case WorldMatrixNumber:      worldIndex = argument & 0xf; break;
  case WorldMatrixData:        data(world, 12, worldIndex); break;
  case ViewMatrixNumber:       viewIndex = argument & 0xf; break;
  case ViewMatrixData:         data(view, 12, viewIndex); break;
  case ProjectionMatrixNumber: projectionIndex = argument & 0xf; break;
  case ProjectionMatrixData:   data(projection, 16, projectionIndex); break;
  case TextureMatrixNumber:    textureIndex = argument & 0xf; break;
  case TextureMatrixData:      data(textureMatrix, 12, textureIndex); break;
  }
}

//(the render GE) A kick: its primitives batched, as run() batches them; then, at its end, drawn and handed on as
//run() does as it returns.
auto GE::runStart() -> void {
  resume();
  drawing.deferring = drawing.threads > 1;
}

auto GE::runEnd() -> void {
  drawing.deferring = false;
  launch(true);
  if(!renderer) return;
  if(memory.vramGuard) renderer->submit(*this);
  else renderer->finish(*this);
}

//(the render GE) A PRIM (or a curved surface: patch) the kernel's GE handed over, its vertices and indices read from
//the copies it took as the PRIM came (copied), not memory.
auto GE::replayPrimitive(u32 word, u32 vertexAt, u32 indexAt, const GeThread::Copies& copies, const u8* bytes,
                         bool patching) -> void {
  u32 command = word >> 24, argument = word & 0xff'ffff;
  commands[command] = word;
  vertexAddress = vertexAt, indexAddress = indexAt;
  copied.ranges = copies.ranges;
  for(u32 n = 0; n < copies.ranges; n++) {
    copied.address[n] = copies.address[n], copied.size[n] = copies.size[n], copied.bytes[n] = bytes;
    bytes += copies.size[n];
  }
  if(!patching) primitive(argument >> 16 & 7, argument & 0xffff);
  else patch(command == Spline, argument);
  copied.ranges = 0;
}

//(the render GE) CLUT_LOAD from the palette's bytes the kernel's GE copied, after loadClut()'s drawnFirst().
auto GE::replayClut(u32 word, u32 address, const u8* bytes, u32 size) -> void {
  commands[ClutLoad] = word;
  drawnFirst(address, size);
  u8 before[sizeof(clut)];
  std::memcpy(before, clut, sizeof(clut));
  std::memcpy(clut, bytes, std::min<u32>(size, sizeof(clut)));
  if(std::memcmp(before, clut, sizeof(clut))) paletteChanged();
}

//(the render GE) Where the copies have size bytes from address, or none.
auto GE::Copied::at(u32 address, u32 size) const -> const u8* {
  for(u32 n = 0; n < ranges; n++) {
    u32 offset = address - this->address[n];
    if(offset < this->size[n] && size <= this->size[n] - offset) return bytes[n] + offset;
  }
  return nullptr;
}

//(a byte of memory as it is: none where nothing is, and nothing told of it, unlike memory.read())
static auto byteAt(Memory& memory, u32 address) -> u8 {
  u32 physical = address & 0x1fff'ffff;
  if(physical >= Memory::RAMBase && physical - Memory::RAMBase < memory.ram.size()) return memory.ram[physical - Memory::RAMBase];
  if(physical >= Memory::VRAMBase && physical - Memory::VRAMBase < Memory::VRAMWindow) {
    u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize, seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
    return memory.vram[Memory::vramOffset(copy, seen)];
  }
  if(physical >= Memory::ScratchpadBase && physical - Memory::ScratchpadBase < Memory::ScratchpadSize) {
    return memory.scratchpad[physical - Memory::ScratchpadBase];
  }
  return 0;
}

//(the kernel's GE) A PRIM's vertices and indices, as readVertices() would read them, copied for the render GE, which
//makes readVertices()' drawnFirst() calls itself (readCopiedVertices()). Where the render GE may still change bytes
//they're read from (the value rule: GeThread::model), those calls are handed over as they come and waited for first;
//its reads are readVertices()' (those with nothing behind them reported as there), the vertices decoded and dropped;
//then the bytes copied, its indices' and its vertices', as memory holds them now, and the addresses moved on as
//readVertices() moves them.
auto GE::copyVertices(u32 count, const VertexFormat& format, GeThread::Copies& copies, std::vector<u8>& bytes) -> void {
  copies = {};
  bytes.clear();
  u32 indexBytes = format.indexFormat == 3 ? 4 : format.indexFormat;
  u64 low = vertexAddress & 0x1fff'ffff, high = low + u64(count) * format.size;
  auto inside = [&](u64 base, u64 size) { return low >= base && high <= base + size; };
  u64 copy = (low - Memory::VRAMBase) / Memory::VRAMSize * Memory::VRAMSize + Memory::VRAMBase;
  bool together = !format.indexFormat && count && (inside(Memory::RAMBase, memory.ram.size()) ||
                  (low >= Memory::VRAMBase && inside(copy, Memory::VRAMSize) &&
                   inside(Memory::VRAMBase, Memory::VRAMWindow)));
  if(together) thread->readClean(vertexAddress, count * format.size);
  u32 most = 0;
  if(!together && count && format.size) {  //(readVertices()' way a vertex at a time; by indices all in RAM, no wait)
    Memory::KnownDrawn known;
    for(u32 n = 0; n < count; n++) {
      if(format.indexFormat) thread->readClean(indexAddress + n * indexBytes, indexBytes);
      u32 index = format.indexFormat ? readIndex(n, format) : n;
      u32 address = vertexAddress + index * format.size;
      thread->readClean(address, format.size);
      (void)readVertex(address, format);
      most = std::max(most, index);
    }
  }
  auto take = [&](u32 address, u32 size) {
    u32 at = bytes.size();
    bytes.resize(at + size);
    for(u32 n = 0; n < size; n++) bytes[at + n] = byteAt(memory, address + n);
    copies.address[copies.ranges] = address, copies.size[copies.ranges] = size, copies.ranges++;
  };
  if(count && format.size) {
    if(format.indexFormat) take(indexAddress, count * indexBytes), take(vertexAddress, (most + 1) * format.size);
    else take(vertexAddress, count * format.size);
  }
  if(format.indexFormat) indexAddress += count * indexBytes;
  else vertexAddress += count * format.size;
}

//(the kernel's GE) Its state handed over whole where it's not what the render GE has (set from outside since: a
//context restored, a test's settings), at a run's start and before a PRIM handed over outside one.
auto GE::handState() -> void {
  State now = stateOf();
  if(handedStateValid && !std::memcmp(&now, &handedState, sizeof(now))) return;
  thread->state(*this);
  handedState = now, handedStateValid = true;
}

//(the kernel's GE) Its drawing switches the render GE's, where a test changed one (the thread drained first).
auto GE::handSwitches() -> void {
  if(render->fourPixels == fourPixels && render->boxes == boxes && render->vertexRuns == vertexRuns &&
     render->drawing.shared == drawing.shared && render->textures.budget == textures.budget) {
    return;
  }
  GE& to = drawer();
  to.fourPixels = fourPixels, to.boxes = boxes, to.vertexRuns = vertexRuns;
  to.drawing.shared = drawing.shared, to.textures.budget = textures.budget;
}

//(the kernel's GE) A PRIM (or a curved surface) handed to the render GE: its bytes copied (copyVertices()), and its
//texture's pages watched, so that a store there before the render GE has decoded it waits for that first (memory's
//drawer: VRAM's stores wait anyway), with its word and the addresses it reads from.
auto GE::handPrimitive(u32 word, u32 count, bool patching) -> void {
  (void)patching;
  if(!running) handSwitches(), handState();
  auto format = vertexFormat();
  u32 vertexAt = vertexAddress, indexAt = indexAddress;
  GeThread::Copies copies;
  copyVertices(count, format, copies, handCopies);
  if(commands[TextureMappingEnable] & 1) {
    Sampler texture = sampler();
    u32 low, high;
    textureBytes(texture, std::min<u32>(texture.height, 512), low, high);
    if(high > low) memory.watch(low, high - low);
  }
  bool hardware = thread->hardware;
  u64 at = thread->nextPlace();
  if(commands[TextureMappingEnable] & 1) {  //(the texture's pages: their stores heard of till the render GE passes it)
    Sampler texture = sampler();
    u32 low, high, first, last;
    textureBytes(texture, std::min<u32>(texture.height, 512), low, high);
    if(high > low && memory.pagesOf(low, high - low, first, last)) {
      for(u32 page = first; page <= last && page < thread->textureUntil.size(); page++) thread->textureUntil[page] = at;
    }
  }
  bool certain = modelPrimitive(thread->model, word, vertexAt, indexAt, copies, handCopies.data(), hardware, at);
  thread->primitive(word, running ? listWordAt : ~0u, vertexAt, indexAt, hardware, certain, copies,
                    handCopies.data());
  if(!running) handedState = stateOf(), thread->drain();  //(outside a list the GE draws a PRIM at once: primitive())
}

//(the kernel's GE) CLUT_LOAD handed over: the palette's bytes read as loadClut() reads them (its drawnFirst() and
//reads made, the render GE's palette left alone), then copied.
auto GE::handClut() -> void {
  if(!running) handSwitches(), handState();
  u32 address = (commands[ClutAddress] & 0xff'fff0) | (commands[ClutAddressUpper] << 8 & 0x0f00'0000);
  u32 bytes = std::min<u32>((commands[ClutLoad] & 0x3f) * 32, sizeof(clut));
  thread->readClean(address, bytes);  //(loadClut()'s drawnFirst() the render GE's, handed over first where it's dirty)
  u8 palette[sizeof(clut)] = {};
  bool copied = [&] {
    Memory::KnownDrawn known;
    return memory.copyOut(palette, address, bytes);
  }();
  if(!copied) {
    for(u32 n = 0; n < bytes; n++) palette[n] = memory.read(1, address + n);
  }
  if(std::memcmp(clut, palette, bytes)) std::memcpy(clut, palette, bytes), paletteChanged();
  thread->clut(commands[ClutLoad], running ? listWordAt : ~0u, address, palette, bytes);
  if(!running) thread->drain();
}

//(the kernel's GE, threaded) TRANSFER_START: the copy made here, row by row as transfer() makes it, the render GE
//replaying transfer()'s checks from its record, in their order (transferChecks()). Where a row's source may still be
//changed by the render GE (the value rule), or a row lands on pixels the drawing threads may still draw or a texture a
//PRIM the software renderer may still draw reads (a conflict), the record goes first and the copy waits for the
//render GE to pass it, so that what it finishes or draws first comes before the copy, as it does synchronously; else
//each row's old bytes are kept first where the render GE may still read them at an earlier place (it hasn't passed
//every record: versions), the copy made, then the record.
auto GE::transferThreaded() -> void {
  //(outside a list, the state set from outside handed over first, as handPrimitive() does)
  if(!running) handSwitches(), handState();
  auto t = transferAsked();
  u32 size = t.width * t.bytes;
  auto from = [&](u32 y) { return t.source + ((t.sourceY + y) * t.sourceStride + t.sourceX) * t.bytes; };
  auto to = [&](u32 y) { return t.destination + ((t.destinationY + y) * t.destinationStride + t.destinationX) * t.bytes; };
  thread->nextPlace();  //(TRANSFER_START's word handed over first)
  thread->takeReports();
  bool first = false;
  for(u32 y = 0; y < t.height && !first; y++) first = thread->dirtyRange(from(y), size) || thread->conflicts(to(y), size);
  u64 recorded = 0;
  if(first) recorded = thread->transfer(thread->place() + 1), thread->waitFor(recorded);
  bool behind = !first && thread->behind();
  u64 at = 2 * (thread->place() + 1);  //(its record's version place)
  std::vector<u8> row(size);
  memory.recording = true;
  for(u32 y = 0; y < t.height; y++) {
    u32 source = from(y), target = to(y);
    bool read = [&] { Memory::KnownDrawn known; return memory.copyOut(row.data(), source, size); }();
    //(kept where the render GE may still read or write back the bytes at an earlier place, or the transfer's own
    //checks, before its rows in the stream's order though made after them, may finish over them)
    bool keep = !first && (behind || thread->gpuBusy(target, size));
    if(read) {
      if(keep) thread->save(target, size, at);
      if(memory.copyInDirect(target, row.data(), size)) continue;
    }
    for(u32 n = 0; n < size; n++) {  //across a piece's end, as transfer()
      u8 byte = [&] { Memory::KnownDrawn known; return memory.read(1, source + n); }();
      if(keep) thread->save(target + n, 1, at);
      memory.storeDirect(target + n, byte);
    }
  }
  memory.recording = false;
  if(!first) thread->transfer(thread->place());
  else thread->wrote(recorded);
  for(u32 y = 0; y < t.height; y++) thread->protect(to(y), size);
}

//(the render GE) readVertices() of a PRIM handed over: its bytes from the copies (copied), which hold every byte it
//reads; its drawnFirst() calls made as readVertices() makes them, the drawing over what it reads finished or drawn
//first where the synchronous GE's would be.
auto GE::readCopiedVertices(u32 count, const VertexFormat& format) -> void {
  u32 ambient = (commands[AmbientColor] & 0xff'ffff) | (commands[AmbientAlpha] & 0xff) << 24;
  u32 indexBytes = format.indexFormat == 3 ? 4 : format.indexFormat;
  auto& vertices = primitiveVertices;
  vertices.clear();
  auto index = [&](u32 n) -> u32 {
    const u8* at = copied.at(indexAddress + n * indexBytes, indexBytes);
    if(!at) return 0;
    if(indexBytes == 1) return at[0];
    if(indexBytes == 2) return at[0] | at[1] << 8;
    return (at[0] | at[1] << 8) & 0xffff;
  };
  {  //(in a row in one area: once for them all; else each index and vertex, those in RAM asking nothing)
    u64 low = vertexAddress & 0x1fff'ffff, high = low + u64(count) * format.size;
    auto inside = [&](u64 base, u64 size) { return low >= base && high <= base + size; };
    u64 copy = (low - Memory::VRAMBase) / Memory::VRAMSize * Memory::VRAMSize + Memory::VRAMBase;
    bool together = !format.indexFormat && count && (inside(Memory::RAMBase, memory.ram.size()) ||
                    (low >= Memory::VRAMBase && inside(copy, Memory::VRAMSize) &&
                     inside(Memory::VRAMBase, Memory::VRAMWindow)));
    if(together) drawnFirst(vertexAddress, count * format.size);
    for(u32 n = 0; !together && n < count && format.size; n++) {
      if(format.indexFormat) drawnFirst(indexAddress + n * indexBytes, indexBytes);
      drawnFirst(vertexAddress + (format.indexFormat ? index(n) : n) * format.size, format.size);
    }
  }
  if(count && format.size) {
    auto& offsets = vertexOffsets;
    offsets.resize(count);
    u32 most = 0;
    for(u32 n = 0; n < count; n++) {
      u32 i = format.indexFormat ? index(n) : n;
      offsets[n] = i * format.size, most = std::max(most, i);
    }
    const u8* bytes = copied.at(vertexAddress, (most + 1) * format.size);
    if(bytes && vertexRuns && format.morphs == 1) {
      vertices.resize(count);
      readVerticesFrom(bytes, offsets.data(), count, format, vertices.data());
      vertexRunsRead++;
    } else {
      for(u32 n = 0; n < count; n++) {
        u32 address = vertexAddress + offsets[n];
        const u8* base = copied.at(address, format.size);
        vertices.push_back(readVertexWith([&](u32 size, u32 at) -> u32 {
          if(!base) return 0;
          const u8* b = base + (at - address);
          if(size == 1) return b[0];
          if(size == 2) return b[0] | b[1] << 8;
          return b[0] | b[1] << 8 | b[2] << 16 | u32(b[3]) << 24;
        }, address, format));
      }
    }
  }
  for(auto& vertex : vertices) {
    if(!format.colorFormat) vertex.color = ambient;
    if(format.through) vertex.u = keep15(vertex.u), vertex.v = keep15(vertex.v);
  }
  if(format.indexFormat) indexAddress += count * indexBytes;
  else vertexAddress += count * format.size;
}
