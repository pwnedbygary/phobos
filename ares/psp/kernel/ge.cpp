//The GE driver (sceGe_user): what the PSP's operating system offers programs for the graphics chip (ge/ge.hpp). A
//program hands it display lists, and the driver queues them, gives each to the GE in turn, and deals with whatever
//makes one stop:
//  - the stall address: the program hasn't written further yet. When it moves the address on
//    (sceGeListUpdateStallAddr), the GE carries on.
//  - FINISH then END: the list is done. The program's finish callback runs, and the next list starts.
//  - SIGNAL then END: the driver does what the signal's kind (its bits 16-23) asks: call the program's signal
//    callback, jump, call or return elsewhere in memory, pause... and the GE goes on.
//Callbacks come in pairs, signal and finish, each with an argument, registered with sceGeSetCallback (16 at most).
//They're called as interrupt handlers (interrupts.cpp) with the FINISH's or SIGNAL's low 16 bits, the argument, and
//where the list had got to.
//
//A program sees how its lists are doing with sceGeListSync and sceGeDrawSync, and can wait there for them to finish.
//The GE's work takes no time, so a list runs as far as it can as soon as it's queued or its stall address moves.
//
//What each call does follows uOFW's reading of the PSP's own driver (ge.c, stall.S). Not yet: sceGeBreak, the
//debugger's breakpoints, SIGNALs that patch texture or CLUT addresses, and what uOFW shows differs for programs built
//with SDKs before 2.0; a list may be queued twice, as for programs that don't say their SDK's version.

auto Kernel::geIndex(u32 id) const -> s32 {
  u32 index = id - GeListIDs;
  return index < 64 ? s32(index) : -1;
}

//The GE takes up a list where it left off (or at its start).
auto Kernel::geLoad(GeList& list) -> void {
  ge.list = list.registers;
  geRestoreBase(list.base);
}

//BASE's word as the driver saved it, put back the way the PSP's driver does it: by having the GE run the word as a
//command (uOFW's _sceGeSetInternalReg). A word of 0, a list that never ran, is a NOP: BASE stays as the last list
//left it.
auto Kernel::geRestoreBase(u32 word) -> void {
  if(word >> 24 == GE::Base) ge.commands[GE::Base] = word;
}

auto Kernel::geStalled() const -> bool {
  return ge.list.stall && ge.list.address == ge.list.stall;
}

//The GE runs its list as far as it goes: to the stall address, or through FINISHes and SIGNALs (dealt with as the
//driver does) to the end of the queue. A list that runs a million commands without stopping is left to go on as the
//CPU runs (Kernel::run()). One the GE gave up on (a bare END, a fault, a SIGNAL the driver can't do) stays in the
//queue as running, and whoever waits for it waits on: the PSP's driver never hears the end of such a list either.
auto Kernel::geRun() -> void {
  geBusy = false;
  while(geRunning >= 0 && !geSuspended) {
    switch(ge.run(GeBudget)) {
    case GE::Stop::Stalled:  return;
    case GE::Stop::Busy:     geBusy = true; return;
    case GE::Stop::Finished: geFinished(); break;
    case GE::Stop::Signaled: geSignaled(); break;
    case GE::Stop::Ended:
      note("a display list ENDed without a FINISH or SIGNAL before it: the GE stopped there");
      geRunning = -1;
      return;
    case GE::Stop::Faulted:  //the GE said why
      geRunning = -1;
      return;
    }
  }
}

//A list's callback: its finish or signal function, with id, its argument and where the list had got to. With
//suspends, the GE waits until it returns. Returns whether there was one to call.
auto Kernel::geCall(GeList& list, bool finish, u32 id, bool suspends) -> bool {
  if(list.callback < 0 || list.callback >= 16 || !geCallbacks[list.callback].used) return false;
  auto& callback = geCallbacks[list.callback];
  u32 function = finish ? callback.finishFunction : callback.signalFunction;
  if(!function) return false;
  if(suspends) geSuspended = true;
  queueCall(function, callback.gp, id, finish ? callback.finishArgument : callback.signalArgument, ge.list.address,
            suspends);
  return true;
}

//Out of the queue and free for the next list; whoever waits for it to finish is told it has.
auto Kernel::geRemove(u32 index) -> void {
  geQueue.erase(std::find(geQueue.begin(), geQueue.end(), index));
  geFree.push_back(index);
  geWake(Wait::GeList, index);
}

auto Kernel::geWake(Wait wait, u32 index) -> void {
  bool woke = false;
  for(auto& [uid, thread] : threads) {
    if(thread->status != Status::Waiting || thread->wait != wait || thread->waitID != index) continue;
    ready(*thread, 0);
    woke = true;
  }
  if(wait == Wait::GeDraw && woke) geDrawSynced();
  if(woke) reschedule();
}

//sceGeDrawSync's return: the lists that finished are forgotten.
auto Kernel::geDrawSynced() -> void {
  for(auto& list : geLists) {
    if(list.state != GeList::State::Completed) continue;
    list.state = GeList::State::None;
    list.started = false;
  }
}

//An END after a FINISH (uOFW's _sceGeFinishInterrupt). After a SYNC signal it only lets the GE go on; after a PAUSE
//signal the list stops here, paused until sceGeContinue, and its signal callback is told. Otherwise the list is done:
//its finish callback runs while the GE waits (so it can change what the next list will meet); then geEnded().
auto Kernel::geFinished() -> void {
  u32 index = geRunning;
  auto& list = geLists[index];
  if(list.syncing) {
    list.syncing = false;
    return;
  }
  if(list.pausing) {
    list.pausing = false;
    list.registers = ge.list;
    list.base = ge.commands[GE::Base];
    geRunning = -1;
    geCall(list, false, list.signalID);
    return;
  }
  list.state = GeList::State::Completed;
  if(!list.stack.empty()) note("a display list finished inside a SIGNAL call");
  geFinishing = index;
  if(!geCall(list, true, ge.finishWord & 0xffff, true)) geEnded();
}

//The rest of a list's ending, once its finish callback has returned: the GE's state is put back if it was saved,
//whoever waits for the list is told, and the next list starts.
auto Kernel::geEnded() -> void {
  u32 index = geFinishing;
  geFinishing = -1;
  if(geLists[index].context) geRestoreContext(geLists[index].context);
  geRemove(index);
  geStartNext();
}

//The GE takes the next list in the queue, unless that one's paused; with none left, whoever waits for all drawing to
//finish is told it has.
auto Kernel::geStartNext() -> void {
  geRunning = -1;
  if(geQueue.empty()) return geWake(Wait::GeDraw, 0);
  u32 index = geQueue.front();
  auto& next = geLists[index];
  if(next.state == GeList::State::Paused) return;
  next.state = GeList::State::Running;
  if(next.context && !next.started) geSaveContext(next.context);
  next.started = true;
  geLoad(next);
  geRunning = index;
}

//An END after a SIGNAL (uOFW's _sceGeListInterrupt). The SIGNAL's bits 16-23 are its kind. Jumps and calls take
//their address from the SIGNAL's low 16 bits (the top half) and the END's (the bottom half): as it is, from the
//SIGNAL's own address, or from the offset (ORIGIN, OFFSET_ADDR). Calls keep where the list was on its own stack, as
//deep as it allows, and the list continues there at a return.
auto Kernel::geSignaled() -> void {
  auto& list = geLists[geRunning];
  u32 signal = ge.signalWord, end = ge.endWord, kind = signal >> 16 & 0xff;
  u32 data = (signal & 0xffff) << 16 | (end & 0xffff);
  u32 at = (ge.list.address - 8) & 0x0fff'ffff;  //the SIGNAL's own address
  auto target = [&](u32 relativeKind, u32 originKind) {
    u32 address = kind == relativeKind ? at + data : kind == originKind ? ge.list.offset + data : data;
    if(address & 3) note("a SIGNAL jumped or called to an address not on a word");
    return address & 0x0fff'fffc;
  };
  switch(kind) {
  case 0x01:  //the callback runs, then the GE goes on
    geCall(list, false, signal & 0xffff, true);
    return;
  case 0x02:  //the GE goes on, and the callback runs
    geCall(list, false, signal & 0xffff);
    return;
  case 0x03:  //paused at the next FINISH
    list.state = GeList::State::Paused;
    list.pausing = true;
    list.signalID = signal & 0xffff;  //the PSP's driver keeps 16 bits of it
    return;
  case 0x08:  //the next FINISH doesn't end the list
    list.syncing = true;
    return;
  case 0x10: case 0x13: case 0x15:  //jump
    ge.list.address = target(0x13, 0x15);
    return;
  case 0x11: case 0x14: case 0x16:  //call
    if(list.stack.size() >= list.stackLimit) {
      note("a SIGNAL called deeper than its list allows: the GE stopped there");
      geRunning = -1;
      return;
    }
    list.stack.push_back({ge.list, ge.commands[GE::Base]});
    ge.list.address = target(0x14, 0x16);
    ge.list.depth = 0;  //the called list starts with the GE's own CALLs unused
    return;
  case 0x12:  //return
    if(list.stack.empty()) {
      note("a SIGNAL returned without a SIGNAL call to return from: the GE stopped there");
      geRunning = -1;
      return;
    }
    {
      u32 stall = ge.list.stall;
      ge.list = list.stack.back().registers;
      ge.list.stall = stall;
      geRestoreBase(list.stack.back().base);
      list.stack.pop_back();
    }
    return;
  case 0xf0: case 0xff:  //breakpoints, for the PSP's debugger
    return;
  }
  if(kind >= 0x20 && kind <= 0x38) return note("SIGNALs that patch texture or CLUT addresses aren't emulated yet");
  note("a SIGNAL of a kind the driver doesn't know: the GE stopped there");
  geRunning = -1;
}

//sceGeSaveContext's buffer (2 KiB of the program's): Phobos's own layout, which programs don't look inside. Each
//command's last word, the matrices and where their next element goes, the list registers, and the vertex and index
//addresses.
auto Kernel::geSaveContext(u32 address) -> void {
  u32 at = address;
  auto put = [&](u32 value) { memory.write(4, at, value); at += 4; };
  for(u32 word : ge.commands) put(word);
  for(u32 element : ge.bones) put(element);
  for(u32 element : ge.world) put(element);
  for(u32 element : ge.view) put(element);
  for(u32 element : ge.projection) put(element);
  for(u32 element : ge.textureMatrix) put(element);
  for(u32 index : {ge.boneIndex, ge.worldIndex, ge.viewIndex, ge.projectionIndex, ge.textureIndex}) put(index);
  put(ge.list.address); put(ge.list.stall); put(ge.list.offset); put(ge.list.depth);
  for(u32 n : {0, 1}) put(ge.list.returnAddress[n]), put(ge.list.returnOffset[n]);
  put(ge.vertexAddress); put(ge.indexAddress);
}

auto Kernel::geRestoreContext(u32 address) -> void {
  u32 at = address;
  auto get = [&]() { u32 value = memory.read(4, at); at += 4; return value; };
  for(u32& word : ge.commands) word = get();
  for(u32& element : ge.bones) element = get();
  for(u32& element : ge.world) element = get();
  for(u32& element : ge.view) element = get();
  for(u32& element : ge.projection) element = get();
  for(u32& element : ge.textureMatrix) element = get();
  for(u32* index : {&ge.boneIndex, &ge.worldIndex, &ge.viewIndex, &ge.projectionIndex, &ge.textureIndex}) *index = get();
  ge.list.address = get(); ge.list.stall = get(); ge.list.offset = get(); ge.list.depth = get() & 3;
  for(u32 n : {0, 1}) ge.list.returnAddress[n] = get(), ge.list.returnOffset[n] = get();
  ge.vertexAddress = get(); ge.indexAddress = get();
}

//The GE's memory: VRAM, 2 MiB at 0x04000000.
auto Kernel::sceGeEdramGetAddr() -> void { result(Memory::VRAMBase); }
auto Kernel::sceGeEdramGetSize() -> void { result(Memory::VRAMSize); }

//(list, stall address (0: none), callbacks (sceGeSetCallback's number; negative for none), options): the list goes at
//the queue's end, and the GE starts on it at once if it's free. Returns the list's ID. The options (PspGeListArgs, if
//given): their size, where to save the GE's state, and how deep SIGNAL calls may go (32 if not said).
auto Kernel::sceGeListEnQueue() -> void { geEnqueue(false); }

//As above, but at the queue's front, paused: it runs when the program calls sceGeContinue. Only while the front list
//is paused itself, or there's none, so as not to cut in on a list the GE has.
auto Kernel::sceGeListEnQueueHead() -> void { geEnqueue(true); }

auto Kernel::geEnqueue(bool head) -> void {
  u32 address = arg(0), stall = arg(1), options = arg(3), context = 0, stackLimit = 32;
  if(options) {
    if(options & 3) return result(ErrorInvalidPointer);
    context = memory.read(4, options + 4);
    if(memory.read(4, options) >= 16) {
      stackLimit = memory.read(4, options + 8);
      if(stackLimit >= 256) return result(ErrorInvalidSize);
    }
  }
  if((address | stall | context) & 3) return result(ErrorInvalidPointer);
  if(geFree.empty()) return result(ErrorOutOfMemory);
  if(head && !geQueue.empty() && geLists[geQueue.front()].state != GeList::State::Paused) {
    return result(ErrorInvalidValue);
  }
  u32 index = geFree.front();
  geFree.pop_front();
  auto& list = geLists[index];
  list = {};
  list.registers.address = address & 0x0fff'ffff;
  list.registers.stall = stall & 0x0fff'ffff;
  list.callback = std::max(s32(arg(2)), -1);
  list.context = context;
  list.stackLimit = stackLimit;
  result(GeListIDs + index);
  if(head) {  //the paused list goes back to queued: it runs once the new one is done, without a sceGeContinue
    if(!geQueue.empty()) geLists[geQueue.front()].state = GeList::State::Queued;
    list.state = GeList::State::Paused;
    geQueue.push_front(index);
    return;
  }
  geQueue.push_back(index);
  if(geQueue.size() > 1) {
    list.state = GeList::State::Queued;
    return;
  }
  list.state = GeList::State::Running;
  list.started = true;
  if(context) geSaveContext(context);
  ge.list = list.registers;  //BASE stays as it was
  geRunning = index;
  geRun();
}

//Takes a list out of the queue, unless the GE has started it.
auto Kernel::sceGeListDeQueue() -> void {
  s32 index = geIndex(arg(0));
  if(index < 0 || geLists[index].state == GeList::State::None) return result(ErrorInvalidID);
  auto& list = geLists[index];
  if(list.started) return result(ErrorBusy);
  list.state = GeList::State::None;
  geRemove(index);
  result(0);
}

//(list, stall address): the GE may go as far as the new address. A list that has finished can't be moved on.
auto Kernel::sceGeListUpdateStallAddr() -> void {
  s32 index = geIndex(arg(0));
  if(index < 0) return result(ErrorInvalidID);
  auto& list = geLists[index];
  u32 stall = arg(1) & 0x0fff'ffff;
  switch(list.state) {
  case GeList::State::Running:
    list.registers.stall = stall;
    if(geRunning == index) ge.list.stall = stall;
    result(0);
    return geRun();
  case GeList::State::Queued:
  case GeList::State::Paused:
    list.registers.stall = stall;
    return result(0);
  case GeList::State::Completed:
    return result(ErrorAlready);
  default:
    return result(ErrorInvalidID);
  }
}

//(list, mode): 0 waits for the list to finish; 1 says how it's doing: 0 finished, 1 queued, 2 drawing, 3 stopped at
//its stall address, 4 paused (or queued again after running).
auto Kernel::sceGeListSync() -> void {
  s32 index = geIndex(arg(0));
  if(index < 0) return result(ErrorInvalidID);
  auto& list = geLists[index];
  using State = GeList::State;
  if(arg(1) == 0) {
    if(!mayWait()) return;
    result(0);
    if(list.state == State::Queued || list.state == State::Running || list.state == State::Paused) {
      block(Wait::GeList, index, 0);
    }
    return;
  }
  if(arg(1) != 1) return result(ErrorInvalidMode);
  switch(list.state) {
  case State::Queued:    return result(list.started ? 4 : 1);
  case State::Running:   return result(geRunning == index && geStalled() ? 3 : 2);
  case State::Completed: return result(0);
  case State::Paused:    return result(4);
  default:               return result(ErrorInvalidID);
  }
}

//(mode): 0 waits for every list to finish, then forgets the finished ones; 1 says how the GE is doing: 0 nothing to
//do, 2 drawing, 3 stopped at a stall address.
auto Kernel::sceGeDrawSync() -> void {
  if(arg(0) == 0) {
    if(!mayWait()) return;
    result(0);
    if(geQueue.empty()) return geDrawSynced();
    return block(Wait::GeDraw, 0, 0);
  }
  if(arg(0) != 1) return result(ErrorInvalidMode);
  if(geQueue.empty()) return result(0);
  result(geRunning == s32(geQueue.front()) && geStalled() ? 3 : 2);
}

//(PspGeCallbackData: signal function and argument, finish function and argument): the callbacks' number, for
//enqueueing lists with. They run with the global pointer the program had here.
auto Kernel::sceGeSetCallback() -> void {
  u32 data = arg(0);
  for(u32 id = 0; id < 16; id++) {
    if(geCallbacks[id].used) continue;
    geCallbacks[id] = {true, memory.read(4, data), memory.read(4, data + 4), memory.read(4, data + 8),
                       memory.read(4, data + 12), cpu.ipu.r[28]};
    return result(id);
  }
  result(ErrorOutOfMemory);
}

auto Kernel::sceGeUnsetCallback() -> void {
  if(arg(0) >= 16) return result(ErrorInvalidID);
  geCallbacks[arg(0)] = {};
  result(0);
}

//Lets the front list go on if it's paused, unless its PAUSE signal is still on its way to its FINISH.
auto Kernel::sceGeContinue() -> void {
  if(geQueue.empty()) return result(0);
  u32 index = geQueue.front();
  auto& list = geLists[index];
  if(list.state == GeList::State::Running) return result(ErrorAlready);
  if(list.state != GeList::State::Paused) return result(ErrorNotSupported);
  if(list.pausing) return result(ErrorBusy);
  list.state = GeList::State::Running;
  if(list.context && !list.started) geSaveContext(list.context);
  list.started = true;
  geLoad(list);
  geRunning = index;
  result(0);
  geRun();
}

//(command): its last word. 0xff is refused, as the PSP's driver does.
auto Kernel::sceGeGetCmd() -> void {
  if(arg(0) >= 0xff) return result(ErrorInvalidIndex);
  result(ge.commands[arg(0)]);
}

//(which: 0-7 the bones, 8 world, 9 view, 10 projection, 11 texture; where): its elements as the GE holds them, each a
//float's top 24 bits.
auto Kernel::sceGeGetMtx() -> void {
  u32 which = arg(0), to = arg(1);
  if(which >= 12) return result(ErrorInvalidIndex);
  const u32* matrix = which < 8 ? ge.bones + which * 12 : which == 8 ? ge.world : which == 9 ? ge.view
                    : which == 10 ? ge.projection : ge.textureMatrix;
  u32 size = which == 10 ? 16 : 12;
  for(u32 n = 0; n < size; n++) memory.write(4, to + n * 4, matrix[n]);
  result(0);
}

//Not while the GE runs a list: -1 then, as the PSP's driver says.
auto Kernel::sceGeSaveContext() -> void {
  if(arg(0) & 3) return result(ErrorInvalidPointer);
  if(geRunning >= 0) return result(0xffff'ffff);
  geSaveContext(arg(0));
  result(0);
}

auto Kernel::sceGeRestoreContext() -> void {
  if(arg(0) & 3) return result(ErrorInvalidPointer);
  if(geRunning >= 0) return result(ErrorBusy);
  geRestoreContext(arg(0));
  result(0);
}
