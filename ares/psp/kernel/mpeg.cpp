//sceMpeg, the library games play their movies with (PSMF files: H.264 video and ATRAC3plus sound), as stubs that let
//a game set a movie up and then find it can't be played, so that it skips it and carries on: there's no video
//decoder yet. The setting up works as on a PSP, with the sizes pspautotests' video/mpeg tests recorded (a
//ringbuffer's memory: 0x868 bytes a packet; the library's: 0x10000), so games make their buffers and threads as
//they would; then sceMpegQueryStreamOffset, which reads the movie's header, finds no stream it can play in any
//file, and the game gives the movie up (the error is 0x80610022, the one video/mpeg/ringbuffer/construct recorded
//for a value the library refuses; which a PSP gives for a header it can't read isn't known here, but games only
//test it for a negative number). GTA Liberty City Stories and Vice City Stories go on to their loading screens so,
//as they did when no sceMpeg function was there at all.
//
//The functions a game calls after the header was read (registering streams, feeding the ringbuffer, getting and
//decoding access units) answer as if the stream had nothing in it: no access unit (0x80618001, the "no data" that
//video/mpeg/basic recorded for a stream not yet fed), nothing put into the ringbuffer. A game that plays on
//regardless sees its movie end at once.

namespace {
  constexpr u32 MpegErrorValue = 0x8061'0022, MpegErrorNoData = 0x8061'8001;
  constexpr u32 RingbufferPacketMemory = 0x868, PacketSize = 2048;
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
  result(0x10000);
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

//(ringbuffer): its free packets, all of them: nothing is ever taken from it to be played.
auto Kernel::sceMpegRingbufferAvailableSize() -> void {
  if(!memory.reaches(arg(0), 4)) return result(ErrorInvalidPointer);
  result(memory.read(4, arg(0)));
}

//(ringbuffer, packets, available): nothing is put in (the callback that would read the movie isn't called).
auto Kernel::sceMpegRingbufferPut() -> void { result(0); }

//(handle, data, size, ringbuffer, frame width, mode, DDR top): the library set up in the memory given, its handle
//written first, which points at its "LIBMPEG" signature there (video/mpeg/basic shows it).
auto Kernel::sceMpegCreate() -> void {
  u32 handle = arg(0), data = arg(1), size = arg(2);
  if(size < 0x10000) return result(ErrorNoMemory);
  if(!memory.reaches(handle, 4) || !memory.reaches(data, 16)) return result(ErrorInvalidPointer);
  memory.write(4, handle, data + 0x30);
  memory.copyIn(data + 0x30, "LIBMPEG", 8);
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

//What follows a header that was read: streams registered (a handle each), buffers handed out, access units set up;
//none ever has anything in it.
auto Kernel::sceMpegRegistStream() -> void { result(0x12c0); }
auto Kernel::sceMpegUnRegistStream() -> void { result(0); }
auto Kernel::sceMpegMallocAvcEsBuf() -> void { result(1); }
auto Kernel::sceMpegFreeAvcEsBuf() -> void { result(0); }
auto Kernel::sceMpegInitAu() -> void { result(0); }
auto Kernel::sceMpegFlushAllStream() -> void { result(0); }
auto Kernel::sceMpegFlushStream() -> void { result(0); }
auto Kernel::sceMpegGetAvcAu() -> void { result(MpegErrorNoData); }
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

//(handle, access unit, frame width, buffer, where to put whether a frame came): no frame.
auto Kernel::sceMpegAvcDecode() -> void {
  if(arg(4)) memory.write(4, arg(4), 0);
  result(0);
}

//(handle, frame width, buffer, where to put its status): nothing left to show.
auto Kernel::sceMpegAvcDecodeStop() -> void {
  if(arg(3)) memory.write(4, arg(3), 0);
  result(0);
}

//(handle): nothing waiting to be decoded.
auto Kernel::sceMpegAvcDecodeFlush() -> void { result(0); }

//(handle, mode, width, height, where to put the size): the memory a decoded YCbCr frame of that size takes: its
//luma and its two quarter-size chroma planes (width x height x 3 / 2, at least 0x80 apiece for the library's own
//bookkeeping: chosen, games only hand the size back in sceMpegAvcInitYCbCr).
auto Kernel::sceMpegAvcQueryYCbCrSize() -> void {
  u32 width = arg(2), height = arg(3);
  if(width > 0x1000 || height > 0x1000) return result(MpegErrorValue);
  if(arg(4)) memory.write(4, arg(4), ((width * height * 3 / 2 + 0x7f) & ~0x7fu) + 0x80);
  result(0);
}

//(handle, mode, width, height, buffer): the buffer set up for decoded frames (none ever come).
auto Kernel::sceMpegAvcInitYCbCr() -> void { result(0); }

//(handle, access unit, buffer, where to put whether a frame came): no frame.
auto Kernel::sceMpegAvcDecodeYCbCr() -> void {
  if(arg(3)) memory.write(4, arg(3), 0);
  result(0);
}

//(handle, buffer, where to put its status): nothing left to show.
auto Kernel::sceMpegAvcDecodeStopYCbCr() -> void {
  if(arg(2)) memory.write(4, arg(2), 0);
  result(0);
}

//(handle, YCbCr buffer, range, frame width, destination): a frame converted to pixels: there's none, the
//destination is left as it is.
auto Kernel::sceMpegAvcCsc() -> void { result(0); }

//(handle, access unit, buffer, initialized): ATRAC3plus sound from a stream that has none: "no data"'s error, as
//video/mpeg/basic recorded (0x807f00fd).
auto Kernel::sceMpegAtracDecode() -> void { result(0x807f'00fd); }
