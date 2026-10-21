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
//                                L-StickX:12345, or the right stick's, R-StickY:-32768); F:C! releases C at frame F
//    --script FILE              the presses in a file, one F:C per line ("#" begins a comment)
//    --png-every K              write a PNG every K frames, into --out's folder
//    --png-at F1,F2,...         write a PNG at each of these frames, into --out's folder
//    --out DIR                  the folder the PNGs go in (needed with --png-every and --png-at)
//    --wav FILE                 the sound, as a 16-bit stereo WAV at the PSP's 44.1 kHz
//    --save-state-at F FILE     the machine's state at frame F, as a save-state file
//    --load-state FILE          start from a save-state file (the game booted, then the state loaded at once)
//    --interpreter              run the CPU's interpreter (the recompiler is the default; with --ge-timing psp its
//                                accesses aren't counted in the CPU's caches, so the GE takes less time beside it than
//                                the recompiler's: docs/psp-core.md part 113)
//    --spin-skip on|off         skip through loops that only poll (spins.cpp), every register, byte and cycle as running
//                                them gives: on by default, off with --ge-profile unless given (so a profile counts
//                                every poll, as the PSP's plugin records them)
//    --ge-threads N             how many threads draw the GE's pictures (0: one fewer than the host's cores)
//    --ge-thread MODE           the GE on a thread of its own (option "GE Thread"): off (the default) or on
//    --renderer NAME            who draws them: Software (the default), Vulkan (accurate) or Vulkan-fast (the
//                                system's Vulkan loader; the summary then gives the GPU renderer's counts)
//    --resolution N             Vulkan's internal resolution, 1 (the PSP's, the default) to 10 times it; the
//                                frames (and PNGs) are then read back at up to 4 times the PSP's size
//    --late-frames              the GPU's frames taken a frame or two late, nothing waited for each frame, as
//                                when the app presents them (for timing, at 1x; PNGs then show an earlier frame)
//    --pipeline-cache FILE      the Vulkan renderer's pipelines kept in FILE between runs, as the app keeps them
//    --ge-timing psp|fast       the GE's time: as the owner's PSP took it (psp, the default), or Sony's peak rates
//                                (fast) (the core's option "GE Timing")
//    --memory-stick DIR         the host folder standing for ms0: (a scratch folder by default)
//    --fonts DIR                the PSP's system fonts (the .pgf files of a PSP's flash0), for the game's text
//    --trace-savedata           each savedata request and its answer, a line with its frame: the mode, the game's
//                                and the save's names, the data file's, the sizes and the result
//    --trace-io                 each file request (sceIo*) and its answer, a line with its frame: the PSP's time and
//                                the thread, the descriptor with its path and position, the sizes, buffers and a hash
//                                of the bytes moved, and each asynchronous request as it's done
//    --ge-profile FILE          how the game drives the GE and the display, as a capture of tools/psp-ge-profile's
//                                (its decoder, geprof.py, reads it as it reads the plugin's from a PSP): each call to
//                                the GE driver's and the display's functions, the GE's callbacks, the vertical blanks
//                                and the GE time the model charges, in the PSP's time, from --ge-profile-from's frame
//                                to the run's end (or to where the program starts itself again: one program's)
//    --ge-profile-from F        the frame the GE profile starts at (default 1; with --ge-profile only)
//    --report FILE              the run as JSON ("phobos-psp-runner 1"; tools/psp-runner/README.md): the runner's
//                                build, the game, what was asked and what ran (the renderer, its device and
//                                resolution, the engine, the GE's threads, the clock), and what came of it
//    --start-time SECONDS       the PSP's clock starts there (seconds since 1970, up to 4294967295), not at the
//                                host's time, so runs see the same dates (the time zone is the host's TZ), and there
//                                again if the game starts itself again; a loaded state keeps its own clock
//    --screen-stops-at F        from frame F the runner takes no more pictures from the screen, as a screen that has
//                                stopped presenting would give none (to test the runner's own capture timeout)
//
//The frames its PNGs name are the frames the runner ran: after a frame's run, the runner waits for the screen's
//own thread to present a new picture (with a timeout), then writes it as a PNG named by the frame run. A PNG whose
//wait ran out (no picture presented since the last PNG) shows the last one: the runner says so on standard error and
//counts it. The screen's thread drops pictures it doesn't take in time, so on a busy host a PNG can be older than its
//name: the report's captures give, for each, the frame that was running when its picture arrived. With the GE on its
//thread (whose pictures the screen shows a frame late) a PNG's frame and the one before it get their own pictures, so
//a PNG a frame older than its name shows that frame's own picture, as with the thread off. Captures (PNGs and
//the save state) are in the frame loop's time; the summary gives the speed without them too where there were any.

#include <psp/psp.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
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

//The source the runner was built from (build.sh names its commit), for the report.
#if !defined(PHOBOS_RUNNER_SOURCE)
  #define PHOBOS_RUNNER_SOURCE "unknown"
#endif

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
//samples, left and right, in the order the stream handed them on. The header is the standard 44 bytes: each tag is
//its four characters alone (a string literal's terminator after one would move everything after it).
auto writeWav(const fs::path& path, const std::vector<s32>& samples) -> bool {
  u32 channels = 2, rate = 44'100, bits = 16;
  u32 bytes = samples.size() * 2;
  std::vector<u8> header;
  auto tag = [&](const char* text) { header.insert(header.end(), text, text + 4); };
  auto little16 = [&](u32 value) { header.push_back(value & 255); header.push_back(value >> 8 & 255); };
  auto little32 = [&](u32 value) {
    header.push_back(value & 255);
    header.push_back(value >> 8 & 255);
    header.push_back(value >> 16 & 255);
    header.push_back(value >> 24 & 255);
  };
  tag("RIFF");
  little32(36 + bytes);
  tag("WAVE");
  tag("fmt ");
  little32(16);
  little16(1);  //format: PCM
  little16(channels);
  little32(rate);
  little32(rate * channels * bits / 8);
  little16(channels * bits / 8);
  little16(bits);
  tag("data");
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
//the system takes it), the right stick's too ("R-StickX:V", "R-StickY:V": the one patched games read from
//SceCtrlData's bytes 10 and 11, ares/psp/kernel/ctrl.cpp), each axis's node named with a space ("L-Stick X").
struct Press {
  u32 frame = 0;
  std::string name;
  s64 value = 1;
};

static const char* stickAxes[] = {"L-StickX:", "L-StickY:", "R-StickX:", "R-StickY:"};

//A number as options and presses write it: decimal digits only (no sign, space or anything after them). False for
//anything else; a value past 32 bits comes back as 0x1'0000'0000.
auto readDigits(const std::string& text, u64& value) -> bool {
  value = 0;
  for(char c : text) {
    if(c < '0' || c > '9') return false;
    value = std::min<u64>(value * 10 + (c - '0'), 0x1'0000'0000);
  }
  return !text.empty();
}

auto parsePress(const std::string& item, std::string& problem) -> Press {
  Press press;
  auto colon = item.find(':');
  if(colon == std::string::npos) {
    problem = "a press is \"frame:control\", like 120:Start: " + item;
    return press;
  }
  u64 frame = 0;
  if(!readDigits(item.substr(0, colon), frame) || frame > 0xffff'ffff) {
    problem = "a press's frame is a number up to 4294967295: " + item;
    return press;
  }
  press.frame = u32(frame);
  auto control = item.substr(colon + 1);
  auto axis = std::find_if(std::begin(stickAxes), std::end(stickAxes),
                           [&](const char* prefix) { return control.rfind(prefix, 0) == 0; });
  if(axis != std::end(stickAxes)) {
    press.name = std::string(*axis, 7) + " " + (*axis)[7];  //("L-Stick X")
    auto value = control.substr(9);
    bool negative = !value.empty() && value[0] == '-';  //(an optional minus sign, then digits only, as every number)
    u64 magnitude = 0;
    if(!readDigits(value.substr(negative), magnitude)) {
      problem = "a stick's value is a number: " + item;
      return press;
    }
    press.value = negative ? -s64(magnitude) : s64(magnitude);
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
  u32 arrivedIn = 0;                      //the frame the runner was running when the last one arrived
  std::atomic<u32> runFrame{0};           //the frame the runner is running
  std::vector<s32> sound;                 //the sound the stream hands on, left and right, as 16-bit samples
  bool running = false;                   //a frame presented as the screen quits, on its way out, is no frame
  bool screenStopped = false;             //--screen-stops-at reached: pictures presented from now on aren't taken

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
    if(!running || screenStopped) return;
    frame.assign(width * height, 0);
    for(u32 y : range(height)) {
      for(u32 x : range(width)) frame[y * width + x] = data[y * (pitch / 4) + x];
    }
    frameWidth = width, frameHeight = height;
    ++presented;
    arrivedIn = runFrame;
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

//A string as JSON writes it: in quotes, with quotes, backslashes and control characters escaped, and each byte that
//isn't part of a UTF-8 character (a game's Shift-JIS text, a file name in Latin-1) written as the character of its
//value (\u00XX), so the report stays UTF-8.
auto jsonText(const std::string& text) -> std::string {
  std::string out = "\"";
  auto escape = [&](u8 c) {
    char escaped[8];
    std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
    out += escaped;
  };
  for(size_t at = 0; at < text.size();) {
    u8 c = text[at];
    if(c == '"' || c == '\\') { out += '\\', out += char(c), at++; continue; }
    if(c < 0x20) { escape(c), at++; continue; }
    if(c < 0x80) { out += char(c), at++; continue; }
    //a UTF-8 character's length by its first byte, and the range of its second (no overlong forms, no surrogates,
    //nothing past U+10FFFF); the rest are 0x80 to 0xbf
    u32 length = 0;
    u8 low = 0x80, high = 0xbf;
    if(c >= 0xc2 && c <= 0xdf) length = 2;
    else if(c >= 0xe0 && c <= 0xef) length = 3, low = c == 0xe0 ? 0xa0 : 0x80, high = c == 0xed ? 0x9f : 0xbf;
    else if(c >= 0xf0 && c <= 0xf4) length = 4, low = c == 0xf0 ? 0x90 : 0x80, high = c == 0xf4 ? 0x8f : 0xbf;
    bool valid = length && at + length <= text.size();
    for(u32 n = 1; valid && n < length; n++) {
      u8 next = text[at + n];
      valid = n == 1 ? next >= low && next <= high : next >= 0x80 && next <= 0xbf;
    }
    if(valid) out.append(text, at, length), at += length;
    else escape(c), at++;
  }
  return out + "\"";
}

//A number from an option's value: anything but digits (readDigits()), or a number past 32 bits, ends the run with a
//message.
auto parseUint(const std::string& text, const std::string& option) -> u32 {
  u64 value = 0;
  if(!readDigits(text, value)) {
    std::fprintf(stderr, "%s needs a number: %s\n", option.c_str(), text.c_str());
    std::exit(1);
  }
  if(value > 0xffff'ffff) {
    std::fprintf(stderr, "%s is at most 4294967295: %s\n", option.c_str(), text.c_str());
    std::exit(1);
  }
  return u32(value);
}

auto main(int argc, char** argv) -> int {
  if(argc < 2) {
    std::fprintf(stderr, "usage: psp-runner GAME [options]; see tools/psp-runner/README.md\n");
    return 1;
  }

  //The options: parsed first, as they name the run's shape.
  fs::path game, outDir, wav, loadState, saveStateFile, memoryStick, fonts, script, geProfileFile;
  std::string renderer, spinSkip;
  u32 resolution = 0;  //(0: the core's own, 1)
  u32 frames = 3600, pngEvery = 0, saveStateAt = 0, geThreads = 0, geProfileFrom = 1;
  std::string geThread;
  std::set<u32> pngAt;
  std::vector<std::string> pressItems;
  bool lateFrames = false, gePspTiming = true;
  std::string pipelineCache;
  fs::path reportFile;
  u32 startTime = 0, screenStopsAt = 0;
  bool startTimeGiven = false, framesGiven = false, geProfileFromGiven = false;
  bool interpreter = false, traceSavedata = false, traceIo = false;
  for(int i = 1; i < argc; i++) {
    auto option = std::string{argv[i]};
    auto next = [&]() -> std::string {
      if(++i >= argc) {
        std::fprintf(stderr, "%s needs a value\n", option.c_str());
        std::exit(1);
      }
      return argv[i];
    };
    if(option == "--frames") frames = parseUint(next(), option), framesGiven = true;
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
    else if(option == "--spin-skip") spinSkip = next();
    else if(option == "--ge-threads") geThreads = parseUint(next(), option);
    else if(option == "--ge-thread") geThread = next();
    else if(option == "--renderer") renderer = next();
    else if(option == "--resolution") resolution = parseUint(next(), option);
    else if(option == "--late-frames") lateFrames = true;
    else if(option == "--ge-timing") {
      auto timing = next();
      if(timing != "psp" && timing != "fast") {
        std::fprintf(stderr, "--ge-timing is psp or fast\n");
        return 1;
      }
      gePspTiming = timing == "psp";
    }
    else if(option == "--pipeline-cache") pipelineCache = next();
    else if(option == "--memory-stick") memoryStick = next();
    else if(option == "--fonts") fonts = next();
    else if(option == "--trace-savedata") traceSavedata = true;
    else if(option == "--trace-io") traceIo = true;
    else if(option == "--ge-profile") geProfileFile = next();
    else if(option == "--ge-profile-from") geProfileFrom = parseUint(next(), option), geProfileFromGiven = true;
    else if(option == "--report") reportFile = next();
    else if(option == "--start-time") startTime = parseUint(next(), option), startTimeGiven = true;
    else if(option == "--screen-stops-at") screenStopsAt = parseUint(next(), option);
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
  if(geProfileFromGiven && geProfileFile.empty()) {
    std::fprintf(stderr, "--ge-profile-from goes with --ge-profile (the file to record to)\n");
    return 1;
  }
  if(!spinSkip.empty() && spinSkip != "on" && spinSkip != "off") {
    std::fprintf(stderr, "--spin-skip is on or off: %s\n", spinSkip.c_str());
    return 1;
  }
  bool spinSkipOn = spinSkip.empty() ? geProfileFile.empty() : spinSkip == "on";

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
    if(press.name.find("-Stick ") != std::string::npos) {
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

  if(!geProfileFile.empty() && (geProfileFrom < 1 || geProfileFrom > frames)) {
    std::fprintf(stderr, "--ge-profile-from is a frame the run runs, 1 to %u: %u\n", frames, geProfileFrom);
    return 1;
  }

  //The folders the run writes to, made if they aren't there: the PNGs' folder, and the WAV's, the save-state file's,
  //the report's and the GE profile's. One that can't be made ends the run, before it has a scratch to leave behind.
  for(auto& folder : {outDir, wav.parent_path(), saveStateFile.parent_path(), reportFile.parent_path(),
                      geProfileFile.parent_path()}) {
    if(folder.empty()) continue;
    std::error_code error;
    fs::create_directories(folder, error);
    if(error) {
      std::fprintf(stderr, "the folder can't be made: %s (%s)\n", folder.string().c_str(), error.message().c_str());
      return 1;
    }
  }

  //The run's scratch, a folder of the runner's own (the memory stick's default): gone with the run.
  auto scratch = fs::temp_directory_path() / ("phobos-psp-runner-" + std::to_string(::getpid()));
  fs::remove_all(scratch);
  fs::create_directories(scratch);
  bool memoryStickGiven = !memoryStick.empty();
  if(!memoryStickGiven) memoryStick = (scratch / "memory-stick").string();

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
  if(!geThread.empty()) PlayStationPortable::option("GE Thread", geThread == "on" || geThread == "On" ? "On" : "Off");
  //(the core's names, "Vulkan (accurate)" and "Vulkan (fast)", as one word each for a shell)
  if(renderer == "Vulkan-accurate") renderer = "Vulkan";
  if(renderer == "Vulkan-fast") renderer = "Vulkan (fast)";
  if(!renderer.empty()) PlayStationPortable::option("Renderer", renderer.c_str());
  if(resolution) PlayStationPortable::option("Resolution", std::to_string(resolution).c_str());
  if(lateFrames) PlayStationPortable::option("Late Frames", "true");
  PlayStationPortable::option("GE Timing", gePspTiming ? "PSP" : "Fast");
  if(!pipelineCache.empty()) PlayStationPortable::option("Pipeline Cache", pipelineCache.c_str());

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
  //The PSP's clock from the time asked for, not the host's, which the kernel took as it powered on (Kernel::power(),
  //before anything ran)
  if(startTimeGiven) psp.kernel.startTime = u64(startTime) * 1'000'000;

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
  psp.kernel.spins = spinSkipOn;
  if(traceSavedata) {
    psp.kernel.savedataTrace = [&](const std::string& text) {
      std::printf("[%06u] savedata: %s\n", frameNumber.load(), text.c_str());
    };
  }
  if(traceIo) {
    psp.kernel.ioTrace = [&](const std::string& text) {
      std::printf("[%06u] io: %s\n", frameNumber.load(), text.c_str());
    };
  }

  //A state loaded at once: the game booted, then the machine as it was when it was saved.
  if(!loadState.empty()) {
    std::ifstream stream(loadState, std::ios::binary);
    std::vector<u8> data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    serializer load{data.data(), u32(data.size())};
    lastNote.clear();
    if(!root->unserialize(load)) {
      std::fprintf(stderr, "the state can't be loaded: %s%s%s\n", loadState.string().c_str(),
                   lastNote.empty() ? "" : ": ", lastNote.c_str());  //(the last note while loading it, if any)
      return 1;
    }
    if(startTimeGiven) {
      std::fprintf(stderr, "--start-time: the loaded state keeps its own clock (the time asked for is taken if the "
                           "game starts itself again)\n");
    }
  }

  //The frames: the controls in, the game run, the picture and the sound out. A PNG is written after the run
  //of a frame that asks for one: the screen's own thread presents a new picture a step behind the run, so we
  //wait for it (with a timeout) and number the picture by the frame run, not by the screen's count.
  host.running = true;
  auto start = std::chrono::steady_clock::now();
  u32 lastPresented = 0;
  std::chrono::steady_clock::duration captured{};  //the frame loop's time on captures: the save state and PNGs
  u32 staleCaptures = 0;                            //PNGs whose wait for a new picture ran out
  u64 clockStart = psp.kernel.startTime;            //the clock the run began with (a loaded state's own)
  struct Capture { u32 frame, arrivedIn; f64 waited; bool fresh, picture; };
  std::vector<Capture> captures;                    //each PNG asked for, for the report
  PlayStationPortable::GeProfile geProfile;         //(--ge-profile) recorded from its frame on
  u32 geProfileTo = frames;                         //its last frame: the run's, or the program's restart's
  //The clock asked for, held: a game that starts itself again (sceKernelLoadExec, or largeRestart with all of RAM)
  //powers the kernel on again, which takes the host's clock again (Kernel::power()). The kernel's run returns as the
  //new program is started, before any of it runs, so the clock is set again at the next frame's top (and after the
  //last frame).
  u32 clockRestarts = 0;
  u64 clockSeen = psp.kernel.startTime, cyclesSeen = psp.kernel.cycles;
  auto holdClock = [&](u32 frame) {
    if(!startTimeGiven) return;
    if(psp.kernel.startTime != clockSeen || psp.kernel.cycles < cyclesSeen) {
      psp.kernel.startTime = u64(startTime) * 1'000'000;
      clockRestarts++;
      std::fprintf(stderr, "frame %u: %s the host's clock; the PSP's clock is the time asked for again\n", frame,
                   psp.kernel.exited ? "a program the game asked for couldn't start, which ended the game and took"
                                     : "the game started itself again, which took");
    }
    clockSeen = psp.kernel.startTime, cyclesSeen = psp.kernel.cycles;
  };
  for(u32 frame = 1; frame <= frames; frame++) {
    frameNumber = frame;
    if(screenStopsAt && frame == screenStopsAt) {
      std::lock_guard lock{host.mutex};
      host.screenStopped = true;
    }
    holdClock(frame - 1);
    host.runFrame = frame;
    for(auto& press : presses) {  //the presses this frame: held from their frame on, released by a "!"
      if(press.frame != frame) continue;
      host.held[press.name] = press.value;
    }
    auto stateStarted = std::chrono::steady_clock::now();
    if(saveStateAt == frame) {
      auto state = root->serialize(true);
      std::ofstream file(saveStateFile, std::ios::binary);
      file.write((const char*)state.data(), state.size());
    }
    if(saveStateAt == frame) captured += std::chrono::steady_clock::now() - stateStarted;
    if(!geProfileFile.empty() && frame == geProfileFrom) psp.kernel.geProfileStart(geProfile);
    //(a frame written as a PNG gets its own picture on the screen, the GE thread waited for where it takes them; so
    //does the frame before it, whose picture the PNG takes where the screen's thread hasn't presented its own yet,
    //as it would be with the thread off)
    auto png = [&](u32 at) { return host.pngEvery ? at % host.pngEvery == 0 : host.pngAt.count(at) > 0; };
    psp.exactFrame = png(frame) || png(frame + 1);
    root->run();
    if(!geProfileFile.empty() && frame >= geProfileFrom && geProfileTo == frames && !psp.kernel.geProfile) {
      geProfileTo = frame;  //the program started again (Kernel::loadExec()), ending the profile
    }
    if(png(frame)) {
      auto pngStarted = std::chrono::steady_clock::now();
      auto deadline = pngStarted + std::chrono::seconds(2);
      while(true) {
        u32 presented;
        {
          std::lock_guard lock{host.mutex};
          presented = host.presented;
        }
        if(presented > lastPresented || std::chrono::steady_clock::now() > deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      auto waited = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - pngStarted).count();
      std::vector<u32> frameData;
      u32 w, h, arrivedIn;
      bool fresh;  //(read with the picture it describes, under the same lock)
      {
        std::lock_guard lock{host.mutex};
        fresh = host.presented > lastPresented;
        lastPresented = host.presented;
        frameData = host.frame;
        w = host.frameWidth;
        h = host.frameHeight;
        arrivedIn = host.arrivedIn;
      }
      if(!fresh) staleCaptures++;
      if(!w || !h) {
        std::fprintf(stderr, "frame %u: the screen presented no picture in 2 s, so it has no PNG\n", frame);
      } else {
        if(!fresh) {
          std::fprintf(stderr, "frame %u: the screen presented no new picture in 2 s; its PNG shows the last one\n",
                       frame);
        }
        writePng(host.outDir / ("frame-" + numberText(frame) + ".png"), frameData, w, h);
      }
      captures.push_back({frame, arrivedIn, waited, fresh, w && h});
      captured += std::chrono::steady_clock::now() - pngStarted;
    }
  }
  holdClock(frames);
  auto end = std::chrono::steady_clock::now();  //the speed is the frame loop's, not the screen's catch-up
  host.running = false;
  psp.kernel.geProfileStop();
  //(with the GE on its thread, that thread done with its last frame first: the renderer is its till then)
  auto& drawing = psp.ge.drawer();
  //The GPU renderer's counts, before unloading lets it go; none if it didn't start (the software renderer drew).
  std::optional<PlayStationPortable::GPU::Statistics> gpu;
  if(psp.gpu) gpu = psp.gpu->statistics;
  u64 pipelines = psp.gpu ? psp.gpu->backend->pipelines : 0, making = psp.gpu ? psp.gpu->backend->pipelineMaking : 0;
  bool ended = psp.kernel.exited;  //(unloading powers the kernel off, which forgets it)
  psp.kernel.geFold(true);  //(with the GE on its thread, the stretches the CPU ended after its runs taken first)
  f64 geBusyCycles = psp.kernel.geBusyCycles, geBusyLines = psp.kernel.geBusyLines;  //(so too)
  u64 geCapped = psp.kernel.geCapped, geSkipsRefused = psp.kernel.geSkipsRefused;  //(the CPU beside the GE's counts)
  u64 geRefusedInstructions = psp.kernel.geRefusedRun(), geInstructions = psp.kernel.geInstructions;
  //what ran, for the report, before unloading lets the renderer go
  std::string ranRenderer = psp.gpu ? (psp.gpu->fast ? "Vulkan (fast)" : "Vulkan") : "Software";
  std::string ranDevice = psp.gpu ? psp.gpu->backend->name() : "";
  bool gpuLost = psp.gpu && !psp.gpu->ready();  //(it stopped answering: the software renderer drew from then on)
  u32 ranResolution = psp.gpu ? psp.gpu->resolution() : 1, ranThreads = drawing.drawing.threads;
  root->unload();

  //The sound, as a WAV (a WAV that can't be written fails the run, once the GE profile is written too).
  bool wavFailed = false;
  if(!wav.empty()) {
    std::vector<s32> sound;
    {
      std::lock_guard lock{host.mutex};
      sound = host.sound;
    }
    if(!writeWav(wav, sound)) {
      std::fprintf(stderr, "the WAV can't be written: %s\n", wav.string().c_str());
      wavFailed = true;
    }
  }

  fs::remove_all(scratch);

  //The GE profile, written beside its name, then renamed (as the report is). Its version, in the field's 15
  //characters: the runner's commit (from its source's description, "-g" and the hash), and "+" if it was changed.
  if(!geProfileFile.empty()) {
    std::string source = PHOBOS_RUNNER_SOURCE, commit = source;
    if(auto at = source.rfind("-g"); at != std::string::npos) commit = source.substr(at + 2);
    commit = commit.substr(0, std::min<size_t>(commit.find('-'), 7));
    if(source.find("-dirty") != std::string::npos) commit += "+";
    auto bytes = geProfile.file("phobos " + commit, npid, title);
    auto part = geProfileFile;
    part += ".part-" + std::to_string(::getpid());
    std::ofstream out(part, std::ios::binary);
    out.write((const char*)bytes.data(), bytes.size());
    out.close();
    std::error_code renameError;
    if(!out.fail()) fs::rename(part, geProfileFile, renameError);
    if(out.fail() || renameError) {
      std::error_code ignored;
      fs::remove(part, ignored);
      std::fprintf(stderr, "the GE profile can't be written: %s\n", geProfileFile.string().c_str());
      return 1;
    }
  }
  if(wavFailed) return 1;

  //The summary: the frames run, the program's end, the speed, the missing functions, and the last note. Scripts find
  //its lines by their first words (or any line with "frames per second" in it), so a new line has words of its own.
  auto elapsed = std::chrono::duration<double>(end - start).count();
  std::printf("--\n");
  std::printf("frames run: %u\n", frames);
  std::printf("program: %s\n", ended ? "ended" : "still running");
  std::printf("frames per second: %.2f\n", frames / elapsed);
  auto capturedSeconds = std::chrono::duration<double>(captured).count();
  if(capturedSeconds > 0) {
    std::printf("capture seconds: %.3f (%.2f frames a second without them)\n", capturedSeconds,
                frames / std::max(elapsed - capturedSeconds, 1e-9));
  }
  if(staleCaptures) std::printf("stale captures: %u\n", staleCaptures);
  if(auto& cache = psp.cpu.dataCache; cache.enabled && frames) {  //(the PSP's GE timing: Allegrex::Cache)
    std::printf("caches: %.0f data lines a frame read in from RAM, %.0f written back, %.0f uncached accesses, %.0f "
                "instruction lines read in; %.0f moved while the GE was busy, %.1f%% of the time\n",
                double(cache.fills) / frames, double(cache.writebacks) / frames, double(cache.uncached) / frames,
                double(psp.cpu.instructionCache.fills) / frames, geBusyLines / frames,
                100 * geBusyCycles / (double(frames) * ares::PlayStationPortable::Kernel::VblankCycles));
  }
  //(with the PSP's timing, the CPU beside the GE: its busy windows whose push reached the cap, GE::Cost::Cap; the spin
  //skips the cap refused, and the instructions run in their place, any still running at the end counted to it; and all
  //the CPU's instructions)
  if(gePspTiming) {
    std::printf("ge contention: %llu windows capped; %llu spin skips refused, %llu instructions run in them, of %llu\n",
                (unsigned long long)geCapped, (unsigned long long)geSkipsRefused,
                (unsigned long long)geRefusedInstructions, (unsigned long long)geInstructions);
  }
  if(!geProfileFile.empty()) {
    std::printf("ge profile: %zu records, frames %u to %u%s (%.3f s of the PSP's time), %s\n", geProfile.records.size(),
                geProfileFrom, geProfileTo, geProfileTo < frames ? ", where the program started again" : "",
                (geProfile.endTime - geProfile.startTime) / 1e6, geProfileFile.string().c_str());
  }
  if(gpu) {
    std::printf("gpu: %llu draws, %llu primitives, %llu 3D PRIMs transformed by the GPU, %llu submits, %llu "
                "finishes, %llu uploads, %llu read-backs, %llu textures, %llu copies, %llu frames shown from the "
                "GPU, %.2f ms a frame waiting, %llu moves\n",
                (unsigned long long)gpu->draws, (unsigned long long)gpu->primitives, (unsigned long long)gpu->meshes,
                (unsigned long long)gpu->submits, (unsigned long long)gpu->finishes,
                (unsigned long long)gpu->uploads, (unsigned long long)gpu->readbacks,
                (unsigned long long)gpu->textures, (unsigned long long)gpu->copies,
                (unsigned long long)gpu->pictures, frames ? gpu->waiting / 1e6 / frames : 0.0,
                (unsigned long long)gpu->moves);
    std::printf("gpu pipelines: %llu made, %.1f ms making them\n", (unsigned long long)pipelines, making / 1e6);
    if(gpuLost) std::printf("gpu lost: the software renderer drew from then on\n");
  }
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

  //The report, as JSON: the runner's build, what was asked (null: not given), what ran, and what came of it (counts
  //the GPU didn't give are null). Written beside its name, then renamed, so a report is never a part of one.
  if(!reportFile.empty()) {
    auto text = [](const std::string& value) { return value.empty() ? std::string("null") : jsonText(value); };
    auto number = [](bool given, u64 value) { return given ? std::to_string(value) : std::string("null"); };
    std::string missingList;
    for(auto& [key, count] : sorted) {
      missingList += std::string(missingList.empty() ? "" : ", ") + "{\"library\": " + jsonText(key.first) +
                     ", \"nid\": " + jsonText(key.second) + ", \"count\": " + std::to_string(count) + "}";
    }
    std::string pngAtList;
    for(u32 at : pngAt) pngAtList += (pngAtList.empty() ? "[" : ", ") + std::to_string(at);
    pngAtList = pngAtList.empty() ? "null" : pngAtList + "]";
    std::string captureList;
    for(auto& capture : captures) {
      char line[160];
      std::snprintf(line, sizeof(line), "{\"frame\": %u, \"arrived in frame\": %s, \"waited ms\": %.1f, \"fresh\": %s}",
                    capture.frame, number(capture.picture, capture.arrivedIn).c_str(), capture.waited,
                    capture.fresh ? "true" : "false");
      captureList += std::string(captureList.empty() ? "" : ", ") + line;
    }
    const char* timeZone = std::getenv("TZ");
    char offset[16] = "";  //(the host's zone at the clock's start, as the kernel takes it: +hhmm)
    time_t clockSeconds = time_t(clockStart / 1'000'000);
    tm local{};
    if(localtime_r(&clockSeconds, &local)) std::strftime(offset, sizeof(offset), "%z", &local);
    std::string gpuCounts = "null";
    if(gpu) {
      char counts[640];
      std::snprintf(counts, sizeof(counts),
                    "{\"draws\": %llu, \"primitives\": %llu, \"meshes\": %llu, \"submits\": %llu, \"finishes\": %llu, "
                    "\"uploads\": %llu, \"readbacks\": %llu, \"textures\": %llu, \"copies\": %llu, \"pictures\": %llu, "
                    "\"moves\": %llu, \"waiting ms a frame\": %.3f, \"pipelines made\": %llu, \"pipeline ms\": %.1f}",
                    (unsigned long long)gpu->draws, (unsigned long long)gpu->primitives,
                    (unsigned long long)gpu->meshes, (unsigned long long)gpu->submits,
                    (unsigned long long)gpu->finishes, (unsigned long long)gpu->uploads,
                    (unsigned long long)gpu->readbacks, (unsigned long long)gpu->textures,
                    (unsigned long long)gpu->copies, (unsigned long long)gpu->pictures, (unsigned long long)gpu->moves,
                    frames ? gpu->waiting / 1e6 / frames : 0.0, (unsigned long long)pipelines, making / 1e6);
      gpuCounts = counts;
    }
#if defined(BUILD_RELEASE)
    const char* build = "release";
#elif defined(BUILD_DEBUG)
    const char* build = "debug";
#else
    const char* build = "unknown";
#endif
#if defined(ARES_ENABLE_FFMPEG)
    bool ffmpeg = true;
#else
    bool ffmpeg = false;
#endif
    std::error_code sizeError;
    auto gameBytes = fs::file_size(game, sizeError);
    auto part = reportFile;
    part += ".part-" + std::to_string(::getpid());
    std::ofstream out(part);
    out << "{\n  \"report\": \"phobos-psp-runner 1\",\n"
        << "  \"runner\": {\"source\": " << jsonText(PHOBOS_RUNNER_SOURCE)
        << ", \"compiler\": " << jsonText(__VERSION__) << ", \"build\": " << jsonText(build)
        << ", \"ffmpeg\": " << (ffmpeg ? "true" : "false") << "},\n"
        << "  \"game\": {\"path\": " << jsonText(game.string()) << ", \"kind\": " << jsonText(name)
        << ", \"title\": " << text(title) << ", \"disc id\": " << text(npid)
        << ", \"bytes\": " << (sizeError ? std::string("null") : std::to_string(gameBytes)) << "},\n"
        << "  \"asked\": {\"renderer\": " << text(renderer) << ", \"resolution\": " << number(resolution, resolution)
        << ", \"engine\": " << (interpreter ? jsonText("interpreter") : std::string("null"))
        << ", \"spin skip\": " << text(spinSkip)
        << ", \"ge threads\": " << number(geThreads, geThreads)
        << ", \"late frames\": " << (lateFrames ? "true" : "false")
        << ", \"ge timing\": " << (gePspTiming ? "\"psp\"" : "\"fast\"")
        << ", \"frames\": " << number(framesGiven, frames)
        << ", \"start time\": " << number(startTimeGiven, startTime) << ", \"load state\": " << text(loadState.string())
        << ", \"presses\": " << presses.size() << ", \"script\": " << text(script.string())
        << ", \"png every\": " << number(pngEvery, pngEvery) << ", \"png at\": " << pngAtList
        << ", \"out\": " << text(outDir.string()) << ", \"wav\": " << text(wav.string())
        << ", \"save state at\": " << number(saveStateAt, saveStateAt)
        << ", \"memory stick\": " << text(memoryStickGiven ? memoryStick.string() : "")
        << ", \"fonts\": " << text(fonts.string()) << ", \"pipeline cache\": " << text(pipelineCache)
        << ", \"screen stops at\": " << number(screenStopsAt, screenStopsAt) << "},\n"
        << "  \"ran\": {\"renderer\": " << jsonText(ranRenderer) << ", \"device\": " << text(ranDevice)
        << ", \"gpu lost\": " << (gpuLost ? "true" : "false") << ", \"resolution\": " << ranResolution
        << ", \"engine\": " << jsonText(interpreter ? "interpreter" : "recompiler")
        << ", \"spin skip\": " << (spinSkipOn ? "true" : "false")
        << ", \"ge threads\": " << ranThreads << ", \"clock start\": " << clockStart / 1'000'000
        << ", \"time zone\": " << (timeZone ? jsonText(timeZone) : std::string("null"))
        << ", \"utc offset\": " << text(offset) << "},\n"
        << "  \"result\": {\"frames\": " << frames << ", \"program\": " << jsonText(ended ? "ended" : "still running")
        << ", \"seconds\": " << elapsed << ", \"capture seconds\": " << capturedSeconds
        << ", \"stale captures\": " << staleCaptures
        << ", \"clock restarts\": " << number(startTimeGiven, clockRestarts)
        << ", \"captures\": [" << captureList << "], \"missing functions\": [" << missingList << "]"
        << ", \"last note\": " << text(lastNote) << "},\n"
        << "  \"gpu\": " << gpuCounts << "\n}\n";
    out.close();
    std::error_code renameError;
    if(!out.fail()) fs::rename(part, reportFile, renameError);
    if(out.fail() || renameError) {
      std::error_code ignored;
      fs::remove(part, ignored);
      std::fprintf(stderr, "the report can't be written: %s\n", reportFile.string().c_str());
      return 1;
    }
  }
  return 0;
}
