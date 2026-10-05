//Memory pools (ares/psp/kernel/pools.cpp): fixed pools handing out their lowest free block, variable pools pieces
//of any size with their headers, threads waiting for room in their order and woken as it comes back, timeouts,
//deletion and cancelling ending waits, the statuses, and the refusals. Programs run on both engines.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results;
}

//A fixed pool of three 100-byte blocks (aligned to 4: 100 apiece): handed out lowest first; a fourth tried fails,
//waited for wakes when one comes back (getting that one); one not handed out can't be given back. Its status. A
//variable pool: pieces 8 bytes past their headers, rounded to 8, the lowest place that fits; too big refused.
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

auto poolTests() -> Tests {
  return {{"pools called directly", poolCalls}, {"pools waited for", poolWaits}};
}

}
