//The user partition (partition 2): games' memory, from 0x08800000 to the end of RAM, handed out in blocks aligned to
//256 bytes, as the PSP does. The program's own segments take the first blocks; thread stacks come from the top.

auto Kernel::userEnd() const -> u32 {
  return Memory::RAMBase + memory.ram.size();
}

//A block of size bytes: the lowest free place (type 0), the highest (type 1), or the lowest at or above address
//(type 2, as pspsdk documents PSP_SMEM_Addr; an address between 256-byte steps counts from the next one). nullptr
//if there's no room.
auto Kernel::allocate(u32 size, u32 type, u32 address, const std::string& name) -> Block* {
  if(!size || size > userEnd() - UserMemory) return nullptr;
  size = (size + 255) & ~255u;
  std::vector<std::pair<u32, u32>> gaps;  //free ranges, lowest first
  u32 at = UserMemory;
  for(auto& block : blocks) {
    if(block.address > at) gaps.push_back({at, block.address});
    at = std::max(at, block.address + block.size);
  }
  if(userEnd() > at) gaps.push_back({at, userEnd()});
  bool found = false;
  u32 start = 0;
  if(type == 1) {
    for(auto gap = gaps.rbegin(); gap != gaps.rend() && !found; gap++) {
      if(gap->second - gap->first < size) continue;
      start = (gap->second - size) & ~255u;
      found = start >= gap->first;
    }
  } else {
    u32 lowest = type == 2 ? (address + 255) & ~255u : UserMemory;
    for(auto [from, to] : gaps) {
      u32 candidate = std::max(from, lowest);
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

//(partition, name, type, size, address): a block's UID. Partitions 2 and 6 are both the user's here.
auto Kernel::sceKernelAllocPartitionMemory() -> void {
  u32 partition = arg(0), type = arg(2), size = arg(3), address = arg(4);
  if(partition != 2 && partition != 6) return result(ErrorIllegalPartition);
  if(type > 2) return result(ErrorIllegalAllocationType);
  auto block = allocate(size, type, address, memory.readString(arg(1), 31));
  result(block ? block->uid : ErrorAllocationFailed);
}

auto Kernel::sceKernelFreePartitionMemory() -> void {
  result(release(arg(0)) ? 0 : ErrorUnknownUID);
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
