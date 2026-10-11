//Thread-local storage pools (sceKernelCreateTlspl and its kin): a pool of blocks all of one size, of which each
//thread holds one at most, asked for by the pool's ID with sceKernelGetTlsAddr (Kernel_Library's, in user mode)
//wherever the thread needs its own copy of something; the same thread asking again gets its same block. A thread
//that asks when every block is held waits until one comes back (in the order they came, or by priority with
//attribute 0x100); blocks come back when their thread frees them (sceKernelFreeTlspl) or ends.
//
//As pspautotests' threads/tls programs recorded on a PSP:
//- create: a NULL name is NO_MEMORY; partitions 2, 5 and 6 are taken, 1, 3 and 4 refused as ILLEGAL_PERM and the rest
//  as ILLEGAL_ARGUMENT; attributes outside 0x41ff ILLEGAL_ATTR (0x200 to 0x2000, 0x8000 and up were refused, 0x1,
//  0x100 and 0x4000 taken); a block size or count of 0, or blocks needing 4 GiB or more, ILLEGAL_MEMSIZE; an
//  alignment (the options' second word) that isn't a power of two ILLEGAL_ARGUMENT, 0 the default; NO_MEMORY when the
//  partition hasn't room. Each block takes its size rounded up to its alignment, 4 bytes
//  at least, and the pool takes the blocks' memory and nothing more (0x100 bytes 4 times: 1 KiB). A program has 16 at
//  most (the 17th, 0x800201d1), each with its index among them;
//- blocks are handed out in turn, the block after the one last handed out (or the next free one after it), not the
//  lowest: with one at a time asked for and freed, 0, 1, 2 and 0 again of three; a block is zeroed as it's handed out
//  and as it's freed, and not when the thread holding it asks again;
//- freeing a block the thread doesn't hold is no error; a thread ending gives its blocks back; deleting a pool while
//  another thread holds a block is 0x800201d2 (its own it may hold), its waiters told it's gone (WAIT_DELETE);
//- the status (SceKernelTlsplInfo, 60 bytes): its size word reads back as 60 for any size asked but 0, as every
//  status's does (report() copies a status as far as its size word says);
//- _sceKernelAllocateTlspl, the call under sceKernelGetTlsAddr: refused as CAN_NOT_WAIT with dispatching or
//  interrupts held off, even with a block free or held already; ILLEGAL_ADDR for pointers into the kernel's memory;
//  its timeout as every wait's (blockTimed()); an ID that isn't a pool's, 0x800201d0;
//- sceKernelGetTlsAddr gives a block's address, or NULL. Given an ID that isn't a pool's, it answers by the ID's
//  bits 3 to 6 alone, as the index of a pool in which the thread holds a block (0 and 1 gave the first pool's, 0xf
//  the second's; 0x10 to 0x18, -1, 0xdeadbeef and a deleted pool's ID nothing): the library looks in the thread's own
//  list of blocks by that index before it calls the kernel, which refuses the ID. So a pool's own ID carries its
//  index there (tlsUID()).
//Partition 5's memory comes from the user partition here, the kernel handing out no other (chosen). From an interrupt
//handler, sceKernelGetTlsAddr gives NULL for a pool's ID and for any other (intr/waits); a handler is taken to have no
//thread of its own to hold, free or delete a block for (chosen). God Eater 2 makes its pools at boot.

namespace {
  constexpr u32 TlsPriority = 0x100;     //waiters served by priority
  constexpr u32 TlsHighMemory = 0x4000;  //the pool's memory from the top of the partition
  constexpr u32 TlsMostPools = 16;
}

//A new pool's ID, with its index in bits 3 to 6: the next ID up that has it, those passed over never handed out
//(newUID()). 0 when IDs have run out.
auto Kernel::tlsUID(u32 index) -> u32 {
  u64 uid = (nextUID & ~0x7full) | index << 3;
  if(uid < nextUID) uid += 0x80;
  if(uid > LastUID) return 0;
  nextUID = uid + 1;
  return uid;
}

auto Kernel::tlsPoolAt(u32 index) -> TlsPool* {
  for(auto& [uid, pool] : tlsPools) if(pool.index == index) return &pool;
  return nullptr;
}

//The block a thread holds in a pool, or none (-1).
auto Kernel::tlsHeld(const TlsPool& pool, u32 thread) const -> s32 {
  for(u32 n = 0; n < pool.holders.size(); n++) if(pool.holders[n] == thread) return n;
  return -1;
}

auto Kernel::tlsAddress(const TlsPool& pool, u32 block) const -> u32 {
  return pool.address + block * pool.stride;
}

//A free block handed to a thread, zeroed: the next free one from the block after the last handed out. Its address,
//or 0 if every block is held.
auto Kernel::tlsTake(TlsPool& pool, u32 thread) -> u32 {
  u32 count = pool.holders.size();
  for(u32 n = 0; n < count; n++) {
    u32 block = (pool.next + n) % count;
    if(pool.holders[block]) continue;
    pool.holders[block] = thread;
    pool.next = (block + 1) % count;
    memory.fill(tlsAddress(pool, block), 0, pool.blockSize);
    return tlsAddress(pool, block);
  }
  return 0;
}

//The threads waiting for a block of the pool, in the order they're served.
auto Kernel::tlsWaiters(const TlsPool& pool) -> std::vector<Thread*> {
  std::vector<Thread*> waiters;
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::Tlspl && thread->waitID == pool.uid) {
      waiters.push_back(thread.get());
    }
  }
  bool byPriority = pool.attributes & TlsPriority;
  std::sort(waiters.begin(), waiters.end(), [&](Thread* a, Thread* b) {
    if(byPriority && a->priority != b->priority) return a->priority < b->priority;
    return a->readySince < b->readySince;
  });
  return waiters;
}

//Blocks came back: the waiters get them in their order while there are any. sceKernelGetTlsAddr returns the block's
//address; _sceKernelAllocateTlspl puts it where it was asked to and returns 0.
auto Kernel::tlsWake(TlsPool& pool) -> void {
  for(auto thread : tlsWaiters(pool)) {
    u32 address = tlsTake(pool, thread->uid);
    if(!address) return;
    if(thread->waitMode == TlsByLibrary) {
      ready(*thread, address);
    } else {
      memory.write(4, thread->waitPointer, address);
      ready(*thread, 0);
    }
  }
}

//A block given back, zeroed, and the pool's waiters served.
auto Kernel::tlsFree(TlsPool& pool, u32 block) -> void {
  pool.holders[block] = 0;
  memory.fill(tlsAddress(pool, block), 0, pool.blockSize);
  tlsWake(pool);
}

//A thread has ended (endThread()): the blocks it held come back.
auto Kernel::tlsThreadEnded(u32 thread) -> void {
  for(auto& [uid, pool] : tlsPools) {
    if(s32 block = tlsHeld(pool, thread); block >= 0) tlsFree(pool, block);
  }
}

//The calling thread's block of a pool: the one it holds, a free one, or a wait for one (no time limit with no
//timeout pointer). With byLibrary (sceKernelGetTlsAddr) the address is the result, NULL for anything refused;
//otherwise it goes where pointer says, and the result is 0 or an error.
auto Kernel::tlsAllocate(u32 uid, u32 pointer, u32 timeoutPointer, bool byLibrary) -> void {
  auto refused = [&](u32 error) { result(byLibrary ? 0 : error); };
  if(!byLibrary && (!userAddress(pointer) || (timeoutPointer && !userAddress(timeoutPointer)))) {
    return refused(ErrorIllegalAddress);
  }
  if(interrupting) return refused(ErrorIllegalContext);
  if(dispatchSuspended || !interruptsEnabled || !current) return refused(ErrorCanNotWait);
  auto found = tlsPools.find(uid);
  if(found == tlsPools.end()) return refused(ErrorUnknownTlspl);
  auto& pool = found->second;
  u32 address = 0;
  if(s32 held = tlsHeld(pool, current->uid); held >= 0) address = tlsAddress(pool, held);
  else if(tlsWaiters(pool).empty()) address = tlsTake(pool, current->uid);  //(those already waiting come first)
  if(address) {
    if(!byLibrary) memory.write(4, pointer, address);
    return result(byLibrary ? address : 0);
  }
  result(0);
  current->waitMode = byLibrary ? TlsByLibrary : 0;
  current->waitPointer = pointer;
  current->readySince = ++readySequence;
  blockTimed(Wait::Tlspl, pool.uid, timeoutPointer, false);
}

//The thread a call acts for: the running one, or none from an interrupt handler.
auto Kernel::tlsCaller() const -> Thread* {
  return interrupting ? nullptr : current;
}

//Whether an address is in the program's memory rather than the kernel's (whose addresses have their top bit set).
auto Kernel::userAddress(u32 address) const -> bool {
  return !(address & 0x8000'0000);
}

//(name, partition, attributes, block size, count, options): a pool's ID. The checks, in the order threads/tls/create
//varies them: the name, the partition, the attributes, the sizes, the alignment, then room.
auto Kernel::sceKernelCreateTlspl() -> void {
  u32 partition = arg(1), attributes = arg(2), blockSize = arg(3), count = arg(4), options = arg(5);
  if(!arg(0)) return result(ErrorNoMemory);
  if(partition < 1 || partition > 6) return result(ErrorIllegalArgument);
  if(partition != 2 && partition != 5 && partition != 6) return result(ErrorIllegalPermission);
  if(attributes & ~0x41ffu) return result(ErrorIllegalAttribute);
  if(!blockSize || !count) return result(ErrorIllegalMemorySize);
  u32 alignment = options && memory.read(4, options) >= 8 ? memory.read(4, options + 4) : 0;
  if(alignment & (alignment - 1)) return result(ErrorIllegalArgument);
  u64 step = std::max<u64>(alignment, 4);
  u64 stride = (u64(blockSize) + step - 1) & ~(step - 1), total = stride * count;
  if(total > 0xffff'ffff) return result(ErrorIllegalMemorySize);
  u32 index = 0;
  while(index < TlsMostPools && tlsPoolAt(index)) index++;
  if(index == TlsMostPools) return result(ErrorTooManyTlspls);
  std::string name = memory.readString(arg(0), 31);
  auto block = allocate(total, attributes & TlsHighMemory ? 4 : 3, step, "tls pool: " + name);
  if(!block) return result(ErrorNoMemory);
  u32 uid = tlsUID(index);
  if(!uid) return release(block->uid), result(ErrorNoMemory);
  TlsPool pool;
  pool.uid = uid;
  pool.name = name;
  pool.attributes = attributes;
  pool.index = index;
  pool.block = block->uid;
  pool.address = block->address;
  pool.blockSize = blockSize;
  pool.stride = stride;
  pool.holders.assign(count, 0);
  tlsPools[uid] = std::move(pool);
  result(uid);
}

//(pool): its memory back to the partition, its waiters told it's gone; refused while another thread holds a block.
auto Kernel::sceKernelDeleteTlspl() -> void {
  auto found = tlsPools.find(arg(0));
  if(found == tlsPools.end()) return result(ErrorUnknownTlspl);
  auto& pool = found->second;
  Thread* caller = tlsCaller();
  for(u32 holder : pool.holders) if(holder && (!caller || holder != caller->uid)) return result(ErrorTlsplInUse);
  for(auto thread : tlsWaiters(pool)) ready(*thread, ErrorWaitDeleted);
  release(pool.block);
  tlsPools.erase(found);
  result(0);
  reschedule();
}

//(pool): the calling thread's block, as Kernel_Library's sceKernelGetTlsAddr finds or asks for it: its address, or
//NULL. An ID that isn't a pool's is read for its index alone, and gets only a block the thread holds there.
auto Kernel::sceKernelGetTlsAddr() -> void {
  u32 uid = arg(0);
  Thread* caller = tlsCaller();
  if(!tlsPools.count(uid)) {
    auto pool = tlsPoolAt(uid >> 3 & 15);
    s32 held = pool && caller ? tlsHeld(*pool, caller->uid) : -1;
    return result(held >= 0 ? tlsAddress(*pool, held) : 0);
  }
  if(caller) {
    auto& pool = tlsPools[uid];
    if(s32 held = tlsHeld(pool, caller->uid); held >= 0) return result(tlsAddress(pool, held));
  }
  tlsAllocate(uid, 0, 0, true);
}

//(pool, where to put the block's address, timeout): the system call under sceKernelGetTlsAddr.
auto Kernel::_sceKernelAllocateTlspl() -> void {
  tlsAllocate(arg(0), arg(1), arg(2), false);
}

//(pool): the calling thread's block given back, if it holds one.
auto Kernel::sceKernelFreeTlspl() -> void {
  auto found = tlsPools.find(arg(0));
  if(found == tlsPools.end()) return result(ErrorUnknownTlspl);
  result(0);
  Thread* caller = tlsCaller();
  if(!caller) return;
  if(s32 held = tlsHeld(found->second, caller->uid); held >= 0) {
    tlsFree(found->second, held);
    reschedule();
  }
}

//(pool, info): a SceKernelTlsplInfo (60 bytes: name, attributes, index, block size, count, free blocks, how many
//wait), as far as its size word says (report()).
auto Kernel::sceKernelReferTlsplStatus() -> void {
  auto found = tlsPools.find(arg(0));
  if(found == tlsPools.end()) return result(ErrorUnknownTlspl);
  auto& pool = found->second;
  Report status(60);
  status.name(4, pool.name);
  status.word(36, pool.attributes);
  status.word(40, pool.index);
  status.word(44, pool.blockSize);
  status.word(48, pool.holders.size());
  status.word(52, std::count(pool.holders.begin(), pool.holders.end(), 0));
  status.word(56, tlsWaiters(pool).size());
  report(arg(1), status);
  result(0);
}
