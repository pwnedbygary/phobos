//sceMpeg, the library games play their movies with (PSMF files: a 2 KiB header, then an MPEG-2 program stream of
//H.264 pictures and ATRAC3plus sound). A movie is fed and taken apart as on a PSP, as pspautotests' video/mpeg tests
//recorded, and decoded by codec.cpp's decoders (FFmpeg's):
//- The setting up works with their sizes (a ringbuffer's memory: 0x868 bytes a packet; the library's: 0x10000), so
//  games make their buffers and threads as they would.
//- sceMpegQueryStreamOffset and sceMpegQueryStreamSize read the movie's header: where its stream starts and how long
//  it is, big-endian words 8 and 12 bytes in (video/mpeg/basic: 0x800 and 0x40800 for its test.pmf). Without
//  decoders they refuse every header (0x80610022, the error video/mpeg/ringbuffer/construct recorded for a value the
//  library refuses), as before there were any: games that check then give their movies up.
//- A game feeds the ringbuffer, which calls the game's own callback for packets of the movie (sceMpegRingbufferPut),
//  and asks for the pictures' access units (sceMpegGetAvcAu): each picture's H.264 data with its time stamps, taken
//  from the packets, which are free again once taken. Both go as video/mpeg/basic recorded them for its movie, to the
//  byte and the packet. Decoding an access unit gives a picture from the second on (basic's first gave none: the
//  decoder holds one back), the one before, converted into the game's buffer in its pixel format
//  (sceMpegAvcDecodeMode), or kept for sceMpegAvcCsc to convert (sceMpegAvcDecodeYCbCr).
//- The sound's access units (sceMpegGetAtracAu) are the ATRAC3plus frames of private stream 1 (each PES packet's data
//  a 4-byte header, then the stream: frames each behind an 8-byte header of their own, 0x0fd0 and the codec's
//  parameters, as the owner's games' movies hold them); sound in packets freed for the pictures before it was asked
//  for is kept. sceMpegAtracDecode decodes one into 2048 stereo samples. With no sound: 0x80618001, the "no data"
//  basic recorded for its movie, which has none, and sceMpegAtracDecode 0x807f00fd.
//The library's own state lives in the memory the game gave it, where Sony's keeps its own; what the Media Engine
//holds (the access unit to decode, sound taken out of freed packets, the pictures) is kept here (mpegStreams).
//Decoding waits a moment (MpegDecodeMicroseconds), as atrac.cpp's does.

namespace {
  constexpr u32 MpegErrorValue = 0x8061'0022, MpegErrorNoData = 0x8061'8001;
  constexpr u32 RingbufferPacketMemory = 0x868, PacketSize = 2048;
  //A ringbuffer's fields (SceMpegRingbuffer2 in video/mpeg's shared.h): its packets, the next to read and to write
  //(places in the ring, from 0), how many hold data, the packets' memory, the callback and its argument, the library
  //using it, and the global pointer the callback runs with.
  constexpr u32 RingPackets = 0, RingRead = 4, RingWritten = 8, RingFilled = 12, RingData = 20, RingCallback = 24,
                RingArgument = 28, RingLibrary = 40, RingGp = 44;
  //The library's memory, where the handle points (its "LIBMPEG"): the ringbuffer it reads (where shared.h's
  //SceMpegBufferHeader has it), then, past the fields shared.h names, the library's own state, as Sony's keeps its
  //own there: the bytes of video already taken from the ring's first packet, whether the decoder holds a picture
  //back, and whether the last feeding came up short (the movie's file ended: its last access unit ends with its
  //data).
  constexpr u32 LibraryRingbuffer = 0x10, LibraryTaken = 0x700, LibraryHolding = 0x704, LibraryEnded = 0x708,
                LibraryPixels = 0x70c;
  constexpr u32 LibraryMemory = 0x10000, LibraryOffset = 0x30, LibraryState = 0x800;
  constexpr u64 NoTime = ~0ull;
  constexpr u32 MpegDecodeMicroseconds = 300;  //chosen, as atrac.cpp's
  constexpr u32 AudioFrameTicks = 4180;        //2048 samples at 44.1 kHz, in the 90 kHz clock's ticks (rounded)

  //A time stamp in a PES header: 33 bits in five bytes, with marker bits between.
  auto timeStamp(const u8* bytes) -> u64 {
    return u64(bytes[0] >> 1 & 7) << 30 | u64(bytes[1] << 7 | bytes[2] >> 1) << 15 | (bytes[3] << 7 | bytes[4] >> 1);
  }

  //Where an access unit delimiter (H.264's NAL unit 9, with a four-byte start code) is in the video, from a place on.
  auto delimiter(const std::vector<u8>& video, u64 from) -> s64 {
    for(u64 at = from; at + 5 <= video.size(); at++) {
      if(!video[at] && !video[at + 1] && !video[at + 2] && video[at + 3] == 1 && (video[at + 4] & 0x1f) == 9) {
        return at;
      }
    }
    return -1;
  }

  //Whether a packet is an MPEG-2 program stream pack (its header's start code, MPEG-2's marker bits).
  auto isPack(const u8* packet) -> bool {
    return !packet[0] && !packet[1] && packet[2] == 1 && packet[3] == 0xba && (packet[4] & 0xc0) == 0x40;
  }

  //A packet's sound: what its private stream 1 PES packets of the first ATRAC3plus channel (substream 0) carry,
  //their data less its 4-byte header, and where their time stamps fall in it.
  auto packetSound(const u8* packet, std::vector<u8>& sound, std::vector<std::pair<u32, u64>>& stamps) -> void {
    u32 at = 14 + (packet[13] & 7);
    while(isPack(packet) && at + 6 <= PacketSize && !packet[at] && !packet[at + 1] && packet[at + 2] == 1) {
      u32 body = at + 6, next = std::min<u32>(body + (packet[at + 4] << 8 | packet[at + 5]), PacketSize);
      if(packet[at + 3] == 0xbd && body + 3 <= next && (packet[body] & 0xc0) == 0x80) {
        u32 flags = packet[body + 1], payload = body + 3 + packet[body + 2];
        if(payload + 4 <= next && !packet[payload]) {
          if(flags & 0x80 && body + 8 <= payload) stamps.push_back({u32(sound.size()), timeStamp(packet + body + 3)});
          sound.insert(sound.end(), packet + payload + 4, packet + next);
        }
      }
      at = next;
    }
  }

  //Whether an access unit holds an H.264 key frame (an IDR slice, NAL unit 5).
  auto keyframe(const std::vector<u8>& unit) -> bool {
    for(u64 at = 0; at + 4 <= unit.size(); at++) {
      if(!unit[at] && !unit[at + 1] && unit[at + 2] == 1 && (unit[at + 3] & 0x1f) == 5) return true;
    }
    return false;
  }

  //A picture's planes, packed one after another: Y, then Cb and Cr at half its size each way.
  auto packPicture(const VideoDecoder::Picture& picture, std::vector<u8>& out) -> void {
    u32 width = picture.width, height = picture.height, half = (width + 1) / 2, halfHeight = (height + 1) / 2;
    out.resize(width * height + 2 * half * halfHeight);
    u8* to = out.data();
    for(u32 y = 0; y < height; y++, to += width) memcpy(to, picture.planes[0] + y * picture.strides[0], width);
    for(u32 plane = 1; plane < 3; plane++) {
      for(u32 y = 0; y < halfHeight; y++, to += half) {
        memcpy(to, picture.planes[plane] + y * picture.strides[plane], half);
      }
    }
  }
}

auto Kernel::sceMpegInit() -> void { result(0); }
auto Kernel::sceMpegFinish() -> void { result(0); }

//(packets): the memory a ringbuffer of that many needs: 0x868 bytes each (wrapping as a PSP's does: -1 gives
//0xfffff798, as video/mpeg/ringbuffer/memsize recorded).
auto Kernel::sceMpegRingbufferQueryMemSize() -> void {
  result(arg(0) * RingbufferPacketMemory);
}

//(mode): the memory sceMpegCreate needs.
auto Kernel::sceMpegQueryMemSize() -> void {
  result(LibraryMemory);
}

//(ringbuffer, packets, data, size, callback, callback's argument): a SceMpegRingbuffer (pspmpeg.h) filled in:
//packets, none read, written or free yet, its data, callback and argument, and where its data ends (2048 bytes a
//packet on), as video/mpeg/ringbuffer/construct recorded. More than 4096 packets, or a negative size, is refused.
auto Kernel::sceMpegRingbufferConstruct() -> void {
  u32 ringbuffer = arg(0), packets = arg(1), data = arg(2), size = arg(3);
  if(s32(packets) > 4096 || s32(size) < 0) return result(MpegErrorValue);
  if(!memory.reaches(ringbuffer, 48)) return result(ErrorInvalidPointer);
  u32 words[12] = {packets, 0, 0, 0, 0, data, arg(4), arg(5), data + packets * PacketSize, 0, 0, cpu.ipu.r[28]};
  for(u32 n = 0; n < 12; n++) memory.write(4, ringbuffer + n * 4, words[n]);
  result(0);
}

auto Kernel::sceMpegRingbufferDestruct() -> void { result(0); }

//(ringbuffer): its free packets: those that don't hold data (video/mpeg/ringbuffer/avail: 512 packets with 1 holding
//data have 511 free).
auto Kernel::sceMpegRingbufferAvailableSize() -> void {
  if(!memory.reaches(arg(0), 16)) return result(ErrorInvalidPointer);
  result(memory.read(4, arg(0) + RingPackets) - memory.read(4, arg(0) + RingFilled));
}

//(ringbuffer, packets, available): the ringbuffer fed, as video/mpeg/basic recorded. Its callback is asked for
//packets (where they go, how many, its argument): as many as asked for, no more than available and free, a run at a
//time that stops at the ring's end; and asked again for what's left as long as it gives some (at its file's end
//basic's gave 9 of 24, and was asked for the other 15). Put returns how many it gave. The callback runs on the
//calling thread, which may wait in it (basic's read a file), with the ringbuffer's global pointer and a stack below
//the caller's, returning to the trampoline's seventh syscall (mpegReturned()). Not from an interrupt handler, nor a
//thread already feeding one: nothing is put in.
auto Kernel::sceMpegRingbufferPut() -> void {
  u32 ringbuffer = arg(0);
  if(!memory.reaches(ringbuffer, 48)) return result(ErrorInvalidPointer);
  s32 room = s32(memory.read(4, ringbuffer + RingPackets)) - s32(memory.read(4, ringbuffer + RingFilled));
  s32 wanted = std::min({s32(arg(1)), s32(arg(2)), room});
  result(0);
  if(wanted <= 0 || !memory.read(4, ringbuffer + RingCallback)) return;
  if(!current || interrupting || mpegCalls.count(current->uid)) return;
  MpegCall call;
  save(call.caller);
  call.ringbuffer = ringbuffer;
  call.left = wanted;
  mpegNext(mpegCalls[current->uid] = call);
}

//The next run of packets asked of a ringbuffer's callback; with all asked for given, Put returns.
auto Kernel::mpegNext(MpegCall& call) -> void {
  u32 ringbuffer = call.ringbuffer;
  u32 packets = memory.read(4, ringbuffer + RingPackets), written = memory.read(4, ringbuffer + RingWritten);
  u32 run = written < packets ? std::min(call.left, packets - written) : 0;
  u32 at = memory.read(4, ringbuffer + RingData) + written * PacketSize;
  if(!run || !memory.reaches(at, run * PacketSize)) return mpegFinish(call);
  call.asked = run;
  restore(call.caller);
  cpu.ipu.r[4] = at;
  cpu.ipu.r[5] = run;
  cpu.ipu.r[6] = memory.read(4, ringbuffer + RingArgument);
  cpu.ipu.r[28] = memory.read(4, ringbuffer + RingGp);
  cpu.ipu.r[29] = (call.caller.gpr[29] - 0x40) & ~15u;
  cpu.ipu.r[31] = Trampoline + 40;
  cpu.ipu.pc = memory.read(4, ringbuffer + RingCallback);
  cpu.ipu.pd = cpu.ipu.pc + 4;
}

//The trampoline's seventh syscall: a ringbuffer's callback returned how many packets it gave (v0; more than it was
//asked for counts as what it was asked for): they hold data now. None, or an error, and nothing more is asked.
auto Kernel::mpegReturned() -> void {
  if(!current) return;
  auto found = mpegCalls.find(current->uid);
  if(found == mpegCalls.end()) return;
  auto& call = found->second;
  s32 gave = s32(cpu.ipu.r[2]);
  u32 ringbuffer = call.ringbuffer;
  u32 packets = memory.read(4, ringbuffer + RingPackets), written = memory.read(4, ringbuffer + RingWritten);
  if(gave <= 0 || !packets) return mpegFinish(call);  //(a ring the game emptied of packets meanwhile: done)
  u32 given = std::min(u32(gave), call.asked);
  memory.write(4, ringbuffer + RingWritten, (written + given) % packets);
  memory.write(4, ringbuffer + RingFilled, memory.read(4, ringbuffer + RingFilled) + given);
  call.put += given;
  call.left -= given;
  mpegNext(call);
}

//Put returns to the game how many packets came. Coming up short of what it asked for is kept for sceMpegGetAvcAu:
//the movie's file ended.
auto Kernel::mpegFinish(MpegCall& call) -> void {
  u32 library = memory.read(4, call.ringbuffer + RingLibrary);
  if(memory.reaches(library, LibraryState)) memory.write(4, library + LibraryEnded, call.left > 0);
  restore(call.caller);
  cpu.ipu.r[2] = call.put;
  mpegCalls.erase(current->uid);
}

//A thread ended, or went, part way through Put (in the callback): Put ends with it, what came staying put in.
auto Kernel::mpegAbandoned(u32 thread) -> void {
  mpegCalls.erase(thread);
}

//The library a handle (the SceMpeg a game holds) stands for: its memory, where its "LIBMPEG" is; 0 if it isn't one.
auto Kernel::mpegLibrary(u32 handle) -> u32 {
  if(!memory.reaches(handle, 4)) return 0;
  u32 library = memory.read(4, handle);
  if(!memory.reaches(library, LibraryState) || memory.readString(library, 8) != "LIBMPEG") return 0;
  return library;
}

//(handle, data, size, ringbuffer, frame width, mode, DDR top): the library set up in the memory given, its handle
//written first, which points at its "LIBMPEG" signature there (video/mpeg/basic shows it), the ringbuffer it reads
//from noted on both sides, and its own state cleared.
auto Kernel::sceMpegCreate() -> void {
  u32 handle = arg(0), data = arg(1), size = arg(2), ringbuffer = arg(3);
  if(size < LibraryMemory) return result(ErrorNoMemory);
  if(!memory.reaches(handle, 4) || !memory.reaches(data, LibraryMemory)) return result(ErrorInvalidPointer);
  u32 library = data + LibraryOffset;
  for(u32 offset = 0; offset < LibraryState; offset += 4) memory.write(4, library + offset, 0);
  memory.copyIn(library, "LIBMPEG", 8);
  memory.write(4, library + LibraryRingbuffer, ringbuffer);
  if(memory.reaches(ringbuffer, 48)) memory.write(4, ringbuffer + RingLibrary, library);
  memory.write(4, handle, library);
  result(0);
}

//(handle): what the Media Engine held for the library goes.
auto Kernel::sceMpegDelete() -> void {
  if(u32 library = mpegLibrary(arg(0))) mpegStreams.erase(library);
  result(0);
}

//(handle, the header, where to put the offset): where the movie's stream starts, after its header (a big-endian word
//8 bytes in); a header that isn't PSMF's, or no decoders to play it with, is refused.
auto Kernel::sceMpegQueryStreamOffset() -> void {
  u32 header = arg(1);
  if(!videoDecoders || !memory.reaches(header, 16) || memory.readString(header, 4) != "PSMF") {
    return result(MpegErrorValue);
  }
  u32 offset = memory.read(1, header + 8) << 24 | memory.read(1, header + 9) << 16 | memory.read(1, header + 10) << 8
             | memory.read(1, header + 11);
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), offset);
  result(0);
}

//(the header, where to put the size): the stream's size (the big-endian word 12 bytes in).
auto Kernel::sceMpegQueryStreamSize() -> void {
  u32 header = arg(0);
  if(!videoDecoders || !memory.reaches(header, 16) || memory.readString(header, 4) != "PSMF") {
    return result(MpegErrorValue);
  }
  u32 size = memory.read(1, header + 12) << 24 | memory.read(1, header + 13) << 16 | memory.read(1, header + 14) << 8
           | memory.read(1, header + 15);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), size);
  result(0);
}

//Streams registered (a handle each: the pictures' 0x12c0 and the sound's 0x1700, as video/mpeg/basic recorded) and
//buffers handed out, for the access units to come.
auto Kernel::sceMpegRegistStream() -> void { result(0x12c0 + 0x440 * std::min(arg(1), 2u)); }
auto Kernel::sceMpegUnRegistStream() -> void { result(0); }
auto Kernel::sceMpegMallocAvcEsBuf() -> void { result(1); }
auto Kernel::sceMpegFreeAvcEsBuf() -> void { result(0); }

//(handle, ES buffer, access unit): the access unit made ready for its stream's: its buffer, no time stamps or size
//yet (an ATRAC3plus one, never filled, showed so in video/mpeg/basic).
auto Kernel::sceMpegInitAu() -> void {
  if(!memory.reaches(arg(2), 24)) return result(ErrorInvalidPointer);
  u32 words[6] = {0, 0, 0, 0, arg(1), 0};
  for(u32 n = 0; n < 6; n++) memory.write(4, arg(2) + n * 4, words[n]);
  result(0);
}

//(handle): every stream flushed: what the ringbuffer holds is dropped, and the video taken apart and decoded
//starts afresh.
auto Kernel::sceMpegFlushAllStream() -> void {
  if(u32 library = mpegLibrary(arg(0))) {
    u32 ringbuffer = memory.read(4, library + LibraryRingbuffer);
    if(memory.reaches(ringbuffer, 48)) {
      memory.write(4, ringbuffer + RingRead, memory.read(4, ringbuffer + RingWritten));
      memory.write(4, ringbuffer + RingFilled, 0);
    }
    for(u32 offset : {LibraryTaken, LibraryHolding, LibraryEnded}) memory.write(4, library + offset, 0);
    if(auto found = mpegStreams.find(library); found != mpegStreams.end()) {
      auto& stream = found->second;
      stream.unit.clear();
      stream.audio.clear();
      stream.audioStamps.clear();
      stream.audioTaken = 0;
      stream.audioTime = stream.audioCarry = NoTime;
      stream.held.clear();
      if(stream.video) stream.video->reset();
      if(stream.sound) stream.sound->reset();
    }
  }
  result(0);
}

auto Kernel::sceMpegFlushStream() -> void { result(0); }

//(handle, stream, access unit, where to put its attribute): the video's next access unit, taken from the
//ringbuffer's packets as video/mpeg/basic recorded for its movie, to the byte: the packets read as MPEG-2 program
//stream packs (one a packet: a pack header, then PES packets), the video those of stream 0xe0 carry; an access unit
//runs from one access unit delimiter to the next (or, the movie's file having ended, to the end of what's there),
//and comes once it's all there. Its time stamps are those of the PES packet that begins with it, if that has any
//(else -1, as most of basic's had); its size is written (the ES buffer stays the one sceMpegInitAu gave), the
//attribute is 1, and the packets whose video it took the last of are free again, a packet with no video once it's
//passed. None ready: "no data", and nothing changes.
auto Kernel::sceMpegGetAvcAu() -> void {
  u32 library = mpegLibrary(arg(0)), au = arg(2);
  if(!library) return result(MpegErrorNoData);
  if(!memory.reaches(au, 24)) return result(ErrorInvalidPointer);
  u32 ringbuffer = memory.read(4, library + LibraryRingbuffer);
  if(!memory.reaches(ringbuffer, 48)) return result(MpegErrorNoData);
  u32 packets = memory.read(4, ringbuffer + RingPackets), read = memory.read(4, ringbuffer + RingRead);
  u32 filled = memory.read(4, ringbuffer + RingFilled), data = memory.read(4, ringbuffer + RingData);
  if(!packets || packets > 4096 || read >= packets || filled > packets) return result(MpegErrorNoData);
  u32 taken = memory.read(4, library + LibraryTaken);
  std::vector<u8> video;  //the video from the ring's first packet on
  std::vector<u32> ends;  //where each packet's video ends in it
  struct Stamp { u64 at, presented, decoded; };
  std::vector<Stamp> stamps;  //PES packets with time stamps: where their video begins
  s64 start = -1, end = -1;
  u64 searched = taken;  //where looking for the next delimiter goes on from
  u8 packet[PacketSize];
  for(u32 n = 0; n < filled && end < 0; n++) {
    u32 address = data + (read + n) % packets * PacketSize;
    if(!memory.reaches(address, PacketSize)) break;
    memory.copyOut(packet, address, PacketSize);
    u32 at = 14 + (packet[13] & 7);
    bool pack = !packet[0] && !packet[1] && packet[2] == 1 && packet[3] == 0xba && (packet[4] & 0xc0) == 0x40;
    while(pack && at + 6 <= PacketSize && !packet[at] && !packet[at + 1] && packet[at + 2] == 1) {
      u32 body = at + 6, next = std::min<u32>(body + (packet[at + 4] << 8 | packet[at + 5]), PacketSize);
      if(packet[at + 3] == 0xe0 && body + 3 <= next && (packet[body] & 0xc0) == 0x80) {
        u32 flags = packet[body + 1], payload = body + 3 + packet[body + 2];
        if(payload <= next) {
          if(flags & 0x80 && body + 8 <= payload) {
            u64 decoded = flags & 0x40 && body + 13 <= payload ? timeStamp(packet + body + 8) : NoTime;
            stamps.push_back({video.size(), timeStamp(packet + body + 3), decoded});
          }
          video.insert(video.end(), packet + payload, packet + next);
        }
      }
      at = next;
    }
    ends.push_back(video.size());
    u64 resume = std::max<u64>(taken, std::max<u64>(video.size(), 4) - 4);  //a delimiter may straddle two packets
    if(start < 0) {
      start = delimiter(video, searched);
      searched = start < 0 ? resume : start + 5;
    }
    if(start >= 0) {
      end = delimiter(video, searched);
      searched = std::max<u64>(resume, start + 5);
    }
  }
  if(start >= 0 && end < 0 && ends.size() == filled && memory.read(4, library + LibraryEnded)) end = video.size();
  if(end < 0) return result(MpegErrorNoData);
  u64 presented = NoTime, decoded = NoTime;
  for(auto& stamp : stamps) {
    if(stamp.at == u64(start)) presented = stamp.presented, decoded = stamp.decoded;
  }
  u32 words[4] = {u32(presented >> 32), u32(presented), u32(decoded >> 32), u32(decoded)};
  for(u32 n = 0; n < 4; n++) memory.write(4, au + n * 4, words[n]);
  memory.write(4, au + 20, end - start);
  if(memory.reaches(arg(3), 4)) memory.write(4, arg(3), 1);
  u32 freed = 0;
  while(freed < ends.size() && ends[freed] <= end) freed++;
  auto& stream = mpegStreams[library];
  stream.unit.assign(video.begin() + start, video.begin() + end);
  //the sound in the packets freed now, not yet handed out, is kept for sceMpegGetAtracAu (at most 1 MiB of it: a
  //game that never asks for its movie's sound doesn't keep it all)
  std::vector<u8> sound;
  std::vector<std::pair<u32, u64>> soundStamps;
  for(u32 n = 0; n < freed; n++) {
    memory.copyOut(packet, data + (read + n) % packets * PacketSize, PacketSize);
    packetSound(packet, sound, soundStamps);
  }
  if(sound.size() > stream.audioTaken) {
    for(auto [at, time] : soundStamps) {
      if(at < stream.audioTaken) continue;
      stream.audioStamps.push_back({u32(stream.audio.size() + at - stream.audioTaken), time});
    }
    stream.audio.insert(stream.audio.end(), sound.begin() + stream.audioTaken, sound.end());
    if(stream.audio.size() > 1_MiB) {
      u32 drop = stream.audio.size() - 1_MiB;
      stream.audio.erase(stream.audio.begin(), stream.audio.begin() + drop);
      std::vector<std::pair<u32, u64>> kept;
      for(auto [at, time] : stream.audioStamps) if(at >= drop) kept.push_back({at - drop, time});
      stream.audioStamps = kept;
    }
  }
  stream.audioTaken -= std::min<u32>(stream.audioTaken, sound.size());
  memory.write(4, library + LibraryTaken, end - (freed ? ends[freed - 1] : 0));
  memory.write(4, ringbuffer + RingRead, (read + freed) % packets);
  memory.write(4, ringbuffer + RingFilled, filled - freed);
  result(0);
}

//(handle, stream, access unit, where to put where its data is): the sound's next access unit, an ATRAC3plus frame
//with its 8-byte header (0x0fd0, then the codec's parameters: the frame's size in 8-byte steps, less one, in the low
//10 bits, its channels above), copied into the access unit's ES buffer (as big as sceMpegQueryAtracEsSize said):
//from the sound kept from freed packets first, then from the packets still in the ring (which it doesn't free: the
//pictures do). Its time stamp is that of the PES packet the frame starts in, else the last one's plus a frame's
//time. Bytes before a frame's header (a stream joined part way) are passed over. None whole yet: "no data".
auto Kernel::sceMpegGetAtracAu() -> void {
  u32 library = mpegLibrary(arg(0)), au = arg(2);
  if(!library || !audioDecoders) return result(MpegErrorNoData);
  if(!memory.reaches(au, 24)) return result(ErrorInvalidPointer);
  auto& stream = mpegStreams[library];
  std::vector<u8> sound = stream.audio;
  auto stamps = stream.audioStamps;
  u32 queued = sound.size();
  u32 ringbuffer = memory.read(4, library + LibraryRingbuffer);
  if(memory.reaches(ringbuffer, 48)) {
    u32 packets = memory.read(4, ringbuffer + RingPackets), read = memory.read(4, ringbuffer + RingRead);
    u32 filled = memory.read(4, ringbuffer + RingFilled), data = memory.read(4, ringbuffer + RingData);
    std::vector<u8> ring;
    std::vector<std::pair<u32, u64>> ringStamps;
    u8 packet[PacketSize];
    for(u32 n = 0; packets && packets <= 4096 && filled <= packets && n < filled; n++) {
      u32 address = data + (read + n) % packets * PacketSize;
      if(!memory.reaches(address, PacketSize)) break;
      memory.copyOut(packet, address, PacketSize);
      packetSound(packet, ring, ringStamps);
    }
    if(ring.size() > stream.audioTaken) {
      for(auto [at, time] : ringStamps) {
        if(at >= stream.audioTaken) stamps.push_back({queued + at - stream.audioTaken, time});
      }
      sound.insert(sound.end(), ring.begin() + stream.audioTaken, ring.end());
    }
  }
  u32 from = 0;
  while(from + 1 < sound.size() && !(sound[from] == 0x0f && sound[from + 1] == 0xd0)) from++;
  if(from + 8 > sound.size()) return result(MpegErrorNoData);
  u32 bytes = 8 + (((sound[from + 2] << 8 | sound[from + 3]) & 0x3ff) + 1) * 8;
  u32 buffer = memory.read(4, au + 16);
  if(from + bytes > sound.size() || bytes > 0x840 || !memory.reaches(buffer, bytes)) return result(MpegErrorNoData);
  //the time stamp of the PES packet the frame is the first to start in: one that starts at or before it (or inside
  //the frame before, carried over), else the last frame's plus a frame's time
  u64 time = stream.audioCarry;
  for(auto [at, stamp] : stamps) if(at <= from) time = stamp;
  if(time == NoTime) time = stream.audioTime == NoTime ? 0 : stream.audioTime + AudioFrameTicks;
  stream.audioCarry = NoTime;
  for(auto [at, stamp] : stamps) if(at > from && at < from + bytes) stream.audioCarry = stamp;
  memory.copyIn(buffer, sound.data() + from, bytes);
  u32 words[4] = {u32(time >> 32), u32(time), u32(time >> 32), u32(time)};
  for(u32 n = 0; n < 4; n++) memory.write(4, au + n * 4, words[n]);
  memory.write(4, au + 20, bytes);
  if(memory.reaches(arg(3), 4)) memory.write(4, arg(3), buffer);
  stream.audioTime = time;
  u32 used = from + bytes;
  if(used <= queued) {
    stream.audio.erase(stream.audio.begin(), stream.audio.begin() + used);
    std::vector<std::pair<u32, u64>> kept;
    for(auto [at, stamp] : stream.audioStamps) if(at >= used) kept.push_back({at - used, stamp});
    stream.audioStamps = kept;
  } else {
    stream.audioTaken += used - queued;
    stream.audio.clear();
    stream.audioStamps.clear();
  }
  result(0);
}

auto Kernel::sceMpegGetPcmAu() -> void { result(MpegErrorNoData); }

//(handle, mode): the pixel format decoded pictures are converted to (pspsdk's SCE_MPEG_AVC_FORMAT_*: 5650, 5551,
//4444, or 8888, the default), the mode's second word; -1 leaves it as it is.
auto Kernel::sceMpegAvcDecodeMode() -> void {
  u32 library = mpegLibrary(arg(0));
  if(library && memory.reaches(arg(1), 8)) {
    u32 format = memory.read(4, arg(1) + 4);
    if(format <= 3) memory.write(4, library + LibraryPixels, format + 1);
  }
  result(0);
}

auto Kernel::sceMpegChangeGetAuMode() -> void { result(0); }

//(handle, where to put the ES buffer's size, and the output's): ATRAC3plus's, as video/mpeg/basic recorded.
auto Kernel::sceMpegQueryAtracEsSize() -> void {
  if(arg(1)) memory.write(4, arg(1), 0x840);
  if(arg(2)) memory.write(4, arg(2), 0x2000);
  result(0);
}

//A picture converted into the game's buffer, frameWidth pixels a row, in the library's pixel format: the part of it
//from (x, y), width by height (0: the rest of it), its colours from the decoder's Y, Cb and Cr by ITU-R BT.601's
//equations for video's range (Y 16-235, colours 16-240), rounded to the nearest; alpha opaque. (Whether the PSP's
//Media Engine rounds the same isn't measured.)
auto Kernel::mpegConvert(MpegStream& stream, u32 library, u32 destination, u32 frameWidth, u32 x, u32 y, u32 width,
                         u32 height) -> void {
  u32 pictureWidth = stream.shownWidth, pictureHeight = stream.shownHeight;
  if(stream.shown.empty() || x >= pictureWidth || y >= pictureHeight || !frameWidth) return;
  if(!width || x + width > pictureWidth) width = pictureWidth - x;
  if(!height || y + height > pictureHeight) height = pictureHeight - y;
  u32 format = memory.read(4, library + LibraryPixels);
  format = format ? format - 1 : 3;
  u32 bytes = format == 3 ? 4 : 2;
  const u8* luma = stream.shown.data();
  const u8* blue = luma + pictureWidth * pictureHeight;
  const u8* red = blue + (pictureWidth + 1) / 2 * ((pictureHeight + 1) / 2);
  u32 half = (pictureWidth + 1) / 2;
  std::vector<u8> row(width * bytes);
  for(u32 line = 0; line < height; line++) {
    u32 address = destination + line * frameWidth * bytes;
    if(!memory.reaches(address, width * bytes)) break;
    u32 py = y + line;
    for(u32 column = 0; column < width; column++) {
      u32 px = x + column;
      s32 c = luma[py * pictureWidth + px] - 16;
      s32 d = blue[py / 2 * half + px / 2] - 128, e = red[py / 2 * half + px / 2] - 128;
      u32 r = std::clamp((298 * c + 409 * e + 128) >> 8, 0, 255);
      u32 g = std::clamp((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255);
      u32 b = std::clamp((298 * c + 516 * d + 128) >> 8, 0, 255);
      u32 pixel = 0;
      if(format == 0) pixel = r >> 3 | (g >> 2) << 5 | (b >> 3) << 11;
      if(format == 1) pixel = r >> 3 | (g >> 3) << 5 | (b >> 3) << 10 | 0x8000;
      if(format == 2) pixel = r >> 4 | (g >> 4) << 4 | (b >> 4) << 8 | 0xf000;
      if(format == 3) pixel = r | g << 8 | b << 16 | 0xff00'0000;
      for(u32 n = 0; n < bytes; n++) row[column * bytes + n] = pixel >> n * 8;
    }
    memory.copyIn(address, row.data(), row.size());
  }
}

//A picture decoded from an access unit (sceMpegAvcDecode, sceMpegAvcDecodeYCbCr): the access unit used up (its size
//0, as video/mpeg/basic recorded), and a picture from the second on, the decoder holding one back (basic's first
//gave none): where to put whether one came gets 1 then, else 0. The picture that comes is the one decoded the time
//before, converted into pixels (when given: sceMpegAvcDecode's buffer, frameWidth wide) and kept for sceMpegAvcCsc.
//A decoder made afresh after a state was loaded shows no new picture until a key frame.
auto Kernel::mpegDecoded(u32 handle, u32 au, u32 frame, u32 pixels, u32 frameWidth) -> void {
  u32 library = mpegLibrary(handle);
  bool came = false, decoded = false;
  if(library && memory.reaches(au, 24) && memory.read(4, au + 20)) {
    came = memory.read(4, library + LibraryHolding);
    memory.write(4, library + LibraryHolding, 1);
    memory.write(4, au + 20, 0);
    auto& stream = mpegStreams[library];
    if(came) stream.shown = stream.held, stream.shownWidth = stream.heldWidth, stream.shownHeight = stream.heldHeight;
    if(!stream.video && videoDecoders) stream.video = videoDecoders();
    if(stream.video && !stream.unit.empty()) {
      if(stream.keyframe && keyframe(stream.unit)) stream.keyframe = false;
      decoded = true;
      if(stream.video->decode(stream.unit.data(), stream.unit.size()) && !stream.keyframe) {
        auto& picture = stream.video->picture();
        packPicture(picture, stream.held);
        stream.heldWidth = picture.width;
        stream.heldHeight = picture.height;
      }
    }
    if(came && pixels) mpegConvert(stream, library, pixels, frameWidth, 0, 0, 0, 0);
  }
  if(memory.reaches(frame, 4)) memory.write(4, frame, came);
  result(0);
  if(decoded) codecWait(MpegDecodeMicroseconds);
}

//(handle, access unit, frame width, where the buffer's address is, where to put whether a picture came).
auto Kernel::sceMpegAvcDecode() -> void {
  u32 pixels = memory.reaches(arg(3), 4) ? memory.read(4, arg(3)) : 0;
  mpegDecoded(arg(0), arg(1), arg(4), pixels, arg(2));
}

//(handle, frame width, where the buffer's address is, where to put its status): the picture the decoder held back,
//given back at the movie's end: converted, and the status 1; none held, 0.
auto Kernel::sceMpegAvcDecodeStop() -> void {
  u32 library = mpegLibrary(arg(0));
  bool held = library && memory.read(4, library + LibraryHolding);
  if(held) {
    memory.write(4, library + LibraryHolding, 0);
    auto& stream = mpegStreams[library];
    stream.shown = stream.held, stream.shownWidth = stream.heldWidth, stream.shownHeight = stream.heldHeight;
    u32 pixels = memory.reaches(arg(2), 4) ? memory.read(4, arg(2)) : 0;
    if(pixels) mpegConvert(stream, library, pixels, arg(1), 0, 0, 0, 0);
  }
  if(memory.reaches(arg(3), 4)) memory.write(4, arg(3), held);
  result(0);
}

//(handle): the decoder emptied: the next picture is held back again.
auto Kernel::sceMpegAvcDecodeFlush() -> void {
  if(u32 library = mpegLibrary(arg(0))) {
    memory.write(4, library + LibraryHolding, 0);
    if(auto found = mpegStreams.find(library); found != mpegStreams.end()) {
      found->second.held.clear();
      if(found->second.video) found->second.video->reset();
    }
  }
  result(0);
}

//(handle, mode, width, height, where to put the size): the memory a decoded YCbCr frame of that size takes: its
//luma and its two quarter-size chroma planes (width x height x 3 / 2, at least 0x80 apiece for the library's own
//bookkeeping: chosen, games only hand the size back in sceMpegAvcInitYCbCr).
auto Kernel::sceMpegAvcQueryYCbCrSize() -> void {
  u32 width = arg(2), height = arg(3);
  if(width > 0x1000 || height > 0x1000) return result(MpegErrorValue);
  if(arg(4)) memory.write(4, arg(4), ((width * height * 3 / 2 + 0x7f) & ~0x7fu) + 0x80);
  result(0);
}

//(handle, mode, width, height, buffer): the buffer set up for decoded frames.
auto Kernel::sceMpegAvcInitYCbCr() -> void { result(0); }

//(handle, access unit, where the YCbCr buffer's address is, where to put whether a picture came): the picture kept
//for sceMpegAvcCsc, which converts it (the YCbCr buffer is the Media Engine's to fill: nothing is written there).
auto Kernel::sceMpegAvcDecodeYCbCr() -> void {
  mpegDecoded(arg(0), arg(1), arg(3), 0, 0);
}

//(handle, buffer, where to put its status): the held picture given back, as sceMpegAvcDecodeStop does.
auto Kernel::sceMpegAvcDecodeStopYCbCr() -> void {
  u32 library = mpegLibrary(arg(0));
  bool held = library && memory.read(4, library + LibraryHolding);
  if(held) {
    memory.write(4, library + LibraryHolding, 0);
    auto& stream = mpegStreams[library];
    stream.shown = stream.held, stream.shownWidth = stream.heldWidth, stream.shownHeight = stream.heldHeight;
  }
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), held);
  result(0);
}

//(handle, details): the last picture decoded described: it's there. The details (the picture's size and the like) are
//left as they were: pspautotests imports the function (video/mpeg's imports name it) but records nothing of them.
//Space Invaders Extreme asks after each picture its movie thread decodes, and waits for the first before its stage
//starts.
auto Kernel::sceMpegAvcDecodeDetail() -> void { result(0); }

//(handle, YCbCr buffer, the part to convert, frame width, destination): the picture sceMpegAvcDecodeYCbCr gave last,
//converted into the destination, frame width pixels a row. The part: four words, its left, top, width and height in
//pixels (Burnout Legends gives 0, 0, 480, 272); all zeros (Space Invaders Extreme's), or a part past the picture,
//means the whole picture.
auto Kernel::sceMpegAvcCsc() -> void {
  u32 library = mpegLibrary(arg(0)), range = arg(2);
  if(library) {
    if(auto found = mpegStreams.find(library); found != mpegStreams.end()) {
      u32 part[4] = {};
      if(memory.reaches(range, 16)) for(u32 n = 0; n < 4; n++) part[n] = memory.read(4, range + n * 4);
      mpegConvert(found->second, library, arg(4), arg(3), part[0], part[1], part[2], part[3]);
    }
  }
  result(0);
}

//(handle, access unit, buffer, initialized): the sound access unit's ATRAC3plus frame decoded into 2048 stereo
//16-bit samples (mono doubled), 0x2000 bytes; the access unit used up. With none: 0x807f00fd, as video/mpeg/basic
//recorded for its movie, which has no sound. A frame that won't decode gives silence.
auto Kernel::sceMpegAtracDecode() -> void {
  u32 library = mpegLibrary(arg(0)), au = arg(1), output = arg(2);
  if(!library || !memory.reaches(au, 24) || !audioDecoders) return result(0x807f'00fd);
  u32 bytes = memory.read(4, au + 20), buffer = memory.read(4, au + 16);
  if(bytes < 8 || bytes > 0x840 || !memory.reaches(buffer, bytes)) return result(0x807f'00fd);
  std::vector<u8> unit(bytes);
  memory.copyOut(unit.data(), buffer, bytes);
  memory.write(4, au + 20, 0);
  auto& stream = mpegStreams[library];
  u32 parameters = unit[2] << 8 | unit[3], frameBytes = ((parameters & 0x3ff) + 1) * 8;
  u32 channels = (parameters >> 10 & 7) == 1 ? 1 : 2;
  std::vector<s16> samples(2048 * 2), out(2048 * 2);
  if(!stream.sound) {
    AudioDecoder::Format format;
    format.codec = AudioDecoder::Codec::Atrac3plus;
    format.channels = channels;
    format.frameBytes = frameBytes;
    stream.sound = audioDecoders(format);
  }
  s32 made = -1;
  if(stream.sound && unit[0] == 0x0f && unit[1] == 0xd0 && 8 + frameBytes <= bytes) {
    made = stream.sound->decode(unit.data() + 8, frameBytes, samples.data(), 2048);
  }
  for(s32 n = 0; n < std::min(made, 2048); n++) {
    out[n * 2] = samples[n * channels];
    out[n * 2 + 1] = samples[n * channels + channels - 1];
  }
  if(memory.reaches(output, 0x2000)) memory.copyIn(output, out.data(), 0x2000);
  result(0);
  codecWait(MpegDecodeMicroseconds);
}
