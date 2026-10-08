#include <algorithm>
#if defined(__ANDROID__)
  #include <android/native_window.h>
#endif

namespace ares::PlayStationPortable {

auto enumerate() -> std::vector<string> {
  return {"[Sony] PlayStation Portable"};
}

auto load(Node::System& node, string name) -> bool {
  auto list = enumerate();
  if(std::find(list.begin(), list.end(), name) == list.end()) return false;
  return system.load(node, name);
}

//What the front end tells the core before loading: "Memory Stick", the host folder standing for ms0:; "Fonts", the
//host folder holding the PSP's system fonts (the .pgf files of the owner's own PSP's flash0:/font), none for a PSP
//without them; "Recompiler", "true" to run the CPU's recompiler (the default) or "false" to run the interpreter
//alone; "GE Threads", how many threads draw the GE's pictures (ge/threads.cpp), 0 (the default) for one fewer than
//the host has cores, 1 for the GE's own alone, and no more than twice the host's cores, nor 64 (more would only wait
//their turn). Every count draws the very same pixels. "Renderer", who draws them: "Software" (the default, the exact
//one) or "Vulkan" (the GPU's, ge/gpu: docs/psp-gpu-renderers.md), taken at the next power on. "Resolution", the
//Vulkan renderer's internal resolution: 1 (the default), the PSP's own, the exact native mode, to 10 times it each
//way, taken at the next load. "Renderer Check", "Fail" to have the Vulkan renderer's start-up check fail as if the
//GPU drew wrong (a front end's debug switch, to see the software renderer take over and the owner told).
auto option(string name, string value) -> bool {
  if(name == "Memory Stick") system.memoryStick = value;
  if(name == "Fonts") system.fonts = value;
  if(name == "Recompiler") system.recompile = value.boolean();
  if(name == "GE Threads") system.geThreads = std::min<u64>(value.natural(), System::MostGeThreads);
  if(name == "Renderer") system.renderer = value == "Vulkan" ? "Vulkan" : "Software";
  if(name == "Resolution") system.resolution = std::clamp<u64>(value.natural(), 1, 10);
  if(name == "Renderer Check") system.failCheck = value == "Fail";
  return true;
}

auto vulkanLoader(void* getInstanceProcAddr) -> void {
  system.vulkanLoader = getInstanceProcAddr;
}

auto window(void* window) -> void {
  std::lock_guard lock{system.windowMutex};
  if(window == system.hostWindow) return;
  System::hold(window, true);
  System::hold(system.hostWindow, false);
  system.hostWindow = window;
}

auto presenting() -> bool {
  return system.presents;
}

//(narrowed to its frame buffer's format and widened again, as GPU::picture() makes them)
auto shot(std::vector<u32>& pixels, u32& width, u32& height) -> bool {
  std::lock_guard lock{system.shotMutex};
  if(system.shotPixels.empty()) return false;
  pixels = system.shotPixels, width = system.shotWidth, height = system.shotHeight;
  for(auto& pixel : pixels) {
    pixel = 0xff00'0000 | (widenTarget(narrowTarget(pixel, system.shotFormat), system.shotFormat) & 0xff'ffff);
  }
  return true;
}

//A reference to the host's window taken (held) or let go of (Android's ANativeWindow; elsewhere there's none).
auto System::hold(void* window, bool held) -> void {
  if(!window) return;
  #if defined(__ANDROID__)
  if(held) ANativeWindow_acquire((ANativeWindow*)window);
  else ANativeWindow_release((ANativeWindow*)window);
  #endif
}

//The host's window, if it's changed, given to the GPU (its swapchain made on it as it's next shown on), the one before
//let go of once the GPU has: on the emulation thread, the GPU's.
auto System::handWindow() -> void {
  std::lock_guard lock{windowMutex};
  if(!gpu || hostWindow == gpuWindow) return;
  gpu->window(hostWindow);
  hold(hostWindow, true);
  hold(gpuWindow, false);
  gpuWindow = hostWindow;
}

auto notice() -> string {
  std::lock_guard lock{system.noticeLock};
  string text = system.pendingNotice.c_str();
  system.pendingNotice.clear();
  return text;
}

System system;

auto System::game() -> string {
  if(gamePak && gamePak->attribute("title")) return gamePak->attribute("title");
  return "(no game inserted)";
}

//One frame: the controls in, the game run for a frame's time, the frame and its sound out.
auto System::run() -> void {
  controls.poll();
  kernel.controller.buttons = controls.buttons();
  kernel.controller.analogX = controls.stick(controls.x);
  kernel.controller.analogY = controls.stick(controls.y);
  if(!kernel.exited) kernel.run(Kernel::VblankCycles);

  //Where the GPU presents on the host's window itself, the frame is shown there (present()), and the screen only
  //hands the host its turn (passthrough: nothing converted, nothing drawn).
  bool presented = present();
  presents = presented;
  screen->setPassthrough(presented);
  if(presented) return screen->frame(), speak();

  //The screen's colors are the pixels' own (red in the low byte, then green and blue: the palette below). The
  //frame comes straight from the hardware renderer's picture of it where the GPU drew it (GPU::picture(): VRAM's
  //pages stay the GPU's), at the screen's size where the GPU draws larger, else from memory's VRAM, as the kernel
  //has it, which finishes what the GPU drew there.
  bool drawn = false;
  u32 at = 1;  //(the picture's size, times the PSP's)
  if(ge.renderer && gpu && kernel.display.frameBuffer) {
    auto& display = kernel.display;
    u32 physical = display.frameBuffer & 0x1fff'ffff;  //(VRAM's first copy alone: the others aren't the same bytes)
    u32 width = display.width ? display.width : 480, height = display.height ? display.height : 272;
    at = std::min(shown, gpu->resolution());
    if(physical >= Memory::VRAMBase && physical - Memory::VRAMBase < Memory::VRAMSize) {
      drawn = gpu->picture(physical - Memory::VRAMBase, display.bufferWidth, display.pixelFormat & 3, width, height,
                           pixels, at);
    }
  }
  if(!drawn) kernel.picture(pixels), at = 1;
  //as picture() made it (480x272 times at: the PSP has no other size), in the screen's 480x272 times shown, each of
  //its pixels repeated where it's smaller
  u64 width = kernel.display.width ? kernel.display.width : 480;
  u64 height = kernel.display.height ? kernel.display.height : 272;
  auto output = screen->pixels().data();
  u32 across = 480 * shown, down = 272 * shown;
  width *= at, height *= at;
  if(width * height == pixels.size()) {
    for(u32 y : range(std::min<u64>(height * shown / at, down))) {
      const u32* row = &pixels[y * at / shown * width];
      u32* to = &output[y * across];
      u32 columns = std::min<u64>(width * shown / at, across);
      if(at == shown) for(u32 x : range(columns)) to[x] = row[x] & 0xff'ffff;
      else for(u32 x : range(columns)) to[x] = row[x * at / shown] & 0xff'ffff;
    }
  }
  screen->frame();
  speak();
}

//The frame presented by the GPU on the host's window itself (GPU::show(): Android's, where its Vulkan renderer has
//the window), with nothing read back: the frame buffer straight from its target, or memory's picture where the GPU
//doesn't hold it; kept for shot() too. False where the GPU doesn't present, has just given up, or couldn't put this
//frame on the window (none, as in the background, or the acquiring timed out): the screen's then, read back.
auto System::present() -> bool {
  handWindow();
  if(!gpu || !gpu->presents()) return false;
  auto& display = kernel.display;
  u32 width = display.width ? display.width : 480, height = display.height ? display.height : 272;
  u32 physical = display.frameBuffer & 0x1fff'ffff;  //(VRAM's first copy alone: the others aren't the same bytes)
  bool fromTarget = false;
  if(ge.renderer && display.frameBuffer && physical >= Memory::VRAMBase &&
     physical - Memory::VRAMBase < Memory::VRAMSize) {
    fromTarget = gpu->show(physical - Memory::VRAMBase, display.bufferWidth, display.pixelFormat & 3, width, height);
  }
  if(!fromTarget) kernel.picture(pixels), gpu->show(pixels, width, height);
  if(!gpu->presents() || !gpu->presented()) return false;
  //(the picture presented last: memory's, or the newest the GPU has finished presenting from a target)
  std::lock_guard lock{shotMutex};
  if(!fromTarget) std::swap(shotPixels, pixels), shotWidth = width, shotHeight = height, shotFormat = 3;
  else gpu->shot(shotPixels, shotWidth, shotHeight, shotFormat);
  return true;
}

auto System::speak() -> void {
  //The sound: the speakers are owed 735.7 frames a frame, the PSP's 44.1 kHz (the stream runs at that rate, and
  //ares converts it to the host's). They get every frame the kernel's channels have made up to its clock
  //(audio.cpp), then silence for any its clock didn't reach: the program ended, or nothing will run again. Its clock
  //may run a few cycles past the frame (the CPU finishes the block it's in), which owes nothing more.
  soundOwed += 44'100.0 * 1001 / 60'000;
  sound.clear();
  kernel.audioOutput(sound);
  for(u32 n = 0; n + 1 < sound.size(); n += 2) stream->frame(sound[n] / 32768.0, sound[n + 1] / 32768.0);
  soundOwed -= f64(sound.size() / 2);
  for(; soundOwed >= 1; soundOwed -= 1) stream->frame(0.0, 0.0);
  soundOwed = std::max(soundOwed, 0.0);
}

auto System::load(Node::System& root, string name) -> bool {
  if(node) unload();

  node = std::make_shared<Core::System>("PlayStation Portable");
  node->setAttribute("configuration", name);
  node->setGame(std::bind_front(&System::game, this));
  node->setRun(std::bind_front(&System::run, this));
  node->setPower(std::bind_front(&System::power, this));
  node->setSave(std::bind_front(&System::save, this));
  node->setUnload(std::bind_front(&System::unload, this));
  node->setSerialize(std::bind_front(&System::serialize, this));
  node->setUnserialize(std::bind_front(&System::unserialize, this));
  root = node;
  if(!node->setPak(pak = platform->pak(node))) return false;

  //(the screen's picture as large as the GPU's is read back, up to MostShown times the PSP's, its own size the PSP's:
  //front ends lay it out as 480x272)
  shown = renderer == "Vulkan" ? std::min(resolution, MostShown) : 1;
  screen = node->append<Node::Video::Screen>("Screen", 480 * shown, 272 * shown);
  screen->colors(1 << 24, [](n32 color) -> n64 {
    u64 a = 65535;
    u64 r = image::normalize(color >>  0 & 255, 8, 16);
    u64 g = image::normalize(color >>  8 & 255, 8, 16);
    u64 b = image::normalize(color >> 16 & 255, 8, 16);
    return a << 48 | r << 32 | g << 16 | b << 0;
  });
  screen->setSize(480 * shown, 272 * shown);
  screen->setScale(1.0 / shown, 1.0 / shown);
  screen->setAspect(1.0, 1.0);
  screen->setViewport(0, 0, 480 * shown, 272 * shown);
  screen->refreshRateHint(60'000.0 / 1001);

  stream = node->append<Node::Audio::Stream>("Audio");
  stream->setChannels(2);
  stream->setFrequency(44'100);

  controls.load(node);

  drive = node->append<Node::Port>("UMD Drive");
  drive->setFamily("PlayStation Portable");
  drive->setType("Universal Media Disc");
  drive->setAllocate([&](auto name) { return allocate(drive); });
  drive->setConnect([&] { connect(); });
  drive->setDisconnect([&] { disconnect(); });
  return true;
}

auto System::unload() -> void {
  if(!node) return;
  save();
  disconnect();
  //The machine goes too, until the next game: the files the program left open (on the memory stick, still open on
  //the host), its threads, its 64 MiB of memory and the compiled code. power() makes them all again.
  kernel.power();
  ge.setRenderer(nullptr);
  gpu.reset();  //(after the kernel's power, which has put back what it drew; and the window let go of)
  presents = false;
  {
    std::lock_guard lock{windowMutex};
    hold(gpuWindow, false), gpuWindow = nullptr;
  }
  gpuFailed = false;
  kernel.devices.clear();
  kernel.disc.reset();
  kernel.systemFonts.clear();
  memory.scratchpad = {};
  memory.vram = {};
  memory.ram = {};
  memory.watched = {};
  pageTable = {};
  cpu.pages = nullptr;
  cpu.watched = nullptr;
  cpu.recompiler.sections.clear();
  cpu.recompiler.sections.shrink_to_fit();
  cpu.recompiler.writePages.clear();
  cpu.recompiler.writePages.shrink_to_fit();
  cpu.recompiler.sectionTable.clear();  //(compiled code reads it, so it mustn't outlive the sections it points at)
  cpu.recompiler.sectionTable.shrink_to_fit();
  cpu.recompiler.table = nullptr;
  cpu.recompiler.allocator.reset();
  pixels = {};
  sound = {};
  if(screen) {
    screen->quit();  //stops the screen's video thread
    node->remove(screen);
  }
  if(stream) node->remove(stream);
  screen.reset();
  stream.reset();
  controls = {};
  drive.reset();
  pak.reset();
  node.reset();
}

//Nothing to write back: the memory stick is a host folder, written as the game writes it.
auto System::save() -> void {
}

auto System::power(bool reset) -> void {
  for(auto& setting : node->find<Node::Setting::Setting>()) setting->setLatch();

  //(what the hardware renderer drew is the old memory's: let go)
  ge.setRenderer(nullptr);
  if(gpu) gpu->drop();
  memory.power(64_MiB);
  memory.buildPages(pageTable);
  cpu.pages = pageTable.data();
  cpu.watched = memory.watched.data();
  memory.watching = [this](u32 page) { cpu.recompiler.protect(page); };
  memory.vramGuard = [this](bool busy) {  //(the pages the GE's workers draw over: memory.hpp)
    for(u32 offset = 0; offset < Memory::VRAMSize; offset += Memory::PageSize) {
      if(!memory.vramPageBusy(offset / Memory::PageSize)) continue;
      u32 page = (Memory::VRAMBase + offset) / Memory::PageSize;
      pageTable[page] = busy ? nullptr : &memory.vram[offset];
      cpu.recompiler.writable(page);
    }
  };
  memory.written = [this](u32 address, u32 size) { cpu.recompiler.invalidateRange(address, size); };
  memory.unmapped = [this](u32 address, bool store) {
    //Under HLE nothing should reach these; a game that does is reported, but only so often.
    if(unmappedReports < 16 && ++unmappedReports) {
      report(true, std::string{store ? "a store to " : "a load from "} + hex(address) + ", where nothing is");
    }
  };
  //An exception nothing handles (under HLE, a crash in the game): it's reported once, and the game ends there.
  cpu.exceptionHook = [this](Allegrex::Exception exception, u32 address) {
    report(true, "the CPU stopped: exception " + std::to_string((u32)exception) + " at " + hex(address));
    cpu.scc.halted = 1;
    kernel.exited = true;
  };
  cpu.recompiler.enabled = recompile;
  u32 cores = std::thread::hardware_concurrency();  //(0 where the host can't tell)
  u32 most = std::min(cores ? 2 * cores : MostGeThreads, MostGeThreads);
  ge.setThreads(geThreads ? std::min(geThreads, most) : std::max(1u, cores ? cores - 1 : 1));
  startRenderer();
  unmappedReports = 0;
  soundOwed = 0;
  programHash = 0;  //until a program starts

  kernel.output = [this](const std::string& text) { report(false, text); };
  kernel.log = [this](const std::string& text) { report(true, text); };
  kernel.power();
  //The devices are the system's to give: the memory stick here, the disc in startProgram(); none left from the game
  //before.
  kernel.devices.clear();
  kernel.disc.reset();
  if(memoryStick) {
    //A memory stick as a PSP formats it: games keep their saves in PSP/SAVEDATA, homebrew lives in PSP/GAME.
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path{(const char*)memoryStick} / "PSP" / "GAME", error);
    std::filesystem::create_directories(std::filesystem::path{(const char*)memoryStick} / "PSP" / "SAVEDATA", error);
    kernel.mount("ms0", (const char*)memoryStick);
  } else {
    report(true, "no memory stick folder: ms0: isn't there");
  }
  //the system's fonts, read from the owner's folder at each power on, as a PSP has them in its flash
  kernel.fontsFrom((const char*)fonts);
  startProgram();
}

//The game: a program from the game's pak, started with the path a PSP would give it (the program's folder on the
//memory stick if it's there, else its folder as the disc), or a disc image (startDisc()).
auto System::startProgram() -> void {
  if(!gamePak) return report(true, "no game in the UMD drive");
  for(auto name : {"program.pbp", "program.elf", "program.prx"}) {
    auto fp = gamePak->read(name);
    if(!fp) continue;
    std::vector<u8> program(fp->size());
    fp->read({program.data(), program.size()});

    //Its folder and its own file name on the host, when it came from a folder there.
    std::filesystem::path location{(const char*)gamePak->attribute("location")};
    std::filesystem::path folder = location.parent_path(), file = location.filename();
    std::string path = std::string{"disc0:/"} + (file.empty() ? std::string{"EBOOT.PBP"} : file.string());
    std::error_code error;
    if(!folder.empty() && std::filesystem::is_directory(folder, error)) {
      std::filesystem::path inside;
      std::filesystem::path stick{(const char*)memoryStick};
      if(memoryStick) inside = std::filesystem::relative(folder, stick, error);
      if(!error && !inside.empty() && *inside.begin() != "..") {
        //on the memory stick already: it runs from there ("." when it's in the memory stick's top folder)
        path = "ms0:/" + (inside / file).lexically_normal().generic_string();
      } else {
        kernel.mount("disc0", folder.string());  //umd0: is another name for it
      }
    }
    std::string problem;
    programHash = hash(program);
    if(!kernel.start(program.data(), program.size(), path, problem)) {
      report(true, "can't start the program: " + problem);
    }
    return;
  }
  for(auto name : {"disc.iso", "disc.cso", "disc.zso", "disc.dax", "disc.jso", "disc.chd"}) {
    if(auto fp = gamePak->read(name)) return startDisc(fp);
  }
  report(true, "the game has no program in it");
}

//A disc image: it goes in the drive (disc0:, umd0:), and its program starts (startDiscProgram()).
auto System::startDisc(std::shared_ptr<vfs::file> fp) -> void {
  auto image = std::make_shared<Disc>();
  std::string problem;
  auto read = [fp](u64 offset, void* data, u64 size) -> u64 {
    if(offset >= fp->size()) return 0;
    size = std::min<u64>(size, fp->size() - offset);
    if(auto bytes = fp->data()) {  //the whole image mapped into memory: read straight from it
      memcpy(data, bytes + offset, size);
    } else {
      fp->seek(offset);
      fp->read({(u8*)data, size});
    }
    return size;
  };
  //a CHD: its sectors come unpacked from its hunks (nall's Decode::CHD), and the disc reads them as an ISO's bytes
  u8 magic[8] = {};
  if(read(0, magic, sizeof(magic)) == sizeof(magic) && !memcmp(magic, "MComprHD", 8)) {
    #if defined(ARES_ENABLE_CHD)
    auto chd = std::make_shared<Decode::CHD>();
    if(!chd->load(read, fp->size())) {
      return report(true, std::string{"can't read the disc: the CHD can't be read: "} + chd->error.data());
    }
    if(!chd->dvd()) return report(true, "can't read the disc: the CHD is a CD's; a UMD's is made with createdvd");
    auto sectors = [chd](u64 offset, void* data, u64 size) -> u64 {
      u64 done = 0;
      while(done < size) {
        u64 at = offset + done;
        auto sector = chd->read(u32(at / Disc::SectorSize));
        if(sector.size() != Disc::SectorSize) break;  //past the end, or a damaged hunk
        u64 count = std::min<u64>(size - done, Disc::SectorSize - at % Disc::SectorSize);
        memcpy((u8*)data + done, sector.data() + at % Disc::SectorSize, count);
        done += count;
      }
      return done;
    };
    u64 size = u64(chd->sectorCount()) * Disc::SectorSize;
    if(!image->open(sectors, size, problem)) return report(true, "can't read the disc: " + problem);
    return startDiscProgram(image);
    #else
    return report(true, "can't read the disc: this build doesn't read CHD images");
    #endif
  }
  if(!image->open(read, fp->size(), problem)) return report(true, "can't read the disc: " + problem);
  startDiscProgram(image);
}

//The disc in the drive, and its program started: PSP_GAME/SYSDIR/EBOOT.BIN, decrypted first when it's encrypted
//("~PSP" at its start), as a shop-bought game's is. A plain BOOT.BIN beside it (a few early games have one; most
//have none, or an empty or blank one) starts only if EBOOT.BIN can't: its tag names a key Phobos doesn't have, say,
//which is reported. Only an ELF, an EBOOT.PBP or an encrypted program is started. A truncated image may cut its
//program short: it's read as far as the image goes (PPSSPP lets such games boot, as truncated images are common),
//and no further than 64 MiB. A program that can't start leaves nothing behind (Kernel::load()), so the next is
//tried on a machine as fresh.
auto System::startDiscProgram(std::shared_ptr<Disc> image) -> void {
  kernel.disc = image;
  std::string problems;  //why each program found couldn't start
  for(auto name : {"EBOOT.BIN", "BOOT.BIN"}) {
    Disc::Entry entry;
    if(!image->find({"PSP_GAME", "SYSDIR", name}, entry) || entry.folder || entry.size < 4) continue;
    u64 start = u64(entry.sector) * Disc::SectorSize;
    if(start >= image->size()) continue;  //a damaged record: nothing of it is on the disc
    u64 size = std::min<u64>({entry.size, image->size() - start, 64_MiB});
    u8 magic[4];
    if(size < 4 || !image->read(start, 4, magic)) continue;
    bool runnable = !memcmp(magic, "~PSP", 4) || !memcmp(magic, "~SCE", 4) || !memcmp(magic, "\x7f" "ELF", 4) ||
                    !memcmp(magic, "\0PBP", 4);
    if(!runnable) continue;
    std::vector<u8> program(size);
    if(!image->read(start, size, program.data())) continue;
    std::string problem;
    if(kernel.load(program.data(), program.size(), std::string{"disc0:/PSP_GAME/SYSDIR/"} + name, problem)) {
      programHash = hash(program);
      if(!problems.empty()) report(true, "can't start " + problems + "; " + name + " starts instead");
      return;
    }
    problems += (problems.empty() ? "" : "; ") + std::string{name} + ": " + problem;
  }
  report(true, problems.empty() ? "the disc has no program" : "can't start the game: " + problems);
}

//Save states: everything the PSP was doing, to carry on from exactly there. A state starts with a header: a
//signature, the version of its layout, RAM's size and the program it was made with, all of which must be the
//machine's; then memory, the CPU, the GE and the kernel. The version goes up whenever the layout changes, or what a
//field means: 17 since a message dialog counts Updates before an abort for its fade length (part 42); 16 since a
//message dialog counts the Updates it takes to finish once aborted (part 42); 15 since the
//disc drive keeps whether the game deactivated it (part 37); 14 since threads keep their
//run figures, the kernel when the running one got the CPU, lightweight
//mutexes their names, attributes and first counts, and the display a base for its count of lines (part 32); 13 since
//the kernel holds scePsmfPlayer's player and threads may wait for it (part 31); 12 since the
//GE keeps its last bounding box's result for BJUMP, and calls into the program say
//which are the GE's callbacks (part 29); 11 since the kernel holds
//mutexes, alarms and virtual timers, calls into the program pass four
//arguments and say whether they're a timer's handler, and the controller's idle thresholds, the GE's translation
//width and the HOME menu's language are kept (part 28); 10 since sceAtrac3plus, sceMp3 and sceMpeg decode, keeping
//their streams' places (part 26); 9 since
//sceMpegRingbufferPut calls a ringbuffer's callback, part way through when a state is saved (part 25); 8 since the
//font library holds libraries, open fonts and its calls into the program (part 23); 7 since
//sound (docs/psp-core.md's part 21) and part 22 merged, each branch having made a version 6 of
//its own: part 21's as sound came to be heard (the output the channels make, the SRC channel's place in its samples,
//VAG voices' decoders), part 22's as interrupts held off came to keep the CPU for the thread holding them and their
//flag became the CPU's alone (mfic and mtic's, on as a program starts), the kernel keeping no copy of it (a version
//5 state's flag held off for a thread that wasn't the holder would leave it spinning, every wait refused); 5 since
//the kernel holds files' asynchronous requests, sceSas, message pipes, mailboxes and the other functions of
//docs/psp-core.md's part 20 (and threads' message pipe transfers); 4 since
//each call into the program says whether it's a vertical blank's handler; 3 when the kernel came to hold both the
//modules the program loaded and its threads', semaphores' and callbacks' new fields (with pools, sound and the
//dialogs), each of which came first on a branch of its own as a version 2, two layouts that differ from each other
//and from these. A state of any older version is refused by it.
static constexpr u32 StateSignature = 0x5350'5350;  //"PSPS"
static constexpr u32 StateVersion = 17;

//The program that started, to tell it from any other: an FNV-1a hash of all its bytes. A state is only loaded into
//the program it was made with, as another's memory, threads and files mean nothing to it.
auto System::hash(std::span<const u8> bytes) -> u64 {
  u64 value = 0xcbf2'9ce4'8422'2325;
  for(u8 byte : bytes) value = (value ^ byte) * 0x100'0000'01b3;
  return value;
}

//Writes the header, or reads one and says whether it's this machine's.
auto System::header(serializer& s) -> bool {
  u32 signature = StateSignature, version = StateVersion, ramSize = memory.ram.size();
  u64 program = programHash;
  s(signature);
  s(version);
  s(ramSize);
  s(program);
  return signature == StateSignature && version == StateVersion && ramSize == memory.ram.size() &&
         program == programHash;
}

//A save state: none (an empty one, which front ends don't keep) once the program has ended, by leaving or by
//crashing: there's nothing to carry on from, and a state of it would only bring back where it stopped.
auto System::serialize(bool synchronize) -> serializer {
  if(kernel.exited) return {};
  return snapshot();
}

//The machine as it is, as a state (unserialize() makes one to go back to, whether the program has ended or not).
auto System::snapshot() -> serializer {
  serializer s;
  header(s);
  memory.serialize(s);
  cpu.serialize(s);
  ge.serialize(s);
  kernel.serialize(s);
  s(soundOwed);
  s(unmappedReports);
  return s;
}

//The machine from what follows a state's header, which ends `length` bytes in: false if the state is damaged (a
//value no machine could hold, or the state ends part way), with the machine left part loaded. Every frame leaves
//less than one sound frame owed (run()), so a state owing more, or a negative or not-a-number amount, is damaged.
auto System::restore(serializer& s, u32 length) -> bool {
  memory.serialize(s);
  cpu.serialize(s);
  bool valid = ge.serialize(s);
  valid = kernel.serialize(s) && valid;
  s(soundOwed);
  s(unmappedReports);
  return valid && soundOwed >= 0 && soundOwed < 1 && s.size() <= length;
}

//Loads a state; false, with the machine as it was, if it isn't this machine's (nothing is touched then) or turns out
//damaged part way (the machine is put back from a state of itself made first, which loads as any of its own does).
//Its open files are set aside as they are and put back the same, still open: a state reopens files by their paths,
//which a file the program removed or renamed while it had it open no longer has. Should even putting the machine
//back fail, it starts the game afresh rather than run on from half of each.
auto System::unserialize(serializer& s) -> bool {
  u32 length = s.capacity();  //a state to load holds exactly its bytes
  if(!header(s)) return false;
  serializer before = snapshot();
  u32 saved = before.size();
  auto files = std::move(kernel.files);
  kernel.files.clear();
  bool loaded = restore(s, length);
  if(!loaded) {
    before.setReading();
    if(!header(before) || !restore(before, saved)) {
      report(true, "a damaged state couldn't be undone: the game starts again");
      power(false);
      return false;
    }
    kernel.files = std::move(files);
  }
  cpu.recompiler.reset();  //what it compiled came from memory as it was before
  return loaded;
}

//The hardware renderer the owner chose, made and checked once a game (the software renderer drawing the game where it
//couldn't start or its pixels aren't the software renderer's, said once), and the GE's from here on.
auto System::startRenderer() -> void {
  if(renderer != "Vulkan") {
    gpu.reset(), presents = false;
    std::lock_guard lock{windowMutex};
    return hold(gpuWindow, false), void(gpuWindow = nullptr);
  }
  if(!gpu && !gpuFailed) {
    std::string error;
    gpu = GPU::vulkan(vulkanLoader, error);
    //(where blending in the shader in rasterization order fails it, the check again with the draws that read apart:
    //each after a barrier, their overlapping primitives in draws of their own, which every GPU orders)
    bool passed = gpu && gpu->check(error);
    if(gpu && !passed && gpu->backend->readsInOrder) {
      report(true, "the Vulkan renderer's start-up check, blending in the shader in order, failed (" + error +
                   "): checked again with overlaps apart");
      gpu->backend->readsInOrder = false, error.clear();
      passed = gpu->check(error);
    }
    if(gpu && !passed) {
      error += " on " + gpu->backend->name() + (gpu->backend->reads ? ", blending in the shader" : "");
      gpu.reset();
    }
    if(gpu && failCheck) gpu.reset(), error = "its start-up check made to fail, as the debug switch asks";
    if(!gpu) {
      gpuFailed = true;
      return tell("The Vulkan renderer couldn't start (" + error + "): the software renderer draws instead");
    }
    //(the check is drawn at the PSP's resolution, the game at the one chosen, as much of it as the GPU takes)
    gpu->resolution(resolution);
    report(false, "the Vulkan renderer draws, on " + gpu->backend->name() + ", at " +
                  std::to_string(gpu->resolution()) + "x" +
                  (gpu->resolution() < resolution ? " (the most this GPU takes)" : "") +
                  (!gpu->backend->reads ? ""
                   : gpu->backend->readsInOrder ? ", blending in the shader, in order"
                                                : ", blending in the shader, overlaps apart"));
    gpu->report = [this](const std::string& what) { tell("The Vulkan renderer stopped: " + what); };
  }
  if(gpu && gpu->ready()) ge.setRenderer(gpu.get());
  //(presenting from the first frame on, so that the host never takes the window first)
  presents = gpu && gpu->presents();
}

//Something the owner should know: to the log, and for the front end to show (notice()).
auto System::tell(const std::string& text) -> void {
  report(true, text);
  std::lock_guard lock{noticeLock};
  pendingNotice = text;
}

//What the program writes, and the kernel's notes: to the log (on Android, logcat's "PSP" tag).
auto System::report(bool problem, const std::string& text) -> void {
  #if defined(__ANDROID__)
  __android_log_print(problem ? ANDROID_LOG_WARN : ANDROID_LOG_INFO, "PSP", "%s", text.c_str());
  #else
  fprintf(stderr, "PSP: %s%s", text.c_str(), !text.empty() && text.back() == '\n' ? "" : "\n");
  #endif
}

//The game in the drive is a disc, even a homebrew program standing for one: front ends give a "...Disc" its medium.
auto System::allocate(Node::Port port) -> Node::Peripheral {
  return disc = port->append<Node::Peripheral>("PlayStation Portable Disc");
}

auto System::connect() -> void {
  disc->setPak(gamePak = platform->pak(disc));
}

auto System::disconnect() -> void {
  gamePak.reset();
  disc.reset();
}

auto System::Controls::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("Controls");
  up       = node->append<Node::Input::Button>("Up");
  down     = node->append<Node::Input::Button>("Down");
  left     = node->append<Node::Input::Button>("Left");
  right    = node->append<Node::Input::Button>("Right");
  triangle = node->append<Node::Input::Button>("Triangle");
  circle   = node->append<Node::Input::Button>("Circle");
  cross    = node->append<Node::Input::Button>("Cross");
  square   = node->append<Node::Input::Button>("Square");
  l        = node->append<Node::Input::Button>("L");
  r        = node->append<Node::Input::Button>("R");
  select   = node->append<Node::Input::Button>("Select");
  start    = node->append<Node::Input::Button>("Start");
  //named as the PlayStation's DualShock names its left stick, so front ends map it the same way
  x        = node->append<Node::Input::Axis>("L-Stick X");
  y        = node->append<Node::Input::Axis>("L-Stick Y");
}

auto System::Controls::poll() -> void {
  for(auto& button : {up, down, left, right, triangle, circle, cross, square, l, r, select, start}) {
    platform->input(button);
  }
  platform->input(x);
  platform->input(y);
}

//pspsdk's PSP_CTRL_* bits (pspctrl.h).
auto System::Controls::buttons() const -> u32 {
  u32 bits = 0;
  if(select->value())   bits |= 0x0001;
  if(start->value())    bits |= 0x0008;
  if(up->value())       bits |= 0x0010;
  if(right->value())    bits |= 0x0020;
  if(down->value())     bits |= 0x0040;
  if(left->value())     bits |= 0x0080;
  if(l->value())        bits |= 0x0100;
  if(r->value())        bits |= 0x0200;
  if(triangle->value()) bits |= 0x1000;
  if(circle->value())   bits |= 0x2000;
  if(cross->value())    bits |= 0x4000;
  if(square->value())   bits |= 0x8000;
  return bits;
}

//A front end gives -32768 (left or up) to 32767 (right or down); the PSP reads 0 to 255, 128 in the middle.
auto System::Controls::stick(const Node::Input::Axis& axis) const -> u8 {
  s64 value = std::clamp<s64>(axis->value(), -32768, 32767);
  return (value + 32768) >> 8;
}

}
