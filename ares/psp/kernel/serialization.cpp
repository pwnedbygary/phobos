//Saving and loading the kernel, for save states: the program (its module, the modules it loaded, and its imports in
//the order its syscall codes count them), its threads (each one's registers while it isn't running, and what it
//waits for), the semaphores, mutexes, event flags and callbacks, the memory handed out, sound output's channels, the
//open files and folders, the controller's samples, the display, the calls into the program and the interrupt
//handlers, the GE driver's lists, and the clock.
//
//Open files are saved by their PSP paths: on loading, a host file is opened again where the devices are then, at
//the position it had (one that's gone since is dropped, and the program's next use of it fails as for any bad file).
//Not saved: the devices and the disc in the drive (the system gives them when it powers on: the same game's), and
//the table of functions (the kernel's own).
//
//Loading checks what it reads. Only a machine like this one makes states, so a value none could have (a position
//past the end of what it's a position in, a list the GE driver can't have queued, a display mode there isn't, a clock
//that would take hours to catch up, a memory pool that doesn't hold together, a block of memory with two owners)
//means the state is damaged, and it's refused. Lists have no limits of their own: their items are read one at a
//time, and a list that claims more than the rest of the state holds runs out of state part way, and is refused then.
//Nothing is made room for in advance, so however damaged a count, what loading takes in memory stays a small
//multiple of the state's own size (an item takes a few times its bytes in the state).

//Text: its length, then its characters. A length past the rest of the state can't be real.
static auto serializeText(serializer& s, u32 end, std::string& text, bool& valid) -> void {
  u32 size = text.size();
  s(size);
  if(s.reading()) {
    if(size > end - std::min(end, s.size())) { valid = false; size = 0; }
    text.resize(size);
  }
  for(auto& c : text) {
    u8 byte = c;
    s(byte);
    c = byte;
  }
}

//A host folder's names as listing it makes them (sceIoDopen()): "." and ".." first, but not at a device's top (which
//has no parent there to go up to), then single names a PSP path can name, with no '/', '\', ':' or NUL in them.
//Reading the folder joins each name to the folder's place on the host, so a path, or ".." at a device's top, would
//reach outside the device's folder.
static auto listed(const std::vector<std::string>& names, const std::string& normalized) -> bool {
  u32 first = 0;
  if(normalized.empty() || normalized.back() != '/') {
    if(names.size() < 2 || names[0] != "." || names[1] != "..") return false;
    first = 2;
  }
  for(u32 n = first; n < names.size(); n++) {
    auto& name = names[n];
    if(name.empty() || name == "." || name == "..") return false;
    if(name.find_first_of(std::string{"/\\:\0", 4}) != std::string::npos) return false;
  }
  return true;
}

//A pool as only making and using it leaves it, which what it hands out relies on: a fixed pool's blocks, of its block
//size (never 0: giving a block back divides by it), adding up to its size, with no pieces; a variable pool with no
//block size and no blocks; either inside the block of the user partition it was made from, which must be there; a
//variable pool's pieces inside it, none empty, each after the last (handing out room trusts the gaps between them).
static auto poolHolds(const Kernel::Pool& pool, const std::vector<Kernel::Block>& blocks) -> bool {
  if(pool.variable ? pool.blockSize || !pool.used.empty()
     : !pool.blockSize || !pool.pieces.empty() || u64(pool.used.size()) * pool.blockSize != pool.size) return false;
  auto block = std::find_if(blocks.begin(), blocks.end(), [&](auto& b) { return b.uid == pool.block; });
  u64 end = u64(pool.address) + pool.size;
  if(block == blocks.end() || pool.address < block->address || end > u64(block->address) + block->size) return false;
  u64 at = pool.address;
  for(auto& [start, length] : pool.pieces) {
    if(start < at || !length || start + u64(length) > end) return false;
    at = start + u64(length);
  }
  return true;
}

auto Kernel::serialize(serializer& s) -> bool {
  bool valid = true;
  //where the state ends: what nall's serializer reads past it is zeros, not the state's
  u32 end = s.capacity();
  auto check = [&](bool good) { if(s.reading() && !good) valid = false; };
  auto text = [&](std::string& value) { serializeText(s, end, value, valid); };
  auto context = [&](Context& c) {
    s(c.gpr); s(c.lo); s(c.hi); s(c.pc); s(c.pd);
    s(c.fpr); s(c.fcsr);
    s(c.vpr); s(c.pfxs); s(c.pfxt); s(c.pfxd); s(c.cc);
  };
  //a list: its count, then each item through each(); loading stops where the state does
  auto vector = [&](auto& items, auto&& each) {
    u32 count = items.size();
    s(count);
    if(s.writing()) {
      for(auto& item : items) each(item);
      return;
    }
    items.clear();
    for(u32 n = 0; n < count && valid; n++) {
      each(items.emplace_back());
      check(s.size() <= end);
    }
  };
  //a map by u32 keys: its count, then each key and its value through each()
  auto map = [&](auto& items, auto&& each) {
    u32 count = items.size();
    s(count);
    if(s.writing()) {
      for(auto& [key, value] : items) {
        u32 at = key;
        s(at);
        each(value);
      }
      return;
    }
    items.clear();
    for(u32 n = 0; n < count && valid; n++) {
      u32 at = 0;
      s(at);
      each(items[at]);
      check(s.size() <= end);
    }
  };

  //the program, and the modules it loaded (modules.cpp)
  auto moduleFields = [&](Module& m) {
    text(m.name);
    s(m.attributes);
    s(m.version);
    s(m.relocatable);
    s(m.base);
    s(m.entry);
    s(m.gp);
    s(m.moduleInfo);
    vector(m.segments, [&](Module::Segment& segment) { s(segment.address); s(segment.size); });
    vector(m.imports, [&](Module::Import& i) { text(i.library); s(i.nid); s(i.stub); });
    vector(m.exports, [&](Module::Export& e) { text(e.library); s(e.nid); s(e.address); s(e.variable); });
    vector(m.skipped, [&](std::string& skipped) { text(skipped); });
  };
  moduleFields(module);
  s(programUID);
  map(modules, [&](LoadedModule& m) {
    s(m.uid); text(m.path); s(m.standIn); s(m.block); s(m.status); s(m.thread);
    moduleFields(m.module);
    check(m.status <= ModuleStatus::Unloading);
  });
  //imports: the syscall codes in the program's memory count them in this order
  vector(imports, [&](Import& i) {
    text(i.library);
    s(i.nid);
    s(i.reported);
    if(s.reading()) {
      i.function = nullptr;
      for(auto& candidate : functions) if(candidate.nid == i.nid) i.function = &candidate;
    }
  });
  s(exited);
  s(cycles);
  s(nextUID);
  s(startTime);
  check(nextUID >= 0x100 && nextUID <= LastUID + 1);  //IDs count up from 0x100, and stop short of 2^31 (newUID())

  //threads, and the one running (which must be one of them)
  u32 count = threads.size();
  s(count);
  auto thread = [&](Thread& t) {
    s(t.uid);
    text(t.name);
    s(t.entry); s(t.priority); s(t.initialPriority); s(t.stackSize); s(t.stackBlock); s(t.attributes); s(t.gp);
    s(t.status);
    context(t.context);
    s(t.wait); s(t.waitID); s(t.waitCount); s(t.waitMode); s(t.waitPointer);
    s(t.wakeAt); s(t.timeoutPointer); s(t.readySince); s(t.exitStatus); s(t.wakeupCount);
    s(t.callbacks); s(t.inCallback); s(t.callbackID);
    context(t.beforeCallback);
    auto& w = t.waitBeforeCallback;
    s(w.wait); s(w.id); s(w.count); s(w.mode); s(w.pointer); s(w.timeoutPointer); s(w.wakeAt); s(w.callbacks);
    check(t.callbackID < nextUID);
    s(t.suspended);
    //a wait to read the controller is for fewer than 64 samples (readController()), the top bit saying which kind
    check(t.wait != Wait::Controller || (t.waitCount & 0x7fff'ffff) < 64);
  };
  if(s.writing()) {
    for(auto& [uid, t] : threads) thread(*t);
  } else {
    threads.clear();
    current = nullptr;  //it was one of them
    for(u32 n = 0; n < count && valid; n++) {
      auto t = std::make_unique<Thread>();
      thread(*t);
      check(s.size() <= end);
      u32 uid = t->uid;
      threads[uid] = std::move(t);
    }
  }
  u32 running = current ? current->uid : 0;
  s(running);
  if(s.reading()) current = running ? findThread(running) : nullptr;
  check(!running || current);
  s(readySequence);
  s(nextVblank);
  s(vblanks);
  //the clock: short of a century's cycles (2^60), with the next vertical blank no more than a frame ahead, nor a
  //frame behind (events() catches up a frame at a time)
  check(cycles < 1ull << 60);
  check(nextVblank > cycles ? nextVblank - cycles <= VblankCycles : cycles - nextVblank < VblankCycles);

  //the kernel's objects
  map(semaphores, [&](Semaphore& semaphore) {
    s(semaphore.uid); text(semaphore.name); s(semaphore.attributes); s(semaphore.count); s(semaphore.maximum);
    s(semaphore.initial);
  });
  map(lwMutexes, [&](u32& workArea) { s(workArea); });
  map(pools, [&](Pool& pool) {
    s(pool.uid); text(pool.name); s(pool.attributes); s(pool.variable); s(pool.block); s(pool.address);
    s(pool.size); s(pool.blockSize);
    vector(pool.used, [&](u8& used) { s(used); });
    map(pool.pieces, [&](u32& length) { s(length); });
  });
  map(eventFlags, [&](EventFlag& flag) {
    s(flag.uid); text(flag.name); s(flag.attributes); s(flag.initial); s(flag.pattern);
  });
  map(callbacks, [&](Callback& callback) {
    s(callback.uid); text(callback.name); s(callback.function); s(callback.argument); s(callback.thread);
    s(callback.notifyCount); s(callback.notifyArg);
  });
  s(exitCallback);
  vector(memoryStickCallbacks, [&](u32& callback) { s(callback); });
  s(umdCallback);
  vector(blocks, [&](Block& block) { s(block.uid); text(block.name); s(block.address); s(block.size); });
  s(largeMemory); s(sdkVersion); s(compilerVersion);
  s(powerState.callbacks); s(powerState.pll); s(powerState.cpu); s(powerState.bus); s(powerState.volatileLocked);
  //sound output (audio.cpp). A mixer channel's counts are those of buffers (multiples of 64, 65472 at most), its
  //volumes 0xFFFF at most, its format stereo or mono. A buffer in a slot is being played, so the DMA runs, its next
  //block no more than a block away (nor overdue by a frame: events() catches up). The SRC channel arms two buffers at
  //most, only while it's reserved, at a rate it takes, the first one's transfer ending within its own time.
  bool playing = false;
  for(auto& c : audio.channels) {
    s(c.reserved); s(c.sampleCount); s(c.format); s(c.leftVolume); s(c.rightVolume);
    s(c.buffer); s(c.length); s(c.remaining);
    check(c.sampleCount % 64 == 0 && c.sampleCount <= 65472 && (c.format == 0x00 || c.format == 0x10));
    check(c.leftVolume <= 0xffff && c.rightVolume <= 0xffff && c.remaining % 64 == 0 && c.remaining <= 65472);
    check(!c.reserved || mixerSamplesValid(c.sampleCount));
    check(!c.buffer || (mixerSamplesValid(c.length) && c.remaining && c.remaining <= c.length));
    if(c.buffer) playing = true;
  }
  auto& dma = audio.dma;
  s(dma.running); s(dma.nextBlock); s(dma.fraction);
  check(dma.fraction < 49 && (!playing || dma.running));
  check(!dma.running || (dma.nextBlock > cycles ? dma.nextBlock - cycles <= Audio::BlockCycles + 1
                                                : cycles - dma.nextBlock < VblankCycles));
  auto& src = audio.src;
  s(src.reserved); s(src.sampleCount); s(src.rate);
  for(auto& buffer : src.buffers) { s(buffer.address); s(buffer.sampleCount); s(buffer.volume); }
  s(src.armed); s(src.retireAt); s(src.completion);
  bool rate = src.rate && srcRateValid(src.rate);  //0 isn't one: reserving makes it 44100
  check(rate && src.armed <= 2 && (!src.armed || src.reserved));
  check(src.reserved ? srcSamplesValid(src.sampleCount) : !src.sampleCount || srcSamplesValid(src.sampleCount));
  for(u32 n = 0; n < std::min(src.armed, 2u); n++) {
    auto& buffer = src.buffers[n];
    check(buffer.address && srcSamplesValid(buffer.sampleCount) && buffer.volume <= 0xf'ffff);
  }
  if(src.armed && rate) {
    u64 duration = srcDuration(src.buffers[0].sampleCount);
    check(src.retireAt > cycles ? src.retireAt - cycles <= duration : cycles - src.retireAt < VblankCycles);
  }
  //threads waiting on sound: on a mixer channel, one at most, while its slot is busy (it's given the slot as the
  //buffer there is used up); on the SRC channel, one at most waiting for a completion, while both buffers are armed
  //(only a buffer retiring wakes it), and any waiting for it to drain while one is
  if(s.reading()) {
    bool waited[8] = {}, srcWaited = false;
    for(auto& [uid, t] : threads) {
      if(t->wait != Wait::Audio) continue;
      check(t->status == Status::Waiting);
      if(t->waitID < 8) {
        check(audio.channels[t->waitID].buffer && !waited[t->waitID]);
        waited[t->waitID] = true;
      } else if(t->waitID == Audio::WaitSrc) {
        check(src.armed == 2 && !srcWaited);
        srcWaited = true;
      } else {
        check(t->waitID == Audio::WaitSrcDrain && src.armed);
      }
    }
  }
  //the utilities: the dialog, and the modules loaded
  s(dialog.kind); s(dialog.status); s(dialog.next); s(dialog.changeAt); s(dialog.parameters);
  vector(utilityModules, [&](u32& module) { s(module); });
  //IDs count up from nextUID as objects are made, so every object's is below it; and a map's key is its object's own
  if(s.reading()) {
    for(auto& [uid, t] : threads) check(uid < nextUID);
    for(auto& [uid, semaphore] : semaphores) check(uid < nextUID && semaphore.uid == uid);
    for(auto& [uid, workArea] : lwMutexes) check(uid < nextUID);
    for(auto& [uid, pool] : pools) {
      check(uid < nextUID && pool.uid == uid && pool.block < nextUID && poolHolds(pool, blocks));
    }
    for(auto& [uid, flag] : eventFlags) check(uid < nextUID && flag.uid == uid);
    for(auto& [uid, callback] : callbacks) check(uid < nextUID && callback.uid == uid);
    check(programUID < nextUID);
    //A module isn't the program. It has a thread exactly while its module_start or module_stop runs, and that thread
    //is there. A stand-in has nothing in memory; another module has a block of the user partition, or none.
    for(auto& [uid, m] : modules) {
      check(uid < nextUID && m.uid == uid && uid != programUID);
      bool running = m.status == ModuleStatus::Starting || m.status == ModuleStatus::Stopping
                  || m.status == ModuleStatus::Unloading;
      check(running == (m.thread != 0) && (!m.thread || threads.count(m.thread)));
      check(!m.standIn || !m.block);
    }
    //Blocks lie inside the user partition, as allocate() hands them out, and each has one owner at most: a thread's
    //stack (a block of its own, of its size: createThread() makes it, 0x200 bytes at least, and
    //sceKernelGetThreadStackFreeSize reads it in place), a memory pool, a module, or the program (the block start()
    //gives it, at its first segment's 256-byte step); the rest are blocks the program asked for. A thread's stack, a
    //module's block and the program's must be there (and a pool's, which poolHolds() finds).
    for(auto& block : blocks) {
      check(block.uid < nextUID && block.address >= UserMemory && u64(block.address) + block.size <= userEnd());
    }
    std::vector<u32> owners(blocks.size());  //how many claim each block
    auto own = [&](auto&& matches) {  //the first block that matches gains an owner; false if none does
      for(u32 n = 0; n < blocks.size(); n++) if(matches(blocks[n])) return owners[n]++, true;
      return false;
    };
    auto owned = [&](u32 uid) { return own([&](const Block& b) { return b.uid == uid; }); };
    for(auto& entry : threads) {
      auto& t = *entry.second;
      auto stack = [&](const Block& b) { return b.address == t.stackBlock && b.size == t.stackSize; };
      check(t.stackSize >= 0x200 && own(stack));
    }
    for(auto& [uid, pool] : pools) owned(pool.block);
    for(auto& [uid, m] : modules) check(!m.block || owned(m.block));
    if(u32 program = programBlockAt()) check(own([&](const Block& b) { return b.address == program; }));
    for(u32 claims : owners) check(claims <= 1);
  }

  //open files and folders. Nothing on the disc is open for writing (openOnDisc() refuses it), and a folder on the
  //disc keeps each of its names' entries, one for one: reading the folder hands out both. Files are numbered counting
  //up from nextFile, as IDs are.
  auto entry = [&](Disc::Entry& e) { text(e.name); s(e.sector); s(e.size); s(e.folder); s(e.date); };
  map(files, [&](OpenFile& open) {
    text(open.path);
    s(open.folder); s(open.flags); s(open.position);
    vector(open.entries, [&](std::string& name) { text(name); });
    s(open.nextEntry);
    s(open.onDisc); s(open.sectors); s(open.sector); s(open.size);
    vector(open.discEntries, entry);
    check(!open.onDisc || !(open.flags & OpenWrite));
    check(!open.onDisc || !open.folder || open.discEntries.size() == open.entries.size());
  });
  s(nextFile);
  check(nextFile >= 3 && nextFile <= LastUID + 1);  //after standard input, output and error; short of 2^31
  if(s.reading()) for(auto& [file, open] : files) check(file < nextFile);
  text(workingDirectory);
  if(s.reading() && valid) {
    //files and folders opened again, where the devices are now: a host file reopened, and one on the disc kept only
    //while a disc image is in the drive (onDisc())
    for(auto at = files.begin(); at != files.end();) {
      auto& open = at->second;
      bool kept = disc && !devices.count("disc0");
      if(!open.onDisc) {
        std::string normalized;
        kept = !resolve(open.path, open.host, normalized);
        if(kept && open.folder) check(listed(open.entries, normalized));
        if(kept && !open.folder) {
          auto mode = std::ios::binary | std::ios::in;
          if(open.flags & OpenWrite) mode |= std::ios::out;
          open.stream = std::make_unique<std::fstream>(open.host, mode);
          kept = open.stream->is_open();
        }
      }
      at = kept ? std::next(at) : files.erase(at);
    }
  }

  //the controller: the next sample's place in the ring of 64, and how many since the last read (63 at most)
  auto& c = controller;
  s(c.buttons); s(c.analogX); s(c.analogY); s(c.cycle); s(c.mode); s(c.nextSample);
  for(auto& sample : c.samples) { s(sample.time); s(sample.buttons); s(sample.x); s(sample.y); }
  s(c.next); s(c.unread); s(c.sampled);
  s(c.latch.made); s(c.latch.broken); s(c.latch.held); s(c.latch.released); s(c.latch.samples);
  check(c.next < 64 && c.unread <= 63);
  //a sampling cycle as sceCtrlSetSamplingCycle allows (none, or 5555 to 20000 microseconds), with the next sample no
  //more than one cycle ahead, nor a frame behind
  u64 period = u64(c.cycle) * (CPUFrequency / 1'000'000);
  check(!c.cycle || (c.cycle >= 5555 && c.cycle <= 20000 &&
        (c.nextSample > cycles ? c.nextSample - cycles <= period : cycles - c.nextSample < VblankCycles)));
  //the display: its one mode, 480x272 (sceDisplaySetMode refuses any other)
  s(display.mode); s(display.width); s(display.height);
  s(display.frameBuffer); s(display.bufferWidth); s(display.pixelFormat);
  check(display.mode == 0 && display.width == 480 && display.height == 272);

  //calls into the program
  vector(calls, [&](Call& call) {
    s(call.function); s(call.gp); s(call.arguments); s(call.resumesGe); s(call.vblank);
  });
  s(interrupting); s(interruptsEnabled); s(rescheduleAfter);
  context(interrupted);
  s(interruptedHalted); s(callResumesGe);
  //sub-interrupt handlers (none on the vertical blank's 16-31: a program can't register those), and a vertical
  //blank held off
  for(auto* set : {vblankSubs, geSubs}) {
    for(u32 sub = 0; sub < 32; sub++) {
      auto& handler = set[sub];
      s(handler.function); s(handler.argument); s(handler.gp); s(handler.enabled);
      check(set != vblankSubs || sub < 16 || !handler.function);
    }
  }
  s(vblankPending);

  //the GE driver. Each list's stack is no deeper than it allows (under 256: geEnqueue), the GE's own CALLs, in a
  //list's registers or kept on its stack, go two deep at most (GE::Registers), and a list running or done has run:
  //it started.
  for(auto& list : geLists) {
    s(list.state); s(list.started); s(list.pausing); s(list.syncing);
    s(list.registers); s(list.base); s(list.callback); s(list.context); s(list.stackLimit);
    vector(list.stack, [&](GeStackEntry& e) { s(e.registers); s(e.base); check(e.registers.depth <= 2); });
    s(list.signalID);
    check(list.state <= GeList::State::Paused && list.registers.depth <= 2);
    check(list.stackLimit < 256 && list.stack.size() <= list.stackLimit);
    check(list.started || (list.state != GeList::State::Running && list.state != GeList::State::Completed));
  }
  for(auto* queue : {&geQueue, &geFree}) vector(*queue, [&](u32& index) { s(index); });
  for(auto& callback : geCallbacks) {
    s(callback.used); s(callback.signalFunction); s(callback.signalArgument);
    s(callback.finishFunction); s(callback.finishArgument); s(callback.gp);
  }
  s(geRunning); s(geBusy); s(geSuspended); s(geFinishing);
  s(geLeft);  //the frame's commands still to run: no more than a frame's (each vertical blank gives it GeBudget)
  check(geLeft <= GeBudget);
  if(s.reading() && valid) {
    //Every list is in the queue or free, once. A list queued, running or paused is in the queue (sceGeListDeQueue
    //and geEnded() take it out of there); one never queued is free; a completed one is either (it leaves the queue
    //once its finish callback has returned). The lists the GE runs and finishes are queued ones, and the one it
    //finishes has completed.
    bool queued[64] = {}, seen[64] = {};
    for(auto* queue : {&geQueue, &geFree}) {
      for(u32 index : *queue) {
        check(index < 64 && !seen[index]);
        if(index < 64) seen[index] = true, queued[index] = queue == &geQueue;
      }
    }
    check(geQueue.size() + geFree.size() == 64);
    for(u32 index = 0; index < 64 && valid; index++) {
      auto state = geLists[index].state;
      if(state == GeList::State::None) check(!queued[index]);
      else if(state != GeList::State::Completed) check(queued[index]);
    }
    for(s32 index : {geRunning, geFinishing}) check(index == -1 || (index >= 0 && index < 64 && queued[index]));
    if(valid && geFinishing >= 0) check(geLists[geFinishing].state == GeList::State::Completed);
  }
  return valid;
}
