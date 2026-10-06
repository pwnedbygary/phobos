//Message pipes and mailboxes (ares/psp/kernel/messages.cpp): what their functions take and refuse, as pspautotests'
//threads/msgpipe and threads/mbx recorded on a PSP; bytes through a pipe's ring and straight across between threads
//waiting part way, timing out with what they moved, held up in line; packets queued in order or by priority, linked
//in the program's memory, handed to waiting threads; and a state saved with a thread waiting part way. Programs run
//on both engines.
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
  }
}

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
    auto state = save(m);
    KernelMachine n;
    n.system.recompiler.enabled = recompile;
    CHECK(load(n, state), true);
    CHECK(save(n) == state, true);
    for(auto* each : {&m, &n}) {
      each->kernel.run(Kernel::CPUFrequency / 10);
      CHECK(each->kernel.exited, true);
      CHECK(word(*each, R + 0x20) == 0 && word(*each, R + 0x24) == 0x100, true);
      CHECK(each->system.memory.read(1, Out + 0xff), 0xff);
    }
  }
}

auto messageTests() -> Tests {
  return {{"message pipes called directly", pipeCalls}, {"message pipes waited for", pipeWaits},
          {"mailboxes called directly", mailboxCalls}, {"mailboxes waited for", mailboxWaits},
          {"message pipes state", pipeState}};
}

}
