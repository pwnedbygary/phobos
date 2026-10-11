//Memory pools (ares/psp/kernel/pools.cpp): fixed pools handing out their lowest free block, variable pools pieces
//of any size with their headers, threads waiting for room in their order and woken as it comes back or as the one
//in front leaves, timeouts, deletion and cancelling ending waits, the statuses, and the refusals. Programs run on
//both engines.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results;
}

//A fixed pool of three 100-byte blocks (aligned to 4: 100 apiece): handed out lowest first; a fourth tried fails,
//waited for wakes when one comes back (getting that one); one not handed out can't be given back. Its status. A
//variable pool: pieces 8 bytes past their headers, rounded to 8, the lowest place that fits; too big refused. A
//fixed pool's blocks of 0 bytes (only a damaged state could make them) refuse a block given back.
static auto poolCalls() -> void {
  KernelMachine m;
  u32 name = m.string("pool");
  u32 fixed = m.call("sceKernelCreateFpl", {name, 2, 0, 100, 3, 0});
  CHECK(fixed < 0x8000'0000, true);
  u32 base = m.kernel.pools[fixed].address;
  for(u32 n = 0; n < 3; n++) {
    CHECK(m.call("sceKernelTryAllocateFpl", {fixed, R + n * 4}), 0);
    CHECK(m.system.memory.read(4, R + n * 4), base + n * 100);
  }
  CHECK(m.call("sceKernelTryAllocateFpl", {fixed, R + 12}), Kernel::ErrorNoMemory);
  CHECK(m.call("sceKernelFreeFpl", {fixed, base + 100}), 0);
  CHECK(m.call("sceKernelFreeFpl", {fixed, base + 100}), Kernel::ErrorIllegalMemoryBlock);
  CHECK(m.call("sceKernelFreeFpl", {fixed, base + 50}), Kernel::ErrorIllegalMemoryBlock);
  m.system.memory.write(4, R + 0x40, 56);
  CHECK(m.call("sceKernelReferFplStatus", {fixed, R + 0x40}), 0);
  CHECK(m.system.memory.readString(R + 0x44, 32) == "pool", true);
  CHECK(m.system.memory.read(4, R + 0x40 + 40), 100);  //block size
  CHECK(m.system.memory.read(4, R + 0x40 + 44), 3);    //blocks
  CHECK(m.system.memory.read(4, R + 0x40 + 48), 1);    //free
  CHECK(m.call("sceKernelTryAllocateFpl", {fixed, R + 12}), 0);
  CHECK(m.system.memory.read(4, R + 12), base + 100);  //the one given back
  //an alignment of 64 from the options: blocks of 128
  m.system.memory.write(4, R + 0x80, 8); m.system.memory.write(4, R + 0x84, 64);
  u32 aligned = m.call("sceKernelCreateFpl", {name, 2, 0, 100, 2, R + 0x80});
  CHECK(m.kernel.pools[aligned].blockSize, 128);
  //the options' alignments taken and refused as threads/fpl/create recorded: 0 (the default), and other powers of two
  //up to 4096, taken; the rest refused (Brothers in Arms: D-Day gives 0)
  for(u32 alignment : {0u, 1u, 2u, 8u, 0x1000u, 3u, 36u, 0x7fff'ffffu, 0xffff'ffffu}) {
    m.system.memory.write(4, R + 0x84, alignment);
    u32 uid = m.call("sceKernelCreateFpl", {name, 2, 0, 0x100, 0x10, R + 0x80});
    bool taken = alignment != 3 && alignment != 36 && alignment < 0x7fff'ffff;
    CHECK(uid < 0x8000'0000, taken);
    if(!taken) CHECK(uid, Kernel::ErrorIllegalArgument);
    else CHECK(m.call("sceKernelDeleteFpl", {uid}), 0);
  }
  //0 is the default's 4: blocks of 102 bytes laid 104 apart
  m.system.memory.write(4, R + 0x84, 0);
  u32 unaligned = m.call("sceKernelCreateFpl", {name, 2, 0, 102, 1, R + 0x80});
  CHECK(m.kernel.pools[unaligned].blockSize, 104);
  CHECK(m.call("sceKernelDeleteFpl", {unaligned}), 0);
  CHECK(m.call("sceKernelCreateFpl", {name, 7, 0, 100, 2, 0}), Kernel::ErrorIllegalArgument);
  CHECK(m.call("sceKernelCreateFpl", {name, 1, 0, 100, 2, 0}), Kernel::ErrorIllegalPermission);
  CHECK(m.call("sceKernelCreateFpl", {name, 2, 0x8000, 100, 2, 0}), Kernel::ErrorIllegalAttribute);
  CHECK(m.call("sceKernelCreateFpl", {name, 2, 0, 0, 2, 0}), Kernel::ErrorIllegalMemorySize);
  CHECK(m.call("sceKernelCreateFpl", {0, 2, 0, 100, 2, 0}), Kernel::ErrorError);
  CHECK(m.call("sceKernelTryAllocateFpl", {0x7777, R}), Kernel::ErrorUnknownFpl);
  CHECK(m.call("sceKernelDeleteFpl", {aligned}), 0);
  CHECK(m.call("sceKernelDeleteFpl", {aligned}), Kernel::ErrorUnknownFpl);

  u32 variable = m.call("sceKernelCreateVpl", {name, 2, 0, 0x1000, 0});
  auto& pool = m.kernel.pools[variable];
  CHECK(pool.size, 0x1000 - 0x20);
  u32 start = pool.address;
  CHECK(m.call("sceKernelTryAllocateVpl", {variable, 10, R}), 0);
  CHECK(m.system.memory.read(4, R), start + 8);          //past its header
  CHECK(m.call("sceKernelTryAllocateVpl", {variable, 16, R + 4}), 0);
  CHECK(m.system.memory.read(4, R + 4), start + 24 + 8);  //10 took 16, and 8 for the header
  CHECK(m.call("sceKernelFreeVpl", {variable, start + 8}), 0);
  CHECK(m.call("sceKernelTryAllocateVpl", {variable, 8, R + 8}), 0);
  CHECK(m.system.memory.read(4, R + 8), start + 8);      //the first place again
  CHECK(m.call("sceKernelTryAllocateVpl", {variable, 0x1000, R}), Kernel::ErrorIllegalMemorySize);
  CHECK(m.call("sceKernelTryAllocateVpl", {variable, 0, R}), Kernel::ErrorIllegalMemorySize);
  //pieces at 0 (16 bytes) and 24 (24): what's left past them, 0xfb0 with a header, fits exactly; a byte more doesn't
  CHECK(m.call("sceKernelTryAllocateVpl", {variable, 0xfa8, R + 12}), 0);
  CHECK(m.call("sceKernelFreeVpl", {variable, m.system.memory.read(4, R + 12)}), 0);
  CHECK(m.call("sceKernelTryAllocateVpl", {variable, 0xfa9, R + 12}), Kernel::ErrorNoMemory);
  CHECK(m.call("sceKernelFreeVpl", {variable, start + 12}), Kernel::ErrorIllegalMemoryBlock);
  m.system.memory.write(4, R + 0x40, 52);
  CHECK(m.call("sceKernelReferVplStatus", {variable, R + 0x40}), 0);
  CHECK(m.system.memory.read(4, R + 0x40 + 40), 0x1000 - 0x20);
  CHECK(m.system.memory.read(4, R + 0x40 + 44), 0x1000 - 0x20 - 16 - 24);  //the two pieces left
  CHECK(m.call("sceKernelTryAllocateFpl", {variable, R}), Kernel::ErrorUnknownFpl);  //not a fixed one
  u32 tiny = m.call("sceKernelCreateVpl", {name, 2, 0, 0x30, 0});  //too small: made 4 KiB
  CHECK(m.kernel.pools[tiny].size, 0x1000 - 0x20);
  //a fixed pool whose blocks are 0 bytes, as only a damaged state could make one (and loading it is refused):
  //giving a block back is refused rather than divided by 0
  m.kernel.pools[fixed].blockSize = 0;
  CHECK(m.call("sceKernelFreeFpl", {fixed, base + 100}), Kernel::ErrorIllegalMemoryBlock);
}

//Threads waiting for a fixed pool's one block: the first to come gets it when it comes back, then the next; one with
//a timeout gives up when it runs out; deleting the pool tells the last it's gone. With a CB wait, a callback runs and
//the wait goes on.
static auto poolWaits() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Pool = R + 0x10, Timeout = R + 0x14;
    //three waiters of lower priority: each allocates (they wait), then writes its result and address down
    for(u32 n = 0; n < 3; n++) {
      Assembler waiter{m, 0x0880'2000 + n * 0x100};
      waiter.li(t0, Pool); waiter.put(lw(a0, 0, t0)); waiter.li(a1, R + 0x20 + n * 8);
      waiter.li(a2, n == 2 ? Timeout : 0);
      waiter.call("sceKernelAllocateFpl");
      waiter.li(t0, R + 0x40 + n * 4); waiter.put(sw(v0, 0, t0));
      waiter.call("sceKernelExitThread");
    }
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("one")); main.li(a1, 2); main.li(a2, 0); main.li(a3, 64); main.li(t0, 1); main.li(t1, 0);
    main.call("sceKernelCreateFpl");
    main.li(t0, Pool); main.put(sw(v0, 0, t0));
    main.put(addu(a0, v0, zero)); main.li(a1, R);
    main.call("sceKernelTryAllocateFpl");  //main holds the block
    main.li(t0, Timeout); main.li(t1, 3000); main.put(sw(t1, 0, t0));
    for(u32 n = 0; n < 3; n++) {
      main.li(a0, m.string("waiter")); main.li(a1, 0x0880'2000 + n * 0x100); main.li(a2, 0x30); main.li(a3, 0x1000);
      main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
    }
    main.li(a0, 1000);
    main.call("sceKernelDelayThread");  //they all wait
    main.li(t0, Pool); main.put(lw(a0, 0, t0)); main.li(t0, R); main.put(lw(a1, 0, t0));
    main.call("sceKernelFreeFpl");  //to the first
    main.li(a0, 1000);
    main.call("sceKernelDelayThread");
    main.li(t0, Pool); main.put(lw(a0, 0, t0)); main.li(t0, R + 0x20); main.put(lw(a1, 0, t0));
    main.call("sceKernelFreeFpl");  //the first's block, to the second
    main.li(a0, 5000);
    main.call("sceKernelDelayThread");  //the third's 3 ms run out
    main.li(t0, Pool); main.put(lw(a0, 0, t0));
    main.call("sceKernelDeleteFpl");
    main.li(a0, 1000);
    main.call("sceKernelDelayThread");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    u32 block = m.system.memory.read(4, R);
    CHECK(m.system.memory.read(4, R + 0x40), 0);
    CHECK(m.system.memory.read(4, R + 0x20), block);
    CHECK(m.system.memory.read(4, R + 0x44), 0);
    CHECK(m.system.memory.read(4, R + 0x28), block);
    CHECK(m.system.memory.read(4, R + 0x48), Kernel::ErrorWaitTimeout);
    CHECK(m.system.memory.read(4, Timeout), 0);
    CHECK(m.notes.size(), 0);
  }
}

//A variable pool's waiters are served in order, one that doesn't fit holding up those behind it; so when the one in
//front leaves without being served, those behind it that fit get their room then, not at the next piece given back.
//Main holds all but 0xd8 bytes of a pool: A waits for 0x100 bytes (2 ms at most) and B behind it for 0x40, and A's
//time runs out; in a second pool, A2 waits for 0x100 and B2 behind it for 0x40, and main terminates A2. B and B2 get
//their pieces as A and A2 go (they waited till their pool was deleted).
static auto poolWaitersLeave() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Pools = R + 0x10, Timeout = R + 0x18;
    //a waiter on pool n: allocates size bytes (2 ms at most, if timed), then writes down its result, after the
    //piece's address
    auto waiter = [&](u32 entry, u32 n, u32 size, bool timed, u32 result) {
      Assembler w{m, entry};
      w.li(t0, Pools + n * 4); w.put(lw(a0, 0, t0));
      w.li(a1, size); w.li(a2, result + 4); w.li(a3, timed ? Timeout : 0);
      w.call("sceKernelAllocateVpl");
      w.li(t0, result); w.put(sw(v0, 0, t0));
      w.call("sceKernelExitThread");
    };
    waiter(0x0880'2000, 0, 0x100, true, R + 0x20);   //A
    waiter(0x0880'2100, 0, 0x40, false, R + 0x28);   //B
    waiter(0x0880'2200, 1, 0x100, false, R + 0x30);  //A2
    waiter(0x0880'2300, 1, 0x40, false, R + 0x38);   //B2
    Assembler main{m, 0x0880'1000};
    auto start = [&](u32 entry) {  //below main's priority: it runs while main waits (its ID left in s1)
      main.li(a0, m.string("waiter")); main.li(a1, entry); main.li(a2, 0x30); main.li(a3, 0x1000);
      main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(s1, v0, zero));
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
    };
    auto delay = [&](u32 microseconds) { main.li(a0, microseconds); main.call("sceKernelDelayThread"); };
    for(u32 n = 0; n < 2; n++) {  //each pool's 0xfe0 bytes but 0xd8 held by main (0xf00 and its header)
      main.li(a0, m.string("vpl")); main.li(a1, 2); main.li(a2, 0); main.li(a3, 0x1000); main.li(t0, 0);
      main.call("sceKernelCreateVpl");
      main.li(t0, Pools + n * 4); main.put(sw(v0, 0, t0));
      main.put(addu(a0, v0, zero)); main.li(a1, 0xf00); main.li(a2, R + n * 4);
      main.call("sceKernelTryAllocateVpl");
    }
    main.li(t0, Timeout); main.li(t1, 2000); main.put(sw(t1, 0, t0));
    start(0x0880'2000);
    start(0x0880'2100);
    delay(1000);  //A waits, B behind it
    delay(5000);  //A's 2 ms run out
    start(0x0880'2200);
    main.put(addu(s2, s1, zero));
    start(0x0880'2300);
    delay(1000);  //A2 waits, B2 behind it
    main.put(addu(a0, s2, zero));
    main.call("sceKernelTerminateThread");
    main.li(t0, R + 0x40); main.put(sw(v0, 0, t0));
    delay(1000);
    for(u32 n = 0; n < 2; n++) {  //whoever still waits is told its pool is gone
      main.li(t0, Pools + n * 4); main.put(lw(a0, 0, t0));
      main.call("sceKernelDeleteVpl");
    }
    delay(1000);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto result = [&](u32 offset) { return m.system.memory.read(4, R + offset); };
    CHECK(result(0x20), Kernel::ErrorWaitTimeout);
    CHECK(result(0x28), 0);
    CHECK(result(0x2c), result(0x00) + 0xf08);  //past main's piece and its header
    CHECK(result(0x38), 0);
    CHECK(result(0x3c), result(0x04) + 0xf08);
    CHECK(result(0x40), 0);
    CHECK(m.notes.size(), 0);
  }
}

constexpr u64 NoOptions = ~0ull;  //(tlsCreated()'s, out here: GCC takes no local in a default argument)

//Thread-local storage pools made (ares/psp/kernel/tls.cpp), as pspautotests' threads/tls/create, partition, memory
//and refer recorded: the refusals and their order, the memory a pool takes, 16 at most, each with its index in its
//ID's bits 3 to 6, and the status (its size word read back as 60 for any size but 0, copied as far as it says), and
//the threadman's lists of pools. Called directly.
static auto tlsCreated() -> void {
  KernelMachine m;
  u32 name = m.string("tls"), options = R + 0x200, info = R + 0x100;
  auto create = [&](u32 partition, u32 attributes, u32 size, u32 count, u64 alignment = NoOptions) {
    if(alignment != NoOptions) m.system.memory.write(4, options, 8), m.system.memory.write(4, options + 4, alignment);
    u32 given = alignment != NoOptions ? options : 0;
    return m.call("sceKernelCreateTlspl", {name, partition, attributes, size, count, given});
  };
  auto taken = [&](u32 uid) {  //the size of the block a pool holds (0: none)
    auto pool = m.kernel.tlsPools.find(uid);
    if(pool == m.kernel.tlsPools.end()) return 0u;
    for(auto& block : m.kernel.blocks) if(block.uid == pool->second.block) return block.size;
    return 0u;
  };
  auto made = [&](u32 uid) { return uid < 0x8000'0000; };
  auto drop = [&](u32 uid) { CHECK(m.call("sceKernelDeleteTlspl", {uid}), 0); };
  CHECK(m.call("sceKernelCreateTlspl", {0, 2, 0, 0x100, 4, 0}), Kernel::ErrorNoMemory);  //no name
  for(u32 partition : {u32(-1), 0u, 7u, 10u}) CHECK(create(partition, 0, 0x100, 4), Kernel::ErrorIllegalArgument);
  for(u32 partition : {1u, 3u, 4u}) CHECK(create(partition, 0, 0x100, 4), Kernel::ErrorIllegalPermission);
  for(u32 partition : {2u, 5u, 6u}) {
    u32 uid = create(partition, 0, 0x100, 4);
    CHECK(made(uid) && taken(uid) == 0x400, true);
    drop(uid);
  }
  for(u32 attributes : {0x200u, 0x2000u, 0x8000u, 0x80000u}) {
    CHECK(create(2, attributes, 0x100, 4), Kernel::ErrorIllegalAttribute);
  }
  u32 high = create(2, 0x4101, 0x100, 4);  //from the partition's top
  CHECK(made(high) && m.kernel.tlsPools[high].address + 0x400 == m.kernel.userEnd(), true);
  drop(high);
  for(auto [size, count] : {std::pair{0u, 4u}, {u32(-1), 4u}, {0x100u, 0u}, {0x100u, u32(-1)}, {0x100u, 0x100'0000u}}) {
    CHECK(create(2, 0, size, count), Kernel::ErrorIllegalMemorySize);
  }
  CHECK(create(2, 0, 0x100, 0x10'0000), Kernel::ErrorNoMemory);  //256 MiB
  //sizes: each block rounded up to its alignment (4 at least), the pool's block to 256 bytes
  struct Room { u32 size, count; u64 alignment; u32 block, stride; };
  for(auto room : {Room{1, 4, NoOptions, 0x100, 4}, {0x131, 4, NoOptions, 0x500, 0x134},
                   {0x100, 0x2f, NoOptions, 0x2f00, 0x100}, {1, 4, 0, 0x100, 4}, {1, 4, 1, 0x100, 4},
                   {1, 4, 0x100, 0x400, 0x100}, {1, 4, 0x10000, 0x40000, 0x10000}}) {
    u32 uid = create(2, 0, room.size, room.count, room.alignment);
    CHECK(made(uid) && taken(uid) == room.block, true);
    if(!made(uid)) continue;
    auto& pool = m.kernel.tlsPools[uid];
    u32 aligned = room.alignment != NoOptions ? std::max<u32>(room.alignment, 4) : 4;
    CHECK(pool.stride == room.stride && pool.blockSize == room.size && pool.address % aligned == 0, true);
    drop(uid);
  }
  for(u32 alignment : {u32(-1), 3u, 0x30u, 0x131u, 0x180'0000u}) {
    CHECK(create(2, 0, 1, 4, alignment), Kernel::ErrorIllegalArgument);
  }
  CHECK(create(2, 0, 1, 4, 0x1000'0000), Kernel::ErrorNoMemory);
  //16 pools, each with its index; the 17th refused; a pool deleted frees its index for the next
  std::vector<u32> uids;
  for(u32 n = 0; n < 16; n++) {
    uids.push_back(create(2, 0, 0x100, 4));
    CHECK(made(uids.back()) && m.kernel.tlsPools[uids.back()].index == n && (uids.back() >> 3 & 15) == n, true);
  }
  CHECK(create(2, 0, 0x100, 4), Kernel::ErrorTooManyTlspls);
  drop(uids[5]);
  u32 again = create(2, 0, 0x100, 4);
  CHECK(made(again) && m.kernel.tlsPools[again].index == 5 && again > uids[15] && (again >> 3 & 15) == 5, true);
  //the status: nothing for a size of 0; for 1, its first byte, the size word reading 60; all of it for 60
  m.system.memory.fill(info, 0, 64);
  CHECK(m.call("sceKernelReferTlsplStatus", {again, info}), 0);
  CHECK(m.system.memory.read(4, info) == 0 && m.system.memory.read(4, info + 40) == 0, true);
  m.system.memory.write(4, info, 1);
  CHECK(m.call("sceKernelReferTlsplStatus", {again, info}), 0);
  CHECK(m.system.memory.read(4, info) == 60 && m.system.memory.read(4, info + 40) == 0, true);
  CHECK(m.call("sceKernelReferTlsplStatus", {again, info}), 0);
  std::array<u32, 7> status = {60, 0, 5, 0x100, 4, 4, 0};
  for(u32 n = 0; n < 7; n++) CHECK(m.system.memory.read(4, info + (n ? 32 + n * 4 : 0)), status[n]);
  CHECK(m.system.memory.readString(info + 4, 32) == "tls", true);
  for(u32 uid : {0u, 1u, 0xdead'beefu, uids[5]}) {
    CHECK(m.call("sceKernelReferTlsplStatus", {uid, info}), Kernel::ErrorUnknownTlspl);
    CHECK(m.call("sceKernelDeleteTlspl", {uid}), Kernel::ErrorUnknownTlspl);
    CHECK(m.call("sceKernelFreeTlspl", {uid}), Kernel::ErrorUnknownTlspl);
  }
  CHECK(m.call("sceKernelGetThreadmanIdType", {again}), 14);
  CHECK(m.call("sceKernelGetThreadmanIdList", {14, R + 0x300, 32, R + 0x3f0}), 16);
  CHECK(m.system.memory.read(4, R + 0x3f0), 16);
  CHECK(roundTrip(m), true);
  CHECK(m.notes.size(), 0);
}

//Blocks handed out, as threads/tls/get, allocate, free and delete recorded: in turn from the block after the last
//handed out (0, 1, 2 and 0 again of three), zeroed as they're handed out and as they're freed, the same one again to a
//thread holding one; freeing none is no error; an ID that isn't a pool's read by its bits 3 to 6 for a block the
//thread holds; the system call's pointers, dispatching held off; an interrupt handler's calls, which have no thread to
//act for; a thread's blocks back as it ends; deleting refused while another thread holds a block. Called directly by
//threads made to be the caller in turn.
static auto tlsHandedOut() -> void {
  KernelMachine m;
  auto& k = m.kernel;
  auto caller = [&](const char* name) {
    s32 uid = k.createThread(name, 0x0880'1000, 0x20, 0x1000, 0, 0);
    k.startThread(*k.threads[uid], 0, 0);
    return uid;
  };
  u32 one = caller("one"), two = caller("two");
  auto as = [&](u32 thread) {
    if(k.current) k.current->status = Kernel::Status::Ready;
    k.current = k.threads[thread].get();
    k.current->status = Kernel::Status::Running;
  };
  as(one);
  u32 tls = m.call("sceKernelCreateTlspl", {m.string("tls"), 2, 0, 0x10, 3, 0});
  u32 base = k.tlsPools[tls].address;
  auto get = [&](u32 uid) { return m.call("sceKernelGetTlsAddr", {uid}); };
  u32 seen[4];
  for(u32 n = 0; n < 4; n++) {
    seen[n] = get(tls);
    m.system.memory.write(4, seen[n], 0xcccc'cccc);
    CHECK(get(tls), seen[n]);  //again: the same, not zeroed
    CHECK(m.system.memory.read(4, seen[n]), 0xcccc'cccc);
    CHECK(m.call("sceKernelFreeTlspl", {tls}), 0);
    CHECK(m.system.memory.read(4, seen[n]), 0);  //zeroed as it's freed
    CHECK(m.call("sceKernelFreeTlspl", {tls}), 0);  //none held: no error
  }
  CHECK(seen[0] == base && seen[1] == base + 0x10 && seen[2] == base + 0x20 && seen[3] == base, true);
  m.system.memory.write(4, base + 0x10, 0xcccc'cccc);
  CHECK(get(tls), base + 0x10);
  CHECK(m.system.memory.read(4, base + 0x10), 0);  //zeroed as it's handed out
  //an ID that isn't a pool's: its bits 3 to 6 as an index, for a block the thread holds there
  u32 index = k.tlsPools[tls].index;
  CHECK(get(index << 3), base + 0x10);
  CHECK(get(index << 3 | 7), base + 0x10);
  CHECK(get((index + 1) << 3), 0);
  CHECK(get(0xffff'ffff), 0);
  //the system call: the block held, its address put where it's asked; pointers into the kernel's memory refused,
  //and IDs that aren't pools'; with dispatching held off, refused even with the block held
  CHECK(m.call("_sceKernelAllocateTlspl", {tls, R, 0}), 0);
  CHECK(m.system.memory.read(4, R), base + 0x10);
  m.system.memory.write(4, R, 0xdead'beef);
  CHECK(m.call("_sceKernelAllocateTlspl", {tls, 0x8800'0000, 0}), Kernel::ErrorIllegalAddress);
  CHECK(m.call("_sceKernelAllocateTlspl", {tls, R, 0x8800'0000}), Kernel::ErrorIllegalAddress);
  CHECK(m.call("_sceKernelAllocateTlspl", {tls ^ 0x100, R, 0}), Kernel::ErrorUnknownTlspl);
  CHECK(m.call("_sceKernelAllocateTlspl", {0, R, 0}), Kernel::ErrorUnknownTlspl);
  k.dispatchSuspended = true;
  CHECK(m.call("_sceKernelAllocateTlspl", {tls, R, 0}), Kernel::ErrorCanNotWait);
  CHECK(get(tls), base + 0x10);  //(the library's own list answers that: no call)
  k.dispatchSuspended = false;
  CHECK(m.system.memory.read(4, R), 0xdead'beef);
  //from an interrupt handler, no thread of its own: no block got, none freed, and the running thread's block holds the
  //pool up against deletion
  k.interrupting = true;
  CHECK(get(tls), 0);
  CHECK(m.call("sceKernelFreeTlspl", {tls}), 0);
  CHECK(m.call("sceKernelDeleteTlspl", {tls}), Kernel::ErrorTlsplInUse);
  k.interrupting = false;
  CHECK(get(tls), base + 0x10);
  //another thread takes the next block; deleting is refused while it holds it, and its end gives it back
  as(two);
  CHECK(get(tls), base + 0x20);
  as(one);
  CHECK(m.call("sceKernelDeleteTlspl", {tls}), Kernel::ErrorTlsplInUse);
  m.system.memory.write(4, base + 0x20, 0xcccc'cccc);
  k.endThread(*k.threads[two], 0);
  CHECK(k.tlsHeld(k.tlsPools[tls], two), -1);
  CHECK(m.system.memory.read(4, base + 0x20), 0);
  CHECK(roundTrip(m), true);
  //the caller's own block doesn't hold the pool up
  CHECK(m.call("sceKernelDeleteTlspl", {tls}), 0);
  CHECK(get(tls), 0);
  CHECK(m.notes.size(), 0);
}

//Threads waiting for a pool's block, on both engines (threads/tls/allocate, priority and delete): main holds a pool's
//one block. A (sceKernelGetTlsAddr, priority 0x31) and B (the system call, 2 ms) wait, then C (the system call, no
//limit, 0x30): B's time runs out (WAIT_TIMEOUT, its address untouched, its time left 0); main frees the block and A
//gets it, zeroed, first come though C is better; as A ends it goes to C. Main takes it back as C ends; D
//(sceKernelGetTlsAddr) waits and is let go (NULL), E waits and the pool is deleted under it (NULL). In a pool served
//by priority, G (0x31), waiting after F (0x34), is served first, F as G ends. A state saved with A and C waiting loads
//into another machine and carries on alike.
static auto tlsWaited() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Pools = R + 0x10, Timeout = R + 0x18, Woken = R + 0x98;
    //a waiter: asks for a block of pool n (by the library, or by the system call with or without a timeout), writes
    //down its result and the address it was given, when it woke (a count of the waiters woken), the block's first
    //word, and ends
    auto waiter = [&](u32 entry, u32 n, bool library, bool timed, u32 result) {
      Assembler w{m, entry};
      w.li(t0, Pools + n * 4); w.put(lw(a0, 0, t0));
      if(library) {
        w.call("sceKernelGetTlsAddr");
        w.li(t0, result); w.put(sw(v0, 4, t0));
      } else {
        w.li(t0, result + 4); w.li(t1, 0xdead'beef); w.put(sw(t1, 0, t0));
        w.li(a1, result + 4); w.li(a2, timed ? Timeout : 0);
        w.call("_sceKernelAllocateTlspl");
        w.li(t0, result); w.put(sw(v0, 0, t0));
      }
      w.li(t4, Woken); w.put(lw(t3, 0, t4)); w.put(addiu(t3, t3, 1)); w.put(sw(t3, 0, t4));
      w.li(t0, result); w.put(sw(t3, 12, t0));
      w.put(lw(t1, 4, t0)); w.put(lw(t5, 0, t0));  //(the block's first word only for a block given)
      u32 skip = w.here();
      w.put(beq(t1, zero, 0)); w.put(nop);
      u32 skip2 = w.here();
      w.put(bne(t5, zero, 0)); w.put(nop);
      w.put(lw(t2, 0, t1)); w.put(sw(t2, 8, t0));
      m.system.memory.write(4, skip, beq(t1, zero, int32_t(w.here() - (skip + 4)) / 4));
      m.system.memory.write(4, skip2, bne(t5, zero, int32_t(w.here() - (skip2 + 4)) / 4));
      w.call("sceKernelExitThread");
    };
    waiter(0x0880'2000, 0, true, false, R + 0x20);   //A
    waiter(0x0880'2100, 0, false, true, R + 0x30);   //B
    waiter(0x0880'2200, 0, false, false, R + 0x40);  //C
    waiter(0x0880'2300, 0, true, false, R + 0x50);   //D
    waiter(0x0880'2400, 0, true, false, R + 0x60);   //E
    waiter(0x0880'2500, 1, true, false, R + 0x70);   //F, priority 0x34
    waiter(0x0880'2600, 1, true, false, R + 0x80);   //G, priority 0x31
    Assembler main{m, 0x0880'1000};
    auto start = [&](u32 entry, u32 priority) {  //below main's: it runs while main waits (its ID left in s1)
      main.li(a0, m.string("waiter")); main.li(a1, entry); main.li(a2, priority); main.li(a3, 0x1000);
      main.li(t0, 0); main.li(t1, 0);
      main.call("sceKernelCreateThread");
      main.put(addu(s1, v0, zero));
      main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
      main.call("sceKernelStartThread");
    };
    auto delay = [&](u32 microseconds) { main.li(a0, microseconds); main.call("sceKernelDelayThread"); };
    auto pool = [&](u32 n) { main.li(t0, Pools + n * 4); main.put(lw(a0, 0, t0)); };
    for(u32 n = 0; n < 2; n++) {
      main.li(a0, m.string("tls")); main.li(a1, 2); main.li(a2, n ? 0x100 : 0); main.li(a3, 0x40); main.li(t0, 1);
      main.li(t1, 0);
      main.call("sceKernelCreateTlspl");
      main.li(t0, Pools + n * 4); main.put(sw(v0, 0, t0));
      pool(n);
      main.call("sceKernelGetTlsAddr");  //main holds the block
      main.li(t0, R + n * 4); main.put(sw(v0, 0, t0));
      main.li(t1, 0x5555'5555); main.put(sw(t1, 0, v0));
    }
    main.li(t0, Timeout); main.li(t1, 2000); main.put(sw(t1, 0, t0));
    start(0x0880'2000, 0x31);
    start(0x0880'2100, 0x30);
    delay(3000);  //A and B wait; B's 2 ms run out
    start(0x0880'2200, 0x30);
    delay(1000);  //C waits too (the state is saved here)
    pool(0);
    main.call("sceKernelFreeTlspl");  //to A, then as A ends to C
    delay(1000);
    pool(0);
    main.call("sceKernelGetTlsAddr");  //C has ended: main takes the block back
    start(0x0880'2300, 0x30);
    delay(1000);
    main.put(addu(a0, s1, zero));
    main.call("sceKernelReleaseWaitThread");  //D let go
    main.li(t0, R + 0x90); main.put(sw(v0, 0, t0));
    delay(1000);
    start(0x0880'2400, 0x30);
    delay(1000);
    pool(0);
    main.call("sceKernelDeleteTlspl");  //under E
    main.li(t0, R + 0x94); main.put(sw(v0, 0, t0));
    delay(1000);
    start(0x0880'2500, 0x34);
    delay(1000);  //F waits first
    start(0x0880'2600, 0x31);
    delay(1000);
    pool(1);
    main.call("sceKernelFreeTlspl");  //to G, the better
    delay(1000);
    main.call("sceKernelExitGame");
    m.system.recompiler.enabled = recompile;
    m.system.power(0x0880'1000);
    s32 uid = m.kernel.createThread("main", 0x0880'1000, 0x20, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
    m.kernel.run(Kernel::CPUFrequency * 35 / 10'000);  //3.5 ms: A and C waiting
    u32 waiting = 0;
    for(auto& [id, thread] : m.kernel.threads) waiting += thread->wait == Kernel::Wait::Tlspl;
    CHECK(waiting, 2);
    auto state = saveState(m);
    KernelMachine fresh;
    fresh.system.recompiler.enabled = recompile;
    CHECK(loadState(fresh, state) && saveState(fresh) == state, true);
    fresh.kernel.run(Kernel::CPUFrequency / 3);
    CHECK(fresh.kernel.exited, true);
    auto at = [&](u32 offset) { return fresh.system.memory.read(4, R + offset); };
    u32 block = at(0x00);
    CHECK(at(0x30) == Kernel::ErrorWaitTimeout && at(0x34) == 0xdead'beef, true);  //B: timed out, first
    CHECK(at(0x3c) == 1 && fresh.system.memory.read(4, Timeout) == 0, true);
    CHECK(at(0x24) == block && at(0x28) == 0 && at(0x2c) == 2, true);              //A: the block, zeroed
    CHECK(at(0x40) == 0 && at(0x44) == block && at(0x48) == 0 && at(0x4c) == 3, true);  //C: after A
    CHECK(at(0x54) == 0 && at(0x5c) == 4 && at(0x90) == 0, true);                  //D: let go, NULL
    CHECK(at(0x64) == 0 && at(0x6c) == 5 && at(0x94) == 0, true);                  //E: deleted, NULL
    CHECK(at(0x84) == at(0x04) && at(0x8c) == 6, true);                            //G first
    CHECK(at(0x74) == at(0x04) && at(0x7c) == 7, true);                            //F as G ended
    CHECK(fresh.notes.size(), 0);
  }
}

auto poolTests() -> Tests {
  return {
    {"pools called directly", poolCalls}, {"pools waited for", poolWaits},
    {"pools served past waiters that left", poolWaitersLeave},
    {"tls pools created", tlsCreated}, {"tls pools handed out", tlsHandedOut}, {"tls pools waited for", tlsWaited},
  };
}

}
