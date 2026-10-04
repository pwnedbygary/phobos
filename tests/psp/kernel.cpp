//The HLE kernel (ares/psp/kernel): NIDs, programs written here that use threads, semaphores and mutexes (run on
//both engines), memory partitions, the vertical blank and the clock; and with PSP_TEST_PROGRAMS (see loader.cpp),
//pspdev's hello world from start to end.
#include "kernel-machine.hpp"

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

//The user partition: the lowest place, the highest, at an address; what's left; freeing.
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
  CHECK(m.call("sceKernelAllocPartitionMemory", {2, name, 3, 0x10, 0}), Kernel::ErrorIllegalAllocationType);
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

//load() reserves the program's memory exactly where the program is, or refuses a program whose segments overlap,
//leaving nothing of it behind.
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
    bool reserved = false;
    for(auto& block : m.kernel.blocks) reserved |= block.address == 0x0880'4000;
    CHECK(reserved, true);
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
    CHECK(error.find("overlaps") != std::string::npos, true);
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
    {"kernel partitions", partitions}, {"kernel start arguments", startArguments},
    {"kernel program memory", programMemory}, {"kernel reload", reload}, {"kernel unknown functions", unknownFunctions},
    {"kernel vblank", vblank},
    {"kernel hello world", hello},
  };
}

}
