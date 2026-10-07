//scePsmf, the library games read their movies' headers with: a PSMF movie (also called PMF) is a header, then an
//MPEG-2 program stream of H.264 pictures and, mostly, ATRAC3plus sound (mpeg.cpp plays one through sceMpeg,
//psmfplayer.cpp through scePsmfPlayer). A game loads the header into its memory, sets up a structure of its own over
//it (scePsmfSetPsmf) and asks it what the movie holds: its streams, their pictures' sizes and sound, its times, the
//entry points pictures can be decoded from. Games that play movies with sceMpeg themselves choose its streams so.
//
//How this was written (docs/psp-core.md, part 31): from a facts-only specification of the library's behaviour that a
//separate agent wrote from other emulators' accounts, pspautotests' recordings and the movies' own headers; its
//author's sources weren't seen here, and no emulator's code was read. pspautotests has no scePsmf program, so where
//the specification found its two accounts disagreeing, this file chooses, and says so where it does.
//
//The header (big-endian, from the PSMF's first byte):
//- 0x00 "PSMF"; 0x04 the version, four ASCII digits ("0012" to "0015" in the movies seen); 0x08 where the program
//  stream begins (the header's length, a multiple of 2048: 0x800 in every movie seen); 0x0c the stream's length.
//- 0x54 and 0x5a the presentation's start and end, six bytes each, in the 90 kHz clock's ticks (90000, one second,
//  is the start in every movie seen; the end is the last picture's time plus a picture's, 3003 ticks).
//- 0x80 how many streams (16 bits), then from 0x82 a table of 16-byte entries: the PES stream ID (0xe0 plus the
//  channel for H.264 video; 0xbd, private stream 1, for sound), the private ID (sound: 0x00 plus the channel for
//  ATRAC3plus, 0x10 plus it for linear PCM), two bytes of no known meaning, where the stream's entry point (EP) map
//  is and how many entries it has (both 0: none), a video stream's picture width and height in 16-pixel units, and
//  a sound stream's channel configuration (1 mono, 2 stereo) and rate code (2 for 44.1 kHz).
//- An EP map's entries are 10 bytes: two bytes of no known meaning, then a picture's time (absolute ticks) and the
//  pack its decoding can start from (counted from the program stream's start), each 32 bits.
//A stream's kind, as the functions number them: 0 H.264 video, 1 ATRAC3plus, 2 PCM, 3 user data, and 15 for either
//kind of sound. Private IDs 0x20-0x2f, and entries of no other kind, count as user data (the specification's ranges,
//as one of its accounts has them; the other took every private ID with high bits set for PCM).
//
//The game's structure, 32 bytes (the specification's two accounts agree on +0, +4 and +20; the rest follows the
//account that describes pointers into the header, kept to the 32 bytes both accounts write, so that a game's
//structure of either size isn't written past):
//- +0 the header's version word, +4 the header's size (0x800, whatever the stream's offset: both accounts),
//  +8 the stream's size, +12 and +16 the grouping period's and group's numbers (0), +20 the current stream's number,
//  +24 the header's address;
//- +28 Phobos's own: the current stream's kind (bits 0-7) and channel (8-15), kept when a search finds none, and the
//  video stream whose EP map the EP functions read (16-31: the one selected last, the first video stream at first).
//Everything is in the structure and the header, so a copy of a structure works on its own, with its own current
//stream, as games rely on; the header must stay where it is while the structure is used.
//
//Choices where the specification's accounts differ (none of them recorded on a PSP):
//- A structure that isn't set up (its +24 not leading to a PSMF header of its version): 0x80615001 from the
//  functions about streams (the number of streams, specifying and asking for the current one, the video and audio
//  information), 0x80615025 from the rest (times, sizes, the EP map, the version, the marks).
//- scePsmfSetPsmf refuses a header without "PSMF" (0x80615501), with a version word of 0 (0x80615002) or a stream
//  offset of 0 (0x806151fe); the other account refused nothing.
//- scePsmfVerifyPsmf refuses a header without "PSMF" or of a version other than the four known (0x80615501); and it
//  leaves the 0x100 bytes below the caller's stack pointer zeroed, as a PSP's leaves the word a game was found
//  reading there unset (the functions run on the caller's stack; what the others leave there isn't known).
//- scePsmfGetPsmfVersion gives the digits as a number (14 for "0014"), not the word as stored.
//- scePsmfSpecifyStream with a number no stream has: 0x80615100, the selection then unusable (the current stream's
//  number and kind 0x80615001). A search by kind and channel that finds none returns 0 and leaves no current stream
//  (its number 0x80615100), the kind and channel of the stream selected before still reported; kind 15 finds either
//  kind of sound.
//- scePsmfGetVideoInfo and GetAudioInfo answer from the current stream's entry (0x80615100 if it isn't of the kind;
//  0x80615001 with no current stream), not from the first video or the last audio entry whatever is selected.
//- The EP map read is the video stream selected last's. An entry's position is the pack count as the map holds it
//  (not multiplied into bytes). An index past the map: 0x80615100. A time before the presentation's start:
//  0x80615500; no EP map: 0x80615025; an entry is the last one whose time isn't after the time asked.
//- Marks (named points, UMD Video's chapters): no movie seen has any, and their layout isn't known: none is reported.

namespace {
  //scePsmf's errors, as the specification describes them
  constexpr u32 PsmfErrorUnusable = 0x8061'5001;     //no usable structure, or a selection made unusable
  constexpr u32 PsmfErrorVersion = 0x8061'5002;      //a header's version word of 0
  constexpr u32 PsmfErrorNotFound = 0x8061'5025;     //a structure not set up, no EP map, nothing found
  constexpr u32 PsmfErrorNoStream = 0x8061'5100;     //no stream (or entry) of that number; a stream of another kind
  constexpr u32 PsmfErrorBadOffset = 0x8061'51fe;    //a header's stream offset of 0
  constexpr u32 PsmfErrorBeforeStart = 0x8061'5500;  //a time before the presentation's start
  constexpr u32 PsmfErrorNotPsmf = 0x8061'5501;      //not a PSMF header

  //The game's structure (above)
  constexpr u32 PsmfVersion = 0, PsmfHeaderSize = 4, PsmfStreamSize = 8, PsmfGroupingPeriod = 12, PsmfGroup = 16,
                PsmfCurrent = 20, PsmfHeaderAt = 24, PsmfSelection = 28, PsmfStructure = 32;
  constexpr u32 PsmfNone = 0xffff'ffff, PsmfUnusable = 0xffff'fffe;  //+20 with no current stream
  constexpr u32 PsmfTableAt = 0x82, PsmfEntrySize = 16, PsmfEPEntrySize = 10;

  auto bigEndian(const u8* bytes, u32 count) -> u64 {
    u64 value = 0;
    for(u32 n = 0; n < count; n++) value = value << 8 | bytes[n];
    return value;
  }
}

//A stream's kind (Kernel::PsmfAvc and the rest) and channel, from its table entry's PES stream ID and private ID.
static auto psmfKind(u8 id, u8 privateID, u32& channel) -> u32 {
  if((id & 0xf0) == 0xe0) return channel = id & 15, Kernel::PsmfAvc;
  channel = privateID & 15;
  if(id == 0xbd && privateID < 0x10) return Kernel::PsmfAtrac;
  if(id == 0xbd && privateID < 0x20) return Kernel::PsmfPcm;
  return Kernel::PsmfUserData;
}

//Whether a stream is of a kind asked for: the kind itself, or 15 for either kind of sound.
static auto psmfOfKind(const Kernel::PsmfStream& stream, u32 kind) -> bool {
  if(kind == Kernel::PsmfAnyAudio) return stream.kind == Kernel::PsmfAtrac || stream.kind == Kernel::PsmfPcm;
  return stream.kind == kind;
}

//A PSMF header's fields, from its first size bytes (at least its stream table's): false if it isn't one (no "PSMF"
//mark, or a table past the bytes given).
auto Kernel::psmfParse(const u8* bytes, u32 size, PsmfHeader& header) -> bool {
  if(size < PsmfTableAt || memcmp(bytes, "PSMF", 4)) return false;
  header.version = bytes[4] | bytes[5] << 8 | bytes[6] << 16 | u32(bytes[7]) << 24;
  header.streamOffset = bigEndian(bytes + 8, 4);
  header.streamSize = bigEndian(bytes + 12, 4);
  header.startTime = bigEndian(bytes + 0x54, 6);
  header.endTime = bigEndian(bytes + 0x5a, 6);
  u32 count = bigEndian(bytes + 0x80, 2);
  if(PsmfTableAt + count * PsmfEntrySize > size) return false;
  header.streams.clear();
  for(u32 n = 0; n < count; n++) {
    const u8* entry = bytes + PsmfTableAt + n * PsmfEntrySize;
    PsmfStream stream;
    stream.id = entry[0], stream.privateID = entry[1];
    stream.kind = psmfKind(stream.id, stream.privateID, stream.channel);
    stream.epOffset = bigEndian(entry + 4, 4);
    stream.epCount = bigEndian(entry + 8, 4);
    stream.width = entry[12] * 16, stream.height = entry[13] * 16;
    stream.channels = entry[14], stream.rate = entry[15];
    header.streams.push_back(stream);
  }
  return true;
}

//An EP map's entry from its 10 bytes.
auto Kernel::psmfEntry(const u8* bytes) -> PsmfEntry {
  return {u32(bigEndian(bytes + 2, 4)), u32(bigEndian(bytes + 6, 4)), bytes[0], bytes[1]};
}

//A PSMF header in the game's memory: false if it isn't one, or doesn't fit in memory.
auto Kernel::psmfRead(u32 address, PsmfHeader& header) -> bool {
  if(!memory.reaches(address, PsmfTableAt)) return false;
  u8 head[PsmfTableAt];
  memory.copyOut(head, address, PsmfTableAt);
  u32 size = PsmfTableAt + bigEndian(head + 0x80, 2) * PsmfEntrySize;
  if(!memory.reaches(address, size)) return false;
  std::vector<u8> bytes(size);
  memory.copyOut(bytes.data(), address, size);
  return psmfParse(bytes.data(), size, header);
}

//The header a game's structure leads to, as scePsmfSetPsmf set it up: +24 holding the address of a PSMF header of
//the version at +0. 0, or notSet for a structure that isn't set up.
auto Kernel::psmfStructure(u32 structure, PsmfHeader& header, u32 notSet) -> u32 {
  if(!memory.reaches(structure, PsmfStructure)) return notSet;
  u32 at = memory.read(4, structure + PsmfHeaderAt);
  if(!psmfRead(at, header) || header.version != memory.read(4, structure + PsmfVersion)) return notSet;
  return 0;
}

//A stream made the structure's current one: its number, kind and channel; a video stream's EP map is the one read
//from now on.
auto Kernel::psmfSelect(u32 structure, const PsmfHeader& header, u32 number) -> void {
  auto& stream = header.streams[number];
  u32 mapped = stream.kind == PsmfAvc ? number : memory.read(4, structure + PsmfSelection) >> 16;
  memory.write(4, structure + PsmfCurrent, number);
  memory.write(4, structure + PsmfSelection, stream.kind | stream.channel << 8 | mapped << 16);
}

//The EP map the structure's EP functions read (the video stream selected last's): where it is in memory and how many
//entries it has, 0 if the stream has none.
auto Kernel::psmfMap(u32 structure, const PsmfHeader& header, u32& at) -> u32 {
  u32 mapped = memory.read(4, structure + PsmfSelection) >> 16;
  if(mapped >= header.streams.size() || header.streams[mapped].kind != PsmfAvc) return 0;
  auto& stream = header.streams[mapped];
  at = memory.read(4, structure + PsmfHeaderAt) + stream.epOffset;
  if(!stream.epOffset || !memory.reaches(at, stream.epCount * PsmfEPEntrySize)) return 0;
  return stream.epCount;
}

//(structure, header): the structure set up over a header in the game's memory (above), stream 0 current.
auto Kernel::scePsmfSetPsmf() -> void {
  u32 structure = arg(0), at = arg(1);
  if(!memory.reaches(structure, PsmfStructure)) return result(ErrorInvalidPointer);
  PsmfHeader header;
  if(!psmfRead(at, header)) return result(PsmfErrorNotPsmf);
  if(!header.version) return result(PsmfErrorVersion);
  if(!header.streamOffset) return result(PsmfErrorBadOffset);
  u32 words[8] = {header.version, 0x800, header.streamSize, 0, 0, PsmfNone, at, 0};
  for(u32 n = 0; n < 8; n++) memory.write(4, structure + n * 4, words[n]);
  for(u32 n = 0; n < header.streams.size(); n++) {  //the first video stream's EP map, until another is selected
    if(header.streams[n].kind == PsmfAvc) { memory.write(4, structure + PsmfSelection, n << 16); break; }
  }
  if(!header.streams.empty()) psmfSelect(structure, header, 0);
  result(0);
}

//(header): 0 for a PSMF header of a version the library knows ("0012" to "0015"), else 0x80615501. It runs on the
//caller's stack, and leaves the 0x100 bytes below its stack pointer zeroed.
auto Kernel::scePsmfVerifyPsmf() -> void {
  u32 below = (cpu.ipu.r[29] - 0x100) & ~3u;
  if(memory.reaches(below, 0x100)) memory.fill(below, 0, 0x100);
  u32 at = arg(0);
  if(!memory.reaches(at, 8) || memory.readString(at, 4) != "PSMF") return result(PsmfErrorNotPsmf);
  u32 version = memory.read(4, at + 4);
  for(auto known : {"0012", "0013", "0014", "0015"}) {
    if(version == u32(known[0] | known[1] << 8 | known[2] << 16 | known[3] << 24)) return result(0);
  }
  result(PsmfErrorNotPsmf);
}

//(header, where to put the word): the stream's offset (the header's big-endian word at 8) or size (at 12) written
//as a word of the CPU's; nothing about the header is checked.
auto Kernel::scePsmfQueryStreamOffset() -> void {
  if(!memory.reaches(arg(0), 16)) return result(ErrorInvalidPointer);
  u8 bytes[4];
  memory.copyOut(bytes, arg(0) + 8, 4);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), bigEndian(bytes, 4));
  result(0);
}

auto Kernel::scePsmfQueryStreamSize() -> void {
  if(!memory.reaches(arg(0), 16)) return result(ErrorInvalidPointer);
  u8 bytes[4];
  memory.copyOut(bytes, arg(0) + 12, 4);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), bigEndian(bytes, 4));
  result(0);
}

//(structure): the header's version, its four digits as a number.
auto Kernel::scePsmfGetPsmfVersion() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  u32 number = 0;
  for(u32 n = 0; n < 4; n++) number = number * 10 + (u8(header.version >> n * 8) - '0') % 10;
  result(number);
}

//(structure, where to put it): the header's size (0x800), the stream's size, and the presentation's start and end
//times (absolute ticks, as words).
auto Kernel::scePsmfGetHeaderSize() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), 0x800);
  result(0);
}

auto Kernel::scePsmfGetStreamSize() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), header.streamSize);
  result(0);
}

auto Kernel::scePsmfGetPresentationStartTime() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), u32(header.startTime));
  result(0);
}

auto Kernel::scePsmfGetPresentationEndTime() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), u32(header.endTime));
  result(0);
}

//(structure): how many streams the header lists.
auto Kernel::scePsmfGetNumberOfStreams() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorUnusable)) return result(error);
  result(header.streams.size());
}

//(structure, kind): how many streams of a kind it lists (15: of either kind of sound; a kind there isn't: none).
auto Kernel::scePsmfGetNumberOfSpecificStreams() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorUnusable)) return result(error);
  result(std::count_if(header.streams.begin(), header.streams.end(), [&](auto& s) { return psmfOfKind(s, arg(1)); }));
}

//(structure, number): that stream made the current one; a number no stream has leaves no usable selection.
auto Kernel::scePsmfSpecifyStream() -> void {
  u32 structure = arg(0), number = arg(1);
  PsmfHeader header;
  if(u32 error = psmfStructure(structure, header, PsmfErrorUnusable)) return result(error);
  if(number >= header.streams.size()) {
    memory.write(4, structure + PsmfCurrent, PsmfUnusable);
    return result(PsmfErrorNoStream);
  }
  psmfSelect(structure, header, number);
  result(0);
}

//(structure, kind, channel): the first stream of that kind (15: either kind of sound) and channel made the current
//one. None: no current stream, and still 0.
auto Kernel::scePsmfSpecifyStreamWithStreamType() -> void {
  u32 structure = arg(0), kind = arg(1), channel = arg(2);
  PsmfHeader header;
  if(u32 error = psmfStructure(structure, header, PsmfErrorUnusable)) return result(error);
  for(u32 n = 0; n < header.streams.size(); n++) {
    if(psmfOfKind(header.streams[n], kind) && header.streams[n].channel == channel) {
      psmfSelect(structure, header, n);
      return result(0);
    }
  }
  memory.write(4, structure + PsmfCurrent, PsmfNone);
  result(0);
}

//(structure, kind, n): the n-th stream of that kind (15: of either kind of sound), counting from 0, made the current
//one. None: 0x80615100, the selection as it was.
auto Kernel::scePsmfSpecifyStreamWithStreamTypeNumber() -> void {
  u32 structure = arg(0), kind = arg(1), wanted = arg(2);
  PsmfHeader header;
  if(u32 error = psmfStructure(structure, header, PsmfErrorUnusable)) return result(error);
  u32 seen = 0;
  for(u32 n = 0; n < header.streams.size(); n++) {
    if(!psmfOfKind(header.streams[n], kind) || seen++ != wanted) continue;
    psmfSelect(structure, header, n);
    return result(0);
  }
  result(PsmfErrorNoStream);
}

//(structure): the current stream's number.
auto Kernel::scePsmfGetCurrentStreamNumber() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorUnusable)) return result(error);
  u32 current = memory.read(4, arg(0) + PsmfCurrent);
  if(current == PsmfNone) return result(PsmfErrorNoStream);
  if(current >= header.streams.size()) return result(PsmfErrorUnusable);
  result(current);
}

//(structure, where to put its kind, and its channel): the current stream's (with no current stream, those of the
//kind and channel last asked for).
auto Kernel::scePsmfGetCurrentStreamType() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorUnusable)) return result(error);
  u32 current = memory.read(4, arg(0) + PsmfCurrent);
  if(current != PsmfNone && current >= header.streams.size()) return result(PsmfErrorUnusable);
  u32 selection = memory.read(4, arg(0) + PsmfSelection);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), selection & 0xff);
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), selection >> 8 & 0xff);
  result(0);
}

//The current stream, of a kind: what scePsmfGetVideoInfo and GetAudioInfo answer from. 0, or the error.
auto Kernel::psmfCurrent(u32 structure, PsmfHeader& header, u32 kind, PsmfStream& stream) -> u32 {
  if(u32 error = psmfStructure(structure, header, PsmfErrorUnusable)) return error;
  u32 current = memory.read(4, structure + PsmfCurrent);
  if(current >= header.streams.size()) return PsmfErrorUnusable;
  stream = header.streams[current];
  return psmfOfKind(stream, kind) ? 0 : PsmfErrorNoStream;
}

//(structure, where to put 8 bytes): the current video stream's picture size in pixels, its width then its height.
auto Kernel::scePsmfGetVideoInfo() -> void {
  PsmfHeader header;
  PsmfStream stream;
  if(u32 error = psmfCurrent(arg(0), header, PsmfAvc, stream)) return result(error);
  if(memory.reaches(arg(1), 8)) memory.write(4, arg(1), stream.width), memory.write(4, arg(1) + 4, stream.height);
  result(0);
}

//(structure, where to put 8 bytes): the current sound stream's channel configuration (1 mono, 2 stereo) and its rate
//code (2: 44.1 kHz), as the header has them.
auto Kernel::scePsmfGetAudioInfo() -> void {
  PsmfHeader header;
  PsmfStream stream;
  if(u32 error = psmfCurrent(arg(0), header, PsmfAnyAudio, stream)) return result(error);
  if(memory.reaches(arg(1), 8)) memory.write(4, arg(1), stream.channels), memory.write(4, arg(1) + 4, stream.rate);
  result(0);
}

//(structure): how many entries the EP map has (0 for none).
auto Kernel::scePsmfGetNumberOfEPentries() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  u32 at = 0;
  result(psmfMap(arg(0), header, at));
}

//(structure): 0 if there's an EP map with entries, else 0x80615025.
auto Kernel::scePsmfCheckEPmap() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  u32 at = 0;
  result(psmfMap(arg(0), header, at) ? 0 : PsmfErrorNotFound);
}

//An EP map's entry written for the game, 16 bytes: its picture's time (absolute ticks), its pack (counted from the
//program stream's start), and its two bytes of no known meaning.
auto Kernel::psmfWriteEntry(u32 at, u32 address) -> void {
  u8 bytes[PsmfEPEntrySize];
  memory.copyOut(bytes, at, PsmfEPEntrySize);
  auto entry = psmfEntry(bytes);
  if(!memory.reaches(address, 16)) return;
  u32 words[4] = {entry.time, entry.pack, entry.first, entry.second};
  for(u32 n = 0; n < 4; n++) memory.write(4, address + n * 4, words[n]);
}

//(structure, index, where to put 16 bytes): an entry of the EP map.
auto Kernel::scePsmfGetEPWithId() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  u32 at = 0, count = psmfMap(arg(0), header, at);
  if(!count) return result(PsmfErrorNotFound);
  if(arg(1) >= count) return result(PsmfErrorNoStream);
  psmfWriteEntry(at + arg(1) * PsmfEPEntrySize, arg(2));
  result(0);
}

//The EP map's entry for a time (absolute ticks), the last whose time isn't after it: where the map is, and the
//entry's index. 0, or the error.
auto Kernel::psmfFindEntry(u32 structure, u32 time, u32& at, u32& index) -> u32 {
  PsmfHeader header;
  if(u32 error = psmfStructure(structure, header, PsmfErrorNotFound)) return error;
  if(time < header.startTime) return PsmfErrorBeforeStart;
  u32 count = psmfMap(structure, header, at);
  if(!count) return PsmfErrorNotFound;
  bool found = false;
  for(u32 n = 0; n < count; n++) {
    u8 bytes[PsmfEPEntrySize];
    memory.copyOut(bytes, at + n * PsmfEPEntrySize, PsmfEPEntrySize);
    if(psmfEntry(bytes).time <= time) found = true, index = n;
  }
  return found ? 0 : PsmfErrorNotFound;
}

//(structure, time, where to put 16 bytes): the entry for that time (above).
auto Kernel::scePsmfGetEPWithTimestamp() -> void {
  u32 at = 0, index = 0;
  if(u32 error = psmfFindEntry(arg(0), arg(1), at, index)) return result(error);
  psmfWriteEntry(at + index * PsmfEPEntrySize, arg(2));
  result(0);
}

//(structure, time): the index of the entry for that time.
auto Kernel::scePsmfGetEPidWithTimestamp() -> void {
  u32 at = 0, index = 0;
  if(u32 error = psmfFindEntry(arg(0), arg(1), at, index)) return result(error);
  result(index);
}

//(structure): how many marks the movie has: none known (above).
auto Kernel::scePsmfGetNumberOfPsmfMarks() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  result(0);
}

//(structure, ?, mark, where to put it): no mark has the number asked for.
auto Kernel::scePsmfGetPsmfMark() -> void {
  PsmfHeader header;
  if(u32 error = psmfStructure(arg(0), header, PsmfErrorNotFound)) return result(error);
  result(PsmfErrorNoStream);
}
