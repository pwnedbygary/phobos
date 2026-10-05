//The PSP as an ares system, as Phobos's front ends meet it (ares/psp/system): the node tree they walk, a homebrew
//program put in the UMD drive and run a frame at a time, the frame it shows reaching the front end, the controls as
//the system hands them to the kernel, and the memory stick folder.
//
//A front end here is TestPlatform: it hands the core the game's files, keeps the frames the core shows, and says
//which controls are held, as Phobos's Android runner does for real.
//
//The checks that run a program need the test programs (tools/psp-test-programs): PSP_TEST_PROGRAMS names the folder
//holding hello.elf and disc.elf. Without it, those checks are skipped.
#include <psp/psp.hpp>
#include "../disc-image.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <fcntl.h>
#include <unistd.h>

namespace ares { Platform* platform = nullptr; }

namespace {

using namespace ares;
using ares::PlayStationPortable::enumerate;
namespace fs = std::filesystem;

auto& psp = ares::PlayStationPortable::system;  //the PSP the tests drive (the core keeps one)

int checks = 0, failures = 0;

auto check(bool passed, const char* file, int line, const std::string& what) -> bool {
  checks++;
  if(!passed) {
    failures++;
    std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
  }
  return passed;
}
#define CHECK(condition, what) check(condition, __FILE__, __LINE__, what)

//What a front end does for the core: hands it the game's files, shows its frames, and reports the controls.
struct TestPlatform : ares::Platform {
  std::shared_ptr<vfs::directory> game;  //the disc's pak, or none
  std::map<std::string, s64> held;       //the controls by node name: a button 0 or 1, the stick -32768 to 32767

  std::mutex mutex;                      //frames arrive on the screen's own thread
  std::vector<u32> frame;                //the last one shown, as ARGB
  u32 frameWidth = 0, frameHeight = 0, frames = 0;

  auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override {
    if(node->name() == "PlayStation Portable Disc" && game) return game;
    return std::make_shared<vfs::directory>();
  }

  auto video(Node::Video::Screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
    std::lock_guard lock{mutex};
    frame.assign(width * height, 0);
    for(u32 y : range(height)) {
      for(u32 x : range(width)) frame[y * width + x] = data[y * (pitch / 4) + x];
    }
    frameWidth = width, frameHeight = height, frames++;
  }

  auto input(Node::Input::Input node) -> void override {
    std::string name = (const char*)node->name();
    if(auto button = node->cast<Node::Input::Button>()) button->setValue(held[name] != 0);
    if(auto axis = node->cast<Node::Input::Axis>()) axis->setValue(held[name]);
  }
};

TestPlatform host;
fs::path scratch;  //a folder of the tests' own, for memory sticks

//A pak as mia's PSP medium makes it: the program under the name the core looks for, and where it is on the host.
auto programPak(const fs::path& file, const char* name) -> std::shared_ptr<vfs::directory> {
  auto pak = std::make_shared<vfs::directory>();
  pak->setAttribute("title", "test program");
  pak->setAttribute("location", file.string().c_str());
  pak->append(name, vfs::disk::open(file.string().c_str(), vfs::read));
  return pak;
}

//Loads the PSP, puts the game in its drive and turns it on, as Phobos's runner does.
auto start(Node::System& root) -> bool {
  if(!PlayStationPortable::load(root, "[Sony] PlayStation Portable")) return false;
  auto drive = root->find<Node::Port>("UMD Drive");
  if(!drive) return false;
  drive->allocate();
  drive->connect();
  root->power();
  return true;
}

//Runs frames until the program leaves (or a few seconds of the PSP's time pass), keeping what it prints.
auto runToExit(Node::System& root, std::string& printed) -> void {
  psp.kernel.output = [&](const std::string& text) { printed += text; };
  for(u32 frame = 0; frame < 300 && !psp.kernel.exited; frame++) root->run();
}

//Whether the recompiler compiled any code (it keeps each compiled block in its section of memory).
auto compiledAny() -> bool {
  auto& sections = psp.cpu.recompiler.sections;
  return std::any_of(sections.begin(), sections.end(), [](auto& section) { return (bool)section; });
}

//The path the program was started with (its argv[0]), as the kernel copied it for the first thread.
auto startPath() -> std::string {
  char path[256] = {};
  psp.memory.copyOut(path, ares::PlayStationPortable::Kernel::Trampoline + 0x100, sizeof(path) - 1);
  return path;
}

auto names() -> void {
  std::printf("the PSP's name, and no other\n");
  auto list = enumerate();
  CHECK(list.size() == 1 && list[0] == "[Sony] PlayStation Portable", "enumerate() lists the PSP alone");
  Node::System root;
  CHECK(!PlayStationPortable::load(root, "[Sony] PlayStation"), "another system's name loads");
}

auto nodeTree() -> void {
  std::printf("the node tree front ends walk\n");
  host.game.reset();
  Node::System root;
  if(!CHECK(PlayStationPortable::load(root, "[Sony] PlayStation Portable"), "the PSP loads")) return;
  CHECK(root->name() == "PlayStation Portable", "the system node's name");
  auto screen = root->find<Node::Video::Screen>("Screen");
  CHECK(screen && screen->width() == 480 && screen->height() == 272, "a 480x272 screen");
  auto stream = root->find<Node::Audio::Stream>("Audio");
  CHECK(stream && stream->channels() == 2 && stream->frequency() == 44'100, "stereo sound at 44.1 kHz");
  //named as the PlayStation's are, so front ends map them the same way
  for(auto name : {"Up", "Down", "Left", "Right", "Triangle", "Circle", "Cross", "Square", "L", "R", "Select",
                    "Start"}) {
    CHECK((bool)root->find<Node::Input::Button>(string{"Controls/", name}), std::string{"a button "} + name);
  }
  for(auto name : {"L-Stick X", "L-Stick Y"}) {
    CHECK((bool)root->find<Node::Input::Axis>(string{"Controls/", name}), std::string{"an axis "} + name);
  }
  auto drive = root->find<Node::Port>("UMD Drive");
  CHECK(drive && drive->type() == "Universal Media Disc" && drive->family() == "PlayStation Portable", "a UMD drive");
  if(drive) {
    //front ends give a peripheral whose name ends in "Disc" the game's medium
    auto disc = drive->allocate();
    CHECK(disc && disc->name() == "PlayStation Portable Disc", "the drive takes a disc");
  }
  root->unload();
}

//A program run from its own folder, which stands for its disc; its picture through to the front end; the controls.
auto program(const fs::path& programs, bool recompile) -> void {
  std::printf("a homebrew program in the drive, run a frame at a time\n");
  auto stick = scratch / "stick";
  PlayStationPortable::option("Memory Stick", stick.string().c_str());
  host.game = programPak(programs / "hello.elf", "program.elf");
  host.held.clear();
  Node::System root;
  if(!CHECK(start(root), "the PSP starts with hello.elf in the drive")) return;
  CHECK(fs::is_directory(stick / "PSP" / "GAME") && fs::is_directory(stick / "PSP" / "SAVEDATA"),
    "the memory stick is formatted as a PSP formats one");
  auto& devices = psp.kernel.devices;
  CHECK(devices.count("ms0") && devices.at("ms0") == stick.string(), "ms0: is the memory stick folder");
  auto folder = (programs / "hello.elf").parent_path().string();
  CHECK(devices.count("disc0") && devices.at("disc0") == folder, "disc0: is the program's folder");
  CHECK(psp.kernel.workingDirectory == "disc0:/", "the program starts in its disc's folder");

  CHECK(startPath() == "disc0:/hello.elf", "argv[0] is the program on its disc");
  std::string printed;
  runToExit(root, printed);
  CHECK(printed.find("hello from a PSP program 42") != std::string::npos, "hello prints its line");
  CHECK(psp.kernel.exited, "hello leaves");
  CHECK(psp.cpu.recompiler.enabled == recompile && compiledAny() == recompile, "it ran on the engine asked for");

  //Its frame stays as it left it, so the frame the front end gets can be compared exactly: the display's frame
  //buffer as kernel.picture() reads it (red in the low byte, then green and blue), through the screen's palette, to
  //the front end's ARGB.
  std::vector<u32> picture;
  psp.kernel.picture(picture);
  u32 shown = 0;
  {
    std::lock_guard lock{host.mutex};
    shown = host.frames;
  }
  for(u32 tries = 0; tries < 200; tries++) {  //the screen's thread presents frames as it gets to them
    root->run();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::lock_guard lock{host.mutex};
    if(host.frames > shown + 1) break;
  }
  {
    std::lock_guard lock{host.mutex};
    bool sized = host.frameWidth == 480 && host.frameHeight == 272 && picture.size() == 480 * 272;
    if(CHECK(sized, "the front end gets 480x272 frames")) {
      u32 differ = 0, lit = 0;
      for(u32 n : range(480 * 272)) {
        u32 p = picture[n];
        u32 argb = 0xff00'0000 | (p & 255) << 16 | (p >> 8 & 255) << 8 | (p >> 16 & 255);
        if(host.frame[n] != argb) differ++;
        if(argb != 0xff00'0000) lit++;
      }
      CHECK(differ == 0, "the frame shown is the display's (" + std::to_string(differ) + " pixels differ)");
      CHECK(lit > 0, "the picture has the program's text in it");
    }
  }

  //The controls: each button as the PSP's controller reports it (pspsdk's PSP_CTRL_* bits), and the stick from the
  //front end's range to the PSP's 0 to 255, with 128 in the middle. The system reads them every frame, even after
  //the program has left.
  struct Button { const char* name; u32 bit; };
  for(auto [name, bit] : {Button{"Select", 0x0001}, {"Start", 0x0008}, {"Up", 0x0010}, {"Right", 0x0020},
                          {"Down", 0x0040}, {"Left", 0x0080}, {"L", 0x0100}, {"R", 0x0200}, {"Triangle", 0x1000},
                          {"Circle", 0x2000}, {"Cross", 0x4000}, {"Square", 0x8000}}) {
    host.held = {{name, 1}};
    root->run();
    CHECK(psp.kernel.controller.buttons == bit, std::string{name} + " is its own bit");
  }
  host.held = {{"L-Stick X", -32768}, {"L-Stick Y", 32767}};
  root->run();
  CHECK(psp.kernel.controller.analogX == 0 && psp.kernel.controller.analogY == 255, "the stick's far corner");
  host.held = {{"L-Stick X", 0}, {"L-Stick Y", -1}};
  root->run();
  CHECK(psp.kernel.controller.analogX == 128 && psp.kernel.controller.analogY == 127, "the stick's middle");
  host.held.clear();

  //States come later: until then a state holds nothing, which front ends must refuse to save.
  CHECK(root->serialize(true).size() == 0, "an empty state");
  root->unload();
}

//A program already on the memory stick runs from there, as the PSP's menu starts it, with no disc.
auto programOnTheMemoryStick(const fs::path& programs) -> void {
  std::printf("a program on the memory stick runs from ms0:\n");
  auto stick = scratch / "stick2";
  auto folder = stick / "PSP" / "GAME" / "HELLO";
  fs::create_directories(folder);
  fs::copy_file(programs / "hello.elf", folder / "hello.elf", fs::copy_options::overwrite_existing);
  PlayStationPortable::option("Memory Stick", stick.string().c_str());
  host.game = programPak(folder / "hello.elf", "program.elf");
  Node::System root;
  if(!CHECK(start(root), "the PSP starts with the memory stick's program in the drive")) return;
  CHECK(psp.kernel.workingDirectory == "ms0:/PSP/GAME/HELLO", "it starts in its folder on the memory stick");
  CHECK(startPath() == "ms0:/PSP/GAME/HELLO/hello.elf", "argv[0] is the program on the memory stick");
  CHECK(!psp.kernel.devices.count("disc0"), "no disc: the last game's folder is gone");
  std::string printed;
  runToExit(root, printed);
  CHECK(printed.find("hello from a PSP program 42") != std::string::npos, "it runs");
  root->unload();

  //in the memory stick's top folder, its path has no "." in it
  fs::copy_file(programs / "hello.elf", stick / "hello.elf", fs::copy_options::overwrite_existing);
  host.game = programPak(stick / "hello.elf", "program.elf");
  if(!CHECK(start(root), "the PSP starts with a program in the memory stick's top folder")) return;
  CHECK(startPath() == "ms0:/hello.elf", "argv[0] is the program in the memory stick's top folder");
  root->unload();
}

//FNV-1a, the fingerprint the disc program prints of what it read.
auto fingerprint(const std::vector<std::uint8_t>& bytes, size_t offset, size_t size) -> std::string {
  u32 hash = 2166136261u;
  for(size_t n = offset; n < offset + size; n++) hash = (hash ^ bytes[n]) * 16777619u;
  char text[16];
  snprintf(text, sizeof(text), "%08x", hash);
  return text;
}

//A disc image in the drive, as an ISO and as a CSO: its program (tools/psp-test-programs' disc) boots from it and
//reads it every way games do, printing what it found, which must be what the image holds. A shop-bought game's
//encrypted program doesn't start (it isn't read yet); a plain BOOT.BIN beside it does.
auto discImage(const fs::path& programs) -> void {
  std::printf("a disc image in the drive: its program boots and reads it\n");
  std::ifstream stream(programs / "disc.elf", std::ios::binary);
  std::vector<std::uint8_t> program((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if(!CHECK(!program.empty(), "disc.elf is among the test programs")) return;
  std::vector<std::uint8_t> data(3 * 2048 + 1000);
  for(size_t n = 0; n < data.size(); n++) data[n] = u8(n * 7 + n / 251);
  auto image = disc_image::makeIso({
    {"PSP_GAME/PARAM.SFO", std::vector<std::uint8_t>(64, 1)},
    {"PSP_GAME/SYSDIR/EBOOT.BIN", program},
    {"PSP_GAME/USRDIR/DATA.BIN", data},
    {"UMD_DATA.BIN", std::vector<std::uint8_t>(16, 2)},
  });
  std::string expected = "medium 1\nactivate 0\nwait 0\ndrive 32\n"
    "data " + std::to_string(data.size()) + " " + fingerprint(data, 0, data.size()) + "\n"
    "ioctl 0\nstat " + std::to_string(data.size()) + " 216d 1\n"
    "lbn 4096 " + fingerprint(data, 0, 4096) + "\n"
    "umd0 1 " + fingerprint(data, 0, 2048) + "\n"
    "entry PARAM.SFO\nentry SYSDIR/\nentry USRDIR/\n"
    "relative 1\nwrite 8001b004\n";
  struct Format { const char* name; std::vector<std::uint8_t> bytes; };
  for(auto& [name, bytes] : {Format{"disc.iso", image.bytes}, Format{"disc.cso", disc_image::makeCso(image.bytes)}}) {
    //from a file on the host, as mia's medium gives it (mapped into memory)
    auto file = scratch / name;
    std::ofstream(file, std::ios::binary).write((const char*)bytes.data(), bytes.size());
    host.game = std::make_shared<vfs::directory>();
    host.game->setAttribute("location", file.string().c_str());
    host.game->append(name, vfs::disk::open(file.string().c_str(), vfs::read));
    Node::System root;
    if(!CHECK(start(root), std::string{"the PSP starts with "} + name + " in the drive")) continue;
    CHECK(psp.kernel.disc != nullptr && !psp.kernel.devices.count("disc0"), "the image is the disc");
    CHECK(psp.kernel.workingDirectory == "disc0:/PSP_GAME/SYSDIR", "it starts in its folder on the disc");
    std::string printed;
    runToExit(root, printed);
    CHECK(psp.kernel.exited, "the disc's program runs to its end");
    if(!CHECK(printed == expected, std::string{"it reads "} + name + " as it is")) {
      std::printf("    printed:\n%s    expected:\n%s", printed.c_str(), expected.c_str());
    }
    root->unload();
  }

  //a truncated image that cuts its program short (here, only padding after it): it still boots
  {
    auto padded = program;
    padded.resize(program.size() + 8192, 0);
    auto bytes = disc_image::makeIso({
      {"PSP_GAME/USRDIR/DATA.BIN", data},
      {"PSP_GAME/SYSDIR/EBOOT.BIN", padded},
    }).bytes;
    bytes.resize(bytes.size() - 4096);
    host.game = std::make_shared<vfs::directory>();
    host.game->append("disc.iso", vfs::memory::open({bytes.data(), bytes.size()}));
    Node::System root;
    if(CHECK(start(root), "the PSP starts with a truncated disc in the drive")) {
      CHECK(!psp.kernel.threads.empty(), "the program cut short starts");
      root->unload();
    }
  }

  //encrypted, alone, then with a blank BOOT.BIN beside it, then with a plain one
  std::vector<std::uint8_t> encrypted(256, 0);
  memcpy(encrypted.data(), "~PSP", 4);
  for(u32 kind : {0u, 1u, 2u}) {
    bool boot = kind == 2;
    std::vector<disc_image::File> files = {{"PSP_GAME/SYSDIR/EBOOT.BIN", encrypted}};
    if(kind == 1) files.push_back({"PSP_GAME/SYSDIR/BOOT.BIN", std::vector<std::uint8_t>(4096, 0)});
    if(kind == 2) files.push_back({"PSP_GAME/SYSDIR/BOOT.BIN", program});
    auto bytes = disc_image::makeIso(files).bytes;
    host.game = std::make_shared<vfs::directory>();
    host.game->append("disc.iso", vfs::memory::open({bytes.data(), bytes.size()}));
    Node::System root;
    if(!CHECK(start(root), "the PSP starts with an encrypted disc in the drive")) continue;
    CHECK(psp.kernel.threads.empty() != boot, boot ? "BOOT.BIN starts" : "the encrypted program doesn't start");
    if(boot) CHECK(psp.kernel.workingDirectory == "disc0:/PSP_GAME/SYSDIR", "BOOT.BIN starts from its folder");
    root->unload();
  }
}

//nall's vfs::descriptor, which mia's PSP medium reads a disc image through when Android hands it over as an open
//descriptor ("/proc/self/fd/N"): the file's bytes, mapped into memory or read a piece at a time; zeros past the end;
//still readable after the descriptor it was given is closed; nothing for a bad descriptor or a folder. Then a disc
//booted through one, unmapped, so the system reads the image a piece at a time.
auto descriptorFiles(const fs::path& programs) -> void {
  std::printf("files read through a descriptor\n");
  std::vector<std::uint8_t> bytes(10000);
  for(size_t n = 0; n < bytes.size(); n++) bytes[n] = u8(n * 13 + n / 97);
  auto path = scratch / "descriptor.bin";
  std::ofstream(path, std::ios::binary).write((const char*)bytes.data(), bytes.size());
  for(bool map : {true, false}) {
    int given = ::open(path.string().c_str(), O_RDONLY);
    auto file = vfs::descriptor::open(given, map);
    ::close(given);
    if(!CHECK(file && file->size() == bytes.size(), "it opens, its size the file's")) continue;
    CHECK((file->data() != nullptr) == map, map ? "mapped into memory" : "not mapped");
    std::vector<std::uint8_t> out(100);
    file->seek(5000);
    file->read({out.data(), out.size()});
    CHECK(std::equal(out.begin(), out.end(), bytes.begin() + 5000), "it reads the file's bytes");
    file->seek(9990);
    file->read({out.data(), 20});
    bool zeros = std::all_of(out.begin() + 10, out.begin() + 20, [](u8 byte) { return byte == 0; });
    CHECK(std::equal(out.begin(), out.begin() + 10, bytes.begin() + 9990) && zeros, "zeros past the end");
    CHECK(file->offset() == 10010, "the position moves past the end too");
  }
  CHECK(!vfs::descriptor::open(-1), "no file for a bad descriptor");
  int folder = ::open(scratch.string().c_str(), O_RDONLY);
  CHECK(!vfs::descriptor::open(folder), "no file for a folder");
  ::close(folder);

  std::ifstream stream(programs / "disc.elf", std::ios::binary);
  std::vector<std::uint8_t> program((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  auto image = disc_image::makeIso({{"PSP_GAME/SYSDIR/EBOOT.BIN", program},
                                    {"PSP_GAME/USRDIR/DATA.BIN", std::vector<std::uint8_t>(7144, 3)}});
  auto disc = scratch / "descriptor.iso";
  std::ofstream(disc, std::ios::binary).write((const char*)image.bytes.data(), image.bytes.size());
  int given = ::open(disc.string().c_str(), O_RDONLY);
  host.game = std::make_shared<vfs::directory>();
  host.game->append("disc.iso", vfs::descriptor::open(given, false));
  ::close(given);
  Node::System root;
  if(CHECK(start(root), "the PSP starts with a disc read through a descriptor")) {
    std::string printed;
    runToExit(root, printed);
    CHECK(printed.find("data 7144 ") != std::string::npos && printed.find("relative 1") != std::string::npos,
      "its program reads it");
    root->unload();
  }
}

//Where the host gives no memory that code may run from, the recompiler turns itself off, and the interpreter runs
//the program instead. (Asking for no code memory at all stands for that: the host refuses the mapping.)
auto noCodeMemory(const fs::path& programs) -> void {
  std::printf("no memory for compiled code: the interpreter runs the program\n");
  PlayStationPortable::option("Memory Stick", (scratch / "stick").string().c_str());
  PlayStationPortable::option("Recompiler", "true");
  host.game = programPak(programs / "hello.elf", "program.elf");
  psp.cpu.recompiler.codeMemory = 0;
  Node::System root;
  if(CHECK(start(root), "the PSP starts")) {
    std::string printed;
    runToExit(root, printed);
    CHECK(!psp.cpu.recompiler.enabled && !compiledAny(), "the recompiler turned itself off");
    CHECK(printed.find("hello from a PSP program 42") != std::string::npos, "the program ran all the same");
    root->unload();
  }
  psp.cpu.recompiler.codeMemory = 32_MiB;
}

}

auto main() -> int {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  //each line out as it's printed, should a check crash
  ares::platform = &host;
  scratch = fs::temp_directory_path() / ("phobos-psp-ares-" + std::to_string(::getpid()));
  fs::remove_all(scratch);
  fs::create_directories(scratch);

  names();
  nodeTree();
  if(auto programs = std::getenv("PSP_TEST_PROGRAMS"); programs && fs::exists(fs::path{programs} / "hello.elf")) {
    //on the recompiler (as the front ends run it) and on the interpreter, its fallback
    for(bool recompile : {true, false}) {
      std::printf("[%s]\n", recompile ? "recompiler" : "interpreter");
      PlayStationPortable::option("Recompiler", recompile ? "true" : "false");
      program(fs::path{programs}, recompile);
      programOnTheMemoryStick(fs::path{programs});
    }
    noCodeMemory(fs::path{programs});
    discImage(fs::path{programs});
    descriptorFiles(fs::path{programs});
  } else {
    std::printf("PSP_TEST_PROGRAMS isn't set (or has no hello.elf): the checks that run a program are skipped\n");
  }

  fs::remove_all(scratch);
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
