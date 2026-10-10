//The HLE kernel (ares/psp/kernel): NIDs, programs written here that use threads, semaphores and mutexes (run on
//both engines), memory partitions, the vertical blank and the clock; and with PSP_TEST_PROGRAMS (see loader.cpp),
//pspdev's hello world from start to end.
#include "kernel-machine.hpp"
#include "disc-image.hpp"

#include <cstdlib>
#include <fstream>

namespace allegrex_test::psp {


//NIDs are the SHA-1 hash of the name: these are the ones pspdev's programs import.
static auto nids() -> void {
  CHECK(Kernel::nid("sceKernelExitGame"), 0x0557'2a5f);
  CHECK(Kernel::nid("sceDisplaySetFrameBuf"), 0x289d'82fe);
  CHECK(Kernel::nid("sceDisplaySetMode"), 0x0e20'f177);
  CHECK(Kernel::nid("sceKernelDelayThread"), 0xcead'eb47);
  CHECK(Kernel::nid("sceKernelCreateSema"), 0xd6da'4ba1);
  CHECK(Kernel::nid(""), 0xeea3'39da);  //SHA-1 of nothing starts da 39 a3 ee
}

//The main thread starts a worker with a higher priority, which runs at once until its delay; the main thread waits
//for its end, and gets what it returned.
static auto threads() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler worker{m, 0x0880'2000};
    worker.put(addiu(sp, sp, -16));
    worker.put(sw(ra, 12, sp));
    worker.print("worker\n");
    worker.li(a0, 1000);
    worker.call("sceKernelDelayThread");
    worker.print("worker done\n");
    worker.put(lw(ra, 12, sp));
    worker.put(addiu(sp, sp, 16));
    worker.put(addiu(v0, zero, 7));
    worker.put(jr(ra));
    worker.put(nop);

    Assembler main{m, 0x0880'1000};
    main.print("main start\n");
    main.li(a0, m.string("worker")); main.li(a1, 0x0880'2000); main.li(a2, 0x10); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(s0, v0, zero));
    main.put(addu(a0, s0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");
    main.print("main after start\n");
    main.put(addu(a0, s0, zero)); main.li(a1, 0);
    main.call("sceKernelWaitThreadEnd");
    main.li(t0, KernelMachine::Results);
    main.put(sw(v0, 0, t0));
    main.print("main end\n");
    main.call("sceKernelExitGame");

    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.output == "main start\nworker\nmain after start\nworker done\nmain end\n", true);
    CHECK(m.system.memory.read(4, KernelMachine::Results), 7);
    CHECK(m.kernel.cycles >= 1000 * 333, true);  //the delay's millisecond passed
    CHECK(m.notes.size(), 0);
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
  }
}

//A semaphore, a lightweight mutex held across threads, and sleep and wakeup:
//  main locks the mutex, then waits on the semaphore; the worker (lower priority) finds the mutex locked, and
//  signals the semaphore, so main takes over, unlocks the mutex and sleeps; the worker now gets the mutex and wakes
//  main, which leaves.
static auto waiting() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Semaphore = KernelMachine::Results + 0x10, MainID = KernelMachine::Results + 0x14;
    constexpr u32 FirstTry = KernelMachine::Results + 0x18, SecondTry = KernelMachine::Results + 0x1c;
    constexpr u32 WorkArea = KernelMachine::Results + 0x40;

    Assembler worker{m, 0x0880'3000};
    worker.li(a0, WorkArea); worker.li(a1, 1);
    worker.call("sceKernelTryLockLwMutex");
    worker.li(t0, FirstTry); worker.put(sw(v0, 0, t0));
    worker.print("worker\n");
    worker.li(t0, Semaphore); worker.put(lw(a0, 0, t0)); worker.li(a1, 1);
    worker.call("sceKernelSignalSema");
    worker.li(a0, WorkArea); worker.li(a1, 1);
    worker.call("sceKernelTryLockLwMutex");
    worker.li(t0, SecondTry); worker.put(sw(v0, 0, t0));
    worker.li(t0, MainID); worker.put(lw(a0, 0, t0));
    worker.call("sceKernelWakeupThread");
    worker.call("sceKernelExitThread");

    Assembler main{m, 0x0880'1000};
    main.call("sceKernelGetThreadId");
    main.li(t0, MainID); main.put(sw(v0, 0, t0));
    main.li(a0, m.string("s")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 1); main.li(t0, 0);
    main.call("sceKernelCreateSema");
    main.li(t0, Semaphore); main.put(sw(v0, 0, t0));
    main.li(a0, m.string("w")); main.li(a1, 0x0880'3000); main.li(a2, 0x30); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");
    main.li(a0, WorkArea); main.li(a1, m.string("m")); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelCreateLwMutex");
    main.li(a0, WorkArea); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelLockLwMutex");
    main.li(t0, Semaphore); main.put(lw(a0, 0, t0)); main.li(a1, 1); main.li(a2, 0);
    main.call("sceKernelWaitSema");
    main.print("main\n");
    main.li(a0, WorkArea); main.li(a1, 1);
    main.call("sceKernelUnlockLwMutex");
    main.call("sceKernelSleepThread");
    main.print("done\n");
    main.call("sceKernelExitGame");

    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.output == "worker\nmain\ndone\n", true);
    CHECK(m.system.memory.read(4, FirstTry), Kernel::ErrorLwMutexLocked);
    CHECK(m.system.memory.read(4, SecondTry), 0);
    CHECK(m.system.memory.read(4, WorkArea), 1);  //the worker still holds it, once
    CHECK(m.notes.size(), 0);
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
  }
}

//A semaphore's waiters are served in order, one wanting more than the count holding up those behind it; so when the
//one in front leaves without its count, those behind it that fit are served then. A semaphore with none: A waits
//for 5 (2 ms at most) and B behind it for 1, and 3 are signalled, which A can't take; A's time runs out and B takes
//1. A second semaphore: A2 waits for 5 and B2 behind it for 1, 3 are signalled, and main terminates and deletes A2:
//B2 takes 1. (They waited till their semaphore was deleted, the count left at 3.)
static auto semaphoreWaitersLeave() -> void {
  constexpr u32 R = KernelMachine::Results;
  for(bool recompile : {false, true}) {
    KernelMachine m;
    constexpr u32 Semaphores = R + 0x10, Timeout = R + 0x18, Info = R + 0x100;
    auto waiter = [&](u32 entry, u32 n, u32 count, bool timed, u32 result) {  //on semaphore n, writing its result
      Assembler w{m, entry};
      w.li(t0, Semaphores + n * 4); w.put(lw(a0, 0, t0)); w.li(a1, count); w.li(a2, timed ? Timeout : 0);
      w.call("sceKernelWaitSema");
      w.li(t0, result); w.put(sw(v0, 0, t0));
      w.call("sceKernelExitThread");
    };
    waiter(0x0880'2000, 0, 5, true, R + 0x20);   //A
    waiter(0x0880'2100, 0, 1, false, R + 0x24);  //B
    waiter(0x0880'2200, 1, 5, false, R + 0x28);  //A2
    waiter(0x0880'2300, 1, 1, false, R + 0x2c);  //B2
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
    auto signal = [&](u32 n, u32 count) {
      main.li(t0, Semaphores + n * 4); main.put(lw(a0, 0, t0)); main.li(a1, count);
      main.call("sceKernelSignalSema");
    };
    auto count = [&](u32 n, u32 offset) {  //the semaphore's count, written down
      main.li(t0, Info); main.li(t1, 56); main.put(sw(t1, 0, t0));
      main.li(t0, Semaphores + n * 4); main.put(lw(a0, 0, t0)); main.li(a1, Info);
      main.call("sceKernelReferSemaStatus");
      main.li(t0, Info); main.put(lw(t1, 44, t0)); main.li(t0, R + offset); main.put(sw(t1, 0, t0));
    };
    for(u32 n = 0; n < 2; n++) {
      main.li(a0, m.string("sema")); main.li(a1, 0); main.li(a2, 0); main.li(a3, 10); main.li(t0, 0);
      main.call("sceKernelCreateSema");
      main.li(t0, Semaphores + n * 4); main.put(sw(v0, 0, t0));
    }
    main.li(t0, Timeout); main.li(t1, 2000); main.put(sw(t1, 0, t0));
    start(0x0880'2000);
    start(0x0880'2100);
    delay(1000);  //A waits, B behind it
    signal(0, 3);
    delay(5000);  //A's 2 ms run out
    count(0, 0x30);
    start(0x0880'2200);
    main.put(addu(s2, s1, zero));
    start(0x0880'2300);
    delay(1000);  //A2 waits, B2 behind it
    signal(1, 3);
    main.put(addu(a0, s2, zero));
    main.call("sceKernelTerminateDeleteThread");
    main.li(t0, R + 0x34); main.put(sw(v0, 0, t0));
    delay(1000);
    count(1, 0x38);
    for(u32 n = 0; n < 2; n++) {  //whoever still waits is told its semaphore is gone
      main.li(t0, Semaphores + n * 4); main.put(lw(a0, 0, t0));
      main.call("sceKernelDeleteSema");
    }
    delay(1000);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto result = [&](u32 offset) { return m.system.memory.read(4, R + offset); };
    CHECK(result(0x20), Kernel::ErrorWaitTimeout);
    CHECK(result(0x24), 0);
    CHECK(result(0x30), 2);
    CHECK(result(0x2c), 0);
    CHECK(result(0x34), 0);
    CHECK(result(0x38), 2);
    CHECK(m.notes.size(), 0);
  }
}

//The user partition: the lowest place, the highest, at an address; what's left; freeing, but not a block the kernel
//holds (a thread's stack, a memory pool's), which would leave its owner in memory handed out again: the machine's
//state, which checks each owner's block, still loads.
static auto partitions() -> void {
  KernelMachine m;
  u32 name = m.string("block");
  u32 low = m.call("sceKernelAllocPartitionMemory", {2, name, 0, 0x1000, 0});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {low}), 0x0880'0000);
  u32 high = m.call("sceKernelAllocPartitionMemory", {2, name, 1, 0x1000, 0});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {high}), 0x09ff'f000);
  u32 at = m.call("sceKernelAllocPartitionMemory", {6, name, 2, 0x10, 0x0890'0000});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {at}), 0x0890'0000);
  CHECK(m.call("sceKernelMaxFreeMemSize", {}), 0x09ff'f000 - 0x0890'0100);
  CHECK(m.call("sceKernelTotalFreeMemSize", {}), 0x0180'0000 - 0x2100);
  CHECK(m.call("sceKernelAllocPartitionMemory", {3, name, 0, 0x10, 0}), Kernel::ErrorIllegalPartition);
  CHECK(m.call("sceKernelAllocPartitionMemory", {2, name, 5, 0x10, 0}), Kernel::ErrorIllegalAllocationType);
  CHECK(m.call("sceKernelAllocPartitionMemory", {2, name, 0, 0x0200'0000, 0}), Kernel::ErrorAllocationFailed);
  CHECK(m.call("sceKernelFreePartitionMemory", {low}), 0);
  CHECK(m.call("sceKernelGetBlockHeadAddr", {low}), Kernel::ErrorUnknownUID);
  CHECK(m.call("sceKernelFreePartitionMemory", {low}), Kernel::ErrorUnknownUID);
  u32 again = m.call("sceKernelAllocPartitionMemory", {2, name, 0, 0x100, 0});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {again}), 0x0880'0000);  //the freed place, used again
  //at or above an address: one already taken moves on to the next free place; one between 256-byte steps counts
  //from the next step, never below
  u32 taken = m.call("sceKernelAllocPartitionMemory", {2, name, 2, 0x10, 0x0890'0080});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {taken}), 0x0890'0100);
  u32 unaligned = m.call("sceKernelAllocPartitionMemory", {2, name, 2, 0x10, 0x08a0'0001});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {unaligned}), 0x08a0'0100);
  s32 thread = m.kernel.createThread("held", 0x0880'1000, 0x20, 0x1000, 0, 0);
  u32 stack = 0, pool = m.call("sceKernelCreateFpl", {name, 2, 0, 16, 1, 0});
  for(auto& block : m.kernel.blocks) if(block.address == m.kernel.threads[thread]->stackBlock) stack = block.uid;
  CHECK(m.call("sceKernelFreePartitionMemory", {stack}), Kernel::ErrorIllegalPermission);
  CHECK(m.call("sceKernelFreePartitionMemory", {m.kernel.pools[pool].block}), Kernel::ErrorIllegalPermission);
  CHECK(m.call("sceKernelGetBlockHeadAddr", {stack}), m.kernel.threads[thread]->stackBlock);
  serializer state;
  CHECK(m.kernel.serialize(state), true);
  KernelMachine n;
  serializer load{state.data(), state.size()};
  CHECK(n.kernel.serialize(load), true);
}

//Blocks starting on a multiple of an alignment (types 3 and 4, as Sony's SDK's heaps ask for them): the lowest such
//place past what's taken, and the highest; an alignment under 256 bytes giving the usual 256; and the refusals in
//their order: the type first, then an alignment that isn't a power of two, then the partition.
static auto alignedBlocks() -> void {
  KernelMachine m;
  u32 name = m.string("aligned");
  m.call("sceKernelAllocPartitionMemory", {2, name, 0, 0x100, 0});  //0x08800000
  u32 low = m.call("sceKernelAllocPartitionMemory", {2, name, 3, 0x2000, 0x10000});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {low}), 0x0881'0000);
  u32 high = m.call("sceKernelAllocPartitionMemory", {2, name, 4, 0x2000, 0x10000});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {high}), 0x09ff'0000);
  u32 small = m.call("sceKernelAllocPartitionMemory", {6, name, 3, 0x100, 0x10});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {small}), 0x0880'0100);
  //the 56 KiB above the high block are room enough for 16 KiB, but not on a 64 KiB step: it goes below it
  u32 between = m.call("sceKernelAllocPartitionMemory", {2, name, 4, 0x4000, 0x10000});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {between}), 0x09fe'0000);
  //from 0x09000000 to the block below the high one there's less than 16 MiB, though more is free lower down
  u32 sixteen = 0x0100'0000;
  CHECK(m.call("sceKernelAllocPartitionMemory", {2, name, 3, sixteen, sixteen}), Kernel::ErrorAllocationFailed);
  CHECK(m.call("sceKernelAllocPartitionMemory", {2, name, 3, 0x0100'0000, 0x10'0000}) < 0x8000'0000, true);
  CHECK(m.call("sceKernelAllocPartitionMemory", {2, name, 3, 0x100, 0}), Kernel::ErrorIllegalAlignmentSize);
  CHECK(m.call("sceKernelAllocPartitionMemory", {2, name, 4, 0x100, 0x300}), Kernel::ErrorIllegalAlignmentSize);
  CHECK(m.call("sceKernelAllocPartitionMemory", {9, name, 3, 0x100, 0x300}), Kernel::ErrorIllegalAlignmentSize);
  CHECK(m.call("sceKernelAllocPartitionMemory", {9, name, 5, 0x100, 0x300}), Kernel::ErrorIllegalAllocationType);
  CHECK(m.call("sceKernelAllocPartitionMemory", {9, name, 3, 0x100, 0x100}), Kernel::ErrorIllegalPartition);
}

//A PARAM.SFO holding the numbers given (the PSP Developer Wiki's layout: a header, a 16-byte entry per key, the keys'
//names, then their values).
static auto parameters(const std::vector<std::pair<std::string, u32>>& numbers) -> std::vector<u8> {
  Bytes sfo;
  std::string names;
  for(auto& [key, value] : numbers) names += key + '\0';
  while(names.size() % 4) names += '\0';
  u32 namesAt = 20 + 16 * numbers.size(), valuesAt = namesAt + names.size();
  sfo.put32(0, 0x4653'5000);
  sfo.put32(4, 0x101);
  sfo.put32(8, namesAt);
  sfo.put32(12, valuesAt);
  sfo.put32(16, numbers.size());
  u32 name = 0;
  for(u32 n = 0; n < numbers.size(); n++) {
    sfo.put16(20 + n * 16, name);
    sfo.put16(22 + n * 16, 0x0404);
    sfo.put32(24 + n * 16, 4);
    sfo.put32(28 + n * 16, 4);
    sfo.put32(32 + n * 16, n * 4);
    name += numbers[n].first.size() + 1;
    sfo.put32(valuesAt + n * 4, numbers[n].second);
  }
  for(u32 n = 0; n < names.size(); n++) sfo.put8(namesAt + n, u8(names[n]));
  return sfo.data;
}

//How much RAM a program gets on the PSP-2000/3000 emulated, with 64 MiB: a user partition of 24 MiB, 0x08800000 to
//0x0a000000 (its first thread's stack at its top), unless its PARAM.SFO asks for all of it (MEMSIZE 1): in its
//EBOOT.PBP, or for a program on the disc in the drive, on the disc. A number other than 1, or none, is no. A program
//bigger than the partition gets all of RAM too, asking or not (Monster Hunter Portable 3rd HD's 26.5 MiB from
//0x08804000, made for the PS3); one that fits it with its first thread's stack doesn't, and one bigger than RAM is
//refused.
static auto userPartition() -> void {
  ElfBuilder elf;
  elf.type = 2;
  elf.entry = 0x0880'4000;
  ElfBuilder::Segment segment;
  segment.address = 0x0880'4000;
  segment.bytes.putString(4, "SIZED");
  segment.bytes.at(0x100);
  elf.segments.push_back(segment);
  elf.sections.push_back({".rodata.sceModuleInfo", 1, 0x0880'4000, {}});
  auto program = elf.build();
  auto pbp = [&](const std::vector<u8>& sfo) {
    Bytes file;
    file.put32(0, 0x5042'5000);
    file.put32(4, 0x10000);
    u32 programAt = (40 + sfo.size() + 15) & ~15u;
    file.put32(8, 40);
    for(u32 n = 1; n < 6; n++) file.put32(8 + n * 4, 40 + sfo.size());
    file.put32(32, programAt);
    file.put32(36, programAt + program.size());
    for(u32 n = 0; n < sfo.size(); n++) file.put8(40 + n, sfo[n]);
    for(u32 n = 0; n < program.size(); n++) file.put8(programAt + n, program[n]);
    return file.data;
  };
  auto stackTop = [](KernelMachine& m) {
    for(auto& [uid, thread] : m.kernel.threads) return thread->stackBlock + thread->stackSize;
    return 0u;
  };
  struct Case { std::vector<u8> sfo; bool large; };
  for(auto& [sfo, large] : {Case{parameters({{"BOOTABLE", 1}, {"MEMSIZE", 1}}), true},
                            Case{parameters({{"MEMSIZE", 2}}), false}, Case{parameters({{"BOOTABLE", 1}}), false}}) {
    KernelMachine m{64_MiB};
    auto file = pbp(sfo);
    std::string error;
    CHECK(m.kernel.load(file.data(), file.size(), "ms0:/PSP/GAME/SIZED/EBOOT.PBP", error), true);
    CHECK(m.kernel.userEnd(), large ? 0x0c00'0000 : 0x0a00'0000);
    CHECK(stackTop(m), large ? 0x0c00'0000 : 0x0a00'0000);
    CHECK(m.call("sceKernelMaxFreeMemSize", {}), (large ? 0x0c00'0000 : 0x0a00'0000) - 256_KiB - 0x0880'4100);
  }
  for(bool large : {false, true}) {
    auto image = disc_image::makeIso({
      {"PSP_GAME/PARAM.SFO", parameters({{"MEMSIZE", large ? 1u : 0u}})}, {"PSP_GAME/SYSDIR/EBOOT.BIN", program},
    });
    KernelMachine m{64_MiB};
    m.kernel.disc = discFrom(image.bytes);
    std::string error;
    CHECK(m.kernel.load(program.data(), program.size(), "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN", error), true);
    CHECK(m.kernel.userEnd(), large ? 0x0c00'0000 : 0x0a00'0000);
    //the same program from the memory stick: the disc's PARAM.SFO isn't its own
    CHECK(m.kernel.load(program.data(), program.size(), "ms0:/EBOOT.BIN", error), true);
    CHECK(m.kernel.userEnd(), 0x0a00'0000);
  }
  for(auto [end, large] : {std::pair{0x0a28'5200u, true}, {0x09fc'0000u, false}, {0x0c00'0100u, false}}) {
    ElfBuilder big;
    big.type = 2;
    big.entry = 0x0880'4000;
    ElfBuilder::Segment code;
    code.address = 0x0880'4000;
    code.bytes.putString(4, "LARGE");
    code.memorySize = end - 0x0880'4000;  //its .bss, as the HD version's 24 MiB of it
    big.segments.push_back(code);
    big.sections.push_back({".rodata.sceModuleInfo", 1, 0x0880'4000, {}});
    auto file = big.build();
    KernelMachine m{64_MiB};
    std::string error;
    bool fits = end <= 0x0c00'0000;
    CHECK(m.kernel.load(file.data(), file.size(), "ms0:/PSP/GAME/LARGE/EBOOT.BIN", error), fits);
    if(!fits) continue;
    CHECK(m.kernel.userEnd(), large ? 0x0c00'0000 : 0x0a00'0000);
    CHECK(stackTop(m), large ? 0x0c00'0000 : 0x0a00'0000);
    auto block = std::find_if(m.kernel.blocks.begin(), m.kernel.blocks.end(),
                              [](auto& b) { return b.address == 0x0880'4000; });
    CHECK(block != m.kernel.blocks.end() && block->address + block->size == end, true);
  }
}

//The program's first thread, as a module's module_start thread is made: at the module_start the program exports for
//itself, whatever its ELF header's entry says (Dissidia 012's says 0), with the priority, stack and attributes its
//module_start_thread_parameter gives (Ghostbusters asks for a 1 KiB stack), each 0 leaving the default; without
//either, its header's entry, at 0x20 with 256 KiB. And as a module's start thread, it goes once its function returns,
//its stack free again (Death Jr. needs that room).
static auto programStart() -> void {
  struct Case { bool exported; u32 priority, stack, attributes; };
  for(auto [exported, priority, stack, attributes] : {Case{true, 0x30, 0x400, 0x8000'0000}, Case{true, 0, 0x400, 0},
                                                      Case{false, 0, 0, 0}}) {
    ElfBuilder elf;
    elf.type = 2;
    elf.entry = exported ? 0 : 0x0880'4100;
    ElfBuilder::Segment segment;
    segment.address = 0x0880'4000;
    auto& b = segment.bytes;
    b.putString(4, "START");                             //module info: attributes, version, name
    b.put32(0x24, exported ? 0x0880'4040 : 0);           //exports
    b.put32(0x28, exported ? 0x0880'4050 : 0);
    b.put32(0x40, 0);                                    //the program's own: 4 words, a function and a variable
    b.put32(0x44, 0x8000'0000);
    b.put32(0x48, 4 | 1 << 8 | 1 << 16);
    b.put32(0x4c, 0x0880'4060);
    b.put32(0x60, 0xd632'acdb); b.put32(0x64, 0x0f7c'276c);  //module_start, module_start_thread_parameter
    b.put32(0x68, 0x0880'4100); b.put32(0x6c, 0x0880'4080);
    b.put32(0x80, 3); b.put32(0x84, priority); b.put32(0x88, stack); b.put32(0x8c, attributes);
    b.put32(0x100, jr(ra)); b.put32(0x104, nop);        //the start function: returns at once
    b.at(0x200);
    elf.segments.push_back(segment);
    elf.sections.push_back({".rodata.sceModuleInfo", 1, 0x0880'4000, {}});
    auto program = elf.build();
    KernelMachine m;
    std::string error;
    CHECK(m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/START/EBOOT.PBP", error), true);
    CHECK(m.kernel.threads.size(), 1);
    for(auto& [uid, thread] : m.kernel.threads) {
      CHECK(thread->entry, 0x0880'4100);
      CHECK(thread->priority, priority ? priority : 0x20);
      CHECK(thread->stackSize, stack ? stack : 256_KiB);
      CHECK(thread->attributes, attributes ? attributes : 0x8000'4000);
      CHECK(thread->stackBlock + thread->stackSize, 0x0a00'0000);
    }
    CHECK(m.system.ipu.pc, 0x0880'4100);
    m.kernel.run(Kernel::VblankCycles);
    CHECK(m.kernel.threads.empty(), true);  //returned, and gone
    CHECK(m.call("sceKernelTotalFreeMemSize", {}), 0x0a00'0000 - 0x0880'0000 - 0x200);  //but for the program
  }
}

//A program that's a PRX goes 16 KiB into the user partition, where pspautotests' recordings of PRXs have their code
//(cpu_branch's 0x420 bytes in at 0x08804420), its block from the partition's start: a low block lands past it, as
//sysmem/partition's lowest free place had room for 1 MiB.
static auto programBase() -> void {
  ElfBuilder elf;  //a PRX, linked at 0
  elf.entry = 0x100;
  ElfBuilder::Segment segment;
  auto& b = segment.bytes;
  b.putString(4, "BASE");                        //module info: attributes, version, name
  b.put32(0x100, jr(ra)); b.put32(0x104, nop);  //its start: returns at once
  b.at(0x1000);
  elf.segments.push_back(segment);
  elf.sections.push_back({".rodata.sceModuleInfo", 1, 0, {}});
  auto program = elf.build();
  KernelMachine m;
  std::string error;
  CHECK(m.kernel.load(program.data(), program.size(), "ms0:/PSP/GAME/BASE/EBOOT.PBP", error), true);
  CHECK(m.kernel.module.segments.size() == 1 && m.kernel.module.segments[0].address == 0x0880'4000, true);
  CHECK(m.kernel.programBlockAt(), Kernel::UserMemory);
  u32 low = m.call("sceKernelAllocPartitionMemory", {2, m.string("low"), 0, 0x10, 0});
  CHECK(m.call("sceKernelGetBlockHeadAddr", {low}), 0x0880'5000);
}

//The SDK and compiler versions a program's start-up code tells the system, through the first SDK's function and
//one of its siblings for later ones (whose NIDs aren't their names' hashes), read back; and a later SDK's way of
//unloading itself, refused (CAN_NOT_STOP) to code no module holds, which runs on (modules.cpp has a module, and the
//program, calling it).
static auto sdkVersions() -> void {
  KernelMachine m;
  CHECK(m.call("sceKernelGetCompiledSdkVersion", {}), 0);
  CHECK(m.call("sceKernelSetCompiledSdkVersion", {0x0301'0110}), 0);
  CHECK(m.call("sceKernelGetCompiledSdkVersion", {}), 0x0301'0110);
  CHECK(m.call("sceKernelSetCompilerVersion", {0x30306}), 0);
  CHECK(m.kernel.compilerVersion, 0x30306);
  m.system.ipu.r[4] = 0x0307'0010;
  m.kernel.syscall(m.kernel.importCode("SysMemUserForUser", 0x3420'61e5));  //sceKernelSetCompiledSdkVersion370
  CHECK(m.system.ipu.r[2], 0);
  CHECK(m.call("sceKernelGetCompiledSdkVersion", {}), 0x0307'0010);
  CHECK(m.notes.size(), 0);

  for(bool recompile : {false, true}) {
    KernelMachine n;
    u32 stub = KernelMachine::Stubs + 0x800;  //a stub of its own, for a function known by its NID alone
    n.system.memory.write(4, stub, jr(ra));
    n.system.memory.write(4, stub + 4, syscall(n.kernel.importCode("ModuleMgrForUser", 0x8f2d'f740)));
    Assembler main{n, 0x0880'1000};
    main.print("aborting\n");
    main.li(a0, 1); main.li(a1, 0); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.put(jal(stub));
    main.put(nop);
    main.li(t0, KernelMachine::Results);
    main.put(sw(v0, 0, t0));
    main.print("refused\n");
    main.call("sceKernelExitGame");
    n.runProgram(0x0880'1000, recompile);
    CHECK(n.kernel.exited, true);
    CHECK(n.output == "aborting\nrefused\n", true);
    CHECK(n.system.memory.read(4, KernelMachine::Results), Kernel::ErrorCanNotStop);
    CHECK(n.notes.size(), 0);
  }
}

//A thread's start argument is copied to its stack: one too long for it, or not readable, is refused and the thread
//stays dormant; the longest that fits lands inside the stack.
static auto startArguments() -> void {
  KernelMachine m;
  u32 thread = m.call("sceKernelCreateThread", {m.string("t"), 0x0880'1000, 0x20, 0x1000, 0, 0});
  u32 argument = m.string(std::string(0x1000, 'a'));
  CHECK(m.call("sceKernelStartThread", {thread, 0x1000, argument}), Kernel::ErrorIllegalArgument);
  CHECK(m.call("sceKernelStartThread", {thread, 16, 0x0a00'0000}), Kernel::ErrorIllegalAddress);
  CHECK(u32(m.kernel.threads[thread]->status), u32(Kernel::Status::Dormant));
  u32 longest = 0x1000 - 0x100 - 0x40 - 15;
  CHECK(m.call("sceKernelStartThread", {thread, longest, argument}), 0);
  u32 stack = m.kernel.threads[thread]->stackBlock;  //the thread runs now: its registers are the CPU's
  CHECK(m.system.ipu.r[4], longest);
  CHECK(m.system.ipu.r[5] >= stack && m.system.ipu.r[5] + longest <= stack + 0x1000 - 0x100, true);
  CHECK(m.system.ipu.r[29] >= stack, true);
  CHECK(m.system.memory.read(1, m.system.ipu.r[5] + longest - 1), 'a');
  CHECK(u32(m.system.fpu.csr), 0x0000'0e00);  //FCSR as a PSP program finds it (measured, round 3)
}

//load() reserves the program's memory exactly where the program is, one block for all its segments (two may share a
//256-byte step, as a PRX's data starts right after its code, whatever order they're listed in, an empty one among
//them), which the program can't free, or refuses a program whose segments overlap, or whose memory can't be reserved
//where it is (outside the user partition), leaving nothing of it behind.
static auto programMemory() -> void {
  ElfBuilder elf;
  elf.type = 2;
  elf.entry = 0x0880'4000;
  ElfBuilder::Segment segment;
  segment.address = 0x0880'4000;
  segment.bytes.putString(4, "OVERLAP");  //module info at the start: a name, and no imports or exports
  segment.bytes.at(0x100);
  elf.segments.push_back(segment);
  elf.sections.push_back({".rodata.sceModuleInfo", 1, 0x0880'4000, {}});
  auto file = elf.build();
  {
    KernelMachine m;
    std::string error;
    CHECK(m.kernel.load(file.data(), file.size(), "ms0:/OVERLAP.ELF", error), true);
    u32 reserved = 0;
    for(auto& block : m.kernel.blocks) if(block.address == 0x0880'4000) reserved = block.uid;
    CHECK(reserved != 0, true);
    CHECK(m.call("sceKernelFreePartitionMemory", {reserved}), Kernel::ErrorIllegalPermission);  //not the program's
  }
  for(bool reversed : {false, true}) {
    //code 0x1238 bytes long, and data starting 8 bytes after it, in the code's last 256-byte step (as Lumines' is),
    //listed after the code or before it; and an empty segment inside the code
    auto sharing = elf;
    sharing.segments[0].bytes.at(0x1238);
    ElfBuilder::Segment data;
    data.address = 0x0880'5240;
    data.bytes.at(0x40);
    data.memorySize = 0x2000;
    sharing.segments.insert(reversed ? sharing.segments.begin() : sharing.segments.end(), data);
    ElfBuilder::Segment empty;
    empty.address = 0x0880'4080;
    sharing.segments.push_back(empty);
    auto file = sharing.build();
    KernelMachine m;
    std::string error;
    CHECK(m.kernel.load(file.data(), file.size(), "ms0:/SHARING.ELF", error), true);
    if(!error.empty()) std::printf("  %s\n", error.c_str());
    u32 covering = 0;
    for(auto& block : m.kernel.blocks) {
      if(block.address == 0x0880'4000 && block.address + block.size >= 0x0880'7240) covering++;
    }
    CHECK(covering, 1);
  }
  {
    //linked into kernel memory (which the loader can write): no block can be reserved there
    auto kernelSide = elf;
    kernelSide.entry = 0x0804'0000;
    kernelSide.segments[0].address = 0x0804'0000;
    kernelSide.sections[0].address = 0x0804'0000;
    auto file = kernelSide.build();
    KernelMachine m;
    std::string error;
    CHECK(m.kernel.load(file.data(), file.size(), "ms0:/KERNEL.ELF", error), false);
    CHECK(error == "the program's memory overlaps memory already handed out", true);
    CHECK(m.kernel.blocks.size(), 0);
  }
  {
    ElfBuilder::Segment second;  //a second segment inside the first
    second.address = 0x0880'4080;
    second.bytes.at(0x40);
    elf.segments.push_back(second);
    auto overlapping = elf.build();
    KernelMachine m;
    std::string error;
    CHECK(m.kernel.load(overlapping.data(), overlapping.size(), "ms0:/OVERLAP.ELF", error), false);
    CHECK(error == "the program's segments overlap each other", true);
    CHECK(m.kernel.blocks.size(), 0);  //the first segment's reservation went too
    CHECK(m.kernel.threads.size(), 0);
  }
}

//A program that leaves at once (it calls sceKernelExitGame through its one import): run to its end, loaded again, and
//run again, each time from a fresh start.
static auto reload() -> void {
  ElfBuilder elf;
  elf.type = 2;
  elf.entry = 0x0880'40a0;
  ElfBuilder::Segment segment;
  segment.address = 0x0880'4000;
  auto& b = segment.bytes;
  b.putString(4, "EXITER");                  //module info: no exports; imports from 0x40 to 0x54
  b.put32(44, 0x0880'4040); b.put32(48, 0x0880'4054);
  b.put32(0x40, 0x0880'4080);                //LoadExecForUser: 5 words, one function
  b.put32(0x44, 0x4009'0000);
  b.put32(0x48, 5 | 1 << 16);
  b.put32(0x4c, 0x0880'4060);
  b.put32(0x50, 0x0880'4070);
  b.put32(0x60, Kernel::nid("sceKernelExitGame"));
  b.put32(0x70, jr(ra)); b.put32(0x74, nop);
  b.putString(0x80, "LoadExecForUser");
  b.put32(0xa0, jal(0x0880'4070)); b.put32(0xa4, nop);
  b.put32(0xa8, break_);
  b.at(0x100);
  elf.segments.push_back(segment);
  elf.sections.push_back({".rodata.sceModuleInfo", 1, 0x0880'4000, {}});
  auto file = elf.build();
  KernelMachine m;
  {
    std::string error;  //a path too long to pass to the first thread: refused, with nothing left behind
    CHECK(m.kernel.load(file.data(), file.size(), "ms0:/" + std::string(5000, 'x'), error), false);
    CHECK(error == "the program's path is too long", true);
    CHECK(m.kernel.blocks.size(), 0);
  }
  //the working folder starts at the program's own, however its path is written
  for(auto [path, folder] : {std::pair{"ms0:\\PSP\\GAME\\X\\EBOOT.PBP", "ms0:/PSP/GAME/X"},
                             std::pair{"MS0:/a/./b/../EXITER.ELF", "ms0:/a"}, std::pair{"fatms0:/EXITER.ELF", "ms0:/"}}) {
    std::string error;
    CHECK(m.kernel.load(file.data(), file.size(), path, error), true);
    CHECK(m.kernel.workingDirectory == folder, true);
  }
  for(u32 run = 0; run < 2; run++) {
    std::string error;
    CHECK(m.kernel.load(file.data(), file.size(), "ms0:/EXITER.ELF", error), true);
    CHECK(m.kernel.exited, false);
    CHECK(m.kernel.threads.size(), 1);
    CHECK(m.kernel.blocks.size(), 2);  //the program, and its first thread's stack
    CHECK(m.kernel.run(10'000) > 0, true);
    CHECK(m.kernel.exited, true);
  }
  CHECK(m.notes.size(), 0);
}

//A function the kernel doesn't have is noted once and fails; a syscall no import stands for isn't answered.
static auto unknownFunctions() -> void {
  KernelMachine m;
  u32 code = m.kernel.importCode("SomeLibrary", 0x1234'5678);
  CHECK(m.kernel.importCode("OtherLibrary", 0x1234'5678), code);  //the same NID, the same code
  m.kernel.syscall(code);
  CHECK(m.system.ipu.r[2], Kernel::ErrorNotYetLinked);
  m.kernel.syscall(code);
  CHECK(m.notes.size(), 1);
  if(!m.notes.empty()) CHECK(m.notes[0] == "not implemented yet: SomeLibrary function 12345678", true);
  CHECK(m.kernel.syscall(0x0f'ffff), false);
}

//With nothing left to run, the kernel says so once, not every frame; once more when a thread has run since and
//nothing can run again; and again after a fresh start.
static auto stuckNote() -> void {
  KernelMachine m;
  auto noted = [&] { return std::count(m.notes.begin(), m.notes.end(), std::string{"no threads left to run"}); };
  m.kernel.run(Kernel::VblankCycles);
  m.kernel.run(Kernel::VblankCycles);
  CHECK(noted(), 1);
  Assembler main{m, 0x0880'1000};
  main.call("sceKernelExitDeleteThread");
  m.runProgram(0x0880'1000, false, Kernel::VblankCycles);
  m.kernel.run(Kernel::VblankCycles);
  CHECK(noted(), 2);
  m.kernel.power();  //a fresh start: the note comes again
  m.kernel.run(Kernel::VblankCycles);
  CHECK(noted(), 3);
}

//Waiting for the vertical blank: twice, and the count and the clock say so.
static auto vblank() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    Assembler main{m, 0x0880'1000};
    main.call("sceKernelGetSystemTimeLow");
    main.put(addu(s0, v0, zero));
    main.call("sceDisplayWaitVblankStart");
    main.call("sceDisplayWaitVblankStart");
    main.call("sceDisplayGetVcount");
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 0, t0));
    main.call("sceKernelGetSystemTimeLow");
    main.put(subu(v0, v0, s0));
    main.li(t0, KernelMachine::Results); main.put(sw(v0, 4, t0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.system.memory.read(4, KernelMachine::Results), 2);
    u32 elapsed = m.system.memory.read(4, KernelMachine::Results + 4);  //microseconds: two frames at 59.94 Hz
    CHECK(elapsed >= 33'000 && elapsed <= 34'000, true);
  }
}

//pspdev's hello world, as a static executable, a PRX and an EBOOT.PBP: it sets up the debug screen, draws a line
//and prints it, and leaves.
static auto hello() -> void {
  const char* folder = testPrograms();
  if(!folder) return;
  for(const char* name : {"hello.elf", "hello.prx", "EBOOT.PBP"}) {
    for(bool recompile : {false, true}) {
      std::ifstream stream(std::string(folder) + "/" + name, std::ios::binary);
      std::vector<u8> file((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
      KernelMachine m;
      m.system.recompiler.enabled = recompile;
      std::string error;
      CHECK(m.kernel.load(file.data(), file.size(), "ms0:/PSP/GAME/HELLO/EBOOT.PBP", error), true);
      if(!error.empty()) std::printf("  %s: %s\n", name, error.c_str());
      m.kernel.run(500'000'000);
      CHECK(m.kernel.exited, true);
      CHECK(m.output == "hello from a PSP program 42\n", true);
      if(m.output != "hello from a PSP program 42\n") std::printf("  %s output: [%s]\n", name, m.output.c_str());
      for(auto& note : m.notes) std::printf("  %s: %s\n", name, note.c_str());
      //the debug screen drew the line: some pixel of its first eight rows is lit
      auto& display = m.kernel.display;
      CHECK(display.bufferWidth, 512);
      CHECK(display.pixelFormat, 3);
      bool lit = false;
      for(u32 y = 0; y < 8; y++) {
        for(u32 x = 0; x < 200; x++) lit |= m.system.memory.read(4, display.frameBuffer + (y * 512 + x) * 4) != 0;
      }
      CHECK(lit, true);
    }
  }
}

auto kernelTests() -> Tests {
  return {
    {"kernel nids", nids}, {"kernel threads", threads}, {"kernel waiting", waiting},
    {"kernel semaphores served past waiters that left", semaphoreWaitersLeave},
    {"kernel partitions", partitions}, {"kernel aligned blocks", alignedBlocks},
    {"kernel user partition", userPartition}, {"kernel program start", programStart},
    {"kernel program base", programBase},
    {"kernel sdk versions", sdkVersions},
    {"kernel start arguments", startArguments},
    {"kernel program memory", programMemory}, {"kernel reload", reload}, {"kernel unknown functions", unknownFunctions},
    {"kernel vblank", vblank}, {"kernel stuck note", stuckNote},
    {"kernel hello world", hello},
  };
}

}
