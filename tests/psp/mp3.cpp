//sceMp3 (mp3.cpp) with a stand-in decoder: the handles, the stream fed into its buffer's halves as the library asks,
//the frames decoded into the sample buffer's halves, the stream's end and its loops, checked against what
//pspautotests' audio/mp3 tests recorded on a PSP. The stream is made up here: MPEG-1 layer III frames at 44.1 kHz and
//128 kbps (417.96 bytes on average: 417, or 418 padded, as an encoder makes them), whose first data bytes say their
//number; the stand-in decodes a frame to 1152 samples, the left its number and the right the sample's place. With
//FFmpeg, and PSP_AUTOTESTS naming a pspautotests checkout, its sample.mp3 is decoded too (never committed: it isn't
//ours).
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::AudioDecoder;

namespace {
constexpr u32 R = KernelMachine::Results, Arguments = 0x0894'0000, Buffer = 0x0894'1000, Pcm = 0x0894'4000;
constexpr u32 Workspace = 1472, Area = 8192 - Workspace;

//Where frame n starts: n frames of 144000 * 128 / 44100 bytes, rounded down.
auto frameAt(u32 n) -> u32 { return u64(n) * 144000 * 128 / 44100; }

struct StandIn : AudioDecoder {
  std::shared_ptr<std::vector<s32>> log;
  auto decode(const u8* data, u32 size, s16* samples, u32 room) -> s32 override {
    if(size != 417 && size != 418) return -1;
    s32 frame = data[4] | data[5] << 8;
    log->push_back(frame);
    for(u32 n = 0; n < std::min(room, 1152u); n++) samples[n * 2] = frame, samples[n * 2 + 1] = n;
    return std::min(room, 1152u);
  }
  auto reset() -> void override { log->push_back(-1); }
};

struct Machine : KernelMachine {
  std::shared_ptr<std::vector<s32>> log = std::make_shared<std::vector<s32>>();
  Machine() {
    kernel.audioDecoders = [log = log](const AudioDecoder::Format&) -> std::unique_ptr<AudioDecoder> {
      auto decoder = std::make_unique<StandIn>();
      decoder->log = log;
      return decoder;
    };
  }
};

//A stream of frames numbered from 0 (each a 128 kbps header, padded or not, its number, the rest 0x5a).
auto stream(u32 frames) -> std::vector<u8> {
  std::vector<u8> out(frameAt(frames), 0x5a);
  for(u32 frame = 0; frame < frames; frame++) {
    u32 at = frameAt(frame);
    bool padded = frameAt(frame + 1) - at == 418;
    out[at] = 0xff, out[at + 1] = 0xfb, out[at + 2] = padded ? 0x92 : 0x90, out[at + 3] = 0x00;
    out[at + 4] = frame, out[at + 5] = frame >> 8;
  }
  return out;
}

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

//A handle for the stream [0, end) with an 8192-byte stream buffer and a 9216-byte sample buffer.
auto reserve(KernelMachine& m, u32 end) -> u32 {
  u32 words[8] = {0, 0, end, 0, Buffer, 8192, Pcm, 9216};
  for(u32 n = 0; n < 8; n++) m.system.memory.write(4, Arguments + n * 4, words[n]);
  return m.call("sceMp3ReserveMp3Handle", {Arguments});
}

//sceMp3GetInfoToAddStreamData's three: where to write (from the buffer's start), how much, and from where.
struct Info { u32 at, bytes, from; };
auto info(KernelMachine& m, u32 handle) -> Info {
  m.call("sceMp3GetInfoToAddStreamData", {handle, R, R + 4, R + 8});
  return {word(m, R) ? word(m, R) - Buffer : 0, word(m, R + 4), word(m, R + 8)};
}

//As a game feeds the library: what it asks for, from the stream, where it says (at the file's end, what's left).
auto feed(KernelMachine& m, u32 handle, const std::vector<u8>& data) -> u32 {
  auto [at, bytes, from] = info(m, handle);
  bytes = std::min<u32>(bytes, data.size() - std::min<u32>(from, data.size()));
  if(!bytes) return 0;
  m.system.memory.copyIn(Buffer + at, data.data() + from, bytes);
  m.call("sceMp3NotifyAddStreamData", {handle, bytes});
  return bytes;
}
}

//Two handles, then none (audio/mp3/reserve); the arguments checked; a handle released is reserved again; none after
//sceMp3TermResource until sceMp3InitResource (audio/mp3/initresource).
static auto mp3Handles() -> void {
  Machine m;
  CHECK(m.call("sceMp3InitResource", {}), 0);
  CHECK(reserve(m, 1000), 0);
  CHECK(reserve(m, 1000), 1);
  CHECK(reserve(m, 1000), 0x8067'1201);
  CHECK(m.call("sceMp3ReleaseMp3Handle", {1}), 0);
  CHECK(m.call("sceMp3ReleaseMp3Handle", {2}), 0x8067'1001);
  m.system.memory.write(4, Arguments + 20, 4096);  //a stream buffer too small
  CHECK(m.call("sceMp3ReserveMp3Handle", {Arguments}), 0x8067'1003);
  m.system.memory.write(4, Arguments + 16, 0);  //and none
  CHECK(m.call("sceMp3ReserveMp3Handle", {Arguments}), 0x8067'1002);
  CHECK(reserve(m, 0), 0x8067'1003);  //ending where it starts
  CHECK(reserve(m, 1000), 1);
  CHECK(m.call("sceMp3GetSumDecodedSample", {5}), 0x8067'1001);
  CHECK(m.call("sceMp3TermResource", {}), 0);
  CHECK(reserve(m, 1000), 0x8067'1201);
  CHECK(m.call("sceMp3GetLoopNum", {0}), 0x8067'1102);
  CHECK(m.call("sceMp3InitResource", {}), 0);
  CHECK(reserve(m, 1000), 0);
  CHECK(m.notes.size(), 0);
}

//A stream from start to end as audio/mp3/stream recorded: the buffer's whole area to fill first, from the file's
//start; the stream described by sceMp3Init; each decode a frame into the sample buffer's halves in turn, 4608 bytes;
//after 9 frames the first half free again for the next 3360 bytes; at the end (no loops) 0, and 0 again.
static auto mp3Stream() -> void {
  Machine m;
  auto data = stream(100);
  u32 handle = reserve(m, data.size());
  CHECK(handle, 0);
  auto first = info(m, handle);
  CHECK(first.at == Workspace && first.bytes == Area && first.from == 0, true);
  CHECK(m.call("sceMp3GetMaxOutputSample", {handle}), 0x8067'1103);  //before sceMp3Init
  CHECK(feed(m, handle, data), Area);
  CHECK(m.call("sceMp3CheckStreamDataNeeded", {handle}), 0);
  CHECK(m.call("sceMp3Init", {handle}), 0);
  CHECK(m.call("sceMp3GetMaxOutputSample", {handle}), 1152);
  CHECK(m.call("sceMp3GetSamplingRate", {handle}), 44100);
  CHECK(m.call("sceMp3GetBitRate", {handle}), 128);
  CHECK(m.call("sceMp3GetMp3ChannelNum", {handle}), 2);
  CHECK(m.call("sceMp3GetMPEGVersion", {handle}), 3);
  CHECK(m.call("sceMp3GetFrameNum", {handle}), 99);  //the stream's bytes over a frame's, rounded down
  CHECK(m.call("sceMp3GetLoopNum", {handle}), 0xffff'ffff);
  CHECK(m.call("sceMp3SetLoopNum", {handle, 0}), 0);
  u32 decodes = 0, wrong = 0;
  for(; decodes < 9; decodes++) {
    CHECK(m.call("sceMp3Decode", {handle, R + 12}), 4608);
    u32 half = Pcm + decodes % 2 * 4608;
    wrong += word(m, R + 12) != half;
    wrong += m.system.memory.read(2, half) != decodes || m.system.memory.read(2, half + 4 * 1151 + 2) != 1151;
    if(decodes < 8) CHECK(m.call("sceMp3CheckStreamDataNeeded", {handle}), 0);
  }
  CHECK(wrong, 0);
  CHECK(m.call("sceMp3CheckStreamDataNeeded", {handle}), 1);
  auto second = info(m, handle);
  CHECK(second.at == Workspace && second.bytes == Area / 2 && second.from == Area, true);
  //on to the end, feeding as asked
  u32 result = 0;
  do {
    feed(m, handle, data);
    result = m.call("sceMp3Decode", {handle, R + 12});
    if(result == 4608) wrong += m.system.memory.read(2, word(m, R + 12)) != decodes++;
  } while(result == 4608 && decodes < 200);
  CHECK(wrong, 0);
  CHECK(result == 0 && decodes == 100, true);
  CHECK(m.call("sceMp3GetSumDecodedSample", {handle}), 100 * 1152);
  CHECK(m.call("sceMp3Decode", {handle, R + 12}), 0);
  CHECK(info(m, handle).bytes, 0);
  std::vector<s32> frames;
  for(s32 n = 0; n < 100; n++) frames.push_back(n);
  CHECK(*m.log == frames, true);
  CHECK(roundTrip(m), true);
  CHECK(m.notes.size(), 0);
}

//Loops: at the stream's end 0, the stream starting again for the game to feed from its start, a loop fewer; and
//sceMp3ResetPlayPositionByFrame starting at a frame's place, a byte early (audio/mp3/resetposbyframe), the frame
//found past it.
static auto mp3Loops() -> void {
  Machine m;
  auto data = stream(20);
  u32 handle = reserve(m, data.size());
  feed(m, handle, data);
  CHECK(m.call("sceMp3Init", {handle}), 0);
  CHECK(m.call("sceMp3SetLoopNum", {handle, 1}), 0);
  u32 decodes = 0, result;
  while((result = m.call("sceMp3Decode", {handle, R + 12})) == 4608) feed(m, handle, data), decodes++;
  CHECK(result == 0 && decodes == 20, true);
  CHECK(m.call("sceMp3GetLoopNum", {handle}), 0);
  auto again = info(m, handle);
  CHECK(again.at == Workspace && again.bytes == Area && again.from == 0, true);
  feed(m, handle, data);
  CHECK(m.call("sceMp3Decode", {handle, R + 12}), 4608);
  CHECK(m.system.memory.read(2, word(m, R + 12)), 0);
  CHECK(m.call("sceMp3ResetPlayPositionByFrame", {handle, 20}), 0x8067'1501);  //past the stream's 19
  CHECK(m.call("sceMp3ResetPlayPositionByFrame", {handle, 5}), 0);
  auto at = info(m, handle);
  CHECK(at.at == Workspace && at.from == frameAt(5) - 1, true);
  CHECK(m.call("sceMp3GetSumDecodedSample", {handle}), 5 * 1152);
  feed(m, handle, data);
  CHECK(m.call("sceMp3Decode", {handle, R + 12}), 4608);
  CHECK(m.system.memory.read(2, word(m, R + 12)), 5);
  CHECK(roundTrip(m), true);
  //a state loaded: the next frame decoded by a decoder made afresh, and a reset needing none
  Machine loaded;
  CHECK(loadState(loaded, saveState(m)), true);
  CHECK(loaded.call("sceMp3Decode", {handle, R + 12}), 4608);
  CHECK(loaded.system.memory.read(2, word(loaded, R + 12)), 6);
  Machine reset;
  CHECK(loadState(reset, saveState(m)), true);
  CHECK(reset.call("sceMp3ResetPlayPosition", {handle}), 0);
  CHECK(info(reset, handle).from, 0);
}

//With FFmpeg's decoders and pspautotests' sample.mp3 (PSP_AUTOTESTS: its checkout), streamed as audio/mp3/stream
//recorded (218 frames by the count): sound in it, neither silence nor clipped, to the stream's end.
static auto mp3FFmpeg() -> void {
  const char* tests = std::getenv("PSP_AUTOTESTS");
  KernelMachine m;
  if(!tests || !*tests || !m.kernel.audioDecoders) return;
  std::ifstream file(std::string(tests) + "/tests/audio/mp3/sample.mp3", std::ios::binary);
  std::vector<u8> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if(data.empty()) return;
  u32 handle = reserve(m, data.size());
  feed(m, handle, data);
  CHECK(m.call("sceMp3Init", {handle}), 0);
  CHECK(m.call("sceMp3GetFrameNum", {handle}), 218);
  m.call("sceMp3SetLoopNum", {handle, 0});
  u32 decodes = 0, samples = 0, loud = 0, clipped = 0, result;
  while((result = m.call("sceMp3Decode", {handle, R + 12})) == 4608 && decodes < 1000) {
    for(u32 n = 0; n < 1152 * 2; n++) {
      s16 sample = m.system.memory.read(2, word(m, R + 12) + n * 2);
      loud += sample > 1000 || sample < -1000;
      clipped += sample == 32767 || sample == -32768;
    }
    samples += 1152 * 2, decodes++;
    feed(m, handle, data);
  }
  CHECK(result == 0 && decodes >= 200, true);
  CHECK(loud > samples / 10 && clipped < samples / 1000, true);
}

auto mp3Tests() -> Tests {
  return {{"mp3 handles", mp3Handles}, {"mp3 stream", mp3Stream}, {"mp3 loops", mp3Loops},
          {"mp3 ffmpeg", mp3FFmpeg}};
}

}
