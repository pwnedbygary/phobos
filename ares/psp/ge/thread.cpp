//The GE on a thread of its own: thread.hpp says how the kernel's GE and the render GE share the work.

GeThread::GeThread(GE& control, GE& render) : control(control), render(render) {
  words.reserve(WordsMost);
  //(the render GE's watches posted to the kernel's GE's memory as the thread drains: compiled stores to those pages
  //go through memory's slow path, which reaches the render GE's view: Memory::drawer)
  render.memory.watching = [this](u32 page) { watches.push_back(page); };
  render.memory.unwatched = nullptr;
  render.memory.written = [this](u32 address, u32 size) { changes.push_back({address, size}); };
  render.memory.unmapped = nullptr;
  thread = std::thread([this] { take(); });
}

GeThread::~GeThread() {
  drain();
  ring.stop();
  thread.join();
  render.memory.watching = nullptr;
  render.memory.written = nullptr;
}

auto GeThread::append(Kind kind, u32 size) -> u8* {
  u8* at = ring.reserve(sizeof(Header) + size);
  if(!at) return nullptr;
  Header header{kind, size, ++serial};
  std::memcpy(at, &header, sizeof(header));
  return at + sizeof(header);
}

auto GeThread::flushWords() -> void {
  if(words.empty()) return;
  u32 count = words.size();
  u8* at = append(Kind::Words, sizeof(u32) + count * sizeof(u32));
  std::memcpy(at, &count, sizeof(u32));
  std::memcpy(at + sizeof(u32), words.data(), count * sizeof(u32));
  ring.publish();
  words.clear();
}

//(a PRIM's or curved surface's record: its word, addresses and copies; one too big for the ring is drawn by the
//emulation thread, the render GE's code there and then, once the thread has drained)
static auto copiedBytes(const GeThread::Copies& copies) -> u32 {
  u32 bytes = 0;
  for(u32 n = 0; n < copies.ranges; n++) bytes += copies.size[n];
  return bytes;
}

auto GeThread::primitive(u32 word, u32 vertexAddress, u32 indexAddress, const Copies& copies, const u8* bytes)
  -> void {
  flushWords();
  u32 size = 3 * sizeof(u32) + sizeof(Copies) + copiedBytes(copies);
  u8* at = append(Kind::Primitive, size);
  if(!at) {
    drain();
    render.replayPrimitive(word, vertexAddress, indexAddress, copies, bytes, false);
    return;
  }
  u32 head[3] = {word, vertexAddress, indexAddress};
  std::memcpy(at, head, sizeof(head));
  std::memcpy(at + sizeof(head), &copies, sizeof(Copies));
  std::memcpy(at + sizeof(head) + sizeof(Copies), bytes, copiedBytes(copies));
  ring.publish();
}

auto GeThread::patch(u32 word, u32 vertexAddress, u32 indexAddress, const Copies& copies, const u8* bytes) -> void {
  flushWords();
  u32 size = 3 * sizeof(u32) + sizeof(Copies) + copiedBytes(copies);
  u8* at = append(Kind::Patch, size);
  if(!at) {
    drain();
    render.replayPrimitive(word, vertexAddress, indexAddress, copies, bytes, true);
    return;
  }
  u32 head[3] = {word, vertexAddress, indexAddress};
  std::memcpy(at, head, sizeof(head));
  std::memcpy(at + sizeof(head), &copies, sizeof(Copies));
  std::memcpy(at + sizeof(head) + sizeof(Copies), bytes, copiedBytes(copies));
  ring.publish();
}

auto GeThread::clut(u32 word, u32 address, const u8* bytes, u32 size) -> void {
  flushWords();
  u8* at = append(Kind::Clut, 3 * sizeof(u32) + size);
  u32 head[3] = {word, address, size};
  std::memcpy(at, head, sizeof(head));
  std::memcpy(at + sizeof(head), bytes, size);
  ring.publish();
}

auto GeThread::runStart() -> void {
  flushWords();
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

auto GeThread::drain() -> void {
  flushWords();
  for(u64 at; (at = done.load(std::memory_order_acquire)) != serial;) done.wait(at, std::memory_order_acquire);
  //(what the render GE watched and changed meanwhile, as the kernel's GE's memory hears of it: its watches kept, and
  //compiled code from what it changed dropped)
  for(u32 page : watches) control.memory.watch(page * Memory::PageSize, Memory::PageSize);
  watches.clear();
  if(control.memory.written) for(auto [address, size] : changes) control.memory.written(address, size);
  changes.clear();
  for(auto& text : notes) control.note(text);  //(each once, whichever GE noted it)
  notes.clear();
}

auto GeThread::take() -> void {
  while(const u8* at = ring.next()) {
    Header header;
    std::memcpy(&header, at, sizeof(header));
    replay(header, at + sizeof(header));
    ring.release();
    done.store(header.serial, std::memory_order_release);
    done.notify_all();
  }
}

auto GeThread::replay(const Header& header, const u8* data) -> void {
  switch(header.kind) {
  case Kind::Words: {
    u32 count;
    std::memcpy(&count, data, sizeof(u32));
    for(u32 n = 0; n < count; n++) {
      u32 word;
      std::memcpy(&word, data + sizeof(u32) + n * sizeof(u32), sizeof(u32));
      render.apply(word);
    }
    break;
  }
  case Kind::Primitive: case Kind::Patch: {
    u32 head[3];
    std::memcpy(head, data, sizeof(head));
    Copies copies;
    std::memcpy(&copies, data + sizeof(head), sizeof(Copies));
    render.replayPrimitive(head[0], head[1], head[2], copies, data + sizeof(head) + sizeof(Copies),
                           header.kind == Kind::Patch);
    publishSoFar();
    break;
  }
  case Kind::Clut: {
    u32 head[3];
    std::memcpy(head, data, sizeof(head));
    render.replayClut(head[0], data + sizeof(head), head[2]);
    break;
  }
  case Kind::RunStart:
    takenRuns++;
    render.owedPixels = render.owedPrimitives = 0;
    publishSoFar();
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

//(the render GE) CLUT_LOAD from the palette's bytes the kernel's GE copied.
auto GE::replayClut(u32 word, const u8* bytes, u32 size) -> void {
  commands[ClutLoad] = word;
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

//(the kernel's GE) A PRIM's vertices and indices, as readVertices() would read them, copied for the render GE. Its
//reads are made as readVertices() makes them, the vertices decoded and dropped, so that VRAM's waits and drawnFirst()
//come where they come synchronously (the render GE's, the thread drained, where they reach VRAM: drawer()); then the
//bytes copied, its indices' and its vertices', as memory holds them now, and the addresses moved on as readVertices()
//moves them.
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
  if(together) drawnFirst(vertexAddress, count * format.size);
  u32 most = 0;
  if(!together && count && format.size) {  //(readVertices()' way a vertex at a time; by indices all in RAM, no wait)
    for(u32 n = 0; n < count; n++) {
      if(format.indexFormat) drawnFirst(indexAddress + n * indexBytes, indexBytes);
      u32 index = format.indexFormat ? readIndex(n, format) : n;
      u32 address = vertexAddress + index * format.size;
      drawnFirst(address, format.size);
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
  if(patching) thread->patch(word, vertexAt, indexAt, copies, handCopies.data());
  else thread->primitive(word, vertexAt, indexAt, copies, handCopies.data());
  if(!running) handedState = stateOf(), thread->drain();  //(outside a list the GE draws a PRIM at once: primitive())
}

//(the kernel's GE) CLUT_LOAD handed over: the palette's bytes read as loadClut() reads them (its drawnFirst() and
//reads made, the render GE's palette left alone), then copied.
auto GE::handClut() -> void {
  if(!running) handSwitches(), handState();
  u32 address = (commands[ClutAddress] & 0xff'fff0) | (commands[ClutAddressUpper] << 8 & 0x0f00'0000);
  u32 bytes = std::min<u32>((commands[ClutLoad] & 0x3f) * 32, sizeof(clut));
  drawnFirst(address, bytes);
  u8 palette[sizeof(clut)] = {};
  bool copied = [&] {
    Memory::KnownDrawn known;
    return memory.copyOut(palette, address, bytes);
  }();
  if(!copied) {
    for(u32 n = 0; n < bytes; n++) palette[n] = memory.read(1, address + n);
  }
  if(std::memcmp(clut, palette, bytes)) std::memcpy(clut, palette, bytes), paletteChanged();
  thread->clut(commands[ClutLoad], address, palette, bytes);
  if(!running) thread->drain();
}

//(the render GE) readVertices() of a PRIM handed over: its bytes from the copies (copied), which hold every byte it
//reads; drawnFirst() and memory's waits were the kernel's GE's (copyVertices()).
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
