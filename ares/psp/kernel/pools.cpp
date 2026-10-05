//Memory pools: a stretch of the user partition a program gets once and hands out itself through the kernel, in blocks
//all of one size (a fixed pool, FPL) or of any size (a variable pool, VPL). A thread that asks when there's no room
//waits until another gives some back (in the order they came, or by priority with attribute 0x100), its timeout
//permitting; deleting or cancelling the pool ends the waits.
//
//A fixed pool's blocks are its block size rounded up to its alignment (4 bytes, or what its options say), one after
//another; the lowest free one is handed out. A variable pool keeps its first 32 bytes for itself and 8 before each
//piece it hands out (as the PSP's do), pieces rounded up to 8 bytes, the lowest place that fits taken. How the PSP
//chooses places isn't known here (PPSSPP models its headers more closely): the free size reported may differ.

namespace {
  constexpr u32 PoolPriority = 0x100;   //waiters are served by priority, not in the order they came
  constexpr u32 PoolHighMemory = 0x4000;  //the pool comes from the top of the partition
  constexpr u32 VariableHeader = 0x20, PieceHeader = 8;
}

//(name, partition, attributes, size, then for a fixed pool how many blocks, then options): the pool's memory taken
//from the user partition. The checks, in PPSSPP's order: the partition (1-6; 2 and 6 the user's), the attributes,
//the sizes.
auto Kernel::createPool(bool variable) -> void {
  u32 partition = arg(1), attributes = arg(2), size = arg(3), count = variable ? 1 : arg(4);
  u32 options = variable ? arg(4) : arg(5);
  if(!arg(0)) return result(ErrorError);
  if(partition < 1 || partition > 6) return result(ErrorIllegalArgument);
  if(partition != 2 && partition != 6) return result(ErrorIllegalPermission);
  if(attributes & ~(variable ? 0x43ffu : 0x41ffu)) return result(ErrorIllegalAttribute);
  if(!size || !count) return result(ErrorIllegalMemorySize);
  u32 alignment = 4;
  if(!variable && options && memory.read(4, options) >= 8) alignment = memory.read(4, options + 4);
  if(!alignment || alignment & (alignment - 1) || alignment > 0x1000) return result(ErrorIllegalArgument);
  u64 blockSize = (u64(size) + alignment - 1) & ~u64(alignment - 1);
  if(variable && size <= 0x30) size = 0x1000;  //too small to hold anything: the PSP makes it 4 KiB (PPSSPP's notes)
  u64 total = variable ? (u64(size) + 7) & ~7ull : blockSize * count;
  if(total >= 0x8000'0000) return result(ErrorNoMemory);
  auto block = allocate(total, attributes & PoolHighMemory ? 1 : 0, 0, "pool: " + memory.readString(arg(0), 31));
  if(!block) return result(ErrorNoMemory);
  u32 uid = newUID();
  if(!uid) return release(block->uid), result(ErrorNoMemory);
  Pool pool;
  pool.uid = uid;
  pool.name = memory.readString(arg(0), 31);
  pool.attributes = attributes;
  pool.variable = variable;
  pool.block = block->uid;
  pool.address = block->address + (variable ? VariableHeader : 0);
  pool.size = total - (variable ? VariableHeader : 0);
  pool.blockSize = variable ? 0 : blockSize;
  pool.used.assign(variable ? 0 : count, 0);
  pools[uid] = std::move(pool);
  result(uid);
}

//Hands out room: a fixed pool's lowest free block, or a variable pool's lowest place for size bytes. Its address, or
//0 if there's no room.
auto Kernel::poolTake(Pool& pool, u32 size) -> u32 {
  if(!pool.variable) {
    for(u32 n = 0; n < pool.used.size(); n++) {
      if(!pool.used[n]) return pool.used[n] = 1, pool.address + n * pool.blockSize;
    }
    return 0;
  }
  u32 needed = ((size + 7) & ~7u) + PieceHeader, at = pool.address;
  for(auto& [start, length] : pool.pieces) {  //by address
    if(start - at >= needed) break;
    at = start + length;
  }
  if(pool.address + pool.size - at < needed) return 0;
  pool.pieces[at] = needed;
  return at + PieceHeader;
}

//The pool's free bytes (a variable pool's) or blocks (a fixed pool's).
auto Kernel::poolFree(const Pool& pool) const -> u32 {
  if(!pool.variable) return std::count(pool.used.begin(), pool.used.end(), 0);
  u32 used = 0;
  for(auto& [start, length] : pool.pieces) used += length;
  return pool.size - used;
}

//The threads waiting on the pool, in the order they're served.
auto Kernel::poolWaiters(const Pool& pool) -> std::vector<Thread*> {
  std::vector<Thread*> waiters;
  Wait wait = pool.variable ? Wait::Vpl : Wait::Fpl;
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == wait && thread->waitID == pool.uid) {
      waiters.push_back(thread.get());
    }
  }
  bool byPriority = pool.attributes & PoolPriority;
  std::sort(waiters.begin(), waiters.end(), [&](Thread* a, Thread* b) {
    if(byPriority && a->priority != b->priority) return a->priority < b->priority;
    return a->readySince < b->readySince;
  });
  return waiters;
}

//Room came back: the waiters get it in their order, while there's enough for the next (the PSP serves them in order:
//one that doesn't fit holds up those behind it).
auto Kernel::poolWake(Pool& pool) -> void {
  for(auto thread : poolWaiters(pool)) {
    u32 address = poolTake(pool, thread->waitCount);
    if(!address) return;
    memory.write(4, thread->waitPointer, address);
    ready(*thread, 0);
  }
}

//(pool, size for a variable one, where to put the address, timeout): room at once, or a wait for it.
auto Kernel::poolAllocate(bool variable, bool callbacks) -> void {
  if(!mayWait()) return;
  u32 size = variable ? arg(1) : 0, pointer = variable ? arg(2) : arg(1), timeoutPointer = variable ? arg(3) : arg(2);
  auto found = pools.find(arg(0));
  if(found == pools.end() || found->second.variable != variable) {
    return result(variable ? ErrorUnknownVpl : ErrorUnknownFpl);
  }
  auto& pool = found->second;
  if(variable && (!size || size > pool.size)) return result(ErrorIllegalMemorySize);
  if(poolWaiters(pool).empty()) {  //(those already waiting come first)
    if(u32 address = poolTake(pool, size)) {
      memory.write(4, pointer, address);
      return result(0);
    }
  }
  result(0);
  current->waitCount = size;
  current->waitPointer = pointer;
  current->readySince = ++readySequence;
  block(variable ? Wait::Vpl : Wait::Fpl, pool.uid, timeout(timeoutPointer), timeoutPointer, callbacks);
}

//(pool, size for a variable one, where to put the address): room at once, or no memory.
auto Kernel::poolTryAllocate(bool variable) -> void {
  u32 size = variable ? arg(1) : 0, pointer = variable ? arg(2) : arg(1);
  auto found = pools.find(arg(0));
  if(found == pools.end() || found->second.variable != variable) {
    return result(variable ? ErrorUnknownVpl : ErrorUnknownFpl);
  }
  auto& pool = found->second;
  if(variable && (!size || size > pool.size)) return result(ErrorIllegalMemorySize);
  u32 address = poolWaiters(pool).empty() ? poolTake(pool, size) : 0;
  if(!address) return result(ErrorNoMemory);
  memory.write(4, pointer, address);
  result(0);
}

//(pool, address): gives it back, and the waiters may have it. An address the pool didn't hand out is refused.
auto Kernel::poolRelease(bool variable) -> void {
  auto found = pools.find(arg(0));
  if(found == pools.end() || found->second.variable != variable) {
    return result(variable ? ErrorUnknownVpl : ErrorUnknownFpl);
  }
  auto& pool = found->second;
  u32 address = arg(1);
  if(!variable) {
    u32 offset = address - pool.address, n = pool.blockSize ? offset / pool.blockSize : 0;
    if(address < pool.address || offset % pool.blockSize || n >= pool.used.size() || !pool.used[n]) {
      return result(ErrorIllegalMemoryBlock);
    }
    pool.used[n] = 0;
  } else {
    auto piece = pool.pieces.find(address - PieceHeader);
    if(address < pool.address + PieceHeader || piece == pool.pieces.end()) return result(ErrorIllegalMemoryBlock);
    pool.pieces.erase(piece);
  }
  result(0);
  poolWake(pool);
  reschedule();
}

//(pool): its waiters are told it's gone, and its memory goes back to the partition.
auto Kernel::poolDelete(bool variable) -> void {
  auto found = pools.find(arg(0));
  if(found == pools.end() || found->second.variable != variable) {
    return result(variable ? ErrorUnknownVpl : ErrorUnknownFpl);
  }
  for(auto thread : poolWaiters(found->second)) ready(*thread, ErrorWaitDeleted);
  release(found->second.block);
  pools.erase(found);
  result(0);
  reschedule();
}

//(pool, where to put how many were waiting): the waiters are told the waits were cancelled.
auto Kernel::poolCancel(bool variable) -> void {
  auto found = pools.find(arg(0));
  if(found == pools.end() || found->second.variable != variable) {
    return result(variable ? ErrorUnknownVpl : ErrorUnknownFpl);
  }
  auto waiters = poolWaiters(found->second);
  for(auto thread : waiters) ready(*thread, ErrorWaitCancelled);
  if(arg(1)) memory.write(4, arg(1), waiters.size());
  result(0);
  reschedule();
}

//(pool, info): a SceKernelFplInfo or SceKernelVplInfo (pspthreadman.h) as far as the size in its first word: name,
//attributes, then the block size, block count and free blocks, or the pool's size and free bytes; how many wait.
auto Kernel::poolStatus(bool variable) -> void {
  auto found = pools.find(arg(0));
  if(found == pools.end() || found->second.variable != variable) {
    return result(variable ? ErrorUnknownVpl : ErrorUnknownFpl);
  }
  auto& pool = found->second;
  u32 info = arg(1), size = memory.read(4, info);
  for(u32 offset = 4; offset < 36 && offset < size; offset++) {
    memory.write(1, info + offset, offset - 4 < pool.name.size() ? u8(pool.name[offset - 4]) : 0);
  }
  std::vector<u32> words = {pool.attributes};
  if(variable) words.insert(words.end(), {pool.size, poolFree(pool)});
  else words.insert(words.end(), {pool.blockSize, u32(pool.used.size()), poolFree(pool)});
  words.push_back(poolWaiters(pool).size());
  for(u32 n = 0; n < words.size(); n++) if(36 + n * 4 + 4 <= size) memory.write(4, info + 36 + n * 4, words[n]);
  result(0);
}

auto Kernel::sceKernelCreateFpl() -> void { createPool(false); }
auto Kernel::sceKernelDeleteFpl() -> void { poolDelete(false); }
auto Kernel::sceKernelAllocateFpl() -> void { poolAllocate(false, false); }
auto Kernel::sceKernelAllocateFplCB() -> void { poolAllocate(false, true); }
auto Kernel::sceKernelTryAllocateFpl() -> void { poolTryAllocate(false); }
auto Kernel::sceKernelFreeFpl() -> void { poolRelease(false); }
auto Kernel::sceKernelCancelFpl() -> void { poolCancel(false); }
auto Kernel::sceKernelReferFplStatus() -> void { poolStatus(false); }
auto Kernel::sceKernelCreateVpl() -> void { createPool(true); }
auto Kernel::sceKernelDeleteVpl() -> void { poolDelete(true); }
auto Kernel::sceKernelAllocateVpl() -> void { poolAllocate(true, false); }
auto Kernel::sceKernelAllocateVplCB() -> void { poolAllocate(true, true); }
auto Kernel::sceKernelTryAllocateVpl() -> void { poolTryAllocate(true); }
auto Kernel::sceKernelFreeVpl() -> void { poolRelease(true); }
auto Kernel::sceKernelCancelVpl() -> void { poolCancel(true); }
auto Kernel::sceKernelReferVplStatus() -> void { poolStatus(true); }
