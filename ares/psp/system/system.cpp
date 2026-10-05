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
  //Save states come later: until then a state holds nothing, and loading one fails.
  node->setSerialize([](bool) -> serializer { return {}; });
  node->setUnserialize([](serializer&) -> bool { return false; });
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
    if(!kernel.start(program.data(), program.size(), path, problem)) {
      report(true, "can't start the program: " + problem);
    }
    return;
  }
  for(auto name : {"disc.iso", "disc.cso"}) {
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
  std::string problem;
  if(!image->open(read, fp->size(), problem)) return report(true, "can't read the disc: " + problem);
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
    if(!kernel.start(program.data(), program.size(), std::string{"disc0:/PSP_GAME/SYSDIR/"} + name, problem)) {
      report(true, "can't start the game: " + problem);
    }
    return;
  }
  report(true, encrypted ? "the game's program is encrypted, which isn't read yet" : "the disc has no program");
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
