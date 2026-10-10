//Files' asynchronous requests (ares/psp/kernel/async.cpp): each done as it's made (a read's bytes landing as it's
//done) but its result held back for the time the file's device takes, then polled or waited for; a damaged image's
//reads; the refusals while one is under way; the descriptor an
//asynchronous open or close leaves for its result; callbacks notified as requests are done, and run in CB waits;
//two threads waiting on one request, and a callback taking the result its thread waits for; an ioctl's time; and a
//state saved while a thread waits for a request. Synchronous reads and writes (io.cpp) wait the same time, and are
//refused where a thread can't wait. Each group's machine, saved at its end, loads into another that makes the same
//state. Programs run on both engines.
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
//second (the memory stick) have passed, its bytes landing over what the program wrote there meanwhile; polls before
//and after; everything else refused meanwhile; seeks, a read past the end, one at the end, one where nothing is, a
//write refused as its result, an ioctl; an asynchronous close and a failed asynchronous open each leaving a
//descriptor for the result alone, which goes once it's taken; the disc's rate, and umd0:'s sectors; the priority's
//and the callback's checks.
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
  m.system.memory.fill(Buffer, 0, 4000);
  CHECK(m.call("sceIoReadAsync", {file, Buffer, 4000}), 0);
  CHECK(m.system.memory.read(4, Buffer + 4) == 0 && m.system.memory.read(4, Buffer + 3996) == 0, true);  //not yet
  m.system.memory.copyIn(Buffer, "MARK", 4);  //Dead or Alive Paradise's marker, written once the read is made
  m.system.memory.copyIn(Buffer + 3996, "MARK", 4);
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
  CHECK(m.system.memory.readString(Buffer, 4) == "MARK", true);
  advance(m, due);
  CHECK(m.system.memory.readString(Buffer, 4) == "abcd", true);  //as it's done, over the marker
  CHECK(m.system.memory.readString(Buffer + 3996, 4) == text(4000).substr(3996), true);
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
  CHECK(m.system.memory.readString(Buffer, 4) == text(99).substr(95), true);
  CHECK(m.call("sceIoReadAsync", {file, Buffer, 4}), 0);  //at the end: none, in a request's time
  CHECK(finish(file), 0);
  CHECK(m.call("sceIoReadAsync", {file, 0x10, 4}), 0);  //where nothing is: refused, as the result
  CHECK(finish(file), u64(s64(s32(Kernel::ErrorIllegalAddress))));
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
  //through umd0:, in sectors: two of them, the position moved by two as it's made, their bytes landing as it's done
  u32 umd = m.call("sceIoOpen", {m.string("umd0:"), 0x0001, 0});
  u32 sector = m.kernel.files[disc].sector;
  CHECK(m.call("sceIoLseek32", {umd, sector, 0}), sector);
  m.system.memory.fill(Buffer, 0, 4096);
  start = m.kernel.cycles;
  CHECK(m.call("sceIoReadAsync", {umd, Buffer, 2}), 0);
  CHECK(m.kernel.files[umd].asyncDoneAt - start, 100 * Microsecond + 4096 * Kernel::CPUFrequency / 1'375'000);
  CHECK(m.kernel.files[umd].position, sector + 2);
  CHECK(word(m, Buffer), 0);
  CHECK(finish(umd), 2);
  CHECK(word(m, Buffer) == 0x0707'0707 && word(m, Buffer + 4092) == 0x0707'0707, true);
  CHECK(m.call("sceIoClose", {umd}), 0);

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
  CHECK(roundTrip(m, [&](KernelMachine& n) {
    n.kernel.mount("ms0", stick.path.string());
    n.kernel.disc = discFrom(image.bytes);
  }), true);
}

//A disc image that can't be read where a read goes (a damaged one): a read is an I/O error, the file's position left
//where it was; an asynchronous one is made, its position moved, and done as an I/O error, nothing landing and its
//position back where the read began. The image readable there again, the same read gets the bytes.
static auto asyncDamaged() -> void {
  auto image = disc_image::makeIso({{"DISC.BIN", std::vector<u8>(8192, 7)}});
  auto bytes = std::make_shared<std::vector<u8>>(image.bytes);
  auto unreadable = std::make_shared<std::pair<u64, u64>>(0, 0);  //where in the image, from and to
  auto disc = std::make_shared<ares::PlayStationPortable::Disc>();
  std::string error;
  disc->open([bytes, unreadable](u64 offset, void* out, u64 size) -> u64 {
    if(offset < unreadable->second && offset + size > unreadable->first) return 0;
    size = offset < bytes->size() ? std::min<u64>(size, bytes->size() - offset) : 0;
    memcpy(out, bytes->data() + offset, size);
    return size;
  }, bytes->size(), error);
  KernelMachine m;
  m.kernel.disc = disc;
  u32 file = m.call("sceIoOpen", {m.string("disc0:/DISC.BIN"), 0x0001, 0});
  u64 at = u64(m.kernel.files[file].sector) * 2048;
  *unreadable = {at + 4096, at + 6144};  //its third sector
  m.system.memory.fill(Buffer, 0xcc, 8192);
  CHECK(m.call("sceIoRead", {file, Buffer, 4096}), 4096);
  CHECK(m.call("sceIoRead", {file, Buffer + 4096, 4096}), Kernel::ErrorIOError);
  CHECK(m.kernel.files[file].position, 4096);
  CHECK(m.call("sceIoReadAsync", {file, Buffer + 4096, 4096}), 0);
  CHECK(m.kernel.files[file].position, 8192);
  advance(m, m.kernel.files[file].asyncDoneAt);
  CHECK(m.call("sceIoPollAsync", {file, R}), 0);
  CHECK(result64(m, R), u64(s64(s32(Kernel::ErrorIOError))));
  CHECK(m.kernel.files[file].position, 4096);
  CHECK(word(m, Buffer + 4096) == 0xcccc'cccc && word(m, Buffer + 8188) == 0xcccc'cccc, true);
  *unreadable = {0, 0};
  CHECK(m.call("sceIoReadAsync", {file, Buffer + 4096, 4096}), 0);
  advance(m, m.kernel.files[file].asyncDoneAt);
  CHECK(m.call("sceIoPollAsync", {file, R}), 0);
  CHECK(result64(m, R), 4096);
  CHECK(word(m, Buffer + 4096) == 0x0707'0707 && word(m, Buffer + 8188) == 0x0707'0707, true);
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
    CHECK(roundTrip(m, [&](KernelMachine& n) { n.kernel.mount("ms0", stick.path.string()); }), true);
  }
}

//Two threads wait on one file's request: as it's done, both run on, the first to have begun waiting with the result,
//the other told there's none left (NOASYNC, as a wait begun then would be), its result's place untouched. (Only the
//first had woken: the other waited for good, and the machine's state was refused.) On both engines.
static auto asyncTwoWaiters() -> void {
  HostFolder stick;
  stick.put("DATA.BIN", text(10000));
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.kernel.mount("ms0", stick.path.string());
    m.system.memory.write(4, R + 0x18, 0x1337);
    m.system.memory.write(4, R + 0x1c, 0x1337);
    Assembler worker{m, 0x0880'2000};
    worker.li(t0, R + 0x100); worker.put(lw(a0, 0, t0)); worker.li(a1, R + 0x18);
    worker.call("sceIoWaitAsync");
    worker.li(t0, R); worker.put(sw(v0, 0x28, t0));
    worker.print("worker woke\n");
    worker.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("ms0:/DATA.BIN")); main.li(a1, 1); main.li(a2, 0);
    main.call("sceIoOpen");
    main.put(addu(s0, v0, zero));
    main.li(t0, R + 0x100); main.put(sw(s0, 0, t0));
    main.put(addu(a0, s0, zero)); main.li(a1, Buffer); main.li(a2, 4000);
    main.call("sceIoReadAsync");
    main.li(a0, m.string("worker")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");
    main.put(addu(a0, s0, zero)); main.li(a1, R + 0x10);
    main.call("sceIoWaitAsync");  //the worker begins waiting after it
    main.li(t0, R); main.put(sw(v0, 0x20, t0));
    main.print("main woke\n");
    main.li(a0, 1000);
    main.call("sceKernelDelayThread");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(m.output == "main woke\nworker woke\n", true);
    if(m.output != "main woke\nworker woke\n") std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R + 0x20) == 0 && result64(m, R + 0x10) == 4000, true);
    CHECK(word(m, R + 0x28), Kernel::ErrorNoAsync);
    CHECK(word(m, R + 0x18) == 0x1337 && word(m, R + 0x1c) == 0x1337, true);
    CHECK(roundTrip(m, [&](KernelMachine& n) { n.kernel.mount("ms0", stick.path.string()); }), true);
    CHECK(m.notes.size(), 0);
  }
}

//The file's callback, run as the main thread waits in sceIoWaitAsyncCB, takes the request's result itself, polling,
//then waits a millisecond: the main thread's wait, once the callback has returned, ends with none left (NOASYNC). A
//state saved while the callback waits (its thread's wait on the file put aside, the file with no request) loads into
//another machine, which makes the same state, and both carry on alike. (The wait had gone on for good, and the state
//was refused.) On both engines.
static auto asyncCallbackTakes() -> void {
  HostFolder stick;
  stick.put("DATA.BIN", text(10000));
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.kernel.mount("ms0", stick.path.string());
    m.system.memory.write(4, R + 0x10, 0x1337);
    Assembler callback{m, 0x0880'3000};
    callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
    callback.li(t0, R + 0x100); callback.put(lw(a0, 0, t0)); callback.li(a1, R + 0x30);
    callback.call("sceIoPollAsync");
    callback.li(t0, R); callback.put(sw(v0, 0x38, t0));
    callback.li(a0, 1000);
    callback.call("sceKernelDelayThread");
    callback.print("callback polled\n");
    callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
    callback.li(v0, 0); callback.put(jr(ra)); callback.put(nop);
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.put(addu(s1, v0, zero));
    main.li(a0, m.string("ms0:/DATA.BIN")); main.li(a1, 1); main.li(a2, 0);
    main.call("sceIoOpen");
    main.put(addu(s0, v0, zero));
    main.li(t0, R + 0x100); main.put(sw(s0, 0, t0));
    main.put(addu(a0, s0, zero)); main.put(addu(a1, s1, zero)); main.li(a2, 0);
    main.call("sceIoSetAsyncCallback");
    main.put(addu(a0, s0, zero)); main.li(a1, Buffer); main.li(a2, 4000);
    main.call("sceIoReadAsync");
    main.put(addu(a0, s0, zero)); main.li(a1, R + 0x10);
    main.call("sceIoWaitAsyncCB");
    main.li(t0, R); main.put(sw(v0, 0x20, t0));
    main.print("main woke\n");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency * 15 / 10'000);  //1.5 ms: the callback waits
    CHECK(m.kernel.exited, false);
    CHECK(m.kernel.files.at(word(m, R + 0x100)).async == Kernel::OpenFile::Async::None, true);
    auto state = saveState(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    n.kernel.mount("ms0", stick.path.string());
    CHECK(loadState(n, state), true);
    CHECK(saveState(n) == state, true);
    for(auto* each : {&m, &n}) {
      each->kernel.run(Kernel::CPUFrequency / 100);
      CHECK(each->kernel.exited, true);
      CHECK(each->output == "callback polled\nmain woke\n", true);
      CHECK(word(*each, R + 0x38) == 0 && result64(*each, R + 0x30) == 4000, true);
      CHECK(word(*each, R + 0x20), Kernel::ErrorNoAsync);
      CHECK(word(*each, R + 0x10), 0x1337);
    }
    if(m.output != "callback polled\nmain woke\n") std::printf("  [%s]\n", m.output.c_str());
    CHECK(m.notes.size(), 0);
  }
}

//An asynchronous ioctl takes the time of what it put in its output, whatever the output's length: the disc file's
//first sector (4 bytes) with a length of 0xffffffff is due 100 microseconds and 4 bytes at the disc's rate on, a
//read of 2048 bytes through an ioctl 100 microseconds and its 2048 bytes on, and the machine's state, saved with each
//under way, loads. (The length had made the first due in 52 minutes, which no state allows: the machine's own was
//refused.)
static auto asyncIoctlTiming() -> void {
  auto image = disc_image::makeIso({{"DISC.BIN", std::vector<u8>(8192, 7)}});
  KernelMachine m;
  m.kernel.disc = discFrom(image.bytes);
  auto prepare = [&](KernelMachine& n) { n.kernel.disc = discFrom(image.bytes); };
  u32 disc = m.call("sceIoOpen", {m.string("disc0:/DISC.BIN"), 0x0001, 0});
  u64 start = m.kernel.cycles;
  CHECK(m.call("sceIoIoctlAsync", {disc, 0x0102'0006, 0, 0, R + 8, 0xffff'ffff}), 0);
  CHECK(word(m, R + 8), m.kernel.files[disc].sector);
  CHECK(m.kernel.files[disc].asyncDoneAt - start, 100 * Microsecond + 4 * Kernel::CPUFrequency / 1'375'000);
  CHECK(roundTrip(m, prepare), true);
  advance(m, m.kernel.files[disc].asyncDoneAt);
  CHECK(m.call("sceIoPollAsync", {disc, R}), 0);
  CHECK(result64(m, R), 0);
  m.system.memory.write(4, R + 0x10, 2048);
  start = m.kernel.cycles;
  CHECK(m.call("sceIoIoctlAsync", {disc, 0x0103'0008, R + 0x10, 4, Buffer, 0xffff'ffff}), 0);
  CHECK(m.kernel.files[disc].asyncDoneAt - start, 100 * Microsecond + 2048 * Kernel::CPUFrequency / 1'375'000);
  CHECK(roundTrip(m, prepare), true);
  advance(m, m.kernel.files[disc].asyncDoneAt);
  CHECK(m.call("sceIoPollAsync", {disc, R}), 0);
  CHECK(result64(m, R), 2048);
  CHECK(m.system.memory.read(1, Buffer + 2047), 7);
  CHECK(m.notes.size(), 0);
}

//A state saved while the main thread waits for a read from the disc loads into another machine, which makes the same
//state and carries on as the first does: the wait ends with the read's result at the same moment, its bytes landing
//then. And a state with the file gone (no disc in the drive) tells the waiting thread it's a bad file, and nothing
//lands.
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
    CHECK(word(m, Buffer) == 0 && word(m, Buffer + 65532) == 0, true);  //its bytes not there yet
    auto state = saveState(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    n.kernel.disc = discFrom(image.bytes);
    CHECK(loadState(n, state), true);
    CHECK(saveState(n) == state, true);
    for(auto* each : {&m, &n}) {
      each->kernel.run(Kernel::CPUFrequency / 10);
      CHECK(each->kernel.exited, true);
      CHECK(result64(*each, R + 0x10), 65536);
      CHECK(word(*each, R + 0x20), 0);
      CHECK(word(*each, Buffer) == 0x0303'0303 && word(*each, Buffer + 65532) == 0x0303'0303, true);
    }
    CHECK(word(m, R + 0x24), word(n, R + 0x24));
    CHECK(word(m, R + 0x24), (100 * Microsecond + 65536 * Kernel::CPUFrequency / 1'375'000) / Microsecond);
    KernelMachine without;  //no disc: the file's dropped, and the waiting thread is told
    without.system.recompiler.enabled = recompile;
    CHECK(loadState(without, state), true);
    without.kernel.run(Kernel::CPUFrequency / 10);
    CHECK(without.kernel.exited, true);
    CHECK(word(without, R + 0x20), Kernel::ErrorBadFile);
    CHECK(word(without, Buffer), 0);  //none came
    CHECK(roundTrip(m, [&](KernelMachine& fresh) { fresh.kernel.disc = discFrom(image.bytes); }), true);
    CHECK(roundTrip(without), true);
  }
}

//Synchronous reads and writes wait for their device, taking an asynchronous request's time (io.cpp): the main thread
//reading 4000 bytes from the memory stick waits 1100 microseconds, a worker of a lower priority running meanwhile; a
//write of 1000 bytes there, 350; 2750 bytes from the disc, 2100 (1,375,000 bytes a second), and 11 of umd0:'s sectors
//16484. A read whose bytes can't go where it's told fails at once. Refused before anything moves, with interrupts or
//dispatching held off (CAN_NOT_WAIT) and in a vertical blank's handler (ILLEGAL_CONTEXT), a bad file refused first,
//as pspautotests' intr/waits recorded; standard output is written at once whatever the context. sceIoIoctl's reads
//do the same: 1375 bytes from the disc file take 1100 microseconds, 11 of umd0:'s sectors 16484, refused alike, a bad
//file first, while its other requests are answered at once even with interrupts held off. (Its reads had taken no
//time, and gone ahead where a thread can't wait.) On both engines.
static auto syncWaits() -> void {
  HostFolder stick;
  stick.put("DATA.BIN", text(10000));
  auto image = disc_image::makeIso({{"DISC.BIN", std::vector<u8>(8192, 3)}});
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.kernel.mount("ms0", stick.path.string());
    m.kernel.disc = discFrom(image.bytes);
    auto store = [&](Assembler& a, u32 offset) { a.li(t0, R + offset); a.put(sw(v0, 0, t0)); };
    auto time = [&](Assembler& a, u32 offset) { a.call("sceKernelGetSystemTimeLow"); store(a, offset); };
    auto io = [&](Assembler& a, const char* name, u32 file, u32 data, u32 size, u32 offset) {
      a.put(addu(a0, file, zero)); a.li(a1, data); a.li(a2, size);
      a.call(name);
      store(a, offset);
    };
    //an ioctl (file, command, in: how many, its 4 bytes, out, out's length)
    auto ioctl = [&](Assembler& a, u32 file, u32 command, u32 count, u32 out, u32 room, u32 offset) {
      a.li(t0, R + 0x110); a.li(t1, count); a.put(sw(t1, 0, t0));
      a.put(addu(a0, file, zero)); a.li(a1, command); a.li(a2, R + 0x110); a.li(a3, 4); a.li(t0, out); a.li(t1, room);
      a.call("sceIoIoctl");
      store(a, offset);
    };
    Assembler handler{m, 0x0880'3000};  //a vertical blank's: a read and a write of good files, a read of a bad one
    handler.put(addiu(sp, sp, -16)); handler.put(sw(ra, 12, sp));
    handler.li(t0, R + 0x100); handler.put(lw(s5, 0, t0)); handler.put(lw(s6, 4, t0)); handler.put(lw(s4, 8, t0));
    handler.li(s7, 63);
    io(handler, "sceIoRead", s5, Buffer + 0x100, 4, 0x50);
    io(handler, "sceIoRead", s7, Buffer + 0x100, 4, 0x54);
    io(handler, "sceIoWrite", s6, Buffer + 0x100, 4, 0x58);
    ioctl(handler, s4, 0x0103'0008, 4, Buffer + 0x100, 4, 0x5c);
    handler.put(lw(ra, 12, sp)); handler.put(addiu(sp, sp, 16));
    handler.li(v0, 0); handler.put(jr(ra)); handler.put(nop);
    Assembler worker{m, 0x0880'2000};
    time(worker, 0x80);
    worker.print("worker\n");
    worker.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    auto open = [&](const char* path, u32 flags, u32 into) {
      main.li(a0, m.string(path)); main.li(a1, flags); main.li(a2, 0777);
      main.call("sceIoOpen");
      main.put(addu(into, v0, zero));
    };
    open("ms0:/DATA.BIN", 0x0001, s0);
    open("ms0:/OUT.BIN", 0x0202, s1);  //PSP_O_WRONLY | PSP_O_CREAT
    open("disc0:/DISC.BIN", 0x0001, s2);
    open("umd0:", 0x0001, s3);
    main.li(t0, R + 0x100); main.put(sw(s0, 0, t0)); main.put(sw(s1, 4, t0)); main.put(sw(s2, 8, t0));
    main.li(s7, 63);
    main.li(a0, m.string("worker")); main.li(a1, 0x0880'2000); main.li(a2, 0x30); main.li(a3, 0x1000);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelCreateThread");
    main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelStartThread");
    time(main, 0x00); io(main, "sceIoRead", s0, Buffer, 4000, 0x20); time(main, 0x04);
    main.print("main read\n");
    time(main, 0x08); io(main, "sceIoWrite", s1, Buffer, 1000, 0x24); time(main, 0x0c);
    time(main, 0x10); io(main, "sceIoRead", s2, Buffer, 2750, 0x28); time(main, 0x14);
    time(main, 0x18); io(main, "sceIoRead", s3, Buffer + 0x1000, 11, 0x2c); time(main, 0x1c);
    time(main, 0x60); io(main, "sceIoRead", s0, 0x10, 4, 0x30); time(main, 0x64);  //nowhere to put them
    time(main, 0x88); ioctl(main, s2, 0x0103'0008, 1375, Buffer + 0x8000, 1375, 0x90); time(main, 0x8c);
    time(main, 0x94); ioctl(main, s3, 0x01f3'0003, 11, Buffer + 0x9000, 11 * 2048, 0x9c); time(main, 0x98);
    main.call("sceKernelCpuSuspendIntr");
    main.put(addu(s4, v0, zero));
    io(main, "sceIoRead", s0, Buffer, 4, 0x34);
    io(main, "sceIoRead", s7, Buffer, 4, 0x38);
    io(main, "sceIoWrite", s1, Buffer, 4, 0x3c);
    ioctl(main, s2, 0x0103'0008, 4, Buffer, 4, 0xa0);
    ioctl(main, s7, 0x0103'0008, 4, Buffer, 4, 0xa4);
    ioctl(main, s2, 0x0102'0006, 0, R + 0x114, 4, 0xa8);  //the file's first sector: no reading, answered at once
    main.print("held off\n");
    main.put(addu(a0, s4, zero));
    main.call("sceKernelCpuResumeIntr");
    main.call("sceKernelSuspendDispatchThread");
    main.put(addu(s4, v0, zero));
    io(main, "sceIoRead", s0, Buffer, 4, 0x40);
    io(main, "sceIoWrite", s1, Buffer, 4, 0x44);
    ioctl(main, s3, 0x01f3'0003, 1, Buffer, 2048, 0xac);
    main.put(addu(a0, s4, zero));
    main.call("sceKernelResumeDispatchThread");
    io(main, "sceIoRead", s0, Buffer, 4, 0x48);  //where the first read left it: nothing refused moved it
    main.li(a0, 30); main.li(a1, 0); main.li(a2, 0x0880'3000); main.li(a3, 0);
    main.call("sceKernelRegisterSubIntrHandler");
    main.li(a0, 30); main.li(a1, 0);
    main.call("sceKernelEnableSubIntr");
    main.call("sceDisplayWaitVblankStart");
    main.li(a0, 30); main.li(a1, 0);
    main.call("sceKernelReleaseSubIntrHandler");
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "worker\nmain read\nheld off\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    auto took = [&](u32 from, u32 microseconds) {  //(the instructions around the calls add under a microsecond)
      u32 elapsed = word(m, R + from + 4) - word(m, R + from);
      return elapsed == microseconds || elapsed == microseconds + 1;
    };
    CHECK(word(m, R + 0x20) == 4000 && took(0x00, 1100), true);
    CHECK(word(m, R + 0x80) >= word(m, R) && word(m, R + 0x80) < word(m, R + 4), true);  //the worker meanwhile
    CHECK(word(m, R + 0x24) == 1000 && took(0x08, 350), true);
    CHECK(stick.get("OUT.BIN") == text(1000), true);
    CHECK(word(m, R + 0x28) == 2750 && took(0x10, 2100), true);
    CHECK(word(m, R + 0x2c) == 11 && took(0x18, 16484), true);
    CHECK(word(m, R + 0x30) == Kernel::ErrorIllegalAddress && took(0x60, 0), true);
    CHECK(word(m, R + 0x34), Kernel::ErrorCanNotWait);
    CHECK(word(m, R + 0x38), Kernel::ErrorBadFile);
    CHECK(word(m, R + 0x3c), Kernel::ErrorCanNotWait);
    CHECK(word(m, R + 0x40), Kernel::ErrorCanNotWait);
    CHECK(word(m, R + 0x44), Kernel::ErrorCanNotWait);
    CHECK(word(m, R + 0x48), 4);
    CHECK(m.system.memory.readString(Buffer, 4) == "wxyz", true);  //bytes 4000 to 4003
    CHECK(word(m, R + 0x50), Kernel::ErrorIllegalContext);
    CHECK(word(m, R + 0x54), Kernel::ErrorBadFile);
    CHECK(word(m, R + 0x58), Kernel::ErrorIllegalContext);
    CHECK(word(m, R + 0x90) == 1375 && took(0x88, 1100), true);
    CHECK(m.system.memory.read(1, Buffer + 0x8000 + 1374), 3);
    CHECK(word(m, R + 0x9c) == 11 && took(0x94, 16484), true);
    CHECK(word(m, R + 0xa0), Kernel::ErrorCanNotWait);
    CHECK(word(m, R + 0xa4), Kernel::ErrorBadFile);
    CHECK(word(m, R + 0xa8) == 0 && word(m, R + 0x114) >= 16, true);  //a sector past the disc's system area
    CHECK(word(m, R + 0xac), Kernel::ErrorCanNotWait);
    CHECK(word(m, R + 0x5c), Kernel::ErrorIllegalContext);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m, [&](KernelMachine& n) {
      n.kernel.mount("ms0", stick.path.string());
      n.kernel.disc = discFrom(image.bytes);
    }), true);
  }
}

//A state saved while the main thread waits in a read of 64 KiB from the disc (its bytes in memory already) loads
//into another machine, which makes the same state and carries on as the first does: the read returns 65536 at the
//same moment, 100 microseconds and its bytes at the disc's rate after it began. With no disc in the drive the file
//is dropped, but the read, done already, still returns its count. On both engines.
static auto syncWaitState() -> void {
  auto image = disc_image::makeIso({{"DISC.BIN", std::vector<u8>(65536, 3)}});
  for(bool recompile : {false, true}) {
    KernelMachine m;
    m.kernel.disc = discFrom(image.bytes);
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("disc0:/DISC.BIN")); main.li(a1, 1); main.li(a2, 0);
    main.call("sceIoOpen");
    main.put(addu(a0, v0, zero)); main.li(a1, Buffer); main.li(a2, 65536);
    main.call("sceIoRead");
    main.li(t0, R); main.put(sw(v0, 0x20, t0));
    main.call("sceKernelGetSystemTimeLow");
    main.li(t0, R); main.put(sw(v0, 0x24, t0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency / 100);  //10 ms of the read's 48
    CHECK(m.kernel.exited, false);
    auto& waiting = *m.kernel.threads.begin()->second;
    CHECK(waiting.status == Kernel::Status::Waiting && waiting.wait == Kernel::Wait::File, true);
    CHECK(m.system.memory.read(1, Buffer + 65535), 3);
    auto state = saveState(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    n.kernel.disc = discFrom(image.bytes);
    CHECK(loadState(n, state), true);
    CHECK(saveState(n) == state, true);
    KernelMachine without;
    without.system.recompiler.enabled = recompile;
    CHECK(loadState(without, state), true);
    for(auto* each : {&m, &n, &without}) {
      each->kernel.run(Kernel::CPUFrequency / 10);
      CHECK(each->kernel.exited, true);
      CHECK(word(*each, R + 0x20), 65536);
      CHECK(word(*each, R + 0x24), word(m, R + 0x24));
    }
    CHECK(word(m, R + 0x24), (100 * Microsecond + 65536 * Kernel::CPUFrequency / 1'375'000) / Microsecond);
    CHECK(roundTrip(m, [&](KernelMachine& fresh) { fresh.kernel.disc = discFrom(image.bytes); }), true);
    CHECK(m.notes.size(), 0);
  }
}

auto asyncTests() -> Tests {
  return {{"async files called directly", asyncCalls}, {"async files from a damaged image", asyncDamaged},
          {"async files waited for", asyncWaits},
          {"async files two waiters", asyncTwoWaiters}, {"async files callback takes the result", asyncCallbackTakes},
          {"async files ioctl timing", asyncIoctlTiming}, {"async files state", asyncState},
          {"files synchronous reads wait", syncWaits}, {"files synchronous wait state", syncWaitState}};
}

}
