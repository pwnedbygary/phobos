//scePsmfPlayer, the library games play PSMF movies with from their files: the game makes a player, gives it a
//movie's file (scePsmfPlayerSetPsmf), starts it, and from then on calls scePsmfPlayerUpdate once a vertical blank,
//asks for the current picture (scePsmfPlayerGetVideoData, converted into its buffer) and, on its sound thread, for
//the sound a frame at a time (scePsmfPlayerGetAudioData), until the status says the movie has ended. Games ship the
//library as a module of their own (Sony's libpsmfplayer.prx); the HLE kernel stands in for it (modules.cpp), its
//functions answered here. The movie is read and taken apart as mpeg.cpp takes sceMpeg's apart (an MPEG-2 program
//stream: H.264 pictures in stream 0xe0 plus a channel, ATRAC3plus sound in private stream 1) and decoded by
//codec.cpp's decoders (FFmpeg's).
//
//How this was written (docs/psp-core.md, part 31): from a facts-only specification of the library's behaviour that a
//separate agent wrote from other emulators' accounts, pspautotests' recordings (its video/psmfplayer programs, run
//on a PSP: the strongest evidence) and the movies' own headers; its author's sources weren't seen here, and no
//emulator's code was read. The recordings are followed where they say anything; the choices made beyond them are
//noted where they're made.
//
//The statuses: 1 made, 2 a movie set (standby), 4 playing, 0x200 played to its end. A handle is the address of a
//word holding the player's handle value, any copy of it working alike; a word of 0, or a value no player has, is
//refused (0x80616001) by every function but scePsmfPlayerCreate, as is a call the player's status doesn't allow.
//Only one player exists at a time (a second: 0x80618005).
//
//Playing, as the recordings show it and the model here has it (the PSP's player reads and decodes on threads of its
//own; here the work is done in the game's calls, and its timing is what the recordings saw):
//- Start-up. After scePsmfPlayerStart (and whenever playing starts again: a loop, leaving fast forward or rewind) the
//  player decodes three pictures ahead (and has three sound frames, when sound is played) before the first picture
//  comes; it decodes one picture each scePsmfPlayerGetVideoData call, so the first picture comes on the third call
//  (basic: two calls refused with 0x8061600c, the third gave the picture at time 0). Until then GetVideoData gives
//  the picture shown last, if there is one (a restart keeps it), else 0x8061600c; GetAudioData gives no sound.
//- Pacing. scePsmfPlayerUpdate counts down toward the next picture: in play mode one is due two Updates after the
//  last was handed out (basic: two Updates a picture, times 0, 3003, 6006...; playmode: 49 in 99 Updates), the count
//  starting again at each picture handed out (with three Updates a picture, the end still came on the second Update
//  after the last: getvideodata, update). GetVideoData hands out at most one new picture a call, the next in order,
//  and writes the current picture into the game's buffer at every call that succeeds (in pause and after the end,
//  the same picture again). The video's own frame rate isn't looked at: an Update a vertical blank makes 29.97.
//- Sound. When sound is played, a picture that's due is held while it's more than two pictures (6006 ticks) ahead of
//  the next sound frame waiting to be taken, and when the sound is more than four pictures ahead of the next one,
//  two pictures are passed over (decoded, not shown) before the next is handed out: the game's taking of the sound,
//  as its output plays it, is the clock. The first frame after a start comes out silent; frames more than one frame
//  before the start, or more than two pictures before the first picture, are dropped. (None of this is recorded: the
//  test movies have no sound. It's the specification's more detailed account.)
//- The end. The movie has ended when everything is read and decoded, the last picture has been handed out and two
//  Updates have passed since, and the sound (if played) has all been taken. Not looping, the status becomes 0x200 at
//  once if the player's threads' priority is better than the calling thread's (getvideodata and update: during the
//  second Update), else as the calling thread next waits (worse: at its next call that blocks; here also once a
//  vertical blank has come since, as a thread that waited for it let the player's run). Looping, playing starts
//  again from the start, in play mode, the status staying 4.
//- Play modes: slow motion a picture every six Updates (playmode: 17 in 99), pause and step frame none, but in all
//  three the picture already due when the mode changed still comes (playmode: one more picture after switching to
//  pause or step frame); asking for step frame again while in it steps a picture (chosen). Fast forward and rewind
//  (only for a movie whose every video stream has an EP map, a "full" one) show EP entries' pictures, the speed's
//  number of entries on, one every 15, 20 or 25 Updates for speeds 1 to 3, without sound; rewinding to the first
//  entry plays on from the start, going forward past the last ends the movie (the specification's account; no PSP
//  recorded either, as no test movie has an EP map).
//- Calls that block (the caller waits, other threads running), as the recordings' marks show: scePsmfPlayerCreate,
//  Delete and ReleasePsmf, SetPsmf and its kin (but not when refused for the status or a NULL path), and
//  GetVideoData when it succeeds; how long isn't measured (PsmfPlayerWait's moments are chosen).
//
//Pictures are converted as sceMpeg's (BT.601, video's range) into the pixel format ConfigPlayer set (8888 at first),
//as many rows as the picture has, each as many pixels as its width, the buffer width apart (0 standing for 512, an
//odd one losing its lowest bit), the rest of each row left alone; with alpha zero, in every format, as the PSP's
//(configplayer told the formats apart by their zero alpha bits). Sound frames are 2048 stereo 16-bit samples, 8192
//bytes; only ATRAC3plus is played (a PCM stream chosen plays without sound: no movie seen has one).

namespace {
  //scePsmfPlayer's errors, as pspautotests' video/psmfplayer recorded them
  constexpr u32 PlayerErrorNoPlayer = 0x8061'6001;      //no player behind the handle, or a call its status refuses
  constexpr u32 PlayerErrorNotSupported = 0x8061'6003;  //a codec, a mode the movie can't do, no stream to switch to
  constexpr u32 PlayerErrorBufferSize = 0x8061'6005;    //Create's buffer too small
  constexpr u32 PlayerErrorBadKey = 0x8061'6006;        //an unknown key or play mode, a stream number out of range
  constexpr u32 PlayerErrorBadValue = 0x8061'6008;      //a priority, NULL path, mode, start time or value refused
  constexpr u32 PlayerErrorNoData = 0x8061'600c;        //nothing to hand out now
  constexpr u32 PlayerErrorInUse = 0x8061'8005;         //a second player
  constexpr u32 PlayerErrorFile = 0x8002'00d2;          //SetPsmf's every failure
  constexpr u32 PlayerErrorNegativeWidth = 0x8000'0023, PlayerErrorNarrowWidth = 0x8000'01fe;

  constexpr u32 StatusMade = 1, StatusStandby = 2, StatusPlaying = 4, StatusEnded = 0x200;
  constexpr s32 ModePlay = 0, ModeSlow = 1, ModeStep = 2, ModePause = 3, ModeForward = 4, ModeRewind = 5;
  constexpr u32 PictureTicks = 3003;    //a picture's time at 29.97 a second, in the 90 kHz clock's ticks
  constexpr u32 Never = ~0u;            //due: no picture coming
  constexpr u32 Ahead = 3;              //pictures (and sound frames) decoded ahead
  constexpr u32 SoundFrameBytes = 0x2000;
  constexpr u32 PlayerBufferLeast = 0x28'5800, TempBufferLeast = 0x1'0000;
  //The handle: on a PSP, the address of the library's block in user memory, whose first word pointed at another
  //word reading 0 (basic printed both); here two words of kernel memory beside the trampoline, the same shape.
  constexpr u32 PlayerBlock = Kernel::Trampoline + 0xa0, PlayerHandle = PlayerBlock + 8;
  //how long the calls that block wait, in microseconds (chosen: the PSP's aren't measured): making and deleting the
  //player, letting a movie go, and handing out a picture (the Media Engine's moment, as sceMpeg's decode waits)
  constexpr u32 CreateMicroseconds = 1000, DeleteMicroseconds = 1000, ReleaseMicroseconds = 500;
  constexpr u32 PictureMicroseconds = MpegDecodeMicroseconds;
  constexpr u32 ChunkPacks = 32;        //the file is read 64 KiB at a time
  constexpr u32 SoundReadAhead = 64;    //the packs a call may read looking for sound
  constexpr u32 StreamLimit = 4 * 1024 * 1024;  //data held for a stream not being taken, at most

  auto bigEndian32(const u8* bytes) -> u32 { return bytes[0] << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3]; }

  //Drops a stream's first bytes (to used), with the time stamps that fell in them: those after the last unit's
  //start (taken) are for the next ones, which start at 0 now.
  auto dropTaken(std::vector<u8>& stream, std::vector<std::pair<u32, u64>>& stamps, u32 start, u32 used) -> void {
    stream.erase(stream.begin(), stream.begin() + used);
    std::vector<std::pair<u32, u64>> kept;
    for(auto [at, time] : stamps) if(at > start) kept.push_back({at > used ? at - used : 0, time});
    stamps = kept;
  }

  //A stream held to at most StreamLimit bytes, its oldest dropped (sound the game never takes, say).
  auto holdTo(std::vector<u8>& stream, std::vector<std::pair<u32, u64>>& stamps) -> void {
    if(stream.size() <= StreamLimit) return;
    u32 drop = stream.size() - StreamLimit;
    dropTaken(stream, stamps, drop, drop);
  }

  //The sound's next whole ATRAC3plus frame from at on (its 8-byte header, 0x0fd0 and the codec's parameters, the
  //frame's size in 8-byte steps less one in their low 10 bits, then the frame): where it starts and how long it is
  //with its header. Bytes before a header (a stream joined part way) are passed over.
  auto soundFrame(const std::vector<u8>& sound, u32 at, u32& from, u32& bytes) -> bool {
    from = at;
    while(from + 1 < sound.size() && !(sound[from] == 0x0f && sound[from + 1] == 0xd0)) from++;
    if(from + 8 > sound.size()) return false;
    bytes = 8 + (((sound[from + 2] << 8 | sound[from + 3]) & 0x3ff) + 1) * 8;
    return from + bytes <= sound.size();
  }
}

//The player behind a handle (the address of a word holding its handle value), or null. A movie's end noticed as the
//player's threads get their turn (psmfPlayerEnd()) has come by the next vertical blank.
auto Kernel::psmfPlayerFor(u32 handle) -> PsmfPlayer* {
  if(!psmfPlayer.status || !memory.reaches(handle, 4) || memory.read(4, handle) != PlayerHandle) return nullptr;
  if(psmfPlayer.ending && vblanks != psmfPlayer.endingAt) psmfPlayerEnded();
  return &psmfPlayer;
}

//A call that blocks returns value once the calling thread has waited length cycles, other threads running
//meanwhile (callbacks too, for the functions whose names end in CB). The player's threads run then: a movie's end
//waiting for them comes now.
auto Kernel::psmfPlayerWait(u32 value, u64 length, bool callbacks) -> void {
  result(value);
  if(!current || interrupting || dispatchSuspended || !interruptsEnabled) return;
  if(psmfPlayer.ending) psmfPlayerEnded();
  current->waitCount = value;
  block(Wait::Psmf, 0, cycles + length, 0, callbacks);
}

//The movie's header (as SetPsmf read it): false if there's no movie.
auto Kernel::psmfPlayerHeader(PsmfHeader& header) const -> bool {
  return !psmfPlayer.header.empty() && psmfParse(psmfPlayer.header.data(), psmfPlayer.header.size(), header);
}

//Opens a movie: its file, and the PSMF header at offset in it (2048 bytes, then the rest of it if the program stream
//begins later), which must be one whose stream begins inside the file. The bytes read (the device's time), or 0 if
//it can't be opened: no such file, a folder, not a PSMF there. (Without decoders every movie is refused, as sceMpeg
//refuses headers then: games give their movies up.)
auto Kernel::psmfPlayerOpen(const std::string& path, u64 offset) -> u64 {
  auto& p = psmfPlayer;
  u32 file = openFile(path, OpenRead);
  if(s32(file) < 0) return 0;
  auto& open = files[file];
  std::vector<u8> bytes;
  open.position = offset;
  readOpenFile(open, PacketSize, bytes);
  u64 read = bytes.size();
  PsmfHeader header;
  bool good = videoDecoders && psmfParse(bytes.data(), bytes.size(), header)
           && header.streamOffset >= 0x82 + header.streams.size() * 16 && header.streamOffset <= 16 * 1024 * 1024;
  if(good && header.streamOffset > bytes.size()) {  //a longer header (a long movie's EP map)
    std::vector<u8> rest;
    readOpenFile(open, header.streamOffset - bytes.size(), rest);
    read += rest.size();
    bytes.insert(bytes.end(), rest.begin(), rest.end());
    good = bytes.size() == header.streamOffset && psmfParse(bytes.data(), bytes.size(), header);
  }
  if(good) {  //the stream begins inside the file
    std::vector<u8> first;
    open.position = offset + header.streamOffset;
    readOpenFile(open, 1, first);
    good = first.size() == 1;
  }
  if(!good) {
    files.erase(file);
    return 0;
  }
  bytes.resize(header.streamOffset);
  open.position = 0;
  p.file = file;
  p.offset = offset;
  p.header = std::move(bytes);
  p.shown = {};
  p.chunk.clear();
  return std::max<u64>(read, 1);
}

//The movie let go: its file closed, and everything read of it.
auto Kernel::psmfPlayerClose() -> void {
  auto& p = psmfPlayer;
  if(p.file) files.erase(p.file);
  p.file = 0, p.offset = 0;
  p.header.clear();
  psmfPlayerRestart(0);
  p.shown = {};
  p.chunk.clear();
  p.decoder.reset(), p.sound.reset();
  p.soundLast.clear();
}

//A pack of the program stream, read from the movie's file (a chunk at a time): false past the file's end, or with
//its file gone (dropped as a state loaded without it).
auto Kernel::psmfPlayerPack(u32 pack, u8* data) -> bool {
  auto& p = psmfPlayer;
  if(pack < p.chunkPack || u64(pack - p.chunkPack + 1) * PacketSize > p.chunk.size()) {
    auto found = files.find(p.file);
    if(found == files.end() || p.header.size() < 12) return false;
    auto& open = found->second;
    u64 position = open.position;
    open.position = p.offset + bigEndian32(p.header.data() + 8) + u64(pack) * PacketSize;
    readOpenFile(open, ChunkPacks * PacketSize, p.chunk);
    open.position = position;
    p.chunkPack = pack;
    if(p.chunk.size() < PacketSize) return false;
  }
  memcpy(data, p.chunk.data() + u64(pack - p.chunkPack) * PacketSize, PacketSize);
  return true;
}

//The program stream's next pack read into the streams played: the video stream's data, and the sound stream's (its
//packets' data less their 4-byte headers, the first of which is the stream's private ID), with where their PES time
//stamps fall. False at the stream's end (the header's stream size: the PSP's library plays that much, not to the
//file's end), or at the file's.
auto Kernel::psmfPlayerRead() -> bool {
  auto& p = psmfPlayer;
  PsmfHeader header;
  if(!psmfPlayerHeader(header)) return false;
  u32 packs = header.streamSize / PacketSize;
  if(p.nextPack >= packs) return false;
  u8 packet[PacketSize];
  if(!psmfPlayerPack(p.nextPack, packet)) {
    p.nextPack = packs;
    return false;
  }
  p.nextPack++;
  pesPackets(packet, [&](u8 id, u32 payload, u32 end, u64 presented, u64) {
    if(id == p.videoID) {
      if(presented != NoTime) p.videoStamps.push_back({u32(p.video.size()), presented});
      p.video.insert(p.video.end(), packet + payload, packet + end);
    } else if(id == 0xbd && p.audioID >= 0 && payload + 4 <= end && packet[payload] == p.audioID) {
      if(presented != NoTime) p.audioStamps.push_back({u32(p.audio.size()), presented});
      p.audio.insert(p.audio.end(), packet + payload + 4, packet + end);
    }
  });
  holdTo(p.audio, p.audioStamps);
  return true;
}

//Whether the video has all been taken: every pack read, and no access unit left.
auto Kernel::psmfPlayerDone() const -> bool {
  auto& p = psmfPlayer;
  PsmfHeader header;
  if(!psmfPlayerHeader(header)) return true;
  return p.nextPack >= header.streamSize / PacketSize && delimiter(p.video, 0) < 0;
}

//The video's next access unit, from one access unit delimiter to the next (the last to the stream's end), read in
//as it's needed, and its time: that of the PES packet it starts in (the last time stamp before it), else the last
//unit's plus a picture's. False when there's none left.
auto Kernel::psmfPlayerUnit(std::vector<u8>& unit, u64& time) -> bool {
  auto& p = psmfPlayer;
  while(true) {
    s64 start = delimiter(p.video, 0), end = start < 0 ? -1 : delimiter(p.video, start + 5);
    if(start < 0 && p.video.size() > 4) {  //no delimiter: what's before the last bytes can't begin a unit
      u32 drop = p.video.size() - 4;
      dropTaken(p.video, p.videoStamps, drop, drop);
    }
    bool all = false;
    if(end < 0 && !psmfPlayerRead()) {
      if(start < 0) return false;
      end = p.video.size(), all = true;
    }
    if(end < 0 && !all) continue;
    unit.assign(p.video.begin() + start, p.video.begin() + end);
    u64 stamp = NoTime;
    for(auto [at, value] : p.videoStamps) if(at <= start) stamp = value;
    PsmfHeader header;
    psmfPlayerHeader(header);
    time = stamp != NoTime ? stamp : p.videoTime != NoTime ? p.videoTime + PictureTicks : header.startTime;
    p.videoTime = time;
    dropTaken(p.video, p.videoStamps, start, end);
    return true;
  }
}

//Where the sound's next frame is (an 8-byte header, 0x0fd0 and the codec's parameters, then the frame), how long it
//is with its header, and its time (as an access unit's), read in as it's needed (a few packs a call: the video read
//with it waits for its pictures). False when there's no whole frame to be had.
auto Kernel::psmfPlayerFrame(u32& from, u32& bytes, u64& time) -> bool {
  auto& p = psmfPlayer;
  for(u32 reads = 0;; reads++) {
    if(soundFrame(p.audio, 0, from, bytes)) {
      u64 stamp = NoTime;
      for(auto [at, value] : p.audioStamps) if(at <= from) stamp = value;
      PsmfHeader header;
      psmfPlayerHeader(header);
      time = stamp != NoTime ? stamp : p.audioTime != NoTime ? p.audioTime + AudioFrameTicks : header.startTime;
      return true;
    }
    if(reads >= SoundReadAhead || p.video.size() > StreamLimit || !psmfPlayerRead()) return false;
  }
}

//The next access unit decoded, its picture (if one comes) joining those waiting. A decoder made afresh (after a
//state was loaded) passes over pictures until one it can start from (mpeg.cpp's keyframe()). False with no unit left.
auto Kernel::psmfPlayerDecode() -> bool {
  auto& p = psmfPlayer;
  std::vector<u8> unit;
  u64 time = 0;
  if(!psmfPlayerUnit(unit, time)) return false;
  if(p.keyframe) {
    if(!keyframe(unit)) return true;
    p.keyframe = false;
  }
  if(!p.decoder && videoDecoders) p.decoder = videoDecoders();
  if(!p.decoder || !p.decoder->decode(unit.data(), unit.size())) return true;
  auto& picture = p.decoder->picture();
  if(!picture.width || !picture.height || picture.width > VideoDecoder::MaxSide
  || picture.height > VideoDecoder::MaxSide) return true;
  PsmfPicture decoded;
  packPicture(picture, decoded.planes);
  decoded.width = picture.width, decoded.height = picture.height, decoded.time = time;
  p.pictures.push_back(std::move(decoded));
  return true;
}

//Whether the start-up is over: three pictures decoded (or the video all decoded), and with sound, three frames of it
//to be had (or no more).
auto Kernel::psmfPlayerReady() -> bool {
  auto& p = psmfPlayer;
  if(p.pictures.size() < Ahead && !psmfPlayerDone()) return false;
  if(p.audioID < 0) return true;
  while(true) {
    u32 frames = 0, at = 0, from = 0, bytes = 0;
    while(frames < Ahead && soundFrame(p.audio, at, from, bytes)) frames++, at = from + bytes;
    if(frames >= Ahead || p.video.size() > StreamLimit || !psmfPlayerRead()) return true;
  }
}

//Playing (re)starts from a time (absolute ticks): from the program stream's start, or, in a full movie, from the EP
//entry of the video stream played at or before it; what was read and decoded goes, the decoders start afresh, and the
//start-up comes again. The picture shown last stays, to be shown till the next.
auto Kernel::psmfPlayerRestart(u64 time) -> void {
  auto& p = psmfPlayer;
  p.nextPack = 0, p.entry = 0;
  PsmfHeader header;
  if(psmfPlayerHeader(header)) {
    for(auto& stream : header.streams) {
      if(stream.kind != PsmfAvc || stream.id != p.videoID || !stream.epCount) continue;
      if(u64(stream.epOffset) + u64(stream.epCount) * 10 > p.header.size()) break;
      for(u32 n = 0; n < stream.epCount; n++) {
        auto entry = psmfEntry(p.header.data() + stream.epOffset + n * 10);
        if(entry.time <= time || !n) p.nextPack = entry.pack, p.entry = n;
      }
      break;
    }
  }
  p.video.clear(), p.audio.clear();
  p.videoStamps.clear(), p.audioStamps.clear();
  p.videoTime = p.audioTime = NoTime;
  p.pictures.clear();
  p.calls = 0, p.started = false, p.due = 0, p.from = time, p.silence = true, p.ending = false;
  if(p.decoder) p.decoder->reset();
  if(p.sound) p.sound->reset();
}

//The next picture handed out, if one's due (scePsmfPlayerGetVideoData, once the start-up is over): pictures before
//where playing started passed over; with sound, held while too far ahead of it, or passed over two at a time while
//too far behind (above). Then the next is due after the play mode's Updates: two playing, six in slow motion, none
//in pause and step frame.
auto Kernel::psmfPlayerAdvance() -> void {
  auto& p = psmfPlayer;
  if(p.due) return;
  auto next = [&]() -> PsmfPicture* {  //the next picture, decoded as it's needed
    while(p.pictures.empty() && psmfPlayerDecode()) {}
    return p.pictures.empty() ? nullptr : &p.pictures.front();
  };
  while(next() && next()->time < p.from && (p.pictures.size() > 1 || !psmfPlayerDone())) p.pictures.pop_front();
  if(!next()) return;
  u32 from = 0, bytes = 0;
  u64 clock = 0;
  if(p.audioID >= 0 && p.mode == ModePlay && psmfPlayerFrame(from, bytes, clock)) {
    if(next()->time > clock + 2 * PictureTicks) return;
    if(clock > next()->time + 4 * PictureTicks) {
      for(u32 n = 0; n < 2 && next() && (p.pictures.size() > 1 || !psmfPlayerDone()); n++) p.pictures.pop_front();
      if(!next()) return;
    }
  }
  p.shown = std::move(p.pictures.front());
  p.pictures.pop_front();
  p.due = p.mode == ModePlay ? 2 : p.mode == ModeSlow ? 6 : Never;
}

//The movie has ended: looping, it plays again from the start; else its status becomes 0x200, now if the player's
//threads are of a better priority than the calling thread (or nothing called: a test), else once they get the CPU.
auto Kernel::psmfPlayerEnd() -> void {
  auto& p = psmfPlayer;
  PsmfHeader header;
  psmfPlayerHeader(header);
  if(p.looping) {
    p.mode = ModePlay, p.speed = 1;
    return psmfPlayerRestart(header.startTime);
  }
  if(!current || interrupting || s32(p.priority) < s32(current->priority)) return psmfPlayerEnded();
  p.ending = true;
  p.endingAt = vblanks;
}

auto Kernel::psmfPlayerEnded() -> void {
  auto& p = psmfPlayer;
  if(p.status == StatusPlaying) p.status = StatusEnded;
  p.ending = false;
}

//An Update in fast forward or rewind: every 15, 20 or 25 Updates (speeds 1 to 3) the picture of the EP entry the
//speed's number on (back) is decoded and shown. Rewound to the first entry, playing goes on from the start; forward
//past the last, the movie has ended.
auto Kernel::psmfPlayerStep() -> void {
  auto& p = psmfPlayer;
  if(p.due && p.due != Never) p.due--;
  if(p.due) return;
  PsmfHeader header;
  if(!psmfPlayerHeader(header)) return;
  const PsmfStream* stream = nullptr;
  for(auto& s : header.streams) if(s.kind == PsmfAvc && s.id == p.videoID) { stream = &s; break; }
  if(!stream || !stream->epCount || u64(stream->epOffset) + u64(stream->epCount) * 10 > p.header.size()) return;
  s32 speed = std::clamp(p.speed, 1, 3);  //(scePsmfPlayerStart keeps any speed as given)
  s64 next = s64(p.entry) + (p.mode == ModeForward ? speed : -speed);
  if(next >= s64(stream->epCount)) return psmfPlayerEnd();
  if(next <= 0) {
    p.mode = ModePlay, p.speed = 1;
    return psmfPlayerRestart(header.startTime);
  }
  auto entry = psmfEntry(p.header.data() + stream->epOffset + next * 10);
  psmfPlayerRestart(entry.time);
  p.started = true;
  p.entry = next;
  if(psmfPlayerDecode() && !p.pictures.empty()) {
    p.shown = std::move(p.pictures.front());
    p.pictures.clear();
  }
  p.due = 15 + 5 * (speed - 1);
}

//How many streams of a kind (PsmfAvc, or PsmfAnyAudio for sound) the movie has whose data is there: the header
//lists them and the program stream's first 64 packs carry them. (test_streams.pmf's header lists two of each, its
//packs misplaced so that none is found: the PSP refused to switch on it, as it does with one stream.)
auto Kernel::psmfPlayerStreams(u32 kind) -> u32 {
  PsmfHeader header;
  if(!psmfPlayerHeader(header)) return 0;
  std::vector<bool> seen(header.streams.size());
  for(u32 pack = 0; pack < 64 && pack < header.streamSize / PacketSize; pack++) {
    u8 packet[PacketSize];
    if(!psmfPlayerPack(pack, packet)) break;
    pesPackets(packet, [&](u8 id, u32 payload, u32 end, u64, u64) {
      for(u32 n = 0; n < header.streams.size(); n++) {
        auto& stream = header.streams[n];
        if(id == stream.id && (id != 0xbd || (payload < end && packet[payload] == stream.privateID))) seen[n] = true;
      }
    });
  }
  u32 count = 0;
  for(u32 n = 0; n < header.streams.size(); n++) {
    auto& stream = header.streams[n];
    bool ofKind = kind == PsmfAvc ? stream.kind == PsmfAvc : stream.kind == PsmfAtrac || stream.kind == PsmfPcm;
    if(ofKind && seen[n]) count++;
  }
  return count;
}

//Switches to the number-th stream of a kind (PsmfAvc, or PsmfAnyAudio): taken from the next packs on, what was read
//of the stream before going; new pictures wait for one the decoder can start from, the picture shown staying.
auto Kernel::psmfPlayerSelect(u32 kind, u32 number) -> void {
  auto& p = psmfPlayer;
  PsmfHeader header;
  psmfPlayerHeader(header);
  u32 seen = 0;
  for(auto& stream : header.streams) {
    bool ofKind = kind == PsmfAvc ? stream.kind == PsmfAvc : stream.kind == PsmfAtrac || stream.kind == PsmfPcm;
    if(!ofKind || seen++ != number) continue;
    if(kind == PsmfAvc) {
      p.videoID = stream.id, p.videoStream = number;
      p.video.clear(), p.videoStamps.clear(), p.pictures.clear();
      if(p.decoder) p.decoder->reset();
      p.keyframe = true;
    } else {
      p.audioID = stream.kind == PsmfAtrac ? stream.privateID : -1, p.audioStream = number;
      p.audio.clear(), p.audioStamps.clear();
      if(p.sound) p.sound->reset();
      p.silence = true;
    }
    return;
  }
}

//(handle's address, parameters): a player made: its parameters (12 bytes) a working buffer and its size (0x285800
//bytes at least, compared unsigned: create recorded 0x80000000 and 0xffffffff accepted, so it isn't checked against
//memory; the player keeps nothing in it here) and its threads' priority (16-109, signed). Refused, the handle's word
//is 0: a priority out of range 0x80616008, a buffer too small 0x80616005 (in that order: chosen, create tried one at
//a time), a player already there 0x80618005 (that player unharmed). Status 1, play mode 0 at speed 1, 8888 pictures,
//not looping.
auto Kernel::scePsmfPlayerCreate() -> void {
  u32 handle = arg(0), parameters = arg(1);
  if(!memory.reaches(handle, 4) || !memory.reaches(parameters, 12)) return result(ErrorInvalidPointer);
  s32 priority = memory.read(4, parameters + 8);
  u32 size = memory.read(4, parameters + 4);
  auto refuse = [&](u32 error) { memory.write(4, handle, 0); result(error); };
  if(priority < 16 || priority > 109) return refuse(PlayerErrorBadValue);
  if(size < PlayerBufferLeast) return refuse(PlayerErrorBufferSize);
  if(psmfPlayer.status) return refuse(PlayerErrorInUse);
  psmfPlayer = {};
  psmfPlayer.status = StatusMade;
  psmfPlayer.priority = priority;
  memory.write(4, PlayerBlock, 0);
  memory.write(4, PlayerHandle, PlayerBlock);
  memory.write(4, handle, PlayerHandle);
  psmfPlayerWait(0, u64(CreateMicroseconds) * (CPUFrequency / 1'000'000));
}

//(handle): the player deleted, in any status: its movie closed, the handle's word (the one given: a copy's
//original keeps the old value) 0.
auto Kernel::scePsmfPlayerDelete() -> void {
  if(!psmfPlayerFor(arg(0))) return result(PlayerErrorNoPlayer);
  psmfPlayerClose();
  psmfPlayer = {};
  memory.write(4, arg(0), 0);
  psmfPlayerWait(0, u64(DeleteMicroseconds) * (CPUFrequency / 1'000'000));
}

//(handle, buffer, size): a buffer for reading the file through, in status 1 only: 64 KiB at least (compared
//unsigned); its address isn't checked (NULL and 0xdeadbeef were accepted). Kept, not needed.
auto Kernel::scePsmfPlayerSetTempBuf() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status != StatusMade) return result(PlayerErrorNoPlayer);
  if(arg(2) < TempBufferLeast) return result(PlayerErrorBadValue);
  player->tempBuffer = arg(1), player->tempSize = arg(2);
  result(0);
}

//What the four SetPsmf functions share (handle, path[, offset]): a movie set, in status 1 only: its file opened at
//the offset and its header read (psmfPlayerOpen()); status 2. A NULL path: 0x80616008, at once. Any failure
//(setpsmf and setpsmfoffset: no such file, a folder, a file that isn't a movie, offsets -1, 0x80000000 and 2048
//where no movie begins): 0x800200d2, the status still 1. Both block, while the header's read (the device's time),
//even when the file isn't there; the CB functions let the caller's callbacks run meanwhile.
auto Kernel::psmfPlayerSetPsmf(bool offset, bool callbacks) -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status != StatusMade) return result(PlayerErrorNoPlayer);
  if(!arg(1)) return result(PlayerErrorBadValue);
  std::string path = memory.reaches(arg(1), 1) ? memory.readString(arg(1), 1024) : std::string{};
  u64 read = path.empty() ? 0 : psmfPlayerOpen(path, offset ? u64(arg(2)) : 0);
  if(read) {
    player->status = StatusStandby;
    player->videoCodec = player->videoStream = player->audioCodec = player->audioStream = -1;
  }
  bool onDisc = read && files.count(player->file) && files[player->file].onDisc;
  psmfPlayerWait(read ? 0 : PlayerErrorFile, asyncDuration(onDisc, read), callbacks);
}

auto Kernel::scePsmfPlayerSetPsmf() -> void { psmfPlayerSetPsmf(false, false); }
auto Kernel::scePsmfPlayerSetPsmfCB() -> void { psmfPlayerSetPsmf(false, true); }
auto Kernel::scePsmfPlayerSetPsmfOffset() -> void { psmfPlayerSetPsmf(true, false); }
auto Kernel::scePsmfPlayerSetPsmfOffsetCB() -> void { psmfPlayerSetPsmf(true, true); }

//(handle): the movie let go (status 2, 4 or 0x200), its file closed: status 1. Blocks.
auto Kernel::scePsmfPlayerReleasePsmf() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status < StatusStandby) return result(PlayerErrorNoPlayer);
  psmfPlayerClose();
  player->status = StatusMade;
  psmfPlayerWait(0, u64(ReleaseMicroseconds) * (CPUFrequency / 1'000'000));
}

//(handle, where to put 20 bytes): the movie described (status 2, 4 or 0x200): its last picture's time, relative to
//the start (the end less the start less a picture: getpsmfinfo's 567567), how many video, ATRAC3plus and PCM
//streams the header lists, and the player's version for it: 0 if every video stream has an EP map, else 1 (a
//"basic" movie, which can't be started part way, fast forwarded or rewound).
auto Kernel::scePsmfPlayerGetPsmfInfo() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status < StatusStandby) return result(PlayerErrorNoPlayer);
  PsmfHeader header;
  psmfPlayerHeader(header);
  u32 video = 0, atrac = 0, pcm = 0, mapped = 0;
  for(auto& stream : header.streams) {
    video += stream.kind == PsmfAvc, atrac += stream.kind == PsmfAtrac, pcm += stream.kind == PsmfPcm;
    mapped += stream.kind == PsmfAvc && stream.epCount;
  }
  u32 words[5] = {u32(header.endTime - header.startTime - PictureTicks), video, atrac, pcm,
                  video && mapped == video ? 0u : 1u};
  if(memory.reaches(arg(1), 20)) for(u32 n = 0; n < 5; n++) memory.write(4, arg(1) + n * 4, words[n]);
  result(0);
}

//(handle, key, value), in any status: key 0 looping (value 0 loops, 1 doesn't); key 1 the pictures' pixel format
//(0-3: 5650, 5551, 4444, 8888; -1 leaves it as it is), for the pictures handed out from the next call on. Another
//value 0x80616008, another key 0x80616006, as configplayer recorded.
auto Kernel::scePsmfPlayerConfigPlayer() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player) return result(PlayerErrorNoPlayer);
  s32 key = arg(1), value = arg(2);
  if(key == 0) {
    if(value != 0 && value != 1) return result(PlayerErrorBadValue);
    player->looping = value == 0;
    return result(0);
  }
  if(key == 1) {
    if(value < -1 || value > 3) return result(PlayerErrorBadValue);
    if(value >= 0) player->pixelFormat = value;
    return result(0);
  }
  result(PlayerErrorBadKey);
}

//(handle, parameters, the time to start at, relative to the presentation's start): playing starts (from status 2,
//or again from 4 or 0x200), as start recorded. The parameters (24 bytes): the video codec (0 or 14, H.264, else
//0x80616003) and the video stream's number among the video streams (else 0x80616006); the sound's codec (1
//ATRAC3plus, or 15 either kind of sound, else 0x80616003) and its stream's number among the streams of the codec's
//kinds (one past them 0x80616006; a negative one plays without sound: chosen, from the specification's account),
//both looked at only when the movie has sound; the play mode (0-3 for a movie without an EP map, 0-5 with them, else
//0x80616008) and speed (kept as given). A movie without EP maps starts only at 0; one with them at any time before
//its end (else 0x80616008), from the EP entry at or before it, the pictures before the time decoded but not handed
//out. (The order of the checks, which start tried one at a time: the status, a basic movie's mode and time, the
//video, the sound, a full movie's mode and time.) Status 4 at once; nothing is handed out until the start-up is over.
auto Kernel::scePsmfPlayerStart() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status < StatusStandby) return result(PlayerErrorNoPlayer);
  if(!memory.reaches(arg(1), 24)) return result(ErrorInvalidPointer);
  s32 values[6];
  for(u32 n = 0; n < 6; n++) values[n] = memory.read(4, arg(1) + n * 4);
  auto [videoCodec, videoNumber, audioCodec, audioNumber, mode, speed] = values;
  s32 time = arg(2);
  PsmfHeader header;
  psmfPlayerHeader(header);
  s32 videos = 0, mapped = 0, atrac = 0, sounds = 0;
  for(auto& stream : header.streams) {
    videos += stream.kind == PsmfAvc, mapped += stream.kind == PsmfAvc && stream.epCount;
    atrac += stream.kind == PsmfAtrac, sounds += stream.kind == PsmfAtrac || stream.kind == PsmfPcm;
  }
  bool full = videos && mapped == videos;
  if(!full && (u32(mode) > 3 || time)) return result(PlayerErrorBadValue);
  if(videoCodec != 0 && videoCodec != 14) return result(PlayerErrorNotSupported);
  if(videoNumber < 0 || videoNumber >= videos) return result(PlayerErrorBadKey);
  if(sounds) {
    if(audioCodec != 1 && audioCodec != 15) return result(PlayerErrorNotSupported);
    if(audioNumber >= (audioCodec == 1 ? atrac : sounds)) return result(PlayerErrorBadKey);
  }
  if(full && (u32(mode) > 5 || time < 0 || u64(time) >= header.endTime - header.startTime)) {
    return result(PlayerErrorBadValue);
  }
  player->status = StatusPlaying;
  player->mode = mode, player->speed = speed;
  player->videoCodec = 0, player->videoStream = videoNumber;
  player->audioID = -1;
  s32 seen = 0;
  for(auto& stream : header.streams) if(stream.kind == PsmfAvc && seen++ == videoNumber) player->videoID = stream.id;
  if(sounds && audioNumber >= 0) {
    player->audioCodec = 1, player->audioStream = audioNumber;
    seen = 0;
    for(auto& stream : header.streams) {
      bool ofKind = stream.kind == PsmfAtrac || (audioCodec == 15 && stream.kind == PsmfPcm);
      if(ofKind && seen++ == audioNumber && stream.kind == PsmfAtrac) player->audioID = stream.privateID;
    }
  } else {
    player->audioCodec = player->audioStream = -1;
  }
  psmfPlayerRestart(header.startTime + time);
  if(mode == ModeForward || mode == ModeRewind) {  //a full movie started in fast forward or rewind
    player->started = true;
    player->due = 0;
  }
  result(0);
}

//(handle): playing stops (status 4 or 0x200): status 2, the movie still set. Doesn't block (stop: the status read 2
//straight after).
auto Kernel::scePsmfPlayerStop() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || (player->status != StatusPlaying && player->status != StatusEnded)) {
    return result(PlayerErrorNoPlayer);
  }
  psmfPlayerRestart(0);
  player->status = StatusStandby;
  result(0);
}

//(handle): the pacing tick, meant to come once a vertical blank (status 4 or 0x200; doesn't block): it counts down to
//the next picture, and notices the movie's end (above).
auto Kernel::scePsmfPlayerUpdate() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || (player->status != StatusPlaying && player->status != StatusEnded)) {
    return result(PlayerErrorNoPlayer);
  }
  result(0);
  if(player->status != StatusPlaying || player->ending) return;
  if(player->mode == ModeForward || player->mode == ModeRewind) return psmfPlayerStep();
  if(!player->started) return;
  if(player->due && player->due != Never) player->due--;
  if(player->due || !player->pictures.empty() || !psmfPlayerDone()) return;
  if(player->audioID >= 0) {
    u32 from = 0, bytes = 0;
    u64 time = 0;
    if(psmfPlayerFrame(from, bytes, time)) return;  //sound still to be taken
  }
  psmfPlayerEnd();
}

//(handle, request: the buffer's width in pixels, where it is, where the picture's time goes): the current picture
//(status 4 or 0x200), as getvideodata recorded: before the first picture 0x8061600c, nothing written (not even for a
//NULL buffer); once there's one, a NULL buffer 0x80000103, a negative width 0x80000023, one narrower than the picture
//0x800001fe. Then the next picture is handed out if it's due (above), and the current one written into the buffer
//with its time (relative to the presentation's start) after the buffer's address; the caller waits a moment.
auto Kernel::scePsmfPlayerGetVideoData() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || (player->status != StatusPlaying && player->status != StatusEnded)) {
    return result(PlayerErrorNoPlayer);
  }
  auto& p = *player;
  bool stepping = p.mode == ModeForward || p.mode == ModeRewind;
  if(p.status == StatusPlaying && !stepping && !p.started) {
    p.calls++;
    if(p.pictures.size() < Ahead) psmfPlayerDecode();
    if(psmfPlayerReady()) {
      p.started = true;
      p.due = 0;
      //sound more than a frame before where playing starts, or two pictures before the first picture, goes
      u64 first = p.pictures.empty() ? p.from : p.pictures.front().time;
      u32 from = 0, bytes = 0;
      u64 time = 0;
      while(p.audioID >= 0 && psmfPlayerFrame(from, bytes, time)
         && (time + AudioFrameTicks < p.from || time + 2 * PictureTicks < first)) {
        p.audioTime = time;
        dropTaken(p.audio, p.audioStamps, from, from + bytes);
      }
    }
  } else if(p.status == StatusPlaying && !stepping && p.pictures.size() < Ahead) {
    psmfPlayerDecode();
  }
  u32 request = arg(1);
  if(!memory.reaches(request, 12)) return result(ErrorInvalidPointer);
  bool comes = p.status == StatusPlaying && p.started && !stepping && !p.due && !p.pictures.empty();
  if(p.shown.planes.empty() && !comes) return result(PlayerErrorNoData);
  auto& sized = p.shown.planes.empty() ? p.pictures.front() : p.shown;
  s32 width = memory.read(4, request);
  u32 destination = memory.read(4, request + 4);
  if(!destination) return result(ErrorInvalidPointer);
  if(width < 0) return result(PlayerErrorNegativeWidth);
  width = width ? width & ~1 : 512;
  if(u32(width) < sized.width) return result(PlayerErrorNarrowWidth);
  if(p.status == StatusPlaying && p.started && !stepping) psmfPlayerAdvance();
  if(p.shown.planes.empty()) return result(PlayerErrorNoData);
  pictureConvert(p.shown.planes, p.shown.width, p.shown.height, p.pixelFormat, false, destination, width, 0, 0, 0, 0);
  PsmfHeader header;
  psmfPlayerHeader(header);
  memory.write(4, request + 8, u32(p.shown.time - header.startTime));
  psmfPlayerWait(0, u64(PictureMicroseconds) * (CPUFrequency / 1'000'000));
}

//(handle, where to put 8192 bytes): the sound's next frame, 2048 stereo 16-bit samples (status 4 or 0x200; doesn't
//block). Nothing (0x8061600c) without sound, before the start-up is over, after the end, in the other play modes
//(where frames more than one behind the next picture go as the calls come), and with no frame to be had.
auto Kernel::scePsmfPlayerGetAudioData() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || (player->status != StatusPlaying && player->status != StatusEnded)) {
    return result(PlayerErrorNoPlayer);
  }
  auto& p = *player;
  if(p.status != StatusPlaying || p.audioID < 0 || !p.started) return result(PlayerErrorNoData);
  u32 from = 0, bytes = 0;
  u64 time = 0;
  if(p.mode != ModePlay) {
    u64 picture = !p.pictures.empty() ? p.pictures.front().time : p.shown.time;
    while(psmfPlayerFrame(from, bytes, time) && time + AudioFrameTicks < picture) {
      p.audioTime = time;
      dropTaken(p.audio, p.audioStamps, from, from + bytes);
    }
    return result(PlayerErrorNoData);
  }
  if(!psmfPlayerFrame(from, bytes, time)) return result(PlayerErrorNoData);
  std::vector<u8> unit(p.audio.begin() + from, p.audio.begin() + from + bytes);
  p.audioTime = time;
  dropTaken(p.audio, p.audioStamps, from, from + bytes);
  std::vector<s16> out(2048 * 2);
  atracUnitDecode(p.sound, p.soundLast, unit, out);
  if(p.silence) std::fill(out.begin(), out.end(), 0), p.silence = false;
  if(memory.reaches(arg(1), SoundFrameBytes)) memory.copyIn(arg(1), out.data(), SoundFrameBytes);
  result(0);
}

//(handle): the bytes of sound a frame GetAudioData writes, 8192, in any status.
auto Kernel::scePsmfPlayerGetAudioOutSize() -> void {
  if(!psmfPlayerFor(arg(0))) return result(PlayerErrorNoPlayer);
  result(SoundFrameBytes);
}

//(handle): the status.
auto Kernel::scePsmfPlayerGetCurrentStatus() -> void {
  auto player = psmfPlayerFor(arg(0));
  result(player ? player->status : PlayerErrorNoPlayer);
}

//(handle, where to put it): the current picture's time, relative to the start, as one word (the game's 64-bit
//variable's high half left as it was, as getcurrentpts recorded); in status 2, or before the first picture,
//0x8061600c.
auto Kernel::scePsmfPlayerGetCurrentPts() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status < StatusStandby) return result(PlayerErrorNoPlayer);
  if(player->status == StatusStandby || player->shown.planes.empty()) return result(PlayerErrorNoData);
  PsmfHeader header;
  psmfPlayerHeader(header);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), u32(player->shown.time - header.startTime));
  result(0);
}

//(handle, where to put the play mode, and its speed), in any status (0 and 1 before any start).
auto Kernel::scePsmfPlayerGetCurrentPlayMode() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player) return result(PlayerErrorNoPlayer);
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), player->mode);
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), player->speed);
  result(0);
}

//(handle, mode, speed): the play mode changed (status 4 or 0x200), as playmode recorded: 0-3 at speed 1 whatever is
//asked; 4 and 5 (fast forward, rewind) only in a full movie (else 0x80616003), at speed 1-3 (else 0x80616008:
//chosen, the specification's account); any other 0x80616006. The mode already in force changes nothing, but step
//frame steps again. Into and out of fast forward and rewind, playing starts again from the current picture.
auto Kernel::scePsmfPlayerChangePlayMode() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || (player->status != StatusPlaying && player->status != StatusEnded)) {
    return result(PlayerErrorNoPlayer);
  }
  auto& p = *player;
  s32 mode = arg(1), speed = arg(2);
  PsmfHeader header;
  psmfPlayerHeader(header);
  bool stepping = p.mode == ModeForward || p.mode == ModeRewind;
  if(mode >= ModePlay && mode <= ModePause) {
    p.speed = 1;
    if(mode == p.mode && mode != ModeStep) return result(0);
    if(mode == ModeStep && p.mode == ModeStep && p.due == Never) p.due = 2;  //a step: the next picture comes
    p.mode = mode;
    if(stepping) {
      u64 time = p.shown.planes.empty() ? header.startTime : p.shown.time;
      psmfPlayerRestart(time);
    } else if(p.due == Never && mode != ModePause) {
      p.due = mode == ModeSlow ? 6 : 2;
    }
    return result(0);
  }
  if(mode != ModeForward && mode != ModeRewind) return result(PlayerErrorBadKey);
  u32 videos = 0, mapped = 0;
  for(auto& stream : header.streams) {
    videos += stream.kind == PsmfAvc, mapped += stream.kind == PsmfAvc && stream.epCount;
  }
  if(!videos || mapped != videos) return result(PlayerErrorNotSupported);
  if(speed < 1 || speed > 3) return result(PlayerErrorBadValue);
  if(mode == p.mode && speed == p.speed) return result(0);
  if(!stepping) {  //from the EP entry at or before the current picture
    u64 time = p.shown.planes.empty() ? header.startTime : p.shown.time;
    psmfPlayerRestart(time);
  }
  p.mode = mode, p.speed = speed;
  p.started = true;
  p.due = 15 + 5 * (speed - 1);
  result(0);
}

//(handle, where to put the codec, and the stream's number): the video or sound stream played (status 2: none yet,
//-1 and -1; after scePsmfPlayerStart, video codec 0 and its number, sound codec 1 and its number or -1 and -1).
auto Kernel::scePsmfPlayerGetCurrentVideoStream() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status < StatusStandby) return result(PlayerErrorNoPlayer);
  bool playing = player->status != StatusStandby;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), playing ? player->videoCodec : -1);
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), playing ? player->videoStream : -1);
  result(0);
}

auto Kernel::scePsmfPlayerGetCurrentAudioStream() -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status < StatusStandby) return result(PlayerErrorNoPlayer);
  bool playing = player->status != StatusStandby;
  if(memory.reaches(arg(1), 4)) memory.write(4, arg(1), playing ? player->audioCodec : -1);
  if(memory.reaches(arg(2), 4)) memory.write(4, arg(2), playing ? player->audioStream : -1);
  result(0);
}

//What SelectVideo and SelectAudio share (handle): the next stream of the kind played (wrapping round to the first),
//while playing (status 4 only); with fewer than two streams of the kind whose data is there, 0x80616003.
auto Kernel::psmfPlayerSelectNext(u32 kind) -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status != StatusPlaying) return result(PlayerErrorNoPlayer);
  u32 count = psmfPlayerStreams(kind);
  if(count < 2) return result(PlayerErrorNotSupported);
  s32 current = kind == PsmfAvc ? player->videoStream : player->audioStream;
  psmfPlayerSelect(kind, u32(current + 1) % count);
  result(0);
}

auto Kernel::scePsmfPlayerSelectVideo() -> void { psmfPlayerSelectNext(PsmfAvc); }
auto Kernel::scePsmfPlayerSelectAudio() -> void { psmfPlayerSelectNext(PsmfAnyAudio); }

//What SelectSpecificVideo and SelectSpecificAudio share (handle, codec, number): that stream of the kind played
//(status 4 only): fewer than two streams of the kind whose data is there 0x80616003, whatever the rest
//(selectspecific); a codec not the kind's (0 or 14 for video, 1 or 15 for sound) 0x80616003, a number out of range
//0x80616006 (the specification's account, from a movie of several streams no PSP recording has).
auto Kernel::psmfPlayerSelectSpecific(u32 kind) -> void {
  auto player = psmfPlayerFor(arg(0));
  if(!player || player->status != StatusPlaying) return result(PlayerErrorNoPlayer);
  u32 count = psmfPlayerStreams(kind);
  if(count < 2) return result(PlayerErrorNotSupported);
  u32 codec = arg(1);
  if(kind == PsmfAvc ? codec != 0 && codec != 14 : codec != 1 && codec != 15) {
    return result(PlayerErrorNotSupported);
  }
  if(arg(2) >= count) return result(PlayerErrorBadKey);
  psmfPlayerSelect(kind, arg(2));
  result(0);
}

auto Kernel::scePsmfPlayerSelectSpecificVideo() -> void { psmfPlayerSelectSpecific(PsmfAvc); }
auto Kernel::scePsmfPlayerSelectSpecificAudio() -> void { psmfPlayerSelectSpecific(PsmfAnyAudio); }

//(handle): in any status, nothing changes (break: the pictures' times went on; it interrupts a network stream's
//read, which no movie here is).
auto Kernel::scePsmfPlayerBreak() -> void {
  result(psmfPlayerFor(arg(0)) ? 0 : PlayerErrorNoPlayer);
}

//scePsmfPlayer's NID 0x340c12cb, whose name, arguments and behaviour aren't known (one of the specification's
//accounts lists it bare): it returns 0 (chosen), doing nothing.
auto Kernel::scePsmfPlayerUnknown() -> void {
  result(0);
}
