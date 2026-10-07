//scePsmf and scePsmfPlayer (psmf.cpp, psmfplayer.cpp; docs/psp-core.md's part 31) on movies made up
//(movie-maker.hpp): a header's fields, streams and EP map read through a game's structure; the player's statuses and
//the calls each allows, its start-up, pacing and end as pspautotests' video/psmfplayer recorded them, its play modes,
//its pictures and sound, and its state saved part way. Pictures come from a stand-in decoder (an access unit's own
//bytes give its picture), so the player is tested without FFmpeg too; with FFmpeg, pictures of I_PCM macroblocks are
//converted exactly, in every pixel format, with alpha zero.
#include "movie-maker.hpp"
#include "disc-image.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::AudioDecoder;
using ares::PlayStationPortable::VideoDecoder;

namespace {
constexpr u32 R = KernelMachine::Results;
constexpr u32 Header = 0x0900'0000, Structure = R + 0x100, Copy = R + 0x140, Out = R + 0x180;
constexpr u32 Handle = R, Copied = R + 4, Parameters = R + 0x10, Data = R + 0x20, Request = R + 0x40;
constexpr u32 Info = R + 0x50, Words = R + 0x70, Log = R + 0x1000, Pixels = 0x0980'0000, Sound = 0x09a0'0000;
constexpr u32 NoPlayer = 0x8061'6001, NotSupported = 0x8061'6003, BadKey = 0x8061'6006, BadValue = 0x8061'6008;
constexpr u32 NoData = 0x8061'600c, InUse = 0x8061'8005, BadFile = 0x8002'00d2;

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

auto put(KernelMachine& m, u32 address, std::initializer_list<u32> words) -> void {
  u32 n = 0;
  for(u32 value : words) m.system.memory.write(4, address + n++ * 4, value);
}

//A stand-in for the H.264 decoder: a picture for each access unit, its luma the unit's bytes added up (16 to 215),
//its colour grey: the same unit gives the same picture whichever decoder decodes it.
struct PictureStandIn : VideoDecoder {
  u32 width = 32, height = 32;
  std::vector<u8> planes[3];
  Picture shown;
  auto decode(const u8* data, u32 size) -> bool override {
    u32 sum = 0;
    for(u32 n = 0; n < size; n++) sum += data[n];
    u32 half = (width + 1) / 2, halfHeight = (height + 1) / 2;
    planes[0].assign(width * height, 16 + sum % 200);
    planes[1].assign(half * halfHeight, 128), planes[2].assign(half * halfHeight, 128);
    shown.width = width, shown.height = height;
    for(u32 n = 0; n < 3; n++) shown.planes[n] = planes[n].data(), shown.strides[n] = n ? half : width;
    return true;
  }
  auto picture() const -> const Picture& override { return shown; }
  auto reset() -> void override {}
};

//Logs each ATRAC3plus frame decoded (its fill byte), giving 2048 stereo samples of it.
struct SoundStandIn : AudioDecoder {
  std::shared_ptr<std::vector<u32>> log;
  auto decode(const u8* data, u32 size, s16* samples, u32 room) -> s32 override {
    if(size != 376) return -1;
    if(log) log->push_back(data[0]);
    for(u32 n = 0; n < std::min(room, 2048u) * 2; n++) samples[n] = data[0] * 100 + n % 2;
    return std::min(room, 2048u);
  }
  auto reset() -> void override {}
};

auto standIns(KernelMachine& m, std::shared_ptr<std::vector<u32>> log = {}) -> void {
  m.kernel.videoDecoders = [] { return std::make_unique<PictureStandIn>(); };
  m.kernel.audioDecoders = [log](const AudioDecoder::Format&) -> std::unique_ptr<AudioDecoder> {
    auto decoder = std::make_unique<SoundStandIn>();
    decoder->log = log;
    return decoder;
  };
}

//An access unit for the stand-in: an IDR picture's slice (or a P picture's), and a filler NAL unit of 1 + n % 50
//bytes, so that each unit's picture is its own.
auto unit(u32 n, bool idr = true) -> std::vector<u8> {
  auto bytes = idr ? slices(5, {7}) : slices(1, {5});
  auto filler = nal(12, std::vector<u8>(1 + n % 50, 0xff));
  bytes.insert(bytes.end(), filler.begin(), filler.end());
  return bytes;
}

//A movie of pictures (32 by 32, an IDR one every gop), sound frames, and an EP map at each IDR picture if mapped.
auto standInMovie(u32 pictures, u32 soundFrames = 0, bool mapped = false, u32 gop = 1) -> Movie {
  Movie movie;
  movie.streams.push_back(videoStream(0xe0, 32, 32));
  if(soundFrames) movie.streams.push_back(soundStream(0));
  for(u32 n = 0; n < pictures; n++) movie.units.push_back(unit(n, n % gop == 0));
  if(mapped) for(u32 n = 0; n < pictures; n += gop) movie.streams[0].entries.push_back(n);
  movie.soundFrames = soundFrames;
  return movie;
}

//A machine with a movie on its memory stick (ms0:/M.PMF) and stand-in decoders; Create's parameters at Parameters
//(a working buffer of 3 MiB, the player's priority), Start's at Data (H.264 stream 0, either kind of sound 0,
//play mode 0 at speed 1).
struct Player {
  KernelMachine m;
  HostFolder stick;
  std::shared_ptr<std::vector<u32>> sounds = std::make_shared<std::vector<u32>>();
  Player(const Movie& movie, u32 priority = 0x17) {
    standIns(m, sounds);
    auto bytes = movie.bytes();
    stick.put("M.PMF", std::string(bytes.begin(), bytes.end()));
    m.kernel.mount("ms0", stick.path.string());
    put(m, Parameters, {0x0880'0000, 0x30'0000, priority});
    put(m, Data, {14, 0, 15, 0, 0, 1});
  }
  auto call(const char* name, std::initializer_list<u32> arguments) -> u32 { return m.call(name, arguments); }
  auto create() -> u32 { return call("scePsmfPlayerCreate", {Handle, Parameters}); }
  auto set(const char* path = "ms0:/M.PMF") -> u32 { return call("scePsmfPlayerSetPsmf", {Handle, m.string(path)}); }
  auto start(u32 time = 0) -> u32 { return call("scePsmfPlayerStart", {Handle, Data, time}); }
  auto playing() -> bool { return !create() && !set() && !start(); }
  auto status() -> u32 { return call("scePsmfPlayerGetCurrentStatus", {Handle}); }
  auto update() -> u32 { return call("scePsmfPlayerUpdate", {Handle}); }
  //GetVideoData into Pixels, width pixels a row: its result; the picture's time is then time()
  auto video(u32 width = 512, u32 destination = Pixels) -> u32 {
    put(m, Request, {width, destination, 0x1337});
    return call("scePsmfPlayerGetVideoData", {Handle, Request});
  }
  auto time() -> u32 { return word(m, Request + 8); }
  auto audio() -> u32 { return call("scePsmfPlayerGetAudioData", {Handle, Sound}); }
  auto mode(u32 mode, u32 speed = 1) -> u32 { return call("scePsmfPlayerChangePlayMode", {Handle, mode, speed}); }
  auto pts() -> u32 {
    put(m, Words, {0xdead'beef});
    u32 result = call("scePsmfPlayerGetCurrentPts", {Handle, Words});
    return result ? result : word(m, Words);
  }
};

//The player's functions: each one's NID name, and the statuses it's allowed in (as bits: 1 made, 2 standby, 4
//playing, and 8 for 0x200, played to its end), as the recordings' tables have them.
struct Allowed { const char* name; u32 statuses; };
const std::vector<Allowed> playerFunctions = {
  {"scePsmfPlayerSetTempBuf", 1}, {"scePsmfPlayerSetPsmf", 1}, {"scePsmfPlayerSetPsmfCB", 1},
  {"scePsmfPlayerSetPsmfOffset", 1}, {"scePsmfPlayerSetPsmfOffsetCB", 1}, {"scePsmfPlayerGetPsmfInfo", 2 | 4 | 8},
  {"scePsmfPlayerConfigPlayer", 15}, {"scePsmfPlayerStart", 2 | 4 | 8}, {"scePsmfPlayerUpdate", 4 | 8},
  {"scePsmfPlayerGetVideoData", 4 | 8}, {"scePsmfPlayerGetAudioData", 4 | 8}, {"scePsmfPlayerGetAudioOutSize", 15},
  {"scePsmfPlayerGetCurrentStatus", 15}, {"scePsmfPlayerGetCurrentPts", 2 | 4 | 8},
  {"scePsmfPlayerGetCurrentPlayMode", 15}, {"scePsmfPlayerChangePlayMode", 4 | 8},
  {"scePsmfPlayerGetCurrentVideoStream", 2 | 4 | 8}, {"scePsmfPlayerGetCurrentAudioStream", 2 | 4 | 8},
  {"scePsmfPlayerSelectVideo", 4}, {"scePsmfPlayerSelectAudio", 4}, {"scePsmfPlayerSelectSpecificVideo", 4},
  {"scePsmfPlayerSelectSpecificAudio", 4}, {"scePsmfPlayerBreak", 15}, {"scePsmfPlayerStop", 4 | 8},
  {"scePsmfPlayerReleasePsmf", 2 | 4 | 8}, {"scePsmfPlayerDelete", 15},
};
}

//scePsmf on a header listing two video streams (the first with an EP map of three entries, the second 144 by 80),
//two ATRAC3plus streams, one PCM and one of user data: the structure SetPsmf fills, the counts, times and sizes, the
//version, the streams specified every way and what's reported of them, the EP map read by index and by time, a copy
//of the structure working on its own, the marks; and the refusals: headers SetPsmf and VerifyPsmf refuse, and a
//structure that isn't set up.
static auto psmfHeaders() -> void {
  KernelMachine m;
  Movie movie;
  movie.streams = {videoStream(0xe0, 480, 272, {0, 15, 74}), videoStream(0xe1, 144, 80), soundStream(0x00),
                   soundStream(0x01), soundStream(0x10), soundStream(0x20)};
  for(u32 n = 0; n < 120; n++) movie.units.push_back(unit(n));
  auto bytes = movie.bytes();
  m.system.memory.copyIn(Header, bytes.data(), 2048);
  CHECK(m.call("scePsmfSetPsmf", {Structure, Header}), 0);
  u32 stream = bytes.size() - 2048;
  CHECK(word(m, Structure) == 0x3531'3030 && word(m, Structure + 4) == 0x800, true);
  CHECK(word(m, Structure + 8), stream);
  CHECK(word(m, Structure + 12) == 0 && word(m, Structure + 16) == 0 && word(m, Structure + 20) == 0, true);
  CHECK(word(m, Structure + 24), Header);
  CHECK(m.call("scePsmfGetNumberOfStreams", {Structure}), 6);
  for(auto [kind, count] : {std::pair{0u, 2u}, {1u, 2u}, {2u, 1u}, {3u, 1u}, {15u, 3u}, {7u, 0u}}) {
    CHECK(m.call("scePsmfGetNumberOfSpecificStreams", {Structure, kind}), count);
  }
  CHECK(m.call("scePsmfGetPresentationStartTime", {Structure, Out}) == 0 && word(m, Out) == 90000, true);
  CHECK(m.call("scePsmfGetPresentationEndTime", {Structure, Out}) == 0 && word(m, Out) == 90000 + 120 * 3003, true);
  CHECK(m.call("scePsmfGetHeaderSize", {Structure, Out}) == 0 && word(m, Out) == 0x800, true);
  CHECK(m.call("scePsmfGetStreamSize", {Structure, Out}) == 0 && word(m, Out) == stream, true);
  CHECK(m.call("scePsmfGetPsmfVersion", {Structure}), 15);
  CHECK(m.call("scePsmfQueryStreamOffset", {Header, Out}) == 0 && word(m, Out) == 0x800, true);
  CHECK(m.call("scePsmfQueryStreamSize", {Header, Out}) == 0 && word(m, Out) == stream, true);
  //specified: by number, by kind and channel, by kind and number; what's reported of each
  auto current = [&](u32 number, u32 kind, u32 channel) {
    CHECK(m.call("scePsmfGetCurrentStreamNumber", {Structure}), number);
    CHECK(m.call("scePsmfGetCurrentStreamType", {Structure, Out, Out + 4}), 0);
    CHECK(word(m, Out) == kind && word(m, Out + 4) == channel, true);
  };
  current(0, 0, 0);
  CHECK(m.call("scePsmfGetVideoInfo", {Structure, Out}) == 0 && word(m, Out) == 480 && word(m, Out + 4) == 272, true);
  CHECK(m.call("scePsmfGetAudioInfo", {Structure, Out}), 0x8061'5100);
  CHECK(m.call("scePsmfSpecifyStream", {Structure, 1}), 0);
  current(1, 0, 1);
  CHECK(m.call("scePsmfGetVideoInfo", {Structure, Out}) == 0 && word(m, Out) == 144 && word(m, Out + 4) == 80, true);
  CHECK(m.call("scePsmfGetNumberOfEPentries", {Structure}), 0);  //stream 1 has no EP map
  CHECK(m.call("scePsmfCheckEPmap", {Structure}), 0x8061'5025);
  CHECK(m.call("scePsmfSpecifyStreamWithStreamType", {Structure, 1, 1}), 0);
  current(3, 1, 1);
  CHECK(m.call("scePsmfGetAudioInfo", {Structure, Out}) == 0 && word(m, Out) == 2 && word(m, Out + 4) == 2, true);
  CHECK(m.call("scePsmfGetVideoInfo", {Structure, Out}), 0x8061'5100);
  CHECK(m.call("scePsmfSpecifyStreamWithStreamType", {Structure, 15, 0}), 0);  //either kind of sound, channel 0
  current(2, 1, 0);
  CHECK(m.call("scePsmfSpecifyStreamWithStreamTypeNumber", {Structure, 15, 2}), 0);  //the third sound stream: PCM
  current(4, 2, 0);
  CHECK(m.call("scePsmfSpecifyStreamWithStreamTypeNumber", {Structure, 2, 1}), 0x8061'5100);  //one PCM stream
  current(4, 2, 0);
  CHECK(m.call("scePsmfSpecifyStreamWithStreamTypeNumber", {Structure, 3, 0}), 0);
  current(5, 3, 0);
  //a search that finds none: no current stream, what was selected still reported
  CHECK(m.call("scePsmfSpecifyStreamWithStreamType", {Structure, 0, 5}), 0);
  CHECK(m.call("scePsmfGetCurrentStreamNumber", {Structure}), 0x8061'5100);
  CHECK(m.call("scePsmfGetCurrentStreamType", {Structure, Out, Out + 4}) == 0 && word(m, Out) == 3, true);
  CHECK(m.call("scePsmfGetVideoInfo", {Structure, Out}), 0x8061'5001);
  //a number no stream has: the selection unusable
  CHECK(m.call("scePsmfSpecifyStream", {Structure, 6}), 0x8061'5100);
  CHECK(m.call("scePsmfGetCurrentStreamNumber", {Structure}), 0x8061'5001);
  CHECK(m.call("scePsmfGetCurrentStreamType", {Structure, Out, Out + 4}), 0x8061'5001);
  //the EP map: video stream 0's, selected last
  CHECK(m.call("scePsmfSpecifyStream", {Structure, 0}), 0);
  CHECK(m.call("scePsmfGetNumberOfEPentries", {Structure}), 3);
  CHECK(m.call("scePsmfCheckEPmap", {Structure}), 0);
  CHECK(m.call("scePsmfSpecifyStream", {Structure, 2}), 0);  //a sound stream: the map stays video 0's
  CHECK(m.call("scePsmfGetNumberOfEPentries", {Structure}), 3);
  auto entry = [&](u32 time) { return word(m, Out) == time && word(m, Out + 8) == 0xc0 && word(m, Out + 12) == 0; };
  CHECK(m.call("scePsmfGetEPWithId", {Structure, 1, Out}) == 0 && entry(90000 + 15 * 3003), true);
  u32 pack = word(m, Out + 4);
  CHECK(pack > 0 && pack < stream / 2048, true);  //the pack count, as the map holds it
  CHECK(m.call("scePsmfGetEPWithId", {Structure, 3, Out}), 0x8061'5100);
  CHECK(m.call("scePsmfGetEPWithTimestamp", {Structure, 89999, Out}), 0x8061'5500);
  CHECK(m.call("scePsmfGetEPWithTimestamp", {Structure, 90000, Out}) == 0 && entry(90000), true);
  CHECK(m.call("scePsmfGetEPWithTimestamp", {Structure, 90000 + 15 * 3003 - 1, Out}) == 0 && entry(90000), true);
  CHECK(m.call("scePsmfGetEPWithTimestamp", {Structure, 90000 + 15 * 3003, Out}), 0);
  CHECK(entry(90000 + 15 * 3003) && word(m, Out + 4) == pack, true);
  CHECK(m.call("scePsmfGetEPidWithTimestamp", {Structure, 90000 + 100 * 3003}), 2);
  CHECK(m.call("scePsmfGetEPidWithTimestamp", {Structure, 10}), 0x8061'5500);
  //a copy works on its own, with its own current stream
  for(u32 n = 0; n < 32; n += 4) m.system.memory.write(4, Copy + n, word(m, Structure + n));
  CHECK(m.call("scePsmfSpecifyStream", {Copy, 1}), 0);
  CHECK(m.call("scePsmfGetCurrentStreamNumber", {Copy}), 1);
  CHECK(m.call("scePsmfGetCurrentStreamNumber", {Structure}), 2);
  CHECK(m.call("scePsmfGetNumberOfPsmfMarks", {Structure}), 0);
  CHECK(m.call("scePsmfGetPsmfMark", {Structure, 0, 0, Out}), 0x8061'5100);
  //VerifyPsmf: the four known versions, nothing set up; the 0x100 bytes below the stack pointer zeroed
  constexpr u32 Stack = 0x0890'8000;
  m.system.memory.fill(Stack - 0x200, 0x77, 0x200);
  m.system.ipu.r[29] = Stack;
  CHECK(m.call("scePsmfVerifyPsmf", {Header}), 0);
  CHECK(word(m, Stack - 4) == 0 && word(m, Stack - 0x100) == 0 && word(m, Stack - 0x104) == 0x7777'7777, true);
  m.system.memory.copyIn(Header + 4, "0016", 4);
  CHECK(m.call("scePsmfVerifyPsmf", {Header}), 0x8061'5501);
  CHECK(m.call("scePsmfSetPsmf", {Copy, Header}), 0);  //SetPsmf takes any version but 0...
  CHECK(m.call("scePsmfGetPsmfVersion", {Copy}), 16);
  CHECK(m.call("scePsmfGetNumberOfStreams", {Structure}), 0x8061'5001);  //...and the first structure isn't set now
  m.system.memory.write(4, Header + 4, 0);
  CHECK(m.call("scePsmfSetPsmf", {Copy, Header}), 0x8061'5002);
  m.system.memory.copyIn(Header + 4, "0015", 4);
  m.system.memory.write(4, Header + 8, 0);
  CHECK(m.call("scePsmfSetPsmf", {Copy, Header}), 0x8061'51fe);
  m.system.memory.write(1, Header, 'X');
  CHECK(m.call("scePsmfSetPsmf", {Copy, Header}), 0x8061'5501);
  CHECK(m.call("scePsmfVerifyPsmf", {Header}), 0x8061'5501);
  //a structure that isn't set up (zeros): 0x80615001 from the stream functions, 0x80615025 from the rest
  m.system.memory.fill(Copy, 0, 32);
  for(auto name : {"scePsmfGetNumberOfStreams", "scePsmfGetNumberOfSpecificStreams", "scePsmfSpecifyStream",
                   "scePsmfSpecifyStreamWithStreamType", "scePsmfSpecifyStreamWithStreamTypeNumber",
                   "scePsmfGetCurrentStreamNumber", "scePsmfGetCurrentStreamType", "scePsmfGetVideoInfo",
                   "scePsmfGetAudioInfo"}) {
    check(__LINE__, name, m.call(name, {Copy, 0, 0, Out}), 0x8061'5001);
  }
  for(auto name : {"scePsmfGetPsmfVersion", "scePsmfGetHeaderSize", "scePsmfGetStreamSize",
                   "scePsmfGetPresentationStartTime", "scePsmfGetPresentationEndTime", "scePsmfGetNumberOfEPentries",
                   "scePsmfCheckEPmap", "scePsmfGetEPWithId", "scePsmfGetEPWithTimestamp",
                   "scePsmfGetEPidWithTimestamp", "scePsmfGetNumberOfPsmfMarks", "scePsmfGetPsmfMark"}) {
    check(__LINE__, name, m.call(name, {Copy, 90000, Out, Out}), 0x8061'5025);
  }
}

//The player's statuses and the calls each allows, as video/psmfplayer recorded them (its own tables): every function
//refused with 0x80616001 where its status doesn't allow it, with no player and with a handle word of 0; Create's
//refusals (a priority out of 16-109, a buffer under 0x285800, a second player), the handle word 0 after each; a copy
//of the handle working alike; Delete zeroing the word given; the transitions between them; the end on the second
//Update after the last picture.
static auto psmfPlayerStatuses() -> void {
  Player p(standInMovie(10));
  auto& m = p.m;
  auto refused = [&](u32 statusBit, const char* where) {
    for(auto& f : playerFunctions) {
      if(f.statuses & statusBit) continue;
      check(__LINE__, (std::string(f.name) + where).c_str(), p.call(f.name, {Handle, 0, 0}), NoPlayer);
    }
  };
  refused(0, " with no player");
  CHECK(p.call("scePsmfPlayerGetCurrentStatus", {Handle}), NoPlayer);
  for(auto [priority, size, error] : {std::tuple{15u, 0x30'0000u, BadValue}, {110u, 0x30'0000u, BadValue},
                                      {0x8000'0010u, 0x30'0000u, BadValue}, {0x17u, 0x28'57ffu, 0x8061'6005u},
                                      {0x17u, 0u, 0x8061'6005u}}) {
    put(m, Parameters, {0x0880'0000, size, priority});
    put(m, Handle, {0x1234});
    CHECK(p.create(), error);
    CHECK(word(m, Handle), 0);
  }
  put(m, Parameters, {0x0880'0000, 0xffff'ffff, 109});  //compared unsigned: any size from 0x285800 up
  CHECK(p.create(), 0);
  u32 handle = word(m, Handle);
  CHECK(handle != 0 && p.status() == 1, true);
  CHECK(word(m, word(m, handle)), 0);  //the handle points at a word that points at one holding 0
  put(m, Copied, {0});
  CHECK(p.call("scePsmfPlayerCreate", {Copied, Parameters}), InUse);
  CHECK(word(m, Copied) == 0 && p.status() == 1, true);
  refused(1, " in status 1");
  put(m, Copied, {handle});
  CHECK(p.call("scePsmfPlayerGetCurrentStatus", {Copied}), 1);  //a copy of the handle works alike
  CHECK(p.call("scePsmfPlayerSetTempBuf", {Handle, 0x0890'0000, 0xffff}), BadValue);
  CHECK(p.call("scePsmfPlayerSetTempBuf", {Handle, 0, 0x1'0000}), 0);
  CHECK(p.call("scePsmfPlayerSetTempBuf", {Handle, 0xdead'beef, 0x8000'0000}), 0);
  CHECK(p.call("scePsmfPlayerGetAudioOutSize", {Handle}), 0x2000);
  CHECK(p.call("scePsmfPlayerGetCurrentPlayMode", {Handle, Out, Out + 4}) == 0 && word(m, Out) == 0, true);
  CHECK(word(m, Out + 4), 1);
  CHECK(p.call("scePsmfPlayerSetPsmf", {Handle, 0}), BadValue);
  CHECK(p.set(), 0);
  CHECK(p.status(), 2);
  refused(2, " in status 2");
  CHECK(p.call("scePsmfPlayerGetCurrentPts", {Handle, Out}), NoData);
  CHECK(p.call("scePsmfPlayerGetCurrentVideoStream", {Handle, Out, Out + 4}), 0);
  CHECK(word(m, Out) == ~0u && word(m, Out + 4) == ~0u, true);
  CHECK(p.call("scePsmfPlayerGetPsmfInfo", {Handle, Info}), 0);
  CHECK(word(m, Info) == 9 * 3003 && word(m, Info + 4) == 1 && word(m, Info + 8) == 0, true);
  CHECK(word(m, Info + 16), 1);
  CHECK(p.start(), 0);
  CHECK(p.status(), 4);
  refused(4, " in status 4");
  CHECK(p.call("scePsmfPlayerGetCurrentVideoStream", {Handle, Out, Out + 4}), 0);
  CHECK(word(m, Out) == 0 && word(m, Out + 4) == 0, true);
  CHECK(p.call("scePsmfPlayerGetCurrentAudioStream", {Handle, Out, Out + 4}), 0);  //no sound: -1 and -1
  CHECK(word(m, Out) == ~0u && word(m, Out + 4) == ~0u, true);
  CHECK(p.call("scePsmfPlayerSelectVideo", {Handle}), NotSupported);  //one video stream
  CHECK(p.call("scePsmfPlayerSelectSpecificAudio", {Handle, 1, 0}), NotSupported);
  CHECK(p.audio(), NoData);
  CHECK(p.pts(), NoData);
  CHECK(p.video(0, 0), NoData);  //no picture yet: not even a NULL buffer is looked at
  for(u32 n = 0; n < 40 && p.time() != 9 * 3003; n++) p.update(), p.video();
  CHECK(p.time(), 9 * 3003);                            //the last picture, just handed out
  CHECK(p.update() == 0 && p.status() == 4, true);      //an Update after it
  CHECK(p.update() == 0 && p.status() == 0x200, true);  //the second: the end
  refused(8, " in status 0x200");
  CHECK(p.video() == 0 && p.time() == 9 * 3003 && p.pts() == 9 * 3003, true);  //the last picture, again
  CHECK(p.audio() == NoData && p.update() == 0 && p.status() == 0x200, true);
  CHECK(p.mode(3, 0), 0);  //allowed after the end too
  CHECK(p.start(), 0);  //again from the start
  CHECK(p.status() == 4 && p.pts() == 9 * 3003, true);  //the picture shown stays till the next
  CHECK(p.call("scePsmfPlayerStop", {Handle}), 0);
  CHECK(p.status(), 2);
  CHECK(p.call("scePsmfPlayerReleasePsmf", {Handle}), 0);
  CHECK(p.status(), 1);
  put(m, Copied, {handle});
  CHECK(p.call("scePsmfPlayerDelete", {Copied}), 0);
  CHECK(word(m, Copied) == 0 && word(m, Handle) == handle, true);  //the copy given is zeroed, not the original
  CHECK(p.status(), NoPlayer);
  put(m, Handle, {0});
  refused(0, " with a handle word of 0");
  CHECK(p.create(), 0);  //a new player, after the last was deleted
  CHECK(roundTrip(m), true);
}

//A program playing a movie of ten pictures as basic did (Create with the player's priority, SetPsmf, Start, then
//rounds of two Updates, the status, GetVideoData and the status again, logged), on both engines: the first two
//GetVideoData refused (0x8061600c), the pictures then one a round at 0, 3003, ... from the third (8888, alpha zero),
//and the movie's end on the second Update after its last picture: during it with the player's priority better than
//the program's (0x17 against 0x20), at its next GetVideoData, the call that waits, with it worse (0x28). The movie's
//file stays open through the game's descriptors, and its state goes with the machine's.
static auto psmfPlayerPlaying() -> void {
  for(u32 priority : {0x17u, 0x28u}) {
    for(bool recompile : {false, true}) {
      Player p(standInMovie(10), priority);
      auto& m = p.m;
      Assembler a{m, 0x0880'0000};
      a.li(a0, Handle); a.li(a1, Parameters); a.call("scePsmfPlayerCreate");
      a.li(a0, Handle); a.li(a1, m.string("ms0:/M.PMF")); a.call("scePsmfPlayerSetPsmf");
      a.li(a0, Handle); a.li(a1, Data); a.li(a2, 0); a.call("scePsmfPlayerStart");
      a.li(s0, 0); a.li(s1, Log);
      u32 loop = a.here();
      a.li(a0, Handle); a.call("scePsmfPlayerUpdate");
      a.li(a0, Handle); a.call("scePsmfPlayerUpdate");
      a.li(a0, Handle); a.call("scePsmfPlayerGetCurrentStatus"); a.put(sw(v0, 8, s1));
      a.li(t0, Request); a.li(t1, 512); a.put(sw(t1, 0, t0)); a.li(t1, Pixels); a.put(sw(t1, 4, t0));
      a.li(t1, 0x1337); a.put(sw(t1, 8, t0));
      a.li(a0, Handle); a.li(a1, Request); a.call("scePsmfPlayerGetVideoData"); a.put(sw(v0, 4, s1));
      a.li(t0, Request); a.put(lw(t1, 8, t0)); a.put(sw(t1, 0, s1));
      a.li(a0, Handle); a.call("scePsmfPlayerGetCurrentStatus"); a.put(sw(v0, 12, s1));
      a.put(addiu(s1, s1, 16)); a.put(addiu(s0, s0, 1));
      a.li(t0, 0x200);
      a.put(beq(v0, t0, 5)); a.put(nop);
      a.li(t0, 40);
      u32 at = a.here();
      a.put(bne(s0, t0, s32(loop - at - 4) / 4)); a.put(nop);
      a.call("sceKernelExitGame");
      m.runProgram(0x0880'0000, recompile);
      CHECK(m.kernel.exited, true);
      auto round = [&](u32 n, u32 field) { return word(m, Log + n * 16 + field * 4); };
      CHECK(round(0, 1) == NoData && round(1, 1) == NoData && round(0, 0) == 0x1337, true);
      bool pictures = true;
      for(u32 n = 2; n < 12; n++) pictures &= round(n, 1) == 0 && round(n, 0) == (n - 2) * 3003 && round(n, 3) == 4;
      CHECK(pictures, true);
      CHECK(word(m, Pixels) >> 24 == 0 && word(m, Pixels) != 0, true);  //8888, alpha zero
      CHECK(round(12, 2), priority < 0x20 ? 0x200 : 4);  //the second Update after the last picture...
      CHECK(round(12, 1) == 0 && round(12, 0) == 9 * 3003 && round(12, 3) == 0x200, true);  //...or the next wait
      CHECK(word(m, Log + 13 * 16 + 4), 0);  //(no thirteenth round)
      CHECK(m.kernel.files.size(), 1);  //the movie's file, open
      CHECK(roundTrip(m, [&](KernelMachine& k) { k.kernel.mount("ms0", p.stick.path.string()); }), true);
    }
  }
}

//Pictures converted (with FFmpeg's decoder: pictures of four I_PCM macroblocks of two colours, 32 by 32): BT.601's
//colours with alpha zero, in 8888 at first, then 5650, 5551 and 4444 (ConfigPlayer's pixel formats, -1 leaving it
//as it is); the buffer widths as getvideodata recorded (0 for 512, an odd one less its lowest bit, a narrower one
//refused, the rest of each row left alone; negative, and a NULL buffer, refused).
static auto psmfPlayerPictures() -> void {
  KernelMachine probe;
  if(!probe.kernel.videoDecoders) return;
  std::vector<Colour> colours = {{81, 90, 240}, {145, 54, 34}};
  Movie movie;
  movie.streams = {videoStream(0xe0, 32, 32)};
  for(u32 n = 0; n < 4; n++) movie.units.push_back(picture(2, 2, colours, n & 1));
  Player p(movie);
  auto& m = p.m;
  m.kernel.videoDecoders = probe.kernel.videoDecoders;  //FFmpeg's
  CHECK(p.playing(), true);
  m.system.memory.fill(Pixels, 0xcd, 512 * 40 * 4);
  CHECK(p.video(), NoData);
  CHECK(p.video(), NoData);
  CHECK(p.video(), 0);
  auto rgb = [](Colour c) {
    double y = 1.164 * (c.y - 16), b = c.cb - 128.0, r = c.cr - 128.0;
    auto clamp = [](double v) { return u32(std::clamp(std::lround(v), 0l, 255l)); };
    return std::array<u32, 3>{clamp(y + 1.596 * r), clamp(y - 0.392 * b - 0.813 * r), clamp(y + 2.017 * b)};
  };
  auto near = [](u32 a, u32 b) { return std::max(a, b) - std::min(a, b) <= 1; };
  bool right = true;
  for(u32 y = 0; y < 32; y++) {
    for(u32 x = 0; x < 32; x++) {
      auto want = rgb(colours[(y / 16 * 2 + x / 16) % 2]);
      u32 got = word(m, Pixels + (y * 512 + x) * 4);
      right &= near(got & 0xff, want[0]) && near(got >> 8 & 0xff, want[1]) && near(got >> 16 & 0xff, want[2]);
      right &= got >> 24 == 0;
    }
  }
  CHECK(right, true);
  CHECK(word(m, Pixels + 32 * 4) == 0xcdcd'cdcd && word(m, Pixels + 512 * 4) != 0xcdcd'cdcd, true);
  for(u32 format : {0u, 1u, 2u}) {
    CHECK(p.call("scePsmfPlayerConfigPlayer", {Handle, 1, format}), 0);
    CHECK(p.call("scePsmfPlayerConfigPlayer", {Handle, 1, u32(-1)}), 0);  //as it is
    CHECK(p.video(), 0);
    u32 pixel = m.system.memory.read(2, Pixels), want = 0;
    auto c = rgb(colours[0]);
    if(format == 0) want = c[0] >> 3 | (c[1] >> 2) << 5 | (c[2] >> 3) << 11;
    if(format == 1) want = c[0] >> 3 | (c[1] >> 3) << 5 | (c[2] >> 3) << 10;
    if(format == 2) want = c[0] >> 4 | (c[1] >> 4) << 4 | (c[2] >> 4) << 8;
    CHECK(pixel == want || pixel == want + 1 || pixel + 1 == want, true);
    CHECK(pixel >> (format == 1 ? 15 : format == 2 ? 12 : 16), 0);  //alpha bits zero
    CHECK(m.system.memory.read(2, Pixels + 512 * 2) != 0xcdcd, true);  //the next row, 512 pixels on
  }
  CHECK(p.call("scePsmfPlayerConfigPlayer", {Handle, 1, 4}), BadValue);
  CHECK(p.call("scePsmfPlayerConfigPlayer", {Handle, 2, 0}), BadKey);
  CHECK(p.call("scePsmfPlayerConfigPlayer", {Handle, 1, 3}), 0);
  for(auto [width, error, row] : {std::tuple{u32(-1), 0x8000'0023u, 0u}, {1u, 0x8000'01feu, 0u},
                                  {31u, 0x8000'01feu, 0u}, {33u, 0u, 32u}, {48u, 0u, 48u}, {0u, 0u, 512u}}) {
    m.system.memory.fill(Pixels, 0xcd, 512 * 4 * 4);
    CHECK(p.video(width), error);
    if(error) { CHECK(word(m, Pixels), 0xcdcd'cdcd); continue; }
    CHECK(word(m, Pixels + 31 * 4) != 0xcdcd'cdcd && word(m, Pixels + row * 4) != 0xcdcd'cdcd, true);
    if(row > 32) CHECK(word(m, Pixels + 32 * 4) == 0xcdcd'cdcd && word(m, Pixels + (row - 1) * 4) == 0xcdcd'cdcd,
                       true);  //the rest of the first row left alone
  }
  CHECK(p.video(512, 0), 0x8000'0103);
}

//Sound (a stand-in decoder): none before the start-up is over; then the frames in order, the first silent; a picture
//held while it's more than two pictures ahead of the next frame waiting, and two passed over while the sound is more
//than four ahead; the movie's end waiting for the sound to be taken. Then a movie without sound asked for (a
//negative sound stream): no sound, the end without it.
static auto psmfPlayerSound() -> void {
  Player p(standInMovie(20, 40));
  auto& m = p.m;
  CHECK(p.playing(), true);
  CHECK(p.audio(), NoData);
  CHECK(p.video() == NoData && p.video() == NoData && p.video() == 0 && p.time() == 0, true);
  CHECK(p.call("scePsmfPlayerGetCurrentAudioStream", {Handle, Out, Out + 4}), 0);
  CHECK(word(m, Out) == 1 && word(m, Out + 4) == 0, true);
  //no sound taken: its clock stays at its first frame (time 0), and pictures stop two ahead of it
  for(u32 n = 0; n < 8; n++) p.update(), p.update(), p.video();
  CHECK(p.time(), 2 * 3003);
  CHECK(p.audio() == 0 && m.system.memory.read(2, Sound) == 0 && m.system.memory.read(2, Sound + 0x1ffe) == 0, true);
  CHECK(p.audio() == 0 && m.system.memory.read(2, Sound) == 0x41 * 100, true);  //the second frame, heard
  CHECK(m.system.memory.read(2, Sound + 2), 0x41 * 100 + 1);
  CHECK(*p.sounds == std::vector<u32>({0x40, 0x41}), true);
  p.update(), p.update(), p.video();  //the clock at 8360 now: 9009 comes
  CHECK(p.time(), 3 * 3003);
  //the sound taken far ahead (to 41800): two pictures passed over each time one comes
  for(u32 n = 0; n < 8; n++) p.audio();
  p.update(), p.update(), p.video();
  CHECK(p.time(), 6 * 3003);  //12012 and 15015 passed over
  //taking a frame a round, the pictures run out before the sound: the end waits for it all to be taken
  for(u32 n = 0; n < 40 && p.time() != 19 * 3003; n++) p.update(), p.update(), p.video(), p.audio();
  CHECK(p.time(), 19 * 3003);
  p.update(), p.update();
  CHECK(p.status(), 4);
  while(p.audio() == 0) {}
  CHECK(p.sounds->size(), 40);
  p.update();
  CHECK(p.status(), 0x200);
  //a negative sound stream: none
  Player q(standInMovie(6, 12));
  put(q.m, Data, {14, 0, 15, u32(-1), 0, 1});
  CHECK(q.playing(), true);
  CHECK(q.call("scePsmfPlayerGetCurrentAudioStream", {Handle, Out, Out + 4}), 0);
  CHECK(word(q.m, Out), ~0u);
  for(u32 n = 0; n < 12 && q.status() == 4; n++) q.update(), q.update(), q.video(), CHECK(q.audio(), NoData);
  CHECK(q.status() == 0x200 && q.sounds->empty(), true);
}

//The play modes, as playmode recorded them: slow motion a picture every six Updates, pause and step frame none, the
//picture already due when the mode changed coming in all three; step frame asked for again stepping a picture; play
//again from pause; fast forward and rewind refused in a movie without an EP map. Then a movie with one (an entry
//every four pictures): fast forward showing every entry's picture, one each 15 Updates (20 at speed 2), rewind going
//back, to the first entry and on playing; forward past the last ending the movie. And looping: at its end the movie
//starts again, its status 4, its start-up again.
static auto psmfPlayerModes() -> void {
  Player p(standInMovie(60));
  CHECK(p.playing(), true);
  for(u32 n = 0; n < 4; n++) p.update(), p.update(), p.video();
  CHECK(p.time(), 1 * 3003);
  auto count = [&](u32 updates) {  //pictures coming in so many Updates, a GetVideoData after each
    u32 last = p.time(), changes = 0;
    for(u32 n = 0; n < updates; n++) {
      p.update(), p.video();
      if(p.time() != last) changes++, last = p.time();
    }
    return changes;
  };
  CHECK(p.mode(1, 5), 0);
  CHECK(p.call("scePsmfPlayerGetCurrentPlayMode", {Handle, Out, Out + 4}) == 0 && word(p.m, Out + 4) == 1, true);
  CHECK(count(99), 17);  //one at 2 (already due), then every 6
  CHECK(p.mode(3), 0);
  CHECK(count(50), 1);   //paused: the one already due
  CHECK(count(50), 0);
  CHECK(p.mode(2), 0);
  CHECK(count(20), 1);   //into step frame: a step
  CHECK(count(20), 0);
  CHECK(p.mode(2), 0);
  CHECK(count(20), 1);   //step frame asked for again: another
  CHECK(p.mode(0), 0);
  CHECK(count(20), 10);  //playing: every second Update
  CHECK(p.mode(4, 1), NotSupported);
  CHECK(p.mode(5, 1), NotSupported);
  CHECK(p.mode(6, 1), BadKey);
  CHECK(p.mode(u32(-1), 1), BadKey);
  //a movie with an EP map
  Player f(standInMovie(40, 0, true, 4));
  CHECK(f.playing(), true);
  CHECK(f.call("scePsmfPlayerGetPsmfInfo", {Handle, Info}) == 0 && word(f.m, Info + 16) == 0, true);
  for(u32 n = 0; n < 3; n++) f.update(), f.update(), f.video();
  CHECK(f.time(), 0);
  CHECK(f.mode(4, 4), BadValue);
  CHECK(f.mode(4, 1), 0);
  auto step = [&](u32 updates) {
    for(u32 n = 0; n < updates; n++) f.update(), f.video();
    return f.time();
  };
  CHECK(step(14), 0);
  CHECK(step(1), 4 * 3003);   //the next entry's picture, 15 Updates on
  CHECK(step(15), 8 * 3003);
  CHECK(f.audio(), NoData);
  CHECK(f.mode(4, 2), 0);
  CHECK(step(20), 16 * 3003);  //two entries on, 20 Updates
  CHECK(f.mode(5, 1), 0);
  CHECK(step(15), 12 * 3003);  //back an entry
  CHECK(f.mode(5, 3), 0);
  CHECK(step(25), 12 * 3003);  //back to the first entry: playing on from the start...
  CHECK(f.call("scePsmfPlayerGetCurrentPlayMode", {Handle, Out, Out + 4}) == 0 && word(f.m, Out) == 0, true);
  for(u32 n = 0; n < 2; n++) f.update(), f.update(), f.video();
  CHECK(f.time(), 0);  //...after a start-up (its first call came with the last step), from the first picture
  CHECK(f.mode(4, 3), 0);
  for(u32 n = 0; n < 100 && f.status() == 4; n++) f.update(), f.video();
  CHECK(f.status(), 0x200);  //forward past the last entry: the end
  //started at a time: from the entry before it, the pictures before it passed over
  CHECK(f.start(11 * 3003), 0);
  for(u32 n = 0; n < 3; n++) f.update(), f.update(), f.video();
  CHECK(f.time(), 11 * 3003);
  CHECK(f.start(40 * 3003), BadValue);  //at its end
  //looping
  Player l(standInMovie(4));
  CHECK(l.playing(), true);
  CHECK(l.call("scePsmfPlayerConfigPlayer", {Handle, 0, 0}), 0);
  for(u32 n = 0; n < 6; n++) l.update(), l.update(), l.video();
  CHECK(l.time(), 3 * 3003);
  l.update(), l.update();
  CHECK(l.status() == 4 && l.video() == 0 && l.time() == 3 * 3003, true);  //again: its start-up
  l.video();
  CHECK(l.video() == 0 && l.time() == 0, true);
}

//Movies set every way: SetPsmfOffset (the movie 2048 bytes into its file) and the CB functions; a movie on the disc
//image; refusals, each 0x800200d2 and the status still 1 (no such file, a folder, a file that isn't a movie, offsets
//-1, 0x80000000 and 2048 where none begins, a build without decoders), the NULL path 0x80616008; the stream counts
//and version GetPsmfInfo gives; Start's refusals as start recorded them (codecs, streams, modes and times, on a movie
//with sound and one without); a second video stream and a second sound stream switched to, and the switch refused
//where the header lists them but its packs carry none.
static auto psmfPlayerFiles() -> void {
  Player p(standInMovie(8));
  auto& m = p.m;
  auto bytes = standInMovie(8).bytes();
  std::string offset(2048, '\0');
  offset.append(bytes.begin(), bytes.end());
  p.stick.put("OFFSET.PMF", offset);
  p.stick.put("TEXT.PMF", "not a movie at all, not even close to one, as long as a header or longer");
  p.stick.put("DIR/X", "x");
  CHECK(p.create(), 0);
  for(auto [path, at] : {std::pair{"ms0:/NONE.PMF", 0u}, {"ms0:/DIR", 0u}, {"ms0:/TEXT.PMF", 0u},
                         {"ms0:/M.PMF", u32(-1)}, {"ms0:/M.PMF", 0x8000'0000u}, {"ms0:/M.PMF", 2048u}}) {
    CHECK(p.call("scePsmfPlayerSetPsmfOffset", {Handle, m.string(path), at}), BadFile);
    CHECK(p.status(), 1);
  }
  CHECK(m.kernel.files.empty(), true);  //nothing left open
  CHECK(p.call("scePsmfPlayerSetPsmfOffsetCB", {Handle, m.string("ms0:/OFFSET.PMF"), 2048}), 0);
  CHECK(p.start() == 0 && p.video() == NoData && p.video() == NoData && p.video() == 0 && p.time() == 0, true);
  CHECK(p.call("scePsmfPlayerReleasePsmf", {Handle}) == 0 && m.kernel.files.empty(), true);
  //on the disc
  m.kernel.disc = discFrom(disc_image::makeIso({{"MOVIE/M.PMF", bytes}}).bytes);
  CHECK(p.call("scePsmfPlayerSetPsmfCB", {Handle, m.string("disc0:/MOVIE/M.PMF")}), 0);
  CHECK(p.start() == 0 && p.video() == NoData && p.video() == NoData && p.video() == 0, true);
  CHECK(p.call("scePsmfPlayerDelete", {Handle}), 0);
  //without decoders, as sceMpeg refuses headers then
  m.kernel.videoDecoders = {};
  CHECK(p.create() == 0 && p.set() == BadFile, true);
  standIns(m);
  CHECK(p.call("scePsmfPlayerDelete", {Handle}), 0);
  //two of each stream: video 0xe0 and 0xe1, ATRAC3plus 0 and 1
  Movie two = standInMovie(12, 30);
  two.streams.push_back(videoStream(0xe1, 32, 32));
  two.streams.push_back(soundStream(1));
  Player t(two);
  CHECK(t.create() == 0 && t.set() == 0, true);
  CHECK(t.call("scePsmfPlayerGetPsmfInfo", {Handle, Info}), 0);
  CHECK(word(t.m, Info + 4) == 2 && word(t.m, Info + 8) == 2 && word(t.m, Info + 12) == 0, true);
  auto start = [&](std::initializer_list<u32> data, u32 time = 0) {
    put(t.m, Data, data);
    return t.start(time);
  };
  CHECK(start({1, 0, 15, 0, 0, 1}), NotSupported);
  CHECK(start({14, 2, 15, 0, 0, 1}), BadKey);
  CHECK(start({14, u32(-1), 15, 0, 0, 1}), BadKey);
  CHECK(start({14, 0, 0, 0, 0, 1}), NotSupported);
  CHECK(start({14, 0, 14, 0, 0, 1}), NotSupported);
  CHECK(start({14, 0, 15, 2, 0, 1}), BadKey);
  CHECK(start({14, 0, 15, 0, 4, 1}), BadValue);  //no EP map
  CHECK(start({14, 0, 15, 0, 0, 1}, 3003), BadValue);
  CHECK(t.status(), 2);
  CHECK(start({0, 1, 1, 1, 0, u32(-2)}), 0);
  CHECK(t.call("scePsmfPlayerGetCurrentPlayMode", {Handle, Out, Out + 4}), 0);
  CHECK(word(t.m, Out + 4), u32(-2));
  CHECK(t.call("scePsmfPlayerGetCurrentVideoStream", {Handle, Out, Out + 4}), 0);
  CHECK(word(t.m, Out) == 0 && word(t.m, Out + 4) == 1, true);
  CHECK(t.call("scePsmfPlayerGetCurrentAudioStream", {Handle, Out, Out + 4}), 0);
  CHECK(word(t.m, Out) == 1 && word(t.m, Out + 4) == 1, true);
  CHECK(t.video() == NoData && t.video() == NoData && t.video() == 0 && t.audio() == 0, true);
  CHECK(t.audio() == 0 && t.sounds->back() == 0x41 + 0x20, true);  //sound stream 1's second frame
  CHECK(t.call("scePsmfPlayerSelectAudio", {Handle}), 0);  //on to stream 0, wrapping round
  CHECK(t.call("scePsmfPlayerGetCurrentAudioStream", {Handle, Out, Out + 4}) == 0 && word(t.m, Out + 4) == 0, true);
  CHECK(t.call("scePsmfPlayerSelectSpecificVideo", {Handle, 1, 0}), NotSupported);
  CHECK(t.call("scePsmfPlayerSelectSpecificVideo", {Handle, 14, 2}), BadKey);
  CHECK(t.call("scePsmfPlayerSelectSpecificVideo", {Handle, 0, 0}), 0);
  CHECK(t.call("scePsmfPlayerGetCurrentVideoStream", {Handle, Out, Out + 4}) == 0 && word(t.m, Out + 4) == 0, true);
  CHECK(t.call("scePsmfPlayerSelectVideo", {Handle}), 0);
  //the header lists two of each, its packs one of each: no switching
  Movie listed = standInMovie(8, 8);
  Player u(listed);
  auto header = listed.bytes();
  header[0x80 + 1] = 4;  //four streams: the two made, and two more the table's next entries hold
  for(u32 n : {2u, 3u}) {
    header[0x82 + n * 16] = n == 2 ? 0xe1 : 0xbd, header[0x82 + n * 16 + 1] = n == 2 ? 0 : 1;
  }
  u.stick.put("M.PMF", std::string(header.begin(), header.end()));
  CHECK(u.playing(), true);
  CHECK(u.call("scePsmfPlayerSelectVideo", {Handle}) == NotSupported && u.call("scePsmfPlayerSelectAudio", {Handle})
        == NotSupported, true);
}

//A state saved part way through a movie (a program playing it, the thread waiting in GetVideoData's moment, pictures
//decoded ahead, the movie's file open) loads into a fresh machine, which makes the very same state, and the two carry
//on alike to the movie's end: the same pictures at the same times. In a movie of P pictures between its IDR ones,
//the decoder made afresh passes over pictures until the next IDR one, the pictures decoded ahead first.
static auto psmfPlayerStates() -> void {
  for(u32 gop : {1u, 5u}) {
    Player p(standInMovie(30, 0, false, gop));
    auto& m = p.m;
    Assembler a{m, 0x0880'0000};
    a.li(a0, Handle); a.li(a1, Parameters); a.call("scePsmfPlayerCreate");
    a.li(a0, Handle); a.li(a1, m.string("ms0:/M.PMF")); a.call("scePsmfPlayerSetPsmf");
    a.li(a0, Handle); a.li(a1, Data); a.li(a2, 0); a.call("scePsmfPlayerStart");
    a.li(s1, Log);
    u32 loop = a.here();
    a.li(a0, Handle); a.call("scePsmfPlayerUpdate");
    a.li(a0, Handle); a.call("scePsmfPlayerUpdate");
    a.li(t0, Request); a.li(t1, 512); a.put(sw(t1, 0, t0)); a.li(t1, Pixels); a.put(sw(t1, 4, t0));
    a.li(a0, Handle); a.li(a1, Request); a.call("scePsmfPlayerGetVideoData");
    a.li(t1, 1); a.put(sw(t1, 0, s1));
    a.li(t0, Request); a.put(lw(t1, 8, t0)); a.put(sw(t1, 4, s1));
    a.li(t0, Pixels); a.put(lw(t1, 0, t0)); a.put(sw(t1, 8, s1));
    a.put(addiu(s1, s1, 12));
    a.li(a0, 1000); a.call("sceKernelDelayThread");
    a.li(a0, Handle); a.call("scePsmfPlayerGetCurrentStatus");
    a.li(t0, 0x200);
    u32 at = a.here();
    a.put(bne(v0, t0, s32(loop - at - 4) / 4)); a.put(nop);
    a.call("sceKernelExitGame");
    m.system.recompiler.enabled = false;
    m.system.power(0x0880'0000);
    s32 uid = m.kernel.createThread("main", 0x0880'0000, 0x20, 0x4000, 0, 0);
    m.kernel.startThread(*m.kernel.threads[uid], 0, 0);
    m.kernel.run(Kernel::CPUFrequency / 50);  //part way: about a dozen pictures in, then on to a P picture to decode
    auto next = [&] { return (m.kernel.psmfPlayer.videoTime - 90000) / 3003 + 1; };
    while(gop > 1 && next() % gop == 0) m.kernel.run(Kernel::CPUFrequency / 5000);
    CHECK(m.kernel.psmfPlayer.status == 4 && m.kernel.psmfPlayer.pictures.size() > 0, true);
    auto state = saveState(m);
    Player q(standInMovie(30, 0, false, gop));
    auto& n = q.m;
    n.kernel.mount("ms0", p.stick.path.string());
    CHECK(loadState(n, state), true);
    CHECK(saveState(n) == state, true);
    m.kernel.run(Kernel::CPUFrequency / 3);
    n.kernel.run(Kernel::CPUFrequency / 3);
    CHECK(m.kernel.exited && n.kernel.exited, true);
    auto rounds = [](KernelMachine& k) {
      u32 count = 0;
      while(word(k, Log + count * 12) == 1) count++;
      return count;
    };
    u32 same = 0, went = rounds(m), loaded = rounds(n);
    bool skipped = false;
    for(u32 at = Log; at < Log + std::min(went, loaded) * 12; at += 12) {
      if(word(m, at + 4) == word(n, at + 4) && word(m, at + 8) == word(n, at + 8)) same++;
      else skipped |= word(n, at + 4) > word(m, at + 4);
    }
    if(gop == 1) CHECK(same == went && loaded == went, true);
    if(gop != 1) CHECK(skipped && loaded < went, true);  //the loaded machine's pictures jumped to an IDR one
    CHECK(word(m, Log + (went - 1) * 12 + 4) == 29 * 3003 && word(n, Log + (loaded - 1) * 12 + 4) == 29 * 3003,
          true);  //both to the last picture
  }
}

auto psmfTests() -> Tests {
  return {{"psmf headers", psmfHeaders}, {"psmf player statuses", psmfPlayerStatuses},
          {"psmf player playing", psmfPlayerPlaying}, {"psmf player pictures", psmfPlayerPictures},
          {"psmf player sound", psmfPlayerSound}, {"psmf player modes", psmfPlayerModes},
          {"psmf player files", psmfPlayerFiles}, {"psmf player states", psmfPlayerStates}};
}

}
