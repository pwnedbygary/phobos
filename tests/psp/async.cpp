//Files' asynchronous requests (ares/psp/kernel/async.cpp): each done as it's made but its result held back for the
//time the file's device takes, then polled or waited for; the refusals while one is under way; the descriptor an
//asynchronous open or close leaves for its result; callbacks notified as requests are done, and run in CB waits;
//and a state saved while a thread waits for a request. Programs run on both engines.
#include "kernel-machine.hpp"
#include "disc-image.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results, Buffer = 0x0893'0000;
constexpr u64 Microsecond = Kernel::CPUFrequency / 1'000'000;

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

//The 64-bit result at an address, as the program reads a SceInt64.
auto result64(KernelMachine& m, u32 address) -> u64 {
  return word(m, address) | u64(word(m, address + 4)) << 32;
}

//Time goes on to a cycle, the kernel catching up with what's due by then.
auto advance(KernelMachine& m, u64 cycle) -> void {
  m.kernel.cycles = cycle;
  m.kernel.events();
}

auto text(u32 size) -> std::string {
  std::string bytes(size, 0);
  for(u32 n = 0; n < size; n++) bytes[n] = char('a' + n % 26);
  return bytes;
}
}

//Requests called directly: a read's bytes and result, held back until 100 microseconds plus its bytes at 4 MB a
//second (the memory stick) have passed; polls before and after; everything else refused meanwhile; seeks, a read
//past the end, a write refused as its result, an ioctl; an asynchronous close and a failed asynchronous open each
//leaving a descriptor for the result alone, which goes once it's taken; the disc's rate; the priority's and the
//callback's checks.
static auto asyncCalls() -> void {
  HostFolder stick;
  stick.put("DATA.BIN", text(10000));
  auto image = disc_image::makeIso({{"DISC.BIN", std::vector<u8>(8192, 7)}});
  KernelMachine m;
  m.kernel.mount("ms0", stick.path.string());
  m.kernel.disc = discFrom(image.bytes);
  u32 file = m.call("sceIoOpen", {m.string("ms0:/DATA.BIN"), 0x0001, 0});
  CHECK(m.call("sceIoPollAsync", {file, R}), Kernel::ErrorNoAsync);
  CHECK(m.call("sceIoWaitAsync", {file, R}), Kernel::ErrorNoAsync);
  CHECK(m.call("sceIoReadAsync", {file, Buffer, 4000}), 0);
  CHECK(m.system.memory.readString(Buffer, 4) == "abcd", true);  //the bytes are there as it's made
  u64 due = m.kernel.cycles + 100 * Microsecond + 4000 * Kernel::CPUFrequency / 4'000'000;
  CHECK(m.kernel.files[file].asyncDoneAt, due);
  CHECK(m.call("sceIoPollAsync", {file, R}), 1);
  CHECK(m.call("sceIoRead", {file, Buffer, 4}), Kernel::ErrorAsyncBusy);
  CHECK(m.call("sceIoWrite", {file, Buffer, 4}), Kernel::ErrorAsyncBusy);
  CHECK(m.call("sceIoLseek32", {file, 0, 0}), Kernel::ErrorAsyncBusy);
  CHECK(m.call("sceIoIoctl", {file, 0x0102'0006, 0, 0, R, 4}), Kernel::ErrorAsyncBusy);
  CHECK(m.call("sceIoClose", {file}), Kernel::ErrorAsyncBusy);
  CHECK(m.call("sceIoReadAsync", {file, Buffer, 4}), Kernel::ErrorAsyncBusy);
  advance(m, due - 1);
  CHECK(m.call("sceIoGetAsyncStat", {file, 1, R}), 1);
  advance(m, due);
  m.system.memory.write(4, R + 4, 0xcccc'cccc);
  CHECK(m.call("sceIoPollAsync", {file, R}), 0);
  CHECK(result64(m, R), 4000);
  CHECK(m.call("sceIoPollAsync", {file, R}), Kernel::ErrorNoAsync);  //taken

  auto finish = [&](u32 descriptor) {  //the request done: its result, through a poll
    advance(m, m.kernel.files[descriptor].asyncDoneAt);
    m.system.memory.write(4, R, 0x1337), m.system.memory.write(4, R + 4, 0x1337);
    CHECK(m.call("sceIoGetAsyncStat", {descriptor, 1, R}), 0);
    return result64(m, R);
  };
  CHECK(m.call("sceIoLseekAsync", {file, 0, 100, 0, 0}), 0);  //(file, unused, the offset's halves, whence)
  CHECK(finish(file), 100);
  CHECK(m.call("sceIoLseek32Async", {file, u32(-5), 1}), 0);
  CHECK(finish(file), 95);
  CHECK(m.call("sceIoReadAsync", {file, Buffer, 20000}), 0);  //past the end: what's left
  CHECK(finish(file), 9905);
  CHECK(m.call("sceIoLseek32Async", {file, u32(-1), 0}), 0);
  CHECK(finish(file), u64(s64(s32(Kernel::ErrorInvalidArgument))));
  CHECK(m.call("sceIoWriteAsync", {file, Buffer, 4}), 0);  //opened to read: refused, as the result
  CHECK(finish(file), 0xffff'ffff'8002'0323ull);
  CHECK(m.call("sceIoIoctlAsync", {file, 0x0102'0006, 0, 0, R + 8, 4}), 0);  //not the disc's
  CHECK(finish(file), u64(s64(s32(Kernel::ErrorFunctionNotSupported))));

  //a result never taken is overwritten by the next request
  CHECK(m.call("sceIoLseek32Async", {file, 7, 0}), 0);
  advance(m, m.kernel.files[file].asyncDoneAt);
  CHECK(m.call("sceIoLseek32Async", {file, 9, 0}), 0);
  CHECK(finish(file), 9);

  //closed at once, the descriptor kept until its result is taken
  CHECK(m.call("sceIoCloseAsync", {file}), 0);
  CHECK(m.kernel.files.count(file), 1);
  CHECK(m.call("sceIoRead", {file, Buffer, 4}), Kernel::ErrorAsyncBusy);  //the close under way
  CHECK(m.call("sceIoCloseAsync", {file}), Kernel::ErrorBadFile);
  advance(m, m.kernel.files[file].asyncDoneAt);
  CHECK(m.call("sceIoRead", {file, Buffer, 4}), Kernel::ErrorBadFile);   //closed, the result not taken yet
  CHECK(finish(file), 0);
  CHECK(m.kernel.files.count(file), 0);
  CHECK(m.call("sceIoPollAsync", {file, R}), Kernel::ErrorBadFile);

  //an asynchronous open: the descriptor at once, which is the result; one that fails holds only the error
  u32 opened = m.call("sceIoOpenAsync", {m.string("ms0:/DATA.BIN"), 0x0001, 0});
  CHECK(opened < 0x8000'0000, true);
  CHECK(m.call("sceIoRead", {opened, Buffer, 4}), Kernel::ErrorAsyncBusy);
  CHECK(finish(opened), opened);
  CHECK(m.call("sceIoRead", {opened, Buffer, 4}), 4);
  u32 missing = m.call("sceIoOpenAsync", {m.string("ms0:/NONE.BIN"), 0x0001, 0});
  CHECK(missing < 0x8000'0000 && m.kernel.files.count(missing), true);
  CHECK(m.call("sceIoReadAsync", {missing, Buffer, 4}), Kernel::ErrorBadFile);
  CHECK(finish(missing), 0xffff'ffff'8001'0002ull);
  CHECK(m.kernel.files.count(missing), 0);
  u32 again = m.call("sceIoOpenAsync", {m.string("ms0:/NONE.BIN"), 0x0001, 0});
  CHECK(m.call("sceIoClose", {again}), Kernel::ErrorAsyncBusy);
  advance(m, m.kernel.files[again].asyncDoneAt);
  CHECK(m.call("sceIoClose", {again}), 0);  //closed with its result never taken
  CHECK(m.kernel.files.count(again), 0);

  //the disc: 100 microseconds and 2048 bytes at 1,375,000 a second; an ioctl answered as its result
  u32 disc = m.call("sceIoOpen", {m.string("disc0:/DISC.BIN"), 0x0001, 0});
  u64 start = m.kernel.cycles;
  CHECK(m.call("sceIoReadAsync", {disc, Buffer, 2048}), 0);
  CHECK(m.kernel.files[disc].asyncDoneAt - start, 100 * Microsecond + 2048 * Kernel::CPUFrequency / 1'375'000);
  CHECK(finish(disc), 2048);
  CHECK(m.call("sceIoIoctlAsync", {disc, 0x0102'0006, 0, 0, R + 8, 4}), 0);
  CHECK(finish(disc), 0);
  CHECK(word(m, R + 8), m.kernel.files[disc].sector);

  //the priority: a user thread's, for a file or (-1) for those to come; the callback: one there is
  CHECK(m.call("sceIoChangeAsyncPriority", {u32(-1), 0x65}), 0);
  CHECK(m.call("sceIoChangeAsyncPriority", {disc, 0x08}), 0);
  CHECK(m.call("sceIoChangeAsyncPriority", {disc, 0x77}), 0);
  CHECK(m.call("sceIoChangeAsyncPriority", {disc, 0x07}), Kernel::ErrorIllegalPriority);
  CHECK(m.call("sceIoChangeAsyncPriority", {u32(-1), 0x78}), Kernel::ErrorIllegalPriority);
  CHECK(m.call("sceIoChangeAsyncPriority", {0x7777, 0x20}), Kernel::ErrorBadFile);
  CHECK(m.call("sceIoSetAsyncCallback", {disc, 0x7777, 0}), Kernel::ErrorUnknownCallback);
  CHECK(m.call("sceIoSetAsyncCallback", {0x7777, 0, 0}), Kernel::ErrorBadFile);
  CHECK(m.call("sceIoSetAsyncCallback", {disc, 0, 0}), 0);
  CHECK(m.notes.size(), 0);
}

//A program waits for its requests: the first wait blocks the main thread (a worker runs meanwhile) for exactly the
//read's 1100 microseconds; the file's callback, notified as each request is done, runs at the next CB wait, which
//runs it again as its own request is done, before the wait takes the result.
static auto asyncWaits() -> void {
  HostFolder stick;
  stick.put("DATA.BIN", text(10000));
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.kernel.mount("ms0", stick.path.string());
    Assembler callback{m, 0x0880'3000};  //logs (count, word) at R + 0x40 on, its count of words at R + 0x3c
    callback.li(t0, R); callback.put(lw(t1, 0x3c, t0)); callback.put(sll(t2, t1, 2)); callback.put(addu(t2, t2, t0));
    callback.put(sw(a0, 0x40, t2)); callback.put(sw(a1, 0x44, t2)); callback.put(addiu(t1, t1, 2));
    callback.put(sw(t1, 0x3c, t0));
    callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
    callback.print("callback\n");
    callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
    callback.li(v0, 0); callback.put(jr(ra)); callback.put(nop);
    Assembler worker{m, 0x0880'2000};
    worker.print("worker\n");
    worker.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.put(addu(s1, v0, zero));
    main.li(a0, m.string("ms0:/DATA.BIN")); main.li(a1, 1); main.li(a2, 0);
    main.call("sceIoOpen");
    main.put(addu(s0, v0, zero));
    main.put(addu(a0, s0, zero)); main.put(addu(a1, s1, zero)); main.li(a2, 0x55);
    main.call("sceIoSetAsyncCallback");
    main.li(a0, m.string("worker")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");
    main.put(addu(a0, s0, zero)); main.li(a1, Buffer); main.li(a2, 4000);
    main.call("sceIoReadAsync");
    main.call("sceKernelGetSystemTimeLow");
    main.li(t0, R); main.put(sw(v0, 0, t0));
    main.put(addu(a0, s0, zero)); main.li(a1, R + 0x10);
    main.call("sceIoWaitAsync");
    main.li(t0, R); main.put(sw(v0, 0x20, t0));
    main.call("sceKernelGetSystemTimeLow");
    main.li(t0, R); main.put(sw(v0, 4, t0));
    main.print("main woke\n");
    main.put(addu(a0, s0, zero)); main.li(a1, Buffer); main.li(a2, 4000);
    main.call("sceIoReadAsync");
    main.put(addu(a0, s0, zero)); main.li(a1, R + 0x18);
    main.call("sceIoWaitAsyncCB");
    main.li(t0, R); main.put(sw(v0, 0x24, t0));
    main.print("main woke again\n");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "worker\nmain woke\ncallback\ncallback\nmain woke again\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R + 4) - word(m, R), 1100);
    CHECK(result64(m, R + 0x10), 4000);
    CHECK(result64(m, R + 0x18), 4000);
    CHECK(word(m, R + 0x20), 0);
    CHECK(word(m, R + 0x24), 0);
    CHECK(word(m, R + 0x3c), 4);  //twice, (1, 0x55) each time
    CHECK(word(m, R + 0x40) == 1 && word(m, R + 0x44) == 0x55 && word(m, R + 0x48) == 1 && word(m, R + 0x4c) == 0x55,
          true);
    CHECK(m.notes.size(), 0);
  }
}

//The machine's state as the system saves it (memory, the CPU, the GE and the kernel), and loaded into another.
static auto save(KernelMachine& m) -> std::vector<u8> {
  serializer s;
  m.system.memory.serialize(s);
  m.system.serialize(s);
  m.system.ge.serialize(s);
  m.kernel.serialize(s);
  return {s.data(), s.data() + s.size()};
}

static auto load(KernelMachine& m, const std::vector<u8>& state) -> bool {
  serializer s{state.data(), u32(state.size())};
  m.system.memory.serialize(s);
  m.system.serialize(s);
  bool valid = m.system.ge.serialize(s);
  return m.kernel.serialize(s) && valid;
}

//A state saved while the main thread waits for a read from the disc loads into another machine, which makes the same
//state and carries on as the first does: the wait ends with the read's result at the same moment. And a state with
//the file gone (no disc in the drive) tells the waiting thread it's a bad file.
static auto asyncState() -> void {
  auto image = disc_image::makeIso({{"DISC.BIN", std::vector<u8>(65536, 3)}});
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.kernel.disc = discFrom(image.bytes);
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("disc0:/DISC.BIN")); main.li(a1, 1); main.li(a2, 0);
    main.call("sceIoOpen");
    main.put(addu(s0, v0, zero));
    main.put(addu(a0, s0, zero)); main.li(a1, Buffer); main.li(a2, 65536);
    main.call("sceIoReadAsync");
    main.put(addu(a0, s0, zero)); main.li(a1, R + 0x10);
    main.call("sceIoWaitAsync");
    main.li(t0, R); main.put(sw(v0, 0x20, t0));
    main.call("sceKernelGetSystemTimeLow");
    main.li(t0, R); main.put(sw(v0, 0x24, t0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency / 100);  //10 ms of the read's 48
    CHECK(m.kernel.exited, false);
    auto state = save(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    n.kernel.disc = discFrom(image.bytes);
    CHECK(load(n, state), true);
    CHECK(save(n) == state, true);
    for(auto* each : {&m, &n}) {
      each->kernel.run(Kernel::CPUFrequency / 10);
      CHECK(each->kernel.exited, true);
      CHECK(result64(*each, R + 0x10), 65536);
      CHECK(word(*each, R + 0x20), 0);
    }
    CHECK(word(m, R + 0x24), word(n, R + 0x24));
    CHECK(word(m, R + 0x24), (100 * Microsecond + 65536 * Kernel::CPUFrequency / 1'375'000) / Microsecond);
    KernelMachine without;  //no disc: the file's dropped, and the waiting thread is told
    without.system.recompiler.enabled = recompile;
    CHECK(load(without, state), true);
    without.kernel.run(Kernel::CPUFrequency / 10);
    CHECK(without.kernel.exited, true);
    CHECK(word(without, R + 0x20), Kernel::ErrorBadFile);
  }
}

auto asyncTests() -> Tests {
  return {{"async files called directly", asyncCalls}, {"async files waited for", asyncWaits},
          {"async files state", asyncState}};
}

}
