#include <algorithm>

namespace ares::PlayStationPortable {

auto enumerate() -> std::vector<string> {
  return {"[Sony] PlayStation Portable"};
}

auto load(Node::System& node, string name) -> bool {
  auto list = enumerate();
  if(std::find(list.begin(), list.end(), name) == list.end()) return false;
  return system.load(node, name);
}

//What the front end tells the core before loading: "Memory Stick", the host folder standing for ms0:; "Recompiler",
//"true" to run the CPU's recompiler (the default) or "false" to run the interpreter alone.
auto option(string name, string value) -> bool {
  if(name == "Memory Stick") system.memoryStick = value;
  if(name == "Recompiler") system.recompile = value.boolean();
  return true;
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

  //The screen's colors are the pixels' own (red in the low byte, then green and blue: the palette below).
  kernel.picture(pixels);
  //as picture() made it (480x272: the PSP has no other)
  u64 width = kernel.display.width ? kernel.display.width : 480;
  u64 height = kernel.display.height ? kernel.display.height : 272;
  auto output = screen->pixels().data();
  if(width * height == pixels.size()) {
    for(u32 y : range(std::min<u64>(height, 272))) {
      for(u32 x : range(std::min<u64>(width, 480))) output[y * 480 + x] = pixels[y * width + x] & 0xff'ffff;
    }
  }
  screen->frame();

  soundOwed += 44'100.0 * 1001 / 60'000;
  while(soundOwed >= 1) {
    stream->frame(0.0, 0.0);
    soundOwed -= 1;
  }
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

  screen = node->append<Node::Video::Screen>("Screen", 480, 272);
  screen->colors(1 << 24, [](n32 color) -> n64 {
    u64 a = 65535;
    u64 r = image::normalize(color >>  0 & 255, 8, 16);
    u64 g = image::normalize(color >>  8 & 255, 8, 16);
    u64 b = image::normalize(color >> 16 & 255, 8, 16);
    return a << 48 | r << 32 | g << 16 | b << 0;
  });
  screen->setSize(480, 272);
  screen->setScale(1.0, 1.0);
  screen->setAspect(1.0, 1.0);
  screen->setViewport(0, 0, 480, 272);
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
  kernel.devices.clear();
  kernel.disc.reset();
  memory.scratchpad = {};
  memory.vram = {};
  memory.ram = {};
  pageTable = {};
  cpu.pages = nullptr;
  cpu.recompiler.sections.clear();
  cpu.recompiler.sections.shrink_to_fit();
  cpu.recompiler.writePages.clear();
  cpu.recompiler.writePages.shrink_to_fit();
  cpu.recompiler.allocator.reset();
  pixels = {};
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

  memory.power(64_MiB);
  memory.buildPages(pageTable);
  cpu.pages = pageTable.data();
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

//A disc image: it goes in the drive (disc0:, umd0:), and its program starts, PSP_GAME/SYSDIR/EBOOT.BIN. A shop-bought
//game's is encrypted ("~PSP" at its start), which isn't read yet; a plain BOOT.BIN beside it stands in for it if
//there's one (a few early games have one; most have none, or an empty or blank one). Only an ELF or an EBOOT.PBP is
//started. A truncated image may cut its program short: it's read as far as the image goes (PPSSPP lets such games
//boot, as truncated images are common), and no further than 64 MiB.
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

//The disc in the drive, and its program started.
auto System::startDiscProgram(std::shared_ptr<Disc> image) -> void {
  std::string problem;
  kernel.disc = image;
  bool encrypted = false;
  for(auto name : {"EBOOT.BIN", "BOOT.BIN"}) {
    Disc::Entry entry;
    if(!image->find({"PSP_GAME", "SYSDIR", name}, entry) || entry.folder || entry.size < 4) continue;
    u64 start = u64(entry.sector) * Disc::SectorSize;
    if(start >= image->size()) continue;  //a damaged record: nothing of it is on the disc
    u64 size = std::min<u64>({entry.size, image->size() - start, 64_MiB});
    u8 magic[4];
    if(size < 4 || !image->read(start, 4, magic)) continue;
    if(!memcmp(magic, "~PSP", 4)) encrypted = true;
    if(memcmp(magic, "\x7f" "ELF", 4) && memcmp(magic, "\0PBP", 4)) continue;
    std::vector<u8> program(size);
    if(!image->read(start, size, program.data())) continue;
    programHash = hash(program);
    if(!kernel.start(program.data(), program.size(), std::string{"disc0:/PSP_GAME/SYSDIR/"} + name, problem)) {
      report(true, "can't start the game: " + problem);
    }
    return;
  }
  report(true, encrypted ? "the game's program is encrypted, which isn't read yet" : "the disc has no program");
}

//Save states: everything the PSP was doing, to carry on from exactly there. A state starts with a header: a
//signature, the version of its layout, RAM's size and the program it was made with, all of which must be the
//machine's; then memory, the CPU, the GE and the kernel.
static constexpr u32 StateSignature = 0x5350'5350;  //"PSPS"
static constexpr u32 StateVersion = 1;

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
