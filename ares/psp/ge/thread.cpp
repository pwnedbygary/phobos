//The GE on a thread of its own: thread.hpp says how the kernel's GE and the render GE share the work.

#include <chrono>

thread_local bool GE::onGeThread = false;

GeThread::GeThread(GE& control, GE& render) : control(control), render(render) {
  versions = std::make_unique<GeVersions>(&GE::hostPage, &control.memory);
  render.versions = versions.get();
  render.reporting = this;
  if(const char* check = std::getenv("PHOBOS_GE_CHECK")) checking = check[0] && check[0] != '0';
  if(const char* fuzzing = std::getenv("PHOBOS_GE_FUZZ"); fuzzing && fuzzing[0]) {
    unsigned long long seed = 0;
    unsigned most = 0;
    char mode[16] = {};
    if(std::sscanf(fuzzing, "%llu,%15[a-z],%u", &seed, mode, &most) >= 2) {
      std::string name = mode;
      fuzz = name == "delay" ? Fuzz::Delay : name == "behind" ? Fuzz::Behind : name == "ahead" ? Fuzz::Ahead
           : Fuzz::None;
      fuzzState = seed, fuzzMost = most ? most : fuzz == Fuzz::Delay ? 50 : 256;
    }
  }
  //(the render GE's watches are pages the kernel's GE's memory watches already: textures' pages, which it watches as
  //it hands their PRIMs over, and VRAM's, all out of the CPU's page table while the thread is on, its code run by the
  //interpreter. So none needs telling it, nor do its changes to VRAM: watching only so that the render GE keeps
  //textures decoded, Memory::canWatch())
  render.memory.watching = [](u32) {};
  render.memory.unwatched = nullptr;
  render.memory.written = nullptr;
  //(its reads of nothing, the software renderer's texels among them, reported as the kernel's GE's memory reports
  //them: the count a state keeps saturates, so the two threads' order doesn't change it)
  render.memory.unmapped = control.memory.unmapped;
  thread = std::thread([this] {
    //(named for profilers and the runner's measurements, which find it by its name: tools/psp-runner/android)
    #if defined(__APPLE__)
    pthread_setname_np("psp-ge-thread");
    #elif defined(__linux__)
    pthread_setname_np(pthread_self(), "psp-ge-thread");
    #endif
    take();
  });
}

GeThread::~GeThread() {
  if(!abandoned.load(std::memory_order_acquire)) drain();  //(abandoned: let go of only once it has come back)
  ring.stop();
  thread.join();
  render.memory.watching = nullptr;
  render.memory.written = nullptr;
  render.versions = nullptr;
  render.reporting = nullptr;
}

auto GeThread::append(Kind kind, u32 size) -> u8* {
  if(storeCount && kind != Kind::Stores) flushStores();
  u8* at = ring.reserve(sizeof(Header) + size);
  if(!at) return nullptr;
  Header header{kind, size, ++serial};
  std::memcpy(at, &header, sizeof(header));
  return at + sizeof(header);
}

auto GeThread::publish() -> void {
  ring.publish();
  if(fuzz == Fuzz::None) return;
  appended.store(serial, std::memory_order_release);
  if(fuzz != Fuzz::Ahead || gate) return;
  for(u64 now; (now = done.load(std::memory_order_acquire)) < serial;) done.wait(now, std::memory_order_acquire);
}

//(the marks after the words: how many, then each one's word and clocks, 12 bytes)
static constexpr u32 MarkBytes = sizeof(u32) + sizeof(f64);

auto GeThread::wordsBytes() const -> u32 {
  if(!wordCount) return 0;
  return sizeof(u32) + runCount * sizeof(WordRun) + wordCount * sizeof(u32) + sizeof(u32) + marks.size() * MarkBytes;
}

auto GeThread::putWords(u8* at) -> void {
  std::memcpy(at, &runCount, sizeof(u32));
  std::memcpy(at + sizeof(u32), wordRuns.data(), runCount * sizeof(WordRun));
  at += sizeof(u32) + runCount * sizeof(WordRun);
  std::memcpy(at, words.data(), wordCount * sizeof(u32));
  at += wordCount * sizeof(u32);
  u32 count = marks.size();
  std::memcpy(at, &count, sizeof(u32));
  at += sizeof(u32);
  for(auto& mark : marks) {
    std::memcpy(at, &mark.word, sizeof(u32));
    std::memcpy(at + sizeof(u32), &mark.clocks, sizeof(f64));
    at += MarkBytes;
  }
  wordCount = runCount = 0, nextWordAt = ~0u;
  marks.clear();
}

auto GeThread::flushWords() -> void {
  if(!wordCount) return;
  putWords(append(Kind::Words, wordsBytes()));
  publish();
}

auto GeThread::flushStores() -> void {
  if(!storeCount) return;
  u64 cx = serial;
  u8* at = append(Kind::Stores, sizeof(cx) + sizeof(u32) + stores.size() * sizeof(u32));
  std::memcpy(at, &cx, sizeof(cx));
  std::memcpy(at + sizeof(cx), &storeCount, sizeof(u32));
  std::memcpy(at + sizeof(cx) + sizeof(u32), stores.data(), stores.size() * sizeof(u32));
  publish();
  stores.clear(), storeCount = 0;
}

auto GeThread::addStore(u32 address, u32 size) -> void {
  stores.push_back(address), stores.push_back(size), stores.push_back(written.size());
  for(auto [first, last] : written) stores.push_back(first), stores.push_back(last);
  written.clear();
  storeCount++;
  if(stores.size() >= StoresMost) flushStores();
}

//(a PRIM's or curved surface's record: its word, addresses and copies; one too big for the ring is drawn by the
//emulation thread, the render GE's code there and then, once the thread has drained)
static auto copiedBytes(const GeThread::Copies& copies) -> u32 {
  u32 bytes = 0;
  for(u32 n = 0; n < copies.ranges; n++) bytes += copies.size[n];
  return bytes;
}

//(a PRIM record's head: its word, its word's address, its vertices' and indices', whether the renderer was a hardware
//one, whether the model took it as surely the GPU's, and the size of the words in the record before its copies)
static constexpr u32 PrimitiveHead = 7 * sizeof(u32);

auto GeThread::primitivePlace(u32 copied) -> u64 {
  flushStores();
  if(!ring.fits(sizeof(Header) + PrimitiveHead + wordsBytes() + sizeof(Copies) + copied)) flushWords();
  return serial + 1;
}

auto GeThread::primitiveBegin(u32 word, u32 wordAddress, u32 vertexAddress, u32 indexAddress, bool hardware,
                              const Copies& copies) -> u8* {
  built = {word, wordAddress, vertexAddress, indexAddress, hardware, copies};
  u32 wordBytes = wordsBytes();
  building = append(word >> 24 != GE::Primitive ? Kind::Patch : Kind::Primitive,
                    PrimitiveHead + wordBytes + sizeof(Copies) + copiedBytes(copies));
  if(!building) return nullptr;
  u32 head[7] = {word, wordAddress, vertexAddress, indexAddress, hardware, 0, wordBytes};
  std::memcpy(building, head, sizeof(head));
  if(wordBytes) putWords(building + PrimitiveHead);
  std::memcpy(building + PrimitiveHead + wordBytes, &copies, sizeof(Copies));
  return building + PrimitiveHead + wordBytes + sizeof(Copies);
}

auto GeThread::primitiveEnd(bool certain, const u8* bytes) -> void {
  if(building) {
    u32 sure = certain;
    std::memcpy(building + 5 * sizeof(u32), &sure, sizeof(u32));
    building = nullptr;
    return publish();
  }
  //(too big for the ring: as the record it would be, there and then, at the place of the next)
  auto& b = built;
  drain();
  if(b.wordAddress != ~0u) listWordDrawn(b.wordAddress);
  if(checking) render.modelPrimitive(replica, b.word, b.vertexAddress, b.indexAddress, b.copies, bytes, b.hardware,
                                     serial + 1);
  runWords++;  //(in the run's count, for its pipeline, as the record would have been)
  render.replayPrimitive(b.word, b.vertexAddress, b.indexAddress, b.copies, bytes, b.word >> 24 != GE::Primitive);
  undrawn = ~0u;
}

auto GeThread::clut(u32 word, u32 wordAddress, u32 address, const u8* bytes, u32 size) -> void {
  flushWords();
  u8* at = append(Kind::Clut, 4 * sizeof(u32) + size);
  u32 head[4] = {word, wordAddress, address, size};
  std::memcpy(at, head, sizeof(head));
  std::memcpy(at + sizeof(head), bytes, size);
  publish();
}

auto GeThread::drawnFirst(u32 address, u32 size) -> u64 {
  flushWords();
  u32 head[2] = {address, size};
  std::memcpy(append(Kind::DrawnFirst, sizeof(head)), head, sizeof(head));
  publish();
  return serial;
}

auto GeThread::read(u32 address, u32 size, bool loading) -> u64 {
  flushWords();
  u32 head[3] = {address, size, loading};
  std::memcpy(append(Kind::Read, sizeof(head)), head, sizeof(head));
  publish();
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
  publish();
  written.clear();
  return serial;
}

auto GeThread::transfer(u64 cx) -> u64 {
  return write(0, 0, Kind::Transfer, cx);
}

auto GeThread::before(u32 address, u32 size, Memory::Writer::Kind kind) -> void {
  flushWords();
  if(kind != Memory::Writer::Kind::Store) flushStores();
  takeReports();
  writePlace = 0;
  if(conflicts(address, size)) {
    flushStores();
    writePlace = write(address, size, kind == Memory::Writer::Kind::Store ? Kind::Store : Kind::Copy, serial + 1);
    return waitFor(writePlace);
  }
  //(where the render GE may still read or write back the bytes at an earlier place, or the write's own record's
  //checks, which come before it in the stream's order though made after it, may finish over them; the record after()
  //appends next, or the stores' it joins, the next)
  if(behind() || gpuBusy(address, size)) save(address, size, 2 * (serial + 1));
}

auto GeThread::after(u32 address, u32 size, Memory::Writer::Kind kind) -> void {
  if(writePlace) wrote(writePlace);  //(its record went first: the bytes it wrote told after)
  else if(kind == Memory::Writer::Kind::Store) addStore(address, size);
  else write(address, size, Kind::Copy, serial);
  writePlace = 0;
  protect(address, size);
  if(u64 passed = done.load(std::memory_order_acquire); passed != freedAt) versions->free(2 * passed), freedAt = passed;
}

//(the pages of a write that a texture the render GE may still decode from is in, watched again after the write's
//changed() let them go: VRAM's are heard of anyway, out of the CPU's page table)
auto GeThread::protect(u32 address, u32 size) -> void {
  u32 first, last, physical = address & 0x1fff'ffff;
  if(physical >= Memory::VRAMBase && physical - Memory::VRAMBase < Memory::VRAMWindow) return;
  if(!control.memory.pagesOf(address, size, first, last)) return;
  u64 passed = done.load(std::memory_order_acquire);
  for(u32 page = first; page <= last && page < textureUntil.size(); page++) {
    if(textureNewest(page) > passed && !control.memory.watched[page]) control.memory.watch(page * Memory::PageSize, 1);
  }
}

auto GeThread::textureRead(u32 first, u32 last, u64 at) -> void {
  if(first != textureFirst || last != textureLast) {
    for(u32 page = textureFirst; page <= textureLast && page < textureUntil.size(); page++) {
      textureUntil[page] = std::max(textureUntil[page], textureAt);
    }
    textureFirst = first, textureLast = last;
  }
  textureAt = at;
}

//(checking: the VRAM bytes written since a record that went before them, with their place, for the render GE's model)
auto GeThread::wrote(u64 at) -> void {
  for(auto [first, last] : written) {
    u32 head[2] = {first, last};
    u8* to = append(Kind::Wrote, sizeof(head) + sizeof(at));
    std::memcpy(to, head, sizeof(head));
    std::memcpy(to + sizeof(head), &at, sizeof(at));
    publish();
  }
  written.clear();
}

//(where a write lands on pixels the drawing threads may still draw, or a texture a PRIM the software renderer may still
//draw reads from memory as it's drawn: the model's, the reports taken)
auto GeThread::conflicts(u32 address, u32 size) -> bool {
  u32 first, last;
  if(model.softwareTexturesNewest > model.drawnKnown && control.memory.pagesOf(address, size, first, last)) {
    for(u32 page = first; page <= last && page < model.softwareTextures.size(); page++) {
      if(model.softwareTextures[page] > model.drawnKnown) return true;
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
      if(model.gpu.newestOn(page) > model.finishKnown) return true;
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
    u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize;
    u32 seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
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
  if(!control.memory.recording) flushStores();
  model.write(first, last, serial);
  if(!checking) return;
  if(control.memory.recording) return written.push_back({first, last});
  u32 head[2] = {first, last};
  u64 at = serial;
  u8* to = append(Kind::Wrote, sizeof(head) + sizeof(at));
  std::memcpy(to, head, sizeof(head));
  std::memcpy(to + sizeof(head), &at, sizeof(at));
  publish();
}

auto GeThread::runStart() -> void {
  flushWords();
  dirtyPage = ~0u;
  runs++;
  for(u64 ended; runs - (ended = runsEnded.load(std::memory_order_acquire)) > RunsKept;) {
    Blocked holding{blocked};
    runsEnded.wait(ended, std::memory_order_acquire);
  }
  u32 head[2] = {control.pspTiming, shifting};
  u8* at = append(Kind::RunStart, sizeof(head) + sizeof(f64));
  std::memcpy(at, head, sizeof(head));
  std::memcpy(at + sizeof(head), &shift, sizeof(f64));
  publish();
  shifting = false, shift = 0;
}

auto GeThread::runEnd(u64 words) -> void {
  flushWords();
  std::memcpy(append(Kind::RunEnd, sizeof(words)), &words, sizeof(words));
  publish();
}

auto GeThread::owe(f64 clocks) -> void {
  flushWords();
  std::memcpy(append(Kind::Owe, sizeof(clocks)), &clocks, sizeof(clocks));
  publish();
}

auto GeThread::state(const GE& from) -> void {
  flushWords();
  GE::State state = from.stateOf();
  std::memcpy(append(Kind::State, sizeof(state)), &state, sizeof(state));
  publish();
}

auto GeThread::frame(const Frame& frame) -> void {
  flushWords();
  u8* at = append(Kind::Frame, sizeof(Frame));
  std::memcpy(at, &frame, sizeof(Frame));
  publish();
  framesHanded++;
}

auto GeThread::waitFrames(u64 most) -> void {
  Blocked holding{blocked};
  for(u64 ended; framesHanded - (ended = framesEnded.load(std::memory_order_acquire)) > most;) {
    framesEnded.wait(ended, std::memory_order_acquire);
  }
}

auto GeThread::frameBytes(const Frame& frame, const std::function<auto (const u8* vram) -> void>& use) -> void {
  u32 bytes = (frame.pixelFormat & 3) == 3 ? 4 : 2;
  for(u32 y = 0; y < frame.height; y++) {
    render.memory.pointer(frame.frameBuffer + y * frame.bufferWidth * bytes, frame.width * bytes);
  }
  u32 low = ((frame.frameBuffer & 0x1fff'ffff) - Memory::VRAMBase) % Memory::VRAMSize;
  u32 high = low + ((frame.height - 1) * frame.bufferWidth + frame.width) * bytes - 1;
  render.readingVRAM(low, high, use);
}

auto GeThread::owedOf(u64 run, bool wait, Owed& owed) -> bool {
  u64 ended = runsEnded.load(std::memory_order_acquire);
  if(ended < run && !wait) return false;
  if(ended < run) {
    flushWords();
    Blocked holding{blocked};
    while(ended < run) {
      runsEnded.wait(ended, std::memory_order_acquire);
      ended = runsEnded.load(std::memory_order_acquire);
    }
  }
  owed = owedRuns[run % RunsKept];
  return true;
}

auto GeThread::owedSoFar(u64 run, Owed& owed) const -> bool {
  u64 before = soFarVersion.load(std::memory_order_acquire);
  if(before & 1) return false;
  u64 at = soFarRun.load(std::memory_order_relaxed);
  owed.pixels = soFarPixels.load(std::memory_order_relaxed);
  owed.primitives = soFarPrimitives.load(std::memory_order_relaxed);
  owed.time = soFarTime.load(std::memory_order_relaxed);
  owed.front = soFarFront.load(std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_acquire);
  return at == run && soFarVersion.load(std::memory_order_relaxed) == before;
}

//(the GE thread's) the run under way's counts so far, and its pipeline's two ends (after its shift, as it starts)
auto GeThread::publishSoFar() -> void {
  soFarVersion.fetch_add(1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  soFarRun.store(takenRuns, std::memory_order_relaxed);
  soFarPixels.store(render.owedPixels, std::memory_order_relaxed);
  soFarPrimitives.store(render.owedPrimitives, std::memory_order_relaxed);
  soFarTime.store(std::max(render.pipe.front, render.pipe.back), std::memory_order_relaxed);
  soFarFront.store(render.pipe.front, std::memory_order_relaxed);
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
  if(!model.putPoints.empty() && model.putPoints.front().at <= model.passed) {  //(in their places' order)
    auto kept = std::find_if(model.putPoints.begin(), model.putPoints.end(),
                             [&](const GeModel::PutPoint& point) { return point.at > model.passed; });
    reportWork += kept - model.putPoints.begin() + (kept != model.putPoints.end());
    model.putPoints.erase(model.putPoints.begin(), kept);
  }
  if(lostReported.load(std::memory_order_acquire)) hardware = false;
  if(uncertain.load(std::memory_order_acquire)) model.unsure = true;
  if(versions->versions()) versions->free(2 * model.passed);
}

auto GeThread::dirty(u32 first, u32 last) -> bool {
  takeReports();
  return model.dirty(first, last);
}

auto GeThread::waitFor(u64 at) -> void {
  flushWords(), flushStores();
  if(done.load(std::memory_order_acquire) < at) {
    waits++;
    Blocked holding{blocked};
    for(u64 now; (now = done.load(std::memory_order_acquire)) < at;) done.wait(now, std::memory_order_acquire);
  }
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
//(out of line: inlined in the list's loop, its caches changed what the compiler made of the loop with the thread off)
NALL_NOINLINE auto GeThread::listWord(u32 address) -> bool {
  u32 first, last;
  if(!vramSpan(address & ~0xfffu, 0x1000, first, last)) return true;  //(RAM: the render GE never changes it)
  takeReports();
  if(address >> 12 != dirtyPage) {
    //(asked again after every PRIM: God of War's lists, in pages beside the GPU's pixels, were looked through whole
    //thousands of times a frame, unchanged)
    CleanPage seen{address >> 12, 0, 0, model.dirtyings, model.staleDepth, model.scaled};
    for(u32 page = first >> 12; page <= last >> 12; page++) {
      seen.gpu = std::max(seen.gpu, model.gpu.newestOn(page));
      seen.software = std::max(seen.software, model.software.newestOn(page));
    }
    auto& known = cleanPages[seen.page % cleanPages.size()];
    if(seen == known) return true;
    if(!model.dirty(first, last)) return known = seen, true;
    dirtyPage = address >> 12;
  }
  vramSpan(address, 4, first, last);
  //(each word of such a page looked at alone: eight share a piece, kept once all its bytes are clean, as a rectangle's
  //edge may fall inside it)
  bool onePiece = first >> 5 == last >> 5;
  CleanPage piece{first >> 5, model.gpu.newestOn(first >> 12), model.software.newestOn(first >> 12),
                  model.dirtyings, model.staleDepth, model.scaled};
  if(onePiece && piece == cleanPiece) return false;
  if(model.dirty(first, last)) waitFor(drawnFirst(address, 4));
  else if(onePiece && !model.dirty(first & ~31u, first | 31u)) cleanPiece = piece;
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
//(out of line: inlined in GE::drawnFirst(), the model's checks made it cost the GE's work with the thread off 0.1 to
//0.3% more instructions)
NALL_NOINLINE auto GeThread::controlRead(u32 address, u32 size, u32 first, u32 last) -> void {
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
  model.staleDepth = replica.staleDepth = model.scaled || (render.renderer && !render.renderer->depthMoves());
}

auto GeThread::drain() -> void {
  flushWords(), flushStores();
  {
    Blocked holding{blocked};
    for(u64 at; (at = done.load(std::memory_order_acquire)) != serial;) done.wait(at, std::memory_order_acquire);
  }
  afterDrain();
}

auto GeThread::drainBy(std::chrono::steady_clock::time_point deadline) -> bool {
  flushWords(), flushStores();
  {
    Blocked holding{blocked};
    while(done.load(std::memory_order_acquire) != serial) {
      if(std::chrono::steady_clock::now() >= deadline) return false;
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }
  afterDrain();
  return true;
}

auto GeThread::afterDrain() -> void {
  for(auto& text : notes) control.note(text);  //(each once, whichever GE noted it)
  notes.clear();
  rendererChanged();  //(its resolution, or the GPU lost, as the render GE has it now)
  if(versions->versions()) versions->free(2 * serial);  //(it has passed them all)
}

auto GeThread::abandon() -> void {
  abandoned.store(true, std::memory_order_release);
  ring.stop();
}

auto GeThread::take() -> void {
  GE::onGeThread = true;
  while(const u8* at = ring.next()) {
    Header header;
    std::memcpy(&header, at, sizeof(header));
    render.place = header.serial;
    if(checking) {
      replica.passed = header.serial - 1;
      //(the put points passed dropped, as takeReports() drops the emulation thread's: dirty() goes through every one
      //kept, for every byte a finish changes)
      auto& points = replica.putPoints;
      if(!points.empty() && points.front().at <= replica.passed) {
        points.erase(points.begin(), std::find_if(points.begin(), points.end(), [&](const GeModel::PutPoint& point) {
          return point.at > replica.passed;
        }));
      }
    }
    if(gate) gate(header.serial);
    if(fuzz != Fuzz::None) fuzzBefore(header.serial);
    if(abandoned.load(std::memory_order_acquire)) break;
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
  left.store(true, std::memory_order_release);
}

//(the GE thread's, fuzzing, before it does the record at place: thread.hpp)
auto GeThread::fuzzBefore(u64 place) -> void {
  u64 drawn = fuzzNext();
  if(fuzz == Fuzz::Delay) {
    if(drawn % 4 == 0) std::this_thread::sleep_for(std::chrono::microseconds(drawn / 4 % (fuzzMost + 1)));
    return;
  }
  if(fuzz != Fuzz::Behind) return;
  u64 deep = place + 1 + drawn % fuzzMost;
  auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
  while(appended.load(std::memory_order_acquire) < deep && !blocked.load(std::memory_order_acquire) &&
        ring.backlog() < ring.capacity() / 4 && std::chrono::steady_clock::now() < until) {
    std::this_thread::sleep_for(std::chrono::microseconds(20));
  }
}

//(splitmix64)
auto GeThread::fuzzNext() -> u64 {
  u64 z = fuzzState += 0x9e37'79b9'7f4a'7c15;
  z = (z ^ z >> 30) * 0xbf58'476d'1ce4'e5b9;
  z = (z ^ z >> 27) * 0x94d0'49bb'1331'11eb;
  return z ^ z >> 31;
}

//(the render GE) run()'s drawnFirst() for a list word it read at address: where its page isn't the one last found
//with nothing drawn over it (since the list's last PRIM, curved surface or block transfer, or its start)
auto GeThread::listWordDrawn(u32 address) -> void {
  if(address >> 12 == undrawn) return;
  render.drawnFirst(address, 4);
  undrawn = render.undrawnPage(address) ? address >> 12 : ~0u;
}

//(each word counted in the run's, as run() counts it, and taken; a TRANSFER_START's time put in the pipeline as
//transfer() puts it, and the control pass's front-end work after the word it came with, with the PSP's timing)
auto GeThread::replayWords(const u8* data) -> void {
  u32 runCount, count = 0;
  std::memcpy(&runCount, data, sizeof(u32));
  for(u32 n = 0; n < runCount; n++) {
    u32 run[2];
    std::memcpy(run, data + sizeof(u32) + n * sizeof(run), sizeof(run));
    count += run[1];
  }
  const u8* word = data + sizeof(u32) + runCount * 2 * sizeof(u32);
  const u8* mark = word + count * sizeof(u32);
  u32 marksLeft;
  std::memcpy(&marksLeft, mark, sizeof(u32));
  mark += sizeof(u32);
  u32 index = 0;
  for(u32 n = 0; n < runCount; n++) {
    u32 run[2];
    std::memcpy(run, data + sizeof(u32) + n * sizeof(run), sizeof(run));
    for(u32 k = 0; k < run[1]; k++, word += sizeof(u32), index++) {
      u32 value;
      std::memcpy(&value, word, sizeof(u32));
      if(run[0] != ~0u) listWordDrawn((run[0] + k * 4) & 0x0fff'ffff);
      runWords++;
      render.apply(value);
      if(value >> 24 == GE::TransferStart) {
        undrawn = ~0u;
        if(render.pspTiming) {
          auto t = render.transferAsked();
          render.oweTransfer(t.width, t.height, t.bytes, t.source, t.destination);
        }
      }
      for(u32 at; marksLeft && (std::memcpy(&at, mark, sizeof(u32)), (at & ~VerticesMark) == index);
          marksLeft--, mark += MarkBytes) {
        f64 clocks;
        std::memcpy(&clocks, mark + sizeof(u32), sizeof(f64));
        if(!(at & VerticesMark)) render.pipeFront(clocks);
        else if(render.pspTiming) render.pipe.vertices += clocks, render.ramRead(0, clocks);  //(as oweVertices())
      }
    }
  }
}

auto GeThread::replay(const Header& header, const u8* data) -> void {
  switch(header.kind) {
  case Kind::Words: replayWords(data); break;
  case Kind::Primitive: case Kind::Patch: {
    u32 head[7];
    std::memcpy(head, data, sizeof(head));
    if(head[6]) replayWords(data + sizeof(head));  //(the words before it, which came in its record)
    runWords++;
    Copies copies;
    std::memcpy(&copies, data + sizeof(head) + head[6], sizeof(Copies));
    const u8* bytes = data + sizeof(head) + head[6] + sizeof(Copies);
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
    runWords++;
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
  case Kind::Stores: {  //(each store's checks in turn, and the bytes it wrote told after them, as its own record did)
    u64 cx;
    u32 count;
    std::memcpy(&cx, data, sizeof(cx));
    std::memcpy(&count, data + sizeof(cx), sizeof(count));
    const u8* at = data + sizeof(cx) + sizeof(count);
    for(u32 n = 0; n < count; n++) {
      u32 store[3];
      std::memcpy(store, at, sizeof(store));
      at += sizeof(store);
      render.memory.storeChecks(store[1], store[0]);
      for(u32 k = 0; k < store[2]; k++, at += 2 * sizeof(u32)) {
        u32 range[2];
        std::memcpy(range, at, sizeof(range));
        if(checking) replica.write(range[0], range[1], cx);
      }
    }
    break;
  }
  case Kind::RunStart: {  //(with the PSP's timing, the pipeline moved on first, as Kernel::geResume() moves it)
    u32 head[2];
    f64 clocks;
    std::memcpy(head, data, sizeof(head));
    std::memcpy(&clocks, data + sizeof(head), sizeof(f64));
    takenRuns++;
    render.pspTiming = head[0];
    if(head[0] && head[1]) render.pipeShift(clocks);
    runWords = 0, render.wordsRun = &runWords;
    render.owedPixels = render.owedPrimitives = 0;
    publishSoFar();
    undrawn = ~0u;
    render.runStart();
    break;
  }
  case Kind::RunEnd: {  //(the front end's last work, as Kernel::geTime() ends a run, and the pipeline's clocks kept)
    u64 words;
    std::memcpy(&words, data, sizeof(words));
    render.runEnd();
    if(checking && words != runWords) {
      std::fprintf(stderr, "PSP GE thread: a run of %llu words counted %llu by the render GE, at record %llu\n",
                   (unsigned long long)words, (unsigned long long)runWords, (unsigned long long)header.serial);
      std::abort();
    }
    if(render.pspTiming) render.pipeStop(words);
    render.wordsRun = nullptr;
    owedRuns[takenRuns % RunsKept] = {render.owedPixels, render.owedPrimitives, render.pipeTime(), render.pipe.front,
                                      {render.ramClocks[0], render.ramClocks[1], render.ramClocks[2], render.ramClocks[3]},
                                      render.allClocks};
    render.owedPixels = render.owedPrimitives = 0;
    runsEnded.store(takenRuns, std::memory_order_release);
    runsEnded.notify_all();
    break;
  }
  case Kind::Owe: {  //(Kernel::geOwe()'s pipeline begun afresh, with the list's own clocks ahead of its words)
    f64 clocks;
    std::memcpy(&clocks, data, sizeof(clocks));
    render.pipeStart();
    render.pipe.front = clocks;
    break;
  }
  case Kind::State: {
    GE::State state;
    std::memcpy(&state, data, sizeof(state));
    render.takeState(state);
    break;
  }
  case Kind::Frame: {
    Frame frame;
    std::memcpy(&frame, data, sizeof(frame));
    if(frameEnd) frameEnd(frame);
    framesEnded.fetch_add(1, std::memory_order_release);
    framesEnded.notify_all();
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
  case TextureFlush:           texelCache = {}; break;  //(the GE's time: its cache emptied, as run() empties it)
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
  if(memory.vramGuard && !stressed()) renderer->submit(*this);
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
  if(pspTiming) pipeDrain(), pipeItem(Cost::PaletteLoad + size * Cost::PaletteByte, 1);  //(as loadClut() times it)
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
    u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize;
    u32 seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
    return memory.vram[Memory::vramOffset(copy, seen)];
  }
  if(physical >= Memory::ScratchpadBase && physical - Memory::ScratchpadBase < Memory::ScratchpadSize) {
    return memory.scratchpad[physical - Memory::ScratchpadBase];
  }
  return 0;
}

//(the kernel's GE) A PRIM's vertices and indices, as readVertices() would read them, for the render GE, which makes
//readVertices()' drawnFirst() calls itself (readCopiedVertices()). Where the render GE may still change bytes they're
//read from (the value rule: GeThread::model), those calls are handed over as they come and waited for first; its reads
//are readVertices()' (those with nothing behind them reported as there), the vertices decoded and dropped; then what's
//copied (copies: its indices' bytes and its vertices', which copyRanges() takes), and the addresses moved on as
//readVertices() moves them.
auto GE::copyVertices(u32 count, const VertexFormat& format, GeThread::Copies& copies) -> void {
  copies = {};
  u32 indexBytes = format.indexFormat == 3 ? 4 : format.indexFormat;
  u64 low = vertexAddress & 0x1fff'ffff, high = low + u64(count) * format.size;
  auto inside = [&](u64 base, u64 size) { return low >= base && high <= base + size; };
  u64 copy = (low - Memory::VRAMBase) / Memory::VRAMSize * Memory::VRAMSize + Memory::VRAMBase;
  bool together = !format.indexFormat && count && (inside(Memory::RAMBase, memory.ram.size()) ||
                  (low >= Memory::VRAMBase && inside(copy, Memory::VRAMSize) &&
                   inside(Memory::VRAMBase, Memory::VRAMWindow)));
  if(together) thread->readClean(vertexAddress, count * format.size);
  u32 least = ~0u, most = 0;
  if(!together && count && format.size) {  //(readVertices()' way a vertex at a time; by indices all in RAM, no wait)
    Memory::KnownDrawn known;
    for(u32 n = 0; n < count; n++) {
      if(format.indexFormat) thread->readClean(indexAddress + n * indexBytes, indexBytes);
      u32 index = format.indexFormat ? readIndex(n, format) : n;
      u32 address = vertexAddress + index * format.size;
      thread->readClean(address, format.size);
      (void)readVertex(address, format);
      least = std::min(least, index), most = std::max(most, index);
    }
  }
  auto take = [&](u32 address, u32 size) {
    copies.address[copies.ranges] = address, copies.size[copies.ranges] = size, copies.ranges++;
  };
  if(count && format.size && !format.indexFormat) take(vertexAddress, count * format.size);
  if(count && format.size && format.indexFormat) {  //(the vertices from the least to the most: a big buffer's, a few)
    take(indexAddress, count * indexBytes);
    take(vertexAddress + least * format.size, (most - least + 1) * format.size);
  }
  if(format.indexFormat) indexAddress += count * indexBytes;
  else vertexAddress += count * format.size;
}

//(the kernel's GE) A PRIM's copies' bytes, as memory holds them now (each clean: copyVertices()), one range after
//another at to. RAM's and those of a copy of VRAM that keeps them in order taken whole: Chains of Olympus's vertices,
//in VRAM, cost the emulation thread an eighth of its time taken a byte at a time.
auto GE::copyRanges(const GeThread::Copies& copies, u8* to) -> void {
  for(u32 n = 0; n < copies.ranges; n++) {
    u32 address = copies.address[n], size = copies.size[n], physical = address & 0x1fff'ffff;
    u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize;
    u32 seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
    if(physical >= Memory::RAMBase && u64(physical - Memory::RAMBase) + size <= memory.ram.size()) {
      std::memcpy(to, memory.ram.data() + (physical - Memory::RAMBase), size);
    } else if(physical >= Memory::VRAMBase && physical - Memory::VRAMBase < Memory::VRAMWindow && !(copy & 1) &&
              u64(seen) + size <= Memory::VRAMSize) {
      std::memcpy(to, memory.vram.data() + seen, size);
    } else {
      for(u32 k = 0; k < size; k++) to[k] = byteAt(memory, address + k);
    }
    to += size;
  }
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
  copyVertices(count, format, copies);
  bool textured = commands[TextureMappingEnable] & 1;
  auto& texture = thread->handTexture;
  if(textured) {
    u32 words[5] = {commands[TextureFormat], commands[TextureAddress0], commands[TextureBufferWidth0],
                    commands[TextureSize0], commands[TextureMode]};
    if(!texture.known || std::memcmp(words, texture.words, sizeof(words))) {
      Sampler sampled = sampler();
      textureBytes(sampled, std::min<u32>(sampled.height, 512), texture.low, texture.high);
      texture.paged = texture.high > texture.low &&
                      memory.pagesOf(texture.low, texture.high - texture.low, texture.first, texture.last);
      std::memcpy(texture.words, words, sizeof(words)), texture.known = true;
    }
    if(texture.high > texture.low) memory.watch(texture.low, texture.high - texture.low);
  }
  bool hardware = thread->hardware;
  u32 copied = 0;
  for(u32 n = 0; n < copies.ranges; n++) copied += copies.size[n];
  u64 at = thread->primitivePlace(copied);
  //(the texture's pages: their stores heard of till the render GE passes it)
  if(textured && texture.paged) thread->textureRead(texture.first, texture.last, at);
  //(its copies' bytes taken into its record, or kept here where it's too big for the ring)
  u8* bytes = thread->primitiveBegin(word, running ? listWordAt : ~0u, vertexAt, indexAt, hardware, copies);
  if(!bytes) handCopies.resize(copied), bytes = handCopies.data();
  copyRanges(copies, bytes);
  bool certain = modelPrimitive(thread->model, word, vertexAt, indexAt, copies, bytes, hardware, at);
  thread->primitiveEnd(certain, bytes);
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
    u32 least = ~0u, most = 0;
    for(u32 n = 0; n < count; n++) {
      u32 i = format.indexFormat ? index(n) : n;
      offsets[n] = i, least = std::min(least, i), most = std::max(most, i);
    }
    for(auto& offset : offsets) offset = (offset - least) * format.size;  //(from the least's vertex: copyVertices())
    u32 first = vertexAddress + least * format.size;
    const u8* bytes = copied.at(first, (most - least + 1) * format.size);
    if(bytes && vertexRuns && format.morphs == 1) {
      vertices.resize(count);
      readVerticesFrom(bytes, offsets.data(), count, format, vertices.data());
      vertexRunsRead++;
    } else {
      for(u32 n = 0; n < count; n++) {
        u32 address = first + offsets[n];
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
  oweVertices(count, format);
}
