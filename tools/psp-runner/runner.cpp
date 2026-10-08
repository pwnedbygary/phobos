//A headless front end for the PSP core (ares/psp/psp.cpp, the whole core as Phobos builds it, with ares's node
//tree): it boots a game through the ares system, as Phobos's front ends do, runs it a frame at a time, and
//reports what happens.
//
//The game: a disc image (ISO, CSO, ZSO, DAX, JSO, CHD) or a homebrew program (PBP, ELF, PRX), named in its pak
//the way mia's PSP medium names it (kind(), below, which it reuses).
//
//The report, on standard output: the kernel's notes (a function it doesn't have, say) and the game's own output,
//each with the frame it happened on; frames per second; the picture as PNGs; the sound as a WAV; and a summary at
//the end: the frames run, the unique missing functions with their counts, whether the program ended, and the last
//note. The system's own messages (a disc that can't start, say) go to standard error, as on the host.
//
//Usage:
//  psp-runner GAME [options]
//  options:
//    --frames N                 how many frames to run (default 3600, a minute of the PSP's time)
//    --press "F:C,F:C,..."      the controls: F:C presses and holds C at frame F (a button, or a stick's value:
//                                L-StickX:12345); F:C! releases C at frame F
//    --script FILE              the presses in a file, one F:C per line ("#" begins a comment)
//    --png-every K              write a PNG every K frames, into --out's folder
//    --png-at F1,F2,...         write a PNG at each of these frames, into --out's folder
//    --out DIR                  the folder the PNGs go in (needed with --png-every and --png-at)
//    --wav FILE                 the sound, as a 16-bit stereo WAV at the PSP's 44.1 kHz
//    --save-state-at F FILE     the machine's state at frame F, as a save-state file
//    --load-state FILE          start from a save-state file (the game booted, then the state loaded at once)
//    --interpreter              run the CPU's interpreter (the recompiler is the default)
//    --ge-threads N             how many threads draw the GE's pictures (0: one fewer than the host's cores)
//    --memory-stick DIR         the host folder standing for ms0: (a scratch folder by default)
//    --fonts DIR                the PSP's system fonts (the .pgf files of a PSP's flash0), for the game's text
//
//The frames its PNGs name are the frames the runner ran: after a frame's run, the runner waits for the screen's
//own thread to present a new picture (with a timeout), then writes it as a PNG named by the frame run.

#include <psp/psp.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <zlib.h>

namespace fs = std::filesystem;
using namespace ares;
using ares::PlayStationPortable::Disc;

namespace ares { Platform* platform = nullptr; }  //the front end this build hands the core (main, below)

//The file's bytes, read a piece at a time from its host file, as the system reads a disc image's (system.cpp's
//startDisc()): zeros past the end, and straight from its mapping when the file is mapped into memory.
auto readFrom(std::shared_ptr<vfs::file> fp, u64 offset, void* data, u64 size) -> u64 {
  if(offset >= fp->size()) return 0;
  size = std::min<u64>(size, fp->size() - offset);
  if(auto bytes = fp->data()) {
    memcpy(data, bytes + offset, size);
  } else {
    fp->seek(offset);
    fp->read({(u8*)data, size});
  }
  return size;
}



//What the file is (the name it goes in the pak under), or nothing if it isn't a PSP game: the disc images by
//their heads, a CHD by its version and unit size, the ISO by its primary volume descriptor, a PBP by its head,
//and an ELF or PRX by its magic and its machine (the MIPS). As mia's PSP medium names it
//(mia/medium/playstation-portable.cpp's kind()).
auto kind(const fs::path& location, vfs::file& file) -> std::string {
  u8 head[64] = {};
  file.seek(0);
  file.read({head, std::min<u64>(file.size(), sizeof(head))});
  if(!memory::compare(head, "CISO", 4)) return "disc.cso";
  if(!memory::compare(head, "ZISO", 4)) return "disc.zso";
  if(!memory::compare(head, "DAX\0", 4)) return "disc.dax";
  if(!memory::compare(head, "JISO", 4)) return "disc.jso";
  //a CHD of version 5: its unit size (big-endian) 60 bytes in
  if(!memory::compare(head, "MComprHD", 8)) {
    u32 version = head[12] << 24 | head[13] << 16 | head[14] << 8 | head[15];
    u32 unit = head[60] << 24 | head[61] << 16 | head[62] << 8 | head[63];
    return version == 5 && unit == 2048 ? "disc.chd" : "";
  }
  if(file.size() >= 0x8000 + 40) {
    u8 descriptor[40];
    file.seek(0x8000);
    file.read({descriptor, sizeof(descriptor)});
    if(!memory::compare(descriptor + 1, "CD001", 5)) {
      return !memory::compare(descriptor + 8, "PSP GAME", 8) ? "disc.iso" : "";
    }
  }
  if(!memory::compare(head, "\0PBP", 4))
    return PlayStationPortable::sfoValue(PlayStationPortable::paramSFO(file), "CATEGORY") != "ME"
      ? "program.pbp" : "";
  if(!memory::compare(head, "\x7f" "ELF", 4) && (head[18] | head[19] << 8) == 8) {
    //a PRX is told from an ELF by its name alone: both are ELF files, and the core starts either the same way
    auto stem = location.string();
    bool prx = stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".prx") == 0;
    return prx ? "program.prx" : "program.elf";
  }
  return {};
}

//The disc's title and disc ID, read off its image by the shared reader (ares/psp/kernel/disc-info.cpp), which
//reads a CHD's sectors through libchdr and the rest straight from the file's bytes.
auto discInfo(const fs::path& file, std::string& npid, std::string& title) -> bool {
  auto fp = vfs::disk::open(file.string().c_str(), vfs::read);
  if(!fp) return false;
  auto read = [fp](u64 offset, void* data, u64 size) -> u64 { return readFrom(fp, offset, data, size); };
  std::string problem;
  auto info = PlayStationPortable::readDiscInfo(read, fp->size(), problem);
  npid = info.discId;
  title = info.title;
  return !npid.empty();
}

//The game's pak, as the front end gives it: the file under the name the core looks for, and where it is on the
//host (its folder standing for its disc, so the game finds the files beside it).
auto makePak(const fs::path& file, const std::string& name, const std::string& title)
  -> std::shared_ptr<vfs::directory>
{
  auto pak = std::make_shared<vfs::directory>();
  pak->setAttribute("title", title.c_str());
  pak->setAttribute("location", file.string().c_str());
  pak->append(name.c_str(), vfs::disk::open(file.string().c_str(), vfs::read));
  return pak;
}

//A frame's picture, as a PNG: its RGB, one scanline after another, each led by a filter byte of 0 (none),
//deflated with zlib, in the format's chunks (each its type, its length, its bytes, and a CRC-32 of its type and
//bytes). The pixels arrive as the front end gets them (red in the high byte, then green, then blue).
auto writePng(const fs::path& path, const std::vector<u32>& frame, u32 width, u32 height) -> bool {
  std::vector<u8> raw((width * 3 + 1) * height);
  u8* row = raw.data();
  for(u32 y = 0; y < height; y++) {
    *row++ = 0;
    for(u32 x = 0; x < width; x++) {
      u32 pixel = frame[y * width + x];
      *row++ = pixel >> 16 & 255;
      *row++ = pixel >> 8 & 255;
      *row++ = pixel & 255;
    }
  }
  z_stream z{};
  deflateInit2(&z, Z_BEST_SPEED, Z_DEFLATED, MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
  z.next_in = raw.data();
  z.avail_in = raw.size();
  std::vector<u8> deflated(compressBound(raw.size()));
  z.next_out = deflated.data();
  z.avail_out = deflated.size();
  deflate(&z, Z_FINISH);
  deflated.resize(z.total_out);
  deflateEnd(&z);

  auto word = [](std::vector<u8>& out, u32 value) {
    out.push_back(value >> 24 & 255);
    out.push_back(value >> 16 & 255);
    out.push_back(value >> 8 & 255);
    out.push_back(value & 255);
  };
  //a chunk: its length, its type and bytes, and its CRC (of the type and bytes, as the format asks)
  auto chunk = [&](const char* type, const u8* data, u32 size) {
    std::vector<u8> out;
    word(out, size);
    u32 crc = crc32(0, (const Bytef*)type, 4);
    for(u32 n = 0; n < 4; n++) out.push_back((u8)type[n]);
    out.insert(out.end(), data, data + size);
    //(zlib's crc32 with no bytes to add, IEND's, gives its own starting value back rather than crc)
    word(out, size ? crc32(crc, data, size) : crc);
    return out;
  };

  std::vector<u8> ihdr;
  word(ihdr, width);
  word(ihdr, height);
  ihdr.push_back(8);  //bit depth
  ihdr.push_back(2);  //color type: truecolor, RGB
  ihdr.push_back(0);  //compression
  ihdr.push_back(0);  //filter
  ihdr.push_back(0);  //interlace

  auto ihdrChunk = chunk("IHDR", ihdr.data(), ihdr.size());
  auto idatChunk = chunk("IDAT", deflated.data(), deflated.size());
  auto iendChunk = chunk("IEND", nullptr, 0);
  std::vector<u8> png;
  png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  png.insert(png.end(), ihdrChunk.begin(), ihdrChunk.end());
  png.insert(png.end(), idatChunk.begin(), idatChunk.end());
  png.insert(png.end(), iendChunk.begin(), iendChunk.end());
  std::ofstream file(path, std::ios::binary);
  if(!file) return false;
  file.write((const char*)png.data(), png.size());
  return true;
}

//The sound, as a WAV: RIFF's header, its format (16-bit signed, two channels, the PSP's 44.1 kHz), and the
//samples, left and right, in the order the stream handed them on.
auto writeWav(const fs::path& path, const std::vector<s32>& samples) -> bool {
  u32 channels = 2, rate = 44'100, bits = 16;
  u32 bytes = samples.size() * 2;
  std::vector<u8> header;
  auto little16 = [&](u32 value) { header.push_back(value & 255); header.push_back(value >> 8 & 255); };
  auto little32 = [&](u32 value) {
    header.push_back(value & 255);
    header.push_back(value >> 8 & 255);
    header.push_back(value >> 16 & 255);
    header.push_back(value >> 24 & 255);
  };
  for(char c : "RIFF") header.push_back((u8)c);
  little32(36 + bytes);
  for(char c : "WAVE") header.push_back((u8)c);
  for(char c : "fmt ") header.push_back((u8)c);
  little32(16);
  little16(1);  //format: PCM
  little16(channels);
  little32(rate);
  little32(rate * channels * bits / 8);
  little16(channels * bits / 8);
  little16(bits);
  for(char c : "data") header.push_back((u8)c);
  little32(bytes);
  std::ofstream file(path, std::ios::binary);
  if(!file) return false;
  file.write((const char*)header.data(), header.size());
  for(s32 sample : samples) {
    //clamp to the 16-bit range: a full-scale +1.0 is 32768, which wraps to -32768
    s16 value = sample > 32767 ? 32767 : (sample < -32768 ? -32768 : s16(sample));
    file.write((const char*)&value, 2);
  }
  return true;
}

//The frame's number, six digits, zero-padded: the PNGs' names.
auto numberText(u32 number) -> std::string {
  char text[8];
  std::snprintf(text, sizeof(text), "%06u", number);
  return text;
}

//A press item, "F:C": the frame, and the control it names: a button (its node's name, "!" after it for a
//release), or a stick's axis with its value ("L-StickX:V", the value -32768 to 32767, the front end's range, as
//the system takes it).
struct Press {
  u32 frame = 0;
  std::string name;
  s64 value = 1;
};

auto parsePress(const std::string& item, std::string& problem) -> Press {
  Press press;
  auto colon = item.find(':');
  if(colon == std::string::npos) {
    problem = "a press is \"frame:control\", like 120:Start: " + item;
    return press;
  }
  try {
    press.frame = std::stoul(item.substr(0, colon));
  } catch(...) {
    problem = "a press's frame is a number: " + item;
    return press;
  }
  auto control = item.substr(colon + 1);
  if(control.rfind("L-StickX:", 0) == 0 || control.rfind("L-StickY:", 0) == 0) {
    press.name = control.rfind("L-StickX:", 0) == 0 ? "L-Stick X" : "L-Stick Y";
    try {
      press.value = std::stol(control.substr(9));
    } catch(...) {
      problem = "a stick's value is a number: " + item;
      return press;
    }
  } else if(!control.empty() && control.back() == '!') {
    press.name = control.substr(0, control.size() - 1);
    press.value = 0;
  } else {
    press.name = control;
  }
  return press;
}

//What a front end does for the core: hands it the game's files, shows its frames, and reports the controls.
struct RunnerPlatform : ares::Platform {
  std::shared_ptr<vfs::directory> game;  //the game's pak, given to the drive
  std::map<std::string, s64> held;        //the controls by node name: a button 0 or 1, the stick -32768 to 32767

  std::mutex mutex;
  std::vector<u32> frame;                 //the last frame presented, as ARGB
  u32 frameWidth = 0, frameHeight = 0;
  u32 presented = 0;                      //how many frames the screen has presented
  std::vector<s32> sound;                 //the sound the stream hands on, left and right, as 16-bit samples
  bool running = false;                   //a frame presented as the screen quits, on its way out, is no frame

  u32 pngEvery = 0;                       //every K-th presented frame, as a PNG
  std::set<u32> pngAt;                    //or these presented frames, as PNGs
  fs::path outDir;                        //the folder the PNGs go in

  auto pak(Node::Object node) -> std::shared_ptr<vfs::directory> override {
    if(node->name() == "PlayStation Portable Disc" && game) return game;
    return std::make_shared<vfs::directory>();
  }

  //The frame the screen's own thread presents: kept, and counted. The main loop writes a PNG when a frame
  //asks for one, after waiting for the screen to present a new picture.
  auto video(Node::Video::Screen, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
    std::lock_guard lock{mutex};
    if(!running) return;
    frame.assign(width * height, 0);
    for(u32 y : range(height)) {
      for(u32 x : range(width)) frame[y * width + x] = data[y * (pitch / 4) + x];
    }
    frameWidth = width, frameHeight = height;
    ++presented;
  }

  //The controls, as the system reads them each frame: the buttons by name, the stick by its node's name and value.
  auto input(Node::Input::Input node) -> void override {
    std::string name = (const char*)node->name();
    if(auto button = node->cast<Node::Input::Button>()) button->setValue(held[name] != 0);
    if(auto axis = node->cast<Node::Input::Axis>()) axis->setValue(held[name]);
  }

  //The sound the stream hands on, sample for sample, left and right.
  auto audio(Node::Audio::Stream stream) -> void override {
    std::lock_guard lock{mutex};
    while(stream->pending()) {
      f64 samples[2];
      stream->read(samples);
      for(f64 sample : samples) sound.push_back(s32(std::lround(sample * 32768.0)));
    }
  }
};

//A number from an option's value: stoul's exceptions (a non-number, say) end the run with a message.
auto parseUint(const std::string& text, const std::string& option) -> u32 {
  try {
    return std::stoul(text);
  } catch(...) {
    std::fprintf(stderr, "%s needs a number: %s\n", option.c_str(), text.c_str());
    std::exit(1);
  }
}

auto main(int argc, char** argv) -> int {
  if(argc < 2) {
    std::fprintf(stderr, "usage: psp-runner GAME [options]; see tools/psp-runner/README.md\n");
    return 1;
  }

  //The options: parsed first, as they name the run's shape.
  fs::path game, outDir, wav, loadState, saveStateFile, memoryStick, fonts, script;
  u32 frames = 3600, pngEvery = 0, saveStateAt = 0, geThreads = 0;
  std::set<u32> pngAt;
  std::vector<std::string> pressItems;
  bool interpreter = false;
  for(int i = 1; i < argc; i++) {
    auto option = std::string{argv[i]};
    auto next = [&]() -> std::string {
      if(++i >= argc) {
        std::fprintf(stderr, "%s needs a value\n", option.c_str());
        std::exit(1);
      }
      return argv[i];
    };
    if(option == "--frames") frames = parseUint(next(), option);
    else if(option == "--press") {  //each of a list's items a press of its own
      auto list = next();
      u32 start = 0;
      for(u32 at = 0; at <= list.size(); at++) {
        if(at < list.size() && list[at] != ',') continue;
        if(at > start) pressItems.push_back(list.substr(start, at - start));
        start = at + 1;
      }
    }
    else if(option == "--script") script = next();
    else if(option == "--png-every") pngEvery = parseUint(next(), option);
    else if(option == "--png-at") {
      auto list = next();
      u32 start = 0;
      for(u32 at = 0; at <= list.size(); at++) {
        if(at < list.size() && list[at] != ',') continue;
        if(at > start) pngAt.insert(parseUint(list.substr(start, at - start), option));
        start = at + 1;
      }
    }
    else if(option == "--out") outDir = next();
    else if(option == "--wav") wav = next();
    else if(option == "--save-state-at") { saveStateAt = parseUint(next(), option); saveStateFile = next(); }
    else if(option == "--load-state") loadState = next();
    else if(option == "--interpreter") interpreter = true;
    else if(option == "--ge-threads") geThreads = parseUint(next(), option);
    else if(option == "--memory-stick") memoryStick = next();
    else if(option == "--fonts") fonts = next();
    else if(option[0] == '-' && option[1] == '-') {
      std::fprintf(stderr, "no such option: %s\n", option.c_str());
      return 1;
    }
    else if(game.empty()) game = option;
    else {
      std::fprintf(stderr, "no such option: %s\n", option.c_str());
      return 1;
    }
  }

  //The script's presses, one per line, "#" beginning a comment.
  if(!script.empty()) {
    std::ifstream stream(script);
    std::string line;
    while(std::getline(stream, line)) {
      if(line.empty() || line[0] == '#') continue;
      pressItems.push_back(line);
    }
  }

  //The presses, parsed: each names a frame and the control held (or released) from it.
  std::vector<Press> presses;
  std::string problem;
  for(auto& item : pressItems) {
    auto press = parsePress(item, problem);
    if(!problem.empty()) {
      std::fprintf(stderr, "%s\n", problem.c_str());
      return 1;
    }
    if(press.name == "L-Stick X" || press.name == "L-Stick Y") {
      if(press.value < -32768 || press.value > 32767) {
        std::fprintf(stderr, "the stick's value is -32768 to 32767: %s\n", item.c_str());
        return 1;
      }
    } else {  //a release names a button too (a name it doesn't know would release nothing, unseen)
      static const char* buttons[] = {"Select", "Start", "Up", "Down", "Left", "Right", "Triangle", "Circle",
                                      "Cross", "Square", "L", "R"};
      if(std::find(std::begin(buttons), std::end(buttons), press.name) == std::end(buttons)) {
        std::fprintf(stderr, "no such control: %s\n", item.c_str());
        return 1;
      }
    }
    presses.push_back(press);
  }

  //The game's file, and what it is.
  if(!fs::is_regular_file(game)) {
    std::fprintf(stderr, "no such file: %s\n", game.string().c_str());
    return 1;
  }
  auto fp = vfs::disk::open(game.string().c_str(), vfs::read);
  if(!fp) {
    std::fprintf(stderr, "the file can't be opened: %s\n", game.string().c_str());
    return 1;
  }
  auto name = kind(game, *fp);
  if(name.empty()) {
    std::fprintf(stderr, "not a PSP game: %s\n", game.string().c_str());
    return 1;
  }

  //The title: a PBP's own (its PARAM.SFO's TITLE), a disc's from its PARAM.SFO's TITLE, else the file's name.
  std::string title, npid;
  if(name == "program.pbp") title = PlayStationPortable::sfoValue(PlayStationPortable::paramSFO(*fp), "TITLE");
  else if(name.rfind("disc.", 0) == 0 && discInfo(game, npid, title)) {
    std::printf("disc: %s (%s)\n", title.c_str(), npid.c_str());
  }
  if(title.empty()) title = game.stem().string();
  std::printf("game: %s\n", game.string().c_str());

  //The run's scratch, a folder of the runner's own (the memory stick's default): gone with the run.
  auto scratch = fs::temp_directory_path() / ("phobos-psp-runner-" + std::to_string(::getpid()));
  fs::remove_all(scratch);
  fs::create_directories(scratch);
  if(memoryStick.empty()) memoryStick = (scratch / "memory-stick").string();

  //The folders the run writes to, made if they aren't there: the PNGs' folder, and the WAV's and the
  //save-state file's.
  if(!outDir.empty()) fs::create_directories(outDir);
  if(!wav.empty()) fs::create_directories(wav.parent_path());
  if(!saveStateFile.empty()) fs::create_directories(saveStateFile.parent_path());

  //The front end and the options, as the system takes them.
  RunnerPlatform host;
  host.game = makePak(game, name, title);
  host.pngEvery = pngEvery;
  host.pngAt = pngAt;
  host.outDir = outDir;
  PlayStationPortable::option("Memory Stick", memoryStick.c_str());
  if(!fonts.empty()) PlayStationPortable::option("Fonts", fonts.c_str());
  if(interpreter) PlayStationPortable::option("Recompiler", "false");
  if(geThreads) PlayStationPortable::option("GE Threads", std::to_string(geThreads).c_str());

  auto& psp = ares::PlayStationPortable::system;
  ares::platform = &host;

  //The PSP, the game in its drive, and the power on, as Phobos's runner does.
  Node::System root;
  if(!PlayStationPortable::load(root, "[Sony] PlayStation Portable")) return 1;
  auto drive = root->find<Node::Port>("UMD Drive");
  if(!drive) return 1;
  drive->allocate();
  drive->connect();
  root->power();

  //The sound's stream, at the PSP's own rate, so the samples the runner writes are at it (the stream's default is
  //the host's 48 kHz).
  if(auto stream = root->find<Node::Audio::Stream>("Audio")) stream->setResamplerFrequency(44'100);

  //The game's own output and the kernel's notes, each with the frame it happened on; the notes that name a
  //function the kernel doesn't have, counted by their library and NID, for the summary.
  auto frameNumber = std::atomic<u32>{0};
  auto lastNote = std::string{};
  auto missing = std::map<std::pair<std::string, std::string>, u32>{};
  auto record = [&](bool kernelNote, const std::string& text) {
    u32 start = 0;
    for(u32 at = 0; at <= text.size(); at++) {
      if(at < text.size() && text[at] != '\n') continue;
      if(at == start) continue;  //an empty line
      auto line = text.substr(start, at - start);
      start = at + 1;
      std::printf("[%06u] %s: %s\n", frameNumber.load(), kernelNote ? "note" : "game", line.c_str());
      lastNote = line;
      if(kernelNote && line.rfind("not implemented yet: ", 0) == 0) {
        auto separator = line.find(" function ");
        auto library = line.substr(21, separator - 21);
        auto nid = line.substr(separator + 10);
        missing[{library, nid}]++;
      }
    }
  };
  psp.kernel.output = [&](const std::string& text) { record(false, text); };
  psp.kernel.log = [&](const std::string& text) { record(true, text); };

  //A state loaded at once: the game booted, then the machine as it was when it was saved.
  if(!loadState.empty()) {
    std::ifstream stream(loadState, std::ios::binary);
    std::vector<u8> data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    serializer load{data.data(), u32(data.size())};
    if(!root->unserialize(load)) {
      std::fprintf(stderr, "the state can't be loaded: %s\n", loadState.string().c_str());
      return 1;
    }
  }

  //The frames: the controls in, the game run, the picture and the sound out. A PNG is written after the run
  //of a frame that asks for one: the screen's own thread presents a new picture a step behind the run, so we
  //wait for it (with a timeout) and number the picture by the frame run, not by the screen's count.
  host.running = true;
  auto start = std::chrono::steady_clock::now();
  u32 lastPresented = 0;
  for(u32 frame = 1; frame <= frames; frame++) {
    frameNumber = frame;
    for(auto& press : presses) {  //the presses this frame: held from their frame on, released by a "!"
      if(press.frame != frame) continue;
      host.held[press.name] = press.value;
    }
    if(saveStateAt == frame) {
      auto state = root->serialize(true);
      std::ofstream file(saveStateFile, std::ios::binary);
      file.write((const char*)state.data(), state.size());
    }
    root->run();
    if(host.pngEvery ? frame % host.pngEvery == 0 : host.pngAt.count(frame)) {
      auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while(true) {
        u32 presented;
        {
          std::lock_guard lock{host.mutex};
          presented = host.presented;
        }
        if(presented > lastPresented || std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      lastPresented = host.presented;
      std::vector<u32> frameData;
      u32 w, h;
      {
        std::lock_guard lock{host.mutex};
        frameData = host.frame;
        w = host.frameWidth;
        h = host.frameHeight;
      }
      writePng(host.outDir / ("frame-" + numberText(frame) + ".png"), frameData, w, h);
    }
  }
  auto end = std::chrono::steady_clock::now();  //the speed is the frame loop's, not the screen's catch-up
  host.running = false;
  bool ended = psp.kernel.exited;  //(unloading powers the kernel off, which forgets it)
  root->unload();

  //The sound, as a WAV.
  if(!wav.empty()) {
    std::vector<s32> sound;
    {
      std::lock_guard lock{host.mutex};
      sound = host.sound;
    }
    if(!writeWav(wav, sound)) {
      std::fprintf(stderr, "the WAV can't be written: %s\n", wav.string().c_str());
      return 1;
    }
  }

  fs::remove_all(scratch);

  //The summary: the frames run, the program's end, the speed, the missing functions, and the last note.
  auto elapsed = std::chrono::duration<double>(end - start).count();
  std::printf("--\n");
  std::printf("frames run: %u\n", frames);
  std::printf("program: %s\n", ended ? "ended" : "still running");
  std::printf("frames per second: %.2f\n", frames / elapsed);
  std::printf("unique missing functions: %u\n", u32(missing.size()));
  auto byCount = [](auto& a, auto& b) {
    if(a.second != b.second) return a.second > b.second;
    return a.first < b.first;
  };
  auto sorted = std::vector<std::pair<std::pair<std::string, std::string>, u32>>(missing.begin(), missing.end());
  std::sort(sorted.begin(), sorted.end(), byCount);
  for(auto& [key, count] : sorted) {
    std::printf("  %s %s: %u\n", key.first.c_str(), key.second.c_str(), count);
  }
  if(!lastNote.empty()) std::printf("last note: %s\n", lastNote.c_str());
  return 0;
}
