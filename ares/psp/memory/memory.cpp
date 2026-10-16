#include "memory.hpp"

namespace ares::PlayStationPortable {

thread_local bool Memory::knownDrawn = false;
thread_local bool Memory::copyingOut = false;

//Where in VRAM an offset seen through copy 0-3 is (see memory.hpp), and the reverse: the offset through that copy
//that sees a byte of VRAM. Each keeps an offset in its 16 KiB.
auto Memory::vramOffset(u32 copy, u32 seen) -> u32 {
  if(!(copy & 1)) return seen;
  if(copy == 3) seen = (seen & ~0x3e0u) | (seen >> 9 & 1) << 5 | (seen >> 5 & 15) << 6;
  return seen ^ 0x2040;
}

auto Memory::vramSeen(u32 copy, u32 offset) -> u32 {
  if(!(copy & 1)) return offset;
  offset ^= 0x2040;
  if(copy == 3) offset = (offset & ~0x3e0u) | (offset >> 5 & 1) << 9 | (offset >> 6 & 15) << 5;
  return offset;
}

//Clears memory, sized for the model: 32 MiB of main RAM for the PSP-1000, 64 MiB for later models. The buffers are
//only made again when a size changes, so a page table built from them stays valid across power().
auto Memory::power(u32 ramSize) -> void {
  if(Memory& m = drawer ? drawer() : *this; m.vramBusy) m.finishDrawing();
  unwatchAll();
  auto clear = [](Bytes& bytes, u32 size) {
    if(bytes.size() != size) bytes.assign(size, 0);
    else std::fill(bytes.begin(), bytes.end(), 0);
  };
  clear(scratchpad, ScratchpadSize);
  clear(vram, VRAMSize);
  clear(ram, ramSize);
  if(watched.size() != 512_MiB / PageSize) watched.assign(512_MiB / PageSize, 0);
}

//Its areas become storage's, seen through this one with watches, busy pages and callbacks of its own; nothing watched.
//(Again after storage's are made afresh: power() with another size, a state of another size.)
auto Memory::view(Memory& storage) -> void {
  unwatchAll();
  scratchpad.view(storage.scratchpad), vram.view(storage.vram), ram.view(storage.ram);
  if(watched.size() != 512_MiB / PageSize) watched.assign(512_MiB / PageSize, 0);
}

//The pages (numbered as `watched` numbers them) that size bytes from address reach: false for none. Through VRAM's
//second and fourth copies, which rearrange it within each 16 KiB, a range longer than its 32-byte piece is taken as
//every 16 KiB it touches.
auto Memory::pagesOf(u32 address, u32 size, u32& first, u32& last) const -> bool {
  if(!size) return false;
  u32 physical = address & 0x1fff'ffff;
  if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
    u32 copy = (physical - VRAMBase) / VRAMSize, seen = (physical - VRAMBase) % VRAMSize;
    u32 low = seen, high = std::min<u32>(seen + std::min<u32>(size, VRAMSize), VRAMSize) - 1;
    if(copy & 1) {
      if((seen & 31) + size <= 32) low = high = vramOffset(copy, seen);
      else low &= ~0x3fffu, high |= 0x3fff;
    }
    first = (VRAMBase + low) / PageSize, last = (VRAMBase + high) / PageSize;
    return true;
  }
  first = physical / PageSize;
  last = std::min<u64>(u64(physical) + size - 1, 0x1fff'ffff) / PageSize;
  return true;
}

//Watches the pages size bytes from address reach (see watched in memory.hpp).
auto Memory::watch(u32 address, u32 size) -> void {
  u32 first, last;
  if(watched.empty() || !pagesOf(address, size, first, last)) return;
  for(u32 page = first; page <= last; page++) {
    if(watched[page]) continue;
    watched[page] = 1;
    watchedPages++;
    if(watching) watching(page);
  }
}

//Every page's contents replaced at once (power, a state loaded): whoever watched them hears of it, and none is
//watched any more.
auto Memory::unwatchAll() -> void {
  if(!watchedPages) return;
  for(u32 page = 0; page < watched.size() && watchedPages; page++) {
    if(!watched[page]) continue;
    watched[page] = 0;
    watchedPages--;
    if(watchedWritten) watchedWritten(page);
    if(unwatched) unwatched(page);
  }
  watchedPages = 0;
}

//page no longer watched, as a change to it stops its watch (changed()): its watchedWritten() and unwatched() told.
auto Memory::unwatch(u32 page) -> void {
  if(page >= watched.size() || !watched[page]) return;
  watched[page] = 0;
  watchedPages--;
  if(watchedWritten) watchedWritten(page);
  if(unwatched) unwatched(page);
}

//(a store of size bytes from address about to land: where one of its pages is watched and a drawing memory may still
//read it, that memory done first: drawer)
auto Memory::storing(u32 address, u32 size) -> void {
  if(!drawer || !watchedPages) return;
  u32 first, last;
  if(!pagesOf(address, size, first, last)) return;
  for(u32 page = first; page <= last; page++) {
    if(watched[page]) return void(drawer());
  }
}

//Saving and loading memory, for save states: the scratchpad, VRAM and main RAM (whoever loads a state checks first
//that RAM is the size it was). Each goes 4 KiB at a time: a byte saying whether the piece holds anything but zeros,
//then its bytes if it does, since games leave much of their 64 MiB untouched and a state needn't carry it.
auto Memory::serialize(serializer& s) -> void {
  if(Memory& m = drawer ? drawer() : *this; m.vramBusy) m.finishDrawing();
  if(s.reading()) unwatchAll();
  for(auto* area : {&scratchpad, &vram, &ram}) {
    for(u32 at = 0; at < area->size(); at += 4_KiB) {
      std::span<u8> piece{area->data() + at, std::min<size_t>(4_KiB, area->size() - at)};
      u8 used = s.writing() && std::any_of(piece.begin(), piece.end(), [](u8 byte) { return byte != 0; });
      s(used);
      if(used) s(piece);
      else if(s.reading()) std::fill(piece.begin(), piece.end(), 0);
    }
  }
}

//Where the size bytes from address are in the host's memory, or nullptr if any of them has nothing behind it (or
//they run past the end of an area, or past a 32-byte piece of VRAM's second or fourth copy, which rearrange them).
//load: they're about to be read, not written (read()). (Main RAM's are found in memory.hpp's pointer().)
auto Memory::pointerElsewhere(u32 address, u32 size, bool load) -> u8* {
  u32 physical = address & 0x1fff'ffff;
  if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
    if(!knownDrawn) {
      if(!drawer) { if(vramBusy) busyCheck(address, size, load); }
      else if((load || copyingOut) && reading) reading(address, size, load);  //(threaded: ge/thread.cpp)
      else drawer().busyCheck(address, size, load);
    }
    u32 copy = (physical - VRAMBase) / VRAMSize, offset = (physical - VRAMBase) % VRAMSize;
    if(copy & 1) {
      if((offset & 31) + size > 32) return nullptr;
      offset = vramOffset(copy, offset);
    }
    return size <= VRAMSize - offset ? &vram[offset] : nullptr;
  }
  if(physical >= ScratchpadBase && physical - ScratchpadBase < ScratchpadSize) {
    u32 offset = physical - ScratchpadBase;
    return size <= ScratchpadSize - offset ? &scratchpad[offset] : nullptr;
  }
  return nullptr;
}

//What pointer() waits for before VRAM's size bytes from address are reached: the drawing over them, where any of their
//pages is busy (the first and third copies: the bytes; others: the 16 KiB they rearrange). load: they're about to be
//read (read()). (Threaded, the render GE's memory's, which the emulation thread's loads have it replay: ge/thread.cpp.)
auto Memory::busyCheck(u32 address, u32 size, bool load) -> void {
  u32 physical = address & 0x1fff'ffff;
  if(!vramBusy || physical < VRAMBase || physical - VRAMBase >= VRAMWindow) return;
  u32 seen = (physical - VRAMBase) % VRAMSize, last = std::min<u32>(seen + size, VRAMSize) - 1;
  bool depthView = (physical - VRAMBase) / VRAMSize & 1;
  if(depthView) seen &= ~0x3fffu, last |= 0x3fff;
  for(u32 page = seen / PageSize; page <= last / PageSize; page++) {
    if(!vramPageBusy(page)) continue;
    if(vramDrawnOver && !vramDrawnOver(seen, last, load && depthView)) break;
    if(finishDrawingOver) finishDrawingOver(seen, last);
    else finishDrawing();
    break;
  }
}

//Whether the size bytes from address all have something behind them, inside one area (one copy of VRAM): what an HLE
//function checks a game's buffer against before copying in or out. (pointer() also wants the bytes side by side in
//the host's memory, which a range through VRAM's second or fourth copy isn't.)
auto Memory::reachesElsewhere(u32 address, u32 size) -> bool {
  u32 physical = address & 0x1fff'ffff;
  if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
    return size <= VRAMSize - (physical - VRAMBase) % VRAMSize;
  }
  return pointer(address, size) != nullptr;
}

//The CPU's loads: size is 1, 2 or 4 bytes, and the CPU has already checked that the address is aligned to it. HLE
//functions use them too, at whatever address a game gave, so an access can cross a 32-byte piece of VRAM's second or
//fourth copy: that one goes a byte at a time.
auto Memory::readElsewhere(u32 size, u32 address) -> u32 {
  u8* bytes = pointer(address, size, true);
  if(!bytes && size > 1 && reaches(address, size)) {
    u32 value = 0;
    for(u32 n = 0; n < size; n++) value |= read(1, address + n) << 8 * n;
    return value;
  }
  if(!bytes) {
    if(unmapped) unmapped(address, false);
    return 0;
  }
  if(size == 1) return bytes[0];
  if(size == 2) { u16 half; std::memcpy(&half, bytes, 2); return half; }
  u32 word;
  std::memcpy(&word, bytes, 4);
  return word;
}

//The CPU's stores, as aligned as its loads (and HLE functions', likewise a byte at a time across such a piece).
//(Threaded, those the GE thread hears of from its records: writeRecorded().)
auto Memory::writeElsewhere(u32 size, u32 address, u32 data) -> void {
  if(writer && recorded(address, size)) return writeRecorded(size, address, data);
  if(!knownDrawn) {  //(not through the second or fourth copy, which rearrange the bytes)
    u32 physical = address & 0x1fff'ffff;
    if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow && !((physical - VRAMBase) / VRAMSize & 1)) {
      Memory& m = drawer ? drawer() : *this;
      u32 seen = (physical - VRAMBase) % VRAMSize;
      if(m.vramBusy && m.vramStoring && seen + size <= VRAMSize) m.vramStoring(seen, seen + size - 1);
    }
  }
  u8* bytes = pointer(address, size);
  if(!bytes && size > 1 && reaches(address, size)) {
    for(u32 n = 0; n < size; n++) write(1, address + n, data >> 8 * n);
    return;
  }
  if(!bytes) {
    if(unmapped) unmapped(address, true);
    return;
  }
  if(size == 1) bytes[0] = data;
  else if(size == 2) { u16 half = data; std::memcpy(bytes, &half, 2); }
  else std::memcpy(bytes, &data, 4);
  changed(address, size);
}

//Tells written() about a change. VRAM's four copies are four physical addresses for the same bytes, so a change
//through one is a change to all of them, and code compiled from any copy must go. Inside one 32-byte piece each copy
//sees the change in one place; a longer one, through a copy that rearranges pieces or seen through one, is reported
//as every 16 KiB it touches (where the rearranging keeps it).
//(Threaded, inside a write the GE thread hears of from its record, recording, the GE's half is that thread's: heard().)
auto Memory::changed(u32 address, u32 size) -> void {
  u32 physical = address & 0x1fff'ffff;
  bool inVRAM = physical >= VRAMBase && physical - VRAMBase < VRAMWindow;
  if(Memory& m = drawer && inVRAM && !recording ? drawer() : *this; m.vramBusy && m.vramChangedBusy && size && inVRAM) {
    u32 seen = (physical - VRAMBase) % VRAMSize, last = std::min<u32>(seen + size, VRAMSize) - 1;
    if((physical - VRAMBase) / VRAMSize & 1) seen &= ~0x3fffu, last |= 0x3fff;
    m.vramChangedBusy(seen, last);  //(the bytes as pointer() has them)
  }
  if(inVRAM && wroteVRAM && size) {  //(the bytes themselves: through a rearranging copy, each 32-byte piece's)
    u32 copy = (physical - VRAMBase) / VRAMSize, seen = (physical - VRAMBase) % VRAMSize;
    u32 end = std::min<u32>(seen + size, VRAMSize);
    if(!(copy & 1)) wroteVRAM(seen, end - 1);
    else for(u32 at = seen; at < end;) {
      u32 run = std::min(end - at, 32 - at % 32), offset = vramOffset(copy, at);
      wroteVRAM(offset, offset + run - 1);
      at += run;
    }
  }
  if(watchedPages) {
    u32 first, last;
    if(pagesOf(address, size, first, last)) {
      for(u32 page = first; page <= last; page++) {
        if(!(page & 7) && page + 7 <= last) {  //(eight at once where none of them is watched)
          u64 eight;
          std::memcpy(&eight, &watched[page], 8);
          if(!eight) { page += 7; continue; }
        }
        if(!watched[page]) continue;
        //(the drawing memory's watch: those it watches are watched here too; inside a write it hears of from its
        //record, its own replay's: heard())
        if(drawer && !recording) drawer().unwatch(page);
        watched[page] = 0;
        watchedPages--;
        if(watchedWritten) watchedWritten(page);
        if(unwatched) unwatched(page);
      }
    }
  }
  if(!written) return;
  if(inVRAM) {
    u32 from = (physical - VRAMBase) / VRAMSize, seen = (physical - VRAMBase) % VRAMSize;
    if((seen & 31) + size <= 32) {
      u32 offset = vramOffset(from, seen);
      for(u32 copy = 0; copy < 4; copy++) written(VRAMBase + copy * VRAMSize + vramSeen(copy, offset), size);
      return;
    }
    u32 first = seen & ~0x3fffu, last = std::min<u32>(seen + size + 0x3fff, VRAMSize) & ~0x3fffu;
    for(u32 copy = 0; copy < 4; copy++) {
      if((copy | from) & 1) written(VRAMBase + copy * VRAMSize + first, last - first);
      else written(VRAMBase + copy * VRAMSize + seen, size);
    }
    return;
  }
  written(address, size);
}

//Threaded (ge/thread.hpp): the writes the GE thread hears of from its records (Writer) are VRAM's, in any copy, and
//those reaching a watched page (a texture the render GE may decode is in one: the kernel's GE watches them as it hands
//a PRIM over).
auto Memory::watchedIn(u32 address, u32 size) const -> bool {
  u32 first, last;
  if(!watchedPages || !size || !pagesOf(address, size, first, last)) return false;
  for(u32 page = first; page <= last; page++) {
    if(watched[page]) return true;
  }
  return false;
}

auto Memory::recorded(u32 address, u32 size) const -> bool {
  u32 physical = address & 0x1fff'ffff;
  return (physical >= VRAMBase && physical - VRAMBase < VRAMWindow) || watchedIn(address, size);
}

//(threaded, the emulation thread's) writeElsewhere()'s store, which the GE thread hears of from its record: made here
//without the GE's checks, which it replays (storeChecks()), the writer told first and after (versions of the bytes, or
//a wait for drawing over them). As writeElsewhere() does: a byte at a time where they aren't side by side.
auto Memory::writeRecorded(u32 size, u32 address, u32 data) -> void {
  writer->before(address, size, Writer::Kind::Store);
  recording = true;
  u8* bytes;
  { KnownDrawn known; bytes = pointer(address, size); }
  if(bytes) {
    if(size == 1) bytes[0] = data;
    else if(size == 2) { u16 half = data; std::memcpy(bytes, &half, 2); }
    else std::memcpy(bytes, &data, 4);
    changed(address, size);
  } else if(size > 1 && reaches(address, size)) {
    for(u32 n = 0; n < size; n++) {
      u8* byte;
      { KnownDrawn known; byte = pointer(address + n, 1); }
      if(byte) *byte = data >> 8 * n, changed(address + n, 1);
      else if(unmapped) unmapped(address + n, true);
    }
  } else if(unmapped) {
    unmapped(address, true);
  }
  recording = false;
  writer->after(address, size, Writer::Kind::Store);
}

//(the emulation thread's) write()'s store to a watched page of main RAM: threaded, one the GE thread hears of from its
//record (writeRecorded()'s way); else, with a drawing memory, what that memory may still read done first (drawer).
auto Memory::writeWatched(u32 size, u32 address, u32 data) -> void {
  u32 offset = (address & 0x1fff'ffff) - RAMBase;
  if(writer) writer->before(address, size, Writer::Kind::Store), recording = true;
  else drawer();
  if(size == 1) ram[offset] = data;
  else if(size == 2) { u16 half = data; std::memcpy(&ram[offset], &half, 2); }
  else std::memcpy(&ram[offset], &data, 4);
  changed(address, size);
  if(writer) recording = false, writer->after(address, size, Writer::Kind::Store);
}

//The host's bytes behind size bytes from address, for the copies below: one piece, or up to 32 bytes at a time
//through VRAM's second and fourth copies (which rearrange 32-byte pieces). use(bytes, done, count) for each; nothing
//is used, and false returned, if any of the bytes has nothing behind it.
template<typename Use> static auto pieces(Memory& memory, u32 address, u32 size, const Use& use) -> bool {
  u32 ram = (address & 0x1fff'ffff) - Memory::RAMBase;
  if(ram < memory.ram.size()) {
    if(size > memory.ram.size() - ram) return false;
    use(&memory.ram[ram], 0, size);
    return true;
  }
  if(!memory.reaches(address, size)) return false;
  u32 physical = address & 0x1fff'ffff, offset = physical - Memory::VRAMBase;
  if(physical < Memory::VRAMBase || offset >= Memory::VRAMWindow || !(offset / Memory::VRAMSize & 1)) {
    use(memory.pointer(address, size), 0, size);
    return true;
  }
  for(u32 done = 0; done < size;) {
    u32 count = std::min(size - done, 32 - (address + done) % 32);
    use(memory.pointer(address + done, count), done, count);
    done += count;
  }
  return true;
}

//(the emulation thread's) copyIn()'s and fill()'s copy, which the GE thread hears of from its record: made here
//without the GE's checks, which it replays (copyChecks()), the writer told first and after; nothing where the bytes
//aren't all reached, as pieces() finds (nor checked there, nor recorded). (use a function of its own: pieces() for
//copyIn()'s and fill()'s own copy, called from there alone, stays inside them.)
auto Memory::copyRecorded(u32 address, u32 size, const std::function<auto (u8*, u32, u32) -> void>& use) -> bool {
  u32 ramAt = (address & 0x1fff'ffff) - RAMBase;
  if(ramAt < ram.size() ? size > ram.size() - ramAt : !reaches(address, size)) return false;
  writer->before(address, size, Writer::Kind::Copy);
  recording = true;
  { KnownDrawn known; pieces(*this, address, size, use); }
  changed(address, size);
  recording = false;
  writer->after(address, size, Writer::Kind::Copy);
  return true;
}

//(the kernel's GE, threaded: a block transfer's rows, whose checks the GE thread makes from the transfer's record) The
//copy itself, and a store of a byte, with no checks of the GE's and no record of their own; what changed() hears
//inside the transfer (recording) its own half. copyInDirect(): false where the bytes aren't all reached (nothing
//copied); storeDirect(): unmapped() told where the byte has nothing behind it.
auto Memory::copyInDirect(u32 address, const void* data, u32 size) -> bool {
  if(!size) return true;
  auto in = [&](u8* bytes, u32 done, u32 count) { std::memcpy(bytes, (const u8*)data + done, count); };
  bool copied = [&] { KnownDrawn known; return pieces(*this, address, size, in); }();
  if(copied) changed(address, size);
  return copied;
}

auto Memory::storeDirect(u32 address, u8 byte) -> void {
  u8* at = [&] { KnownDrawn known; return pointer(address, 1); }();
  if(!at) return void(unmapped ? unmapped(address, true) : void());
  *at = byte;
  changed(address, 1);
}

//The GE thread's replays of the emulation thread's writes (ge/thread.cpp): what this memory's GE side does for each
//as the access itself would have it done here, but the access, which the emulation thread made.
//storeChecks(): writeElsewhere()'s, for a store: vramStoring(), pointer()'s check, those of each byte where its bytes
//aren't side by side, and changed()'s for the GE (heard()).
auto Memory::storeChecks(u32 size, u32 address) -> void {
  u32 physical = address & 0x1fff'ffff;
  if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow && !((physical - VRAMBase) / VRAMSize & 1)) {
    u32 seen = (physical - VRAMBase) % VRAMSize;
    if(vramBusy && vramStoring && seen + size <= VRAMSize) vramStoring(seen, seen + size - 1);
  }
  u8* bytes = pointer(address, size);
  if(!bytes && size > 1 && reaches(address, size)) {
    for(u32 n = 0; n < size; n++) storeChecks(1, address + n);
    return;
  }
  if(bytes) heard(address, size);
}

//copyIn()'s and fill()'s (pieces()' checks, storing()'s drawer the emulation thread's), and copyOut()'s (out,
//nothing heard): whether the bytes were all reached.
auto Memory::copyChecks(u32 address, u32 size, bool out) -> bool {
  if(!size) return true;
  if(!pieces(*this, address, size, [](u8*, u32, u32) {})) return false;
  if(!out) heard(address, size);
  return true;
}

//changed()'s for the GE: a busy page's change heard of, watched pages unwatched (the emulation thread's own written()
//and watches its own).
auto Memory::heard(u32 address, u32 size) -> void {
  u32 physical = address & 0x1fff'ffff;
  if(vramBusy && vramChangedBusy && size && physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
    u32 seen = (physical - VRAMBase) % VRAMSize, last = std::min<u32>(seen + size, VRAMSize) - 1;
    if((physical - VRAMBase) / VRAMSize & 1) seen &= ~0x3fffu, last |= 0x3fff;
    vramChangedBusy(seen, last);
  }
  u32 first, last;
  if(!watchedPages || !pagesOf(address, size, first, last)) return;
  for(u32 page = first; page <= last; page++) {
    if(!watched[page]) continue;
    watched[page] = 0;
    watchedPages--;
    if(watchedWritten) watchedWritten(page);
    if(unwatched) unwatched(page);
  }
}

//Fills the CPU's page table (Allegrex::pages): one entry per 4 KiB of physical memory, pointing at that page in
//the host's memory, or nullptr where there's nothing. The table may list each piece of memory once, so VRAM is
//listed at its first copy only: the others go through read() and write(), whose changes are reported for all four
//copies (changed()), and code there is interpreted. Games reach VRAM through the first copy.
auto Memory::buildPages(std::vector<u8*>& table) -> void {
  table.assign(512_MiB / PageSize, nullptr);
  for(u32 offset = 0; offset < ScratchpadSize; offset += PageSize) table[(ScratchpadBase + offset) / PageSize] = &scratchpad[offset];
  for(u32 offset = 0; offset < VRAMSize; offset += PageSize) table[(VRAMBase + offset) / PageSize] = &vram[offset];
  for(u32 offset = 0; offset < ram.size(); offset += PageSize) table[(RAMBase + offset) / PageSize] = &ram[offset];
}

//The pages of buildPages()' table whose entries never change (Allegrex::steady), one byte each, nonzero for those:
//the scratchpad's and main RAM's. VRAM's come and go as the GE's workers draw there (vramGuard).
auto Memory::buildSteady(std::vector<u8>& steady) -> void {
  steady.assign(512_MiB / PageSize, 0);
  for(u32 offset = 0; offset < ScratchpadSize; offset += PageSize) steady[(ScratchpadBase + offset) / PageSize] = 1;
  for(u32 offset = 0; offset < ram.size(); offset += PageSize) steady[(RAMBase + offset) / PageSize] = 1;
}

//Copies to and from emulated memory, for the loader and the HLE functions. They do nothing and return false when
//the range isn't all inside one area (so a game passing a bad buffer gets an error, not a crash).
auto Memory::copyIn(u32 address, const void* data, u32 size) -> bool {
  if(!size) return true;
  auto in = [&](u8* bytes, u32 done, u32 count) { std::memcpy(bytes, (const u8*)data + done, count); };
  if(writer && recorded(address, size)) [[unlikely]] return copyRecorded(address, size, in);  //(threaded)
  storing(address, size);
  if(!pieces(*this, address, size, in)) return false;
  changed(address, size);
  return true;
}

auto Memory::copyOut(void* data, u32 address, u32 size) -> bool {
  if(!size) return true;
  if(reading) [[unlikely]] return copyOutReading(data, address, size);  //(threaded)
  auto out = [&](u8* bytes, u32 done, u32 count) { std::memcpy((u8*)data + done, bytes, count); };
  return pieces(*this, address, size, out);
}

//(threaded) copyOut()'s, its reads of VRAM asked about by the value rule (copyingOut).
auto Memory::copyOutReading(void* data, u32 address, u32 size) -> bool {
  bool was = copyingOut;
  copyingOut = true;
  auto out = [&](u8* bytes, u32 done, u32 count) { std::memcpy((u8*)data + done, bytes, count); };
  bool copied = pieces(*this, address, size, out);
  copyingOut = was;
  return copied;
}

auto Memory::fill(u32 address, u8 value, u32 size) -> bool {
  if(!size) return true;
  auto set = [&](u8* bytes, u32, u32 count) { std::memset(bytes, value, count); };
  if(writer && recorded(address, size)) [[unlikely]] return copyRecorded(address, size, set);  //(threaded)
  storing(address, size);
  if(!pieces(*this, address, size, set)) return false;
  changed(address, size);
  return true;
}

//A zero-terminated string a game passed (a file name, a thread's name): up to limit bytes, and only as far as
//memory goes.
auto Memory::readString(u32 address, u32 limit) -> std::string {
  std::string text;
  bool was = copyingOut;
  if(reading) copyingOut = true;  //(threaded: copyOut()'s way)
  while(text.size() < limit) {
    u8* byte = pointer(address + text.size());
    if(!byte || !*byte) break;
    text.push_back(char(*byte));
  }
  copyingOut = was;
  return text;
}

}
