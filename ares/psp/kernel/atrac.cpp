//sceAtrac3plus, the library games play their music and longer sounds with: ATRAC3 and ATRAC3plus (the MiniDisc's
//codec and its successor) in RIFF WAVE files. A game hands the library a file, whole or as much as fits a buffer it
//keeps refilling, and asks for a frame of samples at a time; the library finds the frames, loops where the file says,
//and tells the game what part of the file to read in next. The frames themselves are decoded by codec.cpp's decoder
//(FFmpeg's); without one, every file is refused as one the library can't read, and games go without their music, as
//they did before there was one. What the library does is pspsdk's pspatrac3.h and pspautotests' audio/atrac tests
//with the results they recorded on a PSP (the ATRAC3plus file sample.at3 there, frame by frame); no other emulator's
//code was read.
//
//Positions. The library counts samples as its decoder makes them, from the first frame's first sample: "the decoder's
//count" here (the context's decodePos, endSample, loop points). Games see the file's own count, which starts at its
//first sample worth hearing (sceAtracGetNextDecodePosition, sceAtracGetSoundSample): firstValidSample on the
//decoder's count, the fact chunk's sample offset (the encoder's frame of delay; 0 counts as a whole frame) plus the
//decoder's own delay, 368 samples for ATRAC3plus and 69 for ATRAC3 (audio/atrac/headerfields: a header whose samples,
//offset and delay need more frames than its data chunk holds is refused). sample.at3's first sample worth hearing is
//so the decoder's 2416th (2048 + 368), the 368th of its second frame, and its first decoded frame gives 1680 samples
//(decode). A smpl chunk's loop points count the encoder's samples: less the offset they're the file's count
//(getsoundsample), plus the delay the decoder's.
//
//Priming. A frame's samples depend on the frame before it, so to start at a sample the library first decodes,
//unheard, the frames from the one before the frame where the sample falls when the delay is taken off: one frame, or
//two when the sample lies within the delay of its frame's start (resetting: sample 1679 of sample.at3 needs its
//frames 0 and 1, sample 1680 frames 0 to 2). Setting a file and resetting the position decode them at once (the
//frames are then used up: the context's next frame is the sample's own); a loop's jump leaves them to the next
//decode (framesToSkip).
//
//Buffers (the context's state):
//- The whole file in one buffer (2): frames are read where they lie, the buffer standing for the file.
//- A buffer that will hold the whole file, partly filled (3, sceAtracSetHalfwayBuffer with room for it): the same,
//  the game adding the rest as it goes, a frame decodable once it's all there.
//- A buffer smaller than the file (4 without a loop, 5 looping at the file's last frame, 6 looping before it): a ring
//  of frames. The first time round it runs from the file's frames as they lie in the buffer to the last whole frame
//  that fits; every time after, from the buffer's start, as many whole frames as fit. What the game loaded past the
//  first time round's end is moved to the buffer's start (stream: a 0x4500-byte buffer of sample.at3 ends its first
//  time round at 0x43f0 and is next written at 0x110). The game is told where to write, how much (to the end of the
//  time round, or to the frames still to be decoded, and no more than the rest of the file) and from where in the
//  file; at the end of the file (or, with a second buffer, of the loop's last frame) the file goes on from the frames
//  priming the loop's start, as long as the file loops, which decoding finds there when it jumps back. With a loop
//  before the end, a second buffer holds the frames after the loop's last one, which decoding goes on to once the
//  loops are done.
//
//sceAtracDecodeData, setting a file and resetting the position decode on the PSP's Media Engine, and the caller
//waits meanwhile (the tests' checkpoints saw other threads run during each): here the calling thread waits
//AtracDecodeMicroseconds (chosen: the PSP's time isn't measured) after a call that decoded, when it may wait and the
//call returns 0. sceAtracSetDataAndGetID, returning an ID, doesn't wait.

namespace {
  //sceAtrac3plus's errors, as audio/atrac's tests recorded them
  constexpr u32 AtracErrorFailed = 0x8063'0002;           //a frame that won't decode; a reset to before the file
  constexpr u32 AtracErrorNoID = 0x8063'0003;             //no ID free for the codec
  constexpr u32 AtracErrorBadID = 0x8063'0005;            //not an ID handed out
  constexpr u32 AtracErrorUnknownFormat = 0x8063'0006;    //not a file the library reads
  constexpr u32 AtracErrorWrongCodec = 0x8063'0007;       //a file of the other codec than the ID's
  constexpr u32 AtracErrorBadParameters = 0x8063'0008;    //a header promising more samples than its frames hold
  constexpr u32 AtracErrorAllLoaded = 0x8063'0009;        //no more of the file wanted
  constexpr u32 AtracErrorNoData = 0x8063'0010;           //an ID with no file set
  constexpr u32 AtracErrorTooSmall = 0x8063'0011;         //a buffer, or a second buffer, too small
  constexpr u32 AtracErrorSecondNeeded = 0x8063'0012;     //decoding on after the loops with no second buffer
  constexpr u32 AtracErrorBadSample = 0x8063'0015;        //a position outside the file
  constexpr u32 AtracErrorBadFirstSize = 0x8063'0016;     //a reset given the wrong amount of data
  constexpr u32 AtracErrorBadSecondSize = 0x8063'0017;
  constexpr u32 AtracErrorTooMuch = 0x8063'0018;          //more added than there was room for
  constexpr u32 AtracErrorNotMono = 0x8063'0019;          //mono output asked of a stereo file
  constexpr u32 AtracErrorNoLoop = 0x8063'0021;           //a loop count for a file that doesn't loop
  constexpr u32 AtracErrorSecondNotNeeded = 0x8063'0022;
  constexpr u32 AtracErrorEmpty = 0x8063'0023;            //no frame in the buffer to decode
  constexpr u32 AtracErrorAllDecoded = 0x8063'0024;
  constexpr u32 AtracCodecError = 0x20b;                  //sceAtracGetInternalErrorInfo after a bad frame (stream)
  constexpr u32 AtracPlus = 0x1000, AtracClassic = 0x1001;
  constexpr u32 AtracIDs = 6;
  enum : u32 { NoData = 0, AllLoaded = 2, Halfway = 3, Streamed = 4, StreamedLoopAtEnd = 5, StreamedWithSecond = 6 };
  constexpr u32 AtracDecodeMicroseconds = 300;
  //ATRAC3plus in a WAVE_FORMAT_EXTENSIBLE fmt chunk: its subformat's GUID, E923AABF-CB58-4471-A119-FFFA01E4CE62
  constexpr u8 Atrac3plusGuid[16] = {0xbf, 0xaa, 0x23, 0xe9, 0x58, 0xcb, 0x71, 0x44,
                                     0xa1, 0x19, 0xff, 0xfa, 0x01, 0xe4, 0xce, 0x62};

  auto streamed(u32 state) -> bool { return state >= Streamed && state <= StreamedWithSecond; }
}

//Where a frame lies in the file.
static auto atracFrameOffset(const Kernel::Atrac& a, s64 frame) -> s64 {
  return a.dataOff + frame * a.frameBytes;
}

//The frames priming a sample on the decoder's count (above): the first (-1 for a sample in the file's first frame's
//delay, which has none before it) and the sample's own.
static auto atracPriming(const Kernel::Atrac& a, s64 raw, s64& first, s64& own) -> void {
  s64 shifted = raw - a.delay;
  first = (shifted >= 0 ? shifted / a.frameSamples : -((-shifted + a.frameSamples - 1) / a.frameSamples)) - 1;
  own = raw / a.frameSamples;
}

//The last sample decoding plays before it jumps back or ends. While loops are left (and decoding hasn't passed the
//loop's end), the last of the frame where the loop ends: the PSP jumps back a frame at a time (stream: a loop
//ending 16 samples into its frame plays that frame whole, then the loop's start), but never past the file's end.
static auto atracLast(const Kernel::Atrac& a, bool& looping) -> u32 {
  looping = a.looped && a.loopNum != 0 && a.decodePos <= a.loopEnd && a.curBuffer == 0;
  u32 frameEnd = (a.loopEnd / a.frameSamples + 1) * a.frameSamples - 1;
  return looping ? std::min(frameEnd, a.endSample) : a.endSample;
}

//Where the ring's time round ends: the first time from the frames as the header put them, then from its start.
static auto atracLapEnd(const Kernel::Atrac& a, u32 lap) -> u32 {
  return lap ? a.lapEnd : a.firstEnd;
}

//Where the game's bytes stop coming from the file's order: its end, or with a second buffer the end of the loop's
//last frame.
static auto atracWriteEnd(const Kernel::Atrac& a) -> u32 {
  if(a.state != StreamedWithSecond) return a.fileDataEnd;
  return atracFrameOffset(a, a.loopEnd / a.frameSamples + 1);
}

//The first frame of the second buffer's: the one after the loop's last frame.
static auto atracSecondStart(const Kernel::Atrac& a) -> u32 {
  return atracFrameOffset(a, a.loopEnd / a.frameSamples + 1);
}

//What a stream's buffer needs to start decoding again at a sample whose priming frames start at first and whose own
//frame is own: from where in the file, how much at least (those frames, or those before the second buffer's with
//one), how much at most (the whole frames that fit, and what the file has before it goes back to the loop). Frames
//all in the second buffer need nothing in the first.
struct AtracReload { s64 from = 0, least = 0, most = 0; };
static auto atracReload(const Kernel::Atrac& a, s64 first, s64 own) -> AtracReload {
  s64 last = own;
  if(a.state == StreamedWithSecond) last = std::min<s64>(own, a.loopEnd / a.frameSamples);
  if(last < first) return {};
  s64 from = atracFrameOffset(a, first);
  return {from, (last - first + 1) * a.frameBytes,
          std::min<s64>(a.bufferByte / a.frameBytes * a.frameBytes, atracWriteEnd(a) - from)};
}

//Where a stream goes on from at the end of the file (or of the loop's last frame, with a second buffer): the frames
//priming the loop's start, from where in the file and how many.
static auto atracRestart(const Kernel::Atrac& a, u32& count) -> u32 {
  s64 first, own;
  atracPriming(a, a.loopStart, first, own);
  first = std::max<s64>(first, 0);
  count = own - first;
  return atracFrameOffset(a, first);
}

//An ID handed out, holding a file when needData; else the error is the result.
auto Kernel::atracFind(u32 id, bool needData) -> Atrac* {
  if(id >= AtracIDs || !atracs[id].codec) return result(AtracErrorBadID), nullptr;
  if(needData && atracs[id].state == NoData) return result(AtracErrorNoData), nullptr;
  return &atracs[id];
}

//The file's header, read from size bytes of memory at buffer into atrac: its codec, channels, frames, the samples
//worth hearing and the loop. 0, or why the library refuses it.
auto Kernel::atracParse(Atrac& a, u32 buffer, u32 size) -> u32 {
  if(!size) return AtracErrorTooSmall;
  if(!memory.reaches(buffer, size)) return AtracErrorUnknownFormat;
  auto byte = [&](u32 at) -> u32 { return at < size ? memory.read(1, buffer + at) : 0; };
  auto half = [&](u32 at) -> u32 { return byte(at) | byte(at + 1) << 8; };
  auto word = [&](u32 at) -> u32 { return half(at) | half(at + 2) << 16; };
  if(word(0) != 0x4646'4952 || word(8) != 0x4556'4157) return AtracErrorUnknownFormat;  //"RIFF", "WAVE"
  bool format = false, fact = false;
  u32 factSamples = 0, factOffset = 0, smplStart = 0, smplEnd = 0, dataSize = 0, rate = 0;
  a.looped = false;
  //(the chunks walked in 64 bits: a chunk's length, up to 4 GiB, always takes the walk on past it)
  for(u64 at = 12;;) {
    if(at + 8 > size) return format ? AtracErrorTooSmall : AtracErrorUnknownFormat;
    u32 id = word(at), length = word(at + 4), body = at + 8;
    if(id == 0x2074'6d66) {  //"fmt "
      u32 tag = half(body), extraSize = length >= 18 ? half(body + 16) : 0;
      u32 extraStart = body + 18, extraEnd = body + std::min(length, 18 + extraSize);
      a.channels = half(body + 2);
      rate = word(body + 4);
      a.frameBytes = half(body + 12);
      bool plus = tag == 0xfffe && length >= 40;
      for(u32 n = 0; plus && n < 16; n++) plus = byte(body + 24 + n) == Atrac3plusGuid[n];
      if(tag == 0x270) a.codec = AtracClassic;
      else if(plus) a.codec = AtracPlus, extraStart = body + 40;
      else return AtracErrorUnknownFormat;
      a.extra.clear();
      for(u32 n = extraStart; n < extraEnd && a.extra.size() < AudioDecoder::Format::MaxExtra; n++) {
        a.extra.push_back(byte(n));
      }
      format = true;
    } else if(id == 0x7463'6166) {  //"fact"
      factSamples = word(body);
      factOffset = length >= 8 ? word(body + 4) : 0;
      fact = true;
    } else if(id == 0x6c70'6d73) {  //"smpl": its first loop
      if(length >= 52 && word(body + 28)) {
        a.looped = true;
        smplStart = word(body + 44);
        smplEnd = word(body + 48);
      }
    } else if(id == 0x6174'6164) {  //"data"
      a.dataOff = body;
      dataSize = length;
      break;
    }
    at = u64(body) + length + (length & 1);
  }
  if(!format || !a.frameBytes || a.channels < 1 || a.channels > 2) return AtracErrorUnknownFormat;
  bool plus = a.codec == AtracPlus;
  a.frameSamples = plus ? 2048 : 1024;
  a.delay = plus ? 368 : 69;
  //ATRAC3 takes its decoding from the frame size and joint stereo alone: frames of 0xc0 bytes not in joint stereo
  //are mono, as LocoRoco 2 streams its house music under a two-channel header (audio/atrac/c0mono)
  a.monoFrames = !plus && a.frameBytes == 0xc0 && a.extra.size() >= 8 && !a.extra[6];
  //(counted in 64 bits: a file whose data's end, or whose samples' or loop's, a 32-bit count can't hold is refused)
  u64 frames = dataSize / a.frameBytes, samples = frames * a.frameSamples;
  u64 offset = factOffset ? factOffset : a.frameSamples, heard = factSamples;
  if(!fact) heard = samples > offset + a.delay ? samples - offset - a.delay : 0;
  if(heard + offset + a.delay > samples) return AtracErrorBadParameters;
  if(u64(a.dataOff) + dataSize > 0xffff'ffff) return AtracErrorBadParameters;
  if(heard + offset + a.delay > 0xffff'ffff) return AtracErrorBadParameters;
  if(a.looped && u64(std::max(smplStart, smplEnd)) + a.delay > 0xffff'ffff) return AtracErrorBadParameters;
  factSamples = heard;
  a.fileDataEnd = a.dataOff + dataSize;
  a.firstValidSample = offset + a.delay;
  a.endSample = a.firstValidSample + factSamples - 1;
  a.loopStart = a.looped ? smplStart + a.delay : 0;
  a.loopEnd = a.looped ? smplEnd + a.delay : 0;
  (void)rate;
  return 0;
}

//Sets a file on an ID handed out for its codec: readSize bytes of it at buffer, in a buffer of bufferSize bytes
//(sceAtracSetData's buffer is its data), the output mono if mono. 0, or why it's refused.
auto Kernel::atracSet(u32 id, u32 buffer, u32 readSize, u32 bufferSize, bool mono) -> u32 {
  if(!audioDecoders) return AtracErrorUnknownFormat;
  Atrac a;
  if(u32 error = atracParse(a, buffer, readSize)) return error;
  if(a.codec != atracs[id].codec) return AtracErrorWrongCodec;
  if(mono && a.channels != 1) return AtracErrorNotMono;
  AudioDecoder::Format format;
  format.codec = a.codec == AtracPlus ? AudioDecoder::Codec::Atrac3plus : AudioDecoder::Codec::Atrac3;
  format.channels = a.monoFrames ? 1 : a.channels;
  format.frameBytes = a.frameBytes;
  format.extra = a.extra;
  a.decoder = audioDecoders(format);
  if(!a.decoder) return AtracErrorBadParameters;
  a.buffer = buffer;
  a.bufferByte = bufferSize;
  a.outputChannels = mono ? 1 : 2;
  if(readSize >= a.fileDataEnd) a.state = AllLoaded;
  else if(bufferSize >= a.fileDataEnd) a.state = Halfway;
  else if(!a.looped) a.state = Streamed;
  else if(a.loopEnd >= a.endSample) a.state = StreamedLoopAtEnd;
  else a.state = StreamedWithSecond;
  if(streamed(a.state) && bufferSize < a.dataOff + a.frameBytes) return AtracErrorTooSmall;
  a.loaded = std::min(readSize, a.fileDataEnd);
  a.decodePos = a.firstValidSample;
  s64 first, own;
  atracPriming(a, a.decodePos, first, own);
  first = std::max<s64>(first, 0);
  a.curFileOff = atracFrameOffset(a, first);
  a.framesToSkip = own - first;
  a.streamOff = a.curFileOff;
  if(streamed(a.state)) {
    a.firstEnd = a.dataOff + (bufferSize - a.dataOff) / a.frameBytes * a.frameBytes;
    a.lapEnd = bufferSize / a.frameBytes * a.frameBytes;
    a.streamDataByte = readSize > a.streamOff ? readSize - a.streamOff : 0;
    a.writeOff = readSize;
    a.writeFileOff = readSize;
  } else {
    a.streamOff = a.dataOff;
    a.streamDataByte = a.loaded - std::min(a.loaded, a.dataOff);
  }
  u32 codec = a.codec;
  auto& set = atracs[id];
  set = std::move(a);
  //the priming frames decoded; one that won't refuses the file (as the PSP refuses a header of ATRAC3 parameters
  //it hasn't got, audio/atrac/c0mono's joint stereo over mono frames)
  if(!atracPrime(set) && set.error) {
    set = {};
    set.codec = codec;
    return AtracErrorFailed;
  }
  if(streamed(set.state)) {
    //the ring: what was loaded past its first time round goes to its start (over the header and priming frames,
    //used up by now)
    if(readSize >= set.firstEnd) {
      u32 over = readSize - set.firstEnd;
      std::vector<u8> moved(over);
      memory.copyOut(moved.data(), buffer + set.firstEnd, over);
      memory.copyIn(buffer, moved.data(), over);
      set.writeOff = over;
      set.writeLap = 1;
    }
    atracWriterAt(set);
  }
  return 0;
}

//The next frame to decode: its address, or why there's none.
auto Kernel::atracFrame(Atrac& a, u32& address) -> u32 {
  if(a.curBuffer) {
    if(!a.secondBuffer && !a.secondBufferByte) return AtracErrorSecondNeeded;
    if(a.secondStreamOff + a.frameBytes > a.secondBufferByte) return AtracErrorEmpty;
    address = a.secondBuffer + a.secondStreamOff;
  } else if(streamed(a.state)) {
    if(a.streamDataByte < a.frameBytes) return AtracErrorEmpty;
    address = a.buffer + a.streamOff;
  } else {
    u32 end = a.state == Halfway ? a.loaded : a.fileDataEnd;
    if(u64(a.curFileOff) + a.frameBytes > end) return AtracErrorEmpty;
    address = a.buffer + a.curFileOff;
  }
  if(!memory.reaches(address, a.frameBytes)) return AtracErrorEmpty;
  return 0;
}

//The frame just decoded used up: the next one's place in the file and the buffer.
auto Kernel::atracConsume(Atrac& a) -> void {
  a.curFileOff += a.frameBytes;
  if(a.curBuffer) {
    a.secondStreamOff += a.frameBytes;
    a.streamDataByte = a.streamDataByte > a.frameBytes ? a.streamDataByte - a.frameBytes : 0;
  } else if(streamed(a.state)) {
    a.streamOff += a.frameBytes;
    a.streamDataByte -= a.frameBytes;
    if(a.streamOff >= atracLapEnd(a, a.readLap)) a.streamOff = 0, a.readLap++;
  }
  //with a second buffer, decoding that goes on past the loop's last frame (the loops done, or started past the
  //loop's end) goes on there
  bool looping = a.looped && a.loopNum != 0 && a.decodePos <= a.loopEnd;
  if(a.state == StreamedWithSecond && !a.curBuffer && !looping && a.curFileOff == atracSecondStart(a)) {
    a.curBuffer = 1;
    a.secondStreamOff = 0;
    a.streamDataByte = a.fileDataEnd - a.curFileOff;  //the rest of the file, which the second buffer holds
  }
}

//The next frame decoded into samples (the decoder's channels, interleaved): the samples a channel, or -1 if it won't
//decode (nothing is used up then). A decoder made afresh (after a state was loaded) is primed first with the frame
//decoded last, as a reset would prime it.
auto Kernel::atracDecodeFrame(Atrac& a, s16* samples) -> s32 {
  u32 address = 0;
  if(atracFrame(a, address)) return -1;
  std::vector<u8> frame(a.frameBytes);
  memory.copyOut(frame.data(), address, a.frameBytes);
  if(!a.decoder) {
    if(!audioDecoders) return -1;
    AudioDecoder::Format format;
    format.codec = a.codec == AtracPlus ? AudioDecoder::Codec::Atrac3plus : AudioDecoder::Codec::Atrac3;
    format.channels = a.monoFrames ? 1 : a.channels;
    format.frameBytes = a.frameBytes;
    format.extra = a.extra;
    a.decoder = audioDecoders(format);
    if(!a.decoder) return -1;
    if(!a.recent.empty()) a.decoder->decode(a.recent.data(), a.recent.size(), samples, a.frameSamples);
  }
  s32 made = a.decoder->decode(frame.data(), frame.size(), samples, a.frameSamples);
  if(made < 0) {
    a.error = AtracCodecError;
    return -1;
  }
  a.error = 0;
  a.recent = std::move(frame);
  u32 channels = a.monoFrames ? 1 : a.channels;
  for(u32 n = made * channels; n < a.frameSamples * channels; n++) samples[n] = 0;  //a short frame: silence after
  return a.frameSamples;
}

//Decodes, unheard, the frames priming the next sample (framesToSkip): true once they're done; false if one isn't in
//the buffer yet, or won't decode (it's left for the next try).
auto Kernel::atracPrime(Atrac& a) -> bool {
  std::vector<s16> samples(a.frameSamples * 2);
  while(a.framesToSkip) {
    u32 address = 0;
    if(atracFrame(a, address) || atracDecodeFrame(a, samples.data()) < 0) return false;
    atracConsume(a);
    a.framesToSkip--;
  }
  return true;
}

//The frames left to decode in the buffer, or what's in their place (getremainframe, stream): -1 with the whole file
//in the buffer; -2 a stream that has all of the rest of itself in the buffer; -3 the same for a looping stream with
//no loops left (the game may still add the loop's start, which it's offered, should it loop again).
auto Kernel::atracRemain(Atrac& a) -> s32 {
  if(a.state == AllLoaded) return -1;
  if(a.state == Halfway) {
    if(a.loaded >= a.fileDataEnd) return -1;
    return a.loaded > a.curFileOff ? (a.loaded - a.curFileOff) / a.frameBytes : 0;
  }
  if(a.state == Streamed && a.writeFileOff >= a.fileDataEnd) return -2;
  if(a.curBuffer) return -2;  //the rest in the second buffer
  if(a.loopNum == 0 && a.loopsAhead) return -3;
  //the frames priming a loop's start, the next decode's or those the game streamed after the loop's end, aren't
  //counted (stream: sample.at3 looping streamed, 36 frames in the buffer, one of them priming the loop, are 35)
  s64 frames = a.streamDataByte / a.frameBytes, priming = 0;
  if(a.loopsAhead) {
    u32 count = 0, from = atracRestart(a, count);
    u32 since = a.writeFileOff >= from ? a.writeFileOff - from : 0;
    priming = s64(a.loopsAhead - 1) * count + std::min(since / a.frameBytes, count);
  }
  return std::max<s64>(frames - priming - a.framesToSkip, 0);
}

//How many bytes the game may add to a stream now: to the end of the ring's time round, or up to the frames still to
//be decoded, and no more than the file has before it goes back to the loop.
auto Kernel::atracWritable(Atrac& a) -> u32 {
  if(a.state == Streamed && a.writeFileOff >= a.fileDataEnd) return 0;
  u32 end = atracLapEnd(a, a.writeLap);
  u32 room = 0;
  if(a.writeLap == a.readLap) room = end > a.writeOff ? end - a.writeOff : 0;
  else room = std::min(a.streamOff, end) > a.writeOff ? std::min(a.streamOff, end) - a.writeOff : 0;
  u32 fileEnd = atracWriteEnd(a);
  return std::min(room, fileEnd > a.writeFileOff ? fileEnd - a.writeFileOff : 0);
}

//The game's bytes reached the end of what the stream takes from the file in order: a looping stream goes on from the
//frames priming the loop's start.
auto Kernel::atracWriterAt(Atrac& a) -> void {
  if(a.state == Streamed || a.writeFileOff < atracWriteEnd(a)) return;
  u32 count = 0;
  a.writeFileOff = atracRestart(a, count);
  a.loopsAhead++;
}

//The samples the next decode gives.
auto Kernel::atracNextSamples(Atrac& a) -> u32 {
  if(a.ended) return 0;
  bool looping;
  u32 last = atracLast(a, looping);
  u32 count = a.frameSamples - a.decodePos % a.frameSamples;
  if(u64(a.decodePos) + count > u64(last) + 1) count = last >= a.decodePos ? last - a.decodePos + 1 : 0;
  return count;
}

//An ID's context, where _sceAtracGetContextAddress showed it: 128 bytes of its decoder's (the last error, 8 bytes
//in), then 128 of its own, laid out as pspautotests' atrac.h reads them.
auto Kernel::atracContext(u32 id) -> void {
  if(!atracContexts || id >= AtracIDs) return;
  u32 at = atracContexts + id * 256;
  for(u32 n = 0; n < 256; n += 4) memory.write(4, at + n, 0);
  auto& a = atracs[id];
  memory.write(4, at + 8, a.error);
  at += 128;
  u32 words[] = {a.decodePos, a.endSample, a.loopStart, a.loopEnd, a.firstValidSample};
  for(u32 n = 0; n < 5; n++) memory.write(4, at + n * 4, words[n]);
  memory.write(1, at + 20, a.framesToSkip);
  memory.write(1, at + 21, a.state == NoData ? 1 : a.state);
  memory.write(1, at + 22, a.curBuffer);
  memory.write(1, at + 23, a.channels);
  memory.write(2, at + 24, a.frameBytes);
  memory.write(2, at + 26, a.state == NoData ? 0 : a.codec);
  u32 more[] = {a.dataOff, a.curFileOff, a.fileDataEnd, u32(a.loopNum), a.streamDataByte, a.streamOff,
                a.secondStreamOff, a.buffer, a.secondBuffer, a.bufferByte, a.secondBufferByte};
  for(u32 n = 0; n < 11; n++) memory.write(4, at + 28 + n * 4, more[n]);
  memory.write(4, at + 124, id);
}

//(codec: 0x1000 ATRAC3plus, 0x1001 ATRAC3): the lowest ID free of those sceAtracReinit gave the codec (two each at
//first: ATRAC3plus 0 and 1, ATRAC3 2 and 3, audio/atrac/ids).
auto Kernel::sceAtracGetAtracID() -> void {
  u32 codec = arg(0);
  if(codec != AtracPlus && codec != AtracClassic) return result(ErrorInvalidValue);
  u32 from = codec == AtracPlus ? 0 : atracPlusIDs, to = codec == AtracPlus ? atracPlusIDs : from + atracClassicIDs;
  for(u32 id = from; id < to; id++) {
    if(atracs[id].codec) continue;
    atracs[id] = {};
    atracs[id].codec = codec;
    atracContext(id);
    return result(id);
  }
  result(AtracErrorNoID);
}

//(ID): given back, with its file.
auto Kernel::sceAtracReleaseAtracID() -> void {
  u32 id = arg(0);
  if(!atracFind(id, false)) return;
  atracs[id] = {};
  atracContext(id);
  result(0);
}

//(ATRAC3 IDs, ATRAC3plus IDs): how the six are shared. An ATRAC3plus ID takes room for two, so three at most;
//ATRAC3 takes what's left. Asking for more gives what fits and fails with 0x80000022 all the same; refused while any
//is handed out (BUSY), as audio/atrac/ids recorded; negative counts are none.
auto Kernel::sceAtracReinit() -> void {
  for(auto& a : atracs) if(a.codec) return result(ErrorBusy);
  s32 classic = std::max(s32(arg(0)), 0), plus = std::max(s32(arg(1)), 0);
  atracPlusIDs = std::min(plus, 3);
  atracClassicIDs = std::min<s32>(classic, AtracIDs - 2 * atracPlusIDs);
  result(u32(plus) > atracPlusIDs || u32(classic) > atracClassicIDs ? ErrorOutOfMemory : 0);
}

//The setting functions, sharing atracSet(): on an ID (refused for one not handed out, or of the other codec), or
//on an ID of the file's codec they hand out (none free: 0x80630003). Setting decodes the frames priming the file's
//first sample.
auto Kernel::sceAtracSetData() -> void {
  u32 id = arg(0);
  if(!atracFind(id, false)) return;
  u32 error = atracSet(id, arg(1), arg(2), arg(2), false);
  atracContext(id);
  result(error);
  if(!error) codecWait(AtracDecodeMicroseconds);
}

auto Kernel::sceAtracSetHalfwayBuffer() -> void {
  u32 id = arg(0);
  if(!atracFind(id, false)) return;
  u32 error = atracSet(id, arg(1), std::min(arg(2), arg(3)), arg(3), false);
  atracContext(id);
  result(error);
  if(!error) codecWait(AtracDecodeMicroseconds);
}

auto Kernel::sceAtracSetMOutData() -> void {
  u32 id = arg(0);
  if(!atracFind(id, false)) return;
  u32 error = atracSet(id, arg(1), arg(2), arg(2), true);
  atracContext(id);
  result(error);
  if(!error) codecWait(AtracDecodeMicroseconds);
}

auto Kernel::sceAtracSetMOutHalfwayBuffer() -> void {
  u32 id = arg(0);
  if(!atracFind(id, false)) return;
  u32 error = atracSet(id, arg(1), std::min(arg(2), arg(3)), arg(3), true);
  atracContext(id);
  result(error);
  if(!error) codecWait(AtracDecodeMicroseconds);
}

//(buffer, read size, buffer size, mono): the ID of the file's codec it's set on.
static auto atracSetAndGetID(Kernel& kernel, u32 buffer, u32 readSize, u32 bufferSize, bool mono) -> u32 {
  Kernel::Atrac parsed;
  if(!kernel.audioDecoders) return AtracErrorUnknownFormat;
  if(u32 error = kernel.atracParse(parsed, buffer, readSize)) return error;
  u32 from = parsed.codec == AtracPlus ? 0 : kernel.atracPlusIDs;
  u32 to = parsed.codec == AtracPlus ? kernel.atracPlusIDs : from + kernel.atracClassicIDs;
  for(u32 id = from; id < to; id++) {
    if(kernel.atracs[id].codec) continue;
    kernel.atracs[id] = {};
    kernel.atracs[id].codec = parsed.codec;
    if(u32 error = kernel.atracSet(id, buffer, readSize, bufferSize, mono)) {
      kernel.atracs[id] = {};
      return error;
    }
    kernel.atracContext(id);
    return id;
  }
  return AtracErrorNoID;
}

auto Kernel::sceAtracSetDataAndGetID() -> void {
  result(atracSetAndGetID(*this, arg(0), arg(1), arg(1), false));
}

auto Kernel::sceAtracSetHalfwayBufferAndGetID() -> void {
  result(atracSetAndGetID(*this, arg(0), std::min(arg(1), arg(2)), arg(2), false));
}

auto Kernel::sceAtracSetMOutDataAndGetID() -> void {
  result(atracSetAndGetID(*this, arg(0), arg(1), arg(1), true));
}

auto Kernel::sceAtracSetMOutHalfwayBufferAndGetID() -> void {
  result(atracSetAndGetID(*this, arg(0), std::min(arg(1), arg(2)), arg(2), true));
}

//(ID, samples, where to put how many, whether that was the end, and the frames left): a frame's samples, 16 bits,
//interleaved stereo (or mono for the mono output), from where decoding is to the frame's end, the loop's end while
//loops are left (it then jumps back to the loop's start, its priming frames left for the next decode), or the file's
//(the end flag set). No buffer to write to still decodes (decode). After the end: 0x80630024, no samples, the end
//flag set; a frame that won't decode: 0x80630002, nothing used up; none in the buffer: 0x80630023.
auto Kernel::sceAtracDecodeData() -> void {
  u32 id = arg(0), output = arg(1), count = arg(2), finish = arg(3), remain = arg(4);
  auto a = atracFind(id);
  if(!a) return;
  auto answer = [&](u32 samples, bool ended) {
    if(memory.reaches(count, 4)) memory.write(4, count, samples);
    if(memory.reaches(finish, 4)) memory.write(4, finish, ended);
    if(memory.reaches(remain, 4)) memory.write(4, remain, atracRemain(*a));
    atracContext(id);
  };
  if(a->ended) return answer(0, true), result(AtracErrorAllDecoded);
  std::vector<s16> samples(a->frameSamples * 2);
  if(!atracPrime(*a) || atracDecodeFrame(*a, samples.data()) < 0) {
    u32 address = 0;
    u32 missing = atracFrame(*a, address);
    answer(0, false);
    return result(missing ? missing : AtracErrorFailed);
  }
  bool looping;
  u32 last = atracLast(*a, looping);
  u32 from = a->decodePos % a->frameSamples, made = atracNextSamples(*a);
  if(output && made && memory.reaches(output, made * a->outputChannels * 2)) {
    u32 channels = a->monoFrames ? 1 : a->channels;
    std::vector<s16> out(made * a->outputChannels);
    for(u32 n = 0; n < made; n++) {
      s16 left = samples[(from + n) * channels], right = samples[(from + n) * channels + channels - 1];
      if(a->outputChannels == 1) out[n] = left;
      else out[n * 2] = left, out[n * 2 + 1] = right;
    }
    memory.copyIn(output, out.data(), out.size() * 2);
  }
  atracConsume(*a);
  a->decodePos += made;
  if(a->decodePos > last) {
    if(looping) {
      //back to the loop's start: its priming frames are next (in a stream, the game put them after the loop's end)
      a->decodePos = a->loopStart;
      if(a->loopNum > 0) a->loopNum--;
      s64 first, own;
      atracPriming(*a, a->loopStart, first, own);
      first = std::max<s64>(first, 0);
      a->curFileOff = atracFrameOffset(*a, first);
      a->framesToSkip = own - first;
      if(streamed(a->state) && a->loopsAhead) a->loopsAhead--;
      if(a->decoder) a->decoder->reset();
    } else {
      a->ended = true;
      //ended in the second buffer: the context says 2, its places cleared (stream)
      if(a->curBuffer) a->curBuffer = 2, a->streamOff = 0, a->secondStreamOff = 0;
    }
  } else if(a->decodePos % a->frameSamples) {
    //decoding stopped inside a frame it used up: only a loop's end or the file's does that, handled above
    a->decodePos += a->frameSamples - a->decodePos % a->frameSamples;
  }
  answer(made, a->ended);
  result(0);
  codecWait(AtracDecodeMicroseconds);
}

//(ID, where to put the frames left): atracRemain().
auto Kernel::sceAtracGetRemainFrame() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), atracRemain(*a));
  result(0);
}

//(ID, where to put where to write, how much, and from where in the file): what the game should load next. With the
//whole file in the buffer, or all of a stream there (and no loop to go back to), nothing: the buffer, 0 and 0.
auto Kernel::sceAtracGetStreamDataInfo() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  u32 at = a->buffer, bytes = 0, from = 0;
  if(a->state == Halfway && a->loaded < a->fileDataEnd) {
    at = a->buffer + a->loaded;
    bytes = std::min(a->fileDataEnd, a->bufferByte) - a->loaded;
    from = a->loaded;
  } else if(streamed(a->state) && !a->curBuffer && !(a->state == Streamed && a->writeFileOff >= a->fileDataEnd)) {
    at = a->buffer + a->writeOff;
    bytes = atracWritable(*a);
    from = a->writeFileOff;
  }
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), at);
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), bytes);
  if(memory.reaches(arg(3), 4)) memory.write(4, arg(3), from);
  result(0);
}

//(ID, bytes): the game loaded that much where it was told. More than there was room for: 0x80630018; with the whole
//file in the buffer, anything (0 too): 0x80630009 (addstreamdata). A stream with all of itself in still takes 0.
auto Kernel::sceAtracAddStreamData() -> void {
  u32 id = arg(0), bytes = arg(1);
  auto a = atracFind(id);
  if(!a) return;
  if(a->state == AllLoaded) return result(AtracErrorAllLoaded);
  if(a->state == Halfway) {
    if(a->loaded >= a->fileDataEnd) return result(AtracErrorAllLoaded);
    if(bytes > a->fileDataEnd - a->loaded) return result(AtracErrorTooMuch);
    a->loaded += bytes;
    a->streamDataByte = a->loaded - a->dataOff;
    if(a->loaded >= a->fileDataEnd) a->state = AllLoaded;  //all of it there now (stream: the context says 2)
  } else {
    //(audio/atrac/replay adds 0 to a stream with all of itself in, at every frame)
    if(bytes > atracWritable(*a)) return result(AtracErrorTooMuch);
    a->streamDataByte += bytes;
    a->writeOff += bytes;
    if(a->writeOff >= atracLapEnd(*a, a->writeLap)) a->writeOff = 0, a->writeLap++;
    a->writeFileOff += bytes;
    if(bytes) atracWriterAt(*a);
  }
  atracContext(id);
  result(0);
}

//(ID, where to put it): the file's count of the next sample decoded; after the end, 0x80630024.
auto Kernel::sceAtracGetNextDecodePosition() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  if(a->ended) return result(AtracErrorAllDecoded);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), a->decodePos - a->firstValidSample);
  result(0);
}

//(ID, where to put it): the samples the next decode gives (0 after the end).
auto Kernel::sceAtracGetNextSample() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), atracNextSamples(*a));
  result(0);
}

//(ID, where to put it): the most a decode gives, a frame: 2048 for ATRAC3plus, 1024 for ATRAC3.
auto Kernel::sceAtracGetMaxSample() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), a->frameSamples);
  result(0);
}

//(ID, where to put the last sample, the loop's start and its end): the file's count; -1 and -1 with no loop.
auto Kernel::sceAtracGetSoundSample() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  u32 values[3] = {a->endSample - a->firstValidSample, ~0u, ~0u};
  if(a->looped) values[1] = a->loopStart - a->firstValidSample, values[2] = a->loopEnd - a->firstValidSample;
  for(u32 n = 0; n < 3; n++) if(memory.reaches(arg(1 + n), 4)) memory.write(4, arg(1 + n), values[n]);
  result(0);
}

//(ID, where to put them): the header's channels.
auto Kernel::sceAtracGetChannel() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), a->channels);
  result(0);
}

//(ID, where to put them): the channels decoding writes.
auto Kernel::sceAtracGetOutputChannel() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), a->outputChannels);
  result(0);
}

//(ID, where to put it): kilobits a second, a frame's bits 44,100 times a second over its samples, rounded down
//(sample.at3's 376-byte frames: 64).
auto Kernel::sceAtracGetBitrate() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  u32 bitrate = u64(a->frameBytes) * 352'800 / 1000 / a->frameSamples;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), bitrate);
  result(0);
}

//(ID, loops): how many times to play the loop again (-1 for ever); a file without one refuses (0x80630021).
auto Kernel::sceAtracSetLoopNum() -> void {
  u32 id = arg(0);
  auto a = atracFind(id);
  if(!a) return;
  if(!a->looped) return result(AtracErrorNoLoop);
  a->loopNum = s32(arg(1));
  atracContext(id);
  result(0);
}

//(ID, where to put the loops left, and whether decoding may still loop: a file with a loop, until its end or until
//decoding has gone on into the second buffer, stream recorded).
auto Kernel::sceAtracGetLoopStatus() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), a->loopNum);
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), a->looped && !a->ended && !a->curBuffer);
  result(0);
}

//(ID, where to put it): the decoder's last error, 0 if its last frame decoded.
auto Kernel::sceAtracGetInternalErrorInfo() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), a->error);
  result(0);
}

//(ID, sample, where to put the buffer's information): what to load to start decoding at a sample of the file's count,
//two sets of four words (where to write, how much may be, how much must be, from where in the file), as
//audio/atrac/resetting recorded: the whole file in the buffer needs nothing; a halfway buffer, the rest of the file,
//enough of it to reach the sample's frame; a stream, its priming frames and its own (in the buffer's start, from the
//first priming frame on, as much as fits in whole frames and the file has). The second set is for the second buffer:
//nothing. A sample past the file's last, or before its first priming frame's start, is refused (0x80630015).
auto Kernel::sceAtracGetBufferInfoForResetting() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  s64 raw = s64(s32(arg(1))) + a->firstValidSample;
  if(raw < 0 || raw > a->endSample) return result(AtracErrorBadSample);
  if(a->state == StreamedWithSecond && !a->secondBuffer && !a->secondBufferByte) {
    return result(AtracErrorSecondNeeded);
  }
  s64 first, own;
  atracPriming(*a, raw, first, own);
  u32 words[8] = {a->buffer, 0, 0, 0, a->buffer, 0, 0, 0};
  if(a->state == Halfway) {
    s64 need = atracFrameOffset(*a, own + 1) - s64(a->loaded);
    words[0] = a->buffer + a->loaded;
    words[1] = a->fileDataEnd - a->loaded;
    words[2] = need > 0 ? need : 0;
    words[3] = a->loaded;
  } else if(streamed(a->state)) {
    auto reload = atracReload(*a, first, own);
    words[1] = reload.most;
    words[2] = reload.least;
    words[3] = reload.from;
  }
  if(memory.reaches(arg(2), 32)) for(u32 n = 0; n < 8; n++) memory.write(4, arg(2) + n * 4, words[n]);
  result(0);
}

//(ID, sample, bytes loaded into the buffer, bytes into the second): decoding starts again at a sample of the file's
//count, the game having loaded what sceAtracGetBufferInfoForResetting asked for; the priming frames are decoded now.
//The whole file in the buffer takes no bytes; a halfway buffer up to the rest of the file; a stream, at least its
//priming frames and its own, at most what fits (0x80630016, or 0x80630017 for bytes for the second buffer, as
//audio/atrac/resetpos recorded). A sample whose priming would start before the file fails (0x80630002).
auto Kernel::sceAtracResetPlayPosition() -> void {
  u32 id = arg(0), firstBytes = arg(2), secondBytes = arg(3);
  auto a = atracFind(id);
  if(!a) return;
  s64 raw = s64(s32(arg(1))) + a->firstValidSample;
  if(raw < 0 || raw > a->endSample) return result(AtracErrorBadSample);
  if(a->state == StreamedWithSecond && !a->secondBuffer && !a->secondBufferByte) {
    return result(AtracErrorSecondNeeded);
  }
  s64 first, own;
  atracPriming(*a, raw, first, own);
  AtracReload reload;
  if(a->state == AllLoaded) {
    if(firstBytes) return result(AtracErrorBadFirstSize);
    if(secondBytes) return result(AtracErrorBadSecondSize);
  } else if(a->state == Halfway) {
    if(firstBytes > a->fileDataEnd - a->loaded) return result(AtracErrorBadFirstSize);
    if(secondBytes) return result(AtracErrorBadSecondSize);
  } else {
    reload = atracReload(*a, first, own);
    if(firstBytes < reload.least || firstBytes > reload.most) return result(AtracErrorBadFirstSize);
    if(secondBytes) return result(AtracErrorBadSecondSize);
  }
  if(first < 0) return result(AtracErrorFailed);
  if(a->state == Halfway) a->loaded += firstBytes;
  if(a->state == Halfway && a->loaded >= a->fileDataEnd) a->state = AllLoaded;
  a->decodePos = raw;
  a->ended = false;
  a->curBuffer = 0;
  a->secondStreamOff = 0;
  a->error = 0;
  a->curFileOff = atracFrameOffset(*a, first);
  a->framesToSkip = own - first;
  if(streamed(a->state)) {
    a->firstEnd = a->lapEnd;
    a->readLap = 0;
    a->streamOff = 0;
    a->streamDataByte = firstBytes;
    a->writeOff = firstBytes;
    a->writeLap = 0;
    if(a->writeOff >= a->firstEnd) a->writeOff = 0, a->writeLap = 1;
    a->writeFileOff = reload.least ? reload.from + firstBytes : atracWriteEnd(*a);
    a->loopsAhead = 0;
    atracWriterAt(*a);
    if(!reload.least) {  //every frame in the second buffer
      a->curBuffer = 1;
      a->secondStreamOff = (first - a->loopEnd / a->frameSamples - 1) * a->frameBytes;
    }
  } else {
    a->streamDataByte = (a->state == Halfway ? a->loaded : a->fileDataEnd) - a->dataOff;
  }
  if(a->decoder) a->decoder->reset();
  atracPrime(*a);
  atracContext(id);
  result(0);
  codecWait(AtracDecodeMicroseconds);
}

//(ID): 1 for a stream looping before its end, whose rest needs the second buffer, else 0.
auto Kernel::sceAtracIsSecondBufferNeeded() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  result(a->state == StreamedWithSecond);
}

//(ID, where to put where in the file, and how many bytes): what the second buffer holds, the frames after the loop's
//last to the file's end; for any other file, 0 and 0, and 0x80630022 (audio/atrac/second/getinfo).
auto Kernel::sceAtracGetSecondBufferInfo() -> void {
  auto a = atracFind(arg(0));
  if(!a) return;
  u32 from = 0, bytes = 0;
  if(a->state == StreamedWithSecond) {
    from = atracSecondStart(*a);
    bytes = a->fileDataEnd > from ? a->fileDataEnd - from : 0;
  }
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), from);
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), bytes);
  result(a->state == StreamedWithSecond ? 0 : AtracErrorSecondNotNeeded);
}

//(ID, buffer, bytes): the second buffer, holding what sceAtracGetSecondBufferInfo asked for. Fewer bytes than three
//frames, or than what it asks for should that be less, are refused (0x80630011), then a file that needs none
//(0x80630022); its address isn't checked (audio/atrac/second/setbuffer).
auto Kernel::sceAtracSetSecondBuffer() -> void {
  u32 id = arg(0), address = arg(1), bytes = arg(2);
  auto a = atracFind(id);
  if(!a) return;
  u32 least = 3 * a->frameBytes;
  if(a->state == StreamedWithSecond) {
    u32 from = atracSecondStart(*a);
    least = std::min(least, a->fileDataEnd > from ? a->fileDataEnd - from : 0);
  }
  if(bytes < least) return result(AtracErrorTooSmall);
  if(a->state != StreamedWithSecond) return result(AtracErrorSecondNotNeeded);
  a->secondBuffer = address;
  a->secondBufferByte = bytes;
  atracContext(id);
  result(0);
}

//(ID): where the ID's context is (atracContext()), in memory the library keeps for them all, taken from the user
//partition the first time a program asks (none of the games tried does: pspautotests' tests read it); 0 for an ID
//not handed out.
auto Kernel::_sceAtracGetContextAddress() -> void {
  u32 id = arg(0);
  if(id >= AtracIDs || !atracs[id].codec) return result(0);
  if(!atracContexts) {
    auto block = allocate(AtracIDs * 256, 1, 0, "atrac contexts");
    if(!block) return result(0);
    atracContexts = block->address;
  }
  atracContext(id);
  result(atracContexts + id * 256);
}
