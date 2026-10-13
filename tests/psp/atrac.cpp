//sceAtrac3plus (atrac.cpp) with a stand-in decoder: what the library does with a file, its buffers, loops and
//positions, checked against what pspautotests' audio/atrac tests recorded on a PSP. The files are made up here in
//sample.at3's shape (an ATRAC3plus file of 123 frames of 376 bytes from 0x60 on, 247501 samples, a sample offset of
//2048), so the recorded numbers apply as they are; each frame's first word is its number, and the stand-in decodes
//it to samples saying which frame and which sample they are, and logs the frames it decodes. With FFmpeg, and
//PSP_AUTOTESTS naming a pspautotests checkout, sample.at3 itself is decoded too (never committed: it isn't ours).
#include "kernel-machine.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::AudioDecoder;

namespace {
constexpr u32 R = KernelMachine::Results, File = 0x0894'0000, Samples = 0x0898'0000, Second = 0x089a'0000;

//Decodes a frame to 2048 samples a channel: the left the frame's number, the right the sample's place in the frame.
//A frame whose fifth byte is 0xee won't decode. Logs each frame decoded, and -1 for each reset.
struct StandIn : AudioDecoder {
  Format format;
  std::shared_ptr<std::vector<s32>> log;
  auto decode(const u8* data, u32 size, s16* samples, u32 room) -> s32 override {
    if(size != format.frameBytes || data[4] == 0xee) return -1;
    s32 frame = data[0] | data[1] << 8;
    log->push_back(frame);
    for(u32 n = 0; n < std::min(room, 2048u); n++) {
      samples[n * format.channels] = frame;
      if(format.channels == 2) samples[n * 2 + 1] = n;
    }
    return std::min(room, 2048u);
  }
  auto reset() -> void override { log->push_back(-1); }
};

struct Machine : KernelMachine {
  std::shared_ptr<std::vector<s32>> log = std::make_shared<std::vector<s32>>();
  Machine() {
    kernel.audioDecoders = [log = log](const AudioDecoder::Format& format) -> std::unique_ptr<AudioDecoder> {
      auto decoder = std::make_unique<StandIn>();
      decoder->format = format;
      decoder->log = log;
      return decoder;
    };
  }
};

//An ATRAC3plus file in sample.at3's shape: the header (WAVE_FORMAT_EXTENSIBLE with ATRAC3plus's GUID, fact, a smpl
//loop when asked for, data), then frames numbered from 0.
auto file(s64 loopStart = -1, s64 loopEnd = -1, u32 frames = 123) -> std::vector<u8> {
  std::vector<u8> out;
  auto put32 = [&](u32 value) { for(u32 n = 0; n < 4; n++) out.push_back(value >> n * 8); };
  auto put16 = [&](u32 value) { out.push_back(value); out.push_back(value >> 8); };
  bool loop = loopStart >= 0;
  out.insert(out.end(), {'R', 'I', 'F', 'F'});
  put32(0);
  out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
  put32(52);
  put16(0xfffe); put16(2); put32(44100); put32(8096); put16(376); put16(0); put16(34); put16(2048); put32(3);
  for(u8 byte : {0xbf, 0xaa, 0x23, 0xe9, 0x58, 0xcb, 0x71, 0x44, 0xa1, 0x19, 0xff, 0xfa, 0x01, 0xe4, 0xce, 0x62}) {
    out.push_back(byte);
  }
  put16(1); out.push_back(0x28); out.push_back(0x2e); put32(0); put32(0);
  out.insert(out.end(), {'f', 'a', 'c', 't'});
  put32(8); put32(247501); put32(2048);
  if(loop) {
    out.insert(out.end(), {'s', 'm', 'p', 'l'});
    put32(60);
    for(u32 value : {0u, 0u, 22676u, 60u, 0u, 0u, 0u, 1u, 0x18u, 0u, 0u}) put32(value);
    put32(loopStart); put32(loopEnd); put32(0); put32(0);
  }
  out.insert(out.end(), {'d', 'a', 't', 'a'});
  put32(frames * 376);
  for(u32 frame = 0; frame < frames; frame++) {
    u32 at = out.size();
    out.resize(at + 376, 0x5a);
    out[at] = frame, out[at + 1] = frame >> 8, out[at + 2] = 0, out[at + 3] = 0;
  }
  u32 size = out.size() - 8;
  for(u32 n = 0; n < 4; n++) out[4 + n] = size >> n * 8;
  return out;
}

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

//A decode's results: its result, the samples, the end flag and the frames left.
struct Decoded { u32 result, count, ended; s32 remain; };
auto decode(KernelMachine& m, u32 id, u32 output = Samples) -> Decoded {
  u32 result = m.call("sceAtracDecodeData", {id, output, R, R + 4, R + 8});
  return {result, word(m, R), word(m, R + 4), s32(word(m, R + 8))};
}

//sceAtracGetStreamDataInfo's three: where to write (from the buffer's start), how much, and from where in the file.
struct Info { u32 at, bytes, from; };
auto info(KernelMachine& m, u32 id, u32 buffer = File) -> Info {
  m.call("sceAtracGetStreamDataInfo", {id, R, R + 4, R + 8});
  return {word(m, R) - buffer, word(m, R + 4), word(m, R + 8)};
}
}

//The six IDs (audio/atrac/ids): ATRAC3plus 0 and 1, ATRAC3 2 and 3 at first; sceAtracReinit shares them again, an
//ATRAC3plus ID taking room for two, asking for more getting what fits and 0x80000022, refused while any is handed
//out; a released ID is handed out again.
static auto atracIDs() -> void {
  Machine m;
  auto ids = [&](u32 codec) {
    std::vector<u32> got;
    for(u32 n = 0; n < 8; n++) {
      u32 id = m.call("sceAtracGetAtracID", {codec});
      if(s32(id) >= 0) got.push_back(id);
      else CHECK(id, 0x8063'0003);
    }
    for(u32 id : got) m.call("sceAtracReleaseAtracID", {id});
    return got;
  };
  CHECK(ids(0x1000) == std::vector<u32>({0, 1}) && ids(0x1001) == std::vector<u32>({2, 3}), true);
  CHECK(m.call("sceAtracReinit", {3, 1}), 0);
  CHECK(ids(0x1000) == std::vector<u32>({0}) && ids(0x1001) == std::vector<u32>({1, 2, 3}), true);
  CHECK(m.call("sceAtracReinit", {999, 0}), Kernel::ErrorOutOfMemory);
  CHECK(ids(0x1000).empty() && ids(0x1001).size() == 6, true);
  CHECK(m.call("sceAtracReinit", {0, 999}), Kernel::ErrorOutOfMemory);
  CHECK(ids(0x1000) == std::vector<u32>({0, 1, 2}) && ids(0x1001).empty(), true);
  CHECK(m.call("sceAtracReinit", {0, u32(-1)}), 0);
  CHECK(ids(0x1000).empty() && ids(0x1001).empty(), true);
  CHECK(m.call("sceAtracReinit", {6, 0}), 0);
  u32 id = m.call("sceAtracGetAtracID", {0x1001});
  CHECK(m.call("sceAtracReinit", {0, 0}), Kernel::ErrorBusy);
  CHECK(m.call("sceAtracReleaseAtracID", {id}), 0);
  CHECK(m.call("sceAtracReleaseAtracID", {id}), 0x8063'0005);
  CHECK(m.call("sceAtracGetAtracID", {0x1002}), Kernel::ErrorInvalidValue);
  CHECK(roundTrip(m), true);
}

//Setting a file (audio/atrac/setdata, headerfields): one of the other codec, an ID not handed out, no bytes, bytes
//that aren't a RIFF file, and a header whose samples, offset and decoder delay need more frames than its data holds
//are refused; sceAtracSetDataAndGetID hands out an ID of the file's codec. The four given the whole file, and a
//halfway one said to be read as far as its buffer goes, take a size whose top bit is set as far as memory goes (none
//at all: no file). Without decoders (a build without FFmpeg)
//every file is refused as unreadable, as before there were any.
static auto atracSetting() -> void {
  Machine m;
  auto at3 = file();
  m.system.memory.copyIn(File, at3.data(), at3.size());
  u32 classic = m.call("sceAtracGetAtracID", {0x1001}), plus = m.call("sceAtracGetAtracID", {0x1000});
  CHECK(m.call("sceAtracSetData", {classic, File, u32(at3.size())}), 0x8063'0007);
  CHECK(m.call("sceAtracSetData", {plus, File, u32(at3.size())}), 0);
  CHECK(m.call("sceAtracSetData", {1, File, u32(at3.size())}), 0x8063'0005);
  CHECK(m.call("sceAtracSetData", {plus, File, 0}), 0x8063'0011);
  CHECK(m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())}), 1);
  std::vector<u8> zeros(0x1000);
  m.system.memory.copyIn(File + 0x10000, zeros.data(), zeros.size());
  CHECK(m.call("sceAtracSetDataAndGetID", {File + 0x10000, 0x1000}), 0x8063'0006);
  //the limit: 247501 + 2048 + 368 fits 123 frames of 2048 (251904); 4488 more samples don't
  m.system.memory.write(4, File + 0x50, 251904 - 2048 - 368);
  CHECK(m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())}), 0x8063'0003);  //no ID free
  m.call("sceAtracReleaseAtracID", {1});
  CHECK(m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())}), 1);
  m.call("sceAtracReleaseAtracID", {1});
  m.system.memory.write(4, File + 0x50, 251904 - 2048 - 368 + 1);
  CHECK(m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())}), 0x8063'0008);
  m.system.memory.write(4, File + 0x50, 247501);
  //a size taken as negative reaches to the end of memory (7th Dragon 2020-II's 0xffffffff): the whole file there,
  //and a file ending where memory does all of it (the reach found to the byte)
  CHECK(m.call("sceAtracSetDataAndGetID", {File, 0xffff'ffff}), 1);
  m.call("sceAtracGetSoundSample", {1, R, R + 4, R + 8});
  CHECK(word(m, R), 247500);
  m.call("sceAtracGetRemainFrame", {1, R});
  CHECK(word(m, R), ~0u);  //(all of it loaded)
  m.call("sceAtracReleaseAtracID", {1});
  u32 last = 0x0800'0000 + u32(m.system.memory.ram.size()) - u32(at3.size());
  m.system.memory.copyIn(last, at3.data(), at3.size());
  CHECK(m.call("sceAtracSetDataAndGetID", {last, 0x8000'0000}), 1);
  m.call("sceAtracGetRemainFrame", {1, R});
  CHECK(word(m, R), ~0u);
  m.call("sceAtracReleaseAtracID", {1});
  //the same for the others given the whole file: on an ID, and the mono ones (the file read: a stereo one refused);
  //and a halfway buffer said to be read as far as it goes
  CHECK(m.call("sceAtracSetData", {plus, File, 0xffff'ffff}), 0);
  m.call("sceAtracGetRemainFrame", {plus, R});
  CHECK(word(m, R), ~0u);
  CHECK(m.call("sceAtracSetHalfwayBuffer", {plus, File, 0xffff'ffff, 0xffff'ffff}), 0);
  m.call("sceAtracGetRemainFrame", {plus, R});
  CHECK(word(m, R), ~0u);
  CHECK(m.call("sceAtracSetMOutData", {plus, File, 0xffff'ffff}), 0x8063'0019);
  CHECK(m.call("sceAtracSetMOutDataAndGetID", {File, 0xffff'ffff}), 0x8063'0019);
  CHECK(m.call("sceAtracSetDataAndGetID", {File + 0x10000, 0x8000'0000}), 0x8063'0006);  //(no RIFF header there)
  CHECK(m.call("sceAtracSetDataAndGetID", {0, 0xffff'ffff}), 0x8063'0006);  //(no memory there)
  CHECK(m.call("sceAtracSetDataAndGetID", {0x0400'0000, 0xffff'ffff}), 0x8063'0006);
  Machine none;
  none.kernel.audioDecoders = {};
  none.system.memory.copyIn(File, at3.data(), at3.size());
  CHECK(none.call("sceAtracSetDataAndGetID", {File, u32(at3.size())}), 0x8063'0006);
  CHECK(roundTrip(m), true);
}

//The whole file in its buffer (audio/atrac/decode, getsoundsample, getremainframe): the first sample worth hearing
//is the decoder's 2416th, the 368th of frame 1, frame 0 decoded unheard as the file is set; the first decode gives
//1680 samples (stereo, each the frame's number and its place), then 2048 a decode, the last 61, the end flag set;
//after it, 0x80630024; the frames left -1 throughout. A decode without a buffer still decodes.
static auto atracWholeFile() -> void {
  Machine m;
  auto at3 = file();
  m.system.memory.copyIn(File, at3.data(), at3.size());
  u32 id = m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())});
  CHECK(*m.log == std::vector<s32>({0}), true);
  m.call("sceAtracGetSoundSample", {id, R, R + 4, R + 8});
  CHECK(word(m, R), 247500);
  CHECK(word(m, R + 4), ~0u);
  m.call("sceAtracGetNextSample", {id, R});
  CHECK(word(m, R), 1680);
  m.call("sceAtracGetBitrate", {id, R});
  CHECK(word(m, R), 64);
  auto first = decode(m, id);
  CHECK(first.result == 0 && first.count == 1680 && !first.ended && first.remain == -1, true);
  CHECK(m.system.memory.read(2, Samples) == 1 && m.system.memory.read(2, Samples + 2) == 368, true);
  CHECK(m.system.memory.read(2, Samples + 1679 * 4 + 2), 2047);
  m.call("sceAtracGetNextDecodePosition", {id, R});
  CHECK(word(m, R), 1680);
  u32 decodes = 1, total = 1680;
  Decoded d;
  do {
    d = decode(m, id, decodes == 5 ? 0 : Samples);
    total += d.count;
    decodes++;
  } while(d.result == 0 && !d.ended);
  CHECK(d.result == 0 && d.count == 61 && d.ended && decodes == 122 && total == 247501, true);
  CHECK(m.system.memory.read(2, Samples) == 122 && m.system.memory.read(2, Samples + 60 * 4 + 2) == 60, true);
  auto after = decode(m, id);
  CHECK(after.result == 0x8063'0024 && after.count == 0 && after.ended, true);
  CHECK(m.call("sceAtracGetNextDecodePosition", {id, R}), 0x8063'0024);
  CHECK(m.call("sceAtracAddStreamData", {id, 0x100}), 0x8063'0009);
  CHECK(m.log->size(), 123);
  CHECK(roundTrip(m), true);
}

//A buffer that will hold the whole file, half filled (audio/atrac/getremainframe, addstreamdata, stream): the frames
//left are those whole in it past the next; more is added up to the file's end (more than that refused), when it
//becomes the whole file in the buffer (any more: 0x80630009); a decode past what's there finds no frame.
static auto atracHalfway() -> void {
  Machine m;
  auto at3 = file();
  m.system.memory.copyIn(File, at3.data(), 848);
  u32 id = m.call("sceAtracSetHalfwayBufferAndGetID", {File, 848, u32(at3.size())});
  m.call("sceAtracGetRemainFrame", {id, R});
  CHECK(s32(word(m, R)), 1);
  auto at = info(m, id);
  CHECK(at.at == 848 && at.bytes == at3.size() - 848 && at.from == 848, true);
  CHECK(decode(m, id).count, 1680);
  CHECK(decode(m, id).result, 0x8063'0023);
  CHECK(m.call("sceAtracAddStreamData", {id, u32(at3.size())}), 0x8063'0018);
  m.system.memory.copyIn(File + 848, at3.data() + 848, at3.size() - 848);
  CHECK(m.call("sceAtracAddStreamData", {id, u32(at3.size() - 848)}), 0);
  m.call("sceAtracGetRemainFrame", {id, R});
  CHECK(s32(word(m, R)), -1);
  CHECK(m.call("sceAtracAddStreamData", {id, 0}), 0x8063'0009);
  CHECK(decode(m, id).count, 2048);
  CHECK(roundTrip(m), true);
}

//Streamed through a 0x4500-byte buffer (audio/atrac/stream's sixth run, to the byte): the ring's first time round
//ends at 0x43f0, the last whole frame from where the frames lie; what was loaded past it (272 bytes) goes to the
//buffer's start, where the game writes next (200 bytes, up to the frame being decoded); each time after holds 46
//frames from the buffer's start (0x4390); the frames come out in the file's order across the wraps; once the file is
//in, nothing more is asked for and the frames left are -2.
static auto atracStreamed() -> void {
  Machine m;
  auto at3 = file();
  m.system.memory.copyIn(File, at3.data(), 0x4500);
  u32 id = m.call("sceAtracSetDataAndGetID", {File, 0x4500});
  CHECK(m.system.memory.read(4, File) == 46 && m.system.memory.read(1, File + 271) == 0x5a, true);
  m.call("sceAtracGetRemainFrame", {id, R});
  CHECK(s32(word(m, R)), 45);
  auto at = info(m, id);
  CHECK(at.at == 272 && at.bytes == 200 && at.from == 17664, true);
  u32 frame = 1, loaded = 0x4500;
  bool order = true;
  Decoded d;
  do {
    d = decode(m, id);
    order &= d.result == 0 && m.system.memory.read(2, Samples) == frame++;
    if(d.remain >= 0 && d.remain < 10) {
      at = info(m, id);
      if(at.bytes) {
        CHECK(at.from, loaded);
        m.system.memory.copyIn(File + at.at, at3.data() + at.from, at.bytes);
        CHECK(m.call("sceAtracAddStreamData", {id, at.bytes}), 0);
        loaded += at.bytes;
        //the first refill, as recorded: 13736 bytes at 272, then the ring full (0 at 14008, from 31400)
        if(loaded == 17664 + 13736) {
          auto full = info(m, id);
          CHECK(full.at == 14008 && full.bytes == 0 && full.from == 31400, true);
        }
      }
    }
  } while(d.result == 0 && !d.ended);
  CHECK(order && d.ended && frame == 123 && loaded == at3.size() && d.remain == -2, true);
  at = info(m, id);
  CHECK(at.at == 0 && at.bytes == 0 && at.from == 0, true);
  CHECK(m.call("sceAtracAddStreamData", {id, 0}), 0);  //(audio/atrac/replay adds nothing to it at every frame)
  CHECK(roundTrip(m), true);
}

//A loop at the file's end, streamed through a 0x4000-byte buffer (stream's fourth run): the smpl chunk's points less
//the offset (20736 to 247500), the loop's start's priming frame after the file's end (the game told to read from
//3924), the frames left leaving it out; the loop's last frame played to the file's end (61 samples), then its
//priming frame decoded unheard and decoding at 20736 (1424 samples); after the loops, the frames left -3, and the
//end.
static auto atracLooped() -> void {
  Machine m;
  auto at3 = file(2048 + 2048 * 10 + 256, 249548);
  m.system.memory.copyIn(File, at3.data(), 0x4000);
  u32 id = m.call("sceAtracSetDataAndGetID", {File, 0x4000});
  m.call("sceAtracGetSoundSample", {id, R, R + 4, R + 8});
  CHECK(word(m, R + 4) == 20736 && word(m, R + 8) == 247500, true);
  m.call("sceAtracGetLoopStatus", {id, R, R + 4});
  CHECK(word(m, R) == 0 && word(m, R + 4) == 1, true);
  CHECK(m.call("sceAtracSetLoopNum", {id, 1}), 0);
  auto at = info(m, id);
  CHECK(at.at == 52 && at.bytes == 488 && at.from == 16384, true);
  u32 total = 0;
  bool jumped = false, restartOffered = false;
  Decoded d;
  do {
    d = decode(m, id);
    total += d.count;
    if(d.count == 61 && !jumped) {
      jumped = true;
      m.log->clear();
      auto loop = decode(m, id);
      CHECK(loop.count == 1424 && m.system.memory.read(2, Samples) == 11, true);
      CHECK(*m.log == std::vector<s32>({10, 11}), true);
      total += loop.count;
    }
    at = info(m, id);
    if(at.bytes && d.remain >= 0 && d.remain < 32) {
      if(at.from == 3924) restartOffered = true;
      m.system.memory.copyIn(File + at.at, at3.data() + at.from, at.bytes);
      CHECK(m.call("sceAtracAddStreamData", {id, at.bytes}), 0);
    }
  } while(d.result == 0 && !d.ended);
  CHECK(jumped && restartOffered && d.ended && total == 247501 + 247500 - 20736 + 1, true);
  m.call("sceAtracGetLoopStatus", {id, R, R + 4});
  CHECK(word(m, R) == 0 && word(m, R + 4) == 0, true);
  CHECK(roundTrip(m), true);
}

//A loop before the file's end (audio/atrac/second): a second buffer holds the frames after the loop's last frame
//(frame 98 on for a loop ending at 200000: 9400 bytes from 37012); a loop ending at the file's last sample needs
//none; one too small for three frames (or for what it must hold, should that be less) is refused, then a file that
//needs none. Decoding plays the loop's last frame whole before jumping back, and after the loops goes on into the
//second buffer, the frames left -2.
static auto atracSecondBuffer() -> void {
  Machine m;
  for(auto [end, needed] : {std::pair{200000, true}, std::pair{249548, false}, std::pair{249540, true}}) {
    auto at3 = file(2048, end);
    m.system.memory.copyIn(File, at3.data(), at3.size() / 2);
    u32 id = m.call("sceAtracSetDataAndGetID", {File, u32(at3.size() / 2)});
    CHECK(m.call("sceAtracIsSecondBufferNeeded", {id}), needed);
    u32 result = m.call("sceAtracGetSecondBufferInfo", {id, R, R + 4});
    CHECK(result, needed ? 0 : 0x8063'0022);
    if(end == 200000) CHECK(word(m, R) == 37012 && word(m, R + 4) == 9400, true);
    m.call("sceAtracReleaseAtracID", {id});
  }
  auto at3 = file(2048, 200000);
  m.system.memory.copyIn(File, at3.data(), 0x4000);
  u32 id = m.call("sceAtracSetDataAndGetID", {File, 0x4000});
  m.call("sceAtracGetSecondBufferInfo", {id, R, R + 4});
  u32 from = word(m, R), bytes = word(m, R + 4);
  CHECK(m.call("sceAtracSetSecondBuffer", {id, Second, 376 * 3 - 1}), 0x8063'0011);
  CHECK(m.call("sceAtracGetBufferInfoForResetting", {id, 0, R}), 0x8063'0012);  //no second buffer yet
  m.system.memory.copyIn(Second, at3.data() + from, bytes);
  CHECK(m.call("sceAtracSetSecondBuffer", {id, Second, bytes}), 0);
  CHECK(m.call("sceAtracGetBufferInfoForResetting", {id, 0, R}), 0);
  CHECK(m.call("sceAtracSetLoopNum", {id, 1}), 0);
  u32 total = 0;
  Decoded d;
  do {
    d = decode(m, id);
    total += d.count;
    auto at = info(m, id);
    if(at.bytes && d.remain >= 0 && d.remain < 32) {
      m.system.memory.copyIn(File + at.at, at3.data() + at.from, at.bytes);
      CHECK(m.call("sceAtracAddStreamData", {id, at.bytes}), 0);
    }
  } while(d.result == 0 && !d.ended);
  //the loop: 2048 to 200000 in the smpl chunk, the file's 0 to 197952, played to its frame's end (198287), then from
  //0 again to the file's end through the second buffer
  CHECK(d.ended && d.remain == -2 && total == 198288 + 247501, true);
  CHECK(roundTrip(m), true);
}

//Starting again elsewhere (audio/atrac/resetting, resetpos): what a stream must load for a sample, its priming frames
//and its own from the first's place in the file, as much as fits in whole frames at the buffer's start, and how a
//sample within a frame's first 368 needs two priming frames; the reset decodes them at once, and decoding goes on
//from the sample. A sample past the file's last is refused; a stream's reset wants at least the frames asked for.
static auto atracReset() -> void {
  Machine m;
  auto at3 = file();
  m.system.memory.copyIn(File, at3.data(), at3.size() / 2);
  u32 id = m.call("sceAtracSetHalfwayBufferAndGetID", {File, u32(at3.size() / 2), u32(at3.size() / 2)});
  struct Row { s32 sample; u32 writable, least, from; };
  for(auto row : {Row{0, 0x5998, 0x2f0, 0x60}, Row{1679, 0x5998, 0x2f0, 0x60}, Row{1680, 0x5998, 0x468, 0x60},
                  Row{0x1000, 0x5998, 0x2f0, 0x350}, Row{0x47ff, 0x5998, 0x468, 0xc20},
                  Row{0x10000, 0x5998, 0x2f0, 0x2f60}, Row{247500, 0x468, 0x468, 0xb0a0}}) {
    CHECK(m.call("sceAtracGetBufferInfoForResetting", {id, u32(row.sample), R}), 0);
    check(__LINE__, "reset info", word(m, R) == File && word(m, R + 4) == row.writable && word(m, R + 8) == row.least
          && word(m, R + 12) == row.from, true);
  }
  CHECK(m.call("sceAtracGetBufferInfoForResetting", {id, 247501, R}), 0x8063'0015);
  CHECK(m.call("sceAtracResetPlayPosition", {id, 0x10000, 0x100, 0}), 0x8063'0016);
  m.system.memory.copyIn(File, at3.data() + 0x2f60, 0x2f0);
  m.log->clear();
  CHECK(m.call("sceAtracResetPlayPosition", {id, 0x10000, 0x2f0, 0}), 0);
  CHECK(*m.log == std::vector<s32>({-1, 32}), true);
  m.call("sceAtracGetNextDecodePosition", {id, R});
  CHECK(word(m, R), 0x10000);
  auto d = decode(m, id);
  CHECK(d.count == 1680 && m.system.memory.read(2, Samples) == 33 && d.remain == 0, true);
  auto at = info(m, id);
  CHECK(at.at == 0x2f0 && at.from == 0x2f60 + 0x2f0, true);
  //the whole file in its buffer takes no bytes in a reset
  Machine whole;
  whole.system.memory.copyIn(File, at3.data(), at3.size());
  id = whole.call("sceAtracSetDataAndGetID", {File, u32(at3.size())});
  CHECK(whole.call("sceAtracResetPlayPosition", {id, 0, 0x100, 0}), 0x8063'0016);
  CHECK(whole.call("sceAtracResetPlayPosition", {id, 0, 0, 0x100}), 0x8063'0017);
  CHECK(whole.call("sceAtracResetPlayPosition", {id, 196608, 0, 0}), 0);
  CHECK(decode(whole, id).count, 1680);
  CHECK(roundTrip(m) && roundTrip(whole), true);
}

//A frame that won't decode (stream's corrupted run): 0x80630002, nothing used up, the internal error 0x20b, again
//at the next try; the frames before decode as ever.
static auto atracBadFrame() -> void {
  Machine m;
  auto at3 = file();
  at3[0x60 + 3 * 376 + 4] = 0xee;
  m.system.memory.copyIn(File, at3.data(), at3.size());
  u32 id = m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())});
  CHECK(decode(m, id).count, 1680);
  CHECK(decode(m, id).count, 2048);
  for(u32 tries = 0; tries < 2; tries++) {
    auto d = decode(m, id);
    CHECK(d.result == 0x8063'0002 && d.count == 0, true);
    m.call("sceAtracGetInternalErrorInfo", {id, R});
    CHECK(word(m, R), 0x20b);
  }
  CHECK(roundTrip(m), true);
}

//States: a stream saved part way round its ring (and part way through priming after a loop's jump) loads into a
//fresh machine, which decodes the same samples, its decoder made afresh and primed with the frame decoded last.
static auto atracStates() -> void {
  Machine m;
  auto at3 = file(2048 + 2048 * 10 + 256, 249548);
  m.system.memory.copyIn(File, at3.data(), 0x4000);
  u32 id = m.call("sceAtracSetDataAndGetID", {File, 0x4000});
  m.call("sceAtracSetLoopNum", {id, 2});
  auto feed = [&](KernelMachine& k) {
    k.call("sceAtracGetStreamDataInfo", {id, R, R + 4, R + 8});
    u32 at = word(k, R), bytes = word(k, R + 4), from = word(k, R + 8);
    if(bytes) k.system.memory.copyIn(at, at3.data() + from, bytes), k.call("sceAtracAddStreamData", {id, bytes});
  };
  for(u32 n = 0; n < 150; n++) decode(m, id), feed(m);
  auto state = saveState(m);
  Machine fresh;
  CHECK(loadState(fresh, state), true);
  CHECK(saveState(fresh) == state, true);
  fresh.log->clear();
  bool same = true;
  for(u32 n = 0; n < 200; n++) {
    auto a = decode(m, id), b = decode(fresh, id);
    same &= a.result == b.result && a.count == b.count && a.remain == b.remain;
    same &= m.system.memory.read(4, Samples) == fresh.system.memory.read(4, Samples);
    feed(m), feed(fresh);
  }
  CHECK(same, true);
  CHECK(fresh.log->size() > 2 && (*fresh.log)[0] + 1 == (*fresh.log)[1], true);  //primed with the frame before
  CHECK(saveState(m) == saveState(fresh), true);
  //a reset straight after a state is loaded (a file all in its buffer): its decoder made afresh as it primes
  Machine whole;
  auto all = file();
  whole.system.memory.copyIn(File, all.data(), all.size());
  u32 wholeID = whole.call("sceAtracSetDataAndGetID", {File, u32(all.size())});
  decode(whole, wholeID);
  Machine loaded;
  CHECK(loadState(loaded, saveState(whole)), true);
  CHECK(loaded.call("sceAtracResetPlayPosition", {wholeID, 4096, 0, 0}), 0);
  CHECK(whole.call("sceAtracResetPlayPosition", {wholeID, 4096, 0, 0}), 0);
  auto a = decode(whole, wholeID), b = decode(loaded, wholeID);
  CHECK(a.result == 0 && a.result == b.result && a.count == b.count, true);
  CHECK(word(whole, Samples) == word(loaded, Samples), true);
}

//Headers whose sizes don't fit: a 256-byte file whose first chunk's length (0xfffffff8) would take a 32-bit walk
//back to where it was, for ever, is refused as unreadable (no chunk after it); data whose end is past 4 GiB, or
//whose samples a 32-bit count can't hold, is refused as the samples check refuses (0x80630008). An ATRAC3 file with
//100 bytes of fmt extras keeps the first 64 (what a state holds), and saves.
static auto atracHeaderSizes() -> void {
  Machine m;
  std::vector<u8> junk(256, 0);
  auto put32 = [](std::vector<u8>& bytes, u32 at, u32 value) {
    for(u32 n = 0; n < 4; n++) bytes[at + n] = value >> n * 8;
  };
  memcpy(junk.data(), "RIFF", 4), put32(junk, 4, 248), memcpy(junk.data() + 8, "WAVEJUNK", 8);
  put32(junk, 16, 0xffff'fff8);
  m.system.memory.copyIn(File, junk.data(), junk.size());
  CHECK(m.call("sceAtracSetDataAndGetID", {File, 256}), 0x8063'0006);
  auto at3 = file();
  put32(at3, 92, 0xffff'ffc0);  //the data chunk's length: from 0x60, it ends at 0x1'0000'0020
  m.system.memory.copyIn(File, at3.data(), at3.size());
  CHECK(m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())}), 0x8063'0008);
  auto unfacted = file();
  memcpy(unfacted.data() + 72, "junk", 4);  //the fact chunk passed over: the samples counted from the data's
  put32(unfacted, 92, 0xffff'ff00 - 0x60);
  m.system.memory.copyIn(File, unfacted.data(), unfacted.size());
  CHECK(m.call("sceAtracSetDataAndGetID", {File, u32(unfacted.size())}), 0x8063'0008);
  std::vector<u8> classic(12 + 8 + 118 + 8 + 10 * 384, 0);
  memcpy(classic.data(), "RIFF", 4), put32(classic, 4, classic.size() - 8), memcpy(classic.data() + 8, "WAVEfmt ", 8);
  put32(classic, 16, 118);
  for(auto [at, value] : {std::pair{20u, 0x0002'0270u}, {24, 44100}, {28, 16537}, {32, 384}, {36, 100}}) {
    put32(classic, at, value);
  }
  for(u32 n = 0; n < 100; n++) classic[38 + n] = n;
  memcpy(classic.data() + 138, "data", 4), put32(classic, 142, 10 * 384);
  m.system.memory.copyIn(File, classic.data(), classic.size());
  u32 id = m.call("sceAtracSetDataAndGetID", {File, u32(classic.size())});
  CHECK(s32(id) >= 0, true);
  if(s32(id) >= 0) CHECK(m.kernel.atracs[id].extra.size() == 64 && m.kernel.atracs[id].extra[63] == 63, true);
  CHECK(roundTrip(m), true);
}

//With FFmpeg's decoders and pspautotests' sample.at3 (PSP_AUTOTESTS: its checkout), decoded as audio/atrac/decode
//recorded: 1680 samples, then 2048 a decode, the last 61, 122 decodes, sound in them (not silence, not clipped).
static auto atracFFmpeg() -> void {
  const char* tests = std::getenv("PSP_AUTOTESTS");
  KernelMachine m;
  if(!tests || !*tests || !m.kernel.audioDecoders) return;
  std::ifstream stream(std::string(tests) + "/tests/audio/atrac/sample.at3", std::ios::binary);
  std::vector<u8> at3((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  if(at3.empty()) return;
  m.system.memory.copyIn(File, at3.data(), at3.size());
  u32 id = m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())});
  CHECK(id, 0);
  u32 decodes = 0, total = 0, loud = 0, clipped = 0;
  Decoded d;
  do {
    d = decode(m, id);
    for(u32 n = 0; n < d.count * 2; n++) {
      s16 sample = m.system.memory.read(2, Samples + n * 2);
      loud += sample > 1000 || sample < -1000;
      clipped += sample == 32767 || sample == -32768;
    }
    total += d.count, decodes++;
  } while(d.result == 0 && !d.ended);
  CHECK(d.ended && decodes == 122 && total == 247501, true);
  CHECK(loud > total / 10 && clipped < total / 1000, true);
}

//The low level (sceAtracLowLevelInitDecoder, sceAtracLowLevelDecode), as Corpse Party's CRI library uses it: on an
//ID from sceAtracGetAtracID, the frames' channels, the output's and the frame size; each frame decoded into the
//output's channels, interleaved (two from two, the left from two, one doubled), its bytes used and samples' bytes
//written told; ATRAC3's two-channel 0xc0-byte frames joint stereo. Refused: an ID not handed out (BAD_ID), parameters
//out of range (BAD_PARAMETERS), decoding before them (NO_DATA, as on an ID with no file), a frame that won't decode
//(FAILED, nothing written). Its high level sees no file; setting one makes the ID the file's.
static auto atracLowLevel() -> void {
  constexpr u32 AtracErrorFailed = 0x8063'0002, AtracErrorBadID = 0x8063'0005, AtracErrorBadParameters = 0x8063'0008;
  constexpr u32 AtracErrorNoData = 0x8063'0010;
  Machine m;
  constexpr u32 Parameters = R + 0x100, Used = R + 0x110, Written = R + 0x114, Frame = File + 0x1000;
  auto frame = [&](u32 number, u32 bytes = 376) {
    m.system.memory.fill(Frame, 0x5a, bytes);
    m.system.memory.write(2, Frame, number);
  };
  auto initialize = [&](u32 id, u32 channels, u32 output, u32 bytes) {
    m.system.memory.write(4, Parameters, channels);
    m.system.memory.write(4, Parameters + 4, output);
    m.system.memory.write(4, Parameters + 8, bytes);
    return m.call("sceAtracLowLevelInitDecoder", {id, Parameters});
  };
  auto decode = [&] {
    m.system.memory.fill(Samples, 0xcc, 0x4004);
    m.system.memory.write(4, Used, 0x1337), m.system.memory.write(4, Written, 0x1337);
    return m.call("sceAtracLowLevelDecode", {0, Frame, Used, Samples, Written});
  };
  auto sample = [&](u32 n) { return m.system.memory.read(2, Samples + n * 2); };
  CHECK(initialize(0, 2, 2, 376), AtracErrorBadID);
  CHECK(m.call("sceAtracGetAtracID", {0x1000}), 0);
  CHECK(decode(), AtracErrorNoData);
  for(auto [channels, output, bytes] : {std::tuple{0u, 2u, 376u}, {3u, 2u, 376u}, {2u, 0u, 376u}, {2u, 3u, 376u},
                                        {2u, 2u, 0u}, {2u, 2u, 0x2001u}}) {
    CHECK(initialize(0, channels, output, bytes), AtracErrorBadParameters);
  }
  CHECK(initialize(0, 2, 2, 376), 0);
  frame(7);
  CHECK(decode(), 0);
  CHECK(m.system.memory.read(4, Used) == 376 && m.system.memory.read(4, Written) == 2048 * 4, true);
  CHECK(sample(0) == 7 && sample(1) == 0 && sample(4094) == 7 && sample(4095) == 2047, true);
  CHECK(m.system.memory.read(2, Samples + 8192), 0xcccc);
  //no file, its context saying 8: IS_LOW_LEVEL from the calls recorded with a state set to 8, NO_DATA as before from
  //the rest
  CHECK(m.call("sceAtracGetRemainFrame", {0, R}), 0x8063'0031);
  CHECK(m.call("sceAtracDecodeData", {0, Samples, R, R + 4, R + 8}), AtracErrorNoData);
  CHECK(m.call("sceAtracGetInternalErrorInfo", {0, R}), AtracErrorNoData);
  //mono output from two channels: the left; one channel doubled
  CHECK(initialize(0, 2, 1, 376), 0);
  frame(9);
  CHECK(decode(), 0);
  CHECK(m.system.memory.read(4, Written) == 2048 * 2 && sample(0) == 9 && sample(2047) == 9, true);
  CHECK(m.system.memory.read(2, Samples + 4096), 0xcccc);
  CHECK(initialize(0, 1, 2, 376), 0);
  frame(10);
  CHECK(decode(), 0);
  CHECK(sample(0) == 10 && sample(1) == 10 && sample(4095) == 10, true);
  //a frame that won't decode
  m.system.memory.write(1, Frame + 4, 0xee);
  CHECK(decode(), AtracErrorFailed);
  CHECK(sample(0) == 0xcccc && m.system.memory.read(4, Used) == 0x1337, true);
  //ATRAC3: joint stereo for two channels of 0xc0 bytes
  CHECK(m.call("sceAtracGetAtracID", {0x1001}), 2);
  std::vector<u8> extra;
  m.kernel.audioDecoders = [&extra](const AudioDecoder::Format& format) -> std::unique_ptr<AudioDecoder> {
    extra = format.extra;
    auto decoder = std::make_unique<StandIn>();
    decoder->format = format;
    decoder->log = std::make_shared<std::vector<s32>>();
    return decoder;
  };
  for(auto [bytes, joint] : {std::pair{0xc0u, 1u}, {0x130u, 0u}}) {
    CHECK(initialize(2, 2, 2, bytes), 0);
    m.system.memory.fill(Frame, 0x5a, bytes);
    CHECK(m.call("sceAtracLowLevelDecode", {2, Frame, Used, Samples, Written}), 0);
    CHECK(m.system.memory.read(4, Written), 1024 * 4);
    CHECK(extra.size() == 14 && extra[6] == joint && extra[8] == joint, true);
  }
  //a file set on the ID makes it the file's again
  auto bytes = file();
  m.system.memory.copyIn(File, bytes.data(), bytes.size());
  m.kernel.audioDecoders = Machine().kernel.audioDecoders;
  CHECK(m.call("sceAtracSetData", {0, File, u32(bytes.size())}), 0);
  CHECK(m.kernel.atracs[0].lowLevel, false);
  CHECK(m.call("sceAtracLowLevelDecode", {0, Frame, Used, Samples, Written}), AtracErrorNoData);
  CHECK(m.notes.size(), 0);
}

//The low level in a state: the ID's parameters kept, and its decoder, made afresh, primed with the frame decoded last.
static auto atracLowLevelState() -> void {
  Machine m;
  constexpr u32 Parameters = R + 0x100, Used = R + 0x110, Written = R + 0x114, Frame = File + 0x1000;
  CHECK(m.call("sceAtracGetAtracID", {0x1000}), 0);
  m.system.memory.write(4, Parameters, 2), m.system.memory.write(4, Parameters + 4, 1);
  m.system.memory.write(4, Parameters + 8, 376);
  CHECK(m.call("sceAtracLowLevelInitDecoder", {0, Parameters}), 0);
  m.system.memory.fill(Frame, 0x5a, 376);
  m.system.memory.write(2, Frame, 7);
  CHECK(m.call("sceAtracLowLevelDecode", {0, Frame, Used, Samples, Written}), 0);
  auto state = saveState(m);
  Machine n;
  CHECK(loadState(n, state), true);
  CHECK(saveState(n) == state, true);
  n.system.memory.write(2, Frame, 8);
  CHECK(n.call("sceAtracLowLevelDecode", {0, Frame, Used, Samples, Written}), 0);
  CHECK(*n.log == std::vector<s32>({7, 8}), true);
  CHECK(n.system.memory.read(2, Samples) == 8 && n.system.memory.read(4, Written) == 2048 * 2, true);
}

//The contexts (pspautotests' atrac.h): every ID's 256 bytes in kernel memory from power on (no file: state 1, the ID
//at the end), written whenever the library changes the ID (set, decoded, released), without
//_sceAtracGetContextAddress, which only says where (0 for an ID not handed out) and writes nothing: what the program
//wrote stays until the library next changes the ID (a set refused before it changes, a decode after the end, don't).
//A state the program set to 8 or 16 refuses each of the seven calls recorded with one, 0x80630031 and 0x80630040
//(getremainframe, getsoundsample, resetpos, resetting and second), and set back to 2 the ID is taken again; every
//other call, none recorded so, answers as before. A low-level ID's says 8 (the seven IS_LOW_LEVEL, the rest NO_DATA as
//before). A state keeps them; one from before they had memory of their own (0 for it) has them written as it loads.
static auto atracContexts() -> void {
  Machine m;
  auto at3 = file();
  m.system.memory.copyIn(File, at3.data(), at3.size());
  auto context = [](u32 id) { return Kernel::AtracContextMemory + id * 256; };
  auto field = [&](u32 id, u32 offset) { return word(m, context(id) + 128 + offset); };
  auto state = [&](u32 id) { return m.system.memory.read(1, context(id) + 128 + 21); };
  for(u32 id : range(6)) CHECK(state(id) == 1 && field(id, 124) == id && field(id, 0) == 0, true);
  CHECK(m.call("_sceAtracGetContextAddress", {0}), 0);
  CHECK(m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())}), 0);
  CHECK(state(0) == 2 && field(0, 0) == 2416 && field(0, 36) == at3.size() && field(0, 56) == File, true);
  CHECK(m.system.memory.read(2, context(0) + 128 + 26), 0x1000);
  CHECK(decode(m, 0).count, 1680);
  CHECK(field(0, 0), 4096);  //current without asking
  CHECK(m.call("_sceAtracGetContextAddress", {0}), context(0));
  //the program's writes stay through _sceAtracGetContextAddress
  m.system.memory.write(4, context(0) + 128 + 40, 30);
  m.system.memory.write(4, context(0) + 128 + 60, ~0u);
  CHECK(m.call("_sceAtracGetContextAddress", {0}), context(0));
  CHECK(field(0, 40) == 30 && field(0, 60) == ~0u, true);
  CHECK(m.call("sceAtracSetData", {0, File + 0x10000, 0x1000}), 0x8063'0006);  //(refused before the ID changes)
  CHECK(field(0, 40) == 30 && field(0, 60) == ~0u, true);
  //the state it sets
  auto remain = [&] { return m.call("sceAtracGetRemainFrame", {0, R}); };
  for(auto [forced, error] : {std::pair{8u, 0x8063'0031u}, {16u, 0x8063'0040u}}) {
    m.system.memory.write(1, context(0) + 128 + 21, forced);
    CHECK(remain(), error);
    CHECK(m.call("sceAtracGetSoundSample", {0, R, R + 4, R + 8}), error);
    CHECK(m.call("sceAtracIsSecondBufferNeeded", {0}), error);
    CHECK(m.call("sceAtracGetBufferInfoForResetting", {0, 0, R + 0x40}), error);
    CHECK(m.call("sceAtracGetSecondBufferInfo", {0, R, R + 4}), error);
    CHECK(m.call("sceAtracResetPlayPosition", {0, 0, 0, 0}), error);
    CHECK(m.call("sceAtracSetSecondBuffer", {0, Second, 0x1000}), error);
    CHECK(m.call("sceAtracGetInternalErrorInfo", {0, R}), 0);  //(not among them: as before)
    CHECK(m.call("sceAtracGetBitrate", {0, R}), 0);
  }
  m.system.memory.write(1, context(0) + 128 + 21, 2);
  CHECK(remain() == 0 && s32(word(m, R)) == -1, true);
  CHECK(decode(m, 0).count, 2048);
  CHECK(field(0, 0) == 6144 && field(0, 40) == 0 && field(0, 60) == 0, true);  //the library's again, as it changed
  while(!decode(m, 0).ended) {}
  m.system.memory.write(4, context(0) + 128 + 40, 77);
  CHECK(decode(m, 0).result, 0x8063'0024);  //(after the end: nothing changes, nothing's written)
  CHECK(field(0, 40), 77);
  CHECK(roundTrip(m), true);
  CHECK(m.call("sceAtracReleaseAtracID", {0}), 0);
  CHECK(state(0) == 1 && field(0, 0) == 0 && field(0, 124) == 0, true);
  CHECK(m.call("_sceAtracGetContextAddress", {0}), 0);
  //a low-level ID's says 8
  constexpr u32 Parameters = R + 0x100;
  CHECK(m.call("sceAtracGetAtracID", {0x1000}), 0);
  m.system.memory.write(4, Parameters, 2), m.system.memory.write(4, Parameters + 4, 2);
  m.system.memory.write(4, Parameters + 8, 376);
  CHECK(m.call("sceAtracLowLevelInitDecoder", {0, Parameters}), 0);
  CHECK(state(0), 8);
  CHECK(remain(), 0x8063'0031);
  CHECK(m.call("sceAtracGetInternalErrorInfo", {0, R}), 0x8063'0010);
  //a state from before: no memory of their own (0), written as it loads
  CHECK(m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())}), 1);
  m.system.memory.fill(Kernel::AtracContextMemory, 0, 6 * 256);
  m.kernel.atracContexts = 0;
  auto old = saveState(m);
  Machine n;
  CHECK(loadState(n, old), true);
  CHECK(n.kernel.atracContexts, Kernel::AtracContextMemory);
  CHECK(n.system.memory.read(1, context(0) + 128 + 21) == 8 && n.system.memory.read(1, context(1) + 128 + 21) == 2,
        true);
  CHECK(word(n, context(1) + 128) == 2416 && word(n, context(5) + 252) == 5, true);
  CHECK(m.notes.size() == 0 && n.notes.size() == 0, true);
}

//A layout-26 state whose program had asked for the contexts, so that they were put in a block of the user partition
//then (the library's way before they had memory of their own): loaded, they stay in that block as they were, a state
//the program set there honoured (16, as Angus Hates Aliens' state at its title has it), kernel memory left alone; each
//is written there again as the library next changes its ID.
static auto atracContextsOlder() -> void {
  Machine m;
  auto at3 = file();
  m.system.memory.copyIn(File, at3.data(), at3.size());
  CHECK(m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())}), 0);
  auto block = m.kernel.allocate(6 * 256, 1, 0, "atrac contexts");
  CHECK(block != nullptr, true);
  if(!block) return;
  u32 contexts = block->address;
  m.system.memory.fill(Kernel::AtracContextMemory, 0, 6 * 256);  //(nothing there before layout 31)
  m.kernel.atracContexts = contexts;
  for(u32 id : range(6)) m.kernel.atracContext(id);
  m.system.memory.write(1, contexts + 128 + 21, 16);
  //this state as layout 26 has it: without sceVaudio's 15 bytes, where saving it with sceVaudio held shows them,
  //nor the program's count of imported variables (4 bytes before them: layout 28's)
  auto state = saveState(m);
  m.kernel.audio.vaudio.reserved = true;
  auto held = saveState(m);
  m.kernel.audio.vaudio.reserved = false;
  m.kernel.module.variables.push_back({"Lib", 1, 2, 0});
  auto imported = saveState(m);
  m.kernel.module.variables.clear();
  u32 vaudio = 0;
  while(vaudio < state.size() && vaudio < held.size() && state[vaudio] == held[vaudio]) vaudio++;
  u32 variables = 0;
  while(variables < state.size() && variables < imported.size() && state[variables] == imported[variables]) variables++;
  CHECK(variables + 4 <= vaudio && vaudio + 15 < state.size(), true);
  if(variables + 4 > vaudio || vaudio + 15 >= state.size()) return;
  state.erase(state.begin() + vaudio, state.begin() + vaudio + 15);
  state.erase(state.begin() + variables, state.begin() + variables + 4);
  Machine n;
  u32 current = n.kernel.stateLayout;
  n.kernel.stateLayout = 26;
  CHECK(loadState(n, state), true);
  n.kernel.stateLayout = current;
  CHECK(n.kernel.atracContexts, contexts);
  CHECK(n.call("_sceAtracGetContextAddress", {0}), contexts);
  CHECK(n.system.memory.read(1, contexts + 128 + 21), 16);
  CHECK(n.call("sceAtracGetRemainFrame", {0, R}), 0x8063'0040);
  std::vector<u8> kernelMemory(6 * 256, 0xcc);
  n.system.memory.copyOut(kernelMemory.data(), Kernel::AtracContextMemory, kernelMemory.size());
  CHECK(std::all_of(kernelMemory.begin(), kernelMemory.end(), [](u8 byte) { return byte == 0; }), true);
  n.system.memory.write(1, contexts + 128 + 21, 2);
  CHECK(n.call("sceAtracGetRemainFrame", {0, R}), 0);
  CHECK(decode(n, 0).count, 1680);
  CHECK(word(n, contexts + 128), 4096);  //(written there again, as the library changed the ID)
  CHECK(roundTrip(n), true);
  CHECK(m.notes.size() == 0 && n.notes.size() == 0, true);
}

//Angus Hates Aliens' music: its sound code (a wrapper over sceSas's ATRAC3 voices, which pspautotests' audio/atrac/sas
//copies) sets a file, takes its ID's context and sets its state to 16, gives the context to a voice with
//__sceSasSetVoiceATRAC3, which keeps it in the voice's first word in the SasCore, then moves the context's buffer and
//curFileOff on, puts the voice in loopNum and -1 in secondBuffer; it streams the file through the context it reads
//back from the SasCore; it stops the voice only while the context still says 16 (it asked again for good when the
//state it set was written over), setting 1 and releasing the ID. The calls recorded with such a state are refused
//meanwhile. The voice's word is written only into the SasCore __sceSasInit set up, the call naming it: not before
//it's set up, nor into another; it's 0 again as the voice is unset and as the core is set up afresh (each voice's,
//as a fresh SasCore has them).
static auto atracForSas() -> void {
  Machine m;
  auto at3 = file();
  m.system.memory.copyIn(File, at3.data(), at3.size());
  constexpr u32 Core = 0x0890'4000, Other = Core + 0x1000, Words = 20 + 32 * 56;
  auto slot = [](u32 core, u32 voice) { return core + 20 + voice * 56; };
  m.system.memory.fill(Core, 0xcc, Words);
  m.system.memory.fill(Other, 0xcc, Words);
  CHECK(m.call("__sceSasSetVoiceATRAC3", {Core, 3, 0x0880'1234}), 0);
  CHECK(word(m, slot(Core, 3)), 0xcccc'cccc);  //(not set up yet)
  u32 id = m.call("sceAtracSetDataAndGetID", {File, u32(at3.size())});
  u32 context = m.call("_sceAtracGetContextAddress", {id});
  CHECK(context, Kernel::AtracContextMemory + id * 256);
  CHECK(m.call("__sceSasInit", {Core, 1024, 32, 0, 44'100}), 0);
  CHECK(word(m, slot(Core, 0)) == 0 && word(m, slot(Core, 3)) == 0 && word(m, slot(Core, 31)) == 0, true);
  CHECK(word(m, Core) == 0xcccc'cccc && word(m, slot(Core, 0) + 4) == 0xcccc'cccc, true);  //(the rest left alone)
  CHECK(m.call("__sceSasSetVoiceATRAC3", {Other, 31, context}), 0);
  CHECK(word(m, slot(Other, 31)), 0xcccc'cccc);  //(another core)
  m.system.memory.write(1, context + 0x95, 16);
  CHECK(m.call("__sceSasSetVoiceATRAC3", {Core, 31, context}), 0);
  CHECK(word(m, slot(Core, 31)), context);
  CHECK(word(m, slot(Core, 30)), 0);
  auto put = [&](u32 offset, u32 value) { m.system.memory.write(4, context + offset, value); };
  put(0xb8, word(m, context + 0xb8) + word(m, context + 0xb0));
  put(0xa0, word(m, context + 0xa0) + word(m, context + 0xac));
  put(0xa8, 31);
  put(0xbc, ~0u);
  u32 voice = word(m, Core + 20 + 31 * 56);  //(as the game streams: through the SasCore)
  CHECK(word(m, voice + 0xa4) == at3.size() && word(m, voice + 0xbc) == ~0u, true);
  CHECK(m.call("sceAtracGetRemainFrame", {id, R}), 0x8063'0040);
  CHECK(m.call("_sceAtracGetContextAddress", {id}), context);
  CHECK(m.system.memory.read(1, context + 0x95) == 16 && word(m, context + 0xa8) == 31, true);
  CHECK(roundTrip(m), true);
  CHECK(m.call("__sceSasUnsetATRAC3", {Core, word(m, context + 0xa8)}), 0);
  CHECK(word(m, slot(Core, 31)), 0);
  m.system.memory.write(1, context + 0x95, 1);
  CHECK(m.call("sceAtracReleaseAtracID", {id}), 0);
  CHECK(m.call("_sceAtracGetContextAddress", {id}), 0);
  //set up afresh: every voice's word 0 again
  CHECK(m.call("__sceSasSetVoiceATRAC3", {Core, 5, context}), 0);
  CHECK(word(m, slot(Core, 5)), context);
  CHECK(m.call("__sceSasInit", {Core, 1024, 32, 0, 44'100}), 0);
  CHECK(word(m, slot(Core, 5)), 0);
  CHECK(m.notes.size(), 0);
}

auto atracTests() -> Tests {
  return {{"atrac ids", atracIDs}, {"atrac setting", atracSetting}, {"atrac whole file", atracWholeFile},
          {"atrac halfway", atracHalfway}, {"atrac streamed", atracStreamed}, {"atrac looped", atracLooped},
          {"atrac second buffer", atracSecondBuffer}, {"atrac reset", atracReset}, {"atrac bad frame", atracBadFrame},
          {"atrac states", atracStates}, {"atrac header sizes", atracHeaderSizes}, {"atrac ffmpeg", atracFFmpeg},
          {"atrac low level", atracLowLevel}, {"atrac low level in a state", atracLowLevelState},
          {"atrac contexts", atracContexts}, {"atrac contexts in a layout-26 state's block", atracContextsOlder},
          {"atrac contexts for sceSas", atracForSas}};
}

}
