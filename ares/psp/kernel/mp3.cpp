//sceMp3, the library games play MP3 files with: a game reserves a handle for a stream (where it lies in its file,
//a buffer for its bytes, a buffer for the samples), feeds the stream bytes as the library asks, and decodes a frame a
//call. The frames are decoded by codec.cpp's decoder (FFmpeg's); without one, sceMp3Init refuses every stream. What
//the library does is pspsdk's pspmp3.h and pspautotests' audio/mp3 tests with the results they recorded on a PSP;
//no other emulator's code was read.
//- Two handles (reserve: 0, 1, then 0x80671201). The arguments are checked as audio/mp3/reserve recorded: a stream
//  start before its end (as 64-bit numbers, whose top halves must be 0 for the start), a stream buffer of 8192
//  bytes at least and a sample buffer of 9216, neither null. No arguments at all reserves a handle without a stream.
//- The stream buffer: its first 1472 bytes the library's own room (where a frame split across its end is joined),
//  the rest a ring filled a half at a time: at first all of it, then each half once decoding has left it, the game
//  told where to write, how much (the half's rest, whatever is left of the file: audio/mp3/stream) and from where in
//  the file; once the file's end is in, nothing more (or, looping, the stream's start again).
//- sceMp3Init reads the first frame's header, within 1440 bytes of the start: MPEG-1 layer III at 44.1 kHz only
//  (MPEG-2 or 2.5 layer III: 0x80671301; 48 or 32 kHz: 0x80671302; anything else: 0x807f00fd, audio/mp3/init).
//- sceMp3Decode decodes the next frame into the sample buffer's halves in turn (1152 stereo samples, 4608 bytes),
//  pointing the game at the half; at the stream's end it gives 0, or loops (sceMp3SetLoopNum; -1 for ever, the
//  default) back to the stream's start.
//Decoding waits a moment (Mp3DecodeMicroseconds), as atrac.cpp's does. A handle's decoder isn't in a state: it's
//made afresh as the next frame is decoded.

namespace {
  constexpr u32 Mp3ErrorBadHandle = 0x8067'1001, Mp3ErrorNullBuffer = 0x8067'1002, Mp3ErrorBadArgument = 0x8067'1003;
  constexpr u32 Mp3ErrorNotReserved = 0x8067'1102, Mp3ErrorNotInitialized = 0x8067'1103;
  constexpr u32 Mp3ErrorNoHandle = 0x8067'1201, Mp3ErrorBadVersion = 0x8067'1301, Mp3ErrorBadRate = 0x8067'1302;
  constexpr u32 Mp3ErrorBadFrame = 0x8067'1501, Mp3ErrorBadHeader = 0x807f'00fd;
  constexpr u32 Mp3Workspace = 1472, Mp3Samples = 1152, Mp3DecodeMicroseconds = 300;
  constexpr u32 Mp3Bitrates[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};

  //An MPEG audio frame header: its version (3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5, 1 none), layer (1-3, 0 none), bit rate
  //(kilobits a second, MPEG-1 layer III's table), sample rate, channels and size in bytes (MPEG-1 layer III's).
  struct Mp3Header {
    u32 version = 1, layer = 0, bitrate = 0, rate = 0, channels = 0, bytes = 0;
    bool valid = false;
  };
  auto mp3Header(const u8* at) -> Mp3Header {
    Mp3Header header;
    if(at[0] != 0xff || (at[1] & 0xe0) != 0xe0) return header;
    header.version = at[1] >> 3 & 3;
    header.layer = 4 - (at[1] >> 1 & 3);
    header.bitrate = Mp3Bitrates[at[2] >> 4];
    u32 rateIndex = at[2] >> 2 & 3;
    static constexpr u32 rates[3] = {44100, 48000, 32000};
    header.rate = rateIndex < 3 ? rates[rateIndex] : 0;
    header.channels = (at[3] >> 6) == 3 ? 1 : 2;
    if(header.bitrate && header.rate) header.bytes = 144000 * header.bitrate / header.rate + (at[2] >> 1 & 1);
    header.valid = header.layer != 4 && header.bitrate && header.rate && header.version != 1;
    return header;
  }
}

//A handle reserved; else the error is the result (one not reserved: notReserved).
//(A handle reserved without a stream counts as not reserved: audio/mp3's "NULL info arg" cases.)
auto Kernel::mp3Find(u32 handle, u32 notReserved) -> Mp3* {
  if(handle >= 2) return result(Mp3ErrorBadHandle), nullptr;
  if(!mp3s[handle].reserved || !mp3s[handle].bufferSize) return result(notReserved), nullptr;
  return &mp3s[handle];
}

auto Kernel::sceMp3InitResource() -> void {
  mp3Terminated = false;
  result(0);
}

//Every handle given back (audio/mp3/initresource: none to reserve afterwards until sceMp3InitResource again).
auto Kernel::sceMp3TermResource() -> void {
  for(auto& mp3 : mp3s) mp3 = {};
  mp3Terminated = true;
  result(0);
}

//(arguments): a handle for a stream. The arguments: stream start and end (64-bit), the stream buffer and its size,
//the sample buffer and its size.
auto Kernel::sceMp3ReserveMp3Handle() -> void {
  u32 arguments = arg(0);
  if(mp3Terminated) return result(Mp3ErrorNoHandle);
  Mp3 mp3;
  mp3.reserved = true;
  if(arguments) {
    if(!memory.reaches(arguments, 32)) return result(ErrorInvalidPointer);
    u32 words[8];
    for(u32 n = 0; n < 8; n++) words[n] = memory.read(4, arguments + n * 4);
    u64 start = u64(words[1]) << 32 | words[0], end = u64(words[3]) << 32 | words[2];
    if(!words[4] || !words[6]) return result(Mp3ErrorNullBuffer);
    if(words[1] || s64(end) <= s64(start) || s32(words[5]) < 8192 || s32(words[7]) < 9216) {
      return result(Mp3ErrorBadArgument);
    }
    mp3.start = start, mp3.end = end;
    mp3.buffer = words[4], mp3.bufferSize = words[5], mp3.pcm = words[6], mp3.pcmSize = words[7];
    mp3.filePos = mp3.readFilePos = start;
    mp3.writeLimit = mp3.bufferSize - Mp3Workspace;
  }
  for(u32 handle = 0; handle < 2; handle++) {
    if(mp3s[handle].reserved) continue;
    mp3s[handle] = std::move(mp3);
    return result(handle);
  }
  result(Mp3ErrorNoHandle);
}

auto Kernel::sceMp3ReleaseMp3Handle() -> void {
  u32 handle = arg(0);
  if(handle >= 2) return result(Mp3ErrorBadHandle);
  mp3s[handle] = {};
  result(0);
}

//The stream's data area: the stream buffer past the library's room. Its halves.
static auto mp3Area(const Kernel::Mp3& mp3) -> u32 { return mp3.bufferSize - Mp3Workspace; }

//An initialized stream's decoder, made if it isn't there (by sceMp3Init, or after a state loads); null if none.
static auto mp3Decoder(Kernel& kernel, Kernel::Mp3& mp3) -> AudioDecoder* {
  if(!mp3.decoder && kernel.audioDecoders) {
    AudioDecoder::Format format;
    format.codec = AudioDecoder::Codec::Mp3;
    format.channels = 2;
    format.rate = mp3.rate;
    mp3.decoder = kernel.audioDecoders(format);
  }
  return mp3.decoder.get();
}

//How many bytes the game may add now: the rest of the half being filled; or, that one full, the next half once
//decoding has left it.
auto Kernel::mp3Writable(Mp3& mp3) -> u32 {
  if(!mp3.bufferSize || mp3.filePos >= mp3.end) return 0;
  u32 area = mp3Area(mp3), half = area / 2;
  if(mp3.writePos < mp3.writeLimit) return mp3.writeLimit - mp3.writePos;
  u32 next = mp3.writeLimit >= area ? 0 : half;  //the half after the one just filled
  bool readingIt = next == 0 ? mp3.readPos < half : mp3.readPos >= half;
  if(readingIt || mp3.available > area - half) return 0;
  mp3.writePos = next;
  mp3.writeLimit = next ? area : half;
  return mp3.writeLimit - mp3.writePos;
}

//(handle, where to put where to write, how much, and from where in the file).
auto Kernel::sceMp3GetInfoToAddStreamData() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  u32 bytes = mp3Writable(*mp3);
  u32 at = bytes ? mp3->buffer + Mp3Workspace + mp3->writePos : 0;
  u32 from = bytes ? u32(mp3->filePos) : 0;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), at);
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), bytes);
  if(memory.reaches(arg(3), 4)) memory.write(4, arg(3), from);
  result(0);
}

//(handle, bytes): the game added that much where it was told.
auto Kernel::sceMp3NotifyAddStreamData() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  u32 bytes = std::min(arg(1), mp3Writable(*mp3));
  mp3->writePos += bytes;
  mp3->available += bytes;
  mp3->filePos += bytes;
  result(0);
}

//(handle): 1 while there's room for more of the stream, else 0.
auto Kernel::sceMp3CheckStreamDataNeeded() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  result(mp3Writable(*mp3) > 0);
}

//The stream's next frame, read across the ring's end (as the PSP joins it in its room): false if there's no whole
//frame yet. Bytes before a frame header (after a seek by frame, which lands a byte early: resetposbyframe) are passed
//over.
auto Kernel::mp3Frame(Mp3& mp3, std::vector<u8>& frame) -> bool {
  u32 area = mp3Area(mp3);
  auto byte = [&](u32 n) -> u8 { return memory.read(1, mp3.buffer + Mp3Workspace + (mp3.readPos + n) % area); };
  Mp3Header header;
  for(u32 skip = 0; skip < 1440 && skip + 4 <= mp3.available; skip++) {
    u8 head[4] = {byte(skip), byte(skip + 1), byte(skip + 2), byte(skip + 3)};
    header = mp3Header(head);
    if(!header.valid) continue;
    mp3.readPos = (mp3.readPos + skip) % area;
    mp3.available -= skip;
    mp3.readFilePos += skip;
    break;
  }
  if(!header.valid || header.bytes > mp3.available) return false;
  frame.resize(header.bytes);
  for(u32 n = 0; n < header.bytes; n++) frame[n] = byte(n);
  return true;
}

//(handle): the stream's first frame header read: the stream described (its version, rate, channels, bit rate, and
//frames: the stream's bytes over a frame's, an Info or Xing frame's count unread, audio/mp3/getframenum).
auto Kernel::sceMp3Init() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  if(!audioDecoders) return result(Mp3ErrorBadHeader);
  //(read where the stream's first bytes go, whether or not the game has said it added them: audio/mp3/init writes a
  //header there and calls sceMp3Init)
  u32 area = mp3Area(*mp3);
  std::vector<u8> bytes(std::min(area, 1440u + 2048u));
  for(u32 n = 0; n < bytes.size(); n++) {
    bytes[n] = memory.read(1, mp3->buffer + Mp3Workspace + (mp3->readPos + n) % area);
  }
  u32 skip = 0;
  while(skip < 1440 && !(bytes[skip] == 0xff && (bytes[skip + 1] & 0xe0) == 0xe0)) skip++;
  if(skip == 1440) return result(Mp3ErrorBadHeader);
  auto header = mp3Header(&bytes[skip]);
  if(header.layer == 3 && header.version != 3) return result(Mp3ErrorBadVersion);
  if(header.layer != 3 || !header.bitrate || !header.rate) return result(Mp3ErrorBadHeader);
  if(header.rate != 44100) return result(Mp3ErrorBadRate);
  mp3->readPos = (mp3->readPos + skip) % area;
  mp3->available -= std::min(skip, mp3->available);
  mp3->readFilePos += skip;
  mp3->version = header.version, mp3->rate = header.rate, mp3->channels = header.channels;
  mp3->bitrate = header.bitrate;
  mp3->frames = (mp3->end - mp3->start) * header.rate / (144000ull * header.bitrate);
  mp3->decoder.reset();
  if(!mp3Decoder(*this, *mp3)) return result(Mp3ErrorBadHeader);
  mp3->initialized = true;
  mp3->loopNum = mp3->loopSet ? mp3->loopNum : -1;
  result(0);
}

//(handle, where to put where the samples are): the next frame decoded into the next half of the sample buffer, 1152
//stereo samples (mono doubled), 4608 bytes the result. At the stream's end (a frame it cuts short counting as the
//end), 0; and while loops are left the stream starts again, the buffer emptied for the game to feed from the
//stream's start (audio/mp3/getsumdecoded: 0 at each loop's end).
auto Kernel::sceMp3Decode() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  if(!mp3->initialized) return result(Mp3ErrorNotInitialized);
  std::vector<u8> frame;
  bool whole = mp3Frame(*mp3, frame);
  if(mp3->readFilePos >= mp3->end || (whole && mp3->readFilePos + frame.size() > mp3->end) ||
     (!whole && mp3->filePos >= mp3->end)) {
    if(mp3->loopNum != 0) {
      if(mp3->loopNum > 0) mp3->loopNum--;
      mp3->filePos = mp3->readFilePos = mp3->start;
      mp3->readPos = mp3->writePos = mp3->available = 0;
      mp3->writeLimit = mp3Area(*mp3);
      if(mp3->decoder) mp3->decoder->reset();
    } else {
      mp3->readFilePos = mp3->end;
    }
    return result(0);
  }
  auto decoder = mp3Decoder(*this, *mp3);
  if(!whole || !decoder) return result(Mp3ErrorBadHeader);
  std::vector<s16> samples(Mp3Samples * 2);
  s32 made = decoder->decode(frame.data(), frame.size(), samples.data(), Mp3Samples);
  if(made < 0) made = 0, std::fill(samples.begin(), samples.end(), 0);
  u32 area = mp3Area(*mp3);
  mp3->readPos = (mp3->readPos + frame.size()) % area;
  mp3->available -= frame.size();
  mp3->readFilePos += frame.size();
  mp3->sumDecoded += Mp3Samples;
  u32 out = mp3->pcm + mp3->pcmHalf * (mp3->pcmSize / 2);
  if(memory.reaches(out, Mp3Samples * 4)) memory.copyIn(out, samples.data(), Mp3Samples * 4);
  mp3->pcmHalf ^= 1;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), out);
  result(Mp3Samples * 4);
  codecWait(Mp3DecodeMicroseconds);
}

//(handle, loops): -1 for ever (any negative number), else the times to play the stream again.
auto Kernel::sceMp3SetLoopNum() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  mp3->loopNum = std::max(s32(arg(1)), -1);
  mp3->loopSet = true;
  result(0);
}

auto Kernel::sceMp3GetLoopNum() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  result(mp3->loopNum);
}

auto Kernel::sceMp3GetSumDecodedSample() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  result(mp3->sumDecoded);
}

//The queries of an initialized stream: before sceMp3Init, 0x80671103.
auto Kernel::sceMp3GetMaxOutputSample() -> void {
  auto mp3 = mp3Find(arg(0), Mp3ErrorNotInitialized);
  if(!mp3) return;
  result(mp3->initialized ? Mp3Samples : Mp3ErrorNotInitialized);
}

auto Kernel::sceMp3GetSamplingRate() -> void {
  auto mp3 = mp3Find(arg(0), Mp3ErrorNotInitialized);
  if(!mp3) return;
  result(mp3->initialized ? mp3->rate : Mp3ErrorNotInitialized);
}

auto Kernel::sceMp3GetBitRate() -> void {
  auto mp3 = mp3Find(arg(0), Mp3ErrorNotInitialized);
  if(!mp3) return;
  result(mp3->initialized ? mp3->bitrate : Mp3ErrorNotInitialized);
}

auto Kernel::sceMp3GetMp3ChannelNum() -> void {
  auto mp3 = mp3Find(arg(0), Mp3ErrorNotInitialized);
  if(!mp3) return;
  result(mp3->initialized ? mp3->channels : Mp3ErrorNotInitialized);
}

auto Kernel::sceMp3GetFrameNum() -> void {
  auto mp3 = mp3Find(arg(0), Mp3ErrorNotInitialized);
  if(!mp3) return;
  result(mp3->initialized ? mp3->frames : Mp3ErrorNotInitialized);
}

auto Kernel::sceMp3GetMPEGVersion() -> void {
  auto mp3 = mp3Find(arg(0), Mp3ErrorNotInitialized);
  if(!mp3) return;
  result(mp3->initialized ? mp3->version : Mp3ErrorNotInitialized);
}

//(handle, frame): decoding starts again at a frame, the buffer emptied and to be fed from that frame's place: a
//frame's bytes (144000 times the bit rate over the rate, unrounded) that many times, less one (audio/mp3/
//resetposbyframe: frame 1 of 128 kbps at 44.1 kHz is 416 bytes on). Past the stream's frames: 0x80671501.
auto Kernel::sceMp3ResetPlayPositionByFrame() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  u32 frame = arg(1);
  if(!mp3->initialized) return result(Mp3ErrorNotInitialized);
  if(frame >= mp3->frames) return result(Mp3ErrorBadFrame);
  u64 offset = frame ? u64(frame) * 144000 * mp3->bitrate / mp3->rate - 1 : 0;
  mp3->filePos = mp3->readFilePos = mp3->start + offset;
  mp3->readPos = mp3->writePos = mp3->available = mp3->pcmHalf = 0;
  mp3->writeLimit = mp3Area(*mp3);
  mp3->sumDecoded = frame * Mp3Samples;
  if(mp3->decoder) mp3->decoder->reset();
  result(0);
}

auto Kernel::sceMp3ResetPlayPosition() -> void {
  auto mp3 = mp3Find(arg(0));
  if(!mp3) return;
  if(!mp3->initialized) return result(Mp3ErrorNotInitialized);
  mp3->filePos = mp3->readFilePos = mp3->start;
  mp3->readPos = mp3->writePos = mp3->available = mp3->pcmHalf = 0;
  mp3->writeLimit = mp3Area(*mp3);
  mp3->sumDecoded = 0;
  if(mp3->decoder) mp3->decoder->reset();
  result(0);
}
