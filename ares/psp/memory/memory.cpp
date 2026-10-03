#include "memory.hpp"

namespace ares::PlayStationPortable {

//Clears memory, sized for the model: 32 MiB of main RAM for the PSP-1000, 64 MiB for later models. The buffers are
//only made again when a size changes, so a page table built from them stays valid across power().
auto Memory::power(u32 ramSize) -> void {
  auto clear = [](std::vector<u8>& bytes, u32 size) {
    if(bytes.size() != size) bytes.assign(size, 0);
    else std::fill(bytes.begin(), bytes.end(), 0);
  };
  clear(scratchpad, ScratchpadSize);
  clear(vram, VRAMSize);
  clear(ram, ramSize);
}

//Where the size bytes from address are in the host's memory, or nullptr if any of them has nothing behind it (or
//they run past the end of an area).
auto Memory::pointer(u32 address, u32 size) -> u8* {
  u32 physical = address & 0x1fff'ffff;
  if(physical >= RAMBase && physical - RAMBase < ram.size()) {
    u32 offset = physical - RAMBase;
    return size <= ram.size() - offset ? &ram[offset] : nullptr;
  }
  if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
    u32 offset = (physical - VRAMBase) % VRAMSize;
    return size <= VRAMSize - offset ? &vram[offset] : nullptr;
  }
  if(physical >= ScratchpadBase && physical - ScratchpadBase < ScratchpadSize) {
    u32 offset = physical - ScratchpadBase;
    return size <= ScratchpadSize - offset ? &scratchpad[offset] : nullptr;
  }
  return nullptr;
}

//The CPU's loads: size is 1, 2 or 4 bytes, and the CPU has already checked that the address is aligned to it.
auto Memory::read(u32 size, u32 address) -> u32 {
  u8* bytes = pointer(address, size);
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

//The CPU's stores, as aligned as its loads.
auto Memory::write(u32 size, u32 address, u32 data) -> void {
  u8* bytes = pointer(address, size);
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
//through one is a change to all of them, and code compiled from any copy must go.
auto Memory::changed(u32 address, u32 size) -> void {
  if(!written) return;
  u32 physical = address & 0x1fff'ffff;
  if(physical >= VRAMBase && physical - VRAMBase < VRAMWindow) {
    u32 offset = (physical - VRAMBase) % VRAMSize;
    for(u32 copy = 0; copy < VRAMWindow; copy += VRAMSize) written(VRAMBase + copy + offset, size);
    return;
  }
  written(address, size);
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
  u8* bytes = pointer(address, size);
  if(!bytes) return false;
  std::memcpy(bytes, data, size);
  changed(address, size);
  return true;
}

auto Memory::copyOut(void* data, u32 address, u32 size) -> bool {
  if(!size) return true;
  u8* bytes = pointer(address, size);
  if(!bytes) return false;
  std::memcpy(data, bytes, size);
  return true;
}

auto Memory::fill(u32 address, u8 value, u32 size) -> bool {
  if(!size) return true;
  u8* bytes = pointer(address, size);
  if(!bytes) return false;
  std::memset(bytes, value, size);
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
