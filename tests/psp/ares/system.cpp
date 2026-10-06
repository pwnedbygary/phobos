//The PSP as an ares system, as Phobos's front ends meet it (ares/psp/system): the node tree they walk, a homebrew
//program put in the UMD drive and run a frame at a time, the frame it shows and the sound it makes reaching the
//front end, the controls as the system hands them to the kernel, and the memory stick folder.
//
//A front end here is TestPlatform: it hands the core the game's files, keeps the frames the core shows, and says
//which controls are held, as Phobos's Android runner does for real.
//
//The checks that run a program need the test programs (tools/psp-test-programs): PSP_TEST_PROGRAMS names the folder
//holding hello.elf and disc.elf. Without it, those checks are skipped.
#include <psp/psp.hpp>
#include "../disc-formats.hpp"
#include "../encrypt.hpp"
#include "../font-maker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
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
  std::vector<s32> sound;                //the sound the stream hands on, left and right, as 16-bit samples

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

  auto audio(Node::Audio::Stream stream) -> void override {
    while(stream->pending()) {
      f64 samples[2];
      stream->read(samples);
      for(f64 sample : samples) sound.push_back(s32(std::lround(sample * 32768.0)));
    }
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

//What the system reports while something runs: on the host it writes its reports to standard error, which is sent to
//a file for the while.
auto reports(const std::function<void()>& run) -> std::string {
  auto path = scratch / "reports.txt";
  std::fflush(stderr);
  int saved = ::dup(2), file = ::open(path.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  ::dup2(file, 2);
  ::close(file);
  run();
  std::fflush(stderr);
  ::dup2(saved, 2);
  ::close(saved);
  std::ifstream stream(path);
  return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
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

//A disc image in the drive, as an ISO and in each compressed form (CSO, CSO version 2, ZSO, DAX, JSO, and CHD in
//hunks of one sector and of four): its program (tools/psp-test-programs' disc) boots from it and reads it every way
//games do, printing what it found, which must be what the image holds. A CD's CHD, or one holding only its
//differences from another, isn't taken as the disc. The program encrypted, as a shop-bought game's EBOOT.BIN is,
//boots and reads its disc the same, rather than a plain BOOT.BIN beside it; one that can't be decrypted doesn't
//start, saying why, and a plain BOOT.BIN beside it starts instead.
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
  std::vector<Format> formats = {
    {"disc.iso", image.bytes},
    {"disc.cso", disc_image::makeCso(image.bytes)},
    {"disc.cso", disc_image::makeCso2(image.bytes)},
    {"disc.zso", disc_image::makeZso(image.bytes)},
    {"disc.dax", disc_image::makeDax(image.bytes, {{0, 1}})},
    {"disc.jso", disc_image::makeJso(image.bytes, 2048, true)},
    {"disc.chd", disc_image::makeChd(image.bytes)},
    {"disc.chd", disc_image::makeChd(image.bytes, 8192)},
  };
  for(auto& [name, bytes] : formats) {
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

  //a CD's CHD (2448-byte units), and a CHD whose parent's SHA-1 is set: neither is the disc
  for(u32 kind : {0u, 1u}) {
    auto bytes = disc_image::makeChd(image.bytes, kind ? 2048 : 2448 * 8, kind ? 2048 : 2448);
    if(kind) bytes[104] = 1;
    host.game = std::make_shared<vfs::directory>();
    host.game->append("disc.chd", vfs::memory::open({bytes.data(), bytes.size()}));
    Node::System root;
    start(root);
    CHECK(psp.kernel.disc == nullptr, kind ? "a CHD needing its parent isn't the disc" : "a CD's isn't the disc");
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

  //the program encrypted as Sony's tools encrypt a game's (../encrypt.hpp), under a tag with a 144-byte key (type 1)
  //and one with a 16-byte key (type 2), the second packed with gzip, a plain BOOT.BIN beside it: EBOOT.BIN starts
  for(u32 tag : {0xc0cb'167cu, 0xd916'13f0u}) {
    psp_encrypt::Options options;
    options.tag = tag;
    options.gzip = tag == 0xd916'13f0;
    auto bytes = disc_image::makeIso({
      {"PSP_GAME/PARAM.SFO", std::vector<std::uint8_t>(64, 1)},
      {"PSP_GAME/SYSDIR/BOOT.BIN", program},
      {"PSP_GAME/SYSDIR/EBOOT.BIN", psp_encrypt::encrypt(program, options)},
      {"PSP_GAME/USRDIR/DATA.BIN", data},
      {"UMD_DATA.BIN", std::vector<std::uint8_t>(16, 2)},
    }).bytes;
    host.game = std::make_shared<vfs::directory>();
    host.game->append("disc.iso", vfs::memory::open({bytes.data(), bytes.size()}));
    Node::System root;
    if(!CHECK(start(root), "the PSP starts with an encrypted program on its disc")) continue;
    CHECK(startPath() == "disc0:/PSP_GAME/SYSDIR/EBOOT.BIN", "the encrypted EBOOT.BIN starts, not BOOT.BIN");
    std::string printed;
    runToExit(root, printed);
    if(!CHECK(printed == expected, "decrypted, it reads its disc as it is")) {
      std::printf("    printed:\n%s    expected:\n%s", printed.c_str(), expected.c_str());
    }
    root->unload();
  }

  //programs that can't be decrypted: one cut short, one whose tag names no key Phobos has; alone, then with a blank
  //BOOT.BIN beside it, then with a plain one, which starts; each time the reason is reported
  std::vector<std::uint8_t> cutShort(256, 0);
  memcpy(cutShort.data(), "~PSP", 4);
  auto unknownTag = psp_encrypt::encrypt(program);
  psp_encrypt::put32(unknownTag.data() + 0xd0, 0x1234'5678);
  for(auto& [encrypted, why] : {std::pair{cutShort, "cut short in its header"},
                                std::pair{unknownTag, "tag 0x12345678) whose tag names a key Phobos doesn't have"}}) {
    for(u32 kind : {0u, 1u, 2u}) {
      bool boot = kind == 2;
      std::vector<disc_image::File> files = {{"PSP_GAME/SYSDIR/EBOOT.BIN", encrypted}};
      if(kind == 1) files.push_back({"PSP_GAME/SYSDIR/BOOT.BIN", std::vector<std::uint8_t>(4096, 0)});
      if(kind == 2) files.push_back({"PSP_GAME/SYSDIR/BOOT.BIN", program});
      auto bytes = disc_image::makeIso(files).bytes;
      host.game = std::make_shared<vfs::directory>();
      host.game->append("disc.iso", vfs::memory::open({bytes.data(), bytes.size()}));
      Node::System root;
      bool started = false;
      auto reported = reports([&] { started = start(root); });
      if(!CHECK(started, "the PSP starts with an encrypted disc in the drive")) continue;
      CHECK(psp.kernel.threads.empty() != boot, boot ? "BOOT.BIN starts" : "the encrypted program doesn't start");
      if(boot) CHECK(psp.kernel.workingDirectory == "disc0:/PSP_GAME/SYSDIR", "BOOT.BIN starts from its folder");
      CHECK(reported.find("EBOOT.BIN: an encrypted program") != std::string::npos &&
            reported.find(why) != std::string::npos, "why EBOOT.BIN can't start is reported");
      CHECK((reported.find("BOOT.BIN starts instead") != std::string::npos) == boot,
            boot ? "and that BOOT.BIN starts instead" : "and nothing starts instead");
      root->unload();
    }
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

//Save states (System::serialize() and each part's): a game saved part way and loaded again carries on exactly as it
//did the first time, frame by frame, on either engine; in a fresh session too; and a state that isn't one, or is cut
//short, is refused with the game as it was.
auto states(const fs::path& programs) -> void {
  std::printf("save states\n");
  //each frame: the picture the game shows, its time and where the CPU is; at the end, RAM and VRAM
  auto frame = [&]() -> u64 {
    std::vector<u32> picture;
    psp.kernel.picture(picture);
    u64 hash = 1469598103934665603ull;
    for(u32 pixel : picture) hash = (hash ^ pixel) * 1099511628211ull;
    for(u64 value : {psp.kernel.cycles, u64(psp.cpu.ipu.pc), u64(psp.kernel.vblanks)}) {
      hash = (hash ^ value) * 1099511628211ull;
    }
    return hash;
  };
  auto memoryHash = [&]() -> u64 {
    u64 hash = 1469598103934665603ull;
    for(u8 byte : psp.memory.ram) hash = (hash ^ byte) * 1099511628211ull;
    for(u8 byte : psp.memory.vram) hash = (hash ^ byte) * 1099511628211ull;
    return hash;
  };
  auto play = [&](Node::System& root) {
    std::vector<u64> frames;
    for(u32 n = 0; n < 20; n++) {
      root->run();
      frames.push_back(frame());
    }
    frames.push_back(memoryHash());
    return frames;
  };
  for(bool recompile : {true, false}) {
    PlayStationPortable::option("Recompiler", recompile ? "true" : "false");
    PlayStationPortable::option("Memory Stick", (scratch / "stick").string().c_str());
    host.game = programPak(programs / "cube.elf", "program.elf");
    Node::System root;
    if(!CHECK(start(root), "the PSP starts with cube.elf")) continue;
    for(u32 n = 0; n < 30; n++) root->run();
    auto state = root->serialize(true);
    //memory's pieces that hold anything but zeros, and the rest of the machine besides
    u32 used = 0;
    for(u32 at = 0; at < psp.memory.ram.size(); at += 4_KiB) {
      used += std::any_of(psp.memory.ram.begin() + at, psp.memory.ram.begin() + at + 4_KiB, [](u8 b) { return b; });
    }
    CHECK(state.size() > used * 4_KiB && state.size() < used * 4_KiB + 3_MiB, "a state holds memory that's used");
    auto first = play(root);
    serializer load{state.data(), state.size()};
    CHECK(root->unserialize(load), "the state loads");
    std::string engine = recompile ? "recompiler" : "interpreter";
    CHECK(play(root) == first, "it carries on exactly as it did (" + engine + ")");

    root->unload();  //a fresh session of the same game
    if(CHECK(start(root), "the PSP starts again")) {
      serializer fresh{state.data(), state.size()};
      CHECK(root->unserialize(fresh), "the state loads in a fresh session");
      CHECK(play(root) == first, "and carries on exactly as it did");
    }

    //not a state, states cut short, and one owing endless sound: refused, the whole machine as it was. Cut by 4 KiB,
    //the kernel's lists run out of state; cut by 4 bytes, only the state's end shows it (its last value, the count of
    //unmapped accesses reported, is read past it). The sound owed (the 8 bytes before that) is under one frame's
    //after every frame.
    auto same = [](const serializer& a, const serializer& b) {
      return a.size() == b.size() && !memcmp(a.data(), b.data(), a.size());
    };
    //...and a file the program has open whose host file is gone (removed while open, so reopening it by its path, as
    //loading does, can't find it): still open after each refused load, still reading
    auto openPath = scratch / "stick" / "OPEN.TXT";
    std::ofstream(openPath) << "still here";
    PlayStationPortable::Kernel::OpenFile open;
    open.path = "ms0:/OPEN.TXT";
    open.host = openPath.string();
    open.flags = 0x0001;  //PSP_O_RDONLY
    open.stream = std::make_unique<std::fstream>(openPath, std::ios::binary | std::ios::in);
    u32 number = psp.kernel.nextFile++;
    psp.kernel.files[number] = std::move(open);
    fs::remove(openPath);
    auto before = root->serialize(true);
    std::vector<u8> bytes(state.data(), state.data() + state.size());
    bytes[0] ^= 0xff;
    serializer wrong{bytes.data(), u32(bytes.size())};
    CHECK(!root->unserialize(wrong), "a state with the wrong signature is refused");
    //states of the layouts before this one (the header's second word): version 1, before part 17's threads,
    //semaphores and callbacks and part 19's modules; version 2, which part 17's branch and parts 18 and 19's each
    //laid out their own way; version 3, before calls into the program said which are the vertical blank's
    //handlers; version 4, before files' asynchronous requests, sceSas, message pipes and mailboxes; version 5,
    //before sound was heard and interrupts held off kept the CPU; version 6, which part 21's branch (the channels'
    //output, the SRC channel's place, VAG voices' decoders) and part 22's (the interrupt flag the CPU's alone, its
    //meaning changed) each laid out their own way; version 7, before the font library's libraries, fonts and calls
    //into the program; version 8, before a ringbuffer's callback could be part way through; version 9, before
    //sceAtrac3plus, sceMp3 and sceMpeg kept their streams' places (part 26). Each is refused by its header, before
    //anything is touched (even the compiled code, which any load throws away).
    for(u8 version : {1, 2, 3, 4, 5, 6, 7, 8, 9}) {
      bytes.assign(state.data(), state.data() + state.size());
      bytes[4] = version, bytes[5] = bytes[6] = bytes[7] = 0;
      serializer old{bytes.data(), u32(bytes.size())};
      CHECK(!root->unserialize(old), "a state of version " + std::to_string(version) + " is refused");
      CHECK(compiledAny() == recompile && same(root->serialize(true), before), "before anything is touched");
    }
    serializer cut{state.data(), u32(state.size() - 4096)};
    CHECK(!root->unserialize(cut), "a state cut short is refused");
    serializer clipped{state.data(), u32(state.size() - 4)};
    CHECK(!root->unserialize(clipped), "a state 4 bytes short is refused");
    bytes.assign(state.data(), state.data() + state.size());
    f64 endless = std::numeric_limits<f64>::infinity();
    memcpy(bytes.data() + bytes.size() - 12, &endless, sizeof(endless));
    serializer owing{bytes.data(), u32(bytes.size())};
    CHECK(!root->unserialize(owing), "a state owing endless sound is refused");
    CHECK(same(root->serialize(true), before), "the machine is as it was, every part of it");
    auto kept = psp.kernel.files.find(number);
    std::string text(10, '\0');
    if(kept != psp.kernel.files.end() && kept->second.stream) {
      kept->second.stream->seekg(0);
      kept->second.stream->read(text.data(), text.size());
    }
    CHECK(text == "still here", "the file whose host file is gone is still open");
    root->unload();

    //another program's state (pspsdk's blend sample, which runs on, as cube does): refused before anything is touched
    host.game = programPak(programs / "blend.elf", "program.elf");
    if(CHECK(start(root), "the PSP starts with another program")) {
      auto other = root->serialize(true);
      serializer cube{state.data(), state.size()};
      CHECK(!root->unserialize(cube), "it refuses cube's state");
      CHECK(same(root->serialize(true), other), "and is as it was");
      root->unload();
    }
  }

  //a program that has ended (hello leaves at once) makes no state: there's nothing to carry on from
  PlayStationPortable::option("Memory Stick", (scratch / "stick").string().c_str());
  host.game = programPak(programs / "hello.elf", "program.elf");
  if(Node::System ended; CHECK(start(ended), "the PSP starts with hello")) {
    std::string printed;
    runToExit(ended, printed);
    CHECK(psp.kernel.exited && !ended->serialize(true).size(), "a program that has ended makes no state");
    ended->unload();
  }

  //a disc's program is the one its states go with: cube booted from a disc takes its state back, and a disc holding
  //another program refuses it
  auto disc = [&](const char* program) {
    std::ifstream stream(programs / program, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    auto image = disc_image::makeIso({{"PSP_GAME/SYSDIR/EBOOT.BIN", bytes}}).bytes;
    host.game = std::make_shared<vfs::directory>();
    host.game->append("disc.iso", vfs::memory::open({image.data(), image.size()}));
  };
  disc("cube.elf");
  Node::System root;
  if(CHECK(start(root), "the PSP starts with cube on a disc")) {
    for(u32 n = 0; n < 10; n++) root->run();
    auto state = root->serialize(true);
    auto first = play(root);
    serializer load{state.data(), state.size()};
    CHECK(root->unserialize(load) && play(root) == first, "its state loads, and it carries on as it did");
    root->unload();
    disc("hello.elf");
    if(CHECK(start(root), "the PSP starts with another disc")) {
      serializer cube{state.data(), state.size()};
      CHECK(!root->unserialize(cube), "which refuses cube's state");
      root->unload();
    }
  }
}

//What the speakers get (System::run(), Kernel::audioOutput()): the stream, at the PSP's 44.1 kHz (taken here at
//44.1 kHz too, where ares's resampler hands each sample on as it is, a sample late), gets 735.7 frames a frame. While
//cube runs (it makes no sound of its own), a buffer handed to a mixer channel as sceAudioOutputPanned would hand it
//is heard in it sample for sample, from the frame it's handed over at: its left at 0x8000 as it is, its right at
//0x4000 halved (rounded down). A program that has ended (hello leaves at once) still gets silence at that rate.
auto sound(const fs::path& programs) -> void {
  std::printf("sound reaching the front end\n");
  PlayStationPortable::option("Recompiler", "true");
  PlayStationPortable::option("Memory Stick", (scratch / "stick").string().c_str());
  host.game = programPak(programs / "cube.elf", "program.elf");
  Node::System root;
  if(!CHECK(start(root), "the PSP starts with cube.elf")) return;
  auto stream = root->find<Node::Audio::Stream>("Audio");
  if(!CHECK((bool)stream, "the stream is there")) return root->unload();
  stream->setResamplerFrequency(44'100);
  host.sound.clear();
  for(u32 n = 0; n < 60; n++) root->run();
  u64 frames = host.sound.size() / 2;
  CHECK(frames >= 60 * 735 && frames <= 60 * 736 + 2, "735.7 frames a frame: " + std::to_string(frames) + " in 60");
  CHECK(std::all_of(host.sound.begin(), host.sound.end(), [](s32 sample) { return !sample; }), "cube is silent");
  //a ramp of 2048 stereo samples, in a block of its own, handed to channel 0
  auto& kernel = psp.kernel;
  auto block = kernel.allocate(2048 * 4, 0, 0, "sound test");
  if(!CHECK(block != nullptr, "memory for the buffer")) return root->unload();
  auto ramp = [](u32 n) { return s32(n * 15) - 15000; };
  for(u32 n = 0; n < 2048; n++) {
    psp.memory.write(2, block->address + n * 4, u16(ramp(n)));
    psp.memory.write(2, block->address + n * 4 + 2, u16(ramp(n)));
  }
  auto& channel = kernel.audio.channels[0];
  channel.reserved = true, channel.sampleCount = 2048, channel.format = 0;
  host.sound.clear();
  kernel.handOver(0, block->address, 0x8000, 0x4000);
  for(u32 n = 0; n < 10; n++) root->run();
  auto& heard = host.sound;
  u32 at = 0;  //where the ramp starts in what the stream handed on
  while(at + 2 < heard.size() && !(heard[at] == ramp(0) && heard[at + 2] == ramp(1))) at += 2;
  bool exact = at + 2048 * 2 <= heard.size();
  for(u32 n = 0; n < 2048 && exact; n++) {
    exact = heard[at + n * 2] == ramp(n) && heard[at + n * 2 + 1] == (ramp(n) * 0x4000 >> 15);
  }
  CHECK(exact, "a mixer channel's buffer heard sample for sample, its right at half");
  CHECK(at / 2 < 4, "from the frame it was handed over at: " + std::to_string(at / 2) + " frames on");
  root->unload();

  host.game = programPak(programs / "hello.elf", "program.elf");
  if(CHECK(start(root), "the PSP starts with hello")) {
    root->find<Node::Audio::Stream>("Audio")->setResamplerFrequency(44'100);
    std::string printed;
    runToExit(root, printed);
    host.sound.clear();
    for(u32 n = 0; n < 60; n++) root->run();
    frames = host.sound.size() / 2;
    bool rate = frames >= 60 * 735 && frames <= 60 * 736 + 2;
    CHECK(psp.kernel.exited && rate, "a program that has ended: 735.7 frames a frame");
    CHECK(std::all_of(host.sound.begin(), host.sound.end(), [](s32 sample) { return !sample; }), "of silence");
    root->unload();
  }
}

//How many threads draw the GE's pictures (option "GE Threads", taken as the PSP powers on): 0, the default, all the
//host's cores but one; a count as asked, but no more than twice the host's cores, nor 64, however large.
auto drawingThreads() -> void {
  std::printf("the GE's drawing threads\n");
  host.game.reset();
  u32 cores = std::thread::hardware_concurrency();
  u32 most = std::min(cores ? 2 * cores : 64, 64u);
  struct Case { const char* asked; u32 threads; };
  for(auto [asked, threads] : {Case{"0", std::max(1u, cores ? cores - 1 : 1)}, Case{"1", 1},
                               Case{"2", std::min(2u, most)}, Case{"1000", most}, Case{"4294967297", most}}) {
    PlayStationPortable::option("GE Threads", asked);
    Node::System root;
    reports([&] {
      if(!CHECK(start(root), std::string{"the PSP starts, GE Threads "} + asked)) return;
      CHECK(psp.ge.drawing.threads == threads, std::string{"GE Threads "} + asked + ": " +
            std::to_string(psp.ge.drawing.threads) + " threads, not " + std::to_string(threads));
      root->unload();
    });
  }
  PlayStationPortable::option("GE Threads", "0");
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

//The system's fonts (option "Fonts", the owner's folder of PGF files): none without it; with it, the folder's, read
//as the PSP powers on, in the list's eighteen places; none kept once the game unloads. (The font library itself is
//tests/psp's font.cpp.)
auto systemFonts(const fs::path& programs) -> void {
  std::printf("the system's fonts from the Fonts option\n");
  PlayStationPortable::option("Memory Stick", (scratch / "stick").string().c_str());
  host.game = programPak(programs / "hello.elf", "program.elf");
  pgf_maker::FontFolder folder({0, 1});
  Node::System root;
  PlayStationPortable::option("Fonts", "");
  if(CHECK(start(root), "the PSP starts without fonts")) {
    CHECK(!psp.kernel.fontsInstalled() && psp.kernel.systemFonts.empty(), "no fonts without the option");
    root->unload();
  }
  PlayStationPortable::option("Fonts", folder.path.string().c_str());
  if(CHECK(start(root), "the PSP starts with the fonts")) {
    auto& fonts = psp.kernel.systemFonts;
    CHECK(psp.kernel.fontsInstalled() && fonts.size() == 18, "the folder's fonts, in the list's eighteen places");
    CHECK(fonts.size() == 18 && fonts[1].pgf && fonts[1].pgf->name == "FTT-NewRodin Pro Latin" && !fonts[2].pgf,
          "ltn0.pgf read, ltn1.pgf missing");
    root->unload();
    CHECK(psp.kernel.systemFonts.empty(), "none kept once the game unloads");
  }
  PlayStationPortable::option("Fonts", "");
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
  drawingThreads();
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
    states(fs::path{programs});
    sound(fs::path{programs});
    systemFonts(fs::path{programs});
  } else {
    std::printf("PSP_TEST_PROGRAMS isn't set (or has no hello.elf): the checks that run a program are skipped\n");
  }

  fs::remove_all(scratch);
  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
