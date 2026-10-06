//sceMpeg, the library games play their movies with (PSMF files: an MPEG-2 program stream of H.264 video and
//ATRAC3plus sound). There's no video decoder yet, so no picture is ever shown; but a movie is fed and taken apart as
//on a PSP, as pspautotests' video/mpeg tests recorded:
//- The setting up works with their sizes (a ringbuffer's memory: 0x868 bytes a packet; the library's: 0x10000), so
//  games make their buffers and threads as they would.
//- sceMpegQueryStreamOffset, which reads the movie's header, refuses every one (0x80610022, the error
//  video/mpeg/ringbuffer/construct recorded for a value the library refuses; which a PSP gives for a header it can't
//  read isn't known here, but games only test it for a negative number). A game that checks gives the movie up: the
//  GTAs, Midnight Club 3 and Snoopy vs. the Red Baron go on so.
//- A game that plays on regardless feeds the ringbuffer, which calls the game's own callback for packets of the
//  movie (sceMpegRingbufferPut), and asks for the video's access units (sceMpegGetAvcAu): each picture's H.264 data
//  with its time stamps, taken from the packets, which are free again once taken. Both go as video/mpeg/basic
//  recorded them for its movie, to the byte and the packet. "Decoding" an access unit gives a picture from the second
//  on (basic's first gave none), but nothing is drawn: the game's buffer is left as it was. Burnout Legends' and
//  Dominator's callbacks give nothing (their files are set up only once a header is read), so their movies end at
//  once; Space Invaders Extreme's copies its movie from memory, which now plays, unseen, behind its stages. (With
//  nothing ever put in, as before, its movie thread, the best priority there, asked for an access unit until one
//  came, feeding the ringbuffer meanwhile, and kept every other thread from running: it never got past "NOW
//  LOADING".)
//- The sound's access units never come (sceMpegGetAtracAu: 0x80618001, the "no data" basic recorded for its movie,
//  which has no sound; sceMpegAtracDecode: 0x807f00fd).

namespace {
  constexpr u32 MpegErrorValue = 0x8061'0022, MpegErrorNoData = 0x8061'8001;
  constexpr u32 RingbufferPacketMemory = 0x868, PacketSize = 2048;
  //A ringbuffer's fields (SceMpegRingbuffer2 in video/mpeg's shared.h): its packets, the next to read and to write
  //(places in the ring, from 0), how many hold data, the packets' memory, the callback and its argument, and the
  //library using it. (SceMpegRingbuffer2 has a global pointer after them, which Construct writes; pspsdk's
  //SceMpegRingbuffer, 44 bytes, ends before it, so nothing reads it.)
  constexpr u32 RingPackets = 0, RingRead = 4, RingWritten = 8, RingFilled = 12, RingData = 20, RingCallback = 24,
                RingArgument = 28, RingLibrary = 40;
  //The library's memory, where the handle points (its "LIBMPEG"): the ringbuffer it reads (where shared.h's
  //SceMpegBufferHeader has it), then, past the fields shared.h names, the library's own state, as Sony's keeps its
  //own there: the bytes of video already taken from the ring's first packet, whether the decoder holds a picture
  //back, and whether the last feeding came up short (the movie's file ended: its last access unit ends with its
  //data).
  constexpr u32 LibraryRingbuffer = 0x10, LibraryTaken = 0x700, LibraryHolding = 0x704, LibraryEnded = 0x708;
  constexpr u32 LibraryMemory = 0x10000, LibraryOffset = 0x30, LibraryState = 0x800;
  constexpr u64 NoTime = ~0ull;

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
//calling thread, which may wait in it (basic's read a file), with the caller's global pointer and a stack below the
//caller's, returning to the trampoline's seventh syscall (mpegReturned()). Not from an interrupt handler, nor a
//thread already feeding one: nothing is put in. Nor into a ring whose fields, the game's to write, couldn't be a
//ring's (no packets or more than 4096, the next to write not among them, more holding data than there are), as
//sceMpegGetAvcAu takes nothing from one; else its callback could be asked for more than a ring's 4096 packets.
auto Kernel::sceMpegRingbufferPut() -> void {
  u32 ringbuffer = arg(0);
  if(!memory.reaches(ringbuffer, 48)) return result(ErrorInvalidPointer);
  u32 packets = memory.read(4, ringbuffer + RingPackets), written = memory.read(4, ringbuffer + RingWritten);
  u32 filled = memory.read(4, ringbuffer + RingFilled);
  result(0);
  if(!packets || packets > 4096 || written >= packets || filled > packets) return;
  s32 wanted = std::min({s32(arg(1)), s32(arg(2)), s32(packets - filled)});
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
  //the caller's global pointer, not the word at 44 that pspautotests' SceMpegRingbuffer2 keeps Construct's in (in
  //every case seen the same): pspsdk's SceMpegRingbuffer is 44 bytes, the word after it someone else's
  cpu.ipu.r[28] = call.caller.gpr[28];
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

auto Kernel::sceMpegDelete() -> void { result(0); }

//(handle, buffer, where to put the offset): the movie's header can't be read: the movie is given up.
auto Kernel::sceMpegQueryStreamOffset() -> void {
  result(MpegErrorValue);
}

//(buffer, where to put the size): likewise.
auto Kernel::sceMpegQueryStreamSize() -> void {
  result(MpegErrorValue);
}

//Streams registered (a handle each) and buffers handed out, for the access units to come.
auto Kernel::sceMpegRegistStream() -> void { result(0x12c0); }
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
  memory.write(4, library + LibraryTaken, end - (freed ? ends[freed - 1] : 0));
  memory.write(4, ringbuffer + RingRead, (read + freed) % packets);
  memory.write(4, ringbuffer + RingFilled, filled - freed);
  result(0);
}

auto Kernel::sceMpegGetAtracAu() -> void { result(MpegErrorNoData); }
auto Kernel::sceMpegGetPcmAu() -> void { result(MpegErrorNoData); }
auto Kernel::sceMpegAvcDecodeMode() -> void { result(0); }
auto Kernel::sceMpegChangeGetAuMode() -> void { result(0); }

//(handle, where to put the ES buffer's size, and the output's): ATRAC3plus's, as video/mpeg/basic recorded.
auto Kernel::sceMpegQueryAtracEsSize() -> void {
  if(arg(1)) memory.write(4, arg(1), 0x840);
  if(arg(2)) memory.write(4, arg(2), 0x2000);
  result(0);
}

//A picture decoded from an access unit (sceMpegAvcDecode, sceMpegAvcDecodeYCbCr): the access unit used up (its size
//0, as video/mpeg/basic recorded), and a picture from the second on, the decoder holding one back (basic's first
//gave none): where to put whether one came gets 1 then, else 0. No picture is drawn: the buffer is left as it was.
auto Kernel::mpegDecoded(u32 handle, u32 au, u32 frame) -> void {
  u32 library = mpegLibrary(handle);
  bool came = false;
  if(library && memory.reaches(au, 24) && memory.read(4, au + 20)) {
    came = memory.read(4, library + LibraryHolding);
    memory.write(4, library + LibraryHolding, 1);
    memory.write(4, au + 20, 0);
  }
  if(memory.reaches(frame, 4)) memory.write(4, frame, came);
  result(0);
}

//(handle, access unit, frame width, buffer, where to put whether a picture came).
auto Kernel::sceMpegAvcDecode() -> void {
  mpegDecoded(arg(0), arg(1), arg(4));
}

//(handle, frame width, buffer, where to put its status): nothing left to show.
auto Kernel::sceMpegAvcDecodeStop() -> void {
  if(arg(3)) memory.write(4, arg(3), 0);
  result(0);
}

//(handle): the decoder emptied: the next picture is held back again.
auto Kernel::sceMpegAvcDecodeFlush() -> void {
  if(u32 library = mpegLibrary(arg(0))) memory.write(4, library + LibraryHolding, 0);
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

//(handle, access unit, buffer, where to put whether a picture came).
auto Kernel::sceMpegAvcDecodeYCbCr() -> void {
  mpegDecoded(arg(0), arg(1), arg(3));
}

//(handle, buffer, where to put its status): nothing left to show.
auto Kernel::sceMpegAvcDecodeStopYCbCr() -> void {
  if(arg(2)) memory.write(4, arg(2), 0);
  result(0);
}

//(handle, details): the last picture decoded described: it's there. The details (the picture's size and the like) are
//left as they were: pspautotests imports the function (video/mpeg's imports name it) but records nothing of them.
//Space Invaders Extreme asks after each picture its movie thread decodes, and waits for the first before its stage
//starts.
auto Kernel::sceMpegAvcDecodeDetail() -> void { result(0); }

//(handle, YCbCr buffer, range, frame width, destination): a frame converted to pixels: no picture was decoded, the
//destination is left as it is.
auto Kernel::sceMpegAvcCsc() -> void { result(0); }

//(handle, access unit, buffer, initialized): ATRAC3plus sound from a stream that has none: "no data"'s error, as
//video/mpeg/basic recorded (0x807f00fd).
auto Kernel::sceMpegAtracDecode() -> void { result(0x807f'00fd); }
