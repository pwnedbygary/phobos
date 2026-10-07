//The user partition (partition 2): games' memory, from 0x08800000, handed out in blocks aligned to 256 bytes, as the
//PSP does. The program's own segments take the first blocks; thread stacks come from the top.
//
//And what a program tells the system about itself as it starts: the SDK it was built with, and its compiler.

//Where the user partition ends. A PSP-1000 has 32 MiB of RAM, of which the user partition is the last 24
//(0x08800000 to 0x0a000000: the first 8 are the system's, the kernel's and 4 MiB it lends a game that asks). Later
//models have 64 MiB, but give a program the same 24 MiB unless it asks for more in its PARAM.SFO (MEMSIZE 1, as
//homebrew ports often do); then the partition runs to the end of RAM. Shop-bought games don't ask: they were made
//for the PSP-1000, and the system keeps the rest.
auto Kernel::userEnd() const -> u32 {
  u32 end = Memory::RAMBase + memory.ram.size();
  return largeMemory ? end : std::min<u32>(end, 0x0a00'0000);
}

//A number from a PARAM.SFO, the small table of a game's title, version and needs that comes beside its program; or
//fallback if it has no such number. Its layout (the PSP Developer Wiki's "PARAM.SFO"): a 20-byte header ("\0PSF",
//a version, where the keys' names start, where their values start, how many keys there are), then 16 bytes per
//key: where its name is among the names, what kind of value it has (0x0404: a 32-bit number), the value's length,
//the room kept for it, and where it is among the values.
static auto parameterNumber(const std::vector<u8>& sfo, const std::string& key, u32 fallback) -> u32 {
  auto word = [&](u64 at) { return u32(sfo[at] | sfo[at + 1] << 8 | sfo[at + 2] << 16 | sfo[at + 3] << 24); };
  if(sfo.size() < 20 || word(0) != 0x4653'5000) return fallback;  //"\0PSF"
  u64 names = word(8), values = word(12), count = word(16);
  for(u64 n = 0; n < count && 20 + n * 16 + 16 <= sfo.size(); n++) {
    u64 entry = 20 + n * 16;
    u64 name = names + (sfo[entry] | sfo[entry + 1] << 8);
    u32 kind = sfo[entry + 2] | sfo[entry + 3] << 8;
    u64 value = values + word(entry + 12);
    if(name + key.size() >= sfo.size() || memcmp(&sfo[name], key.c_str(), key.size() + 1)) continue;
    if(kind == 0x0404 && value + 4 <= sfo.size()) return word(value);
  }
  return fallback;
}

//The PARAM.SFO that comes with a program: the first file in its EBOOT.PBP (an EBOOT.PBP holds eight, each found by
//its offset in the header), or, for a program on the disc in the drive, the disc's PSP_GAME/PARAM.SFO. Empty if
//there's none (a program on its own, an ELF or a PRX).
auto Kernel::programParameters(const u8* data, u64 size, const std::string& path) -> std::vector<u8> {
  auto word = [&](u64 at) { return u32(data[at] | data[at + 1] << 8 | data[at + 2] << 16 | data[at + 3] << 24); };
  if(size >= 40 && word(0) == 0x5042'5000) {  //"\0PBP"
    u64 start = word(8), end = word(12);
    if(start <= end && end <= size) return {data + start, data + end};
    return {};
  }
  Disc::Entry entry;
  if(!onDisc(path) || !disc->find({"PSP_GAME", "PARAM.SFO"}, entry) || entry.folder) return {};
  std::vector<u8> sfo(std::min<u32>(entry.size, 64_KiB));
  if(!disc->read(u64(entry.sector) * Disc::SectorSize, sfo.size(), sfo.data())) return {};
  return sfo;
}

//A block of size bytes, starting on a multiple of 256 bytes: the lowest free place (type 0), the highest (type 1),
//the lowest at or above address (type 2, as pspsdk documents PSP_SMEM_Addr; an address between 256-byte steps
//counts from the next one), or the lowest (type 3) or highest (type 4) place starting on a multiple of address,
//which is then an alignment (a power of two; the caller has checked). nullptr if there's no room.
auto Kernel::allocate(u32 size, u32 type, u32 address, const std::string& name) -> Block* {
  if(!size || size > userEnd() - UserMemory) return nullptr;
  size = (size + 255) & ~255u;
  u64 alignment = type >= 3 ? std::max<u32>(address, 256) : 256;
  std::vector<std::pair<u32, u32>> gaps;  //free ranges, lowest first
  u32 at = UserMemory;
  for(auto& block : blocks) {
    if(block.address > at) gaps.push_back({at, block.address});
    at = std::max(at, block.address + block.size);
  }
  if(userEnd() > at) gaps.push_back({at, userEnd()});
  bool found = false;
  u32 start = 0;
  if(type == 1 || type == 4) {
    for(auto gap = gaps.rbegin(); gap != gaps.rend() && !found; gap++) {
      if(gap->second - gap->first < size) continue;
      start = (gap->second - size) & ~u32(alignment - 1);
      found = start >= gap->first;
    }
  } else {
    u64 lowest = type == 2 ? (u64(address) + 255) & ~255ull : UserMemory;
    for(auto [from, to] : gaps) {
      u64 candidate = (std::max<u64>(from, lowest) + alignment - 1) & ~(alignment - 1);
      if(candidate < to && to - candidate >= size) { start = candidate; found = true; break; }
    }
  }
  u32 uid = found ? newUID() : 0;
  if(!uid) return nullptr;
  auto place = std::lower_bound(blocks.begin(), blocks.end(), start, [](const Block& block, u32 address) {
    return block.address < address;
  });
  return &*blocks.insert(place, {uid, name, start, size});
}

auto Kernel::release(u32 uid) -> bool {
  for(auto block = blocks.begin(); block != blocks.end(); block++) {
    if(block->uid == uid) { blocks.erase(block); return true; }
  }
  return false;
}

auto Kernel::largestFree() const -> u32 {
  u32 at = UserMemory, largest = 0;
  for(auto& block : blocks) {
    if(block.address > at) largest = std::max(largest, block.address - at);
    at = std::max(at, block.address + block.size);
  }
  return std::max(largest, userEnd() > at ? userEnd() - at : 0);
}

//(partition, name, type, size, address or alignment): a block's UID. Partitions 2 and 6 are both the user's here.
//The type is checked first, then an aligned block's alignment, then the partition (PPSSPP's reading of the PSP's
//sysmem tests).
auto Kernel::sceKernelAllocPartitionMemory() -> void {
  u32 partition = arg(0), type = arg(2), size = arg(3), address = arg(4);
  if(type > 4) return result(ErrorIllegalAllocationType);
  if(type >= 3 && (!address || address & (address - 1))) return result(ErrorIllegalAlignmentSize);
  if(partition != 2 && partition != 6) return result(ErrorIllegalPartition);
  auto block = allocate(size, type, address, memory.readString(arg(1), 31));
  result(block ? block->uid : ErrorAllocationFailed);
}

//Where the program's block starts: start() gives the program one block, from its first segment's 256-byte step to
//the end of its last. 0 when it has no memory of its own (no segment with any bytes).
auto Kernel::programBlockAt() const -> u32 {
  u32 low = 0;
  for(auto& segment : module.segments) {
    if(segment.size && (!low || (segment.address & ~255u) < low)) low = segment.address & ~255u;
  }
  return low;
}

//Whether the kernel holds a block for something: a thread's stack, a memory pool, a message pipe's buffer, a module,
//or the program itself. The rest are blocks the program asked for.
auto Kernel::blockHeld(const Block& block) const -> bool {
  if(u32 program = programBlockAt(); program && block.address == program) return true;
  for(auto& [uid, thread] : threads) if(thread->stackBlock == block.address) return true;
  for(auto& [uid, pool] : pools) if(pool.block == block.uid) return true;
  for(auto& [uid, pipe] : pipes) if(pipe.block == block.uid) return true;
  for(auto& [uid, loaded] : modules) if(loaded.block == block.uid) return true;
  return false;
}

//(block): one the program asked for goes back to the partition. One the kernel holds isn't the program's to free
//(ILLEGAL_PERMISSION; not tried on a PSP): its owner would be left in memory handed out again.
auto Kernel::sceKernelFreePartitionMemory() -> void {
  auto block = std::find_if(blocks.begin(), blocks.end(), [&](auto& b) { return b.uid == arg(0); });
  if(block == blocks.end()) return result(ErrorUnknownUID);
  if(blockHeld(*block)) return result(ErrorIllegalPermission);
  blocks.erase(block);
  result(0);
}

auto Kernel::sceKernelGetBlockHeadAddr() -> void {
  for(auto& block : blocks) if(block.uid == arg(0)) return result(block.address);
  result(ErrorUnknownUID);
}

auto Kernel::sceKernelMaxFreeMemSize() -> void {
  result(largestFree());
}

auto Kernel::sceKernelTotalFreeMemSize() -> void {
  u32 used = 0;
  for(auto& block : blocks) used += block.size;
  result(userEnd() - UserMemory - used);
}

//(version): the SDK the program was built with, which its start-up code tells the system first thing, through this
//function or, from SDK 3.7 on, one of its siblings for a range of versions (sceKernelSetCompiledSdkVersion370 and so
//on, whose NIDs aren't their names' hashes: Sony gave later functions random ones). The PSP keeps older programs
//working as they were built to with it; nothing here depends on it yet, but it's kept to be read back.
auto Kernel::sceKernelSetCompiledSdkVersion() -> void {
  sdkVersion = arg(0);
  result(0);
}

auto Kernel::sceKernelGetCompiledSdkVersion() -> void {
  result(sdkVersion);
}

//(version): the version of the compiler that built the program (games built with Sony's SDK give numbers such as
//0x30306), as its start-up code tells it.
auto Kernel::sceKernelSetCompilerVersion() -> void {
  compilerVersion = arg(0);
  result(0);
}

//The firmware the PSP runs, as pspsdk's pspsysmem.h numbers it (0x02070110 is 2.71): 6.61, the owner's PSP's, on
//which pspautotests' threads/tls/partition read "firmware 6.06" (its major and minor bytes).
auto Kernel::sceKernelDevkitVersion() -> void {
  result(0x0606'0110);
}

//Later SDKs' memory blocks: blocks of the user partition by another name, as pspautotests' sysmem/memblock recorded
//(a block of either kind works with the other's functions). (name, type, size, options): a block, taken from the
//partition's lowest free place (type 0) or its highest (1), in 256-byte steps; returns its ID. Checked in this order
//here (the recordings don't show it): a name is needed (ERROR), the type must be 0 or 1 (ILLEGAL_MEMBLOCKTYPE),
//options given must say they're 4 bytes long (ILLEGAL_ARGUMENT); then a size of 0, or more than there's room for, is
//MEMBLOCK_ALLOC_FAILED.
auto Kernel::sceKernelAllocMemoryBlock() -> void {
  u32 name = arg(0), type = arg(1), size = arg(2), options = arg(3);
  if(!name) return result(ErrorError);
  if(type > 1) return result(ErrorIllegalAllocationType);
  if(options && memory.read(4, options) != 4) return result(ErrorIllegalArgument);
  auto block = allocate(size, type, 0, memory.readString(name, 31));
  result(block ? block->uid : ErrorAllocationFailed);
}

//(block): as sceKernelFreePartitionMemory.
auto Kernel::sceKernelFreeMemoryBlock() -> void {
  sceKernelFreePartitionMemory();
}

//(block, where to put its address): 0, the address written. A block there isn't returns 0 all the same, writing
//nothing (memblock's freed block).
auto Kernel::sceKernelGetMemoryBlockPtr() -> void {
  for(auto& block : blocks) {
    if(block.uid == arg(0) && arg(1)) memory.write(4, arg(1), block.address);
  }
  result(0);
}
