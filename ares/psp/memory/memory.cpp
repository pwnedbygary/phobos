#include "memory.hpp"

namespace ares::PlayStationPortable {

thread_local bool Memory::knownDrawn = false;

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
  if(vramBusy) finishDrawing();
  unwatchAll();
  auto clear = [](std::vector<u8>& bytes, u32 size) {
    if(bytes.size() != size) bytes.assign(size, 0);
    else std::fill(bytes.begin(), bytes.end(), 0);
  };
  clear(scratchpad, ScratchpadSize);
  clear(vram, VRAMSize);
  clear(ram, ramSize);
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
  }
  watchedPages = 0;
}

//Saving and loading memory, for save states: the scratchpad, VRAM and main RAM (whoever loads a state checks first
//that RAM is the size it was). Each goes 4 KiB at a time: a byte saying whether the piece holds anything but zeros,
//then its bytes if it does, since games leave much of their 64 MiB untouched and a state needn't carry it.
auto Memory::serialize(serializer& s) -> void {
  if(vramBusy) finishDrawing();
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
auto Memory::pointer(u32 address, u32 size) -> u8* {
  u32 physical = address & 0x1fff'ffff;
  if(physical >= RAMBase && physical - RAMBase < ram.size()) {
    u32 offset = physical - RAMBase;
    return size <= ram.size() - offset ? &ram[offset] : nullptr;
  }
  if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
    if(!knownDrawn && vramBusy) {  //(the first and third copies: the bytes; others: the 16 KiB they rearrange)
      u32 seen = (physical - VRAMBase) % VRAMSize, last = std::min<u32>(seen + size, VRAMSize) - 1;
      if((physical - VRAMBase) / VRAMSize & 1) seen &= ~0x3fffu, last |= 0x3fff;
      for(u32 page = seen / PageSize; page <= last / PageSize; page++) {
        if(!vramPageBusy(page)) continue;
        if(!vramDrawnOver || vramDrawnOver(seen, last)) finishDrawing();
        break;
      }
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

//Whether the size bytes from address all have something behind them, inside one area (one copy of VRAM): what an HLE
//function checks a game's buffer against before copying in or out. (pointer() also wants the bytes side by side in
//the host's memory, which a range through VRAM's second or fourth copy isn't.)
auto Memory::reaches(u32 address, u32 size) -> bool {
  u32 physical = address & 0x1fff'ffff;
  if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
    return size <= VRAMSize - (physical - VRAMBase) % VRAMSize;
  }
  return pointer(address, size) != nullptr;
}

//The CPU's loads: size is 1, 2 or 4 bytes, and the CPU has already checked that the address is aligned to it. HLE
//functions use them too, at whatever address a game gave, so an access can cross a 32-byte piece of VRAM's second or
//fourth copy: that one goes a byte at a time.
auto Memory::read(u32 size, u32 address) -> u32 {
  u8* bytes = pointer(address, size);
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
auto Memory::write(u32 size, u32 address, u32 data) -> void {
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
auto Memory::changed(u32 address, u32 size) -> void {
  if(vramBusy && vramChangedBusy && size) {  //(the bytes as pointer() has them)
    u32 physical = address & 0x1fff'ffff;
    if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
      u32 seen = (physical - VRAMBase) % VRAMSize, last = std::min<u32>(seen + size, VRAMSize) - 1;
      if((physical - VRAMBase) / VRAMSize & 1) seen &= ~0x3fffu, last |= 0x3fff;
      vramChangedBusy(seen, last);
    }
  }
  if(watchedPages) {
    u32 first, last;
    if(pagesOf(address, size, first, last)) {
      for(u32 page = first; page <= last; page++) {
        if(!watched[page]) continue;
        watched[page] = 0;
        watchedPages--;
        if(watchedWritten) watchedWritten(page);
      }
    }
  }
  if(!written) return;
  u32 physical = address & 0x1fff'ffff;
  if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
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

//The host's bytes behind size bytes from address, for the copies below: one piece, or up to 32 bytes at a time
//through VRAM's second and fourth copies (which rearrange 32-byte pieces). use(bytes, done, count) for each; nothing
//is used, and false returned, if any of the bytes has nothing behind it.
static auto pieces(Memory& memory, u32 address, u32 size, const std::function<void(u8*, u32, u32)>& use) -> bool {
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

//Copies to and from emulated memory, for the loader and the HLE functions. They do nothing and return false when
//the range isn't all inside one area (so a game passing a bad buffer gets an error, not a crash).
auto Memory::copyIn(u32 address, const void* data, u32 size) -> bool {
  if(!size) return true;
  auto in = [&](u8* bytes, u32 done, u32 count) { std::memcpy(bytes, (const u8*)data + done, count); };
  if(!pieces(*this, address, size, in)) return false;
  changed(address, size);
  return true;
}

auto Memory::copyOut(void* data, u32 address, u32 size) -> bool {
  if(!size) return true;
  auto out = [&](u8* bytes, u32 done, u32 count) { std::memcpy((u8*)data + done, bytes, count); };
  return pieces(*this, address, size, out);
}

auto Memory::fill(u32 address, u8 value, u32 size) -> bool {
  if(!size) return true;
  auto set = [&](u8* bytes, u32, u32 count) { std::memset(bytes, value, count); };
  if(!pieces(*this, address, size, set)) return false;
  changed(address, size);
  return true;
}

//A zero-terminated string a game passed (a file name, a thread's name): up to limit bytes, and only as far as
//memory goes.
auto Memory::readString(u32 address, u32 limit) -> std::string {
  std::string text;
  while(text.size() < limit) {
    u8* byte = pointer(address + text.size());
    if(!byte || !*byte) break;
    text.push_back(char(*byte));
  }
  return text;
}

}
