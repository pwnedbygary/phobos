//Modules a program loads besides itself (ModuleMgrForUser): PRXs from the disc or the memory stick, often encrypted
//(decrypt.cpp), put in the user partition and relocated there by the loader, their imports linked to the functions
//the program and the modules loaded earlier export, and their module_start run on a thread of their own. Sony's own
//modules, which early games carried for the libraries they use (sound, video, the network), aren't run: the HLE
//kernel is their stand-in. docs/psp-core.md, part 19, describes it.

enum : u32 {
  ModuleStartNID     = 0xd632'acdb,  //the module_start a module exports for itself (no library name)
  ModuleStopNID      = 0xcee8'593c,  //and its module_stop
  StartParametersNID = 0x0f7c'276c,  //module_start_thread_parameter: a variable it exports for itself, saying what
                                     //thread module_start runs on (makeModuleThread())
  StopParametersNID  = 0xcf0c'c697,  //module_stop_thread_parameter: module_stop's
};

//Sony's own modules aren't run: the HLE kernel stands in for them, its versions of their functions answering the
//game's imports of them (sceSAScore's, sceMpeg_library's...). They're told by their names, which all start with
//"sce" or "Sce", or by being kernel modules (attribute 0x1000), which a game's own modules never are. (Peace
//Walker's psmf.prx and libpsmfplayer.prx are scePsmf_library and scePsmfP_library, stood in for so. The movie
//player's library goes by other names in some games, libpsmfplayer, psmf_jk, jkPsmfP_library: those are loaded and
//run as a game's own code, their module_start too; but the kernel's own functions go before a module's exports as
//imports are linked, so a game's calls to scePsmfPlayer reach the kernel's player whatever its module is called.
//docs/psp-core.md, part 31.)
static auto sonyModule(const std::string& name, u32 attributes) -> bool {
  return !name.compare(0, 3, "sce") || !name.compare(0, 3, "Sce") || (attributes & 0x1000);
}

//A file's bytes, read whole into the host's memory (at most 64 MiB, the PSP's memory): one on the disc, by its path
//or as a run of sectors by number ("sce_lbn...": games name modules that way too), or one on a host folder standing
//for a device. Returns 0, or why it can't be read.
auto Kernel::readWhole(const std::string& path, std::vector<u8>& data) -> u32 {
  data.clear();
  if(onDisc(path)) {
    u32 first = 0;
    u64 size = 0;
    if(!runOnDisc(path, first, size)) {
      Disc::Entry entry;
      std::string normalized;
      std::vector<std::string> names;
      if(u32 error = resolveOnDisc(path, entry, normalized, names)) return error;
      if(entry.folder) return ErrorIsDirectory;
      first = entry.sector, size = entry.size;
    }
    u64 start = u64(first) * Disc::SectorSize;
    if(start > disc->size()) return ErrorFileNotFound;
    size = std::min<u64>({size, disc->size() - start, 64_MiB});
    data.resize(size);
    if(size && !disc->read(start, size, data.data())) return data.clear(), ErrorIOError;
    return 0;
  }
  std::string host, normalized;
  if(u32 error = resolve(path, host, normalized)) return error;
  std::error_code error;
  if(std::filesystem::is_directory(host, error)) return ErrorIsDirectory;
  std::ifstream file(host, std::ios::binary);
  u64 size = std::filesystem::file_size(host, error);
  if(!file || error) return ErrorFileNotFound;
  data.resize(std::min<u64>(size, 64_MiB));
  file.read((char*)data.data(), data.size());
  data.resize(file.gcount());
  return 0;
}

//Up to size bytes of an open file, from where it's at (which moves past them), into the host's memory: 64 MiB at
//most (the PSP's memory), and never more than the file has left, so a size a damaged header claims costs nothing.
auto Kernel::readOpenFile(OpenFile& open, u64 size, std::vector<u8>& data) -> u32 {
  data.clear();
  if(open.folder || open.sectors || !(open.flags & OpenRead)) return ErrorBadFile;
  size = std::min<u64>(size, 64_MiB);
  if(open.onDisc) {
    u64 start = u64(open.sector) * Disc::SectorSize + open.position, end = disc->size();
    size = std::min<u64>({size, open.size > open.position ? open.size - open.position : 0,
                          start < end ? end - start : 0});
    data.resize(size);
    if(size && !disc->read(start, size, data.data())) return data.clear(), ErrorIOError;
  } else {
    open.stream->clear();
    open.stream->seekg(0, std::ios::end);
    std::streamoff end = open.stream->tellg();  //-1 if the host can't say: nothing is read
    size = std::min<u64>(size, end > std::streamoff(open.position) ? u64(end) - open.position : 0);
    data.resize(size);
    open.stream->seekg(std::streamoff(open.position));
    open.stream->read((char*)data.data(), data.size());
    data.resize(open.stream->gcount());
    open.stream->clear();
  }
  open.position += data.size();
  return 0;
}

//One of Sony's modules, stood in for: an ID and its name, nothing in memory.
auto Kernel::standIn(const std::string& name, u32 attributes, const std::string& path) -> u32 {
  u32 uid = newUID();
  if(!uid) return ErrorNoMemory;
  LoadedModule loaded;
  loaded.uid = uid;
  loaded.path = path;
  loaded.standIn = true;
  loaded.module.name = name;
  loaded.module.attributes = attributes;
  modules[uid] = std::move(loaded);
  note(path + " is Sony's " + name + ", which the HLE kernel stands in for");
  return uid;
}

//Loads a module from a file's bytes. One of Sony's is stood in for: an encrypted one says whose it is in the clear
//(its ~PSP header's name and attributes), so it needn't even be decrypted. Another is decrypted if it's encrypted,
//and loaded by the loader: a PRX in a block of the user partition (the lowest free place), a static program where
//it was linked. Then every module's imports are linked again. Returns its new ID, or why it can't be loaded (the
//decrypter's or the loader's reason is noted).
auto Kernel::loadModule(const std::vector<u8>& file, const std::string& path) -> u32 {
  const u8* data = file.data();
  u64 size = file.size();
  unwrapProgram(data, size);
  std::vector<u8> decrypted;
  if(encryptedProgram(data, size)) {
    if(size >= 0x150) {
      std::string name;
      for(u32 at = 0x0a; at < 0x0a + 28 && data[at]; at++) name.push_back(char(data[at]));
      u32 attributes = data[4] | data[5] << 8;
      if(sonyModule(name, attributes)) return standIn(name, attributes, path);
    }
    if(auto why = decryptProgram(data, size, decrypted); !why.empty()) {
      note("can't load " + path + ": " + why);
      return ErrorUnsupportedPrxType;
    }
    data = decrypted.data(), size = decrypted.size();
  }
  u32 low = 0, high = 0;
  bool relocatable = false;
  if(!Loader::extent(data, size, low, high, relocatable)) {
    note("can't load " + path + ": it isn't a MIPS program");
    return ErrorIllegalObject;
  }
  //where it goes: a PRX anywhere (its addresses count from 0), a static program exactly where it was linked
  u32 from = relocatable ? low : low & ~255u;
  auto block = relocatable ? allocate(high - from, 0, 0, path) : allocate(high - from, 2, from, path);
  if(!block) return ErrorNoMemory;
  if(!relocatable && block->address != from) return release(block->uid), ErrorNoMemory;
  LoadedModule loaded;
  loaded.path = path;
  loaded.block = block->uid;
  u32 base = relocatable ? block->address - low : 0;
  auto why = Loader::load(memory, data, size, base, [this](const std::string& library, u32 nid) {
    return importCode(library, nid);
  }, loaded.module);
  if(!why.empty()) {
    release(loaded.block);
    note("can't load " + path + ": " + why);
    return ErrorIllegalObject;
  }
  if(sonyModule(loaded.module.name, loaded.module.attributes)) {  //one of Sony's that came unencrypted
    release(loaded.block);
    return standIn(loaded.module.name, loaded.module.attributes, path);
  }
  loaded.uid = newUID();
  if(!loaded.uid) return release(loaded.block), ErrorNoMemory;
  for(auto& skipped : loaded.module.skipped) note(path + ": the loader left out " + skipped);
  u32 uid = loaded.uid;
  modules[uid] = std::move(loaded);
  linkImports();
  return uid;
}

//Links every module's imports (the program's too) to the functions the program and loaded modules export. An
//import the HLE kernel has no function for, which one of them exports in a library (a module's own entry points
//have no library name), becomes "j address; nop" in place of the kernel's syscall: the caller's jal left ra pointing
//back at it, so the function returns straight there. Every other import is the kernel's syscall, as the loader made
//it, so a stub linked to a module since unloaded goes back to the kernel. A stub already right isn't written again
//(that would throw away the code compiled around it).
auto Kernel::linkImports() -> void {
  std::map<std::pair<std::string, u32>, u32> offered;
  auto offer = [&](const Module& offering) {
    for(auto& e : offering.exports) {
      if(!e.library.empty() && !e.variable) offered[{e.library, e.nid}] = e.address;
    }
  };
  offer(module);  //a game's modules may import from libraries its EBOOT.BIN exports
  for(auto& [uid, loaded] : modules) if(!loaded.standIn) offer(loaded.module);
  auto link = [&](const Module& linked) {
    for(auto& i : linked.imports) {
      bool kernels = std::any_of(functions.begin(), functions.end(), [&](auto& f) { return f.nid == i.nid; });
      auto found = offered.find({i.library, i.nid});
      u32 first = 0x03e0'0008, second = (importCode(i.library, i.nid) & 0xf'ffff) << 6 | 0x0c;  //jr ra; syscall
      if(found != offered.end() && !kernels) first = 0x0800'0000 | (found->second >> 2 & 0x03ff'ffff), second = 0;
      if(memory.read(4, i.stub) != first) memory.write(4, i.stub, first);
      if(memory.read(4, i.stub + 4) != second) memory.write(4, i.stub + 4, second);
    }
  };
  link(module);
  for(auto& [uid, loaded] : modules) if(!loaded.standIn) link(loaded.module);
}

//The module whose segments hold an address (in any of memory's windows), the program's own included; or 0.
auto Kernel::moduleAt(u32 address) const -> u32 {
  auto holds = [&](const Module& m) {
    for(auto& segment : m.segments) {
      if((address & 0x1fff'ffff) - (segment.address & 0x1fff'ffff) < segment.size) return true;
    }
    return false;
  };
  if(programUID && holds(module)) return programUID;
  for(auto& [uid, loaded] : modules) if(!loaded.standIn && holds(loaded.module)) return uid;
  return 0;
}

//The function a module runs as it starts or stops (nid: module_start's or module_stop's), exported for itself; or 0
//if it has none. One that exports no module_start starts at its ELF header's entry point instead, where its code
//begins, if that's in the module: an entry of 0, or one outside it, means there's nothing to run.
auto Kernel::moduleFunction(const LoadedModule& loaded, u32 nid) const -> u32 {
  for(auto& e : loaded.module.exports) {
    if(e.library.empty() && e.nid == nid && !e.variable) return e.address;
  }
  u32 entry = loaded.module.entry;
  return nid == ModuleStartNID && entry && moduleAt(entry) == loaded.uid ? entry : 0;
}

//Makes the thread a module's module_start or module_stop runs on, from entry, dormant, with room on its stack for
//the argument (length bytes at argument, if any). Its priority, stack size and attributes are the program's first
//thread's (0x20, 256 KiB, user mode with the VFPU), unless the module's own thread parameters say otherwise
//(parameters: the NID of module_start_thread_parameter or module_stop_thread_parameter, a variable it exports for
//itself: how many values follow, Sony's SDK writing 3, then a priority, a stack size and attributes, as uOFW's
//SceModuleEntryThread has them), or the caller's options do (SceKernelSMOption: its size, a stack's partition, then
//the same three). A 0 in either leaves the value as it was. Returns the thread's ID, or why it couldn't be made.
auto Kernel::makeModuleThread(const LoadedModule& loaded, u32 entry, u32 parameters, u32 length, u32 argument,
                              u32 options) -> s32 {
  u32 priority = 0x20, stackSize = 256_KiB, attributes = 0x8000'4000;
  for(auto& e : loaded.module.exports) {
    if(!e.library.empty() || e.nid != parameters || !e.variable || !memory.reaches(e.address, 4)) continue;
    u32 count = std::min<u32>(memory.read(4, e.address), 3);
    if(!memory.reaches(e.address, 4 + count * 4)) continue;
    if(u32 value = count >= 1 ? memory.read(4, e.address + 4) : 0) priority = value;
    if(u32 value = count >= 2 ? memory.read(4, e.address + 8) : 0) stackSize = value;
    if(u32 value = count >= 3 ? memory.read(4, e.address + 12) : 0) attributes = value;
  }
  if(options && memory.reaches(options, 20) && memory.read(4, options) >= 20) {
    if(u32 value = memory.read(4, options + 8)) stackSize = value;
    if(u32 value = memory.read(4, options + 12)) priority = value;
    if(u32 value = memory.read(4, options + 16)) attributes = value;
  }
  s32 made = createThread(loaded.module.name, entry, priority, stackSize, attributes, loaded.module.gp);
  if(made < 0) return made;
  if(argument && length && !argumentFits(*threads[made], length)) {
    deleteThread(*threads[made]);
    return ErrorIllegalArgument;
  }
  return made;
}

//Whether a thread is one made to run a module's module_start or module_stop.
auto Kernel::madeForModule(u32 thread) const -> bool {
  return std::any_of(modules.begin(), modules.end(), [&](auto& entry) { return entry.second.thread == thread; });
}

//What sceKernelStartModule and sceKernelStopModule share (module, argument size, argument, where to put the result,
//options): the module's module_start (or module_stop) runs on a thread made for it (makeModuleThread()), its
//argument copied onto the thread's stack, and returns to the trampoline's third syscall (moduleReturned()). The
//calling thread waits meanwhile, and then gets the module's ID, the function's result going where it asked
//(moduleThreadEnded()). A module without the function, or a stand-in, is started (or stopped) at once.
auto Kernel::runModuleFunction(LoadedModule& loaded, u32 nid, ModuleStatus during) -> void {
  u32 length = arg(1), argument = arg(2), status = arg(3), options = arg(4);
  if(status && !memory.reaches(status, 4)) return result(ErrorIllegalAddress);
  if(argument && length && !memory.reaches(argument, length)) return result(ErrorIllegalAddress);
  u32 entry = loaded.standIn ? 0 : moduleFunction(loaded, nid);
  if(!entry) {
    loaded.status = during == ModuleStatus::Starting ? ModuleStatus::Started : ModuleStatus::Stopped;
    if(status) memory.write(4, status, 0);
    return result(loaded.uid);
  }
  if(!mayWait()) return;
  u32 parameters = nid == ModuleStartNID ? StartParametersNID : StopParametersNID;
  s32 made = makeModuleThread(loaded, entry, parameters, length, argument, options);
  if(made < 0) return result(u32(made));
  loaded.status = during;
  loaded.thread = made;
  if(current) {
    current->waitPointer = status;
    block(Wait::Module, loaded.uid, 0);
  } else {
    result(loaded.uid);  //no thread called it (the kernel's own tests): nothing to wait
  }
  startThread(*threads[made], length, argument, Trampoline + 16);
}

//A thread that ran a module's module_start or module_stop has ended (by returning, or exiting): the module is
//started (or stopped), and the threads waiting for it run again, given its ID, the function's result written where
//each asked. A module whose module_stop ran as it unloaded itself goes now (unloadSelf()). The thread itself is
//deleted by whoever ended it, once it's no longer the one running.
auto Kernel::moduleThreadEnded(Thread& thread, s32 status) -> void {
  auto found = std::find_if(modules.begin(), modules.end(), [&](auto& entry) {
    return entry.second.thread == thread.uid;
  });
  if(found == modules.end()) return;
  u32 uid = found->first;
  auto& loaded = found->second;
  loaded.thread = 0;
  for(auto& [id, other] : threads) {
    if(other->status != Status::Waiting || other->wait != Wait::Module || other->waitID != uid) continue;
    if(other->waitPointer) memory.write(4, other->waitPointer, u32(status));
    ready(*other, uid);
  }
  if(loaded.status == ModuleStatus::Unloading) return unloadModule(uid);
  loaded.status = loaded.status == ModuleStatus::Starting ? ModuleStatus::Started : ModuleStatus::Stopped;
}

//A module unloaded: its memory goes back to the user partition, and stubs linked to it go back to the kernel.
auto Kernel::unloadModule(u32 uid) -> void {
  auto found = modules.find(uid);
  if(found == modules.end()) return;
  if(found->second.block) release(found->second.block);
  modules.erase(found);
  linkImports();
}

//A module stopping and unloading itself (exit status, argument size, argument, options as sceKernelStopModule's):
//what sceKernelSelfStopUnloadModule, and the other functions by which a module does that, share. The module is the
//one holding the code that called (ra points back into it); the program, or code no module holds, is the program
//leaving. Otherwise the calling thread ends, as by sceKernelExitThread(exitStatus), and is deleted, as what it would
//return to is going; then the module, if it's running, has its module_stop run with the argument on a thread made
//for it, as sceKernelStopModule would, and goes once that ends (moduleThreadEnded()). One that isn't running (never
//started, or the caller was its module_stop), or has no module_stop, goes at once. Nothing waits for module_stop's
//result: the thread that asked is gone. Refused while another thread runs its module_start or module_stop, and from
//a call into the program (which runs on top of whichever thread was running, not one of the module's).
auto Kernel::unloadSelf(s32 exitStatus, u32 length, u32 argument, u32 options) -> void {
  auto found = modules.find(moduleAt(cpu.ipu.r[31]));
  if(found == modules.end()) {
    exited = true;
    return switchTo(nullptr);
  }
  u32 uid = found->first, running = found->second.thread;
  if(interrupting) return result(ErrorIllegalContext);
  if(running && (!current || running != current->uid)) return result(ErrorNotStopped);
  if(argument && length && !memory.reaches(argument, length)) return result(ErrorIllegalAddress);
  result(0);  //what a caller that isn't a thread (the kernel's own tests) sees
  Thread* caller = current;
  if(caller) {
    endThread(*caller, exitStatus);  //which ends the module's module_start or module_stop, if the caller ran it
    current = nullptr;  //nothing to save: it goes once module_stop's argument is copied off its stack
  }
  found = modules.find(uid);  //gone already if the caller was the module_stop it ran as it unloaded itself
  if(found != modules.end()) {
    auto& loaded = found->second;
    u32 stop = loaded.status == ModuleStatus::Started ? moduleFunction(loaded, ModuleStopNID) : 0;
    s32 made = stop ? makeModuleThread(loaded, stop, StopParametersNID, length, argument, options) : -1;
    if(made >= 0) {
      loaded.status = ModuleStatus::Unloading;
      loaded.thread = made;
      startThread(*threads[made], length, argument, Trampoline + 16);
    } else {
      if(stop) note(loaded.module.name + " unloads itself without its module_stop, which had no thread to run on");
      unloadModule(uid);
    }
  }
  if(caller) deleteThread(*caller);
  reschedule();
}

//The trampoline's third syscall: a module's module_start or module_stop returned, its result in v0. Its thread ends
//and is deleted, as the PSP's module manager deletes the thread it made for it.
auto Kernel::moduleReturned() -> void {
  if(!current) return;
  Thread* thread = current;
  endThread(*thread, s32(cpu.ipu.r[2]));
  current = nullptr;  //nothing to save: the thread is gone
  deleteThread(*thread);
  reschedule();
}

//(path, flags, options): a module loaded from a file; its ID, or why it couldn't be loaded.
auto Kernel::sceKernelLoadModule() -> void {
  std::string path = memory.readString(arg(0), 1024);
  std::vector<u8> data;
  if(u32 error = readWhole(path, data)) return result(error);
  result(loadModule(data, path));
}

//(file, flags, options): a module loaded from a file already open, from where it's at: games keep modules inside
//archives of their own, and seek to one before loading it. An encrypted module is as long as its ~PSP header says
//(0x2c), after the ~SCE header some have: one said to be shorter than that header, or longer than 64 MiB (the PSP's
//memory), is refused, the file left where it was. A plain one runs to the file's end (16 MiB at most).
auto Kernel::sceKernelLoadModuleByID() -> void {
  auto found = files.find(arg(0));
  if(found == files.end()) return result(ErrorBadFile);
  auto& open = found->second;
  u64 position = open.position;
  std::vector<u8> data;
  if(u32 error = readOpenFile(open, 0x200, data)) return result(error);
  open.position = position;
  const u8* header = data.data();
  u64 left = data.size();
  unwrapProgram(header, left);
  u64 wrapped = header - data.data(), size = 16_MiB;
  if(left >= 0x150 && encryptedProgram(header, left)) {
    u32 claimed = header[0x2c] | header[0x2d] << 8 | header[0x2e] << 16 | u32(header[0x2f]) << 24;
    if(claimed < 0x150 || claimed > 64_MiB) {
      note("can't load " + open.path + ": its ~PSP header says it's " + std::to_string(claimed) + " bytes long");
      return result(ErrorIllegalObject);
    }
    size = wrapped + claimed;
  }
  if(u32 error = readOpenFile(open, size, data)) return result(error);
  result(loadModule(data, open.path));
}

//(module, argument size, argument, where to put module_start's result, options)
auto Kernel::sceKernelStartModule() -> void {
  auto found = modules.find(arg(0));
  if(found == modules.end()) return result(ErrorUnknownModule);
  if(found->second.status != ModuleStatus::Loaded) return result(ErrorAlreadyStarted);
  runModuleFunction(found->second, ModuleStartNID, ModuleStatus::Starting);
}

//(module, argument size, argument, where to put module_stop's result, options)
auto Kernel::sceKernelStopModule() -> void {
  auto found = modules.find(arg(0));
  if(found == modules.end()) return result(ErrorUnknownModule);
  auto status = found->second.status;
  if(status == ModuleStatus::Stopped) return result(ErrorAlreadyStopped);
  if(status != ModuleStatus::Started) return result(ErrorNotStarted);
  runModuleFunction(found->second, ModuleStopNID, ModuleStatus::Stopping);
}

//(module): a module that isn't running (never started, or stopped) unloaded: its memory goes back to the user
//partition, and stubs linked to it go back to the kernel.
auto Kernel::sceKernelUnloadModule() -> void {
  auto found = modules.find(arg(0));
  if(found == modules.end()) return result(ErrorUnknownModule);
  auto status = found->second.status;
  if(status != ModuleStatus::Loaded && status != ModuleStatus::Stopped) return result(ErrorNotStopped);
  u32 uid = found->first;
  unloadModule(uid);
  result(uid);
}

//(address): the module holding it.
auto Kernel::sceKernelGetModuleIdByAddress() -> void {
  u32 uid = moduleAt(arg(0));
  result(uid ? uid : ErrorUnknownModule);
}

//The calling module's: the one holding the code that called (ra points back into it), else the program's.
auto Kernel::sceKernelGetModuleId() -> void {
  u32 uid = moduleAt(cpu.ipu.r[31]);
  if(!uid) uid = programUID;
  result(uid ? uid : ErrorUnknownModule);
}

//(where to put the IDs, its size in bytes, where to put how many there are): the program's ID, then its modules',
//as many as fit; the count is all of them.
auto Kernel::sceKernelGetModuleIdList() -> void {
  u32 list = arg(0), room = arg(1) / 4, count = arg(2);
  if((room && !memory.reaches(list, room * 4)) || (count && !memory.reaches(count, 4))) {
    return result(ErrorIllegalAddress);
  }
  std::vector<u32> ids;
  if(programUID) ids.push_back(programUID);
  for(auto& [uid, loaded] : modules) ids.push_back(uid);
  for(u32 n = 0; n < ids.size() && n < room; n++) memory.write(4, list + n * 4, ids[n]);
  if(count) memory.write(4, count, ids.size());
  result(0);
}

//(module, where to put its information): a SceKernelModuleInfo (pspmodulemgr.h) as far as the size its first word
//gives: its segments (four at most), entry, gp, attributes, version and name. The loader keeps each segment's size
//in memory, not its file and zeroed parts: the text is the first segment, the data the others, and the bss 0.
auto Kernel::sceKernelQueryModuleInfo() -> void {
  const Module* m = nullptr;
  if(arg(0) && arg(0) == programUID) m = &module;
  if(auto found = modules.find(arg(0)); found != modules.end()) m = &found->second.module;
  if(!m) return result(ErrorUnknownModule);
  u32 info = arg(1);
  if(!memory.reaches(info, 4)) return result(ErrorIllegalAddress);
  u32 size = std::min<u32>(memory.read(4, info), 96);
  if(!memory.reaches(info, size)) return result(ErrorIllegalAddress);
  auto put = [&](u32 offset, u32 bytes, u32 value) {
    if(offset + bytes <= size) memory.write(bytes, info + offset, value);
  };
  u32 segments = std::min<u32>(m->segments.size(), 4), data = 0;
  put(4, 1, segments);
  for(u32 n = 0; n < 4; n++) {
    put(8 + n * 4, 4, n < segments ? m->segments[n].address : 0);
    put(24 + n * 4, 4, n < segments ? m->segments[n].size : 0);
    if(n && n < segments) data += m->segments[n].size;
  }
  put(40, 4, m->entry);
  put(44, 4, m->gp);
  put(48, 4, segments ? m->segments[0].address : 0);
  put(52, 4, segments ? m->segments[0].size : 0);
  put(56, 4, data);
  put(60, 4, 0);
  put(64, 2, m->attributes);
  put(66, 1, m->version[0]);
  put(67, 1, m->version[1]);
  for(u32 n = 0; n < 28; n++) put(68 + n, 1, n < m->name.size() ? u8(m->name[n]) : 0);
  result(0);
}
