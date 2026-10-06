//Message pipes and mailboxes: two ways threads hand each other data.
//
//A message pipe moves bytes. It has a buffer of its own (any size, none at all too), taken from the user partition,
//a ring holding what's been sent and not yet received. A send gives the threads waiting to receive what they want,
//straight from the sender's memory into theirs, and puts the rest in the buffer; a receive takes from the buffer
//first, then straight from the threads waiting to send. Each says how it waits: for its whole message (mode 0), or
//as soon as any of it can go (mode 1, "ASAP"). A thread that can't finish waits, its timeout permitting, in line
//behind those waiting the same way: its place in the line holds up those behind it, so a sender whose message
//doesn't fit keeps a later small one from going into the buffer's room. Whatever it moved before its wait ended
//(done, timed out, cancelled, the pipe deleted) is written where it asked. The try functions never wait (MPP_FULL,
//MPP_EMPTY instead). Cancelling ends every wait and empties the buffer.
//
//All of that is as pspautotests' threads/msgpipe recorded it on a PSP: its create, send, receive, trysend,
//tryreceive and cancel tests, among them the threads waiting part way (a receiver given half its message by one
//send and timing out with that half, a sender handing its first half straight across and its second into the
//buffer). The checks and their errors are those tests' too: an unknown pipe, a negative size (ILLEGAL_ADDR), a mode
//but 0 or 1 (ILLEGAL_MODE), a message bigger than a buffer the pipe has (ILLEGAL_SIZE; a pipe without one takes any
//size, straight across), and on creating: no name (NO_MEMORY, as recorded), the partition as memory pools check it,
//attributes outside 0x51ff, a buffer the partition hasn't room for. Not shown by them, and chosen: which comes first
//of a bad mode and a bad size; a message or buffer without memory behind all of it refused (ILLEGAL_ADDR), after
//the rest (send's test of a null message with a length is left out of threads/msgpipe, so what a PSP does with one
//isn't known; without the check, a pipe with no buffer would move however many bytes a count asked for); that
//attribute 0x100 lines receivers up by priority and 0x1000 senders (as event flags' 0x100 does waiters: pspsdk's
//pspthreadman.h names no pipe attributes), and 0x4000 takes the buffer from the top of the partition (as memory
//pools').
//
//A mailbox hands over packets: the program's own memory, starting with a SceKernelMsgPacket (pspthreadman.h: the
//next packet's address, which the kernel writes, and a priority byte). A send gives the packet to the first thread
//waiting, or queues it: in the order sent, or by its priority byte (attribute 0x400, the lowest first, equals in the
//order sent); threads wait in the order they came, or by priority (attribute 0x100). As pspautotests'
//threads/mbx recorded, the queued packets are a ring in the program's memory, each one's first word the next's
//address and the last's the first's (a packet alone points at itself), which the kernel keeps written as the queue
//changes; a packet received keeps the address it had. Here the queue itself is the kernel's, so a program writing
//over those words can't lose packets (on a PSP it can, and mbx/send tries it); a packet sent while it's still queued
//is refused (0x800201c9, as mbx/send recorded).

namespace {
  constexpr u32 PipeReceivePriority = 0x100, PipeSendPriority = 0x1000, PipeHighMemory = 0x4000;
  constexpr u32 MailboxThreadPriority = 0x100, MailboxMessagePriority = 0x400;
  constexpr u32 PipeWhole = 0, PipeAsap = 1;  //how a send or receive waits
}

//The threads waiting to send (Wait::PipeSend) or receive on a pipe, in the order they're served: the order they
//came, or by priority as the pipe's attributes say.
auto Kernel::pipeWaiters(const MessagePipe& pipe, Wait wait) -> std::vector<Thread*> {
  std::vector<Thread*> waiters;
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == wait && thread->waitID == pipe.uid) {
      waiters.push_back(thread.get());
    }
  }
  bool byPriority = pipe.attributes & (wait == Wait::PipeSend ? PipeSendPriority : PipeReceivePriority);
  std::sort(waiters.begin(), waiters.end(), [&](Thread* a, Thread* b) {
    if(byPriority && a->priority != b->priority) return a->priority < b->priority;
    return a->readySince < b->readySince;
  });
  return waiters;
}

//Bytes straight from a sender's message into a receiver's buffer: one move where both lie side by side in the host's
//memory, else a byte at a time (through VRAM's copies that rearrange it). Both were found to have memory behind them
//as their send and receive began (pipeFor()), so nothing is made room for, however many bytes go.
auto Kernel::pipeCopy(u32 to, u32 from, u32 bytes) -> void {
  u8* target = memory.pointer(to, bytes);
  u8* source = memory.pointer(from, bytes);
  if(target && source) {
    std::memmove(target, source, bytes);
    return memory.changed(to, bytes);
  }
  for(u32 n = 0; n < bytes; n++) memory.write(1, to + n, memory.read(1, from + n));
}

//Bytes into the pipe's buffer from the program's memory (in), or out of it into the program's memory: the caller
//has checked there's that much room, or that many bytes.
auto Kernel::pipeMove(MessagePipe& pipe, u32 address, u32 bytes, bool in) -> void {
  if(!pipe.size) return;  //(no buffer, so nothing to move: callers ask for 0 bytes then)
  for(u32 n = 0; n < bytes; n++) {
    u32 at = pipe.address + (pipe.start + (in ? pipe.used : 0)) % pipe.size;
    if(in) memory.write(1, at, memory.read(1, address + n)), pipe.used++;
    else memory.write(1, address + n, memory.read(1, at)), pipe.start = (pipe.start + 1) % pipe.size, pipe.used--;
  }
}

//Moves what can move between the pipe's buffer and the threads waiting on it, until nothing more can: the buffer to
//the receivers, in line; the senders straight to the receivers; the senders into the buffer, in line. A thread
//whose send or receive is done, or (ASAP) has moved anything, runs on (ready() writes how many it moved). Called
//whenever something changes: a send, a receive, a waiter leaving.
auto Kernel::pipeServe(MessagePipe& pipe) -> void {
  auto finished = [](Thread* thread) {
    return thread->waitDone == thread->waitCount || (thread->waitMode == PipeAsap && thread->waitDone > 0);
  };
  for(bool moved = true; moved;) {
    moved = false;
    for(auto receiver : pipeWaiters(pipe, Wait::PipeReceive)) {
      if(!pipe.used) break;
      u32 bytes = std::min(pipe.used, receiver->waitCount - receiver->waitDone);
      pipeMove(pipe, receiver->waitPointer + receiver->waitDone, bytes, false);
      receiver->waitDone += bytes;
      moved = moved || bytes;
      if(!finished(receiver)) break;
      ready(*receiver, 0);
    }
    auto receivers = pipeWaiters(pipe, Wait::PipeReceive), senders = pipeWaiters(pipe, Wait::PipeSend);
    if(!receivers.empty() && !senders.empty()) {
      auto receiver = receivers.front(), sender = senders.front();
      u32 bytes = std::min(receiver->waitCount - receiver->waitDone, sender->waitCount - sender->waitDone);
      pipeCopy(receiver->waitPointer + receiver->waitDone, sender->waitPointer + sender->waitDone, bytes);
      receiver->waitDone += bytes, sender->waitDone += bytes;
      if(finished(receiver)) ready(*receiver, 0);
      if(finished(sender)) ready(*sender, 0);
      moved = true;
      continue;
    }
    for(auto sender : senders) {
      u32 left = sender->waitCount - sender->waitDone, room = pipe.size - pipe.used;
      u32 bytes = sender->waitMode == PipeAsap ? std::min(left, room) : left <= room ? left : 0;
      if(!bytes) break;
      pipeMove(pipe, sender->waitPointer + sender->waitDone, bytes, true);
      sender->waitDone += bytes;
      moved = true;
      ready(*sender, 0);
    }
  }
}

//What a send or receive checks first, in order: the size (negative: ILLEGAL_ADDR); for one that would wait, whether
//it may (mayWait(): pspautotests' intr/waits recorded the size refused ahead of that, and an unknown pipe after it);
//the pipe, the mode, a message bigger than the pipe's buffer, then memory behind all of the message or buffer at
//address (ILLEGAL_ADDR): last, so that a message too big for the pipe's buffer is refused as threads/msgpipe recorded
//(0x40000000 bytes: ILLEGAL_SIZE). Null, with the error for the result, if one fails.
auto Kernel::pipeFor(u32 uid, u32 address, u32 size, u32 mode, bool wait) -> MessagePipe* {
  if(size & 0x8000'0000) return result(ErrorIllegalAddress), nullptr;
  if(wait && !mayWait()) return nullptr;
  auto found = pipes.find(uid);
  if(found == pipes.end()) return result(ErrorUnknownMessagePipe), nullptr;
  if(mode > PipeAsap) return result(ErrorIllegalMode), nullptr;
  if(found->second.size && size > found->second.size) return result(ErrorIllegalSize), nullptr;
  if(size && !memory.reaches(address, size)) return result(ErrorIllegalAddress), nullptr;
  return &found->second;
}

//(name, partition, attributes, buffer size, options)
auto Kernel::sceKernelCreateMsgPipe() -> void {
  u32 partition = arg(1), attributes = arg(2), size = arg(3);
  if(!arg(0)) return result(ErrorNoMemory);
  if(partition < 1 || partition > 6) return result(ErrorIllegalArgument);
  if(partition != 2 && partition != 6) return result(ErrorIllegalPermission);
  if(attributes & ~0x51ffu) return result(ErrorIllegalAttribute);
  std::string name = memory.readString(arg(0), 31);
  Block* block = nullptr;
  if(size) {
    block = allocate(size, attributes & PipeHighMemory ? 1 : 0, 0, "msgpipe: " + name);
    if(!block) return result(ErrorNoMemory);
  }
  u32 uid = newUID();
  if(!uid) {
    if(block) release(block->uid);
    return result(ErrorNoMemory);
  }
  auto& pipe = pipes[uid];
  pipe.uid = uid;
  pipe.name = name;
  pipe.attributes = attributes;
  if(block) pipe.block = block->uid, pipe.address = block->address, pipe.size = size;
  result(uid);
}

//(pipe): its waiters are told it's gone (each having moved what it had), and its buffer goes back.
auto Kernel::sceKernelDeleteMsgPipe() -> void {
  auto found = pipes.find(arg(0));
  if(found == pipes.end()) return result(ErrorUnknownMessagePipe);
  for(auto wait : {Wait::PipeSend, Wait::PipeReceive}) {
    for(auto thread : pipeWaiters(found->second, wait)) ready(*thread, ErrorWaitDeleted);
  }
  if(found->second.block) release(found->second.block);
  pipes.erase(found);
  result(0);
  reschedule();
}

//(pipe, message, size, mode, where to put how many bytes went, timeout): the message to the waiting receivers and
//into the buffer, and, if it can't all go (or with ASAP, none of it), a wait for the rest. A sender behind others
//waiting waits behind them.
auto Kernel::pipeSend(bool wait, bool callbacks) -> void {
  u32 message = arg(1), size = arg(2), mode = arg(3), sent = arg(4), timeoutPointer = arg(5);
  auto pipe = pipeFor(arg(0), message, size, mode, wait);
  if(!pipe) return;
  u32 done = 0;
  if(pipeWaiters(*pipe, Wait::PipeSend).empty()) {
    for(auto receiver : pipeWaiters(*pipe, Wait::PipeReceive)) {
      if(done == size) break;
      u32 bytes = std::min(size - done, receiver->waitCount - receiver->waitDone);
      pipeCopy(receiver->waitPointer + receiver->waitDone, message + done, bytes);
      done += bytes, receiver->waitDone += bytes;
      if(receiver->waitDone == receiver->waitCount || (receiver->waitMode == PipeAsap && receiver->waitDone)) {
        ready(*receiver, 0);
      }
    }
    u32 left = size - done, room = pipe->size - pipe->used;
    u32 bytes = mode == PipeAsap ? std::min(left, room) : left <= room ? left : 0;
    pipeMove(*pipe, message + done, bytes, true);
    done += bytes;
  }
  if(done == size || (mode == PipeAsap && done)) {
    if(sent) memory.write(4, sent, done);
    result(0);
    callbacksOnReturn(callbacks);  //the caller's, before a receiver it woke takes over (current would be that one)
    return reschedule();
  }
  if(!wait) {
    if(mode == PipeAsap && sent) memory.write(4, sent, 0);  //as trysend recorded: told nothing went
    return result(ErrorPipeFull);
  }
  if(timeoutPointer && !memory.read(4, timeoutPointer)) {
    if(sent) memory.write(4, sent, done);
    return result(ErrorWaitTimeout);
  }
  result(0);
  current->waitPointer = message;
  current->waitCount = size;
  current->waitMode = mode;
  current->waitDone = done;
  current->waitResult = sent;
  current->readySince = ++readySequence;  //its place in the line
  block(Wait::PipeSend, pipe->uid, timeout(timeoutPointer), timeoutPointer, callbacks);
}

//(pipe, buffer, size, mode, where to put how many bytes came, timeout): from the pipe's buffer, then straight from
//the waiting senders (the buffer then taking in what the next senders have, as it has room), and, if it isn't all
//there (or with ASAP, none of it), a wait for the rest. A receiver behind others waiting waits behind them.
auto Kernel::pipeReceive(bool wait, bool callbacks) -> void {
  u32 buffer = arg(1), size = arg(2), mode = arg(3), got = arg(4), timeoutPointer = arg(5);
  auto pipe = pipeFor(arg(0), buffer, size, mode, wait);
  if(!pipe) return;
  u32 done = 0;
  if(pipeWaiters(*pipe, Wait::PipeReceive).empty()) {
    done = std::min(size, pipe->used);
    pipeMove(*pipe, buffer, done, false);
    for(auto sender : pipeWaiters(*pipe, Wait::PipeSend)) {
      if(done == size) break;
      u32 bytes = std::min(size - done, sender->waitCount - sender->waitDone);
      pipeCopy(buffer + done, sender->waitPointer + sender->waitDone, bytes);
      done += bytes, sender->waitDone += bytes;
      if(sender->waitDone == sender->waitCount || sender->waitMode == PipeAsap) ready(*sender, 0);
    }
    pipeServe(*pipe);
  }
  if(done == size || (mode == PipeAsap && done)) {
    if(got) memory.write(4, got, done);
    result(0);
    callbacksOnReturn(callbacks);  //the caller's, before a sender it woke takes over (current would be that one)
    return reschedule();
  }
  if(!wait) {
    if(mode == PipeAsap && got) memory.write(4, got, 0);
    return result(ErrorPipeEmpty);
  }
  if(timeoutPointer && !memory.read(4, timeoutPointer)) {
    if(got) memory.write(4, got, done);
    return result(ErrorWaitTimeout);
  }
  result(0);
  current->waitPointer = buffer;
  current->waitCount = size;
  current->waitMode = mode;
  current->waitDone = done;
  current->waitResult = got;
  current->readySince = ++readySequence;
  block(Wait::PipeReceive, pipe->uid, timeout(timeoutPointer), timeoutPointer, callbacks);
}

auto Kernel::sceKernelSendMsgPipe() -> void { pipeSend(true, false); }
auto Kernel::sceKernelSendMsgPipeCB() -> void { pipeSend(true, true); }
auto Kernel::sceKernelTrySendMsgPipe() -> void { pipeSend(false, false); }
auto Kernel::sceKernelReceiveMsgPipe() -> void { pipeReceive(true, false); }
auto Kernel::sceKernelReceiveMsgPipeCB() -> void { pipeReceive(true, true); }
auto Kernel::sceKernelTryReceiveMsgPipe() -> void { pipeReceive(false, false); }

//(pipe, where to put how many were waiting to send, and to receive): every wait ends (cancelled), and the buffer is
//emptied.
auto Kernel::sceKernelCancelMsgPipe() -> void {
  auto found = pipes.find(arg(0));
  if(found == pipes.end()) return result(ErrorUnknownMessagePipe);
  auto& pipe = found->second;
  auto senders = pipeWaiters(pipe, Wait::PipeSend), receivers = pipeWaiters(pipe, Wait::PipeReceive);
  for(auto thread : senders) ready(*thread, ErrorWaitCancelled);
  for(auto thread : receivers) ready(*thread, ErrorWaitCancelled);
  if(arg(1)) memory.write(4, arg(1), senders.size());
  if(arg(2)) memory.write(4, arg(2), receivers.size());
  pipe.start = pipe.used = 0;
  result(0);
  reschedule();
}

//(pipe, info): a SceKernelMppInfo (pspthreadman.h) as far as the size in its first word: name, attributes, buffer
//size, free bytes, how many wait to send and to receive.
auto Kernel::sceKernelReferMsgPipeStatus() -> void {
  auto found = pipes.find(arg(0));
  if(found == pipes.end()) return result(ErrorUnknownMessagePipe);
  auto& pipe = found->second;
  u32 info = arg(1), size = memory.read(4, info);
  for(u32 offset = 4; offset < 36 && offset < size; offset++) {
    memory.write(1, info + offset, offset - 4 < pipe.name.size() ? u8(pipe.name[offset - 4]) : 0);
  }
  u32 words[] = {pipe.attributes, pipe.size, pipe.size - pipe.used, u32(pipeWaiters(pipe, Wait::PipeSend).size()),
                 u32(pipeWaiters(pipe, Wait::PipeReceive).size())};
  for(u32 n = 0; n < 5; n++) if(36 + n * 4 + 4 <= size) memory.write(4, info + 36 + n * 4, words[n]);
  result(0);
}

//The threads waiting on a mailbox, in the order they're given packets.
auto Kernel::mailboxWaiters(const Mailbox& mailbox) -> std::vector<Thread*> {
  std::vector<Thread*> waiters;
  for(auto& [uid, thread] : threads) {
    if(thread->status == Status::Waiting && thread->wait == Wait::Mailbox && thread->waitID == mailbox.uid) {
      waiters.push_back(thread.get());
    }
  }
  bool byPriority = mailbox.attributes & MailboxThreadPriority;
  std::sort(waiters.begin(), waiters.end(), [&](Thread* a, Thread* b) {
    if(byPriority && a->priority != b->priority) return a->priority < b->priority;
    return a->readySince < b->readySince;
  });
  return waiters;
}

//The queued packets' first words written as the PSP keeps them: each the next's address, the last's the first's.
auto Kernel::mailboxLink(const Mailbox& mailbox) -> void {
  for(u32 n = 0; n < mailbox.messages.size(); n++) {
    memory.write(4, mailbox.messages[n], mailbox.messages[(n + 1) % mailbox.messages.size()]);
  }
}

//(name, attributes, options): no name is ERROR, and attributes outside 0x5ff ILLEGAL_ATTR, as threads/mbx/create
//recorded.
auto Kernel::sceKernelCreateMbx() -> void {
  if(!arg(0)) return result(ErrorError);
  if(arg(1) & ~0x5ffu) return result(ErrorIllegalAttribute);
  u32 uid = newUID();
  if(!uid) return result(ErrorNoMemory);
  auto& mailbox = mailboxes[uid];
  mailbox.uid = uid;
  mailbox.name = memory.readString(arg(0), 31);
  mailbox.attributes = arg(1);
  result(uid);
}

//(mailbox): its waiters are told it's gone; the packets queued stay as they are in the program's memory.
auto Kernel::sceKernelDeleteMbx() -> void {
  auto found = mailboxes.find(arg(0));
  if(found == mailboxes.end()) return result(ErrorUnknownMailbox);
  for(auto thread : mailboxWaiters(found->second)) ready(*thread, ErrorWaitDeleted);
  mailboxes.erase(found);
  result(0);
  reschedule();
}

//(mailbox, packet): to the first thread waiting, or queued (by its priority byte, at 4, with attribute 0x400).
auto Kernel::sceKernelSendMbx() -> void {
  auto found = mailboxes.find(arg(0));
  if(found == mailboxes.end()) return result(ErrorUnknownMailbox);
  auto& mailbox = found->second;
  u32 packet = arg(1);
  if(!memory.reaches(packet, 8)) return result(ErrorIllegalAddress);
  if(std::count(mailbox.messages.begin(), mailbox.messages.end(), packet)) return result(ErrorMessageQueued);
  result(0);
  if(auto waiters = mailboxWaiters(mailbox); !waiters.empty()) {
    auto thread = waiters.front();
    if(thread->waitPointer) memory.write(4, thread->waitPointer, packet);
    ready(*thread, 0);
    return reschedule();
  }
  auto at = mailbox.messages.end();
  if(mailbox.attributes & MailboxMessagePriority) {
    u8 priority = memory.read(1, packet + 4);
    at = std::find_if(mailbox.messages.begin(), mailbox.messages.end(), [&](u32 queued) {
      return memory.read(1, queued + 4) > priority;
    });
  }
  mailbox.messages.insert(at, packet);
  mailboxLink(mailbox);
}

//(mailbox, where to put the packet's address, timeout): the first packet queued, or a wait for one.
auto Kernel::mailboxReceive(bool callbacks) -> void {
  if(!mayWait()) return;
  auto found = mailboxes.find(arg(0));
  if(found == mailboxes.end()) return result(ErrorUnknownMailbox);
  auto& mailbox = found->second;
  u32 pointer = arg(1), timeoutPointer = arg(2);
  if(!mailbox.messages.empty()) {
    if(pointer) memory.write(4, pointer, mailbox.messages.front());
    mailbox.messages.pop_front();
    mailboxLink(mailbox);
    result(0);
    return callbacksOnReturn(callbacks);
  }
  if(timeoutPointer && !memory.read(4, timeoutPointer)) return result(ErrorWaitTimeout);
  result(0);
  current->waitPointer = pointer;
  current->readySince = ++readySequence;
  block(Wait::Mailbox, mailbox.uid, timeout(timeoutPointer), timeoutPointer, callbacks);
}

auto Kernel::sceKernelReceiveMbx() -> void { mailboxReceive(false); }
auto Kernel::sceKernelReceiveMbxCB() -> void { mailboxReceive(true); }

//(mailbox, where to put the packet's address): the first packet queued, or MBOX_NOMSG.
auto Kernel::sceKernelPollMbx() -> void {
  auto found = mailboxes.find(arg(0));
  if(found == mailboxes.end()) return result(ErrorUnknownMailbox);
  auto& mailbox = found->second;
  if(mailbox.messages.empty()) return result(ErrorMailboxEmpty);
  if(arg(1)) memory.write(4, arg(1), mailbox.messages.front());
  mailbox.messages.pop_front();
  mailboxLink(mailbox);
  result(0);
}

//(mailbox, where to put how many were waiting): every wait ends, cancelled; the packets stay queued.
auto Kernel::sceKernelCancelReceiveMbx() -> void {
  auto found = mailboxes.find(arg(0));
  if(found == mailboxes.end()) return result(ErrorUnknownMailbox);
  auto waiters = mailboxWaiters(found->second);
  for(auto thread : waiters) ready(*thread, ErrorWaitCancelled);
  if(arg(1)) memory.write(4, arg(1), waiters.size());
  result(0);
  reschedule();
}

//(mailbox, info): a SceKernelMbxInfo (pspthreadman.h): its size (52) written over the program's unless that's 0, as
//threads/mbx/refer recorded, then as far as the program's size the name, attributes, how many wait, how many
//packets are queued and the first.
auto Kernel::sceKernelReferMbxStatus() -> void {
  auto found = mailboxes.find(arg(0));
  if(found == mailboxes.end()) return result(ErrorUnknownMailbox);
  auto& mailbox = found->second;
  u32 info = arg(1), size = memory.read(4, info);
  if(!size) return result(0);
  memory.write(4, info, 52);
  for(u32 offset = 4; offset < 36 && offset < size; offset++) {
    memory.write(1, info + offset, offset - 4 < mailbox.name.size() ? u8(mailbox.name[offset - 4]) : 0);
  }
  u32 words[] = {mailbox.attributes, u32(mailboxWaiters(mailbox).size()), u32(mailbox.messages.size()),
                 mailbox.messages.empty() ? 0 : mailbox.messages.front()};
  for(u32 n = 0; n < 4; n++) if(36 + n * 4 + 4 <= size) memory.write(4, info + 36 + n * 4, words[n]);
  result(0);
}
