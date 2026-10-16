//Versions of memory's bytes: versions.hpp says how the two threads share them.

GeVersions::~GeVersions() {
  for(auto& [page, version] : live) delete version;
  for(auto* version : spare) delete version;
}

//(the emulation thread waiting for the GE thread's access to a page to end: one access, which never waits pinned)
static auto unpinned(std::atomic<u32>& pins) -> void {
  for(u32 spins = 0; pins.load(std::memory_order_seq_cst); spins++) {
    if(spins >= 64) std::this_thread::yield();
  }
}

auto GeVersions::save(u32 page, u32 offset, u32 size, u64 at) -> void {
  if(page >= Pages || !size || offset >= PageBytes) return;
  size = std::min(size, PageBytes - offset);
  Page& p = pages[page];
  Version* version;
  if(spare.empty()) version = new Version;
  else version = spare.back(), spare.pop_back();
  version->at = at, version->offset = offset, version->size = size;
  version->ready.store(false, std::memory_order_relaxed);
  version->next.store(nullptr, std::memory_order_relaxed);
  version->bytes.resize(size);
  //1. published, the newest last
  if(p.last) p.last->next.store(version, std::memory_order_seq_cst);
  else p.first.store(version, std::memory_order_seq_cst);
  p.last = version;
  if(published) published();
  //2. the GE thread's access to the page, if one is under way, ended
  unpinned(p.pins);
  //3. the bytes as they are now (the GE thread's write-backs under the pin among them), then ready; the caller then
  //writes memory (4.)
  if(u8* bytes = host(context, page)) std::memcpy(version->bytes.data(), bytes + offset, size);
  else std::memset(version->bytes.data(), 0, size);
  version->ready.store(true, std::memory_order_release);
  live.push_back({page, version});
}

//(the oldest made is its page's oldest: a page's versions are made in the order of their places)
auto GeVersions::free(u64 passed) -> void {
  while(!live.empty() && live.front().second->at <= passed) {
    auto [page, version] = live.front();
    live.pop_front();
    Page& p = pages[page];
    p.first.store(version->next.load(std::memory_order_relaxed), std::memory_order_seq_cst);
    if(p.last == version) p.last = nullptr;
    unpinned(p.pins);  //(an access under way may still be looking at it)
    spare.push_back(version);
  }
}

GeVersions::Hold::Hold(GeVersions& v, u64 q, bool write, u32 first, u32 last, u32 first2, u32 last2)
: v(v), q(q), write(write), ranges{{first, std::min(last, Pages - 1)}, {first2, std::min(last2, Pages - 1)}} {
  while(true) {
    pin(+1);
    if(v.pinned) v.pinned();
    Version* waiting = nullptr;
    versioned = false;
    for(auto& range : ranges) {
      for(u32 page = range[0]; page <= range[1] && !waiting; page++) {
        Version* version = v.pages[page].first.load(std::memory_order_seq_cst);
        for(; version; version = version->next.load(std::memory_order_seq_cst)) {
          if(!applies(*version)) continue;
          if(!version->ready.load(std::memory_order_acquire)) { waiting = version; break; }
          versioned = true;
        }
      }
    }
    if(!waiting) return;
    //(never waiting pinned: let go, wait till it's ready, and look again. It isn't freed meanwhile: its place is
    //past this one's, which the GE thread hasn't passed)
    pin(-1);
    for(u32 spins = 0; !waiting->ready.load(std::memory_order_acquire); spins++) {
      if(spins >= 64) std::this_thread::yield();
    }
  }
}

GeVersions::Hold::~Hold() {
  pin(-1);
}

auto GeVersions::Hold::pin(s32 delta) const -> void {
  for(auto& range : ranges) {
    for(u32 page = range[0]; page <= range[1]; page++) {
      if(delta > 0) v.pages[page].pins.fetch_add(1, std::memory_order_seq_cst);
      else v.pages[page].pins.fetch_sub(1, std::memory_order_release);
    }
  }
}

//(a version published since the pages were pinned isn't ready, nor will be till they're let go of: the emulation
//thread waits for that before it copies the bytes and writes memory, so memory has them as that version will)
auto GeVersions::Hold::read(u32 page, u32 offset, u32 size, u8* out) const -> void {
  if(!size) return;
  const u8* storage = page < Pages ? v.host(v.context, page) : nullptr;
  u64 taken[PageBytes / 64] = {};
  if(page < Pages) {
    for(auto* version = v.pages[page].first.load(std::memory_order_seq_cst); version;
        version = version->next.load(std::memory_order_seq_cst)) {
      if(version->at <= q || !version->ready.load(std::memory_order_acquire)) continue;
      u32 low = std::max(offset, version->offset), high = std::min(offset + size, version->offset + version->size);
      for(u32 byte = low; byte < high; byte++) {
        if(taken[byte >> 6] >> (byte & 63) & 1) continue;
        taken[byte >> 6] |= 1ull << (byte & 63);
        out[byte - offset] = version->bytes[byte - version->offset];
      }
    }
  }
  for(u32 byte = offset; byte < offset + size; byte++) {
    if(!(taken[byte >> 6] >> (byte & 63) & 1)) out[byte - offset] = storage ? storage[byte] : 0;
  }
}

auto GeVersions::Hold::writeBack(u32 page, u32 offset, u32 size, const u8* in,
                                 const std::function<auto (u32 byte) -> void>* stored) const -> void {
  u8* storage = page < Pages ? v.host(v.context, page) : nullptr;
  if(!storage || !size) return;
  u64 taken[PageBytes / 64] = {};
  for(auto* version = v.pages[page].first.load(std::memory_order_seq_cst); version;
      version = version->next.load(std::memory_order_seq_cst)) {
    if(version->at < q || !version->ready.load(std::memory_order_acquire)) continue;  //(as read()'s: its copy takes it)
    u32 low = std::max(offset, version->offset), high = std::min(offset + size, version->offset + version->size);
    for(u32 byte = low; byte < high; byte++) {
      if(taken[byte >> 6] >> (byte & 63) & 1) continue;
      taken[byte >> 6] |= 1ull << (byte & 63);
      u8& kept = version->bytes[byte - version->offset];
      if(kept != in[byte - offset]) kept = in[byte - offset];
    }
  }
  for(u32 byte = offset; byte < offset + size; byte++) {
    if(taken[byte >> 6] >> (byte & 63) & 1) continue;
    if(storage[byte] == in[byte - offset]) continue;
    if(stored) (*stored)(byte);
    storage[byte] = in[byte - offset];
  }
}

//The GE's side: the render GE's reads of memory and its write-backs, as of the record it does (place).

//(GeVersions' host(): where a page of memory is in the host's, context the Memory)
auto GE::hostPage(void* context, u32 page) -> u8* {
  auto& memory = *(Memory*)context;
  u32 address = page << GeVersions::PageBits;
  if(address >= Memory::RAMBase && address - Memory::RAMBase < memory.ram.size()) {
    return &memory.ram[address - Memory::RAMBase];
  }
  if(address >= Memory::VRAMBase && address - Memory::VRAMBase < Memory::VRAMSize) {
    return &memory.vram[address - Memory::VRAMBase];
  }
  if(address >= Memory::ScratchpadBase && address - Memory::ScratchpadBase < Memory::ScratchpadSize) {
    return &memory.scratchpad[address - Memory::ScratchpadBase];
  }
  return nullptr;
}

//(the pages of VRAM's bytes low to high, offsets, those past its end its start's: GeVersions' page numbers)
static auto vramHold(GeVersions& versions, u64 place, bool write, u64 low, u64 high) -> GeVersions::Hold {
  u32 base = Memory::VRAMBase / GeVersions::PageBytes;
  if(high < low || high - low >= Memory::VRAMSize) low = 0, high = Memory::VRAMSize - 1;
  high -= low - low % Memory::VRAMSize, low %= Memory::VRAMSize;
  u32 first = base + low / GeVersions::PageBytes;
  u32 last = base + u32(std::min<u64>(high, Memory::VRAMSize - 1) / GeVersions::PageBytes);
  u32 first2 = 1, last2 = 0;
  if(high >= Memory::VRAMSize) first2 = base, last2 = base + u32((high - Memory::VRAMSize) / GeVersions::PageBytes);
  return {versions, place, write, first, last, first2, last2};
}

auto GE::vramBytes() -> u8* {
  return memory.vram.data();
}

//(readingVRAM()'s on the GE thread)
//(Version places are 2 a record: the record at place n is 2n, a write with a record of its own at n 2n, one without,
//after the record at n, 2n + 1: GeThread::before().)
auto GE::readingVersions(u64 low, u64 high, const std::function<auto (const u8* vram) -> void>& use) -> void {
  readDeclared(low, high);
  auto hold = vramHold(*versions, 2 * readPlace(), false, low, high);
  if(!hold.versioned) return use(memory.vram.data());
  u32 base = Memory::VRAMBase / GeVersions::PageBytes;
  versionedVRAM.resize(Memory::VRAMSize);
  for(auto& range : hold.ranges) {
    for(u32 page = range[0]; page <= range[1]; page++) {
      hold.read(page, 0, GeVersions::PageBytes, versionedVRAM.data() + (page - base) * GeVersions::PageBytes);
    }
  }
  use(versionedVRAM.data());
}

//(writingVRAM()'s on the GE thread)
auto GE::writingVersions(u64 low, u64 high, const std::function<auto (const Writes&) -> void>& use) -> void {
  auto hold = vramHold(*versions, 2 * place, true, low, high);
  use(Writes{*this, hold.versioned ? &hold : nullptr});
}

auto GE::Writes::put(u32 offset, const u8* bytes, u32 size) const -> void {
  if(!hold) {
    u8* vram = ge.memory.vram.data() + offset;
    bool checking = ge.reporting && ge.reporting->checking;
    for(u32 n = 0; n < size; n++) {
      if(vram[n] == bytes[n]) continue;
      if(checking) ge.reporting->checkChanged(offset + n);
      vram[n] = bytes[n];
    }
    return;
  }
  u32 base = Memory::VRAMBase / GeVersions::PageBytes;
  bool checking = ge.reporting && ge.reporting->checking;
  for(u32 done = 0; done < size;) {
    u32 at = offset + done, part = std::min(size - done, GeVersions::PageBytes - at % GeVersions::PageBytes);
    u32 page = at / GeVersions::PageBytes;
    std::function<auto (u32) -> void> stored = [&](u32 byte) {
      ge.reporting->checkChanged(page * GeVersions::PageBytes + byte);
    };
    hold->writeBack(base + page, at % GeVersions::PageBytes, part, bytes + done, checking ? &stored : nullptr);
    done += part;
  }
}

//(a byte's page and its offset in it, as GeVersions numbers them: VRAM's through any copy by where it is, the first
//copy's page; false where nothing is)
static auto pageOf(const Memory& memory, u32 address, u32& page, u32& offset) -> bool {
  u32 physical = address & 0x1fff'ffff;
  if(physical >= Memory::VRAMBase && physical - Memory::VRAMBase < Memory::VRAMWindow) {
    u32 copy = (physical - Memory::VRAMBase) / Memory::VRAMSize, seen = (physical - Memory::VRAMBase) % Memory::VRAMSize;
    physical = Memory::VRAMBase + Memory::vramOffset(copy, seen);
  } else if(!(physical >= Memory::RAMBase && physical - Memory::RAMBase < memory.ram.size()) &&
            !(physical >= Memory::ScratchpadBase && physical - Memory::ScratchpadBase < Memory::ScratchpadSize)) {
    return false;
  }
  page = physical / GeVersions::PageBytes, offset = physical % GeVersions::PageBytes;
  return true;
}

GE::Reading::Reading(GE& ge, u32 address, u32 size) : ge(ge) {
  u32 first, last;
  if(ge.versioned()) ge.readDeclared(address, size);
  if(ge.versioned() && ge.memory.pagesOf(address, size, first, last)) {
    hold.emplace(*ge.versions, 2 * ge.readPlace(), false, first, last);
  }
}

auto GE::Reading::pointer(u32 address, u32 size) -> const u8* {
  const u8* bytes = ge.memory.pointer(address, size);
  if(!bytes || !hold || !hold->versioned) return bytes;
  //(side by side in the host's memory, as pointer() found them: RAM, the scratchpad, VRAM through a plain copy, or
  //inside a 32-byte piece of a rearranging one)
  copy.resize(size);
  u32 page, offset;
  if(!pageOf(ge.memory, address, page, offset)) return bytes;
  for(u32 done = 0; done < size;) {
    u32 part = std::min(size - done, GeVersions::PageBytes - offset);
    hold->read(page, offset, part, copy.data() + done);
    done += part, page++, offset = 0;
  }
  return copy.data();
}

auto GE::Reading::read(u32 size, u32 address) -> u32 {
  if(!hold || !hold->versioned) return ge.memory.read(size, address);
  if(!ge.memory.reaches(address, size)) return ge.memory.read(size, address);  //(nothing behind them: as memory says)
  u32 value = 0;
  for(u32 n = 0; n < size; n++) {
    u32 page, offset;
    u8 byte = 0;
    if(pageOf(ge.memory, address + n, page, offset)) hold->read(page, offset, 1, &byte);
    value |= u32(byte) << 8 * n;
  }
  return value;
}
