//Movies decoded (mpeg.cpp, docs/psp-core.md's part 26): the header read, pictures decoded by FFmpeg's H.264 decoder
//and converted into the game's buffer, and the sound's access units taken out of the stream and decoded. The
//pictures are a stream made up (movie-maker.hpp), H.264 with every macroblock I_PCM (its samples stored as they are,
//so what it decodes to is known to the value): a baseline sequence and picture parameter set and an IDR picture an
//access unit, each behind an access unit delimiter, which sceMpegGetAvcAu cuts on. The sound: ATRAC3plus frames as a
//PSMF movie's private stream 1 carries them, decoded by a stand-in. Programs feed the movies as video/mpeg/basic did.
#include "movie-maker.hpp"

namespace allegrex_test::psp {

using ares::PlayStationPortable::AudioDecoder;
using ares::PlayStationPortable::VideoDecoder;

namespace {
constexpr u32 R = KernelMachine::Results;
constexpr u32 MovieAt = 0x0900'0000, Ring = R + 0x100, RingData = 0x0940'0000, Library = 0x0960'0000;
constexpr u32 Handle = R + 0x40, Au = R + 0x60, Got = R + 0x84, Place = R + 0x88, Mode = R + 0x90, Range = R + 0xa0;
constexpr u32 Pixels = 0x0980'0000, EsBuffer = 0x0990'0000, Sound = 0x09a0'0000, CallbackCode = 0x0880'3000;

auto word(KernelMachine& m, u32 address) -> u32 { return m.system.memory.read(4, address); }

//A PSMF movie of these access units and sound: the 2048-byte header (PSMF0015, the stream's offset and size), then a
//pack per 2000 bytes of video (stream 0xe0, each pack's PES packet with a time stamp where an access unit starts it),
//and a pack per frame of sound (private stream 1, its 4-byte header and an 8-byte ATRAC3plus frame header before
//each frame, the PES packet stamped).
auto movie(const std::vector<std::vector<u8>>& units, u32 soundFrames = 0) -> std::vector<u8> {
  std::vector<u8> out(2048, 0);
  memcpy(out.data(), "PSMF0015", 8);
  auto pack = [&](u8 stream, const std::vector<u8>& data, u64 time) {
    std::vector<u8> p = {0, 0, 1, 0xba, 0x44, 0, 4, 0, 4, 1, 1, 0x89, 0xc3, 0xf8};
    u32 length = 3 + 5 + data.size();
    for(u32 byte : {0u, 0u, 1u, u32(stream), length >> 8, length & 0xff, 0x81u, 0x80u, 5u}) p.push_back(byte);
    for(u64 byte : {0x21 | (time >> 29 & 0x0e), time >> 22 & 0xff, (time >> 14 & 0xfe) | 1, time >> 7 & 0xff,
                    (time << 1 & 0xfe) | 1}) {
      p.push_back(byte);
    }
    p.insert(p.end(), data.begin(), data.end());
    u32 padding = 2048 - p.size() - 6;
    for(u32 byte : {0u, 0u, 1u, 0xbeu, padding >> 8, padding & 0xff}) p.push_back(byte);
    p.resize(2048, 0xff);
    out.insert(out.end(), p.begin(), p.end());
  };
  u64 time = 90000;
  for(auto& unit : units) {
    for(u32 from = 0; from < unit.size(); from += 2000) {
      pack(0xe0, {unit.begin() + from, unit.begin() + std::min<u32>(unit.size(), from + 2000)}, time);
    }
    time += 3003;
  }
  for(u32 n = 0; n < soundFrames; n++) {
    std::vector<u8> data = {0, 0, 0, 0, 0x0f, 0xd0, 0x28, 0x2e, 0, 0, 0, 0};  //376-byte stereo frames
    data.resize(12 + 376, 0x40 + n);
    pack(0xbd, data, 90000 + n * 4180);
  }
  u32 size = out.size() - 2048;
  for(u32 n = 0; n < 4; n++) out[8 + n] = 0x800 >> (24 - n * 8), out[12 + n] = size >> (24 - n * 8);
  return out;
}

//The ringbuffer's callback, copying the movie's packets from memory (as media.cpp's); looping, its place goes back
//to the file's start at its end.
auto callback(KernelMachine& m, u32 size, bool looping = false) -> void {
  Assembler c{m, CallbackCode};
  c.put(lw(t1, 0, a2));
  c.put(sll(t2, a1, 11));
  c.li(t3, size);
  c.put(subu(t3, t3, t1));
  c.put(min(t2, t2, t3));
  c.li(t5, MovieAt); c.put(addu(t5, t5, t1));
  c.put(addu(t1, t1, t2));
  if(looping) {
    c.li(t4, size);
    c.put(subu(t4, t1, t4));
    c.put(movz(t1, zero, t4));
  }
  c.put(sw(t1, 0, a2));
  c.put(srl(v0, t2, 11));
  c.put(beq(t2, zero, 7)); c.put(nop);
  c.put(lw(t8, 0, t5)); c.put(sw(t8, 0, a0)); c.put(addiu(t5, t5, 4)); c.put(addiu(a0, a0, 4));
  c.put(beq(zero, zero, -7)); c.put(addiu(t2, t2, -4));
  c.put(jr(ra)); c.put(nop);
}

auto setUp(KernelMachine& m, const std::vector<u8>& bytes) -> void {
  m.system.memory.copyIn(MovieAt, bytes.data(), bytes.size());
  m.system.memory.write(4, Place, 2048);  //past the header, read by the game itself
  callback(m, bytes.size());
  m.call("sceMpegInit", {});
  m.call("sceMpegRingbufferConstruct", {Ring, 64, RingData, 64 * 0x868, CallbackCode, Place});
  m.call("sceMpegCreate", {Handle, Library, 0x10000, Ring, 512, 0, 0});
  m.call("sceMpegInitAu", {Handle, 1, Au});
}

//A program feeding the ringbuffer and decoding each access unit with sceMpegAvcDecode (to Pixels, 64 wide, or as
//wide as given) or sceMpegAvcDecodeYCbCr then sceMpegAvcCsc (the range at Range); each picture's "came" logged from
//Got on.
auto program(KernelMachine& m, u32 frames, bool ycbcr, u32 frameWidth = 64) -> void {
  Assembler a{m, 0x0880'0000};
  a.li(s0, 0); a.li(s1, R + 0x200);
  u32 loop = a.here();
  a.li(a0, Ring); a.li(a1, 16); a.li(a2, 64); a.call("sceMpegRingbufferPut");
  a.li(a0, Handle); a.li(a1, 0x12c0); a.li(a2, Au); a.li(a3, R + 0xc0); a.call("sceMpegGetAvcAu");
  a.li(a0, Handle); a.li(a1, Au);
  if(ycbcr) {
    a.li(a2, Mode + 8); a.li(a3, Got); a.call("sceMpegAvcDecodeYCbCr");
    a.li(a0, Handle); a.li(a1, 0); a.li(a2, Range); a.li(a3, 64); a.li(t0, Pixels); a.call("sceMpegAvcCsc");
  } else {
    a.li(a2, frameWidth); a.li(a3, Mode + 8); a.li(t0, Got); a.call("sceMpegAvcDecode");
  }
  a.li(t0, Got); a.put(lw(t1, 0, t0)); a.put(sw(t1, 0, s1));
  a.put(addiu(s1, s1, 4)); a.put(addiu(s0, s0, 1)); a.li(t0, frames);
  u32 at = a.here();
  a.put(bne(s0, t0, s32(loop - at - 4) / 4)); a.put(nop);
  a.call("sceKernelExitGame");
}

//BT.601's equations for video's range, worked out in floating point and rounded: what a colour converts to.
auto rgb(Colour c) -> std::array<u32, 3> {
  double y = 1.164 * (c.y - 16), b = c.cb - 128.0, r = c.cr - 128.0;
  auto clamp = [](double v) { return u32(std::clamp(std::lround(v), 0l, 255l)); };
  return {clamp(y + 1.596 * r), clamp(y - 0.392 * b - 0.813 * r), clamp(y + 2.017 * b)};
}

auto near(u32 a, u32 b) -> bool { return std::max(a, b) - std::min(a, b) <= 1; }

//Logs each ATRAC3plus frame decoded (its fill byte), giving 2048 stereo samples of it.
struct StandIn : AudioDecoder {
  std::shared_ptr<std::vector<u32>> log;
  auto decode(const u8* data, u32 size, s16* samples, u32 room) -> s32 override {
    if(size != 376) return -1;
    log->push_back(data[0]);
    for(u32 n = 0; n < std::min(room, 2048u) * 2; n++) samples[n] = data[0] * 100 + n % 2;
    return std::min(room, 2048u);
  }
  auto reset() -> void override {}
};

//Gives a picture of width by height for each access unit, its luma 16 plus its row's number (plus how many it
//decoded before), its colours grey.
struct PictureStandIn : VideoDecoder {
  u32 width = 32, height = 32, decoded = 0;
  std::vector<u8> planes[3];
  Picture shown;
  auto decode(const u8*, u32) -> bool override {
    u32 half = (width + 1) / 2, halfHeight = (height + 1) / 2;
    planes[0].resize(width * height);
    for(u32 y = 0; y < height; y++) memset(planes[0].data() + y * width, 16 + (y + decoded) % 200, width);
    planes[1].assign(half * halfHeight, 128), planes[2].assign(half * halfHeight, 128);
    shown.width = width, shown.height = height;
    for(u32 n = 0; n < 3; n++) shown.planes[n] = planes[n].data(), shown.strides[n] = n ? half : width;
    decoded++;
    return true;
  }
  auto picture() const -> const Picture& override { return shown; }
  auto reset() -> void override {}
};

auto pictures(KernelMachine& m, u32 width = 32, u32 height = 32) -> void {
  m.kernel.videoDecoders = [width, height]() -> std::unique_ptr<VideoDecoder> {
    auto decoder = std::make_unique<PictureStandIn>();
    decoder->width = width, decoder->height = height;
    return decoder;
  };
}

//A movie's packets put into the ring by hand, as the callback would leave them (no thread to call it from).
auto putIn(KernelMachine& m, const std::vector<u8>& bytes) -> void {
  u32 packets = (bytes.size() - 2048) / 2048;
  m.system.memory.copyIn(RingData, bytes.data() + 2048, packets * 2048);
  m.system.memory.write(4, Ring + 4, 0);
  m.system.memory.write(4, Ring + 8, packets);
  m.system.memory.write(4, Ring + 12, packets);
}

constexpr u32 LibraryAt = Library + 0x30;  //the library's memory, where its handle points
}

//The header (video/mpeg/basic): the stream's offset and size, its big-endian words 8 and 12; a header that isn't
//PSMF's refused; and without decoders (a build without FFmpeg) every header refused, as before there were any.
static auto mpegHeader() -> void {
  KernelMachine m;
  auto bytes = movie({});
  setUp(m, bytes);
  m.system.memory.write(4, MovieAt + 8, 0x0008'0000);   //0x800, big-endian
  m.system.memory.write(4, MovieAt + 12, 0x0008'0400);  //0x40800
  bool decoders = bool(m.kernel.videoDecoders);
  CHECK(m.call("sceMpegQueryStreamOffset", {Handle, MovieAt, R}), decoders ? 0 : 0x8061'0022);
  if(decoders) CHECK(word(m, R), 0x800);
  CHECK(m.call("sceMpegQueryStreamSize", {MovieAt, R}), decoders ? 0 : 0x8061'0022);
  if(decoders) CHECK(word(m, R), 0x40800);
  m.system.memory.write(1, MovieAt, 'X');
  CHECK(m.call("sceMpegQueryStreamOffset", {Handle, MovieAt, R}), 0x8061'0022);
  m.kernel.videoDecoders = {};
  m.system.memory.write(1, MovieAt, 'P');
  CHECK(m.call("sceMpegQueryStreamOffset", {Handle, MovieAt, R}), 0x8061'0022);
  CHECK(roundTrip(m), true);
}

//Pictures decoded and converted (with FFmpeg): three access units, each a picture of four macroblocks of two
//colours, decoded with sceMpegAvcDecode into a 64-pixel-wide buffer: the first gives no picture (the decoder holds
//one back), the second the first picture, the third the second, each pixel BT.601's for its macroblock's colour, in
//8888 (alpha opaque); after sceMpegAvcDecodeMode, 5650. On both engines. Then a state saved as the third picture is
//held, loaded into a fresh machine, which gives it back at sceMpegAvcDecodeStop, as the first does.
static auto mpegPictures() -> void {
  KernelMachine probe;
  if(!probe.kernel.videoDecoders) return;
  std::vector<Colour> red = {{81, 90, 240}, {145, 54, 34}}, blue = {{41, 240, 110}, {210, 16, 146}},
                      grey = {{128, 128, 128}, {16, 128, 128}};
  auto bytes = movie({picture(2, 2, red, 0), picture(2, 2, blue, 1), picture(2, 2, grey, 0)});
  for(bool recompile : {false, true}) {
    KernelMachine m;
    setUp(m, bytes);
    m.system.memory.write(4, Mode + 8, Pixels);
    program(m, 2, false);
    m.runProgram(0x0880'0000, recompile);
    CHECK(m.kernel.exited, true);
    CHECK(word(m, R + 0x200) == 0 && word(m, R + 0x204) == 1, true);
    auto pixel = [&](u32 x, u32 y) { return word(m, Pixels + (y * 64 + x) * 4); };
    bool right = true;
    for(u32 y = 0; y < 32; y++) {
      for(u32 x = 0; x < 32; x++) {
        auto want = rgb(red[(y / 16 * 2 + x / 16) % 2]);
        u32 got = pixel(x, y);
        right &= near(got & 0xff, want[0]) && near(got >> 8 & 0xff, want[1]) && near(got >> 16 & 0xff, want[2]);
        right &= got >> 24 == 0xff;
      }
    }
    CHECK(right, true);
    CHECK(pixel(32, 0), 0);  //nothing past the picture's width
  }
  //5650, and the held picture in a state
  KernelMachine m;
  setUp(m, bytes);
  m.system.memory.write(4, Mode, u32(-1));
  m.system.memory.write(4, Mode + 4, 0);
  CHECK(m.call("sceMpegAvcDecodeMode", {Handle, Mode}), 0);
  m.system.memory.write(4, Mode + 8, Pixels);
  program(m, 3, false);
  m.runProgram(0x0880'0000, false);
  auto want = rgb(blue[0]);
  u32 got = m.system.memory.read(2, Pixels);
  CHECK(near(got & 0x1f, want[0] >> 3) && near(got >> 5 & 0x3f, want[1] >> 2) && near(got >> 11, want[2] >> 3), true);
  auto state = saveState(m);
  KernelMachine fresh;
  CHECK(loadState(fresh, state), true);
  CHECK(saveState(fresh) == state, true);
  for(auto* k : {&m, &fresh}) {
    CHECK(k->call("sceMpegAvcDecodeStop", {Handle, 64, Mode + 8, Got}), 0);
    CHECK(word(*k, Got), 1);
    auto grey0 = rgb(grey[0]);
    u32 pixel = k->system.memory.read(2, Pixels);
    CHECK(near(pixel & 0x1f, grey0[0] >> 3) && near(pixel >> 11, grey0[2] >> 3), true);
    CHECK(k->call("sceMpegAvcDecodeStop", {Handle, 64, Mode + 8, Got}), 0);
    CHECK(word(*k, Got), 0);
  }
}

//sceMpegAvcDecodeYCbCr keeps the picture, sceMpegAvcCsc converts it: the part asked for (its left, top, width and
//height), frame width pixels a row; zeros for the whole picture; in 8888, then 5551 and 4444.
static auto mpegConversion() -> void {
  KernelMachine probe;
  if(!probe.kernel.videoDecoders) return;
  std::vector<Colour> colours = {{81, 90, 240}, {145, 54, 34}, {41, 240, 110}, {210, 16, 146}};
  auto bytes = movie({picture(2, 2, colours, 0), picture(2, 2, colours, 1)});
  KernelMachine m;
  setUp(m, bytes);
  for(u32 n = 0; n < 4; n++) m.system.memory.write(4, Range + n * 4, std::array<u32, 4>{16, 16, 16, 16}[n]);
  program(m, 2, true);
  m.runProgram(0x0880'0000, false);
  CHECK(word(m, R + 0x204), 1);
  auto want = rgb(colours[3]);
  u32 got = word(m, Pixels);
  CHECK(near(got & 0xff, want[0]) && near(got >> 8 & 0xff, want[1]) && near(got >> 16 & 0xff, want[2]), true);
  CHECK(word(m, Pixels + 16 * 4), 0);  //16 wide: the row ends there
  for(u32 n = 0; n < 4; n++) m.system.memory.write(4, Range + n * 4, 0);
  CHECK(m.call("sceMpegAvcCsc", {Handle, 0, Range, 64, Pixels}), 0);
  want = rgb(colours[0]);
  got = word(m, Pixels);
  CHECK(near(got & 0xff, want[0]) && near(got >> 16 & 0xff, want[2]), true);
  //the other 16-bit formats (sceMpegAvcDecodeMode's second word: 1 for 5551, 2 for 4444), alpha opaque
  for(u32 format : {1u, 2u}) {
    m.system.memory.write(4, Mode, u32(-1));
    m.system.memory.write(4, Mode + 4, format);
    CHECK(m.call("sceMpegAvcDecodeMode", {Handle, Mode}), 0);
    CHECK(m.call("sceMpegAvcCsc", {Handle, 0, Range, 64, Pixels}), 0);
    u32 pixel = m.system.memory.read(2, Pixels);
    u32 bits = format == 1 ? 5 : 4, mask = (1 << bits) - 1;
    bool right = true;
    for(u32 c = 0; c < 3; c++) right &= near(pixel >> c * bits & mask, want[c] >> (8 - bits));
    CHECK(right && pixel >> 3 * bits == (format == 1 ? 1u : 0xfu), true);
  }
  CHECK(roundTrip(m), true);
}

//The sound's access units: each ATRAC3plus frame with its header, from private stream 1, into the access unit's ES
//buffer with its time stamp (each PES packet's), then decoded into 2048 stereo samples; no more: "no data". The sound
//of packets freed for the pictures first is kept for it.
static auto mpegSound() -> void {
  KernelMachine m;
  auto log = std::make_shared<std::vector<u32>>();
  m.kernel.audioDecoders = [log](const AudioDecoder::Format& format) -> std::unique_ptr<AudioDecoder> {
    CHECK(format.codec == AudioDecoder::Codec::Atrac3plus && format.channels == 2 && format.frameBytes == 376, true);
    auto decoder = std::make_unique<StandIn>();
    decoder->log = log;
    return decoder;
  };
  auto bytes = movie({}, 3);
  setUp(m, bytes);
  constexpr u32 SoundAu = R + 0x300;
  m.call("sceMpegInitAu", {Handle, EsBuffer, SoundAu});
  CHECK(m.call("sceMpegRingbufferPut", {Ring, 16, 64}), 0);  //(no thread: nothing put in)
  //put the packets in by hand: the ring as the callback would leave it
  for(u32 n = 0; n < 3; n++) {
    m.system.memory.copyIn(RingData + n * 2048, bytes.data() + 2048 + n * 2048, 2048);
  }
  m.system.memory.write(4, Ring + 8, 3);
  m.system.memory.write(4, Ring + 12, 3);
  for(u32 n = 0; n < 3; n++) {
    CHECK(m.call("sceMpegGetAtracAu", {Handle, 0x1700, SoundAu, R + 0x380}), 0);
    CHECK(word(m, SoundAu + 4) == 90000 + n * 4180 && word(m, SoundAu + 20) == 384, true);
    CHECK(word(m, EsBuffer) == 0x2e28d00f && m.system.memory.read(1, EsBuffer + 8) == 0x40 + n, true);
    CHECK(word(m, R + 0x380), EsBuffer);
    CHECK(m.call("sceMpegAtracDecode", {Handle, SoundAu, Sound, 1}), 0);
    CHECK(m.system.memory.read(2, Sound), (0x40 + n) * 100);
    CHECK(m.system.memory.read(2, Sound + 2), (0x40 + n) * 100 + 1);
  }
  CHECK(m.call("sceMpegGetAtracAu", {Handle, 0x1700, SoundAu, R + 0x380}), 0x8061'8001);
  CHECK(*log == std::vector<u32>({0x40, 0x41, 0x42}), true);
  CHECK(roundTrip(m), true);
}

//The sound's access units from a ring of more packets than 4096 (Sega Rally Revo's 4800), as from a small one.
static auto mpegSoundBigRing() -> void {
  KernelMachine m;
  auto log = std::make_shared<std::vector<u32>>();
  m.kernel.audioDecoders = [log](const AudioDecoder::Format&) -> std::unique_ptr<AudioDecoder> {
    auto decoder = std::make_unique<StandIn>();
    decoder->log = log;
    return decoder;
  };
  auto bytes = movie({}, 3);
  setUp(m, bytes);
  constexpr u32 SoundAu = R + 0x300;
  m.call("sceMpegInitAu", {Handle, EsBuffer, SoundAu});
  for(u32 n = 0; n < 3; n++) m.system.memory.copyIn(RingData + n * 2048, bytes.data() + 2048 + n * 2048, 2048);
  m.system.memory.write(4, Ring, 4800);
  m.system.memory.write(4, Ring + 8, 3);
  m.system.memory.write(4, Ring + 12, 3);
  for(u32 n = 0; n < 3; n++) {
    CHECK(m.call("sceMpegGetAtracAu", {Handle, 0x1700, SoundAu, R + 0x380}), 0);
    CHECK(word(m, SoundAu + 4), 90000 + n * 4180);
  }
  CHECK(m.call("sceMpegGetAtracAu", {Handle, 0x1700, SoundAu, R + 0x380}), 0x8061'8001);
}

//An access unit with nothing in it: 0x807f00fd before the movie's sound has been decoded (video/mpeg/basic's, for a
//movie with none), silence once it has (Juiced: Eliminator decodes so as its movies end), nothing decoded; after a
//flush, 0x807f00fd again.
static auto mpegSoundPastItsEnd() -> void {
  KernelMachine m;
  auto log = std::make_shared<std::vector<u32>>();
  m.kernel.audioDecoders = [log](const AudioDecoder::Format&) -> std::unique_ptr<AudioDecoder> {
    auto decoder = std::make_unique<StandIn>();
    decoder->log = log;
    return decoder;
  };
  auto bytes = movie({}, 1);
  setUp(m, bytes);
  constexpr u32 SoundAu = R + 0x300;
  m.call("sceMpegInitAu", {Handle, EsBuffer, SoundAu});
  CHECK(m.call("sceMpegAtracDecode", {Handle, SoundAu, Sound, 1}), 0x807f'00fd);
  m.system.memory.copyIn(RingData, bytes.data() + 2048, 2048);
  m.system.memory.write(4, Ring + 8, 1);
  m.system.memory.write(4, Ring + 12, 1);
  CHECK(m.call("sceMpegGetAtracAu", {Handle, 0x1700, SoundAu, R + 0x380}), 0);
  CHECK(m.call("sceMpegAtracDecode", {Handle, SoundAu, Sound, 1}), 0);
  CHECK(m.call("sceMpegGetAtracAu", {Handle, 0x1700, SoundAu, R + 0x380}), 0x8061'8001);
  CHECK(word(m, SoundAu + 20), 0);
  m.system.memory.fill(Sound, 0xdd, 0x2004);
  CHECK(m.call("sceMpegAtracDecode", {Handle, SoundAu, Sound, 1}), 0);
  CHECK(word(m, Sound) == 0 && word(m, Sound + 0x1ffc) == 0 && word(m, Sound + 0x2000) == 0xdddd'dddd, true);
  CHECK(*log == std::vector<u32>({0x40}), true);
  CHECK(m.call("sceMpegAtracDecode", {Handle, SoundAu, 0, 1}), 0x807f'00fd);  //no buffer to decode into
  CHECK(roundTrip(m), true);
  CHECK(m.call("sceMpegFlushAllStream", {Handle}), 0);
  CHECK(m.call("sceMpegAtracDecode", {Handle, SoundAu, Sound, 1}), 0x807f'00fd);
}

//A movie its callback feeds round and round from its file's start, header and all (as Space Invaders Extreme's
//does), two packets a time: the header coming after the movie's packs is the movie starting over, not its end, so
//its access units come again and again, none lost.
static auto mpegRingLoops() -> void {
  std::vector<std::vector<u8>> units = {slices(5, {7}), slices(5, {7, 7}), slices(5, {7, 7, 7})};
  auto bytes = movie(units);
  for(bool recompile : {false, true}) {
    KernelMachine m;
    setUp(m, bytes);
    m.system.memory.write(4, Place, 0);
    callback(m, bytes.size(), true);
    Assembler a{m, 0x0880'0000};
    a.li(s0, 0); a.li(s1, R + 0x300);
    u32 loop = a.here();
    a.li(a0, Ring); a.li(a1, 2); a.li(a2, 64); a.call("sceMpegRingbufferPut");
    a.li(a0, Handle); a.li(a1, 0x12c0); a.li(a2, Au); a.li(a3, R + 0xc0); a.call("sceMpegGetAvcAu");
    a.put(sw(v0, 0, s1)); a.li(t0, Au); a.put(lw(t1, 20, t0)); a.put(sw(t1, 4, s1));
    a.put(addiu(s1, s1, 8)); a.put(addiu(s0, s0, 1)); a.li(t0, 20);
    u32 at = a.here();
    a.put(bne(s0, t0, s32(loop - at - 4) / 4)); a.put(nop);
    a.call("sceKernelExitGame");
    m.runProgram(0x0880'0000, recompile);
    CHECK(m.kernel.exited, true);
    std::vector<u32> sizes;
    for(u32 n = 0; n < 20; n++) {
      if(!word(m, R + 0x300 + n * 8)) sizes.push_back(word(m, R + 0x304 + n * 8));
    }
    bool repeats = sizes.size() >= 9;
    for(u32 n = 0; n < sizes.size(); n++) repeats &= sizes[n] == units[n % 3].size();
    CHECK(repeats, true);
  }
}

//sceMpegAvcCsc's part reaching past the picture (32 by 32, a stand-in's) is cut to it, however large: from row 1,
//0xffffffff rows tall, gives the 31 rows left (as 32-bit sums, 1 + 0xffffffff passed for 0, and the conversion read
//on past the picture); from column 1, 0xffffffff wide, the 31 columns left (where it made a row of 4 GiB).
static auto mpegConversionPart() -> void {
  KernelMachine m;
  pictures(m);
  setUp(m, movie({slices(5, {7}), slices(5, {7}), slices(5, {7})}));
  program(m, 2, true);
  m.runProgram(0x0880'0000, false);
  CHECK(word(m, R + 0x204), 1);
  auto grey = [](u32 y) {
    u32 v = std::clamp((298 * (s32(y) - 16) + 128) >> 8, 0, 255);
    return v | v << 8 | v << 16;
  };
  for(u32 n = 0; n < 4; n++) m.system.memory.write(4, Range + n * 4, std::array<u32, 4>{0, 1, 0, ~0u}[n]);
  m.system.memory.fill(Pixels, 0xdd, 64 * 40 * 4);
  CHECK(m.call("sceMpegAvcCsc", {Handle, 0, Range, 64, Pixels}), 0);
  CHECK(word(m, Pixels) == (0xff00'0000 | grey(17)) && word(m, Pixels + 31 * 4) == (0xff00'0000 | grey(17)), true);
  CHECK(word(m, Pixels + 30 * 256) == (0xff00'0000 | grey(47)) && word(m, Pixels + 31 * 256) == 0xdddd'dddd, true);
  for(u32 n = 0; n < 4; n++) m.system.memory.write(4, Range + n * 4, std::array<u32, 4>{1, 0, ~0u, 0}[n]);
  m.system.memory.fill(Pixels, 0xdd, 64 * 40 * 4);
  CHECK(m.call("sceMpegAvcCsc", {Handle, 0, Range, 64, Pixels}), 0);
  CHECK(word(m, Pixels + 30 * 4) == (0xff00'0000 | grey(16)) && word(m, Pixels + 31 * 4) == 0xdddd'dddd, true);
  CHECK(word(m, Pixels + 31 * 256) == (0xff00'0000 | grey(47)) && word(m, Pixels + 32 * 256) == 0xdddd'dddd, true);
}

//Pictures larger than a state holds (VideoDecoder::MaxSide, 1024, either way) are passed over, and the states saved
//meanwhile load; 1024 by 1024 is kept. With FFmpeg too: its pictures 1040 wide passed over, the next kept.
static auto mpegPictureSizes() -> void {
  for(auto [width, height, kept] : {std::tuple{1040u, 16u, false}, {16u, 1040u, false}, {1024u, 1024u, true}}) {
    KernelMachine m;
    pictures(m, width, height);
    setUp(m, movie({slices(5, {7}), slices(5, {7}), slices(5, {7})}));
    program(m, 2, true);
    m.runProgram(0x0880'0000, false);
    auto& stream = m.kernel.mpegStreams[LibraryAt];
    CHECK(word(m, R + 0x204) == 1 && stream.shown.empty() == !kept && stream.held.empty() == !kept, true);
    if(kept) CHECK(stream.shownWidth == 1024 && stream.shownHeight == 1024, true);
    CHECK(roundTrip(m), true);
  }
  KernelMachine probe;
  if(!probe.kernel.videoDecoders) return;
  std::vector<Colour> grey = {{128, 128, 128}};
  KernelMachine m;
  setUp(m, movie({picture(65, 1, grey, 0), picture(2, 2, grey, 1), picture(2, 2, grey, 0)}));
  program(m, 2, true);
  m.runProgram(0x0880'0000, false);
  auto& stream = m.kernel.mpegStreams[LibraryAt];
  CHECK(stream.shown.empty() && stream.heldWidth == 32 && stream.heldHeight == 32, true);
  CHECK(roundTrip(m), true);
}

//sceMpegCreate starts the library afresh, over what the Media Engine held for a library there before: the sound
//queued and handed out, its time stamps and its decoder. A movie played again from the same memory (the ring
//refilled from its start, sceMpegDelete never called) gives its first sound frame again, at its own time stamp,
//through a decoder of its own.
static auto mpegCreateAfresh() -> void {
  KernelMachine m;
  auto log = std::make_shared<std::vector<u32>>();
  u32 made = 0;
  m.kernel.audioDecoders = [log, &made](const AudioDecoder::Format&) -> std::unique_ptr<AudioDecoder> {
    made++;
    auto decoder = std::make_unique<StandIn>();
    decoder->log = log;
    return decoder;
  };
  auto bytes = movie({}, 3);
  setUp(m, bytes);
  constexpr u32 SoundAu = R + 0x300;
  for(u32 time : {0u, 1u}) {
    if(time) m.call("sceMpegCreate", {Handle, Library, 0x10000, Ring, 512, 0, 0});
    m.call("sceMpegInitAu", {Handle, EsBuffer, SoundAu});
    putIn(m, bytes);
    CHECK(m.call("sceMpegGetAtracAu", {Handle, 0x1700, SoundAu, R + 0x380}), 0);
    CHECK(word(m, SoundAu + 4) == 90000 && m.system.memory.read(1, EsBuffer + 8) == 0x40, true);
    CHECK(m.call("sceMpegAtracDecode", {Handle, SoundAu, Sound, 1}), 0);
  }
  CHECK(made == 2 && *log == std::vector<u32>({0x40, 0x40}), true);
}

//After a state is loaded: the movie's sound decoder, made afresh, is primed with the frame decoded last (and the
//PCM matches the machine that went on); pictures wait for one a decoder can start from: not a P picture, nor one
//with a P slice among its I slices, but an I picture that isn't an IDR one (its header read past an emulation
//prevention byte: its first macroblock 2^22 - 1, coded 00 00 02...).
static auto mpegAfterState() -> void {
  auto log = std::make_shared<std::vector<u32>>();
  auto sound = [log](KernelMachine& k) {
    k.kernel.audioDecoders = [log](const AudioDecoder::Format&) -> std::unique_ptr<AudioDecoder> {
      auto decoder = std::make_unique<StandIn>();
      decoder->log = log;
      return decoder;
    };
  };
  KernelMachine m;
  sound(m);
  auto bytes = movie({}, 3);
  setUp(m, bytes);
  constexpr u32 SoundAu = R + 0x300;
  m.call("sceMpegInitAu", {Handle, EsBuffer, SoundAu});
  putIn(m, bytes);
  auto frame = [&](KernelMachine& k) {
    CHECK(k.call("sceMpegGetAtracAu", {Handle, 0x1700, SoundAu, R + 0x380}), 0);
    CHECK(k.call("sceMpegAtracDecode", {Handle, SoundAu, Sound, 1}), 0);
  };
  frame(m), frame(m);
  auto state = saveState(m);
  KernelMachine fresh;
  sound(fresh);
  CHECK(loadState(fresh, state), true);
  log->clear();
  frame(fresh);
  CHECK(*log == std::vector<u32>({0x41, 0x42}), true);
  std::vector<u8> after(0x2000), went(0x2000);
  fresh.system.memory.copyOut(after.data(), Sound, after.size());
  frame(m);
  m.system.memory.copyOut(went.data(), Sound, went.size());
  CHECK(after == went, true);
  CHECK(saveState(m) == saveState(fresh), true);
  //the pictures
  KernelMachine v;
  pictures(v);
  auto units = movie({slices(5, {7}), slices(1, {5}), slices(1, {5}), slices(1, {7, 5}),
                      slices(1, {2}, (1 << 22) - 1), slices(1, {5}), slices(1, {5})});
  setUp(v, units);
  putIn(v, units);
  auto picture = [&](KernelMachine& k) {
    CHECK(k.call("sceMpegGetAvcAu", {Handle, 0x12c0, Au, R + 0xc0}), 0);
    CHECK(k.call("sceMpegAvcDecodeYCbCr", {Handle, Au, Mode + 8, Got}), 0);
  };
  picture(v), picture(v);
  KernelMachine loaded;
  pictures(loaded);
  CHECK(loadState(loaded, saveState(v)), true);
  auto& stream = loaded.kernel.mpegStreams[LibraryAt];
  CHECK(stream.keyframe && stream.held.size() && stream.held[0] == 17, true);
  picture(loaded);
  CHECK(stream.keyframe && stream.held[0] == 17, true);  //a P picture
  picture(loaded);
  CHECK(stream.keyframe && stream.held[0] == 17, true);  //an I slice and a P slice
  picture(loaded);
  CHECK(!stream.keyframe && stream.held[0] == 18, true);  //I slices, not IDR: its decoder's second picture
  picture(loaded);
  CHECK(!stream.keyframe && stream.held[0] == 19, true);
}

//A movie fed from a file that goes on past it (Mega Man Maverick Hunter X's): the packets that aren't packs, past its
//end, aren't taken. Put gives the movie's packs alone, the next Put none, and the movie has ended. What comes before
//the first pack (the header, put in by a game that reads from the file's start) is taken, and after a flush is
//taken again. (A ring never given a pack takes whatever it's given: media.cpp's rings.)
static auto mpegRingEnd() -> void {
  auto bytes = movie({slices(5, {7}), slices(5, {7})});
  u32 packs = (bytes.size() - 2048) / 2048;
  for(u32 n = 0; n < 3; n++) {  //the next file's
    std::vector<u8> other(2048, n);
    memcpy(other.data(), "LINK", 4);
    bytes.insert(bytes.end(), other.begin(), other.end());
  }
  for(bool header : {false, true}) {
    for(bool recompile : {false, true}) {
      KernelMachine m;
      setUp(m, bytes);
      if(header) m.system.memory.write(4, Place, 0);  //from the file's start
      Assembler a{m, 0x0880'0000};
      for(u32 n = 0; n < 3; n++) {
        if(n == 2) {  //the movie again, header and all
          a.li(t0, Place); a.put(sw(zero, 0, t0));
          a.li(a0, Handle); a.call("sceMpegFlushAllStream");
        }
        a.li(a0, Ring); a.li(a1, 16); a.li(a2, 64); a.call("sceMpegRingbufferPut");
        a.li(t0, R + 0x200 + n * 4); a.put(sw(v0, 0, t0));
      }
      a.call("sceKernelExitGame");
      m.runProgram(0x0880'0000, recompile);
      CHECK(m.kernel.exited, true);
      CHECK(word(m, R + 0x200), packs + header);
      CHECK(word(m, R + 0x204), 0);
      CHECK(word(m, R + 0x208), packs + 1);
      CHECK(m.call("sceMpegRingbufferAvailableSize", {Ring}), 64 - packs - 1);
      CHECK(word(m, LibraryAt + 0x708), 1);  //ended
    }
  }
  //a ring with no library to keep that in takes all it's given, what's past the movie too
  KernelMachine m;
  setUp(m, bytes);
  m.system.memory.write(4, Ring + 40, 0);  //(the ring's library)
  Assembler a{m, 0x0880'0000};
  a.li(a0, Ring); a.li(a1, 16); a.li(a2, 64); a.call("sceMpegRingbufferPut");
  a.li(t0, R + 0x200); a.put(sw(v0, 0, t0));
  a.call("sceKernelExitGame");
  m.runProgram(0x0880'0000, false);
  CHECK(word(m, R + 0x200), packs + 3);
}

//A picture decoded with a frame width of 0 goes into the buffer as wide as the library was made with (512).
static auto mpegDecodeWidth() -> void {
  KernelMachine m;
  pictures(m);
  setUp(m, movie({slices(5, {7}), slices(5, {7}), slices(5, {7})}));
  m.system.memory.write(4, Mode + 8, Pixels);
  program(m, 2, false, 0);
  m.runProgram(0x0880'0000, false);
  CHECK(word(m, R + 0x204), 1);
  CHECK(word(m, Pixels + 512 * 4) >> 24, 0xff);  //row 1, 512 pixels on
  CHECK(word(m, Pixels + 64 * 4), 0);            //(not 64: past the picture's 32)
}

auto movieTests() -> Tests {
  return {{"mpeg header", mpegHeader}, {"mpeg pictures decoded", mpegPictures},
          {"mpeg pictures converted", mpegConversion}, {"mpeg sound access units", mpegSound},
          {"mpeg sound from a big ring", mpegSoundBigRing},
          {"mpeg csc part past the picture", mpegConversionPart}, {"mpeg picture sizes", mpegPictureSizes},
          {"mpeg create afresh", mpegCreateAfresh}, {"mpeg after a state", mpegAfterState},
          {"mpeg ring at the movie's end", mpegRingEnd}, {"mpeg decode at the library's width", mpegDecodeWidth},
          {"mpeg sound past its end", mpegSoundPastItsEnd}, {"mpeg ring fed round and round", mpegRingLoops}};
}

}
