//Message pipes and mailboxes (ares/psp/kernel/messages.cpp): what their functions take and refuse, as pspautotests'
//threads/msgpipe and threads/mbx recorded on a PSP; bytes through a pipe's ring and straight across between threads
//waiting part way, timing out with what they moved, held up in line; memory behind messages; the caller's callbacks
//run as a send or receive returns, not those of the thread it woke; packets queued in order or by priority, linked
//in the program's memory, handed to waiting threads; and a state saved with a thread waiting part way. Each group's
//machine, saved at its end, loads into another that makes the same state. Programs run on both engines.
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

namespace {
constexpr u32 R = KernelMachine::Results, Data = 0x0893'0000, Out = 0x0894'0000;

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

//A pipe's status: (buffer size, free, senders waiting, receivers waiting), from sceKernelReferMsgPipeStatus.
auto status(KernelMachine& m, u32 pipe) -> std::array<u32, 4> {
  m.system.memory.write(4, R + 0x200, 56);
  m.call("sceKernelReferMsgPipeStatus", {pipe, R + 0x200});
  return {word(m, R + 0x200 + 40), word(m, R + 0x200 + 44), word(m, R + 0x200 + 48), word(m, R + 0x200 + 52)};
}

//A thread that sends or receives on the pipe in R + 0x10 (size bytes, mode 0, timeout 10 ms), then writes down its
//result at at and the count at at + 4.
auto pipeThread(KernelMachine& m, u32 entry, bool send, u32 size, u32 at) -> void {
  Assembler a{m, entry};
  a.li(t0, R + 0x10); a.put(lw(a0, 0, t0));
  a.li(a1, send ? Data : Out + (at & 0xff) * 0x10); a.li(a2, size); a.li(a3, 0); a.li(t0, at + 4);
  a.li(t1, R + 0x14);  //a timeout of its own: a copy of 10 ms
  a.li(t2, 10'000); a.put(sw(t2, 0, t1));
  a.call(send ? "sceKernelSendMsgPipe" : "sceKernelReceiveMsgPipe");
  a.li(t0, at); a.put(sw(v0, 0, t0));
  a.call("sceKernelExitThread");
}

//Starts a thread (entry, priority 0x30: it runs as main waits) from a program.
auto start(Assembler& main, KernelMachine& m, u32 entry, u32 priority = 0x30) -> void {
  main.li(a0, m.string("thread")); main.li(a1, entry); main.li(a2, priority); main.li(a3, 0x1000);
  main.li(t0, 0); main.li(t1, 0);
  main.call("sceKernelCreateThread");
  main.put(addu(a0, v0, zero)); main.li(a1, 0); main.li(a2, 0);
  main.call("sceKernelStartThread");
}

auto delay(Assembler& main, u32 microseconds) -> void {
  main.li(a0, microseconds);
  main.call("sceKernelDelayThread");
}
}

//Creating, as threads/msgpipe/create recorded; sending and receiving through the buffer, its ring wrapping, as
//send, receive, trysend and tryreceive recorded (sizes, modes, ASAP's part, the try functions' refusals); cancelling
//empties it; deleting gives its memory back.
static auto pipeCalls() -> void {
  KernelMachine m;
  auto create = [&](u32 name, u32 partition, u32 attributes, u32 size) {
    return m.call("sceKernelCreateMsgPipe", {name, partition, attributes, size, 0});
  };
  u32 name = m.string("pipe");
  CHECK(create(0, 2, 0, 0x100), Kernel::ErrorNoMemory);
  for(u32 partition : {u32(-1), 0u, 7u, 10u}) CHECK(create(name, partition, 0, 0x100), Kernel::ErrorIllegalArgument);
  for(u32 partition : {1u, 3u, 4u}) CHECK(create(name, partition, 0, 0x100), Kernel::ErrorIllegalPermission);
  for(u32 attributes : {0x200u, 0x400u, 0x2000u, 0x8000u}) {
    CHECK(create(name, 2, attributes, 0x100), Kernel::ErrorIllegalAttribute);
  }
  CHECK(create(name, 2, 0, 0x400'0000), Kernel::ErrorNoMemory);
  CHECK(create(name, 2, 0, u32(-1)), Kernel::ErrorNoMemory);
  u32 free = m.kernel.largestFree();
  u32 odd = create(name, 6, 0x51ff, 0x39);
  CHECK(odd < 0x8000'0000, true);
  CHECK((status(m, odd) == std::array<u32, 4>{0x39, 0x39, 0, 0}), true);
  CHECK(m.call("sceKernelDeleteMsgPipe", {odd}), 0);
  CHECK(m.kernel.largestFree(), free);  //its buffer given back
  CHECK(m.call("sceKernelDeleteMsgPipe", {odd}), Kernel::ErrorUnknownMessagePipe);

  for(u32 n = 0; n < 0x1000; n++) m.system.memory.write(1, Data + n, u8(n * 7));
  u32 pipe = create(name, 2, 0, 0x1000);
  auto send = [&](u32 size, u32 mode, bool wait = false) {
    m.system.memory.write(4, R, 1337);
    m.system.memory.write(4, R + 4, 1000);  //a timeout (direct calls don't wait: no thread)
    return m.call(wait ? "sceKernelSendMsgPipe" : "sceKernelTrySendMsgPipe", {pipe, Data, size, mode, R, R + 4});
  };
  auto receive = [&](u32 size, u32 mode) {
    m.system.memory.write(4, R, 1337);
    return m.call("sceKernelTryReceiveMsgPipe", {pipe, Out, size, mode, R});
  };
  CHECK(send(0x100, 0), 0);
  CHECK(word(m, R), 0x100);
  CHECK((status(m, pipe) == std::array<u32, 4>{0x1000, 0xf00, 0, 0}), true);
  CHECK(send(0, 0), 0);
  CHECK(word(m, R), 0);
  CHECK(send(0x2000, 0), Kernel::ErrorIllegalSize);
  CHECK(word(m, R), 1337);
  CHECK(send(u32(-1), 0), Kernel::ErrorIllegalAddress);
  for(u32 mode : {u32(-1), 2u, 0x101u}) CHECK(send(0x10, mode), Kernel::ErrorIllegalMode);
  CHECK(m.call("sceKernelTrySendMsgPipe", {0xdead'beef, Data, 0x10, 0, R}), Kernel::ErrorUnknownMessagePipe);
  CHECK(send(0x1000, 1), 0);  //ASAP: what fits
  CHECK(word(m, R), 0xf00);
  CHECK(send(0x100, 0), Kernel::ErrorPipeFull);
  CHECK(word(m, R), 1337);
  CHECK(send(0x100, 1), Kernel::ErrorPipeFull);
  CHECK(word(m, R), 0);  //ASAP is told nothing went, as trysend recorded
  CHECK(receive(0x80, 0), 0);
  CHECK(word(m, R), 0x80);
  CHECK(m.system.memory.read(1, Out + 0x7f), u8(0x7f * 7));
  CHECK(receive(0x2000, 1), Kernel::ErrorIllegalSize);
  CHECK(receive(0x1000, 1), 0);  //ASAP: what's there
  CHECK(word(m, R), 0xf80);
  CHECK(m.system.memory.read(1, Out), u8(0x80 * 7));          //the rest of the first message
  CHECK(m.system.memory.read(1, Out + 0x80), u8(0));          //then the second, from its start
  CHECK(m.system.memory.read(1, Out + 0xf7f), u8(0xeff * 7));
  CHECK(receive(0x10, 0), Kernel::ErrorPipeEmpty);
  CHECK((status(m, pipe) == std::array<u32, 4>{0x1000, 0x1000, 0, 0}), true);
  //around the ring: its start is at 0x1000 now, a message of 0x800 goes in from 0 and comes out whole
  CHECK(send(0x800, 0), 0);
  CHECK(receive(0x800, 0), 0);
  CHECK(m.system.memory.read(1, Out + 0x7ff), u8(0x7ff * 7));
  CHECK(send(0x200, 0), 0);
  CHECK(m.call("sceKernelCancelMsgPipe", {pipe, R + 8, R + 12}), 0);
  CHECK(word(m, R + 8) == 0 && word(m, R + 12) == 0, true);
  CHECK(status(m, pipe)[1], 0x1000);  //emptied
  CHECK(send(0x100, 0, true), 0);     //a send that needn't wait, through the waiting function
  //no buffer: nothing goes without a receiver waiting
  u32 bare = create(name, 2, 0, 0);
  CHECK((status(m, bare) == std::array<u32, 4>{0, 0, 0, 0}), true);
  CHECK(m.call("sceKernelTrySendMsgPipe", {bare, Data, 0x2000, 0, R}), Kernel::ErrorPipeFull);
  CHECK(m.call("sceKernelTrySendMsgPipe", {bare, Data, 0, 0, R}), 0);
  CHECK(m.call("sceKernelTryReceiveMsgPipe", {bare, Out, 0x10, 0, R}), Kernel::ErrorPipeEmpty);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//Threads waiting part way, as threads/msgpipe's send and receive tests recorded. Two receivers wait on an empty pipe
//of 0x100 bytes, for 0x80 and 0x100: a send of 0x100 gives the first its 0x80 and the second the other 0x80, which
//it times out with (10 ms). Then three senders, of 0x80 (into the buffer at once), 0x100 (it doesn't fit) and 0x80
//(it would, but waits in line behind the second): a receive of 0x100 takes the buffer's 0x80 and the second
//sender's first 0x80 straight across, the buffer then taking the second's other 0x80 and the third's whole message.
//Last, a pipe with no buffer: two receivers of 0x100 waiting get a send's 0x80 straight across, then another's 0x100;
//deleting the pipe tells the second, with its 0x80. On both engines.
static auto pipeWaits() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    for(u32 n = 0; n < 0x100; n++) m.system.memory.write(1, Data + n, u8(n));
    constexpr u32 First = R + 0x20, Second = R + 0x28;
    pipeThread(m, 0x0880'2000, false, 0x80, First);
    pipeThread(m, 0x0880'2100, false, 0x100, Second);
    pipeThread(m, 0x0880'2200, true, 0x80, R + 0x40);
    pipeThread(m, 0x0880'2300, true, 0x100, R + 0x48);
    pipeThread(m, 0x0880'2400, true, 0x80, R + 0x50);
    pipeThread(m, 0x0880'2500, false, 0x100, R + 0x60);
    pipeThread(m, 0x0880'2600, false, 0x100, R + 0x68);
    Assembler main{m, 0x0880'1000};
    auto create = [&](u32 size) {
      main.li(a0, m.string("pipe")); main.li(a1, 2); main.li(a2, 0); main.li(a3, size); main.li(t0, 0);
      main.call("sceKernelCreateMsgPipe");
      main.li(t0, R + 0x10); main.put(sw(v0, 0, t0));
    };
    auto transfer = [&](bool send, u32 size, u32 at) {
      main.li(t0, R + 0x10); main.put(lw(a0, 0, t0));
      main.li(a1, send ? Data : Out); main.li(a2, size); main.li(a3, 0); main.li(t0, at + 4); main.li(t1, 0);
      main.call(send ? "sceKernelSendMsgPipe" : "sceKernelReceiveMsgPipe");
      main.li(t0, at); main.put(sw(v0, 0, t0));
    };
    create(0x100);
    start(main, m, 0x0880'2000);
    start(main, m, 0x0880'2100);
    delay(main, 1000);
    transfer(true, 0x100, R + 0x80);
    delay(main, 20'000);  //the second's 10 ms run out
    create(0x100);
    start(main, m, 0x0880'2200);
    start(main, m, 0x0880'2300);
    start(main, m, 0x0880'2400);
    delay(main, 1000);
    transfer(false, 0x100, R + 0x88);
    main.li(t0, R + 0x10); main.put(lw(a0, 0, t0)); main.li(a1, R + 0x100);
    main.li(t0, 56); main.put(sw(t0, 0, a1));
    main.call("sceKernelReferMsgPipeStatus");
    delay(main, 1000);
    create(0);
    start(main, m, 0x0880'2500);
    start(main, m, 0x0880'2600);
    delay(main, 1000);
    transfer(true, 0x80, R + 0x90);
    transfer(true, 0x100, R + 0x98);
    main.li(t0, R + 0x10); main.put(lw(a0, 0, t0));
    main.call("sceKernelDeleteMsgPipe");
    delay(main, 1000);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto pair = [&](u32 at) { return std::pair{word(m, at), word(m, at + 4)}; };
    CHECK((pair(First) == std::pair{0u, 0x80u}), true);
    CHECK((pair(Second) == std::pair{Kernel::ErrorWaitTimeout, 0x80u}), true);
    CHECK((pair(R + 0x80) == std::pair{0u, 0x100u}), true);
    CHECK(m.system.memory.read(1, Out + (First & 0xff) * 0x10 + 0x7f), 0x7f);
    CHECK(m.system.memory.read(1, Out + (Second & 0xff) * 0x10 + 0x7f), 0xff);  //the send's second half
    CHECK((pair(R + 0x40) == std::pair{0u, 0x80u}), true);
    CHECK((pair(R + 0x48) == std::pair{0u, 0x100u}), true);
    CHECK((pair(R + 0x50) == std::pair{0u, 0x80u}), true);
    CHECK((pair(R + 0x88) == std::pair{0u, 0x100u}), true);
    CHECK(word(m, R + 0x100 + 44), 0);  //the buffer full: the second's other 0x80, the third's 0x80
    CHECK((pair(R + 0x90) == std::pair{0u, 0x80u}), true);
    CHECK((pair(R + 0x98) == std::pair{0u, 0x100u}), true);
    CHECK((pair(R + 0x60) == std::pair{0u, 0x100u}), true);
    CHECK((pair(R + 0x68) == std::pair{Kernel::ErrorWaitDeleted, 0x80u}), true);
    CHECK(m.notes.size(), 0);
    for(auto& note : m.notes) std::printf("  note: %s\n", note.c_str());
    CHECK(roundTrip(m), true);
  }
}

//Memory behind messages, which threads/msgpipe never tried going without: a send or receive whose message or buffer
//runs past memory is refused (ILLEGAL_ADDR) and moves nothing, after the size checks threads/msgpipe recorded; a null
//message of no bytes is taken. In a program, on a pipe without a buffer: a receiver and a sender of 1 GiB at address
//0x10, each refused at once (a 32 MiB machine had copied the 1 GiB through as much of the host's memory); messages of
//0x3001 bytes straight across each way, and one into VRAM's second copy (which rearranges its bytes); and a receiver
//back from its callback finding a sender waiting, which hands it its message straight across. On both engines.
static auto pipeMemory() -> void {
  {
    KernelMachine m;
    u32 name = m.string("pipe");
    u32 pipe = m.call("sceKernelCreateMsgPipe", {name, 2, 0, 0x100, 0});
    u32 bare = m.call("sceKernelCreateMsgPipe", {name, 2, 0, 0, 0});
    constexpr u32 Nowhere = 0x10, End = 0x0a00'0000;  //nothing at 0x10; RAM's end in a 32 MiB machine
    auto transfer = [&](const char* function, u32 id, u32 address, u32 size) {
      m.system.memory.write(4, R, 1337);
      return m.call(function, {id, address, size, 0, R});
    };
    CHECK(transfer("sceKernelTrySendMsgPipe", pipe, Nowhere, 0x20), Kernel::ErrorIllegalAddress);
    CHECK(transfer("sceKernelTrySendMsgPipe", pipe, End - 0x10, 0x20), Kernel::ErrorIllegalAddress);
    CHECK(word(m, R), 1337);
    CHECK(status(m, pipe)[1], 0x100);  //nothing went in
    CHECK(transfer("sceKernelTrySendMsgPipe", pipe, Nowhere, 0x101), Kernel::ErrorIllegalSize);
    CHECK(transfer("sceKernelTrySendMsgPipe", pipe, 0, 0), 0);
    CHECK(transfer("sceKernelTrySendMsgPipe", pipe, End - 0x10, 0x10), 0);  //up to RAM's end
    CHECK(transfer("sceKernelTryReceiveMsgPipe", pipe, Nowhere, 0x10), Kernel::ErrorIllegalAddress);
    CHECK(transfer("sceKernelTryReceiveMsgPipe", pipe, End - 8, 0x10), Kernel::ErrorIllegalAddress);
    CHECK(status(m, pipe)[1], 0xf0);  //the 0x10 bytes still there
    CHECK(transfer("sceKernelTryReceiveMsgPipe", bare, Nowhere, 0x100), Kernel::ErrorIllegalAddress);
    CHECK(transfer("sceKernelTrySendMsgPipe", bare, Nowhere, 0x4000'0000), Kernel::ErrorIllegalAddress);
    CHECK(word(m, R), 1337);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
  constexpr u32 Vram = 0x0420'1000;  //VRAM's second copy
  for(bool recompile : {false, true}) {
    KernelMachine m;
    for(u32 n = 0; n < 0x4000; n++) m.system.memory.write(1, Data + n, u8(n * 13 + 1));
    m.system.memory.write(4, R + 0x24, 0x1337);
    m.system.memory.write(4, R + 0x44, 0x1337);
    Assembler callback{m, 0x0880'3000};  //waits 2 ms, as main sends meanwhile
    callback.put(addiu(sp, sp, -16)); callback.put(sw(ra, 12, sp));
    callback.li(a0, 2000); callback.call("sceKernelDelayThread");
    callback.put(lw(ra, 12, sp)); callback.put(addiu(sp, sp, 16));
    callback.li(v0, 0); callback.put(jr(ra)); callback.put(nop);
    //a thread that sends or receives size bytes at address on the pipe in R + 0x10, then writes down its result at
    //`at` and how many bytes moved at at + 4; with callbacks, it makes one first, its ID in R + 0x18
    auto transfer = [&](u32 entry, bool send, u32 address, u32 size, u32 at, bool callbacks = false) {
      Assembler a{m, entry};
      if(callbacks) {
        a.li(a0, m.string("cb")); a.li(a1, 0x0880'3000); a.li(a2, 0);
        a.call("sceKernelCreateCallback");
        a.li(t0, R + 0x18); a.put(sw(v0, 0, t0));
      }
      a.li(t0, R + 0x10); a.put(lw(a0, 0, t0));
      a.li(a1, address); a.li(a2, size); a.li(a3, 0); a.li(t0, at + 4); a.li(t1, 0);
      if(send) a.call("sceKernelSendMsgPipe");
      else a.call(callbacks ? "sceKernelReceiveMsgPipeCB" : "sceKernelReceiveMsgPipe");
      a.li(t0, at); a.put(sw(v0, 0, t0));
      a.call("sceKernelExitThread");
    };
    transfer(0x0880'2000, false, 0x10, 0x4000'0000, R + 0x20);
    transfer(0x0880'2100, false, Out + 3, 0x3001, R + 0x28);
    transfer(0x0880'2200, true, Data + 1, 0x3001, R + 0x30);
    transfer(0x0880'2300, false, Vram + 1, 0x101, R + 0x38);
    transfer(0x0880'2400, false, Out + 0xc000, 0x80, R + 0x68, true);
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("pipe")); main.li(a1, 2); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelCreateMsgPipe");
    main.li(t0, R + 0x10); main.put(sw(v0, 0, t0));
    auto mainTransfer = [&](bool send, u32 address, u32 size, u32 at) {
      main.li(t0, R + 0x10); main.put(lw(a0, 0, t0));
      main.li(a1, address); main.li(a2, size); main.li(a3, 0); main.li(t0, at + 4); main.li(t1, 0);
      main.call(send ? "sceKernelSendMsgPipe" : "sceKernelReceiveMsgPipe");
      main.li(t0, at); main.put(sw(v0, 0, t0));
    };
    start(main, m, 0x0880'2000);  //1 GiB at 0x10, each way
    delay(main, 1000);
    mainTransfer(true, 0x10, 0x4000'0000, R + 0x40);
    start(main, m, 0x0880'2100);  //a receiver of 0x3001 waits, a send hands it them
    delay(main, 1000);
    mainTransfer(true, Data + 1, 0x3001, R + 0x48);
    start(main, m, 0x0880'2200);  //a sender of 0x3001 waits, a receive takes them
    delay(main, 1000);
    mainTransfer(false, Out + 0x8000 + 5, 0x3001, R + 0x50);
    start(main, m, 0x0880'2300);  //a receiver into VRAM's second copy
    delay(main, 1000);
    mainTransfer(true, Data, 0x101, R + 0x58);
    start(main, m, 0x0880'2400);  //a receiver with callbacks
    delay(main, 1000);
    main.li(t0, R + 0x18); main.put(lw(a0, 0, t0)); main.li(a1, 0);
    main.call("sceKernelNotifyCallback");
    delay(main, 500);  //its callback runs, waiting 2 ms
    mainTransfer(true, Data, 0x80, R + 0x60);  //no receiver waiting meanwhile: the send waits
    delay(main, 1000);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    auto pair = [&](u32 at) { return std::pair{word(m, at), word(m, at + 4)}; };
    CHECK((pair(R + 0x20) == std::pair{Kernel::ErrorIllegalAddress, 0x1337u}), true);
    CHECK((pair(R + 0x40) == std::pair{Kernel::ErrorIllegalAddress, 0x1337u}), true);
    auto same = [&](u32 at, u32 from, u32 size) {
      for(u32 n = 0; n < size; n++) {
        if(m.system.memory.read(1, at + n) != m.system.memory.read(1, from + n)) return false;
      }
      return true;
    };
    CHECK((pair(R + 0x28) == std::pair{0u, 0x3001u}) && (pair(R + 0x48) == std::pair{0u, 0x3001u}), true);
    CHECK(same(Out + 3, Data + 1, 0x3001), true);
    CHECK((pair(R + 0x30) == std::pair{0u, 0x3001u}) && (pair(R + 0x50) == std::pair{0u, 0x3001u}), true);
    CHECK(same(Out + 0x8000 + 5, Data + 1, 0x3001), true);
    CHECK((pair(R + 0x38) == std::pair{0u, 0x101u}) && (pair(R + 0x58) == std::pair{0u, 0x101u}), true);
    CHECK(same(Vram + 1, Data, 0x101), true);
    CHECK((pair(R + 0x60) == std::pair{0u, 0x80u}) && (pair(R + 0x68) == std::pair{0u, 0x80u}), true);
    CHECK(same(Out + 0xc000, Data, 0x80), true);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//A send with callbacks that needn't wait wakes a receiver of higher priority waiting without them, which has a
//callback notified: the receiver takes over at once, its callback not run (it didn't wait where callbacks may run),
//and the sender's own callback runs as the send returns, once the sender has the CPU again. Then the same for a
//receive with callbacks waking a sender. (The callbacks run on return had been those of whichever thread was
//running by then: the woken one's.) On both engines.
static auto pipeCallbacksOnReturn() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    auto callback = [&](u32 entry, const std::string& text) {
      Assembler a{m, entry};
      a.put(addiu(sp, sp, -16)); a.put(sw(ra, 12, sp));
      a.print(text);
      a.put(lw(ra, 12, sp)); a.put(addiu(sp, sp, 16));
      a.li(v0, 0); a.put(jr(ra)); a.put(nop);
    };
    callback(0x0880'3000, "main's callback\n");
    callback(0x0880'3100, "worker's callback\n");
    //a worker with a callback notified, then sending or receiving on the pipe in R + 0x10, waiting without callbacks
    auto worker = [&](u32 entry, bool send, u32 size) {
      Assembler a{m, entry};
      a.li(a0, m.string("cb")); a.li(a1, 0x0880'3100); a.li(a2, 0);
      a.call("sceKernelCreateCallback");
      a.put(addu(a0, v0, zero)); a.li(a1, 0);
      a.call("sceKernelNotifyCallback");
      a.li(t0, R + 0x10); a.put(lw(a0, 0, t0));
      a.li(a1, send ? Data : Out); a.li(a2, size); a.li(a3, 0); a.li(t0, 0); a.li(t1, 0);
      a.call(send ? "sceKernelSendMsgPipe" : "sceKernelReceiveMsgPipe");
      a.print(send ? "worker sent\n" : "worker received\n");
      a.call("sceKernelExitThread");
    };
    worker(0x0880'2000, false, 4);
    worker(0x0880'2200, true, 8);
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("cb")); main.li(a1, 0x0880'3000); main.li(a2, 0);
    main.call("sceKernelCreateCallback");
    main.put(addu(s1, v0, zero));
    for(bool send : {true, false}) {
      main.li(a0, m.string("pipe")); main.li(a1, 2); main.li(a2, 0); main.li(a3, send ? 0x100 : 0); main.li(t0, 0);
      main.call("sceKernelCreateMsgPipe");
      main.li(t0, R + 0x10); main.put(sw(v0, 0, t0));
      start(main, m, send ? 0x0880'2000 : 0x0880'2200, 0x10);  //it runs at once, and waits
      main.put(addu(a0, s1, zero)); main.li(a1, 0);
      main.call("sceKernelNotifyCallback");
      main.li(t0, R + 0x10); main.put(lw(a0, 0, t0));
      main.li(a1, send ? Data : Out); main.li(a2, send ? 4 : 8); main.li(a3, 0); main.li(t0, 0); main.li(t1, 0);
      main.call(send ? "sceKernelSendMsgPipeCB" : "sceKernelReceiveMsgPipeCB");
      main.li(t0, R); main.put(sw(v0, send ? 0 : 4, t0));
      main.print(send ? "main sent\n" : "main received\n");
    }
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    std::string expected = "worker received\nmain's callback\nmain sent\n"
                           "worker sent\nmain's callback\nmain received\n";
    CHECK(m.output == expected, true);
    if(m.output != expected) std::printf("  [%s]\n", m.output.c_str());
    CHECK(word(m, R) == 0 && word(m, R + 4) == 0, true);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//Mailboxes: creating as threads/mbx/create recorded; packets queued in the order sent, or by their priority byte
//with attribute 0x400, their first words a ring of the queue; polled; a packet sent twice refused; the status as
//threads/mbx/refer recorded.
static auto mailboxCalls() -> void {
  KernelMachine m;
  u32 name = m.string("box");
  CHECK(m.call("sceKernelCreateMbx", {0, 0, 0}), Kernel::ErrorError);
  for(u32 attributes : {0x200u, 0x300u, 0x900u, 0x1200u}) {
    CHECK(m.call("sceKernelCreateMbx", {name, attributes, 0}), Kernel::ErrorIllegalAttribute);
  }
  u32 plain = m.call("sceKernelCreateMbx", {name, 0x122, 0});
  u32 ordered = m.call("sceKernelCreateMbx", {name, 0x400, 0});
  CHECK(plain < 0x8000'0000 && ordered < 0x8000'0000, true);
  constexpr u32 A = Data, B = Data + 0x20, C = Data + 0x40;
  for(auto [packet, priority] : {std::pair{A, 15u}, {B, 5u}, {C, 10u}}) {
    m.system.memory.write(4, packet, 0xdead'beef);
    m.system.memory.write(1, packet + 4, priority);
  }
  CHECK(m.call("sceKernelPollMbx", {plain, R}), Kernel::ErrorMailboxEmpty);
  CHECK(m.call("sceKernelSendMbx", {plain, A}), 0);
  CHECK(word(m, A), A);  //alone: it points at itself
  CHECK(m.call("sceKernelSendMbx", {plain, A}), Kernel::ErrorMessageQueued);
  CHECK(m.call("sceKernelSendMbx", {plain, B}), 0);
  CHECK(word(m, A) == B && word(m, B) == A, true);
  CHECK(m.call("sceKernelSendMbx", {0xdead'beef, C}), Kernel::ErrorUnknownMailbox);
  m.system.memory.write(4, R + 0x100, 0);  //a size of 0: nothing written
  CHECK(m.call("sceKernelReferMbxStatus", {plain, R + 0x100}), 0);
  CHECK(word(m, R + 0x100), 0);
  m.system.memory.write(4, R + 0x100, 0xcccc'cccc);
  CHECK(m.call("sceKernelReferMbxStatus", {plain, R + 0x100}), 0);
  CHECK(word(m, R + 0x100), 52);
  CHECK(m.system.memory.readString(R + 0x104, 32) == "box", true);
  CHECK(word(m, R + 0x124) == 0x122 && word(m, R + 0x128) == 0, true);  //attributes, none waiting
  CHECK(word(m, R + 0x12c) == 2 && word(m, R + 0x130) == A, true);     //two queued, A first
  CHECK(m.call("sceKernelPollMbx", {plain, R}), 0);
  CHECK(word(m, R), A);
  CHECK(word(m, B), B);  //alone again
  CHECK(m.call("sceKernelPollMbx", {plain, R}), 0);
  CHECK(word(m, R), B);
  for(u32 packet : {A, B, C}) m.call("sceKernelSendMbx", {ordered, packet});
  for(u32 packet : {B, C, A}) {  //by priority: 5, 10, 15
    CHECK(m.call("sceKernelPollMbx", {ordered, R}), 0);
    check(__LINE__, "a packet by priority", word(m, R), packet);
  }
  CHECK(m.call("sceKernelDeleteMbx", {plain}), 0);
  CHECK(m.call("sceKernelPollMbx", {plain, R}), Kernel::ErrorUnknownMailbox);
  CHECK(m.notes.size(), 0);
  CHECK(roundTrip(m), true);
}

//Threads waiting on a mailbox: a send hands the packet to the first, in the order they came; a receive with a
//timeout gives up; cancelling tells the rest (counted), and a thread waiting when the mailbox is deleted is told
//that. On both engines.
static auto mailboxWaits() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    auto receiver = [&](u32 entry, u32 at, u32 timeout) {
      Assembler a{m, entry};
      a.li(t0, R + 0x10); a.put(lw(a0, 0, t0)); a.li(a1, at + 4); a.li(a2, timeout ? R + 0x14 : 0);
      a.call("sceKernelReceiveMbx");
      a.li(t0, at); a.put(sw(v0, 0, t0));
      a.call("sceKernelExitThread");
    };
    receiver(0x0880'2000, R + 0x20, 0);
    receiver(0x0880'2100, R + 0x28, 0);
    receiver(0x0880'2200, R + 0x30, 0);
    receiver(0x0880'2300, R + 0x38, 0);
    receiver(0x0880'2400, R + 0x40, 0);
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("box")); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelCreateMbx");
    main.li(t0, R + 0x10); main.put(sw(v0, 0, t0));
    for(u32 n = 0; n < 4; n++) start(main, m, 0x0880'2000 + n * 0x100);
    delay(main, 1000);
    for(u32 packet : {Data, Data + 0x20}) {
      main.li(t0, R + 0x10); main.put(lw(a0, 0, t0)); main.li(a1, packet);
      main.call("sceKernelSendMbx");
    }
    delay(main, 1000);
    main.li(t0, R + 0x10); main.put(lw(a0, 0, t0)); main.li(a1, R + 0x18);
    main.call("sceKernelCancelReceiveMbx");
    delay(main, 1000);
    start(main, m, 0x0880'2400);
    delay(main, 1000);
    main.li(t0, R + 0x10); main.put(lw(a0, 0, t0));
    main.call("sceKernelDeleteMbx");
    delay(main, 1000);
    //a receive that gives up: 500 microseconds on a mailbox with nothing in it
    main.li(a0, m.string("box")); main.li(a1, 0); main.li(a2, 0);
    main.call("sceKernelCreateMbx");
    main.put(addu(a0, v0, zero)); main.li(a1, R + 0x4c); main.li(a2, R + 0x14);
    main.li(t0, R + 0x14); main.li(t1, 500); main.put(sw(t1, 0, t0));
    main.call("sceKernelReceiveMbx");
    main.li(t0, R + 0x48); main.put(sw(v0, 0, t0));
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(word(m, R + 0x20) == 0 && word(m, R + 0x24) == Data, true);
    CHECK(word(m, R + 0x28) == 0 && word(m, R + 0x2c) == Data + 0x20, true);
    CHECK(word(m, R + 0x30), Kernel::ErrorWaitCancelled);
    CHECK(word(m, R + 0x38), Kernel::ErrorWaitCancelled);
    CHECK(word(m, R + 0x18), 2);
    CHECK(word(m, R + 0x40), Kernel::ErrorWaitDeleted);
    CHECK(word(m, R + 0x48), Kernel::ErrorWaitTimeout);
    CHECK(word(m, R + 0x14), 0);
    CHECK(m.notes.size(), 0);
    CHECK(roundTrip(m), true);
  }
}

//A state saved while a receiver waits part way on a pipe without a buffer (0x80 of 0x100 moved) loads into another
//machine, which makes the same state; both carry on alike: the next send completes the receiver, its bytes whole.
//On both engines.
static auto pipeState() -> void {
  for(bool recompile : {false, true}) {
    KernelMachine m;
    for(u32 n = 0; n < 0x100; n++) m.system.memory.write(1, Data + n, u8(n));
    Assembler receiver{m, 0x0880'2000};  //(on the pipe at R + 0x10, no buffer)
    receiver.li(t0, R + 0x10); receiver.put(lw(a0, 0, t0)); receiver.li(a1, Out); receiver.li(a2, 0x100);
    receiver.li(a3, 0); receiver.li(t0, R + 0x24); receiver.li(t1, 0);
    receiver.call("sceKernelReceiveMsgPipe");
    receiver.li(t0, R + 0x20); receiver.put(sw(v0, 0, t0));
    receiver.call("sceKernelExitThread");
    Assembler main{m, 0x0880'1000};
    main.li(a0, m.string("pipe")); main.li(a1, 2); main.li(a2, 0); main.li(a3, 0); main.li(t0, 0);
    main.call("sceKernelCreateMsgPipe");
    main.li(t0, R + 0x10); main.put(sw(v0, 0, t0));
    start(main, m, 0x0880'2000);
    delay(main, 1000);
    main.li(t0, R + 0x10); main.put(lw(a0, 0, t0)); main.li(a1, Data); main.li(a2, 0x80); main.li(a3, 0);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelSendMsgPipe");
    delay(main, 20'000);  //the state is saved here
    main.li(t0, R + 0x10); main.put(lw(a0, 0, t0)); main.li(a1, Data + 0x80); main.li(a2, 0x80); main.li(a3, 0);
    main.li(t0, 0); main.li(t1, 0);
    main.call("sceKernelSendMsgPipe");
    delay(main, 1000);
    main.call("sceKernelExitGame");
    m.runProgram(0x0880'1000, recompile, Kernel::CPUFrequency / 100);
    CHECK(m.kernel.exited, false);
    auto state = saveState(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    CHECK(loadState(n, state), true);
    CHECK(saveState(n) == state, true);
    for(auto* each : {&m, &n}) {
      each->kernel.run(Kernel::CPUFrequency / 10);
      CHECK(each->kernel.exited, true);
      CHECK(word(*each, R + 0x20) == 0 && word(*each, R + 0x24) == 0x100, true);
      CHECK(each->system.memory.read(1, Out + 0xff), 0xff);
      CHECK(roundTrip(*each), true);
    }
  }
}

auto messageTests() -> Tests {
  return {{"message pipes called directly", pipeCalls}, {"message pipes waited for", pipeWaits},
          {"message pipes memory", pipeMemory}, {"message pipes callbacks on return", pipeCallbacksOnReturn},
          {"mailboxes called directly", mailboxCalls},
          {"mailboxes waited for", mailboxWaits}, {"message pipes state", pipeState}};
}

}
